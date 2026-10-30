using System;
using System.Collections.Concurrent;
using VulpesDolch.Host.Common.Link;
using VulpesDolch.Host.Common.Protocol;

namespace VulpesDolch.Host.Common.Update
{
    public class UpdateException : Exception
    {
        public UpdateException(string Text) : base(Text)
        {
        }
    }

    /// <summary>
    /// The PC side of the Client's firmware update mode: puts the Client into update mode (its USB side goes away), sends a signed update file,
    /// lets the Client check it and program its boot EEPROM, and restarts it. Create it before the link is started (the handler of the
    /// channel has to be registered first). Every call blocks and throws <see cref="UpdateException"/> when the Client refuses or fails.
    /// </summary>
    public sealed class ClientUpdater
    {
        /// <summary>Payload bytes of one DATA message (a multiple of 4).</summary>
        private const int ChunkSize = 8192;

        /// <summary>After this many chunks the Client is asked how much it has, so a message lost on the way shows up early.</summary>
        private const int ChunksPerCheck = 8;

        private readonly ILinkLayer LinkLayer;
        private readonly BlockingCollection<UpdateStateInfo> Inbox = new BlockingCollection<UpdateStateInfo>(256);

        public ClientUpdater(ILinkLayer LinkLayer)
        {
            this.LinkLayer = LinkLayer;
            LinkLayer.SetHandler(MessageChannel.Update, OnMessage);
        }

        private void OnMessage(Message Reply)
        {
            UpdateStateInfo Info = UpdateStateInfo.TryParse(Reply);
            if (Info == null)
                return;

            // Never block the dispatch thread: drop the oldest when nobody reads.
            while (Inbox.TryAdd(Info) == false)
                Inbox.TryTake(out UpdateStateInfo Dropped);
        }

        private void Send(Message Request)
        {
            LinkSendResult Result = LinkLayer.Send(Request, 5000);
            if (Result != LinkSendResult.Ok)
                throw new UpdateException("Sending to the Client failed: " + Result + ".");
        }

        private void ClearInbox()
        {
            while (Inbox.TryTake(out UpdateStateInfo Dropped))
            {
            }
        }

        /// <summary>Waits for the next STATE message and returns it; throws when none arrives.</summary>
        private UpdateStateInfo NextState(int TimeoutMs)
        {
            if (Inbox.TryTake(out UpdateStateInfo Info, TimeoutMs) == false)
                throw new UpdateException("The Client did not answer within " + TimeoutMs + " ms.");
            return (Info);
        }

        private static void Require(UpdateStateInfo Info, UpdateState Expected, string Step)
        {
            if (Info.State == UpdateState.Failed)
                throw new UpdateException(Step + " failed: " + Info.Error + ".");
            if (Info.State != Expected)
                throw new UpdateException(Step + ": the Client is in state " + Info.State + " (" + Info.Error + "), expected " + Expected + ".");
        }

        public UpdateStateInfo Query(int TimeoutMs)
        {
            ClearInbox();
            Send(UpdateMessages.Create(UpdateMessages.Query));
            return (NextState(TimeoutMs));
        }

        /// <summary>Puts the Client into update mode: its USB devices disappear from the PC. Does nothing if it is in update mode already.</summary>
        public void Enter(int TimeoutMs)
        {
            UpdateStateInfo Info = Query(2000);
            if (Info.State != UpdateState.Normal)
                return;

            ClearInbox();
            Send(UpdateMessages.Create(UpdateMessages.Enter));
            Require(NextState(TimeoutMs), UpdateState.UpdateMode, "Entering update mode");
        }

        /// <summary>Sends the file and lets the Client check it (signature, target, lengths). Returns the image version the Client read from it.</summary>
        public uint Send(UpdatePackage Package, Action<int> Progress)
        {
            byte[] File = Package.File;

            ClearInbox();
            Send(UpdateMessages.CreateBegin((uint)File.Length));
            Require(NextState(5000), UpdateState.Receiving, "Starting the transfer");

            int Sent = 0;
            int Chunks = 0;
            while (Sent < File.Length)
            {
                int Count = Math.Min(ChunkSize, File.Length - Sent);
                Send(UpdateMessages.CreateData((uint)Sent, File, Sent, Count));
                Sent += Count;
                Chunks++;

                if (((Chunks % ChunksPerCheck) == 0) || (Sent == File.Length))
                {
                    UpdateStateInfo Info = Query(5000);
                    Require(Info, UpdateState.Receiving, "Transfer");
                    if (Info.Detail != (uint)Sent)
                        throw new UpdateException("The Client has " + Info.Detail + " bytes, " + Sent + " were sent.");
                    if (Progress != null)
                        Progress((int)((long)Sent * 100 / File.Length));
                }
            }

            ClearInbox();
            Send(UpdateMessages.Create(UpdateMessages.End));
            UpdateStateInfo Verified = NextState(10000);
            Require(Verified, UpdateState.Verified, "Checking the file");
            return (Verified.Extra);
        }

        /// <summary>Lets the Client program its EEPROM with the checked file and waits until it is done (seconds).</summary>
        public void Commit(Action<int> Progress, int TimeoutMs)
        {
            ClearInbox();
            Send(UpdateMessages.Create(UpdateMessages.Commit));

            DateTime Deadline = DateTime.UtcNow.AddMilliseconds(TimeoutMs);
            for (;;)
            {
                int Left = (int)(Deadline - DateTime.UtcNow).TotalMilliseconds;
                if (Left <= 0)
                    throw new UpdateException("Programming did not finish within " + TimeoutMs + " ms.");

                UpdateStateInfo Info = NextState(Left);
                if (Info.State == UpdateState.Programming)
                {
                    if (Progress != null)
                        Progress((int)Info.Detail);
                    continue;
                }

                Require(Info, UpdateState.Done, "Programming");
                return;
            }
        }

        /// <summary>Resets the Client; it boots from its EEPROM and its USB devices come back.</summary>
        public void Restart()
        {
            Send(UpdateMessages.Create(UpdateMessages.Restart));
        }
    }
}
