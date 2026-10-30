/* The GPIF "lane" between the Host and the Client device. The same code runs on both boards (full RTOS API); only the DMA
 * channels differ: the Host reads PIB socket 0 and writes PIB socket 1, the Client uses the same socket numbers the other way round.
 *
 * What the GPIF gives us: a clean stream of WHOLE DMA buffers (188 MB/s per direction raw, no errors in ~61 GB of soak tests)
 * but no packet boundaries below one buffer (a short buffer waits in the receiver for the next one). So:
 *
 *   * one message block (8 byte header, payload padded to 4 bytes; no check word) travels in ONE GPIF buffer of GPIF_LINK_BUFFER_BYTES; the
 *     sender commits the whole buffer, the bytes behind the message are stale and never looked at. The message therefore is at most one
 *     buffer: GPIF_LINK_MAX_MESSAGE_BYTES (8 + GPIF_LINK_MAX_PAYLOAD_BYTES).
 *   * no ARQ, no retransmission, no sequence numbers: the GPIF hop made no error in the soak tests, USB protects its own hops with
 *     CRC16, and the message layer above has its own timeouts (60 s media timeout, Client supervision by PING). A buffer that does not hold
 *     a plausible header (floating bus, damage) is dropped and counted.
 *   * flow control is the DMA buffer chain itself: a sender that finds no free write buffer waits, the Master stalls when the Slave does not read.
 *
 * Message block: byte 0 channel, byte 1 type, bytes 2-3 tag, bytes 4-7 payload length, then the payload padded with zeros to a multiple of 4.
 * Channel 0 with type 0 does not exist (it identifies an empty / floating buffer). */

#ifndef _INCLUDED_GPIFLINKCORE_H_
#define _INCLUDED_GPIFLINKCORE_H_

#include "cyu3types.h"
#include "cyu3dma.h"
#include "cyu3externcstart.h"

/* One GPIF buffer = one message. The Host's GPIF counters span 2 KB, the buffer is a multiple of that. */
#ifndef GPIF_LINK_BUFFER_BYTES
#define GPIF_LINK_BUFFER_BYTES           (16384)
#endif
/* The message does not start at the beginning of the GPIF buffer but GPIF_LINK_MESSAGE_OFFSET bytes into it: then the payload (8 bytes behind
 * the message start) begins at offset 32, a 32 byte aligned address, so a USB DMA channel can receive into / send from a lane buffer directly
 * (CyU3PDmaChannelSetupRecvBuffer / SetupSendBuffer want 32 byte aligned buffers) and the CPU never has to copy bulk data. The bytes in front of
 * the message are never looked at. */
#define GPIF_LINK_MESSAGE_OFFSET         (24)
#define GPIF_LINK_MAX_MESSAGE_BYTES      (GPIF_LINK_BUFFER_BYTES - GPIF_LINK_MESSAGE_OFFSET)   /* A message block at its largest: header + payload. */
#define GPIF_LINK_MAX_PAYLOAD_BYTES      (GPIF_LINK_MAX_MESSAGE_BYTES - 8)                     /* 16352, a multiple of 4. */
#define GPIF_LINK_MAX_CHANNEL            (7)                                    /* Message channels 0-7 exist; a first byte above that is not a message. */

/* Buffers per DMA channel of the lane (Host and Client create the channels with this count). */
#ifndef GPIF_LINK_CHANNEL_BUFFERS
#define GPIF_LINK_CHANNEL_BUFFERS        (3)
#endif

/* Left over from the ARQ link so that the test builds still compile; they have no effect. */
#ifndef GPIF_LINK_MAX_WINDOW
#define GPIF_LINK_MAX_WINDOW             (2)
#endif
#ifndef GPIF_LINK_WINDOW
#define GPIF_LINK_WINDOW                 (2)
#endif
#define GPIF_LINK_FAULT_NONE             (0)
#define GPIF_LINK_FAULT_BIT_FLIP         (1)
#define GPIF_LINK_FAULT_ZERO_WORD        (2)
#define GPIF_LINK_FAULT_BLANK_BUFFER     (3)
#define GPIF_LINK_FAULT_DELETE_WORD      (4)

typedef void (*GpifLinkMessageCallback) (const uint8_t *MessageBlock, uint32_t Length);

typedef struct
{
    CyU3PDmaChannel         *ReadChannel;      /* MANUAL_IN: PIB socket -> CPU, buffers of GPIF_LINK_BUFFER_BYTES. */
    CyU3PDmaChannel         *WriteChannel;     /* MANUAL_OUT: CPU -> PIB socket, buffers of GPIF_LINK_BUFFER_BYTES. */
    GpifLinkMessageCallback  OnMessage;        /* Called from the link thread, in order, for every plausible message block; the block is only valid during the call. */
} GpifLinkConfig;

