using System;
using System.Drawing;
using System.Runtime.InteropServices;
using System.Windows.Forms;
using VulpesDolch.Host.Common.Input;
using VulpesDolch.Host.Common.Service;

namespace Vulpes_Dolch_Host_Tool
{
    /// <summary>
    /// The window to control the target PC. The picture field is the main thing (a video will be shown in it later). A click into it captures the mouse and the
    /// keyboard: every key (a low level keyboard hook swallows them, so Alt+Tab, the Windows key and so on go to the target) and the mouse movement are sent to the
    /// target PC through the service. A clean tap of left Ctrl + left Alt (nothing pressed in between) gives the input back. Ctrl+Alt+Insert is sent to the target
    /// as Ctrl+Alt+Delete (the real Ctrl+Alt+Delete never reaches an application on this PC).
    /// </summary>
    public partial class frmRemoteControl : Form
    {
        private const int WhKeyboardLl = 13;
        private const int WmKeydown = 0x0100;
        private const int WmKeyup = 0x0101;
        private const int WmSyskeydown = 0x0104;
        private const int WmSyskeyup = 0x0105;
        private const int VkPause = 0x13;
        private const int VkLControl = 0xA2;
        private const int VkLMenu = 0xA4;

        private const byte UsageInsert = 0x49;
        private const byte UsageDelete = 0x4C;
        private const int ButtonLeft = 0x01;
        private const int ButtonRight = 0x02;
        private const int ButtonMiddle = 0x04;

        private const uint LlkhfInjected = 0x10;
        private const int VkNumLock = 0x90;
        private const int VkCapital = 0x14;
        private const int VkScroll = 0x91;
        private const int LedNum = 0x01;
        private const int LedCaps = 0x02;
        private const int LedScroll = 0x04;

        private delegate IntPtr LowLevelKeyboardProc(int Code, IntPtr WParam, IntPtr LParam);

        [StructLayout(LayoutKind.Sequential)]
        private struct KbdLlHookStruct
        {
            public uint VkCode;
            public uint ScanCode;
            public uint Flags;
            public uint Time;
            public IntPtr ExtraInfo;
        }

        [DllImport("user32.dll", SetLastError = true)]
        private static extern IntPtr SetWindowsHookEx(int HookType, LowLevelKeyboardProc Callback, IntPtr Module, uint ThreadId);

        [DllImport("user32.dll", SetLastError = true)]
        [return: MarshalAs(UnmanagedType.Bool)]
        private static extern bool UnhookWindowsHookEx(IntPtr Hook);

        [DllImport("user32.dll")]
        private static extern IntPtr CallNextHookEx(IntPtr Hook, int Code, IntPtr WParam, IntPtr LParam);

        [DllImport("kernel32.dll", CharSet = CharSet.Unicode)]
        private static extern IntPtr GetModuleHandle(string ModuleName);

        [DllImport("user32.dll")]
        private static extern short GetKeyState(int VirtualKey);

        [DllImport("user32.dll")]
        private static extern void keybd_event(byte VirtualKey, byte ScanCode, uint Flags, UIntPtr ExtraInfo);

        // Raw input: the mouse movement is read as the device reports it (relative counts, like a real mouse), not as "position minus window centre".
        private const int WmInput = 0x00FF;
        private const uint RidInput = 0x10000003;
        private const uint RidevRemove = 0x00000001;
        private const ushort MouseMoveAbsolute = 0x0001;
        private const ushort MouseVirtualDesktop = 0x0002;

        [StructLayout(LayoutKind.Sequential)]
        private struct RawInputDevice
        {
            public ushort UsagePage;
            public ushort Usage;
            public uint Flags;
            public IntPtr Target;
        }

        [DllImport("user32.dll", SetLastError = true)]
        [return: MarshalAs(UnmanagedType.Bool)]
        private static extern bool RegisterRawInputDevices([In] RawInputDevice[] Devices, uint Count, uint Size);

        [DllImport("user32.dll")]
        private static extern uint GetRawInputData(IntPtr RawInput, uint Command, IntPtr Data, ref uint Size, uint HeaderSize);

