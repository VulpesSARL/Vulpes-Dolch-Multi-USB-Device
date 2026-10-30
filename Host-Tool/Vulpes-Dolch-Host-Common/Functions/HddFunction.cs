using System;
using System.Collections.Concurrent;
using System.Collections.Generic;
using System.Diagnostics;
using System.Threading;
using VulpesDolch.Host.Common.Link;
using VulpesDolch.Host.Common.Media;
using VulpesDolch.Host.Common.Protocol;

namespace VulpesDolch.Host.Common.Functions
{
    /// <summary>
    /// The PC side of the Client's HDD: attaches and detaches a disk image, reports the drive's state
    /// changes (the OS ejected the disk, the OS locked it) and does every read, write, flush and unmap the Client asks for, straight on
    /// the image - the devices keep no sector cache and no unwritten data.
    /// <para>
    /// A write is confirmed (COMPLETE with status OK) only after every byte of it is in the image, so the Client can acknowledge the
    /// OS's WRITE only for data that is safe on the PC. When the OS ejects the disk, or the application detaches it, the image is
    /// flushed and closed on this side before the event is raised: the file is free afterwards.
    /// </para>
    /// <para>
    /// Create it before the link is started (the handler of the channel has to be registered first), then call
    /// <see cref="Attach"/> once the Client is running. All requests are served in order on a worker thread of their own, so a slow
    /// disk never holds up the state messages.
    /// </para>
    /// </summary>
    public sealed class HddFunction : IDisposable
    {
        private const int SectorSize = HddMedia.SectorSize;

        private readonly ILinkLayer LinkLayer;
        private byte[] BulkBuffer;

        /// <summary>Set it to let big reads travel over the dedicated bulk data lane (null = everything as DATA messages, the Client is then never told to expect bulk).</summary>
        public IBulkDataWriter BulkWriter { get; set; }

        private const int BulkBufferBytes = 16384;
        private const int MaxBulkBytes = 16 * 1024 * 1024;
        private readonly object MediaLock = new object();
        private readonly BlockingCollection<Message> RequestQueue = new BlockingCollection<Message>(512);
        private readonly CancellationTokenSource Stop = new CancellationTokenSource();
        private readonly HashSet<ushort> CancelledTags = new HashSet<ushort>();
        private readonly object ReplyLock = new object();
        private readonly Thread Worker;

        private HddMedia Media;
        private HddDiskState LastState;
        private ushort ReplyTag;
        private readonly ManualResetEventSlim NameArrived = new ManualResetEventSlim(false);
        private volatile string LastName = "";
        private bool ReplyArrived;
        private HddDiskState ReplyState;
        private int NextTag = 0;
        private int DisposedFlag;
        private WriteProgress CurrentWrite;
        private readonly object StatsLock = new object();
        private HddStats Stats = new HddStats();
        private long StatsStart = Stopwatch.GetTimestamp();
        private long EmptySinceTick = 0;

        /// <summary>
        /// How long the drive has to be seen empty before the next disk is presented (milliseconds). The OS only polls a drive every few
        /// seconds; a new disk that appears faster than that is not noticed as a change. 0 switches the wait off.
        /// </summary>
        public int MediaChangeSettleMs { get; set; } = 5000;

        /// <summary>A write that is being received: the Client sends WRITE_REQUEST, then WRITE_DATA pieces of the same tag.</summary>
        private sealed class WriteProgress
        {
            public ushort Tag;
            public HddMedia Target;
            public UInt64 Lba;
            public uint Sectors;
            public long BytesReceived;
        }

        /// <summary>Every DISK_STATE the Client sends (the answers to Attach and Detach included).</summary>
        public event Action<HddDiskState> StateReceived;

        /// <summary>The OS ejected the disk (not locked): the image is flushed and closed; the application plays its eject sound and marks the drive free.</summary>
        public event Action EjectedByOs;

