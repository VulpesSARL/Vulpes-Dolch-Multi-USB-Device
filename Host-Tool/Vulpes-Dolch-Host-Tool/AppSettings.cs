using System;
using System.Collections.Generic;
using Microsoft.Win32;

namespace Vulpes_Dolch_Host_Tool
{
    /// <summary>All settings of the application live in HKCU\Software\Fox\Vulpes Dolch Tool.</summary>
    internal static class AppSettings
    {
        public const string KeyPath = @"Software\Fox\Vulpes Dolch Tool";

        public static List<string> LoadList(string Name)
        {
            List<string> Items = new List<string>();
            try
            {
                using (RegistryKey Key = Registry.CurrentUser.OpenSubKey(KeyPath))
                {
                    if (Key == null)
                        return (Items);

                    string[] Stored = Key.GetValue(Name) as string[];
                    if (Stored != null)
                        Items.AddRange(Stored);
                }
            }
            catch (Exception)
            {
                // Unreadable settings are treated as empty.
            }
            return (Items);
        }

        public static void SaveList(string Name, List<string> Items)
        {
            try
            {
                using (RegistryKey Key = Registry.CurrentUser.CreateSubKey(KeyPath))
                    Key.SetValue(Name, Items.ToArray(), RegistryValueKind.MultiString);
            }
            catch (Exception)
            {
                // A failing save must never disturb the application.
            }
        }

        public static bool GetBool(string Name, bool Default)
        {
            try
            {
                using (RegistryKey Key = Registry.CurrentUser.OpenSubKey(KeyPath))
                {
                    if (Key == null)
                        return (Default);

                    object Value = Key.GetValue(Name);
                    if (Value is int)
                        return ((int)Value != 0);
                }
            }
            catch (Exception)
            {
            }
            return (Default);
        }

        public static void SetBool(string Name, bool Value)
        {
            try
            {
                using (RegistryKey Key = Registry.CurrentUser.CreateSubKey(KeyPath))
                    Key.SetValue(Name, (Value == true) ? 1 : 0, RegistryValueKind.DWord);
            }
            catch (Exception)
            {
            }
        }

        public static int GetInt(string Name, int Default)
        {
            try
            {
                using (RegistryKey Key = Registry.CurrentUser.OpenSubKey(KeyPath))
                {
                    if (Key == null)
                        return (Default);

                    object Value = Key.GetValue(Name);
                    if (Value is int)
                        return ((int)Value);
                }
            }
            catch (Exception)
            {
            }
            return (Default);
        }

        public static void SetInt(string Name, int Value)
        {
            try
            {
                using (RegistryKey Key = Registry.CurrentUser.CreateSubKey(KeyPath))
                    Key.SetValue(Name, Value, RegistryValueKind.DWord);
            }
            catch (Exception)
            {
            }
        }
    }
}
