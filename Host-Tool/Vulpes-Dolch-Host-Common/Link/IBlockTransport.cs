using System;

namespace VulpesDolch.Host.Common.Link
{
    /// <summary>
    /// Moves message blocks between the PC and the Host device over two bulk endpoints: one block is one
    /// USB transfer, ended by a short packet. Read and write may be used from different threads at the same time.
    /// </summary>
    public interface IBlockTransport : IDisposable
    {
        /// <summary>Sends one block (or a block followed by up to 4 padding bytes) as one transfer; returns when it is on the wire. Throws on failure.</summary>
        void WriteBlock(ReadOnlySpan<byte> Block);

        /// <summary>
        /// Waits for one transfer and copies it into Buffer (at least <see cref="BulkConstants.BufferSize"/> bytes).
        /// Returns its length (0 for an empty transfer), or -1 once <see cref="CancelRead"/> or Dispose has been called. Throws on failure.
        /// </summary>
        int ReadBlock(byte[] Buffer);

        /// <summary>Makes a ReadBlock that is waiting (or the next one) return -1.</summary>
        void CancelRead();
    }

    public static class BulkConstants
    {
        /// <summary>The Host device receives and sends transfers of up to this many bytes (its DMA buffer size).</summary>
        public const int BufferSize = 20480;

        /// <summary>
        /// A block whose length is a multiple of this would end exactly on a packet boundary (64 bytes at full speed, 512 at high
        /// speed, 1024 at super speed) and so without the short packet that ends a transfer; 4 zero bytes are appended then.
        /// </summary>
        public const int PacketAlign = 64;

        public const int PaddingSize = 4;

        /// <summary>The length a block has on the wire.</summary>
        public static int TransferLength(int BlockLength)
        {
            return (((BlockLength % PacketAlign) == 0) ? (BlockLength + PaddingSize) : BlockLength);
        }
    }
}
