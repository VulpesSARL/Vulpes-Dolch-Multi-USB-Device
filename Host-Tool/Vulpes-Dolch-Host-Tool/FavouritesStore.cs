using System;
using System.Collections.Generic;

namespace Vulpes_Dolch_Host_Tool
{
    /// <summary>One favourite image. ReadOnly only means something for storage images (how the disk is passed to the target PC).</summary>
    internal sealed class Favourite
    {
        public string Path;
        public bool ReadOnly;

        public Favourite(string Path, bool ReadOnly)
        {
            this.Path = Path;
            this.ReadOnly = ReadOnly;
        }
    }

    /// <summary>
    /// The favourite images, kept in the registry (HKCU\Software\Fox\Vulpes Dolch Tool) as two lists, one for CD images and one for storage images.
    /// A stored entry is "0|path" or "1|path": the first character is the read-only flag (a Windows path never holds a '|').
    /// </summary>
    internal static class FavouritesStore
    {
        private const string CdName = "FavouritesCd";
        private const string StorageName = "FavouritesStorage";

        public static List<Favourite> LoadCd()
        {
            return (Load(CdName));
        }

        public static List<Favourite> LoadStorage()
        {
            return (Load(StorageName));
        }

        public static void SaveCd(List<Favourite> Items)
        {
            Save(CdName, Items);
        }

        public static void SaveStorage(List<Favourite> Items)
        {
            Save(StorageName, Items);
        }

        private static List<Favourite> Load(string Name)
        {
            List<Favourite> Result = new List<Favourite>();
            foreach (string Stored in AppSettings.LoadList(Name))
            {
                if ((Stored == null) || (Stored.Length < 3) || (Stored[1] != '|'))
                    continue;

                Result.Add(new Favourite(Stored.Substring(2), Stored[0] == '1'));
            }
            return (Result);
        }

        private static void Save(string Name, List<Favourite> Items)
        {
            List<string> Stored = new List<string>();
            foreach (Favourite Item in Items)
                Stored.Add(((Item.ReadOnly == true) ? "1|" : "0|") + Item.Path);
            AppSettings.SaveList(Name, Stored);
        }
    }
}
