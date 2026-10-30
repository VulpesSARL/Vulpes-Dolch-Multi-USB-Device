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

/* The Client's peripheral functions: keyboard and mouse (HID reports from the PC application, channel 3), MIDI (channel 4) and the serial port
 * (channel 5) - see PeriphCore.h for the two byte streams. One worker thread does all the USB work; the lane thread only records what arrived. */

#define PERIPH_THREAD_STACK      (0x800)
#define PERIPH_THREAD_PRIORITY   (9)       /* Above the mass storage threads (11) so a key or a click is never queued behind a long disk command; it sleeps every pass, so the guard thread (10) still runs. */
#define MIDI_STALL_MS            (2000)
#define SERIAL_STALL_MS          (3000)

static CyU3PThread PeriphThread;
static CyU3PDmaChannel KeyboardChannel;                /* the one HID channel: keyboard and mouse reports (report IDs 1 and 2) */
static CyU3PDmaChannel MidiOutChannel;
static CyU3PDmaChannel MidiInChannel;
static CyU3PDmaChannel SerialOutChannel;
static CyU3PDmaChannel SerialInChannel;
static CyU3PDmaChannel SerialNotifyChannel;
static CyBool_t KeyboardChannelOk = CyFalse;
static volatile uint8_t HidProtocol = 1;               /* 1 report protocol (reports start with their ID), 0 boot protocol (8 byte keyboard report, no mouse) */
static CyBool_t MidiChannelsOk = CyFalse;
static CyBool_t SerialChannelsOk = CyFalse;
static CyBool_t NotifyChannelOk = CyFalse;

static PeriphStream MidiStream;
static PeriphStream SerialStream;
static PeriphCdc SerialCdc;
static volatile CyBool_t StreamsReady = CyFalse;
static volatile CyBool_t ChannelsDown = CyFalse;       /* The USB stack is being restarted: the worker thread must not touch the channels. */

static volatile CyBool_t UsbConfigured = CyFalse;
static volatile uint8_t UsbSpeedAtUp = 0;               /* the link speed when the PC configured the device */
static volatile CyBool_t UsbSuspended = CyFalse;
static volatile uint8_t KeyboardLeds = 0;
static volatile CyBool_t StateChanged = CyFalse;       /* Tell the PC about KM_STATE at the next pass. */

/* Pending HID reports are queued, never coalesced across a change of keys or buttons: a key pressed and released within one pass of the worker
 * thread (or a quick click) must still reach the target PC as two reports. Only consecutive mouse reports with the same buttons are summed. A queue
 * that overflows (the target PC does not poll) keeps the newest state. */
#define KEYBOARD_QUEUE_LENGTH    (16)
#define MOUSE_QUEUE_LENGTH       (32)
static uint8_t KeyboardQueue[KEYBOARD_QUEUE_LENGTH][8];
static volatile uint32_t KeyboardHead = 0;           /* free running counters: next to write / next to send */
static volatile uint32_t KeyboardTail = 0;
static uint8_t MouseQueue[MOUSE_QUEUE_LENGTH][4];
static volatile uint32_t MouseHead = 0;
static volatile uint32_t MouseTail = 0;

static uint8_t Ep0Buffer[32] __attribute__((aligned(32)));

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
        CyU3PDebugPrint (4, "Peripherals: DMA channel (socket 0x%x) failed, Error code = %d\r\n", Consumer, Status);
        return (CyFalse);
    }

    return (CyTrue);
}

