using System;
using System.Collections.Generic;
using VulpesDolch.Host.Common.Protocol;

namespace VulpesDolch.Host.Common.Media
{
    /// <summary>
    /// Builds the byte blobs the Client returns for READ TOC and READ DISC INFORMATION: the exact
    /// response of the command, header included, so the Client only copies the first "allocation length" bytes. All multi-byte
    /// fields here are big endian, as on the SCSI side.
    /// </summary>
    public static class CdTocBuilder
    {
        /// <summary>MMC profile numbers.</summary>
        public const ushort ProfileCdRom = 0x0008;
        public const ushort ProfileDvdRom = 0x0010;
        public const ushort ProfileBdRom = 0x0040;

        // Largest sector counts of the media sizes: a 99 minute CD, a dual layer DVD.
        private const Int64 LargestCdSectors = 450000;
        private const Int64 LargestDvdSectors = 4400000;

        /// <summary>The MMC profile an image is reported under: every BIN/CUE is a CD, an ISO or a mounted folder by its size.</summary>
        public static ushort ChooseProfile(CdImage Image)
        {
            bool SizeDecides = string.Equals(Image.FormatName, "ISO", StringComparison.OrdinalIgnoreCase) || Image.FormatName.StartsWith("Folder", StringComparison.OrdinalIgnoreCase);
            if (SizeDecides == false)
                return (ProfileCdRom);
            if (Image.LeadOutLba <= LargestCdSectors)
                return (ProfileCdRom);
            if (Image.LeadOutLba <= LargestDvdSectors)
                return (ProfileDvdRom);
            return (ProfileBdRom);
        }

        /// <summary>All blobs MEDIA_INSERT carries for an image.</summary>
        public static List<CdRomBlob> BuildBlobs(CdImage Image)
        {
            List<CdRomBlob> Blobs = new List<CdRomBlob>();
            Blobs.Add(new CdRomBlob(CdRomBlobKind.TocLba, BuildToc(Image, false)));
            Blobs.Add(new CdRomBlob(CdRomBlobKind.TocMsf, BuildToc(Image, true)));
            Blobs.Add(new CdRomBlob(CdRomBlobKind.SessionInfoLba, BuildSessionInfo(Image, false)));
            Blobs.Add(new CdRomBlob(CdRomBlobKind.SessionInfoMsf, BuildSessionInfo(Image, true)));
            Blobs.Add(new CdRomBlob(CdRomBlobKind.FullToc, BuildFullToc(Image)));
            Blobs.Add(new CdRomBlob(CdRomBlobKind.DiscInformation, BuildDiscInformation(Image)));
            return (Blobs);
        }

        /// <summary>READ TOC, format 0: header, then one 8 byte descriptor per track and the lead-out (track number 0xAA).</summary>
        public static byte[] BuildToc(CdImage Image, bool Msf)
        {
            IReadOnlyList<CdTocEntry> Entries = Image.GetToc();
            byte[] Data = new byte[4 + (8 * Entries.Count)];

            WriteBigEndian16(Data, 0, Data.Length - 2);
            Data[2] = (byte)Image.Tracks[0].Number;
            Data[3] = (byte)Image.Tracks[Image.Tracks.Count - 1].Number;

            int Position = 4;
            foreach (CdTocEntry Entry in Entries)
            {
                Data[Position + 1] = (byte)((Entry.Adr << 4) | Entry.Control);
                Data[Position + 2] = (byte)Entry.TrackNumber;
                WriteAddress(Data, Position + 4, Entry.Lba, Msf);
                Position += 8;
            }

            return (Data);
        }

        /// <summary>READ TOC, format 1 (multi-session information): the first track of the last (and only) session.</summary>
        public static byte[] BuildSessionInfo(CdImage Image, bool Msf)
        {
            CdTrack First = Image.Tracks[0];
            byte[] Data = new byte[12];

            WriteBigEndian16(Data, 0, 10);
            Data[2] = 1;
            Data[3] = 1;
            Data[5] = (byte)((1 << 4) | ControlOf(First));
            Data[6] = (byte)First.Number;
            WriteAddress(Data, 8, First.StartLba, Msf);
            return (Data);
        }

