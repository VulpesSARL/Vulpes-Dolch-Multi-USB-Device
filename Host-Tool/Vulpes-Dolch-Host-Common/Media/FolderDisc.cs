using System;
using System.Collections.Generic;
using System.IO;
using System.Text;
using Microsoft.Win32.SafeHandles;

namespace VulpesDolch.Host.Common.Media
{
    /// <summary>Where the sectors of a virtual disc come from (a folder, not an image file).</summary>
    internal interface ISectorProvider : IDisposable
    {
        /// <summary>Fills Destination (2048 bytes) with the sector. Throws <see cref="CdImageException"/> with <see cref="CdImageErrorKind.ReadError"/> when the data cannot be had.</summary>
        void ReadSector(Int64 Lba, Span<byte> Destination);
    }

    /// <summary>
    /// A folder presented as a data disc: ISO 9660 with Joliet long names. Only the INDEX lives in memory (volume descriptors, path tables and
    /// directories, a few bytes per file); the content of a file is never kept - every sector of a file is read from the physical file when the
    /// Client asks for it. A file that cannot be read any more (deleted, locked, shorter than at mount time, I/O error) makes that sector fail
    /// with a read error, which the Client reports to the OS as a medium error ("corrupted sector").
    /// The folder is scanned once, when the disc is built: files added or removed afterwards do not show up until it is mounted again.
    /// </summary>
    internal sealed partial class FolderDisc : ISectorProvider
    {
        private const int SectorSize = 2048;
        private const int HandleCacheSize = 16;

        private sealed class Node
        {
            internal string Name;
            internal string FullPath;
            internal bool IsDirectory;
            internal Int64 Length;
            internal DateTime ModifiedUtc;
            internal Node Parent;
            internal List<Node> Children = new List<Node>();

            // [0] = ISO 9660 tree, [1] = Joliet tree.
            internal string[] Id = new string[2];
            internal List<Node>[] Sorted = new List<Node>[2];
            internal Int64[] DirLba = new Int64[2];
            internal Int64[] DirSize = new Int64[2];
            internal int[] DirNumber = new int[2];

            internal Int64 FileLba;
            internal int FileIndex = -1;

            // UDF layout: block of the file entry, of the data (directory data or file content), its length, and the unique id.
            internal Int64 FeBlock;
            internal Int64 DataBlock;
            internal Int64 DataBytes;
            internal Int64 UniqueId;
        }

        private readonly object ReadLock = new object();
        private byte[] Meta;
        private byte[] Trailer = null;
        private Int64 MetaSectors;
        private Int64[] FileStarts;
        private Int64[] FileSectorCounts;
        private Int64[] FileLengths;
        private string[] FilePaths;
        private Int64 LargestFile = 0;
        private readonly Dictionary<int, SafeFileHandle> Handles = new Dictionary<int, SafeFileHandle>();
        private readonly List<int> HandleOrder = new List<int>();
        private bool Disposed = false;

        private FolderDisc(string FolderPath, FolderDiscOptions Options)
        {
            Node Root = new Node();
            Root.Name = "";
            Root.FullPath = FolderPath;
            Root.IsDirectory = true;
            Root.ModifiedUtc = Directory.GetLastWriteTimeUtc(FolderPath);

            Scan(Root);

            IsUdf = (Options.ForceUdf == true) || (LargestFile > Options.MaxIsoFileBytes);
            if (IsUdf == true)
                BuildUdf(Root, FolderPath, Options);
            else
                BuildIso(Root, FolderPath);

            ContentId = ComputeContentId(FolderPath, Root);
        }