        /// <summary>The OS wanted to eject but holds the lock; the application may offer a forced detach.</summary>
        public event Action EjectRefused;

        /// <summary>A request was answered: kind (Read, Write, Flush, Unmap), first LBA, sector count, status.</summary>
        public event Action<string, UInt64, uint, HddStatus> RequestServed;

        public HddFunction(ILinkLayer LinkLayer)
        {
            if (LinkLayer == null)
                throw new ArgumentNullException("LinkLayer");

            this.LinkLayer = LinkLayer;
            LinkLayer.SetHandler(MessageChannel.Hdd, OnMessage);

            Worker = new Thread(WorkerMain);
            Worker.Name = "Vulpes-Dolch HDD requests";
            Worker.IsBackground = true;
            Worker.Start();
        }

        /// <summary>The disk in the drive (as far as the PC knows); null when empty.</summary>
        public HddMedia CurrentMedia
        {
            get
            {
                lock (MediaLock)
                    return (Media);
            }
        }

        /// <summary>The last DISK_STATE received; null before the first.</summary>
        public HddDiskState LastDiskState
        {
            get
            {
                lock (ReplyLock)
                    return (LastState);
            }
        }

        /// <summary>
        /// Puts a disk into the drive. Takes ownership of the media (flushed and closed when detached or replaced). Returns true when the
        /// Client answered that the disk is present. With <paramref name="Reattach"/> the Client keeps the disk it already holds and the OS
        /// sees nothing.
        /// </summary>
        public bool Attach(HddMedia NewMedia, bool Reattach, int TimeoutMs)
        {
            if (NewMedia == null)
                throw new ArgumentNullException("NewMedia");

            if (Reattach == false)
                WaitForSettle();

            HddMedia Old;
            lock (MediaLock)
            {
                Old = Media;
                Media = NewMedia;
            }
            if ((Old != null) && (Old != NewMedia))
                Old.Dispose();

            HddDiskState Answer;
            if (SendAndWait(HddMessages.CreateDiskAttach(AllocateTag(), NewMedia.CreateAttachInfo(Reattach)), TimeoutMs, out Answer) == false)
                return (false);

            bool Present = ((Answer.Present == true) && (Answer.MediaId == NewMedia.MediaId));
            if (Present == true)
                SendName(NewMedia.ImagePath);
            return (Present);
        }

        /// <summary>Tells the Client the image path (kept in its RAM only). Best effort; paths over 128 bytes are not sent.</summary>
        private void SendName(string Path)
        {
            Message Named = HddMessages.CreateSetName(AllocateTag(), Path);
            if (Named != null)
                LinkLayer.Send(Named, 1000);
        }

        /// <summary>Asks the Client for the name of the disk it holds: the path, "" when it has none, null when it does not answer.</summary>
        public string RequestName(int TimeoutMs)
        {
            NameArrived.Reset();
            if (LinkLayer.Send(HddMessages.CreateNameRequest(AllocateTag()), TimeoutMs) != LinkSendResult.Ok)
                return (null);
            if (NameArrived.Wait(TimeoutMs) == false)
                return (null);
            return (LastName);
        }

        /// <summary>
        /// Takes the disk out. Returns true when the Client confirmed the drive is empty (the image is then flushed and closed); false when
        /// the OS holds a lock (and <paramref name="Force"/> is not set) or the Client did not answer.
        /// </summary>
        public bool Detach(bool Force, int TimeoutMs)
        {
            HddDiskState Answer;
            if (SendAndWait(HddMessages.CreateDiskDetach(AllocateTag(), Force), TimeoutMs, out Answer) == false)
                return (false);
            if (Answer.Present == true)
                return (false);

            CloseMedia();
            return (true);
        }

        /// <summary>Returns the timing sums of the requests served since the last call (or since the start) and starts a new window.</summary>
        public HddStats TakeStats()
        {
            HddStats Taken;
            long Now = Stopwatch.GetTimestamp();

            lock (StatsLock)
            {
                Taken = Stats;
                Taken.WindowMs = (Now - StatsStart) * 1000.0 / Stopwatch.Frequency;
                Stats = new HddStats();
                StatsStart = Now;
            }

            return (Taken);
        }

