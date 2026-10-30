#include "cyu3system.h"
#include "cyu3os.h"
#include "cyu3dma.h"
#include "cyu3error.h"
#include "cyu3gpif.h"
#include "cyu3pib.h"
#include "cyu3usb.h"

/* Structure definitions: include once. */
#include "gpif_device.h"
#include "GpifLinkCore.h"
#include "UPDATE.h"
#include "CDMEDIA.h"
#include "HDDMEDIA.h"
#include "PERIPH.h"
#include "../Version/FirmwareVersionTag.h"

/* The Client's side of the PC <-> Host <-> Client link: GPIF slave bring-up, the GPIF lane, and the message handler. The Host owns
 * the Client's lifecycle; this side announces itself with CLIENT_STATUS, answers the Host's PING and serves the function channels.
 * The test messages of channel 1 (echo, sink, source) check the whole chain from the PC. Everything else is answered with an ERROR. */

#define CLIENT_FIRMWARE_VERSION      (FW_VERSION_PACKED)   /* The build stamp, see Version/FirmwareVersionTag.h. */
#define CLIENT_LINK_STACK            (0x1000)
#define CLIENT_HELPER_STACK          (0x1000)
#define CLIENT_THREAD_PRIORITY       (11)     /* Below the guard thread (10): the lane thread runs flat out under bulk traffic and must not starve the watchdog clear. */
/* Two 16 KB buffers per direction on the Client (the Host has three): the Client's DMA area also holds the CD and HDD rings. */
#ifndef CLIENT_CHANNEL_BUFFERS
#define CLIENT_CHANNEL_BUFFERS       (2)
#endif

#define CONTROL_PING                 (0x04)
#define CONTROL_PONG                 (0x05)
#define CONTROL_ERROR                (0x07)
#define CONTROL_PC_LINK              (0x09)
#define CONTROL_CLIENT_STATUS        (0x0A)
#define CONTROL_CLIENT_USB           (0x0D)    /* Host -> Client: u8 1 = connect your USB side to the PC now. */
#define CONTROL_CLIENT_RESET_REQUEST (0x0C)    /* Client -> Host: please reset me with the reset line. */

#define ERROR_UNKNOWN_CHANNEL        (1)
#define ERROR_UNKNOWN_TYPE           (2)
#define ERROR_BUSY                   (7)

#define TEST_CHANNEL                 (1)
#define TEST_SOURCE_DATA             (0x7A)    /* Client -> PC: one message of a source run. */
#define TEST_SOURCE                  (0x7B)    /* PC -> Client: u32 count, u32 size: send that many messages of that size. */
#define TEST_SINK_END                (0x7C)    /* PC -> Client: end of a sink run; the Client answers with u32 messages, u32 bytes. */
#define TEST_SINK_DATA               (0x7D)    /* PC -> Client: data to be counted and dropped. */
#define TEST_ECHO                    (0x7E)    /* PC -> Client: answered with the same message as TEST_ECHO_REPLY. */
#define TEST_ECHO_REPLY              (0x7F)
#define TEST_CRASH_FATAL             (0x78)    /* PC -> Client (debug): run the fatal error handler (USB disconnect + device reset). */
#define TEST_CRASH_SPIN              (0x79)    /* PC -> Client (debug): hang the link thread in an endless loop (the watchdog has to reset the device). */

#define REQUEST_NONE                 (0)
#define REQUEST_ECHO                 (1)
#define REQUEST_SOURCE               (2)
#define REQUEST_SINK_DONE            (3)
#define REQUEST_USB_CONNECT          (4)

extern void CyFxAppErrorHandler (CyU3PReturnStatus_t apiRetStatus);
extern void CyFxClientUsbConnect (void);
extern CyBool_t CyFxClientUsbIsConnected (void);
extern CyBool_t CyFxClientUsbRestartPending (void);
extern void CyFxClientUsbRestartRun (void);

static CyU3PDmaChannel ReceiveChannel;           /* PIB socket 0 -> CPU: what the Host writes. */
static CyU3PDmaChannel SendChannel;              /* CPU -> PIB socket 1: what the Host reads. */
static CyU3PThread LinkThread;
static CyU3PThread HelperThread;
static CyU3PSemaphore HelperWake;
static uint8_t *HelperBuffer = NULL;             /* One message block, used by the helper thread. */
static volatile uint32_t HelperRequest = REQUEST_NONE;
static uint32_t HelperLength = 0;
static uint32_t SourceCount = 0;
static uint32_t SourceSize = 0;
static uint32_t SinkMessages = 0;
static uint32_t SinkBytes = 0;
static volatile uint8_t PcLinkUp = 0;
static uint32_t PingCount = 0;

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