        // ISO 9660 + Joliet: the index of the disc is built here, the files follow it.
        private void BuildIso(Node Root, string FolderPath)
        {
            AssignIds(Root, 0);
            AssignIds(Root, 1);

            List<Node>[] Bfs = new List<Node>[2];
            for (int Tree = 0; Tree < 2; Tree++)
                Bfs[Tree] = OrderDirectories(Root, Tree);

            // Sizes of the path tables and the directories of both trees.
            Int64[] PathTableSize = new Int64[2];
            for (int Tree = 0; Tree < 2; Tree++)
            {
                foreach (Node Folder in Bfs[Tree])
                {
                    int NameBytes = (Folder.Parent == null) ? 1 : NameLength(Folder, Tree);
                    PathTableSize[Tree] += 8 + NameBytes + (NameBytes & 1);
                    Folder.DirSize[Tree] = DirectorySize(Folder, Tree);
                }
            }

            // Layout: 16 PVD, 17 SVD, 18 terminator, then L and M path tables of each tree, then the directories of each tree, then the files.
            Int64 Next = 19;
            Int64[] LTable = new Int64[2];
            Int64[] MTable = new Int64[2];
            for (int Tree = 0; Tree < 2; Tree++)
            {
                Int64 TableSectors = (PathTableSize[Tree] + SectorSize - 1) / SectorSize;
                LTable[Tree] = Next;
                Next += TableSectors;
                MTable[Tree] = Next;
                Next += TableSectors;
            }
            for (int Tree = 0; Tree < 2; Tree++)
            {
                foreach (Node Folder in Bfs[Tree])
                {
                    Folder.DirLba[Tree] = Next;
                    Next += Folder.DirSize[Tree] / SectorSize;
                }
            }
            MetaSectors = Next;

            List<Node> Files = new List<Node>();
            foreach (Node Folder in Bfs[0])
            {
                foreach (Node Child in Folder.Sorted[0])
                {
                    if (Child.IsDirectory == true)
                        continue;
                    Child.FileLba = Next;
                    if (Child.Length > 0)
                    {
                        Child.FileIndex = Files.Count;
                        Files.Add(Child);
                        Next += (Child.Length + SectorSize - 1) / SectorSize;
                    }
                }
            }

            TotalSectors = Next;
            FileStarts = new Int64[Files.Count];
            FileSectorCounts = new Int64[Files.Count];
            FileLengths = new Int64[Files.Count];
            FilePaths = new string[Files.Count];
            for (int Index = 0; Index < Files.Count; Index++)
            {
                FileStarts[Index] = Files[Index].FileLba;
                FileSectorCounts[Index] = (Files[Index].Length + SectorSize - 1) / SectorSize;
                FileLengths[Index] = Files[Index].Length;
                FilePaths[Index] = Files[Index].FullPath;
            }

            Meta = new byte[MetaSectors * SectorSize];
            WriteDescriptors(Root, FolderPath, PathTableSize, LTable, MTable);
            for (int Tree = 0; Tree < 2; Tree++)
            {
                WritePathTable(Bfs[Tree], Tree, LTable[Tree], false);
                WritePathTable(Bfs[Tree], Tree, MTable[Tree], true);
                foreach (Node Folder in Bfs[Tree])
                    WriteDirectory(Folder, Tree);
            }

        }

        /// <summary>Number of sectors of the whole disc.</summary>
        internal Int64 TotalSectors { get; private set; }

        /// <summary>A number that is the same for the same folder with the same files (names, sizes, times), so the Client's disc can be recognised again after a restart.</summary>
        internal Int64 ContentId { get; }

        /// <summary>True when the disc is laid out as UDF (a file of 4 GiB or more), false for ISO 9660 + Joliet.</summary>
        internal bool IsUdf { get; private set; }

        internal int FileCount { get; private set; }

        internal int DirectoryCount { get; private set; }

        /// <summary>Entries that could not be listed (no access) and were left out of the index.</summary>
        internal int SkippedCount { get; private set; }

        /// <summary>Scans the folder and builds the index. Throws <see cref="CdImageException"/> when the folder cannot be used.</summary>
        internal static FolderDisc Build(string FolderPath, FolderDiscOptions Options)
        {
            try
            {
                return (new FolderDisc(FolderPath, Options));
            }
            catch (CdImageException)
            {
                throw;
            }
            catch (IOException Problem)
            {
                throw new CdImageException("Cannot read the folder '" + FolderPath + "': " + Problem.Message);
            }
            catch (UnauthorizedAccessException Problem)
            {
                throw new CdImageException("Cannot read the folder '" + FolderPath + "': " + Problem.Message);
            }
        }

