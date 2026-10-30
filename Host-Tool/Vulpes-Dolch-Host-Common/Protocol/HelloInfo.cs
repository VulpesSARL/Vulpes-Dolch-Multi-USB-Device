namespace VulpesDolch.Host.Common.Protocol
{
    /// <summary>Payload of HELLO (PC -> Host).</summary>
    public sealed class HelloInfo
    {
        /// <summary>Major in the high byte, minor in the low byte.</summary>
        public ushort ProtocolVersion { get; set; }

        /// <summary>How many reports the sender can hold in its receive queue.</summary>
        public byte ReceiveCredits { get; set; }
    }

    /// <summary>Payload of HELLO_ACK (Host -> PC).</summary>
    public sealed class HelloAckInfo
    {
        public ushort ProtocolVersion { get; set; }

        public byte ReceiveCredits { get; set; }

        public uint HostFirmwareVersion { get; set; }

        /// <summary>0 = no Client application.</summary>
        public uint ClientFirmwareVersion { get; set; }

        public uint MaxPayload { get; set; }
    }
}
