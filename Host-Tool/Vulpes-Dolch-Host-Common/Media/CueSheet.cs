using System;
using System.Collections.Generic;
using System.Globalization;
using System.IO;

namespace VulpesDolch.Host.Common.Media
{
    /// <summary>One TRACK of a cue sheet. Positions are frames (sectors) from the start of the owning FILE.</summary>
    internal sealed class CueTrack
    {
        internal CueTrack(int Number, CdTrackType Type, int StoredSectorSize)
        {
            this.Number = Number;
            this.Type = Type;
            this.StoredSectorSize = StoredSectorSize;
        }

        internal int Number { get; }

        internal CdTrackType Type { get; }

        internal int StoredSectorSize { get; }

        /// <summary>INDEX 00, or -1 if the track has none.</summary>
        internal Int64 Index00Frames { get; set; } = -1;

        /// <summary>INDEX 01, or -1 until it was seen.</summary>
        internal Int64 Index01Frames { get; set; } = -1;

        /// <summary>PREGAP: silence that is part of the disc but not stored in any file.</summary>
        internal Int64 PregapFrames { get; set; } = 0;

        /// <summary>POSTGAP: silence after the track that is not stored in any file.</summary>
        internal Int64 PostgapFrames { get; set; } = 0;
    }

    /// <summary>One FILE of a cue sheet and the tracks stored in it.</summary>
    internal sealed class CueFile
    {
        internal CueFile(string ResolvedPath)
        {
            this.ResolvedPath = ResolvedPath;
        }

        internal string ResolvedPath { get; }

        /// <summary>A WAVE file (16 bit, 44.1 kHz, stereo PCM, audio tracks only) instead of a BINARY file of raw sectors.</summary>
        internal bool IsWave { get; set; }

        internal List<CueTrack> Tracks { get; } = new List<CueTrack>();
    }

    /// <summary>
    /// A parsed .cue sheet. Understands FILE (BINARY and WAVE), TRACK, INDEX, PREGAP and POSTGAP; descriptive
    /// commands (TITLE, PERFORMER, FLAGS, ISRC, CATALOG, REM, ...) are accepted and ignored.
    /// </summary>
    internal sealed class CueSheet
    {
        internal List<CueFile> Files { get; } = new List<CueFile>();

