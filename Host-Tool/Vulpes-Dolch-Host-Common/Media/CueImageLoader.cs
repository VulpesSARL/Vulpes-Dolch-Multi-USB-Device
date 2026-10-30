using System;
using System.Collections.Generic;
using System.IO;
using Microsoft.Win32.SafeHandles;

namespace VulpesDolch.Host.Common.Media
{
    /// <summary>
    /// Builds a CdImage from a .cue sheet and its BINARY or WAVE files (a WAVE file is served from its data chunk). Works in a running "cursor" LBA space that
    /// grows file after file (inserting PREGAP/POSTGAP silence where the sheet asks for it), then shifts
    /// everything so that INDEX 01 of track 1 is LBA 0, the way a real disc is addressed. Sectors before
    /// that point (a track 1 pregap stored in the file) fall into negative LBAs and are dropped.
    /// </summary>
    internal static class CueImageLoader
    {
        internal static CdImage Load(string CuePath)
        {
            CueSheet Sheet = CueSheet.Parse(CuePath);
            List<SafeFileHandle> FileHandles = new List<SafeFileHandle>();

            try
            {
                List<CdTrack> Tracks = new List<CdTrack>();
                List<CdSegment> Segments = new List<CdSegment>();
                Int64 Cursor = 0;

                foreach (CueFile SheetFile in Sheet.Files)
                    Cursor = AddFile(SheetFile, FileHandles, Tracks, Segments, Cursor);

                // INDEX 01 of track 1 is LBA 0.
                Int64 Origin = Tracks[0].StartLba;
                List<CdSegment> Shifted = ShiftSegments(Segments, Origin);

                foreach (CdTrack Track in Tracks)
                {
                    Track.PregapStartLba = Math.Max(0, Track.PregapStartLba - Origin);
                    Track.StartLba -= Origin;
                    Track.EndLba -= Origin;
                }

                return (new CdImage(CuePath, "BIN/CUE", Tracks, Shifted, FileHandles, Cursor - Origin));
            }
            catch
            {
                foreach (SafeFileHandle Handle in FileHandles)
                    Handle.Dispose();
                throw;
            }
        }

        /// <summary>Opens one FILE, appends its tracks and segments, and returns the new cursor.</summary>
        private static Int64 AddFile(CueFile SheetFile, List<SafeFileHandle> FileHandles, List<CdTrack> Tracks, List<CdSegment> Segments, Int64 Cursor)
        {
            int StoredSize = SheetFile.Tracks[0].StoredSectorSize;
            foreach (CueTrack Check in SheetFile.Tracks)
            {
                if (Check.StoredSectorSize != StoredSize)
                    throw new CdImageException("'" + SheetFile.ResolvedPath + "' holds tracks with different sector sizes (" + StoredSize + " and " + Check.StoredSectorSize + "): a file must use one sector size.");
            }

            SafeFileHandle Handle;
            try
            {
                Handle = File.OpenHandle(SheetFile.ResolvedPath, FileMode.Open, FileAccess.Read, FileShare.Read, FileOptions.RandomAccess);
            }
            catch (IOException Problem)
            {
                throw new CdImageException("Cannot open '" + SheetFile.ResolvedPath + "': " + Problem.Message);
            }
            catch (UnauthorizedAccessException Problem)
            {
                throw new CdImageException("Cannot open '" + SheetFile.ResolvedPath + "': " + Problem.Message);
            }

            FileHandles.Add(Handle);
            int FileIndex = FileHandles.Count - 1;

            Int64 FileLength = RandomAccess.GetLength(Handle);
            Int64 DataOffset = 0;
            if (SheetFile.IsWave == true)
            {
                Int64 DataLength;
                WaveFile.ReadDataChunk(Handle, FileLength, SheetFile.ResolvedPath, out DataOffset, out DataLength);
                FileLength = DataLength;
            }
            if ((FileLength % StoredSize) != 0)
                throw new CdImageException("'" + SheetFile.ResolvedPath + "' has a size (" + FileLength + " bytes) that is not a whole number of " + StoredSize + " byte sectors.");
            Int64 FileSectors = FileLength / StoredSize;

            for (int TrackIndex = 0; TrackIndex < SheetFile.Tracks.Count; TrackIndex++)
            {
                CueTrack Cue = SheetFile.Tracks[TrackIndex];
                Int64 Index01 = Cue.Index01Frames;
                Int64 Index00 = (Cue.Index00Frames >= 0) ? Cue.Index00Frames : Index01;

                // The first track of a file also owns everything in front of its first index; every other
                // track starts at its INDEX 00 (or INDEX 01) and ends where the next track's range begins.
                Int64 RangeStart = (TrackIndex == 0) ? 0 : Index00;
                Int64 RangeEnd = FileSectors;
                if ((TrackIndex + 1) < SheetFile.Tracks.Count)
                {
                    CueTrack Next = SheetFile.Tracks[TrackIndex + 1];
                    RangeEnd = (Next.Index00Frames >= 0) ? Next.Index00Frames : Next.Index01Frames;
                }

                if (Index00 > Index01)
                    throw new CdImageException("Track " + Cue.Number + ": INDEX 00 lies after INDEX 01.");
                if ((Index01 < RangeStart) || (Index01 >= RangeEnd))
                    throw new CdImageException("Track " + Cue.Number + ": its indexes are out of order or leave the track without any data.");
                if (RangeEnd > FileSectors)
                    throw new CdImageException("Track " + Cue.Number + ": a position lies beyond the end of '" + SheetFile.ResolvedPath + "' (" + FileSectors + " sectors).");

                CdTrack Track = new CdTrack(Cue.Number, Cue.Type, StoredSize);
                Track.PregapStartLba = Cursor;

                if (Cue.PregapFrames > 0)
                {
                    Segments.Add(new CdSegment(Cursor, Cue.PregapFrames, Track, -1, 0));
                    Cursor += Cue.PregapFrames;
                }

                Int64 FileSectorCount = RangeEnd - RangeStart;
                Segments.Add(new CdSegment(Cursor, FileSectorCount, Track, FileIndex, DataOffset + (RangeStart * StoredSize)));
                Track.StartLba = Cursor + (Index01 - RangeStart);
                Cursor += FileSectorCount;

                if (Cue.PostgapFrames > 0)
                {
                    Segments.Add(new CdSegment(Cursor, Cue.PostgapFrames, Track, -1, 0));
                    Cursor += Cue.PostgapFrames;
                }

                Track.EndLba = Cursor;
                Tracks.Add(Track);
            }

            return (Cursor);
        }

        /// <summary>Moves every segment down by Origin sectors, trimming or dropping what ends up below LBA 0.</summary>
        private static List<CdSegment> ShiftSegments(List<CdSegment> Segments, Int64 Origin)
        {
            List<CdSegment> Shifted = new List<CdSegment>();

            foreach (CdSegment Segment in Segments)
            {
                Int64 NewStart = Segment.StartLba - Origin;
                if ((NewStart + Segment.SectorCount) <= 0)
                    continue;

                if (NewStart < 0)
                {
                    Int64 Cut = -NewStart;
                    if (Segment.FileIndex >= 0)
                        Segment.FileOffset += Cut * Segment.Track.StoredSectorSize;
                    Segment.SectorCount -= Cut;
                    NewStart = 0;
                }

                Segment.StartLba = NewStart;
                Shifted.Add(Segment);
            }

            return (Shifted);
        }
    }
}
