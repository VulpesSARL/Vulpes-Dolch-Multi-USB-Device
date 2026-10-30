using System;
using System.Threading;

namespace VulpesDolch.Host.Common
{
    /// <summary>
    /// Keeps a second copy of the Host tool from talking to the Host device at the same time. A machine-wide named mutex: the "Global\" prefix makes it visible across user sessions on Windows
    /// and across processes on Linux. The mutex belongs to the thread that took it, so acquire and dispose it on the
    /// same thread.
    /// </summary>
    public sealed class SingleInstanceLock : IDisposable
    {
        /// <summary>The mutex name; part of the protocol contract between all copies of the tool.</summary>
        public const string MutexName = @"Global\VulpesDolch-Host-Tool";

        private Mutex LockMutex;
        private bool Owned;

        private SingleInstanceLock(Mutex LockMutex, bool Owned)
        {
            this.LockMutex = LockMutex;
            this.Owned = Owned;
        }

        /// <summary>
        /// Tries to take the lock without waiting.
        /// </summary>
        /// <returns>The lock, or null if another copy of the tool already holds it.</returns>
        public static SingleInstanceLock TryAcquire()
        {
            Mutex Candidate;
            try
            {
                Candidate = new Mutex(false, MutexName);
            }
            catch (UnauthorizedAccessException)
            {
                // The mutex exists and belongs to another account's session that did not share it: someone else runs it.
                return (null);
            }

            bool Acquired;
            try
            {
                Acquired = Candidate.WaitOne(0);
            }
            catch (AbandonedMutexException)
            {
                // The previous owner died without releasing it; the lock is ours now.
                Acquired = true;
            }

            if (Acquired == false)
            {
                Candidate.Dispose();
                return (null);
            }

            return (new SingleInstanceLock(Candidate, true));
        }

        public void Dispose()
        {
            if (LockMutex == null)
                return;

            if (Owned == true)
                LockMutex.ReleaseMutex();
            LockMutex.Dispose();
            LockMutex = null;
            Owned = false;
        }
    }
}
