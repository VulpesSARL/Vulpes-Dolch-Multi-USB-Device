namespace VulpesDolch.Host.Common.Link
{
    /// <summary>The outcome of sending a message on a link layer.</summary>
    public enum LinkSendResult
    {
        Ok,

        /// <summary>The link was not free within the time given; the message may be partly sent.</summary>
        Timeout,

        /// <summary>The session is closed (transport failed or session disposed).</summary>
        Closed,

        /// <summary>The payload is above the size limit; nothing was sent.</summary>
        TooLarge
    }

    /// <summary>What kind of problem a link layer reports through its problem event.</summary>
    public enum LinkProblemKind
    {
        /// <summary>A transfer with an impossible header (too short, bad length field).</summary>
        BadReport,

        /// <summary>A transfer was lost or arrived out of order; the message in progress is dropped.</summary>
        SequenceGap,

        /// <summary>Pieces that do not form a valid block (no start, too long, wrong size).</summary>
        BadBlock,

        /// <summary>A complete block failed its check word; it is dropped.</summary>
        BadCheck,

        /// <summary>A message handler threw an exception.</summary>
        HandlerFailed,

        /// <summary>The transport threw an exception; the session is closed.</summary>
        TransportFailed
    }
}
