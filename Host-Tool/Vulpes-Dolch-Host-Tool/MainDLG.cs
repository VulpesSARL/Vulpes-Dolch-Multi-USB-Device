using System;
using System.Collections.Generic;
using System.Drawing;
using System.IO;
using System.Threading.Tasks;
using System.Windows.Forms;
using VulpesDolch.Host.Common.Service;

namespace Vulpes_Dolch_Host_Tool
{
    /// <summary>
    /// The main dialog. It owns no device: everything goes through the Vulpes Dolch service (ServiceClient), which keeps running when the user logs off.
    /// The controls are disabled while the service is not there or the service has no Host device.
    /// </summary>
    public partial class MainDLG : Form
    {
        private const string CdHistoryName = "CdHistory";
        private const string StorageHistoryName = "StorageHistory";
        private const string StorageReadOnlyName = "StorageReadOnly";
        private const string CdFilter = "CD / DVD / BD images (*.iso;*.cue)|*.iso;*.cue|All files (*.*)|*.*";
        private const string StorageFilter = "Disk images (*.vhdx;*.vhd;*.vmdk;*.vdi;*.img;*.raw;*.bin;*.dd)|*.vhdx;*.vhd;*.vmdk;*.vdi;*.img;*.raw;*.bin;*.dd|All files (*.*)|*.*";

        private ServiceClient Client;
        private MediaHistory CdHistory;
        private MediaHistory StorageHistory;
        private bool MediaLoadEnabled;
        private bool CdBusy;
        private bool StorageBusy;
        private long CdCancelUntil;
        private long StorageCancelUntil;
        private ServiceState LastState;

        public MainDLG()
        {
            InitializeComponent();
        }

        private void MainDLG_Load(object sender, EventArgs e)
        {
            FormClosed += MainDLG_FormClosed;
            CdHistory = new MediaHistory(CdHistoryName, lstCDMedia);
            StorageHistory = new MediaHistory(StorageHistoryName, lstStorageMedia);
            chkStorageMediaReadOnly.Checked = AppSettings.GetBool(StorageReadOnlyName, false);

            // Picking an entry from the middle of a list moves it to the top.
            lstCDMedia.SelectionChangeCommitted += delegate (object Sender, EventArgs Args) { PromotePick(lstCDMedia, CdHistory); };
            lstStorageMedia.SelectionChangeCommitted += delegate (object Sender, EventArgs Args) { PromotePick(lstStorageMedia, StorageHistory); };

            favouritesToolStripMenuItem.DropDownOpening += delegate (object Sender, EventArgs Args) { BuildFavouritesMenu(); };
            RestoreWindowPosition();
            label2.Text = "Firmware (Host / Client):";
            ApplyState(null, false);

            Client = new ServiceClient();
            Client.StateChanged += delegate (ServiceState State) { RunOnUi(delegate () { ApplyState(State, true); }); };
            Client.ConnectionChanged += delegate (bool Up)
            {
                if (Up == false)
                    RunOnUi(delegate () { ApplyState(null, false); });
            };
            Client.Start();
            InitExternal();
        }

        private void MainDLG_FormClosed(object sender, FormClosedEventArgs e)
        {
            AppSettings.SetBool(StorageReadOnlyName, chkStorageMediaReadOnly.Checked);
            SaveWindowPosition();
            ShutdownExternal();
            if (Client != null)
                Client.Dispose();
        }

        private void RunOnUi(Action Work)
        {
            if ((IsHandleCreated == false) || (IsDisposed == true))
                return;

            try
            {
                BeginInvoke(Work);
            }
            catch (InvalidOperationException)
            {
                // The form is closing.
            }
        }

        private static void PromotePick(ComboBox Box, MediaHistory History)
        {
            string Picked = Box.SelectedItem as string;
            if (Picked == null)
                return;

            // After the selection event, so that the list is not rebuilt while the combobox is still handling it.
            Box.BeginInvoke(new Action(delegate () { History.Promote(Picked); }));
        }

