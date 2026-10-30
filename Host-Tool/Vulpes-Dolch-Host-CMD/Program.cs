using System;
using System.Collections.Generic;
using System.IO;
using System.Security.Cryptography;
using System.Text;
using System.Threading;
using VulpesDolch.Host.Common;
using VulpesDolch.Host.Common.Link;
using VulpesDolch.Host.Common.Protocol;
using VulpesDolch.Host.Common.Update;

namespace VulpesDolch.Host.Cmd
{
    /// <summary>
    /// Command line tool for the firmware of the Vulpes Dolch Multi USB Device: makes signed update files, installs them into the Host and the Client,
    /// shows versions and creates the update key. Only one program may use the Host device at a time: stop the Windows service first.
    /// </summary>
    internal static class Program
    {
        private static int Main(string[] Args)
        {
            if (Args.Length == 0)
            {
                PrintUsage();
                return (2);
            }

            try
            {
                string Command = Args[0].ToLowerInvariant();

                if (Command == "update-pack")
                    return (RunUpdatePack(Args));
                if (Command == "update-client")
                    return (RunUpdateClient(Args));
                if (Command == "update-host")
                    return (RunUpdateHost(Args));
                if (Command == "update-host-state")
                    return (RunUpdateHostState(Args));
                if (Command == "image-version")
                    return (RunImageVersion(Args));
                if (Command == "keygen")
                    return (RunKeyGen(Args));
                if (Command == "version")
                {
                    Console.WriteLine("Vulpes-Dolch-Host-CMD, library " + BuildVersion.Text);
                    return (0);
                }

                Console.Error.WriteLine("Unknown command '" + Args[0] + "'.");
                PrintUsage();
                return (2);
            }
            catch (UpdateException Problem)
            {
                Console.Error.WriteLine("Update failed: " + Problem.Message);
                return (1);
            }
            catch (IOException Problem)
            {
                Console.Error.WriteLine("Error: " + Problem.Message);
                return (1);
            }
        }

        private static void PrintUsage()
        {
            Console.WriteLine("Vulpes-Dolch-Host-CMD <command> [options]");
            Console.WriteLine();
            Console.WriteLine("  keygen [--out <UpdateKey.hex>] [--header <UpdateKey.h>]");
            Console.WriteLine("        Creates a new random update key: the hex file for this tool and the matching header for the firmware build.");
            Console.WriteLine("  update-pack <image.img> <out.vdu> --target host|client [--version <packed number>] [--key <file>] [--raw]");
            Console.WriteLine("        Signs a firmware image into an update file. The version is read from the image by default.");
            Console.WriteLine("  update-client <file.vdu> [--key <file>] [--no-commit] [--no-restart]");
            Console.WriteLine("        Installs an update file into the Client (through the Host's update mode).");
            Console.WriteLine("  update-host <file.vdu> [--no-restart] [--method reset|watchdog] [--key <file>]");
            Console.WriteLine("        Installs an update file into the Host.");
            Console.WriteLine("  update-host-state [--leave]");
            Console.WriteLine("        Shows the update state of the Host; --leave takes it out of update mode.");
            Console.WriteLine("  image-version <image.img|file.vdu>");
            Console.WriteLine("        Shows the firmware version inside an image or an update file.");
            Console.WriteLine("  version");
            Console.WriteLine();
            Console.WriteLine("The key is looked up in --key, the environment variable VULPES_UPDATE_KEY, and Update\\UpdateKey.hex above the tool.");
            Console.WriteLine("Exit codes: 0 ok, 1 failed, 2 usage, 3 another program uses the Host device, 4 the device does not answer.");
        }

