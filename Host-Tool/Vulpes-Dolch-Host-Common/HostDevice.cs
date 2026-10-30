using System;
using System.Threading;
using VulpesDolch.Host.Common.Functions;
using VulpesDolch.Host.Common.Link;
using VulpesDolch.Host.Common.Media;
using VulpesDolch.Host.Common.Protocol;

namespace VulpesDolch.Host.Common
{
    /// <summary>
    /// The whole Host device as one object for an application (a GUI, a service, a command line tool): it finds the device, opens the session, creates
    /// the functions (CD-ROM, disk, keyboard / mouse), keeps the session alive and starts over when the device goes away and comes back (re-plugged,
    /// reset, switched to the update mode and back). The CD image and the disk image that were wanted are loaded again by themselves, without a media
    /// change for the target PC when the Client still holds them.
    /// <para>
    /// MIDI and the serial port need no application at all - the Host and Client devices join them by themselves.
    /// </para>
    /// <para>
    /// Threading: the connect loop runs on a thread of its own; the events are raised on that thread or on the link's dispatch thread, so keep the
    /// handlers short. The function objects change at every reconnect: subscribe to <see cref="FunctionsCreated"/> to wire their events, and never
    /// keep a function across a <see cref="ConnectionChanged"/> to false.
    /// </para>
    /// </summary>
    public sealed class HostDevice : IDisposable
    {
        private const int ConnectTimeoutMs = 3000;
        private const int RetryDelayMs = 1000;

        // The Host device treats every report of the PC as a sign of life, a rejected HELLO included: it drops a dead session only after 3.5 s of complete silence.
        // After a Rejected (the session of a killed or crashed predecessor is still open) the next try must therefore come after more than that.
        private const int RejectedRetryDelayMs = 4500;

        private readonly Func<IBlockTransport> TransportFactory;
        private readonly object StateLock = new object();
        private readonly ManualResetEventSlim StopEvent = new ManualResetEventSlim(false);
        private readonly ManualResetEventSlim WakeEvent = new ManualResetEventSlim(false);
        private Thread Worker;
        private int NextRetryDelayMs = RetryDelayMs;

        private HostSession CurrentSession;
        private CdRomFunction CdFunction;
        private HddFunction DiskFunction;
        private KeyboardMouseFunction InputFunction;
        private bool Connected;
        private HostStatus LastStatus;
        private ClientState LastClientState = ClientState.Absent;
        private int DisposedFlag;

        // What the application wants in the drives; loaded (again) whenever the Client runs.
        private string WantedCdPath;
        private string WantedDiskPath;
        private bool WantedDiskReadOnly;
        private bool CdLoaded;
        private bool DiskLoaded;

        // Recovery after a restart of the program (opt-in, see EnableRecovery).
        private ISavedMediaState Memory;
        private string SavedCdPath;
        private string SavedDiskPath;
        private bool RecoveryDone;

        /// <summary>Opens the Host device through WinUSB (Windows).</summary>
        public HostDevice() : this(delegate () { return (WinUsbBlockTransport.Open()); })
        {
        }

        /// <param name="TransportFactory">Opens the transport to the device or throws when it is not there; called again and again until it works.</param>
        public HostDevice(Func<IBlockTransport> TransportFactory)
        {
            if (TransportFactory == null)
                throw new ArgumentNullException("TransportFactory");

            this.TransportFactory = TransportFactory;
        }

        /// <summary>The session is up (true) or it ended (false).</summary>
        public event Action<bool> ConnectionChanged;

        /// <summary>The functions of a fresh session exist now; wire their events here.</summary>
        public event Action FunctionsCreated;

        /// <summary>Every STATUS the Host device sends (the state of the Client, the media).</summary>
        public event Action<HostStatus> StatusChanged;

        /// <summary>Progress and problems as text (connect attempts, media loaded, errors), for a log window.</summary>
        public event Action<string> Log;

