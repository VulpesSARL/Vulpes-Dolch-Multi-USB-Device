#include "cyu3system.h"
#include "cyu3os.h"
#include "cyu3dma.h"
#include "cyu3error.h"
#include "cyu3usb.h"
#include "main.h"
#include "LINK.h"
#include "BULK.h"
#include "GpifLinkCore.h"
#include "HOSTUPD.h"

/* The PC link over two bulk endpoints. A message block is one USB transfer: the PC writes a block
 * as one transfer to the OUT endpoint, which ends with a short packet and so commits one DMA buffer; this side commits one buffer per
 * block to the IN endpoint. USB retries damaged packets and stalls the sender (NAK) when a buffer is not free, so the link needs no
 * sequence numbers, credits or fragmenting. Blocks of the control channel are answered here, everything else belongs to the Client (CLIENT.c). */

#define LINK_POLL_TIMEOUT_MS             (20)
#define LINK_TX_WAIT_MS                  (200)      /* How long a sender waits for a free transmit buffer before the PC is considered not reading. */

extern void CyFxAppErrorHandler (CyU3PReturnStatus_t apiRetStatus);

static CyU3PDmaChannel RxChannel;                /* MANUAL_IN: OUT endpoint -> CPU. */
static CyU3PDmaChannel TxChannel;                /* MANUAL_OUT: CPU -> IN endpoint. */
static CyU3PMutex LinkLock;                      /* Held by the link thread during each poll, taken by Start and Stop. */
static CyU3PMutex TxLock;                        /* One sender at a time between getting and committing a transmit buffer. */
static CyBool_t ChannelsExist = CyFalse;         /* Written under TxLock. */
static volatile CyBool_t LinkEnabled = CyFalse;
static CyBool_t CheckSelfTested = CyFalse;

static volatile CyBool_t SessionOpen = CyFalse;
static CyBool_t ResetRequested = CyFalse;
static uint32_t LastReceiveTime = 0;
static uint32_t DroppedSends = 0;

#define LINK_CLIENT_STATUS_BYTES    (28)        /* The Client's own status body; the Host's STATUS adds the Client firmware version behind it. */

static uint8_t StatusPayload[LINK_CLIENT_STATUS_BYTES + 4];     /* What the PC is told about the Client; byte 0 = LINK_CLIENT_*. */
static uint32_t ClientFirmwareVersion = 0;

static uint16_t ReadUInt16 (const uint8_t *Data)
{
    return ((uint16_t)(Data[0] | (Data[1] << 8)));
}

static uint32_t ReadUInt32 (const uint8_t *Data)
{
    return ((uint32_t)Data[0] | ((uint32_t)Data[1] << 8) | ((uint32_t)Data[2] << 16) | ((uint32_t)Data[3] << 24));
}

static void WriteUInt16 (uint8_t *Data, uint16_t Value)
{
    Data[0] = (uint8_t)(Value & 0xFF);
    Data[1] = (uint8_t)(Value >> 8);
}

static void WriteUInt32 (uint8_t *Data, uint32_t Value)
{
    Data[0] = (uint8_t)(Value & 0xFF);
    Data[1] = (uint8_t)((Value >> 8) & 0xFF);
    Data[2] = (uint8_t)((Value >> 16) & 0xFF);
    Data[3] = (uint8_t)(Value >> 24);
}

/* Back to the state of a link nobody has talked to yet. The PC does the same when it opens a new session. */
static void ResetSession (void)
{
    SessionOpen = CyFalse;
    ResetRequested = CyFalse;
    LastReceiveTime = CyU3PGetTime ();
}

/* ---- Transmit --------------------------------------------------------------------------------------------------- */

/* Takes the transmit lock and a free transmit buffer. On CyTrue the caller must call EndSend; on CyFalse nothing is held. */
static CyBool_t BeginSend (CyU3PDmaBuffer_t *Buffer)
{
    CyU3PMutexGet (&TxLock, CYU3P_WAIT_FOREVER);
    if (ChannelsExist == CyFalse)
    {
        CyU3PMutexPut (&TxLock);
        return (CyFalse);
    }

    if (CyU3PDmaChannelGetBuffer (&TxChannel, Buffer, LINK_TX_WAIT_MS) != CY_U3P_SUCCESS)
    {
        CyU3PMutexPut (&TxLock);
        if ((DroppedSends % 100) == 0)
            CyU3PDebugPrint (4, "Link: no free transmit buffer, the PC is not reading (%d blocks dropped so far)\r\n", DroppedSends);
        DroppedSends++;
        return (CyFalse);
    }

    return (CyTrue);
}

