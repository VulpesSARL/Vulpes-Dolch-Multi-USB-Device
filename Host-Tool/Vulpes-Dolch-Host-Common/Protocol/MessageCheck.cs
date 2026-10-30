using System;
using System.Buffers.Binary;
using System.Numerics;

namespace VulpesDolch.Host.Common.Protocol
{
    /// <summary>
    /// The check word at the end of every message block: a Fletcher style sum over the 32 bit
    /// words of the header and the padded payload, cheap enough for the ARM9 in the devices (about 2.5 cycles per word; a
    /// CRC-32 took 36). Two running sums, S1 = sum of the words and S2 = sum of the S1 values, which makes it sensitive to the
    /// order and position of the words as well as to their values (a zeroed, flipped, dropped or swapped word changes it).
    /// The result is S1 xor rotl(S2 * 0x9E3779B1, 13) (the rotation keeps a flip of the same bit in both sums from cancelling); S1 starts at 0x4B434456 and S2 at the word count. All arithmetic is modulo 2^32.
    /// The data length must be a multiple of 4 (message blocks always are).
    /// </summary>
    public static class MessageCheck
    {
        public const uint Seed = 0x4B434456;
        public const uint Multiplier = 0x9E3779B1;

        public static uint Compute(ReadOnlySpan<byte> Data)
        {
            if ((Data.Length & 3) != 0)
                throw new ArgumentException("The length must be a multiple of 4 bytes.", "Data");

            unchecked
            {
                uint S1 = Seed;
                uint S2 = (uint)(Data.Length / 4);
                for (int Offset = 0; Offset < Data.Length; Offset += 4)
                {
                    S1 += BinaryPrimitives.ReadUInt32LittleEndian(Data.Slice(Offset, 4));
                    S2 += S1;
                }

                return (S1 ^ BitOperations.RotateLeft(S2 * Multiplier, 13));
            }
        }
    }
}
