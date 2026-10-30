using System;
using System.Collections.Generic;
using System.IO;
using System.Text;
using Microsoft.Win32.SafeHandles;

namespace VulpesDolch.Host.Common.Media
{
    /// <summary>
    /// A disc image opened for sector reads: a plain ISO (CD, DVD or BD, 2048 byte sectors) or a BIN/CUE
    /// image (data, audio or mixed mode). ReadSector may be called from several threads at once.
    /// </summary>
    public sealed class CdImage : IDisposable
    {
        // FileIndex of a segment whose sectors come from the provider.
        private const int ProviderFileIndex = -2;

        private readonly List<SafeFileHandle> FileHandles;
        private readonly List<CdSegment> Segments;
        private readonly List<CdTrack> TrackList;
        private ISectorProvider Provider = null;
        private bool Disposed = false;
        private bool VolumeLabelLoaded = false;
        private string VolumeLabelValue = null;

        internal CdImage(string ImagePath, string FormatName, List<CdTrack> Tracks, List<CdSegment> Segments, List<SafeFileHandle> FileHandles, Int64 LeadOutLba)
        {
            this.ImagePath = ImagePath;
            this.FormatName = FormatName;
            this.TrackList = Tracks;
            this.Segments = Segments;
            this.FileHandles = FileHandles;
            this.LeadOutLba = LeadOutLba;

            bool HasData = false;
            bool HasAudio = false;
            foreach (CdTrack Track in Tracks)
            {
                if (Track.IsData == true)
                    HasData = true;
                else
                    HasAudio = true;
            }

            if ((HasData == true) && (HasAudio == true))
                DiscType = CdDiscType.Mixed;
            else if (HasAudio == true)
                DiscType = CdDiscType.Audio;
            else
                DiscType = CdDiscType.Data;
        }

        public string ImagePath { get; }

        /// <summary>For a mounted folder: a number identifying the folder and its files (names, sizes, times); 0 for an image file.</summary>
        public Int64 ContentId { get; private set; }

        /// <summary>"ISO", "BIN/CUE" or "Folder (...)" for a mounted folder.</summary>
        public string FormatName { get; }

        public IReadOnlyList<CdTrack> Tracks => TrackList;

        /// <summary>Total number of addressable sectors; also the lead-out position in the TOC.</summary>
        public Int64 LeadOutLba { get; }

        public CdDiscType DiscType { get; }

        /// <summary>The ISO 9660 volume label of the first data track, or null if there is none.</summary>
        public string VolumeLabel
        {
            get
            {
                if (VolumeLabelLoaded == false)
                {
                    VolumeLabelValue = ReadVolumeLabel();
                    VolumeLabelLoaded = true;
                }
                return (VolumeLabelValue);
            }
        }

        /// <summary>Opens an image, choosing the format from the file extension (.iso or .cue). A folder is mounted as a data disc (ISO 9660 + Joliet) whose files are read from the disk when they are accessed.</summary>
        public static CdImage Open(string ImagePath)
        {
            if (string.IsNullOrEmpty(ImagePath) == true)
                throw new ArgumentException("An image path is required.", nameof(ImagePath));

            if (Directory.Exists(ImagePath) == true)
                return (FolderImageLoader.Load(ImagePath));

            string Extension = Path.GetExtension(ImagePath);

            if (string.Equals(Extension, ".iso", StringComparison.OrdinalIgnoreCase) == true)
                return (IsoImageLoader.Load(ImagePath));
            if (string.Equals(Extension, ".cue", StringComparison.OrdinalIgnoreCase) == true)
                return (CueImageLoader.Load(ImagePath));

            throw new CdImageException("Unsupported image type '" + Extension + "': expected .iso, or the .cue sheet of a BIN/CUE image.");
        }

        // A disc whose sectors come from a provider (a mounted folder) instead of from image files: one Mode 1 track.
        internal static CdImage CreateVirtual(string Path, string FormatName, ISectorProvider Provider, Int64 SectorCount, Int64 ContentId)
        {
            CdTrack Track = new CdTrack(1, CdTrackType.Mode1, CdSector.UserDataSize);
            Track.PregapStartLba = 0;
            Track.StartLba = 0;
            Track.EndLba = SectorCount;

            List<CdTrack> Tracks = new List<CdTrack>();
            Tracks.Add(Track);
            List<CdSegment> Segments = new List<CdSegment>();
            Segments.Add(new CdSegment(0, SectorCount, Track, ProviderFileIndex, 0));

            CdImage Image = new CdImage(Path, FormatName, Tracks, Segments, new List<SafeFileHandle>(), SectorCount);
            Image.Provider = Provider;
            Image.ContentId = ContentId;
            return (Image);
        }

