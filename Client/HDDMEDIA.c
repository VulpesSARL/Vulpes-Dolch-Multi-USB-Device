#include "cyu3system.h"
#include "cyu3os.h"
#include "cyu3dma.h"
#include "cyu3error.h"
#include "GpifLinkCore.h"
#include "HDDMEDIA.h"

extern void CyFxClientUsbDiskChanged (CyBool_t Present);       /* main.c */

#if (CLIENT_WITH_HDD == 0)

/* The bridge build: no HDD, only what the rest of the firmware calls. */
void HddMediaInit (void)
{
}

void HddMediaOnMessage (const uint8_t *Block, uint32_t Length)
{
}

void HddMediaOnPcLink (uint8_t Up)
{
}

CyBool_t HddMediaIsPresent (void)
{
    return (CyFalse);
}

CyBool_t HddMediaIsReadOnly (void)
{
    return (CyFalse);
}

void HddMediaGetId (uint32_t *Low, uint32_t *High)
{
    *Low = 0;
    *High = 0;
}

uint8_t *HddMediaTakeBuffer (uint32_t *Bytes)
{
    return (NULL);
}

#else

/* See HDDMEDIA.h. Threads: the link thread runs HddMediaOnMessage (and so appends read DATA to the ring); the HDD thread runs the SCSI commands
 * and does the requests. The medium state is protected by a mutex that is only ever held for a few instructions. */

#define HDM_CHANNEL                  (2)
#define HDM_ATTACH_BYTES             (24)
#define HDM_SCRATCH_BYTES            (4096)
#define HDM_RING_WAIT_MS             (2000)     /* The link thread waits this long for the USB side to free ring space. */
#define HDM_SEND_WAIT_MS             (10000)    /* A piece of write data waits this long for a free slot in the GPIF link window. */

#define HDM_ERROR_BAD_LENGTH         (3)
#define HDM_ERROR_MEDIA_ID_MISMATCH  (4)
#define HDM_ERROR_UNKNOWN_TYPE       (2)

#define HDM_CONSUMER_DATA            (0x01)
#define HDM_CONSUMER_COMPLETE        (0x02)
#define HDM_CONSUMER_ABORT           (0x04)
#define HDM_PRODUCER_SPACE           (0x01)
#define HDM_PRODUCER_ABORT           (0x02)

#define HDM_KIND_READ                (0)
#define HDM_KIND_OTHER               (1)        /* write, flush, unmap: never resumed after the PC went away */

/* Provided by LINKCLIENT.c. */
extern CyBool_t ClientLinkSend (uint8_t Channel, uint8_t Type, uint16_t Tag, const uint8_t *Payload, uint32_t Length, uint32_t TimeoutMs);
extern void ClientLinkAnnounceStatus (void);
extern void ClientLinkSendError (uint8_t Code, uint16_t ReferenceTag);

static CyU3PMutex StateLock;
static CyBool_t Initialized = CyFalse;

/* The medium. */
static volatile CyBool_t Present = CyFalse;
static uint8_t MediaName[HDM_NAME_BYTES];       /* Name of the image the PC gave, RAM only. */
static uint32_t MediaNameLength = 0;
static volatile CyBool_t Locked = CyFalse;
static volatile CyBool_t ReadOnly = CyFalse;
static uint32_t MediaIdLow = 0;
static uint32_t MediaIdHigh = 0;
static uint32_t SectorsLow = 0;
static uint32_t SectorsHigh = 0;
static volatile uint32_t ChangeFlags = 0;
static volatile uint8_t PcUp = 0;

static uint8_t *Scratch = NULL;

/* The request in progress (one at a time). */
static uint8_t *Ring = NULL;
static volatile uint32_t RingWrite = 0;          /* Total bytes put into the ring (the link thread writes this). */
static volatile uint32_t RingRead = 0;           /* Total bytes taken out of it (the HDD thread writes this). */
static CyU3PEvent ConsumerEvents;
static CyU3PEvent ProducerEvents;
static volatile CyBool_t Active = CyFalse;
static volatile uint8_t Kind = HDM_KIND_READ;
static volatile uint16_t Tag = 0;
static volatile uint32_t ReadTotal = 0;
static volatile uint32_t ReadReceived = 0;
static volatile uint8_t Status = HDM_STATUS_NONE;
static volatile CyBool_t Complete = CyFalse;
static uint32_t ReadLbaLow = 0;
static uint32_t ReadLbaHigh = 0;
static uint32_t ReadCount = 0;
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

