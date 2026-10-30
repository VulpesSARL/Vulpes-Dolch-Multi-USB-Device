using System;

namespace VulpesDolch.Host.Common.Service
{
    public enum NetworkAccess
    {
        /// <summary>The file can be opened for reading.</summary>
        Ok = 0,

        /// <summary>Access denied or logon failure: other credentials may help.</summary>
        NeedCredentials = 1,

        /// <summary>Missing, unreachable or anything else that credentials will not fix.</summary>
        Failed = 2,
    }

    /// <summary>The part of the service that talks to the network (Windows: WNetAddConnection2); the engine only knows this interface.</summary>
    public interface INetworkConnector
    {
        /// <summary>Tries to open the file with the service's own account.</summary>
        NetworkAccess CheckAccess(string Path);

        /// <summary>Connects the share with the given account until <see cref="Disconnect"/>; false (with a text) when the share refuses it.</summary>
        bool Connect(string Share, string User, string Password, out string Error);

        void Disconnect(string Share);
    }

    /// <summary>UNC path helpers.</summary>
    public static class UncPath
    {
        public static bool IsUnc(string Path)
        {
            return (ShareOf(Path) != null);
        }

        /// <summary>The share (\\server\share) of a UNC path, also from the \\?\UNC\server\share\... form; null for anything else.</summary>
        public static string ShareOf(string Path)
        {
            if (string.IsNullOrEmpty(Path) == true)
                return (null);

            string Rest;
            if (Path.StartsWith(@"\\?\UNC\", StringComparison.OrdinalIgnoreCase) == true)
                Rest = Path.Substring(8);
            else if ((Path.StartsWith(@"\\", StringComparison.Ordinal) == true) && (Path.StartsWith(@"\\?\", StringComparison.Ordinal) == false) &&
                (Path.StartsWith(@"\\.\", StringComparison.Ordinal) == false))
                Rest = Path.Substring(2);
            else
                return (null);

            string[] Parts = Rest.Split('\\');
            if ((Parts.Length < 2) || (Parts[0].Length == 0) || (Parts[1].Length == 0))
                return (null);

            return (@"\\" + Parts[0] + @"\" + Parts[1]);
        }
    }
}