/* Creates the channels once; they stay (like the mass storage ones). Called after the endpoints were configured. */
void ClientPeriphDmaInit (void)
{
    KeyboardChannelOk = CreateChannel (&KeyboardChannel, CY_U3P_DMA_TYPE_MANUAL_OUT, CY_U3P_CPU_SOCKET_PROD,
            (CyU3PDmaSocketId_t)(CY_U3P_UIB_SOCKET_CONS_0 | CY_FX_EP_KEYBOARD_IN_SOCKET), 16, 2);

    MidiChannelsOk = CreateChannel (&MidiOutChannel, CY_U3P_DMA_TYPE_MANUAL_IN,
            (CyU3PDmaSocketId_t)(CY_U3P_UIB_SOCKET_PROD_0 | CY_FX_EP_MIDI_OUT_SOCKET), CY_U3P_CPU_SOCKET_CONS, 1024, 2);
    if (MidiChannelsOk == CyTrue)
        MidiChannelsOk = CreateChannel (&MidiInChannel, CY_U3P_DMA_TYPE_MANUAL_OUT, CY_U3P_CPU_SOCKET_PROD,
                (CyU3PDmaSocketId_t)(CY_U3P_UIB_SOCKET_CONS_0 | CY_FX_EP_MIDI_IN_SOCKET), 1024, 2);

    SerialChannelsOk = CreateChannel (&SerialOutChannel, CY_U3P_DMA_TYPE_MANUAL_IN,
            (CyU3PDmaSocketId_t)(CY_U3P_UIB_SOCKET_PROD_0 | CY_FX_EP_SERIAL_OUT_SOCKET), CY_U3P_CPU_SOCKET_CONS, 1024, 2);
    if (SerialChannelsOk == CyTrue)
        SerialChannelsOk = CreateChannel (&SerialInChannel, CY_U3P_DMA_TYPE_MANUAL_OUT, CY_U3P_CPU_SOCKET_PROD,
                (CyU3PDmaSocketId_t)(CY_U3P_UIB_SOCKET_CONS_0 | CY_FX_EP_SERIAL_IN_SOCKET), 1024, 2);
    NotifyChannelOk = CreateChannel (&SerialNotifyChannel, CY_U3P_DMA_TYPE_MANUAL_OUT, CY_U3P_CPU_SOCKET_PROD,
            (CyU3PDmaSocketId_t)(CY_U3P_UIB_SOCKET_CONS_0 | CY_FX_EP_SERIAL_NOTIFY_SOCKET), 16, 2);

    ChannelsDown = CyFalse;
}

static void UsbDown (void);

/* Destroys the channels before the USB stack is stopped (the stack restart of main.c); ClientPeriphDmaInit creates them again afterwards. */
void ClientPeriphDmaDeInit (void)
{
    if (StreamsReady == CyFalse)
        return;

    ChannelsDown = CyTrue;
    UsbDown ();
    CyU3PThreadSleep (20);              /* the worker thread parks itself at the top of its next pass */

    if (KeyboardChannelOk == CyTrue)
        CyU3PDmaChannelDestroy (&KeyboardChannel);
    if (MidiChannelsOk == CyTrue)
    {
        CyU3PDmaChannelDestroy (&MidiOutChannel);
        CyU3PDmaChannelDestroy (&MidiInChannel);
    }
    if (SerialChannelsOk == CyTrue)
    {
        CyU3PDmaChannelDestroy (&SerialOutChannel);
        CyU3PDmaChannelDestroy (&SerialInChannel);
    }
    if (NotifyChannelOk == CyTrue)
        CyU3PDmaChannelDestroy (&SerialNotifyChannel);

    KeyboardChannelOk = CyFalse;
    MidiChannelsOk = CyFalse;
    SerialChannelsOk = CyFalse;
    NotifyChannelOk = CyFalse;
}

static void StartChannel (CyU3PDmaChannel *Channel, CyBool_t Ok)
{
    if (Ok == CyFalse)
        return;

    CyU3PDmaChannelReset (Channel);
    CyU3PDmaChannelSetXfer (Channel, 0);
}

