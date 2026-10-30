using System;
using Microsoft.Win32;
using VulpesDolch.Host.Common;

namespace Vulpes_Dolch_Host_Service
{
    /// <summary>
    /// What the service had in the drives, kept in HKCU\Software\Fox\Vulpes Dolch Tool\Service (for the service account, SYSTEM, that is its own hive), so that a
    /// restart of the service or of the PC can find the images again (see <see cref="HostDevice.EnableRecovery"/>).
    /// </summary>
    internal sealed class RegistryMediaState : ISavedMediaState
    {
        private const string KeyPath = @"Software\Fox\Vulpes Dolch Tool\Service";

        public void Load(out string CdPath, out string DiskPath, out bool DiskReadOnly)
        {
            CdPath = null;
            DiskPath = null;
            DiskReadOnly = false;

            using (RegistryKey Key = Registry.CurrentUser.OpenSubKey(KeyPath))
            {
                if (Key == null)
                    return;

                CdPath = Key.GetValue("CdPath") as string;
                DiskPath = Key.GetValue("DiskPath") as string;
                object ReadOnly = Key.GetValue("DiskReadOnly");
                DiskReadOnly = (ReadOnly is int) && ((int)ReadOnly != 0);
            }
        }

        public void Save(string CdPath, string DiskPath, bool DiskReadOnly)
        {
            using (RegistryKey Key = Registry.CurrentUser.CreateSubKey(KeyPath))
            {
                SetOrDelete(Key, "CdPath", CdPath);
                SetOrDelete(Key, "DiskPath", DiskPath);
                Key.SetValue("DiskReadOnly", (DiskReadOnly == true) ? 1 : 0, RegistryValueKind.DWord);
            }
        }

        private static void SetOrDelete(RegistryKey Key, string Name, string Value)
        {
            if (string.IsNullOrEmpty(Value) == true)
                Key.DeleteValue(Name, false);
            else
                Key.SetValue(Name, Value, RegistryValueKind.String);
        }
    }
}
