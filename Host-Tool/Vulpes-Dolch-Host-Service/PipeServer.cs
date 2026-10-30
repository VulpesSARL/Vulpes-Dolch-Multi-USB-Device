using System;
using System.Collections.Generic;
using System.IO;
using System.IO.Pipes;
using System.Security.AccessControl;
using System.Security.Principal;
using System.Threading;
using VulpesDolch.Host.Common.Service;

namespace Vulpes_Dolch_Host_Service
{
    /// <summary>Accepts the GUI connections on the named pipe, runs their requests on the engine and pushes every state change to all of them.</summary>
    internal sealed class PipeServer : IDisposable
    {
        private readonly ServiceEngine Engine;
        private readonly object ClientsLock = new object();
        private readonly List<Connection> Clients = new List<Connection>();
        private readonly ManualResetEventSlim StopEvent = new ManualResetEventSlim(false);
        private Thread Acceptor;

        private sealed class Connection
        {
            public NamedPipeServerStream Stream;
            public readonly object WriteLock = new object();
        }

        public PipeServer(ServiceEngine Engine)
        {
            this.Engine = Engine;
            this.Engine.StateChanged += delegate (ServiceState State)
            {
                Broadcast(State);
            };
        }

        public void Start()
        {
            Acceptor = new Thread(AcceptMain);
            Acceptor.Name = "Vulpes-Dolch pipe accept";
            Acceptor.IsBackground = true;
            Acceptor.Start();
        }

        private static PipeSecurity CreateSecurity()
        {
            // Any signed-in user may use the device through the GUI; SYSTEM and the administrators have full control.
            PipeSecurity Security = new PipeSecurity();
            Security.AddAccessRule(new PipeAccessRule(new SecurityIdentifier(WellKnownSidType.LocalSystemSid, null), PipeAccessRights.FullControl, AccessControlType.Allow));
            Security.AddAccessRule(new PipeAccessRule(new SecurityIdentifier(WellKnownSidType.BuiltinAdministratorsSid, null), PipeAccessRights.FullControl, AccessControlType.Allow));
            Security.AddAccessRule(new PipeAccessRule(new SecurityIdentifier(WellKnownSidType.AuthenticatedUserSid, null), PipeAccessRights.ReadWrite, AccessControlType.Allow));
            return (Security);
        }

        private void AcceptMain()
        {
            PipeSecurity Security = CreateSecurity();

            while (StopEvent.IsSet == false)
            {
                NamedPipeServerStream Stream = null;
                try
                {
                    Stream = NamedPipeServerStreamAcl.Create(ServiceNames.PipeName, PipeDirection.InOut, NamedPipeServerStream.MaxAllowedServerInstances, PipeTransmissionMode.Byte,
                        PipeOptions.Asynchronous, 0, 0, Security);     // overlapped: the state push may write while the reader thread waits for the next request
                    Stream.WaitForConnection();
                }
                catch (Exception)
                {
                    if (Stream != null)
                        Stream.Dispose();
                    if (StopEvent.Wait(500) == true)
                        return;
                    continue;
                }

                Connection Client = new Connection();
                Client.Stream = Stream;
                lock (ClientsLock)
                    Clients.Add(Client);

                Thread Reader = new Thread(delegate () { ClientMain(Client); });
                Reader.Name = "Vulpes-Dolch pipe client";
                Reader.IsBackground = true;
                Reader.Start();
            }
        }

        private void ClientMain(Connection Client)
        {
            try
            {
                while (StopEvent.IsSet == false)
                {
                    ServiceFrame Frame = PipeFraming.Read(Client.Stream);
                    if (Frame == null)
                        break;

                    if (Frame.Request == null)
                        continue;

                    ServiceFrame Answer = new ServiceFrame();
                    Answer.Id = Frame.Id;
                    Answer.Response = Engine.Execute(Frame.Request);
                    if (Frame.Request.Command == ServiceCommand.GetState)
                        Answer.State = Engine.BuildState();

                    Send(Client, Answer);
                    Engine.Publish();
                }
            }
            catch (Exception)
            {
                // A broken client only ends its own connection.
            }
            finally
            {
                lock (ClientsLock)
                    Clients.Remove(Client);
                Client.Stream.Dispose();
            }
        }

        private static void Send(Connection Client, ServiceFrame Frame)
        {
            lock (Client.WriteLock)
                PipeFraming.Write(Client.Stream, Frame);
        }

        private void Broadcast(ServiceState State)
        {
            Connection[] Snapshot;
            lock (ClientsLock)
                Snapshot = Clients.ToArray();

            ServiceFrame Frame = new ServiceFrame();
            Frame.Id = 0;
            Frame.State = State;

            foreach (Connection Client in Snapshot)
            {
                try
                {
                    Send(Client, Frame);
                }
                catch (Exception)
                {
                    // Its reader thread notices a broken pipe and removes it; one client can never stop the others from getting the state.
                }
            }
        }

        public void Dispose()
        {
            StopEvent.Set();
            lock (ClientsLock)
            {
                foreach (Connection Client in Clients)
                    Client.Stream.Dispose();
            }
        }
    }
}