        private readonly ServiceClient Client;
        private readonly InputSender Output;
        private readonly LowLevelKeyboardProc HookCallback;
        private readonly System.Windows.Forms.Timer FlushTimer = new System.Windows.Forms.Timer();
        private IntPtr HookHandle = IntPtr.Zero;
        private bool Captured;
        private bool LeftControlDown;
        private bool LeftAltDown;
        private bool CleanChord;            // Left Ctrl and left Alt are down and nothing else was pressed since.
        private bool InsertSentAsDelete;    // The Insert key that is down right now was turned into Delete.
        private int PendingDx;
        private int PendingDy;
        private int PendingWheel;
        private Point Center;
        private bool RawRegistered;
        private bool HaveAbsolute;          // Remote Desktop reports the mouse as absolute positions: the last one, to get the movement from.
        private int LastAbsoluteX;
        private int LastAbsoluteY;
        private bool DeviceReady;
        private int SavedLocalLeds;        // The lock keys of this PC as they were before the capture (restored when the input is given back).
        private int LocalLeds;             // What this PC's lock keys are set to now (bit 0 Num, 1 Caps, 2 Scroll), as far as we set them.

        /// <summary>For the designer only.</summary>
        public frmRemoteControl() : this(null)
        {
        }

        public frmRemoteControl(ServiceClient Client)
        {
            InitializeComponent();

            this.Client = Client;
            HookCallback = OnHook;

            pictureBox1.BackColor = Color.Black;
            pictureBox1.Paint += OnPicturePaint;
            pictureBox1.MouseDown += OnPictureMouseDown;
            pictureBox1.MouseUp += OnPictureMouseUp;
            pictureBox1.MouseMove += OnPictureMouseMove;
            pictureBox1.MouseWheel += OnPictureMouseWheel;
            exitToolStripMenuItem.Click += delegate (object Sender, EventArgs Args) { Close(); };

            if (Client != null)
            {
                Output = new InputSender(Client);
                Client.StateChanged += OnServiceState;
                ShowState(Client.State);
            }

            FlushTimer.Interval = 5;
            FlushTimer.Tick += OnFlushTick;
            FlushTimer.Start();
            UpdateStatusText();
        }

        // ---- the service state ----

        private void OnServiceState(ServiceState State)
        {
            if ((IsHandleCreated == false) || (IsDisposed == true))
                return;

            try
            {
                BeginInvoke(new Action(delegate () { ShowState(State); }));
            }
            catch (InvalidOperationException)
            {
                // Closing.
            }
        }

        private void ShowState(ServiceState State)
        {
            DeviceReady = (State != null) && (State.DeviceConnected == true) && (State.ClientState == "Running");
            if ((Captured == true) && (State != null))
                ApplyLocalLeds(State.TargetLeds);
            sendCTRLALTDELETEToolStripMenuItem.Enabled = DeviceReady;
            if ((DeviceReady == false) && (Captured == true))
                Release(false);
            UpdateStatusText();
            pictureBox1.Invalidate();
        }

        private void UpdateStatusText()
        {
            if (DeviceReady == false)
                lblStatusGeneral.Text = "The target PC is not reachable (Host or Client device missing).";
            else if (Captured == true)
                lblStatusGeneral.Text = "CAPTURED - tap left Ctrl + left Alt to give the keyboard and mouse back; Ctrl+Alt+Insert sends Ctrl+Alt+Delete";
            else
                lblStatusGeneral.Text = "Click in the picture to capture the keyboard and mouse.";
        }

        private void OnPicturePaint(object Sender, PaintEventArgs Args)
        {
            string Text = (DeviceReady == false) ? "No connection to the target PC" : ((Captured == true) ? "" : "Click to control the target PC");
            if (Text.Length == 0)
                return;

            using (Font TextFont = new Font("Segoe UI", 12f))
            using (SolidBrush Brush = new SolidBrush(Color.Silver))
                Args.Graphics.DrawString(Text, TextFont, Brush, 16, 16);
        }

        // ---- the mouse ----

