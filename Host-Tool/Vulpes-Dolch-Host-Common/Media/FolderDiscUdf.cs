using System;
using System.Collections.Generic;
using System.IO;
using System.Text;

namespace VulpesDolch.Host.Common.Media
{
    /// <summary>
    /// The UDF 1.02 layout of a mounted folder, used instead of ISO 9660 + Joliet when a file is too big for ISO 9660 (4 GiB or more).
    /// Same principle as the ISO layout: the descriptors, the file entries and the directories are the index in memory, the content of every
    /// file stays on the disk and is read when it is accessed. One physical partition, no sparing, no virtual allocation: a plain read-only disc.
    /// Disc layout: sectors 0-15 empty, 16-18 volume recognition (BEA01, NSR02, TEA01), 32 main and 48 reserve volume descriptor sequence,
    /// 64 integrity descriptor, 256 anchor, the partition from 257 (file set, file entries, directories, then the files), anchor again in the last sector.
    /// </summary>
    internal sealed partial class FolderDisc
    {
        private const int PartitionStart = 257;
        private const int UdfBlock = 2048;
        private const int FileEntryHeader = 176;
        private const int ShortAdSize = 8;

        private sealed class UdfExtent
        {
            internal Int64 Block;
            internal Int64 Length;
        }

        private sealed class UdfState
        {
            internal List<Node> Directories = new List<Node>();
            internal List<Node> Files = new List<Node>();
            internal List<Node> EntryOrder = new List<Node>();
            internal Dictionary<Node, byte[][]> Identifiers = new Dictionary<Node, byte[][]>();
            internal Int64 UniqueIdCounter = 16;
            internal DateTime Now = DateTime.UtcNow;
            internal Int64 MaxExtentBytes;
            internal string Label;
        }

        private static readonly ushort[] CrcTable = MakeCrcTable();

        private static ushort[] MakeCrcTable()
        {
            ushort[] Table = new ushort[256];
            for (int Index = 0; Index < 256; Index++)
            {
                int Value = Index << 8;
                for (int Bit = 0; Bit < 8; Bit++)
                    Value = ((Value & 0x8000) != 0) ? ((Value << 1) ^ 0x1021) : (Value << 1);
                Table[Index] = (ushort)(Value & 0xFFFF);
            }
            return (Table);
        }

        internal static ushort Crc16(byte[] Data, int Start, int Length)
        {
            int Crc = 0;
            for (int Index = 0; Index < Length; Index++)
                Crc = ((Crc << 8) ^ CrcTable[((Crc >> 8) ^ Data[Start + Index]) & 0xFF]) & 0xFFFF;
            return ((ushort)Crc);
        }

        // ---------------------------------------------------------------- layout

