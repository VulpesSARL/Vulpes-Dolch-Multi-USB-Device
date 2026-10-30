#include "cyu3system.h"
#include "cyu3os.h"
#include "cyu3dma.h"
#include "cyu3error.h"
#include "GpifLinkCore.h"
#include "CDMEDIA.h"

/* See CDMEDIA.h. Threads: the link thread runs CdMediaOnMessage (and so appends DATA to the ring); the CD-ROM thread runs the SCSI
 * commands and consumes the ring. The medium state is protected by a mutex that is only ever held for a few instructions. */

#define CDM_MAX_BLOBS                (8)
#define CDM_BLOB_STORE_BYTES         (6144)
#define CDM_SCRATCH_BYTES            (4096)
#define CDM_INSERT_HEADER_BYTES      (20)
#define CDM_BLOB_HEADER_BYTES        (8)
#define CDM_INSERT_FLAG_REATTACH     (0x01)
#define CDM_INSERT_FLAG_SYNTHETIC    (0x80)     /* Speed test: the Client makes the data up itself, nothing is asked from the PC. */
#define CDM_INSERT_FLAG_NO_PATTERN   (0x40)     /* With SYNTHETIC: do not even write the counting pattern (speed test of the USB side alone; the data is stale). */
#define CDM_RING_WAIT_MS             (2000)     /* The link thread waits this long for the USB side to free ring space. */

#define CDM_ERROR_BAD_LENGTH         (3)
#define CDM_ERROR_MEDIA_ID_MISMATCH  (4)
#define CDM_ERROR_UNKNOWN_TYPE       (2)

#define CDM_CONSUMER_DATA            (0x01)
#define CDM_CONSUMER_COMPLETE        (0x02)
#define CDM_CONSUMER_ABORT           (0x04)
#define CDM_PRODUCER_SPACE           (0x01)
#define CDM_PRODUCER_ABORT           (0x02)

/* Provided by LINKCLIENT.c. */
extern CyBool_t ClientLinkSend (uint8_t Channel, uint8_t Type, uint16_t Tag, const uint8_t *Payload, uint32_t Length, uint32_t TimeoutMs);
extern void ClientLinkAnnounceStatus (void);
extern void ClientLinkSendError (uint8_t Code, uint16_t ReferenceTag);

static CyU3PMutex StateLock;
static CyBool_t Initialized = CyFalse;

/* The medium. */
static volatile CyBool_t Present = CyFalse;
static uint8_t MediaName[CDM_NAME_BYTES];       /* Name of the image the PC gave, RAM only. */
static uint32_t MediaNameLength = 0;
static volatile CyBool_t Locked = CyFalse;
static uint32_t MediaIdLow = 0;
static uint32_t MediaIdHigh = 0;
static uint16_t Profile = 0;
static uint8_t DiscType = 0;
static uint32_t LeadOutLba = 0;
static uint32_t BlobCount = 0;
static uint8_t BlobKinds[CDM_MAX_BLOBS];
static uint32_t BlobOffsets[CDM_MAX_BLOBS];
static uint32_t BlobLengths[CDM_MAX_BLOBS];
static uint8_t BlobStore[CDM_BLOB_STORE_BYTES] __attribute__((aligned(4)));
static volatile uint32_t ChangeFlags = 0;
static volatile uint8_t MediaEvent = CDM_MEDIA_EVENT_NONE;
static volatile CyBool_t Synthetic = CyFalse;     /* The medium is a speed test disc whose sectors the Client generates. */
static volatile CyBool_t SyntheticNoPattern = CyFalse;

static uint8_t *Scratch = NULL;

/* The read in progress. */
static uint8_t *Ring = NULL;
static volatile uint32_t RingWrite = 0;          /* Total bytes put into the ring (the link thread writes this). */
static volatile uint32_t RingRead = 0;           /* Total bytes taken out of it (the CD-ROM thread writes this). */
static CyU3PEvent ConsumerEvents;
static CyU3PEvent ProducerEvents;
static volatile CyBool_t ReadActive = CyFalse;
static volatile uint16_t ReadTag = 0;
static volatile uint32_t ReadTotal = 0;
static volatile uint32_t ReadReceived = 0;
static volatile uint8_t ReadStatus = CDM_STATUS_NONE;
static volatile CyBool_t ReadComplete = CyFalse;
static uint32_t ReadLba = 0;
static uint32_t ReadCount = 0;
static uint8_t ReadFormat = 0;
static uint16_t TagCounter = 0;

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

