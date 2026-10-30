using System;

namespace VulpesDolch.Host.Common.Media
{
    /// <summary>
    /// A run of consecutive disc sectors that all belong to one track and come from one place: a byte range
    /// of an image file, or (FileIndex below 0) generated silence for a PREGAP/POSTGAP that no file stores.
    /// </summary>
    internal sealed class CdSegment
    {
        internal CdSegment(Int64 StartLba, Int64 SectorCount, CdTrack Track, int FileIndex, Int64 FileOffset)
        {
            this.StartLba = StartLba;
            this.SectorCount = SectorCount;
            this.Track = Track;
            this.FileIndex = FileIndex;
            this.FileOffset = FileOffset;
        }

        internal Int64 StartLba { get; set; }

        internal Int64 SectorCount { get; set; }

        internal CdTrack Track { get; }

        /// <summary>Index into the image's file list, or -1 for generated (all zero) sectors.</summary>
        internal int FileIndex { get; }

        /// <summary>Byte offset in the file of the segment's first sector.</summary>
        internal Int64 FileOffset { get; set; }
    }
}
