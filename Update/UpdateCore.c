#include "cyu3system.h"
#include "cyu3os.h"
#include "cyu3error.h"
#include "cyu3i2c.h"
#include "UpdateCore.h"
#include "UpdateKey.h"

static const uint8_t SigningKey[32] = UPDATE_KEY_BYTES;

/* ---- SHA-256 and HMAC-SHA256 --------------------------------------------------------------------------------------------- */

typedef struct
{
    uint32_t State[8];
    uint32_t Length;
    uint8_t  Buffer[64];
    uint32_t Fill;
} Sha256_t;

typedef struct
{
    Sha256_t Inner;
    uint8_t  OuterPad[64];
} Hmac_t;

static const uint32_t RoundConstants[64] =
{
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2
};

#define ROTATE_RIGHT(Value, Count) (((Value) >> (Count)) | ((Value) << (32 - (Count))))

static void Sha256Block (Sha256_t *Context, const uint8_t *Data)
{
    uint32_t W[64];
    uint32_t A, B, C, D, E, F, G, H;
    uint32_t Sum0, Sum1, Choose, Majority, Temp1, Temp2;
    uint32_t Index;

    for (Index = 0; Index < 16; Index++)
        W[Index] = ((uint32_t)Data[Index * 4] << 24) | ((uint32_t)Data[Index * 4 + 1] << 16) | ((uint32_t)Data[Index * 4 + 2] << 8) | (uint32_t)Data[Index * 4 + 3];

    for (Index = 16; Index < 64; Index++)
    {
        Sum0 = ROTATE_RIGHT (W[Index - 15], 7) ^ ROTATE_RIGHT (W[Index - 15], 18) ^ (W[Index - 15] >> 3);
        Sum1 = ROTATE_RIGHT (W[Index - 2], 17) ^ ROTATE_RIGHT (W[Index - 2], 19) ^ (W[Index - 2] >> 10);
        W[Index] = W[Index - 16] + Sum0 + W[Index - 7] + Sum1;
    }

    A = Context->State[0];
    B = Context->State[1];
    C = Context->State[2];
    D = Context->State[3];
    E = Context->State[4];
    F = Context->State[5];
    G = Context->State[6];
    H = Context->State[7];

    for (Index = 0; Index < 64; Index++)
    {
        Sum1 = ROTATE_RIGHT (E, 6) ^ ROTATE_RIGHT (E, 11) ^ ROTATE_RIGHT (E, 25);
        Choose = (E & F) ^ ((~E) & G);
        Temp1 = H + Sum1 + Choose + RoundConstants[Index] + W[Index];
        Sum0 = ROTATE_RIGHT (A, 2) ^ ROTATE_RIGHT (A, 13) ^ ROTATE_RIGHT (A, 22);
        Majority = (A & B) ^ (A & C) ^ (B & C);
        Temp2 = Sum0 + Majority;

        H = G;
        G = F;
        F = E;
        E = D + Temp1;
        D = C;
        C = B;
        B = A;
        A = Temp1 + Temp2;
    }

    Context->State[0] += A;
    Context->State[1] += B;
    Context->State[2] += C;
    Context->State[3] += D;
    Context->State[4] += E;
    Context->State[5] += F;
    Context->State[6] += G;
    Context->State[7] += H;
}

static void Sha256Init (Sha256_t *Context)
{
    Context->State[0] = 0x6a09e667;
    Context->State[1] = 0xbb67ae85;
    Context->State[2] = 0x3c6ef372;
    Context->State[3] = 0xa54ff53a;
    Context->State[4] = 0x510e527f;
    Context->State[5] = 0x9b05688c;
    Context->State[6] = 0x1f83d9ab;
    Context->State[7] = 0x5be0cd19;
    Context->Length = 0;
    Context->Fill = 0;
}

static void Sha256Update (Sha256_t *Context, const uint8_t *Data, uint32_t Count)
{
    uint32_t Index;

    for (Index = 0; Index < Count; Index++)
    {
        Context->Buffer[Context->Fill++] = Data[Index];
        if (Context->Fill == 64)
        {
            Sha256Block (Context, Context->Buffer);
            Context->Fill = 0;
        }
    }
    Context->Length += Count;
}

static void Sha256Final (Sha256_t *Context, uint8_t *Digest)
{
    uint32_t BitsHigh = Context->Length >> 29;
    uint32_t BitsLow = Context->Length << 3;
    uint8_t Padding = 0x80;
    uint8_t Zero = 0;
    uint8_t Tail[8];
    uint32_t Index;

    Sha256Update (Context, &Padding, 1);
    while (Context->Fill != 56)
        Sha256Update (Context, &Zero, 1);

    Tail[0] = (uint8_t)(BitsHigh >> 24);
    Tail[1] = (uint8_t)(BitsHigh >> 16);
    Tail[2] = (uint8_t)(BitsHigh >> 8);
    Tail[3] = (uint8_t)BitsHigh;
    Tail[4] = (uint8_t)(BitsLow >> 24);
    Tail[5] = (uint8_t)(BitsLow >> 16);
    Tail[6] = (uint8_t)(BitsLow >> 8);
    Tail[7] = (uint8_t)BitsLow;
    Sha256Update (Context, Tail, 8);

    for (Index = 0; Index < 8; Index++)
    {
        Digest[Index * 4] = (uint8_t)(Context->State[Index] >> 24);
        Digest[Index * 4 + 1] = (uint8_t)(Context->State[Index] >> 16);
        Digest[Index * 4 + 2] = (uint8_t)(Context->State[Index] >> 8);
        Digest[Index * 4 + 3] = (uint8_t)Context->State[Index];
    }
}