void CdMediaInit (void)
{
    /* CyU3PDmaBufferAlloc takes a 16 bit size. */
    if (CDM_RING_BYTES > 65535)
    {
        CyU3PDebugPrint (4, "CD-ROM: the ring is too big for CyU3PDmaBufferAlloc\r\n");
        return;
    }

    Ring = (uint8_t *)CyU3PDmaBufferAlloc (CDM_RING_BYTES);
    Scratch = (uint8_t *)CyU3PDmaBufferAlloc (CDM_SCRATCH_BYTES);
    if ((Ring == NULL) || (Scratch == NULL))
    {
        CyU3PDebugPrint (4, "CD-ROM: no memory for the read ring\r\n");
        return;
    }

    CyU3PMutexCreate (&StateLock, CYU3P_NO_INHERIT);
    CyU3PEventCreate (&ConsumerEvents);
    CyU3PEventCreate (&ProducerEvents);
    Initialized = CyTrue;
}

/* ---- State queries --------------------------------------------------------------------------------------------------------- */

uint8_t *CdMediaTakeBuffer (uint32_t *Bytes)
{
    if (Ring == NULL)
        return (NULL);

    Initialized = CyFalse;
    *Bytes = CDM_RING_BYTES;
    return (Ring);
}

CyBool_t CdMediaIsPresent (void)
{
    return (Present);
}

CyBool_t CdMediaIsLocked (void)
{
    return (Locked);
}

uint32_t CdMediaLeadOutLba (void)
{
    return (LeadOutLba);
}

uint16_t CdMediaProfile (void)
{
    return (Profile);
}

uint8_t CdMediaDiscType (void)
{
    return (DiscType);
}

void CdMediaGetId (uint32_t *Low, uint32_t *High)
{
    *Low = MediaIdLow;
    *High = MediaIdHigh;
}

const uint8_t *CdMediaGetBlob (uint8_t Kind, uint32_t *Length)
{
    uint32_t Index;

    for (Index = 0; Index < BlobCount; Index++)
    {
        if (BlobKinds[Index] == Kind)
        {
            *Length = BlobLengths[Index];
            return (BlobStore + BlobOffsets[Index]);
        }
    }

    *Length = 0;
    return (NULL);
}

uint32_t CdMediaTakeChanges (void)
{
    uint32_t Flags;

    if (Initialized == CyFalse)
        return (0);

    CyU3PMutexGet (&StateLock, CYU3P_WAIT_FOREVER);
    Flags = ChangeFlags;
    ChangeFlags = 0;
    CyU3PMutexPut (&StateLock);
    return (Flags);
}

uint8_t CdMediaTakeMediaEvent (void)
{
    uint8_t Event;

    if (Initialized == CyFalse)
        return (CDM_MEDIA_EVENT_NONE);

    CyU3PMutexGet (&StateLock, CYU3P_WAIT_FOREVER);
    Event = MediaEvent;
    MediaEvent = CDM_MEDIA_EVENT_NONE;
    CyU3PMutexPut (&StateLock);
    return (Event);
}

uint8_t *CdMediaScratch (void)
{
    return (Scratch);
}

/* ---- Messages to the PC ---------------------------------------------------------------------------------------------------- */

static void SendMediaState (uint16_t Tag, uint8_t Event)
{
    uint8_t Payload[12];

    CyU3PMemSet (Payload, 0, sizeof (Payload));
    Payload[0] = (Present == CyTrue) ? 1 : 0;
    Payload[1] = (Locked == CyTrue) ? 1 : 0;
    Payload[2] = Event;
    WriteWord (Payload + 4, MediaIdLow);
    WriteWord (Payload + 8, MediaIdHigh);
    ClientLinkSend (1, CDM_TYPE_MEDIA_STATE, Tag, Payload, sizeof (Payload), 0);
}

/* Wakes whoever waits for a read and makes the read fail. */
static void AbortRead (void)
{
    if (ReadActive == CyFalse)
        return;

    CyU3PEventSet (&ConsumerEvents, CDM_CONSUMER_ABORT, CYU3P_EVENT_OR);
    CyU3PEventSet (&ProducerEvents, CDM_PRODUCER_ABORT, CYU3P_EVENT_OR);
}

