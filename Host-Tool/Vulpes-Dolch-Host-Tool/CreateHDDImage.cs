using System;
using System.Collections.Generic;
using System.ComponentModel;
using System.Data;
using System.Drawing;
using System.IO;
using System.Text;
using System.Threading.Tasks;
using System.Windows.Forms;
using VulpesDolch.Host.Common.Media;

namespace Vulpes_Dolch_Host_Tool
{
    /// <summary>
    /// "Create new Storage Disk File": makes a new disk image of one of the four formats, with a partition table and a file system if asked. The order of the entries of
    /// the type and the Prepare list is the order of <see cref="DiskImageType"/> and <see cref="DiskPreparation"/>. The image is created here (in the user's process, with the
    /// user's rights); loading it into the target PC afterwards is done by the main dialog (<see cref="LoadAfterwards"/>).
    /// </summary>
    public partial class frmCreateHDDImage : Form
    {
        /// <summary>The file that was created (valid when the dialog returned OK).</summary>
        public string CreatedPath { get; private set; }

        /// <summary>The user wants the new image loaded into the target PC as the storage disk.</summary>
        public bool LoadAfterwards { get; private set; }

        /// <summary>The path the save dialog returned (and already asked about replacing); null until Browse was used.</summary>
        private string BrowsedPath;

        public frmCreateHDDImage()
        {
            InitializeComponent();
        }

        private void frmCreateHDDImage_Load(object sender, EventArgs e)
        {
            lstSizeUnit.Items.Add("MiB");
            lstSizeUnit.Items.Add("GiB");
            lstSizeUnit.Items.Add("TiB");
            lstSizeUnit.SelectedIndex = 0;

            lstImageType.Items.Add("Microsoft VHDX");
            lstImageType.Items.Add("Plain binary");
            lstImageType.Items.Add("Microsoft / Connectix old VHD");
            lstImageType.Items.Add("VMware VMDK");
            lstImageType.Items.Add("Oracle VirtualBox VDI");
            lstImageType.SelectedIndex = 0;

            lstPrepare.Items.Add("(no preparation)");
            lstPrepare.Items.Add("NTFS partition on MBR");
            lstPrepare.Items.Add("NTFS partition on GPT");
            lstPrepare.Items.Add("NTFS partition on GPT (with EFI System Partition and MSR)"); //EFI Partition = 256MiB, MSR = 16MiB, then the NTFS partition
            lstPrepare.Items.Add("NTFS - plain");
            lstPrepare.Items.Add("FAT32 partition on MBR");
            lstPrepare.Items.Add("FAT32 partition on GPT");
            lstPrepare.Items.Add("FAT32 - plain");
            // exFAT cannot be formatted (the disk image library has no exFAT writer).
            //lstPrepare.Items.Add("exFAT partition on MBR");
            //lstPrepare.Items.Add("exFAT partition on GPT");
            //lstPrepare.Items.Add("exFAT - plain");
            lstPrepare.SelectedIndex = 0;

            lstImageType.SelectedIndexChanged += delegate (object Sender, EventArgs Args) { FitExtension(); };
            cmdBrowseNewFile.Click += cmdBrowseNewFile_Click;
        }

        private DiskImageType SelectedType
        {
            get { return ((DiskImageType)lstImageType.SelectedIndex); }
        }

        private static string ExtensionOf(DiskImageType Type)
        {
            if (Type == DiskImageType.Vhdx)
                return (".vhdx");
            if (Type == DiskImageType.Vhd)
                return (".vhd");
            if (Type == DiskImageType.Vmdk)
                return (".vmdk");
            if (Type == DiskImageType.Vdi)
                return (".vdi");
            return (".img");
        }

        /// <summary>When the type changes, a file name that has one of the image extensions gets the extension of the new type.</summary>
        private void FitExtension()
        {
            string Name = txtFilename.Text.Trim();
            if (Name.Length == 0)
                return;

            string Current = Path.GetExtension(Name).ToLowerInvariant();
            if ((Current == ".vhdx") || (Current == ".vhd") || (Current == ".vmdk") || (Current == ".vdi") || (Current == ".img") || (Current == ".raw") || (Current == ".bin"))
                txtFilename.Text = Path.ChangeExtension(Name, ExtensionOf(SelectedType));
        }

