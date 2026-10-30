using System;
using System.IO;
using System.Reflection;
using System.Runtime.InteropServices;
using System.Text;
using DiscUtils;
using DiscUtils.Fat;
using DiscUtils.Ntfs;
using DiscUtils.Partitions;
using DiscUtils.Streams;

namespace VulpesDolch.Host.Common.Media
{
    public enum DiskImageType
    {
        /// <summary>Microsoft VHDX, dynamic (grows as it is written).</summary>
        Vhdx = 0,

        /// <summary>A plain binary image; a sparse file on NTFS, so an empty 2 TiB image costs nothing.</summary>
        Raw = 1,

        /// <summary>The old Microsoft / Connectix VHD, dynamic.</summary>
        Vhd = 2,

        /// <summary>VMware VMDK, monolithic sparse.</summary>
        Vmdk = 3,

        /// <summary>Oracle VirtualBox VDI, dynamic.</summary>
        Vdi = 4,
    }

    /// <summary>What is put into a new image. "Plain" means the file system starts at sector 0 with no partition table (like a floppy or a card reader stick).</summary>
    public enum DiskPreparation
    {
        None = 0,
        NtfsMbr = 1,
        NtfsGpt = 2,

        /// <summary>GPT with a 256 MiB EFI System Partition (FAT32), a 16 MiB Microsoft Reserved partition, then the NTFS partition.</summary>
        NtfsGptEfi = 3,
        NtfsPlain = 4,
        Fat32Mbr = 5,
        Fat32Gpt = 6,
        Fat32Plain = 7,
    }

    /// <summary>Creates a new disk image file of one of the supported formats and, if asked, puts a partition table and a file system into it.</summary>
    public static class DiskImageCreator
    {
        public const Int64 MiB = 1024 * 1024;
        public const Int64 GiB = 1024 * MiB;
        public const Int64 TiB = 1024 * GiB;

        /// <summary>The volume label when none is given; the EFI System Partition is always labelled "EFI".</summary>
        public const string DefaultLabel = "Vulpes-Dolch";

        /// <summary>The default on FAT, which allows 11 upper case characters (the hyphen of the NTFS name is dropped): this fits exactly.</summary>
        public const string DefaultFatLabel = "VULPESDOLCH";

        private const int SectorSize = 512;
        private const Int64 AlignmentSectors = MiB / SectorSize;               // partitions start on 1 MiB
        private const Int64 EfiPartitionBytes = 256 * MiB;
        private const Int64 ReservedPartitionBytes = 16 * MiB;
        private const Int64 GptBackupSectors = 33;                             // secondary header + table at the end of a GPT disk
        private const Int64 FirstPartitionSector = AlignmentSectors;
        private const uint FSCTL_SET_SPARSE = 0x900C4;

        [DllImport("kernel32.dll", SetLastError = true)]
        private static extern bool DeviceIoControl(Microsoft.Win32.SafeHandles.SafeFileHandle Handle, uint Code, IntPtr InBuffer, int InSize, IntPtr OutBuffer, int OutSize,
            out int Returned, IntPtr Overlapped);

        /// <summary>The reason the combination cannot be created, or null when it can.</summary>
        public static string Validate(DiskImageType Type, Int64 SizeBytes, DiskPreparation Preparation)
        {
            if (SizeBytes < MiB)
                return ("The size must be at least 1 MiB.");

            if (SizeBytes > 64 * TiB)
                return ("The size must be at most 64 TiB.");

            if ((Type == DiskImageType.Vhd) && (SizeBytes > 2040 * GiB))
                return ("The old VHD format holds at most 2040 GiB.");

            bool Mbr = (Preparation == DiskPreparation.NtfsMbr) || (Preparation == DiskPreparation.Fat32Mbr);
            if ((Mbr == true) && (SizeBytes > 2 * TiB - 2 * MiB))
                return ("A partition table of the MBR kind cannot address more than 2 TiB; use GPT or no partition table.");

            Int64 Minimum = MinimumSize(Preparation);
            if (SizeBytes < Minimum)
                return ("This preparation needs at least " + (Minimum / MiB) + " MiB.");

            return (null);
        }

        private static Int64 MinimumSize(DiskPreparation Preparation)
        {
            switch (Preparation)
            {
                case DiskPreparation.NtfsPlain:
                    return (8 * MiB);
                case DiskPreparation.NtfsMbr:
                case DiskPreparation.NtfsGpt:
                    return (16 * MiB);
                case DiskPreparation.NtfsGptEfi:
                    return (512 * MiB);                  // 256 + 16 + room for the NTFS partition
                case DiskPreparation.Fat32Plain:
                    return (4 * MiB);                    // tiny volumes become FAT12 / FAT16, they are never forced to FAT32
                case DiskPreparation.Fat32Mbr:
                case DiskPreparation.Fat32Gpt:
                    return (6 * MiB);
            }
            return (MiB);
        }

