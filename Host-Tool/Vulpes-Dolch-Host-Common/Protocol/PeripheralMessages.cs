using System;

namespace VulpesDolch.Host.Common.Protocol
{
    /// <summary>Message types of the keyboard / mouse channel (channel 3). Mirrors GPIF\PeriphProtocol.h of the firmware.</summary>
    public static class KeyboardMouseType
    {
        /// <summary>PC -> Client: 8 bytes, the HID boot keyboard report (modifiers, reserved, 6 key codes).</summary>
        public const byte KeyboardReport = 0x10;

        /// <summary>PC -> Client: 4 bytes, buttons, dx, dy, wheel (signed 8 bit, relative).</summary>
        public const byte MouseReport = 0x11;

        /// <summary>PC -> Client: no payload, every key and button up.</summary>
        public const byte ReleaseAll = 0x12;

        /// <summary>PC -> Client: no payload, the Client answers with <see cref="State"/> and <see cref="Leds"/>.</summary>
        public const byte Query = 0x13;

        /// <summary>Client -> PC: 1 byte, the keyboard LEDs the target PC set.</summary>
        public const byte Leds = 0x20;

        /// <summary>Client -> PC: 4 bytes, keyboard configured, mouse configured, LEDs, USB suspended.</summary>
        public const byte State = 0x21;
    }

    /// <summary>What the Client says about the keyboard and mouse the target PC sees.</summary>
    public sealed class KeyboardMouseState
    {
        /// <summary>The target PC has configured the Client's USB device (keyboard and mouse then exist for it).</summary>
        public bool KeyboardConfigured { get; set; }

        public bool MouseConfigured { get; set; }

        /// <summary>The LEDs the target PC set: bit 0 num lock, 1 caps lock, 2 scroll lock, 3 compose, 4 kana.</summary>
        public byte Leds { get; set; }

        /// <summary>The target PC has suspended the USB bus (it sleeps).</summary>
        public bool Suspended { get; set; }
    }

    public static class PeripheralMessages
    {
        public const int KeyboardReportSize = 8;
        public const int MouseReportSize = 4;

        public static Message CreateKeyboardReport(byte[] Report)
        {
            if ((Report == null) || (Report.Length != KeyboardReportSize))
                throw new ArgumentException("A keyboard report is 8 bytes.", "Report");

            return (new Message(MessageChannel.KeyboardMouse, KeyboardMouseType.KeyboardReport, 0, (byte[])Report.Clone()));
        }

        public static Message CreateMouseReport(byte Buttons, sbyte Dx, sbyte Dy, sbyte Wheel)
        {
            byte[] Payload = new byte[MouseReportSize];
            Payload[0] = Buttons;
            Payload[1] = (byte)Dx;
            Payload[2] = (byte)Dy;
            Payload[3] = (byte)Wheel;
            return (new Message(MessageChannel.KeyboardMouse, KeyboardMouseType.MouseReport, 0, Payload));
        }

        public static Message CreateReleaseAll()
        {
            return (new Message(MessageChannel.KeyboardMouse, KeyboardMouseType.ReleaseAll, 0, null));
        }

        public static Message CreateQuery()
        {
            return (new Message(MessageChannel.KeyboardMouse, KeyboardMouseType.Query, 0, null));
        }

        public static bool TryParseKeyboardReport(Message Source, out byte[] Report)
        {
            Report = null;
            if ((Source.Channel != MessageChannel.KeyboardMouse) || (Source.Type != KeyboardMouseType.KeyboardReport) || (Source.Payload.Length < KeyboardReportSize))
                return (false);

            Report = new byte[KeyboardReportSize];
            Array.Copy(Source.Payload, Report, KeyboardReportSize);
            return (true);
        }

        public static bool TryParseMouseReport(Message Source, out byte Buttons, out sbyte Dx, out sbyte Dy, out sbyte Wheel)
        {
            Buttons = 0;
            Dx = 0;
            Dy = 0;
            Wheel = 0;
            if ((Source.Channel != MessageChannel.KeyboardMouse) || (Source.Type != KeyboardMouseType.MouseReport) || (Source.Payload.Length < MouseReportSize))
                return (false);

            Buttons = Source.Payload[0];
            Dx = (sbyte)Source.Payload[1];
            Dy = (sbyte)Source.Payload[2];
            Wheel = (sbyte)Source.Payload[3];
            return (true);
        }

        public static Message CreateLeds(byte Leds)
        {
            return (new Message(MessageChannel.KeyboardMouse, KeyboardMouseType.Leds, 0, new byte[] { Leds }));
        }

        public static bool TryParseLeds(Message Source, out byte Leds)
        {
            Leds = 0;
            if ((Source.Channel != MessageChannel.KeyboardMouse) || (Source.Type != KeyboardMouseType.Leds) || (Source.Payload.Length < 1))
                return (false);

            Leds = Source.Payload[0];
            return (true);
        }

        public static Message CreateState(KeyboardMouseState State)
        {
            byte[] Payload = new byte[4];
            Payload[0] = (byte)((State.KeyboardConfigured == true) ? 1 : 0);
            Payload[1] = (byte)((State.MouseConfigured == true) ? 1 : 0);
            Payload[2] = State.Leds;
            Payload[3] = (byte)((State.Suspended == true) ? 1 : 0);
            return (new Message(MessageChannel.KeyboardMouse, KeyboardMouseType.State, 0, Payload));
        }

        public static bool TryParseState(Message Source, out KeyboardMouseState State)
        {
            State = null;
            if ((Source.Channel != MessageChannel.KeyboardMouse) || (Source.Type != KeyboardMouseType.State) || (Source.Payload.Length < 4))
                return (false);

            State = new KeyboardMouseState();
            State.KeyboardConfigured = Source.Payload[0] != 0;
            State.MouseConfigured = Source.Payload[1] != 0;
            State.Leds = Source.Payload[2];
            State.Suspended = Source.Payload[3] != 0;
            return (true);
        }
    }
}
