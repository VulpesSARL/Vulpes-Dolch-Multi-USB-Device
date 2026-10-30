#include "cyu3system.h"
#include "cyu3os.h"
#include "cyu3dma.h"
#include "cyu3error.h"
#include "GpifLinkCore.h"

/* See GpifLinkCore.h for the design (the GPIF "lane": one message per 16 KB GPIF buffer, no ARQ, no check word). Everything that receives runs
 * in the link thread; GpifLinkSend may be called from any thread. */

#define MIN_MESSAGE_BYTES        (8)
#define WAIT_MS                  (20)       /* Longest sleep of the link thread in the receive call; also the pause acknowledge latency. */
#define JUNK_BACKOFF_AFTER       (4)        /* Invalid buffers in a row before the thread starts sleeping 2 ms per buffer (a floating bus delivers junk non-stop). */

static GpifLinkConfig Config;
static GpifLinkStats Stats;
static CyBool_t Initialized = CyFalse;

/* Slicing-by-4: table 0 is the usual byte table, table n advances a byte through n more zero bytes. */
static uint32_t Crc32Table[4][256];

static CyU3PMutex TxLock;

/* Set by GpifLinkPause, acknowledged by the link thread at the top of its loop (nothing is in progress inside the thread then). */
static volatile CyBool_t PauseRequested = CyFalse;
static volatile CyBool_t PauseAcknowledged = CyFalse;
static volatile uint32_t Heartbeat = 0;           /* Counts passes of the link thread; the guard thread watches it. */

static void Crc32Init (void)
{
    uint32_t Index;
    uint32_t Bit;
    uint32_t Value;
    uint32_t Slice;

    for (Index = 0; Index < 256; Index++)
    {
        Value = Index;
        for (Bit = 0; Bit < 8; Bit++)
            Value = ((Value & 1) != 0) ? ((Value >> 1) ^ 0xEDB88320) : (Value >> 1);
        Crc32Table[0][Index] = Value;
    }

    for (Index = 0; Index < 256; Index++)
    {
        Value = Crc32Table[0][Index];
        for (Slice = 1; Slice < 4; Slice++)
        {
            Value = Crc32Table[0][Value & 0xFF] ^ (Value >> 8);
            Crc32Table[Slice][Index] = Value;
        }
    }
}

/* CRC-32 (the zlib one). Four bytes per step when the data is word aligned (it always is inside the link). */
static uint32_t Crc32Compute (const uint8_t *Data, uint32_t Length)
{
    uint32_t Value = 0xFFFFFFFF;
    uint32_t Index = 0;
    const uint32_t *Words;
    uint32_t WordCount;

    if ((((uint32_t)Data) & 3) == 0)
    {
        Words = (const uint32_t *)Data;
        WordCount = Length / 4;
        for (; Index < WordCount; Index++)
        {
            Value ^= Words[Index];
            Value = Crc32Table[3][Value & 0xFF] ^ Crc32Table[2][(Value >> 8) & 0xFF] ^
                    Crc32Table[1][(Value >> 16) & 0xFF] ^ Crc32Table[0][Value >> 24];
        }
        Index = WordCount * 4;
    }

    for (; Index < Length; Index++)
        Value = Crc32Table[0][(Value ^ Data[Index]) & 0xFF] ^ (Value >> 8);

    return (~Value);
}

static uint32_t ReadWord (const uint8_t *Data)
{
    return ((uint32_t)Data[0] | ((uint32_t)Data[1] << 8) | ((uint32_t)Data[2] << 16) | ((uint32_t)Data[3] << 24));
}

static void WriteWord (uint8_t *Data, uint32_t Value)
{
    Data[0] = (uint8_t)(Value & 0xFF);
    Data[1] = (uint8_t)((Value >> 8) & 0xFF);
    Data[2] = (uint8_t)((Value >> 16) & 0xFF);
    Data[3] = (uint8_t)(Value >> 24);
}

/* ---- Word helpers ----------------------------------------------------------------------------------------------- */

/* Copies Count words. Eight words per step with load/store multiple (one burst each instead of eight single accesses: about twice
 * as fast as a plain loop on this core). The regions may overlap if Destination is below Source. */
