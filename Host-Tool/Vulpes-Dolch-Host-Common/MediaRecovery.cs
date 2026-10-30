using System;
using System.Collections.Generic;

namespace VulpesDolch.Host.Common
{
    /// <summary>Where a program that restarts keeps what it had in the drives: only the image paths and the read-only flag of the disk.</summary>
    public interface ISavedMediaState
    {
        void Load(out string CdPath, out string DiskPath, out bool DiskReadOnly);

        void Save(string CdPath, string DiskPath, bool DiskReadOnly);
    }

    /// <summary>
    /// The decision part of the recovery after a restart of the service (or of the whole PC) while the Client still holds media: which image is the one the
    /// Client has? Every candidate path (what the service saved, what the Client kept in its RAM) is checked by the media id, which the Client reports and which
    /// is computed from the file (path, size, time): only a path whose id is the Client's id is the same image. No match = the caller force-ejects.
    /// </summary>
    public static class MediaRecovery
    {
        /// <param name="Candidates">Paths to try, best guess first; null and empty entries and repeats are skipped.</param>
        /// <param name="ClientMediaId">The id the Client reports for its medium.</param>
        /// <param name="MediaIdOf">The id of the image at a path; throws when the file cannot be opened.</param>
        /// <returns>The first path that is the Client's image, or null.</returns>
        public static string FindMatch(IEnumerable<string> Candidates, Int64 ClientMediaId, Func<string, Int64> MediaIdOf)
        {
            List<string> Tried = new List<string>();
            foreach (string Candidate in Candidates)
            {
                if (string.IsNullOrEmpty(Candidate) == true)
                    continue;
                if (Tried.Contains(Candidate) == true)
                    continue;
                Tried.Add(Candidate);

                try
                {
                    if (MediaIdOf(Candidate) == ClientMediaId)
                        return (Candidate);
                }
                catch (Exception)
                {
                    // Missing, unreadable or not an image any more: not this one.
                }
            }
            return (null);
        }
    }
}