        public void ReadSector(Int64 Lba, Span<byte> Destination)
        {
            if (Lba < MetaSectors)
            {
                Meta.AsSpan((int)(Lba * SectorSize), SectorSize).CopyTo(Destination);
                return;
            }

            if ((Trailer != null) && (Lba == (TotalSectors - 1)))
            {
                Trailer.AsSpan(0, SectorSize).CopyTo(Destination);
                return;
            }

            int Low = 0;
            int High = FileStarts.Length - 1;
            int Found = -1;
            while (Low <= High)
            {
                int Middle = Low + ((High - Low) / 2);
                if (Lba < FileStarts[Middle])
                    High = Middle - 1;
                else if (Lba >= (FileStarts[Middle] + FileSectorCounts[Middle]))
                    Low = Middle + 1;
                else
                {
                    Found = Middle;
                    break;
                }
            }

            if (Found < 0)
            {
                Destination.Slice(0, SectorSize).Clear();
                return;
            }

            Int64 Offset = (Lba - FileStarts[Found]) * SectorSize;
            int Wanted = (int)Math.Min((Int64)SectorSize, FileLengths[Found] - Offset);

            lock (ReadLock)
            {
                if (Disposed == true)
                    throw new ObjectDisposedException(nameof(FolderDisc));

                SafeFileHandle Handle = GetHandle(Found);
                try
                {
                    int Done = 0;
                    while (Done < Wanted)
                    {
                        int Count = RandomAccess.Read(Handle, Destination.Slice(Done, Wanted - Done), Offset + Done);
                        if (Count == 0)
                            throw new CdImageException("The file '" + FilePaths[Found] + "' is shorter than when the folder was mounted (LBA " + Lba + ").", CdImageErrorKind.ReadError);
                        Done += Count;
                    }
                }
                catch (CdImageException)
                {
                    CloseHandle(Found);
                    throw;
                }
                catch (Exception Problem) when ((Problem is IOException) || (Problem is UnauthorizedAccessException))
                {
                    CloseHandle(Found);
                    throw new CdImageException("Cannot read the file '" + FilePaths[Found] + "' (LBA " + Lba + "): " + Problem.Message, CdImageErrorKind.ReadError);
                }
            }

            if (Wanted < SectorSize)
                Destination.Slice(Wanted, SectorSize - Wanted).Clear();
        }

        public void Dispose()
        {
            lock (ReadLock)
            {
                if (Disposed == true)
                    return;

                Disposed = true;
                foreach (SafeFileHandle Handle in Handles.Values)
                    Handle.Dispose();
                Handles.Clear();
                HandleOrder.Clear();
            }
        }

        // ---------------------------------------------------------------- file access

        private SafeFileHandle GetHandle(int Index)
        {
            SafeFileHandle Handle;
            if (Handles.TryGetValue(Index, out Handle) == true)
            {
                HandleOrder.Remove(Index);
                HandleOrder.Add(Index);
                return (Handle);
            }

            try
            {
                Handle = File.OpenHandle(FilePaths[Index], FileMode.Open, FileAccess.Read, FileShare.ReadWrite | FileShare.Delete, FileOptions.RandomAccess);
                if (RandomAccess.GetLength(Handle) < FileLengths[Index])
                {
                    Handle.Dispose();
                    throw new CdImageException("The file '" + FilePaths[Index] + "' is shorter than when the folder was mounted.", CdImageErrorKind.ReadError);
                }
            }
            catch (Exception Problem) when ((Problem is IOException) || (Problem is UnauthorizedAccessException))
            {
                throw new CdImageException("Cannot open the file '" + FilePaths[Index] + "': " + Problem.Message, CdImageErrorKind.ReadError);
            }

            if (Handles.Count >= HandleCacheSize)
            {
                int Oldest = HandleOrder[0];
                CloseHandle(Oldest);
            }

            Handles[Index] = Handle;
            HandleOrder.Add(Index);
            return (Handle);
        }

        private void CloseHandle(int Index)
        {
            SafeFileHandle Handle;
            if (Handles.TryGetValue(Index, out Handle) == true)
            {
                Handle.Dispose();
                Handles.Remove(Index);
            }
            HandleOrder.Remove(Index);
        }

        // ---------------------------------------------------------------- scan and names