/* Commits the block now in the buffer (BlockLength bytes, a multiple of 4) as one transfer and releases the transmit lock. A block that
 * would end on a packet boundary gets 4 zero bytes so that a short packet ends the transfer (the PC ignores them). */
static CyBool_t EndSend (CyU3PDmaBuffer_t *Buffer, uint32_t BlockLength)
{
    uint32_t TransferLength = BlockLength;
    CyBool_t Result = CyTrue;

    if ((TransferLength % LINK_PACKET_ALIGN) == 0)
    {
        *(uint32_t *)(Buffer->buffer + TransferLength) = 0;
        TransferLength += 4;
    }

    if (CyU3PDmaChannelCommitBuffer (&TxChannel, TransferLength, 0) != CY_U3P_SUCCESS)
    {
        CyU3PDebugPrint (4, "Link: committing a block failed\r\n");
        Result = CyFalse;
    }

    CyU3PMutexPut (&TxLock);
    return (Result);
}

static void SendMessage (uint8_t Channel, uint8_t Type, uint16_t Tag, const uint8_t *Payload, uint32_t Length)
{
    CyU3PDmaBuffer_t Buffer;
    uint32_t Total;

    if (BeginSend (&Buffer) == CyFalse)
        return;

    Total = GpifLinkBuildMessage (Buffer.buffer, Channel, Type, Tag, Payload, Length);
    EndSend (&Buffer, Total);
}

#if (CY_FX_DEBUG_COMMANDS == 1)
static uint32_t DebugSinkMessages = 0;
static uint32_t DebugSinkBytes = 0;

/* A message with PayloadLength payload bytes that are whatever the buffer held: the speed test measures the path, not the content. The
 * check word is real, so the PC side accepts it like any other block. */
static void SendStaleMessage (uint8_t Type, uint16_t Tag, uint32_t PayloadLength)
{
    CyU3PDmaBuffer_t Buffer;
    uint32_t Padded = (PayloadLength + 3) & ~((uint32_t)3);

    if (BeginSend (&Buffer) == CyFalse)
        return;

    Buffer.buffer[0] = LINK_CHANNEL_CONTROL;
    Buffer.buffer[1] = Type;
    WriteUInt16 (Buffer.buffer + 2, Tag);
    WriteUInt32 (Buffer.buffer + 4, PayloadLength);
    EndSend (&Buffer, 8 + Padded);
}
#endif

static void SendError (uint8_t Code, uint16_t ReferenceTag, uint16_t ReferenceSequence)
{
    uint8_t Payload[6];

    Payload[0] = Code;
    Payload[1] = 0;
    WriteUInt16 (Payload + 2, ReferenceTag);
    WriteUInt16 (Payload + 4, ReferenceSequence);
    SendMessage (LINK_CHANNEL_CONTROL, LINK_CONTROL_ERROR, 0, Payload, sizeof (Payload));
}

static void SendStatus (uint16_t Tag)
{
    /* Byte 1 is the USB speed of the Host device itself towards its PC (0 unknown, 1 full, 2 high, 3 super). */
    StatusPayload[1] = (uint8_t)CyU3PUsbGetSpeed ();
    /* Bytes 28..31: firmware version of the running Client application (0 = none), so the PC follows a Client that restarts. */
    WriteUInt32 (StatusPayload + LINK_CLIENT_STATUS_BYTES, ClientFirmwareVersion);
    SendMessage (LINK_CHANNEL_CONTROL, LINK_CONTROL_STATUS, Tag, StatusPayload, sizeof (StatusPayload));
}

/* ---- Receive ---------------------------------------------------------------------------------------------------- */