/* Key of up to 64 bytes. */
static void HmacInit (Hmac_t *Context, const uint8_t *Key, uint32_t KeyLength)
{
    uint8_t Pad[64];
    uint32_t Index;

    for (Index = 0; Index < 64; Index++)
    {
        Pad[Index] = (Index < KeyLength) ? Key[Index] : 0;
        Context->OuterPad[Index] = Pad[Index] ^ 0x5C;
        Pad[Index] ^= 0x36;
    }

    Sha256Init (&Context->Inner);
    Sha256Update (&Context->Inner, Pad, 64);
}

static void HmacUpdate (Hmac_t *Context, const uint8_t *Data, uint32_t Count)
{
    Sha256Update (&Context->Inner, Data, Count);
}

static void HmacFinal (Hmac_t *Context, uint8_t *Mac)
{
    Sha256_t Outer;
    uint8_t InnerDigest[32];

    Sha256Final (&Context->Inner, InnerDigest);
    Sha256Init (&Outer);
    Sha256Update (&Outer, Context->OuterPad, 64);
    Sha256Update (&Outer, InnerDigest, 32);
    Sha256Final (&Outer, Mac);
}

CyBool_t UpdateSelfTest (void)
{
    static const uint8_t Abc[3] = { 'a', 'b', 'c' };
    static const uint8_t AbcDigest[32] =
    {
        0xba, 0x78, 0x16, 0xbf, 0x8f, 0x01, 0xcf, 0xea, 0x41, 0x41, 0x40, 0xde, 0x5d, 0xae, 0x22, 0x23,
        0xb0, 0x03, 0x61, 0xa3, 0x96, 0x17, 0x7a, 0x9c, 0xb4, 0x10, 0xff, 0x61, 0xf2, 0x00, 0x15, 0xad
    };
    static const uint8_t HiThere[8] = { 'H', 'i', ' ', 'T', 'h', 'e', 'r', 'e' };
    static const uint8_t HiThereMac[32] =
    {
        0xb0, 0x34, 0x4c, 0x61, 0xd8, 0xdb, 0x38, 0x53, 0x5c, 0xa8, 0xaf, 0xce, 0xaf, 0x0b, 0xf1, 0x2b,
        0x88, 0x1d, 0xc2, 0x00, 0xc9, 0x83, 0x3d, 0xa7, 0x26, 0xe9, 0x37, 0x6c, 0x2e, 0x32, 0xcf, 0xf7
    };
    uint8_t Key[20];
    uint8_t Output[32];
    Sha256_t Sha;
    Hmac_t Hmac;
    uint32_t Index;

    Sha256Init (&Sha);
    Sha256Update (&Sha, Abc, 3);
    Sha256Final (&Sha, Output);
    for (Index = 0; Index < 32; Index++)
        if (Output[Index] != AbcDigest[Index])
            return (CyFalse);

    for (Index = 0; Index < 20; Index++)
        Key[Index] = 0x0B;
    HmacInit (&Hmac, Key, 20);
    HmacUpdate (&Hmac, HiThere, 8);
    HmacFinal (&Hmac, Output);
    for (Index = 0; Index < 32; Index++)
        if (Output[Index] != HiThereMac[Index])
            return (CyFalse);

    return (CyTrue);
}

/* ---- CRC-32 (the usual one: reflected, polynomial 0xEDB88320) --------------------------------------------------------------- */

static uint32_t CrcTable[16];
static CyBool_t CrcReady = CyFalse;

static uint32_t Crc32Update (uint32_t Crc, const uint8_t *Data, uint32_t Count)
{
    uint32_t Index;
    uint32_t Bit;
    uint32_t Value;

    if (CrcReady == CyFalse)
    {
        for (Index = 0; Index < 16; Index++)
        {
            Value = Index;
            for (Bit = 0; Bit < 4; Bit++)
                Value = ((Value & 1) != 0) ? ((Value >> 1) ^ 0xEDB88320u) : (Value >> 1);
            CrcTable[Index] = Value;
        }
        CrcReady = CyTrue;
    }

    Crc = ~Crc;
    for (Index = 0; Index < Count; Index++)
    {
        Crc ^= Data[Index];
        Crc = (Crc >> 4) ^ CrcTable[Crc & 15];
        Crc = (Crc >> 4) ^ CrcTable[Crc & 15];
    }
    return (~Crc);
}

/* ---- The received file ------------------------------------------------------------------------------------------------------ */

