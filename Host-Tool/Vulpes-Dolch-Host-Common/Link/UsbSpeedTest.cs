using System;
using System.Buffers.Binary;
using System.Diagnostics;
using System.Threading;
using VulpesDolch.Host.Common.Protocol;

namespace VulpesDolch.Host.Common.Link
{
    /// <summary>Result of <see cref="UsbSpeedTest.Run"/>: the speed of the USB link between the PC and the Host device alone (no GPIF, no Client).</summary>
    public sealed class UsbSpeedResult
    {
        /// <summary>PC -> Host in MB/s (1 MB = 1048576 bytes); 0 when the Host did not confirm in time.</summary>
        public double SinkMegabytesPerSecond { get; set; }

        /// <summary>Host -> PC in MB/s.</summary>
        public double SourceMegabytesPerSecond { get; set; }

        public int SinkCounted { get; set; }

        public int SourceReceived { get; set; }
    }

    /// <summary>
    /// Measures the bulk link to the Host device with the debug messages of the Host firmware (CY_FX_DEBUG_COMMANDS): messages of the
    /// given size go to the Host, which counts and drops them, and the Host sends messages of that size back. The Client is not involved.
    /// </summary>
    public static class UsbSpeedTest
    {
        public static UsbSpeedResult Run(HostSession Session, int Count, int Size, int TimeoutMs)
        {
            UsbSpeedResult Result = new UsbSpeedResult();
            ManualResetEventSlim SinkEnded = new ManualResetEventSlim(false);
            ManualResetEventSlim SourceDone = new ManualResetEventSlim(false);
            int Received = 0;
            int Counted = 0;

            Action<Message> Handler = delegate (Message Seen)
            {
                if (Seen.Type == ControlType.DebugSinkEnd)
                {
                    if (Seen.Payload.Length >= 8)
                        Counted = (int)BinaryPrimitives.ReadUInt32LittleEndian(Seen.Payload.AsSpan(0, 4));
                    SinkEnded.Set();
                }
                else if (Seen.Type == ControlType.DebugSource)
                {
                    if (Interlocked.Increment(ref Received) == Count)
                        SourceDone.Set();
                }
            };

            Session.DebugMessageReceived += Handler;
            try
            {
                byte[] Payload = new byte[Size];
                Message Data = ControlMessages.CreateDebugSinkData(0, Payload);

                // PC -> Host: send everything, then ask how many arrived.
                Stopwatch Watch = Stopwatch.StartNew();
                for (int Index = 0; Index < Count; Index++)
                {
                    if (Session.Link.Send(Data, TimeoutMs) != LinkSendResult.Ok)
                        return (Result);
                }
                Session.Link.Send(ControlMessages.CreateDebugSinkEnd(0), TimeoutMs);
                if (SinkEnded.Wait(TimeoutMs) == true)
                {
                    Result.SinkCounted = Counted;
                    Result.SinkMegabytesPerSecond = ((double)Count * Size) / 1048576.0 / Watch.Elapsed.TotalSeconds;
                }

                // Host -> PC: ask for Count messages of Size bytes and wait for all of them.
                Watch.Restart();
                Session.Link.Send(ControlMessages.CreateDebugSource(0, (uint)Count, (uint)Size), TimeoutMs);
                SourceDone.Wait(TimeoutMs);
                double Seconds = Watch.Elapsed.TotalSeconds;
                Result.SourceReceived = Volatile.Read(ref Received);
                if (Result.SourceReceived > 0)
                    Result.SourceMegabytesPerSecond = ((double)Result.SourceReceived * Size) / 1048576.0 / Seconds;
            }
            finally
            {
                Session.DebugMessageReceived -= Handler;
            }

            return (Result);
        }
    }
}
