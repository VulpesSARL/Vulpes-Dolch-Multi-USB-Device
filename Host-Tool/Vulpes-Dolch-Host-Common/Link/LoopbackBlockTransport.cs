using System;
using System.Collections.Concurrent;
using System.Threading;

namespace VulpesDolch.Host.Common.Link
{
    /// <summary>
    /// Two <see cref="IBlockTransport"/> ends wired to each other in memory, for tests: what one end writes, the other reads, one block per
    /// transfer. <see cref="OutgoingFilter"/> may change or drop what an end writes.
    /// </summary>
    public sealed class LoopbackBlockTransport : IBlockTransport
    {
        private readonly BlockingCollection<byte[]> Incoming = new BlockingCollection<byte[]>();
        private LoopbackBlockTransport Peer;
        private volatile bool Canceled;
        private int DisposedFlag;

        private LoopbackBlockTransport()
        {
        }

        /// <summary>Called for every block this end writes; return the block to deliver, or null to drop it.</summary>
        public Func<byte[], byte[]> OutgoingFilter { get; set; }

        /// <summary>When true, WriteBlock and ReadBlock throw (a failed transport).</summary>
        public volatile bool Fail;

        public static void CreatePair(out LoopbackBlockTransport First, out LoopbackBlockTransport Second)
        {
            First = new LoopbackBlockTransport();
            Second = new LoopbackBlockTransport();
            First.Peer = Second;
            Second.Peer = First;
        }

        public void WriteBlock(ReadOnlySpan<byte> Block)
        {
            if (Fail == true)
                throw new System.IO.IOException("The loopback transport was told to fail.");
            if (Volatile.Read(ref DisposedFlag) != 0)
                throw new ObjectDisposedException(nameof(LoopbackBlockTransport));
            if (Block.Length > BulkConstants.BufferSize)
                throw new ArgumentException("The block is longer than a transfer.", nameof(Block));

            byte[] Copy = Block.ToArray();
            Func<byte[], byte[]> Filter = OutgoingFilter;
            if (Filter != null)
            {
                Copy = Filter(Copy);
                if (Copy == null)
                    return;
            }

            if (Peer.Incoming.IsAddingCompleted == false)
            {
                try
                {
                    Peer.Incoming.Add(Copy);
                }
                catch (InvalidOperationException)
                {
                }
            }
        }

        public int ReadBlock(byte[] Buffer)
        {
            while (true)
            {
                if (Fail == true)
                    throw new System.IO.IOException("The loopback transport was told to fail.");
                if (Canceled == true)
                    return (-1);

                byte[] Block;
                try
                {
                    if (Incoming.TryTake(out Block, 20) == false)
                        continue;
                }
                catch (ObjectDisposedException)
                {
                    return (-1);
                }

                Block.CopyTo(Buffer, 0);
                return (Block.Length);
            }
        }

        public void CancelRead()
        {
            Canceled = true;
        }

        public void Dispose()
        {
            if (Interlocked.Exchange(ref DisposedFlag, 1) != 0)
                return;

            Canceled = true;
            Incoming.CompleteAdding();
        }
    }
}
