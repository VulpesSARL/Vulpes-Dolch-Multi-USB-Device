using System;

namespace VulpesDolch.Host.Common.Protocol
{
    /// <summary>
    /// CRC-32 of the message blocks: IEEE 802.3 polynomial (reflected 0xEDB88320), initial
    /// value and final xor 0xFFFFFFFF - the same value zlib, Ethernet and PNG produce.
    /// </summary>
    public static class Crc32
    {
        private static readonly uint[] Table = BuildTable();

        private static uint[] BuildTable()
        {
            uint[] Result = new uint[256];
            for (uint Index = 0; Index < 256; Index++)
            {
                uint Value = Index;
                for (int Bit = 0; Bit < 8; Bit++)
                    Value = ((Value & 1) != 0) ? ((Value >> 1) ^ 0xEDB88320u) : (Value >> 1);
                Result[Index] = Value;
            }

            return (Result);
        }

        public static uint Compute(ReadOnlySpan<byte> Data)
        {
            return (Update(0, Data));
        }

        /// <summary>Continues a CRC: pass the result of the previous call (0 for the first piece).</summary>
        public static uint Update(uint Crc, ReadOnlySpan<byte> Data)
        {
            uint Value = ~Crc;
            for (int Index = 0; Index < Data.Length; Index++)
                Value = Table[(Value ^ Data[Index]) & 0xFF] ^ (Value >> 8);

            return (~Value);
        }
    }
}