        private void OnPictureMouseDown(object Sender, MouseEventArgs Args)
        {
            if (Captured == false)
            {
                if ((Args.Button == MouseButtons.Left) && (DeviceReady == true) && (Output != null))
                    BeginCapture();
                return;
            }

            SendButton(Args.Button, true);
        }

        private void OnPictureMouseUp(object Sender, MouseEventArgs Args)
        {
            if (Captured == true)
                SendButton(Args.Button, false);
        }

        private void OnPictureMouseWheel(object Sender, MouseEventArgs Args)
        {
            if (Captured == true)
                PendingWheel += Args.Delta / 120;
        }

        private void OnPictureMouseMove(object Sender, MouseEventArgs Args)
        {
            if (Captured == false)
                return;

            // The movement itself comes from the raw input (ProcessRawInput). In a local session the pointer is only kept in the middle of the picture so that
            // it never sits at the edge of the clip rectangle. In a Remote Desktop session this PC's pointer does not follow the one of the Remote Desktop client,
            // so moving it would only confuse: nothing is done there.
            if (SystemInformation.TerminalServerSession == true)
                return;

            Point Now = pictureBox1.PointToScreen(Args.Location);
            if (Now != Center)
                Cursor.Position = Center;
        }

        protected override void WndProc(ref Message Msg)
        {
            if ((Msg.Msg == WmInput) && (Captured == true))
                ProcessRawInput(Msg.LParam);

            base.WndProc(ref Msg);
        }

        private void ProcessRawInput(IntPtr Handle)
        {
            uint Size = 0;
            uint HeaderSize = (uint)((IntPtr.Size * 2) + 8);        // RAWINPUTHEADER: type, size, device handle, wParam

            if ((GetRawInputData(Handle, RidInput, IntPtr.Zero, ref Size, HeaderSize) != 0) || (Size == 0) || (Size > 256))
                return;

            IntPtr Buffer = Marshal.AllocHGlobal((int)Size);
            try
            {
                if (GetRawInputData(Handle, RidInput, Buffer, ref Size, HeaderSize) != Size)
                    return;

                if (Marshal.ReadInt32(Buffer, 0) != 0)              // RIM_TYPEMOUSE only
                    return;

                int Data = (int)HeaderSize;
                ushort Flags = (ushort)Marshal.ReadInt16(Buffer, Data);
                int X = Marshal.ReadInt32(Buffer, Data + 12);
                int Y = Marshal.ReadInt32(Buffer, Data + 16);

                if ((Flags & MouseMoveAbsolute) != 0)
                {
                    // Absolute (Remote Desktop, tablets, virtual machines): 0..65535 over the screen; the movement is the difference to the last position.
                    Rectangle Area = ((Flags & MouseVirtualDesktop) != 0) ? SystemInformation.VirtualScreen : Screen.PrimaryScreen.Bounds;
                    if (HaveAbsolute == true)
                    {
                        PendingDx += (int)Math.Round((X - LastAbsoluteX) * (double)Area.Width / 65535.0);
                        PendingDy += (int)Math.Round((Y - LastAbsoluteY) * (double)Area.Height / 65535.0);
                    }
                    LastAbsoluteX = X;
                    LastAbsoluteY = Y;
                    HaveAbsolute = true;
                }
                else
                {
                    PendingDx += X;
                    PendingDy += Y;
                }
            }
            finally
            {
                Marshal.FreeHGlobal(Buffer);
            }
        }

        private void RegisterRawMouse()
        {
            RawInputDevice[] Devices = new RawInputDevice[1];
            Devices[0].UsagePage = 0x01;
            Devices[0].Usage = 0x02;
            Devices[0].Flags = 0;
            Devices[0].Target = Handle;
            RawRegistered = RegisterRawInputDevices(Devices, 1, (uint)Marshal.SizeOf<RawInputDevice>());
            HaveAbsolute = false;
        }

        private void UnregisterRawMouse()
        {
            if (RawRegistered == false)
                return;

            RawInputDevice[] Devices = new RawInputDevice[1];
            Devices[0].UsagePage = 0x01;
            Devices[0].Usage = 0x02;
            Devices[0].Flags = RidevRemove;
            Devices[0].Target = IntPtr.Zero;
            RegisterRawInputDevices(Devices, 1, (uint)Marshal.SizeOf<RawInputDevice>());
            RawRegistered = false;
            HaveAbsolute = false;
        }