void HddMediaInit (void)
{
    /* CyU3PDmaBufferAlloc takes a 16 bit size. */
    if (HDM_RING_BYTES > 65535)
    {
        CyU3PDebugPrint (4, "HDD: the ring is too big for CyU3PDmaBufferAlloc\r\n");
        return;
    }

    Ring = (uint8_t *)CyU3PDmaBufferAlloc (HDM_RING_BYTES);
    Scratch = (uint8_t *)CyU3PDmaBufferAlloc (HDM_SCRATCH_BYTES);
    if ((Ring == NULL) || (Scratch == NULL))
    {
        CyU3PDebugPrint (4, "HDD: no memory for the ring\r\n");
        return;
    }

    CyU3PMutexCreate (&StateLock, CYU3P_NO_INHERIT);
    CyU3PEventCreate (&ConsumerEvents);
    CyU3PEventCreate (&ProducerEvents);
    Initialized = CyTrue;
}

/* ---- State queries --------------------------------------------------------------------------------------------------------- */

uint8_t *HddMediaTakeBuffer (uint32_t *Bytes)
{
    if (Ring == NULL)
        return (NULL);

    Initialized = CyFalse;
    *Bytes = HDM_RING_BYTES;
    return (Ring);
}

CyBool_t HddMediaIsPresent (void)
{
    return (Present);
}

CyBool_t HddMediaIsLocked (void)
{
    return (Locked);
}

CyBool_t HddMediaIsReadOnly (void)
{
    return (ReadOnly);
}

void HddMediaGetSectorCount (uint32_t *Low, uint32_t *High)
{
    *Low = SectorsLow;
    *High = SectorsHigh;
}

void HddMediaGetId (uint32_t *Low, uint32_t *High)
{
    *Low = MediaIdLow;
    *High = MediaIdHigh;
}

CyBool_t HddMediaPcIsUp (void)
{
    return ((PcUp != 0) ? CyTrue : CyFalse);
}

uint32_t HddMediaTakeChanges (void)
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

uint8_t *HddMediaScratch (void)
{
    return (Scratch);
}

/* ---- Messages to the PC ---------------------------------------------------------------------------------------------------- */

static void SendDiskState (uint16_t StateTag, uint8_t Event)
{
    uint8_t Payload[12];

    CyU3PMemSet (Payload, 0, sizeof (Payload));
    Payload[0] = (Present == CyTrue) ? 1 : 0;
    Payload[1] = (Locked == CyTrue) ? 1 : 0;
    Payload[2] = Event;
    Payload[3] = (ReadOnly == CyTrue) ? 1 : 0;
    WriteWord (Payload + 4, MediaIdLow);
    WriteWord (Payload + 8, MediaIdHigh);
    ClientLinkSend (HDM_CHANNEL, HDM_TYPE_DISK_STATE, StateTag, Payload, sizeof (Payload), 0);
}

/* Wakes whoever waits for the request in progress and makes it fail. */
static void AbortRequest (void)
{
    if (Active == CyFalse)
        return;

    CyU3PEventSet (&ConsumerEvents, HDM_CONSUMER_ABORT, CYU3P_EVENT_OR);
    CyU3PEventSet (&ProducerEvents, HDM_PRODUCER_ABORT, CYU3P_EVENT_OR);
}

/* The disk is gone (detached by the PC or ejected by the OS). Event says why for the DISK_STATE that tells the PC. */
static void DropMedium (uint16_t StateTag, uint8_t Event)
{
    CyU3PMutexGet (&StateLock, CYU3P_WAIT_FOREVER);
    Present = CyFalse;
    MediaNameLength = 0;
    Locked = CyFalse;
    ChangeFlags |= HDM_CHANGE_REMOVED;
    CyU3PMutexPut (&StateLock);

    AbortRequest ();
    SendDiskState (StateTag, Event);
    ClientLinkAnnounceStatus ();

    /* The disk leaves the USB configuration: the target PC sees it disappear. */
    CyFxClientUsbDiskChanged (CyFalse);
}

