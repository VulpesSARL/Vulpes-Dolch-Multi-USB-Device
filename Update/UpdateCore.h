#ifndef UPDATE_CORE_H
#define UPDATE_CORE_H

#include "cyu3types.h"

/* Firmware update: the container format, its signature check and the programming of the on-board I2C EEPROM (M24M02, 256 KiB). Shared by
 * Host and Client (RTOS API). The container is built by the PC tool (Vulpes-Dolch-Host-Common\Update):
 *
 *   0   u32  magic "VDUP" (0x50554456)
 *   4   u16  format version (1)
 *   6   u16  target: 1 Host, 2 Client
 *   8   u32  image version (the product's own number)
 *   12  u32  flags: bit 0 = payload is LZSS compressed
 *   16  u32  payload length (bytes after this header)
 *   20  u32  image length (bytes written to the EEPROM from address 0: the .img the boot ROM loads)
 *   24  u32  CRC-32 of the image
 *   28  u32  reserved (0)
 *   32  u8[32]  HMAC-SHA256 over bytes 0..31 followed by the payload, with the shared key (UpdateKey.h, not published)
 *   64  payload
 *
 * Nothing is written before the signature, the CRC of the decompressed image is checked against the header, and the target matches. The EEPROM
 * is written so that an interruption leaves an image the boot ROM rejects (page 0 first blanked, written again last), and only pages that
 * differ are written at all (the part is good for 4 million write cycles per byte, the fewer the better). */

#define UPDATE_MAGIC                (0x50554456u)
#define UPDATE_FORMAT_VERSION       (1)
#define UPDATE_HEADER_BYTES         (64)
#define UPDATE_TARGET_HOST          (1)
#define UPDATE_TARGET_CLIENT        (2)
#define UPDATE_FLAG_LZSS            (0x00000001u)

#define UPDATE_EEPROM_BYTES         (262144u)
#define UPDATE_PAGE_BYTES           (256u)
#define UPDATE_EEPROM_BASE_ADDRESS  (0xA0)

#define UPDATE_CHUNK_BYTES          (8192u)     /* The file is kept in chunks of this size (a DMA buffer allocation is limited to 64 KB). */
#define UPDATE_MAX_CHUNKS           (32)

/* Result codes (0 = fine); the PC sees them in the STATE message. */
#define UPDATE_OK                   (0)
#define UPDATE_ERR_STATE            (1)         /* The command does not fit the current state. */
#define UPDATE_ERR_LENGTH           (2)         /* File too large or too small. */
#define UPDATE_ERR_MEMORY           (3)         /* No memory for the file. */
#define UPDATE_ERR_OFFSET           (4)         /* Data arrived out of order. */
#define UPDATE_ERR_FORMAT           (5)         /* Wrong magic, version, lengths or flags. */
#define UPDATE_ERR_TARGET           (6)         /* The file is for the other device. */
#define UPDATE_ERR_SIGNATURE        (7)         /* The signature does not match. */
#define UPDATE_ERR_DECOMPRESS       (8)
#define UPDATE_ERR_IMAGE            (9)         /* The decompressed image is not an FX3 boot image or its CRC differs from the header. */
#define UPDATE_ERR_EEPROM           (10)        /* The EEPROM did not answer. */
#define UPDATE_ERR_VERIFY           (11)        /* What was read back differs from what was written. */

typedef struct
{
    uint32_t Magic;
    uint16_t FormatVersion;
    uint16_t Target;
    uint32_t ImageVersion;
    uint32_t Flags;
    uint32_t PayloadLength;
    uint32_t ImageLength;
    uint32_t ImageCrc;
    uint8_t  Mac[32];
} UpdateHeader_t;

/* The whole file as it was received: UPDATE_CHUNK_BYTES per chunk, the container header at file offset 0. */
typedef struct
{
    uint8_t  *Chunk[UPDATE_MAX_CHUNKS];
    uint32_t  FileLength;
} UpdateFile_t;

typedef void (*UpdateProgressCallback_t) (uint32_t Percent);

/* SHA-256 and HMAC-SHA256 known-answer test; returns CyTrue if both pass (run once before trusting a signature check). */
extern CyBool_t UpdateSelfTest (void);

/* Checks the header and signature of a received file for the given target; fills Header. Returns an UPDATE_ERR_* code or UPDATE_OK. */
extern uint32_t UpdateVerifyFile (const UpdateFile_t *File, uint16_t Target, UpdateHeader_t *Header);

/* The EEPROM can only be written while this is enabled; the update modes of Host and Client switch it on when they are entered, nothing else does.
 * (Any write attempt without it fails, whatever message arrives.) */
extern void UpdateWriteEnable (CyBool_t Enable);

/* How many EEPROM pages were actually written since the start (pages that already held the right content are not written). */
extern uint32_t UpdatePagesWritten (void);

/* Starts the I2C master for the EEPROM (call once, after the IO matrix was set up with useI2C). */
extern CyBool_t UpdateEepromInit (void);

/* Shuts the I2C master down again, so the pins are released and the boot ROM finds an idle bus after the next reset. */
extern void UpdateEepromDeinit (void);

/* Decompresses the (already verified) file and programs the EEPROM, then reads everything back and compares its CRC with the header. Takes
 * seconds; Progress is called now and then with 0..100. Returns an UPDATE_ERR_* code or UPDATE_OK. */
extern uint32_t UpdateProgramEeprom (const UpdateFile_t *File, const UpdateHeader_t *Header, UpdateProgressCallback_t Progress);

/* Streaming variant for a device without room for the whole file (the Host): the PC sends the file twice. Pass 1 (verify) checks the signature and the
 * CRC of the decompressed image and writes nothing; pass 2 (program) must carry the same file: it writes the pages while they arrive, keeps the
 * EEPROM image invalid (page 0 blank) until its own signature and CRC check out, then writes page 0 and reads everything back. Feed the file in
 * order, any piece sizes. The calls return UPDATE_OK or an UPDATE_ERR_* code. */
#define UPDATE_STREAM_VERIFY    (1)
#define UPDATE_STREAM_PROGRAM   (2)
extern uint32_t UpdateStreamBegin (uint32_t FileLength, uint32_t Pass);
extern uint32_t UpdateStreamFeed (const uint8_t *Data, uint32_t Count);
extern uint32_t UpdateStreamEnd (void);
extern void UpdateStreamAbort (void);

#endif
