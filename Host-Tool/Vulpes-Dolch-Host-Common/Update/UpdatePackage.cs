using System;
using System.Buffers.Binary;
using System.IO;
using System.Security.Cryptography;
using VulpesDolch.Host.Common.Protocol;

namespace VulpesDolch.Host.Common.Update
{
    public enum UpdateTarget : ushort
    {
        Host = 1,
        Client = 2
    }

    /// <summary>
    /// The firmware update container (layout in Update\UpdateCore.h): a 64 byte header, signed with HMAC-SHA256 under a shared key that is not
    /// published, and the image the boot ROM loads from the EEPROM, optionally LZSS compressed.
    /// </summary>
    public sealed class UpdatePackage
    {
        public const uint Magic = 0x50554456;
        public const int HeaderSize = 64;
        public const int MaximumImageSize = 262144;
        public const int KeySize = 32;
        private const uint FlagCompressed = 1;

        public UpdateTarget Target { get; private set; }

        public uint ImageVersion { get; private set; }

        public int ImageLength { get; private set; }

        public int PayloadLength { get; private set; }

        public bool Compressed { get; private set; }

        public uint ImageCrc { get; private set; }

        /// <summary>The whole file: header and payload.</summary>
        public byte[] File { get; private set; }

        private UpdatePackage()
        {
        }

        /// <summary>Reads the key file: 64 hex digits, whitespace allowed.</summary>
        public static byte[] LoadKey(string Path)
        {
            string Text = System.IO.File.ReadAllText(Path);
            string Digits = string.Empty;
            foreach (char Letter in Text)
            {
                if (char.IsWhiteSpace(Letter) == false)
                    Digits += Letter;
            }

            if (Digits.Length != KeySize * 2)
                throw new InvalidDataException("The key file must hold " + (KeySize * 2) + " hex digits.");

            return (Convert.FromHexString(Digits));
        }

        /// <summary>Builds a signed update file from an FX3 boot image (the .img that elf2img makes).</summary>
        public static UpdatePackage Create(byte[] Image, UpdateTarget Target, uint ImageVersion, byte[] Key, bool Compress)
        {
            if ((Image.Length < 512) || (Image.Length > MaximumImageSize))
                throw new ArgumentException("The image must be between 512 bytes and 256 KiB.");
            if ((Image[0] != (byte)'C') || (Image[1] != (byte)'Y'))
                throw new ArgumentException("This is not an FX3 boot image (it does not start with 'CY').");
            if (Key.Length != KeySize)
                throw new ArgumentException("The key must be 32 bytes.");

            byte[] Payload = (Compress == true) ? LzssCodec.Compress(Image) : Image;
            if ((Compress == true) && (Payload.Length >= Image.Length))
            {
                Payload = Image;
                Compress = false;
            }

            byte[] Bytes = new byte[HeaderSize + Payload.Length];
            BinaryPrimitives.WriteUInt32LittleEndian(Bytes.AsSpan(0, 4), Magic);
            BinaryPrimitives.WriteUInt16LittleEndian(Bytes.AsSpan(4, 2), 1);
            BinaryPrimitives.WriteUInt16LittleEndian(Bytes.AsSpan(6, 2), (ushort)Target);
            BinaryPrimitives.WriteUInt32LittleEndian(Bytes.AsSpan(8, 4), ImageVersion);
            BinaryPrimitives.WriteUInt32LittleEndian(Bytes.AsSpan(12, 4), (Compress == true) ? FlagCompressed : 0u);
            BinaryPrimitives.WriteUInt32LittleEndian(Bytes.AsSpan(16, 4), (uint)Payload.Length);
            BinaryPrimitives.WriteUInt32LittleEndian(Bytes.AsSpan(20, 4), (uint)Image.Length);
            BinaryPrimitives.WriteUInt32LittleEndian(Bytes.AsSpan(24, 4), Crc32.Compute(Image));
            Payload.CopyTo(Bytes, HeaderSize);

            byte[] Mac = ComputeMac(Bytes, Key);
            Mac.CopyTo(Bytes, 32);
            return (Parse(Bytes));
        }

        /// <summary>Signature of a file: HMAC-SHA256 over header bytes 0-31 and the payload.</summary>
        private static byte[] ComputeMac(byte[] Bytes, byte[] Key)
        {
            byte[] Signed = new byte[32 + Bytes.Length - HeaderSize];
            Array.Copy(Bytes, 0, Signed, 0, 32);
            Array.Copy(Bytes, HeaderSize, Signed, 32, Bytes.Length - HeaderSize);
            return (HMACSHA256.HashData(Key, Signed));
        }

        /// <summary>Reads the header of a file and checks its structure (not the signature: see <see cref="Verify"/>).</summary>
        public static UpdatePackage Parse(byte[] Bytes)
        {
            if (Bytes.Length <= HeaderSize)
                throw new InvalidDataException("The update file is too short.");

            ReadOnlySpan<byte> Raw = Bytes;
            if (BinaryPrimitives.ReadUInt32LittleEndian(Raw.Slice(0, 4)) != Magic)
                throw new InvalidDataException("This is not an update file (wrong magic).");
            if (BinaryPrimitives.ReadUInt16LittleEndian(Raw.Slice(4, 2)) != 1)
                throw new InvalidDataException("Unknown update file version.");

            UpdatePackage Result = new UpdatePackage();
            Result.File = Bytes;
            Result.Target = (UpdateTarget)BinaryPrimitives.ReadUInt16LittleEndian(Raw.Slice(6, 2));
            Result.ImageVersion = BinaryPrimitives.ReadUInt32LittleEndian(Raw.Slice(8, 4));
            Result.Compressed = ((BinaryPrimitives.ReadUInt32LittleEndian(Raw.Slice(12, 4)) & FlagCompressed) != 0);
            Result.PayloadLength = (int)BinaryPrimitives.ReadUInt32LittleEndian(Raw.Slice(16, 4));
            Result.ImageLength = (int)BinaryPrimitives.ReadUInt32LittleEndian(Raw.Slice(20, 4));
            Result.ImageCrc = BinaryPrimitives.ReadUInt32LittleEndian(Raw.Slice(24, 4));
            if (Result.PayloadLength != Bytes.Length - HeaderSize)
                throw new InvalidDataException("The payload length in the header does not match the file.");
            return (Result);
        }

        public static UpdatePackage Load(string Path)
        {
            return (Parse(System.IO.File.ReadAllBytes(Path)));
        }

        /// <summary>Checks the signature, and that the payload gives the image with the CRC in the header.</summary>
        public bool Verify(byte[] Key)
        {
            byte[] Mac = ComputeMac(File, Key);
            if (CryptographicOperations.FixedTimeEquals(Mac, File.AsSpan(32, 32)) == false)
                return (false);

            byte[] Image = ExtractImage();
            return ((Image != null) && (Crc32.Compute(Image) == ImageCrc));
        }

        /// <summary>The image as the EEPROM will hold it; null if the payload is damaged.</summary>
        public byte[] ExtractImage()
        {
            byte[] Payload = new byte[PayloadLength];
            Array.Copy(File, HeaderSize, Payload, 0, PayloadLength);
            return ((Compressed == true) ? LzssCodec.Decompress(Payload, ImageLength) : Payload);
        }
    }
}
