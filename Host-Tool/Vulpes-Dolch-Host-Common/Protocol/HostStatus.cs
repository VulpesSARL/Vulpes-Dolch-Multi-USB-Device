using System;

namespace VulpesDolch.Host.Common.Protocol
{
    /// <summary>Payload of STATUS (Host -> PC): the state of the Client and of its two media functions.</summary>
    public sealed class HostStatus
    {
        public const int PayloadSize = 28;

        /// <summary>The STATUS of a Host firmware that also reports the Client firmware version (bytes 28..31).</summary>
        public const int PayloadSizeWithFirmware = 32;

        public ClientState ClientState { get; set; }

        /// <summary>USB speed of the Host device towards its PC: 0 unknown, 1 full, 2 high, 3 SuperSpeed (payload byte 1).</summary>
        public byte HostUsbSpeed { get; set; }

        /// <summary>USB speed of the Client towards its PC: 0 = no PC / not enumerated, 1 full, 2 high, 3 SuperSpeed (payload byte 2).</summary>
        public byte ClientUsbSpeed { get; set; }

        /// <summary>The Host told the Client to connect its USB side (payload byte 6).</summary>
        public bool ClientUsbEnabled { get; set; }

        /// <summary>Firmware version of the running Client application, 0 = none (payload bytes 28..31); only valid when HasClientFirmware is true.</summary>
        public uint ClientFirmwareVersion { get; set; }

        /// <summary>The STATUS carried the Client firmware version (a Host firmware older than that sends 28 bytes and the HELLO_ACK value has to do).</summary>
        public bool HasClientFirmware { get; set; }

        public bool CdRomPresent { get; set; }

        /// <summary>The OS holds a PREVENT MEDIUM REMOVAL lock.</summary>
        public bool CdRomLocked { get; set; }

        public Int64 CdRomMediaId { get; set; }

        public bool HddPresent { get; set; }

        public bool HddReadOnly { get; set; }

        public Int64 HddMediaId { get; set; }
    }
}