        public bool IsConnected
        {
            get
            {
                lock (StateLock)
                    return (Connected);
            }
        }

        public HostStatus Status
        {
            get
            {
                lock (StateLock)
                    return (LastStatus);
            }
        }

        public HelloAckInfo HostInfo
        {
            get
            {
                lock (StateLock)
                    return ((CurrentSession != null) ? CurrentSession.HostInfo : null);
            }
        }

        public HostSession Session
        {
            get
            {
                lock (StateLock)
                    return (CurrentSession);
            }
        }

        /// <summary>The CD-ROM function of the current session; null while not connected.</summary>
        public CdRomFunction CdRom
        {
            get
            {
                lock (StateLock)
                    return (CdFunction);
            }
        }

        public HddFunction Disk
        {
            get
            {
                lock (StateLock)
                    return (DiskFunction);
            }
        }

        public KeyboardMouseFunction Input
        {
            get
            {
                lock (StateLock)
                    return (InputFunction);
            }
        }

        public string CdImagePath
        {
            get
            {
                lock (StateLock)
                    return (WantedCdPath);
            }
        }

        public string DiskImagePath
        {
            get
            {
                lock (StateLock)
                    return (WantedDiskPath);
            }
        }

        /// <summary>
        /// Switches the recovery after a restart on (the service does; a tool that merely looks at the device must not, because the recovery force-ejects what it cannot
        /// recognise). The image paths are saved through <paramref name="Memory"/> whenever they change. When a new session finds media in the Client's drives that this
        /// object did not put there, it looks for the image (the saved path, then the name the Client kept in its RAM), accepts a path only if its media id is the
        /// Client's, and loads it again without a media change for the target PC; if no path matches, the medium is ejected by force. Empty drives stay empty.
        /// </summary>
        public void EnableRecovery(ISavedMediaState Memory)
        {
            if (Memory == null)
                throw new ArgumentNullException("Memory");

            string Cd;
            string Disk;
            bool ReadOnly;
            Memory.Load(out Cd, out Disk, out ReadOnly);

            lock (StateLock)
            {
                this.Memory = Memory;
                SavedCdPath = Cd;
                SavedDiskPath = Disk;
            }
        }

        private void SaveMemory()
        {
            ISavedMediaState Target;
            string Cd;
            string Disk;
            bool ReadOnly;
            lock (StateLock)
            {
                Target = Memory;
                Cd = WantedCdPath;
                Disk = WantedDiskPath;
                ReadOnly = WantedDiskReadOnly;
            }

            if (Target == null)
                return;

            try
            {
                Target.Save(Cd, Disk, ReadOnly);
            }
            catch (Exception Problem)
            {
                RaiseLog("Cannot save the media state: " + Problem.Message);
            }
        }

        /// <summary>Starts the connect loop. Safe to call once.</summary>
        public void Start()
        {
            lock (StateLock)
            {
                if (Worker != null)
                    return;

                Worker = new Thread(WorkerMain);
                Worker.Name = "Vulpes-Dolch device";
                Worker.IsBackground = true;
                Worker.Start();
            }
        }

        /// <summary>Wants this image in the CD-ROM drive (replacing the current one); it is loaded as soon as the Client runs. Returns false when the file cannot be opened.</summary>
        public bool InsertCd(string ImagePath)
        {
            if (ImagePath == null)
                throw new ArgumentNullException("ImagePath");

            try
            {
                CdRomMedia Probe = CdRomMedia.Open(ImagePath);
                Probe.Dispose();
            }
            catch (CdImageException Problem)
            {
                RaiseLog("CD image " + ImagePath + ": " + Problem.Message);
                return (false);
            }

            lock (StateLock)
            {
                WantedCdPath = ImagePath;
                CdLoaded = false;
            }
            SaveMemory();
            WakeEvent.Set();
            return (true);
        }

