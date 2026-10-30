#include "cyu3system.h"
#include "cyu3os.h"
#include "cyu3dma.h"
#include "cyu3error.h"
#include "cyu3usb.h"
#include "GpifLinkCore.h"
#include "PeriphProtocol.h"
#include "PeriphCore.h"

/* See PeriphCore.h. */

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

CyBool_t PeriphSendMessage (uint8_t Channel, uint8_t Type, const uint8_t *Payload, uint32_t Length, uint32_t TimeoutMs)
{
    uint8_t *Message;
    uint32_t Padded = (Length + 3) & ~((uint32_t)3);
    uint32_t Index;

    if (Length > GPIF_LINK_MAX_PAYLOAD_BYTES)
        return (CyFalse);

    if (GpifLinkTxGet (TimeoutMs, &Message) == CyFalse)
        return (CyFalse);

    Message[0] = Channel;
    Message[1] = Type;
    Message[2] = 0;
    Message[3] = 0;
    WriteWord (Message + 4, Length);
    if (Length != 0)
        GpifLinkCopyBytes (Message + 8, Payload, Length);
    for (Index = Length; Index < Padded; Index++)
        Message[8 + Index] = 0;

    return (GpifLinkTxCommit ());
}

CyBool_t PeriphStreamInit (PeriphStream *Stream, uint8_t Channel, uint32_t RingBytes, uint32_t PeerRingBytes, uint32_t Align, uint32_t StallMs)
{
    CyU3PMemSet ((uint8_t *)Stream, 0, sizeof (PeriphStream));
    Stream->Channel = Channel;
    Stream->Align = (uint8_t)Align;
    Stream->RingBytes = RingBytes;
    Stream->PeerRingBytes = PeerRingBytes;
    Stream->StallMs = StallMs;

    Stream->Ring = (uint8_t *)CyU3PDmaBufferAlloc (RingBytes);
    if (Stream->Ring == NULL)
        return (CyFalse);

    if (CyU3PMutexCreate (&Stream->UsbLock, CYU3P_NO_INHERIT) != CY_U3P_SUCCESS)
        return (CyFalse);

    return (CyTrue);
}

void PeriphStreamSetUsb (PeriphStream *Stream, CyU3PDmaChannel *UsbOut, CyU3PDmaChannel *UsbIn, uint32_t UsbPacketSize, uint8_t UsbInEndpoint)
{
    /* The pump holds the lock for the length of one pass, so after this the old channels are no longer in use. */
    CyU3PMutexGet (&Stream->UsbLock, CYU3P_WAIT_FOREVER);
    Stream->UsbOut = UsbOut;
    Stream->UsbIn = UsbIn;
    Stream->UsbPacketSize = UsbPacketSize;
    Stream->UsbInEndpoint = UsbInEndpoint;
    Stream->NeedFlush = CyFalse;
    Stream->ProgressTime = CyU3PGetTime ();
    CyU3PMutexPut (&Stream->UsbLock);
}

void PeriphStreamRequestReset (PeriphStream *Stream)
{
    Stream->ResetRequested = CyTrue;
}

void PeriphStreamPortOpened (PeriphStream *Stream)
{
    if (Stream->UsePortState == CyTrue)
        Stream->OpenHint = CyTrue;
}

void PeriphStreamOnMessage (PeriphStream *Stream, uint8_t Type, const uint8_t *Payload, uint32_t Length)
{
    uint32_t Space;
    uint32_t Position;
    uint32_t First;
    uint32_t Value;

    if (Type == STREAM_DATA)
    {
        Space = Stream->RingBytes - (Stream->Head - Stream->Tail);
        if (Length > Space)
        {
            Stream->Overflows += Length;
            return;
        }

        Stream->MessagesIn++;
        Position = Stream->Head & (Stream->RingBytes - 1);
        First = Stream->RingBytes - Position;
        if (First > Length)
            First = Length;
        CyU3PMemCopy (Stream->Ring + Position, (uint8_t *)Payload, First);
        if (First < Length)
            CyU3PMemCopy (Stream->Ring, (uint8_t *)Payload + First, Length - First);
        Stream->Head += Length;
    }
    else if ((Type == STREAM_CREDIT) && (Length >= 4))
    {
        Value = ReadWord (Payload);
        if ((int32_t)(Value - Stream->PeerFreedTotal) > 0)
            Stream->PeerFreedTotal = Value;
    }
    else if (Type == STREAM_RESET)
    {
        Stream->ResetRequested = CyTrue;
    }
}