static uint8_t FileByte (const UpdateFile_t *File, uint32_t Index)
{
    return (File->Chunk[Index / UPDATE_CHUNK_BYTES][Index % UPDATE_CHUNK_BYTES]);
}

static uint32_t LoadWord (const uint8_t *Data)
{
    return ((uint32_t)Data[0] | ((uint32_t)Data[1] << 8) | ((uint32_t)Data[2] << 16) | ((uint32_t)Data[3] << 24));
}

static void HmacFeedFile (Hmac_t *Context, const UpdateFile_t *File, uint32_t Start, uint32_t End)
{
    uint32_t Piece;

    while (Start < End)
    {
        Piece = UPDATE_CHUNK_BYTES - (Start % UPDATE_CHUNK_BYTES);
        if (Piece > (End - Start))
            Piece = End - Start;
        HmacUpdate (Context, &File->Chunk[Start / UPDATE_CHUNK_BYTES][Start % UPDATE_CHUNK_BYTES], Piece);
        Start += Piece;
    }
}

uint32_t UpdateVerifyFile (const UpdateFile_t *File, uint16_t Target, UpdateHeader_t *Header)
{
    uint8_t Raw[UPDATE_HEADER_BYTES];
    uint8_t Mac[32];
    Hmac_t Hmac;
    uint32_t Index;
    uint32_t Difference = 0;

    if ((File->FileLength <= UPDATE_HEADER_BYTES) || (File->FileLength > (UPDATE_MAX_CHUNKS * UPDATE_CHUNK_BYTES)))
        return (UPDATE_ERR_LENGTH);

    for (Index = 0; Index < UPDATE_HEADER_BYTES; Index++)
        Raw[Index] = FileByte (File, Index);

    Header->Magic = LoadWord (Raw);
    Header->FormatVersion = (uint16_t)(Raw[4] | (Raw[5] << 8));
    Header->Target = (uint16_t)(Raw[6] | (Raw[7] << 8));
    Header->ImageVersion = LoadWord (Raw + 8);
    Header->Flags = LoadWord (Raw + 12);
    Header->PayloadLength = LoadWord (Raw + 16);
    Header->ImageLength = LoadWord (Raw + 20);
    Header->ImageCrc = LoadWord (Raw + 24);
    for (Index = 0; Index < 32; Index++)
        Header->Mac[Index] = Raw[32 + Index];

    if ((Header->Magic != UPDATE_MAGIC) || (Header->FormatVersion != UPDATE_FORMAT_VERSION) || (LoadWord (Raw + 28) != 0) ||
        ((Header->Flags & ~UPDATE_FLAG_LZSS) != 0) || (Header->PayloadLength != (File->FileLength - UPDATE_HEADER_BYTES)))
        return (UPDATE_ERR_FORMAT);

    /* The signature first: nothing else in the header is trusted before it matches. */
    HmacInit (&Hmac, SigningKey, sizeof (SigningKey));
    HmacUpdate (&Hmac, Raw, 32);
    HmacFeedFile (&Hmac, File, UPDATE_HEADER_BYTES, File->FileLength);
    HmacFinal (&Hmac, Mac);
    for (Index = 0; Index < 32; Index++)
        Difference |= (uint32_t)(Mac[Index] ^ Header->Mac[Index]);
    if (Difference != 0)
        return (UPDATE_ERR_SIGNATURE);

    if (Header->Target != Target)
        return (UPDATE_ERR_TARGET);

    if ((Header->ImageLength < 512) || (Header->ImageLength > UPDATE_EEPROM_BYTES) ||
        (((Header->Flags & UPDATE_FLAG_LZSS) == 0) && (Header->PayloadLength != Header->ImageLength)))
        return (UPDATE_ERR_LENGTH);

    return (UPDATE_OK);
}

/* ---- Reading the image out of the payload ----------------------------------------------------------------------------------- */

#define LZSS_WINDOW_BYTES   (4096u)
#define LZSS_WINDOW_MASK    (LZSS_WINDOW_BYTES - 1)

typedef struct
{
    const UpdateFile_t *File;
    uint32_t            PayloadLength;
    uint32_t            SourcePosition;
    uint32_t            Produced;
    uint32_t            ImageLength;
    CyBool_t            Compressed;
    CyBool_t            Failed;
    uint8_t             Flags;
    uint8_t             BitIndex;
    uint16_t            PendingOffset;
    uint16_t            PendingLength;
    uint8_t             Window[LZSS_WINDOW_BYTES];
} ImageReader_t;

static ImageReader_t Reader;

static void ReaderInit (const UpdateFile_t *File, const UpdateHeader_t *Header)
{
    Reader.File = File;
    Reader.PayloadLength = Header->PayloadLength;
    Reader.SourcePosition = 0;
    Reader.Produced = 0;
    Reader.ImageLength = Header->ImageLength;
    Reader.Compressed = ((Header->Flags & UPDATE_FLAG_LZSS) != 0) ? CyTrue : CyFalse;
    Reader.Failed = CyFalse;
    Reader.BitIndex = 8;
    Reader.PendingLength = 0;
}