        /// <summary>Creates a random 32 byte update key and writes it as hex (for this tool) and as a C header (for the firmware build).</summary>
        private static int RunKeyGen(string[] Args)
        {
            string HexFile = Path.Combine("Update", "UpdateKey.hex");
            string HeaderFile = Path.Combine("Update", "UpdateKey.h");

            for (int Index = 1; Index < Args.Length; Index++)
            {
                if ((Args[Index] == "--out") && (Index + 1 < Args.Length))
                    HexFile = Args[++Index];
                else if ((Args[Index] == "--header") && (Index + 1 < Args.Length))
                    HeaderFile = Args[++Index];
                else
                {
                    Console.Error.WriteLine("Unexpected argument '" + Args[Index] + "'.");
                    return (2);
                }
            }

            if ((File.Exists(HexFile) == true) || (File.Exists(HeaderFile) == true))
            {
                Console.Error.WriteLine("Refusing to overwrite an existing key file (" + HexFile + " / " + HeaderFile + "): delete it first. Firmware signed with another key is refused by devices that carry the old one.");
                return (1);
            }

            byte[] Key = RandomNumberGenerator.GetBytes(32);
            StringBuilder Hex = new StringBuilder();
            StringBuilder Bytes = new StringBuilder();
            for (int Index = 0; Index < Key.Length; Index++)
            {
                Hex.Append(Key[Index].ToString("x2"));
                Bytes.Append("0x" + Key[Index].ToString("X2") + ((Index < Key.Length - 1) ? ", " : ""));
            }

            Directory.CreateDirectory(Path.GetDirectoryName(Path.GetFullPath(HexFile)));
            Directory.CreateDirectory(Path.GetDirectoryName(Path.GetFullPath(HeaderFile)));
            File.WriteAllText(HexFile, Hex.ToString() + "\r\n");
            File.WriteAllText(HeaderFile, "#ifndef UPDATE_KEY_H\r\n#define UPDATE_KEY_H\r\n\r\n#define UPDATE_KEY_BYTES    { " + Bytes.ToString() + " }\r\n\r\n#endif\r\n");

            Console.WriteLine("Key written to " + HexFile + " and " + HeaderFile + ".");
            Console.WriteLine("Keep these two files secret and build both firmwares with this header: devices accept only update files signed with the key they were built with.");
            return (0);
        }
        /// <summary>Finds the update key: the --key file, else the VULPES_UPDATE_KEY environment variable, else the repository's Update\UpdateKey.hex above the tool.</summary>
        private static string FindKeyFile(string Explicit)
        {
            if (string.IsNullOrEmpty(Explicit) == false)
                return (Explicit);

            string FromEnvironment = Environment.GetEnvironmentVariable("VULPES_UPDATE_KEY");
            if (string.IsNullOrEmpty(FromEnvironment) == false)
                return (FromEnvironment);

            string Directory = AppContext.BaseDirectory;
            for (int Level = 0; (Level < 10) && (string.IsNullOrEmpty(Directory) == false); Level++)
            {
                string Candidate = Path.Combine(Directory, "Update", "UpdateKey.hex");
                if (File.Exists(Candidate) == true)
                    return (Candidate);
                Directory = Path.GetDirectoryName(Directory);
            }

            return (null);
        }

        /// <summary>Shows the version stamped into a firmware image (.img) or into an update file (.vdu).</summary>
        private static int RunImageVersion(string[] Args)
        {
            if (Args.Length != 2)
            {
                Console.Error.WriteLine("Usage: image-version <image.img|file.vdu>");
                return (2);
            }

            byte[] Data = File.ReadAllBytes(Args[1]);
            if (Args[1].EndsWith(".vdu", StringComparison.OrdinalIgnoreCase) == true)
            {
                UpdatePackage Package = UpdatePackage.Parse(Data);
                Console.WriteLine(Package.Target + " update file, version " + FirmwareVersion.Format(Package.ImageVersion));
                return (0);
            }

            uint Packed;
            if (FirmwareVersion.TryFindInImage(Data, out Packed) == false)
            {
                Console.WriteLine("No version tag in the image.");
                return (1);
            }

            Console.WriteLine("Firmware image, version " + FirmwareVersion.Format(Packed));
            return (0);
        }

