using System;
using System.Collections.Generic;

namespace VulpesDolch.Host.Common.Input
{
    /// <summary>
    /// USB HID keyboard usages (usage page 0x07) and the tables that lead to them: PC scan codes (what a capture window gets from the OS),
    /// key names (for the command line) and US layout characters (for typing text).
    /// </summary>
    public static class HidKeys
    {
        public const byte LeftControl = 0xE0;
        public const byte LeftShift = 0xE1;
        public const byte LeftAlt = 0xE2;
        public const byte LeftGui = 0xE3;
        public const byte RightControl = 0xE4;
        public const byte RightShift = 0xE5;
        public const byte RightAlt = 0xE6;
        public const byte RightGui = 0xE7;

        public const byte Enter = 0x28;
        public const byte Escape = 0x29;
        public const byte Backspace = 0x2A;
        public const byte Tab = 0x2B;
        public const byte Space = 0x2C;
        public const byte CapsLock = 0x39;
        public const byte PrintScreen = 0x46;
        public const byte ScrollLock = 0x47;
        public const byte Pause = 0x48;
        public const byte NumLock = 0x53;

        // Keyboard LED bits (HID output report).
        public const byte LedNumLock = 0x01;
        public const byte LedCapsLock = 0x02;
        public const byte LedScrollLock = 0x04;

        private static readonly Dictionary<string, byte> Names = BuildNames();

        /// <summary>True for the eight modifier usages 0xE0 - 0xE7.</summary>
        public static bool IsModifier(byte Usage)
        {
            return ((Usage >= 0xE0) && (Usage <= 0xE7));
        }

        /// <summary>The bit of a modifier usage in the modifier byte of the keyboard report.</summary>
        public static byte ModifierBit(byte Usage)
        {
            return ((byte)(1 << (Usage - 0xE0)));
        }

        /// <summary>Looks a key up by name: "a", "f5", "enter", "ctrl", "lshift", "rwin", "numpad5", "pageup", ... (case does not matter).</summary>
        public static bool TryFromName(string Name, out byte Usage)
        {
            Usage = 0;
            if (Name == null)
                return (false);

            return (Names.TryGetValue(Name.Trim().ToLowerInvariant(), out Usage));
        }

        /// <summary>
        /// The usage of a PC (set 1) scan code, as Windows reports it in WM_KEYDOWN / raw input: <paramref name="Extended"/> is the E0 prefix.
        /// Returns false for keys that have none.
        /// </summary>
        public static bool TryFromScanCode(int ScanCode, bool Extended, out byte Usage)
        {
            Usage = 0;
            if ((ScanCode < 0) || (ScanCode > 0x7F))
                return (false);

            byte Found = (Extended == true) ? ExtendedScan[ScanCode] : PlainScan[ScanCode];
            if (Found == 0)
                return (false);

            Usage = Found;
            return (true);
        }

        /// <summary>The usage that types a character on a US keyboard, and whether shift has to be held. False when there is no such key.</summary>
        public static bool TryFromCharacter(char Character, out byte Usage, out bool Shift)
        {
            Usage = 0;
            Shift = false;

            if ((Character >= 'a') && (Character <= 'z'))
            {
                Usage = (byte)(0x04 + (Character - 'a'));
                return (true);
            }
            if ((Character >= 'A') && (Character <= 'Z'))
            {
                Usage = (byte)(0x04 + (Character - 'A'));
                Shift = true;
                return (true);
            }
            if ((Character >= '1') && (Character <= '9'))
            {
                Usage = (byte)(0x1E + (Character - '1'));
                return (true);
            }
            if (Character == '0')
            {
                Usage = 0x27;
                return (true);
            }

            const string ShiftedDigits = "!@#$%^&*()";
            int Index = ShiftedDigits.IndexOf(Character);
            if (Index >= 0)
            {
                Usage = (byte)((Index == 9) ? 0x27 : (0x1E + Index));
                Shift = true;
                return (true);
            }

            // Plain and shifted variants of the punctuation keys, in usage order 0x2D .. 0x38.
            const string PlainPunctuation = "-=[]\\#;'`,./";
            const string ShiftedPunctuation = "_+{}|\u0001:\"~<>?";
            Index = PlainPunctuation.IndexOf(Character);
            if ((Index >= 0) && (Character != '#'))
            {
                Usage = (byte)(0x2D + Index);
                return (true);
            }
            Index = ShiftedPunctuation.IndexOf(Character);
            if (Index >= 0)
            {
                Usage = (byte)(0x2D + Index);
                Shift = true;
                return (true);
            }

            switch (Character)
            {
                case ' ':
                    Usage = Space;
                    return (true);
                case '\n':
                case '\r':
                    Usage = Enter;
                    return (true);
                case '\t':
                    Usage = Tab;
                    return (true);
                case '#':
                    Usage = 0x20;       // shift 3 on a US keyboard
                    Shift = true;
                    return (true);
                default:
                    return (false);
            }
        }

