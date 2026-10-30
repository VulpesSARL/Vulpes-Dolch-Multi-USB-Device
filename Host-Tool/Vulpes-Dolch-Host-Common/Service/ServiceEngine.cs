using System;
using System.Collections.Generic;
using System.Threading;
using VulpesDolch.Host.Common.Functions;
using VulpesDolch.Host.Common.Protocol;

namespace VulpesDolch.Host.Common.Service
{
    /// <summary>
    /// The part of the service that does the work: owns the <see cref="HostDevice"/>, executes the requests of the clients and tells the service when the
    /// state changed so that it can push it to every connected GUI. Free of any pipe or Windows service code, so it can be tested and also run in a console.
    /// </summary>
    public sealed class ServiceEngine : IDisposable
    {
        private const int PollMs = 1000;

        private readonly HostDevice Device;
        private readonly INetworkConnector Network;
        private readonly HashSet<string> ConnectedShares = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
        private readonly object StateLock = new object();
        private readonly ManualResetEventSlim StopEvent = new ManualResetEventSlim(false);
        private Thread Poller;
        private ServiceState LastState;
        private string LastMessage = "";

        public ServiceEngine(HostDevice Device) : this(Device, null)
        {
        }

        /// <param name="Network">Connects network shares with credentials the user gave; null = images on shares are opened with the service's own account only.</param>
        public ServiceEngine(HostDevice Device, INetworkConnector Network)
        {
            this.Network = Network;
            if (Device == null)
                throw new ArgumentNullException("Device");

            this.Device = Device;
            this.Device.Log += delegate (string Text)
            {
                lock (StateLock)
                    LastMessage = Text;
                Publish();
            };
            this.Device.ConnectionChanged += delegate (bool Connected)
            {
                Publish();
            };
            this.Device.StatusChanged += delegate (HostStatus Status)
            {
                Publish();
            };
            this.Device.FunctionsCreated += delegate ()
            {
                // The target's keyboard LEDs are part of the state: ask for them and publish every change.
                KeyboardMouseFunction Input = this.Device.Input;
                if (Input != null)
                {
                    Input.LedsChanged += delegate (byte Leds) { Publish(); };
                    Input.StateChanged += delegate (KeyboardMouseState InputState) { Publish(); };
                    Input.Query();
                }
            };
        }

        /// <summary>The state changed (also raised once per second when something moved that no event announces, like a speed).</summary>
        public event Action<ServiceState> StateChanged;

        /// <summary>Text for the service's own log.</summary>
        public event Action<string> Log
        {
            add { Device.Log += value; }
            remove { Device.Log -= value; }
        }

        public HostDevice HostDevice
        {
            get { return (Device); }
        }

        public void Start()
        {
            Device.Start();

            Poller = new Thread(PollMain);
            Poller.Name = "Vulpes-Dolch service poll";
            Poller.IsBackground = true;
            Poller.Start();
        }

        public ServiceState BuildState()
        {
            ServiceState State = new ServiceState();
            State.DeviceConnected = Device.IsConnected;

            HelloAckInfo Info = Device.HostInfo;
            HostStatus Status = Device.Status;

            State.HostFirmware = (State.DeviceConnected == true && Info != null) ? FirmwareVersion.Format(Info.HostFirmwareVersion) : "";
            // The HELLO_ACK value is a snapshot from the start of the session; the STATUS of a newer Host firmware follows the Client when it restarts.
            uint ClientFirmwareValue = 0;
            if (Info != null)
                ClientFirmwareValue = Info.ClientFirmwareVersion;
            if ((Status != null) && (Status.HasClientFirmware == true))
                ClientFirmwareValue = Status.ClientFirmwareVersion;
            State.ClientFirmware = (State.DeviceConnected == true && Info != null) ? FirmwareVersion.Format(ClientFirmwareValue) : "";
            State.ClientState = (State.DeviceConnected == true && Status != null) ? Status.ClientState.ToString() : "Absent";

            // USB speeds: 1 full, 2 high, 3 super. The Client shows -1 (disconnected) whenever it has no speed: no PC behind it, not enumerated, or not running.
            State.HostUsbSpeed = 0;
            State.ClientUsbSpeed = 0;
            if ((State.DeviceConnected == true) && (Status != null))
            {
                State.HostUsbSpeed = Status.HostUsbSpeed;
                State.ClientUsbSpeed = ((Status.ClientState == ClientState.Running) && (Status.ClientUsbSpeed != 0)) ? Status.ClientUsbSpeed : -1;
            }

            State.CdPath = Device.CdImagePath;
            State.DiskPath = Device.DiskImagePath;
            if (Status != null)
            {
                State.CdPresent = Status.CdRomPresent;
                State.CdLocked = Status.CdRomLocked;
                State.DiskPresent = Status.HddPresent;
                State.DiskReadOnly = Status.HddReadOnly;
            }

            KeyboardMouseFunction Keys = Device.Input;
            State.TargetLeds = ((State.DeviceConnected == true) && (Keys != null) && (Keys.LastKnownState != null)) ? Keys.Leds : -1;

            lock (StateLock)
                State.Message = LastMessage;

            return (State);
        }

