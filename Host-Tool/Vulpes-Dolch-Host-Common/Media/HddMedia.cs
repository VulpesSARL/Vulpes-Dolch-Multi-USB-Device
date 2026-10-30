using System;
using System.IO;
using System.Text;
using DiscUtils;
using DiscUtils.Streams;
using VulpesDolch.Host.Common.Protocol;

namespace VulpesDolch.Host.Common.Media
{
    /// <summary>Why an access to the disk image failed.</summary>
    public enum HddErrorKind
    {
        /// <summary>The sectors lie outside the disk.</summary>
        OutOfRange,

        /// <summary>The image could not be read.</summary>
        ReadError,

        /// <summary>The image could not be written (also: it was opened read-only).</summary>
        WriteError,

        /// <summary>The file is not a disk image this tool can use.</summary>
        BadFormat
    }

    public sealed class HddException : Exception
    {
        public HddException(string Message, HddErrorKind Kind) : base(Message)
        {
            this.Kind = Kind;
        }

        public HddException(string Message, HddErrorKind Kind, Exception Inner) : base(Message, Inner)
        {
            this.Kind = Kind;
        }

        public HddErrorKind Kind { get; }
    }

    /// <summary>
    /// A disk image prepared for the Client's HDD: VHDX (preferred), VHD, VMDK, VDI or a raw image, opened through DiscUtils. Serves
    /// sectors by LBA, writes them, flushes, and releases ranges. Owns the image; thread safe (one lock around the image stream).
    /// </summary>
    public sealed class HddMedia : IDisposable
    {
        public const int SectorSize = 512;

        /// <summary>Granularity of the zero test and the zero writes of <see cref="Release"/>.</summary>
        private const int ReleaseStep = 1024 * 1024;

        private readonly object StreamLock = new object();
        private readonly VirtualDisk Disk;
        private readonly SparseStream Content;
        private readonly FileStream ImageStream;
        private bool Disposed;

        private HddMedia(string ImagePath, VirtualDisk Disk, FileStream ImageStream, bool ReadOnly)
        {
            this.ImagePath = ImagePath;
            this.Disk = Disk;
            this.ImageStream = ImageStream;
            this.ReadOnly = ReadOnly;
            Content = Disk.Content;
            SectorCount = (UInt64)(Content.Length / SectorSize);
            MediaId = ComputeMediaId(ImagePath, Content.Length);
        }

        public string ImagePath { get; }

        /// <summary>Opaque 64 bit number identifying this disk to the Client (a hash of the path, the creation time and the capacity - not the
        /// modification time, a disk that is written to keeps its identity).</summary>
        public Int64 MediaId { get; }

        public UInt64 SectorCount { get; }

        /// <summary>The disk is write protected: the Client refuses writes by itself, and this class refuses them as well.</summary>
        public bool ReadOnly { get; }

        /// <summary>Opens an existing image. Throws <see cref="HddException"/> if it cannot be used.</summary>
        public static HddMedia Open(string ImagePath, bool ReadOnly)
        {
            return (Open(ImagePath, ReadOnly, true));
        }

        /// <summary>
        /// Opens an existing image. With <paramref name="WriteThrough"/> (the default) every write goes to the storage before the call returns -
        /// slower, but what the Client was told is written is on the disk even if the PC loses power right after.
        /// </summary>
        public static HddMedia Open(string ImagePath, bool ReadOnly, bool WriteThrough)
        {
            string FullPath = Path.GetFullPath(ImagePath);
            if (File.Exists(FullPath) == false)
                throw new HddException("The image file does not exist: " + FullPath, HddErrorKind.BadFormat);

            VirtualDisk Opened = null;
            FileStream Stream = null;
            try
            {
                Opened = OpenDisk(FullPath, ReadOnly, WriteThrough, out Stream);
                if ((Opened.Capacity % SectorSize) != 0)
                    throw new HddException("The size of the disk is not a multiple of 512 bytes.", HddErrorKind.BadFormat);
                if (Opened.Capacity == 0)
                    throw new HddException("The disk is empty.", HddErrorKind.BadFormat);

                HddMedia Media = new HddMedia(FullPath, Opened, Stream, ReadOnly);
                Opened = null;
                Stream = null;
                return (Media);
            }
            catch (HddException)
            {
                throw;
            }
            catch (Exception Problem) when ((Problem is IOException) || (Problem is InvalidDataException) || (Problem is UnauthorizedAccessException) || (Problem is NotSupportedException) || (Problem is NotImplementedException))
            {
                throw new HddException("The image could not be opened: " + Problem.Message, HddErrorKind.BadFormat, Problem);
            }
            finally
            {
                if (Opened != null)
                    Opened.Dispose();
                if (Stream != null)
                    Stream.Dispose();
            }
        }