        public void Dispose()
        {
            if (Interlocked.Exchange(ref DisposedFlag, 1) != 0)
                return;

            Stop.Cancel();
            RequestQueue.CompleteAdding();
            if (Worker != Thread.CurrentThread)
                Worker.Join(2000);
            CloseMedia();
            RequestQueue.Dispose();
            Stop.Dispose();
        }

        /// <summary>Waits until the drive has been reported empty for <see cref="MediaChangeSettleMs"/>, so the OS notices the removal before the next disk.</summary>
        private void WaitForSettle()
        {
            long Since;
            lock (ReplyLock)
                Since = EmptySinceTick;
            if ((Since == 0) || (MediaChangeSettleMs <= 0))
                return;

            long Left = (Since + MediaChangeSettleMs) - Environment.TickCount64;
            if (Left > 0)
                Thread.Sleep((int)Math.Min(Left, MediaChangeSettleMs));
        }

        private ushort AllocateTag()
        {
            return ((ushort)Interlocked.Increment(ref NextTag));
        }

        /// <summary>Closes the image without talking to the Client (a load that failed must not leave the file locked).</summary>
        public void ReleaseMedia()
        {
            CloseMedia();
        }

        /// <summary>Flushes and closes the image.</summary>
        private void CloseMedia()
        {
            HddMedia Old;
            lock (MediaLock)
            {
                Old = Media;
                Media = null;
            }
            if (Old != null)
            {
                try
                {
                    if (Old.ReadOnly == false)
                        Old.Flush(true);
                }
                catch (HddException)
                {
                    // Dispose flushes once more and reports nothing; the Client has seen COMPLETE only for what was written.
                }
                Old.Dispose();
            }
        }

        private bool SendAndWait(Message Request, int TimeoutMs, out HddDiskState Answer)
        {
            Answer = null;
            lock (ReplyLock)
            {
                ReplyTag = Request.Tag;
                ReplyArrived = false;
                ReplyState = null;
            }

            if (LinkLayer.Send(Request, TimeoutMs) != LinkSendResult.Ok)
                return (false);

            long Deadline = Environment.TickCount64 + TimeoutMs;
            lock (ReplyLock)
            {
                while (ReplyArrived == false)
                {
                    if (LinkLayer.IsClosed == true)
                        return (false);

                    long Left = Deadline - Environment.TickCount64;
                    if (Left <= 0)
                        return (false);
                    Monitor.Wait(ReplyLock, (int)Math.Min(Left, 100));
                }

                Answer = ReplyState;
                return (true);
            }
        }

        /// <summary>Runs on the dispatch thread of the link: must not do any slow work.</summary>
        private void OnMessage(Message Received)
        {
            if (Received.Type == HddType.Name)
            {
                LastName = System.Text.Encoding.UTF8.GetString(Received.Payload, 0, Received.Payload.Length);
                NameArrived.Set();
                return;
            }

            if (Received.Type == HddType.DiskState)
            {
                HddDiskState State;
                if (HddMessages.TryParseDiskState(Received, out State) == false)
                    return;

                OnDiskState(Received.Tag, State);
                return;
            }

            if (Received.Type == HddType.Cancel)
            {
                lock (CancelledTags)
                    CancelledTags.Add(Received.Tag);
                return;
            }

            if ((Received.Type == HddType.ReadRequest) || (Received.Type == HddType.WriteRequest) || (Received.Type == HddType.WriteData) ||
                (Received.Type == HddType.FlushRequest) || (Received.Type == HddType.UnmapRequest))
            {
                Received.ArrivedAt = Stopwatch.GetTimestamp();

                // Writes must never lose a piece: wait a little for room in the queue (the link has credits for it), then give up on the request
                // (the Client then runs into its own timeout and fails the OS command - nothing is acknowledged that was not written).
                if (RequestQueue.IsAddingCompleted == false)
                {
                    try
                    {
                        RequestQueue.TryAdd(Received, 5000, Stop.Token);
                    }
                    catch (OperationCanceledException)
                    {
                    }
                    catch (InvalidOperationException)
                    {
                    }
                }
                return;
            }
        }

