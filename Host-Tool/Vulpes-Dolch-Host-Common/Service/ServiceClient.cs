using System;
using System.Collections.Generic;
using System.IO;
using System.IO.Pipes;
using System.Threading;

namespace VulpesDolch.Host.Common.Service
{
    /// <summary>
    /// The GUI's (or any other program's) connection to the service: sends the requests, waits for the answers and raises <see cref="StateChanged"/> whenever the
    /// service pushes a new state. Reconnects by itself when the service is restarted; <see cref="ConnectionChanged"/> tells the application so that it can
    /// disable its controls.
    /// </summary>
    public sealed class ServiceClient : IDisposable
    {
        private const int ConnectAttemptMs = 500;
        private const int RetryDelayMs = 1000;

        private readonly object WriteLock = new object();
        private readonly object PendingLock = new object();
        private readonly Dictionary<int, PendingCall> Pending = new Dictionary<int, PendingCall>();
        private readonly ManualResetEventSlim StopEvent = new ManualResetEventSlim(false);
        private Thread Worker;
        private NamedPipeClientStream Pipe;
        private int NextId = 1;
        private bool Connected;
        private ServiceState LastState;

        private sealed class PendingCall
        {
            public readonly ManualResetEventSlim Done = new ManualResetEventSlim(false);
            public ServiceResponse Response;
        }

        /// <summary>The service answered (true) or it is gone / not running (false). Raised on the worker thread.</summary>
        public event Action<bool> ConnectionChanged;

        /// <summary>The service pushed a state. Raised on the worker thread.</summary>
        public event Action<ServiceState> StateChanged;

        public bool IsConnected
        {
            get
            {
                lock (PendingLock)
                    return (Connected);
            }
        }

        public ServiceState State
        {
            get
            {
                lock (PendingLock)
                    return (LastState);
            }
        }

        public void Start()
        {
            if (Worker != null)
                return;

            Worker = new Thread(WorkerMain);
            Worker.Name = "Vulpes-Dolch service client";
            Worker.IsBackground = true;
            Worker.Start();
        }

        /// <summary>Sends a request and waits for the answer; a response with Ok = false and a text when the service is not there or does not answer in time.</summary>
        public ServiceResponse Call(ServiceRequest Request, int TimeoutMs)
        {
            NamedPipeClientStream Stream;
            lock (PendingLock)
                Stream = (Connected == true) ? Pipe : null;

            if (Stream == null)
                return (Failed("The Vulpes Dolch service is not running."));

            PendingCall Call = new PendingCall();
            int Id;
            lock (PendingLock)
            {
                Id = NextId++;
                Pending[Id] = Call;
            }

            ServiceFrame Frame = new ServiceFrame();
            Frame.Id = Id;
            Frame.Request = Request;

            try
            {
                lock (WriteLock)
                    PipeFraming.Write(Stream, Frame);
            }
            catch (IOException)
            {
                lock (PendingLock)
                    Pending.Remove(Id);
                return (Failed("The connection to the service was lost."));
            }

            bool Answered = Call.Done.Wait(TimeoutMs);
            lock (PendingLock)
                Pending.Remove(Id);

            return ((Answered == true && Call.Response != null) ? Call.Response : Failed("The service did not answer."));
        }

        private static ServiceResponse Failed(string Text)
        {
            ServiceResponse Result = new ServiceResponse();
            Result.Ok = false;
            Result.Error = Text;
            return (Result);
        }

        private void WorkerMain()
        {
            while (StopEvent.IsSet == false)
            {
                // Asynchronous (overlapped) handle: with a synchronous one a blocked Read on the worker thread would hold up every Write of a Call() on the same handle.
                NamedPipeClientStream Stream = new NamedPipeClientStream(".", ServiceNames.PipeName, PipeDirection.InOut, PipeOptions.Asynchronous);
                try
                {
                    Stream.Connect(ConnectAttemptMs);
                }
                catch (Exception)
                {
                    Stream.Dispose();
                    if (StopEvent.Wait(RetryDelayMs) == true)
                        return;
                    continue;
                }

                lock (PendingLock)
                {
                    Pipe = Stream;
                    Connected = true;
                }
                RaiseConnection(true);

                // Ask for the state at once, the service also pushes it on every change.
                ServiceFrame First = new ServiceFrame();
                First.Id = 0;
                First.Request = new ServiceRequest();
                First.Request.Command = ServiceCommand.GetState;

                try
                {
                    lock (WriteLock)
                        PipeFraming.Write(Stream, First);

                    while (StopEvent.IsSet == false)
                    {
                        ServiceFrame Frame = PipeFraming.Read(Stream);
                        if (Frame == null)
                            break;

                        Dispatch(Frame);
                    }
                }
                catch (Exception)
                {
                    // The pipe broke: fall through, reconnect.
                }

                lock (PendingLock)
                {
                    Connected = false;
                    Pipe = null;
                    foreach (PendingCall Call in Pending.Values)
                        Call.Done.Set();
                    LastState = null;
                }
                Stream.Dispose();
                RaiseConnection(false);

                if (StopEvent.Wait(RetryDelayMs) == true)
                    return;
            }
        }

        private void Dispatch(ServiceFrame Frame)
        {
            if (Frame.State != null)
            {
                lock (PendingLock)
                    LastState = Frame.State;

                Action<ServiceState> Handler = StateChanged;
                if (Handler != null)
                    Handler(Frame.State);
            }

            if ((Frame.Response != null) && (Frame.Id != 0))
            {
                PendingCall Call;
                lock (PendingLock)
                    Pending.TryGetValue(Frame.Id, out Call);

                if (Call != null)
                {
                    Call.Response = Frame.Response;
                    Call.Done.Set();
                }
            }
        }

        private void RaiseConnection(bool Up)
        {
            Action<bool> Handler = ConnectionChanged;
            if (Handler != null)
                Handler(Up);
        }

        public void Dispose()
        {
            StopEvent.Set();
            NamedPipeClientStream Stream;
            lock (PendingLock)
                Stream = Pipe;

            if (Stream != null)
            {
                try
                {
                    Stream.Dispose();
                }
                catch (Exception)
                {
                }
            }
        }
    }
}
