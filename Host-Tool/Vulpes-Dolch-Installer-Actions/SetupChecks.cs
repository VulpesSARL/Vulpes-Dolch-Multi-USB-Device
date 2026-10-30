using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.IO.Pipes;
using System.ServiceProcess;
using System.Text;
using System.Threading.Tasks;
using System.Web.Script.Serialization;
using Microsoft.Win32;
using WixToolset.Dtf.WindowsInstaller;

namespace VulpesDolch.InstallerActions
{
    /// <summary>
    /// Runs before the setup changes anything (install, repair, upgrade and removal): the GUI must not be running, and when the Vulpes Dolch service is running it is asked
    /// (over its pipe) what the target PC has loaded - any CD or disk image in it stops the setup with a message, because the service is stopped and replaced by the setup.
    /// </summary>
    public static class SetupChecks
    {
        private const string ServiceName = "VulpesDolchHost";
        private const string PipeName = "VulpesDolchHostService";
        private const string GuiProcessName = "Vulpes-Dolch-Host-Tool";
        private const int PipeWaitMs = 10000;
        private const string LocationKey = @"Software\Vulpes\Dolch USB-Multi-Tool";
        private const string LocationValue = "InstallFolder";

        [CustomAction]
        public static ActionResult CheckSetupPreconditions(Session Setup)
        {
            try
            {
                UseEarlierInstallFolder(Setup);
                string Problem = FindGuiProblem();
                if (Problem == null)
                    Problem = FindServiceProblem(Setup);

                if (Problem == null)
                {
                    Setup.Log("Vulpes Dolch setup check: the GUI is not running and no media is loaded.");
                    return (ActionResult.Success);
                }

                Setup.Log("Vulpes Dolch setup check failed: " + Problem);
                using (Record Message = new Record(1))
                {
                    Message.FormatString = "[1]";
                    Message[1] = Problem;
                    Setup.Message((InstallMessage)((int)InstallMessage.User | 0x10 /* MB_ICONERROR */), Message);
                }
                return (ActionResult.Failure);
            }
            catch (Exception Failure)
            {
                // A check that itself breaks must not hide the reason: the setup stops, the log has the exception.
                Setup.Log("Vulpes Dolch setup check failed with an exception: " + Failure);
                using (Record Message = new Record(1))
                {
                    Message.FormatString = "[1]";
                    Message[1] = "The setup could not check whether the Vulpes Dolch service has media loaded:\r\n\r\n" + Failure.Message;
                    Setup.Message((InstallMessage)((int)InstallMessage.User | 0x10), Message);
                }
                return (ActionResult.Failure);
            }
        }