        private void OnDiskState(ushort Tag, HddDiskState State)
        {
            lock (ReplyLock)
            {
                if ((LastState != null) && (LastState.Present == true) && (State.Present == false))
                    EmptySinceTick = Environment.TickCount64;
                else if (State.Present == true)
                    EmptySinceTick = 0;
                LastState = State;
                if (Tag == ReplyTag)
                {
                    ReplyArrived = true;
                    ReplyState = State;
                    Monitor.PulseAll(ReplyLock);
                }
            }

            // The OS ejected: flush and close before anybody hears about it, so the file is free when the event fires.
            if ((State.Event == HddEvent.EjectedByOs) && (State.Present == false))
                CloseMedia();

            Action<HddDiskState> StateCopy = StateReceived;
            if (StateCopy != null)
                StateCopy(State);

            if (State.Event == HddEvent.EjectedByOs)
            {
                Action EjectedCopy = EjectedByOs;
                if (EjectedCopy != null)
                    EjectedCopy();
            }
            else if (State.Event == HddEvent.EjectRefused)
            {
                Action RefusedCopy = EjectRefused;
                if (RefusedCopy != null)
                    RefusedCopy();
            }
        }

        private void WorkerMain()
        {
            try
            {
                foreach (Message Request in RequestQueue.GetConsumingEnumerable(Stop.Token))
                {
                    try
                    {
                        Serve(Request);
                    }
                    catch (Exception Problem) when ((Problem is InvalidOperationException) || (Problem is ObjectDisposedException))
                    {
                        // The link or the image went away while a request was being answered; the Client's 60 s timeout covers it.
                        CurrentWrite = null;
                    }
                }
            }
            catch (OperationCanceledException)
            {
            }
        }

        private void Serve(Message Request)
        {
            if (Request.ArrivedAt != 0)
            {
                long Waited = Stopwatch.GetTimestamp() - Request.ArrivedAt;
                lock (StatsLock)
                {
                    Stats.QueueTicks += Waited;
                    Stats.QueueSamples++;
                }
            }

            // A new request means the Client is done with the former one (it runs one at a time).
            if ((Request.Type != HddType.WriteData) && (CurrentWrite != null))
                CurrentWrite = null;

            switch (Request.Type)
            {
                case HddType.ReadRequest:
                    ServeRead(Request);
                    break;

                case HddType.WriteRequest:
                    BeginWrite(Request);
                    break;

                case HddType.WriteData:
                    ContinueWrite(Request);
                    break;

                case HddType.FlushRequest:
                    ServeFlush(Request);
                    break;

                case HddType.UnmapRequest:
                    ServeUnmap(Request);
                    break;
            }
        }

        /// <summary>Reads Bytes bytes (a multiple of 16384) and writes them to the bulk data pipe, after telling the Host with BULK_ARM so that it is ready while the disk is read.</summary>
        private HddStatus ServeBulk(ushort Tag, HddMedia Current, UInt64 Lba, int Bytes, IBulkDataWriter Writer, ref long DiskTicks, ref long SendTicks)
        {
            if ((BulkBuffer == null) || (BulkBuffer.Length < Bytes))
                BulkBuffer = new byte[Bytes];

            if (LinkLayer.Send(ControlMessages.CreateBulkArm(Tag, (uint)Bytes), 10000) != LinkSendResult.Ok)
                return (HddStatus.Failed);

            long DiskStarted = Stopwatch.GetTimestamp();
            try
            {
                Current.Read(Lba, BulkBuffer, 0, Bytes);
            }
            catch (HddException Problem)
            {
                return (MapError(Problem.Kind));
            }
            catch (ObjectDisposedException)
            {
                return (HddStatus.NoMedium);
            }
            DiskTicks += Stopwatch.GetTimestamp() - DiskStarted;

            long SendStarted = Stopwatch.GetTimestamp();
            if (Writer.WriteBulk(BulkBuffer, Bytes) == false)
                return (HddStatus.Failed);
            SendTicks += Stopwatch.GetTimestamp() - SendStarted;
            return (HddStatus.Ok);
        }