        /// <summary>
        /// Creates the image at <paramref name="Path"/> (which must not exist) and prepares it. The label is the volume label of the file system (cut and cleaned as the
        /// file system needs it). A file that was started and then fails is deleted again.
        /// </summary>
        public static void Create(string Path, DiskImageType Type, Int64 SizeBytes, DiskPreparation Preparation, string Label)
        {
            string Problem = Validate(Type, SizeBytes, Preparation);
            if (Problem != null)
                throw new ArgumentException(Problem);
            if (File.Exists(Path) == true)
                throw new IOException("The file " + Path + " exists already.");

            bool Done = false;
            try
            {
                using (VirtualDisk Disk = CreateDisk(Path, Type, SizeBytes))
                    Prepare(Disk, SizeBytes, Preparation, Label);
                Done = true;
            }
            finally
            {
                if ((Done == false) && (File.Exists(Path) == true))
                {
                    try
                    {
                        File.Delete(Path);
                    }
                    catch (IOException)
                    {
                        // A half written image that cannot be removed is reported by the original exception.
                    }
                }
            }
        }

        private static VirtualDisk CreateDisk(string Path, DiskImageType Type, Int64 SizeBytes)
        {
            switch (Type)
            {
                case DiskImageType.Vhdx:
                    return (DiscUtils.Vhdx.Disk.InitializeDynamic(new FileStream(Path, FileMode.CreateNew, FileAccess.ReadWrite, FileShare.None), Ownership.Dispose, SizeBytes));

                case DiskImageType.Vhd:
                    return (DiscUtils.Vhd.Disk.InitializeDynamic(new FileStream(Path, FileMode.CreateNew, FileAccess.ReadWrite, FileShare.None), Ownership.Dispose, SizeBytes));

                case DiskImageType.Vmdk:
                    return (DiscUtils.Vmdk.Disk.Initialize(Path, SizeBytes, DiscUtils.Vmdk.DiskCreateType.MonolithicSparse));

                case DiskImageType.Vdi:
                    return (DiscUtils.Vdi.Disk.InitializeDynamic(new FileStream(Path, FileMode.CreateNew, FileAccess.ReadWrite, FileShare.None), Ownership.Dispose, SizeBytes));

                default:
                    FileStream Raw = new FileStream(Path, FileMode.CreateNew, FileAccess.ReadWrite, FileShare.None);
                    MakeSparse(Raw);
                    Raw.SetLength(SizeBytes);
                    return (new DiscUtils.Raw.Disk(Raw, Ownership.Dispose, Geometry.FromCapacity(SizeBytes)));
            }
        }

        /// <summary>Marks an NTFS file as sparse before it is extended, so a big empty image takes no space. Other file systems and platforms simply keep a normal file.</summary>
        private static void MakeSparse(FileStream Stream)
        {
            if (OperatingSystem.IsWindows() == false)
                return;

            int Returned;
            DeviceIoControl(Stream.SafeFileHandle, FSCTL_SET_SPARSE, IntPtr.Zero, 0, IntPtr.Zero, 0, out Returned, IntPtr.Zero);
        }

        // ---- partitions and file systems ----