        /// <summary>Creates a new, empty VHDX image (dynamic = sparse, or fixed) and opens it.</summary>
        public static HddMedia CreateVhdx(string ImagePath, Int64 Capacity, bool Fixed)
        {
            string FullPath = Path.GetFullPath(ImagePath);
            if (File.Exists(FullPath) == true)
                throw new HddException("The file exists already: " + FullPath, HddErrorKind.BadFormat);
            if ((Capacity <= 0) || ((Capacity % SectorSize) != 0))
                throw new HddException("The capacity must be a positive multiple of 512 bytes.", HddErrorKind.BadFormat);

            FileStream ImageFile = null;
            try
            {
                ImageFile = new FileStream(FullPath, FileMode.CreateNew, FileAccess.ReadWrite, FileShare.None, 1, FileOptions.WriteThrough);
                VirtualDisk Created;
                if (Fixed == true)
                    Created = DiscUtils.Vhdx.Disk.InitializeFixed(ImageFile, Ownership.Dispose, Capacity);
                else
                    Created = DiscUtils.Vhdx.Disk.InitializeDynamic(ImageFile, Ownership.Dispose, Capacity);
                FileStream Kept = ImageFile;
                ImageFile = null;
                return (new HddMedia(FullPath, Created, Kept, false));
            }
            catch (NotImplementedException Problem)
            {
                // DiscUtils.Vhdx cannot create fixed images; a raw image (.img) is the fixed-size alternative.
                if (ImageFile != null)
                {
                    ImageFile.Dispose();
                    File.Delete(FullPath);
                }
                throw new HddException("Creating a fixed VHDX is not supported by the VHDX library; create a dynamic one, or use a raw image (.img, e.g. fsutil file createnew <file.img> <bytes>).", HddErrorKind.BadFormat, Problem);
            }
            catch (Exception Problem) when ((Problem is IOException) || (Problem is UnauthorizedAccessException) || (Problem is NotSupportedException))
            {
                if (ImageFile != null)
                {
                    ImageFile.Dispose();
                    File.Delete(FullPath);
                }
                throw new HddException("The image could not be created: " + Problem.Message, HddErrorKind.BadFormat, Problem);
            }
        }

