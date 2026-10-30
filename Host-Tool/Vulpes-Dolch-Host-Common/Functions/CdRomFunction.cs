using System;
using System.Collections.Concurrent;
using System.Collections.Generic;
using System.Threading;
using VulpesDolch.Host.Common.Link;
using VulpesDolch.Host.Common.Media;
using VulpesDolch.Host.Common.Protocol;

namespace VulpesDolch.Host.Common.Functions
{
    /// <summary>
    /// The PC side of the Client's CD-ROM drive: inserts and removes a disc image, reports the
    /// drive's state changes (the OS ejected, the OS locked the tray) and answers every sector read the Client asks for, straight
    /// from the image file - the devices keep no sector cache.
    /// <para>
    /// Create it before the link is started (the handler of the channel has to be registered first), then call
    /// <see cref="Insert"/> once the Client is running. Reads are served on a worker thread of their own, so a slow disk never
    /// holds up the state messages.
    /// </para>
    /// </summary>
    public sealed class CdRomFunction : IDisposable
    {
        /// <summary>Largest payload of one DATA message that holds whole sectors of either size (6 raw or 8 cooked sectors fit in 16 KB).</summary>
        private const int DataChunkLimit = 16352;

        private const int CookedSectorSize = CdSector.UserDataSize;

        private readonly ILinkLayer LinkLayer;
        private readonly object MediaLock = new object();
        private readonly BlockingCollection<Message> RequestQueue = new BlockingCollection<Message>(64);
        private readonly CancellationTokenSource Stop = new CancellationTokenSource();
        private readonly HashSet<ushort> CancelledTags = new HashSet<ushort>();
        private readonly object ReplyLock = new object();
        private readonly Thread Worker;

        private CdRomMedia Media;
        private CdRomMediaState LastState;
        private ushort ReplyTag;
        private readonly ManualResetEventSlim NameArrived = new ManualResetEventSlim(false);
        private volatile string LastName = "";
        private bool ReplyArrived;
        private CdRomMediaState ReplyState;
        private int NextTag = 0;
        private int DisposedFlag;
        private long EmptySinceTick = 0;

        /// <summary>
        /// How long the drive has to be seen empty before the next disc is presented (milliseconds). The OS only polls a drive every few
        /// seconds; a new disc that appears faster than that is not noticed as a change. 0 switches the wait off.
        /// </summary>
        public int MediaChangeSettleMs { get; set; } = 5000;

        /// <summary>Every MEDIA_STATE the Client sends (the answers to Insert and Remove included).</summary>
        public event Action<CdRomMediaState> StateReceived;

        /// <summary>The OS ejected the disc (not locked): the image is closed; the application plays its eject sound and marks the drive free.</summary>
        public event Action EjectedByOs;

        /// <summary>The OS wanted to eject but holds the lock; the application may offer a forced removal.</summary>
        public event Action EjectRefused;

        /// <summary>A read was answered: first LBA, sector count, status.</summary>
        public event Action<uint, uint, CdRomStatus> ReadServed;

        public CdRomFunction(ILinkLayer LinkLayer)
        {
            if (LinkLayer == null)
                throw new ArgumentNullException("LinkLayer");

            this.LinkLayer = LinkLayer;
            LinkLayer.SetHandler(MessageChannel.CdRom, OnMessage);

            Worker = new Thread(WorkerMain);
            Worker.Name = "Vulpes-Dolch CD-ROM reads";
            Worker.IsBackground = true;
            Worker.Start();
        }

        /// <summary>The disc in the drive (as far as the PC knows); null when empty.</summary>
        public CdRomMedia CurrentMedia
        {
            get
            {
                lock (MediaLock)
                    return (Media);
            }
        }

        /// <summary>The last MEDIA_STATE received; null before the first.</summary>
        public CdRomMediaState LastMediaState
        {
            get
            {
                lock (ReplyLock)
                    return (LastState);
            }
        }

        /// <summary>
        /// Puts a disc into the drive. Takes ownership of the media (disposed when removed or replaced). Returns true when the Client
        /// answered that the disc is present. With <paramref name="Reattach"/> the Client keeps the disc it already holds and the OS
        /// sees nothing.
        /// </summary>
        public bool Insert(CdRomMedia NewMedia, bool Reattach, int TimeoutMs)
        {
            if (NewMedia == null)
                throw new ArgumentNullException("NewMedia");

            return (Insert(NewMedia, NewMedia.CreateInsertInfo(Reattach), TimeoutMs));
        }