/* The medium is gone (removed by the PC or ejected by the OS). Event says why for the MEDIA_STATE that tells the PC. */
static void DropMedium (uint16_t Tag, uint8_t Event)
{
    CyU3PMutexGet (&StateLock, CYU3P_WAIT_FOREVER);
    Present = CyFalse;
    Locked = CyFalse;
    MediaNameLength = 0;
    BlobCount = 0;
    ChangeFlags |= CDM_CHANGE_REMOVED;
    MediaEvent = CDM_MEDIA_EVENT_REMOVAL;
    CyU3PMutexPut (&StateLock);

    AbortRead ();
    SendMediaState (Tag, Event);
    ClientLinkAnnounceStatus ();
}

/* ---- Messages from the PC (link thread) ------------------------------------------------------------------------------------- */

static void HandleInsert (uint16_t Tag, const uint8_t *Payload, uint32_t Length)
{
    uint32_t NewLow;
    uint32_t NewHigh;
    uint32_t Count;
    uint32_t Index;
    uint32_t Position;
    uint32_t BlobLength;
    uint32_t Stored;
    uint8_t Flags;

    if (Length < CDM_INSERT_HEADER_BYTES)
    {
        ClientLinkSendError (CDM_ERROR_BAD_LENGTH, Tag);
        return;
    }

    NewLow = ReadWord (Payload);
    NewHigh = ReadWord (Payload + 4);
    Flags = Payload[11];

    /* The PC reconnects to a medium the Client already holds: nothing changes, the OS must not notice. */
    if ((Flags & CDM_INSERT_FLAG_REATTACH) != 0)
    {
        if ((Present == CyTrue) && (NewLow == MediaIdLow) && (NewHigh == MediaIdHigh))
        {
            SendMediaState (Tag, CDM_EVENT_NONE);
            return;
        }

        ClientLinkSendError (CDM_ERROR_MEDIA_ID_MISMATCH, Tag);
        return;
    }

    /* First check that every blob fits, so a bad message leaves the old medium alone. */
    Count = Payload[16] | ((uint32_t)Payload[17] << 8);
    if (Count > CDM_MAX_BLOBS)
    {
        ClientLinkSendError (CDM_ERROR_BAD_LENGTH, Tag);
        return;
    }

    Position = CDM_INSERT_HEADER_BYTES;
    Stored = 0;
    for (Index = 0; Index < Count; Index++)
    {
        if ((Position + CDM_BLOB_HEADER_BYTES) > Length)
        {
            ClientLinkSendError (CDM_ERROR_BAD_LENGTH, Tag);
            return;
        }

        BlobLength = ReadWord (Payload + Position + 4);
        if (((Position + CDM_BLOB_HEADER_BYTES + ((BlobLength + 3) & ~((uint32_t)3))) > Length) ||
            ((Stored + ((BlobLength + 3) & ~((uint32_t)3))) > CDM_BLOB_STORE_BYTES))
        {
            ClientLinkSendError (CDM_ERROR_BAD_LENGTH, Tag);
            return;
        }

        Position += CDM_BLOB_HEADER_BYTES + ((BlobLength + 3) & ~((uint32_t)3));
        Stored += (BlobLength + 3) & ~((uint32_t)3);
    }

    /* A read for the old medium cannot be finished any more. */
    AbortRead ();

    CyU3PMutexGet (&StateLock, CYU3P_WAIT_FOREVER);
    MediaIdLow = NewLow;
    MediaIdHigh = NewHigh;
    Profile = (uint16_t)(Payload[8] | ((uint16_t)Payload[9] << 8));
    DiscType = Payload[10];
    LeadOutLba = ReadWord (Payload + 12);
    Synthetic = ((Flags & CDM_INSERT_FLAG_SYNTHETIC) != 0) ? CyTrue : CyFalse;
    SyntheticNoPattern = ((Flags & CDM_INSERT_FLAG_NO_PATTERN) != 0) ? CyTrue : CyFalse;

    Position = CDM_INSERT_HEADER_BYTES;
    Stored = 0;
    for (Index = 0; Index < Count; Index++)
    {
        BlobLength = ReadWord (Payload + Position + 4);
        BlobKinds[Index] = Payload[Position];
        BlobOffsets[Index] = Stored;
        BlobLengths[Index] = BlobLength;
        GpifLinkCopyBytes (BlobStore + Stored, Payload + Position + CDM_BLOB_HEADER_BYTES, (BlobLength + 3) & ~((uint32_t)3));
        Position += CDM_BLOB_HEADER_BYTES + ((BlobLength + 3) & ~((uint32_t)3));
        Stored += (BlobLength + 3) & ~((uint32_t)3);
    }

    BlobCount = Count;
    Present = CyTrue;
    Locked = CyFalse;
    ChangeFlags |= CDM_CHANGE_INSERTED;
    MediaEvent = CDM_MEDIA_EVENT_NEW_MEDIA;
    CyU3PMutexPut (&StateLock);

    CyU3PDebugPrint (4, "CD-ROM: medium inserted (%d sectors, profile 0x%x, %d blobs)\r\n", LeadOutLba, Profile, Count);
    SendMediaState (Tag, CDM_EVENT_NONE);
    ClientLinkAnnounceStatus ();
}