        private static VirtualDisk OpenDisk(string FullPath, bool ReadOnly, bool WriteThrough, out FileStream ImageStream)
        {
            ImageStream = null;
            string Extension = Path.GetExtension(FullPath).ToLowerInvariant();
            FileAccess Access = (ReadOnly == true) ? FileAccess.Read : FileAccess.ReadWrite;

            // VMDK can be split over several files that DiscUtils finds itself; every other format is one file that is opened here, so it can
            // be flushed to the storage (DiscUtils offers no flush of its own for VHDX).
            if (Extension == ".vmdk")
                return (new DiscUtils.Vmdk.Disk(FullPath, Access));

            if ((Extension != ".vhdx") && (Extension != ".vhd") && (Extension != ".vdi") && (Extension != ".img") && (Extension != ".raw") && (Extension != ".bin") && (Extension != ".dd"))
                throw new HddException("Unknown image type '" + Extension + "' (supported: .vhdx, .vhd, .vmdk, .vdi, .img, .raw).", HddErrorKind.BadFormat);

            FileStream Stream = new FileStream(FullPath, FileMode.Open, Access, (ReadOnly == true) ? FileShare.Read : FileShare.None, 1,
                ((ReadOnly == false) && (WriteThrough == true)) ? FileOptions.WriteThrough : FileOptions.None);
            try
            {
                VirtualDisk Result;
                switch (Extension)
                {
                    case ".vhdx":
                        Result = new DiscUtils.Vhdx.Disk(Stream, Ownership.Dispose);
                        break;
                    case ".vhd":
                        Result = new DiscUtils.Vhd.Disk(Stream, Ownership.Dispose);
                        break;
                    case ".vdi":
                        Result = new DiscUtils.Vdi.Disk(Stream, Ownership.Dispose);
                        break;
                    default:
                        Result = new DiscUtils.Raw.Disk(Stream, Ownership.Dispose);
                        break;
                }

                ImageStream = Stream;
                return (Result);
            }
            catch
            {
                Stream.Dispose();
                throw;
            }
        }

        /// <summary>The same number for the same file, so the PC can recognise its disk after it was away.</summary>
        public static Int64 ComputeMediaId(string ImagePath, Int64 Capacity)
        {
            string FullPath = Path.GetFullPath(ImagePath).ToLowerInvariant();
            FileInfo Info = new FileInfo(FullPath);

            // FNV-1a over the path, the creation time of the file and the capacity of the disk.
            UInt64 Hash = 14695981039346656037UL;
            Hash = Mix(Hash, Encoding.Unicode.GetBytes(FullPath));
            Hash = Mix(Hash, BitConverter.GetBytes((Info.Exists == true) ? Info.CreationTimeUtc.Ticks : 0L));
            Hash = Mix(Hash, BitConverter.GetBytes(Capacity));

            if (Hash == 0)
                Hash = 1;
            return (unchecked((Int64)Hash));
        }

        /// <summary>The DISK_ATTACH payload data for this disk.</summary>
        public HddAttachInfo CreateAttachInfo(bool Reattach)
        {
            HddAttachInfo Info = new HddAttachInfo();
            Info.MediaId = MediaId;
            Info.SectorCount = SectorCount;
            Info.SectorSize = SectorSize;
            Info.ReadOnly = ReadOnly;
            Info.Reattach = Reattach;
            return (Info);
        }

        /// <summary>Reads Count bytes (whole sectors) starting at the sector Lba into Buffer.</summary>
        public void Read(UInt64 Lba, byte[] Buffer, int Offset, int Count)
        {
            CheckRange(Lba, Count);

            lock (StreamLock)
            {
                CheckOpen();
                try
                {
                    Content.Position = (Int64)Lba * SectorSize;
                    StreamUtilities.ReadExact(Content, Buffer, Offset, Count);
                }
                catch (Exception Problem) when ((Problem is IOException) || (Problem is InvalidDataException) || (Problem is NotSupportedException))
                {
                    throw new HddException("Reading the image failed: " + Problem.Message, HddErrorKind.ReadError, Problem);
                }
            }
        }

        /// <summary>Writes Count bytes (whole sectors) from Buffer to the image, starting at the sector Lba.</summary>
        public void Write(UInt64 Lba, byte[] Buffer, int Offset, int Count)
        {
            CheckWritable();
            CheckRange(Lba, Count);

            lock (StreamLock)
            {
                CheckOpen();
                try
                {
                    Content.Position = (Int64)Lba * SectorSize;
                    Content.Write(Buffer, Offset, Count);
                }
                catch (Exception Problem) when ((Problem is IOException) || (Problem is InvalidDataException) || (Problem is NotSupportedException))
                {
                    throw new HddException("Writing the image failed: " + Problem.Message, HddErrorKind.WriteError, Problem);
                }
            }
        }

