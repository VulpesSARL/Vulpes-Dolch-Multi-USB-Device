using System;
using System.IO;
using Microsoft.Win32.SafeHandles;

namespace VulpesDolch.Host.Common.Media
{
    /// <summary>
    /// Reads just enough of a RIFF/WAVE file to use it as CD audio: the position and length of the "data" chunk, after checking that it
    /// holds 16 bit, 44.1 kHz, stereo PCM - the format of an audio CD, so the samples are the sector contents as they are.
    /// </summary>
    internal static class WaveFile
    {
        private const int HeaderScanBytes = 4096;

        private const ushort FormatPcm = 1;
        private const ushort FormatExtensible = 0xFFFE;

        /// <summary>Finds the data chunk. Throws CdImageException when the file is not CD format PCM or has no usable data chunk.</summary>
        internal static void ReadDataChunk(SafeFileHandle Handle, Int64 FileLength, string Path, out Int64 DataOffset, out Int64 DataLength)
        {
            byte[] Header = new byte[(int)Math.Min(FileLength, HeaderScanBytes)];
            int Done = 0;
            while (Done < Header.Length)
            {
                int Count = RandomAccess.Read(Handle, Header.AsSpan(Done), Done);
                if (Count <= 0)
                    break;
                Done += Count;
            }

            if ((Done < 12) || (BitConverter.ToUInt32(Header, 0) != 0x46464952) || (BitConverter.ToUInt32(Header, 8) != 0x45564157))
                throw new CdImageException("'" + Path + "' is not a RIFF/WAVE file.");

            bool FormatSeen = false;
            int Position = 12;
            while ((Position + 8) <= Done)
            {
                uint ChunkId = BitConverter.ToUInt32(Header, Position);
                Int64 ChunkSize = BitConverter.ToUInt32(Header, Position + 4);
                int Body = Position + 8;

                if (ChunkId == 0x20746D66)
                {
                    if ((ChunkSize < 16) || ((Body + 16) > Done))
                        throw new CdImageException("'" + Path + "' has a damaged fmt chunk.");

                    ushort Format = BitConverter.ToUInt16(Header, Body);
                    ushort Channels = BitConverter.ToUInt16(Header, Body + 2);
                    uint Rate = BitConverter.ToUInt32(Header, Body + 4);
                    ushort Bits = BitConverter.ToUInt16(Header, Body + 14);

                    bool Pcm = (Format == FormatPcm) || ((Format == FormatExtensible) && (ChunkSize >= 40) && ((Body + 26) <= Done) && (BitConverter.ToUInt16(Header, Body + 24) == FormatPcm));
                    if ((Pcm == false) || (Channels != 2) || (Rate != 44100) || (Bits != 16))
                        throw new CdImageException("'" + Path + "' is not CD audio: only 16 bit, 44100 Hz, stereo PCM WAVE files can be used (this one is format " + Format + ", " + Channels + " channel(s), " + Rate + " Hz, " + Bits + " bit).");

                    FormatSeen = true;
                }
                else if (ChunkId == 0x61746164)
                {
                    if (FormatSeen == false)
                        throw new CdImageException("'" + Path + "' has its data chunk before the fmt chunk.");

                    DataOffset = Body;
                    // Streaming writers leave the size at 0 or 0xFFFFFFFF; the rest of the file is the data then.
                    DataLength = ((ChunkSize == 0) || (ChunkSize == 0xFFFFFFFFL) || ((DataOffset + ChunkSize) > FileLength)) ? (FileLength - DataOffset) : ChunkSize;
                    if (DataLength <= 0)
                        throw new CdImageException("'" + Path + "' contains no audio data.");
                    return;
                }

                Position = Body + (int)Math.Min(ChunkSize + (ChunkSize & 1), int.MaxValue - Body);
            }

            throw new CdImageException("'" + Path + "' has no data chunk within its first " + HeaderScanBytes + " bytes.");
        }
    }
}
