/* The Host device side of the PC link: one message block per USB bulk transfer in each direction (vendor specific interface 0,
 * bound to WinUSB on the PC) and the control channel (HELLO, PING, STATUS, BYE, ERROR). USB itself paces the transfers (NAK) and
 * repeats damaged packets, so there are no sequence numbers, credits or fragments on this link. Mirrors the PC side in
 * Host-Tool\Vulpes-Dolch-Host-Common\Link\BulkLinkSession.cs. */

#ifndef _INCLUDED_LINK_H_
#define _INCLUDED_LINK_H_

#include "cyu3types.h"
#include "cyu3externcstart.h"

/* Message block: 8 byte header, payload padded to 4 bytes; no check word. One block always fits one GPIF lane buffer. */
#define LINK_MESSAGE_HEADER_SIZE         (8)
#define LINK_CHECK_SIZE                  (0)
#define LINK_MAX_MESSAGE_PAYLOAD         (16352)
#define LINK_MAX_BLOCK_SIZE              (LINK_MESSAGE_HEADER_SIZE + LINK_MAX_MESSAGE_PAYLOAD + LINK_CHECK_SIZE)

/* A block whose length is a multiple of this many bytes would end a transfer exactly on a packet boundary (64 bytes at full speed,
 * 512 at high speed, 1024 at super speed) and so without the short packet that ends it; both sides append 4 zero bytes then and
 * the receiver ignores them. */
#define LINK_PACKET_ALIGN                (64)

/* Message channels. */
#define LINK_CHANNEL_CONTROL             (0)
#define LINK_CHANNEL_CDROM               (1)
#define LINK_CHANNEL_HDD                 (2)
#define LINK_CHANNEL_KEYBOARD_MOUSE      (3)
#define LINK_CHANNEL_HOST_UPDATE         (7)            /* Firmware update of the Host itself: handled by the Host, not passed on. */

/* Control channel message types. */
#define LINK_CONTROL_HELLO               (0x01)
#define LINK_CONTROL_HELLO_ACK           (0x02)
#define LINK_CONTROL_BYE                 (0x03)
#define LINK_CONTROL_PING                (0x04)
#define LINK_CONTROL_PONG                (0x05)
#define LINK_CONTROL_STATUS              (0x06)
#define LINK_CONTROL_ERROR               (0x07)
#define LINK_CONTROL_STATUS_REQUEST      (0x08)
#define LINK_CONTROL_PC_LINK             (0x09)         /* Host -> Client only: u8 1 = the PC is connected, 0 = it is not. */
#define LINK_CONTROL_CLIENT_STATUS       (0x0A)         /* Client -> Host only: u32 Client firmware version + the 28 byte STATUS body (media fields). */
#define LINK_CONTROL_CLIENT_USB          (0x0D)         /* Host -> Client only: u8 1 = connect your USB side to the PC (the Client keeps it off until the Host says so). */
#define LINK_CONTROL_CLIENT_RESET_REQUEST (0x0C)        /* Client -> Host only: please pulse my reset line (a chip that resets itself ends up in the USB bootloader until the next power cycle). */
#define LINK_CONTROL_BULK_ARM            (0x0E)         /* PC -> Host only: u32 bytes (a multiple of 16384) - the next transfer on bulk OUT 2 goes to the Client over GPIF thread 2 (BULK.c). */
#define LINK_CONTROL_RESET_CLIENT        (0x0B)         /* PC -> Host only: reset the Client board (no payload). */

/* Debug commands (0x70 and up), only accepted when CY_FX_DEBUG_COMMANDS is 1 (main.h); otherwise ERROR UnknownType. */
#define LINK_CONTROL_DEBUG_RESET_HOST    (0x70)         /* PC -> Host: answer with a STATUS, then do a cold reset of the Host (it falls back into the USB bootloader while the download jumper is set). */
#define LINK_CONTROL_DEBUG_SINK_DATA     (0x72)         /* PC -> Host: speed test, the Host counts the message and drops it. */
#define LINK_CONTROL_DEBUG_SOURCE        (0x73)         /* PC -> Host: u32 count, u32 size - the Host sends that many messages of that size (same type, Tag = index). */
#define LINK_CONTROL_DEBUG_SINK_END      (0x74)         /* PC -> Host: answered with the same type: u32 messages, u32 bytes counted since the last end. */
#define LINK_CONTROL_DEBUG_CRASH         (0x76)         /* PC -> Host: u32 what - 1 hang the PC link thread in an endless loop (the watchdog has to reset the Host), 2 run the fatal error handler. */
#define LINK_CONTROL_DEBUG_FAULT         (0x75)         /* PC -> Host: u32 mode, u32 period - from now on every period-th buffer the Host sends to the Client is damaged (1 bit flip, 2 zeroed word, 3 blank buffer, 4 deleted word); period 0 = off. Tests the repair of the GPIF link. */

