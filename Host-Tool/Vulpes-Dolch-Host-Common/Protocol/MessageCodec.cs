using System;
using System.Buffers.Binary;

namespace VulpesDolch.Host.Common.Protocol
{
    public enum MessageDecodeResult
    {
        Ok,

        /// <summary>Shorter than the smallest possible block (header + check word).</summary>
        TooShort,

        /// <summary>The length field is above the limit or does not match the size of the block.</summary>
        BadLength,

        BadCheck
    }

    /// <summary>
    /// Turns messages into message blocks and back: 8 byte header, payload padded to a whole
    /// number of 32 bit words, check word over header and padded payload. Everything little-endian.
    /// </summary>
    public static class MessageCodec
    {
        public const int HeaderSize = 8;
        /// <summary>No check word: USB protects its hops with CRC16 and the GPIF hop showed no error in soak tests.</summary>
        public const int CheckSize = 0;

        /// <summary>One message block is at most one 16 KB lane buffer of the boards: 8 byte header + 16352 bytes of payload.</summary>
        public const int MaxPayload = 16352;
        public const int MaxBlockSize = HeaderSize + MaxPayload + CheckSize;

        public static int PaddedLength(int Length)
        {
            return ((Length + 3) & ~3);
        }

        public static int BlockSize(int PayloadLength)
        {
            return (HeaderSize + PaddedLength(PayloadLength) + CheckSize);
        }

        public static byte[] Encode(Message Source)
        {
            if (Source == null)
                throw new ArgumentNullException("Source");
            if (Source.Payload.Length > MaxPayload)
                throw new ArgumentException("The payload is longer than " + MaxPayload + " bytes.", "Source");

            byte[] Block = new byte[BlockSize(Source.Payload.Length)];
            Block[0] = (byte)Source.Channel;
            Block[1] = Source.Type;
            BinaryPrimitives.WriteUInt16LittleEndian(Block.AsSpan(2, 2), Source.Tag);
            BinaryPrimitives.WriteUInt32LittleEndian(Block.AsSpan(4, 4), (uint)Source.Payload.Length);
            Source.Payload.CopyTo(Block, HeaderSize);

            return (Block);
        }

        /// <summary>Checks and unpacks one complete block. Result is null unless the return value is Ok.</summary>
        public static MessageDecodeResult Decode(ReadOnlySpan<byte> Block, out Message Result)
        {
            Result = null;
            if (Block.Length < HeaderSize + CheckSize)
                return (MessageDecodeResult.TooShort);

            uint Length = BinaryPrimitives.ReadUInt32LittleEndian(Block.Slice(4, 4));
            if (Length > MaxPayload)
                return (MessageDecodeResult.BadLength);
            if (Block.Length != BlockSize((int)Length))
                return (MessageDecodeResult.BadLength);

            Result = new Message((MessageChannel)Block[0], Block[1], BinaryPrimitives.ReadUInt16LittleEndian(Block.Slice(2, 2)), Block.Slice(HeaderSize, (int)Length).ToArray());
            return (MessageDecodeResult.Ok);
        }
    }
}
