using System;
using System.Collections.Generic;
using System.IO;
using Microsoft.Win32.SafeHandles;

namespace VulpesDolch.Host.Common.Media
{
    /// <summary>
    /// Loads a plain .iso: a file of consecutive 2048 byte sectors, a single Mode 1 data track. This covers
    /// CD-ROM, DVD and BD images alike - the sector size is the same, only the size differs.
    /// </summary>
    internal static class IsoImageLoader
    {
        internal static CdImage Load(string IsoPath)
        {
            SafeFileHandle Handle;
            try
            {
                Handle = File.OpenHandle(IsoPath, FileMode.Open, FileAccess.Read, FileShare.Read, FileOptions.RandomAccess);
            }
            catch (IOException Problem)
            {
                throw new CdImageException("Cannot open '" + IsoPath + "': " + Problem.Message);
            }
            catch (UnauthorizedAccessException Problem)
            {
                throw new CdImageException("Cannot open '" + IsoPath + "': " + Problem.Message);
            }

            Int64 Length = RandomAccess.GetLength(Handle);
            if ((Length == 0) || ((Length % CdSector.UserDataSize) != 0))
            {
                Handle.Dispose();
                throw new CdImageException("'" + IsoPath + "' is not a plain ISO image: its size (" + Length + " bytes) is not a whole number of 2048 byte sectors.");
            }

            Int64 SectorCount = Length / CdSector.UserDataSize;

            CdTrack Track = new CdTrack(1, CdTrackType.Mode1, CdSector.UserDataSize);
            Track.PregapStartLba = 0;
            Track.StartLba = 0;
            Track.EndLba = SectorCount;

            List<CdTrack> Tracks = new List<CdTrack>();
            Tracks.Add(Track);

            List<CdSegment> Segments = new List<CdSegment>();
            Segments.Add(new CdSegment(0, SectorCount, Track, 0, 0));

            List<SafeFileHandle> FileHandles = new List<SafeFileHandle>();
            FileHandles.Add(Handle);

            return (new CdImage(IsoPath, "ISO", Tracks, Segments, FileHandles, SectorCount));
        }
    }
}