static void HandleRemove (uint16_t Tag, const uint8_t *Payload, uint32_t Length)
{
    CyBool_t Force = ((Length >= 1) && (Payload[0] != 0)) ? CyTrue : CyFalse;

    if (Present == CyFalse)
    {
        SendMediaState (Tag, CDM_EVENT_NONE);
        return;
    }

    /* The OS holds the lock: only the user-confirmed override gets through. */
    if ((Locked == CyTrue) && (Force == CyFalse))
    {
        SendMediaState (Tag, CDM_EVENT_EJECT_REFUSED);
        return;
    }

    CyU3PDebugPrint (4, "CD-ROM: medium removed by the PC\r\n");
    DropMedium (Tag, CDM_EVENT_NONE);
}

static void HandleData (uint16_t Tag, const uint8_t *Payload, uint32_t Length)
{
    uint32_t Space;
    uint32_t Piece;
    uint32_t Flags;

    if ((ReadActive == CyFalse) || (Tag != ReadTag))
        return;

    if (Length > (ReadTotal - ReadReceived))
        Length = ReadTotal - ReadReceived;

    while (Length > 0)
    {
        Space = CDM_RING_BYTES - (RingWrite - RingRead);
        if (Space == 0)
        {
            /* The USB side has not taken what it was given: wait a bounded time for it, then give the read up. */
            if (CyU3PEventGet (&ProducerEvents, CDM_PRODUCER_SPACE | CDM_PRODUCER_ABORT, CYU3P_EVENT_OR_CLEAR, &Flags, CDM_RING_WAIT_MS) != CY_U3P_SUCCESS)
            {
                CyU3PDebugPrint (4, "CD-ROM: the USB side did not take data for %d ms, the read is dropped\r\n", CDM_RING_WAIT_MS);
                ReadStatus = CDM_STATUS_FAILED;
                ReadComplete = CyTrue;
                CyU3PEventSet (&ConsumerEvents, CDM_CONSUMER_COMPLETE, CYU3P_EVENT_OR);
                return;
            }

            if ((Flags & CDM_PRODUCER_ABORT) != 0)
                return;
            continue;
        }

        Piece = CDM_RING_BYTES - (RingWrite % CDM_RING_BYTES);
        if (Piece > Space)
            Piece = Space;
        if (Piece > Length)
            Piece = Length;

        GpifLinkCopyBytes (Ring + (RingWrite % CDM_RING_BYTES), Payload, Piece);
        RingWrite += Piece;
        ReadReceived += Piece;
        Payload += Piece;
        Length -= Piece;
        CyU3PEventSet (&ConsumerEvents, CDM_CONSUMER_DATA, CYU3P_EVENT_OR);
    }
}

static void HandleComplete (uint16_t Tag, const uint8_t *Payload, uint32_t Length)
{
    if ((ReadActive == CyFalse) || (Tag != ReadTag) || (Length < 1))
        return;

    ReadStatus = Payload[0];
    ReadComplete = CyTrue;
    CyU3PEventSet (&ConsumerEvents, CDM_CONSUMER_COMPLETE, CYU3P_EVENT_OR);
}