        /// <summary>Answers one READ_REQUEST: DATA messages in whole sectors, then COMPLETE.</summary>
        private void ServeRead(Message Request)
        {
            HddRangeRequest Read;
            if (HddMessages.TryParseRangeRequest(Request, HddType.ReadRequest, out Read) == false)
            {
                SendComplete(Request.Tag, HddStatus.Failed, 0);
                return;
            }

            HddMedia Current = CurrentMedia;
            if (Current == null)
            {
                SendComplete(Request.Tag, HddStatus.NoMedium, 0);
                RaiseServed("Read", Read, HddStatus.NoMedium);
                return;
            }

            int SectorsPerMessage = HddMessages.DataChunkLimit / SectorSize;
            byte[] Chunk = new byte[SectorsPerMessage * SectorSize];
            HddStatus Status = HddStatus.Ok;
            uint Delivered = 0;
            long ReadStarted = Stopwatch.GetTimestamp();
            long DiskTicks = 0;
            long SendTicks = 0;

            // The whole 16 KB buffers of a big read come over the dedicated bulk data lane (the Client has already switched its USB side for them).
            if (Read.BulkBuffers != 0)
            {
                long BulkBytes = (long)Read.BulkBuffers * BulkBufferBytes;
                IBulkDataWriter Writer = BulkWriter;

                if ((Writer == null) || (BulkBytes > (long)Read.Count * SectorSize) || (BulkBytes > MaxBulkBytes))
                {
                    SendComplete(Request.Tag, HddStatus.Failed, 0);
                    RaiseServed("Read", Read, HddStatus.Failed);
                    return;
                }

                HddStatus BulkStatus = ServeBulk(Request.Tag, Current, Read.Lba, (int)BulkBytes, Writer, ref DiskTicks, ref SendTicks);
                if (BulkStatus != HddStatus.Ok)
                {
                    SendComplete(Request.Tag, BulkStatus, 0);
                    RaiseServed("Read", Read, BulkStatus);
                    return;
                }

                Delivered = (uint)(BulkBytes / SectorSize);
            }

            while (Delivered < Read.Count)
            {
                if (WasCancelled(Request.Tag) == true)
                {
                    Status = HddStatus.Cancelled;
                    break;
                }

                int Sectors = (int)Math.Min((uint)SectorsPerMessage, Read.Count - Delivered);
                long DiskStarted = Stopwatch.GetTimestamp();
                try
                {
                    Current.Read(Read.Lba + Delivered, Chunk, 0, Sectors * SectorSize);
                    DiskTicks += Stopwatch.GetTimestamp() - DiskStarted;
                }
                catch (HddException Problem)
                {
                    Status = MapError(Problem.Kind);
                    break;
                }
                catch (ObjectDisposedException)
                {
                    Status = HddStatus.NoMedium;
                    break;
                }

                long SendStarted = Stopwatch.GetTimestamp();
                if (LinkLayer.Send(HddMessages.CreateData(HddType.Data, Request.Tag, Chunk, 0, Sectors * SectorSize), 10000) != LinkSendResult.Ok)
                    return;
                SendTicks += Stopwatch.GetTimestamp() - SendStarted;
                Delivered += (uint)Sectors;
            }

            SendComplete(Request.Tag, Status, Delivered);
            if (Status == HddStatus.Ok)
            {
                lock (StatsLock)
                {
                    Stats.ReadRequestCount++;
                    Stats.ReadSectorCount += Delivered;
                    Stats.ReadDiskTicks += DiskTicks;
                    Stats.ReadSendTicks += SendTicks;
                    Stats.ReadTotalTicks += Stopwatch.GetTimestamp() - ReadStarted;
                }
            }
            RaiseServed("Read", Read, Status);
        }

