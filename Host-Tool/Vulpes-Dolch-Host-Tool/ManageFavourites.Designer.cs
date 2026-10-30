using System.Drawing;
using System.Windows.Forms;

namespace Vulpes_Dolch_Host_Tool
{
    partial class frmManageFavourites
    {
        /// <summary>
        /// Required designer variable.
        /// </summary>
        private System.ComponentModel.IContainer components = null;

        /// <summary>
        /// Clean up any resources being used.
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
        /// Required method for Designer support - do not modify
        /// the contents of this method with the code editor.
        /// </summary>
        private void InitializeComponent()
        {
            ListViewGroup listViewGroup3 = new ListViewGroup("CD Medias", HorizontalAlignment.Left);
            ListViewGroup listViewGroup4 = new ListViewGroup("Storage Medias", HorizontalAlignment.Left);
            cmdClose = new Button();
            lstFavourites = new ListView();
            columnHeader1 = new ColumnHeader();
            cmdAddCD = new Button();
            cmdAddStorage = new Button();
            cmdMoveUp = new Button();
            cmdMoveDown = new Button();
            cmdRemove = new Button();
            columnHeader2 = new ColumnHeader();
            cmdSetUnSetReadOnly = new Button();
            SuspendLayout();
            // 
            // cmdClose
            // 
            cmdClose.Location = new Point(764, 500);
            cmdClose.Name = "cmdClose";
            cmdClose.Size = new Size(115, 29);
            cmdClose.TabIndex = 7;
            cmdClose.Text = "Close";
            cmdClose.UseVisualStyleBackColor = true;
            cmdClose.Click += cmdClose_Click;
            // 
            // lstFavourites
            // 
            lstFavourites.Columns.AddRange(new ColumnHeader[] { columnHeader1, columnHeader2 });
            lstFavourites.FullRowSelect = true;
            listViewGroup3.Header = "CD Medias";
            listViewGroup3.Name = "listViewGroup1";
            listViewGroup4.Header = "Storage Medias";
            listViewGroup4.Name = "listViewGroup2";
            lstFavourites.Groups.AddRange(new ListViewGroup[] { listViewGroup3, listViewGroup4 });
            lstFavourites.Location = new Point(12, 12);
            lstFavourites.MultiSelect = false;
            lstFavourites.Name = "lstFavourites";
            lstFavourites.Size = new Size(746, 517);
            lstFavourites.TabIndex = 0;
            lstFavourites.UseCompatibleStateImageBehavior = false;
            lstFavourites.View = View.Details;
            // 
            // columnHeader1
            // 
            columnHeader1.Text = "Files";
            columnHeader1.Width = 610;
            // 
            // cmdAddCD
            // 
            cmdAddCD.Location = new Point(764, 12);
            cmdAddCD.Name = "cmdAddCD";
            cmdAddCD.Size = new Size(115, 29);
            cmdAddCD.TabIndex = 1;
            cmdAddCD.Text = "Add CD";
            cmdAddCD.UseVisualStyleBackColor = true;
            cmdAddCD.Click += cmdAddCD_Click;
            // 
            // cmdAddStorage
            // 
            cmdAddStorage.Location = new Point(764, 47);
            cmdAddStorage.Name = "cmdAddStorage";
            cmdAddStorage.Size = new Size(115, 29);
            cmdAddStorage.TabIndex = 2;
            cmdAddStorage.Text = "Add Storage";
            cmdAddStorage.UseVisualStyleBackColor = true;
            cmdAddStorage.Click += cmdAddStorage_Click;
            // 
            // cmdMoveUp
            // 
            cmdMoveUp.Location = new Point(764, 159);
            cmdMoveUp.Name = "cmdMoveUp";
            cmdMoveUp.Size = new Size(115, 29);
            cmdMoveUp.TabIndex = 3;
            cmdMoveUp.Text = "Move up";
            cmdMoveUp.UseVisualStyleBackColor = true;
            cmdMoveUp.Click += cmdMoveUp_Click;
            // 
            // cmdMoveDown
            // 
            cmdMoveDown.Location = new Point(764, 194);
            cmdMoveDown.Name = "cmdMoveDown";
            cmdMoveDown.Size = new Size(115, 29);
            cmdMoveDown.TabIndex = 4;
            cmdMoveDown.Text = "Move down";
            cmdMoveDown.UseVisualStyleBackColor = true;
            cmdMoveDown.Click += cmdMoveDown_Click;
            // 
            // cmdRemove
            // 
            cmdRemove.Location = new Point(764, 389);
            cmdRemove.Name = "cmdRemove";
            cmdRemove.Size = new Size(115, 29);
            cmdRemove.TabIndex = 6;
            cmdRemove.Text = "Remove";
            cmdRemove.UseVisualStyleBackColor = true;
            cmdRemove.Click += cmdRemove_Click;
            // 
            // columnHeader2
            // 
            columnHeader2.Text = "Read-Only";
            columnHeader2.Width = 100;
            // 
            // cmdSetUnSetReadOnly
            // 
            cmdSetUnSetReadOnly.Location = new Point(764, 252);
            cmdSetUnSetReadOnly.Name = "cmdSetUnSetReadOnly";
            cmdSetUnSetReadOnly.Size = new Size(115, 29);
            cmdSetUnSetReadOnly.TabIndex = 5;
            cmdSetUnSetReadOnly.Text = "S/U R/O?";
            cmdSetUnSetReadOnly.UseVisualStyleBackColor = true;
            cmdSetUnSetReadOnly.Click += cmdSetUnSetReadOnly_Click;
            // 
            // frmManageFavourites
            // 
            AutoScaleDimensions = new SizeF(9F, 20F);
            AutoScaleMode = AutoScaleMode.Font;
            CancelButton = cmdClose;
            ClientSize = new Size(891, 541);
            Controls.Add(cmdSetUnSetReadOnly);
            Controls.Add(cmdRemove);
            Controls.Add(cmdMoveDown);
            Controls.Add(cmdMoveUp);
            Controls.Add(cmdAddStorage);
            Controls.Add(cmdAddCD);
            Controls.Add(lstFavourites);
            Controls.Add(cmdClose);
            MaximizeBox = false;
            MinimizeBox = false;
            Name = "frmManageFavourites";
            ShowIcon = false;
            ShowInTaskbar = false;
            StartPosition = FormStartPosition.CenterParent;
            Text = "Manage Favourites";
            Load += frmManageFavourites_Load;
            ResumeLayout(false);
        }

        #endregion

        private Button cmdClose;
        private ListView lstFavourites;
        private ColumnHeader columnHeader1;
        private Button cmdAddCD;
        private Button cmdAddStorage;
        private Button cmdMoveUp;
        private Button cmdMoveDown;
        private Button cmdRemove;
        private ColumnHeader columnHeader2;
        private Button cmdSetUnSetReadOnly;
    }
}