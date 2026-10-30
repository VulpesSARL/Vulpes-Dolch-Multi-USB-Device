using System;
using System.Threading;
using VulpesDolch.Host.Common.Protocol;

namespace VulpesDolch.Host.Common.Link
{
    public enum HostConnectResult
    {
        Ok,

        /// <summary>No HELLO_ACK within the time given.</summary>
        Timeout,

        /// <summary>The Host device speaks another major protocol version.</summary>
        VersionMismatch,

        /// <summary>The Host device refused the session (it already has one: ERROR "busy").</summary>
        Rejected,

        /// <summary>The transport failed.</summary>
        Closed
    }

    /// <summary>
    /// The PC side of a session with the Host device on the control channel: HELLO handshake,
    /// heartbeat (PING), STATUS and error reporting. Other channels are reached through <see cref="Link"/>.
    /// </summary>
    public sealed class HostSession : IDisposable
    {
        private readonly ILinkLayer LinkLayer;
        private readonly object StateLock = new object();
        private readonly ManualResetEventSlim StopEvent = new ManualResetEventSlim(false);
        private Thread HeartbeatThread;
        private HelloAckInfo AckInfo;
        private HostStatus LastStatus;
        private bool RejectedByHost;
        private bool Connected;
        private bool PeerIsLost;
        private int StatusCount;
        private uint LastPongNonce;
        private bool PongArrived;
        private int NextTag;
        private int NextNonce;
        private int DisposedFlag;

        /// <summary>The Host device sends nothing for this long while idle -> PING (milliseconds). Set before Connect.</summary>
        public int HeartbeatIntervalMs { get; set; } = 1000;

        /// <summary>Nothing received for this long -> <see cref="PeerLost"/> (milliseconds). Set before Connect.</summary>
        public int PeerTimeoutMs { get; set; } = 3500;

        /// <summary>Raised with every STATUS message (the answer to a request, or sent by the Host when something changed).</summary>
        public event Action<HostStatus> StatusChanged;

        /// <summary>Raised with every ERROR message the Host device sends.</summary>
        public event Action<ErrorInfo> ErrorReceived;

        /// <summary>A debug message (type 0x70 and up) from the Host device: the speed test answers.</summary>
        public event Action<Message> DebugMessageReceived;

        /// <summary>Raised once when nothing has been received for PeerTimeoutMs.</summary>
        public event Action PeerLost;

        /// <summary>Raised when reports arrive again after PeerLost.</summary>
        public event Action PeerResumed;

        /// <param name="Layer">A link layer over any transport (for example <see cref="BulkLinkSession"/>); owned by the session.</param>
        public HostSession(ILinkLayer Layer)
        {
            if (Layer == null)
                throw new ArgumentNullException("Layer");

            LinkLayer = Layer;
        }

        /// <summary>The link layer, for registering the handlers of the other channels (before Connect) and sending on them.</summary>
        public ILinkLayer Link
        {
            get { return (LinkLayer); }
        }

        /// <summary>What the Host device answered in HELLO_ACK; null before a successful Connect.</summary>
        public HelloAckInfo HostInfo
        {
            get
            {
                lock (StateLock)
                    return (AckInfo);
            }
        }

        /// <summary>The last STATUS received; null if none yet.</summary>
        public HostStatus Status
        {
            get
            {
                lock (StateLock)
                    return (LastStatus);
            }
        }

        public bool IsConnected
        {
            get
            {
                lock (StateLock)
                    return (Connected);
            }
        }

        /// <summary>Starts the link, sends HELLO and waits for HELLO_ACK; on success the heartbeat starts.</summary>
        public HostConnectResult Connect(int TimeoutMs)
        {
            LinkLayer.SetHandler(MessageChannel.Control, OnControlMessage);
            LinkLayer.Start();

            long Deadline = Environment.TickCount64 + TimeoutMs;
            HelloInfo Hello = new HelloInfo();
            Hello.ProtocolVersion = ControlMessages.ProtocolVersion;
            Hello.ReceiveCredits = (byte)LinkLayer.ReceiveCapacity;

            LinkSendResult Sent = LinkLayer.Send(ControlMessages.CreateHello(AllocateTag(), Hello), TimeoutMs);
            if (Sent == LinkSendResult.Closed)
                return (HostConnectResult.Closed);
            if (Sent != LinkSendResult.Ok)
                return (HostConnectResult.Timeout);

            lock (StateLock)
            {
                while ((AckInfo == null) && (RejectedByHost == false))
                {
                    if (LinkLayer.IsClosed == true)
                        return (HostConnectResult.Closed);

                    long Left = Deadline - Environment.TickCount64;
                    if (Left <= 0)
                        return (HostConnectResult.Timeout);
                    Monitor.Wait(StateLock, (int)Math.Min(Left, 100));
                }

                if (RejectedByHost == true)
                    return (HostConnectResult.Rejected);
                if ((AckInfo.ProtocolVersion >> 8) != (ControlMessages.ProtocolVersion >> 8))
                    return (HostConnectResult.VersionMismatch);

                Connected = true;
            }

            HeartbeatThread = new Thread(HeartbeatMain);
            HeartbeatThread.Name = "Vulpes-Dolch heartbeat";
            HeartbeatThread.IsBackground = true;
            HeartbeatThread.Start();
            return (HostConnectResult.Ok);
        }

