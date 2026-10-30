using System;
using System.Buffers.Binary;
using System.Collections.Generic;

namespace VulpesDolch.Host.Common.Protocol
{
    /// <summary>Message types of the CD-ROM channel.</summary>
    public static class CdRomType
    {
        /// <summary>PC -> Client: a medium is inserted (see <see cref="CdRomMessages.CreateMediaInsert"/>).</summary>
        public const byte MediaInsert = 0x10;

        /// <summary>PC -> Client: take the medium away (u8 Force).</summary>
        public const byte MediaRemove = 0x11;

        /// <summary>PC -> Client: sector bytes answering a <see cref="ReadRequest"/> (several messages of one Tag when large).</summary>
        public const byte Data = 0x12;

        /// <summary>PC -> Client: the end of the answer to a <see cref="ReadRequest"/>, with the status.</summary>
        public const byte Complete = 0x13;

        /// <summary>PC -> Client: ask the OS to eject the disc (the Client reports the MMC media event EjectRequest); no payload, no answer.</summary>
        public const byte EjectRequest = 0x14;

        /// <summary>PC -> Client: the name (full path, UTF-8, at most 128 bytes) of the image just inserted; the Client keeps it in RAM only, so the PC can ask for it after a restart.</summary>
        public const byte SetName = 0x15;

        /// <summary>PC -> Client: no payload; answered with <see cref="Name"/>.</summary>
        public const byte NameRequest = 0x16;

        /// <summary>Client -> PC: the state of the drive; also the answer to MediaInsert and MediaRemove.</summary>
        public const byte MediaState = 0x20;

        /// <summary>Client -> PC: read sectors.</summary>
        public const byte ReadRequest = 0x21;

        /// <summary>Client -> PC: the Client gave up on a ReadRequest (60 s without an answer).</summary>
        public const byte Cancel = 0x22;

        /// <summary>Client -> PC: the name given with <see cref="SetName"/>; empty payload = none.</summary>
        public const byte Name = 0x23;
    }

    /// <summary>Status of a COMPLETE message.</summary>
    public enum CdRomStatus : byte
    {
        Ok = 0,
        NoMedium = 1,
        OutOfRange = 2,
        ReadError = 3,
        BadFormat = 4,
        Cancelled = 5,
        Failed = 6
    }

    /// <summary>What a MEDIA_STATE message reports besides the present flag.</summary>
    public enum CdRomEvent : byte
    {
        None = 0,
        EjectedByOs = 1,
        EjectRefused = 2,
        LockChanged = 3,
        BusReset = 4
    }

    /// <summary>The kind of a blob inside MEDIA_INSERT: the exact bytes one command returns, built by the PC.</summary>
    public enum CdRomBlobKind : byte
    {
        TocLba = 0x01,
        TocMsf = 0x02,
        SessionInfoLba = 0x03,
        SessionInfoMsf = 0x04,
        FullToc = 0x05,
        DiscInformation = 0x10
    }

    /// <summary>One blob of MEDIA_INSERT.</summary>
    public sealed class CdRomBlob
    {
        public CdRomBlob(CdRomBlobKind Kind, byte[] Data)
        {
            this.Kind = Kind;
            this.Data = Data;
        }

        public CdRomBlobKind Kind { get; }

        public byte[] Data { get; }
    }

    /// <summary>Payload of MEDIA_INSERT.</summary>
    public sealed class CdRomInsertInfo
    {
        /// <summary>Set in <see cref="Flags"/> when the Client already holds this medium and only the PC side reconnects (no UNIT ATTENTION).</summary>
        public const byte FlagReattach = 0x01;

        /// <summary>Speed test disc: the Client makes the sector data up itself (a counting pattern), nothing is asked from the PC.</summary>
        public const byte FlagSynthetic = 0x80;

        /// <summary>With <see cref="FlagSynthetic"/>: the Client does not even write the pattern (a speed test of its USB side alone).</summary>
        public const byte FlagNoPattern = 0x40;

        public Int64 MediaId { get; set; }

        /// <summary>MMC current profile: 0x0008 CD-ROM, 0x0010 DVD-ROM, 0x0040 BD-ROM.</summary>
        public ushort Profile { get; set; }

        /// <summary>0 data, 1 audio, 2 mixed.</summary>
        public byte DiscType { get; set; }

        public byte Flags { get; set; }

        /// <summary>Total number of sectors; the lead-out position.</summary>
        public uint LeadOutLba { get; set; }

        public List<CdRomBlob> Blobs { get; } = new List<CdRomBlob>();
    }

    /// <summary>Payload of MEDIA_STATE.</summary>
    public sealed class CdRomMediaState
    {
        public bool Present { get; set; }

        public bool Locked { get; set; }

        public CdRomEvent Event { get; set; }

