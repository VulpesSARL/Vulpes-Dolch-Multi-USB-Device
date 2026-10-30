using System;
using System.Buffers.Binary;

namespace VulpesDolch.Host.Common.Protocol
{
    /// <summary>Message types of the HDD channel.</summary>
    public static class HddType
    {
        /// <summary>PC -> Client: a disk is attached (see <see cref="HddMessages.CreateDiskAttach"/>).</summary>
        public const byte DiskAttach = 0x10;

        /// <summary>PC -> Client: take the disk away (u8 Force).</summary>
        public const byte DiskDetach = 0x11;

        /// <summary>PC -> Client: sector bytes answering a <see cref="ReadRequest"/> (several messages of one Tag when large).</summary>
        public const byte Data = 0x12;

        /// <summary>PC -> Client: the end of a request (read, write, flush, unmap), with the status.</summary>
        public const byte Complete = 0x13;

        /// <summary>Client -> PC: the state of the drive; also the answer to DiskAttach and DiskDetach.</summary>
        public const byte DiskState = 0x20;

        /// <summary>Client -> PC: read sectors.</summary>
        public const byte ReadRequest = 0x21;

        /// <summary>Client -> PC: the Client gave up on a ReadRequest (60 s without an answer).</summary>
        public const byte Cancel = 0x22;

        /// <summary>Client -> PC: write sectors; the bytes follow in <see cref="WriteData"/> messages of the same Tag, and the PC answers with Complete once everything is written.</summary>
        public const byte WriteRequest = 0x23;

        /// <summary>Client -> PC: the sector bytes of a <see cref="WriteRequest"/>, in pieces of up to 16 KB.</summary>
        public const byte WriteData = 0x24;

        /// <summary>Client -> PC: flush everything written so far to the storage under the image (SYNCHRONIZE CACHE).</summary>
        public const byte FlushRequest = 0x25;

        /// <summary>Client -> PC: release or zero a range of sectors (UNMAP, WRITE SAME, FORMAT UNIT).</summary>
        public const byte UnmapRequest = 0x26;

        /// <summary>Client -> PC: the name given with <see cref="SetName"/>; empty payload = none.</summary>
        public const byte Name = 0x27;

        /// <summary>PC -> Client: the name (full path, UTF-8, at most 128 bytes) of the image just attached; kept in the Client's RAM only.</summary>
        public const byte SetName = 0x15;

        /// <summary>PC -> Client: no payload; answered with <see cref="Name"/>.</summary>
        public const byte NameRequest = 0x16;
    }

    /// <summary>Status of a COMPLETE message.</summary>
    public enum HddStatus : byte
    {
        Ok = 0,
        NoMedium = 1,
        OutOfRange = 2,
        ReadError = 3,
        Cancelled = 5,
        Failed = 6,
        WriteError = 7
    }

    /// <summary>What a DISK_STATE message reports besides the present flag.</summary>
    public enum HddEvent : byte
    {
        None = 0,
        EjectedByOs = 1,
        EjectRefused = 2,
        LockChanged = 3,
        BusReset = 4
    }

    /// <summary>What an UNMAP request asks for: the range must read back as zeros afterwards either way.</summary>
    public enum HddUnmapMode : byte
    {
        /// <summary>Give the space back where the image can (a sparse VHDX keeps nothing allocated for zeros); otherwise write zeros.</summary>
        Release = 0,

        /// <summary>Write zeros.</summary>
        Zero = 1
    }

    /// <summary>Payload of DISK_ATTACH.</summary>
    public sealed class HddAttachInfo
    {
        /// <summary>Write protected: set by the PC, enforced by the Client.</summary>
        public const byte FlagReadOnly = 0x01;

        /// <summary>The Client already holds this disk and only the PC side reconnects (no UNIT ATTENTION).</summary>
        public const byte FlagReattach = 0x04;

        public Int64 MediaId { get; set; }

        /// <summary>Size in sectors of <see cref="SectorSize"/> bytes.</summary>
        public UInt64 SectorCount { get; set; }

        /// <summary>Only 512 is supported.</summary>
        public uint SectorSize { get; set; } = 512;

        public bool ReadOnly { get; set; }

        public bool Reattach { get; set; }
    }

    /// <summary>Payload of DISK_STATE.</summary>
    public sealed class HddDiskState
    {
        public bool Present { get; set; }