        internal static CueSheet Parse(string CuePath)
        {
            string[] Lines;
            try
            {
                Lines = File.ReadAllLines(CuePath);
            }
            catch (IOException Problem)
            {
                throw new CdImageException("Cannot read '" + CuePath + "': " + Problem.Message);
            }
            catch (UnauthorizedAccessException Problem)
            {
                throw new CdImageException("Cannot read '" + CuePath + "': " + Problem.Message);
            }

            string CueDirectory = Path.GetDirectoryName(Path.GetFullPath(CuePath));
            CueSheet Sheet = new CueSheet();
            CueFile CurrentFile = null;
            CueTrack CurrentTrack = null;
            int LastTrackNumber = 0;

            for (int LineIndex = 0; LineIndex < Lines.Length; LineIndex++)
            {
                int LineNumber = LineIndex + 1;
                List<string> Tokens = Tokenize(Lines[LineIndex]);
                if (Tokens.Count == 0)
                    continue;

                string Command = Tokens[0].ToUpperInvariant();

                if (Command == "FILE")
                {
                    if (Tokens.Count < 3)
                        throw Failure(LineNumber, "FILE needs a name and a type.");

                    string FileType = Tokens[Tokens.Count - 1].ToUpperInvariant();
                    if ((FileType != "BINARY") && (FileType != "WAVE"))
                        throw Failure(LineNumber, "FILE type '" + FileType + "' is not supported: only BINARY (raw sector) and WAVE (CD format PCM) files can be used.");

                    string Name = string.Join(" ", Tokens.GetRange(1, Tokens.Count - 2));
                    CurrentFile = new CueFile(ResolveImageFile(CueDirectory, Name, LineNumber));
                    CurrentFile.IsWave = (FileType == "WAVE");
                    CurrentTrack = null;
                    Sheet.Files.Add(CurrentFile);
                }
                else if (Command == "TRACK")
                {
                    if (CurrentFile == null)
                        throw Failure(LineNumber, "TRACK before any FILE.");
                    if (Tokens.Count < 3)
                        throw Failure(LineNumber, "TRACK needs a number and a mode.");

                    int Number;
                    if (int.TryParse(Tokens[1], NumberStyles.None, CultureInfo.InvariantCulture, out Number) == false)
                        throw Failure(LineNumber, "'" + Tokens[1] + "' is not a track number.");
                    if ((Number < 1) || (Number > 99) || (Number <= LastTrackNumber))
                        throw Failure(LineNumber, "Track " + Number + " is out of order: track numbers must ascend from 1 to 99.");

                    CdTrackType Type;
                    int StoredSize;
                    ParseMode(LineNumber, Tokens[2], out Type, out StoredSize);

                    CurrentTrack = new CueTrack(Number, Type, StoredSize);
                    CurrentFile.Tracks.Add(CurrentTrack);
                    LastTrackNumber = Number;
                }
                else if (Command == "INDEX")
                {
                    if (CurrentTrack == null)
                        throw Failure(LineNumber, "INDEX before any TRACK.");
                    if (Tokens.Count < 3)
                        throw Failure(LineNumber, "INDEX needs a number and a time.");

                    int IndexNumber;
                    if (int.TryParse(Tokens[1], NumberStyles.None, CultureInfo.InvariantCulture, out IndexNumber) == false)
                        throw Failure(LineNumber, "'" + Tokens[1] + "' is not an index number.");

                    // Only INDEX 00 (pregap start) and INDEX 01 (track start) matter; sub-indexes are not needed.
                    if (IndexNumber == 0)
                        CurrentTrack.Index00Frames = ParseTime(LineNumber, Tokens[2]);
                    else if (IndexNumber == 1)
                        CurrentTrack.Index01Frames = ParseTime(LineNumber, Tokens[2]);
                }
                else if ((Command == "PREGAP") || (Command == "POSTGAP"))
                {
                    if (CurrentTrack == null)
                        throw Failure(LineNumber, Command + " before any TRACK.");
                    if (Tokens.Count < 2)
                        throw Failure(LineNumber, Command + " needs a time.");

                    Int64 Frames = ParseTime(LineNumber, Tokens[1]);
                    if (Command == "PREGAP")
                        CurrentTrack.PregapFrames = Frames;
                    else
                        CurrentTrack.PostgapFrames = Frames;
                }
            }

            Sheet.Validate(CuePath);
            return (Sheet);
        }

        private void Validate(string CuePath)
        {
            if (Files.Count == 0)
                throw new CdImageException("'" + CuePath + "' contains no FILE entry.");

            foreach (CueFile Entry in Files)
            {
                if (Entry.Tracks.Count == 0)
                    throw new CdImageException("'" + CuePath + "': a FILE entry has no TRACK.");

                foreach (CueTrack Track in Entry.Tracks)
                {
                    if ((Entry.IsWave == true) && (Track.Type != CdTrackType.Audio))
                        throw new CdImageException("'" + CuePath + "': track " + Track.Number + " is in a WAVE file but is not an AUDIO track.");
                    if (Track.Index01Frames < 0)
                        throw new CdImageException("'" + CuePath + "': track " + Track.Number + " has no INDEX 01.");
                    if ((Track.PregapFrames > 0) && (Track.Index00Frames >= 0))
                        throw new CdImageException("'" + CuePath + "': track " + Track.Number + " has both PREGAP and INDEX 00.");
                }
            }
        }

        private static CdImageException Failure(int LineNumber, string Message)
        {
            return (new CdImageException("Cue sheet line " + LineNumber + ": " + Message));
        }