        private static void Prepare(VirtualDisk Disk, Int64 SizeBytes, DiskPreparation Preparation, string Label)
        {
            Int64 Total = SizeBytes / SectorSize;
            Geometry Layout = Disk.Geometry ?? Geometry.FromCapacity(SizeBytes);

            switch (Preparation)
            {
                case DiskPreparation.None:
                    return;

                case DiskPreparation.NtfsPlain:
                    FormatNtfs(Disk.Content, Layout, 0, Total, Label);
                    return;

                case DiskPreparation.Fat32Plain:
                    FormatFat32(Disk.Content, Layout, 0, Total, Label);
                    return;

                case DiskPreparation.NtfsMbr:
                case DiskPreparation.Fat32Mbr:
                {
                    BiosPartitionTable Table = BiosPartitionTable.Initialize(Disk);
                    Int64 Last = AlignDown(Total, AlignmentSectors) - 1;
                    bool Ntfs = (Preparation == DiskPreparation.NtfsMbr);
                    int Index = Table.CreatePrimaryBySector(FirstPartitionSector, Last, (Ntfs == true) ? BiosPartitionTypes.Ntfs : BiosPartitionTypes.Fat32Lba, true);
                    FormatPartition(Disk, Table, Index, Layout, Ntfs, Label);
                    return;
                }

                case DiskPreparation.NtfsGpt:
                case DiskPreparation.Fat32Gpt:
                {
                    GuidPartitionTable Table = GuidPartitionTable.Initialize(Disk);
                    Int64 Last = AlignDown(Total - GptBackupSectors, AlignmentSectors) - 1;
                    bool Ntfs = (Preparation == DiskPreparation.NtfsGpt);
                    int Index = Table.Create(FirstPartitionSector, Last, GuidPartitionTypes.WindowsBasicData, 0, "Basic data partition");
                    FormatPartition(Disk, Table, Index, Layout, Ntfs, Label);
                    return;
                }

                case DiskPreparation.NtfsGptEfi:
                {
                    GuidPartitionTable Table = GuidPartitionTable.Initialize(Disk);
                    Int64 EfiSectors = EfiPartitionBytes / SectorSize;
                    Int64 ReservedSectors = ReservedPartitionBytes / SectorSize;
                    Int64 EfiLast = FirstPartitionSector + EfiSectors - 1;
                    Int64 ReservedFirst = EfiLast + 1;
                    Int64 ReservedLast = ReservedFirst + ReservedSectors - 1;
                    Int64 MainFirst = ReservedLast + 1;
                    Int64 MainLast = AlignDown(Total - GptBackupSectors, AlignmentSectors) - 1;

                    int Efi = Table.Create(FirstPartitionSector, EfiLast, GuidPartitionTypes.EfiSystem, 0, "EFI system partition");
                    Table.Create(ReservedFirst, ReservedLast, GuidPartitionTypes.MicrosoftReserved, 0, "Microsoft reserved partition");
                    int Main = Table.Create(MainFirst, MainLast, GuidPartitionTypes.WindowsBasicData, 0, "Basic data partition");

                    FormatPartition(Disk, Table, Efi, Layout, false, "EFI", true);
                    FormatPartition(Disk, Table, Main, Layout, true, Label);
                    return;
                }
            }
        }

        private static Int64 AlignDown(Int64 Sector, Int64 Alignment)
        {
            return ((Sector / Alignment) * Alignment);
        }

        private static void FormatPartition(VirtualDisk Disk, PartitionTable Table, int Index, Geometry Layout, bool Ntfs, string Label, bool ForceFat32 = false)
        {
            PartitionInfo Info = Table.Partitions[Index];
            using (Stream Content = Info.Open())
            {
                if (Ntfs == true)
                    FormatNtfs(Content, Layout, Info.FirstSector, Info.SectorCount, Label);
                else if (ForceFat32 == true)
                    FormatExactFat32(Content, Layout, Info.FirstSector, Info.SectorCount, Label);
                else
                    FormatFat32(Content, Layout, Info.FirstSector, Info.SectorCount, Label);
            }
        }

        private static void FormatNtfs(Stream Content, Geometry Layout, Int64 FirstSector, Int64 SectorCount, string Label)
        {
            // The library marks its NTFS formatter as Windows only (it builds Windows security descriptors); this tool is a Windows tool.
            if (OperatingSystem.IsWindows() == false)
                throw new PlatformNotSupportedException("Formatting NTFS is supported on Windows only.");

            // CA1416: checked just above (the analyzer does not understand the == false form of the check).
#pragma warning disable CA1416
            using (NtfsFileSystem Formatted = NtfsFileSystem.Format(Content, NtfsLabel(Label), Layout, FirstSector, SectorCount))
            {
                // DiscUtils builds the case mapping table ($UpCase) from the .NET tables, which chkdsk reports as an error in the uppercase file. Windows' own table makes
                // Windows accept the new volume as clean (checked with chkdsk on Windows Server 2022).
                using (Stream Table = Formatted.OpenFile(@"\$UpCase", FileMode.Open, FileAccess.ReadWrite))
                {
                    byte[] Windows = WindowsUpCase();
                    Table.Position = 0;
                    Table.Write(Windows, 0, Windows.Length);
                }
            }
#pragma warning restore CA1416
        }

        /// <summary>FAT as DiscUtils formats it: the variant (FAT12 / 16 / 32) follows the size of the volume, so small volumes get FAT16; big ones FAT32.</summary>
        private static void FormatFat32(Stream Content, Geometry Layout, Int64 FirstSector, Int64 SectorCount, string Label)
        {
            string VolumeLabel = FatLabel(Label);
            using (FatFileSystem Formatted = FatFileSystem.FormatPartition(Content, VolumeLabel, Layout, (int)FirstSector, (int)SectorCount, 0))
            {
            }
            WriteFatLabelEntry(Content, VolumeLabel);
        }