/* ---- Messages from the PC (link thread) ------------------------------------------------------------------------------------- */

static void HandleAttach (uint16_t MessageTag, const uint8_t *Payload, uint32_t Length)
{
    uint32_t NewLow;
    uint32_t NewHigh;
    uint32_t NewSectorsLow;
    uint32_t NewSectorsHigh;
    uint8_t Flags;

    if (Length < HDM_ATTACH_BYTES)
    {
        ClientLinkSendError (HDM_ERROR_BAD_LENGTH, MessageTag);
        return;
    }

    NewLow = ReadWord (Payload);
    NewHigh = ReadWord (Payload + 4);
    NewSectorsLow = ReadWord (Payload + 8);
    NewSectorsHigh = ReadWord (Payload + 12);
    Flags = Payload[20];

    /* The PC reconnects to a disk the Client already holds: nothing changes, the OS must not notice. */
    if ((Flags & HDM_FLAG_REATTACH) != 0)
    {
        if ((Present == CyTrue) && (NewLow == MediaIdLow) && (NewHigh == MediaIdHigh))
        {
            SendDiskState (MessageTag, HDM_EVENT_NONE);
            return;
        }

        ClientLinkSendError (HDM_ERROR_MEDIA_ID_MISMATCH, MessageTag);
        return;
    }

    /* Only 512 byte sectors, and the disk must have at least one sector (a bad message leaves the old disk alone). */
    if ((ReadWord (Payload + 16) != HDM_SECTOR_BYTES) || ((NewSectorsLow == 0) && (NewSectorsHigh == 0)))
    {
        ClientLinkSendError (HDM_ERROR_BAD_LENGTH, MessageTag);
        return;
    }

    /* A request for the old disk cannot be finished any more. */
    AbortRequest ();

    CyU3PMutexGet (&StateLock, CYU3P_WAIT_FOREVER);
    MediaIdLow = NewLow;
    MediaIdHigh = NewHigh;
    SectorsLow = NewSectorsLow;
    SectorsHigh = NewSectorsHigh;
    ReadOnly = ((Flags & HDM_FLAG_READ_ONLY) != 0) ? CyTrue : CyFalse;
    Present = CyTrue;
    Locked = CyFalse;
    ChangeFlags |= HDM_CHANGE_INSERTED;
    CyU3PMutexPut (&StateLock);

    CyU3PDebugPrint (4, "HDD: disk attached (%d sectors, %s)\r\n", SectorsLow, (ReadOnly == CyTrue) ? "read-only" : "read-write");
    SendDiskState (MessageTag, HDM_EVENT_NONE);
    ClientLinkAnnounceStatus ();

    /* The disk enters the USB configuration (a swap restarts it as well): the target PC sees a new disk device. */
    CyFxClientUsbDiskChanged (CyTrue);
}

static void HandleDetach (uint16_t MessageTag, const uint8_t *Payload, uint32_t Length)
{
    CyBool_t Force = ((Length >= 1) && (Payload[0] != 0)) ? CyTrue : CyFalse;

    if (Present == CyFalse)
    {
        SendDiskState (MessageTag, HDM_EVENT_NONE);
        return;
    }

    /* The OS holds the lock: only the user-confirmed override gets through. */
    if ((Locked == CyTrue) && (Force == CyFalse))
    {
        SendDiskState (MessageTag, HDM_EVENT_EJECT_REFUSED);
        return;
    }

    CyU3PDebugPrint (4, "HDD: disk detached by the PC\r\n");
    DropMedium (MessageTag, HDM_EVENT_NONE);
}