        private void Scan(Node Folder)
        {
            DirectoryCount++;
            DirectoryInfo Info = new DirectoryInfo(Folder.FullPath);
            IEnumerable<FileSystemInfo> Entries;
            try
            {
                Entries = Info.EnumerateFileSystemInfos();
                foreach (FileSystemInfo Entry in Entries)
                {
                    bool IsDirectory = (Entry.Attributes & FileAttributes.Directory) != 0;
                    if (IsDirectory == true)
                    {
                        // Junctions and symbolic links to folders are not followed (they can loop).
                        if ((Entry.Attributes & FileAttributes.ReparsePoint) != 0)
                        {
                            SkippedCount++;
                            continue;
                        }

                        Node Sub = new Node();
                        Sub.Name = Entry.Name;
                        Sub.FullPath = Entry.FullName;
                        Sub.IsDirectory = true;
                        Sub.ModifiedUtc = Entry.LastWriteTimeUtc;
                        Sub.Parent = Folder;
                        Folder.Children.Add(Sub);
                        Scan(Sub);
                    }
                    else
                    {
                        Int64 Length = ((FileInfo)Entry).Length;
                        if (Length > LargestFile)
                            LargestFile = Length;

                        Node FileNode = new Node();
                        FileNode.Name = Entry.Name;
                        FileNode.FullPath = Entry.FullName;
                        FileNode.IsDirectory = false;
                        FileNode.Length = Length;
                        FileNode.ModifiedUtc = Entry.LastWriteTimeUtc;
                        FileNode.Parent = Folder;
                        Folder.Children.Add(FileNode);
                        FileCount++;
                    }
                }
            }
            catch (Exception Problem) when ((Problem is IOException) || (Problem is UnauthorizedAccessException))
            {
                // The root must be readable; a folder further down that is not is left out.
                if (Folder.Parent == null)
                    throw new CdImageException("Cannot read the folder '" + Folder.FullPath + "': " + Problem.Message);
                SkippedCount++;
            }
        }

        private static void AssignIds(Node Folder, int Tree)
        {
            HashSet<string> Used = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
            List<Node> Sorted = new List<Node>(Folder.Children);

            foreach (Node Child in Sorted)
            {
                string Id = (Tree == 0) ? IsoId(Child, Used) : JolietId(Child, Used);
                Child.Id[Tree] = Id;
                Used.Add(Id);
            }

            Sorted.Sort(delegate (Node A, Node B) { return (string.CompareOrdinal(A.Id[Tree], B.Id[Tree])); });
            Folder.Sorted[Tree] = Sorted;

            foreach (Node Child in Sorted)
            {
                if (Child.IsDirectory == true)
                    AssignIds(Child, Tree);
            }
        }

        private static string IsoId(Node Entry, HashSet<string> Used)
        {
            string Base;
            string Extension = "";

            if (Entry.IsDirectory == true)
            {
                Base = IsoClean(Entry.Name);
            }
            else
            {
                int Dot = Entry.Name.LastIndexOf('.');
                if (Dot > 0)
                {
                    Base = IsoClean(Entry.Name.Substring(0, Dot));
                    Extension = IsoClean(Entry.Name.Substring(Dot + 1));
                    if (Extension.Length > 8)
                        Extension = Extension.Substring(0, 8);
                }
                else
                {
                    Base = IsoClean(Entry.Name);
                }
            }

            int MaxBase = (Entry.IsDirectory == true) ? 31 : (30 - 1 - Extension.Length);
            if (Base.Length == 0)
                Base = "_";

            for (int Attempt = 0; ; Attempt++)
            {
                string Suffix = (Attempt == 0) ? "" : ("~" + Attempt);
                string Trimmed = (Base.Length + Suffix.Length > MaxBase) ? Base.Substring(0, Math.Max(1, MaxBase - Suffix.Length)) : Base;
                string Id = (Entry.IsDirectory == true) ? (Trimmed + Suffix) : (Trimmed + Suffix + "." + Extension + ";1");
                if (Used.Contains(Id) == false)
                    return (Id);
            }
        }

        private static string IsoClean(string Text)
        {
            StringBuilder Result = new StringBuilder();
            foreach (char Character in Text.ToUpperInvariant())
            {
                bool Allowed = ((Character >= 'A') && (Character <= 'Z')) || ((Character >= '0') && (Character <= '9')) || (Character == '_');
                Result.Append((Allowed == true) ? Character : '_');
            }
            return (Result.ToString());
        }

