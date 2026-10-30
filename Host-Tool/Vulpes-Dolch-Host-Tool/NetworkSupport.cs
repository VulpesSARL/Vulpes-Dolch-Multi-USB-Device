using System;
using System.Runtime.InteropServices;
using System.Text;
using System.Windows.Forms;

namespace Vulpes_Dolch_Host_Tool
{
    /// <summary>Network helpers of the GUI: mapped drive letters mean nothing to the service (another logon session), and the credentials for a share are asked here.</summary>
    internal static class NetworkSupport
    {
        private const int CREDUI_FLAGS_INCORRECT_PASSWORD = 0x1;
        private const int CREDUI_FLAGS_DO_NOT_PERSIST = 0x2;
        private const int CREDUI_FLAGS_EXCLUDE_CERTIFICATES = 0x8;
        private const int CREDUI_FLAGS_ALWAYS_SHOW_UI = 0x80;
        private const int CREDUI_FLAGS_GENERIC_CREDENTIALS = 0x40000;
        private const int NO_ERROR = 0;

        [StructLayout(LayoutKind.Sequential, CharSet = CharSet.Unicode)]
        private struct CREDUI_INFO
        {
            public int cbSize;
            public IntPtr hwndParent;
            public string pszMessageText;
            public string pszCaptionText;
            public IntPtr hbmBanner;
        }

        [DllImport("credui.dll", CharSet = CharSet.Unicode)]
        private static extern int CredUIPromptForCredentials(ref CREDUI_INFO UiInfo, string TargetName, IntPtr Reserved, int AuthError, StringBuilder UserName, int UserNameMaxChars,
            StringBuilder Password, int PasswordMaxChars, ref bool Save, int Flags);

        [DllImport("mpr.dll", CharSet = CharSet.Unicode)]
        private static extern int WNetGetConnection(string LocalName, StringBuilder RemoteName, ref int Length);

        /// <summary>
        /// A path on a mapped network drive (Z:\dir\file.iso) as the UNC path it stands for (\\server\share\dir\file.iso): the service runs in another logon session and
        /// does not see this user's drive letters. Every other path is returned unchanged.
        /// </summary>
        public static string ToServicePath(string Path)
        {
            if ((Path == null) || (Path.Length < 3) || (char.IsLetter(Path[0]) == false) || (Path[1] != ':') || (Path[2] != '\\'))
                return (Path);

            StringBuilder Remote = new StringBuilder(1024);
            int Length = Remote.Capacity;
            if (WNetGetConnection(Path.Substring(0, 2), Remote, ref Length) != NO_ERROR)
                return (Path);

            return (Remote.ToString().TrimEnd('\\') + Path.Substring(2));
        }

        /// <summary>Shows the Windows credential dialog for a share. False when the user cancels. Nothing is saved by Windows.</summary>
        public static bool AskCredentials(IWin32Window Owner, string Share, string Problem, bool Retry, out string User, out string Password)
        {
            User = "";
            Password = "";

            CREDUI_INFO Info = new CREDUI_INFO();
            Info.cbSize = Marshal.SizeOf(typeof(CREDUI_INFO));
            Info.hwndParent = (Owner != null) ? Owner.Handle : IntPtr.Zero;
            Info.pszCaptionText = "Vulpes Dolch - network share";
            Info.pszMessageText = "The image is on " + Share + ".\r\n" + (string.IsNullOrEmpty(Problem) ? "" : (Problem + "\r\n")) +
                "The account is used while the image is loaded and is not saved.";

            StringBuilder UserName = new StringBuilder(513);
            StringBuilder Secret = new StringBuilder(257);
            bool Save = false;
            int Flags = CREDUI_FLAGS_GENERIC_CREDENTIALS | CREDUI_FLAGS_DO_NOT_PERSIST | CREDUI_FLAGS_EXCLUDE_CERTIFICATES | CREDUI_FLAGS_ALWAYS_SHOW_UI;
            if (Retry == true)
                Flags |= CREDUI_FLAGS_INCORRECT_PASSWORD;

            int Result = CredUIPromptForCredentials(ref Info, Share, IntPtr.Zero, 0, UserName, UserName.Capacity, Secret, Secret.Capacity, ref Save, Flags);
            if (Result != NO_ERROR)
                return (false);

            User = UserName.ToString();
            Password = Secret.ToString();
            Secret.Clear();
            return (true);
        }
    }
}
