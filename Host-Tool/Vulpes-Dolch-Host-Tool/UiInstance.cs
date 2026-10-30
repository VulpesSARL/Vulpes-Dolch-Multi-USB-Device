using System;
using System.Collections.Concurrent;
using System.Diagnostics;
using System.IO;
using System.IO.Pipes;
using System.Runtime.InteropServices;
using System.Security.AccessControl;
using System.Security.Principal;
using System.Text;
using System.Threading;

namespace Vulpes_Dolch_Host_Tool
{
    internal enum ExternalKind
    {
        /// <summary>Only bring the running program to the front.</summary>
        Show,
        MountCd,
        MountDisk,
    }

    /// <summary>A request from the command line (the Explorer context menu): mount a path as CD-ROM or as disk.</summary>
    internal sealed class ExternalRequest
    {
        public ExternalKind Kind;
        public string Path;

        /// <summary>Environment.TickCount64 until which the request may wait for the service, the device and the Client.</summary>
        public long Deadline;
    }

    /// <summary>
    /// One copy of the program per logon session: a second start (for example from the Explorer context menu) hands its request to the running copy over a named
    /// pipe that only this user may use, and ends.
    /// </summary>
    internal static class UiInstance
    {
        private const int ASFW_ANY = -1;

        private static Mutex InstanceMutex;
        private static readonly ConcurrentQueue<ExternalRequest> Inbox = new ConcurrentQueue<ExternalRequest>();
        private static readonly object NotifyLock = new object();
        private static Action Notify;

        [DllImport("user32.dll")]
        private static extern bool AllowSetForegroundWindow(int ProcessId);

        [DllImport("user32.dll")]
        private static extern bool SetForegroundWindow(IntPtr Window);

        private static string PipeName()
        {
            return ("VulpesDolchHostTool-" + Process.GetCurrentProcess().SessionId);
        }

        /// <summary>True for the first copy in this logon session.</summary>
        public static bool TryBecomePrimary()
        {
            bool Created;
            InstanceMutex = new Mutex(true, @"Local\VulpesDolchHostTool", out Created);
            return (Created);
        }

        /// <summary>The running copy listens for requests of later starts.</summary>
        public static void StartListener()
        {
            Thread Worker = new Thread(ListenMain);
            Worker.IsBackground = true;
            Worker.Name = "Request pipe";
            Worker.Start();
        }

        /// <summary>Hands a request to the running copy; false when it does not answer.</summary>
        public static bool Forward(ExternalRequest Request)
        {
            try
            {
                // The running copy may bring its window to the front for this start.
                AllowSetForegroundWindow(ASFW_ANY);
                using (NamedPipeClientStream Client = new NamedPipeClientStream(".", PipeName(), PipeDirection.Out))
                {
                    Client.Connect(5000);
                    using (StreamWriter Writer = new StreamWriter(Client, new UTF8Encoding(false)))
                    {
                        Writer.WriteLine(Request.Kind.ToString());
                        Writer.WriteLine(Request.Path ?? "");
                        Writer.Flush();
                    }
                }
                return (true);
            }
            catch (Exception)
            {
                return (false);
            }
        }

        /// <summary>A request for the main window (from the command line of this copy or from the pipe).</summary>
        public static void Post(ExternalRequest Request)
        {
            Inbox.Enqueue(Request);
            Action Wake;
            lock (NotifyLock)
                Wake = Notify;
            if (Wake != null)
                Wake();
        }

        /// <summary>The main window asks to be told (on any thread) when requests are waiting; null stops that.</summary>
        public static void SetNotify(Action Wake)
        {
            lock (NotifyLock)
                Notify = Wake;
        }

        public static bool TryTake(out ExternalRequest Request)
        {
            return (Inbox.TryDequeue(out Request));
        }

        public static void ToForeground(IntPtr Window)
        {
            SetForegroundWindow(Window);
        }

        private static void ListenMain()
        {
            PipeSecurity Security = new PipeSecurity();
            Security.AddAccessRule(new PipeAccessRule(WindowsIdentity.GetCurrent().User, PipeAccessRights.FullControl, AccessControlType.Allow));

            for (;;)
            {
                try
                {
                    using (NamedPipeServerStream Stream = NamedPipeServerStreamAcl.Create(PipeName(), PipeDirection.In, 1, PipeTransmissionMode.Byte, PipeOptions.None, 0, 0, Security))
                    {
                        Stream.WaitForConnection();
                        using (StreamReader Reader = new StreamReader(Stream, Encoding.UTF8))
                        {
                            string KindText = Reader.ReadLine();
                            string PathText = Reader.ReadLine();
                            ExternalKind Kind;
                            if (Enum.TryParse<ExternalKind>(KindText, out Kind) == true)
                            {
                                ExternalRequest Request = new ExternalRequest();
                                Request.Kind = Kind;
                                Request.Path = PathText;
                                Post(Request);
                            }
                        }
                    }
                }
                catch (Exception)
                {
                    // A broken client must not end the listener.
                    Thread.Sleep(200);
                }
            }
        }
    }
}