static uint8_t ReaderSourceByte (void)
{
    uint8_t Value = FileByte (Reader.File, UPDATE_HEADER_BYTES + Reader.SourcePosition);

    Reader.SourcePosition++;
    return (Value);
}

/* Next Count bytes of the image into Output; returns how many were produced (less than Count at the end of the image, or on a damaged stream). */
static uint32_t ReaderProduce (uint8_t *Output, uint32_t Count)
{
    uint32_t Written = 0;
    uint8_t Value;
    uint8_t First;
    uint8_t Second;

    while ((Written < Count) && (Reader.Produced < Reader.ImageLength) && (Reader.Failed == CyFalse))
    {
        if (Reader.Compressed == CyFalse)
        {
            if (Reader.SourcePosition >= Reader.PayloadLength)
            {
                Reader.Failed = CyTrue;
                break;
            }
            Value = ReaderSourceByte ();
        }
        else if (Reader.PendingLength > 0)
        {
            Value = Reader.Window[(Reader.Produced - Reader.PendingOffset) & LZSS_WINDOW_MASK];
            Reader.PendingLength--;
        }
        else
        {
            if (Reader.BitIndex >= 8)
            {
                if (Reader.SourcePosition >= Reader.PayloadLength)
                {
                    Reader.Failed = CyTrue;
                    break;
                }
                Reader.Flags = ReaderSourceByte ();
                Reader.BitIndex = 0;
            }

            if ((Reader.Flags & (1 << Reader.BitIndex)) != 0)
            {
                if (Reader.SourcePosition >= Reader.PayloadLength)
                {
                    Reader.Failed = CyTrue;
                    break;
                }
                Value = ReaderSourceByte ();
            }
            else
            {
                if ((Reader.SourcePosition + 1) >= Reader.PayloadLength)
                {
                    Reader.Failed = CyTrue;
                    break;
                }
                First = ReaderSourceByte ();
                Second = ReaderSourceByte ();
                Reader.PendingOffset = (uint16_t)(First | ((Second >> 4) << 8));
                Reader.PendingLength = (uint16_t)((Second & 0x0F) + 3);
                if ((Reader.PendingOffset == 0) || (Reader.PendingOffset > Reader.Produced))
                {
                    Reader.Failed = CyTrue;
                    break;
                }
                Value = Reader.Window[(Reader.Produced - Reader.PendingOffset) & LZSS_WINDOW_MASK];
                Reader.PendingLength--;
            }
            Reader.BitIndex++;
        }

        Output[Written] = Value;
        Reader.Window[Reader.Produced & LZSS_WINDOW_MASK] = Value;
        Reader.Produced++;
        Written++;
    }

    return (Written);
}

/* ---- The EEPROM ------------------------------------------------------------------------------------------------------------- */

static uint8_t CurrentPage[UPDATE_PAGE_BYTES];
static uint8_t CompareBuffer[UPDATE_PAGE_BYTES];

CyBool_t UpdateEepromInit (void)
{
    CyU3PI2cConfig_t Config;
    CyU3PReturnStatus_t Status;

    Status = CyU3PI2cInit ();
    if ((Status != CY_U3P_SUCCESS) && (Status != CY_U3P_ERROR_ALREADY_STARTED))
        return (CyFalse);

    CyU3PMemSet ((uint8_t *)&Config, 0, sizeof (Config));
    Config.bitRate = 400000;
    Config.busTimeout = 0xFFFFFFFF;
    Config.dmaTimeout = 0xFFFF;
    Config.isDma = CyFalse;
    return ((CyU3PI2cSetConfig (&Config, NULL) == CY_U3P_SUCCESS) ? CyTrue : CyFalse);
}

void UpdateEepromDeinit (void)
{
    CyU3PI2cDeInit ();
}

/* The address bits above 16 go into the device select byte, the same way the boot ROM reads a part bigger than 64 KB. */
static uint8_t DeviceSelect (uint32_t Address)
{
    return ((uint8_t)(UPDATE_EEPROM_BASE_ADDRESS | (((Address >> 16) & 0x03) << 1)));
}

static CyU3PReturnStatus_t EepromReadPage (uint32_t Address, uint8_t *Buffer)
{
    CyU3PI2cPreamble_t Preamble;

    Preamble.length = 4;
    Preamble.buffer[0] = DeviceSelect (Address);
    Preamble.buffer[1] = (uint8_t)(Address >> 8);
    Preamble.buffer[2] = (uint8_t)Address;
    Preamble.buffer[3] = (uint8_t)(DeviceSelect (Address) | 0x01);
    Preamble.ctrlMask = 0x0004;
    return (CyU3PI2cReceiveBytes (&Preamble, Buffer, UPDATE_PAGE_BYTES, 0));
}

static volatile CyBool_t WriteEnabled = CyFalse;
static volatile uint32_t PagesWritten = 0;

uint32_t UpdatePagesWritten (void)
{
    return (PagesWritten);
}