        private static string JolietId(Node Entry, HashSet<string> Used)
        {
            StringBuilder Clean = new StringBuilder();
            foreach (char Character in Entry.Name)
            {
                bool Forbidden = (Character < ' ') || (Character == '*') || (Character == '/') || (Character == ':') || (Character == ';') || (Character == '?') || (Character == '\\');
                Clean.Append((Forbidden == true) ? '_' : Character);
            }

            string Name = Clean.ToString();
            string Extension = "";
            string Stem = Name;
            if (Entry.IsDirectory == false)
            {
                string Found = Path.GetExtension(Name);
                if ((Found.Length > 0) && (Found.Length <= 12) && (Found.Length < Name.Length))
                {
                    Extension = Found;
                    Stem = Name.Substring(0, Name.Length - Found.Length);
                }
            }

            for (int Attempt = 0; ; Attempt++)
            {
                string Suffix = (Attempt == 0) ? "" : ("~" + Attempt);
                int MaxStem = 64 - Extension.Length - Suffix.Length;
                string Trimmed = (Stem.Length > MaxStem) ? Stem.Substring(0, Math.Max(1, MaxStem)) : Stem;
                string Id = Trimmed + Suffix + Extension;
                if (Used.Contains(Id) == false)
                    return (Id);
            }
        }

        // Directories of a tree in path table order: level by level, inside a level by parent and then by name.
        private static List<Node> OrderDirectories(Node Root, int Tree)
        {
            List<Node> Order = new List<Node>();
            Order.Add(Root);
            for (int Index = 0; Index < Order.Count; Index++)
            {
                Order[Index].DirNumber[Tree] = Index + 1;
                foreach (Node Child in Order[Index].Sorted[Tree])
                {
                    if (Child.IsDirectory == true)
                        Order.Add(Child);
                }
            }
            return (Order);
        }

        private static int NameLength(Node Entry, int Tree)
        {
            return ((Tree == 0) ? Entry.Id[0].Length : (Entry.Id[1].Length * 2));
        }

        private static int RecordLength(int NameBytes)
        {
            int Length = 33 + NameBytes;
            return (Length + (Length & 1));
        }

        private static Int64 DirectorySize(Node Folder, int Tree)
        {
            Int64 Used = 2 * RecordLength(1);
            Int64 Sectors = 1;

            foreach (Node Child in Folder.Sorted[Tree])
            {
                int Length = RecordLength(NameLength(Child, Tree));
                if (((Used % SectorSize) + Length) > SectorSize)
                {
                    Used = Sectors * SectorSize;
                    Sectors++;
                }
                Used += Length;
            }

            return (((Used + SectorSize - 1) / SectorSize) * SectorSize);
        }

        // ---------------------------------------------------------------- writing the index

        private void WriteDescriptors(Node Root, string FolderPath, Int64[] PathTableSize, Int64[] LTable, Int64[] MTable)
        {
            string Label = Path.GetFileName(FolderPath.TrimEnd('\\', '/'));
            if (string.IsNullOrEmpty(Label) == true)
                Label = "DISC";

            DateTime Now = DateTime.UtcNow;
            for (int Tree = 0; Tree < 2; Tree++)
            {
                int Base = (16 + Tree) * SectorSize;
                Meta[Base] = (byte)((Tree == 0) ? 1 : 2);
                Encoding.ASCII.GetBytes("CD001").CopyTo(Meta, Base + 1);
                Meta[Base + 6] = 1;

                if (Tree == 0)
                {
                    PutText(Base + 8, 32, "");
                    PutText(Base + 40, 32, IsoClean(Label));
                }
                else
                {
                    PutUcs(Base + 8, 32, "");
                    PutUcs(Base + 40, 32, (Label.Length > 16) ? Label.Substring(0, 16) : Label);
                    Meta[Base + 88] = 0x25;
                    Meta[Base + 89] = 0x2F;
                    Meta[Base + 90] = 0x45;
                }

                PutBoth32(Base + 80, (uint)TotalSectors);
                PutBoth16(Base + 120, 1);
                PutBoth16(Base + 124, 1);
                PutBoth16(Base + 128, SectorSize);
                PutBoth32(Base + 132, (uint)PathTableSize[Tree]);
                BitConverter.GetBytes((uint)LTable[Tree]).CopyTo(Meta, Base + 140);
                byte[] Big = BitConverter.GetBytes((uint)MTable[Tree]);
                Array.Reverse(Big);
                Big.CopyTo(Meta, Base + 148);

                WriteRecord(Base + 156, Root, Tree, true, true);

                // Volume set, publisher, preparer, application, copyright, abstract and bibliographic identifiers: all blank.
                int[] BlankFields = new int[] { 190, 318, 446, 574 };
                foreach (int Field in BlankFields)
                {
                    if (Tree == 0) PutText(Base + Field, 128, ""); else PutUcs(Base + Field, 128, "");
                }
                int[] BlankFiles = new int[] { 702, 739, 776 };
                foreach (int Field in BlankFiles)
                {
                    if (Tree == 0) PutText(Base + Field, 37, ""); else PutUcs(Base + Field, 36, "");
                }
                PutDate17(Base + 813, Now);
                PutDate17(Base + 830, Now);
                Encoding.ASCII.GetBytes("0000000000000000").CopyTo(Meta, Base + 847);
                PutDate17(Base + 864, Now);
                Meta[Base + 881] = 1;
            }

            int Terminator = 18 * SectorSize;
            Meta[Terminator] = 255;
            Encoding.ASCII.GetBytes("CD001").CopyTo(Meta, Terminator + 1);
            Meta[Terminator + 6] = 1;
        }