        /// <summary>READ TOC, format 2 (full TOC): the A0, A1 and A2 points, then one descriptor per track, all addresses as MSF.</summary>
        public static byte[] BuildFullToc(CdImage Image)
        {
            CdTrack First = Image.Tracks[0];
            CdTrack Last = Image.Tracks[Image.Tracks.Count - 1];
            int Descriptors = 3 + Image.Tracks.Count;
            byte[] Data = new byte[4 + (11 * Descriptors)];

            WriteBigEndian16(Data, 0, Data.Length - 2);
            Data[2] = 1;
            Data[3] = 1;

            int Position = 4;

            // A0: the first track number, and the disc type (0x20 = CD-ROM XA when the first track is Mode 2).
            WriteFullDescriptor(Data, Position, ControlOf(First), 0xA0, First.Number, (First.Type == CdTrackType.Mode2) ? 0x20 : 0x00, 0);
            Position += 11;

            // A1: the last track number.
            WriteFullDescriptor(Data, Position, ControlOf(Last), 0xA1, Last.Number, 0, 0);
            Position += 11;

            // A2: the start of the lead-out.
            int Minutes;
            int Seconds;
            int Frames;
            CdAddress.ToMsf(Image.LeadOutLba, out Minutes, out Seconds, out Frames);
            WriteFullDescriptor(Data, Position, ControlOf(Last), 0xA2, Minutes, Seconds, Frames);
            Position += 11;

            foreach (CdTrack Track in Image.Tracks)
            {
                CdAddress.ToMsf(Track.StartLba, out Minutes, out Seconds, out Frames);
                WriteFullDescriptor(Data, Position, ControlOf(Track), Track.Number, Minutes, Seconds, Frames);
                Position += 11;
            }

            return (Data);
        }

        /// <summary>READ DISC INFORMATION (standard disc information block, 34 bytes): a finalized, single session disc.</summary>
        public static byte[] BuildDiscInformation(CdImage Image)
        {
            CdTrack First = Image.Tracks[0];
            CdTrack Last = Image.Tracks[Image.Tracks.Count - 1];
            byte[] Data = new byte[34];

            WriteBigEndian16(Data, 0, 32);
            Data[2] = 0x0E;                                         // not erasable, last session complete, disc complete
            Data[3] = (byte)First.Number;                           // first track on the disc
            Data[4] = 1;                                            // sessions (low byte)
            Data[5] = (byte)First.Number;                           // first track of the last session
            Data[6] = (byte)Last.Number;                            // last track of the last session
            Data[8] = (byte)((First.Type == CdTrackType.Mode2) ? 0x20 : 0x00);   // disc type: CD-ROM XA or CD-DA / CD-ROM
            return (Data);
        }

        private static byte ControlOf(CdTrack Track)
        {
            return ((Track.IsData == true) ? (byte)0x04 : (byte)0x00);
        }

        private static void WriteBigEndian16(byte[] Data, int Offset, int Value)
        {
            Data[Offset] = (byte)(Value >> 8);
            Data[Offset + 1] = (byte)Value;
        }

        /// <summary>A 4 byte TOC address: the LBA as a big endian number, or 0 followed by minutes, seconds, frames (absolute time).</summary>
        private static void WriteAddress(byte[] Data, int Offset, Int64 Lba, bool Msf)
        {
            if (Msf == false)
            {
                Data[Offset] = (byte)(Lba >> 24);
                Data[Offset + 1] = (byte)(Lba >> 16);
                Data[Offset + 2] = (byte)(Lba >> 8);
                Data[Offset + 3] = (byte)Lba;
                return;
            }

            int Minutes;
            int Seconds;
            int Frames;
            CdAddress.ToMsf(Lba, out Minutes, out Seconds, out Frames);
            Data[Offset] = 0;
            Data[Offset + 1] = (byte)Minutes;
            Data[Offset + 2] = (byte)Seconds;
            Data[Offset + 3] = (byte)Frames;
        }

        /// <summary>Session 1, ADR 1 with the control nibble, TNO 0, POINT, the (zero) running time, a zero byte, then PMIN, PSEC, PFRAME.</summary>
        private static void WriteFullDescriptor(byte[] Data, int Offset, byte Control, int Point, int Pmin, int Psec, int Pframe)
        {
            Data[Offset] = 1;
            Data[Offset + 1] = (byte)((1 << 4) | Control);
            Data[Offset + 2] = 0;
            Data[Offset + 3] = (byte)Point;
            Data[Offset + 8] = (byte)Pmin;
            Data[Offset + 9] = (byte)Psec;
            Data[Offset + 10] = (byte)Pframe;
        }
    }
}