static void CopyWords (uint32_t *Destination, const uint32_t *Source, uint32_t Count)
{
    uint32_t Blocks = Count & ~((uint32_t)7);

    if (Blocks != 0)
    {
        __asm__ volatile (
            "1:\n\t"
            "ldmia %[src]!, {r4-r11}\n\t"
            "stmia %[dst]!, {r4-r11}\n\t"
            "subs %[cnt], %[cnt], #8\n\t"
            "bne 1b\n\t"
            : [src] "+r" (Source), [dst] "+r" (Destination), [cnt] "+r" (Blocks)
            :
            : "r4", "r5", "r6", "r7", "r8", "r9", "r10", "r11", "cc", "memory");
    }

    for (Count &= 7; Count > 0; Count--)
        *Destination++ = *Source++;
}

static void ZeroWords (uint32_t *Destination, uint32_t Count)
{
    uint32_t Blocks = Count & ~((uint32_t)7);

    if (Blocks != 0)
    {
        __asm__ volatile (
            "mov r4, #0\n\t"
            "mov r5, #0\n\t"
            "mov r6, #0\n\t"
            "mov r7, #0\n\t"
            "1:\n\t"
            "stmia %[dst]!, {r4-r7}\n\t"
            "stmia %[dst]!, {r4-r7}\n\t"
            "subs %[cnt], %[cnt], #8\n\t"
            "bne 1b\n\t"
            : [dst] "+r" (Destination), [cnt] "+r" (Blocks)
            :
            : "r4", "r5", "r6", "r7", "cc", "memory");
    }

    for (Count &= 7; Count > 0; Count--)
        *Destination++ = 0;
}

/* The check word (the PC side is MessageCheck.cs and must give the same value):
 * S1 = seed + sum of the words, S2 = word count + sum of the S1 values, result = S1 xor rotl(S2 * 0x9E3779B1, 13). Two adds per
 * word, eight words per load multiple. Length must be a multiple of 4. */
#define CHECK_SEED               (0x4B434456)
#define CHECK_MULTIPLIER         (0x9E3779B1)

static uint32_t CheckCompute (const uint8_t *Data, uint32_t Length)
{
    uint32_t Count = Length / 4;
    uint32_t S1 = CHECK_SEED;
    uint32_t S2 = Count;
    uint32_t Blocks;
    uint32_t Mixed;
    const uint32_t *Words = (const uint32_t *)Data;

    if ((((uint32_t)Data) & 3) != 0)
    {
        uint32_t Index;

        for (Index = 0; Index < Count; Index++)
        {
            S1 += ReadWord (Data + Index * 4);
            S2 += S1;
        }
    }
    else
    {
        Blocks = Count & ~((uint32_t)7);
        if (Blocks != 0)
        {
            __asm__ volatile (
                "1:\n\t"
                "ldmia %[src]!, {r4-r11}\n\t"
                "add %[s1], %[s1], r4\n\t"
                "add %[s2], %[s2], %[s1]\n\t"
                "add %[s1], %[s1], r5\n\t"
                "add %[s2], %[s2], %[s1]\n\t"
                "add %[s1], %[s1], r6\n\t"
                "add %[s2], %[s2], %[s1]\n\t"
                "add %[s1], %[s1], r7\n\t"
                "add %[s2], %[s2], %[s1]\n\t"
                "add %[s1], %[s1], r8\n\t"
                "add %[s2], %[s2], %[s1]\n\t"
                "add %[s1], %[s1], r9\n\t"
                "add %[s2], %[s2], %[s1]\n\t"
                "add %[s1], %[s1], r10\n\t"
                "add %[s2], %[s2], %[s1]\n\t"
                "add %[s1], %[s1], r11\n\t"
                "add %[s2], %[s2], %[s1]\n\t"
                "subs %[cnt], %[cnt], #8\n\t"
                "bne 1b\n\t"
                : [src] "+r" (Words), [s1] "+r" (S1), [s2] "+r" (S2), [cnt] "+r" (Blocks)
                :
                : "r4", "r5", "r6", "r7", "r8", "r9", "r10", "r11", "cc", "memory");
        }

        for (Count &= 7; Count > 0; Count--)
        {
            S1 += *Words++;
            S2 += S1;
        }
    }

    Mixed = S2 * CHECK_MULTIPLIER;
    return (S1 ^ ((Mixed << 13) | (Mixed >> 19)));
}

/* Moves words towards the front; the regions may overlap. */
static void MoveWordsDown (uint32_t *Destination, const uint32_t *Source, uint32_t Count)
{
    CopyWords (Destination, Source, Count);
}

