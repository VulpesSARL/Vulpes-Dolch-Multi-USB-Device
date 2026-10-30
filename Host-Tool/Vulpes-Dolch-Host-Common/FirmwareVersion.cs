using System;
using System.Buffers.Binary;
using System.Globalization;
using System.Text;

namespace VulpesDolch.Host.Common
{
    /// <summary>
    /// The build stamp "Major.Minor.YYMM.DDXX" (XX = build number of the day) of the Host / Client firmware, packed into the u32 the devices report
    /// (bits 30..27 Major, 26..23 Minor, 22..16 YY, 15..12 MM, 11..7 DD, 6..0 XX; 0 = unknown / no application). It is only shown by the application,
    /// it is not part of any USB descriptor.
    /// </summary>
    public static class FirmwareVersion
    {
        private const string TagMagic = "VDFWTAG1";
        private const int TagLength = 32;

        public static uint Pack(int Major, int Minor, int Year, int Month, int Day, int Build)
        {
            return (((uint)Major & 0xF) << 27) | (((uint)Minor & 0xF) << 23) | (((uint)Year & 0x7F) << 16) | (((uint)Month & 0xF) << 12) | (((uint)Day & 0x1F) << 7) | ((uint)Build & 0x7F);
        }

        /// <summary>"1.0.2610.2901", or "none" for 0 (no application running / unknown).</summary>
        public static string Format(uint Packed)
        {
            if (Packed == 0)
                return ("none");

            int Major = (int)((Packed >> 27) & 0xF);
            int Minor = (int)((Packed >> 23) & 0xF);
            int Year = (int)((Packed >> 16) & 0x7F);
            int Month = (int)((Packed >> 12) & 0xF);
            int Day = (int)((Packed >> 7) & 0x1F);
            int Build = (int)(Packed & 0x7F);

            return (Major + "." + Minor + "." + Year.ToString("00", CultureInfo.InvariantCulture) + Month.ToString("00", CultureInfo.InvariantCulture) + "." +
                Day.ToString("00", CultureInfo.InvariantCulture) + Build.ToString("00", CultureInfo.InvariantCulture));
        }

        /// <summary>Looks for the 32 byte version tag (see FirmwareVersionTag.h) in a firmware image; false when the image has none.</summary>
        public static bool TryFindInImage(byte[] Image, out uint Packed)
        {
            Packed = 0;
            byte[] Magic = Encoding.ASCII.GetBytes(TagMagic);

            for (int Offset = 0; Offset + TagLength <= Image.Length; Offset++)
            {
                if (Image.AsSpan(Offset, Magic.Length).SequenceEqual(Magic) == false)
                    continue;

                uint Candidate = BinaryPrimitives.ReadUInt32LittleEndian(Image.AsSpan(Offset + 8, 4));
                if (Candidate == 0)
                    continue;

                Packed = Candidate;
                return (true);
            }

            return (false);
        }
    }
}