        /// <summary>Builds a signed update file from an FX3 boot image.</summary>
        private static int RunUpdatePack(string[] Args)
        {
            string Target = null;
            string KeyFile = null;
            uint Version = 0;
            bool Compress = true;
            List<string> Files = new List<string>();

            for (int Index = 1; Index < Args.Length; Index++)
            {
                if ((Args[Index] == "--target") && (Index + 1 < Args.Length))
                    Target = Args[++Index];
                else if ((Args[Index] == "--version") && (Index + 1 < Args.Length) && (uint.TryParse(Args[Index + 1], out Version) == true))
                    Index++;
                else if ((Args[Index] == "--key") && (Index + 1 < Args.Length))
                    KeyFile = Args[++Index];
                else if (Args[Index] == "--raw")
                    Compress = false;
                else if (Args[Index].StartsWith("--") == false)
                    Files.Add(Args[Index]);
                else
                {
                    Console.Error.WriteLine("Unexpected argument '" + Args[Index] + "'.");
                    return (2);
                }
            }

            if ((Files.Count != 2) || ((Target != "host") && (Target != "client")))
            {
                Console.Error.WriteLine("Usage: update-pack <image.img> <out.vdu> --target host|client [--version <packed number>] [--key <file>] [--raw]  (the version is read from the image\'s version tag by default)");
                return (2);
            }

            string FoundKey = FindKeyFile(KeyFile);
            if (FoundKey == null)
            {
                Console.Error.WriteLine("No key: use --key <file>, or set VULPES_UPDATE_KEY, or keep Update\\UpdateKey.hex above the tool.");
                return (2);
            }

            byte[] Image = File.ReadAllBytes(Files[0]);
            if (Version == 0)
            {
                if (FirmwareVersion.TryFindInImage(Image, out Version) == false)
                {
                    Console.Error.WriteLine("The image has no version tag (built before the build stamp existed?); give --version <packed number>.");
                    return (2);
                }
            }

            UpdatePackage Package = UpdatePackage.Create(Image, (Target == "host") ? UpdateTarget.Host : UpdateTarget.Client, Version, UpdatePackage.LoadKey(FoundKey), Compress);
            if (Package.Verify(UpdatePackage.LoadKey(FoundKey)) == false)
            {
                Console.Error.WriteLine("Internal error: the file just built does not verify.");
                return (1);
            }

            File.WriteAllBytes(Files[1], Package.File);
            Console.WriteLine("Update file " + Files[1] + ": " + Package.Target + ", version " + FirmwareVersion.Format(Package.ImageVersion) + ", image " + Package.ImageLength + " bytes, file " +
                Package.File.Length + " bytes" + ((Package.Compressed == true) ? " (compressed)" : " (not compressed)") + ", image CRC " + Package.ImageCrc.ToString("X8") + ".");
            return (0);
        }

        /// <summary>Opens the Host device's WinUSB link, retrying for a while (the update mode shows up as another device after a few seconds).</summary>
        private static HostSession OpenHostSessionWithRetry(int TimeoutSeconds, out HostUpdater HostPart, out ClientUpdater ClientPart)
        {
            DateTime Deadline = DateTime.UtcNow.AddSeconds(TimeoutSeconds);
            while (DateTime.UtcNow < Deadline)
            {
                HostSession Session = null;
                try
                {
                    Session = new HostSession(new BulkLinkSession(WinUsbBlockTransport.Open()));
                    HostPart = new HostUpdater(Session.Link);
                    ClientPart = new ClientUpdater(Session.Link);
                    if (Session.Connect(2000) == HostConnectResult.Ok)
                        return (Session);
                }
                catch (IOException)
                {
                }
                catch (InvalidOperationException)
                {
                }

                if (Session != null)
                    Session.Dispose();
                Thread.Sleep(1500);
            }

            HostPart = null;
            ClientPart = null;
            return (null);
        }