void UpdateWriteEnable (CyBool_t Enable)
{
    WriteEnabled = Enable;
}

static CyU3PReturnStatus_t EepromWritePage (uint32_t Address, const uint8_t *Buffer)
{
    CyU3PI2cPreamble_t Preamble;

    /* The one and only place that writes the EEPROM: only in update mode. */
    if (WriteEnabled == CyFalse)
        return (CY_U3P_ERROR_NOT_SUPPORTED);

    Preamble.length = 3;
    Preamble.buffer[0] = DeviceSelect (Address);
    Preamble.buffer[1] = (uint8_t)(Address >> 8);
    Preamble.buffer[2] = (uint8_t)Address;
    Preamble.ctrlMask = 0x0000;
    return (CyU3PI2cTransmitBytes (&Preamble, (uint8_t *)Buffer, UPDATE_PAGE_BYTES, 0));
}

/* Reads a page, trying again for up to 40 ms: the part does not answer while it is still busy with a write. */
static CyBool_t EepromReadWait (uint32_t Address, uint8_t *Buffer)
{
    uint32_t Try;

    /* The I2C transfers poll the hardware without sleeping: let the other threads of the same priority (the GPIF link) run between pages. */
    CyU3PThreadRelinquish ();

    for (Try = 0; Try < 40; Try++)
    {
        if (EepromReadPage (Address, Buffer) == CY_U3P_SUCCESS)
            return (CyTrue);
        CyU3PThreadSleep (1);
    }
    return (CyFalse);
}

static CyBool_t PagesEqual (const uint8_t *First, const uint8_t *Second)
{
    uint32_t Index;

    for (Index = 0; Index < UPDATE_PAGE_BYTES; Index++)
        if (First[Index] != Second[Index])
            return (CyFalse);
    return (CyTrue);
}

/* Makes the page at Address hold Wanted: nothing is written if it already does. */
static uint32_t EnsurePage (uint32_t Address, const uint8_t *Wanted)
{
    uint32_t Attempt;

    CyU3PThreadRelinquish ();

    if (EepromReadWait (Address, CurrentPage) == CyFalse)
        return (UPDATE_ERR_EEPROM);
    if (PagesEqual (CurrentPage, Wanted) == CyTrue)
        return (UPDATE_OK);

    for (Attempt = 0; Attempt < 3; Attempt++)
    {
        PagesWritten++;
        if (EepromWritePage (Address, Wanted) != CY_U3P_SUCCESS)
        {
            CyU3PThreadSleep (10);
            continue;
        }
        if (EepromReadWait (Address, CompareBuffer) == CyFalse)
            return (UPDATE_ERR_EEPROM);
        if (PagesEqual (CompareBuffer, Wanted) == CyTrue)
            return (UPDATE_OK);
    }

    return (UPDATE_ERR_VERIFY);
}

/* The next page of the image; the tail of the last page is padded with 0xFF. Returns CyFalse on a damaged stream. */
static CyBool_t ReaderPage (uint8_t *Page)
{
    uint32_t Got = ReaderProduce (Page, UPDATE_PAGE_BYTES);
    uint32_t Index;

    if (Reader.Failed == CyTrue)
        return (CyFalse);
    if ((Got != UPDATE_PAGE_BYTES) && (Reader.Produced != Reader.ImageLength))
        return (CyFalse);

    for (Index = Got; Index < UPDATE_PAGE_BYTES; Index++)
        Page[Index] = 0xFF;
    return (CyTrue);
}

