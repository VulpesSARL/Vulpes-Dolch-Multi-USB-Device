#include "cyu3system.h"
#include "cyu3os.h"
#include "cyu3dma.h"
#include "cyu3error.h"
#include "cyu3usb.h"
#include "main.h"
#include "GpifLinkCore.h"
#include "PeriphProtocol.h"
#include "PeriphCore.h"
#include "PERIPH.h"

/* The Host's peripheral functions that need no PC application: the USB-MIDI interface (endpoints 0x04 / 0x84) and the CDC-ACM serial port
 * (0x05 / 0x85 and the notification endpoint 0x86). Each is one byte stream (PeriphCore.h) to the same function of the Client. */

#define PERIPH_THREAD_STACK      (0x800)
#define PERIPH_THREAD_PRIORITY   (11)      /* Below the guard thread (10). */
#define MIDI_STALL_MS            (2000)
#define SERIAL_STALL_MS          (3000)

static CyU3PThread PeriphThread;
static CyU3PDmaChannel MidiOutChannel;
static CyU3PDmaChannel MidiInChannel;
static CyU3PDmaChannel SerialOutChannel;
static CyU3PDmaChannel SerialInChannel;
static CyU3PDmaChannel SerialNotifyChannel;
static PeriphStream MidiStream;
static PeriphStream SerialStream;
static PeriphCdc SerialCdc;
static volatile CyBool_t Ready = CyFalse;
static CyBool_t ChannelsExist = CyFalse;
static CyBool_t NotifyExists = CyFalse;

static CyBool_t CreateChannel (CyU3PDmaChannel *Channel, CyU3PDmaType_t Type, CyU3PDmaSocketId_t Producer, CyU3PDmaSocketId_t Consumer, uint16_t Size, uint16_t Count)
{
    CyU3PDmaChannelConfig_t Config;
    CyU3PReturnStatus_t Status;

    CyU3PMemSet ((uint8_t *)&Config, 0, sizeof (Config));
    Config.size = Size;
    Config.count = Count;
    Config.prodSckId = Producer;
    Config.consSckId = Consumer;
    Config.dmaMode = CY_U3P_DMA_MODE_BYTE;
    Status = CyU3PDmaChannelCreate (Channel, Type, &Config);
    if (Status != CY_U3P_SUCCESS)
    {
        CyU3PDebugPrint (4, "Peripherals: DMA channel failed, Error code = %d\r\n", Status);
        return (CyFalse);
    }

    return (CyTrue);
}

static void ConfigureEndpoint (uint8_t Endpoint, uint8_t Type, uint16_t PacketSize, uint8_t Burst)
{
    CyU3PEpConfig_t Config;

    CyU3PMemSet ((uint8_t *)&Config, 0, sizeof (Config));
    Config.enable = CyTrue;
    Config.epType = Type;
    Config.pcktSize = PacketSize;
    Config.burstLen = Burst;
    CyU3PSetEpConfig (Endpoint, &Config);
    CyU3PUsbFlushEp (Endpoint);
}

static void DisableEndpoint (uint8_t Endpoint)
{
    CyU3PEpConfig_t Config;

    CyU3PMemSet ((uint8_t *)&Config, 0, sizeof (Config));
    Config.enable = CyFalse;
    CyU3PUsbFlushEp (Endpoint);
    CyU3PSetEpConfig (Endpoint, &Config);
}

