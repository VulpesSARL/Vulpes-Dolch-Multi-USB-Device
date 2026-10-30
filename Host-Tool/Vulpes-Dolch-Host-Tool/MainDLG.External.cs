using System;
using System.Collections.Generic;
using System.Windows.Forms;
using VulpesDolch.Host.Common.Service;

namespace Vulpes_Dolch_Host_Tool
{
    /// <summary>Requests from outside (the Explorer context menu through the command line) and the Options menu that installs those entries.</summary>
    public partial class MainDLG
    {
        // How long a request waits for the service, the Host device and the Client (a start of the program from Explorer may come before all three are up).
        private const long ExternalWaitMs = 10000;

        private readonly Queue<ExternalRequest> ExternalQueue = new Queue<ExternalRequest>();
        private System.Windows.Forms.Timer ExternalTimer;
        private bool ExternalRunning;

        private void InitExternal()
        {
            installContextMenusToolStripMenuItem.Checked = ExplorerIntegration.IsInstalled();
            optionsToolStripMenuItem.DropDownOpening += delegate (object Sender, EventArgs Args) { installContextMenusToolStripMenuItem.Checked = ExplorerIntegration.IsInstalled(); };

            ExternalTimer = new System.Windows.Forms.Timer();
            ExternalTimer.Interval = 250;
            ExternalTimer.Tick += ExternalTimer_Tick;

            UiInstance.SetNotify(delegate () { RunOnUi(TakeExternalRequests); });
            TakeExternalRequests();
        }

        private void ShutdownExternal()
        {
            UiInstance.SetNotify(null);
            if (ExternalTimer != null)
                ExternalTimer.Dispose();
        }

        private void installContextMenusToolStripMenuItem_Click(object sender, EventArgs e)
        {
            try
            {
                if (ExplorerIntegration.IsInstalled() == true)
                    ExplorerIntegration.Uninstall();
                else
                    ExplorerIntegration.Install();
            }
            catch (Exception Problem)
            {
                MessageBox.Show(this, "The Explorer context menus could not be changed:\r\n\r\n" + Problem.Message, "Options", MessageBoxButtons.OK, MessageBoxIcon.Warning);
            }

            installContextMenusToolStripMenuItem.Checked = ExplorerIntegration.IsInstalled();
        }

        /// <summary>The window comes to the front when something is asked of it from outside.</summary>
        private void BringToFrontExternal()
        {
            if (WindowState == FormWindowState.Minimized)
                WindowState = FormWindowState.Normal;

            Show();
            Activate();
            UiInstance.ToForeground(Handle);
        }

        private void TakeExternalRequests()
        {
            ExternalRequest Request;
            while (UiInstance.TryTake(out Request) == true)
            {
                BringToFrontExternal();
                if (Request.Kind == ExternalKind.Show)
                    continue;

                Request.Deadline = Environment.TickCount64 + ExternalWaitMs;
                ExternalQueue.Enqueue(Request);
                ExternalTimer.Start();
            }
        }

        /// <summary>Null when the request can be carried out now, otherwise what is missing (the request waits for it until its deadline).</summary>
        private string WhyNotReady(ExternalRequest Request)
        {
            ServiceState State = LastState;
            if (State == null)
                return ("The Vulpes Dolch service is not running or not reachable. Start the service (VulpesDolchHost) and try again.");
            if (State.DeviceConnected == false)
                return ("The Vulpes Dolch Host device is not connected. Plug it in and try again.");
            if (State.ClientState != "Running")
                return ("The Client is not running yet (state: " + State.ClientState + "). Wait a few seconds and try again.");
            if ((Request.Kind == ExternalKind.MountCd) && (CdBusy == true))
                return ("The CD-ROM drive is busy with another image.");
            if ((Request.Kind == ExternalKind.MountDisk) && (StorageBusy == true))
                return ("The storage drive is busy with another image.");
            return (null);
        }

        private async void ExternalTimer_Tick(object sender, EventArgs e)
        {
            if (ExternalRunning == true)
                return;

            if (ExternalQueue.Count == 0)
            {
                ExternalTimer.Stop();
                return;
            }

            ExternalRequest Request = ExternalQueue.Peek();
            string Missing = WhyNotReady(Request);
            if ((Missing != null) && (Environment.TickCount64 < Request.Deadline))
                return;

            ExternalQueue.Dequeue();
            ExternalRunning = true;
            try
            {
                if (Missing != null)
                {
                    MessageBox.Show(this, "'" + Request.Path + "' cannot be mounted.\r\n\r\n" + Missing, "Mount with Vulpes Dolch", MessageBoxButtons.OK, MessageBoxIcon.Warning);
                    return;
                }

                // The same steps as the Load buttons: ask first when the drive is not empty, then load; problems are reported by the load itself.
                if (Request.Kind == ExternalKind.MountCd)
                {
                    if (await MakeRoomCdAsync(Request.Path) == true)
                        await LoadCdAsync(Request.Path);
                }
                else
                {
                    if (await MakeRoomStorageAsync(Request.Path) == true)
                        await LoadStorageAsync(Request.Path, chkStorageMediaReadOnly.Checked);
                }
            }
            finally
            {
                ExternalRunning = false;
            }
        }
    }
}