/* The PC configured the device (or an endpoint reset happened): endpoint packet sizes for the speed, fresh channels, streams attached. */
static void UsbUp (void)
{
    uint16_t BulkSize;

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

    CyU3PSetEpPacketSize (CY_FX_EP_MIDI_OUT, BulkSize);
    CyU3PSetEpPacketSize (CY_FX_EP_MIDI_IN, BulkSize);
    CyU3PSetEpPacketSize (CY_FX_EP_SERIAL_OUT, BulkSize);
    CyU3PSetEpPacketSize (CY_FX_EP_SERIAL_IN, BulkSize);

    /* A key, a mouse click, a MIDI note or a character must reach the target PC at once. In the SuperSpeed low power states U1 / U2 the device has to
     * wake the link itself before its data can go, and the target PC only wakes it now and then (seconds): data committed to an idle link waited that
     * long. So the link is kept in U0, like the Host does. */
    CyU3PUsbLPMDisable ();

    CyU3PUsbFlushEp (CY_FX_EP_KEYBOARD_IN);
    CyU3PUsbFlushEp (CY_FX_EP_MIDI_OUT);
    CyU3PUsbFlushEp (CY_FX_EP_MIDI_IN);
    CyU3PUsbFlushEp (CY_FX_EP_SERIAL_OUT);
    CyU3PUsbFlushEp (CY_FX_EP_SERIAL_IN);
    CyU3PUsbFlushEp (CY_FX_EP_SERIAL_NOTIFY);

    /* The pump must not touch the channels while they are reset. */
    PeriphStreamSetUsb (&MidiStream, NULL, NULL, 0, 0);
    PeriphStreamSetUsb (&SerialStream, NULL, NULL, 0, 0);
    SerialCdc.Notify = NULL;

    HidProtocol = 1;                    /* after a reset / new configuration the host is in report protocol */
    StartChannel (&KeyboardChannel, KeyboardChannelOk);
    if (MidiChannelsOk == CyTrue)
    {
        StartChannel (&MidiOutChannel, CyTrue);
        StartChannel (&MidiInChannel, CyTrue);
        PeriphStreamSetUsb (&MidiStream, &MidiOutChannel, &MidiInChannel, BulkSize, CY_FX_EP_MIDI_IN);
    }
    if (SerialChannelsOk == CyTrue)
    {
        StartChannel (&SerialOutChannel, CyTrue);
        StartChannel (&SerialInChannel, CyTrue);
        PeriphStreamSetUsb (&SerialStream, &SerialOutChannel, &SerialInChannel, BulkSize, CY_FX_EP_SERIAL_IN);
    }
    if (NotifyChannelOk == CyTrue)
    {
        StartChannel (&SerialNotifyChannel, CyTrue);
        SerialCdc.NotifyNeeded = CyTrue;
        SerialCdc.Notify = &SerialNotifyChannel;
    }

    UsbSpeedAtUp = (uint8_t)CyU3PUsbGetSpeed ();
    UsbConfigured = CyTrue;
    UsbSuspended = CyFalse;
    StateChanged = CyTrue;
}

static void UsbDown (void)
{
    UsbConfigured = CyFalse;
    SerialCdc.Notify = NULL;
    PeriphStreamSetUsb (&MidiStream, NULL, NULL, 0, 0);
    PeriphStreamSetUsb (&SerialStream, NULL, NULL, 0, 0);
    SerialCdc.LocalLines = 0;                    /* the COM port of the target PC is gone */
    KeyboardTail = KeyboardHead;
    MouseTail = MouseHead;
    StateChanged = CyTrue;
}

/* The speed of the link to the target PC for the status shown to the user: 0 (disconnected) unless the PC has configured the device.
 * VBUS is not wired on the Client PCB, so a pulled cable raises no event; the only trace is the FX3 reporting a different speed
 * than the one the device was configured with, which is taken as "link gone" until the PC configures it again. */
uint8_t ClientPeriphUsbSpeed (void)
{
    uint8_t Speed;

    if (UsbConfigured == CyFalse)
        return (0);

    Speed = (uint8_t)CyU3PUsbGetSpeed ();
    if (Speed != UsbSpeedAtUp)
        return (0);

    return (Speed);
}

/* USB events (called from CyFxMscApplnUSBEventCB). */
void ClientPeriphUsbEvent (CyU3PUsbEventType_t EventType, uint16_t EventData)
{
    if (StreamsReady == CyFalse)
        return;

    switch (EventType)
    {
    case CY_U3P_USB_EVENT_SETCONF:
        if (EventData == 0)
            UsbDown ();
        else
            UsbUp ();
        break;

    case CY_U3P_USB_EVENT_RESET:
    case CY_U3P_USB_EVENT_DISCONNECT:
    case CY_U3P_USB_EVENT_VBUS_REMOVED:
        UsbDown ();
        break;

    case CY_U3P_USB_EVENT_SUSPEND:
        UsbSuspended = CyTrue;
        StateChanged = CyTrue;
        break;

    case CY_U3P_USB_EVENT_RESUME:
        UsbSuspended = CyFalse;
        StateChanged = CyTrue;
        break;

    default:
        break;
    }
}

/* ---- keyboard and mouse ---- */

