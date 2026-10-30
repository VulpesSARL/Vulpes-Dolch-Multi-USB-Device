using System;
using VulpesDolch.Host.Common.Protocol;

namespace VulpesDolch.Host.Common.Link
{
    /// <summary>
    /// What <see cref="HostSession"/> and the tools need from a link layer: message handlers per channel, sending a message,
    /// problem and close notifications, and the time since the last traffic. <see cref="BulkLinkSession"/> implements it over bulk
    /// transfers (WinUSB); tests can put another implementation behind it.
    /// </summary>
    public interface ILinkLayer : IDisposable
    {
        /// <summary>Raised on any thread; keep it short.</summary>
        event Action<LinkProblemKind, string> Problem;

        /// <summary>Raised once when the session closes because the transport failed.</summary>
        event Action Closed;

        bool IsClosed { get; }

        /// <summary>What this side announces in HELLO as its receive capacity (unused on the bulk link).</summary>
        int ReceiveCapacity { get; }

        /// <summary>Units written to the transport (bulk transfers).</summary>
        long ReportsSent { get; }

        /// <summary>Units read from the transport.</summary>
        long ReportsReceived { get; }

        long MillisecondsSinceLastSend { get; }

        long MillisecondsSinceLastReceive { get; }

        /// <summary>Registers the handler for one channel; it runs on the dispatch thread. Do this before Start.</summary>
        void SetHandler(MessageChannel Channel, Action<Message> Handler);

        void Start();

        LinkSendResult Send(Message Outgoing, int TimeoutMs);
    }
}