        private void RestoreWindowPosition()
        {
            int Left = AppSettings.GetInt("WindowLeft", int.MinValue);
            int Top = AppSettings.GetInt("WindowTop", int.MinValue);
            if ((Left == int.MinValue) || (Top == int.MinValue))
                return;

            Rectangle Wanted = new Rectangle(Left, Top, Width, Height);
            foreach (Screen Display in Screen.AllScreens)
            {
                if (Display.WorkingArea.IntersectsWith(Wanted) == true)
                {
                    StartPosition = FormStartPosition.Manual;
                    Location = new Point(Left, Top);
                    return;
                }
            }
        }

        private void SaveWindowPosition()
        {
            if (WindowState != FormWindowState.Normal)
                return;

            AppSettings.SetInt("WindowLeft", Left);
            AppSettings.SetInt("WindowTop", Top);
        }

        // ---- State -> controls -------------------------------------------------------------------------------------------------

        private static string SpeedText(int Speed)
        {
            if (Speed < 0)
                return ("disconnected");
            if (Speed == 1)
                return ("Full speed (USB 1.1)");
            if (Speed == 2)
                return ("High speed (USB 2)");
            if (Speed == 3)
                return ("SuperSpeed (USB 3)");
            return ("---");
        }

        private static string MediaText(bool Present, string Path, bool Locked)
        {
            if (Present == true)
                return ("Loaded: " + System.IO.Path.GetFileName(Path ?? "") + ((Locked == true) ? " (locked by the target PC)" : ""));
            if (string.IsNullOrEmpty(Path) == false)
                return ("Loading: " + System.IO.Path.GetFileName(Path));
            return ("Empty");
        }

        /// <summary>Shows the service state. State = null or ServiceUp = false: the service is not there, everything is disabled.</summary>
        private void ApplyState(ServiceState State, bool ServiceUp)
        {
            bool DeviceThere = (ServiceUp == true) && (State != null) && (State.DeviceConnected == true);
            ServiceState Before = LastState;
            LastState = (ServiceUp == true) ? State : null;
            MediaLoadEnabled = false;
            ApplyMediaControls();
            ReportDroppedLoads(Before, LastState);

            groupBox1.Enabled = DeviceThere;
            groupBox2.Enabled = DeviceThere;
            groupBox3.Enabled = DeviceThere;
            forceEjectCDROMToolStripMenuItem.Enabled = DeviceThere;
            forceEjectStorageMediaToolStripMenuItem.Enabled = DeviceThere;

            mountFolderAsCDROMToolStripMenuItem.Enabled = false;

            if (ServiceUp == false)
            {
                lblVersion.Text = "---";
                lblHostUSBSpeed.Text = "service not running";
                lblClientUSBSpeed.Text = "---";
                lblStatusCDMedia.Text = "-----";
                lblStatusStorageMedia.Text = "-----";
                return;
            }

            if (DeviceThere == false)
            {
                lblVersion.Text = "---";
                lblHostUSBSpeed.Text = "Host device not connected";
                lblClientUSBSpeed.Text = "---";
                lblStatusCDMedia.Text = "-----";
                lblStatusStorageMedia.Text = "-----";
                return;
            }

            lblVersion.Text = State.HostFirmware + " / " + State.ClientFirmware;
            lblHostUSBSpeed.Text = SpeedText(State.HostUsbSpeed);
            lblClientUSBSpeed.Text = SpeedText(State.ClientUsbSpeed);
            lblStatusCDMedia.Text = MediaText(State.CdPresent, State.CdPath, State.CdLocked);
            lblStatusStorageMedia.Text = MediaText(State.DiskPresent, State.DiskPath, false) + ((State.DiskPresent == true && State.DiskReadOnly == true) ? " (read-only)" : "");

            // Without a running Client application there is nothing to load media into.
            bool ClientThere = (State.ClientState == "Running");
            cmdControlRemotePC.Enabled = ClientThere;
            MediaLoadEnabled = ClientThere;
            mountFolderAsCDROMToolStripMenuItem.Enabled = ClientThere;
            ApplyMediaControls();
        }