uint32_t UpdateProgramEeprom (const UpdateFile_t *File, const UpdateHeader_t *Header, UpdateProgressCallback_t Progress)
{
    static uint8_t FirstPage[UPDATE_PAGE_BYTES];
    static uint8_t Page[UPDATE_PAGE_BYTES];
    uint32_t Pages = (Header->ImageLength + UPDATE_PAGE_BYTES - 1) / UPDATE_PAGE_BYTES;
    uint32_t Index;
    uint32_t Result;
    uint32_t Crc = 0;
    uint32_t Remaining;

    /* Decompress everything once without writing: a stream that does not give the signed CRC must not reach the EEPROM at all. */
    ReaderInit (File, Header);
    Remaining = Header->ImageLength;
    for (Index = 0; Index < Pages; Index++)
    {
        if (ReaderPage (Page) == CyFalse)
            return (UPDATE_ERR_DECOMPRESS);
        Crc = Crc32Update (Crc, Page, (Remaining < UPDATE_PAGE_BYTES) ? Remaining : UPDATE_PAGE_BYTES);
        Remaining -= (Remaining < UPDATE_PAGE_BYTES) ? Remaining : UPDATE_PAGE_BYTES;
    }
    if (Crc != Header->ImageCrc)
        return (UPDATE_ERR_IMAGE);

    ReaderInit (File, Header);
    Crc = 0;

    /* Page 0 holds the image header the boot ROM looks at; it is written last. */
    if (ReaderPage (FirstPage) == CyFalse)
        return (UPDATE_ERR_DECOMPRESS);
    if ((FirstPage[0] != 'C') || (FirstPage[1] != 'Y'))
        return (UPDATE_ERR_IMAGE);

    /* Blank page 0 first: from here on until the end the boot ROM finds no image, and falls back to the USB bootloader if the power goes. */
    for (Index = 0; Index < UPDATE_PAGE_BYTES; Index++)
        Page[Index] = 0xFF;
    Result = EnsurePage (0, Page);
    if (Result != UPDATE_OK)
        return (Result);

    for (Index = 1; Index < Pages; Index++)
    {
        if (ReaderPage (Page) == CyFalse)
            return (UPDATE_ERR_DECOMPRESS);
        Result = EnsurePage (Index * UPDATE_PAGE_BYTES, Page);
        if (Result != UPDATE_OK)
            return (Result);
        if ((Progress != NULL) && ((Index % 16) == 0))
            Progress ((Index * 90) / Pages);
    }

    if (Reader.Produced != Header->ImageLength)
        return (UPDATE_ERR_DECOMPRESS);

    Result = EnsurePage (0, FirstPage);
    if (Result != UPDATE_OK)
        return (Result);

    /* Everything back once more, against the CRC in the signed header. */
    Remaining = Header->ImageLength;
    for (Index = 0; Index < Pages; Index++)
    {
        if (EepromReadWait (Index * UPDATE_PAGE_BYTES, Page) == CyFalse)
            return (UPDATE_ERR_EEPROM);
        Crc = Crc32Update (Crc, Page, (Remaining < UPDATE_PAGE_BYTES) ? Remaining : UPDATE_PAGE_BYTES);
        Remaining -= (Remaining < UPDATE_PAGE_BYTES) ? Remaining : UPDATE_PAGE_BYTES;
        if ((Progress != NULL) && ((Index % 32) == 0))
            Progress (90 + (Index * 10) / Pages);
    }

    if (Crc != Header->ImageCrc)
        return (UPDATE_ERR_VERIFY);

    if (Progress != NULL)
        Progress (100);
    return (UPDATE_OK);
}

#ifdef UPDATE_STREAMING        /* Only the Host needs this (and 5 KB of RAM for it). */

/* ---- Streaming update (the Host has no room for the whole file) ----------------------------------------------------------------- */

#define STREAM_PASS_NONE        (0)
#define STREAM_PASS_VERIFY      (1)
#define STREAM_PASS_PROGRAM     (2)

typedef struct
{
    uint32_t Pass;
    uint32_t FileLength;
    uint32_t Fed;
    uint32_t Error;
    uint8_t  HeaderRaw[UPDATE_HEADER_BYTES];
    Hmac_t   Mac;
    UpdateHeader_t Header;
    CyBool_t Compressed;
    uint32_t Produced;
    uint32_t Crc;
    uint32_t PageIndex;
    uint32_t PageFill;
    uint8_t  Flags;
    uint8_t  BitIndex;
    uint8_t  Phase;
    uint8_t  MatchFirst;
    uint8_t  Window[LZSS_WINDOW_BYTES];
    uint8_t  Page[UPDATE_PAGE_BYTES];
    uint8_t  FirstPage[UPDATE_PAGE_BYTES];
} Stream_t;

static Stream_t Stream;
static uint8_t VerifiedHeader[UPDATE_HEADER_BYTES];
static uint32_t VerifiedLength = 0;

#define PHASE_TOKEN     (0)
#define PHASE_MATCH     (1)

static void StreamFlushPage (uint32_t ValidBytes)
{
    uint32_t Index;

    for (Index = ValidBytes; Index < UPDATE_PAGE_BYTES; Index++)
        Stream.Page[Index] = 0xFF;

    Stream.Crc = Crc32Update (Stream.Crc, Stream.Page, ValidBytes);

    if (Stream.PageIndex == 0)
    {
        if ((Stream.Page[0] != 'C') || (Stream.Page[1] != 'Y'))
        {
            Stream.Error = UPDATE_ERR_IMAGE;
            return;
        }
        for (Index = 0; Index < UPDATE_PAGE_BYTES; Index++)
            Stream.FirstPage[Index] = Stream.Page[Index];
    }
    else if (Stream.Pass == STREAM_PASS_PROGRAM)
    {
        Index = EnsurePage (Stream.PageIndex * UPDATE_PAGE_BYTES, Stream.Page);
        if (Index != UPDATE_OK)
            Stream.Error = Index;
    }

    Stream.PageIndex++;
    Stream.PageFill = 0;
}

static void StreamEmit (uint8_t Value)
{
    if (Stream.Produced >= Stream.Header.ImageLength)
        return;

    Stream.Window[Stream.Produced & LZSS_WINDOW_MASK] = Value;
    Stream.Produced++;
    Stream.Page[Stream.PageFill++] = Value;
    if (Stream.PageFill == UPDATE_PAGE_BYTES)
        StreamFlushPage (UPDATE_PAGE_BYTES);
}

