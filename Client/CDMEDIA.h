#ifndef _INCLUDED_CDMEDIA_H_
#define _INCLUDED_CDMEDIA_H_

#include "cyu3types.h"
#include "cyu3externcstart.h"

/* The media model and the sector read engine of the Client's CD-ROM drive. The PC owns the
 * disc image; the Client only knows what MEDIA_INSERT told it (state, profile, size, ready-made TOC blobs) and asks the PC for
 * every sector the OS reads. Messages of channel 1 arrive from the GPIF link thread (CdMediaOnMessage, must not block for long);
 * the SCSI side (CDROM.c) runs in the CD-ROM thread and uses the functions below. */

/* Channel 1 message types. */
#define CDM_TYPE_MEDIA_INSERT        (0x10)
#define CDM_TYPE_MEDIA_REMOVE        (0x11)
#define CDM_TYPE_DATA                (0x12)
#define CDM_TYPE_COMPLETE            (0x13)
#define CDM_TYPE_EJECT_REQUEST       (0x14)     /* PC -> Client: ask the OS to eject the disc (media event EjectRequest), no payload, no answer */
#define CDM_TYPE_SET_NAME            (0x15)     /* PC -> Client: UTF-8 name of the image (up to CDM_NAME_BYTES), kept in RAM only so that the PC can ask for it after a restart */
#define CDM_TYPE_NAME_REQUEST        (0x16)     /* PC -> Client: no payload, answered with NAME */
#define CDM_NAME_BYTES               (320)      /* opaque UTF-8 bytes with a length, never used as a C string; 320 holds an ASCII MAX_PATH path or about 100 CJK characters */
#define CDM_TYPE_MEDIA_STATE         (0x20)
#define CDM_TYPE_READ_REQUEST        (0x21)
#define CDM_TYPE_CANCEL              (0x22)
#define CDM_TYPE_NAME                (0x23)     /* Client -> PC: the name given with SET_NAME, empty when there is none */

/* MEDIA_STATE event codes. */
#define CDM_EVENT_NONE               (0)
#define CDM_EVENT_EJECTED_BY_OS      (1)
#define CDM_EVENT_EJECT_REFUSED      (2)
#define CDM_EVENT_LOCK_CHANGED       (3)
#define CDM_EVENT_BUS_RESET          (4)

/* COMPLETE status codes. */
#define CDM_STATUS_OK                (0)
#define CDM_STATUS_NO_MEDIUM         (1)
#define CDM_STATUS_OUT_OF_RANGE      (2)
#define CDM_STATUS_READ_ERROR        (3)
#define CDM_STATUS_BAD_FORMAT        (4)
#define CDM_STATUS_CANCELLED         (5)
#define CDM_STATUS_FAILED            (6)
#define CDM_STATUS_NONE              (0xFF)     /* No COMPLETE has arrived (yet). */

/* Blob kinds of MEDIA_INSERT. */
#define CDM_BLOB_TOC_LBA             (0x01)
#define CDM_BLOB_TOC_MSF             (0x02)
#define CDM_BLOB_SESSION_LBA         (0x03)
#define CDM_BLOB_SESSION_MSF         (0x04)
#define CDM_BLOB_FULL_TOC            (0x05)
#define CDM_BLOB_DISC_INFO           (0x10)

/* Sector formats of READ_REQUEST. */
#define CDM_FORMAT_USER_DATA         (0)        /* 2048 bytes per sector */
#define CDM_FORMAT_RAW               (1)        /* 2352 bytes per sector */

/* What the media change flags tell the SCSI side (a UNIT ATTENTION is due). */
#define CDM_CHANGE_INSERTED          (0x01)
#define CDM_CHANGE_REMOVED           (0x02)

/* GET EVENT STATUS NOTIFICATION media event codes. */
#define CDM_MEDIA_EVENT_NONE         (0)
#define CDM_MEDIA_EVENT_EJECT_REQUEST (1)
#define CDM_MEDIA_EVENT_NEW_MEDIA    (2)
#define CDM_MEDIA_EVENT_REMOVAL      (3)

/* Bits returned by CdMediaReadWait. */
#define CDM_READ_DATA                (0x01)
#define CDM_READ_COMPLETE            (0x02)
#define CDM_READ_ABORT               (0x04)