static void HandleData (uint16_t MessageTag, const uint8_t *Payload, uint32_t Length)
{
    uint32_t Space;
    uint32_t Piece;
    uint32_t Flags;

    if ((Active == CyFalse) || (Kind != HDM_KIND_READ) || (MessageTag != Tag))
        return;

    if (Length > (ReadTotal - ReadReceived))
        Length = ReadTotal - ReadReceived;

    while (Length > 0)
    {
        Space = HDM_RING_BYTES - (RingWrite - RingRead);
        if (Space == 0)
        {
            /* The USB side has not taken what it was given: wait a bounded time for it, then give the read up. */
            if (CyU3PEventGet (&ProducerEvents, HDM_PRODUCER_SPACE | HDM_PRODUCER_ABORT, CYU3P_EVENT_OR_CLEAR, &Flags, HDM_RING_WAIT_MS) != CY_U3P_SUCCESS)
            {
                CyU3PDebugPrint (4, "HDD: the USB side did not take data for %d ms, the read is dropped\r\n", HDM_RING_WAIT_MS);
                Status = HDM_STATUS_FAILED;
                Complete = CyTrue;
                CyU3PEventSet (&ConsumerEvents, HDM_CONSUMER_COMPLETE, CYU3P_EVENT_OR);
                return;
            }

            if ((Flags & HDM_PRODUCER_ABORT) != 0)
                return;
            continue;
        }

        Piece = HDM_RING_BYTES - (RingWrite % HDM_RING_BYTES);
        if (Piece > Space)
            Piece = Space;
        if (Piece > Length)
            Piece = Length;

        GpifLinkCopyBytes (Ring + (RingWrite % HDM_RING_BYTES), Payload, Piece);
        RingWrite += Piece;
        ReadReceived += Piece;
        Payload += Piece;
        Length -= Piece;
        CyU3PEventSet (&ConsumerEvents, HDM_CONSUMER_DATA, CYU3P_EVENT_OR);
    }
}

static void HandleComplete (uint16_t MessageTag, const uint8_t *Payload, uint32_t Length)
{
    if ((Active == CyFalse) || (MessageTag != Tag) || (Length < 1))
        return;

    Status = Payload[0];
    Complete = CyTrue;
    CyU3PEventSet (&ConsumerEvents, HDM_CONSUMER_COMPLETE, CYU3P_EVENT_OR);
}

void HddMediaOnMessage (const uint8_t *Block, uint32_t Length)
{
    uint8_t Type = Block[1];
    uint16_t MessageTag = (uint16_t)(Block[2] | (Block[3] << 8));
    uint32_t PayloadLength = ReadWord (Block + 4);
    const uint8_t *Payload = Block + 8;

    if (Initialized == CyFalse)
        return;

    switch (Type)
    {
    case HDM_TYPE_DISK_ATTACH:
        HandleAttach (MessageTag, Payload, PayloadLength);
        break;

    case HDM_TYPE_DISK_DETACH:
        HandleDetach (MessageTag, Payload, PayloadLength);
        break;

    case HDM_TYPE_DATA:
        HandleData (MessageTag, Payload, PayloadLength);
        break;

    case HDM_TYPE_COMPLETE:
        HandleComplete (MessageTag, Payload, PayloadLength);
        break;

    case HDM_TYPE_SET_NAME:
        CyU3PMutexGet (&StateLock, CYU3P_WAIT_FOREVER);
        if (Present == CyTrue)
        {
            MediaNameLength = (PayloadLength > HDM_NAME_BYTES) ? HDM_NAME_BYTES : PayloadLength;
            CyU3PMemCopy (MediaName, (uint8_t *)Payload, MediaNameLength);
        }
        CyU3PMutexPut (&StateLock);
        break;

    case HDM_TYPE_NAME_REQUEST:
        ClientLinkSend (HDM_CHANNEL, HDM_TYPE_NAME, MessageTag, MediaName, (Present == CyTrue) ? MediaNameLength : 0, 0);
        break;

    default:
        ClientLinkSendError (HDM_ERROR_UNKNOWN_TYPE, MessageTag);
        break;
    }
}

/* ---- The OS changes the drive (HDD thread) ----------------------------------------------------------------------------------- */

