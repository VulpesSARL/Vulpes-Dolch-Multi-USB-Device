using System;
using System.Buffers.Binary;

namespace VulpesDolch.Host.Common.Protocol
{
    /// <summary>Builds and reads the messages of the control channel.</summary>
    public static class ControlMessages
    {
        /// <summary>Major 0, minor 1 - this draft.</summary>
        public const ushort ProtocolVersion = 0x0001;

        public static Message CreateHello(ushort Tag, HelloInfo Info)
        {
            byte[] Payload = new byte[4];
            BinaryPrimitives.WriteUInt16LittleEndian(Payload.AsSpan(0, 2), Info.ProtocolVersion);
            Payload[2] = Info.ReceiveCredits;
            return (new Message(MessageChannel.Control, ControlType.Hello, Tag, Payload));
        }

        public static bool TryParseHello(Message Source, out HelloInfo Info)
        {
            Info = null;
            if (IsControl(Source, ControlType.Hello, 4) == false)
                return (false);

            Info = new HelloInfo();
            Info.ProtocolVersion = BinaryPrimitives.ReadUInt16LittleEndian(Source.Payload.AsSpan(0, 2));
            Info.ReceiveCredits = Source.Payload[2];
            return (true);
        }

        public static Message CreateHelloAck(ushort Tag, HelloAckInfo Info)
        {
            byte[] Payload = new byte[16];
            BinaryPrimitives.WriteUInt16LittleEndian(Payload.AsSpan(0, 2), Info.ProtocolVersion);
            Payload[2] = Info.ReceiveCredits;
            BinaryPrimitives.WriteUInt32LittleEndian(Payload.AsSpan(4, 4), Info.HostFirmwareVersion);
            BinaryPrimitives.WriteUInt32LittleEndian(Payload.AsSpan(8, 4), Info.ClientFirmwareVersion);
            BinaryPrimitives.WriteUInt32LittleEndian(Payload.AsSpan(12, 4), Info.MaxPayload);
            return (new Message(MessageChannel.Control, ControlType.HelloAck, Tag, Payload));
        }

        public static bool TryParseHelloAck(Message Source, out HelloAckInfo Info)
        {
            Info = null;
            if (IsControl(Source, ControlType.HelloAck, 16) == false)
                return (false);

            Info = new HelloAckInfo();
            Info.ProtocolVersion = BinaryPrimitives.ReadUInt16LittleEndian(Source.Payload.AsSpan(0, 2));
            Info.ReceiveCredits = Source.Payload[2];
            Info.HostFirmwareVersion = BinaryPrimitives.ReadUInt32LittleEndian(Source.Payload.AsSpan(4, 4));
            Info.ClientFirmwareVersion = BinaryPrimitives.ReadUInt32LittleEndian(Source.Payload.AsSpan(8, 4));
            Info.MaxPayload = BinaryPrimitives.ReadUInt32LittleEndian(Source.Payload.AsSpan(12, 4));
            return (true);
        }

        public static Message CreateBye(ushort Tag)
        {
            return (new Message(MessageChannel.Control, ControlType.Bye, Tag, null));
        }

        public static Message CreatePing(ushort Tag, uint Nonce)
        {
            return (CreateNonce(ControlType.Ping, Tag, Nonce));
        }

        public static Message CreatePong(ushort Tag, uint Nonce)
        {
            return (CreateNonce(ControlType.Pong, Tag, Nonce));
        }

        /// <summary>Reads the nonce of a PING or a PONG.</summary>
        public static bool TryParseNonce(Message Source, out uint Nonce)
        {
            Nonce = 0;
            if ((Source.Channel != MessageChannel.Control) || ((Source.Type != ControlType.Ping) && (Source.Type != ControlType.Pong)))
                return (false);
            if (Source.Payload.Length < 4)
                return (false);

            Nonce = BinaryPrimitives.ReadUInt32LittleEndian(Source.Payload.AsSpan(0, 4));
            return (true);
        }

        public static Message CreateStatusRequest(ushort Tag)
        {
            return (new Message(MessageChannel.Control, ControlType.StatusRequest, Tag, null));
        }

        public static Message CreateBulkArm(ushort Tag, uint Bytes)
        {
            byte[] Payload = new byte[4];
            BinaryPrimitives.WriteUInt32LittleEndian(Payload.AsSpan(0, 4), Bytes);
            return (new Message(MessageChannel.Control, ControlType.BulkArm, Tag, Payload));
        }

        public static Message CreateResetClient(ushort Tag)
        {
            return (new Message(MessageChannel.Control, ControlType.ResetClient, Tag, null));
        }

        public static Message CreateDebugResetHost(ushort Tag)
        {
            return (new Message(MessageChannel.Control, ControlType.DebugResetHost, Tag, null));
        }

        public static Message CreateDebugSinkData(ushort Tag, byte[] Payload)
        {
            return (new Message(MessageChannel.Control, ControlType.DebugSinkData, Tag, Payload));
        }

        public static Message CreateDebugFault(ushort Tag, uint Mode, uint Period)
        {
            byte[] Payload = new byte[8];
            BinaryPrimitives.WriteUInt32LittleEndian(Payload.AsSpan(0, 4), Mode);
            BinaryPrimitives.WriteUInt32LittleEndian(Payload.AsSpan(4, 4), Period);
            return (new Message(MessageChannel.Control, ControlType.DebugFault, Tag, Payload));
        }