void CdMediaOnMessage (const uint8_t *Block, uint32_t Length)
{
    uint8_t Type = Block[1];
    uint16_t Tag = (uint16_t)(Block[2] | (Block[3] << 8));
    uint32_t PayloadLength = ReadWord (Block + 4);
    const uint8_t *Payload = Block + 8;

    if (Initialized == CyFalse)
        return;

    switch (Type)
    {
    case CDM_TYPE_MEDIA_INSERT:
        HandleInsert (Tag, Payload, PayloadLength);
        break;

    case CDM_TYPE_MEDIA_REMOVE:
        HandleRemove (Tag, Payload, PayloadLength);
        break;

    case CDM_TYPE_DATA:
        HandleData (Tag, Payload, PayloadLength);
        break;

    case CDM_TYPE_COMPLETE:
        HandleComplete (Tag, Payload, PayloadLength);
        break;

    case CDM_TYPE_EJECT_REQUEST:
        /* The next GET EVENT STATUS NOTIFICATION of the OS reports an eject request. */
        CyU3PMutexGet (&StateLock, CYU3P_WAIT_FOREVER);
        if (Present == CyTrue)
            MediaEvent = CDM_MEDIA_EVENT_EJECT_REQUEST;
        CyU3PMutexPut (&StateLock);
        CyU3PDebugPrint (4, "CD-ROM: eject request from the PC\r\n");
        break;

    case CDM_TYPE_SET_NAME:
        CyU3PMutexGet (&StateLock, CYU3P_WAIT_FOREVER);
        if (Present == CyTrue)
        {
            MediaNameLength = (PayloadLength > CDM_NAME_BYTES) ? CDM_NAME_BYTES : PayloadLength;
            CyU3PMemCopy (MediaName, (uint8_t *)Payload, MediaNameLength);
        }
        CyU3PMutexPut (&StateLock);
        break;

    case CDM_TYPE_NAME_REQUEST:
        ClientLinkSend (1, CDM_TYPE_NAME, Tag, MediaName, (Present == CyTrue) ? MediaNameLength : 0, 0);
        break;

    default:
        ClientLinkSendError (CDM_ERROR_UNKNOWN_TYPE, Tag);
        break;
    }
}

/* ---- The OS changes the drive (CD-ROM thread) ------------------------------------------------------------------------------- */

void CdMediaSetLock (CyBool_t Prevent)
{
    if (Present == CyFalse)
        return;

    if (Locked == Prevent)
        return;

    Locked = Prevent;
    SendMediaState (0, CDM_EVENT_LOCK_CHANGED);
    ClientLinkAnnounceStatus ();
}

CyBool_t CdMediaEject (void)
{
    if (Present == CyFalse)
        return (CyTrue);

    if (Locked == CyTrue)
    {
        SendMediaState (0, CDM_EVENT_EJECT_REFUSED);
        return (CyFalse);
    }

    CyU3PDebugPrint (4, "CD-ROM: medium ejected by the OS\r\n");
    DropMedium (0, CDM_EVENT_EJECTED_BY_OS);
    return (CyTrue);
}

void CdMediaBusReset (void)
{
    if (Initialized == CyFalse)
        return;

    SendMediaState (0, CDM_EVENT_BUS_RESET);
}

/* The PC application is back: a read that has not received anything yet is asked for again (reads can be repeated safely). */
static void ResendRequest (uint32_t TimeoutMs)
{
    uint8_t Payload[12];

    CyU3PMemSet (Payload, 0, sizeof (Payload));
    WriteWord (Payload, ReadLba);
    WriteWord (Payload + 4, ReadCount);
    Payload[8] = ReadFormat;
    ClientLinkSend (1, CDM_TYPE_READ_REQUEST, ReadTag, Payload, sizeof (Payload), TimeoutMs);
}

void CdMediaOnPcLink (uint8_t Up)
{
    if (Initialized == CyFalse)
        return;

    if ((Up != 0) && (ReadActive == CyTrue) && (ReadReceived == 0) && (ReadComplete == CyFalse))
        ResendRequest (0);
}

/* ---- Sector reads (CD-ROM thread) ------------------------------------------------------------------------------------------- */

CyBool_t CdMediaReadBegin (uint32_t Lba, uint32_t Count, uint8_t Format, uint32_t TotalBytes)
{
    uint32_t Flags;

    if ((Initialized == CyFalse) || (Present == CyFalse))
        return (CyFalse);

    /* Whatever a former read left behind is dropped; its late DATA carries another tag and is ignored. */
    ReadActive = CyFalse;
    RingRead = RingWrite = 0;
    CyU3PEventGet (&ConsumerEvents, CDM_CONSUMER_DATA | CDM_CONSUMER_COMPLETE | CDM_CONSUMER_ABORT, CYU3P_EVENT_OR_CLEAR, &Flags, CYU3P_NO_WAIT);
    CyU3PEventGet (&ProducerEvents, CDM_PRODUCER_SPACE | CDM_PRODUCER_ABORT, CYU3P_EVENT_OR_CLEAR, &Flags, CYU3P_NO_WAIT);

    TagCounter++;
    if (TagCounter == 0)
        TagCounter = 1;
    ReadTag = TagCounter;
    ReadTotal = TotalBytes;
    ReadReceived = 0;
    ReadStatus = CDM_STATUS_NONE;
    ReadComplete = CyFalse;
    ReadLba = Lba;
    ReadCount = Count;
    ReadFormat = Format;
    ReadActive = CyTrue;

    if (Synthetic == CyFalse)
        ResendRequest (2000);
    return (CyTrue);
}