        /// <summary>
        /// An image the service wanted and then dropped by itself (the Client did not take it, the file was locked, ...): the drive is empty again and the user is told
        /// why with the service's last message. Not shown when the user pressed Cancel.
        /// </summary>
        private void ReportDroppedLoads(ServiceState Before, ServiceState Now)
        {
            if ((Before == null) || (Now == null))
                return;

            bool CdDropped = (Environment.TickCount64 > CdCancelUntil) && (ModeOf(Before.CdPresent, Before.CdPath) == MediaMode.Loading) && (ModeOf(Now.CdPresent, Now.CdPath) == MediaMode.Empty);
            bool DiskDropped = (Environment.TickCount64 > StorageCancelUntil) && (ModeOf(Before.DiskPresent, Before.DiskPath) == MediaMode.Loading) &&
                (ModeOf(Now.DiskPresent, Now.DiskPath) == MediaMode.Empty);
            if ((CdDropped == false) && (DiskDropped == false))
                return;

            string Why = string.IsNullOrEmpty(Now.Message) ? "The Client did not take the image." : Now.Message;
            BeginInvoke(new Action(delegate ()
            {
                MessageBox.Show(this, (CdDropped ? "The CD image was not loaded.\r\n\r\n" : "The storage image was not loaded.\r\n\r\n") + Why, "Load media", MessageBoxButtons.OK, MessageBoxIcon.Warning);
            }));
        }

        // ---- Commands ----------------------------------------------------------------------------------------------------------

        private async Task<ServiceResponse> CallAsync(ServiceRequest Request, int TimeoutMs)
        {
            ServiceClient Service = Client;
            if (Service == null)
                return (new ServiceResponse { Ok = false, Error = "The Vulpes Dolch service is not running." });

            return (await Task.Run(delegate () { return (Service.Call(Request, TimeoutMs)); }));
        }

        /// <summary>
        /// Sends a load command. If the image is on a network share the service may not use, the Windows credential dialog is shown and the command is sent again with
        /// the account the user gives (up to three tries); the password is dropped from the request as soon as it was sent.
        /// </summary>
        private async Task<ServiceResponse> LoadWithCredentialsAsync(ServiceRequest Request)
        {
            ServiceResponse Answer = await CallAsync(Request, 30000);
            int Tries = 0;
            while ((Answer.NeedsCredentials == true) && (Tries < 3))
            {
                string User;
                string Password;
                if (NetworkSupport.AskCredentials(this, Answer.Share, Answer.Error, Tries > 0, out User, out Password) == false)
                    break;

                Request.User = User;
                Request.Password = Password;
                Answer = await CallAsync(Request, 30000);
                Request.Password = null;
                Password = null;
                Tries++;
            }
            Request.Password = null;
            return (Answer);
        }

        private static string CleanPath(string Text)
        {
            return ((Text ?? "").Trim().Trim('"'));
        }

        private enum MediaMode
        {
            Empty,
            Loading,
            Loaded,
        }

        /// <summary>What a drive is doing according to the last service state: nothing wanted, an image wanted but not in yet, or in.</summary>
        private static MediaMode ModeOf(bool Present, string Path)
        {
            if (Present == true)
                return (MediaMode.Loaded);
            if (string.IsNullOrEmpty(Path) == false)
                return (MediaMode.Loading);
            return (MediaMode.Empty);
        }

        private MediaMode CdMode()
        {
            ServiceState State = LastState;
            return ((State == null) ? MediaMode.Empty : ModeOf(State.CdPresent, State.CdPath));
        }

