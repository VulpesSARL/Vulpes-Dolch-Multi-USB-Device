using System;
using System.Collections.Generic;
using System.IO;
using System.Windows.Forms;

namespace Vulpes_Dolch_Host_Tool
{
    /// <summary>
    /// The list of favourite images, grouped into CD images and storage images. Every change is saved at once; the order inside a group is the order of the
    /// Favourites menu of the main dialog.
    /// </summary>
    public partial class frmManageFavourites : Form
    {
        private const string CdFilter = "CD / DVD / BD images (*.iso;*.cue)|*.iso;*.cue|All files (*.*)|*.*";
        private const string StorageFilter = "Disk images (*.vhdx;*.vhd;*.vmdk;*.vdi;*.img;*.raw;*.bin;*.dd)|*.vhdx;*.vhd;*.vmdk;*.vdi;*.img;*.raw;*.bin;*.dd|All files (*.*)|*.*";

        private List<Favourite> CdItems;
        private List<Favourite> StorageItems;

        public frmManageFavourites()
        {
            InitializeComponent();
        }

        private void frmManageFavourites_Load(object sender, EventArgs e)
        {
            CdItems = FavouritesStore.LoadCd();
            StorageItems = FavouritesStore.LoadStorage();
            lstFavourites.SelectedIndexChanged += delegate (object Sender, EventArgs Args) { UpdateButtons(); };
            lstFavourites.DoubleClick += delegate (object Sender, EventArgs Args) { ToggleReadOnly(); };
            Fill(null);
        }

        private void cmdClose_Click(object sender, EventArgs e)
        {
            this.Close();
        }

        // ---- list <-> items ----

        private bool SelectedIsCd()
        {
            return ((lstFavourites.SelectedItems.Count > 0) && (lstFavourites.SelectedItems[0].Group == lstFavourites.Groups[0]));
        }

        private List<Favourite> SelectedList()
        {
            return ((SelectedIsCd() == true) ? CdItems : StorageItems);
        }

        private Favourite SelectedFavourite()
        {
            if (lstFavourites.SelectedItems.Count == 0)
                return (null);

            return (lstFavourites.SelectedItems[0].Tag as Favourite);
        }

        /// <summary>Rebuilds the list view; <paramref name="Select"/> (if given) is selected afterwards.</summary>
        private void Fill(Favourite Select)
        {
            lstFavourites.BeginUpdate();
            lstFavourites.Items.Clear();
            AddGroup(CdItems, lstFavourites.Groups[0], Select);
            AddGroup(StorageItems, lstFavourites.Groups[1], Select);
            lstFavourites.EndUpdate();
            UpdateButtons();
        }

        private void AddGroup(List<Favourite> Items, ListViewGroup Group, Favourite Select)
        {
            foreach (Favourite Item in Items)
            {
                ListViewItem Row = new ListViewItem(Item.Path);
                Row.SubItems.Add((Group == lstFavourites.Groups[1]) ? ((Item.ReadOnly == true) ? "yes" : "no") : "");
                Row.Group = Group;
                Row.Tag = Item;
                lstFavourites.Items.Add(Row);
                if (Item == Select)
                {
                    Row.Selected = true;
                    Row.EnsureVisible();
                }
            }
        }

        private void UpdateButtons()
        {
            Favourite Chosen = SelectedFavourite();
            bool Any = (Chosen != null);
            List<Favourite> Items = (Any == true) ? SelectedList() : null;
            int Index = (Any == true) ? Items.IndexOf(Chosen) : -1;

            cmdRemove.Enabled = Any;
            cmdMoveUp.Enabled = (Any == true) && (Index > 0);
            cmdMoveDown.Enabled = (Any == true) && (Index < Items.Count - 1);
            cmdSetUnSetReadOnly.Enabled = (Any == true) && (SelectedIsCd() == false);
        }

        private void SaveAll()
        {
            FavouritesStore.SaveCd(CdItems);
            FavouritesStore.SaveStorage(StorageItems);
        }

        // ---- buttons ----

        private void AddFiles(List<Favourite> Items, string Title, string Filter, bool Storage)
        {
            using (OpenFileDialog Dialog = new OpenFileDialog())
            {
                Dialog.Title = Title;
                Dialog.Filter = Filter;
                Dialog.Multiselect = true;
                if (Dialog.ShowDialog(this) != DialogResult.OK)
                    return;

                Favourite Last = null;
                foreach (string Name in Dialog.FileNames)
                {
                    if (Contains(Items, Name) == true)
                        continue;

                    Last = new Favourite(Name, false);
                    Items.Add(Last);
                }
                SaveAll();
                Fill(Last);
            }
        }

        private static bool Contains(List<Favourite> Items, string Path)
        {
            foreach (Favourite Item in Items)
            {
                if (string.Equals(Item.Path, Path, StringComparison.OrdinalIgnoreCase) == true)
                    return (true);
            }
            return (false);
        }

        private void cmdAddCD_Click(object sender, EventArgs e)
        {
            AddFiles(CdItems, "Add CD / DVD / BD images", CdFilter, false);
        }

        private void cmdAddStorage_Click(object sender, EventArgs e)
        {
            AddFiles(StorageItems, "Add disk images", StorageFilter, true);
        }

        private void MoveSelected(int Step)
        {
            Favourite Chosen = SelectedFavourite();
            if (Chosen == null)
                return;

            List<Favourite> Items = SelectedList();
            int Index = Items.IndexOf(Chosen);
            int Target = Index + Step;
            if ((Target < 0) || (Target >= Items.Count))
                return;

            Items.RemoveAt(Index);
            Items.Insert(Target, Chosen);
            SaveAll();
            Fill(Chosen);
        }

        private void cmdMoveUp_Click(object sender, EventArgs e)
        {
            MoveSelected(-1);
        }

        private void cmdMoveDown_Click(object sender, EventArgs e)
        {
            MoveSelected(1);
        }

        private void cmdRemove_Click(object sender, EventArgs e)
        {
            Favourite Chosen = SelectedFavourite();
            if (Chosen == null)
                return;

            List<Favourite> Items = SelectedList();
            int Index = Items.IndexOf(Chosen);
            Items.RemoveAt(Index);
            SaveAll();

            // The next entry of the same group stays selected, so that several can be removed in a row.
            Fill((Index < Items.Count) ? Items[Index] : ((Items.Count > 0) ? Items[Items.Count - 1] : null));
        }

        private void ToggleReadOnly()
        {
            Favourite Chosen = SelectedFavourite();
            if ((Chosen == null) || (SelectedIsCd() == true))
                return;

            Chosen.ReadOnly = !Chosen.ReadOnly;
            SaveAll();
            Fill(Chosen);
        }

        private void cmdSetUnSetReadOnly_Click(object sender, EventArgs e)
        {
            ToggleReadOnly();
        }
    }
}
