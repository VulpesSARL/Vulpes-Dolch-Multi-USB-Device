using System;

namespace VulpesDolch.Host.Common.Media
{
    /// <summary>Conversions between LBAs and the minute:second:frame addresses a CD uses.</summary>
    public static class CdAddress
    {
        /// <summary>Absolute disc time of an LBA, i.e. including the 2 second lead-in offset.</summary>
        public static void ToMsf(Int64 Lba, out int Minutes, out int Seconds, out int Frames)
        {
            Int64 Absolute = Lba + CdSector.LeadInFrames;
            Minutes = (int)(Absolute / (CdSector.FramesPerSecond * 60));
            Seconds = (int)((Absolute / CdSector.FramesPerSecond) % 60);
            Frames = (int)(Absolute % CdSector.FramesPerSecond);
        }

        public static string FormatMsf(Int64 Lba)
        {
            int Minutes;
            int Seconds;
            int Frames;
            ToMsf(Lba, out Minutes, out Seconds, out Frames);
            return (Minutes.ToString("D2") + ":" + Seconds.ToString("D2") + ":" + Frames.ToString("D2"));
        }
    }

    /// <summary>One row of a Table Of Contents, as a READ TOC command would return it.</summary>
    public sealed class CdTocEntry
    {
        /// <summary>The track number a TOC uses for the lead-out entry.</summary>
        public const int LeadOutTrackNumber = 0xAA;

        public CdTocEntry(int TrackNumber, byte Adr, byte Control, Int64 Lba)
        {
            this.TrackNumber = TrackNumber;
            this.Adr = Adr;
            this.Control = Control;
            this.Lba = Lba;
        }

        public int TrackNumber { get; }

        /// <summary>Q sub-channel mode, always 1 (position) here.</summary>
        public byte Adr { get; }

        /// <summary>Q sub-channel control nibble: 0x0 for 2-channel audio without pre-emphasis, 0x4 for data.</summary>
        public byte Control { get; }

        public Int64 Lba { get; }

        public bool IsLeadOut => TrackNumber == LeadOutTrackNumber;
    }
}