typedef struct
{
    uint32_t BlocksSent;          /* Messages put into a write buffer. */
    uint32_t Retransmissions;     /* (unused, always 0) */
    uint32_t BareAcksSent;        /* (unused) */
    uint32_t NaksSent;            /* (unused) */
    uint32_t NaksReceived;        /* (unused) */
    uint32_t BlocksDelivered;     /* Messages passed to OnMessage. */
    uint32_t Duplicates;          /* (unused) */
    uint32_t Gaps;                /* (unused) */
    uint32_t MessageCrcErrors;    /* (unused) */
    uint32_t HeaderSkips;         /* Received buffers without a plausible message header (dropped). */
    uint32_t BuffersSent;
    uint32_t BuffersReceived;
    uint32_t EmptyBuffers;        /* (unused) */
    uint32_t StaleAborts;         /* (unused) */
} GpifLinkStats;

/* Stores the configuration and creates the transmit lock. Call once; the DMA channels have to exist and be started (SetXfer 0). */
extern CyBool_t GpifLinkInit (const GpifLinkConfig *Config);

/* Nothing is kept between messages; kept for the callers (the channel reset is theirs). */
extern void GpifLinkReset (void);

/* Makes the link thread stop doing anything (it then sleeps in its loop, outside any callback) and waits up to TimeoutMs until it
 * has. Returns CyFalse if it did not stop in time (the pause request is withdrawn). Needed before the DMA channels of the lane are
 * torn down or reset. GpifLinkResume lets it run again. */
extern CyBool_t GpifLinkPause (uint32_t TimeoutMs);
extern void GpifLinkResume (void);

/* Queues a complete message block (header, payload padded to 4 bytes) into the next free GPIF write buffer. Waits up to TimeoutMs for a free
 * buffer; the block is copied. Returns CyFalse on timeout or a malformed block. May be called from any thread. */
extern CyBool_t GpifLinkSend (const uint8_t *MessageBlock, uint32_t Length, uint32_t TimeoutMs);

/* Zero copy sending: GpifLinkTxGet takes the next free GPIF write buffer (waiting up to TimeoutMs) and returns where its message block starts
 * (32 byte aligned payload at Message + 8); the caller fills in the header (and the payload, or lets a USB channel receive into it) and calls
 * GpifLinkTxCommit, which ships the OLDEST buffer taken so far. Up to 4 buffers may be taken before the first is committed. The transmit lock is
 * held from the first Get until the last taken buffer is committed or cancelled, so Get / Commit / Cancel have to come from one thread, and the
 * time in between has to be short. GpifLinkTxCancel commits every buffer taken so far as an empty (invalid) buffer that the receiver drops. */
extern CyBool_t GpifLinkTxGet (uint32_t TimeoutMs, uint8_t **Message);
extern CyBool_t GpifLinkTxCommit (void);
extern void GpifLinkTxCancel (void);

/* Lays out a message block (channel, type, tag, payload) in Destination; returns its length in bytes (8 + payload rounded up to 4).
 * Destination must hold GPIF_LINK_MAX_MESSAGE_BYTES. */
extern uint32_t GpifLinkBuildMessage (uint8_t *Destination, uint8_t Channel, uint8_t Type, uint16_t Tag,
        const uint8_t *Payload, uint32_t PayloadLength);

/* A Fletcher style sum over Length bytes (a multiple of 4); not part of the message format, kept for tests and benchmarks. */
extern uint32_t GpifLinkCheck (const uint8_t *Data, uint32_t Length);

/* Copies Length bytes; word-aligned source and destination go eight words at a time (about 3x faster than CyU3PMemCopy). */
extern void GpifLinkCopyBytes (uint8_t *Destination, const uint8_t *Source, uint32_t Length);

/* CRC-32 (the zlib one). */
extern uint32_t GpifLinkCrc32 (const uint8_t *Data, uint32_t Length);
extern uint32_t GpifLinkCrc32Size (const uint8_t *Data, uint32_t Length);   /* benchmark only (GPIF_LINK_BENCH) */
extern uint32_t GpifLinkCrc32Byte (const uint8_t *Data, uint32_t Length);   /* benchmark only */

/* No check word any more: does nothing. */
extern void GpifLinkSealMessage (uint8_t *MessageBlock, uint32_t Length);

/* Not needed any more (the receive call blocks on the DMA event); kept so that code that registers it still builds. */
extern void GpifLinkDmaCallback (CyU3PDmaChannel *Handle, CyU3PDmaCbType_t Type, CyU3PDmaCBInput_t *Input);

/* Entry function of the link thread (the receiver); create it with a 4 KB stack at a priority BELOW the guard thread's (a number above
 * GUARD_THREAD_PRIORITY): it can legitimately run flat out for minutes and must not starve the guard. */
extern void GpifLinkThreadEntry (uint32_t Input);

extern CyBool_t GpifLinkIsIdle (void);
extern CyBool_t GpifLinkSetWindow (uint32_t NewWindow);

extern const GpifLinkStats *GpifLinkGetStats (void);
extern void GpifLinkPrintState (void);

/* Passes of the link thread so far (0 = it never ran); a value that stops changing means the thread hangs. */
extern uint32_t GpifLinkHeartbeat (void);

extern void GpifLinkSetFault (uint32_t Mode, uint32_t Period);

#include "cyu3externcend.h"

#endif /* _INCLUDED_GPIFLINKCORE_H_ */