/* One pass over a stream. Runs with UsbLock held. */
static CyBool_t PumpLocked (PeriphStream *Stream)
{
    CyU3PDmaBuffer_t Buffer;
    CyBool_t Worked = CyFalse;
    uint32_t Available;
    uint32_t Chunk;
    uint32_t Position;
    uint32_t First;
    uint32_t Credit;
    uint32_t Now = CyU3PGetTime ();
    uint8_t Announce[4];

    /* The other board restarted: nothing of what was counted before is valid any more. */
    if (Stream->ResetRequested == CyTrue)
    {
        Stream->ResetRequested = CyFalse;
        Stream->Resets++;
        Stream->Tail = Stream->Head;
        Stream->SentTotal = 0;
        Stream->FreedTotal = 0;
        Stream->FreedAnnounced = 0;
        Stream->PeerFreedTotal = 0;
        Stream->ProgressTime = Now;
        Worked = CyTrue;
    }

    /* The port was opened: nothing that arrived while it was closed belongs to the new session. */
    if (Stream->OpenHint == CyTrue)
    {
        Stream->OpenHint = CyFalse;

        /* Only data that has been waiting for a reader (a second or more, or the port was found closed) is stale; a program that changes its baud
         * rate or toggles DTR in the middle of a transfer must not lose what is on its way. */
        if ((Stream->ClosedMode == CyTrue) || ((Stream->NeedFlush == CyTrue) && ((Now - Stream->LastCommitTime) >= 1000)))
        {
            Stream->ClosedMode = CyFalse;
            Stream->NeedFlush = CyFalse;
            Available = Stream->Head - Stream->Tail;
            Stream->Tail += Available;
            Stream->FreedTotal += Available;
            Stream->ProgressTime = Now;
            /* The IN endpoint itself is NOT flushed here: doing that while the USB host is in the middle of opening the port (control requests in
             * flight) made the Client drop off the bus on the test PC. Flushing is only done when the host has been idle for StallMs (see below). */
        }
    }

    /* USB OUT endpoint -> lane. */
    if ((Stream->UsbOut != NULL) && (CyU3PDmaChannelGetBuffer (Stream->UsbOut, &Buffer, CYU3P_NO_WAIT) == CY_U3P_SUCCESS))
    {
        /* MIDI moves whole 4 byte event packets (a SysEx dump is a long run of them): never let a partial one into the stream. */
        Buffer.count -= Buffer.count % Stream->Align;

        if (Buffer.count == 0)
        {
            CyU3PDmaChannelDiscardBuffer (Stream->UsbOut);
            Worked = CyTrue;
        }
        else if (Stream->PeerUp == CyFalse)
        {
            Stream->Dropped += Buffer.count;
            CyU3PDmaChannelDiscardBuffer (Stream->UsbOut);
            Worked = CyTrue;
        }
        else
        {
            Credit = Stream->PeerRingBytes + Stream->PeerFreedTotal - Stream->SentTotal;
            if (((int32_t)Credit >= (int32_t)Buffer.count) &&
                (PeriphSendMessage (Stream->Channel, STREAM_DATA, Buffer.buffer, Buffer.count, 0) == CyTrue))
            {
                Stream->SentTotal += Buffer.count;
                Stream->BytesFromUsb += Buffer.count;
                CyU3PDmaChannelDiscardBuffer (Stream->UsbOut);
                Worked = CyTrue;
            }
            else if ((int32_t)Credit < (int32_t)Buffer.count)
            {
                Stream->CreditStalls++;
            }
        }
    }

    /* Ring -> USB IN endpoint. */
    Available = Stream->Head - Stream->Tail;
    if (Stream->ClosedMode == CyTrue)
    {
        /* Closed port: drop everything at once, the sender must not be held up. */
        Stream->Tail += Available;
        Stream->FreedTotal += Available;
        Stream->Dropped += Available;
        Stream->ProgressTime = Now;
    }
    else if (Available == 0)
    {
        Stream->ProgressTime = Now;
    }
    else if ((Stream->UsbIn != NULL) && (CyU3PDmaChannelGetBuffer (Stream->UsbIn, &Buffer, CYU3P_NO_WAIT) == CY_U3P_SUCCESS))
    {
        Chunk = (Available < Buffer.size) ? Available : Buffer.size;
        Chunk -= Chunk % Stream->Align;

        /* A transfer that ends exactly on a packet boundary would need a zero length packet to be seen as finished by the USB host: keep the
         * last Align bytes back for the next buffer instead. */
        if ((Stream->UsbPacketSize != 0) && (Chunk > Stream->Align) && ((Chunk % Stream->UsbPacketSize) == 0))
            Chunk -= Stream->Align;

        if (Chunk != 0)
        {
            Position = Stream->Tail & (Stream->RingBytes - 1);
            First = Stream->RingBytes - Position;
            if (First > Chunk)
                First = Chunk;
            CyU3PMemCopy (Buffer.buffer, Stream->Ring + Position, First);
            if (First < Chunk)
                CyU3PMemCopy (Buffer.buffer + First, Stream->Ring, Chunk - First);

            if (CyU3PDmaChannelCommitBuffer (Stream->UsbIn, Chunk, 0) == CY_U3P_SUCCESS)
            {
                Stream->Tail += Chunk;
                Stream->FreedTotal += Chunk;
                Stream->BytesToUsb += Chunk;
                Stream->ProgressTime = Now;
                Stream->LastCommitTime = Now;
                Stream->NeedFlush = CyTrue;
                Worked = CyTrue;
            }
        }
        else
        {
            CyU3PDmaChannelDiscardBuffer (Stream->UsbIn);
        }
    }
    else if ((Now - Stream->ProgressTime) >= Stream->StallMs)
    {
        /* Nobody takes the data (the port is closed or the board is not connected): throw it away so the sender is not held up for ever. */
        Chunk = Stream->Head - Stream->Tail;
        Stream->Tail += Chunk;
        Stream->FreedTotal += Chunk;
        Stream->Dropped += Chunk;
        Stream->ProgressTime = Now;
        Worked = CyTrue;

        /* No flush or reset of the IN endpoint here (or anywhere): on SuperSpeed an endpoint reset behind the USB host's back made the Client drop off
         * the bus on the test PC. The at most two buffers already in the endpoint stay there until the host reads them. */
        if (Stream->UsePortState == CyTrue)
            Stream->ClosedMode = CyTrue;
    }

    /* Give the sender its window back: when a quarter of the ring was delivered, or when the ring is empty. */
    if (Stream->FreedTotal != Stream->FreedAnnounced)
    {
        if (((Stream->FreedTotal - Stream->FreedAnnounced) >= (Stream->RingBytes / 4)) || (Stream->Head == Stream->Tail))
        {
            WriteWord (Announce, Stream->FreedTotal);
            if ((Stream->PeerUp == CyFalse) || (PeriphSendMessage (Stream->Channel, STREAM_CREDIT, Announce, 4, 0) == CyTrue))
            {
                Stream->FreedAnnounced = Stream->FreedTotal;
                Worked = CyTrue;
            }
        }
    }

    return (Worked);
}