#ifdef INPUT_DIAG
/* Keyboard lag diagnostics: counters over a 2 s window, printed by the worker thread while keyboard messages arrive. */
static volatile uint32_t DiagKbRx = 0;          /* keyboard messages from the lane */
static volatile uint32_t DiagKbReal = 0;        /* real reports committed */
static volatile uint32_t DiagKbRepeat = 0;      /* repeated (idle) reports committed */
static volatile uint32_t DiagKbGetFail = 0;     /* GetBuffer found no free buffer: the host has not taken the last ones */
static volatile uint32_t DiagKbCommitFail = 0;
static volatile uint32_t DiagKbQueueMax = 0;
static volatile uint32_t DiagMouseReal = 0;
static volatile uint32_t DiagMouseRepeat = 0;
static volatile uint32_t DiagMouseGetFail = 0;
static volatile uint32_t DiagLoops = 0;         /* passes of the worker loop */
static volatile uint32_t DiagLoopGapMax = 0;    /* longest time between two passes (ms) */
static volatile uint32_t DiagRxGapMax = 0;      /* longest time between two keyboard messages (ms) */
static uint32_t DiagLastRx = 0;
static uint32_t DiagLastLoop = 0;
static uint32_t DiagLastPrint = 0;
#endif

static void KeyboardPush (const uint8_t *Report)
{
    uint32_t Slot;

    if ((KeyboardHead - KeyboardTail) >= KEYBOARD_QUEUE_LENGTH)
        Slot = (KeyboardHead - 1) % KEYBOARD_QUEUE_LENGTH;        /* full: the newest state replaces the newest queued one */
    else
        Slot = KeyboardHead % KEYBOARD_QUEUE_LENGTH;

    CyU3PMemCopy (KeyboardQueue[Slot], (uint8_t *)Report, 8);
    if ((KeyboardHead - KeyboardTail) < KEYBOARD_QUEUE_LENGTH)
        KeyboardHead++;
#ifdef INPUT_DIAG
    if ((KeyboardHead - KeyboardTail) > DiagKbQueueMax)
        DiagKbQueueMax = KeyboardHead - KeyboardTail;
#endif
}

static int32_t Clamp127 (int32_t Value)
{
    if (Value > 127)
        return (127);
    if (Value < -127)
        return (-127);
    return (Value);
}

static void MousePush (uint8_t Buttons, int32_t Dx, int32_t Dy, int32_t Wheel)
{
    uint32_t Last;
    uint32_t Slot;
    uint8_t *Entry;

    /* Merge into the newest queued report when the buttons are the same and it is not the one being sent right now. */
    if ((MouseHead - MouseTail) >= 2)
    {
        Last = (MouseHead - 1) % MOUSE_QUEUE_LENGTH;
        Entry = MouseQueue[Last];
        if ((Entry[0] == Buttons) && (Clamp127 ((int32_t)(int8_t)Entry[1] + Dx) == ((int32_t)(int8_t)Entry[1] + Dx)) &&
                (Clamp127 ((int32_t)(int8_t)Entry[2] + Dy) == ((int32_t)(int8_t)Entry[2] + Dy)) &&
                (Clamp127 ((int32_t)(int8_t)Entry[3] + Wheel) == ((int32_t)(int8_t)Entry[3] + Wheel)))
        {
            Entry[1] = (uint8_t)(int8_t)((int32_t)(int8_t)Entry[1] + Dx);
            Entry[2] = (uint8_t)(int8_t)((int32_t)(int8_t)Entry[2] + Dy);
            Entry[3] = (uint8_t)(int8_t)((int32_t)(int8_t)Entry[3] + Wheel);
            return;
        }
    }

    if ((MouseHead - MouseTail) >= MOUSE_QUEUE_LENGTH)
    {
        /* Full: the newest button state wins, the movement of the newest entry is kept. */
        Last = (MouseHead - 1) % MOUSE_QUEUE_LENGTH;
        MouseQueue[Last][0] = Buttons;
        return;
    }

    Slot = MouseHead % MOUSE_QUEUE_LENGTH;
    MouseQueue[Slot][0] = Buttons;
    MouseQueue[Slot][1] = (uint8_t)(int8_t)Clamp127 (Dx);
    MouseQueue[Slot][2] = (uint8_t)(int8_t)Clamp127 (Dy);
    MouseQueue[Slot][3] = (uint8_t)(int8_t)Clamp127 (Wheel);
    MouseHead++;
}

