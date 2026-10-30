using System;
using System.Collections.Generic;
using System.ComponentModel;
using System.IO;
using System.Runtime.InteropServices;
using VulpesDolch.Host.Common.Service;

namespace Vulpes_Dolch_Host_Service
{
    /// <summary>
    /// Opens images on network shares for the service. The service runs as SYSTEM, which on a domain-joined PC reaches only what the machine account may use; for
    /// anything else the user gives credentials in the GUI and they are used here with WNetAddConnection2 (a temporary, non-persistent connection of the service's
    /// logon session) for as long as an image on that share is loaded. The password is passed to Windows and not kept.
    /// </summary>
    internal sealed class WindowsNetworkConnector : INetworkConnector
    {
        private const uint RESOURCETYPE_DISK = 1;
        private const uint CONNECT_TEMPORARY = 4;
        private const int NO_ERROR = 0;
        private const int ERROR_SESSION_CREDENTIAL_CONFLICT = 1219;

        [StructLayout(LayoutKind.Sequential, CharSet = CharSet.Unicode)]
        private struct NETRESOURCE
        {
            public uint dwScope;
            public uint dwType;
            public uint dwDisplayType;
            public uint dwUsage;
            public string lpLocalName;
            public string lpRemoteName;
            public string lpComment;
            public string lpProvider;
        }

        [DllImport("mpr.dll", CharSet = CharSet.Unicode)]
        private static extern int WNetAddConnection2(ref NETRESOURCE Resource, string Password, string UserName, uint Flags);

        [DllImport("mpr.dll", CharSet = CharSet.Unicode)]
        private static extern int WNetCancelConnection2(string Name, uint Flags, bool Force);

        // Windows error numbers after which other credentials may help.
        private static readonly int[] CredentialErrors = new int[] { 5, 86, 1219, 1326, 1327, 1328, 1329, 1330, 1331, 1385, 1909, 1910 };

        public NetworkAccess CheckAccess(string Path)
        {
            try
            {
                // A folder (mounted as a CD-ROM) is probed by listing it; a file by opening it.
                if (Directory.Exists(Path) == true)
                {
                    using (IEnumerator<string> Entries = Directory.EnumerateFileSystemEntries(Path).GetEnumerator())
                        Entries.MoveNext();
                    return (NetworkAccess.Ok);
                }

                using (FileStream Probe = new FileStream(Path, FileMode.Open, FileAccess.Read, FileShare.ReadWrite | FileShare.Delete))
                    return (NetworkAccess.Ok);
            }
            catch (UnauthorizedAccessException)
            {
                return (NetworkAccess.NeedCredentials);
            }
            catch (IOException Problem)
            {
                int Code = Problem.HResult & 0xFFFF;
                return ((Array.IndexOf(CredentialErrors, Code) >= 0) ? NetworkAccess.NeedCredentials : NetworkAccess.Failed);
            }
            catch (Exception)
            {
                return (NetworkAccess.Failed);
            }
        }

        public bool Connect(string Share, string User, string Password, out string Error)
        {
            int Result = Add(Share, User, Password);
            if (Result == ERROR_SESSION_CREDENTIAL_CONFLICT)
            {
                // The service already has a session to this server with other credentials (for instance from the failed attempt with the machine account): drop it and try again.
                string Server = Share.Substring(0, Share.IndexOf('\\', 2));
                WNetCancelConnection2(Share, 0, true);
                WNetCancelConnection2(Server + @"\IPC$", 0, true);
                Result = Add(Share, User, Password);
            }

            if (Result == NO_ERROR)
            {
                Error = "";
                return (true);
            }

            Error = new Win32Exception(Result).Message;
            return (false);
        }

        private static int Add(string Share, string User, string Password)
        {
            NETRESOURCE Resource = new NETRESOURCE();
            Resource.dwType = RESOURCETYPE_DISK;
            Resource.lpRemoteName = Share;
            return (WNetAddConnection2(ref Resource, Password, User, CONNECT_TEMPORARY));
        }

        public void Disconnect(string Share)
        {
            WNetCancelConnection2(Share, 0, true);
        }
    }
}