uint32_t GpifLinkBuildMessage (uint8_t *Destination, uint8_t Channel, uint8_t Type, uint16_t Tag,
        const uint8_t *Payload, uint32_t PayloadLength)
{
    uint32_t PaddedLength = (PayloadLength + 3) & ~((uint32_t)3);
    uint32_t CrcOffset = 8 + PaddedLength;
    uint32_t Index;

    if (Crc32Table[0][1] == 0)
        Crc32Init ();

    Destination[0] = Channel;
    Destination[1] = Type;
    Destination[2] = (uint8_t)(Tag & 0xFF);
    Destination[3] = (uint8_t)(Tag >> 8);
    WriteWord (Destination + 4, PayloadLength);
    if (((((uint32_t)Payload) | ((uint32_t)Destination)) & 3) == 0)
    {
        CopyWords ((uint32_t *)(Destination + 8), (const uint32_t *)Payload, PayloadLength / 4);
        Index = PayloadLength & ~((uint32_t)3);
    }
    else
    {
        Index = 0;
    }
    for (; Index < PaddedLength; Index++)
        Destination[8 + Index] = (Index < PayloadLength) ? Payload[Index] : 0;
    return (CrcOffset);
}

uint32_t GpifLinkCrc32 (const uint8_t *Data, uint32_t Length)
{
    if (Crc32Table[0][1] == 0)
        Crc32Init ();

    return (Crc32Compute (Data, Length));
}

uint32_t GpifLinkCheck (const uint8_t *Data, uint32_t Length)
{
    return (CheckCompute (Data, Length));
}

void GpifLinkCopyBytes (uint8_t *Destination, const uint8_t *Source, uint32_t Length)
{
    uint32_t Whole;

    if ((((uint32_t)Destination | (uint32_t)Source) & 3) != 0)
    {
        CyU3PMemCopy (Destination, (uint8_t *)Source, Length);
        return;
    }

    Whole = Length & ~((uint32_t)3);
    CopyWords ((uint32_t *)Destination, (const uint32_t *)Source, Whole / 4);
    for (; Whole < Length; Whole++)
        Destination[Whole] = Source[Whole];
}

#ifdef GPIF_LINK_BENCH
/* Same loop as Crc32Compute, compiled for size like the rest of the build: for the benchmark only. */
uint32_t GpifLinkCrc32Size (const uint8_t *Data, uint32_t Length)
{
    uint32_t Value = 0xFFFFFFFF;
    uint32_t Index;
    const uint32_t *Words = (const uint32_t *)Data;

    for (Index = 0; Index < Length / 4; Index++)
    {
        Value ^= Words[Index];
        Value = Crc32Table[3][Value & 0xFF] ^ Crc32Table[2][(Value >> 8) & 0xFF] ^
                Crc32Table[1][(Value >> 16) & 0xFF] ^ Crc32Table[0][Value >> 24];
    }

    return (~Value);
}

/* Byte-at-a-time CRC with the 1 KB table (smaller cache footprint than slicing-by-4): for the benchmark only. */
uint32_t GpifLinkCrc32Byte (const uint8_t *Data, uint32_t Length)
{
    uint32_t Value = 0xFFFFFFFF;
    uint32_t Index;
    const uint32_t *Table = Crc32Table[0];

    for (Index = 0; Index < Length; Index++)
        Value = Table[(Value ^ Data[Index]) & 0xFF] ^ (Value >> 8);

    return (~Value);
}
#endif

void GpifLinkSealMessage (uint8_t *MessageBlock, uint32_t Length)
{
    (void)MessageBlock;
    (void)Length;
}

/* ---- The lane ------------------------------------------------------------------------------------------------------ */

/* Whether the start of a received GPIF buffer holds a plausible message block: returns its length in bytes, 0 if not. A floating or
 * undriven bus delivers all-zero or all-one buffers; those are no messages (channel 0 type 0 does not exist, the length field of all ones is
 * far above the limit). */
static uint32_t BlockLength (const uint8_t *Data, uint32_t Count)
{
    uint32_t PayloadLength;
    uint32_t Bytes;

    if (Count < MIN_MESSAGE_BYTES)
        return (0);
    if ((Data[0] == 0) && (Data[1] == 0))
        return (0);
    if (Data[0] > GPIF_LINK_MAX_CHANNEL)
        return (0);

    PayloadLength = ReadWord (Data + 4);
    if (PayloadLength > GPIF_LINK_MAX_PAYLOAD_BYTES)
        return (0);

    Bytes = 8 + ((PayloadLength + 3) & ~((uint32_t)3));
    return ((Bytes <= Count) ? Bytes : 0);
}