        /// <summary>
        /// Updates the Client's firmware: puts the Host (and with it the Client) into update mode - the Host then shows up as the single Update Mode
        /// device -, sends the signed file to the Client through it, lets the Client program its EEPROM, and leaves update mode (the Host resets the
        /// Client, which boots the new firmware).
        /// </summary>
        private static int RunUpdateClient(string[] Args)
        {
            string FileName = null;
            string KeyFile = null;
            bool Commit = true;
            bool Restart = true;

            for (int Index = 1; Index < Args.Length; Index++)
            {
                if ((Args[Index] == "--key") && (Index + 1 < Args.Length))
                    KeyFile = Args[++Index];
                else if (Args[Index] == "--no-commit")
                    Commit = false;
                else if (Args[Index] == "--no-restart")
                    Restart = false;
                else if ((Args[Index].StartsWith("--") == false) && (FileName == null))
                    FileName = Args[Index];
                else
                {
                    Console.Error.WriteLine("Unexpected argument '" + Args[Index] + "'.");
                    return (2);
                }
            }

            if (FileName == null)
            {
                Console.Error.WriteLine("Usage: update-client <file.vdu> [--key <file>] [--no-commit] [--no-restart]");
                return (2);
            }

            UpdatePackage Package = UpdatePackage.Load(FileName);
            if (Package.Target != UpdateTarget.Client)
            {
                Console.Error.WriteLine("This update file is for the " + Package.Target + ", not for the Client.");
                return (2);
            }

            string FoundKey = FindKeyFile(KeyFile);
            if (FoundKey != null)
            {
                if (Package.Verify(UpdatePackage.LoadKey(FoundKey)) == false)
                {
                    Console.Error.WriteLine("The update file does not verify against the key " + FoundKey + ".");
                    return (1);
                }
                Console.WriteLine("The file verifies against the key.");
            }
            else
                Console.WriteLine("(No key here: the Client does the checking.)");

            using (SingleInstanceLock InstanceLock = SingleInstanceLock.TryAcquire())
            {
                if (InstanceLock == null)
                {
                    Console.Error.WriteLine("Another copy of the Host tool is already using the Host device.");
                    return (3);
                }

                HostUpdater HostPart;
                ClientUpdater ClientPart;
                HostSession Session = OpenHostSessionWithRetry(10, out HostPart, out ClientPart);
                if (Session == null)
                {
                    Console.Error.WriteLine("The Host device does not answer.");
                    return (4);
                }

                try
                {
                    Console.WriteLine("Putting the Host and the Client into update mode (the Host shows up as one single \"Update Mode\" device) ...");
                    HostPart.Enter(10000);
                    Session.Dispose();
                    Session = null;

                    Thread.Sleep(4000);
                    Session = OpenHostSessionWithRetry(40, out HostPart, out ClientPart);
                    if (Session == null)
                    {
                        Console.Error.WriteLine("The Update Mode device did not show up within 40 s.");
                        return (4);
                    }
                    Console.WriteLine("Connected to the Update Mode device.");

                    ClientPart.Enter(15000);
                    int LastPercent = -10;
                    uint Version = ClientPart.Send(Package, delegate (int Percent)
                    {
                        if (Percent >= LastPercent + 10)
                        {
                            Console.WriteLine("  sent " + Percent + " %");
                            LastPercent = Percent;
                        }
                    });
                    Console.WriteLine("The Client accepted the file (image version " + FirmwareVersion.Format(Version) + ").");

                    if (Commit == false)
                    {
                        Console.WriteLine("Not programming (--no-commit). Both stay in update mode.");
                        return (0);
                    }

                    int LastProgrammed = -10;
                    Console.WriteLine("Programming the Client's EEPROM ...");
                    ClientPart.Commit(delegate (int Percent)
                    {
                        if (Percent >= LastProgrammed + 10)
                        {
                            Console.WriteLine("  programmed " + Percent + " %");
                            LastProgrammed = Percent;
                        }
                    }, 120000);
                    Console.WriteLine("The EEPROM holds the new image and was read back and checked.");

                    if (Restart == true)
                    {
                        HostPart.Leave();
                        Thread.Sleep(2000);
                        Console.WriteLine("Left update mode: the Host's normal devices come back and the Client restarts from its EEPROM.");
                    }
                    return (0);
                }
                catch (UpdateException Problem)
                {
                    Console.Error.WriteLine("Update failed: " + Problem.Message);
                    return (1);
                }
                finally
                {
                    if (Session != null)
                        Session.Dispose();
                }
            }
        }