        /// <summary>WRITE_REQUEST: remembers the write; the bytes arrive in WRITE_DATA messages.</summary>
        private void BeginWrite(Message Request)
        {
            HddRangeRequest Write;
            if (HddMessages.TryParseRangeRequest(Request, HddType.WriteRequest, out Write) == false)
            {
                SendComplete(Request.Tag, HddStatus.Failed, 0);
                return;
            }

            HddMedia Current = CurrentMedia;
            if (Current == null)
            {
                SendComplete(Request.Tag, HddStatus.NoMedium, 0);
                RaiseServed("Write", Write, HddStatus.NoMedium);
                return;
            }

            // Belt and braces: the Client never asks for a write to a read-only disk.
            if (Current.ReadOnly == true)
            {
                SendComplete(Request.Tag, HddStatus.WriteError, 0);
                RaiseServed("Write", Write, HddStatus.WriteError);
                return;
            }

            if ((Write.Count == 0) || (Write.Lba >= Current.SectorCount) || (Write.Count > (Current.SectorCount - Write.Lba)))
            {
                SendComplete(Request.Tag, HddStatus.OutOfRange, 0);
                RaiseServed("Write", Write, HddStatus.OutOfRange);
                return;
            }

            WriteProgress Progress = new WriteProgress();
            Progress.Tag = Request.Tag;
            Progress.Target = Current;
            Progress.Lba = Write.Lba;
            Progress.Sectors = Write.Count;
            CurrentWrite = Progress;
        }

        /// <summary>WRITE_DATA: writes the piece; after the last one flushes and confirms the whole write.</summary>
        private void ContinueWrite(Message Piece)
        {
            WriteProgress Progress = CurrentWrite;
            if ((Progress == null) || (Progress.Tag != Piece.Tag))
                return;     // late data of a write that already failed or was given up

            HddRangeRequest Range = new HddRangeRequest();
            Range.Lba = Progress.Lba;
            Range.Count = Progress.Sectors;

            long Total = (long)Progress.Sectors * SectorSize;
            int Length = Piece.Payload.Length;
            if (((Length % SectorSize) != 0) || (Length == 0) || ((Progress.BytesReceived + Length) > Total))
            {
                CurrentWrite = null;
                SendComplete(Piece.Tag, HddStatus.WriteError, (uint)(Progress.BytesReceived / SectorSize));
                RaiseServed("Write", Range, HddStatus.WriteError);
                return;
            }

            long DiskStarted = Stopwatch.GetTimestamp();
            try
            {
                Progress.Target.Write(Progress.Lba + (UInt64)(Progress.BytesReceived / SectorSize), Piece.Payload, 0, Length);
                Progress.BytesReceived += Length;
                if (Progress.BytesReceived == Total)
                    Progress.Target.Flush(false);
                long DiskTicks = Stopwatch.GetTimestamp() - DiskStarted;
                lock (StatsLock)
                {
                    Stats.WriteDiskTicks += DiskTicks;
                    Stats.WritePieceCount++;
                }
            }
            catch (HddException Problem)
            {
                CurrentWrite = null;
                HddStatus Failure = (Problem.Kind == HddErrorKind.OutOfRange) ? HddStatus.OutOfRange : HddStatus.WriteError;
                SendComplete(Piece.Tag, Failure, (uint)(Progress.BytesReceived / SectorSize));
                RaiseServed("Write", Range, Failure);
                return;
            }
            catch (ObjectDisposedException)
            {
                CurrentWrite = null;
                SendComplete(Piece.Tag, HddStatus.NoMedium, 0);
                RaiseServed("Write", Range, HddStatus.NoMedium);
                return;
            }

            if (Progress.BytesReceived == Total)
            {
                CurrentWrite = null;
                long CompleteStarted = Stopwatch.GetTimestamp();
                SendComplete(Piece.Tag, HddStatus.Ok, Progress.Sectors);
                long CompleteTicks = Stopwatch.GetTimestamp() - CompleteStarted;
                lock (StatsLock)
                {
                    Stats.WriteRequestCount++;
                    Stats.WriteSectorCount += Progress.Sectors;
                    Stats.WriteCompleteTicks += CompleteTicks;
                }
                RaiseServed("Write", Range, HddStatus.Ok);
            }
        }

