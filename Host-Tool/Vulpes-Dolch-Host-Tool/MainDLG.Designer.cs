using System.Drawing;
using System.Windows.Forms;

namespace Vulpes_Dolch_Host_Tool
{
    partial class MainDLG
    {
        /// <summary>
        ///  Required designer variable.
        /// </summary>
        private System.ComponentModel.IContainer components = null;

        /// <summary>
        ///  Clean up any resources being used.
        /// </summary>
        /// <param name="disposing">true if managed resources should be disposed; otherwise, false.</param>
        protected override void Dispose(bool disposing)
        {
            if (disposing && (components != null))
            {
                components.Dispose();
            }
            base.Dispose(disposing);
        }

        #region Windows Form Designer generated code

        /// <summary>
        ///  Required method for Designer support - do not modify
        ///  the contents of this method with the code editor.
        /// </summary>
        private void InitializeComponent()
        {
            System.ComponentModel.ComponentResourceManager resources = new System.ComponentModel.ComponentResourceManager(typeof(MainDLG));
            menuStrip1 = new MenuStrip();
            fileToolStripMenuItem = new ToolStripMenuItem();
            createNewStorageDiskFileToolStripMenuItem = new ToolStripMenuItem();
            mountFolderAsCDROMToolStripMenuItem = new ToolStripMenuItem();
            toolStripMenuItem2 = new ToolStripSeparator();
            exitToolStripMenuItem = new ToolStripMenuItem();
            mediaToolStripMenuItem = new ToolStripMenuItem();
            forceEjectCDROMToolStripMenuItem = new ToolStripMenuItem();
            forceEjectStorageMediaToolStripMenuItem = new ToolStripMenuItem();
            favouritesToolStripMenuItem = new ToolStripMenuItem();
            manageFavouritesToolStripMenuItem = new ToolStripMenuItem();
            toolStripMenuItem1 = new ToolStripSeparator();
            optionsToolStripMenuItem = new ToolStripMenuItem();
            installContextMenusToolStripMenuItem = new ToolStripMenuItem();
            groupBox1 = new GroupBox();
            lblStatusCDMedia = new Label();
            label1 = new Label();
            cmdLoadCDMedia = new Button();
            cmdBrowseCDMedia = new Button();
            lstCDMedia = new ComboBox();
            groupBox2 = new GroupBox();
            chkStorageMediaReadOnly = new CheckBox();
            lblStatusStorageMedia = new Label();
            label3 = new Label();
            cmdLoadStorageMedia = new Button();
            cmdBrowseStorageMedia = new Button();
            lstStorageMedia = new ComboBox();
            groupBox3 = new GroupBox();
            cmdControlRemotePC = new Button();
            label2 = new Label();
            lblVersion = new Label();
            label4 = new Label();
            lblHostUSBSpeed = new Label();
            lblClientUSBSpeed = new Label();
            label6 = new Label();
            menuStrip1.SuspendLayout();
            groupBox1.SuspendLayout();
            groupBox2.SuspendLayout();
            groupBox3.SuspendLayout();
            SuspendLayout();
            // 
            // menuStrip1
            // 
            menuStrip1.Items.AddRange(new ToolStripItem[] { fileToolStripMenuItem, mediaToolStripMenuItem, favouritesToolStripMenuItem, optionsToolStripMenuItem });
            menuStrip1.Location = new Point(0, 0);
            menuStrip1.Name = "menuStrip1";
            menuStrip1.Size = new Size(523, 28);
            menuStrip1.TabIndex = 0;
            menuStrip1.Text = "menuStrip1";
            // 
            // fileToolStripMenuItem
            // 
            fileToolStripMenuItem.DropDownItems.AddRange(new ToolStripItem[] { createNewStorageDiskFileToolStripMenuItem, mountFolderAsCDROMToolStripMenuItem, toolStripMenuItem2, exitToolStripMenuItem });
            fileToolStripMenuItem.Name = "fileToolStripMenuItem";
            fileToolStripMenuItem.Size = new Size(45, 24);
            fileToolStripMenuItem.Text = "&File";
            // 
            // createNewStorageDiskFileToolStripMenuItem
            // 
            createNewStorageDiskFileToolStripMenuItem.Name = "createNewStorageDiskFileToolStripMenuItem";
            createNewStorageDiskFileToolStripMenuItem.Size = new Size(276, 24);
            createNewStorageDiskFileToolStripMenuItem.Text = "&Create new Storage Disk File";
            createNewStorageDiskFileToolStripMenuItem.Click += createNewStorageDiskFileToolStripMenuItem_Click;
            // 
            // mountFolderAsCDROMToolStripMenuItem
            // 
            mountFolderAsCDROMToolStripMenuItem.Name = "mountFolderAsCDROMToolStripMenuItem";
            mountFolderAsCDROMToolStripMenuItem.Size = new Size(276, 24);
            mountFolderAsCDROMToolStripMenuItem.Text = "&Mount a Folder as CD-ROM";
            mountFolderAsCDROMToolStripMenuItem.Click += mountFolderAsCDROMToolStripMenuItem_Click;
            // 
            // toolStripMenuItem2
            // 
            toolStripMenuItem2.Name = "toolStripMenuItem2";
            toolStripMenuItem2.Size = new Size(273, 6);
            // 
            // exitToolStripMenuItem
            // 
            exitToolStripMenuItem.Name = "exitToolStripMenuItem";
            exitToolStripMenuItem.Size = new Size(276, 24);
            exitToolStripMenuItem.Text = "E&xit";
            exitToolStripMenuItem.Click += exitToolStripMenuItem_Click;
            // 
            // mediaToolStripMenuItem
            // 
            mediaToolStripMenuItem.DropDownItems.AddRange(new ToolStripItem[] { forceEjectCDROMToolStripMenuItem, forceEjectStorageMediaToolStripMenuItem });
            mediaToolStripMenuItem.Name = "mediaToolStripMenuItem";
            mediaToolStripMenuItem.Size = new Size(64, 24);
            mediaToolStripMenuItem.Text = "&Media";
            // 
            // forceEjectCDROMToolStripMenuItem
            // 
            forceEjectCDROMToolStripMenuItem.Name = "forceEjectCDROMToolStripMenuItem";
            forceEjectCDROMToolStripMenuItem.Size = new Size(258, 24);
            forceEjectCDROMToolStripMenuItem.Text = "&Force eject CD-ROM";
            forceEjectCDROMToolStripMenuItem.Click += forceEjectCDROMToolStripMenuItem_Click;
            // 
            // forceEjectStorageMediaToolStripMenuItem
            // 
            forceEjectStorageMediaToolStripMenuItem.Name = "forceEjectStorageMediaToolStripMenuItem";
            forceEjectStorageMediaToolStripMenuItem.Size = new Size(258, 24);
            forceEjectStorageMediaToolStripMenuItem.Text = "Force eject &Storage Media";
            forceEjectStorageMediaToolStripMenuItem.Click += forceEjectStorageMediaToolStripMenuItem_Click;
            // 
            // favouritesToolStripMenuItem
            // 
            favouritesToolStripMenuItem.DropDownItems.AddRange(new ToolStripItem[] { manageFavouritesToolStripMenuItem, toolStripMenuItem1 });
            favouritesToolStripMenuItem.Name = "favouritesToolStripMenuItem";
            favouritesToolStripMenuItem.Size = new Size(94, 24);
            favouritesToolStripMenuItem.Text = "&Favourites";
            // 
            // manageFavouritesToolStripMenuItem
            // 
            manageFavouritesToolStripMenuItem.Name = "manageFavouritesToolStripMenuItem";
            manageFavouritesToolStripMenuItem.Size = new Size(211, 24);
            manageFavouritesToolStripMenuItem.Text = "&Manage Favourites";
            manageFavouritesToolStripMenuItem.Click += manageFavouritesToolStripMenuItem_Click;
            // 
            // toolStripMenuItem1
            // 
            toolStripMenuItem1.Name = "toolStripMenuItem1";
            toolStripMenuItem1.Size = new Size(208, 6);
            // 
            // optionsToolStripMenuItem
            // 
            optionsToolStripMenuItem.DropDownItems.AddRange(new ToolStripItem[] { installContextMenusToolStripMenuItem });
            optionsToolStripMenuItem.Name = "optionsToolStripMenuItem";
            optionsToolStripMenuItem.Size = new Size(76, 24);
            optionsToolStripMenuItem.Text = "&Options";
            // 
            // installContextMenusToolStripMenuItem
            // 
            installContextMenusToolStripMenuItem.Name = "installContextMenusToolStripMenuItem";
            installContextMenusToolStripMenuItem.Size = new Size(310, 24);
            installContextMenusToolStripMenuItem.Text = "Install &context menus to Explorer";
            installContextMenusToolStripMenuItem.Click += installContextMenusToolStripMenuItem_Click;
            // 
            // groupBox1
            // 
            groupBox1.Controls.Add(lblStatusCDMedia);
            groupBox1.Controls.Add(label1);
            groupBox1.Controls.Add(cmdLoadCDMedia);
            groupBox1.Controls.Add(cmdBrowseCDMedia);
            groupBox1.Controls.Add(lstCDMedia);
            groupBox1.Location = new Point(12, 31);
            groupBox1.Name = "groupBox1";
            groupBox1.Size = new Size(499, 123);
            groupBox1.TabIndex = 0;
            groupBox1.TabStop = false;
            groupBox1.Text = "CD-ROM";
            // 
            // lblStatusCDMedia
            // 
            lblStatusCDMedia.AutoSize = true;
            lblStatusCDMedia.Location = new Point(82, 92);
            lblStatusCDMedia.Name = "lblStatusCDMedia";
            lblStatusCDMedia.Size = new Size(39, 20);
            lblStatusCDMedia.TabIndex = 4;
            lblStatusCDMedia.Text = "-----";
            // 
            // label1
            // 
            label1.AutoSize = true;
            label1.Location = new Point(6, 92);
            label1.Name = "label1";
            label1.Size = new Size(57, 20);
            label1.TabIndex = 3;
            label1.Text = "Status:";
            // 
            // cmdLoadCDMedia
            // 
            cmdLoadCDMedia.Location = new Point(6, 60);
            cmdLoadCDMedia.Name = "cmdLoadCDMedia";
            cmdLoadCDMedia.Size = new Size(432, 29);
            cmdLoadCDMedia.TabIndex = 2;
            cmdLoadCDMedia.Text = "Load CD-Media";
            cmdLoadCDMedia.UseVisualStyleBackColor = true;
            cmdLoadCDMedia.Click += cmdLoadCDMedia_Click;
            // 
            // cmdBrowseCDMedia
            // 
            cmdBrowseCDMedia.Location = new Point(444, 25);
            cmdBrowseCDMedia.Name = "cmdBrowseCDMedia";
            cmdBrowseCDMedia.Size = new Size(49, 28);
            cmdBrowseCDMedia.TabIndex = 1;
            cmdBrowseCDMedia.Text = "...";
            cmdBrowseCDMedia.UseVisualStyleBackColor = true;
            cmdBrowseCDMedia.Click += cmdBrowseCDMedia_Click;
            // 
            // lstCDMedia
            // 
            lstCDMedia.FormattingEnabled = true;
            lstCDMedia.Location = new Point(6, 26);
            lstCDMedia.Name = "lstCDMedia";
            lstCDMedia.Size = new Size(432, 28);
            lstCDMedia.TabIndex = 0;
            // 
            // groupBox2
            // 
            groupBox2.Controls.Add(chkStorageMediaReadOnly);
            groupBox2.Controls.Add(lblStatusStorageMedia);
            groupBox2.Controls.Add(label3);
            groupBox2.Controls.Add(cmdLoadStorageMedia);
            groupBox2.Controls.Add(cmdBrowseStorageMedia);
            groupBox2.Controls.Add(lstStorageMedia);
            groupBox2.Location = new Point(12, 160);
            groupBox2.Name = "groupBox2";
            groupBox2.Size = new Size(499, 154);
            groupBox2.TabIndex = 1;
            groupBox2.TabStop = false;
            groupBox2.Text = "Storage";
            // 
            // chkStorageMediaReadOnly
            // 
            chkStorageMediaReadOnly.AutoSize = true;
            chkStorageMediaReadOnly.Location = new Point(6, 60);
            chkStorageMediaReadOnly.Name = "chkStorageMediaReadOnly";
            chkStorageMediaReadOnly.Size = new Size(206, 24);
            chkStorageMediaReadOnly.TabIndex = 2;
            chkStorageMediaReadOnly.Text = "Pass storage as read-only";
            chkStorageMediaReadOnly.UseVisualStyleBackColor = true;
            // 
            // lblStatusStorageMedia
            // 
            lblStatusStorageMedia.AutoSize = true;
            lblStatusStorageMedia.Location = new Point(82, 122);
            lblStatusStorageMedia.Name = "lblStatusStorageMedia";
            lblStatusStorageMedia.Size = new Size(39, 20);
            lblStatusStorageMedia.TabIndex = 4;
            lblStatusStorageMedia.Text = "-----";
            // 
            // label3
            // 
            label3.AutoSize = true;
            label3.Location = new Point(6, 122);
            label3.Name = "label3";
            label3.Size = new Size(57, 20);
            label3.TabIndex = 3;
            label3.Text = "Status:";
            // 
            // cmdLoadStorageMedia
            // 
            cmdLoadStorageMedia.Location = new Point(6, 90);
            cmdLoadStorageMedia.Name = "cmdLoadStorageMedia";
            cmdLoadStorageMedia.Size = new Size(432, 29);
            cmdLoadStorageMedia.TabIndex = 3;
            cmdLoadStorageMedia.Text = "Load Storage Media";
            cmdLoadStorageMedia.UseVisualStyleBackColor = true;
            cmdLoadStorageMedia.Click += cmdLoadStorageMedia_Click;
            // 
            // cmdBrowseStorageMedia
            // 
            cmdBrowseStorageMedia.Location = new Point(444, 25);
            cmdBrowseStorageMedia.Name = "cmdBrowseStorageMedia";
            cmdBrowseStorageMedia.Size = new Size(49, 28);
            cmdBrowseStorageMedia.TabIndex = 1;
            cmdBrowseStorageMedia.Text = "...";
            cmdBrowseStorageMedia.UseVisualStyleBackColor = true;
            cmdBrowseStorageMedia.Click += cmdBrowseStorageMedia_Click;
            // 
            // lstStorageMedia
            // 
            lstStorageMedia.FormattingEnabled = true;
            lstStorageMedia.Location = new Point(6, 26);
            lstStorageMedia.Name = "lstStorageMedia";
            lstStorageMedia.Size = new Size(432, 28);
            lstStorageMedia.TabIndex = 0;
            // 
            // groupBox3
            // 
            groupBox3.Controls.Add(cmdControlRemotePC);
            groupBox3.Location = new Point(12, 320);
            groupBox3.Name = "groupBox3";
            groupBox3.Size = new Size(499, 72);
            groupBox3.TabIndex = 2;
            groupBox3.TabStop = false;
            groupBox3.Text = "Control remote PC";
            // 
            // cmdControlRemotePC
            // 
            cmdControlRemotePC.Location = new Point(6, 26);
            cmdControlRemotePC.Name = "cmdControlRemotePC";
            cmdControlRemotePC.Size = new Size(432, 29);
            cmdControlRemotePC.TabIndex = 0;
            cmdControlRemotePC.Text = "Control remote PC";
            cmdControlRemotePC.UseVisualStyleBackColor = true;
            cmdControlRemotePC.Click += cmdControlRemotePC_Click;
            // 
            // label2
            // 
            label2.AutoSize = true;
            label2.Location = new Point(12, 407);
            label2.Name = "label2";
            label2.Size = new Size(92, 20);
            label2.TabIndex = 3;
            label2.Text = "FW Version:";
            // 
            // lblVersion
            // 
            lblVersion.AutoSize = true;
            lblVersion.Location = new Point(197, 407);
            lblVersion.Name = "lblVersion";
            lblVersion.Size = new Size(27, 20);
            lblVersion.TabIndex = 4;
            lblVersion.Text = "---";
            // 
            // label4
            // 
            label4.AutoSize = true;
            label4.Location = new Point(12, 427);
            label4.Name = "label4";
            label4.Size = new Size(125, 20);
            label4.TabIndex = 5;
            label4.Text = "Host USB Speed:";
            // 
            // lblHostUSBSpeed
            // 
            lblHostUSBSpeed.AutoSize = true;
            lblHostUSBSpeed.Location = new Point(197, 427);
            lblHostUSBSpeed.Name = "lblHostUSBSpeed";
            lblHostUSBSpeed.Size = new Size(27, 20);
            lblHostUSBSpeed.TabIndex = 6;
            lblHostUSBSpeed.Text = "---";
            // 
            // lblClientUSBSpeed
            // 
            lblClientUSBSpeed.AutoSize = true;
            lblClientUSBSpeed.Location = new Point(197, 447);
            lblClientUSBSpeed.Name = "lblClientUSBSpeed";
            lblClientUSBSpeed.Size = new Size(27, 20);
            lblClientUSBSpeed.TabIndex = 8;
            lblClientUSBSpeed.Text = "---";
            // 
            // label6
            // 
            label6.AutoSize = true;
            label6.Location = new Point(12, 447);
            label6.Name = "label6";
            label6.Size = new Size(132, 20);
            label6.TabIndex = 7;
            label6.Text = "Client USB Speed:";
            // 
            // MainDLG
            // 
            AutoScaleDimensions = new SizeF(9F, 20F);
            AutoScaleMode = AutoScaleMode.Font;
            ClientSize = new Size(523, 480);
            Controls.Add(lblClientUSBSpeed);
            Controls.Add(label6);
            Controls.Add(lblHostUSBSpeed);
            Controls.Add(label4);
            Controls.Add(lblVersion);
            Controls.Add(label2);
            Controls.Add(groupBox3);
            Controls.Add(groupBox2);
            Controls.Add(groupBox1);
            Controls.Add(menuStrip1);
            FormBorderStyle = FormBorderStyle.FixedSingle;
            Icon = (Icon)resources.GetObject("$this.Icon");
            MainMenuStrip = menuStrip1;
            MaximizeBox = false;
            Name = "MainDLG";
            StartPosition = FormStartPosition.CenterParent;
            Text = "Vulpes Dolch Host Tool";
            Load += MainDLG_Load;
            menuStrip1.ResumeLayout(false);
            menuStrip1.PerformLayout();
            groupBox1.ResumeLayout(false);
            groupBox1.PerformLayout();
            groupBox2.ResumeLayout(false);
            groupBox2.PerformLayout();
            groupBox3.ResumeLayout(false);
            ResumeLayout(false);
            PerformLayout();
        }