/* Builds a short message and queues it without waiting (for use inside the link callback). */
static void ReplyShort (uint8_t Channel, uint8_t Type, uint16_t Tag, const uint8_t *Payload, uint32_t Length)
{
    uint32_t Block[16];
    uint32_t Total;

    Total = GpifLinkBuildMessage ((uint8_t *)Block, Channel, Type, Tag, Payload, Length);
    GpifLinkSend ((uint8_t *)Block, Total, 0);
}

static void ReplyError (uint8_t Code, uint16_t ReferenceTag)
{
    uint8_t Payload[6];

    Payload[0] = Code;
    Payload[1] = 0;
    Payload[2] = (uint8_t)(ReferenceTag & 0xFF);
    Payload[3] = (uint8_t)(ReferenceTag >> 8);
    Payload[4] = 0;
    Payload[5] = 0;
    ReplyShort (0, CONTROL_ERROR, 0, Payload, sizeof (Payload));
}

/* For CDMEDIA.c: the same ERROR reply, usable from any thread. */
void ClientLinkSendError (uint8_t Code, uint16_t ReferenceTag)
{
    ReplyError (Code, ReferenceTag);
}

/* A short message to the PC (up to 52 payload bytes); TimeoutMs 0 never waits (link thread). Returns CyFalse if it did not fit the window. */
CyBool_t ClientLinkSend (uint8_t Channel, uint8_t Type, uint16_t Tag, const uint8_t *Payload, uint32_t Length, uint32_t TimeoutMs)
{
    uint32_t Block[16];
    uint32_t Total;

    if (Length > 52)
        return (CyFalse);

    Total = GpifLinkBuildMessage ((uint8_t *)Block, Channel, Type, Tag, Payload, Length);
    return (GpifLinkSend ((uint8_t *)Block, Total, TimeoutMs));
}

/* The Client never resets itself (CyU3PDeviceReset leaves the chip in the USB bootloader until a power cycle): it asks the Host to pulse the reset line. */
void ClientLinkRequestReset (void)
{
    ClientLinkSend (0, CONTROL_CLIENT_RESET_REQUEST, 0, NULL, 0, 2000);
}

/* Tells the Host (and through it the PC) what the Client is: the firmware version and the state of its media functions. */
void ClientLinkAnnounceStatus (void)
{
    uint8_t Status[32];
    uint32_t Low;
    uint32_t High;

    CyU3PMemSet (Status, 0, sizeof (Status));
    WriteWord (Status, CLIENT_FIRMWARE_VERSION);
    Status[8] = (CdMediaIsPresent () == CyTrue) ? 1 : 0;
    Status[9] = (CdMediaIsLocked () == CyTrue) ? 1 : 0;
    /* Body byte 2 (= 6 here): the Client's USB speed towards its PC, 0 = no PC / not enumerated. */
    Status[6] = ClientPeriphUsbSpeed ();
    Status[10] = (CyFxClientUsbIsConnected () == CyTrue) ? 1 : 0;
    CdMediaGetId (&Low, &High);
    WriteWord (Status + 12, Low);
    WriteWord (Status + 16, High);
    Status[20] = (HddMediaIsPresent () == CyTrue) ? 1 : 0;
    Status[21] = (HddMediaIsReadOnly () == CyTrue) ? 1 : 0;
    HddMediaGetId (&Low, &High);
    WriteWord (Status + 24, Low);
    WriteWord (Status + 28, High);
    ClientLinkSend (0, CONTROL_CLIENT_STATUS, 0, Status, sizeof (Status), 0);
}

/* Hands a request to the helper thread; only one at a time. */
static CyBool_t StartRequest (uint32_t Request)
{
    if (HelperRequest != REQUEST_NONE)
        return (CyFalse);

    HelperRequest = Request;
    CyU3PSemaphorePut (&HelperWake);
    return (CyTrue);
}