        private void WritePathTable(List<Node> Directories, int Tree, Int64 Lba, bool BigEndian)
        {
            int Position = (int)(Lba * SectorSize);
            foreach (Node Folder in Directories)
            {
                byte[] Name = (Folder.Parent == null) ? new byte[] { 0 } : IdBytes(Folder, Tree);
                Meta[Position] = (byte)Name.Length;
                Meta[Position + 1] = 0;

                byte[] Extent = BitConverter.GetBytes((uint)Folder.DirLba[Tree]);
                int ParentNumber = (Folder.Parent == null) ? 1 : Folder.Parent.DirNumber[Tree];
                byte[] Parent = BitConverter.GetBytes((ushort)ParentNumber);
                if (BigEndian == true)
                {
                    Array.Reverse(Extent);
                    Array.Reverse(Parent);
                }
                Extent.CopyTo(Meta, Position + 2);
                Parent.CopyTo(Meta, Position + 6);
                Name.CopyTo(Meta, Position + 8);
                Position += 8 + Name.Length + (Name.Length & 1);
            }
        }

        private void WriteDirectory(Node Folder, int Tree)
        {
            int Start = (int)(Folder.DirLba[Tree] * SectorSize);
            int Position = Start;

            // "." and ".."
            WriteRecord(Position, Folder, Tree, true, true);
            Position += RecordLength(1);
            Node Up = (Folder.Parent == null) ? Folder : Folder.Parent;
            WriteRecord(Position, Up, Tree, true, false);
            Position += RecordLength(1);

            foreach (Node Child in Folder.Sorted[Tree])
            {
                int Length = RecordLength(NameLength(Child, Tree));
                if ((((Position - Start) % SectorSize) + Length) > SectorSize)
                    Position = Start + ((((Position - Start) / SectorSize) + 1) * SectorSize);

                WriteRecord(Position, Child, Tree, false, false);
                Position += Length;
            }
        }

        // One directory record. For "." and ".." (Special) the name is a single 0 / 1 byte.
        private void WriteRecord(int Position, Node Entry, int Tree, bool Special, bool IsSelf)
        {
            byte[] Name;
            if (Special == true)
                Name = new byte[] { (byte)((IsSelf == true) ? 0 : 1) };
            else
                Name = IdBytes(Entry, Tree);

            // The root entry in the PVD has the name 0, like ".".
            Int64 Lba = (Entry.IsDirectory == true) ? Entry.DirLba[Tree] : Entry.FileLba;
            Int64 Size = (Entry.IsDirectory == true) ? Entry.DirSize[Tree] : Entry.Length;

            Meta[Position] = (byte)RecordLength(Name.Length);
            PutBoth32(Position + 2, (uint)Lba);
            PutBoth32(Position + 10, (uint)Size);
            PutDate7(Position + 18, Entry.ModifiedUtc);
            Meta[Position + 25] = (byte)((Entry.IsDirectory == true) ? 2 : 0);
            PutBoth16(Position + 28, 1);
            Meta[Position + 32] = (byte)Name.Length;
            Name.CopyTo(Meta, Position + 33);
        }

        private static byte[] IdBytes(Node Entry, int Tree)
        {
            return ((Tree == 0) ? Encoding.ASCII.GetBytes(Entry.Id[0]) : Encoding.BigEndianUnicode.GetBytes(Entry.Id[1]));
        }

