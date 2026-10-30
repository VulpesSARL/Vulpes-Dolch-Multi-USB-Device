namespace VulpesDolch.Host.Common.Media
{
    /// <summary>Fixed CD sector geometry (Red/Yellow Book).</summary>
    public static class CdSector
    {
        /// <summary>A full sector as stored on the disc: audio PCM, or sync + header + data + EDC/ECC.</summary>
        public const int RawSize = 2352;

        /// <summary>User data of a Mode 1 or Mode 2 Form 1 sector (also every DVD/BD sector).</summary>
        public const int UserDataSize = 2048;

        /// <summary>User data of a Mode 2 Form 2 sector.</summary>
        public const int Form2UserDataSize = 2324;

        /// <summary>Mode 2 sector without sync and header (subheader + data + EDC/ECC), as stored in MODE2/2336 images.</summary>
        public const int Mode2HeaderlessSize = 2336;

        public const int FramesPerSecond = 75;

        /// <summary>The disc's 2 second lead-in offset: LBA 0 is absolute time 00:02:00.</summary>
        public const int LeadInFrames = 150;
    }

    /// <summary>What a sector read returns.</summary>
    public enum CdSectorReadMode
    {
        /// <summary>Cooked user data: 2048 bytes (2324 for Mode 2 Form 2). Data tracks only.</summary>
        UserData,

        /// <summary>The whole 2352 byte sector. Audio tracks: PCM. Data tracks: only if the image stores raw sectors.</summary>
        Raw
    }
}