/* A verified message block from the Host (link thread: must not block). */
static void OnMessage (const uint8_t *Block, uint32_t Length)
{
    uint8_t Channel = Block[0];
    uint8_t Type = Block[1];
    uint16_t Tag = (uint16_t)(Block[2] | (Block[3] << 8));
    uint32_t PayloadLength = ReadWord (Block + 4);
    const uint8_t *Payload = Block + 8;
    uint32_t Index;
    uint8_t Reply[8];

    if (Channel == 0)
    {
        if (Type == CONTROL_PING)
        {
            ReplyShort (0, CONTROL_PONG, Tag, Payload, (PayloadLength >= 4) ? 4 : 0);

            /* Every 10 s (the Host pings once a second): the lane counters, printed only when buffers without a plausible header were seen. */
            PingCount++;
            if (((PingCount % 10) == 0) && (GpifLinkGetStats ()->HeaderSkips != 0))
                GpifLinkPrintState ();
        }
        else if (Type == CONTROL_CLIENT_USB)
        {
            /* Not while the firmware update has the Client; the helper thread does the connecting (it can block). */
            if ((PayloadLength > 0) && (Payload[0] != 0) && (UpdateIsActive () == CyFalse) && (CyFxClientUsbIsConnected () == CyFalse))
                StartRequest (REQUEST_USB_CONNECT);
        }
        else if (Type == CONTROL_PC_LINK)
        {
            PcLinkUp = (PayloadLength > 0) ? Payload[0] : 0;
            CdMediaOnPcLink (PcLinkUp);
            HddMediaOnPcLink (PcLinkUp);
            ClientPeriphPcLink (PcLinkUp);
            CyU3PDebugPrint (4, "Client link: PC is %s\r\n", (PcLinkUp != 0) ? "connected" : "gone");
        }
        return;
    }

    if (Channel == UPDATE_CHANNEL)
    {
        UpdateOnMessage (Block, Length);
        return;
    }

    if (Channel == 2)
    {
        HddMediaOnMessage (Block, Length);
        return;
    }

    if ((Channel >= 3) && (Channel <= 5))
    {
        ClientPeriphOnMessage (Block, Length);
        return;
    }

    if (Channel != TEST_CHANNEL)
    {
        ReplyError(ERROR_UNKNOWN_CHANNEL, Tag);
        return;
    }

    /* The CD-ROM function uses the message types below 0x70; 0x7A and up stay the test messages. */
    if (Type < 0x70)
    {
        CdMediaOnMessage (Block, Length);
        return;
    }

    switch (Type)
    {
    case TEST_CRASH_FATAL:
        CyFxAppErrorHandler (CY_U3P_ERROR_FAILURE);
        break;

    case TEST_CRASH_SPIN:
        for (;;)
            ;

    case TEST_ECHO:
        if (HelperRequest != REQUEST_NONE)
        {
            ReplyError (ERROR_BUSY, Tag);
            break;
        }
        for (Index = 0; Index < Length / 4; Index++)
            ((uint32_t *)HelperBuffer)[Index] = ((const uint32_t *)Block)[Index];
        HelperLength = Length;
        StartRequest (REQUEST_ECHO);
        break;

    case TEST_SINK_DATA:
        SinkMessages++;
        SinkBytes += PayloadLength;
        break;

    case TEST_SINK_END:
        if (HelperRequest != REQUEST_NONE)
        {
            ReplyError (ERROR_BUSY, Tag);
            break;
        }
        WriteWord (Reply, SinkMessages);
        WriteWord (Reply + 4, SinkBytes);
        SinkMessages = 0;
        SinkBytes = 0;
        HelperLength = GpifLinkBuildMessage (HelperBuffer, TEST_CHANNEL, TEST_SINK_END, Tag, Reply, sizeof (Reply));
        StartRequest (REQUEST_SINK_DONE);
        break;

    case TEST_SOURCE:
        if ((HelperRequest != REQUEST_NONE) || (PayloadLength < 8))
        {
            ReplyError (ERROR_BUSY, Tag);
            break;
        }
        SourceCount = ReadWord (Payload);
        SourceSize = ReadWord (Payload + 4);
        if (SourceSize > GPIF_LINK_MAX_PAYLOAD_BYTES)
            SourceSize = GPIF_LINK_MAX_PAYLOAD_BYTES;
        StartRequest (REQUEST_SOURCE);
        break;

    default:
        ReplyError (ERROR_UNKNOWN_TYPE, Tag);
        break;
    }
}

/* Re-announces the status when the Client's USB speed or connection changed (the PC enumerates some time after the soft connect), and every 10 s anyway. */
static void PollUsbSpeed (void)
{
    static uint8_t AnnouncedSpeed = 0xFF;
    static uint8_t AnnouncedConnected = 0xFF;
    static uint32_t Quiet = 0;
    uint8_t Speed = ClientPeriphUsbSpeed ();
    uint8_t Connected = (CyFxClientUsbIsConnected () == CyTrue) ? 1 : 0;

    Quiet++;
    if ((Speed == AnnouncedSpeed) && (Connected == AnnouncedConnected) && (Quiet < 10))
        return;

    AnnouncedSpeed = Speed;
    AnnouncedConnected = Connected;
    Quiet = 0;
    ClientLinkAnnounceStatus ();
}