CyBool_t GpifLinkInit (const GpifLinkConfig *NewConfig)
{
    if ((NewConfig == NULL) || (NewConfig->ReadChannel == NULL) || (NewConfig->WriteChannel == NULL) || (NewConfig->OnMessage == NULL))
        return (CyFalse);

    if (Crc32Table[0][1] == 0)
        Crc32Init ();

    Config = *NewConfig;
    CyU3PMemSet ((uint8_t *)&Stats, 0, sizeof (Stats));
    if (CyU3PMutexCreate (&TxLock, CYU3P_NO_INHERIT) != CY_U3P_SUCCESS)
        return (CyFalse);

    Initialized = CyTrue;
    return (CyTrue);
}

void GpifLinkReset (void)
{
    /* Nothing is kept between messages. The callers that tear the DMA channels down or reset them do it themselves. */
}

CyBool_t GpifLinkPause (uint32_t TimeoutMs)
{
    uint32_t Waited = 0;

    PauseAcknowledged = CyFalse;
    PauseRequested = CyTrue;
    while ((PauseAcknowledged == CyFalse) && (Waited < TimeoutMs))
    {
        CyU3PThreadSleep (2);
        Waited += 2;
    }

    if (PauseAcknowledged == CyFalse)
    {
        PauseRequested = CyFalse;
        return (CyFalse);
    }
    return (CyTrue);
}

void GpifLinkResume (void)
{
    PauseRequested = CyFalse;
}

/* A message block goes into the next free GPIF write buffer, which is committed as a whole buffer (the GPIF moves only whole buffers; the
 * bytes behind the message are stale and ignored by the receiver). Waits up to TimeoutMs for a free buffer. */
CyBool_t GpifLinkSend (const uint8_t *MessageBlock, uint32_t Length, uint32_t TimeoutMs)
{
    CyU3PDmaBuffer_t Buffer;
    CyU3PReturnStatus_t Status;
    uint32_t PayloadLength;

    if ((Initialized == CyFalse) || (Length < MIN_MESSAGE_BYTES) || (Length > GPIF_LINK_MAX_MESSAGE_BYTES) || ((Length & 3) != 0))
        return (CyFalse);

    PayloadLength = ReadWord (MessageBlock + 4);
    if ((PayloadLength > GPIF_LINK_MAX_PAYLOAD_BYTES) || (Length != 8 + ((PayloadLength + 3) & ~((uint32_t)3))))
        return (CyFalse);

    if (CyU3PMutexGet (&TxLock, TimeoutMs) != CY_U3P_SUCCESS)
        return (CyFalse);

    Status = CyU3PDmaChannelGetBuffer (Config.WriteChannel, &Buffer, TimeoutMs);
    if (Status == CY_U3P_SUCCESS)
    {
        GpifLinkCopyBytes (Buffer.buffer + GPIF_LINK_MESSAGE_OFFSET, MessageBlock, Length);
        Status = CyU3PDmaChannelCommitBuffer (Config.WriteChannel, Buffer.size, 0);
        if (Status == CY_U3P_SUCCESS)
        {
            Stats.BlocksSent++;
            Stats.BuffersSent++;
        }
    }
    CyU3PMutexPut (&TxLock);

    return ((Status == CY_U3P_SUCCESS) ? CyTrue : CyFalse);
}

#define TX_HELD_MAX              (4)

static uint8_t *TxHeld[TX_HELD_MAX];
static uint32_t TxHeldCount = 0;                  /* Protected by TxLock (held by the one thread that has buffers taken). */

CyBool_t GpifLinkTxGet (uint32_t TimeoutMs, uint8_t **Message)
{
    CyU3PDmaBuffer_t Buffer;

    if ((Initialized == CyFalse) || (Message == NULL))
        return (CyFalse);

    if (CyU3PMutexGet (&TxLock, TimeoutMs) != CY_U3P_SUCCESS)
        return (CyFalse);

    if ((TxHeldCount >= TX_HELD_MAX) || (CyU3PDmaChannelGetBuffer (Config.WriteChannel, &Buffer, TimeoutMs) != CY_U3P_SUCCESS))
    {
        CyU3PMutexPut (&TxLock);
        return (CyFalse);
    }

    TxHeld[TxHeldCount++] = Buffer.buffer;
    *Message = Buffer.buffer + GPIF_LINK_MESSAGE_OFFSET;
    return (CyTrue);
}