/* A complete, checked control message has arrived. */
static void DispatchControl (uint8_t Type, uint16_t Tag, const uint8_t *Payload, uint32_t Length)
{
    uint8_t Reply[16];
    uint8_t ResetResult;

    /* An ERROR is never answered with an ERROR. */
    if (Type == LINK_CONTROL_ERROR)
    {
        CyU3PDebugPrint (4, "Link: the PC reported error %d (tag %d)\r\n", (Length > 0) ? Payload[0] : 0, Tag);
        return;
    }

    if (Type == LINK_CONTROL_HELLO)
    {
        if (Length < 4)
        {
            SendError (LINK_ERROR_BAD_LENGTH, Tag, 0);
            return;
        }

        /* A second HELLO while a session is open is refused. */
        if (SessionOpen == CyTrue)
        {
            SendError (LINK_ERROR_BUSY, Tag, 0);
            return;
        }

        SessionOpen = CyTrue;
        LastReceiveTime = CyU3PGetTime ();
        CyU3PDebugPrint (4, "Link: session opened (PC protocol version 0x%x)\r\n", ReadUInt16 (Payload));

        CyU3PMemSet (Reply, 0, sizeof (Reply));
        WriteUInt16 (Reply, LINK_PROTOCOL_VERSION);
        Reply[2] = LINK_RECEIVE_CAPACITY;
        WriteUInt32 (Reply + 4, LINK_HOST_FIRMWARE_VERSION);
        WriteUInt32 (Reply + 8, ClientFirmwareVersion);               /* 0 = no Client application. */
        WriteUInt32 (Reply + 12, LINK_MAX_MESSAGE_PAYLOAD);
        SendMessage (LINK_CHANNEL_CONTROL, LINK_CONTROL_HELLO_ACK, Tag, Reply, sizeof (Reply));
        return;
    }

    if (Type == LINK_CONTROL_BYE)
    {
        if (SessionOpen == CyTrue)
            ResetRequested = CyTrue;
        return;
    }

    if (SessionOpen == CyFalse)
    {
        SendError (LINK_ERROR_NO_HELLO, Tag, 0);
        return;
    }

    switch (Type)
    {
    case LINK_CONTROL_PING:
        if (Length < 4)
        {
            SendError (LINK_ERROR_BAD_LENGTH, Tag, 0);
            break;
        }
        SendMessage (LINK_CHANNEL_CONTROL, LINK_CONTROL_PONG, Tag, Payload, 4);
        break;

    case LINK_CONTROL_STATUS_REQUEST:
        SendStatus (Tag);
        break;

    case LINK_CONTROL_BULK_ARM:
        if (Length < 4)
        {
            SendError (LINK_ERROR_BAD_LENGTH, Tag, 0);
            break;
        }
        ResetResult = HostBulkArm (ReadUInt32 (Payload));
        if (ResetResult != 0)
            SendError (ResetResult, Tag, 0);
        break;

    case LINK_CONTROL_RESET_CLIENT:
        ResetResult = HostRequestClientReset ();
        if (ResetResult != 0)
            SendError (ResetResult, Tag, 0);
        break;

#if (CY_FX_DEBUG_COMMANDS == 1)
    case LINK_CONTROL_DEBUG_SINK_DATA:
        DebugSinkMessages++;
        DebugSinkBytes += Length;
        break;

    case LINK_CONTROL_DEBUG_SINK_END:
        WriteUInt32 (Reply, DebugSinkMessages);
        WriteUInt32 (Reply + 4, DebugSinkBytes);
        DebugSinkMessages = 0;
        DebugSinkBytes = 0;
        SendMessage (LINK_CHANNEL_CONTROL, LINK_CONTROL_DEBUG_SINK_END, Tag, Reply, 8);
        break;

    case LINK_CONTROL_DEBUG_FAULT:
        if (Length < 8)
        {
            SendError (LINK_ERROR_BAD_LENGTH, Tag, 0);
            break;
        }
        GpifLinkSetFault (ReadUInt32 (Payload), ReadUInt32 (Payload + 4));
        CyU3PDebugPrint (4, "Link: debug command, GPIF fault injection mode %d every %d buffers\r\n", ReadUInt32 (Payload), ReadUInt32 (Payload + 4));
        break;

    case LINK_CONTROL_DEBUG_SOURCE:
        if (Length < 8)
        {
            SendError (LINK_ERROR_BAD_LENGTH, Tag, 0);
            break;
        }
        {
            uint32_t Count = ReadUInt32 (Payload);
            uint32_t Size = ReadUInt32 (Payload + 4);
            uint32_t Index;

            if (Size > LINK_MAX_MESSAGE_PAYLOAD)
                Size = LINK_MAX_MESSAGE_PAYLOAD;
            for (Index = 0; Index < Count; Index++)
                SendStaleMessage (LINK_CONTROL_DEBUG_SOURCE, (uint16_t)Index, Size);
        }
        break;

    case LINK_CONTROL_DEBUG_CRASH:
        if ((Length >= 4) && (ReadUInt32 (Payload) == 2))
            CyFxAppErrorHandler (CY_U3P_ERROR_FAILURE);
        for (;;)
            ;

    case LINK_CONTROL_DEBUG_RESET_HOST:
        CyU3PDebugPrint (4, "Link: debug command, cold reset of the Host\r\n");
        SendStatus (Tag);                   /* so the PC knows it arrived */
        CyU3PThreadSleep (300);             /* let that transfer finish */
        CyU3PDeviceReset (CyFalse);
        break;
#endif

    default:
        SendError (LINK_ERROR_UNKNOWN_TYPE, Tag, 0);
        break;
    }
}