        /// <summary>Mounts a folder as a data disc with explicit options (see <see cref="FolderDiscOptions"/>); "Open" uses the defaults.</summary>
        public static CdImage OpenFolder(string FolderPath, FolderDiscOptions Options)
        {
            return (FolderImageLoader.Load(FolderPath, Options));
        }

        /// <summary>The TOC a READ TOC command would return: one entry per track plus the lead-out.</summary>
        public IReadOnlyList<CdTocEntry> GetToc()
        {
            List<CdTocEntry> Entries = new List<CdTocEntry>();

            foreach (CdTrack Track in TrackList)
                Entries.Add(new CdTocEntry(Track.Number, 1, ControlFor(Track), Track.StartLba));

            Entries.Add(new CdTocEntry(CdTocEntry.LeadOutTrackNumber, 1, ControlFor(TrackList[TrackList.Count - 1]), LeadOutLba));
            return (Entries);
        }

        /// <summary>The track a sector belongs to, pregap sectors included. Throws if the LBA is outside the disc.</summary>
        public CdTrack FindTrack(Int64 Lba)
        {
            return (FindSegment(Lba).Track);
        }

        /// <summary>
        /// Reads one sector. Destination must hold at least CdSector.RawSize bytes.
        /// </summary>
        /// <returns>The number of bytes written to Destination.</returns>
        public int ReadSector(Int64 Lba, CdSectorReadMode Mode, Span<byte> Destination)
        {
            if (Disposed == true)
                throw new ObjectDisposedException(nameof(CdImage));
            if (Destination.Length < CdSector.RawSize)
                throw new ArgumentException("The destination must hold at least " + CdSector.RawSize + " bytes.", nameof(Destination));

            CdSegment Segment = FindSegment(Lba);
            CdTrack Track = Segment.Track;

            Span<byte> Buffer = stackalloc byte[CdSector.RawSize];
            Span<byte> Stored = Buffer.Slice(0, Track.StoredSectorSize);

            if (Segment.FileIndex == ProviderFileIndex)
            {
                Provider.ReadSector(Lba, Stored);
            }
            else if (Segment.FileIndex < 0)
            {
                Stored.Clear();
            }
            else
            {
                Int64 Offset = Segment.FileOffset + ((Lba - Segment.StartLba) * Track.StoredSectorSize);
                SafeFileHandle Handle = FileHandles[Segment.FileIndex];
                int Done = 0;
                while (Done < Stored.Length)
                {
                    int Count = RandomAccess.Read(Handle, Stored.Slice(Done), Offset + Done);
                    if (Count == 0)
                        throw new CdImageException("Unexpected end of the image file while reading LBA " + Lba + ": the file is shorter than expected.", CdImageErrorKind.ReadError);
                    Done += Count;
                }
            }

            return (CdSectorConverter.ToRequestedMode(Track, Lba, Stored, Mode, Destination));
        }

        public void Dispose()
        {
            if (Disposed == true)
                return;

            Disposed = true;
            foreach (SafeFileHandle Handle in FileHandles)
                Handle.Dispose();
            if (Provider != null)
                Provider.Dispose();
        }

        private static byte ControlFor(CdTrack Track)
        {
            return ((Track.IsData == true) ? (byte)0x04 : (byte)0x00);
        }

        private CdSegment FindSegment(Int64 Lba)
        {
            int Low = 0;
            int High = Segments.Count - 1;

            while (Low <= High)
            {
                int Middle = Low + ((High - Low) / 2);
                CdSegment Candidate = Segments[Middle];

                if (Lba < Candidate.StartLba)
                    High = Middle - 1;
                else if (Lba >= (Candidate.StartLba + Candidate.SectorCount))
                    Low = Middle + 1;
                else
                    return (Candidate);
            }

            throw new CdImageException("LBA " + Lba + " is outside the disc (0 to " + (LeadOutLba - 1) + ").", CdImageErrorKind.OutOfRange);
        }

        private string ReadVolumeLabel()
        {
            CdTrack DataTrack = null;
            foreach (CdTrack Track in TrackList)
            {
                if (Track.IsData == true)
                {
                    DataTrack = Track;
                    break;
                }
            }

            // The ISO 9660 primary volume descriptor sits in sector 16 of the data track.
            if (DataTrack == null)
                return (null);
            if ((DataTrack.StartLba + 16) >= DataTrack.EndLba)
                return (null);

            byte[] Sector = new byte[CdSector.RawSize];
            try
            {
                ReadSector(DataTrack.StartLba + 16, CdSectorReadMode.UserData, Sector);
            }
            catch (CdImageException)
            {
                return (null);
            }

            bool IsPrimaryDescriptor = (Sector[0] == 1) && (Encoding.ASCII.GetString(Sector, 1, 5) == "CD001");
            if (IsPrimaryDescriptor == false)
                return (null);

            return (Encoding.ASCII.GetString(Sector, 40, 32).Trim());
        }
    }
}
