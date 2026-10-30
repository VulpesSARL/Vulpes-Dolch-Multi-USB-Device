namespace VulpesDolch.Host.Common.Protocol
{
    /// <summary>Code of an ERROR control message.</summary>
    public enum ErrorCode : byte
    {
        UnknownChannel = 1,
        UnknownType = 2,
        BadLength = 3,
        MediaIdMismatch = 4,
        SequenceGap = 5,
        NoHello = 6,
        Busy = 7,
        CrcMismatch = 8,

        /// <summary>The message is for the Client but no Client application is running (yet).</summary>
        ClientAbsent = 9
    }
}