CyBool_t GpifLinkTxCommit (void)
{
    CyU3PReturnStatus_t Status;
    uint32_t Index;

    if (TxHeldCount == 0)
        return (CyFalse);

    Status = CyU3PDmaChannelCommitBuffer (Config.WriteChannel, GPIF_LINK_BUFFER_BYTES, 0);
    for (Index = 1; Index < TxHeldCount; Index++)
        TxHeld[Index - 1] = TxHeld[Index];
    TxHeldCount--;

    if (Status == CY_U3P_SUCCESS)
    {
        Stats.BlocksSent++;
        Stats.BuffersSent++;
    }

    CyU3PMutexPut (&TxLock);
    return ((Status == CY_U3P_SUCCESS) ? CyTrue : CyFalse);
}

void GpifLinkTxCancel (void)
{
    while (TxHeldCount != 0)
    {
        /* Channel 0 type 0 does not exist: the receiver drops the buffer. */
        TxHeld[0][GPIF_LINK_MESSAGE_OFFSET] = 0;
        TxHeld[0][GPIF_LINK_MESSAGE_OFFSET + 1] = 0;
        GpifLinkTxCommit ();
    }
}

void GpifLinkDmaCallback (CyU3PDmaChannel *Handle, CyU3PDmaCbType_t Type, CyU3PDmaCBInput_t *Input)
{
    /* Not needed: the receive call blocks on the DMA event itself. Kept so that code that registers it still builds. */
}

void GpifLinkThreadEntry (uint32_t Input)
{
    CyU3PDmaBuffer_t Buffer;
    CyU3PReturnStatus_t Status;
    uint32_t Bytes;
    uint32_t Junk = 0;

    for (;;)
    {
        Heartbeat++;

        if (PauseRequested == CyTrue)
        {
            PauseAcknowledged = CyTrue;
            Junk = 0;
            CyU3PThreadSleep (2);
            continue;
        }
        PauseAcknowledged = CyFalse;

        if (Initialized == CyFalse)
        {
            CyU3PThreadSleep (10);
            continue;
        }

        Status = CyU3PDmaChannelGetBuffer (Config.ReadChannel, &Buffer, WAIT_MS);
        if (Status != CY_U3P_SUCCESS)
        {
            if (Status != CY_U3P_ERROR_TIMEOUT)
                CyU3PThreadSleep (2);
            continue;
        }

        Stats.BuffersReceived++;
        Bytes = (Buffer.count > GPIF_LINK_MESSAGE_OFFSET) ? BlockLength (Buffer.buffer + GPIF_LINK_MESSAGE_OFFSET, Buffer.count - GPIF_LINK_MESSAGE_OFFSET) : 0;
        if (Bytes != 0)
        {
            Junk = 0;
            Stats.BlocksDelivered++;
            Config.OnMessage (Buffer.buffer + GPIF_LINK_MESSAGE_OFFSET, Bytes);
        }
        else
        {
            Stats.HeaderSkips++;
            Junk++;
        }
        CyU3PDmaChannelDiscardBuffer (Config.ReadChannel);

        if (Junk >= JUNK_BACKOFF_AFTER)
            CyU3PThreadSleep (2);
    }
}

CyBool_t GpifLinkIsIdle (void)
{
    return (CyTrue);
}

CyBool_t GpifLinkSetWindow (uint32_t NewWindow)
{
    return (CyTrue);
}

const GpifLinkStats *GpifLinkGetStats (void)
{
    return (&Stats);
}

void GpifLinkPrintState (void)
{
    CyU3PDebugPrint (4, "Lane: sent %d, delivered %d, buffers received %d, invalid %d\r\n", Stats.BlocksSent, Stats.BlocksDelivered, Stats.BuffersReceived,
            Stats.HeaderSkips);
}

uint32_t GpifLinkHeartbeat (void)
{
    return (Heartbeat);
}

void GpifLinkSetFault (uint32_t Mode, uint32_t Period)
{
    /* No ARQ on the lane, so there is nothing a fault injection could test. */
}
