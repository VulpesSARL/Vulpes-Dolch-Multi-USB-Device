/* The byte stream engine behind the MIDI and the serial port function, shared by the Host and the Client firmware (full RTOS API).
 *
 * A stream joins one pair of USB bulk endpoints of THIS board with the same pair of the OTHER board through the GPIF lane (message channel 4 or 5,
 * PeriphProtocol.h): whatever the USB host writes to the OUT endpoint is sent over the lane and written to the IN endpoint of the other board, and
 * the other way round - like a cable between the two. No PC application is involved.
 *
 *   OUT endpoint --> [USB buffer] --STREAM_DATA--> lane --> [ring] --> IN endpoint of the other board
 *
 * Flow control: the receiving board has a ring (PeriphStream.RingBytes); the sending board may have at most that many bytes in flight and counts
 * them with the totals of the STREAM_CREDIT messages. A USB OUT buffer that does not fit is simply not taken, so the USB host sees NAKs - exactly
 * what happens on a hardware flow-controlled line. If the IN endpoint's USB host stops reading (the port is closed) for StallMs the ring is thrown
 * away, so a closed port on one side can never hang the application on the other.
 *
 * Threading: PeriphStreamOnMessage is called from the lane thread, PeriphStreamPump from one worker thread (priority 11, below the guard
 * thread) that calls it for every stream in a loop and sleeps 1 ms when nothing happened; PeriphStreamSetUsb from the USB event context. Nothing
 * here ever blocks for long. */

#ifndef _INCLUDED_PERIPHCORE_H_
#define _INCLUDED_PERIPHCORE_H_

#include "cyu3types.h"
#include "cyu3os.h"
#include "cyu3dma.h"
#include "cyu3externcstart.h"

typedef struct
{
    /* Set by PeriphStreamInit. */
    uint8_t                 Channel;
    uint8_t                 Align;              /* USB data is moved in multiples of this (4 for MIDI event packets, 1 for the serial port). */
    uint32_t                RingBytes;          /* Size of our receive ring (power of two); the other board's window. */
    uint32_t                PeerRingBytes;      /* Size of the other board's ring: our window. */
    uint32_t                StallMs;
    uint8_t                *Ring;
    CyU3PMutex              UsbLock;

    /* Set by the owner. */
    CyU3PDmaChannel *volatile UsbOut;           /* MANUAL_IN, USB OUT endpoint -> CPU; NULL while the endpoints are down. */
    CyU3PDmaChannel *volatile UsbIn;            /* MANUAL_OUT, CPU -> USB IN endpoint. */
    volatile uint32_t       UsbPacketSize;      /* Bulk packet size of the speed in use (64 / 512 / 1024). */
    volatile uint8_t        UsbInEndpoint;      /* Address of the IN endpoint (to flush it when nobody reads). */
    volatile CyBool_t       PeerUp;             /* The other board is there: if not, data from the USB OUT endpoint is thrown away. */
    CyBool_t                UsePortState;       /* Serial port: remember a closed port (see ClosedMode) and notice the opening (PeriphStreamPortOpened). */
    volatile CyBool_t       ClosedMode;         /* Nobody reads the IN endpoint: incoming data is dropped at once instead of throttling the sender. */
    volatile CyBool_t       OpenHint;           /* The USB host just (re)opened the port: throw away what piled up while it was closed. */

    /* Written by the lane thread only. */
    volatile uint32_t       Head;               /* Free running byte counters of the ring. */
    volatile uint32_t       PeerFreedTotal;     /* The other board's last STREAM_CREDIT. */
    volatile CyBool_t       ResetRequested;

    /* Written by the pump only. */
    volatile uint32_t       Tail;
    uint32_t                SentTotal;
    uint32_t                FreedTotal;
    uint32_t                FreedAnnounced;
    uint32_t                ProgressTime;
    uint32_t                LastCommitTime;     /* Last bytes put into the IN endpoint ... */
    CyBool_t                NeedFlush;          /* ... and not yet known to be taken by the USB host. */

    /* Statistics. */
    uint32_t                BytesToUsb;
    uint32_t                BytesFromUsb;
    uint32_t                Dropped;            /* Bytes thrown away (peer down, stalled port). */
    uint32_t                Overflows;          /* Bytes that did not fit the ring although the window said they should (a bug if not 0). */
    uint32_t                MessagesIn;         /* STREAM_DATA messages received from the other board. */
    uint32_t                CreditStalls;       /* Passes where a USB buffer was waiting but the window was too small. */
    uint32_t                Resets;             /* STREAM_RESETs seen (own request or the other board's). */
    uint32_t                DiagTime;
    uint32_t                DiagSum;
} PeriphStream;