        private void SendButton(MouseButtons Button, bool Down)
        {
            FlushMovement();
            if (Button == MouseButtons.Left)
                Output.MouseButton(ButtonLeft, Down);
            else if (Button == MouseButtons.Right)
                Output.MouseButton(ButtonRight, Down);
            else if (Button == MouseButtons.Middle)
                Output.MouseButton(ButtonMiddle, Down);
        }

        private void OnFlushTick(object Sender, EventArgs Args)
        {
            if (Captured == true)
                FlushMovement();
        }

        private void FlushMovement()
        {
            if ((PendingDx == 0) && (PendingDy == 0) && (PendingWheel == 0))
                return;

            int Dx = PendingDx;
            int Dy = PendingDy;
            int Wheel = PendingWheel;
            PendingDx = 0;
            PendingDy = 0;
            PendingWheel = 0;
            Output.MouseMove(Dx, Dy, Wheel);
        }

        // ---- capture and release ----

        private void BeginCapture()
        {
            Center = pictureBox1.PointToScreen(new Point(pictureBox1.ClientSize.Width / 2, pictureBox1.ClientSize.Height / 2));
            if (SystemInformation.TerminalServerSession == false)
            {
                Cursor.Position = Center;
                Cursor.Clip = pictureBox1.RectangleToScreen(pictureBox1.ClientRectangle);
            }
            Cursor.Hide();
            RegisterRawMouse();
            pictureBox1.Focus();
            LeftControlDown = false;
            LeftAltDown = false;
            CleanChord = false;
            InsertSentAsDelete = false;
            PendingDx = PendingDy = PendingWheel = 0;
            Captured = true;
            SavedLocalLeds = ReadLocalLeds();
            LocalLeds = SavedLocalLeds;
            HookHandle = SetWindowsHookEx(WhKeyboardLl, HookCallback, GetModuleHandle(null), 0);
            if (Client != null && Client.State != null)
                ApplyLocalLeds(Client.State.TargetLeds);
            UpdateStatusText();
            pictureBox1.Invalidate();
        }

        /// <summary>Gives the keyboard and mouse back to this PC. Tell = true: the target gets "all keys and buttons up".</summary>
        private void Release(bool Tell)
        {
            if (Captured == false)
                return;

            Captured = false;
            UnregisterRawMouse();
            if (HookHandle != IntPtr.Zero)
            {
                UnhookWindowsHookEx(HookHandle);
                HookHandle = IntPtr.Zero;
            }
            Cursor.Clip = Rectangle.Empty;
            Cursor.Show();

            ApplyLocalLeds(SavedLocalLeds);

            if ((Tell == true) && (Output != null))
                Output.ReleaseAll();
            UpdateStatusText();
            pictureBox1.Invalidate();
        }

        protected override void OnDeactivate(EventArgs Args)
        {
            base.OnDeactivate(Args);
            Release(true);
        }

        protected override void OnFormClosing(FormClosingEventArgs Args)
        {
            Release(true);
            FlushTimer.Stop();
            if (Client != null)
                Client.StateChanged -= OnServiceState;
            if (Output != null)
                Output.Dispose();
            base.OnFormClosing(Args);
        }

        // ---- the keyboard ----

