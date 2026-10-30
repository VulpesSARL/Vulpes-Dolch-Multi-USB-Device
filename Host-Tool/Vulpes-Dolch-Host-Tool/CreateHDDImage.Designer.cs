namespace Vulpes_Dolch_Host_Tool
{
    partial class frmCreateHDDImage
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
            cmdCancel = new System.Windows.Forms.Button();
            cmdOK = new System.Windows.Forms.Button();
            label1 = new System.Windows.Forms.Label();
            lstImageType = new System.Windows.Forms.ComboBox();
            txtFilename = new System.Windows.Forms.TextBox();
            label2 = new System.Windows.Forms.Label();
            cmdBrowseNewFile = new System.Windows.Forms.Button();
            label3 = new System.Windows.Forms.Label();
            lstSizeUnit = new System.Windows.Forms.ComboBox();
            txtSize = new System.Windows.Forms.TextBox();
            lstPrepare = new System.Windows.Forms.ComboBox();
            label4 = new System.Windows.Forms.Label();
            chkLoadAfterwards = new System.Windows.Forms.CheckBox();
            SuspendLayout();
            // 
            // cmdCancel
            // 
            cmdCancel.Location = new System.Drawing.Point(510, 228);
            cmdCancel.Name = "cmdCancel";
            cmdCancel.Size = new System.Drawing.Size(75, 29);
            cmdCancel.TabIndex = 8;
            cmdCancel.Text = "Cancel";
            cmdCancel.UseVisualStyleBackColor = true;
            cmdCancel.Click += cmdCancel_Click;
            // 
            // cmdOK
            // 
            cmdOK.Location = new System.Drawing.Point(429, 228);
            cmdOK.Name = "cmdOK";
            cmdOK.Size = new System.Drawing.Size(75, 29);
            cmdOK.TabIndex = 7;
            cmdOK.Text = "OK";
            cmdOK.UseVisualStyleBackColor = true;
            cmdOK.Click += cmdOK_Click;
            // 
            // label1
            // 
            label1.AutoSize = true;
            label1.Location = new System.Drawing.Point(12, 15);
            label1.Name = "label1";
            label1.Size = new System.Drawing.Size(46, 20);
            label1.TabIndex = 2;
            label1.Text = "Type:";
            // 
            // lstImageType
            // 
            lstImageType.DropDownStyle = System.Windows.Forms.ComboBoxStyle.DropDownList;
            lstImageType.FormattingEnabled = true;
            lstImageType.Location = new System.Drawing.Point(106, 12);
            lstImageType.Name = "lstImageType";
            lstImageType.Size = new System.Drawing.Size(479, 28);
            lstImageType.TabIndex = 0;
            // 
            // txtFilename
            // 
            txtFilename.Location = new System.Drawing.Point(106, 46);
            txtFilename.Name = "txtFilename";
            txtFilename.Size = new System.Drawing.Size(424, 27);
            txtFilename.TabIndex = 1;
            // 
            // label2
            // 
            label2.AutoSize = true;
            label2.Location = new System.Drawing.Point(12, 49);
            label2.Name = "label2";
            label2.Size = new System.Drawing.Size(37, 20);
            label2.TabIndex = 5;
            label2.Text = "File:";
            // 
            // cmdBrowseNewFile
            // 
            cmdBrowseNewFile.Location = new System.Drawing.Point(536, 45);
            cmdBrowseNewFile.Name = "cmdBrowseNewFile";
            cmdBrowseNewFile.Size = new System.Drawing.Size(49, 28);
            cmdBrowseNewFile.TabIndex = 2;
            cmdBrowseNewFile.Text = "...";
            cmdBrowseNewFile.UseVisualStyleBackColor = true;
            // 
            // label3
            // 
            label3.AutoSize = true;
            label3.Location = new System.Drawing.Point(12, 83);
            label3.Name = "label3";
            label3.Size = new System.Drawing.Size(40, 20);
            label3.TabIndex = 7;
            label3.Text = "Size:";
            // 
            // lstSizeUnit
            // 
            lstSizeUnit.DropDownStyle = System.Windows.Forms.ComboBoxStyle.DropDownList;
            lstSizeUnit.FormattingEnabled = true;
            lstSizeUnit.Location = new System.Drawing.Point(282, 79);
            lstSizeUnit.Name = "lstSizeUnit";
            lstSizeUnit.Size = new System.Drawing.Size(303, 28);
            lstSizeUnit.TabIndex = 4;
            // 
            // txtSize
            // 
            txtSize.Location = new System.Drawing.Point(106, 80);
            txtSize.Name = "txtSize";
            txtSize.Size = new System.Drawing.Size(170, 27);
            txtSize.TabIndex = 3;
            // 
            // lstPrepare
            // 
            lstPrepare.DropDownStyle = System.Windows.Forms.ComboBoxStyle.DropDownList;
            lstPrepare.FormattingEnabled = true;
            lstPrepare.Location = new System.Drawing.Point(106, 133);
            lstPrepare.Name = "lstPrepare";
            lstPrepare.Size = new System.Drawing.Size(479, 28);
            lstPrepare.TabIndex = 5;
            // 
            // label4
            // 
            label4.AutoSize = true;
            label4.Location = new System.Drawing.Point(12, 136);
            label4.Name = "label4";
            label4.Size = new System.Drawing.Size(67, 20);
            label4.TabIndex = 11;
            label4.Text = "Prepare:";
            // 
            // chkLoadAfterwards
            // 
            chkLoadAfterwards.AutoSize = true;
            chkLoadAfterwards.Location = new System.Drawing.Point(106, 179);
            chkLoadAfterwards.Name = "chkLoadAfterwards";
            chkLoadAfterwards.Size = new System.Drawing.Size(208, 24);
            chkLoadAfterwards.TabIndex = 6;
            chkLoadAfterwards.Text = "&Load image after creation";
            chkLoadAfterwards.UseVisualStyleBackColor = true;
            // 
            // frmCreateHDDImage
            // 
            AcceptButton = cmdOK;
            AutoScaleDimensions = new System.Drawing.SizeF(9F, 20F);
            AutoScaleMode = System.Windows.Forms.AutoScaleMode.Font;
            CancelButton = cmdCancel;
            ClientSize = new System.Drawing.Size(597, 269);
            Controls.Add(chkLoadAfterwards);
            Controls.Add(label4);
            Controls.Add(lstPrepare);
            Controls.Add(txtSize);
            Controls.Add(lstSizeUnit);
            Controls.Add(label3);
            Controls.Add(cmdBrowseNewFile);
            Controls.Add(label2);
            Controls.Add(txtFilename);
            Controls.Add(lstImageType);
            Controls.Add(label1);
            Controls.Add(cmdOK);
            Controls.Add(cmdCancel);
            MaximizeBox = false;
            MinimizeBox = false;
            Name = "frmCreateHDDImage";
            ShowIcon = false;
            ShowInTaskbar = false;
            StartPosition = System.Windows.Forms.FormStartPosition.CenterParent;
            Text = "Create Harddisk image file";
            Load += frmCreateHDDImage_Load;
            ResumeLayout(false);
            PerformLayout();
        }

        #endregion

        private System.Windows.Forms.Button cmdCancel;
        private System.Windows.Forms.Button cmdOK;
        private System.Windows.Forms.Label label1;
        private System.Windows.Forms.ComboBox lstImageType;
        private System.Windows.Forms.TextBox txtFilename;
        private System.Windows.Forms.Label label2;
        private System.Windows.Forms.Button cmdBrowseNewFile;
        private System.Windows.Forms.Label label3;
        private System.Windows.Forms.ComboBox lstSizeUnit;
        private System.Windows.Forms.TextBox txtSize;
        private System.Windows.Forms.ComboBox lstPrepare;
        private System.Windows.Forms.Label label4;
        private System.Windows.Forms.CheckBox chkLoadAfterwards;
    }
}