        public Int64 MediaId { get; set; }
    }

    /// <summary>Payload of READ_REQUEST.</summary>
    public sealed class CdRomReadRequest
    {
        /// <summary>Format value for cooked user data (2048 bytes per sector).</summary>
        public const byte FormatUserData = 0;

        /// <summary>Format value for raw sectors (2352 bytes per sector).</summary>
        public const byte FormatRaw = 1;

        public uint Lba { get; set; }

        public uint Count { get; set; }

        public byte Format { get; set; }
    }

    /// <summary>Builders and parsers for the CD-ROM channel.</summary>
    public static class CdRomMessages
    {
        public const int InsertHeaderSize = 20;
        public const int BlobHeaderSize = 8;
        public const int MediaStateSize = 12;
        public const int ReadRequestSize = 12;
        public const int CompleteSize = 8;

        public static Message CreateMediaInsert(ushort Tag, CdRomInsertInfo Info)
        {
            int Size = InsertHeaderSize;
            foreach (CdRomBlob Blob in Info.Blobs)
                Size += BlobHeaderSize + ((Blob.Data.Length + 3) & ~3);

            byte[] Payload = new byte[Size];
            BinaryPrimitives.WriteInt64LittleEndian(Payload.AsSpan(0, 8), Info.MediaId);
            BinaryPrimitives.WriteUInt16LittleEndian(Payload.AsSpan(8, 2), Info.Profile);
            Payload[10] = Info.DiscType;
            Payload[11] = Info.Flags;
            BinaryPrimitives.WriteUInt32LittleEndian(Payload.AsSpan(12, 4), Info.LeadOutLba);
            BinaryPrimitives.WriteUInt16LittleEndian(Payload.AsSpan(16, 2), (ushort)Info.Blobs.Count);

            int Position = InsertHeaderSize;
            foreach (CdRomBlob Blob in Info.Blobs)
            {
                Payload[Position] = (byte)Blob.Kind;
                BinaryPrimitives.WriteUInt32LittleEndian(Payload.AsSpan(Position + 4, 4), (uint)Blob.Data.Length);
                Blob.Data.CopyTo(Payload, Position + BlobHeaderSize);
                Position += BlobHeaderSize + ((Blob.Data.Length + 3) & ~3);
            }

            return (new Message(MessageChannel.CdRom, CdRomType.MediaInsert, Tag, Payload));
        }

        /// <summary>Parses a MEDIA_INSERT payload (used by tests and by anything that plays the Client).</summary>
        public static bool TryParseMediaInsert(Message Source, out CdRomInsertInfo Info)
        {
            Info = null;
            if ((Source.Channel != MessageChannel.CdRom) || (Source.Type != CdRomType.MediaInsert))
                return (false);
            if (Source.Payload.Length < InsertHeaderSize)
                return (false);

            CdRomInsertInfo Parsed = new CdRomInsertInfo();
            Parsed.MediaId = BinaryPrimitives.ReadInt64LittleEndian(Source.Payload.AsSpan(0, 8));
            Parsed.Profile = BinaryPrimitives.ReadUInt16LittleEndian(Source.Payload.AsSpan(8, 2));
            Parsed.DiscType = Source.Payload[10];
            Parsed.Flags = Source.Payload[11];
            Parsed.LeadOutLba = BinaryPrimitives.ReadUInt32LittleEndian(Source.Payload.AsSpan(12, 4));
            int Count = BinaryPrimitives.ReadUInt16LittleEndian(Source.Payload.AsSpan(16, 2));

            int Position = InsertHeaderSize;
            for (int Index = 0; Index < Count; Index++)
            {
                if ((Position + BlobHeaderSize) > Source.Payload.Length)
                    return (false);

                CdRomBlobKind Kind = (CdRomBlobKind)Source.Payload[Position];
                int Length = (int)BinaryPrimitives.ReadUInt32LittleEndian(Source.Payload.AsSpan(Position + 4, 4));
                int Padded = (Length + 3) & ~3;
                if ((Length < 0) || ((Position + BlobHeaderSize + Padded) > Source.Payload.Length))
                    return (false);

                byte[] Data = new byte[Length];
                Array.Copy(Source.Payload, Position + BlobHeaderSize, Data, 0, Length);
                Parsed.Blobs.Add(new CdRomBlob(Kind, Data));
                Position += BlobHeaderSize + Padded;
            }

            Info = Parsed;
            return (true);
        }

        /// <summary>Largest name the Client keeps (bytes of UTF-8).</summary>
        public const int MaxNameBytes = 320;

        /// <summary>The name message, or null when the name is empty or longer than the Client can keep (a cut path would be useless).</summary>
        public static Message CreateSetName(ushort Tag, string Name)
        {
            if (string.IsNullOrEmpty(Name) == true)
                return (null);

            byte[] Bytes = System.Text.Encoding.UTF8.GetBytes(Name);
            if (Bytes.Length > MaxNameBytes)
                return (null);

            return (new Message(MessageChannel.CdRom, CdRomType.SetName, Tag, Bytes));
        }