/* Speed test discs: tops the ring up with a counting pattern - every 32 bit word holds its own byte offset divided by 4 (counted from the first
 * sector of the disc), which the PC side can check. Runs in the consumer thread, so nothing else touches the ring meanwhile. */
static void FillSynthetic (void)
{
    uint32_t Space;
    uint32_t Piece;
    uint32_t Remaining;
    uint32_t Index;
    uint32_t *Words;
    uint32_t Value;

    while (ReadReceived < ReadTotal)
    {
        Space = CDM_RING_BYTES - (RingWrite - RingRead);
        if (Space == 0)
            break;

        Piece = CDM_RING_BYTES - (RingWrite % CDM_RING_BYTES);
        if (Piece > Space)
            Piece = Space;
        Remaining = ReadTotal - ReadReceived;
        if (Piece > Remaining)
            Piece = Remaining;

        Words = (uint32_t *)(Ring + (RingWrite % CDM_RING_BYTES));
        Value = (ReadLba * 512) + (ReadReceived / 4);
        if (SyntheticNoPattern == CyFalse)
        {
            for (Index = 0; Index < (Piece / 4); Index++)
                Words[Index] = Value + Index;
        }

        RingWrite += Piece;
        ReadReceived += Piece;
    }

    if ((ReadReceived == ReadTotal) && (ReadComplete == CyFalse))
    {
        ReadStatus = CDM_STATUS_OK;
        ReadComplete = CyTrue;
    }
}

uint32_t CdMediaReadPeek (uint8_t **Pointer, uint32_t Max)
{
    if ((Synthetic == CyTrue) && (ReadActive == CyTrue))
        FillSynthetic ();

    uint32_t Available = RingWrite - RingRead;
    uint32_t ToEnd = CDM_RING_BYTES - (RingRead % CDM_RING_BYTES);

    if (Available > ToEnd)
        Available = ToEnd;
    if (Available > Max)
        Available = Max;

    *Pointer = Ring + (RingRead % CDM_RING_BYTES);
    return (Available);
}

void CdMediaReadConsume (uint32_t Bytes)
{
    RingRead += Bytes;
    CyU3PEventSet (&ProducerEvents, CDM_PRODUCER_SPACE, CYU3P_EVENT_OR);
}

uint32_t CdMediaReadWait (uint32_t TimeoutMs)
{
    uint32_t Flags = 0;

    if (CyU3PEventGet (&ConsumerEvents, CDM_CONSUMER_DATA | CDM_CONSUMER_COMPLETE | CDM_CONSUMER_ABORT, CYU3P_EVENT_OR_CLEAR, &Flags, TimeoutMs) != CY_U3P_SUCCESS)
        return (0);

    return (((Flags & CDM_CONSUMER_DATA) ? CDM_READ_DATA : 0) |
            ((Flags & CDM_CONSUMER_COMPLETE) ? CDM_READ_COMPLETE : 0) |
            ((Flags & CDM_CONSUMER_ABORT) ? CDM_READ_ABORT : 0));
}

CyBool_t CdMediaReadIsComplete (void)
{
    return (ReadComplete);
}

uint8_t CdMediaReadStatus (void)
{
    return (ReadStatus);
}

uint32_t CdMediaReadReceived (void)
{
    return (ReadReceived);
}

void CdMediaReadEnd (CyBool_t SendCancel)
{
    uint16_t Tag = ReadTag;
    CyBool_t Open = ReadActive;

    ReadActive = CyFalse;
    CyU3PEventSet (&ProducerEvents, CDM_PRODUCER_ABORT, CYU3P_EVENT_OR);     /* lets a link thread that waits for space go */

    if ((SendCancel == CyTrue) && (Open == CyTrue))
        ClientLinkSend (1, CDM_TYPE_CANCEL, Tag, NULL, 0, 0);
}