/* Wakes the helper thread without a request of its own (the USB restart is picked up at the top of its loop). */
void ClientLinkWakeHelper (void)
{
    CyU3PSemaphorePut (&HelperWake);
}

/* Does what the link callback may not: waits for window slots. */
static void HelperEntry (uint32_t Input)
{
    uint8_t Status[32];
    uint32_t Block[16];
    uint32_t Total;
    uint32_t Index;
    uint32_t Position;

    /* Announce the Client application to the Host. The block waits in the window (and is retransmitted) until the Host's link is up. */
    CyU3PMemSet (Status, 0, sizeof (Status));
    WriteWord (Status, CLIENT_FIRMWARE_VERSION);
    Total = GpifLinkBuildMessage ((uint8_t *)Block, 0, CONTROL_CLIENT_STATUS, 0, Status, sizeof (Status));
    GpifLinkSend ((uint8_t *)Block, Total, CYU3P_WAIT_FOREVER);
    CyU3PDebugPrint (4, "Client link: announced the Client application to the Host\r\n");
    ClientPeriphAnnounce ();

    for (;;)
    {
        /* The disk came or went: the USB stack is restarted with or without the disk interface (takes a few seconds; the link thread goes on). */
        if (CyFxClientUsbRestartPending () == CyTrue)
        {
            CyFxClientUsbRestartRun ();
            ClientLinkAnnounceStatus ();
        }

        if (CyU3PSemaphoreGet (&HelperWake, 1000) != CY_U3P_SUCCESS)
        {
            PollUsbSpeed ();
            continue;
        }

        switch (HelperRequest)
        {
        case REQUEST_ECHO:
            HelperBuffer[1] = TEST_ECHO_REPLY;
            GpifLinkSealMessage (HelperBuffer, HelperLength);
            GpifLinkSend (HelperBuffer, HelperLength, 10000);
            break;

        case REQUEST_USB_CONNECT:
            CyFxClientUsbConnect ();
            ClientLinkAnnounceStatus ();
            break;

        case REQUEST_SINK_DONE:
            GpifLinkSend (HelperBuffer, HelperLength, 10000);
            break;

        case REQUEST_SOURCE:
            for (Position = 0; Position < SourceSize; Position++)
                HelperBuffer[8 + Position] = (uint8_t)(Position * 7 + (Position >> 8));
            for (Index = 0; Index < SourceCount; Index++)
            {
                Total = GpifLinkBuildMessage (HelperBuffer, TEST_CHANNEL, TEST_SOURCE_DATA, (uint16_t)Index, HelperBuffer + 8, SourceSize);
                if (GpifLinkSend (HelperBuffer, Total, 10000) == CyFalse)
                    break;
            }
            break;

        default:
            break;
        }

        HelperRequest = REQUEST_NONE;
    }
}