static void ReleaseAll (void)
{
    static const uint8_t NoKeys[8] = { 0, 0, 0, 0, 0, 0, 0, 0 };

    KeyboardPush (NoKeys);
    MousePush (0, 0, 0, 0);
}

/* The PC application is gone: no key may stay pressed on the target PC. */
void ClientPeriphPcLink (uint8_t Up)
{
    if ((Up == 0) && (StreamsReady == CyTrue))
        ReleaseAll ();
}

/* The target PC set the keyboard LEDs (SET_REPORT, output report). */
void ClientPeriphKeyboardLeds (uint8_t Leds)
{
    KeyboardLeds = Leds;
    StateChanged = CyTrue;
}

/* With HID_IDLE_REPEAT the HID endpoint always has a report committed: real events first, otherwise the last keyboard state again - what a
 * keyboard with a HID idle rate does; a repeated keyboard report changes nothing for the target PC. */
static uint8_t KeyboardLast[8];

/* 1: the endpoint always has a report committed (see above). 0: nothing is committed while there is no real event, so an idle endpoint answers
 * "not ready" like a normal keyboard. */
#ifndef HID_IDLE_REPEAT
#define HID_IDLE_REPEAT          (0)
#endif

/* The one HID endpoint carries both devices. Report protocol (the normal case): every report starts with its report ID, keyboard 1 + 8 bytes,
 * mouse 2 + 4 bytes. Boot protocol (a BIOS): the plain 8 byte keyboard report, the mouse is not available (its queue is dropped).
 * Order: a real keyboard report first, then a real mouse report, otherwise the last keyboard state again (an idle repeat). */
static CyBool_t PumpHid (void)
{
    CyU3PDmaBuffer_t Buffer;
    CyBool_t Boot;
    CyBool_t RealKeyboard = CyFalse;
    CyBool_t RealMouse = CyFalse;
    uint16_t Count;

    if ((KeyboardChannelOk == CyFalse) || (UsbConfigured == CyFalse))
        return (CyFalse);

    Boot = (HidProtocol == 0) ? CyTrue : CyFalse;
    if (Boot == CyTrue)
        MouseTail = MouseHead;

#if (HID_IDLE_REPEAT == 0)
    if ((KeyboardTail == KeyboardHead) && (MouseTail == MouseHead))
        return (CyFalse);
#endif

    if (CyU3PDmaChannelGetBuffer (&KeyboardChannel, &Buffer, CYU3P_NO_WAIT) != CY_U3P_SUCCESS)
    {
#ifdef INPUT_DIAG
        DiagKbGetFail++;
#endif
        return (CyFalse);
    }

    if (KeyboardTail != KeyboardHead)
    {
        RealKeyboard = CyTrue;
        CyU3PMemCopy (KeyboardLast, KeyboardQueue[KeyboardTail % KEYBOARD_QUEUE_LENGTH], 8);
    }
    else if (MouseTail != MouseHead)
    {
        RealMouse = CyTrue;
    }

    if (RealMouse == CyTrue)
    {
        Buffer.buffer[0] = 2;
        CyU3PMemCopy (Buffer.buffer + 1, MouseQueue[MouseTail % MOUSE_QUEUE_LENGTH], 4);
        Count = 5;
    }
    else if (Boot == CyTrue)
    {
        CyU3PMemCopy (Buffer.buffer, KeyboardLast, 8);
        Count = 8;
    }
    else
    {
        Buffer.buffer[0] = 1;
        CyU3PMemCopy (Buffer.buffer + 1, KeyboardLast, 8);
        Count = 9;
    }

    if (CyU3PDmaChannelCommitBuffer (&KeyboardChannel, Count, 0) != CY_U3P_SUCCESS)
    {
#ifdef INPUT_DIAG
        DiagKbCommitFail++;
#endif
        return (CyFalse);
    }
#ifdef INPUT_DIAG
    if (RealKeyboard == CyTrue)
        DiagKbReal++;
    else if (RealMouse == CyTrue)
        DiagMouseReal++;
    else
        DiagKbRepeat++;
#endif

    if (RealKeyboard == CyTrue)
        KeyboardTail++;
    if (RealMouse == CyTrue)
        MouseTail++;

    /* A repeat is not "work": the worker thread sleeps a millisecond before it looks again. */
    return ((RealKeyboard == CyTrue) || (RealMouse == CyTrue));
}