        #endregion

        private MenuStrip menuStrip1;
        private ToolStripMenuItem fileToolStripMenuItem;
        private ToolStripMenuItem exitToolStripMenuItem;
        private ToolStripMenuItem mediaToolStripMenuItem;
        private ToolStripMenuItem forceEjectCDROMToolStripMenuItem;
        private ToolStripMenuItem forceEjectStorageMediaToolStripMenuItem;
        private GroupBox groupBox1;
        private Label label1;
        private Button cmdLoadCDMedia;
        private Button cmdBrowseCDMedia;
        private ComboBox lstCDMedia;
        private Label lblStatusCDMedia;
        private GroupBox groupBox2;
        private Label lblStatusStorageMedia;
        private Label label3;
        private Button cmdLoadStorageMedia;
        private Button cmdBrowseStorageMedia;
        private ComboBox lstStorageMedia;
        private ToolStripMenuItem favouritesToolStripMenuItem;
        private ToolStripMenuItem manageFavouritesToolStripMenuItem;
        private ToolStripSeparator toolStripMenuItem1;
        private GroupBox groupBox3;
        private Button cmdControlRemotePC;
        private CheckBox chkStorageMediaReadOnly;
        private Label label2;
        private Label lblVersion;
        private Label label4;
        private Label lblHostUSBSpeed;
        private Label lblClientUSBSpeed;
        private Label label6;
        private ToolStripMenuItem createNewStorageDiskFileToolStripMenuItem;
        private ToolStripMenuItem mountFolderAsCDROMToolStripMenuItem;
        private ToolStripSeparator toolStripMenuItem2;
        private ToolStripMenuItem optionsToolStripMenuItem;
        private ToolStripMenuItem installContextMenusToolStripMenuItem;
    }
}