#ifdef MIDI_DIAG
void PeriphStreamDiagTick (const char *Name, PeriphStream *Stream)
{
    uint32_t Now = CyU3PGetTime ();
    uint32_t Sum;

    if ((Now - Stream->DiagTime) < 2000)
        return;
    Stream->DiagTime = Now;

    Sum = Stream->BytesFromUsb + Stream->BytesToUsb + Stream->Dropped + Stream->Overflows + Stream->MessagesIn + Stream->Resets;
    if (Sum == Stream->DiagSum)
        return;
    Stream->DiagSum = Sum;

    CyU3PDebugPrint (4, "%s: fromUSB %d toUSB %d msgIn %d dropped %d overflow %d stalls %d resets %d | head %d tail %d sent %d freed %d ann %d peer %d up %d\r\n",
            Name, Stream->BytesFromUsb, Stream->BytesToUsb, Stream->MessagesIn, Stream->Dropped, Stream->Overflows, Stream->CreditStalls, Stream->Resets,
            Stream->Head, Stream->Tail, Stream->SentTotal, Stream->FreedTotal, Stream->FreedAnnounced, Stream->PeerFreedTotal, Stream->PeerUp);
    GpifLinkPrintState ();
}
#else
void PeriphStreamDiagTick (const char *Name, PeriphStream *Stream)
{
}
#endif

CyBool_t PeriphStreamPump (PeriphStream *Stream)
{
    CyBool_t Worked;

    if (CyU3PMutexGet (&Stream->UsbLock, CYU3P_NO_WAIT) != CY_U3P_SUCCESS)
        return (CyFalse);

    Worked = PumpLocked (Stream);
    CyU3PMutexPut (&Stream->UsbLock);
    return (Worked);
}

/* ---------------------------------------------------------------- CDC-ACM ---------------------------------------------------------------- */

void PeriphCdcInit (PeriphCdc *Cdc, uint16_t CommInterface)
{
    CyU3PMemSet ((uint8_t *)Cdc, 0, sizeof (PeriphCdc));
    Cdc->CommInterface = CommInterface;
    Cdc->LocalLinesSent = 0xFF;
    Cdc->NotifiedState = 0xFF;

    /* 9600 8N1, as a usual default; the application sets what it wants and the port only remembers it. */
    WriteWord (Cdc->LineCoding, 9600);
    Cdc->LineCoding[4] = 0;
    Cdc->LineCoding[5] = 0;
    Cdc->LineCoding[6] = 8;
}