CyBool_t HostPeriphUsbStart (void)
{
    uint16_t BulkSize;
    CyBool_t Ok = CyTrue;

    if (Ready == CyFalse)
        return (CyFalse);

    HostPeriphUsbStop ();

    switch (CyU3PUsbGetSpeed ())
    {
    case CY_U3P_FULL_SPEED:
        BulkSize = 64;
        break;
    case CY_U3P_HIGH_SPEED:
        BulkSize = 512;
        break;
    default:
        BulkSize = 1024;
        break;
    }

    ConfigureEndpoint (CY_FX_EP_PRODUCER4, CY_U3P_USB_EP_BULK, BulkSize, 1);
    ConfigureEndpoint (CY_FX_EP_CONSUMER4, CY_U3P_USB_EP_BULK, BulkSize, 1);
    ConfigureEndpoint (CY_FX_EP_SERIAL_OUT, CY_U3P_USB_EP_BULK, BulkSize, 1);
    ConfigureEndpoint (CY_FX_EP_SERIAL_IN, CY_U3P_USB_EP_BULK, BulkSize, 1);
    ConfigureEndpoint (CY_FX_EP_SERIAL_NOTIFY, CY_U3P_USB_EP_INTR, 16, 1);

    Ok = CreateChannel (&MidiOutChannel, CY_U3P_DMA_TYPE_MANUAL_IN, CY_U3P_UIB_SOCKET_PROD_4, CY_U3P_CPU_SOCKET_CONS, 1024, 2);
    if (Ok == CyTrue)
        Ok = CreateChannel (&MidiInChannel, CY_U3P_DMA_TYPE_MANUAL_OUT, CY_U3P_CPU_SOCKET_PROD, CY_U3P_UIB_SOCKET_CONS_4, 1024, 2);
    if (Ok == CyTrue)
        Ok = CreateChannel (&SerialOutChannel, CY_U3P_DMA_TYPE_MANUAL_IN, CY_U3P_UIB_SOCKET_PROD_5, CY_U3P_CPU_SOCKET_CONS, 1024, 2);
    if (Ok == CyTrue)
        Ok = CreateChannel (&SerialInChannel, CY_U3P_DMA_TYPE_MANUAL_OUT, CY_U3P_CPU_SOCKET_PROD, CY_U3P_UIB_SOCKET_CONS_5, 1024, 2);
    if (Ok == CyFalse)
    {
        CyU3PDebugPrint (4, "Peripherals: not all channels could be created\r\n");
        return (CyFalse);
    }

    CyU3PDmaChannelSetXfer (&MidiOutChannel, 0);
    CyU3PDmaChannelSetXfer (&MidiInChannel, 0);
    CyU3PDmaChannelSetXfer (&SerialOutChannel, 0);
    CyU3PDmaChannelSetXfer (&SerialInChannel, 0);
    ChannelsExist = CyTrue;

    NotifyExists = CreateChannel (&SerialNotifyChannel, CY_U3P_DMA_TYPE_MANUAL_OUT, CY_U3P_CPU_SOCKET_PROD, CY_U3P_UIB_SOCKET_CONS_6, 16, 2);
    if (NotifyExists == CyTrue)
    {
        CyU3PDmaChannelSetXfer (&SerialNotifyChannel, 0);
        SerialCdc.NotifyNeeded = CyTrue;
        SerialCdc.Notify = &SerialNotifyChannel;
    }

    PeriphStreamSetUsb (&MidiStream, &MidiOutChannel, &MidiInChannel, BulkSize, CY_FX_EP_CONSUMER4);
    PeriphStreamSetUsb (&SerialStream, &SerialOutChannel, &SerialInChannel, BulkSize, CY_FX_EP_SERIAL_IN);
    return (CyTrue);
}

void HostPeriphUsbStop (void)
{
    if (Ready == CyFalse)
        return;

    SerialCdc.Notify = NULL;
    SerialCdc.LocalLines = 0;               /* the COM port on the PC is gone */
    PeriphStreamSetUsb (&MidiStream, NULL, NULL, 0, 0);
    PeriphStreamSetUsb (&SerialStream, NULL, NULL, 0, 0);

    if (ChannelsExist == CyTrue)
    {
        CyU3PDmaChannelDestroy (&MidiOutChannel);
        CyU3PDmaChannelDestroy (&MidiInChannel);
        CyU3PDmaChannelDestroy (&SerialOutChannel);
        CyU3PDmaChannelDestroy (&SerialInChannel);
        ChannelsExist = CyFalse;
    }
    if (NotifyExists == CyTrue)
    {
        CyU3PDmaChannelDestroy (&SerialNotifyChannel);
        NotifyExists = CyFalse;
    }

    DisableEndpoint (CY_FX_EP_PRODUCER4);
    DisableEndpoint (CY_FX_EP_CONSUMER4);
    DisableEndpoint (CY_FX_EP_SERIAL_OUT);
    DisableEndpoint (CY_FX_EP_SERIAL_IN);
    DisableEndpoint (CY_FX_EP_SERIAL_NOTIFY);
}