        public static Message CreateDebugCrash(ushort Tag, uint What)
        {
            byte[] Payload = new byte[4];
            BinaryPrimitives.WriteUInt32LittleEndian(Payload.AsSpan(0, 4), What);
            return (new Message(MessageChannel.Control, ControlType.DebugCrash, Tag, Payload));
        }

        public static Message CreateDebugSinkEnd(ushort Tag)
        {
            return (new Message(MessageChannel.Control, ControlType.DebugSinkEnd, Tag, null));
        }

        public static Message CreateDebugSource(ushort Tag, uint Count, uint Size)
        {
            byte[] Payload = new byte[8];
            BinaryPrimitives.WriteUInt32LittleEndian(Payload.AsSpan(0, 4), Count);
            BinaryPrimitives.WriteUInt32LittleEndian(Payload.AsSpan(4, 4), Size);
            return (new Message(MessageChannel.Control, ControlType.DebugSource, Tag, Payload));
        }

        public static Message CreateStatus(ushort Tag, HostStatus Status)
        {
            byte[] Payload = new byte[HostStatus.PayloadSizeWithFirmware];
            BinaryPrimitives.WriteUInt32LittleEndian(Payload.AsSpan(28, 4), Status.ClientFirmwareVersion);
            Payload[0] = (byte)Status.ClientState;
            Payload[1] = Status.HostUsbSpeed;
            Payload[2] = Status.ClientUsbSpeed;
            Payload[6] = Status.ClientUsbEnabled ? (byte)1 : (byte)0;
            Payload[4] = Status.CdRomPresent ? (byte)1 : (byte)0;
            Payload[5] = Status.CdRomLocked ? (byte)1 : (byte)0;
            BinaryPrimitives.WriteInt64LittleEndian(Payload.AsSpan(8, 8), Status.CdRomMediaId);
            Payload[16] = Status.HddPresent ? (byte)1 : (byte)0;
            Payload[17] = Status.HddReadOnly ? (byte)1 : (byte)0;
            BinaryPrimitives.WriteInt64LittleEndian(Payload.AsSpan(20, 8), Status.HddMediaId);
            return (new Message(MessageChannel.Control, ControlType.Status, Tag, Payload));
        }

        public static bool TryParseStatus(Message Source, out HostStatus Status)
        {
            Status = null;
            if (IsControl(Source, ControlType.Status, HostStatus.PayloadSize) == false)
                return (false);

            Status = new HostStatus();
            Status.ClientState = (ClientState)Source.Payload[0];
            Status.HostUsbSpeed = Source.Payload[1];
            Status.ClientUsbSpeed = Source.Payload[2];
            Status.ClientUsbEnabled = (Source.Payload[6] != 0);
            Status.CdRomPresent = (Source.Payload[4] != 0);
            Status.CdRomLocked = (Source.Payload[5] != 0);
            Status.CdRomMediaId = BinaryPrimitives.ReadInt64LittleEndian(Source.Payload.AsSpan(8, 8));
            Status.HddPresent = (Source.Payload[16] != 0);
            Status.HddReadOnly = (Source.Payload[17] != 0);
            Status.HddMediaId = BinaryPrimitives.ReadInt64LittleEndian(Source.Payload.AsSpan(20, 8));
            if (Source.Payload.Length >= HostStatus.PayloadSizeWithFirmware)
            {
                Status.ClientFirmwareVersion = BinaryPrimitives.ReadUInt32LittleEndian(Source.Payload.AsSpan(28, 4));
                Status.HasClientFirmware = true;
            }
            return (true);
        }

        public static Message CreateError(ushort Tag, ErrorInfo Info)
        {
            byte[] Payload = new byte[ErrorInfo.PayloadSize];
            Payload[0] = (byte)Info.Code;
            BinaryPrimitives.WriteUInt16LittleEndian(Payload.AsSpan(2, 2), Info.RefTag);
            BinaryPrimitives.WriteUInt16LittleEndian(Payload.AsSpan(4, 2), Info.RefSequence);
            return (new Message(MessageChannel.Control, ControlType.Error, Tag, Payload));
        }

        public static Message CreateError(ErrorCode Code, ushort RefTag, ushort RefSequence)
        {
            ErrorInfo Info = new ErrorInfo();
            Info.Code = Code;
            Info.RefTag = RefTag;
            Info.RefSequence = RefSequence;
            return (CreateError(0, Info));
        }

        public static bool TryParseError(Message Source, out ErrorInfo Info)
        {
            Info = null;
            if (IsControl(Source, ControlType.Error, ErrorInfo.PayloadSize) == false)
                return (false);

            Info = new ErrorInfo();
            Info.Code = (ErrorCode)Source.Payload[0];
            Info.RefTag = BinaryPrimitives.ReadUInt16LittleEndian(Source.Payload.AsSpan(2, 2));
            Info.RefSequence = BinaryPrimitives.ReadUInt16LittleEndian(Source.Payload.AsSpan(4, 2));
            return (true);
        }

        private static Message CreateNonce(byte Type, ushort Tag, uint Nonce)
        {
            byte[] Payload = new byte[4];
            BinaryPrimitives.WriteUInt32LittleEndian(Payload, Nonce);
            return (new Message(MessageChannel.Control, Type, Tag, Payload));
        }

        private static bool IsControl(Message Source, byte Type, int MinimumPayload)
        {
            if (Source == null)
                return (false);
            if ((Source.Channel != MessageChannel.Control) || (Source.Type != Type))
                return (false);

            return (Source.Payload.Length >= MinimumPayload);
        }
    }
}
