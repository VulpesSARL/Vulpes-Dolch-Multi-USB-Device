#ifndef _INCLUDED_HDDMEDIA_H_
#define _INCLUDED_HDDMEDIA_H_

#include "cyu3types.h"

/* 1 = the HDD function is built in. 0 builds a smaller "bridge" firmware without it (stubs instead): a Client whose update code can only hold 5 chunks of 16 KB
 * is updated with it first. */
#ifndef CLIENT_WITH_HDD
#define CLIENT_WITH_HDD (1)
#endif
#include "cyu3externcstart.h"

/* The media model and the sector engine of the Client's HDD. The PC owns the disk image (a VHDX or any
 * other format it can open); the Client only knows what DISK_ATTACH told it (size, read-only flag) and asks the PC for every sector the OS reads
 * or writes. Messages of channel 2 arrive from the GPIF link thread (HddMediaOnMessage, must not block for long); the SCSI side (STORAGE.c) runs
 * in the HDD thread and uses the functions below.
 *
 * Reliability rules (the PC holds the user's files): a WRITE is acknowledged to the OS only after the PC said COMPLETE for it; the OS is never
 * told that data is safe that the PC did not confirm. A write is never repeated or resumed after the PC went away - it fails and the OS decides
 * (Windows retries). Reads wait for a missing PC for up to 60 s and are asked for again when it comes back. */

/* Channel 2 message types. */
#define HDM_TYPE_DISK_ATTACH         (0x10)     /* PC -> Client */
#define HDM_TYPE_DISK_DETACH         (0x11)     /* PC -> Client */
#define HDM_TYPE_DATA                (0x12)     /* PC -> Client: sectors of a read */
#define HDM_TYPE_COMPLETE            (0x13)     /* PC -> Client: end of a read / write / flush / unmap, with a status */
#define HDM_TYPE_SET_NAME            (0x15)     /* PC -> Client: UTF-8 name of the image (up to HDM_NAME_BYTES), kept in RAM only so that the PC can ask for it after a restart */
#define HDM_TYPE_NAME_REQUEST        (0x16)     /* PC -> Client: no payload, answered with NAME */
#define HDM_NAME_BYTES               (320)      /* opaque UTF-8 bytes with a length, never used as a C string; 320 holds an ASCII MAX_PATH path or about 100 CJK characters */
#define HDM_TYPE_DISK_STATE          (0x20)     /* Client -> PC */
#define HDM_TYPE_READ_REQUEST        (0x21)     /* Client -> PC: u64 Lba, u32 Count */
#define HDM_TYPE_CANCEL              (0x22)     /* Client -> PC */
#define HDM_TYPE_WRITE_REQUEST       (0x23)     /* Client -> PC: u64 Lba, u32 Count */
#define HDM_TYPE_WRITE_DATA          (0x24)     /* Client -> PC: the sectors of the write, in pieces of up to 16 KB */
#define HDM_TYPE_FLUSH_REQUEST       (0x25)     /* Client -> PC */
#define HDM_TYPE_UNMAP_REQUEST       (0x26)     /* Client -> PC: u64 Lba, u32 Count, u8 Mode (0 = release the range, 1 = write zeros) */
#define HDM_TYPE_NAME                (0x27)     /* Client -> PC: the name given with SET_NAME, empty when there is none */

/* DISK_STATE event codes. */
#define HDM_EVENT_NONE               (0)
#define HDM_EVENT_EJECTED_BY_OS      (1)
#define HDM_EVENT_EJECT_REFUSED      (2)
#define HDM_EVENT_LOCK_CHANGED       (3)
#define HDM_EVENT_BUS_RESET          (4)

/* DISK_ATTACH flags. */
#define HDM_FLAG_READ_ONLY           (0x01)
#define HDM_FLAG_REATTACH            (0x04)

/* COMPLETE status codes. */
#define HDM_STATUS_OK                (0)
#define HDM_STATUS_NO_MEDIUM         (1)
#define HDM_STATUS_OUT_OF_RANGE      (2)
#define HDM_STATUS_READ_ERROR        (3)
#define HDM_STATUS_CANCELLED         (5)
#define HDM_STATUS_FAILED            (6)
#define HDM_STATUS_WRITE_ERROR       (7)
#define HDM_STATUS_NONE              (0xFF)     /* No COMPLETE has arrived (yet). */

#define HDM_SECTOR_BYTES             (512)

/* Mode of an UNMAP request. */
#define HDM_UNMAP_RELEASE            (0)
#define HDM_UNMAP_ZERO               (1)

/* What the media change flags tell the SCSI side (a UNIT ATTENTION is due). */
#define HDM_CHANGE_INSERTED          (0x01)
#define HDM_CHANGE_REMOVED           (0x02)

/* Bits returned by HddMediaWait. */
#define HDM_WAIT_DATA                (0x01)
#define HDM_WAIT_COMPLETE            (0x02)
#define HDM_WAIT_ABORT               (0x04)

/* The ring that carries read DATA from the link thread to the USB side, which doubles as the staging buffer of a write (the two never run
 * together - the HDD thread does one command at a time). A multiple of 1024; at most 65535 because CyU3PDmaBufferAlloc takes a 16 bit size. */
#define HDM_RING_BYTES               (32768)