        /// <summary>Like <see cref="Insert(CdRomMedia, bool, int)"/> with a prepared message (the speed test uses it to flag a synthetic disc).</summary>
        public bool Insert(CdRomMedia NewMedia, CdRomInsertInfo Info, int TimeoutMs)
        {
            if (NewMedia == null)
                throw new ArgumentNullException("NewMedia");

            if ((Info.Flags & CdRomInsertInfo.FlagReattach) == 0)
                WaitForSettle();

            CdRomMedia Old;
            lock (MediaLock)
            {
                Old = Media;
                Media = NewMedia;
            }
            if ((Old != null) && (Old != NewMedia))
                Old.Dispose();

            CdRomMediaState Answer;
            if (SendAndWait(CdRomMessages.CreateMediaInsert(AllocateTag(), Info), TimeoutMs, out Answer) == false)
                return (false);

            bool Present = ((Answer.Present == true) && (Answer.MediaId == NewMedia.MediaId));
            if (Present == true)
                SendName(NewMedia.ImagePath);
            return (Present);
        }

        /// <summary>
        /// Takes the disc out. Returns true when the Client confirmed the drive is empty; false when the OS holds a lock (and
        /// <paramref name="Force"/> is not set) or the Client did not answer.
        /// </summary>
        public bool Remove(bool Force, int TimeoutMs)
        {
            CdRomMediaState Answer;
            if (SendAndWait(CdRomMessages.CreateMediaRemove(AllocateTag(), Force), TimeoutMs, out Answer) == false)
                return (false);
            if (Answer.Present == true)
                return (false);

            CloseMedia();
            return (true);
        }

        /// <summary>
        /// Asks the OS to eject the disc, the way a drive's eject button would: the Client reports an eject request in its media event
        /// status and the OS answers by ejecting (<see cref="EjectedByOs"/> follows) or by refusing while the disc is in use. Returns true
        /// when the request was sent.
        /// </summary>
        /// <summary>Tells the Client the image's path (kept in its RAM only) so that it can be found again after a restart of the PC side. Best effort; paths over 128 bytes are not sent.</summary>
        private void SendName(string Path)
        {
            Message Named = CdRomMessages.CreateSetName(AllocateTag(), Path);
            if (Named != null)
                LinkLayer.Send(Named, 1000);
        }

        /// <summary>Asks the Client for the name of the disc it holds: the path, "" when it has none, null when it does not answer.</summary>
        public string RequestName(int TimeoutMs)
        {
            NameArrived.Reset();
            if (LinkLayer.Send(CdRomMessages.CreateNameRequest(AllocateTag()), TimeoutMs) != LinkSendResult.Ok)
                return (null);
            if (NameArrived.Wait(TimeoutMs) == false)
                return (null);
            return (LastName);
        }

        public bool RequestEject(int TimeoutMs)
        {
            if (Media == null)
                return (false);

            return (LinkLayer.Send(CdRomMessages.CreateEjectRequest(AllocateTag()), TimeoutMs) == LinkSendResult.Ok);
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

        /// <summary>Waits until the drive has been reported empty for <see cref="MediaChangeSettleMs"/>, so the OS notices the removal before the next disc.</summary>
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

        /// <summary>Closes the image without talking to the Client (a load that failed must not leave the file open).</summary>
        public void ReleaseMedia()
        {
            CloseMedia();
        }

        private void CloseMedia()
        {
            CdRomMedia Old;
            lock (MediaLock)
            {
                Old = Media;
                Media = null;
            }
            if (Old != null)
                Old.Dispose();
        }

        private bool SendAndWait(Message Request, int TimeoutMs, out CdRomMediaState Answer)
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
            if (Received.Type == CdRomType.MediaState)
            {
                CdRomMediaState State;
                if (CdRomMessages.TryParseMediaState(Received, out State) == false)
                    return;

                OnMediaState(Received.Tag, State);
                return;
            }

            if (Received.Type == CdRomType.Name)
            {
                LastName = System.Text.Encoding.UTF8.GetString(Received.Payload, 0, Received.Payload.Length);
                NameArrived.Set();
                return;
            }

            if (Received.Type == CdRomType.ReadRequest)
            {
                if (RequestQueue.IsAddingCompleted == false)
                    RequestQueue.TryAdd(Received);
                return;
            }

            if (Received.Type == CdRomType.Cancel)
            {
                lock (CancelledTags)
                    CancelledTags.Add(Received.Tag);
                return;
            }
        }

        private void OnMediaState(ushort Tag, CdRomMediaState State)
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

            if ((State.Event == CdRomEvent.EjectedByOs) && (State.Present == false))
                CloseMedia();

            Action<CdRomMediaState> StateCopy = StateReceived;
            if (StateCopy != null)
                StateCopy(State);