        private void BuildUdf(Node Root, string FolderPath, FolderDiscOptions Options)
        {
            UdfState State = new UdfState();
            State.MaxExtentBytes = (Options.MaxUdfExtentBytes / UdfBlock) * UdfBlock;
            if (State.MaxExtentBytes < UdfBlock)
                State.MaxExtentBytes = UdfBlock;

            string Label = Path.GetFileName(FolderPath.TrimEnd('\\', '/'));
            State.Label = (string.IsNullOrEmpty(Label) == true) ? "DISC" : Label;

            Collect(Root, State);

            // Blocks: 0 file set descriptor, 1 terminating descriptor, then one file entry per directory and file, then the directories, then the files.
            Int64 Next = 2;
            foreach (Node Entry in State.EntryOrder)
                Entry.FeBlock = Next++;

            foreach (Node Directory in State.Directories)
            {
                Directory.DataBytes = DirectoryBytes(Directory, State);
                Directory.DataBlock = Next;
                Next += (Directory.DataBytes + UdfBlock - 1) / UdfBlock;
            }

            Int64 FirstFileBlock = Next;
            foreach (Node File in State.Files)
            {
                File.DataBytes = File.Length;
                File.DataBlock = Next;
                if (File.Length > 0)
                    Next += (File.Length + UdfBlock - 1) / UdfBlock;

                Int64 Extents = (File.Length + State.MaxExtentBytes - 1) / State.MaxExtentBytes;
                if (Extents > ((UdfBlock - FileEntryHeader) / ShortAdSize))
                    throw new CdImageException("The file '" + File.FullPath + "' is too big for one UDF file entry (" + File.Length + " bytes).");
            }

            Int64 PartitionBlocks = Next;
            TotalSectors = PartitionStart + PartitionBlocks + 1;
            MetaSectors = PartitionStart + FirstFileBlock;

            List<Node> FilesWithData = new List<Node>();
            foreach (Node File in State.Files)
            {
                File.FileLba = PartitionStart + File.DataBlock;
                if (File.Length > 0)
                    FilesWithData.Add(File);
            }

            FileStarts = new Int64[FilesWithData.Count];
            FileSectorCounts = new Int64[FilesWithData.Count];
            FileLengths = new Int64[FilesWithData.Count];
            FilePaths = new string[FilesWithData.Count];
            for (int Index = 0; Index < FilesWithData.Count; Index++)
            {
                FileStarts[Index] = FilesWithData[Index].FileLba;
                FileSectorCounts[Index] = (FilesWithData[Index].Length + UdfBlock - 1) / UdfBlock;
                FileLengths[Index] = FilesWithData[Index].Length;
                FilePaths[Index] = FilesWithData[Index].FullPath;
            }

            Meta = new byte[MetaSectors * UdfBlock];

            WriteVolumeRecognition();
            WriteDescriptorSequence(32, PartitionBlocks, State);
            WriteDescriptorSequence(48, PartitionBlocks, State);
            WriteIntegrity(64, PartitionBlocks, State);
            WriteAnchor(Meta, 256 * UdfBlock, 256);

            Trailer = new byte[UdfBlock];
            WriteAnchor(Trailer, 0, TotalSectors - 1);

            WriteFileSet(Root, State);
            foreach (Node Entry in State.EntryOrder)
                WriteFileEntry(Entry, State);
            foreach (Node Directory in State.Directories)
                WriteDirectoryData(Directory, State);
        }

        private void Collect(Node Folder, UdfState State)
        {
            Folder.Children.Sort(delegate (Node A, Node B) { return (string.CompareOrdinal(A.Name, B.Name)); });

            // Identifiers of the children, unique after cutting.
            byte[][] Ids = new byte[Folder.Children.Count][];
            HashSet<string> Used = new HashSet<string>(StringComparer.Ordinal);
            for (int Index = 0; Index < Folder.Children.Count; Index++)
            {
                string Name = Folder.Children[Index].Name;
                byte[] Id = UdfIdentifier(Name);
                for (int Attempt = 1; Used.Contains(Convert.ToBase64String(Id)) == true; Attempt++)
                    Id = UdfIdentifier(CutName(Name, Attempt));
                Used.Add(Convert.ToBase64String(Id));
                Ids[Index] = Id;
            }
            State.Identifiers[Folder] = Ids;

            if (Folder.Parent == null)
            {
                State.EntryOrder.Add(Folder);
                State.Directories.Add(Folder);
            }

            foreach (Node Child in Folder.Children)
            {
                State.EntryOrder.Add(Child);
                Child.UniqueId = State.UniqueIdCounter++;
                if (Child.IsDirectory == true)
                {
                    State.Directories.Add(Child);
                    Collect(Child, State);
                }
                else
                {
                    State.Files.Add(Child);
                }
            }
        }

        private static string CutName(string Name, int Attempt)
        {
            string Extension = Path.GetExtension(Name);
            if (Extension.Length > 12)
                Extension = "";
            string Stem = Name.Substring(0, Name.Length - Extension.Length);
            string Suffix = "~" + Attempt;
            int Keep = Math.Max(1, Math.Min(Stem.Length, 100 - Suffix.Length - Extension.Length));
            return (Stem.Substring(0, Keep) + Suffix + Extension);
        }