static void StreamDecode (uint8_t Byte)
{
    uint32_t Offset;
    uint32_t Length;
    uint32_t Copy;

    if (Stream.Compressed == CyFalse)
    {
        StreamEmit (Byte);
        return;
    }

    if (Stream.Phase == PHASE_MATCH)
    {
        Offset = Stream.MatchFirst | ((uint32_t)(Byte >> 4) << 8);
        Length = (uint32_t)(Byte & 0x0F) + 3;
        Stream.BitIndex++;
        Stream.Phase = PHASE_TOKEN;
        if ((Offset == 0) || (Offset > Stream.Produced))
        {
            Stream.Error = UPDATE_ERR_DECOMPRESS;
            return;
        }
        for (Copy = 0; Copy < Length; Copy++)
            StreamEmit (Stream.Window[(Stream.Produced - Offset) & LZSS_WINDOW_MASK]);
        return;
    }

    if (Stream.BitIndex >= 8)
    {
        Stream.Flags = Byte;
        Stream.BitIndex = 0;
        return;
    }

    if ((Stream.Flags & (1 << Stream.BitIndex)) != 0)
    {
        Stream.BitIndex++;
        StreamEmit (Byte);
    }
    else
    {
        Stream.MatchFirst = Byte;
        Stream.Phase = PHASE_MATCH;
    }
}

/* Pass 1 (verify): nothing is written. Pass 2 (program): the same file again; only valid after pass 1 succeeded with an identical header. */
uint32_t UpdateStreamBegin (uint32_t FileLength, uint32_t Pass)
{
    if ((Pass != STREAM_PASS_VERIFY) && (Pass != STREAM_PASS_PROGRAM))
        return (UPDATE_ERR_STATE);
    if ((FileLength <= UPDATE_HEADER_BYTES) || (FileLength > (UPDATE_EEPROM_BYTES + UPDATE_HEADER_BYTES)))
        return (UPDATE_ERR_LENGTH);
    if ((Pass == STREAM_PASS_PROGRAM) && ((VerifiedLength == 0) || (VerifiedLength != FileLength)))
        return (UPDATE_ERR_STATE);

    if ((Pass == STREAM_PASS_PROGRAM) && (UpdateEepromInit () == CyFalse))
        return (UPDATE_ERR_EEPROM);

    Stream.Pass = Pass;
    Stream.FileLength = FileLength;
    Stream.Fed = 0;
    Stream.Error = UPDATE_OK;
    Stream.Produced = 0;
    Stream.Crc = 0;
    Stream.PageIndex = 0;
    Stream.PageFill = 0;
    Stream.BitIndex = 8;
    Stream.Phase = PHASE_TOKEN;
    return (UPDATE_OK);
}

uint32_t UpdateStreamFeed (const uint8_t *Data, uint32_t Count)
{
    uint32_t Index = 0;
    uint32_t Take;
    uint32_t Difference;
    uint32_t Check;

    if (Stream.Pass == STREAM_PASS_NONE)
        return (UPDATE_ERR_STATE);
    if (Stream.Error != UPDATE_OK)
        return (Stream.Error);
    if ((Stream.Fed + Count) > Stream.FileLength)
        return (Stream.Error = UPDATE_ERR_LENGTH);

    /* The header first. */
    while ((Stream.Fed < UPDATE_HEADER_BYTES) && (Index < Count))
    {
        Stream.HeaderRaw[Stream.Fed++] = Data[Index++];
        if (Stream.Fed == UPDATE_HEADER_BYTES)
        {
            Stream.Header.Magic = LoadWord (Stream.HeaderRaw);
            Stream.Header.FormatVersion = (uint16_t)(Stream.HeaderRaw[4] | (Stream.HeaderRaw[5] << 8));
            Stream.Header.Target = (uint16_t)(Stream.HeaderRaw[6] | (Stream.HeaderRaw[7] << 8));
            Stream.Header.ImageVersion = LoadWord (Stream.HeaderRaw + 8);
            Stream.Header.Flags = LoadWord (Stream.HeaderRaw + 12);
            Stream.Header.PayloadLength = LoadWord (Stream.HeaderRaw + 16);
            Stream.Header.ImageLength = LoadWord (Stream.HeaderRaw + 20);
            Stream.Header.ImageCrc = LoadWord (Stream.HeaderRaw + 24);
            for (Check = 0; Check < 32; Check++)
                Stream.Header.Mac[Check] = Stream.HeaderRaw[32 + Check];

            if ((Stream.Header.Magic != UPDATE_MAGIC) || (Stream.Header.FormatVersion != UPDATE_FORMAT_VERSION) || (LoadWord (Stream.HeaderRaw + 28) != 0) ||
                ((Stream.Header.Flags & ~UPDATE_FLAG_LZSS) != 0) || (Stream.Header.PayloadLength != (Stream.FileLength - UPDATE_HEADER_BYTES)) ||
                (Stream.Header.ImageLength < 512) || (Stream.Header.ImageLength > UPDATE_EEPROM_BYTES) ||
                (((Stream.Header.Flags & UPDATE_FLAG_LZSS) == 0) && (Stream.Header.PayloadLength != Stream.Header.ImageLength)))
                return (Stream.Error = UPDATE_ERR_FORMAT);

            if (Stream.Pass == STREAM_PASS_PROGRAM)
            {
                /* The second pass must carry the very header the first pass verified. */
                Difference = 0;
                for (Check = 0; Check < UPDATE_HEADER_BYTES; Check++)
                    Difference |= (uint32_t)(Stream.HeaderRaw[Check] ^ VerifiedHeader[Check]);
                if (Difference != 0)
                    return (Stream.Error = UPDATE_ERR_FORMAT);

                /* Blank page 0 before anything else is written: the boot ROM finds no image until the very end. */
                for (Check = 0; Check < UPDATE_PAGE_BYTES; Check++)
                    Stream.Page[Check] = 0xFF;
                Check = EnsurePage (0, Stream.Page);
                if (Check != UPDATE_OK)
                    return (Stream.Error = Check);
            }

            Stream.Compressed = ((Stream.Header.Flags & UPDATE_FLAG_LZSS) != 0) ? CyTrue : CyFalse;
            HmacInit (&Stream.Mac, SigningKey, sizeof (SigningKey));
            HmacUpdate (&Stream.Mac, Stream.HeaderRaw, 32);
        }
    }

    /* Then the payload: signature and decoder see every byte. */
    if (Index < Count)
    {
        Take = Count - Index;
        Stream.Fed += Take;
        HmacUpdate (&Stream.Mac, Data + Index, Take);
        for (; Index < Count; Index++)
        {
            StreamDecode (Data[Index]);
            if (Stream.Error != UPDATE_OK)
                return (Stream.Error);
        }
    }

    return (UPDATE_OK);
}