/* A write reaches the PC in pieces of this many bytes: a multiple of 1024 (the largest USB packet, so a piece never ends inside a packet) and
 * of 512 (the PC takes whole sectors); two pieces plus their message framing fit into the ring (see HDM_WRITE_SLOT_STRIDE). */
#define HDM_WRITE_PIECE_BYTES        (15360)

/* Offset of the sector data inside the buffer returned by HddMediaWriteBuffer: the message header (8 bytes) sits right in front of it, and the
 * data stays aligned to the cache line size for the USB DMA. */
#define HDM_WRITE_DATA_OFFSET        (32)

/* The ring holds two write buffers, so USB can fill one while the other goes to the PC: the data of slot 1 starts this many bytes after the
 * data of slot 0 (slot 0: 8 byte header, 15360 bytes, 4 byte check word; slot 1 follows, its data again aligned to 32 bytes). */
#define HDM_WRITE_SLOT_STRIDE        (15392)

/* The OS gets an error after this long without an answer from the PC (the project requirement). */
#define HDM_TIMEOUT_MS               (60000)

extern void HddMediaInit (void);

/* For the update mode: hands the ring over to somebody else for good. The HDD function stops working (it is ignored from now on); returns the buffer
 * and its size. */
extern uint8_t *HddMediaTakeBuffer (uint32_t *Bytes);

/* A verified message block of channel 2 from the PC (link thread). */
extern void HddMediaOnMessage (const uint8_t *Block, uint32_t Length);

/* The PC session came up (1) or went away (0), as told by the Host. */
extern void HddMediaOnPcLink (uint8_t Up);
extern CyBool_t HddMediaPcIsUp (void);

/* ---- State -------------------------------------------------------------------------------------------------------------- */

extern CyBool_t HddMediaIsPresent (void);
extern CyBool_t HddMediaIsLocked (void);
extern CyBool_t HddMediaIsReadOnly (void);
extern void HddMediaGetSectorCount (uint32_t *Low, uint32_t *High);     /* number of 512 byte sectors */
extern void HddMediaGetId (uint32_t *Low, uint32_t *High);

/* Returns and clears the change flags (HDM_CHANGE_*): the SCSI side raises a UNIT ATTENTION for them. */
extern uint32_t HddMediaTakeChanges (void);

/* A 4 KB buffer for the SCSI side to build responses in (aligned, DMA capable). */
extern uint8_t *HddMediaScratch (void);

extern void HddMediaSetLock (CyBool_t Prevent);

/* START STOP UNIT with LoEj: ejects unless locked. Returns CyFalse when it is locked (the PC is told that the eject was refused). */
extern CyBool_t HddMediaEject (void);

extern void HddMediaBusReset (void);

/* ---- Requests to the PC (HDD thread, one at a time) --------------------------------------------------------------------- */

/* Sector reads: Begin sends READ_REQUEST; Peek/Consume hand out the received bytes in order; Wait blocks for news. */
extern CyBool_t HddMediaReadBegin (uint32_t LbaLow, uint32_t LbaHigh, uint32_t Count, uint32_t TotalBytes);
extern void HddMediaReadBulkHint (uint32_t Buffers);       /* before ReadBegin: the first Buffers * 16 KB of this read come over the bulk data lane */
extern uint32_t HddMediaReadPeek (uint8_t **Pointer, uint32_t Max);
extern void HddMediaReadConsume (uint32_t Bytes);
extern uint32_t HddMediaWait (uint32_t TimeoutMs);
extern CyBool_t HddMediaIsComplete (void);
extern uint8_t HddMediaStatus (void);
extern uint32_t HddMediaReadReceived (void);
extern void HddMediaReadEnd (CyBool_t SendCancel);

/* Writes: Begin sends WRITE_REQUEST (returns CyFalse if the PC is not there); WriteBuffer is where the next piece of up to HDM_WRITE_PIECE_BYTES
 * must be put (at offset HDM_WRITE_DATA_OFFSET); WriteSend ships Bytes of it to the PC (a multiple of 4); then WaitComplete returns the status of
 * the PC's COMPLETE once the PC has written everything. */
extern CyBool_t HddMediaWriteBegin (uint32_t LbaLow, uint32_t LbaHigh, uint32_t Count);
extern uint8_t *HddMediaWriteBuffer (void);
extern CyBool_t HddMediaWriteSend (uint32_t Bytes);

/* The same for the two buffers (Slot 0 or 1; any other value counts modulo 2). */
extern uint8_t *HddMediaWriteBufferAt (uint32_t Slot);
extern CyBool_t HddMediaWriteSendAt (uint32_t Slot, uint32_t Bytes);

/* FLUSH (SYNCHRONIZE CACHE) and UNMAP / WRITE SAME / FORMAT: a request, then WaitComplete. */
extern CyBool_t HddMediaFlushBegin (void);
extern CyBool_t HddMediaUnmapBegin (uint32_t LbaLow, uint32_t LbaHigh, uint32_t Count, uint8_t Mode);

/* Waits for the COMPLETE of the request in progress (write, flush, unmap); returns its status, HDM_STATUS_NONE on timeout or when the PC went away
 * or the medium was removed. Ends the request. */
extern uint8_t HddMediaWaitComplete (uint32_t TimeoutMs);

/* Gives up a request in progress (after an error on the USB side). */
extern void HddMediaRequestEnd (void);

#include "cyu3externcend.h"

#endif /* _INCLUDED_HDDMEDIA_H_ */