        // The name as an OSTA compressed unicode string: a compression id (8 or 16) and the characters; at most 255 bytes.
        private static byte[] UdfIdentifier(string Name)
        {
            bool Narrow = true;
            foreach (char Character in Name)
            {
                if (Character > 0xFF)
                {
                    Narrow = false;
                    break;
                }
            }

            if (Narrow == true)
            {
                int Count = Math.Min(Name.Length, 254);
                byte[] Result = new byte[1 + Count];
                Result[0] = 8;
                for (int Index = 0; Index < Count; Index++)
                    Result[1 + Index] = (byte)Name[Index];
                return (Result);
            }
            else
            {
                int Count = Math.Min(Name.Length, 127);
                byte[] Result = new byte[1 + (Count * 2)];
                Result[0] = 16;
                for (int Index = 0; Index < Count; Index++)
                {
                    Result[1 + (Index * 2)] = (byte)(Name[Index] >> 8);
                    Result[2 + (Index * 2)] = (byte)(Name[Index] & 0xFF);
                }
                return (Result);
            }
        }

        private static int FidLength(int IdentifierBytes)
        {
            int Length = 38 + IdentifierBytes;
            return ((Length + 3) & ~3);
        }

        private static Int64 DirectoryBytes(Node Folder, UdfState State)
        {
            Int64 Bytes = FidLength(0);
            byte[][] Ids = State.Identifiers[Folder];
            foreach (byte[] Id in Ids)
                Bytes += FidLength(Id.Length);
            return (Bytes);
        }

        // ---------------------------------------------------------------- descriptors

        private void WriteVolumeRecognition()
        {
            PutAscii(16 * UdfBlock + 1, "BEA01");
            Meta[16 * UdfBlock + 6] = 1;
            PutAscii(17 * UdfBlock + 1, "NSR02");
            Meta[17 * UdfBlock + 6] = 1;
            PutAscii(18 * UdfBlock + 1, "TEA01");
            Meta[18 * UdfBlock + 6] = 1;
        }

        private void WriteDescriptorSequence(int FirstSector, Int64 PartitionBlocks, UdfState State)
        {
            // Primary volume descriptor.
            int Position = FirstSector * UdfBlock;
            PutU32(Position + 16, 1);
            PutU32(Position + 20, 0);
            PutDString(Position + 24, 32, State.Label);
            PutU16(Position + 56, 1);
            PutU16(Position + 58, 1);
            PutU16(Position + 60, 2);
            PutU16(Position + 62, 3);
            PutU32(Position + 64, 1);
            PutU32(Position + 68, 1);
            PutDString(Position + 72, 128, Guid.NewGuid().ToString("N").Substring(0, 16).ToUpperInvariant() + State.Label);
            PutCharSpec(Position + 200);
            PutCharSpec(Position + 264);
            PutRegId(Position + 344, "*Vulpes Dolch", 0, 0);
            PutTimestamp(Position + 376, State.Now);
            PutRegId(Position + 388, "*Vulpes Dolch", 0, 0);
            FinishTag(Position, 1, 512, (uint)FirstSector);

            // Implementation use volume descriptor.
            Position = (FirstSector + 1) * UdfBlock;
            PutU32(Position + 16, 2);
            PutRegId(Position + 20, "*UDF LV Info", 0x0102, 0);
            PutCharSpec(Position + 52);
            PutDString(Position + 52 + 64, 128, State.Label);
            PutRegId(Position + 52 + 300, "*Vulpes Dolch", 0, 0);
            FinishTag(Position, 4, 512, (uint)(FirstSector + 1));

            // Partition descriptor.
            Position = (FirstSector + 2) * UdfBlock;
            PutU32(Position + 16, 3);
            PutU16(Position + 20, 1);
            PutU16(Position + 22, 0);
            PutRegId(Position + 24, "+NSR02", 0, 0);
            PutU32(Position + 184, 1);
            PutU32(Position + 188, PartitionStart);
            PutU32(Position + 192, (uint)PartitionBlocks);
            PutRegId(Position + 196, "*Vulpes Dolch", 0, 0);
            FinishTag(Position, 5, 512, (uint)(FirstSector + 2));

            // Logical volume descriptor with its single type 1 partition map.
            Position = (FirstSector + 3) * UdfBlock;
            PutU32(Position + 16, 4);
            PutCharSpec(Position + 20);
            PutDString(Position + 84, 128, State.Label);
            PutU32(Position + 212, UdfBlock);
            PutRegId(Position + 216, "*OSTA UDF Compliant", 0x0102, 0);
            PutU32(Position + 248, UdfBlock);
            PutU32(Position + 252, 0);
            PutU16(Position + 256, 0);
            PutU32(Position + 264, 6);
            PutU32(Position + 268, 1);
            PutRegId(Position + 272, "*Vulpes Dolch", 0, 0);
            PutU32(Position + 432, 2 * UdfBlock);
            PutU32(Position + 436, 64);
            Meta[Position + 440] = 1;
            Meta[Position + 441] = 6;
            PutU16(Position + 442, 1);
            PutU16(Position + 444, 0);
            FinishTag(Position, 6, 446, (uint)(FirstSector + 3));

            // Unallocated space descriptor (empty) and terminating descriptor.
            Position = (FirstSector + 4) * UdfBlock;
            PutU32(Position + 16, 5);
            PutU32(Position + 20, 0);
            FinishTag(Position, 7, 24, (uint)(FirstSector + 4));

            Position = (FirstSector + 5) * UdfBlock;
            FinishTag(Position, 8, 512, (uint)(FirstSector + 5));
        }

