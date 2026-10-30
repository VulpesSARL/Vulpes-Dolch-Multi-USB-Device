using System;
using System.Collections.Generic;
using System.Windows.Forms;

namespace Vulpes_Dolch_Host_Tool
{
    /// <summary>
    /// The history behind one of the media comboboxes: at most 10 paths, the newest on top. A path that is used moves to the top (a load or a pick from the
    /// middle of the list), a path that cannot be loaded is removed. Kept in the registry under the given value name.
    /// </summary>
    internal sealed class MediaHistory
    {
        public const int MaxEntries = 10;

        private readonly string ValueName;
        private readonly ComboBox Box;
        private readonly List<string> Items;

        public MediaHistory(string ValueName, ComboBox Box)
        {
            this.ValueName = ValueName;
            this.Box = Box;
            this.Items = AppSettings.LoadList(ValueName);
            Trim();
            Refill(string.Empty);
        }

        /// <summary>The path was loaded (or picked): it goes to the top.</summary>
        public void Promote(string Path)
        {
            if (string.IsNullOrWhiteSpace(Path) == true)
                return;

            RemoveEntry(Path);
            Items.Insert(0, Path);
            Trim();
            Save();
            Refill(Path);
        }

        /// <summary>The path cannot be loaded (any more): it leaves the list.</summary>
        public void Drop(string Path)
        {
            if (RemoveEntry(Path) == false)
                return;

            Save();
            Refill(string.Empty);
        }

        private bool RemoveEntry(string Path)
        {
            for (int Index = 0; Index < Items.Count; Index++)
            {
                if (string.Equals(Items[Index], Path, StringComparison.OrdinalIgnoreCase) == true)
                {
                    Items.RemoveAt(Index);
                    return (true);
                }
            }
            return (false);
        }

        private void Trim()
        {
            while (Items.Count > MaxEntries)
                Items.RemoveAt(Items.Count - 1);
        }

        private void Save()
        {
            AppSettings.SaveList(ValueName, Items);
        }

        private void Refill(string Text)
        {
            Box.BeginUpdate();
            Box.Items.Clear();
            foreach (string Item in Items)
                Box.Items.Add(Item);
            Box.EndUpdate();
            Box.Text = Text;
        }
    }
}