        /// <summary>Takes the disc out. False when the OS holds the lock and <paramref name="Force"/> is not set.</summary>
        public bool EjectCd(bool Force)
        {
            CdRomFunction Function;
            lock (StateLock)
                Function = CdFunction;

            if (Function != null)
            {
                if (Function.Remove(Force, 5000) == false)
                    return (false);
            }

            lock (StateLock)
            {
                WantedCdPath = null;
                CdLoaded = false;
            }
            SaveMemory();
            return (true);
        }

        public bool AttachDisk(string ImagePath, bool ReadOnly)
        {
            if (ImagePath == null)
                throw new ArgumentNullException("ImagePath");

            try
            {
                HddMedia Probe = HddMedia.Open(ImagePath, true);
                Probe.Dispose();
            }
            catch (HddException Problem)
            {
                RaiseLog("Disk image " + ImagePath + ": " + Problem.Message);
                return (false);
            }

            lock (StateLock)
            {
                WantedDiskPath = ImagePath;
                WantedDiskReadOnly = ReadOnly;
                DiskLoaded = false;
            }
            SaveMemory();
            WakeEvent.Set();
            return (true);
        }

        public bool DetachDisk(bool Force)
        {
            HddFunction Function;
            lock (StateLock)
                Function = DiskFunction;

            if (Function != null)
            {
                if (Function.Detach(Force, 5000) == false)
                    return (false);
            }

            lock (StateLock)
            {
                WantedDiskPath = null;
                DiskLoaded = false;
            }
            SaveMemory();
            return (true);
        }

        private void RaiseLog(string Text)
        {
            Action<string> Handler = Log;
            if (Handler != null)
                Handler(Text);
        }

        private void WorkerMain()
        {
            while (StopEvent.IsSet == false)
            {
                RunSession();
                if (StopEvent.IsSet == false)
                    StopEvent.Wait(NextRetryDelayMs);
                NextRetryDelayMs = RetryDelayMs;
            }
        }

        /// <summary>One session from opening the transport to its end.</summary>
        private void RunSession()
        {
            IBlockTransport Transport;
            BulkLinkSession Link;
            HostSession NewSession;

            try
            {
                Transport = TransportFactory();
            }
            catch (Exception Problem)
            {
                // The device is not there (yet): the loop tries again. Which exception the platform throws is its own business.
                RaiseLog("Waiting for the Host device (" + Problem.Message + ")");
                return;
            }

            Link = new BulkLinkSession(Transport);
            NewSession = new HostSession(Link);
            CdRomFunction NewCd = new CdRomFunction(Link);
            HddFunction NewDisk = new HddFunction(Link);
            KeyboardMouseFunction NewInput = new KeyboardMouseFunction(Link);
            BulkLinkSession BulkSession = Link;
            NewDisk.BulkWriter = BulkSession.BulkWriter;

            NewSession.StatusChanged += OnSessionStatus;

            // Nothing heard from the device for 3.5 s: it is gone or restarted (a restarted Host device wants a new HELLO), so start over.
            bool PeerGone = false;
            Action OnPeerLost = delegate ()
            {
                PeerGone = true;
                WakeEvent.Set();
            };
            NewSession.PeerLost += OnPeerLost;

            Action<LinkProblemKind, string> OnLinkProblem = delegate (LinkProblemKind Kind, string Text)
            {
                RaiseLog("Link problem: " + Kind + " " + Text);
            };
            Link.Problem += OnLinkProblem;

            try
            {
                HostConnectResult Result = NewSession.Connect(ConnectTimeoutMs);
                if (Result != HostConnectResult.Ok)
                {
                    RaiseLog("The Host device did not accept the session (" + Result + ")");
                    if (Result == HostConnectResult.Rejected)
                        NextRetryDelayMs = RejectedRetryDelayMs;
                    return;
                }

                lock (StateLock)
                {
                    CurrentSession = NewSession;
                    CdFunction = NewCd;
                    DiskFunction = NewDisk;
                    InputFunction = NewInput;
                    Connected = true;
                    LastClientState = ClientState.Absent;
                    CdLoaded = false;
                    DiskLoaded = false;
                    RecoveryDone = false;
                }

                RaiseLog("Connected to the Host device");
                Action Created = FunctionsCreated;
                if (Created != null)
                    Created();
                Action<bool> Changed = ConnectionChanged;
                if (Changed != null)
                    Changed(true);

                NewSession.RequestStatus(2000);
                while ((StopEvent.IsSet == false) && (Link.IsClosed == false) && (PeerGone == false))
                {
                    RecoverMedia(NewCd, NewDisk);
                    LoadWantedMedia(NewCd, NewDisk);
                    WakeEvent.Wait(250);
                    WakeEvent.Reset();
                }
            }
            finally
            {
                bool WasConnected;
                lock (StateLock)
                {
                    WasConnected = Connected;
                    Connected = false;
                    CurrentSession = null;
                    CdFunction = null;
                    DiskFunction = null;
                    InputFunction = null;
                }

                if (WasConnected == true)
                    RaiseLog("Session ends: peer lost " + PeerGone + ", link closed " + Link.IsClosed + ", stopping " + StopEvent.IsSet);

                NewSession.StatusChanged -= OnSessionStatus;
                NewSession.PeerLost -= OnPeerLost;
                Link.Problem -= OnLinkProblem;
                NewInput.Dispose();
                NewCd.Dispose();
                NewDisk.Dispose();
                NewSession.Dispose();

                if (WasConnected == true)
                {
                    RaiseLog("The connection to the Host device ended");
                    Action<bool> Changed = ConnectionChanged;
                    if (Changed != null)
                        Changed(false);
                }
            }
        }

