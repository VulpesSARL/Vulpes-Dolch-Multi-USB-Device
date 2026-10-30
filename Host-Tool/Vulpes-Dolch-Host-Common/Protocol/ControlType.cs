namespace VulpesDolch.Host.Common.Protocol
{
    /// <summary>Message types of the control channel.</summary>
    public static class ControlType
    {
        public const byte Hello = 0x01;
        public const byte HelloAck = 0x02;
        public const byte Bye = 0x03;
        public const byte Ping = 0x04;
        public const byte Pong = 0x05;
        public const byte Status = 0x06;
        public const byte Error = 0x07;
        public const byte StatusRequest = 0x08;

        /// <summary>Host -> Client only (never reaches the PC): u8 1 = a PC is connected, 0 = not.</summary>
        public const byte PcLink = 0x09;

        /// <summary>Client -> Host only: u32 Client firmware version, then the 28 byte STATUS body (media fields).</summary>
        public const byte ClientStatus = 0x0A;

        /// <summary>PC -> Host only: reset the Client board (no payload). The Host answers with STATUS changes (Lost, Booting, Running) or ERROR (busy, unknown type).</summary>
        public const byte ResetClient = 0x0B;

        /// <summary>PC -> Host only: u32 bytes (a multiple of 16384) - the next transfer on the dedicated bulk OUT pipe goes to the Client over GPIF thread 2.</summary>
        public const byte BulkArm = 0x0E;

        /// <summary>Debug command (0x70 and up, Host builds with CY_FX_DEBUG_COMMANDS only): the Host answers with a STATUS and then does a cold reset, which drops it into the USB bootloader while the download jumper is set.</summary>
        public const byte DebugResetHost = 0x70;

        /// <summary>Debug speed test (Host builds with CY_FX_DEBUG_COMMANDS): a message the Host counts and drops.</summary>
        public const byte DebugSinkData = 0x72;

        /// <summary>Debug speed test: u32 count, u32 size - the Host sends that many messages of that type and payload size.</summary>
        public const byte DebugSource = 0x73;

        /// <summary>Debug speed test: the Host answers with the same type: u32 messages, u32 bytes counted since the last end.</summary>
        public const byte DebugSinkEnd = 0x74;

        /// <summary>Debug: u32 mode, u32 period - the Host damages every period-th GPIF buffer it sends to the Client (1 bit flip, 2 zeroed word, 3 blank buffer, 4 deleted word; period 0 = off).</summary>
        public const byte DebugFault = 0x75;

        /// <summary>Debug: u32 what - 1 hangs the Host's PC link thread in an endless loop (the watchdog has to reset the Host), 2 runs the fatal error handler.</summary>
        public const byte DebugCrash = 0x76;
    }
}