void HddMediaSetLock (CyBool_t Prevent)
{
    if (Present == CyFalse)
        return;

    if (Locked == Prevent)
        return;

    Locked = Prevent;
    CyU3PDebugPrint (4, "HDD: lock %d\r\n", Prevent);
    SendDiskState (0, HDM_EVENT_LOCK_CHANGED);
    ClientLinkAnnounceStatus ();
}

CyBool_t HddMediaEject (void)
{
    if (Present == CyFalse)
        return (CyTrue);

    if (Locked == CyTrue)
    {
        SendDiskState (0, HDM_EVENT_EJECT_REFUSED);
        return (CyFalse);
    }

    CyU3PDebugPrint (4, "HDD: disk ejected by the OS\r\n");
    DropMedium (0, HDM_EVENT_EJECTED_BY_OS);
    return (CyTrue);
}

void HddMediaBusReset (void)
{
    if (Initialized == CyFalse)
        return;

    SendDiskState (0, HDM_EVENT_BUS_RESET);
}

/* The read is asked for again (reads can be repeated safely). */
/* Number of 16 KB buffers the PC is to send over the dedicated bulk data lane (GPIF threads 2 / 3, Host BULK.c) instead of as DATA messages: the first
 * ReadBulkBuffers * 16384 bytes of the read; 0 = everything as messages. Set by the storage thread before HddMediaReadBegin. */
static uint32_t ReadBulkBuffers = 0;

void HddMediaReadBulkHint (uint32_t Buffers)
{
    ReadBulkBuffers = Buffers;
}

static void ResendReadRequest (uint32_t TimeoutMs)
{
    uint8_t Payload[16];

    CyU3PMemSet (Payload, 0, sizeof (Payload));
    WriteWord (Payload, ReadLbaLow);
    WriteWord (Payload + 4, ReadLbaHigh);
    WriteWord (Payload + 8, ReadCount);
    WriteWord (Payload + 12, ReadBulkBuffers);
    ClientLinkSend (HDM_CHANNEL, HDM_TYPE_READ_REQUEST, Tag, Payload, sizeof (Payload), TimeoutMs);
}

void HddMediaOnPcLink (uint8_t Up)
{
    PcUp = Up;

    if (Initialized == CyFalse)
        return;

    if (Up == 0)
    {
        /* A write, flush or unmap cannot be continued after the PC lost its state: it fails, the OS decides what to do. */
        if ((Active == CyTrue) && (Kind == HDM_KIND_OTHER))
            AbortRequest ();
        return;
    }

    if ((Active == CyTrue) && (Kind == HDM_KIND_READ) && (ReadReceived == 0) && (Complete == CyFalse))
        ResendReadRequest (0);
}

/* ---- Requests (HDD thread) ---------------------------------------------------------------------------------------------------- */

/* Starts a request of the given kind; whatever a former one left behind is dropped (its late messages carry another tag and are ignored). */
static void BeginRequest (uint8_t NewKind)
{
    uint32_t Flags;

    Active = CyFalse;
    RingRead = RingWrite = 0;
    CyU3PEventGet (&ConsumerEvents, HDM_CONSUMER_DATA | HDM_CONSUMER_COMPLETE | HDM_CONSUMER_ABORT, CYU3P_EVENT_OR_CLEAR, &Flags, CYU3P_NO_WAIT);
    CyU3PEventGet (&ProducerEvents, HDM_PRODUCER_SPACE | HDM_PRODUCER_ABORT, CYU3P_EVENT_OR_CLEAR, &Flags, CYU3P_NO_WAIT);

    TagCounter++;
    if (TagCounter == 0)
        TagCounter = 1;
    Tag = TagCounter;
    Kind = NewKind;
    ReadTotal = 0;
    ReadReceived = 0;
    Status = HDM_STATUS_NONE;
    Complete = CyFalse;
    Active = CyTrue;
}

