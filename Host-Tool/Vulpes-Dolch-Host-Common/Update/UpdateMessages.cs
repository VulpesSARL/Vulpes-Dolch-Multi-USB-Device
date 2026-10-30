using System;
using System.Buffers.Binary;
using VulpesDolch.Host.Common.Protocol;

namespace VulpesDolch.Host.Common.Update
{
    public enum UpdateState : uint
    {
        Normal = 0,
        UpdateMode = 1,
        Receiving = 2,
        Verified = 3,
        Programming = 4,
        Done = 5,
        Failed = 6
    }

    /// <summary>Result codes of the firmware (UPDATE_ERR_* in UpdateCore.h).</summary>
    public enum UpdateError : uint
    {
        None = 0,
        State = 1,
        Length = 2,
        Memory = 3,
        Offset = 4,
        Format = 5,
        Target = 6,
        Signature = 7,
        Decompress = 8,
        Image = 9,
        Eeprom = 10,
        Verify = 11
    }

    /// <summary>The STATE message of the device (UPDATE_MSG_STATE).</summary>
    public sealed class UpdateStateInfo
    {
        public UpdateState State { get; }

        /// <summary>Bytes received (Receiving), percent (Programming), or an <see cref="UpdateError"/> code (Failed, or a refused command).</summary>
        public uint Detail { get; }

        public uint Extra { get; }

        public UpdateStateInfo(UpdateState State, uint Detail, uint Extra)
        {
            this.State = State;
            this.Detail = Detail;
            this.Extra = Extra;
        }

        /// <summary>The error code of a failure or of a refused command (the device sets the detail of a refused command to the code).</summary>
        public UpdateError Error
        {
            get
            {
                if ((State == UpdateState.Receiving) || (State == UpdateState.Programming))
                    return (UpdateError.None);
                return ((UpdateError)Detail);
            }
        }

        public static UpdateStateInfo TryParse(Message Reply)
        {
            if (((Reply.Channel != MessageChannel.Update) && (Reply.Channel != MessageChannel.HostUpdate)) || (Reply.Type != UpdateMessages.State) || (Reply.Payload == null) || (Reply.Payload.Length < 12))
                return (null);

            ReadOnlySpan<byte> Raw = Reply.Payload;
            return (new UpdateStateInfo((UpdateState)BinaryPrimitives.ReadUInt32LittleEndian(Raw.Slice(0, 4)),
                BinaryPrimitives.ReadUInt32LittleEndian(Raw.Slice(4, 4)), BinaryPrimitives.ReadUInt32LittleEndian(Raw.Slice(8, 4))));
        }

        public override string ToString()
        {
            return (State + " (detail " + Detail + ", extra " + Extra + ")");
        }
    }

    /// <summary>Message types of the update channel (UPDATE_MSG_* in Client\UPDATE.h).</summary>
    public static class UpdateMessages
    {
        public const byte Enter = 0x01;
        public const byte Begin = 0x02;
        public const byte Data = 0x03;
        public const byte End = 0x04;
        public const byte Commit = 0x05;
        public const byte Restart = 0x06;
        public const byte Abort = 0x07;
        public const byte Query = 0x08;
        public const byte Leave = 0x09;
        public const byte State = 0x81;

        public static Message Create(byte Type)
        {
            return (new Message(MessageChannel.Update, Type, 0, null));
        }

        /// <summary>BEGIN for the Host: the file length and the pass (1 verify, 2 program).</summary>
        public static Message CreateHostBegin(uint FileLength, uint Pass)
        {
            byte[] Payload = new byte[8];
            BinaryPrimitives.WriteUInt32LittleEndian(Payload.AsSpan(0, 4), FileLength);
            BinaryPrimitives.WriteUInt32LittleEndian(Payload.AsSpan(4, 4), Pass);
            return (new Message(MessageChannel.HostUpdate, Begin, 0, Payload));
        }

        public static Message CreateHost(byte Type)
        {
            return (new Message(MessageChannel.HostUpdate, Type, 0, null));
        }

        public static Message CreateHostData(uint Offset, byte[] File, int Start, int Count)
        {
            byte[] Payload = new byte[4 + Count];
            BinaryPrimitives.WriteUInt32LittleEndian(Payload.AsSpan(0, 4), Offset);
            Array.Copy(File, Start, Payload, 4, Count);
            return (new Message(MessageChannel.HostUpdate, Data, 0, Payload));
        }

        public static Message CreateBegin(uint FileLength)
        {
            byte[] Payload = new byte[4];
            BinaryPrimitives.WriteUInt32LittleEndian(Payload, FileLength);
            return (new Message(MessageChannel.Update, Begin, 0, Payload));
        }

        public static Message CreateData(uint Offset, byte[] File, int Start, int Count)
        {
            byte[] Payload = new byte[4 + Count];
            BinaryPrimitives.WriteUInt32LittleEndian(Payload.AsSpan(0, 4), Offset);
            Array.Copy(File, Start, Payload, 4, Count);
            return (new Message(MessageChannel.Update, Data, 0, Payload));
        }
    }
}