void UpdateStreamAbort (void)
{
    if (Stream.Pass == STREAM_PASS_PROGRAM)
        UpdateEepromDeinit ();
    Stream.Pass = STREAM_PASS_NONE;
}

/* Ends a pass: pass 1 remembers the verified header, pass 2 writes page 0 last and reads the whole image back. */
uint32_t UpdateStreamEnd (void)
{
    uint8_t Mac[32];
    uint8_t Page[UPDATE_PAGE_BYTES];
    uint32_t Pass = Stream.Pass;
    uint32_t Index;
    uint32_t Difference = 0;
    uint32_t Result = UPDATE_OK;
    uint32_t Pages;
    uint32_t Remaining;
    uint32_t Crc = 0;

    if (Pass == STREAM_PASS_NONE)
        return (UPDATE_ERR_STATE);

    if (Stream.Error != UPDATE_OK)
        Result = Stream.Error;
    else if (Stream.Fed != Stream.FileLength)
        Result = UPDATE_ERR_LENGTH;
    else
    {
        if (Stream.PageFill > 0)
            StreamFlushPage (Stream.PageFill);
        if (Stream.Error != UPDATE_OK)
            Result = Stream.Error;
        else if (Stream.Produced != Stream.Header.ImageLength)
            Result = UPDATE_ERR_DECOMPRESS;
    }

    if (Result == UPDATE_OK)
    {
        HmacFinal (&Stream.Mac, Mac);
        for (Index = 0; Index < 32; Index++)
            Difference |= (uint32_t)(Mac[Index] ^ Stream.Header.Mac[Index]);
        if (Difference != 0)
            Result = UPDATE_ERR_SIGNATURE;
        else if (Stream.Crc != Stream.Header.ImageCrc)
            Result = UPDATE_ERR_IMAGE;
    }

    if ((Result == UPDATE_OK) && (Pass == STREAM_PASS_VERIFY))
    {
        for (Index = 0; Index < UPDATE_HEADER_BYTES; Index++)
            VerifiedHeader[Index] = Stream.HeaderRaw[Index];
        VerifiedLength = Stream.FileLength;
    }

    if ((Result == UPDATE_OK) && (Pass == STREAM_PASS_PROGRAM))
    {
        Result = EnsurePage (0, Stream.FirstPage);

        Pages = (Stream.Header.ImageLength + UPDATE_PAGE_BYTES - 1) / UPDATE_PAGE_BYTES;
        Remaining = Stream.Header.ImageLength;
        for (Index = 0; (Result == UPDATE_OK) && (Index < Pages); Index++)
        {
            if (EepromReadWait (Index * UPDATE_PAGE_BYTES, Page) == CyFalse)
            {
                Result = UPDATE_ERR_EEPROM;
                break;
            }
            Crc = Crc32Update (Crc, Page, (Remaining < UPDATE_PAGE_BYTES) ? Remaining : UPDATE_PAGE_BYTES);
            Remaining -= (Remaining < UPDATE_PAGE_BYTES) ? Remaining : UPDATE_PAGE_BYTES;
        }
        if ((Result == UPDATE_OK) && (Crc != Stream.Header.ImageCrc))
            Result = UPDATE_ERR_VERIFY;
        VerifiedLength = 0;
    }

    UpdateStreamAbort ();
    return (Result);
}

#endif /* UPDATE_STREAMING */