        private MediaMode StorageMode()
        {
            ServiceState State = LastState;
            return ((State == null) ? MediaMode.Empty : ModeOf(State.DiskPresent, State.DiskPath));
        }

        /// <summary>
        /// Path box, Browse and read-only box only work while the drive is empty and nothing is being sent; the button loads when the drive is empty, cancels
        /// while the image is on its way in, and ejects once it is in. (The state pushes call this too, so they never unlock a busy row early.)
        /// </summary>
        private void ApplyMediaRow(MediaMode Mode, bool Busy, ComboBox Path, Button Browse, CheckBox ReadOnly, Button Action, string LoadText)
        {
            bool Idle = ((Mode == MediaMode.Empty) && (Busy == false));
            Path.Enabled = Idle;
            Browse.Enabled = Idle;
            if (ReadOnly != null)
                ReadOnly.Enabled = Idle;

            if (Mode == MediaMode.Empty)
            {
                Action.Text = LoadText;
                Action.Enabled = ((Busy == false) && (MediaLoadEnabled == true));
                return;
            }

            Action.Text = (Mode == MediaMode.Loading) ? "Cancel" : "Eject";
            Action.Enabled = (Busy == false);
        }

        private void ApplyMediaControls()
        {
            ApplyMediaRow(CdMode(), CdBusy, lstCDMedia, cmdBrowseCDMedia, null, cmdLoadCDMedia, "Load CD-Media");
            ApplyMediaRow(StorageMode(), StorageBusy, lstStorageMedia, cmdBrowseStorageMedia, chkStorageMediaReadOnly, cmdLoadStorageMedia, "Load Storage Media");
        }

        private void SetCdBusy(bool Busy)
        {
            CdBusy = Busy;
            ApplyMediaControls();
        }

        private void SetStorageBusy(bool Busy)
        {
            StorageBusy = Busy;
            ApplyMediaControls();
        }

        /// <summary>Takes the image out (Eject, respects the lock of the target PC) or, while it is still on its way in, drops it (Cancel).</summary>
        private async Task<bool> EjectCdAsync(bool Cancel)
        {
            ServiceRequest Request = new ServiceRequest();
            Request.Command = ServiceCommand.EjectCd;
            Request.Flag = Cancel;

            // An unload the user asked for (Eject or Cancel) passes through "wanted but not present" on its way to empty: not a dropped load.
            CdCancelUntil = long.MaxValue;
            SetCdBusy(true);
            ServiceResponse Answer;
            try
            {
                Answer = await CallAsync(Request, 15000);
            }
            finally
            {
                SetCdBusy(false);
                CdCancelUntil = Environment.TickCount64 + 3000;
            }

            if (Answer.Ok == false)
                MessageBox.Show(this, Answer.Error, "Eject CD-Media", MessageBoxButtons.OK, MessageBoxIcon.Warning);
            return (Answer.Ok);
        }

        private async Task<bool> EjectStorageAsync(bool Cancel)
        {
            ServiceRequest Request = new ServiceRequest();
            Request.Command = ServiceCommand.DetachDisk;
            Request.Flag = Cancel;

            StorageCancelUntil = long.MaxValue;
            SetStorageBusy(true);
            ServiceResponse Answer;
            try
            {
                Answer = await CallAsync(Request, 15000);
            }
            finally
            {
                SetStorageBusy(false);
                StorageCancelUntil = Environment.TickCount64 + 3000;
            }

            if (Answer.Ok == false)
                MessageBox.Show(this, Answer.Error, "Eject Storage Media", MessageBoxButtons.OK, MessageBoxIcon.Warning);
            return (Answer.Ok);
        }

        private async void cmdLoadCDMedia_Click(object sender, EventArgs e)
        {
            MediaMode Mode = CdMode();
            if (Mode == MediaMode.Empty)
                await LoadCdAsync(CleanPath(lstCDMedia.Text));
            else
                await EjectCdAsync(Mode == MediaMode.Loading);
        }