void ClientPeriphSetProtocol (uint8_t Protocol)
{
    HidProtocol = (Protocol == 0) ? 0 : 1;
}

uint8_t ClientPeriphGetProtocol (void)
{
    return (HidProtocol);
}

static void SendState (void)
{
    uint8_t Body[4];

    Body[0] = (UsbConfigured == CyTrue) ? 1 : 0;
    Body[1] = (UsbConfigured == CyTrue) ? 1 : 0;
    Body[2] = KeyboardLeds;
    Body[3] = (UsbSuspended == CyTrue) ? 1 : 0;
    PeriphSendMessage (PERIPH_CHANNEL_KEYBOARD_MOUSE, KM_STATE, Body, sizeof (Body), 0);
    PeriphSendMessage (PERIPH_CHANNEL_KEYBOARD_MOUSE, KM_LEDS, (const uint8_t *)&KeyboardLeds, 1, 0);
}

/* ---- messages from the Host (lane thread: record only, never block) ---- */

void ClientPeriphOnMessage (const uint8_t *Block, uint32_t Length)
{
    uint8_t Channel = Block[0];
    uint8_t Type = Block[1];
    uint32_t PayloadLength = (uint32_t)Block[4] | ((uint32_t)Block[5] << 8) | ((uint32_t)Block[6] << 16) | ((uint32_t)Block[7] << 24);
    const uint8_t *Payload = Block + 8;

    if (StreamsReady == CyFalse)
        return;

    if (Channel == PERIPH_CHANNEL_KEYBOARD_MOUSE)
    {
        if ((Type == KM_KEYBOARD_REPORT) && (PayloadLength >= 8))
        {
            KeyboardPush (Payload);
#ifdef INPUT_DIAG
            {
                uint32_t DiagNow = CyU3PGetTime ();

                DiagKbRx++;
                if ((DiagLastRx != 0) && ((DiagNow - DiagLastRx) < 10000) && ((DiagNow - DiagLastRx) > DiagRxGapMax))
                    DiagRxGapMax = DiagNow - DiagLastRx;
                DiagLastRx = DiagNow;
            }
#endif
        }
        else if ((Type == KM_MOUSE_REPORT) && (PayloadLength >= 4))
        {
            MousePush (Payload[0], (int8_t)Payload[1], (int8_t)Payload[2], (int8_t)Payload[3]);
        }
        else if (Type == KM_RELEASE_ALL)
        {
            ReleaseAll ();
        }
        else if (Type == KM_QUERY)
        {
            StateChanged = CyTrue;
        }
    }
    else if (Channel == PERIPH_CHANNEL_MIDI)
    {
        PeriphStreamOnMessage (&MidiStream, Type, Payload, PayloadLength);
    }
    else if (Channel == PERIPH_CHANNEL_SERIAL)
    {
        if (Type == SERIAL_LINES)
        {
            if (PayloadLength >= 1)
                PeriphCdcOnLines (&SerialCdc, Payload[0]);
        }
        else
        {
            PeriphStreamOnMessage (&SerialStream, Type, Payload, PayloadLength);
        }
    }
}

/* ---- USB class requests of the serial port's communication interface (from the setup callback) ---- */

CyBool_t ClientPeriphCdcSetup (uint8_t Request, uint16_t Value, uint16_t Length)
{
    return (PeriphCdcSetup (&SerialCdc, Request, Value, Length));
}

/* The target PC has read the keyboard LED output report: 1 byte data stage of SET_REPORT. Returns CyTrue when the data stage was handled. */
CyBool_t ClientPeriphSetReport (uint16_t Length)
{
    uint16_t Read = 0;

    if ((Length == 0) || (Length > 8))
        return (CyFalse);

    if ((CyU3PUsbGetEP0Data (32, Ep0Buffer, &Read) != CY_U3P_SUCCESS) || (Read == 0))
    {
        CyU3PUsbStall (0, CyTrue, CyFalse);
        return (CyTrue);
    }

    /* Report protocol: report ID 1 + the LED byte; boot protocol: the LED byte alone. */
    if ((Read >= 2) && (Ep0Buffer[0] == 1))
        ClientPeriphKeyboardLeds (Ep0Buffer[1]);
    else if (Read == 1)
        ClientPeriphKeyboardLeds (Ep0Buffer[0]);
    return (CyTrue);
}