        private void PutText(int Position, int Length, string Text)
        {
            for (int Index = 0; Index < Length; Index++)
                Meta[Position + Index] = (byte)((Index < Text.Length) ? Text[Index] : ' ');
        }

        private void PutUcs(int Position, int Length, string Text)
        {
            for (int Index = 0; Index < Length; Index += 2)
            {
                char Character = ((Index / 2) < Text.Length) ? Text[Index / 2] : ' ';
                Meta[Position + Index] = (byte)(Character >> 8);
                Meta[Position + Index + 1] = (byte)(Character & 0xFF);
            }
        }

        private void PutBoth32(int Position, uint Value)
        {
            BitConverter.GetBytes(Value).CopyTo(Meta, Position);
            byte[] Big = BitConverter.GetBytes(Value);
            Array.Reverse(Big);
            Big.CopyTo(Meta, Position + 4);
        }

        private void PutBoth16(int Position, ushort Value)
        {
            BitConverter.GetBytes(Value).CopyTo(Meta, Position);
            byte[] Big = BitConverter.GetBytes(Value);
            Array.Reverse(Big);
            Big.CopyTo(Meta, Position + 2);
        }

        private void PutDate17(int Position, DateTime Time)
        {
            string Text = Time.ToString("yyyyMMddHHmmss", System.Globalization.CultureInfo.InvariantCulture) + "00";
            Encoding.ASCII.GetBytes(Text).CopyTo(Meta, Position);
            Meta[Position + 16] = 0;
        }

        private void PutDate7(int Position, DateTime Time)
        {
            if ((Time.Year < 1900) || (Time.Year > 2155))
                Time = new DateTime(1980, 1, 1, 0, 0, 0, DateTimeKind.Utc);

            Meta[Position] = (byte)(Time.Year - 1900);
            Meta[Position + 1] = (byte)Time.Month;
            Meta[Position + 2] = (byte)Time.Day;
            Meta[Position + 3] = (byte)Time.Hour;
            Meta[Position + 4] = (byte)Time.Minute;
            Meta[Position + 5] = (byte)Time.Second;
            Meta[Position + 6] = 0;
        }

        // ---------------------------------------------------------------- identity

        private static Int64 ComputeContentId(string FolderPath, Node Root)
        {
            UInt64 Hash = 14695981039346656037UL;
            Hash = Mix(Hash, Encoding.Unicode.GetBytes(Path.GetFullPath(FolderPath).ToLowerInvariant()));
            Hash = MixNode(Hash, Root, "");

            if (Hash == 0)
                Hash = 1;
            return (unchecked((Int64)Hash));
        }

        private static UInt64 MixNode(UInt64 Hash, Node Folder, string Prefix)
        {
            List<Node> Ordered = new List<Node>(Folder.Children);
            Ordered.Sort(delegate (Node A, Node B) { return (string.CompareOrdinal(A.Name, B.Name)); });
            foreach (Node Child in Ordered)
            {
                string Relative = Prefix + "\\" + Child.Name;
                Hash = Mix(Hash, Encoding.Unicode.GetBytes(Relative));
                Hash = Mix(Hash, BitConverter.GetBytes(Child.Length));
                Hash = Mix(Hash, BitConverter.GetBytes(Child.ModifiedUtc.Ticks));
                if (Child.IsDirectory == true)
                    Hash = MixNode(Hash, Child, Relative);
            }
            return (Hash);
        }

        private static UInt64 Mix(UInt64 Hash, byte[] Data)
        {
            foreach (byte Value in Data)
            {
                Hash ^= Value;
                Hash *= 1099511628211UL;
            }
            return (Hash);
        }
    }

    /// <summary>How a folder is turned into a disc. The defaults are what the application uses; the other values exist for tests.</summary>
    public sealed class FolderDiscOptions
    {
        /// <summary>Lay the disc out as UDF even if ISO 9660 + Joliet would do.</summary>
        public bool ForceUdf { get; set; } = false;

        /// <summary>The largest file ISO 9660 can hold (4 GiB - 1); a bigger file makes the disc UDF.</summary>
        public Int64 MaxIsoFileBytes { get; set; } = 0xFFFFFFFFL;

        /// <summary>The longest run of sectors one UDF allocation descriptor covers (just under 1 GiB; rounded down to whole sectors).</summary>
        public Int64 MaxUdfExtentBytes { get; set; } = 0x3FFFF800L;
    }}