        /// <summary>
        /// A FAT32 volume whatever its size (the library chooses FAT12 / 16 / 32 by size and gives a 256 MiB volume FAT16; firmware of many PCs expects FAT32 on the EFI System
        /// Partition). Layout as Windows formats it: 32 reserved sectors, two FATs, the root directory in cluster 2, FSInfo in sector 1, a backup of the boot sector in sector 6,
        /// the cluster size of Microsoft's table for 512 byte sectors. The volume must be new (all zero); the data area is not touched.
        /// </summary>
        private static void FormatExactFat32(Stream Content, Geometry Layout, Int64 FirstSector, Int64 SectorCount, string Label)
        {
            const int ReservedSectors = 32;
            const int FatCount = 2;

            string VolumeLabel = FatLabel(Label);
            int SectorsPerCluster;
            if (SectorCount <= 66600)
                throw new ArgumentException("The volume is too small for FAT32.");
            else if (SectorCount <= 532480)
                SectorsPerCluster = 1;
            else if (SectorCount <= 16777216)
                SectorsPerCluster = 8;
            else if (SectorCount <= 33554432)
                SectorsPerCluster = 16;
            else if (SectorCount <= 67108864)
                SectorsPerCluster = 32;
            else
                SectorsPerCluster = 64;

            Int64 FatSectors = 1;
            Int64 Clusters;
            for (;;)
            {
                Clusters = (SectorCount - ReservedSectors - FatCount * FatSectors) / SectorsPerCluster;
                Int64 Needed = ((Clusters + 2) * 4 + SectorSize - 1) / SectorSize;
                if (Needed <= FatSectors)
                    break;
                FatSectors = Needed;
            }

            if ((Clusters < 65525) || (Clusters >= 0x0FFFFFF5))
                throw new ArgumentException("The volume size does not give a valid FAT32 cluster count.");

            byte[] Boot = new byte[SectorSize];
            Boot[0] = 0xEB;
            Boot[1] = 0x58;
            Boot[2] = 0x90;
            Array.Copy(Encoding.ASCII.GetBytes("MSWIN4.1"), 0, Boot, 3, 8);
            PutValue(Boot, 11, SectorSize, 2);
            Boot[13] = (byte)SectorsPerCluster;
            PutValue(Boot, 14, ReservedSectors, 2);
            Boot[16] = FatCount;
            PutValue(Boot, 17, 0, 2);
            PutValue(Boot, 19, 0, 2);
            Boot[21] = 0xF8;
            PutValue(Boot, 22, 0, 2);
            PutValue(Boot, 24, Math.Min(Layout.SectorsPerTrack, 63), 2);
            PutValue(Boot, 26, Math.Min(Layout.HeadsPerCylinder, 255), 2);
            PutValue(Boot, 28, Math.Min(FirstSector, uint.MaxValue), 4);
            PutValue(Boot, 32, SectorCount, 4);
            PutValue(Boot, 36, FatSectors, 4);
            PutValue(Boot, 40, 0, 2);
            PutValue(Boot, 42, 0, 2);
            PutValue(Boot, 44, 2, 4);
            PutValue(Boot, 48, 1, 2);
            PutValue(Boot, 50, 6, 2);
            Boot[64] = 0x80;
            Boot[66] = 0x29;
            PutValue(Boot, 67, (uint)Environment.TickCount, 4);
            Array.Copy(Encoding.ASCII.GetBytes(VolumeLabel.PadRight(11).Substring(0, 11)), 0, Boot, 71, 11);
            Array.Copy(Encoding.ASCII.GetBytes("FAT32   "), 0, Boot, 82, 8);
            Boot[510] = 0x55;
            Boot[511] = 0xAA;

            byte[] Info = new byte[SectorSize];
            PutValue(Info, 0, 0x41615252, 4);
            PutValue(Info, 484, 0x61417272, 4);
            PutValue(Info, 488, Clusters - 1, 4);       // free clusters: all but the root directory
            PutValue(Info, 492, 3, 4);                  // where to look for the next free cluster
            PutValue(Info, 508, 0xAA550000, 4);

            WriteSector(Content, 0, Boot);
            WriteSector(Content, 1, Info);
            WriteSector(Content, 6, Boot);
            WriteSector(Content, 7, Info);

            // The FATs: media entry, end-of-chain marker, and cluster 2 (the root directory) as a single cluster chain.
            byte[] Fat = new byte[SectorSize];
            PutValue(Fat, 0, 0x0FFFFFF8, 4);
            PutValue(Fat, 4, 0x0FFFFFFF, 4);
            PutValue(Fat, 8, 0x0FFFFFFF, 4);
            for (int Index = 0; Index < FatCount; Index++)
                WriteSector(Content, ReservedSectors + Index * FatSectors, Fat);

            // The root directory (cluster 2, the first sector of the data area) is empty, with the volume label as its first entry.
            byte[] Empty = new byte[SectorSize];
            Int64 RootSector = ReservedSectors + FatCount * FatSectors;
            for (int Index = 0; Index < SectorsPerCluster; Index++)
                WriteSector(Content, RootSector + Index, Empty);
            WriteFatLabelEntry(Content, VolumeLabel);
        }

