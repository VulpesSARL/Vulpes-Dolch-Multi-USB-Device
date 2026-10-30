using System;

namespace VulpesDolch.Host.Common.Link
{
    /// <summary>
    /// Writes raw bulk data on the dedicated bulk data endpoint of the Host device (the second bulk OUT pipe, GPIF thread 2): no message framing, the Host is told
    /// beforehand with the control message BULK_ARM how many bytes (a multiple of 16384) follow.
    /// </summary>
    public interface IBulkDataWriter
    {
        /// <summary>True when the device has the dedicated bulk pipe.</summary>
        bool IsAvailable { get; }

        /// <summary>Writes the first <paramref name="Count"/> bytes of <paramref name="Data"/> as one transfer; returns when the device has accepted all of it. False on failure.</summary>
        bool WriteBulk(byte[] Data, int Count);
    }
}