        /// <summary>
        /// An install or upgrade proposes the folder the program is already in instead of the default one: first the folder the setup itself recorded (HKLM, 64 bit view), for
        /// an install of an older setup the folder of the registered service. Not when the folder was given on the command line (the property is set then), and not in
        /// maintenance mode (the installer knows the folder).
        /// </summary>
        private static void UseEarlierInstallFolder(Session Setup)
        {
            if ((Setup["INSTALLFOLDER"].Length > 0) || (Setup["Installed"].Length > 0))
                return;

            string Folder = null;
            try
            {
                using (RegistryKey Machine = RegistryKey.OpenBaseKey(RegistryHive.LocalMachine, RegistryView.Registry64))
                {
                    using (RegistryKey Key = Machine.OpenSubKey(LocationKey))
                    {
                        if (Key != null)
                            Folder = Key.GetValue(LocationValue) as string;
                    }

                    if (string.IsNullOrEmpty(Folder) == true)
                    {
                        using (RegistryKey Service = Machine.OpenSubKey(@"SYSTEM\CurrentControlSet\Services\" + ServiceName))
                        {
                            string Image = (Service == null) ? null : Service.GetValue("ImagePath") as string;
                            if (string.IsNullOrEmpty(Image) == false)
                                Folder = Path.GetDirectoryName(Image.Trim().Trim('"'));
                        }
                    }
                }
            }
            catch (Exception Problem)
            {
                Setup.Log("Could not read the earlier install folder: " + Problem.Message);
                return;
            }

            if ((string.IsNullOrEmpty(Folder) == true) || (Directory.Exists(Folder) == false))
                return;

            if (Folder.EndsWith("\\") == false)
                Folder += "\\";
            Setup.Log("Earlier install folder: " + Folder);
            Setup["INSTALLFOLDER"] = Folder;
        }

        private static string FindGuiProblem()
        {
            Process[] Running = Process.GetProcessesByName(GuiProcessName);
            if (Running.Length == 0)
                return (null);

            return ("Vulpes Dolch USB-Multi-Tool is running (" + Running.Length + ((Running.Length == 1) ? " copy" : " copies") + ", possibly of another user).\r\n\r\n" +
                "Close it and run the setup again.");
        }

        /// <summary>Null when there is no service, when it is stopped, or when it reports no media; otherwise the message for the user.</summary>
        private static string FindServiceProblem(Session Setup)
        {
            ServiceControllerStatus Status;
            try
            {
                using (ServiceController Service = new ServiceController(ServiceName))
                    Status = Service.Status;
            }
            catch (InvalidOperationException)
            {
                Setup.Log("The service " + ServiceName + " is not installed.");
                return (null);
            }

            Setup.Log("The service " + ServiceName + " is " + Status + ".");
            if (Status == ServiceControllerStatus.Stopped)
                return (null);

            if (Status != ServiceControllerStatus.Running)
                return ("The Vulpes Dolch service is " + Status.ToString().ToLowerInvariant() + ". Try again in a moment.");

            Dictionary<string, object> State = null;
            Task<Dictionary<string, object>> Asking = Task.Run(delegate () { return (AskState()); });
            if (Asking.Wait(PipeWaitMs) == true)
                State = Asking.Result;

            if (State == null)
                return ("The Vulpes Dolch service is running but does not answer.\r\n\r\nRestart the service or the PC, then run the setup again.");

            string CdPath = Text(State, "CdPath");
            string DiskPath = Text(State, "DiskPath");
            bool Cd = (Flag(State, "CdPresent") == true) || (CdPath.Length > 0);
            bool Disk = (Flag(State, "DiskPresent") == true) || (DiskPath.Length > 0);
            if ((Cd == false) && (Disk == false))
                return (null);

            // The message box of the installer cuts long texts (and abbreviates paths): the box names the files only, the log has the full paths.
            Setup.Log("Media loaded: CD-ROM [" + CdPath + "], disk [" + DiskPath + "].");
            StringBuilder Loaded = new StringBuilder();
            if (Cd == true)
                Loaded.Append("CD-ROM: " + ((CdPath.Length > 0) ? ShortName(CdPath) : "a disc"));
            if (Disk == true)
                Loaded.Append(((Loaded.Length > 0) ? ", disk: " : "Disk: ") + ((DiskPath.Length > 0) ? ShortName(DiskPath) : "a disk"));

            return ("Media is still loaded in the target PC (" + Loaded + ").\r\n\r\nEject it in Vulpes Dolch USB-Multi-Tool, then run the setup again.");
        }

        private static string ShortName(string Path)
        {
            try
            {
                string Name = System.IO.Path.GetFileName(Path.TrimEnd('\\'));
                return ((Name.Length > 0) ? Name : Path);
            }
            catch (ArgumentException)
            {
                return (Path);
            }
        }

        private static string Text(Dictionary<string, object> State, string Name)
        {
            object Value;
            if ((State.TryGetValue(Name, out Value) == false) || (Value == null))
                return ("");
            return (Convert.ToString(Value));
        }

        private static bool Flag(Dictionary<string, object> State, string Name)
        {
            object Value;
            return ((State.TryGetValue(Name, out Value) == true) && (Value is bool) && ((bool)Value == true));
        }

        /// <summary>Asks the service for its state (GetState) over its pipe: frames are a 4 byte little endian length followed by UTF-8 JSON. Null when it cannot be read.</summary>
        private static Dictionary<string, object> AskState()
        {
            JavaScriptSerializer Json = new JavaScriptSerializer();
            using (NamedPipeClientStream Pipe = new NamedPipeClientStream(".", PipeName, PipeDirection.InOut, PipeOptions.None))
            {
                Pipe.Connect(5000);

                // GetState is command 0.
                Dictionary<string, object> Request = new Dictionary<string, object>();
                Request["Id"] = 1;
                Request["Request"] = new Dictionary<string, object> { { "Command", 0 } };
                byte[] Body = Encoding.UTF8.GetBytes(Json.Serialize(Request));
                byte[] Frame = new byte[4 + Body.Length];
                BitConverter.GetBytes(Body.Length).CopyTo(Frame, 0);
                Body.CopyTo(Frame, 4);
                Pipe.Write(Frame, 0, Frame.Length);
                Pipe.Flush();

                // State pushes (Id 0) may come first; the answer to the request has Id 1.
                for (int Count = 0; Count < 50; Count++)
                {
                    byte[] Header = new byte[4];
                    if (ReadExactly(Pipe, Header) == false)
                        return (null);

                    int Length = BitConverter.ToInt32(Header, 0);
                    if ((Length <= 0) || (Length > 1024 * 1024))
                        return (null);

                    byte[] Payload = new byte[Length];
                    if (ReadExactly(Pipe, Payload) == false)
                        return (null);

                    Dictionary<string, object> Answer = Json.DeserializeObject(Encoding.UTF8.GetString(Payload)) as Dictionary<string, object>;
                    if ((Answer == null) || (Answer.ContainsKey("State") == false))
                        continue;

                    object Id;
                    if ((Answer.TryGetValue("Id", out Id) == true) && (Convert.ToInt32(Id) == 1))
                        return (Answer["State"] as Dictionary<string, object>);
                }
                return (null);
            }
        }

        private static bool ReadExactly(Stream Source, byte[] Buffer)
        {
            int Done = 0;
            while (Done < Buffer.Length)
            {
                int Got = Source.Read(Buffer, Done, Buffer.Length - Done);
                if (Got <= 0)
                    return (false);
                Done += Got;
            }
            return (true);
        }
    }
}