        /// <summary>
        /// Pushes everything written so far down to the storage. DiscUtils writes straight through to the file (VHDX offers no flush of its own),
        /// so what is left to do is flushing the file itself. A VMDK is opened by DiscUtils and cannot be flushed from here.
        /// </summary>
        public void Flush(bool ToDisk)
        {
            lock (StreamLock)
            {
                if ((Disposed == true) || (ReadOnly == true))
                    return;

                try
                {
                    if (ImageStream != null)
                        ImageStream.Flush(ToDisk);
                }
                catch (Exception Problem) when ((Problem is IOException) || (Problem is NotSupportedException))
                {
                    throw new HddException("Flushing the image failed: " + Problem.Message, HddErrorKind.WriteError, Problem);
                }
            }
        }

        /// <summary>
        /// The range reads back as zeros afterwards. Only what is allocated is touched: the unallocated part of a sparse image is zero already,
        /// pieces that are zero already are not written. A sparse VHDX can be compacted later (for example with Optimize-VHD), which gives
        /// the zeroed blocks back.
        /// </summary>
        public void Release(UInt64 Lba, UInt64 Count)
        {
            CheckWritable();
            if ((Lba >= SectorCount) || (Count > (SectorCount - Lba)))
                throw new HddException("The range lies outside the disk.", HddErrorKind.OutOfRange);
            if (Count == 0)
                return;

            Int64 Start = (Int64)Lba * SectorSize;
            Int64 Length = (Int64)Count * SectorSize;
            byte[] Piece = new byte[ReleaseStep];
            byte[] Zeros = new byte[ReleaseStep];

            lock (StreamLock)
            {
                CheckOpen();
                try
                {
                    foreach (StreamExtent Extent in Content.GetExtentsInRange(Start, Length))
                    {
                        Int64 Position = Math.Max(Extent.Start, Start);
                        Int64 End = Math.Min(Extent.Start + Extent.Length, Start + Length);
                        while (Position < End)
                        {
                            int Size = (int)Math.Min(ReleaseStep, End - Position);
                            Content.Position = Position;
                            StreamUtilities.ReadExact(Content, Piece, 0, Size);
                            if (IsZero(Piece, Size) == false)
                            {
                                Content.Position = Position;
                                Content.Write(Zeros, 0, Size);
                            }
                            Position += Size;
                        }
                    }
                }
                catch (Exception Problem) when ((Problem is IOException) || (Problem is InvalidDataException) || (Problem is NotSupportedException))
                {
                    throw new HddException("Releasing the range failed: " + Problem.Message, HddErrorKind.WriteError, Problem);
                }
            }
        }

        /// <summary>Flushes and closes the image; the file is free for anybody else afterwards.</summary>
        public void Dispose()
        {
            lock (StreamLock)
            {
                if (Disposed == true)
                    return;
                Disposed = true;

                try
                {
                    if ((ReadOnly == false) && (ImageStream != null))
                        ImageStream.Flush(true);
                }
                catch (IOException)
                {
                }
                finally
                {
                    Disk.Dispose();
                }
            }
        }

        private void CheckWritable()
        {
            if (ReadOnly == true)
                throw new HddException("The disk is read-only.", HddErrorKind.WriteError);
        }

        private void CheckOpen()
        {
            if (Disposed == true)
                throw new ObjectDisposedException("HddMedia");
        }

        private void CheckRange(UInt64 Lba, int Bytes)
        {
            if (((Bytes % SectorSize) != 0) || (Bytes < 0))
                throw new ArgumentException("The length must be a whole number of sectors.");

            UInt64 Count = (UInt64)(Bytes / SectorSize);
            if ((Lba >= SectorCount) || (Count > (SectorCount - Lba)))
                throw new HddException("The sectors lie outside the disk.", HddErrorKind.OutOfRange);
        }

        private static bool IsZero(byte[] Data, int Length)
        {
            for (int Index = 0; Index < Length; Index++)
            {
                if (Data[Index] != 0)
                    return (false);
            }
            return (true);
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
}