        /// <summary>FLUSH_REQUEST (SYNCHRONIZE CACHE): pushes everything down to the storage under the image.</summary>
        private void ServeFlush(Message Request)
        {
            HddRangeRequest Range = new HddRangeRequest();
            HddMedia Current = CurrentMedia;
            HddStatus Status = HddStatus.Ok;

            if (Current == null)
                Status = HddStatus.NoMedium;
            else if (Current.ReadOnly == false)
            {
                try
                {
                    Current.Flush(true);
                }
                catch (HddException)
                {
                    Status = HddStatus.WriteError;
                }
                catch (ObjectDisposedException)
                {
                    Status = HddStatus.NoMedium;
                }
            }

            SendComplete(Request.Tag, Status, 0);
            RaiseServed("Flush", Range, Status);
        }

        /// <summary>UNMAP_REQUEST: the range reads back as zeros afterwards (see <see cref="HddMedia.Release"/>).</summary>
        private void ServeUnmap(Message Request)
        {
            HddRangeRequest Range;
            if (HddMessages.TryParseRangeRequest(Request, HddType.UnmapRequest, out Range) == false)
            {
                SendComplete(Request.Tag, HddStatus.Failed, 0);
                return;
            }

            HddMedia Current = CurrentMedia;
            HddStatus Status = HddStatus.Ok;
            if (Current == null)
                Status = HddStatus.NoMedium;
            else if (Current.ReadOnly == true)
                Status = HddStatus.WriteError;
            else
            {
                try
                {
                    Current.Release(Range.Lba, Range.Count);
                    Current.Flush(false);
                }
                catch (HddException Problem)
                {
                    Status = (Problem.Kind == HddErrorKind.OutOfRange) ? HddStatus.OutOfRange : HddStatus.WriteError;
                }
                catch (ObjectDisposedException)
                {
                    Status = HddStatus.NoMedium;
                }
            }

            SendComplete(Request.Tag, Status, (Status == HddStatus.Ok) ? Range.Count : 0);
            RaiseServed("Unmap", Range, Status);
        }

        private void SendComplete(ushort Tag, HddStatus Status, uint Done)
        {
            LinkLayer.Send(HddMessages.CreateComplete(Tag, Status, Done), 10000);
            lock (CancelledTags)
                CancelledTags.Remove(Tag);
        }

        private bool WasCancelled(ushort Tag)
        {
            lock (CancelledTags)
                return (CancelledTags.Contains(Tag));
        }

        private void RaiseServed(string Kind, HddRangeRequest Range, HddStatus Status)
        {
            Action<string, UInt64, uint, HddStatus> Copy = RequestServed;
            if (Copy != null)
                Copy(Kind, Range.Lba, Range.Count, Status);
        }

        private static HddStatus MapError(HddErrorKind Kind)
        {
            switch (Kind)
            {
                case HddErrorKind.OutOfRange:
                    return (HddStatus.OutOfRange);
                case HddErrorKind.ReadError:
                    return (HddStatus.ReadError);
                case HddErrorKind.WriteError:
                    return (HddStatus.WriteError);
                default:
                    return (HddStatus.Failed);
            }
        }
    }
}
