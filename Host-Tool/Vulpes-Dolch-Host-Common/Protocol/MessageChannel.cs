namespace VulpesDolch.Host.Common.Protocol
{
    /// <summary>The Channel field of a message block: which function the message belongs to.</summary>
    public enum MessageChannel : byte
    {
        Control = 0,
        CdRom = 1,
        Hdd = 2,
        KeyboardMouse = 3,
        Midi = 4,
        Serial = 5,

        /// <summary>Firmware update of the Client (and later the Host): see the Update folder.</summary>
        Update = 6,

        /// <summary>Firmware update of the Host device itself.</summary>
        HostUpdate = 7
    }
}
