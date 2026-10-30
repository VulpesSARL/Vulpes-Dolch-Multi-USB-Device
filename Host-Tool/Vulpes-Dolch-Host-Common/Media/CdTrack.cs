using System;

namespace VulpesDolch.Host.Common.Media
{
    public enum CdTrackType
    {
        Audio,
        Mode1,
        Mode2
    }

    /// <summary>The kind of disc an image represents, as a CD-ROM drive would report it.</summary>
    public enum CdDiscType
    {
        /// <summary>Only data tracks (CD-ROM, and every DVD/BD image).</summary>
        Data,

        /// <summary>Only audio tracks (CD-DA).</summary>
        Audio,

        /// <summary>Data and audio tracks (mixed mode / CD-Extra).</summary>
        Mixed
    }

    /// <summary>One track of a disc image. All LBAs are disc LBAs: track 1 INDEX 01 is LBA 0.</summary>
    public sealed class CdTrack
    {
        public CdTrack(int Number, CdTrackType Type, int StoredSectorSize)
        {
            this.Number = Number;
            this.Type = Type;
            this.StoredSectorSize = StoredSectorSize;
        }

        public int Number { get; }

        public CdTrackType Type { get; }

        /// <summary>Bytes per sector as stored in the image file: 2048, 2336 or 2352.</summary>
        public int StoredSectorSize { get; }

        /// <summary>First sector of the track's pregap (INDEX 00). Equals StartLba when the track has none.</summary>
        public Int64 PregapStartLba { get; internal set; }

        /// <summary>First sector of the track proper (INDEX 01), the position a TOC reports.</summary>
        public Int64 StartLba { get; internal set; }

        /// <summary>One past the last sector of the track (the next track's pregap start, or the lead-out).</summary>
        public Int64 EndLba { get; internal set; }

        /// <summary>Sectors from StartLba to the end, not counting the pregap.</summary>
        public Int64 SectorCount => EndLba - StartLba;

        public bool IsData => Type != CdTrackType.Audio;
    }
}
