
#ifndef SCSI_H_
#define SCSI_H_


// SCSI command opcodes
#define SCSI_INQUIRY        0x12
#define SCSI_TEST_UNIT_READY 0x00
#define SCSI_REQUEST_SENSE  0x03
#define SCSI_READ_CAPACITY  0x25

#define CBW_SIGNATURE 0x43425355
#define CSW_SIGNATURE 0x53425355

#pragma pack(push, 1)

typedef struct CBW {
    uint32_t dCBWSignature;
    uint32_t dCBWTag;
    uint32_t dCBWDataTransferLength;
    uint8_t  bmCBWFlags;
    uint8_t  bCBWLUN;
    uint8_t  bCBWCBLength;
    uint8_t  CBWCB[16];
} CBW;

typedef struct CSW {
    uint32_t dCSWSignature;
    uint32_t dCSWTag;
    uint32_t dCSWDataResidue;
    uint8_t  bCSWStatus;
} CSW;

#pragma pack(pop)

#endif /* SCSI_H_ */