/* The Client announced itself to the Host: tell the Host to forget the old stream state (call from the helper thread, after the announcement). */
void ClientPeriphAnnounce (void)
{
    uint8_t Dummy = 0;

    if (StreamsReady == CyFalse)
        return;

    MidiStream.PeerUp = CyTrue;
    SerialStream.PeerUp = CyTrue;
    PeriphStreamRequestReset (&MidiStream);
    PeriphStreamRequestReset (&SerialStream);
    PeriphCdcResync (&SerialCdc);
    PeriphSendMessage (PERIPH_CHANNEL_MIDI, STREAM_RESET, &Dummy, 0, 5000);
    PeriphSendMessage (PERIPH_CHANNEL_SERIAL, STREAM_RESET, &Dummy, 0, 5000);
    StateChanged = CyTrue;
}

static void PeriphEntry (uint32_t Input)
{
    CyBool_t Worked;
#ifdef INPUT_DIAG
    uint32_t DiagNow;
#endif

    for (;;)
    {
        Worked = CyFalse;

#ifdef INPUT_DIAG
        DiagNow = CyU3PGetTime ();
        DiagLoops++;
        if ((DiagLastLoop != 0) && ((DiagNow - DiagLastLoop) > DiagLoopGapMax))
            DiagLoopGapMax = DiagNow - DiagLastLoop;
        DiagLastLoop = DiagNow;

        if (((DiagNow - DiagLastPrint) >= 2000) && ((DiagKbRx != 0) || (DiagKbReal != 0)))
        {
            DiagLastPrint = DiagNow;
            CyU3PDebugPrint (4, "DIAG kb rx %d real %d rep %d getfail %d cfail %d q %d rxgap %d | mouse real %d rep %d gf %d | loops %d gap %d ms spd %d\r\n",
                    DiagKbRx, DiagKbReal, DiagKbRepeat, DiagKbGetFail, DiagKbCommitFail, DiagKbQueueMax, DiagRxGapMax, DiagMouseReal, DiagMouseRepeat,
                    DiagMouseGetFail, DiagLoops, DiagLoopGapMax, CyU3PUsbGetSpeed ());
            DiagKbRx = 0;
            DiagKbReal = 0;
            DiagKbRepeat = 0;
            DiagKbGetFail = 0;
            DiagKbCommitFail = 0;
            DiagKbQueueMax = 0;
            DiagRxGapMax = 0;
            DiagMouseReal = 0;
            DiagMouseRepeat = 0;
            DiagMouseGetFail = 0;
            DiagLoops = 0;
            DiagLoopGapMax = 0;
        }
#endif


        if (ChannelsDown == CyTrue)
        {
            CyU3PThreadSleep (1);
            continue;
        }

        if (PumpHid () == CyTrue)
            Worked = CyTrue;
        if (PeriphStreamPump (&MidiStream) == CyTrue)
            Worked = CyTrue;
        if (PeriphStreamPump (&SerialStream) == CyTrue)
            Worked = CyTrue;
        if (PeriphCdcPump (&SerialCdc, SerialStream.PeerUp) == CyTrue)
            Worked = CyTrue;

#ifdef MIDI_DIAG
        PeriphStreamDiagTick ("Client MIDI", &MidiStream);
#endif

        if (StateChanged == CyTrue)
        {
            StateChanged = CyFalse;
            SendState ();
            Worked = CyTrue;
        }

        if (Worked == CyFalse)
            CyU3PThreadSleep (1);
    }
}

void ClientPeriphInit (void)
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
    if ((Stack == NULL) || (CyU3PThreadCreate (&PeriphThread, "29:Periph", PeriphEntry, 0, Stack, PERIPH_THREAD_STACK,
            PERIPH_THREAD_PRIORITY, PERIPH_THREAD_PRIORITY, CYU3P_NO_TIME_SLICE, CYU3P_AUTO_START) != CY_U3P_SUCCESS))
    {
        CyU3PDebugPrint (4, "Peripherals: could not create the worker thread\r\n");
        return;
    }

    StreamsReady = CyTrue;
}
