using System;
using System.Buffers.Binary;
using System.Collections.Concurrent;
using System.Threading;
using VulpesDolch.Host.Common.Protocol;

namespace VulpesDolch.Host.Common.Link
{
    /// <summary>
    /// The link layer over bulk transfers (<see cref="IBlockTransport"/>, normally WinUSB): one
    /// message block is one USB transfer, so there are no sequence numbers, credits or fragments - USB retries damaged packets and
    /// stalls the sender when the other side is not ready. The session checks the message check word of every block and hands each
    /// complete message to the handler of its channel.
    /// <para>
    /// Threads: one reader thread takes transfers from the transport and never blocks on handlers; one dispatch thread calls the
    /// handlers in arrival order, so a handler may call <see cref="Send"/>.
    /// </para>
    /// </summary>
    public sealed class BulkLinkSession : ILinkLayer
    {
        private const int WaitSliceMs = 100;
        private const int DispatchQueueSize = 256;

        private readonly IBlockTransport Transport;
        private readonly Action<Message>[] Handlers = new Action<Message>[256];
        private readonly BlockingCollection<Message> DispatchQueue = new BlockingCollection<Message>(DispatchQueueSize);
        private readonly CancellationTokenSource Cancel = new CancellationTokenSource();
        private readonly object WriteLock = new object();

        private Thread ReaderThread;
        private Thread DispatchThread;
        private bool Started;
        private int ClosedFlag;
        private int DisposedFlag;
        private volatile bool Stopping;
        private long LastSendTicks;
        private long LastReceiveTicks;
        private long SentCount;
        private long ReceivedCount;

        /// <summary>Raised on any thread; keep it short.</summary>
        public event Action<LinkProblemKind, string> Problem;

        /// <summary>Raised once when the session closes because the transport failed.</summary>
        public event Action Closed;

        /// <param name="Transport">Owned by the session: disposed with it.</param>
        public BulkLinkSession(IBlockTransport Transport)
        {
            if (Transport == null)
                throw new ArgumentNullException("Transport");

            this.Transport = Transport;
        }

        /// <summary>The dedicated bulk data pipe of the device, or null when the transport has none.</summary>
        public IBulkDataWriter BulkWriter
        {
            get
            {
                IBulkDataWriter Writer = Transport as IBulkDataWriter;
                return ((Writer != null) && (Writer.IsAvailable == true) ? Writer : null);
            }
        }

        /// <summary>Announced in HELLO for the credit field, which the bulk link does not use.</summary>
        public int ReceiveCapacity
        {
            get { return (1); }
        }

        public bool IsClosed
        {
            get { return (Volatile.Read(ref ClosedFlag) != 0); }
        }

        /// <summary>Transfers written to the transport.</summary>
        public long ReportsSent
        {
            get { return (Interlocked.Read(ref SentCount)); }
        }

        /// <summary>Non-empty transfers read from the transport.</summary>
        public long ReportsReceived
        {
            get { return (Interlocked.Read(ref ReceivedCount)); }
        }

        public long MillisecondsSinceLastSend
        {
            get { return (Environment.TickCount64 - Interlocked.Read(ref LastSendTicks)); }
        }

        public long MillisecondsSinceLastReceive
        {
            get { return (Environment.TickCount64 - Interlocked.Read(ref LastReceiveTicks)); }
        }

        public void SetHandler(MessageChannel Channel, Action<Message> Handler)
        {
            Handlers[(byte)Channel] = Handler;
        }

        public void Start()
        {
            if (Started == true)
                throw new InvalidOperationException("The session is already started.");

            Started = true;
            long Now = Environment.TickCount64;
            Interlocked.Exchange(ref LastSendTicks, Now);
            Interlocked.Exchange(ref LastReceiveTicks, Now);

            ReaderThread = new Thread(ReaderMain);
            ReaderThread.Name = "Vulpes-Dolch bulk link reader";
            ReaderThread.IsBackground = true;
            DispatchThread = new Thread(DispatchMain);
            DispatchThread.Name = "Vulpes-Dolch bulk link dispatch";
            DispatchThread.IsBackground = true;
            ReaderThread.Start();
            DispatchThread.Start();
        }

        /// <summary>
        /// Sends one message as one transfer. TimeoutMs is how long to wait for other senders (-1 = without limit); the transfer itself
        /// waits as long as the Host device keeps its buffers full.
        /// </summary>
        public LinkSendResult Send(Message Outgoing, int TimeoutMs)
        {
            if (Outgoing == null)
                throw new ArgumentNullException("Outgoing");
            if (Outgoing.Payload.Length > MessageCodec.MaxPayload)
                return (LinkSendResult.TooLarge);
            if (IsClosed == true)
                return (LinkSendResult.Closed);

            byte[] Block = MessageCodec.Encode(Outgoing);
            byte[] Transfer = Block;
            if (BulkConstants.TransferLength(Block.Length) != Block.Length)
            {
                Transfer = new byte[BulkConstants.TransferLength(Block.Length)];
                Block.CopyTo(Transfer, 0);
            }

            long Deadline = (TimeoutMs < 0) ? long.MaxValue : Environment.TickCount64 + TimeoutMs;
            while (Monitor.TryEnter(WriteLock, WaitSliceMs) == false)
            {
                if (IsClosed == true)
                    return (LinkSendResult.Closed);
                if (Environment.TickCount64 >= Deadline)
                    return (LinkSendResult.Timeout);
            }

            try
            {
                if (IsClosed == true)
                    return (LinkSendResult.Closed);

                try
                {
                    Transport.WriteBlock(Transfer);
                }
                catch (Exception Failure)
                {
                    Fail(Failure);
                    return (LinkSendResult.Closed);
                }

                Interlocked.Exchange(ref LastSendTicks, Environment.TickCount64);
                Interlocked.Increment(ref SentCount);
                return (LinkSendResult.Ok);
            }
            finally
            {
                Monitor.Exit(WriteLock);
            }
        }