        /// <summary>The OS holds a PREVENT MEDIUM REMOVAL lock.</summary>
        public bool Locked { get; set; }

        public HddEvent Event { get; set; }

        public bool ReadOnly { get; set; }

        public Int64 MediaId { get; set; }
    }

    /// <summary>Payload of READ_REQUEST, WRITE_REQUEST and UNMAP_REQUEST: a range of sectors.</summary>
    public sealed class HddRangeRequest
    {
        public UInt64 Lba { get; set; }

        public uint Count { get; set; }

        /// <summary>Only for UNMAP_REQUEST.</summary>
        public HddUnmapMode Mode { get; set; }

        /// <summary>Only for READ_REQUEST: the first <c>BulkBuffers</c> x 16384 bytes of the read are to travel over the dedicated bulk data lane (0 = all as DATA messages).</summary>
        public uint BulkBuffers { get; set; }
    }

    /// <summary>Builders and parsers for the HDD channel.</summary>
    public static class HddMessages
    {
        public const int AttachSize = 24;
        public const int DiskStateSize = 12;
        public const int RangeRequestSize = 16;
        public const int CompleteSize = 8;

        /// <summary>The largest payload of one DATA / WRITE_DATA message.</summary>
        public const int DataChunkLimit = 16352;

        public static Message CreateDiskAttach(ushort Tag, HddAttachInfo Info)
        {
            byte[] Payload = new byte[AttachSize];
            BinaryPrimitives.WriteInt64LittleEndian(Payload.AsSpan(0, 8), Info.MediaId);
            BinaryPrimitives.WriteUInt64LittleEndian(Payload.AsSpan(8, 8), Info.SectorCount);
            BinaryPrimitives.WriteUInt32LittleEndian(Payload.AsSpan(16, 4), Info.SectorSize);
            byte Flags = 0;
            if (Info.ReadOnly == true)
                Flags |= HddAttachInfo.FlagReadOnly;
            if (Info.Reattach == true)
                Flags |= HddAttachInfo.FlagReattach;
            Payload[20] = Flags;
            return (new Message(MessageChannel.Hdd, HddType.DiskAttach, Tag, Payload));
        }

        /// <summary>Parses a DISK_ATTACH payload (used by tests and by anything that plays the Client).</summary>
        public static bool TryParseDiskAttach(Message Source, out HddAttachInfo Info)
        {
            Info = null;
            if ((Source.Channel != MessageChannel.Hdd) || (Source.Type != HddType.DiskAttach))
                return (false);
            if (Source.Payload.Length < AttachSize)
                return (false);

            HddAttachInfo Parsed = new HddAttachInfo();
            Parsed.MediaId = BinaryPrimitives.ReadInt64LittleEndian(Source.Payload.AsSpan(0, 8));
            Parsed.SectorCount = BinaryPrimitives.ReadUInt64LittleEndian(Source.Payload.AsSpan(8, 8));
            Parsed.SectorSize = BinaryPrimitives.ReadUInt32LittleEndian(Source.Payload.AsSpan(16, 4));
            Parsed.ReadOnly = (Source.Payload[20] & HddAttachInfo.FlagReadOnly) != 0;
            Parsed.Reattach = (Source.Payload[20] & HddAttachInfo.FlagReattach) != 0;
            Info = Parsed;
            return (true);
        }

        /// <summary>Largest name the Client keeps (bytes of UTF-8).</summary>
        public const int MaxNameBytes = 320;

        /// <summary>The name message, or null when the name is empty or longer than the Client can keep.</summary>
        public static Message CreateSetName(ushort Tag, string Name)
        {
            if (string.IsNullOrEmpty(Name) == true)
                return (null);

            byte[] Bytes = System.Text.Encoding.UTF8.GetBytes(Name);
            if (Bytes.Length > MaxNameBytes)
                return (null);

            return (new Message(MessageChannel.Hdd, HddType.SetName, Tag, Bytes));
        }

        public static Message CreateNameRequest(ushort Tag)
        {
            return (new Message(MessageChannel.Hdd, HddType.NameRequest, Tag, new byte[0]));
        }

        public static Message CreateDiskDetach(ushort Tag, bool Force)
        {
            return (new Message(MessageChannel.Hdd, HddType.DiskDetach, Tag, new byte[] { (Force == true) ? (byte)1 : (byte)0 }));
        }