/* Channels 4 and 5 from the Client (lane thread: record only). */
void HostPeriphOnClientMessage (const uint8_t *Block, uint32_t Length)
{
    uint32_t PayloadLength = (uint32_t)Block[4] | ((uint32_t)Block[5] << 8) | ((uint32_t)Block[6] << 16) | ((uint32_t)Block[7] << 24);
    const uint8_t *Payload = Block + 8;

    if (Ready == CyFalse)
        return;

    if (Block[0] == PERIPH_CHANNEL_MIDI)
    {
        PeriphStreamOnMessage (&MidiStream, Block[1], Payload, PayloadLength);
    }
    else if (Block[0] == PERIPH_CHANNEL_SERIAL)
    {
        if (Block[1] == SERIAL_LINES)
        {
            if (PayloadLength >= 1)
                PeriphCdcOnLines (&SerialCdc, Payload[0]);
        }
        else
        {
            PeriphStreamOnMessage (&SerialStream, Block[1], Payload, PayloadLength);
        }
    }
}

void HostPeriphClientState (CyBool_t Up)
{
    if (Ready == CyFalse)
        return;

    /* Only a Client that was down and is up now has empty rings. CLIENT_STATUS arrives on every change and every 10 s while the Client runs: a reset on each
     * of those zeroed the Host's counters while the Client kept its cumulative ones, so the Client's next credit announcement was far too large, the Host
     * overran the Client's ring (whole messages dropped) and the Client's own credit stalled (its USB OUT endpoint NAKed for ever). */
    if ((Up == CyTrue) && (MidiStream.PeerUp == CyFalse))
    {
        /* A freshly started Client: its rings are empty, so forget what was counted. The Client sends STREAM_RESET as well, right after its announcement. */
        PeriphStreamRequestReset (&MidiStream);
        PeriphStreamRequestReset (&SerialStream);
        PeriphCdcResync (&SerialCdc);
    }

    MidiStream.PeerUp = Up;
    SerialStream.PeerUp = Up;
}

CyBool_t HostPeriphCdcSetup (uint8_t Request, uint16_t Value, uint16_t Length)
{
    if (Ready == CyFalse)
        return (CyFalse);

    return (PeriphCdcSetup (&SerialCdc, Request, Value, Length));
}

static void PeriphEntry (uint32_t Input)
{
    CyBool_t Worked;

    for (;;)
    {
        Worked = CyFalse;

        if (PeriphStreamPump (&MidiStream) == CyTrue)
            Worked = CyTrue;
        if (PeriphStreamPump (&SerialStream) == CyTrue)
            Worked = CyTrue;
        if (PeriphCdcPump (&SerialCdc, SerialStream.PeerUp) == CyTrue)
            Worked = CyTrue;

#ifdef MIDI_DIAG
        PeriphStreamDiagTick ("Host MIDI", &MidiStream);
#endif

        if (Worked == CyFalse)
            CyU3PThreadSleep (1);
    }
}

void HostPeriphInit (void)
{
    void *Stack;

    if ((PeriphStreamInit (&MidiStream, PERIPH_CHANNEL_MIDI, PERIPH_MIDI_RING_BYTES, PERIPH_MIDI_RING_BYTES, 4, MIDI_STALL_MS) == CyFalse) ||
        (PeriphStreamInit (&SerialStream, PERIPH_CHANNEL_SERIAL, PERIPH_SERIAL_RING_BYTES, PERIPH_SERIAL_RING_BYTES, 1, SERIAL_STALL_MS) == CyFalse))
    {
        CyU3PDebugPrint (4, "Peripherals: no memory for the stream rings\r\n");
        return;
    }

    SerialStream.UsePortState = CyTrue;
    PeriphCdcInit (&SerialCdc, CY_FX_USB_SERIAL_COMM_INTF);
    SerialCdc.Stream = &SerialStream;

    Stack = CyU3PMemAlloc (PERIPH_THREAD_STACK);
    if ((Stack == NULL) || (CyU3PThreadCreate (&PeriphThread, "35:Periph", PeriphEntry, 0, Stack, PERIPH_THREAD_STACK,
            PERIPH_THREAD_PRIORITY, PERIPH_THREAD_PRIORITY, CYU3P_NO_TIME_SLICE, CYU3P_AUTO_START) != CY_U3P_SUCCESS))
    {
        CyU3PDebugPrint (4, "Peripherals: could not create the worker thread\r\n");
        return;
    }

    Ready = CyTrue;
}