        public static Message CreateNameRequest(ushort Tag)
        {
            return (new Message(MessageChannel.CdRom, CdRomType.NameRequest, Tag, new byte[0]));
        }

        public static Message CreateEjectRequest(ushort Tag)
        {
            return (new Message(MessageChannel.CdRom, CdRomType.EjectRequest, Tag, new byte[0]));
        }

        public static Message CreateMediaRemove(ushort Tag, bool Force)
        {
            return (new Message(MessageChannel.CdRom, CdRomType.MediaRemove, Tag, new byte[] { (Force == true) ? (byte)1 : (byte)0 }));
        }

        public static Message CreateMediaState(ushort Tag, CdRomMediaState State)
        {
            byte[] Payload = new byte[MediaStateSize];
            Payload[0] = (State.Present == true) ? (byte)1 : (byte)0;
            Payload[1] = (State.Locked == true) ? (byte)1 : (byte)0;
            Payload[2] = (byte)State.Event;
            BinaryPrimitives.WriteInt64LittleEndian(Payload.AsSpan(4, 8), State.MediaId);
            return (new Message(MessageChannel.CdRom, CdRomType.MediaState, Tag, Payload));
        }

        public static bool TryParseMediaState(Message Source, out CdRomMediaState State)
        {
            State = null;
            if ((Source.Channel != MessageChannel.CdRom) || (Source.Type != CdRomType.MediaState))
                return (false);
            if (Source.Payload.Length < MediaStateSize)
                return (false);

            CdRomMediaState Parsed = new CdRomMediaState();
            Parsed.Present = Source.Payload[0] != 0;
            Parsed.Locked = Source.Payload[1] != 0;
            Parsed.Event = (CdRomEvent)Source.Payload[2];
            Parsed.MediaId = BinaryPrimitives.ReadInt64LittleEndian(Source.Payload.AsSpan(4, 8));
            State = Parsed;
            return (true);
        }

        public static Message CreateReadRequest(ushort Tag, CdRomReadRequest Request)
        {
            byte[] Payload = new byte[ReadRequestSize];
            BinaryPrimitives.WriteUInt32LittleEndian(Payload.AsSpan(0, 4), Request.Lba);
            BinaryPrimitives.WriteUInt32LittleEndian(Payload.AsSpan(4, 4), Request.Count);
            Payload[8] = Request.Format;
            return (new Message(MessageChannel.CdRom, CdRomType.ReadRequest, Tag, Payload));
        }

        public static bool TryParseReadRequest(Message Source, out CdRomReadRequest Request)
        {
            Request = null;
            if ((Source.Channel != MessageChannel.CdRom) || (Source.Type != CdRomType.ReadRequest))
                return (false);
            if (Source.Payload.Length < ReadRequestSize)
                return (false);

            CdRomReadRequest Parsed = new CdRomReadRequest();
            Parsed.Lba = BinaryPrimitives.ReadUInt32LittleEndian(Source.Payload.AsSpan(0, 4));
            Parsed.Count = BinaryPrimitives.ReadUInt32LittleEndian(Source.Payload.AsSpan(4, 4));
            Parsed.Format = Source.Payload[8];
            Request = Parsed;
            return (true);
        }

        public static Message CreateData(ushort Tag, byte[] Buffer, int Offset, int Length)
        {
            byte[] Payload = new byte[Length];
            Array.Copy(Buffer, Offset, Payload, 0, Length);
            return (new Message(MessageChannel.CdRom, CdRomType.Data, Tag, Payload));
        }

        public static Message CreateComplete(ushort Tag, CdRomStatus Status, uint SectorsDelivered)
        {
            byte[] Payload = new byte[CompleteSize];
            Payload[0] = (byte)Status;
            BinaryPrimitives.WriteUInt32LittleEndian(Payload.AsSpan(4, 4), SectorsDelivered);
            return (new Message(MessageChannel.CdRom, CdRomType.Complete, Tag, Payload));
        }

        public static bool TryParseComplete(Message Source, out CdRomStatus Status, out uint SectorsDelivered)
        {
            Status = CdRomStatus.Failed;
            SectorsDelivered = 0;
            if ((Source.Channel != MessageChannel.CdRom) || (Source.Type != CdRomType.Complete))
                return (false);
            if (Source.Payload.Length < CompleteSize)
                return (false);

            Status = (CdRomStatus)Source.Payload[0];
            SectorsDelivered = BinaryPrimitives.ReadUInt32LittleEndian(Source.Payload.AsSpan(4, 4));
            return (true);
        }
    }
}