            if (State.Event == CdRomEvent.EjectedByOs)
            {
                Action EjectedCopy = EjectedByOs;
                if (EjectedCopy != null)
                    EjectedCopy();
            }
            else if (State.Event == CdRomEvent.EjectRefused)
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
                        // The link or the image went away while a read was being answered; the Client's 60 s timeout covers it.
                    }
                }
            }
            catch (OperationCanceledException)
            {
            }
        }

        /// <summary>Answers one READ_REQUEST: DATA messages in whole sectors, then COMPLETE.</summary>
        private void Serve(Message Request)
        {
            CdRomReadRequest Read;
            if (CdRomMessages.TryParseReadRequest(Request, out Read) == false)
            {
                SendComplete(Request.Tag, CdRomStatus.Failed, 0);
                return;
            }

            CdRomMedia Current = CurrentMedia;
            if (Current == null)
            {
                SendComplete(Request.Tag, CdRomStatus.NoMedium, 0);
                RaiseReadServed(Read, CdRomStatus.NoMedium);
                return;
            }

            CdSectorReadMode Mode = (Read.Format == CdRomReadRequest.FormatRaw) ? CdSectorReadMode.Raw : CdSectorReadMode.UserData;
            int SectorSize = (Mode == CdSectorReadMode.Raw) ? CdSector.RawSize : CookedSectorSize;
            int SectorsPerMessage = DataChunkLimit / SectorSize;
            byte[] Chunk = new byte[SectorsPerMessage * SectorSize];
            byte[] Sector = new byte[CdSector.RawSize];

            CdRomStatus Status = CdRomStatus.Ok;
            uint Delivered = 0;
            int InChunk = 0;

            while (Delivered < Read.Count)
            {
                if (WasCancelled(Request.Tag) == true)
                {
                    Status = CdRomStatus.Cancelled;
                    break;
                }

                Int64 Lba = (Int64)Read.Lba + Delivered;
                try
                {
                    int Written;
                    lock (MediaLock)
                    {
                        if (Media != Current)
                            throw new CdImageException("The disc was removed while reading.", CdImageErrorKind.ReadError);
                        Written = Current.Image.ReadSector(Lba, Mode, Sector);
                    }

                    // A Mode 2 Form 2 sector has 2324 user bytes; a READ with 2048 byte blocks gets the first 2048 of them.
                    int Count = Math.Min(Written, SectorSize);
                    Array.Copy(Sector, 0, Chunk, InChunk * SectorSize, Count);
                    if (Count < SectorSize)
                        Array.Clear(Chunk, (InChunk * SectorSize) + Count, SectorSize - Count);
                }
                catch (CdImageException Problem)
                {
                    Status = MapError(Problem.Kind);
                    break;
                }

                InChunk++;
                Delivered++;
                if (InChunk == SectorsPerMessage)
                {
                    if (SendData(Request.Tag, Chunk, InChunk * SectorSize) == false)
                        return;
                    InChunk = 0;
                }
            }

            if (InChunk > 0)
            {
                if (SendData(Request.Tag, Chunk, InChunk * SectorSize) == false)
                    return;
            }

            SendComplete(Request.Tag, Status, Delivered);
            RaiseReadServed(Read, Status);
        }

        private bool SendData(ushort Tag, byte[] Chunk, int Length)
        {
            return (LinkLayer.Send(CdRomMessages.CreateData(Tag, Chunk, 0, Length), 10000) == LinkSendResult.Ok);
        }

        private void SendComplete(ushort Tag, CdRomStatus Status, uint Delivered)
        {
            LinkLayer.Send(CdRomMessages.CreateComplete(Tag, Status, Delivered), 10000);
            lock (CancelledTags)
                CancelledTags.Remove(Tag);
        }

        private bool WasCancelled(ushort Tag)
        {
            lock (CancelledTags)
                return (CancelledTags.Contains(Tag));
        }

        private void RaiseReadServed(CdRomReadRequest Read, CdRomStatus Status)
        {
            Action<uint, uint, CdRomStatus> Copy = ReadServed;
            if (Copy != null)
                Copy(Read.Lba, Read.Count, Status);
        }

        private static CdRomStatus MapError(CdImageErrorKind Kind)
        {
            switch (Kind)
            {
                case CdImageErrorKind.OutOfRange:
                    return (CdRomStatus.OutOfRange);
                case CdImageErrorKind.BadFormat:
                    return (CdRomStatus.BadFormat);
                case CdImageErrorKind.ReadError:
                    return (CdRomStatus.ReadError);
                default:
                    return (CdRomStatus.Failed);
            }
        }
    }
}