        public ServiceResponse Execute(ServiceRequest Request)
        {
            if (Request == null)
                return (Fail("Empty request."));

            try
            {
                switch (Request.Command)
                {
                    case ServiceCommand.GetState:
                        return (Succeed());

                    case ServiceCommand.InsertCd:
                        if (string.IsNullOrEmpty(Request.Path) == true)
                            return (Fail("No image given."));
                    {
                        ServiceResponse CdPrepared = PrepareNetworkPath(Request);
                        if (CdPrepared != null)
                            return (CdPrepared);
                        bool CdLoaded = Device.InsertCd(Request.Path);
                        ReleaseUnusedShares();
                        return ((CdLoaded == true) ? Succeed() : FailFile("The CD image cannot be opened."));
                    }

                    case ServiceCommand.EjectCd:
                        if (Device.EjectCd(Request.Flag) == false)
                            return (Fail("The target PC holds the lock on the CD-ROM."));
                        ReleaseUnusedShares();
                        return (Succeed());

                    case ServiceCommand.AttachDisk:
                        if (string.IsNullOrEmpty(Request.Path) == true)
                            return (Fail("No image given."));
                    {
                        ServiceResponse DiskPrepared = PrepareNetworkPath(Request);
                        if (DiskPrepared != null)
                            return (DiskPrepared);
                        bool DiskLoaded = Device.AttachDisk(Request.Path, Request.Flag);
                        ReleaseUnusedShares();
                        return ((DiskLoaded == true) ? Succeed() : FailFile("The disk image cannot be opened."));
                    }

                    case ServiceCommand.DetachDisk:
                        if (Device.DetachDisk(Request.Flag) == false)
                            return (Fail("The target PC holds the lock on the disk."));
                        ReleaseUnusedShares();
                        return (Succeed());
                }

                return (ExecuteInput(Request));
            }
            catch (Exception Problem)
            {
                return (Fail(Problem.Message));
            }
        }

        /// <summary>
        /// An image on a network share: with credentials in the request the share is connected with them; without, the service's own account is tried and
        /// if the share refuses it the answer asks the client for credentials. Returns null when the load may go on (the normal path then reports a missing file).
        /// </summary>
        private ServiceResponse PrepareNetworkPath(ServiceRequest Request)
        {
            string Share = UncPath.ShareOf(Request.Path);
            if ((Network == null) || (Share == null))
                return (null);

            if (string.IsNullOrEmpty(Request.User) == false)
            {
                string Problem;
                if (Network.Connect(Share, Request.User, Request.Password, out Problem) == false)
                    return (NeedCredentials(Share, Problem));

                lock (StateLock)
                    ConnectedShares.Add(Share);
                return (null);
            }

            if (Network.CheckAccess(Request.Path) == NetworkAccess.NeedCredentials)
                return (NeedCredentials(Share, "Access to " + Share + " was denied."));

            return (null);
        }

        private static ServiceResponse NeedCredentials(string Share, string Text)
        {
            ServiceResponse Result = Fail(Text);
            Result.NeedsCredentials = true;
            Result.Share = Share;
            return (Result);
        }