/* Debug build (-DMIDI_DIAG): prints the counters of a stream on the debug UART every 2 s while they change. */
extern void PeriphStreamDiagTick (const char *Name, PeriphStream *Stream);

/* Allocates the ring and the lock. Returns CyFalse when memory is short. */
extern CyBool_t PeriphStreamInit (PeriphStream *Stream, uint8_t Channel, uint32_t RingBytes, uint32_t PeerRingBytes, uint32_t Align, uint32_t StallMs);

/* The USB endpoints came up (channels created) or are about to go away (NULL, NULL). Waits until the pump is not using the old channels. */
extern void PeriphStreamSetUsb (PeriphStream *Stream, CyU3PDmaChannel *UsbOut, CyU3PDmaChannel *UsbIn, uint32_t UsbPacketSize, uint8_t UsbInEndpoint);

/* A message of this stream's channel arrived from the other board (lane thread). */
extern void PeriphStreamOnMessage (PeriphStream *Stream, uint8_t Type, const uint8_t *Payload, uint32_t Length);

/* Asks for the state to be forgotten (done by the pump at its next pass). */
extern void PeriphStreamRequestReset (PeriphStream *Stream);

/* The USB host opened the serial port (it sent SET_LINE_CODING / SET_CONTROL_LINE_STATE): data that arrived while it was closed is discarded, as a real
 * UART would have lost it, and a closed port is no longer assumed. */
extern void PeriphStreamPortOpened (PeriphStream *Stream);

/* One pass: USB OUT -> lane, ring -> USB IN, credits. Returns CyTrue when it moved something (the caller then does not sleep). */
extern CyBool_t PeriphStreamPump (PeriphStream *Stream);

/* Sends one message of this stream's channel through the lane without waiting (CyFalse when no lane buffer is free right now). */
extern CyBool_t PeriphSendMessage (uint8_t Channel, uint8_t Type, const uint8_t *Payload, uint32_t Length, uint32_t TimeoutMs);

/* ---- CDC-ACM serial port bookkeeping (the class requests of the communication interface), shared by both boards ---- */

#define PERIPH_CDC_SET_LINE_CODING          (0x20)
#define PERIPH_CDC_GET_LINE_CODING          (0x21)
#define PERIPH_CDC_SET_CONTROL_LINE_STATE   (0x22)
#define PERIPH_CDC_SEND_BREAK               (0x23)

typedef struct
{
    uint8_t                 LineCoding[7];      /* dwDTERate (le), bCharFormat, bParityType, bDataBits - only remembered and returned. */
    volatile uint8_t        LocalLines;         /* DTR / RTS of the COM port on this board. */
    volatile uint8_t        LocalLinesSent;     /* ... as last sent to the other board. */
    volatile uint8_t        RemoteLines;        /* DTR / RTS of the COM port on the other board. */
    volatile uint8_t        NotifiedState;      /* The SERIAL_STATE bits last sent to the USB host (0xFF = none yet). */
    volatile CyBool_t       NotifyNeeded;
    CyU3PDmaChannel *volatile Notify;           /* MANUAL_OUT to the interrupt IN endpoint, NULL while down. */
    uint16_t                CommInterface;
    PeriphStream           *Stream;             /* The data stream of this port (told when the port is opened). */
} PeriphCdc;

extern void PeriphCdcInit (PeriphCdc *Cdc, uint16_t CommInterface);

/* Handles a class request addressed to the communication interface (runs in the USB setup callback and does the data stage itself).
 * Returns CyTrue when the request was one of the four CDC requests. */
extern CyBool_t PeriphCdcSetup (PeriphCdc *Cdc, uint8_t Request, uint16_t Value, uint16_t Length);

/* The other board's SERIAL_LINES message arrived (lane thread). */
extern void PeriphCdcOnLines (PeriphCdc *Cdc, uint8_t Lines);

/* Called by the pump thread: sends our lines to the other board when they changed, and the SERIAL_STATE notification to the USB host. */
extern CyBool_t PeriphCdcPump (PeriphCdc *Cdc, CyBool_t PeerUp);

/* The other board is (back) up: send the lines again. The notification state is sent again too. */
extern void PeriphCdcResync (PeriphCdc *Cdc);

#include "cyu3externcend.h"

#endif /* _INCLUDED_PERIPHCORE_H_ */
