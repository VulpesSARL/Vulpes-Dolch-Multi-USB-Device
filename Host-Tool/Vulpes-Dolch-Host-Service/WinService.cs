using System;
using System.ComponentModel;
using System.Runtime.InteropServices;
using System.Threading;

namespace Vulpes_Dolch_Host_Service
{
    /// <summary>
    /// A thin native Windows service wrapper (advapi32) instead of ServiceBase: it needs nothing beyond the framework and it can accept the PRESHUTDOWN
    /// control, which the framework's ServiceBase cannot. The service body runs through <see cref="IServiceBody"/>.
    /// </summary>
    internal interface IServiceBody
    {
        void OnStart();

        /// <summary>Stop requested by the SCM. Return when stopped.</summary>
        void OnStop();

        /// <summary>The system is shutting down (after the users were logged off). Called repeatedly until it returns true; each call may take a few seconds at most,
        /// the wrapper reports the progress to the SCM in between so that the shutdown screen keeps waiting.</summary>
        bool OnPreshutdownStep(int ElapsedSeconds);
    }

    internal static class WinService
    {
        private const uint SERVICE_WIN32_OWN_PROCESS = 0x10;
        private const uint SERVICE_START_PENDING = 2;
        private const uint SERVICE_STOP_PENDING = 3;
        private const uint SERVICE_RUNNING = 4;
        private const uint SERVICE_STOPPED = 1;
        private const uint SERVICE_ACCEPT_STOP = 0x1;
        private const uint SERVICE_ACCEPT_PRESHUTDOWN = 0x100;
        private const uint SERVICE_CONTROL_STOP = 1;
        private const uint SERVICE_CONTROL_INTERROGATE = 4;
        private const uint SERVICE_CONTROL_PRESHUTDOWN = 0xF;
        private const uint SC_MANAGER_ALL_ACCESS = 0xF003F;
        private const uint SERVICE_ALL_ACCESS = 0xF01FF;
        private const uint SERVICE_AUTO_START = 2;
        private const uint SERVICE_ERROR_NORMAL = 1;
        private const uint SERVICE_CONFIG_DESCRIPTION = 1;
        private const uint SERVICE_CONFIG_PRESHUTDOWN_INFO = 7;
        private const uint DELETE = 0x10000;
        private const uint SERVICE_NO_CHANGE = 0xFFFFFFFF;
        private const int ERROR_SERVICE_ALREADY_RUNNING = 1056;
        private const int ERROR_SERVICE_MARKED_FOR_DELETE = 1072;
        private const int ERROR_SERVICE_EXISTS = 1073;

        /// <summary>How long the SCM waits for the service at the preshutdown (the shutdown is held for up to 5 minutes).</summary>
        public const int PreshutdownTimeoutMs = 300000;

        [StructLayout(LayoutKind.Sequential)]
        private struct SERVICE_STATUS
        {
            public uint dwServiceType;
            public uint dwCurrentState;
            public uint dwControlsAccepted;
            public uint dwWin32ExitCode;
            public uint dwServiceSpecificExitCode;
            public uint dwCheckPoint;
            public uint dwWaitHint;
        }

        [StructLayout(LayoutKind.Sequential, CharSet = CharSet.Unicode)]
        private struct SERVICE_TABLE_ENTRY
        {
            public string lpServiceName;
            public IntPtr lpServiceProc;
        }

        [StructLayout(LayoutKind.Sequential)]
        private struct SERVICE_PRESHUTDOWN_INFO
        {
            public uint dwPreshutdownTimeout;
        }

        [StructLayout(LayoutKind.Sequential, CharSet = CharSet.Unicode)]
        private struct SERVICE_DESCRIPTION
        {
            public string lpDescription;
        }

        private delegate void ServiceMainProc(uint Argc, IntPtr Argv);

        private delegate uint HandlerExProc(uint Control, uint EventType, IntPtr EventData, IntPtr Context);

