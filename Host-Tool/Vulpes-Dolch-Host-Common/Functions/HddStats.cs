using System;
using System.Diagnostics;

namespace VulpesDolch.Host.Common.Functions
{
    /// <summary>
    /// Where the PC side spends its time while it serves the Client's HDD requests, summed over a measuring window (see
    /// <see cref="HddFunction.TakeStats"/>). Times are Stopwatch ticks, shown in milliseconds by the properties.
    /// </summary>
    public sealed class HddStats
    {
        internal long ReadRequestCount;
        internal long ReadSectorCount;
        internal long ReadDiskTicks;
        internal long ReadSendTicks;
        internal long ReadTotalTicks;

        internal long WriteRequestCount;
        internal long WriteSectorCount;
        internal long WritePieceCount;
        internal long WriteDiskTicks;
        internal long WriteCompleteTicks;

        internal long QueueTicks;
        internal long QueueSamples;

        /// <summary>Length of the window in milliseconds.</summary>
        public double WindowMs { get; internal set; }

        public long ReadRequests
        {
            get
            {
                return (ReadRequestCount);
            }
        }

        public long WriteRequests
        {
            get
            {
                return (WriteRequestCount);
            }
        }

        public long ReadSectors
        {
            get
            {
                return (ReadSectorCount);
            }
        }

        public long WriteSectors
        {
            get
            {
                return (WriteSectorCount);
            }
        }

        /// <summary>Total time of the reads on the PC: disk read + sending the data + the rest, per request.</summary>
        public double ReadTotalMsPerRequest
        {
            get
            {
                return (PerRequest(ReadTotalTicks, ReadRequestCount));
            }
        }

        public double ReadDiskMsPerRequest
        {
            get
            {
                return (PerRequest(ReadDiskTicks, ReadRequestCount));
            }
        }

        public double ReadSendMsPerRequest
        {
            get
            {
                return (PerRequest(ReadSendTicks, ReadRequestCount));
            }
        }

        /// <summary>Disk time of the write pieces of one request.</summary>
        public double WriteDiskMsPerRequest
        {
            get
            {
                return (PerRequest(WriteDiskTicks, WriteRequestCount));
            }
        }

        /// <summary>Time to send the COMPLETE of one write request.</summary>
        public double WriteCompleteMsPerRequest
        {
            get
            {
                return (PerRequest(WriteCompleteTicks, WriteRequestCount));
            }
        }

        /// <summary>Average wait of a request between the link handing it over and the worker starting on it.</summary>
        public double QueueMsPerMessage
        {
            get
            {
                return (PerRequest(QueueTicks, QueueSamples));
            }
        }

        private static double PerRequest(long Ticks, long Count)
        {
            if (Count == 0)
                return (0.0);

            return (Ticks * 1000.0 / Stopwatch.Frequency / Count);
        }
    }
}