        private void OnSessionStatus(HostStatus Status)
        {
            lock (StateLock)
            {
                LastStatus = Status;
                if (Status.ClientState != LastClientState)
                {
                    // A Client that restarted has forgotten what it was told: the media go in again.
                    if (Status.ClientState != ClientState.Running)
                    {
                        CdLoaded = false;
                        DiskLoaded = false;
                    }
                    LastClientState = Status.ClientState;
                }
            }

            Action<HostStatus> Handler = StatusChanged;
            if (Handler != null)
                Handler(Status);
            WakeEvent.Set();
        }

        /// <summary>Once per session, when the Client runs: finds the images of media the Client holds but this object knows nothing about, or ejects them.</summary>
        private void RecoverMedia(CdRomFunction Cd, HddFunction Disk)
        {
            HostStatus Status;
            string SavedCd;
            string SavedDisk;
            bool CdUnknown;
            bool DiskUnknown;

            lock (StateLock)
            {
                if ((Memory == null) || (RecoveryDone == true) || (LastClientState != ClientState.Running) || (LastStatus == null))
                    return;

                RecoveryDone = true;
                Status = LastStatus;
                SavedCd = SavedCdPath;
                SavedDisk = SavedDiskPath;
                CdUnknown = (WantedCdPath == null);
                DiskUnknown = (WantedDiskPath == null);
            }

            if ((Status.CdRomPresent == true) && (CdUnknown == true))
            {
                string Named = Cd.RequestName(2000);
                string Found = MediaRecovery.FindMatch(new string[] { SavedCd, Named }, Status.CdRomMediaId, delegate (string Path)
                {
                    using (CdRomMedia Probe = CdRomMedia.Open(Path))
                        return (Probe.MediaId);
                });

                if (Found != null)
                {
                    lock (StateLock)
                    {
                        WantedCdPath = Found;
                        CdLoaded = false;
                    }
                    SaveMemory();
                    RaiseLog("CD image " + Found + " recovered after the restart");
                }
                else
                {
                    RaiseLog("The Client holds a CD that is not known any more: ejected by force");
                    Cd.Remove(true, 5000);
                }
            }

            if ((Status.HddPresent == true) && (DiskUnknown == true))
            {
                string Named = Disk.RequestName(2000);
                string Found = MediaRecovery.FindMatch(new string[] { SavedDisk, Named }, Status.HddMediaId, delegate (string Path)
                {
                    using (HddMedia Probe = HddMedia.Open(Path, true))
                        return (Probe.MediaId);
                });

                if (Found != null)
                {
                    lock (StateLock)
                    {
                        WantedDiskPath = Found;
                        WantedDiskReadOnly = Status.HddReadOnly;
                        DiskLoaded = false;
                    }
                    SaveMemory();
                    RaiseLog("Disk image " + Found + " recovered after the restart");
                }
                else
                {
                    RaiseLog("The Client holds a disk that is not known any more: ejected by force");
                    Disk.Detach(true, 5000);
                }
            }
        }