/* Size of the byte ring that carries DATA from the link thread to the USB side; a multiple of 1024 (the largest USB packet). At most 65535:
 * CyU3PDmaBufferAlloc takes a 16 bit size, so 65536 would silently become 0 and the ring would overlap the buffers allocated after it. */
#ifndef CDM_RING_BYTES
#define CDM_RING_BYTES               (64512)
#endif

/* The OS gets a sector error after this long without an answer from the PC (the project requirement). */
#define CDM_READ_TIMEOUT_MS          (60000)

/* Allocates the ring and the scratch buffer, creates the locks. Call once before the link thread starts. */
extern void CdMediaInit (void);

/* For the update mode: hands the sector ring over to somebody else for good. The CD-ROM function stops working (it is ignored from now on); returns
 * the buffer and its size. */
extern uint8_t *CdMediaTakeBuffer (uint32_t *Bytes);

/* A verified message block of channel 1 from the PC (link thread). */
extern void CdMediaOnMessage (const uint8_t *Block, uint32_t Length);

/* The PC session came up (1) or went away (0), as told by the Host. */
extern void CdMediaOnPcLink (uint8_t Up);

/* ---- State, for the SCSI commands and the status announcement ----------------------------------------------------------- */

extern CyBool_t CdMediaIsPresent (void);
extern CyBool_t CdMediaIsLocked (void);
extern uint32_t CdMediaLeadOutLba (void);
extern uint16_t CdMediaProfile (void);
extern uint8_t CdMediaDiscType (void);

/* The MediaId, as two little endian words (low, high). */
extern void CdMediaGetId (uint32_t *Low, uint32_t *High);

/* A blob by kind; returns NULL if the PC did not send it. The pointer stays valid while the medium stays. */
extern const uint8_t *CdMediaGetBlob (uint8_t Kind, uint32_t *Length);

/* Returns and clears the change flags (CDM_CHANGE_*): the SCSI side raises a UNIT ATTENTION for them. */
extern uint32_t CdMediaTakeChanges (void);

/* Returns and clears the pending media event for GET EVENT STATUS NOTIFICATION (CDM_MEDIA_EVENT_*). */
extern uint8_t CdMediaTakeMediaEvent (void);

/* A 4 KB buffer for the SCSI side to build responses in (aligned, DMA capable). */
extern uint8_t *CdMediaScratch (void);

/* PREVENT / ALLOW MEDIUM REMOVAL from the OS: remembers the lock and tells the PC. */
extern void CdMediaSetLock (CyBool_t Prevent);

/* START STOP UNIT with LoEj from the OS: ejects the medium unless it is locked. Returns CyFalse when it is locked (the PC is told that
 * the eject was refused); CyTrue when the medium is gone (the PC is told it was ejected). */
extern CyBool_t CdMediaEject (void);

/* The USB bus was reset by the OS: tells the PC. */
extern void CdMediaBusReset (void);

/* ---- Sector reads ------------------------------------------------------------------------------------------------------- */

/* Starts a read: sends READ_REQUEST for Count sectors from Lba in the given format; TotalBytes is what the OS wants (Count times
 * the sector size). Only one read at a time. Returns CyFalse if there is no medium or the request could not be queued. */
extern CyBool_t CdMediaReadBegin (uint32_t Lba, uint32_t Count, uint8_t Format, uint32_t TotalBytes);

/* The next run of received bytes that can be sent as one piece: sets *Pointer and returns how many bytes (at most Max), 0 if none. */
extern uint32_t CdMediaReadPeek (uint8_t **Pointer, uint32_t Max);

/* The bytes returned by CdMediaReadPeek have been taken; gives their space back to the link thread. */
extern void CdMediaReadConsume (uint32_t Bytes);

/* Waits up to TimeoutMs for new data, the COMPLETE message or an abort; returns CDM_READ_* bits (0 on timeout). */
extern uint32_t CdMediaReadWait (uint32_t TimeoutMs);

/* Whether the COMPLETE message has arrived, and with which status (CDM_STATUS_*; CDM_STATUS_NONE before). */
extern CyBool_t CdMediaReadIsComplete (void);
extern uint8_t CdMediaReadStatus (void);

/* Bytes received from the PC so far for the current read. */
extern uint32_t CdMediaReadReceived (void);

/* Ends the read; with SendCancel the PC is told to stop (after a timeout or an error on the USB side). */
extern void CdMediaReadEnd (CyBool_t SendCancel);

#include "cyu3externcend.h"

#endif /* _INCLUDED_CDMEDIA_H_ */