/* The EP0 data stage wants a 32 byte aligned buffer and a count that is a multiple of 32; Read tells how much really came. */
static uint8_t CdcEp0Buffer[32] __attribute__((aligned(32)));

CyBool_t PeriphCdcSetup (PeriphCdc *Cdc, uint8_t Request, uint16_t Value, uint16_t Length)
{
    uint16_t Read = 0;

    if ((Cdc->Stream != NULL) && ((Request == PERIPH_CDC_SET_LINE_CODING) || (Request == PERIPH_CDC_SET_CONTROL_LINE_STATE)))
        PeriphStreamPortOpened (Cdc->Stream);

    switch (Request)
    {
    case PERIPH_CDC_SET_LINE_CODING:
        if ((Length != 7) || (CyU3PUsbGetEP0Data (32, CdcEp0Buffer, &Read) != CY_U3P_SUCCESS) || (Read != 7))
        {
            CyU3PUsbStall (0, CyTrue, CyFalse);
            return (CyTrue);
        }
        CyU3PMemCopy (Cdc->LineCoding, CdcEp0Buffer, 7);
        return (CyTrue);

    case PERIPH_CDC_GET_LINE_CODING:
        CyU3PUsbSendEP0Data ((Length < 7) ? Length : 7, Cdc->LineCoding);
        return (CyTrue);

    case PERIPH_CDC_SET_CONTROL_LINE_STATE:
        Cdc->LocalLines = (uint8_t)(Value & 0x03);
        CyU3PUsbAckSetup ();
        return (CyTrue);

    case PERIPH_CDC_SEND_BREAK:
        CyU3PUsbAckSetup ();
        return (CyTrue);

    default:
        return (CyFalse);
    }
}

void PeriphCdcOnLines (PeriphCdc *Cdc, uint8_t Lines)
{
    Cdc->RemoteLines = (uint8_t)(Lines & 0x03);
}

void PeriphCdcResync (PeriphCdc *Cdc)
{
    Cdc->LocalLinesSent = 0xFF;
    Cdc->NotifiedState = 0xFF;
    Cdc->RemoteLines = 0;
}

CyBool_t PeriphCdcPump (PeriphCdc *Cdc, CyBool_t PeerUp)
{
    CyU3PDmaBuffer_t Buffer;
    CyBool_t Worked = CyFalse;
    uint8_t Wanted;
    uint8_t Lines;

    /* Our DTR / RTS to the other board. */
    Lines = Cdc->LocalLines;
    if ((PeerUp == CyTrue) && (Lines != Cdc->LocalLinesSent))
    {
        if (PeriphSendMessage (PERIPH_CHANNEL_SERIAL, SERIAL_LINES, &Lines, 1, 0) == CyTrue)
        {
            Cdc->LocalLinesSent = Lines;
            Worked = CyTrue;
        }
    }

    /* The other board's DTR as DCD + DSR to our USB host, like the cross-wired DTR of a null modem cable. */
    Wanted = ((PeerUp == CyTrue) && ((Cdc->RemoteLines & 1) != 0)) ? 0x03 : 0x00;
    if ((Cdc->Notify != NULL) && ((Wanted != Cdc->NotifiedState) || (Cdc->NotifyNeeded == CyTrue)))
    {
        if (CyU3PDmaChannelGetBuffer (Cdc->Notify, &Buffer, CYU3P_NO_WAIT) == CY_U3P_SUCCESS)
        {
            Buffer.buffer[0] = 0xA1;                    /* class, interface, device to host */
            Buffer.buffer[1] = 0x20;                    /* SERIAL_STATE */
            Buffer.buffer[2] = 0;
            Buffer.buffer[3] = 0;
            Buffer.buffer[4] = (uint8_t)(Cdc->CommInterface & 0xFF);
            Buffer.buffer[5] = (uint8_t)(Cdc->CommInterface >> 8);
            Buffer.buffer[6] = 2;
            Buffer.buffer[7] = 0;
            Buffer.buffer[8] = Wanted;
            Buffer.buffer[9] = 0;
            if (CyU3PDmaChannelCommitBuffer (Cdc->Notify, 10, 0) == CY_U3P_SUCCESS)
            {
                Cdc->NotifiedState = Wanted;
                Cdc->NotifyNeeded = CyFalse;
                Worked = CyTrue;
            }
        }
    }

    return (Worked);
}
