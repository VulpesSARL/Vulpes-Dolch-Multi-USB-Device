using System;
using System.Collections.Generic;
using System.Threading;
using VulpesDolch.Host.Common.Input;
using VulpesDolch.Host.Common.Link;
using VulpesDolch.Host.Common.Protocol;

namespace VulpesDolch.Host.Common.Functions
{
    /// <summary>
    /// The PC side of the Client's keyboard and mouse: keeps the state of the virtual keyboard (modifiers and up to
    /// six pressed keys) and the mouse buttons, and sends it to the Client as the HID boot reports the target PC receives unchanged. A report is a
    /// snapshot, so a lost one is repaired by the next; mouse movement is relative and is sent in steps of at most 127 counts.
    /// <para>
    /// Create it before the link is started (the handler of the channel has to be registered first). When the application goes away the
    /// Client releases all keys by itself (it notices the missing PC session); <see cref="ReleaseAll"/> does it on purpose.
    /// </para>
    /// </summary>
    public sealed class KeyboardMouseFunction : IDisposable
    {
        public const int MaxPressedKeys = 6;

        public const byte ButtonLeft = 0x01;
        public const byte ButtonRight = 0x02;
        public const byte ButtonMiddle = 0x04;

        private readonly ILinkLayer LinkLayer;
        private readonly object InputLock = new object();
        private readonly List<byte> PressedKeys = new List<byte>();
        private byte Modifiers;
        private byte Buttons;
        private KeyboardMouseState LastState;
        private byte LastLeds;
        private int DisposedFlag;

        /// <summary>How long a send may wait for the link (milliseconds).</summary>
        public int SendTimeoutMs { get; set; } = 1000;

        /// <summary>The target PC changed the keyboard LEDs (bit 0 num lock, 1 caps lock, 2 scroll lock).</summary>
        public event Action<byte> LedsChanged;

        /// <summary>The Client reported its USB state (configured, suspended) or the LEDs.</summary>
        public event Action<KeyboardMouseState> StateChanged;

        public KeyboardMouseFunction(ILinkLayer LinkLayer)
        {
            if (LinkLayer == null)
                throw new ArgumentNullException("LinkLayer");

            this.LinkLayer = LinkLayer;
            LinkLayer.SetHandler(MessageChannel.KeyboardMouse, OnMessage);
        }

        /// <summary>The last state the Client reported; null before the first.</summary>
        public KeyboardMouseState LastKnownState
        {
            get
            {
                lock (InputLock)
                    return (LastState);
            }
        }

        /// <summary>The keyboard LEDs the target PC set last.</summary>
        public byte Leds
        {
            get
            {
                lock (InputLock)
                    return (LastLeds);
            }
        }

        /// <summary>The keys held down now (usages), without the modifiers.</summary>
        public byte[] HeldKeys
        {
            get
            {
                lock (InputLock)
                    return (PressedKeys.ToArray());
            }
        }

        public byte HeldModifiers
        {
            get
            {
                lock (InputLock)
                    return (Modifiers);
            }
        }

        public byte HeldButtons
        {
            get
            {
                lock (InputLock)
                    return (Buttons);
            }
        }

        /// <summary>Presses a key (a HID usage, see <see cref="HidKeys"/>); modifiers are 0xE0 - 0xE7. A seventh key is ignored (the report holds six). Returns false when the report could not be sent.</summary>
        public bool KeyDown(byte Usage)
        {
            byte[] Report;

            lock (InputLock)
            {
                if (HidKeys.IsModifier(Usage) == true)
                {
                    Modifiers = (byte)(Modifiers | HidKeys.ModifierBit(Usage));
                }
                else if (Usage != 0)
                {
                    if (PressedKeys.Contains(Usage) == false)
                    {
                        if (PressedKeys.Count >= MaxPressedKeys)
                            return (false);
                        PressedKeys.Add(Usage);
                    }
                }
                else
                {
                    return (false);
                }

                Report = BuildReport();
            }

            return (SendReport(Report));
        }

        public bool KeyUp(byte Usage)
        {
            byte[] Report;

            lock (InputLock)
            {
                if (HidKeys.IsModifier(Usage) == true)
                    Modifiers = (byte)(Modifiers & ~HidKeys.ModifierBit(Usage));
                else
                    PressedKeys.Remove(Usage);

                Report = BuildReport();
            }

            return (SendReport(Report));
        }

        /// <summary>Presses and releases a key, optionally with modifiers held around it.</summary>
        public bool Tap(byte Usage, params byte[] ModifierUsages)
        {
            bool Ok = true;

            foreach (byte Modifier in ModifierUsages)
                Ok = KeyDown(Modifier) && Ok;
            Ok = KeyDown(Usage) && Ok;
            Ok = KeyUp(Usage) && Ok;
            for (int Index = ModifierUsages.Length - 1; Index >= 0; Index--)
                Ok = KeyUp(ModifierUsages[Index]) && Ok;
            return (Ok);
        }

