using System;
using System.Collections.Concurrent;
using VulpesDolch.Host.Common.Link;
using VulpesDolch.Host.Common.Protocol;

namespace VulpesDolch.Host.Common.Update
{
    /// <summary>
    /// The PC side of the Host device's own firmware update. The Host has no room for the whole file, so the file is sent twice: pass 1 only
    /// checks it (signature, target, the CRC of the decompressed image), pass 2 carries the same file and is programmed into the EEPROM while it
    /// streams in. Create it before the link is started. Every call blocks and throws <see cref="UpdateException"/> on a refusal or failure.
    /// </summary>
    public sealed class HostUpdater
    {
        private const int ChunkSize = 8192;
        private const int ChunksPerCheck = 8;

        private readonly ILinkLayer LinkLayer;
        private readonly BlockingCollection<UpdateStateInfo> Inbox = new BlockingCollection<UpdateStateInfo>(256);

        public HostUpdater(ILinkLayer LinkLayer)
        {
            this.LinkLayer = LinkLayer;
            LinkLayer.SetHandler(MessageChannel.HostUpdate, OnMessage);
        }

        private void OnMessage(Message Reply)
        {
            UpdateStateInfo Info = UpdateStateInfo.TryParse(Reply);
            if (Info == null)
                return;

            while (Inbox.TryAdd(Info) == false)
                Inbox.TryTake(out UpdateStateInfo Dropped);
        }

        private void Send(Message Request)
        {
            LinkSendResult Result = LinkLayer.Send(Request, 10000);
            if (Result != LinkSendResult.Ok)
                throw new UpdateException("Sending to the Host failed: " + Result + ".");
        }

        private void ClearInbox()
        {
            while (Inbox.TryTake(out UpdateStateInfo Dropped))
            {
            }
        }

        private UpdateStateInfo NextState(int TimeoutMs)
        {
            if (Inbox.TryTake(out UpdateStateInfo Info, TimeoutMs) == false)
                throw new UpdateException("The Host did not answer within " + TimeoutMs + " ms.");
            return (Info);
        }

        private static void Require(UpdateStateInfo Info, UpdateState Expected, string Step)
        {
            if (Info.State == UpdateState.Failed)
                throw new UpdateException(Step + " failed: " + Info.Error + ".");
            if (Info.State != Expected)
                throw new UpdateException(Step + ": the Host is in state " + Info.State + " (" + Info.Error + "), expected " + Expected + ".");
        }

        private UpdateStateInfo Query(int TimeoutMs)
        {
            ClearInbox();
            Send(UpdateMessages.CreateHost(UpdateMessages.Query));
            return (NextState(TimeoutMs));
        }

        public void Enter(int TimeoutMs)
        {
            ClearInbox();
            Send(UpdateMessages.CreateHost(UpdateMessages.Enter));
            Require(NextState(TimeoutMs), UpdateState.UpdateMode, "Entering update mode");
        }

        /// <summary>Sends the whole file once. pass 1 expects Verified at the end, pass 2 reports the progress and expects Done.</summary>
        private void SendPass(UpdatePackage Package, uint Pass, Action<int> Progress)
        {
            byte[] File = Package.File;
            UpdateState Running = (Pass == 1) ? UpdateState.Receiving : UpdateState.Programming;

            ClearInbox();
            Send(UpdateMessages.CreateHostBegin((uint)File.Length, Pass));
            Require(NextState(15000), Running, "Starting pass " + Pass);

            int Sent = 0;
            int Chunks = 0;
            while (Sent < File.Length)
            {
                int Count = Math.Min(ChunkSize, File.Length - Sent);
                Send(UpdateMessages.CreateHostData((uint)Sent, File, Sent, Count));
                Sent += Count;
                Chunks++;

                if (((Chunks % ChunksPerCheck) == 0) || (Sent == File.Length))
                {
                    // Progress messages of pass 2 and the answer to the query arrive in order; the last STATE tells where the Host is.
                    ClearInbox();
                    Send(UpdateMessages.CreateHost(UpdateMessages.Query));
                    UpdateStateInfo Info = NextState(60000);
                    Require(Info, Running, "Transfer (pass " + Pass + ")");
                    if ((Pass == 1) && (Info.Detail != (uint)Sent))
                        throw new UpdateException("The Host has " + Info.Detail + " bytes, " + Sent + " were sent.");
                    if (Progress != null)
                        Progress((int)((long)Sent * 100 / File.Length));
                }
            }

            ClearInbox();
            Send(UpdateMessages.CreateHost(UpdateMessages.End));
            UpdateStateInfo Final;
            do
            {
                // Progress messages of the programming may still be on their way; the end is Verified/Done or a failure.
                Final = NextState((Pass == 1) ? 15000 : 60000);
            }
            while ((Final.State == UpdateState.Programming) || (Final.State == UpdateState.Receiving));
            Require(Final, (Pass == 1) ? UpdateState.Verified : UpdateState.Done, "Ending pass " + Pass);
        }

        /// <summary>Pass 1 then pass 2: the Host checks the file, then programs its EEPROM from the second copy.</summary>
        public void Update(UpdatePackage Package, Action<string, int> Progress)
        {
            SendPass(Package, 1, delegate (int Percent)
            {
                if (Progress != null)
                    Progress("checking", Percent);
            });
            SendPass(Package, 2, delegate (int Percent)
            {
                if (Progress != null)
                    Progress("programming", Percent);
            });
        }

        public UpdateStateInfo QueryState(int TimeoutMs)
        {
            return (Query(TimeoutMs));
        }

        /// <summary>Leaves update mode without a restart: the normal USB devices come back.</summary>
        public void Leave()
        {
            Send(UpdateMessages.CreateHost(UpdateMessages.Leave));
        }

        /// <summary>Restarts the Host: method 0 = the Host resets itself, 1 = it lets its watchdog fire.</summary>
        public void Restart(byte Method)
        {
            Send(new Message(MessageChannel.HostUpdate, UpdateMessages.Restart, 0, new byte[] { Method }));
        }
    }
}