CyBool_t HddMediaReadBegin (uint32_t LbaLow, uint32_t LbaHigh, uint32_t Count, uint32_t TotalBytes)
{
    if ((Initialized == CyFalse) || (Present == CyFalse))
        return (CyFalse);

    BeginRequest (HDM_KIND_READ);
    ReadTotal = TotalBytes;
    ReadLbaLow = LbaLow;
    ReadLbaHigh = LbaHigh;
    ReadCount = Count;
    ResendReadRequest (2000);
    return (CyTrue);
}

uint32_t HddMediaReadPeek (uint8_t **Pointer, uint32_t Max)
{
    uint32_t Available = RingWrite - RingRead;
    uint32_t ToEnd = HDM_RING_BYTES - (RingRead % HDM_RING_BYTES);

    if (Available > ToEnd)
        Available = ToEnd;
    if (Available > Max)
        Available = Max;

    *Pointer = Ring + (RingRead % HDM_RING_BYTES);
    return (Available);
}

void HddMediaReadConsume (uint32_t Bytes)
{
    RingRead += Bytes;
    CyU3PEventSet (&ProducerEvents, HDM_PRODUCER_SPACE, CYU3P_EVENT_OR);
}

uint32_t HddMediaWait (uint32_t TimeoutMs)
{
    uint32_t Flags = 0;

    if (CyU3PEventGet (&ConsumerEvents, HDM_CONSUMER_DATA | HDM_CONSUMER_COMPLETE | HDM_CONSUMER_ABORT, CYU3P_EVENT_OR_CLEAR, &Flags, TimeoutMs) != CY_U3P_SUCCESS)
        return (0);

    return (((Flags & HDM_CONSUMER_DATA) ? HDM_WAIT_DATA : 0) |
            ((Flags & HDM_CONSUMER_COMPLETE) ? HDM_WAIT_COMPLETE : 0) |
            ((Flags & HDM_CONSUMER_ABORT) ? HDM_WAIT_ABORT : 0));
}

CyBool_t HddMediaIsComplete (void)
{
    return (Complete);
}

uint8_t HddMediaStatus (void)
{
    return (Status);
}

uint32_t HddMediaReadReceived (void)
{
    return (ReadReceived);
}

void HddMediaReadEnd (CyBool_t SendCancel)
{
    uint16_t EndTag = Tag;
    CyBool_t Open = Active;

    Active = CyFalse;
    CyU3PEventSet (&ProducerEvents, HDM_PRODUCER_ABORT, CYU3P_EVENT_OR);     /* lets a link thread that waits for space go */

    if ((SendCancel == CyTrue) && (Open == CyTrue))
        ClientLinkSend (HDM_CHANNEL, HDM_TYPE_CANCEL, EndTag, NULL, 0, 0);
}

/* ---- Writes, flush, unmap ----------------------------------------------------------------------------------------------------- */

CyBool_t HddMediaWriteBegin (uint32_t LbaLow, uint32_t LbaHigh, uint32_t Count)
{
    uint8_t Payload[16];

    if ((Initialized == CyFalse) || (Present == CyFalse) || (PcUp == 0))
        return (CyFalse);

    BeginRequest (HDM_KIND_OTHER);

    CyU3PMemSet (Payload, 0, sizeof (Payload));
    WriteWord (Payload, LbaLow);
    WriteWord (Payload + 4, LbaHigh);
    WriteWord (Payload + 8, Count);
    if (ClientLinkSend (HDM_CHANNEL, HDM_TYPE_WRITE_REQUEST, Tag, Payload, sizeof (Payload), 2000) == CyFalse)
    {
        Active = CyFalse;
        return (CyFalse);
    }

    return (CyTrue);
}

/* Write data goes straight into GPIF lane buffers: USB receives into the payload area (32 byte aligned) of a buffer taken from the lane
 * (GpifLinkTxGet), the header is filled in and the very same buffer is committed - the CPU never copies the data. WriteMessage[] holds the message
 * start of the buffer taken for each slot (NULL = none). The lane transmit lock stays with the storage thread from the first take to the last
 * commit, so this is only for the one write command that is running. */
static uint8_t *WriteMessage[2];