/* ERROR codes. */
#define LINK_ERROR_UNKNOWN_CHANNEL       (1)
#define LINK_ERROR_UNKNOWN_TYPE          (2)
#define LINK_ERROR_BAD_LENGTH            (3)
#define LINK_ERROR_MEDIA_ID_MISMATCH     (4)
#define LINK_ERROR_SEQUENCE_GAP          (5)
#define LINK_ERROR_NO_HELLO              (6)
#define LINK_ERROR_BUSY                  (7)
#define LINK_ERROR_CRC_MISMATCH          (8)            /* The check word of a block did not match (the name is from the CRC-32 days). */
#define LINK_ERROR_CLIENT_ABSENT         (9)

/* STATUS: state of the Client. */
#define LINK_CLIENT_ABSENT               (0)
#define LINK_CLIENT_BOOTING              (1)
#define LINK_CLIENT_RUNNING              (2)
#define LINK_CLIENT_LOST                 (3)

#include "../Version/FirmwareVersionTag.h"

#define LINK_PROTOCOL_VERSION            (0x0001)       /* Major in the high byte, minor in the low byte. */
#define LINK_HOST_FIRMWARE_VERSION       (FW_VERSION_PACKED)   /* The build stamp, see Version/FirmwareVersionTag.h. */

/* The PC pings at least every second while idle; this long without any block means the PC is gone. */
#define LINK_PEER_TIMEOUT_MS             (3500)

/* Announced in HELLO_ACK for compatibility with the credit field; the bulk link does not use credits. */
#define LINK_RECEIVE_CAPACITY            (CY_FX_LINK_DMA_BUF_COUNT)

#define LINK_THREAD_STACK                (0x1000)
#define LINK_THREAD_PRIORITY             (11)           /* Below the guard thread (10): flat out under bulk traffic it must not starve the watchdog clear. */

/* Creates the mutexes. Call once, before the link thread runs. */
extern void PcLinkInit (void);

/* Entry function of the link thread (create it with LINK_THREAD_STACK / LINK_THREAD_PRIORITY). */
extern void PcLinkThreadEntry (uint32_t Input);

/* Creates the two DMA channels between the link endpoints and the CPU, resets the link state and lets the link thread run. Call
 * after the endpoints are configured. Returns CyFalse if a channel could not be created. */
extern CyBool_t PcLinkStart (void);

/* Stops the link thread's work and destroys the channels. Safe to call when not started. */
extern void PcLinkStop (void);

/* Stores what the PC is told about the Client (the 28 byte STATUS body; byte 0 is the client state, LINK_CLIENT_*) and the Client
 * firmware version for HELLO_ACK, and sends a STATUS to the PC if a session is open. */
extern void PcLinkSetClientStatus (const uint8_t *StatusBody, uint32_t ClientFirmwareVersion);

/* Whether the PC has an open session (HELLO seen, no BYE, no timeout). */
extern CyBool_t PcLinkIsSessionOpen (void);

/* Sends a complete message block that came from the Client to the PC. Waits a short while for a free transmit buffer; returns
 * CyFalse if there is no PC session (the block is dropped) or the PC does not read. */
extern CyBool_t PcLinkForwardToPc (const uint8_t *Block, uint32_t Length);

/* Provided by CLIENT.c: the PC asked for the Client to be reset. Returns 0 if that was started, or the ERROR
 * code to answer with (LINK_ERROR_BUSY while a recovery is already running). */
extern uint8_t HostRequestClientReset (void);

/* Provided by CLIENT.c: hands a complete message block from the PC to the Client. Returns 0, or the ERROR code to answer with
 * (LINK_ERROR_CLIENT_ABSENT, LINK_ERROR_BUSY). */
extern uint8_t HostForwardToClient (const uint8_t *Block, uint32_t Length);

#include "cyu3externcend.h"

/* A message to the PC from other Host code (any thread; waits briefly for a free transmit buffer). */
extern void PcLinkReply (uint8_t Channel, uint8_t Type, uint16_t Tag, const uint8_t *Payload, uint32_t Length);

#endif /* _INCLUDED_LINK_H_ */

/*[]*/