        /// <summary>When the Client runs, puts the wanted images into the drives (once per Client start).</summary>
        private void LoadWantedMedia(CdRomFunction Cd, HddFunction Disk)
        {
            string CdPath;
            string DiskPath;
            bool DiskReadOnly;
            bool NeedCd;
            bool NeedDisk;

            lock (StateLock)
            {
                if (LastClientState != ClientState.Running)
                    return;

                CdPath = WantedCdPath;
                DiskPath = WantedDiskPath;
                DiskReadOnly = WantedDiskReadOnly;
                NeedCd = (CdPath != null) && (CdLoaded == false);
                NeedDisk = (DiskPath != null) && (DiskLoaded == false);
            }

            if (NeedCd == true)
            {
                try
                {
                    // Re-load first (the Client may still hold this disc from before): no media change for the target PC then. The image is opened ONCE and
                    // used for both tries (a second open of a read-write image would fail on the first one's lock).
                    CdRomMedia Image = CdRomMedia.Open(CdPath);
                    bool Ok = Cd.Insert(Image, true, 3000);
                    if (Ok == false)
                        Ok = Cd.Insert(Image, false, 5000);
                    RaiseLog("CD image " + CdPath + ((Ok == true) ? " loaded" : " could not be loaded"));
                    if (Ok == false)
                        Cd.ReleaseMedia();
                    lock (StateLock)
                        CdLoaded = Ok;

                    // A failed load is not tried again every 250 ms: the wanted image is dropped, the log line says why, the drive shows empty.
                    if (Ok == false)
                    {
                        lock (StateLock)
                            WantedCdPath = null;
                        SaveMemory();
                    }
                }
                catch (CdImageException Problem)
                {
                    RaiseLog("CD image " + CdPath + ": " + Problem.Message);
                    Cd.ReleaseMedia();
                    lock (StateLock)
                        WantedCdPath = null;
                    SaveMemory();
                }
            }

            if (NeedDisk == true)
            {
                try
                {
                    HddMedia Image = HddMedia.Open(DiskPath, DiskReadOnly);
                    bool Ok = Disk.Attach(Image, true, 3000);
                    if (Ok == false)
                        Ok = Disk.Attach(Image, false, 5000);
                    RaiseLog("Disk image " + DiskPath + ((Ok == true) ? " attached" : " could not be attached"));
                    if (Ok == false)
                        Disk.ReleaseMedia();
                    lock (StateLock)
                        DiskLoaded = Ok;

                    if (Ok == false)
                    {
                        lock (StateLock)
                            WantedDiskPath = null;
                        SaveMemory();
                    }
                }
                catch (HddException Problem)
                {
                    RaiseLog("Disk image " + DiskPath + ": " + Problem.Message);
                    Disk.ReleaseMedia();
                    lock (StateLock)
                        WantedDiskPath = null;
                    SaveMemory();
                }
            }
        }

        /// <summary>Ends the session (all keys are released first) and stops the connect loop.</summary>
        public void Dispose()
        {
            if (Interlocked.Exchange(ref DisposedFlag, 1) != 0)
                return;

            StopEvent.Set();
            WakeEvent.Set();
            Thread Running = Worker;
            if ((Running != null) && (Running != Thread.CurrentThread))
                Running.Join(5000);
        }
    }
}