        [DllImport("advapi32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
        private static extern bool StartServiceCtrlDispatcher([In] SERVICE_TABLE_ENTRY[] Table);

        [DllImport("advapi32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
        private static extern IntPtr RegisterServiceCtrlHandlerEx(string Name, HandlerExProc Handler, IntPtr Context);

        [DllImport("advapi32.dll", SetLastError = true)]
        private static extern bool SetServiceStatus(IntPtr Handle, ref SERVICE_STATUS Status);

        [DllImport("advapi32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
        private static extern IntPtr OpenSCManager(string Machine, string Database, uint Access);

        [DllImport("advapi32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
        private static extern IntPtr CreateService(IntPtr Manager, string Name, string Display, uint Access, uint Type, uint Start, uint ErrorControl, string BinaryPath,
            string LoadOrderGroup, IntPtr TagId, string Dependencies, string Account, string Password);

        [DllImport("advapi32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
        private static extern IntPtr OpenService(IntPtr Manager, string Name, uint Access);

        [DllImport("advapi32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
        private static extern bool ChangeServiceConfig(IntPtr Service, uint ServiceType, uint StartType, uint ErrorControl, string BinaryPath, string LoadOrderGroup, IntPtr TagId,
            string Dependencies, string Account, string Password, string DisplayName);

        [DllImport("advapi32.dll", SetLastError = true)]
        private static extern bool DeleteService(IntPtr Service);

        [DllImport("advapi32.dll", SetLastError = true)]
        private static extern bool CloseServiceHandle(IntPtr Handle);

        [DllImport("advapi32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
        private static extern bool ChangeServiceConfig2(IntPtr Service, uint InfoLevel, ref SERVICE_PRESHUTDOWN_INFO Info);

        [DllImport("advapi32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
        private static extern bool ChangeServiceConfig2(IntPtr Service, uint InfoLevel, ref SERVICE_DESCRIPTION Info);

        [DllImport("advapi32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
        private static extern bool StartService(IntPtr Service, uint Argc, IntPtr Argv);

        // Kept in static fields so that the garbage collector never frees the delegates the SCM calls back.
        private static ServiceMainProc MainProc;
        private static HandlerExProc HandlerProc;
        private static IServiceBody Body;
        private static string Name;
        private static IntPtr StatusHandle;
        private static uint CheckPoint;
        private static readonly ManualResetEventSlim StopRequested = new ManualResetEventSlim(false);
        private static int PreshutdownStarted;

        /// <summary>Runs the service until the SCM stops it. Returns false when the process was not started by the SCM.</summary>
        public static bool Run(string ServiceName, IServiceBody ServiceBody)
        {
            Name = ServiceName;
            Body = ServiceBody;
            MainProc = ServiceMain;

            SERVICE_TABLE_ENTRY[] Table = new SERVICE_TABLE_ENTRY[2];
            Table[0].lpServiceName = ServiceName;
            Table[0].lpServiceProc = Marshal.GetFunctionPointerForDelegate(MainProc);
            Table[1].lpServiceName = null;
            Table[1].lpServiceProc = IntPtr.Zero;

            return (StartServiceCtrlDispatcher(Table));
        }

        private static void ServiceMain(uint Argc, IntPtr Argv)
        {
            HandlerProc = Handler;
            StatusHandle = RegisterServiceCtrlHandlerEx(Name, HandlerProc, IntPtr.Zero);
            if (StatusHandle == IntPtr.Zero)
                return;

            Report(SERVICE_START_PENDING, 0, 10000);
            try
            {
                Body.OnStart();
            }
            catch (Exception)
            {
                Report(SERVICE_STOPPED, 0, 0);
                return;
            }
            Report(SERVICE_RUNNING, SERVICE_ACCEPT_STOP | SERVICE_ACCEPT_PRESHUTDOWN, 0);

            StopRequested.Wait();
        }

        private static uint Handler(uint Control, uint EventType, IntPtr EventData, IntPtr Context)
        {
            if (Control == SERVICE_CONTROL_INTERROGATE)
                return (0);

            if (Control == SERVICE_CONTROL_STOP)
            {
                Report(SERVICE_STOP_PENDING, 0, 15000);
                Thread Stopper = new Thread(delegate ()
                {
                    try
                    {
                        Body.OnStop();
                    }
                    finally
                    {
                        Report(SERVICE_STOPPED, 0, 0);
                        StopRequested.Set();
                    }
                });
                Stopper.Start();
                return (0);
            }

            if (Control == SERVICE_CONTROL_PRESHUTDOWN)
            {
                if (Interlocked.Exchange(ref PreshutdownStarted, 1) == 0)
                {
                    Report(SERVICE_STOP_PENDING, 0, 10000);
                    Thread Holder = new Thread(PreshutdownMain);
                    Holder.Start();
                }
                return (0);
            }

            return (0);
        }

        private static void PreshutdownMain()
        {
            DateTime Started = DateTime.UtcNow;
            try
            {
                // Each step reports to the SCM, so the wait hint never runs out while the hold is going on (up to PreshutdownTimeoutMs in total).
                while ((DateTime.UtcNow - Started).TotalMilliseconds < PreshutdownTimeoutMs - 10000)
                {
                    if (Body.OnPreshutdownStep((int)(DateTime.UtcNow - Started).TotalSeconds) == true)
                        break;
                    Report(SERVICE_STOP_PENDING, 0, 10000);
                }

                Body.OnStop();
            }
            finally
            {
                Report(SERVICE_STOPPED, 0, 0);
                StopRequested.Set();
            }
        }

        private static void Report(uint State, uint Accepted, uint WaitHint)
        {
            SERVICE_STATUS Status = new SERVICE_STATUS();
            Status.dwServiceType = SERVICE_WIN32_OWN_PROCESS;
            Status.dwCurrentState = State;
            Status.dwControlsAccepted = Accepted;
            Status.dwWin32ExitCode = 0;
            Status.dwServiceSpecificExitCode = 0;
            Status.dwCheckPoint = (State == SERVICE_RUNNING || State == SERVICE_STOPPED) ? 0 : ++CheckPoint;
            Status.dwWaitHint = WaitHint;
            SetServiceStatus(StatusHandle, ref Status);
        }

        /// <summary>
        /// Registers the service (own process, automatic start, LocalSystem) with the 5 minute preshutdown timeout and starts it. A service that is already registered is not an
        /// error: its program path, display name and settings are brought up to date (so a repair, an upgrade or an install over an older copy works), and a service that is
        /// only marked for deletion (the old one was still running) is waited for.
        /// </summary>
        public static void Install(string ServiceName, string DisplayName, string Description, string BinaryPath)
        {
            IntPtr Manager = OpenSCManager(null, null, SC_MANAGER_ALL_ACCESS);
            if (Manager == IntPtr.Zero)
                throw new Win32Exception(Marshal.GetLastWin32Error());

            try
            {
                IntPtr Service = IntPtr.Zero;
                for (int Attempt = 0; Attempt < 40; Attempt++)
                {
                    Service = CreateService(Manager, ServiceName, DisplayName, SERVICE_ALL_ACCESS, SERVICE_WIN32_OWN_PROCESS, SERVICE_AUTO_START, SERVICE_ERROR_NORMAL,
                        BinaryPath, null, IntPtr.Zero, null, null, null);
                    if (Service != IntPtr.Zero)
                        break;

                    int Error = Marshal.GetLastWin32Error();
                    if (Error == ERROR_SERVICE_EXISTS)
                    {
                        Service = OpenService(Manager, ServiceName, SERVICE_ALL_ACCESS);
                        if (Service == IntPtr.Zero)
                            throw new Win32Exception(Marshal.GetLastWin32Error());

                        if (ChangeServiceConfig(Service, SERVICE_NO_CHANGE, SERVICE_AUTO_START, SERVICE_NO_CHANGE, BinaryPath, null, IntPtr.Zero, null, null, null, DisplayName) == false)
                        {
                            int ChangeError = Marshal.GetLastWin32Error();
                            CloseServiceHandle(Service);
                            throw new Win32Exception(ChangeError);
                        }
                        Console.WriteLine("The service was already registered; its settings were updated.");
                        break;
                    }

                    // The old registration is still being removed (the service was running when it was deleted): wait up to 20 seconds.
                    if (Error != ERROR_SERVICE_MARKED_FOR_DELETE)
                        throw new Win32Exception(Error);
                    Thread.Sleep(500);
                }

                if (Service == IntPtr.Zero)
                    throw new Win32Exception(ERROR_SERVICE_MARKED_FOR_DELETE);

                try
                {
                    SERVICE_PRESHUTDOWN_INFO Preshutdown = new SERVICE_PRESHUTDOWN_INFO();
                    Preshutdown.dwPreshutdownTimeout = PreshutdownTimeoutMs;
                    if (ChangeServiceConfig2(Service, SERVICE_CONFIG_PRESHUTDOWN_INFO, ref Preshutdown) == false)
                        throw new Win32Exception(Marshal.GetLastWin32Error());

                    SERVICE_DESCRIPTION DescriptionInfo = new SERVICE_DESCRIPTION();
                    DescriptionInfo.lpDescription = Description;
                    ChangeServiceConfig2(Service, SERVICE_CONFIG_DESCRIPTION, ref DescriptionInfo);

                    if ((StartService(Service, 0, IntPtr.Zero) == false) && (Marshal.GetLastWin32Error() != ERROR_SERVICE_ALREADY_RUNNING))
                        Console.WriteLine("Installed, but not started: " + new Win32Exception(Marshal.GetLastWin32Error()).Message);
                }
                finally
                {
                    CloseServiceHandle(Service);
                }
            }
            finally
            {
                CloseServiceHandle(Manager);
            }
        }

        public static void Uninstall(string ServiceName)
        {
            IntPtr Manager = OpenSCManager(null, null, SC_MANAGER_ALL_ACCESS);
            if (Manager == IntPtr.Zero)
                throw new Win32Exception(Marshal.GetLastWin32Error());

            try
            {
                IntPtr Service = OpenService(Manager, ServiceName, DELETE);
                if (Service == IntPtr.Zero)
                    throw new Win32Exception(Marshal.GetLastWin32Error());

                try
                {
                    if (DeleteService(Service) == false)
                        throw new Win32Exception(Marshal.GetLastWin32Error());
                }
                finally
                {
                    CloseServiceHandle(Service);
                }
            }
            finally
            {
                CloseServiceHandle(Manager);
            }
        }
    }
}