        private void WriteIntegrity(int Sector, Int64 PartitionBlocks, UdfState State)
        {
            int Position = Sector * UdfBlock;
            PutTimestamp(Position + 16, State.Now);
            PutU32(Position + 28, 1);
            PutU64(Position + 40, (UInt64)State.UniqueIdCounter);
            PutU32(Position + 72, 1);
            PutU32(Position + 76, 46);
            PutU32(Position + 80, 0);
            PutU32(Position + 84, (uint)PartitionBlocks);
            PutRegId(Position + 88, "*Vulpes Dolch", 0, 0);
            PutU32(Position + 88 + 32, (uint)State.Files.Count);
            PutU32(Position + 88 + 36, (uint)State.Directories.Count);
            PutU16(Position + 88 + 40, 0x0102);
            PutU16(Position + 88 + 42, 0x0102);
            PutU16(Position + 88 + 44, 0x0102);
            FinishTag(Position, 9, 134, (uint)Sector);

            FinishTag((Sector + 1) * UdfBlock, 8, 512, (uint)(Sector + 1));
        }

        private static void WriteAnchor(byte[] Buffer, int Position, Int64 Location)
        {
            PutU32(Buffer, Position + 16, 16 * UdfBlock);
            PutU32(Buffer, Position + 20, 32);
            PutU32(Buffer, Position + 24, 16 * UdfBlock);
            PutU32(Buffer, Position + 28, 48);
            FinishTag(Buffer, Position, 2, 512, (uint)Location);
        }

        private void WriteFileSet(Node Root, UdfState State)
        {
            int Position = PartitionStart * UdfBlock;
            PutTimestamp(Position + 16, State.Now);
            PutU16(Position + 28, 3);
            PutU16(Position + 30, 3);
            PutU32(Position + 32, 1);
            PutU32(Position + 36, 1);
            PutU32(Position + 40, 0);
            PutU32(Position + 44, 0);
            PutCharSpec(Position + 48);
            PutDString(Position + 112, 128, State.Label);
            PutCharSpec(Position + 240);
            PutDString(Position + 304, 32, State.Label);
            PutU32(Position + 400, UdfBlock);
            PutU32(Position + 404, (uint)Root.FeBlock);
            PutU16(Position + 408, 0);
            PutRegId(Position + 416, "*OSTA UDF Compliant", 0x0102, 0);
            FinishTag(Position, 256, 512, 0);

            FinishTag((PartitionStart + 1) * UdfBlock, 8, 512, 1);
        }