        private IntPtr OnHook(int Code, IntPtr WParam, IntPtr LParam)
        {
            if ((Code < 0) || (Captured == false))
                return (CallNextHookEx(HookHandle, Code, WParam, LParam));

            int Message = WParam.ToInt32();
            bool Down = (Message == WmKeydown) || (Message == WmSyskeydown);
            bool Up = (Message == WmKeyup) || (Message == WmSyskeyup);
            if ((Down == false) && (Up == false))
                return (CallNextHookEx(HookHandle, Code, WParam, LParam));

            KbdLlHookStruct Key = Marshal.PtrToStructure<KbdLlHookStruct>(LParam);

            // Our own lock key toggles (LED sync) are not typed by the user: let them reach this PC and do not send them to the target.
            if ((Key.Flags & LlkhfInjected) != 0)
                return (CallNextHookEx(HookHandle, Code, WParam, LParam));

            bool IsChordKey = (Key.VkCode == VkLControl) || (Key.VkCode == VkLMenu);

            if (Key.VkCode == VkLControl)
                LeftControlDown = Down;
            else if (Key.VkCode == VkLMenu)
                LeftAltDown = Down;

            if ((Down == true) && (IsChordKey == true) && (LeftControlDown == true) && (LeftAltDown == true))
                CleanChord = true;
            else if ((Down == true) && (IsChordKey == false))
                CleanChord = false;

            // Left Ctrl and left Alt were pressed and one of them is let go with nothing pressed in between: give the input back.
            if ((Up == true) && (IsChordKey == true) && (CleanChord == true))
            {
                CleanChord = false;
                LeftControlDown = false;
                LeftAltDown = false;
                BeginInvoke(new Action(delegate () { Release(true); }));
                return ((IntPtr)1);
            }

            byte Usage;
            bool Found;
            if (Key.VkCode == VkPause)
            {
                Usage = HidKeys.Pause;
                Found = true;
            }
            else
            {
                Found = HidKeys.TryFromScanCode((int)Key.ScanCode, (Key.Flags & 1) != 0, out Usage);
            }

            if (Found == true)
            {
                // Ctrl+Alt+Insert is Ctrl+Alt+Delete on the target.
                if ((Usage == UsageInsert) && (Down == true) && (LeftControlDown == true) && (LeftAltDown == true))
                    InsertSentAsDelete = true;

                if (InsertSentAsDelete == true && Usage == UsageInsert)
                {
                    if (Down == true)
                        Output.KeyDown(UsageDelete);
                    else
                    {
                        Output.KeyUp(UsageDelete);
                        InsertSentAsDelete = false;
                    }
                }
                else if (Down == true)
                {
                    Output.KeyDown(Usage);
                }
                else
                {
                    Output.KeyUp(Usage);
                }
            }

            return ((IntPtr)1);
        }

        // ---- the LEDs (like VMware: the lock keys of this PC follow the target while the input is captured) ----

        private static int ReadLocalLeds()
        {
            int Leds = 0;
            if ((GetKeyState(VkNumLock) & 1) != 0)
                Leds |= LedNum;
            if ((GetKeyState(VkCapital) & 1) != 0)
                Leds |= LedCaps;
            if ((GetKeyState(VkScroll) & 1) != 0)
                Leds |= LedScroll;
            return (Leds);
        }

        /// <summary>Sets this PC's lock keys to the wanted LEDs (negative = not known, nothing is done) by pressing the toggle keys.</summary>
        private void ApplyLocalLeds(int Wanted)
        {
            if (Wanted < 0)
                return;

            ToggleIfDifferent(LedNum, VkNumLock, 0x45, true, Wanted);
            ToggleIfDifferent(LedCaps, VkCapital, 0x3A, false, Wanted);
            ToggleIfDifferent(LedScroll, VkScroll, 0x46, false, Wanted);
        }

        private void ToggleIfDifferent(int Bit, int VirtualKey, byte ScanCode, bool Extended, int Wanted)
        {
            if (((Wanted ^ LocalLeds) & Bit) == 0)
                return;

            uint Flags = (Extended == true) ? 1u : 0u;
            keybd_event((byte)VirtualKey, ScanCode, Flags, UIntPtr.Zero);
            keybd_event((byte)VirtualKey, ScanCode, Flags | 2u, UIntPtr.Zero);
            LocalLeds ^= Bit;
        }

        // ---- the menu ----

        private void sendCTRLALTDELETEToolStripMenuItem_Click(object sender, EventArgs e)
        {
            if ((Output == null) || (DeviceReady == false))
                return;

            Output.KeyDown(HidKeys.LeftControl);
            Output.KeyDown(HidKeys.LeftAlt);
            Output.KeyDown(UsageDelete);
            Output.KeyUp(UsageDelete);
            Output.KeyUp(HidKeys.LeftAlt);
            Output.KeyUp(HidKeys.LeftControl);
        }
    }
}