void PcLinkReply (uint8_t Channel, uint8_t Type, uint16_t Tag, const uint8_t *Payload, uint32_t Length)
{
    SendMessage (Channel, Type, Tag, Payload, Length);
}

/* One received transfer: Count bytes in Block (a message block, possibly followed by 4 padding bytes). */
static void ProcessBlock (const uint8_t *Block, uint32_t Count)
{
    uint32_t Length;
    uint32_t BlockLength;
    uint16_t Tag;
    uint8_t Reply;

    if (Count < LINK_MESSAGE_HEADER_SIZE)
    {
        CyU3PDebugPrint (4, "Link: a block that is too short (%d bytes) was dropped\r\n", Count);
        SendError (LINK_ERROR_BAD_LENGTH, 0, 0);
        return;
    }

    Length = ReadUInt32 (Block + 4);
    if (Length > LINK_MAX_MESSAGE_PAYLOAD)
    {
        CyU3PDebugPrint (4, "Link: a block with an impossible length (%d) was dropped\r\n", Length);
        SendError (LINK_ERROR_BAD_LENGTH, 0, 0);
        return;
    }

    BlockLength = LINK_MESSAGE_HEADER_SIZE + ((Length + 3) & ~((uint32_t)3)) + LINK_CHECK_SIZE;
    if ((Count < BlockLength) || (Count > BlockLength + 4))
    {
        CyU3PDebugPrint (4, "Link: a block of %d bytes does not fit its length field (%d) and was dropped\r\n", Count, BlockLength);
        SendError (LINK_ERROR_BAD_LENGTH, 0, 0);
        return;
    }

    Tag = ReadUInt16 (Block + 2);

    LastReceiveTime = CyU3PGetTime ();

    if (Block[0] == LINK_CHANNEL_CONTROL)
    {
        DispatchControl (Block[1], Tag, Block + LINK_MESSAGE_HEADER_SIZE, Length);
        return;
    }

    if (SessionOpen == CyFalse)
    {
        SendError (LINK_ERROR_NO_HELLO, Tag, 0);
        return;
    }

    /* The Host's own firmware update. */
    if (Block[0] == LINK_CHANNEL_HOST_UPDATE)
    {
        HostUpdateOnMessage (Block, BlockLength);
        return;
    }

    /* The Client's update messages only while the Host itself is in update mode. */
    if ((Block[0] == UPDATE_CHANNEL) && (HostUpdateIsActive () == CyFalse))
    {
        HostUpdateRefuseClient ();
        return;
    }

    /* Everything else belongs to the Client: pass the verified block on as it is. */

    Reply = HostForwardToClient (Block, BlockLength);
    if (Reply != 0)
        SendError (Reply, Tag, 0);
}

