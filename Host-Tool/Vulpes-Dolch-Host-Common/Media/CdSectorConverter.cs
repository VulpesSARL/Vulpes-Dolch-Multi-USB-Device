using System;

namespace VulpesDolch.Host.Common.Media
{
    /// <summary>Turns a sector as stored in an image file into what a read request asked for.</summary>
    internal static class CdSectorConverter
    {
        private static readonly byte[] SyncPattern = new byte[] { 0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00 };

        private const int SyncSize = 12;
        private const int HeaderSize = 4;
        private const int SubheaderSize = 8;
        private const byte SubmodeForm2Flag = 0x20;

        /// <returns>The number of bytes written to Destination.</returns>
        internal static int ToRequestedMode(CdTrack Track, Int64 Lba, ReadOnlySpan<byte> Stored, CdSectorReadMode Mode, Span<byte> Destination)
        {
            if (Track.Type == CdTrackType.Audio)
                return (FromAudio(Track, Stored, Mode, Destination));
            if (Track.Type == CdTrackType.Mode1)
                return (FromMode1(Track, Stored, Mode, Destination));
            return (FromMode2(Track, Lba, Stored, Mode, Destination));
        }

        private static int FromAudio(CdTrack Track, ReadOnlySpan<byte> Stored, CdSectorReadMode Mode, Span<byte> Destination)
        {
            if (Mode != CdSectorReadMode.Raw)
                throw new CdImageException("Track " + Track.Number + " is an audio track: it has no cooked user data, read it as Raw.", CdImageErrorKind.BadFormat);

            Stored.CopyTo(Destination);
            return (Stored.Length);
        }

        private static int FromMode1(CdTrack Track, ReadOnlySpan<byte> Stored, CdSectorReadMode Mode, Span<byte> Destination)
        {
            if (Mode == CdSectorReadMode.UserData)
            {
                // Raw sectors carry 12 bytes sync + 4 bytes header in front of the data.
                int DataOffset = (Stored.Length == CdSector.RawSize) ? (SyncSize + HeaderSize) : 0;
                Stored.Slice(DataOffset, CdSector.UserDataSize).CopyTo(Destination);
                return (CdSector.UserDataSize);
            }

            if (Stored.Length != CdSector.RawSize)
                throw new CdImageException("Track " + Track.Number + " is stored as cooked " + Stored.Length + " byte sectors: raw sectors (sync, EDC/ECC) are not available.", CdImageErrorKind.BadFormat);

            Stored.CopyTo(Destination);
            return (CdSector.RawSize);
        }

        private static int FromMode2(CdTrack Track, Int64 Lba, ReadOnlySpan<byte> Stored, CdSectorReadMode Mode, Span<byte> Destination)
        {
            if (Stored.Length == CdSector.UserDataSize)
            {
                // Cooked Form 1 only: no subheader stored.
                if (Mode == CdSectorReadMode.Raw)
                    throw new CdImageException("Track " + Track.Number + " is stored as cooked 2048 byte sectors: raw sectors are not available.", CdImageErrorKind.BadFormat);

                Stored.CopyTo(Destination);
                return (CdSector.UserDataSize);
            }

            int SubheaderOffset = (Stored.Length == CdSector.RawSize) ? (SyncSize + HeaderSize) : 0;

            if (Mode == CdSectorReadMode.UserData)
            {
                bool IsForm2 = (Stored[SubheaderOffset + 2] & SubmodeForm2Flag) != 0;
                int DataSize = (IsForm2 == true) ? CdSector.Form2UserDataSize : CdSector.UserDataSize;
                Stored.Slice(SubheaderOffset + SubheaderSize, DataSize).CopyTo(Destination);
                return (DataSize);
            }

            if (Stored.Length == CdSector.RawSize)
            {
                Stored.CopyTo(Destination);
                return (CdSector.RawSize);
            }

            // MODE2/2336 images lack exactly the 16 bytes of sync and header: rebuild them.
            int Minutes;
            int Seconds;
            int Frames;
            CdAddress.ToMsf(Lba, out Minutes, out Seconds, out Frames);

            SyncPattern.CopyTo(Destination);
            Destination[SyncSize] = ToBcd(Minutes);
            Destination[SyncSize + 1] = ToBcd(Seconds);
            Destination[SyncSize + 2] = ToBcd(Frames);
            Destination[SyncSize + 3] = 2;
            Stored.CopyTo(Destination.Slice(SyncSize + HeaderSize));
            return (CdSector.RawSize);
        }

        private static byte ToBcd(int Value)
        {
            return ((byte)(((Value / 10) << 4) | (Value % 10)));
        }
    }
}