        private static Dictionary<string, byte> BuildNames()
        {
            Dictionary<string, byte> Table = new Dictionary<string, byte>();
            int Index;

            for (Index = 0; Index < 26; Index++)
                Table[((char)('a' + Index)).ToString()] = (byte)(0x04 + Index);
            for (Index = 1; Index <= 9; Index++)
                Table[Index.ToString()] = (byte)(0x1E + Index - 1);
            Table["0"] = 0x27;
            for (Index = 1; Index <= 12; Index++)
                Table["f" + Index] = (byte)(0x3A + Index - 1);
            for (Index = 13; Index <= 24; Index++)
                Table["f" + Index] = (byte)(0x68 + Index - 13);
            for (Index = 1; Index <= 9; Index++)
                Table["numpad" + Index] = (byte)(0x59 + Index - 1);
            Table["numpad0"] = 0x62;

            Table["enter"] = Enter;
            Table["return"] = Enter;
            Table["esc"] = Escape;
            Table["escape"] = Escape;
            Table["backspace"] = Backspace;
            Table["tab"] = Tab;
            Table["space"] = Space;
            Table["minus"] = 0x2D;
            Table["equals"] = 0x2E;
            Table["lbracket"] = 0x2F;
            Table["rbracket"] = 0x30;
            Table["backslash"] = 0x31;
            Table["semicolon"] = 0x33;
            Table["quote"] = 0x34;
            Table["grave"] = 0x35;
            Table["comma"] = 0x36;
            Table["period"] = 0x37;
            Table["slash"] = 0x38;
            Table["capslock"] = CapsLock;
            Table["printscreen"] = PrintScreen;
            Table["scrolllock"] = ScrollLock;
            Table["pause"] = Pause;
            Table["insert"] = 0x49;
            Table["home"] = 0x4A;
            Table["pageup"] = 0x4B;
            Table["delete"] = 0x4C;
            Table["del"] = 0x4C;
            Table["end"] = 0x4D;
            Table["pagedown"] = 0x4E;
            Table["right"] = 0x4F;
            Table["left"] = 0x50;
            Table["down"] = 0x51;
            Table["up"] = 0x52;
            Table["numlock"] = NumLock;
            Table["numpaddivide"] = 0x54;
            Table["numpadmultiply"] = 0x55;
            Table["numpadminus"] = 0x56;
            Table["numpadplus"] = 0x57;
            Table["numpadenter"] = 0x58;
            Table["numpaddecimal"] = 0x63;
            Table["menu"] = 0x65;
            Table["application"] = 0x65;

            Table["ctrl"] = LeftControl;
            Table["lctrl"] = LeftControl;
            Table["control"] = LeftControl;
            Table["shift"] = LeftShift;
            Table["lshift"] = LeftShift;
            Table["alt"] = LeftAlt;
            Table["lalt"] = LeftAlt;
            Table["win"] = LeftGui;
            Table["lwin"] = LeftGui;
            Table["gui"] = LeftGui;
            Table["rctrl"] = RightControl;
            Table["rshift"] = RightShift;
            Table["ralt"] = RightAlt;
            Table["altgr"] = RightAlt;
            Table["rwin"] = RightGui;
            return (Table);
        }

        // PC scan code set 1 (make codes) -> HID usage; 0 = no such key. Without the E0 prefix ...
        private static readonly byte[] PlainScan = BuildPlainScan();

        // ... and with it (the grey keys of the 101 key layout).
        private static readonly byte[] ExtendedScan = BuildExtendedScan();

        private static byte[] BuildPlainScan()
        {
            byte[] Table = new byte[128];
            byte[] Row = new byte[]
            {
                0x00, 0x29, 0x1E, 0x1F, 0x20, 0x21, 0x22, 0x23, 0x24, 0x25, 0x26, 0x27, 0x2D, 0x2E, 0x2A, 0x2B,     // 00 - 0F
                0x14, 0x1A, 0x08, 0x15, 0x17, 0x1C, 0x18, 0x0C, 0x12, 0x13, 0x2F, 0x30, 0x28, 0xE0, 0x04, 0x16,     // 10 - 1F
                0x07, 0x09, 0x0A, 0x0B, 0x0D, 0x0E, 0x0F, 0x33, 0x34, 0x35, 0xE1, 0x31, 0x1D, 0x1B, 0x06, 0x19,     // 20 - 2F
                0x05, 0x11, 0x10, 0x36, 0x37, 0x38, 0xE5, 0x55, 0xE2, 0x2C, 0x39, 0x3A, 0x3B, 0x3C, 0x3D, 0x3E,     // 30 - 3F
                0x3F, 0x40, 0x41, 0x42, 0x43, 0x53, 0x47, 0x5F, 0x60, 0x61, 0x56, 0x5C, 0x5D, 0x5E, 0x57, 0x59,     // 40 - 4F
                0x5A, 0x5B, 0x62, 0x63, 0x00, 0x00, 0x64, 0x44, 0x45, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,     // 50 - 5F
                0x00, 0x00, 0x00, 0x00, 0x68, 0x69, 0x6A, 0x6B, 0x6C, 0x6D, 0x6E, 0x00, 0x00, 0x00, 0x00, 0x00,     // 60 - 6F
                0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00      // 70 - 7F
            };
            Array.Copy(Row, Table, 128);
            return (Table);
        }

        private static byte[] BuildExtendedScan()
        {
            byte[] Table = new byte[128];
            Table[0x1C] = 0x58;     // keypad enter
            Table[0x1D] = RightControl;
            Table[0x35] = 0x54;     // keypad /
            Table[0x37] = PrintScreen;
            Table[0x38] = RightAlt;
            Table[0x45] = NumLock;
            Table[0x46] = Pause;    // ctrl + break
            Table[0x47] = 0x4A;     // home
            Table[0x48] = 0x52;     // up
            Table[0x49] = 0x4B;     // page up
            Table[0x4B] = 0x50;     // left
            Table[0x4D] = 0x4F;     // right
            Table[0x4F] = 0x4D;     // end
            Table[0x50] = 0x51;     // down
            Table[0x51] = 0x4E;     // page down
            Table[0x52] = 0x49;     // insert
            Table[0x53] = 0x4C;     // delete
            Table[0x5B] = LeftGui;
            Table[0x5C] = RightGui;
            Table[0x5D] = 0x65;     // menu
            return (Table);
        }
    }
}