        /// <summary>Disconnects every share this engine connected that no wanted image lives on any more (the credentials are only kept while the image is in use).</summary>
        private void ReleaseUnusedShares()
        {
            if (Network == null)
                return;

            string CdShare = UncPath.ShareOf(Device.CdImagePath);
            string DiskShare = UncPath.ShareOf(Device.DiskImagePath);
            List<string> Unused = new List<string>();
            lock (StateLock)
            {
                foreach (string Share in ConnectedShares)
                {
                    if ((string.Equals(Share, CdShare, StringComparison.OrdinalIgnoreCase) == false) && (string.Equals(Share, DiskShare, StringComparison.OrdinalIgnoreCase) == false))
                        Unused.Add(Share);
                }
                foreach (string Share in Unused)
                    ConnectedShares.Remove(Share);
            }

            foreach (string Share in Unused)
                Network.Disconnect(Share);
        }

        private ServiceResponse ExecuteInput(ServiceRequest Request)
        {
            KeyboardMouseFunction Input = Device.Input;
            if (Input == null)
                return (Fail("The device is not connected."));

            bool Done;
            switch (Request.Command)
            {
                case ServiceCommand.KeyDown:
                    Done = Input.KeyDown((byte)Request.Value);
                    break;
                case ServiceCommand.KeyUp:
                    Done = Input.KeyUp((byte)Request.Value);
                    break;
                case ServiceCommand.MouseMove:
                    Done = Input.MouseMoveAndWheel(Request.Dx, Request.Dy, Request.Wheel);
                    break;
                case ServiceCommand.MouseButton:
                    Done = Input.MouseButton((byte)Request.Value, Request.Flag);
                    break;
                case ServiceCommand.ReleaseAll:
                    Done = Input.ReleaseAll();
                    break;
                default:
                    return (Fail("Unknown command."));
            }

            return ((Done == true) ? Succeed() : Fail("The input was not accepted."));
        }

        private static ServiceResponse Succeed()
        {
            ServiceResponse Result = new ServiceResponse();
            Result.Ok = true;
            Result.Error = "";
            return (Result);
        }

        private static ServiceResponse FailFile(string Text)
        {
            ServiceResponse Result = Fail(Text);
            Result.FileError = true;
            return (Result);
        }

        private static ServiceResponse Fail(string Text)
        {
            ServiceResponse Result = new ServiceResponse();
            Result.Ok = false;
            Result.Error = Text;
            return (Result);
        }

        /// <summary>Raises <see cref="StateChanged"/> when the state differs from the last one that was announced.</summary>
        public void Publish()
        {
            ServiceState Fresh = BuildState();
            lock (StateLock)
            {
                if ((LastState != null) && (Same(LastState, Fresh) == true))
                    return;
                LastState = Fresh;
            }

            Action<ServiceState> Handler = StateChanged;
            if (Handler != null)
                Handler(Fresh);
        }

        private static bool Same(ServiceState A, ServiceState B)
        {
            return (A.DeviceConnected == B.DeviceConnected && A.HostFirmware == B.HostFirmware && A.ClientFirmware == B.ClientFirmware &&
                A.HostUsbSpeed == B.HostUsbSpeed && A.ClientUsbSpeed == B.ClientUsbSpeed && A.ClientState == B.ClientState && A.CdPath == B.CdPath &&
                A.CdPresent == B.CdPresent && A.CdLocked == B.CdLocked && A.DiskPath == B.DiskPath && A.DiskPresent == B.DiskPresent &&
                A.DiskReadOnly == B.DiskReadOnly && A.TargetLeds == B.TargetLeds && A.Message == B.Message);
        }

        private void PollMain()
        {
            while (StopEvent.Wait(PollMs) == false)
            {
                try
                {
                    Publish();
                    ReleaseUnusedShares();
                }
                catch (Exception)
                {
                    // A failing poll must never kill the service.
                }
            }
        }

        public void Dispose()
        {
            StopEvent.Set();
            Device.Dispose();
            if (Network != null)
            {
                List<string> Shares;
                lock (StateLock)
                {
                    Shares = new List<string>(ConnectedShares);
                    ConnectedShares.Clear();
                }
                foreach (string Share in Shares)
                    Network.Disconnect(Share);
            }
        }
    }
}