        private static void ParseMode(int LineNumber, string Mode, out CdTrackType Type, out int StoredSize)
        {
            switch (Mode.ToUpperInvariant())
            {
                case "AUDIO":
                    Type = CdTrackType.Audio;
                    StoredSize = CdSector.RawSize;
                    return;
                case "MODE1/2048":
                    Type = CdTrackType.Mode1;
                    StoredSize = CdSector.UserDataSize;
                    return;
                case "MODE1/2352":
                    Type = CdTrackType.Mode1;
                    StoredSize = CdSector.RawSize;
                    return;
                case "MODE2/2048":
                    Type = CdTrackType.Mode2;
                    StoredSize = CdSector.UserDataSize;
                    return;
                case "MODE2/2336":
                    Type = CdTrackType.Mode2;
                    StoredSize = CdSector.Mode2HeaderlessSize;
                    return;
                case "MODE2/2352":
                    Type = CdTrackType.Mode2;
                    StoredSize = CdSector.RawSize;
                    return;
                default:
                    throw Failure(LineNumber, "Track mode '" + Mode + "' is not supported (supported: AUDIO, MODE1/2048, MODE1/2352, MODE2/2048, MODE2/2336, MODE2/2352).");
            }
        }

        /// <summary>Parses MM:SS:FF into a frame count (75 frames per second).</summary>
        private static Int64 ParseTime(int LineNumber, string Text)
        {
            string[] Parts = Text.Split(':');
            if (Parts.Length != 3)
                throw Failure(LineNumber, "'" + Text + "' is not a MM:SS:FF time.");

            int Minutes;
            int Seconds;
            int Frames;
            if (int.TryParse(Parts[0], NumberStyles.None, CultureInfo.InvariantCulture, out Minutes) == false)
                throw Failure(LineNumber, "'" + Text + "' is not a MM:SS:FF time.");
            if (int.TryParse(Parts[1], NumberStyles.None, CultureInfo.InvariantCulture, out Seconds) == false)
                throw Failure(LineNumber, "'" + Text + "' is not a MM:SS:FF time.");
            if (int.TryParse(Parts[2], NumberStyles.None, CultureInfo.InvariantCulture, out Frames) == false)
                throw Failure(LineNumber, "'" + Text + "' is not a MM:SS:FF time.");
            if ((Seconds >= 60) || (Frames >= CdSector.FramesPerSecond))
                throw Failure(LineNumber, "'" + Text + "' is out of range (seconds below 60, frames below 75).");

            return (((Int64)((Minutes * 60) + Seconds) * CdSector.FramesPerSecond) + Frames);
        }

        /// <summary>Splits a line into words, keeping "quoted text" together.</summary>
        private static List<string> Tokenize(string Line)
        {
            List<string> Tokens = new List<string>();
            int Position = 0;

            while (Position < Line.Length)
            {
                if (char.IsWhiteSpace(Line[Position]) == true)
                {
                    Position++;
                    continue;
                }

                if (Line[Position] == '"')
                {
                    int Closing = Line.IndexOf('"', Position + 1);
                    if (Closing < 0)
                    {
                        Tokens.Add(Line.Substring(Position + 1));
                        break;
                    }

                    Tokens.Add(Line.Substring(Position + 1, Closing - Position - 1));
                    Position = Closing + 1;
                }
                else
                {
                    int Start = Position;
                    while ((Position < Line.Length) && (char.IsWhiteSpace(Line[Position]) == false))
                        Position++;
                    Tokens.Add(Line.Substring(Start, Position - Start));
                }
            }

            return (Tokens);
        }

        /// <summary>
        /// Finds the file a FILE entry names. Cue sheets are written on Windows, so both slash kinds are
        /// accepted, and on case-sensitive systems a file whose name differs only in case is used.
        /// </summary>
        private static string ResolveImageFile(string CueDirectory, string Name, int LineNumber)
        {
            string Normalized = Name.Replace('\\', Path.DirectorySeparatorChar).Replace('/', Path.DirectorySeparatorChar);
            string Candidate = Path.Combine(CueDirectory, Normalized);
            if (File.Exists(Candidate) == true)
                return (Candidate);

            string SearchDirectory = Path.GetDirectoryName(Candidate);
            string WantedName = Path.GetFileName(Candidate);
            if (Directory.Exists(SearchDirectory) == true)
            {
                foreach (string Existing in Directory.EnumerateFiles(SearchDirectory))
                {
                    if (string.Equals(Path.GetFileName(Existing), WantedName, StringComparison.OrdinalIgnoreCase) == true)
                        return (Existing);
                }
            }

            throw Failure(LineNumber, "The referenced file '" + Name + "' was not found next to the cue sheet.");
        }
    }
}
