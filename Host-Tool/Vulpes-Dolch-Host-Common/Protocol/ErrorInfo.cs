namespace VulpesDolch.Host.Common.Protocol
{
    /// <summary>Payload of ERROR (either direction).</summary>
    public sealed class ErrorInfo
    {
        public const int PayloadSize = 6;

        public ErrorCode Code { get; set; }

        /// <summary>Tag of the message the error is about (0 if none).</summary>
        public ushort RefTag { get; set; }

        /// <summary>Link sequence number of the report the error is about (0 if none).</summary>
        public ushort RefSequence { get; set; }
    }
}