        private void WriteFileEntry(Node Entry, UdfState State)
        {
            List<UdfExtent> Extents = new List<UdfExtent>();
            Int64 Remaining = Entry.DataBytes;
            Int64 Block = Entry.DataBlock;
            while (Remaining > 0)
            {
                Int64 Part = Math.Min(Remaining, State.MaxExtentBytes);
                UdfExtent Extent = new UdfExtent();
                Extent.Block = Block;
                Extent.Length = Part;
                Extents.Add(Extent);
                Block += (Part + UdfBlock - 1) / UdfBlock;
                Remaining -= Part;
            }

            int Position = (int)((PartitionStart + Entry.FeBlock) * UdfBlock);

            // ICB tag: strategy 4, one entry, directory (4) or byte stream (5), short allocation descriptors.
            PutU32(Position + 16, 0);
            PutU16(Position + 20, 4);
            PutU16(Position + 22, 0);
            PutU16(Position + 24, 1);
            Meta[Position + 27] = (byte)((Entry.IsDirectory == true) ? 4 : 5);
            PutU16(Position + 34, 0);

            PutU32(Position + 36, 0xFFFFFFFF);
            PutU32(Position + 40, 0xFFFFFFFF);
            PutU32(Position + 44, 0x000014A5);
            PutU16(Position + 48, 1);
            PutU64(Position + 56, (UInt64)Entry.DataBytes);
            PutU64(Position + 64, (UInt64)((Entry.DataBytes + UdfBlock - 1) / UdfBlock));
            PutTimestamp(Position + 72, Entry.ModifiedUtc);
            PutTimestamp(Position + 84, Entry.ModifiedUtc);
            PutTimestamp(Position + 96, Entry.ModifiedUtc);
            PutU32(Position + 108, 1);
            PutRegId(Position + 128, "*Vulpes Dolch", 0, 0);
            PutU64(Position + 160, (UInt64)((Entry.Parent == null) ? 0 : Entry.UniqueId));
            PutU32(Position + 168, 0);
            PutU32(Position + 172, (uint)(Extents.Count * ShortAdSize));

            int Offset = Position + FileEntryHeader;
            foreach (UdfExtent Extent in Extents)
            {
                PutU32(Offset, (uint)Extent.Length);
                PutU32(Offset + 4, (uint)Extent.Block);
                Offset += ShortAdSize;
            }

            FinishTag(Position, 261, FileEntryHeader + (Extents.Count * ShortAdSize), (uint)Entry.FeBlock);
        }

        private void WriteDirectoryData(Node Folder, UdfState State)
        {
            int Start = (int)((PartitionStart + Folder.DataBlock) * UdfBlock);
            int Offset = 0;

            Node Parent = (Folder.Parent == null) ? Folder : Folder.Parent;
            WriteFid(Start, ref Offset, Folder.DataBlock, 0x0A, new byte[0], Parent.FeBlock);

            byte[][] Ids = State.Identifiers[Folder];
            for (int Index = 0; Index < Folder.Children.Count; Index++)
            {
                Node Child = Folder.Children[Index];
                WriteFid(Start, ref Offset, Folder.DataBlock, (Child.IsDirectory == true) ? (byte)0x02 : (byte)0x00, Ids[Index], Child.FeBlock);
            }
        }

        private void WriteFid(int Start, ref int Offset, Int64 DataBlock, byte Characteristics, byte[] Identifier, Int64 FeBlock)
        {
            int Position = Start + Offset;
            int Length = FidLength(Identifier.Length);

            PutU16(Position + 16, 1);
            Meta[Position + 18] = Characteristics;
            Meta[Position + 19] = (byte)Identifier.Length;
            PutU32(Position + 20, UdfBlock);
            PutU32(Position + 24, (uint)FeBlock);
            PutU16(Position + 28, 0);
            PutU16(Position + 36, 0);
            Identifier.CopyTo(Meta, Position + 38);

            // The tag location is the block in which the descriptor starts.
            FinishTag(Position, 257, Length, (uint)(DataBlock + (Offset / UdfBlock)));
            Offset += Length;
        }

        // ---------------------------------------------------------------- field writers

        private static void FinishTag(byte[] Buffer, int Position, ushort Id, int Length, uint Location)
        {
            Buffer[Position] = (byte)(Id & 0xFF);
            Buffer[Position + 1] = (byte)(Id >> 8);
            Buffer[Position + 2] = 2;
            Buffer[Position + 3] = 0;
            PutU16(Buffer, Position + 6, 0);
            PutU16(Buffer, Position + 8, Crc16(Buffer, Position + 16, Length - 16));
            PutU16(Buffer, Position + 10, (ushort)(Length - 16));
            PutU32(Buffer, Position + 12, Location);

            int Sum = 0;
            for (int Index = 0; Index < 16; Index++)
            {
                if (Index != 4)
                    Sum += Buffer[Position + Index];
            }
            Buffer[Position + 4] = (byte)(Sum & 0xFF);
        }