/* Brings up the GPIF slave interface, the DMA channels, the block link and its two threads. Call before the USB setup. */
void ClientLinkInit (void)
{
    CyU3PPibClock_t PibClock;
    CyU3PDmaChannelConfig_t DmaConfig;
    GpifLinkConfig LinkConfig;
    CyU3PReturnStatus_t Status;
    void *Stack;

    /* Same bring-up as the stock AutoSlave reference: PIB clock divider 2 (the Host provides PCLK), DLL off, thread 0 receives, thread 1 sends. */
    PibClock.clkDiv = 2;
    PibClock.clkSrc = CY_U3P_SYS_CLK;
    PibClock.isHalfDiv = CyFalse;
    PibClock.isDllEnable = CyFalse;
    Status = CyU3PPibInit (CyTrue, &PibClock);
    if (Status != CY_U3P_SUCCESS)
    {
        CyU3PDebugPrint (4, "CyU3PPibInit failed, Error code = %d\r\n", Status);
        CyFxAppErrorHandler (Status);
    }

    Status = CyU3PGpifLoad (&CyFxGpifConfig);
    if (Status != CY_U3P_SUCCESS)
    {
        CyU3PDebugPrint (4, "CyU3PGpifLoad failed, Error code = %d\r\n", Status);
        CyFxAppErrorHandler (Status);
    }

    CyU3PGpifSocketConfigure (0, CY_U3P_PIB_SOCKET_0, 4, CyFalse, 7);
    CyU3PGpifSocketConfigure (1, CY_U3P_PIB_SOCKET_1, 0, CyFalse, 1);

    Status = CyU3PGpifSMStart (RESET, ALPHA_RESET);
    if (Status != CY_U3P_SUCCESS)
    {
        CyU3PDebugPrint (4, "CyU3PGpifSMStart failed, Error code = %d\r\n", Status);
        CyFxAppErrorHandler (Status);
    }

    CyU3PMemSet ((uint8_t *)&DmaConfig, 0, sizeof (DmaConfig));
    DmaConfig.size = GPIF_LINK_BUFFER_BYTES;
    DmaConfig.count = CLIENT_CHANNEL_BUFFERS;
    DmaConfig.prodSckId = CY_U3P_PIB_SOCKET_0;
    DmaConfig.consSckId = CY_U3P_CPU_SOCKET_CONS;
    DmaConfig.dmaMode = CY_U3P_DMA_MODE_BYTE;
    DmaConfig.notification = 0;
    DmaConfig.cb = NULL;
    Status = CyU3PDmaChannelCreate (&ReceiveChannel, CY_U3P_DMA_TYPE_MANUAL_IN, &DmaConfig);
    if (Status == CY_U3P_SUCCESS)
        Status = CyU3PDmaChannelSetXfer (&ReceiveChannel, 0);

    CyU3PMemSet ((uint8_t *)&DmaConfig, 0, sizeof (DmaConfig));
    DmaConfig.size = GPIF_LINK_BUFFER_BYTES;
    DmaConfig.count = CLIENT_CHANNEL_BUFFERS;
    DmaConfig.prodSckId = CY_U3P_CPU_SOCKET_PROD;
    DmaConfig.consSckId = CY_U3P_PIB_SOCKET_1;
    DmaConfig.dmaMode = CY_U3P_DMA_MODE_BYTE;
    DmaConfig.notification = 0;
    DmaConfig.cb = NULL;
    if (Status == CY_U3P_SUCCESS)
        Status = CyU3PDmaChannelCreate (&SendChannel, CY_U3P_DMA_TYPE_MANUAL_OUT, &DmaConfig);
    if (Status == CY_U3P_SUCCESS)
        Status = CyU3PDmaChannelSetXfer (&SendChannel, 0);
    if (Status != CY_U3P_SUCCESS)
    {
        CyU3PDebugPrint (4, "GPIF DMA channel setup failed, Error code = %d\r\n", Status);
        CyFxAppErrorHandler (Status);
    }

    CdMediaInit ();
    HddMediaInit ();
    UpdateInit ();
    ClientPeriphInit ();

    /* How much of the DMA buffer area is left (the bulk data lane of the mass-storage function needs 16 KB, or 32 KB for full speed). */
    {
        void *Probe[16];
        uint32_t Found = 0;
        uint32_t Index;

        while ((Found < 16) && ((Probe[Found] = CyU3PDmaBufferAlloc (16384)) != NULL))
            Found++;
        for (Index = 0; Index < Found; Index++)
            CyU3PDmaBufferFree (Probe[Index]);
        CyU3PDebugPrint (4, "Client link: %d free 16 KB blocks left in the DMA area\r\n", Found);
    }

    HelperBuffer = (uint8_t *)CyU3PDmaBufferAlloc (GPIF_LINK_MAX_MESSAGE_BYTES);
    if (HelperBuffer == NULL)
        CyFxAppErrorHandler (CY_U3P_ERROR_MEMORY_ERROR);
    CyU3PSemaphoreCreate (&HelperWake, 0);

    LinkConfig.ReadChannel = &ReceiveChannel;
    LinkConfig.WriteChannel = &SendChannel;
    LinkConfig.OnMessage = OnMessage;
    if (GpifLinkInit (&LinkConfig) == CyFalse)
    {
        CyU3PDebugPrint (4, "GpifLinkInit failed\r\n");
        CyFxAppErrorHandler (CY_U3P_ERROR_MEMORY_ERROR);
    }

    Stack = CyU3PMemAlloc (CLIENT_LINK_STACK);
    CyU3PThreadCreate (&LinkThread, "27:Gpif_link", GpifLinkThreadEntry, 0, Stack, CLIENT_LINK_STACK,
            CLIENT_THREAD_PRIORITY, CLIENT_THREAD_PRIORITY, CYU3P_NO_TIME_SLICE, CYU3P_AUTO_START);

    Stack = CyU3PMemAlloc (CLIENT_HELPER_STACK);
    CyU3PThreadCreate (&HelperThread, "28:Link_helper", HelperEntry, 0, Stack, CLIENT_HELPER_STACK,
            CLIENT_THREAD_PRIORITY, CLIENT_THREAD_PRIORITY, CYU3P_NO_TIME_SLICE, CYU3P_AUTO_START);

    CyU3PDebugPrint (4, "Client link: GPIF slave and block link running\r\n");
}