        /// <summary>Asks for STATUS and waits for it. The new status is in <see cref="Status"/> afterwards.</summary>
        public bool RequestStatus(int TimeoutMs)
        {
            long Deadline = Environment.TickCount64 + TimeoutMs;
            int Before;
            lock (StateLock)
                Before = StatusCount;

            if (LinkLayer.Send(ControlMessages.CreateStatusRequest(AllocateTag()), TimeoutMs) != LinkSendResult.Ok)
                return (false);

            lock (StateLock)
            {
                while (StatusCount == Before)
                {
                    if (LinkLayer.IsClosed == true)
                        return (false);

                    long Left = Deadline - Environment.TickCount64;
                    if (Left <= 0)
                        return (false);
                    Monitor.Wait(StateLock, (int)Math.Min(Left, 100));
                }
            }

            return (true);
        }

        /// <summary>
        /// Asks the Host device to reset the Client board. Returns once the request is sent; the progress
        /// (Lost, Booting, Running) arrives through <see cref="StatusChanged"/>, a refusal (busy, not supported) through
        /// <see cref="ErrorReceived"/>.
        /// </summary>
        public bool ResetClient(int TimeoutMs)
        {
            return (LinkLayer.Send(ControlMessages.CreateResetClient(AllocateTag()), TimeoutMs) == LinkSendResult.Ok);
        }

        /// <summary>
        /// Debug command: asks the Host device to do a cold reset (it answers with a STATUS first). With the download jumper set
        /// the board then sits in the USB bootloader, ready for a new image, without anybody pressing a button. Host builds
        /// without CY_FX_DEBUG_COMMANDS answer ERROR UnknownType. Returns once the request is sent.
        /// </summary>
        public bool DebugResetHost(int TimeoutMs)
        {
            return (LinkLayer.Send(ControlMessages.CreateDebugResetHost(AllocateTag()), TimeoutMs) == LinkSendResult.Ok);
        }

        /// <summary>Sends a PING and waits for the matching PONG; Milliseconds is the round trip.</summary>
        public bool Ping(int TimeoutMs, out double Milliseconds)
        {
            Milliseconds = 0;
            uint Nonce = (uint)Interlocked.Increment(ref NextNonce);
            long Started = System.Diagnostics.Stopwatch.GetTimestamp();
            long Deadline = Environment.TickCount64 + TimeoutMs;

            lock (StateLock)
                PongArrived = false;

            if (LinkLayer.Send(ControlMessages.CreatePing(AllocateTag(), Nonce), TimeoutMs) != LinkSendResult.Ok)
                return (false);

            lock (StateLock)
            {
                // Nonces only grow and PONGs come back in order, so a later PONG also answers this PING.
                while ((PongArrived == false) || (LastPongNonce < Nonce))
                {
                    if (LinkLayer.IsClosed == true)
                        return (false);

                    long Left = Deadline - Environment.TickCount64;
                    if (Left <= 0)
                        return (false);
                    Monitor.Wait(StateLock, (int)Math.Min(Left, 100));
                }
            }

            Milliseconds = (System.Diagnostics.Stopwatch.GetTimestamp() - Started) * 1000.0 / System.Diagnostics.Stopwatch.Frequency;
            return (true);
        }

        public void Dispose()
        {
            if (Interlocked.Exchange(ref DisposedFlag, 1) != 0)
                return;

            bool WasConnected = IsConnected;
            StopEvent.Set();
            if ((HeartbeatThread != null) && (HeartbeatThread != Thread.CurrentThread))
                HeartbeatThread.Join(2000);

            if (WasConnected == true)
                LinkLayer.Send(ControlMessages.CreateBye(AllocateTag()), 200);

            LinkLayer.Dispose();
            StopEvent.Dispose();
        }

