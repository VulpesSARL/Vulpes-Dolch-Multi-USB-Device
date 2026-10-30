using System;

namespace VulpesDolch.Host.Common.Media
{
    /// <summary>What went wrong, for the errors a sector read can raise.</summary>
    public enum CdImageErrorKind
    {
        /// <summary>Unsupported, malformed or missing image (raised while opening).</summary>
        General,

        /// <summary>The LBA is outside the disc.</summary>
        OutOfRange,

        /// <summary>The requested sector format does not exist for that track (user data of an audio track, raw sectors of a cooked image).</summary>
        BadFormat,

        /// <summary>The image file could not be read (shorter than expected, I/O error).</summary>
        ReadError
    }

    /// <summary>An image file is unsupported, malformed, missing, or a read request is invalid for it.</summary>
    public sealed class CdImageException : Exception
    {
        public CdImageException(string Message) : base(Message)
        {
            Kind = CdImageErrorKind.General;
        }

        public CdImageException(string Message, CdImageErrorKind Kind) : base(Message)
        {
            this.Kind = Kind;
        }

        public CdImageErrorKind Kind { get; }
    }
}