static void ServiceTimers (void)
{
    if (ResetRequested == CyTrue)
    {
        CyU3PDebugPrint (4, "Link: session closed by the PC\r\n");
        ResetSession ();
        return;
    }

    if (SessionOpen == CyFalse)
        return;

    if ((CyU3PGetTime () - LastReceiveTime) > LINK_PEER_TIMEOUT_MS)
    {
        CyU3PDebugPrint (4, "Link: nothing from the PC for %d ms, session closed\r\n", LINK_PEER_TIMEOUT_MS);
        ResetSession ();
    }
}

#ifdef INPUT_DIAG
/* Input path diagnostics: how long the PC link thread stays away from its queue and how long one block takes (read and cleared by CLIENT.c). */
volatile uint32_t PcLinkDiagMaxGap = 0;
volatile uint32_t PcLinkDiagMaxHandle = 0;
volatile uint32_t PcLinkDiagBlocks = 0;
static uint32_t DiagLastPoll = 0;
#endif

/* One pass of the link thread: take a block if one arrives within a few milliseconds, then service the timers. */
static void Poll (void)
{
    static uint32_t ErrorCount = 0;
    CyU3PDmaBuffer_t Buffer;
    CyU3PReturnStatus_t Status;
#ifdef INPUT_DIAG
    uint32_t DiagNow = CyU3PGetTime ();

    if ((DiagLastPoll != 0) && ((DiagNow - DiagLastPoll) > PcLinkDiagMaxGap))
        PcLinkDiagMaxGap = DiagNow - DiagLastPoll;
    DiagLastPoll = DiagNow;
#endif

    Status = CyU3PDmaChannelGetBuffer (&RxChannel, &Buffer, LINK_POLL_TIMEOUT_MS);
    if (Status == CY_U3P_SUCCESS)
    {
#ifdef INPUT_DIAG
        uint32_t DiagStart = CyU3PGetTime ();
#endif
        ProcessBlock (Buffer.buffer, Buffer.count);
        CyU3PDmaChannelDiscardBuffer (&RxChannel);
#ifdef INPUT_DIAG
        PcLinkDiagBlocks++;
        if ((CyU3PGetTime () - DiagStart) > PcLinkDiagMaxHandle)
            PcLinkDiagMaxHandle = CyU3PGetTime () - DiagStart;
#endif
    }
    else if (Status != CY_U3P_ERROR_TIMEOUT)
    {
        if ((ErrorCount % 100) == 0)
            CyU3PDebugPrint (4, "Link: waiting for a block failed, Error code = %d\r\n", Status);
        ErrorCount++;
        CyU3PThreadSleep (5);
    }

    ServiceTimers ();
}

/* ---- Setup ------------------------------------------------------------------------------------------------------ */

static CyBool_t CreateChannels (void)
{
    CyU3PDmaChannelConfig_t DmaConfig;
    CyU3PReturnStatus_t Status;

    CyU3PMemSet ((uint8_t *)&DmaConfig, 0, sizeof (DmaConfig));
    DmaConfig.size = CY_FX_LINK_DMA_BUF_SIZE;
    DmaConfig.count = CY_FX_LINK_DMA_BUF_COUNT;
    DmaConfig.prodSckId = CY_FX_EP_PRODUCER_SOCKET;
    DmaConfig.consSckId = CY_U3P_CPU_SOCKET_CONS;
    DmaConfig.dmaMode = CY_U3P_DMA_MODE_BYTE;
    DmaConfig.notification = 0;
    DmaConfig.cb = NULL;

    Status = CyU3PDmaChannelCreate (&RxChannel, CY_U3P_DMA_TYPE_MANUAL_IN, &DmaConfig);
    if (Status != CY_U3P_SUCCESS)
    {
        CyU3PDebugPrint (4, "Link: CyU3PDmaChannelCreate (receive) failed, Error code = %d\r\n", Status);
        return (CyFalse);
    }

    CyU3PMemSet ((uint8_t *)&DmaConfig, 0, sizeof (DmaConfig));
    DmaConfig.size = CY_FX_LINK_DMA_BUF_SIZE;
    DmaConfig.count = CY_FX_LINK_DMA_BUF_COUNT;
    DmaConfig.prodSckId = CY_U3P_CPU_SOCKET_PROD;
    DmaConfig.consSckId = CY_FX_EP_CONSUMER_SOCKET;
    DmaConfig.dmaMode = CY_U3P_DMA_MODE_BYTE;
    DmaConfig.notification = 0;
    DmaConfig.cb = NULL;

    Status = CyU3PDmaChannelCreate (&TxChannel, CY_U3P_DMA_TYPE_MANUAL_OUT, &DmaConfig);
    if (Status != CY_U3P_SUCCESS)
    {
        CyU3PDebugPrint (4, "Link: CyU3PDmaChannelCreate (send) failed, Error code = %d\r\n", Status);
        CyU3PDmaChannelDestroy (&RxChannel);
        return (CyFalse);
    }

    Status = CyU3PDmaChannelSetXfer (&RxChannel, 0);
    if (Status == CY_U3P_SUCCESS)
        Status = CyU3PDmaChannelSetXfer (&TxChannel, 0);
    if (Status != CY_U3P_SUCCESS)
    {
        CyU3PDebugPrint (4, "Link: CyU3PDmaChannelSetXfer failed, Error code = %d\r\n", Status);
        CyU3PDmaChannelDestroy (&TxChannel);
        CyU3PDmaChannelDestroy (&RxChannel);
        return (CyFalse);
    }

    return (CyTrue);
}

