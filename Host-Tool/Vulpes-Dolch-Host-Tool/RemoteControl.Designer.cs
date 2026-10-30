using System.Drawing;
using System.Windows.Forms;

namespace Vulpes_Dolch_Host_Tool
{
    partial class frmRemoteControl
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
            System.ComponentModel.ComponentResourceManager resources = new System.ComponentModel.ComponentResourceManager(typeof(frmRemoteControl));
            menuStrip1 = new MenuStrip();
            fileToolStripMenuItem = new ToolStripMenuItem();
            exitToolStripMenuItem = new ToolStripMenuItem();
            keyboardToolStripMenuItem = new ToolStripMenuItem();
            sendCTRLALTDELETEToolStripMenuItem = new ToolStripMenuItem();
            videoToolStripMenuItem = new ToolStripMenuItem();
            soonToolStripMenuItem = new ToolStripMenuItem();
            statusStrip1 = new StatusStrip();
            lblStatusGeneral = new ToolStripStatusLabel();
            pictureBox1 = new PictureBox();
            menuStrip1.SuspendLayout();
            statusStrip1.SuspendLayout();
            ((System.ComponentModel.ISupportInitialize)pictureBox1).BeginInit();
            SuspendLayout();
            // 
            // menuStrip1
            // 
            menuStrip1.Items.AddRange(new ToolStripItem[] { fileToolStripMenuItem, keyboardToolStripMenuItem, videoToolStripMenuItem });
            menuStrip1.Location = new Point(0, 0);
            menuStrip1.Name = "menuStrip1";
            menuStrip1.Size = new Size(800, 28);
            menuStrip1.TabIndex = 0;
            menuStrip1.Text = "menuStrip1";
            // 
            // fileToolStripMenuItem
            // 
            fileToolStripMenuItem.DropDownItems.AddRange(new ToolStripItem[] { exitToolStripMenuItem });
            fileToolStripMenuItem.Name = "fileToolStripMenuItem";
            fileToolStripMenuItem.Size = new Size(45, 24);
            fileToolStripMenuItem.Text = "&File";
            // 
            // exitToolStripMenuItem
            // 
            exitToolStripMenuItem.Name = "exitToolStripMenuItem";
            exitToolStripMenuItem.Size = new Size(104, 24);
            exitToolStripMenuItem.Text = "E&xit";
            // 
            // keyboardToolStripMenuItem
            // 
            keyboardToolStripMenuItem.DropDownItems.AddRange(new ToolStripItem[] { sendCTRLALTDELETEToolStripMenuItem });
            keyboardToolStripMenuItem.Name = "keyboardToolStripMenuItem";
            keyboardToolStripMenuItem.Size = new Size(88, 24);
            keyboardToolStripMenuItem.Text = "&Keyboard";
            // 
            // sendCTRLALTDELETEToolStripMenuItem
            // 
            sendCTRLALTDELETEToolStripMenuItem.Name = "sendCTRLALTDELETEToolStripMenuItem";
            sendCTRLALTDELETEToolStripMenuItem.Size = new Size(253, 24);
            sendCTRLALTDELETEToolStripMenuItem.Text = "&Send CTRL+ALT+DELETE";
            sendCTRLALTDELETEToolStripMenuItem.Click += sendCTRLALTDELETEToolStripMenuItem_Click;
            // 
            // videoToolStripMenuItem
            // 
            videoToolStripMenuItem.DropDownItems.AddRange(new ToolStripItem[] { soonToolStripMenuItem });
            videoToolStripMenuItem.Name = "videoToolStripMenuItem";
            videoToolStripMenuItem.Size = new Size(61, 24);
            videoToolStripMenuItem.Text = "&Video";
            // 
            // soonToolStripMenuItem
            // 
            soonToolStripMenuItem.Enabled = false;
            soonToolStripMenuItem.Name = "soonToolStripMenuItem";
            soonToolStripMenuItem.Size = new Size(124, 24);
            soonToolStripMenuItem.Text = "(soon)";
            // 
            // statusStrip1
            // 
            statusStrip1.Items.AddRange(new ToolStripItem[] { lblStatusGeneral });
            statusStrip1.Location = new Point(0, 425);
            statusStrip1.Name = "statusStrip1";
            statusStrip1.Size = new Size(800, 25);
            statusStrip1.TabIndex = 1;
            statusStrip1.Text = "statusStrip1";
            // 
            // lblStatusGeneral
            // 
            lblStatusGeneral.AutoSize = false;
            lblStatusGeneral.BorderSides = ToolStripStatusLabelBorderSides.Left | ToolStripStatusLabelBorderSides.Top | ToolStripStatusLabelBorderSides.Right | ToolStripStatusLabelBorderSides.Bottom;
            lblStatusGeneral.BorderStyle = Border3DStyle.SunkenOuter;
            lblStatusGeneral.Name = "lblStatusGeneral";
            lblStatusGeneral.Size = new Size(300, 20);
            lblStatusGeneral.Text = "---";
            lblStatusGeneral.TextAlign = ContentAlignment.MiddleLeft;
            // 
            // pictureBox1
            // 
            pictureBox1.BackColor = Color.Teal;
            pictureBox1.Dock = DockStyle.Fill;
            pictureBox1.Location = new Point(0, 28);
            pictureBox1.Name = "pictureBox1";
            pictureBox1.Size = new Size(800, 397);
            pictureBox1.TabIndex = 2;
            pictureBox1.TabStop = false;
            // 
            // frmRemoteControl
            // 
            AutoScaleDimensions = new SizeF(9F, 20F);
            AutoScaleMode = AutoScaleMode.Font;
            ClientSize = new Size(800, 450);
            Controls.Add(pictureBox1);
            Controls.Add(statusStrip1);
            Controls.Add(menuStrip1);
            Icon = (Icon)resources.GetObject("$this.Icon");
            MainMenuStrip = menuStrip1;
            Name = "frmRemoteControl";
            StartPosition = FormStartPosition.CenterParent;
            Text = "Remote Control a PC";
            menuStrip1.ResumeLayout(false);
            menuStrip1.PerformLayout();
            statusStrip1.ResumeLayout(false);
            statusStrip1.PerformLayout();
            ((System.ComponentModel.ISupportInitialize)pictureBox1).EndInit();
            ResumeLayout(false);
            PerformLayout();
        }

        #endregion

        private MenuStrip menuStrip1;
        private ToolStripMenuItem fileToolStripMenuItem;
        private ToolStripMenuItem exitToolStripMenuItem;
        private ToolStripMenuItem videoToolStripMenuItem;
        private ToolStripMenuItem soonToolStripMenuItem;
        private StatusStrip statusStrip1;
        private ToolStripStatusLabel lblStatusGeneral;
        private PictureBox pictureBox1;
        private ToolStripMenuItem keyboardToolStripMenuItem;
        private ToolStripMenuItem sendCTRLALTDELETEToolStripMenuItem;
    }
}