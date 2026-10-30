using System;
using System.IO;
using System.Text.Json;

namespace VulpesDolch.Host.Common.Service
{
    /// <summary>Length prefixed JSON frames on a stream (4 byte little endian length, then UTF-8 JSON).</summary>
    public static class PipeFraming
    {
        private const int MaxFrameBytes = 1024 * 1024;

        public static void Write(Stream Target, ServiceFrame Frame)
        {
            byte[] Json = JsonSerializer.SerializeToUtf8Bytes(Frame);
            byte[] Header = BitConverter.GetBytes(Json.Length);

            // One write for the whole frame so concurrent writers (guarded by a lock in the callers) never interleave.
            byte[] All = new byte[Header.Length + Json.Length];
            Header.CopyTo(All, 0);
            Json.CopyTo(All, Header.Length);
            Target.Write(All, 0, All.Length);
            Target.Flush();
        }

        /// <summary>Reads one frame; null when the other side closed the pipe.</summary>
        public static ServiceFrame Read(Stream Source)
        {
            byte[] Header = new byte[4];
            if (ReadExactly(Source, Header) == false)
                return (null);

            int Length = BitConverter.ToInt32(Header, 0);
            if ((Length <= 0) || (Length > MaxFrameBytes))
                throw new InvalidDataException("Bad frame length " + Length);

            byte[] Json = new byte[Length];
            if (ReadExactly(Source, Json) == false)
                return (null);

            return (JsonSerializer.Deserialize<ServiceFrame>(Json));
        }

        private static bool ReadExactly(Stream Source, byte[] Buffer)
        {
            int Done = 0;
            while (Done < Buffer.Length)
            {
                int Got = Source.Read(Buffer, Done, Buffer.Length - Done);
                if (Got <= 0)
                    return (false);
                Done += Got;
            }
            return (true);
        }
    }
}
