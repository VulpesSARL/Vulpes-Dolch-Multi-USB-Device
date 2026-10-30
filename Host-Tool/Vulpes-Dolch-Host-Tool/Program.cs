using System;
using System.Windows.Forms;

namespace Vulpes_Dolch_Host_Tool
{
    internal static class Program
    {
        private const string Usage = "Usage:\r\n  /mountcd \"<ISO / CUE file or folder>\"   presents it to the target PC as a CD-ROM\r\n  /mountdisk \"<disk image file>\"   presents it to the target PC as a disk";

        /// <summary>
        ///  The main entry point for the application.
        /// </summary>
        [STAThread]
        static void Main(string[] Args)
        {
            ApplicationConfiguration.Initialize();

            ExternalRequest Request = null;
            if (Args.Length > 0)
            {
                string Problem;
                Request = ParseArguments(Args, out Problem);
                if (Request == null)
                {
                    MessageBox.Show(Problem, "Vulpes Dolch", MessageBoxButtons.OK, MessageBoxIcon.Warning);
                    return;
                }
            }

            // A second copy only hands its request to the one that is already running.
            if (UiInstance.TryBecomePrimary() == false)
            {
                if (Request == null)
                {
                    Request = new ExternalRequest();
                    Request.Kind = ExternalKind.Show;
                }

                if (UiInstance.Forward(Request) == false)
                    MessageBox.Show("Another copy of Vulpes Dolch is running but does not answer.", "Vulpes Dolch", MessageBoxButtons.OK, MessageBoxIcon.Warning);
                return;
            }

            UiInstance.StartListener();
            ExplorerIntegration.RefreshIfInstalled();
            if (Request != null)
                UiInstance.Post(Request);

            Application.Run(new MainDLG());
        }

        private static ExternalRequest ParseArguments(string[] Args, out string Problem)
        {
            Problem = null;
            ExternalRequest Request = new ExternalRequest();
            string Switch = Args[0].ToLowerInvariant();
            if (Switch == ExplorerIntegration.CdSwitch)
                Request.Kind = ExternalKind.MountCd;
            else if (Switch == ExplorerIntegration.DiskSwitch)
                Request.Kind = ExternalKind.MountDisk;
            else
            {
                Problem = "Unknown option '" + Args[0] + "'.\r\n\r\n" + Usage;
                return (null);
            }

            // A path with spaces that was not quoted arrives in pieces.
            string Given = (Args.Length > 1) ? string.Join(" ", Args, 1, Args.Length - 1).Trim() : "";
            if (Given.Length == 0)
            {
                Problem = "The option " + Args[0] + " needs a path.\r\n\r\n" + Usage;
                return (null);
            }

            try
            {
                Request.Path = System.IO.Path.GetFullPath(Given);
            }
            catch (Exception)
            {
                Problem = "'" + Given + "' is not a valid path.";
                return (null);
            }
            return (Request);
        }
    }
}