        private async Task LoadCdAsync(string Path)
        {
            Path = NetworkSupport.ToServicePath(Path);
            if (Path.Length == 0)
                return;

            ServiceRequest Request = new ServiceRequest();
            Request.Command = ServiceCommand.InsertCd;
            Request.Path = Path;

            SetCdBusy(true);
            ServiceResponse Answer;
            try
            {
                Answer = await LoadWithCredentialsAsync(Request);
            }
            finally
            {
                SetCdBusy(false);
            }

            if (Answer.Ok == true)
            {
                CdHistory.Promote(Path);
                return;
            }

            if (Answer.FileError == true)
                CdHistory.Drop(Path);
            MessageBox.Show(this, Answer.Error, "Load CD-Media", MessageBoxButtons.OK, MessageBoxIcon.Warning);
        }

        private void cmdBrowseCDMedia_Click(object sender, EventArgs e)
        {
            using (OpenFileDialog Dialog = new OpenFileDialog())
            {
                Dialog.Title = "Select a CD / DVD / BD image";
                Dialog.Filter = CdFilter;
                if (Dialog.ShowDialog(this) == DialogResult.OK)
                    lstCDMedia.Text = Dialog.FileName;
            }
        }

        private async void cmdLoadStorageMedia_Click(object sender, EventArgs e)
        {
            MediaMode Mode = StorageMode();
            if (Mode == MediaMode.Empty)
                await LoadStorageAsync(CleanPath(lstStorageMedia.Text), chkStorageMediaReadOnly.Checked);
            else
                await EjectStorageAsync(Mode == MediaMode.Loading);
        }

        private async Task LoadStorageAsync(string Path, bool ReadOnly)
        {
            Path = NetworkSupport.ToServicePath(Path);
            if (Path.Length == 0)
                return;

            ServiceRequest Request = new ServiceRequest();
            Request.Command = ServiceCommand.AttachDisk;
            Request.Path = Path;
            Request.Flag = ReadOnly;

            SetStorageBusy(true);
            ServiceResponse Answer;
            try
            {
                Answer = await LoadWithCredentialsAsync(Request);
            }
            finally
            {
                SetStorageBusy(false);
            }

            if (Answer.Ok == true)
            {
                StorageHistory.Promote(Path);
                return;
            }

            if (Answer.FileError == true)
                StorageHistory.Drop(Path);
            MessageBox.Show(this, Answer.Error, "Load Storage Media", MessageBoxButtons.OK, MessageBoxIcon.Warning);
        }

        private void cmdBrowseStorageMedia_Click(object sender, EventArgs e)
        {
            using (OpenFileDialog Dialog = new OpenFileDialog())
            {
                Dialog.Title = "Select a disk image";
                Dialog.Filter = StorageFilter;
                if (Dialog.ShowDialog(this) == DialogResult.OK)
                    lstStorageMedia.Text = Dialog.FileName;
            }
        }

        private async void forceEjectCDROMToolStripMenuItem_Click(object sender, EventArgs e)
        {
            if (MessageBox.Show(this, "Take the CD out of the target PC even if the target PC holds a lock on it?", "Force eject CD-ROM", MessageBoxButtons.YesNo, MessageBoxIcon.Warning) != DialogResult.Yes)
                return;

            ServiceRequest Request = new ServiceRequest();
            Request.Command = ServiceCommand.EjectCd;
            Request.Flag = true;
            CdCancelUntil = long.MaxValue;
            ServiceResponse Answer;
            try
            {
                Answer = await CallAsync(Request, 15000);
            }
            finally
            {
                CdCancelUntil = Environment.TickCount64 + 3000;
            }
            if (Answer.Ok == false)
                MessageBox.Show(this, Answer.Error, "Force eject CD-ROM", MessageBoxButtons.OK, MessageBoxIcon.Warning);
        }