        private void cmdBrowseNewFile_Click(object sender, EventArgs e)
        {
            using (SaveFileDialog Dialog = new SaveFileDialog())
            {
                string Extension = ExtensionOf(SelectedType);
                Dialog.Title = "New disk image file";
                Dialog.Filter = lstImageType.Text + " (*" + Extension + ")|*" + Extension + "|All files (*.*)|*.*";
                Dialog.DefaultExt = Extension.TrimStart('.');
                Dialog.AddExtension = true;
                Dialog.OverwritePrompt = true;                     // an existing file is confirmed here; OK does not ask again for this very path
                if (txtFilename.Text.Trim().Length > 0)
                {
                    Dialog.FileName = Path.GetFileName(txtFilename.Text.Trim());
                    string Folder = Path.GetDirectoryName(txtFilename.Text.Trim());
                    if ((string.IsNullOrEmpty(Folder) == false) && (Directory.Exists(Folder) == true))
                        Dialog.InitialDirectory = Folder;
                }

                if (Dialog.ShowDialog(this) == DialogResult.OK)
                {
                    txtFilename.Text = Dialog.FileName;
                    BrowsedPath = Dialog.FileName;
                }
            }
        }

        private void cmdCancel_Click(object sender, EventArgs e)
        {
            this.DialogResult = DialogResult.Cancel;
            this.Close();
        }

        /// <summary>The size field times the unit, rounded to whole MiB; false when the text is not a positive number.</summary>
        private bool TryReadSize(out Int64 Bytes)
        {
            Bytes = 0;
            double Number;
            if ((double.TryParse(txtSize.Text.Trim(), out Number) == false) || (Number <= 0) || (double.IsInfinity(Number) == true))
                return (false);

            double Unit = DiskImageCreator.MiB;
            if (lstSizeUnit.SelectedIndex == 1)
                Unit = DiskImageCreator.GiB;
            else if (lstSizeUnit.SelectedIndex == 2)
                Unit = DiskImageCreator.TiB;

            double Total = Math.Round(Number * Unit / DiskImageCreator.MiB) * DiskImageCreator.MiB;
            if (Total > 1e15)
                return (false);

            Bytes = (Int64)Total;
            return (true);
        }

        private void Warn(string Text)
        {
            MessageBox.Show(this, Text, "Create Harddisk image file", MessageBoxButtons.OK, MessageBoxIcon.Warning);
        }

        private async void cmdOK_Click(object sender, EventArgs e)
        {
            string Path = txtFilename.Text.Trim().Trim('"');
            if (Path.Length == 0)
            {
                Warn("Please enter the name of the file.");
                return;
            }

            try
            {
                Path = System.IO.Path.GetFullPath(Path);
            }
            catch (Exception)
            {
                Warn("The file name is not valid.");
                return;
            }

            if (Directory.Exists(System.IO.Path.GetDirectoryName(Path)) == false)
            {
                Warn("The folder of the file does not exist.");
                return;
            }

            Int64 Size;
            if (TryReadSize(out Size) == false)
            {
                Warn("Please enter the size as a number greater than zero.");
                return;
            }

            DiskImageType Type = SelectedType;
            DiskPreparation Preparation = (DiskPreparation)lstPrepare.SelectedIndex;
            string Problem = DiskImageCreator.Validate(Type, Size, Preparation);
            if (Problem != null)
            {
                Warn(Problem);
                return;
            }

            if (File.Exists(Path) == true)
            {
                // The save dialog has asked already when the field still holds the path it returned; anything typed or changed since is asked about here.
                bool AlreadyConfirmed = (BrowsedPath != null) && (string.Equals(Path, BrowsedPath, StringComparison.OrdinalIgnoreCase) == true);
                if ((AlreadyConfirmed == false) && (MessageBox.Show(this, "The file " + Path + " exists already.\r\n\r\nReplace it with a new, empty image?", "Create Harddisk image file",
                    MessageBoxButtons.YesNo, MessageBoxIcon.Warning, MessageBoxDefaultButton.Button2) != DialogResult.Yes))
                    return;

                try
                {
                    File.Delete(Path);
                }
                catch (Exception Failure)
                {
                    Warn("The existing file cannot be replaced: " + Failure.Message);
                    return;
                }
            }

            // Formatting a big image takes a moment: the dialog waits with a wait cursor and nothing to click.
            string Target = Path;
            Enabled = false;
            UseWaitCursor = true;
            string Error = null;
            try
            {
                await Task.Run(delegate () { DiskImageCreator.Create(Target, Type, Size, Preparation, ""); });
            }
            catch (Exception Failure)
            {
                Error = Failure.Message;
            }
            finally
            {
                UseWaitCursor = false;
                Enabled = true;
            }

            if (Error != null)
            {
                Warn("The image could not be created: " + Error);
                return;
            }

            CreatedPath = Target;
            LoadAfterwards = chkLoadAfterwards.Checked;
            this.DialogResult = DialogResult.OK;
            this.Close();
        }
    }
}