        /// <summary>Updates the Host device's own firmware (its boot EEPROM) from a signed update file.</summary>
        private static int RunUpdateHost(string[] Args)
        {
            string FileName = null;
            bool Restart = true;
            byte Method = 0;

            for (int Index = 1; Index < Args.Length; Index++)
            {
                if (Args[Index] == "--no-restart")
                    Restart = false;
                else if ((Args[Index] == "--method") && (Index + 1 < Args.Length))
                {
                    string Text = Args[++Index];
                    if (Text == "reset")
                        Method = 0;
                    else if (Text == "watchdog")
                        Method = 1;
                    else
                    {
                        Console.Error.WriteLine("--method must be reset or watchdog.");
                        return (2);
                    }
                }
                else if ((Args[Index].StartsWith("--") == false) && (FileName == null))
                    FileName = Args[Index];
                else
                {
                    Console.Error.WriteLine("Unexpected argument '" + Args[Index] + "'.");
                    return (2);
                }
            }

            if (FileName == null)
            {
                Console.Error.WriteLine("Usage: update-host <file.vdu> [--no-restart] [--method reset|watchdog]");
                return (2);
            }

            UpdatePackage Package = UpdatePackage.Load(FileName);
            if (Package.Target != UpdateTarget.Host)
            {
                Console.Error.WriteLine("This update file is for the " + Package.Target + ", not for the Host.");
                return (2);
            }

            using (SingleInstanceLock InstanceLock = SingleInstanceLock.TryAcquire())
            {
                if (InstanceLock == null)
                {
                    Console.Error.WriteLine("Another copy of the Host tool is already using the Host device.");
                    return (3);
                }

                HostUpdater Updater;
                ClientUpdater Unused;
                HostSession Session = OpenHostSessionWithRetry(10, out Updater, out Unused);
                if (Session == null)
                {
                    Console.Error.WriteLine("The Host device does not answer.");
                    return (4);
                }

                try
                {
                    Console.WriteLine("Putting the Host into update mode (it shows up as one single \"Update Mode\" device) ...");
                    Updater.Enter(10000);
                    Session.Dispose();
                    Session = null;

                    // The USB side is switched after about a second: wait for the update mode device.
                    Thread.Sleep(4000);
                    Session = OpenHostSessionWithRetry(40, out Updater, out Unused);
                    if (Session == null)
                    {
                        Console.Error.WriteLine("The Update Mode device did not show up within 40 s.");
                        return (4);
                    }
                    Console.WriteLine("Connected to the Update Mode device.");

                    string LastPhase = null;
                    int LastPercent = -10;
                    Updater.Update(Package, delegate (string Phase, int Percent)
                    {
                        if ((Phase != LastPhase) || (Percent >= LastPercent + 20))
                        {
                            Console.WriteLine("  " + Phase + " " + Percent + " %");
                            LastPhase = Phase;
                            LastPercent = Percent;
                        }
                    });
                    Console.WriteLine("The Host's EEPROM holds the new image and was read back and checked.");

                    if (Restart == true)
                    {
                        Updater.Restart(Method);
                        Thread.Sleep(2000);
                        Console.WriteLine("The Host restarts (" + ((Method == 0) ? "reset" : "watchdog") + ").");
                    }
                    else
                    {
                        Updater.Leave();
                        Thread.Sleep(2000);       // the Host handles the message before the session goes away
                        Console.WriteLine("Left update mode, the normal devices come back (the new firmware runs after the next restart).");
                    }
                    return (0);
                }
                catch (UpdateException Problem)
                {
                    Console.Error.WriteLine("Update failed: " + Problem.Message);
                    try
                    {
                        Updater.Leave();
                    }
                    catch (UpdateException)
                    {
                    }
                    return (1);
                }
                finally
                {
                    if (Session != null)
                        Session.Dispose();
                }
            }
        }

        /// <summary>Shows the update state of the Host and optionally takes it out of update mode again.</summary>
        private static int RunUpdateHostState(string[] Args)
        {
            bool Leave = (Args.Length > 1) && (Args[1] == "--leave");

            using (SingleInstanceLock InstanceLock = SingleInstanceLock.TryAcquire())
            {
                if (InstanceLock == null)
                {
                    Console.Error.WriteLine("Another copy of the Host tool is already using the Host device.");
                    return (3);
                }

                HostUpdater Updater;
                ClientUpdater Unused;
                using (HostSession Session = OpenHostSessionWithRetry(10, out Updater, out Unused))
                {
                    if (Session == null)
                    {
                        Console.Error.WriteLine("The Host device does not answer.");
                        return (4);
                    }

                    Console.WriteLine("Host update state: " + Updater.QueryState(3000));
                    if (Leave == true)
                    {
                        Updater.Leave();
                        Thread.Sleep(1500);
                        Console.WriteLine("Leave sent.");
                    }
                    return (0);
                }
            }
        }
    }
}