        private async void forceEjectStorageMediaToolStripMenuItem_Click(object sender, EventArgs e)
        {
            if (MessageBox.Show(this, "Take the disk out of the target PC even if the target PC holds a lock on it? Unsaved data on the target PC may be lost.", "Force eject Storage Media",
                MessageBoxButtons.YesNo, MessageBoxIcon.Warning) != DialogResult.Yes)
                return;

            ServiceRequest Request = new ServiceRequest();
            Request.Command = ServiceCommand.DetachDisk;
            Request.Flag = true;
            StorageCancelUntil = long.MaxValue;
            ServiceResponse Answer;
            try
            {
                Answer = await CallAsync(Request, 15000);
            }
            finally
            {
                StorageCancelUntil = Environment.TickCount64 + 3000;
            }
            if (Answer.Ok == false)
                MessageBox.Show(this, Answer.Error, "Force eject Storage Media", MessageBoxButtons.OK, MessageBoxIcon.Warning);
        }

        private void cmdControlRemotePC_Click(object sender, EventArgs e)
        {
            // The window sends the keys and mouse movement through the same service connection.
            frmRemoteControl Window = new frmRemoteControl(Client);
            Window.Show(this);
        }

        // ---- favourites ----

        /// <summary>Everything below the separator of the Favourites menu is rebuilt from the store each time the menu opens.</summary>
        private void BuildFavouritesMenu()
        {
            ToolStripItemCollection Entries = favouritesToolStripMenuItem.DropDownItems;
            int Separator = Entries.IndexOf(toolStripMenuItem1);
            while (Entries.Count > Separator + 1)
                Entries.RemoveAt(Entries.Count - 1);

            List<Favourite> Cds = FavouritesStore.LoadCd();
            List<Favourite> Storages = FavouritesStore.LoadStorage();
            if ((Cds.Count == 0) && (Storages.Count == 0))
            {
                ToolStripMenuItem None = new ToolStripMenuItem("(no favourites yet)");
                None.Enabled = false;
                Entries.Add(None);
                return;
            }

            AddFavouriteEntries(Entries, "CD-ROM", Cds, false);
            if ((Cds.Count > 0) && (Storages.Count > 0))
                Entries.Add(new ToolStripSeparator());
            AddFavouriteEntries(Entries, "Storage", Storages, true);
        }

        private void AddFavouriteEntries(ToolStripItemCollection Entries, string Title, List<Favourite> Items, bool Storage)
        {
            if (Items.Count == 0)
                return;

            ToolStripMenuItem Header = new ToolStripMenuItem(Title);
            Header.Enabled = false;
            Entries.Add(Header);

            foreach (Favourite Item in Items)
            {
                string Text = Item.Path.Replace("&", "&&") + ((Storage == true && Item.ReadOnly == true) ? "  [read-only]" : "");
                ToolStripMenuItem Entry = new ToolStripMenuItem(Text);
                Entry.Tag = Item;
                Entry.Enabled = MediaLoadEnabled;
                if (Storage == true)
                    Entry.Click += favouriteStorage_Click;
                else
                    Entry.Click += favouriteCd_Click;
                Entries.Add(Entry);
            }
        }

        /// <summary>The "drive is not empty" question, a task dialog whose buttons say what they do.</summary>
        private bool AskReplace(string Drive, string Current, string NewPath, bool Loading)
        {
            TaskDialogButton Replace = new TaskDialogButton(Loading ? "Cancel the current one and load the new one" : "Eject the current one and load the new one");
            TaskDialogButton Keep = new TaskDialogButton("Keep the current one");

            TaskDialogPage Page = new TaskDialogPage();
            Page.Caption = "Vulpes Dolch";
            Page.Heading = "The " + Drive + " drive is not empty";
            Page.Text = (Loading ? "Being loaded:  " : "Loaded:  ") + Current + "\r\n\r\nNew image:  " + NewPath;
            Page.Icon = TaskDialogIcon.Warning;
            Page.Buttons.Add(Replace);
            Page.Buttons.Add(Keep);
            Page.DefaultButton = Keep;
            Page.AllowCancel = true;
            return (TaskDialog.ShowDialog(this, Page) == Replace);
        }

