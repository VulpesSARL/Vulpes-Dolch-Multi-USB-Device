using System;
using System.IO;
using System.Text;
using VulpesDolch.Host.Common.Protocol;

namespace VulpesDolch.Host.Common.Media
{
    /// <summary>
    /// A disc image prepared for the Client: the opened image, the MediaId the Client will report back, and the MEDIA_INSERT
    /// message data (profile, blobs). Owns the image.
    /// </summary>
    public sealed class CdRomMedia : IDisposable
    {
        private CdRomMedia(CdImage Image, Int64 MediaId, ushort Profile)
        {
            this.Image = Image;
            this.MediaId = MediaId;
            this.Profile = Profile;
        }

        public CdImage Image { get; }

        /// <summary>Opaque 64 bit number identifying this image to the Client (a hash of the path, size and modification time).</summary>
        public Int64 MediaId { get; }

        public ushort Profile { get; }

        public string ImagePath
        {
            get { return (Image.ImagePath); }
        }

        /// <summary>Opens an image for insertion. Throws <see cref="CdImageException"/> if it cannot be used.</summary>
        public static CdRomMedia Open(string ImagePath)
        {
            CdImage Opened = CdImage.Open(ImagePath);
            Int64 Id = (Opened.ContentId != 0) ? Opened.ContentId : ComputeMediaId(ImagePath);
            return (new CdRomMedia(Opened, Id, CdTocBuilder.ChooseProfile(Opened)));
        }

        /// <summary>The same number for the same file in the same state, so the PC can recognise its image after it was away.</summary>
        public static Int64 ComputeMediaId(string ImagePath)
        {
            string FullPath = Path.GetFullPath(ImagePath).ToLowerInvariant();
            FileInfo Info = new FileInfo(FullPath);

            // FNV-1a over the path and the size and time of the file (the .cue sheet itself for BIN/CUE images).
            UInt64 Hash = 14695981039346656037UL;
            Hash = Mix(Hash, Encoding.Unicode.GetBytes(FullPath));
            Hash = Mix(Hash, BitConverter.GetBytes((Info.Exists == true) ? Info.Length : 0L));
            Hash = Mix(Hash, BitConverter.GetBytes((Info.Exists == true) ? Info.LastWriteTimeUtc.Ticks : 0L));

            if (Hash == 0)
                Hash = 1;
            return (unchecked((Int64)Hash));
        }

        /// <summary>The MEDIA_INSERT payload data for this image.</summary>
        public CdRomInsertInfo CreateInsertInfo(bool Reattach)
        {
            CdRomInsertInfo Info = new CdRomInsertInfo();
            Info.MediaId = MediaId;
            Info.Profile = Profile;
            Info.DiscType = (byte)Image.DiscType;
            Info.Flags = (Reattach == true) ? CdRomInsertInfo.FlagReattach : (byte)0;
            Info.LeadOutLba = (uint)Image.LeadOutLba;
            Info.Blobs.AddRange(CdTocBuilder.BuildBlobs(Image));
            return (Info);
        }

        public void Dispose()
        {
            Image.Dispose();
        }

        private static UInt64 Mix(UInt64 Hash, byte[] Data)
        {
            foreach (byte Value in Data)
            {
                Hash ^= Value;
                Hash *= 1099511628211UL;
            }
            return (Hash);
        }
    }
}