        /// <summary>Types text on a US keyboard layout (the target PC maps the keys to characters with its own layout); characters without a key are skipped. Returns the number of characters typed.</summary>
        public int TypeText(string Text, int DelayMs)
        {
            int Typed = 0;

            if (Text == null)
                return (0);

            foreach (char Character in Text)
            {
                byte Usage;
                bool Shift;

                if (HidKeys.TryFromCharacter(Character, out Usage, out Shift) == false)
                    continue;

                if (Shift == true)
                    KeyDown(HidKeys.LeftShift);
                KeyDown(Usage);
                KeyUp(Usage);
                if (Shift == true)
                    KeyUp(HidKeys.LeftShift);
                Typed++;

                if (DelayMs > 0)
                    Thread.Sleep(DelayMs);
            }

            return (Typed);
        }

        /// <summary>Moves the pointer by Dx / Dy counts (positive: right / down). Large movements are sent in several reports.</summary>
        public bool MouseMove(int Dx, int Dy)
        {
            return (MouseMoveAndWheel(Dx, Dy, 0));
        }

        /// <summary>Turns the wheel (positive: away from the user = up).</summary>
        public bool MouseWheel(int Notches)
        {
            return (MouseMoveAndWheel(0, 0, Notches));
        }

        public bool MouseMoveAndWheel(int Dx, int Dy, int Wheel)
        {
            bool Ok = true;

            while ((Dx != 0) || (Dy != 0) || (Wheel != 0))
            {
                int StepX = Math.Clamp(Dx, -127, 127);
                int StepY = Math.Clamp(Dy, -127, 127);
                int StepWheel = Math.Clamp(Wheel, -127, 127);
                byte CurrentButtons;

                lock (InputLock)
                    CurrentButtons = Buttons;

                Ok = SendMouse(CurrentButtons, StepX, StepY, StepWheel) && Ok;
                Dx -= StepX;
                Dy -= StepY;
                Wheel -= StepWheel;
            }

            return (Ok);
        }

        /// <summary>Presses (Down) or releases a button; <paramref name="Button"/> is one of the Button* masks.</summary>
        public bool MouseButton(byte Button, bool Down)
        {
            byte NewButtons;

            lock (InputLock)
            {
                if (Down == true)
                    Buttons = (byte)(Buttons | (Button & 0x07));
                else
                    Buttons = (byte)(Buttons & ~Button);
                NewButtons = Buttons;
            }

            return (SendMouse(NewButtons, 0, 0, 0));
        }

        public bool MouseClick(byte Button)
        {
            bool Ok = MouseButton(Button, true);
            return (MouseButton(Button, false) && Ok);
        }

        /// <summary>Releases every key and button, here and on the Client.</summary>
        public bool ReleaseAll()
        {
            lock (InputLock)
            {
                PressedKeys.Clear();
                Modifiers = 0;
                Buttons = 0;
            }

            return (LinkLayer.Send(PeripheralMessages.CreateReleaseAll(), SendTimeoutMs) == LinkSendResult.Ok);
        }

        /// <summary>Asks the Client for its USB state and the LEDs; the answers arrive through <see cref="StateChanged"/> and <see cref="LedsChanged"/>.</summary>
        public bool Query()
        {
            return (LinkLayer.Send(PeripheralMessages.CreateQuery(), SendTimeoutMs) == LinkSendResult.Ok);
        }

        private byte[] BuildReport()
        {
            byte[] Report = new byte[PeripheralMessages.KeyboardReportSize];
            Report[0] = Modifiers;
            for (int Index = 0; Index < PressedKeys.Count; Index++)
                Report[2 + Index] = PressedKeys[Index];
            return (Report);
        }

        private bool SendReport(byte[] Report)
        {
            return (LinkLayer.Send(PeripheralMessages.CreateKeyboardReport(Report), SendTimeoutMs) == LinkSendResult.Ok);
        }

        private bool SendMouse(byte CurrentButtons, int Dx, int Dy, int Wheel)
        {
            return (LinkLayer.Send(PeripheralMessages.CreateMouseReport(CurrentButtons, (sbyte)Dx, (sbyte)Dy, (sbyte)Wheel), SendTimeoutMs) == LinkSendResult.Ok);
        }

        private void OnMessage(Message Incoming)
        {
            byte NewLeds;
            KeyboardMouseState NewState;

            if (PeripheralMessages.TryParseLeds(Incoming, out NewLeds) == true)
            {
                lock (InputLock)
                    LastLeds = NewLeds;

                Action<byte> Handler = LedsChanged;
                if (Handler != null)
                    Handler(NewLeds);
            }
            else if (PeripheralMessages.TryParseState(Incoming, out NewState) == true)
            {
                lock (InputLock)
                {
                    LastState = NewState;
                    LastLeds = NewState.Leds;
                }

                Action<KeyboardMouseState> Handler = StateChanged;
                if (Handler != null)
                    Handler(NewState);
            }
        }

        public void Dispose()
        {
            if (Interlocked.Exchange(ref DisposedFlag, 1) != 0)
                return;

            // Nothing may stay pressed on the target PC.
            try
            {
                LinkLayer.Send(PeripheralMessages.CreateReleaseAll(), 200);
            }
            catch (ObjectDisposedException)
            {
            }
        }
    }
}
