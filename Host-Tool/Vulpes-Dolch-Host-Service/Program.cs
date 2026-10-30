using System;
using System.Diagnostics;
using System.IO;
using System.Threading;
using VulpesDolch.Host.Common;
using VulpesDolch.Host.Common.Functions;
using VulpesDolch.Host.Common.Service;

namespace Vulpes_Dolch_Host_Service
{
    /// <summary>
    /// The Vulpes Dolch service: owns the Host device (so that a logoff of the user does not touch the media and the keyboard / mouse state of the target PC)
    /// and offers it to the GUI through a named pipe. At the shutdown of the PC it holds the shutdown for up to 5 minutes until the target PC has ejected the
    /// media (or the device was unplugged).
    ///
    ///   Vulpes-Dolch-Host-Service /install      register and start the service (needs administrator rights)
    ///   Vulpes-Dolch-Host-Service /uninstall    remove it
    ///   Vulpes-Dolch-Host-Service /console      run in the console (for tests; Ctrl+C stops)
    ///   Vulpes-Dolch-Host-Service /?            help
    /// </summary>
    internal static class Program
    {
        private const string HelpText =
            "Vulpes Dolch Host Service\r\n" +
            "Keeps the Vulpes Dolch USB multi device (media, keyboard, mouse) running, independent of the users who are logged on.\r\n" +
            "\r\n" +
            "Usage: Vulpes-Dolch-Host-Service [option]\r\n" +
            "\r\n" +
            "  /install     register this program as the Windows service (starts automatically, runs as LocalSystem,\r\n" +
            "               holds the PC shutdown for up to 5 minutes while media is loaded) and start it.\r\n" +
            "               Needs an administrator command prompt. The path of this exe is the one that is registered.\r\n" +
            "  /uninstall   remove the service.\r\n" +
            "  /console     run in this console instead of as a service (for tests; Ctrl+C stops).\r\n" +
            "  /?           this help.\r\n" +
            "\r\n" +
            "Without an option the program expects to be started by the Windows service control manager.\r\n";

        private static int Main(string[] Args)
        {
            if (Args.Length > 0)
            {
                // Options are written with a slash (/install), in any case.
                string Mode = (Args[0].StartsWith("/", StringComparison.Ordinal) == true) ? Args[0].Substring(1).ToLowerInvariant() : "";
                try
                {
                    if ((Mode == "?") || (Mode == "help"))
                    {
                        Console.Write(HelpText);
                        return (0);
                    }

                    if (Mode == "install")
                    {
                        WinService.Install(ServiceNames.ServiceName, ServiceNames.DisplayName, "Keeps the Vulpes Dolch USB multi device (media, keyboard, mouse) running independent of the logged-on users.",
                            "\"" + Process.GetCurrentProcess().MainModule.FileName + "\"");
                        Console.WriteLine("Service installed.");
                        return (0);
                    }

                    if (Mode == "uninstall")
                    {
                        WinService.Uninstall(ServiceNames.ServiceName);
                        Console.WriteLine("Service removed.");
                        return (0);
                    }

                    if (Mode == "console")
                        return (RunConsole());
                }
                catch (Exception Problem)
                {
                    Console.Error.WriteLine(Problem.Message);
                    return (1);
                }

                Console.Error.WriteLine("Unknown option '" + Args[0] + "'. Use /? for the options.");
                return (2);
            }

            // Started by the service control manager.
            if (WinService.Run(ServiceNames.ServiceName, new ServiceBody()) == false)
            {
                // Started by hand: show how it is meant to be used.
                Console.Write(HelpText);
                return (2);
            }
            return (0);
        }

        private static int RunConsole()
        {
            ServiceBody Body = new ServiceBody();
            Body.OnStart();
            Console.WriteLine("Running; press Ctrl+C to stop.");

            ManualResetEventSlim Stop = new ManualResetEventSlim(false);
            Console.CancelKeyPress += delegate (object Sender, ConsoleCancelEventArgs Event)
            {
                Event.Cancel = true;
                Stop.Set();
            };
            Stop.Wait();

            Body.OnStop();
            return (0);
        }
    }

    internal sealed class ServiceBody : IServiceBody
    {
        private ServiceEngine Engine;
        private PipeServer Pipe;
        private bool EjectAsked;

        public void OnStart()
        {
            HostDevice Device = new HostDevice();
            Device.EnableRecovery(new RegistryMediaState());
            Engine = new ServiceEngine(Device, new WindowsNetworkConnector());
            Engine.Log += delegate (string Text)
            {
                Console.WriteLine(Text);
            };
            Pipe = new PipeServer(Engine);
            Pipe.Start();
            Engine.Start();
        }

        public void OnStop()
        {
            if (Pipe != null)
                Pipe.Dispose();
            if (Engine != null)
                Engine.Dispose();
        }

        /// <summary>Holds the shutdown while the target PC still has media: asks it to eject (once) and waits; true = nothing left to wait for.</summary>
        public bool OnPreshutdownStep(int ElapsedSeconds)
        {
            ServiceState State = Engine.BuildState();
            if ((State.DeviceConnected == false) || (State.ClientState != "Running"))
                return (true);

            if ((State.CdPresent == false) && (State.DiskPresent == false))
                return (true);

            if (EjectAsked == false)
            {
                EjectAsked = true;
                CdRomFunction Cd = Engine.HostDevice.CdRom;
                if ((Cd != null) && (State.CdPresent == true))
                    Cd.RequestEject(2000);
            }

            Thread.Sleep(2000);
            return (false);
        }
    }
}