        private static void PutValue(byte[] Target, int Offset, Int64 Value, int Bytes)
        {
            for (int Index = 0; Index < Bytes; Index++)
                Target[Offset + Index] = (byte)(Value >> (8 * Index));
        }

        private static void WriteSector(Stream Content, Int64 Sector, byte[] Data)
        {
            Content.Position = Sector * SectorSize;
            Content.Write(Data, 0, Data.Length);
        }

        private static byte[] WindowsUpCaseTable;
        /// <summary>Windows' $UpCase table: 65536 little endian 16 bit values, as stored in the embedded resource.</summary>
        private static byte[] WindowsUpCase()
        {
            if (WindowsUpCaseTable != null)
                return (WindowsUpCaseTable);

            using (Stream Resource = Assembly.GetExecutingAssembly().GetManifestResourceStream("NtfsUpCase.bin"))
            using (MemoryStream Table = new MemoryStream())
            {
                Resource.CopyTo(Table);
                WindowsUpCaseTable = Table.ToArray();
            }
            return (WindowsUpCaseTable);
        }

        /// <summary>
        /// Puts the volume label into the root directory of a new FAT volume as well (as Windows does; DiscUtils only writes it into the boot sector and Windows then shows no
        /// label): the root directory of a freshly formatted volume is empty, so the label entry becomes its first entry. The root directory of FAT12 / 16 starts right behind
        /// the FATs, that of FAT32 is cluster 2, which is the first sector of the data area - behind the FATs as well.
        /// </summary>
        private static void WriteFatLabelEntry(Stream Content, string Label)
        {
            byte[] Boot = new byte[SectorSize];
            Content.Position = 0;
            Content.ReadExactly(Boot, 0, SectorSize);

            int Reserved = Boot[14] | (Boot[15] << 8);
            int Fats = Boot[16];
            int FatSectors = Boot[22] | (Boot[23] << 8);
            if (FatSectors == 0)
                FatSectors = Boot[36] | (Boot[37] << 8) | (Boot[38] << 16) | (Boot[39] << 24);

            Int64 RootSector = Reserved + (Int64)Fats * FatSectors;

            byte[] Entry = new byte[32];
            byte[] Name = Encoding.ASCII.GetBytes(Label.PadRight(11).Substring(0, 11));
            Array.Copy(Name, 0, Entry, 0, 11);
            Entry[11] = 0x08;
            DateTime Now = DateTime.Now;
            int Time = (Now.Hour << 11) | (Now.Minute << 5) | (Now.Second / 2);
            int Date = ((Now.Year - 1980) << 9) | (Now.Month << 5) | Now.Day;
            Entry[22] = (byte)Time;
            Entry[23] = (byte)(Time >> 8);
            Entry[24] = (byte)Date;
            Entry[25] = (byte)(Date >> 8);

            Content.Position = RootSector * SectorSize;
            Content.Write(Entry, 0, Entry.Length);
        }

        // ---- labels ----

        private static string NtfsLabel(string Label)
        {
            string Clean = (Label ?? "").Trim();
            if (Clean.Length == 0)
                Clean = DefaultLabel;
            return ((Clean.Length > 32) ? Clean.Substring(0, 32) : Clean);
        }

        /// <summary>FAT labels are at most 11 characters of upper case ASCII without the characters FAT forbids (without a label it is VULPESDOLCH).</summary>
        private static string FatLabel(string Label)
        {
            StringBuilder Clean = new StringBuilder();
            foreach (char Letter in (Label ?? "").ToUpperInvariant())
            {
                if ((Letter >= ' ') && (Letter <= '~') && ("\"*+,./:;<=>?[\\]|".IndexOf(Letter) < 0))
                    Clean.Append(Letter);
            }

            string Result = Clean.ToString().Trim();
            if (Result.Length > 11)
                Result = Result.Substring(0, 11).TrimEnd();
            return ((Result.Length == 0) ? DefaultFatLabel : Result);
        }
    }
}
