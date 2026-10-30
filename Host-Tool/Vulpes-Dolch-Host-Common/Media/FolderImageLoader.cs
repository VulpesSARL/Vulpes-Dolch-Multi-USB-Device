using System;
using System.IO;

namespace VulpesDolch.Host.Common.Media
{
    /// <summary>Mounts a folder as a data disc: the index (ISO 9660 + Joliet) is built from the folder, the content of the files stays on the disk.</summary>
    internal static class FolderImageLoader
    {
        internal static CdImage Load(string FolderPath)
        {
            return (Load(FolderPath, new FolderDiscOptions()));
        }

        internal static CdImage Load(string FolderPath, FolderDiscOptions Options)
        {
            string Trimmed = FolderPath;
            if ((Trimmed.Length > 3) && ((Trimmed.EndsWith("\\", StringComparison.Ordinal) == true) || (Trimmed.EndsWith("/", StringComparison.Ordinal) == true)))
                Trimmed = Trimmed.TrimEnd('\\', '/');

            FolderDisc Disc = FolderDisc.Build(Trimmed, Options);
            return (CdImage.CreateVirtual(Trimmed, (Disc.IsUdf == true) ? "Folder (UDF)" : "Folder (ISO 9660 + Joliet)", Disc, Disc.TotalSectors, Disc.ContentId));
        }
    }
}