using System;
using System.Runtime.InteropServices;
using Microsoft.Win32;

namespace Vulpes_Dolch_Host_Tool
{
    /// <summary>
    /// The "Mount with Vulpes Dolch" entries of the Explorer context menu. They live in HKCU\Software\Classes (no administrator rights, only for this user) and call this
    /// program with /mountcd or /mountdisk and the path. File entries are registered below SystemFileAssociations, so the program associations of the extensions
    /// (what a double click does) are not touched.
    /// </summary>
    internal static class ExplorerIntegration
    {
        public const string FileText = "Mount with Vulpes Dolch Multi-USB";
        public const string FolderText = "Mount with Vulpes Dolch Multi USB as CD-ROM";

        public const string CdSwitch = "/mountcd";
        public const string DiskSwitch = "/mountdisk";

        private const string ClassesPath = @"Software\Classes";
        private const string VerbName = "VulpesDolch.MountMedia";
        private const string FileAssociations = @"SystemFileAssociations\";
        private const string FolderShell = @"Directory\shell";
        private const int SHCNE_ASSOCCHANGED = 0x08000000;

        // Images that are presented as a CD-ROM / as a disk, decided by the extension.
        private static readonly string[] CdExtensions = { ".iso", ".cue" };
        private static readonly string[] DiskExtensions = { ".vhdx", ".vhd", ".vdi", ".vmdk", ".bin", ".img" };

        [DllImport("shell32.dll")]
        private static extern void SHChangeNotify(int EventId, uint Flags, IntPtr Item1, IntPtr Item2);

        private static string ProgramPath()
        {
            return (Environment.ProcessPath);
        }

        private static string CommandText(string Switch)
        {
            return ("\"" + ProgramPath() + "\" " + Switch + " \"%1\"");
        }

        private static string ShellPath(string Extension)
        {
            return (FileAssociations + Extension + @"\shell");
        }

        /// <summary>True when the entries are in the registry (checked on the entry of the .iso files).</summary>
        public static bool IsInstalled()
        {
            try
            {
                using (RegistryKey Key = Registry.CurrentUser.OpenSubKey(ClassesPath + "\\" + ShellPath(CdExtensions[0]) + "\\" + VerbName + @"\command"))
                    return (Key != null);
            }
            catch (Exception)
            {
                return (false);
            }
        }

        /// <summary>Adds the entries (or brings them up to date); throws when the registry cannot be written.</summary>
        public static void Install()
        {
            foreach (string Extension in CdExtensions)
                WriteVerb(ShellPath(Extension), FileText, CdSwitch);

            foreach (string Extension in DiskExtensions)
                WriteVerb(ShellPath(Extension), FileText, DiskSwitch);

            WriteVerb(FolderShell, FolderText, CdSwitch);
            SHChangeNotify(SHCNE_ASSOCCHANGED, 0, IntPtr.Zero, IntPtr.Zero);
        }

        /// <summary>Removes the entries, and the keys that were only there for them.</summary>
        public static void Uninstall()
        {
            foreach (string Extension in CdExtensions)
                DeleteVerb(ShellPath(Extension), true);

            foreach (string Extension in DiskExtensions)
                DeleteVerb(ShellPath(Extension), true);

            DeleteVerb(FolderShell, false);
            SHChangeNotify(SHCNE_ASSOCCHANGED, 0, IntPtr.Zero, IntPtr.Zero);
        }

        /// <summary>At the start of the program: installed entries that point to another copy of the program (it was moved) are rewritten.</summary>
        public static void RefreshIfInstalled()
        {
            try
            {
                if (IsInstalled() == false)
                    return;

                using (RegistryKey Key = Registry.CurrentUser.OpenSubKey(ClassesPath + "\\" + ShellPath(CdExtensions[0]) + "\\" + VerbName + @"\command"))
                {
                    if (string.Equals(Key.GetValue("") as string, CommandText(CdSwitch), StringComparison.OrdinalIgnoreCase) == true)
                        return;
                }
                Install();
            }
            catch (Exception)
            {
                // The entries keep working with the old path or stay as they are; the program must start anyway.
            }
        }

        private static void WriteVerb(string ParentPath, string Text, string Switch)
        {
            string VerbPath = ClassesPath + "\\" + ParentPath + "\\" + VerbName;
            using (RegistryKey Verb = Registry.CurrentUser.CreateSubKey(VerbPath))
            {
                Verb.SetValue("", Text);
                Verb.SetValue("Icon", "\"" + ProgramPath() + "\",0");
                Verb.SetValue("MultiSelectModel", "Single");
            }

            using (RegistryKey Command = Registry.CurrentUser.CreateSubKey(VerbPath + @"\command"))
                Command.SetValue("", CommandText(Switch));
        }

        private static void DeleteVerb(string ParentPath, bool RemoveEmptyParents)
        {
            using (RegistryKey Classes = Registry.CurrentUser.OpenSubKey(ClassesPath, true))
            {
                if (Classes == null)
                    return;

                Classes.DeleteSubKeyTree(ParentPath + "\\" + VerbName, false);
                if (RemoveEmptyParents == false)
                    return;

                // ...\shell, then the extension key below SystemFileAssociations, if nothing else is in them.
                DeleteIfEmpty(Classes, ParentPath);
                DeleteIfEmpty(Classes, ParentPath.Substring(0, ParentPath.LastIndexOf('\\')));
            }
        }

        private static void DeleteIfEmpty(RegistryKey Classes, string Path)
        {
            using (RegistryKey Key = Classes.OpenSubKey(Path))
            {
                if ((Key == null) || (Key.SubKeyCount != 0) || (Key.ValueCount != 0))
                    return;
            }
            Classes.DeleteSubKey(Path, false);
        }
    }
}