        public static Message CreateDiskState(ushort Tag, HddDiskState State)
        {
            byte[] Payload = new byte[DiskStateSize];
            Payload[0] = (State.Present == true) ? (byte)1 : (byte)0;
            Payload[1] = (State.Locked == true) ? (byte)1 : (byte)0;
            Payload[2] = (byte)State.Event;
            Payload[3] = (State.ReadOnly == true) ? (byte)1 : (byte)0;
            BinaryPrimitives.WriteInt64LittleEndian(Payload.AsSpan(4, 8), State.MediaId);
            return (new Message(MessageChannel.Hdd, HddType.DiskState, Tag, Payload));
        }

        public static bool TryParseDiskState(Message Source, out HddDiskState State)
        {
            State = null;
            if ((Source.Channel != MessageChannel.Hdd) || (Source.Type != HddType.DiskState))
                return (false);
            if (Source.Payload.Length < DiskStateSize)
                return (false);

            HddDiskState Parsed = new HddDiskState();
            Parsed.Present = Source.Payload[0] != 0;
            Parsed.Locked = Source.Payload[1] != 0;
            Parsed.Event = (HddEvent)Source.Payload[2];
            Parsed.ReadOnly = Source.Payload[3] != 0;
            Parsed.MediaId = BinaryPrimitives.ReadInt64LittleEndian(Source.Payload.AsSpan(4, 8));
            State = Parsed;
            return (true);
        }

        /// <summary>Builds READ_REQUEST, WRITE_REQUEST or UNMAP_REQUEST (type chooses; Mode only matters for UNMAP).</summary>
        public static Message CreateRangeRequest(byte Type, ushort Tag, HddRangeRequest Request)
        {
            byte[] Payload = new byte[RangeRequestSize];
            BinaryPrimitives.WriteUInt64LittleEndian(Payload.AsSpan(0, 8), Request.Lba);
            BinaryPrimitives.WriteUInt32LittleEndian(Payload.AsSpan(8, 4), Request.Count);
            Payload[12] = (byte)Request.Mode;
            return (new Message(MessageChannel.Hdd, Type, Tag, Payload));
        }

        public static bool TryParseRangeRequest(Message Source, byte ExpectedType, out HddRangeRequest Request)
        {
            Request = null;
            if ((Source.Channel != MessageChannel.Hdd) || (Source.Type != ExpectedType))
                return (false);
            if (Source.Payload.Length < RangeRequestSize)
                return (false);

            HddRangeRequest Parsed = new HddRangeRequest();
            Parsed.Lba = BinaryPrimitives.ReadUInt64LittleEndian(Source.Payload.AsSpan(0, 8));
            Parsed.Count = BinaryPrimitives.ReadUInt32LittleEndian(Source.Payload.AsSpan(8, 4));
            Parsed.Mode = (HddUnmapMode)Source.Payload[12];
            if (Source.Payload.Length >= 16)
                Parsed.BulkBuffers = BinaryPrimitives.ReadUInt32LittleEndian(Source.Payload.AsSpan(12, 4));
            Request = Parsed;
            return (true);
        }

        public static Message CreateData(byte Type, ushort Tag, byte[] Buffer, int Offset, int Length)
        {
            byte[] Payload = new byte[Length];
            Array.Copy(Buffer, Offset, Payload, 0, Length);
            return (new Message(MessageChannel.Hdd, Type, Tag, Payload));
        }

        public static Message CreateComplete(ushort Tag, HddStatus Status, uint SectorsDone)
        {
            byte[] Payload = new byte[CompleteSize];
            Payload[0] = (byte)Status;
            BinaryPrimitives.WriteUInt32LittleEndian(Payload.AsSpan(4, 4), SectorsDone);
            return (new Message(MessageChannel.Hdd, HddType.Complete, Tag, Payload));
        }

        public static bool TryParseComplete(Message Source, out HddStatus Status, out uint SectorsDone)
        {
            Status = HddStatus.Failed;
            SectorsDone = 0;
            if ((Source.Channel != MessageChannel.Hdd) || (Source.Type != HddType.Complete))
                return (false);
            if (Source.Payload.Length < CompleteSize)
                return (false);

            Status = (HddStatus)Source.Payload[0];
            SectorsDone = BinaryPrimitives.ReadUInt32LittleEndian(Source.Payload.AsSpan(4, 4));
            return (true);
        }
    }
}