        private ushort AllocateTag()
        {
            return ((ushort)Interlocked.Increment(ref NextTag));
        }

        private void HeartbeatMain()
        {
            long LastPingTicks = Environment.TickCount64;

            while (StopEvent.Wait(25) == false)
            {
                if (LinkLayer.IsClosed == true)
                    return;

                // Ping when we have been quiet, and also when the Host has been quiet: traffic that
                // draws no reply (keyboard, mouse) must not keep us from asking whether it is alive.
                long Now = Environment.TickCount64;
                bool SendQuiet = LinkLayer.MillisecondsSinceLastSend >= HeartbeatIntervalMs;
                bool ReceiveQuiet = (LinkLayer.MillisecondsSinceLastReceive >= HeartbeatIntervalMs) && ((Now - LastPingTicks) >= HeartbeatIntervalMs);
                if ((SendQuiet == true) || (ReceiveQuiet == true))
                {
                    uint Nonce = (uint)Interlocked.Increment(ref NextNonce);
                    LinkLayer.Send(ControlMessages.CreatePing(AllocateTag(), Nonce), 0);
                    LastPingTicks = Now;
                }

                bool Silent = LinkLayer.MillisecondsSinceLastReceive >= PeerTimeoutMs;
                bool RaiseLost = false;
                bool RaiseResumed = false;
                lock (StateLock)
                {
                    if ((Silent == true) && (PeerIsLost == false))
                    {
                        PeerIsLost = true;
                        RaiseLost = true;
                    }
                    else if ((Silent == false) && (PeerIsLost == true))
                    {
                        PeerIsLost = false;
                        RaiseResumed = true;
                    }
                }

                if ((RaiseLost == true) && (PeerLost != null))
                    PeerLost();
                if ((RaiseResumed == true) && (PeerResumed != null))
                    PeerResumed();
            }
        }

        private void OnControlMessage(Message Received)
        {
            switch (Received.Type)
            {
                case ControlType.HelloAck:
                    HelloAckInfo Ack;
                    if (ControlMessages.TryParseHelloAck(Received, out Ack) == false)
                    {
                        LinkLayer.Send(ControlMessages.CreateError(ErrorCode.BadLength, Received.Tag, 0), 1000);
                        return;
                    }

                    lock (StateLock)
                    {
                        AckInfo = Ack;
                        Monitor.PulseAll(StateLock);
                    }
                    return;

                case ControlType.Pong:
                    uint Nonce;
                    if (ControlMessages.TryParseNonce(Received, out Nonce) == false)
                        return;

                    lock (StateLock)
                    {
                        LastPongNonce = Nonce;
                        PongArrived = true;
                        Monitor.PulseAll(StateLock);
                    }
                    return;

                case ControlType.Status:
                    HostStatus Parsed;
                    if (ControlMessages.TryParseStatus(Received, out Parsed) == false)
                    {
                        LinkLayer.Send(ControlMessages.CreateError(ErrorCode.BadLength, Received.Tag, 0), 1000);
                        return;
                    }

                    lock (StateLock)
                    {
                        LastStatus = Parsed;
                        StatusCount++;
                        Monitor.PulseAll(StateLock);
                    }

                    Action<HostStatus> StatusCopy = StatusChanged;
                    if (StatusCopy != null)
                        StatusCopy(Parsed);
                    return;

                case ControlType.Error:
                    ErrorInfo Problem;
                    if (ControlMessages.TryParseError(Received, out Problem) == false)
                        return;

                    lock (StateLock)
                    {
                        // A "busy" before the handshake finished means the Host refused this session.
                        if ((Connected == false) && (AckInfo == null) && (Problem.Code == ErrorCode.Busy))
                        {
                            RejectedByHost = true;
                            Monitor.PulseAll(StateLock);
                        }
                    }

                    Action<ErrorInfo> ErrorCopy = ErrorReceived;
                    if (ErrorCopy != null)
                        ErrorCopy(Problem);
                    return;

                default:
                    if (Received.Type >= 0x70)
                    {
                        Action<Message> DebugCopy = DebugMessageReceived;
                        if (DebugCopy != null)
                            DebugCopy(Received);
                        return;
                    }

                    LinkLayer.Send(ControlMessages.CreateError(ErrorCode.UnknownType, Received.Tag, 0), 1000);
                    return;
            }
        }
    }
}