void PcLinkInit (void)
{
    CyU3PMutexCreate (&LinkLock, CYU3P_NO_INHERIT);
    CyU3PMutexCreate (&TxLock, CYU3P_NO_INHERIT);
}

CyBool_t PcLinkStart (void)
{
    CyBool_t Result = CyFalse;

    CyU3PMutexGet (&LinkLock, CYU3P_WAIT_FOREVER);

    if (ChannelsExist == CyFalse)
    {
        ResetSession ();
        CyU3PMutexGet (&TxLock, CYU3P_WAIT_FOREVER);
        if (CreateChannels () == CyTrue)
        {
            ChannelsExist = CyTrue;
            LinkEnabled = CyTrue;
            Result = CyTrue;
        }
        CyU3PMutexPut (&TxLock);
    }

    CyU3PMutexPut (&LinkLock);
    return (Result);
}

void PcLinkStop (void)
{
    LinkEnabled = CyFalse;

    /* The link thread holds the lock during each poll, so this returns once it has finished the current pass. */
    CyU3PMutexGet (&LinkLock, CYU3P_WAIT_FOREVER);
    CyU3PMutexGet (&TxLock, CYU3P_WAIT_FOREVER);
    if (ChannelsExist == CyTrue)
    {
        CyU3PDmaChannelDestroy (&RxChannel);
        CyU3PDmaChannelDestroy (&TxChannel);
        ChannelsExist = CyFalse;
    }
    SessionOpen = CyFalse;
    CyU3PMutexPut (&TxLock);
    CyU3PMutexPut (&LinkLock);
}

void PcLinkSetClientStatus (const uint8_t *StatusBody, uint32_t NewClientFirmwareVersion)
{
    CyU3PMemCopy (StatusPayload, (uint8_t *)StatusBody, LINK_CLIENT_STATUS_BYTES);
    ClientFirmwareVersion = NewClientFirmwareVersion;
    if (SessionOpen == CyTrue)
        SendStatus (0);
}

CyBool_t PcLinkIsSessionOpen (void)
{
    return (SessionOpen);
}

CyBool_t PcLinkForwardToPc (const uint8_t *Block, uint32_t Length)
{
    CyU3PDmaBuffer_t Buffer;

    if ((SessionOpen == CyFalse) || (Length > LINK_MAX_BLOCK_SIZE) || ((Length & 3) != 0))
        return (CyFalse);

    if (BeginSend (&Buffer) == CyFalse)
        return (CyFalse);

    GpifLinkCopyBytes (Buffer.buffer, Block, Length);
    return (EndSend (&Buffer, Length));
}

void PcLinkThreadEntry (uint32_t Input)
{
    for (;;)
    {
        CyU3PMutexGet (&LinkLock, CYU3P_WAIT_FOREVER);
        if (LinkEnabled == CyTrue)
            Poll ();
        CyU3PMutexPut (&LinkLock);

        if (LinkEnabled == CyFalse)
            CyU3PThreadSleep (10);
    }
}