        private void FinishTag(int Position, ushort Id, int Length, uint Location)
        {
            FinishTag(Meta, Position, Id, Length, Location);
        }

        private static void PutU16(byte[] Buffer, int Position, ushort Value)
        {
            Buffer[Position] = (byte)(Value & 0xFF);
            Buffer[Position + 1] = (byte)(Value >> 8);
        }

        private static void PutU32(byte[] Buffer, int Position, uint Value)
        {
            Buffer[Position] = (byte)(Value & 0xFF);
            Buffer[Position + 1] = (byte)((Value >> 8) & 0xFF);
            Buffer[Position + 2] = (byte)((Value >> 16) & 0xFF);
            Buffer[Position + 3] = (byte)(Value >> 24);
        }

        private void PutU16(int Position, ushort Value)
        {
            PutU16(Meta, Position, Value);
        }

        private void PutU32(int Position, uint Value)
        {
            PutU32(Meta, Position, Value);
        }

        private void PutU64(int Position, UInt64 Value)
        {
            PutU32(Position, (uint)(Value & 0xFFFFFFFF));
            PutU32(Position + 4, (uint)(Value >> 32));
        }

        private void PutAscii(int Position, string Text)
        {
            Encoding.ASCII.GetBytes(Text).CopyTo(Meta, Position);
        }

        private void PutCharSpec(int Position)
        {
            Meta[Position] = 0;
            Encoding.ASCII.GetBytes("OSTA Compressed Unicode").CopyTo(Meta, Position + 1);
        }

        // A dstring: compression id, characters, and in the last byte the number of bytes used.
        private void PutDString(int Position, int FieldLength, string Text)
        {
            if (string.IsNullOrEmpty(Text) == true)
                return;

            bool Narrow = true;
            foreach (char Character in Text)
            {
                if (Character > 0xFF)
                {
                    Narrow = false;
                    break;
                }
            }

            int Used;
            if (Narrow == true)
            {
                int Count = Math.Min(Text.Length, FieldLength - 2);
                Meta[Position] = 8;
                for (int Index = 0; Index < Count; Index++)
                    Meta[Position + 1 + Index] = (byte)Text[Index];
                Used = 1 + Count;
            }
            else
            {
                int Count = Math.Min(Text.Length, (FieldLength - 2) / 2);
                Meta[Position] = 16;
                for (int Index = 0; Index < Count; Index++)
                {
                    Meta[Position + 1 + (Index * 2)] = (byte)(Text[Index] >> 8);
                    Meta[Position + 2 + (Index * 2)] = (byte)(Text[Index] & 0xFF);
                }
                Used = 1 + (Count * 2);
            }
            Meta[Position + FieldLength - 1] = (byte)Used;
        }

        // An entity identifier: flags, 23 characters, and a suffix (UDF revision for the UDF identifiers).
        private void PutRegId(int Position, string Identifier, ushort Revision, byte OsClass)
        {
            Encoding.ASCII.GetBytes(Identifier).CopyTo(Meta, Position + 1);
            PutU16(Position + 24, Revision);
            Meta[Position + 26] = OsClass;
        }

        private void PutTimestamp(int Position, DateTime Time)
        {
            if ((Time.Year < 1) || (Time.Year > 9999))
                Time = new DateTime(1980, 1, 1, 0, 0, 0, DateTimeKind.Utc);

            PutU16(Position, 0x1000);
            PutU16(Position + 2, (ushort)Time.Year);
            Meta[Position + 4] = (byte)Time.Month;
            Meta[Position + 5] = (byte)Time.Day;
            Meta[Position + 6] = (byte)Time.Hour;
            Meta[Position + 7] = (byte)Time.Minute;
            Meta[Position + 8] = (byte)Time.Second;
        }
    }
}