uint8_t *HddMediaWriteBufferAt (uint32_t Slot)
{
    uint8_t *Message;

    Slot &= 1;
    if (WriteMessage[Slot] == NULL)
    {
        if (GpifLinkTxGet (HDM_SEND_WAIT_MS, &Message) == CyFalse)
            return (NULL);

        WriteMessage[Slot] = Message;
    }

    return (WriteMessage[Slot] + 8);
}

uint8_t *HddMediaWriteBuffer (void)
{
    return (HddMediaWriteBufferAt (0));
}

CyBool_t HddMediaWriteSend (uint32_t Bytes)
{
    return (HddMediaWriteSendAt (0, Bytes));
}

CyBool_t HddMediaWriteSendAt (uint32_t Slot, uint32_t Bytes)
{
    uint8_t *Block = WriteMessage[Slot & 1];

    if ((Block == NULL) || (Active == CyFalse) || (Bytes == 0) || (Bytes > HDM_WRITE_PIECE_BYTES) || ((Bytes & 3) != 0))
        return (CyFalse);

    /* The sector data is already in place behind the header: add the header and ship the buffer (the lane has no check word). */
    Block[0] = HDM_CHANNEL;
    Block[1] = HDM_TYPE_WRITE_DATA;
    Block[2] = (uint8_t)(Tag & 0xFF);
    Block[3] = (uint8_t)(Tag >> 8);
    WriteWord (Block + 4, Bytes);
    WriteMessage[Slot & 1] = NULL;
    return (GpifLinkTxCommit ());
}

CyBool_t HddMediaFlushBegin (void)
{
    if ((Initialized == CyFalse) || (Present == CyFalse) || (PcUp == 0))
        return (CyFalse);

    BeginRequest (HDM_KIND_OTHER);
    if (ClientLinkSend (HDM_CHANNEL, HDM_TYPE_FLUSH_REQUEST, Tag, NULL, 0, 2000) == CyFalse)
    {
        Active = CyFalse;
        return (CyFalse);
    }

    return (CyTrue);
}

CyBool_t HddMediaUnmapBegin (uint32_t LbaLow, uint32_t LbaHigh, uint32_t Count, uint8_t Mode)
{
    uint8_t Payload[16];

    if ((Initialized == CyFalse) || (Present == CyFalse) || (PcUp == 0))
        return (CyFalse);

    BeginRequest (HDM_KIND_OTHER);

    CyU3PMemSet (Payload, 0, sizeof (Payload));
    WriteWord (Payload, LbaLow);
    WriteWord (Payload + 4, LbaHigh);
    WriteWord (Payload + 8, Count);
    Payload[12] = Mode;
    if (ClientLinkSend (HDM_CHANNEL, HDM_TYPE_UNMAP_REQUEST, Tag, Payload, sizeof (Payload), 2000) == CyFalse)
    {
        Active = CyFalse;
        return (CyFalse);
    }

    return (CyTrue);
}

uint8_t HddMediaWaitComplete (uint32_t TimeoutMs)
{
    uint32_t Flags = 0;
    uint8_t Result;

    if (Active == CyFalse)
        return (HDM_STATUS_NONE);

    if ((Complete == CyFalse) &&
        (CyU3PEventGet (&ConsumerEvents, HDM_CONSUMER_COMPLETE | HDM_CONSUMER_ABORT, CYU3P_EVENT_OR_CLEAR, &Flags, TimeoutMs) != CY_U3P_SUCCESS))
    {
        Active = CyFalse;
        return (HDM_STATUS_NONE);
    }

    Result = (Complete == CyTrue) ? Status : HDM_STATUS_NONE;
    Active = CyFalse;
    return (Result);
}

void HddMediaRequestEnd (void)
{
    /* A write that ends early gives its lane buffers back (empty ones, the receiver drops them). */
    if ((WriteMessage[0] != NULL) || (WriteMessage[1] != NULL))
    {
        WriteMessage[0] = NULL;
        WriteMessage[1] = NULL;
        GpifLinkTxCancel ();
    }

    Active = CyFalse;
    CyU3PEventSet (&ProducerEvents, HDM_PRODUCER_ABORT, CYU3P_EVENT_OR);
}

#endif /* CLIENT_WITH_HDD */