        /// <summary>True when the CD-ROM drive is free for <paramref name="NewPath"/>: it was empty, or the user agreed and the image was taken out (a lock of the target PC refuses that).</summary>
        private async Task<bool> MakeRoomCdAsync(string NewPath)
        {
            if (CdBusy == true)
                return (false);

            MediaMode Mode = CdMode();
            if (Mode == MediaMode.Empty)
                return (true);

            if (AskReplace("CD-ROM", LastState.CdPath, NewPath, Mode == MediaMode.Loading) == false)
                return (false);

            return (await EjectCdAsync(Mode == MediaMode.Loading));
        }

        private async Task<bool> MakeRoomStorageAsync(string NewPath)
        {
            if (StorageBusy == true)
                return (false);

            MediaMode Mode = StorageMode();
            if (Mode == MediaMode.Empty)
                return (true);

            if (AskReplace("storage", LastState.DiskPath, NewPath, Mode == MediaMode.Loading) == false)
                return (false);

            return (await EjectStorageAsync(Mode == MediaMode.Loading));
        }

        private async void favouriteCd_Click(object sender, EventArgs e)
        {
            Favourite Item = (sender as ToolStripMenuItem).Tag as Favourite;
            if (await MakeRoomCdAsync(Item.Path) == false)
                return;

            await LoadCdAsync(Item.Path);
        }

        private async void favouriteStorage_Click(object sender, EventArgs e)
        {
            Favourite Item = (sender as ToolStripMenuItem).Tag as Favourite;
            if (await MakeRoomStorageAsync(Item.Path) == false)
                return;

            chkStorageMediaReadOnly.Checked = Item.ReadOnly;
            await LoadStorageAsync(Item.Path, Item.ReadOnly);
        }

        private void manageFavouritesToolStripMenuItem_Click(object sender, EventArgs e)
        {
            frmManageFavourites frm = new frmManageFavourites();
            frm.ShowDialog(this);
        }

        private void exitToolStripMenuItem_Click(object sender, EventArgs e)
        {
            this.Close();
        }

        /// <summary>Asks for a folder (on a disk or on the network) and presents it to the target PC as a CD-ROM: the service builds the index, the files stay where they are and are read when the target PC accesses them.</summary>
        private async void mountFolderAsCDROMToolStripMenuItem_Click(object sender, EventArgs e)
        {
            string FolderPath;
            using (FolderBrowserDialog Dialog = new FolderBrowserDialog())
            {
                Dialog.Description = "Select the folder to present to the target PC as a CD-ROM. Its files are read from the disk when the target PC accesses them.";
                Dialog.UseDescriptionForTitle = true;
                Dialog.ShowNewFolderButton = false;
                if (Dialog.ShowDialog(this) != DialogResult.OK)
                    return;

                FolderPath = Dialog.SelectedPath;
            }

            if (await MakeRoomCdAsync(FolderPath) == false)
                return;

            await LoadCdAsync(FolderPath);
        }

        private async void createNewStorageDiskFileToolStripMenuItem_Click(object sender, EventArgs e)
        {
            string NewPath = null;
            using (frmCreateHDDImage Dialog = new frmCreateHDDImage())
            {
                if (Dialog.ShowDialog(this) != DialogResult.OK)
                    return;

                if (Dialog.LoadAfterwards == true)
                    NewPath = Dialog.CreatedPath;
            }

            // A new image is empty and meant to be written to: it is loaded read-write (after asking, when the drive is not empty).
            if ((NewPath != null) && (await MakeRoomStorageAsync(NewPath) == true))
                await LoadStorageAsync(NewPath, false);
        }
    }
}