        public void Dispose()
        {
            if (Interlocked.Exchange(ref DisposedFlag, 1) != 0)
                return;

            Stopping = true;
            Interlocked.Exchange(ref ClosedFlag, 1);
            Cancel.Cancel();
            try
            {
                Transport.CancelRead();
            }
            catch (Exception)
            {
            }

            JoinThread(ReaderThread);
            JoinThread(DispatchThread);
            Transport.Dispose();
        }

        private static void JoinThread(Thread Worker)
        {
            if ((Worker != null) && (Worker != Thread.CurrentThread))
                Worker.Join(2000);
        }

        private void Fail(Exception Failure)
        {
            if (Interlocked.Exchange(ref ClosedFlag, 1) != 0)
                return;

            RaiseProblem(LinkProblemKind.TransportFailed, Failure.Message);

            Action Copy = Closed;
            if (Copy != null)
                Copy();
        }

        private void RaiseProblem(LinkProblemKind Kind, string Text)
        {
            Action<LinkProblemKind, string> Copy = Problem;
            if (Copy != null)
                Copy(Kind, Text);
        }

        private void ReaderMain()
        {
            byte[] Buffer = new byte[BulkConstants.BufferSize];

            while (Stopping == false)
            {
                int Got;
                try
                {
                    Got = Transport.ReadBlock(Buffer);
                }
                catch (Exception Failure)
                {
                    if (Stopping == false)
                        Fail(Failure);
                    return;
                }

                if (Got < 0)
                    return;
                if (Got == 0)
                    continue;

                Interlocked.Exchange(ref LastReceiveTicks, Environment.TickCount64);
                Interlocked.Increment(ref ReceivedCount);
                DeliverBlock(Buffer, Got);
            }
        }

        /// <summary>One transfer arrived: the message block, possibly followed by the 4 padding bytes.</summary>
        private void DeliverBlock(byte[] Buffer, int Count)
        {
            if (Count < MessageCodec.HeaderSize + MessageCodec.CheckSize)
            {
                RaiseProblem(LinkProblemKind.BadBlock, "A transfer that is too short (" + Count + " bytes) was dropped.");
                return;
            }

            uint Length = BinaryPrimitives.ReadUInt32LittleEndian(Buffer.AsSpan(4, 4));
            if (Length > MessageCodec.MaxPayload)
            {
                RaiseProblem(LinkProblemKind.BadBlock, "A block with an impossible length (" + Length + ") was dropped.");
                return;
            }

            int BlockLength = MessageCodec.BlockSize((int)Length);
            if ((Count < BlockLength) || (Count > BlockLength + BulkConstants.PaddingSize))
            {
                RaiseProblem(LinkProblemKind.BadBlock, "A transfer of " + Count + " bytes does not fit its length field (" + BlockLength + ") and was dropped.");
                return;
            }

            Message Received;
            MessageDecodeResult Result = MessageCodec.Decode(Buffer.AsSpan(0, BlockLength), out Received);
            if (Result == MessageDecodeResult.BadCheck)
            {
                RaiseProblem(LinkProblemKind.BadCheck, "A block failed its check word and was dropped.");
                Send(ControlMessages.CreateError(ErrorCode.CrcMismatch, 0, 0), 0);
                return;
            }

            if (Result != MessageDecodeResult.Ok)
            {
                RaiseProblem(LinkProblemKind.BadBlock, "A block with an inconsistent length (" + Result + ") was dropped.");
                return;
            }

            try
            {
                while (DispatchQueue.TryAdd(Received, WaitSliceMs, Cancel.Token) == false)
                {
                    if (Stopping == true)
                        return;
                }
            }
            catch (OperationCanceledException)
            {
            }
        }

        private void DispatchMain()
        {
            try
            {
                foreach (Message Received in DispatchQueue.GetConsumingEnumerable(Cancel.Token))
                    Dispatch(Received);
            }
            catch (OperationCanceledException)
            {
            }
            catch (ObjectDisposedException)
            {
            }
        }

        private void Dispatch(Message Received)
        {
            Action<Message> Handler = Handlers[(byte)Received.Channel];
            if (Handler == null)
            {
                // Never answer an ERROR with an ERROR: two sessions without handlers would feed each other forever.
                bool IsError = (Received.Channel == MessageChannel.Control) && (Received.Type == ControlType.Error);
                if (IsError == false)
                    Send(ControlMessages.CreateError(ErrorCode.UnknownChannel, Received.Tag, 0), 1000);
                return;
            }

            try
            {
                Handler(Received);
            }
            catch (Exception Failure)
            {
                RaiseProblem(LinkProblemKind.HandlerFailed, Failure.Message);
            }
        }
    }
}
