#include "cyu3system.h"
#include "cyu3os.h"
#include "cyu3dma.h"
#include "cyu3error.h"
#include "cyu3usb.h"
#include "cyu3uart.h"
#include "cyu3gpio.h"
#include "cyu3utils.h"
#include "scsi.h"
#include "main.h"
#include "main_def.h"
#include "CDMEDIA.h"
#include "GpifLinkCore.h"

/* Request Sense Lookup Table */
static const uint8_t glReqSenseCode_CDROM[][3] =
{
    /* SK,  ASC,  ASCQ */
    {0x00, 0x00, 0x00},    /* senseOk                     0    */
    {0x0b, 0x08, 0x03},    /* senseCRCError               1    */
    {0x05, 0x24, 0x00},    /* senseInvalidFieldInCDB      2    */
    {0x02, 0x3a, 0x00},    /* senseNoMedia                3    */
    {0x03, 0x03, 0x00},    /* senseWriteFault             4    */
    {0x03, 0x11, 0x00},    /* senseReadError              5    */
    {0x03, 0x12, 0x00},    /* senseAddrNotFound           6    */
    {0x05, 0x20, 0x00},    /* senseInvalidOpcode          7    */
    {0x05, 0x21, 0x00},    /* senseInvalidLBA             8    */
    {0x05, 0x26, 0x00},    /* senseInvalidParameter       9    */
    {0x05, 0x53, 0x02},    /* senseCantEject              0xa  */
    {0x06, 0x28, 0x00},    /* senseMediaChanged           0xb  */
    {0x06, 0x29, 0x00},    /* senseDeviceReset            0xc  */
    {0x07, 0x27, 0x00},    /* senseWriteProtected         0xd  */
    {0x02, 0x04, 0x02},    /* senseInitRequired           0xe  */
    {0x05, 0x39, 0x00},    /* senseSavePageNotSupported   0xf  */
    {0x05, 0x64, 0x00},    /* senseIllegalModeForTrack    0x10 */
    {0x04, 0x44, 0x00}     /* senseTargetFailure          0x11 */
};

#define CDROM_SENSE_ILLEGAL_MODE_FOR_TRACK   (0x10)
#define CDROM_SENSE_TARGET_FAILURE           (0x11)

// Example static INQUIRY response for a CD-ROM
uint8_t CyFxMscScsiInquiryData_CDROM[] = {
        0x05,       /* PQ and PDT */
        0x80,       /* Removable device. */
        0x00,       /* Version */
        0x32,       /* Response data format */
        0x5A,       /* Addnl Length (total length minus 4 bytes) */
        0x00,
        0x00,
        0x00,

        'V',        /* Vendor Id */
        'u',
        'l',
        'p',
        'e',
        's',
        0x00,
        0x00,
        'D',        /* Product Id */
        'o',
        'l',
        'c',
        'h',
        ' ',
        'C',
        'D',
        '-',
        'R',
        'O',
        'M',
        0x00,
        0x00,

        '0',        /* Revision */
        '0',
        '0',
        '1',

        0, 0, 0, 0,
        0, 0, 0, 0,
        0, 0, 0, 0,
        0, 0, 0, 0,
        0, 0, 0, 0, /* 20 bytes of vendor specific info: not used. */
        0, 0,       /* Reserved fields for SCSI over USB. */

        0x00, 0x80, /* SAM-4 spec compliant. */
        0x17, 0x30, /* BOT spec compliant. */
        0x04, 0x60, /* SPC-4 spec compliant. */
        0x04, 0xC0, /* SBC-3 spec compliant. */
        0x00, 0x00, /* No more specs. */
        0x00, 0x00, /* No more specs. */
        0x00, 0x00, /* No more specs. */
        0x00, 0x00, /* No more specs. */
        0x00, 0x00, /* Reserved. */

        0, 0, 0, 0, /* Reserved. */
        0, 0, 0, 0, /* Reserved. */
        0, 0, 0, 0, /* Reserved. */
        0, 0, 0, 0, /* Reserved. */
        0, 0, 0, 0  /* Reserved. */
};

static const uint8_t CyFxMscSupportedPageList_CDROM[] __attribute__((aligned(32))) = {
    0x00,                                       /* PDT = 0. */
    CY_FX_SCSI_INQCMD_PCODE_LIST,               /* Page code = 0. */
    0x00,                                       /* Reserved. */
    0x04,                                       /* Max Index - 3. */
    CY_FX_SCSI_INQCMD_PCODE_LIST,
    CY_FX_SCSI_INQCMD_PCODE_SERNUM,
    CY_FX_SCSI_INQCMD_PCODE_DEVIDENT,
    CY_FX_SCSI_INQCMD_PCODE_BLKLIMITS,
};

static const uint8_t CyFxMscScsiSerialNumber_CDROM[] __attribute__((aligned(32))) = {
    0x00,                                       /* PDT = 0. */
    CY_FX_SCSI_INQCMD_PCODE_SERNUM,             /* Page code = 0x80. */
    0x00,                                       /* Reserved. */
    0x0C,                                       /* Max Index (15) - 3. */
    0x30, 0x31, 0x32, 0x33,                     /* "0123" */
    0x34, 0x35, 0x36, 0x37,                     /* "4567" */
    0x38, 0x39, 0x30, 0x31                      /* "8901" */
};

static uint8_t CyFxMscScsiDevIdent_CDROM[] __attribute__((aligned(32))) = {
    0x00,                                       /* PDT = 0. */
    CY_FX_SCSI_INQCMD_PCODE_DEVIDENT,           /* Page code = 0x83. */
    0x00,                                       /* MSB (PAGE LENGTH) = 0x00. */
    0x0C,                                       /* LSB (PAGE LENGTH) = 15 - 3 = 12. */

    0x01, /* Protocol ID & Code Set */
    0x02, /* PIV & Association & Identifier Type */
    0x00, /* Reserved */
    0x08, /* ID Length (n-3) */
    0x00, 0xA0, 0x50, 0x46, 0x58, 0x33, 0x53, 0x20 /* Text: PFX3S */
};

static const uint8_t CyFxMscScsiBlkLimits_CDROM[] __attribute__((aligned(32))) = {
    0x00,                                       /* PDT = 0. */
    CY_FX_SCSI_INQCMD_PCODE_BLKLIMITS,          /* Page code = 0xB0. */
    0x00,                                       /* Reserved. */
    0x3C,                                       /* Max Index (63) - 3. */
    0x00, 0x00, 0x08, 0x00,                     /* Optimal size granularity = 1 MB. */
    0x00, 0x00, 0x40, 0x00,                     /* Maximum size = 8 MB. */
    0x00, 0x00, 0x40, 0x00,                     /* Optimum size = 8 MB. */
    0x00, 0x00, 0x40, 0x00,                     /* Max. Prefetch size = 8 MB. */
    0x00, 0x00, 0x00, 0x00,                     /* No unmap support. */
    0x00, 0x00, 0x00, 0x00,                     /* No unmap support. */
    0x00, 0x00, 0x00, 0x00,                     /* No unmap support. */
    0x00, 0x00, 0x00, 0x00,                     /* No unmap support. */
    0x00, 0x00, 0x00, 0x00,                     /* Reserved. */
    0x00, 0x00, 0x00, 0x00,                     /* Reserved. */
    0x00, 0x00, 0x00, 0x00,                     /* Reserved. */
    0x00, 0x00, 0x00, 0x00,                     /* Reserved. */
    0x00, 0x00, 0x00, 0x00,                     /* Reserved. */
    0x00, 0x00, 0x00, 0x00,                     /* Reserved. */
    0x00, 0x00, 0x00, 0x00                      /* Reserved. */
};

static uint8_t CyFxMscScsiGetConfig_CDROM[] __attribute__((aligned(32))) = {
    0x00, 0x00, 0x00, 0x18,                    /* Length (BE) */
    0x00, 0x00,                                /* Reserved */
    0x00, 0x00,                                /* Current profile (0) */

    0x00, 0x40,    0x00, 0x00,                 /* BD ROM */
    0x00, 0x10,    0x00, 0x00,                 /* DVD ROM */
    0x00, 0x08,    0x00, 0x00,                 /* CD ROM */
    0x00, 0x02,    0x00, 0x00,                 /* Removable disk */

    0x00, 0x01,                                /* Feature: core */
    0x0b,                                      /* Version */
    0x00,                                      /* Additional length */
};


static CyBool_t glLunState_CDROM;                   /* Whether the LUN has valid storage. */
static uint16_t glLunBlkSize_CDROM;                 /* Sector size for each of the LUNs. */
static uint32_t glLunNumBlks_CDROM;                 /* Capacity of the LUN in sectors. */
static CyBool_t glLunStopped_CDROM;                 /* Whether each LUN has been stopped by the user. */
static uint8_t  glSensePtr_CDROM = CY_FX_MSC_SENSE_DEVICE_RESET;

CyBool_t         glDevConfigured_CDROM = CyFalse;              /* Whether the device has been configured. */
CyBool_t         glInPhaseError_CDROM = CyFalse;              /* Whether a phase error condition has been detected. */
static CyFxMscFuncState glMscState_CDROM = CY_FX_MSC_STATE_INACTIVE;

static CyU3PDmaChannel     glChHandleMscOut_CDROM;                    /* DMA channel for OUT endpoint. */
static CyU3PDmaChannel     glChHandleMscIn_CDROM;                     /* DMA channel for IN endpoint. */

CyU3PEvent  glMscAppEvent_CDROM;                               /* MSC application Event group */

uint8_t* glMscCbwBuffer_CDROM = 0;                                   /* Scratch buffer used for CBW. */
uint8_t* glMscCswBuffer_CDROM = 0;                                   /* Scratch buffer used for CSW. */
uint8_t* glMscDataBuffer_CDROM = 0;                                   /* Scratch buffer used for query commands. */

static uint8_t  glCmdDirection_CDROM = 0;                            /* SCSI Command Direction */
static uint8_t  glMscCmdStatus_CDROM = 0;                            /* MSC command status. */
static uint8_t  glMscCmdLun_CDROM = 0;                            /* Current MSC LUN. */
static uint32_t glMscResidue_CDROM = 0;                            /* Residue length for CSW */
static CyBool_t glMscDriverBusy_CDROM = CyFalse;

void CyFxMscApplnResetDatapath_CDROM(void)
{
    //CyU3PDebugPrint(4, "CyFxMscApplnResetDatapath_CDROM()\r\n");

	CyU3PUsbSetEpNak(CY_FX_EP_CD_ROM_OUT, CyTrue);
    CyU3PUsbSetEpNak(CY_FX_EP_CD_ROM_IN, CyTrue);
    CyU3PBusyWait(120);

    CyU3PDmaChannelReset(&glChHandleMscOut_CDROM);
    CyU3PDmaChannelReset(&glChHandleMscIn_CDROM);

    CyU3PUsbFlushEp(CY_FX_EP_CD_ROM_OUT);
    CyU3PUsbStall(CY_FX_EP_CD_ROM_OUT, CyFalse, CyTrue);

    CyU3PUsbFlushEp(CY_FX_EP_CD_ROM_IN);
    CyU3PUsbStall(CY_FX_EP_CD_ROM_IN, CyFalse, CyTrue);

    /* Request Sense Index */
    glSensePtr_CDROM = CY_FX_MSC_SENSE_DEVICE_RESET;

    CyU3PUsbSetEpNak(CY_FX_EP_CD_ROM_OUT, CyFalse);
    CyU3PUsbSetEpNak(CY_FX_EP_CD_ROM_IN, CyFalse);

    glInPhaseError_CDROM = CyFalse;
    glMscDriverBusy_CDROM = CyFalse;

    //CyU3PDebugPrint(4, "CyFxMscApplnResetDatapath_CDROM() -- DONE\r\n");
}

/* Function to initiate sending of data to the USB host. */
CyU3PReturnStatus_t CyFxMscApplnSendDataToHost_CDROM(uint8_t* data, uint32_t length)
{
    CyU3PDmaBuffer_t    dmaBuf;
    CyU3PReturnStatus_t status;

    /* Prepare the DMA Buffer */
    dmaBuf.buffer = data;
    dmaBuf.status = 0;
    dmaBuf.size = (length + 15) & 0xFFF0;      /* Round up to a multiple of 16.  */
    dmaBuf.count = length;

    status = CyU3PDmaChannelSetupSendBuffer(&glChHandleMscIn_CDROM, &dmaBuf);
    return status;
}

/* Function to initiate sending of data to the USB host. */
CyU3PReturnStatus_t CyFxMscApplnReceiveUsbData_CDROM(uint8_t* data, uint32_t length)
{
    CyU3PDmaBuffer_t    dmaBuf;
    CyU3PReturnStatus_t status;

    /* Prepare the DMA Buffer */
    dmaBuf.buffer = data;
    dmaBuf.status = 0;
    dmaBuf.size = (length + 15) & 0xFFF0;      /* Round up to a multiple of 16.  */
    dmaBuf.count = length;

    status = CyU3PDmaChannelSetupRecvBuffer(&glChHandleMscOut_CDROM, &dmaBuf);
    return status;
}


/* Common function to check all CBW parameters against expected values. */
CyBool_t CyFxMscApplnCheckCbwParams(
    uint8_t   lun,                  /* Lun to which the command is addressed. */
    CyBool_t  lunState,             /* Whether the LUN should be active or don't care (for INQUIRY/REQ_SENSE) */
    CyBool_t  isReadExpected,       /* Whether the op-code corresponds to a read (IN) data transfer. */
    CyBool_t  isDataCmd,            /* Whether this is a read or write command. Exact data length match is
                                       expected in data commands. */
    uint32_t  expLength,            /* Default data transfer length for this opcode. */
    uint32_t* actLength_p           /* Return parameter to pass the actual data transfer length. */
)
{
    /* Verify that:
        1. Direction of data transfer matches the expected value.
        2. That the transfer length matches the expected value for read/write commands.
        3. That a non-zero transfer length is provided for commands that want to return data.
        4. That zero transfer length is provided for commands that have no data.
     */
    if ((isReadExpected != (glCmdDirection_CDROM == 0x80)) || ((isDataCmd) && (expLength != glMscResidue_CDROM)) ||
        ((expLength != 0) && (glMscResidue_CDROM == 0)) || ((expLength == 0) && (glMscResidue_CDROM != 0)))
    {
        glMscCmdStatus_CDROM = 1;
        glSensePtr_CDROM = CY_FX_MSC_SENSE_INVALID_PARAMETER;
        CyU3PEventSet(&glMscAppEvent_CDROM, CY_FX_MSC_SIBCB_EVENT_FLAG, CYU3P_EVENT_OR);
        return CyFalse;
    }

    /* Verify that the LUN is ready for commands that report the LUN state or do data transfers. */
    if (lunState)
    {
        if (glSensePtr_CDROM != CY_FX_MSC_SENSE_MEDIA_CHANGED)
        {
            if ((!glLunState_CDROM) || (glLunStopped_CDROM))
            {
                glMscCmdStatus_CDROM = 1;
                glSensePtr_CDROM = CY_FX_MSC_SENSE_NO_MEDIA;
                CyU3PEventSet(&glMscAppEvent_CDROM, CY_FX_MSC_SIBCB_EVENT_FLAG, CYU3P_EVENT_OR);
                return CyFalse;
            }
            else
            {
                glSensePtr_CDROM = CY_FX_MSC_SENSE_OK;
            }
        }
        else
        {
            glMscCmdStatus_CDROM = 1;
            CyU3PEventSet(&glMscAppEvent_CDROM, CY_FX_MSC_SIBCB_EVENT_FLAG, CYU3P_EVENT_OR);
            return CyFalse;
        }
    }

    /* Calculate the size of data to be transferred in the case of non-data commands. */
    if (!isDataCmd)
    {
        if (expLength > glMscResidue_CDROM)
        {
            *actLength_p = glMscResidue_CDROM;
            glMscResidue_CDROM = 0;
        }
        else
        {
            *actLength_p = expLength;
            glMscResidue_CDROM -= expLength;
        }
    }

    return CyTrue;
}

void HandleModeSenseCommand_CDROM(uint8_t lun)
{
    CyU3PReturnStatus_t status;
    uint32_t            dataLength;

    glMscDataBuffer_CDROM[0] = 0x03;
    glMscDataBuffer_CDROM[1] = 0x00;
    glMscDataBuffer_CDROM[2] = 0x00;
    glMscDataBuffer_CDROM[3] = 0x00;

    dataLength = 4;
    if (!CyFxMscApplnCheckCbwParams(lun, CyTrue, CyTrue, CyFalse, dataLength, &dataLength))
        return;

    glMscState_CDROM = CY_FX_MSC_STATE_DATA;
    status = CyFxMscApplnSendDataToHost_CDROM(glMscDataBuffer_CDROM, dataLength);
    if (status != CY_U3P_SUCCESS)
    {
        glMscCmdStatus_CDROM = 1;
        glSensePtr_CDROM = CY_FX_MSC_SENSE_CRC_ERROR;
        CyU3PEventSet(&glMscAppEvent_CDROM, CY_FX_MSC_SIBCB_EVENT_FLAG, CYU3P_EVENT_OR);
    }
}

#include "CDROM_commands.inc"

/* Parse the received CBW and handle the command. */
void CyFxMscApplnParseCbw_CDROM(void)
{
    CyU3PReturnStatus_t status;
    uint8_t cmd = glMscCbwBuffer_CDROM[15];
    uint8_t lun = glMscCbwBuffer_CDROM[13] & 0x0F;
    uint32_t startAddr, dataLength;
    uint16_t numBlks;

    uint8_t* inqData_p = 0;
    uint32_t inqDataLen = 0;

    /* Direction of data transfer. */
    glCmdDirection_CDROM = glMscCbwBuffer_CDROM[12] & 0x80;

    /* Expected transfer length. */
    glMscResidue_CDROM = *((uint32_t*)(glMscCbwBuffer_CDROM + 8));
    glMscCmdStatus_CDROM = 0;

    /* Invalid LUN. */
    if (lun != 0)
    {
        glMscCmdStatus_CDROM = 1;
        CyU3PEventSet(&glMscAppEvent_CDROM, CY_FX_MSC_SIBCB_EVENT_FLAG, CYU3P_EVENT_OR);
        return;
    }

    glMscCmdLun_CDROM = lun;

    /* The medium may have been inserted or taken out by the PC since the last command: raise the matching UNIT ATTENTION and follow its state. */
    {
        uint32_t changes = CdMediaTakeChanges();

        if ((changes & CDM_CHANGE_INSERTED) != 0)
            glSensePtr_CDROM = CY_FX_MSC_SENSE_MEDIA_CHANGED;
        else if ((changes & CDM_CHANGE_REMOVED) != 0)
            glSensePtr_CDROM = CY_FX_MSC_SENSE_NO_MEDIA;

        glLunState_CDROM = CdMediaIsPresent();
    }

    /* Execute the command */
    switch (cmd)
    {
        case CY_FX_MSC_SCSI_INQUIRY:
        {
            if ((glMscCbwBuffer_CDROM[CY_FX_SCSI_INQCMD_EVPD_OFFSET] & CY_FX_SCSI_INQCMD_EVPD_MASK) != 0)
            {
                /* Request for Vital Product Data. */
                switch (glMscCbwBuffer_CDROM[CY_FX_SCSI_INQCMD_PCODE_OFFSET])
                {
                    case CY_FX_SCSI_INQCMD_PCODE_LIST:
                        /* List of supported Vital Product Data Pages. */
                        inqData_p = (uint8_t*)CyFxMscSupportedPageList_CDROM;
                        inqDataLen = sizeof(CyFxMscSupportedPageList_CDROM);
                        break;

                    case CY_FX_SCSI_INQCMD_PCODE_SERNUM:
                        /* Unit Serial Number Page. */
                        inqData_p = (uint8_t*)CyFxMscScsiSerialNumber_CDROM;
                        inqDataLen = sizeof(CyFxMscScsiSerialNumber_CDROM);
                        break;

                    case CY_FX_SCSI_INQCMD_PCODE_DEVIDENT:
                        /* Device Identification Page. */
                        inqData_p = (uint8_t*)CyFxMscScsiDevIdent_CDROM;
                        inqDataLen = sizeof(CyFxMscScsiDevIdent_CDROM);

                        /* Update the last byte based on the LUN. */
                        CyFxMscScsiDevIdent_CDROM[inqDataLen - 1] = '0' + lun;
                        break;

                    case CY_FX_SCSI_INQCMD_PCODE_BLKLIMITS:
                        /* Block Limits Page. */
                        inqData_p = (uint8_t*)CyFxMscScsiBlkLimits_CDROM;
                        inqDataLen = sizeof(CyFxMscScsiBlkLimits_CDROM);
                        break;

                    default:
                        /* No other VPD pages supported. */
                        glMscCmdStatus_CDROM = 1;
                        glSensePtr_CDROM = CY_FX_MSC_SENSE_INVALID_FIELD_IN_CBW;
                        CyU3PEventSet(&glMscAppEvent_CDROM, CY_FX_MSC_SIBCB_EVENT_FLAG, CYU3P_EVENT_OR);
                        break;
                }

                if (glMscCmdStatus_CDROM != 0)
                    break;
            }
            else
            {
                /* Page code must be zero if Vital Product Data is not being requested. */
                if (glMscCbwBuffer_CDROM[CY_FX_SCSI_INQCMD_PCODE_OFFSET] != 0)
                {
                    glMscCmdStatus_CDROM = 1;
                    glSensePtr_CDROM = CY_FX_MSC_SENSE_INVALID_PARAMETER;
                    CyU3PEventSet(&glMscAppEvent_CDROM, CY_FX_MSC_SIBCB_EVENT_FLAG, CYU3P_EVENT_OR);
                    break;
                }

                inqDataLen = CY_FX_SCSI_INQUIRY_DATALEN;
                inqData_p = (uint8_t*)CyFxMscScsiInquiryData_CDROM;
            }

            if (!CyFxMscApplnCheckCbwParams(lun, CyFalse, CyTrue, CyFalse,
                inqDataLen, &dataLength))
                break;

            glMscState_CDROM = CY_FX_MSC_STATE_DATA;
            status = CyFxMscApplnSendDataToHost_CDROM(inqData_p, dataLength);

            if (status != CY_U3P_SUCCESS)
            {
                glMscCmdStatus_CDROM = 1;
                glSensePtr_CDROM = CY_FX_MSC_SENSE_CRC_ERROR;
                CyU3PEventSet(&glMscAppEvent_CDROM, CY_FX_MSC_SIBCB_EVENT_FLAG, CYU3P_EVENT_OR);
            }
        }
        break;
        case CY_FX_MSC_SCSI_GET_CONFIG:
            CdRomGetConfiguration(lun);
            break;

        case CY_FX_MSC_SCSI_READ_CAPACITY:
            CdRomReadCapacity(lun);
            break;

        case CDROM_SCSI_READ_TOC:
            CdRomReadToc(lun);
            break;

        case CDROM_SCSI_READ_DISC_INFO:
            CdRomReadDiscInfo(lun);
            break;

        case CDROM_SCSI_GET_EVENT_STATUS:
            CdRomGetEventStatus(lun);
            break;

        case CY_FX_MSC_SCSI_MODE_SENSE_6:
            CdRomModeSense(lun, CyFalse);
            break;

        case CDROM_SCSI_MODE_SENSE_10:
            CdRomModeSense(lun, CyTrue);
            break;

        case CY_FX_MSC_SCSI_START_STOP_UNIT:
            CdRomStartStop(lun);
            break;

        case CY_FX_MSC_SCSI_PREVENT_ALLOW_MEDIUM:
            CdRomPreventAllow(lun);
            break;

        case CDROM_SCSI_SET_CD_SPEED:
            CdRomDone(lun, CyFalse);
            break;

        case CDROM_SCSI_READ_SUB_CHANNEL:
            CdRomReadSubChannel(lun);
            break;

        case CDROM_SCSI_READ_HEADER:
            CdRomReadHeader(lun);
            break;

        case CY_FX_MSC_SCSI_MODE_SELECT_6:
            CdRomModeSelect(lun, CyFalse);
            break;

        case CDROM_SCSI_MODE_SELECT_10:
            CdRomModeSelect(lun, CyTrue);
            break;

        case CY_FX_MSC_SCSI_READ_10:
            CdRomStreamRead(lun, CdRomGet32(2), CdRomGet16(7), CDM_FORMAT_USER_DATA);
            break;

        case CDROM_SCSI_READ_12:
            CdRomStreamRead(lun, CdRomGet32(2), CdRomGet32(6), CDM_FORMAT_USER_DATA);
            break;

        case CDROM_SCSI_READ_CD:
            CdRomReadCd(lun, CdRomGet32(2), ((uint32_t)CDB(6) << 16) | ((uint32_t)CDB(7) << 8) | CDB(8));
            break;

        case CDROM_SCSI_READ_CD_MSF:
        {
            uint32_t startLba = CdRomMsfToLba(CDB(3), CDB(4), CDB(5));
            uint32_t endLba = CdRomMsfToLba(CDB(6), CDB(7), CDB(8));

            CdRomReadCd(lun, startLba, (endLba > startLba) ? (endLba - startLba) : 0);
        }
        break;

        /*
                case CY_FX_MSC_SCSI_READ_CAPACITY:
                case CY_FX_MSC_SCSI_READ_FORMAT_CAPACITY:
                    {
                        uint8_t i = 0;

                        if (!CyFxMscApplnCheckCbwParams (lun, CyTrue, CyTrue, CyFalse,
                                    ((cmd == CY_FX_MSC_SCSI_READ_CAPACITY) ? 8 : 12), &dataLength))
                            break;

                        CyU3PMemSet (glMscDataBuffer, 0, 12);
                        if (cmd == CY_FX_MSC_SCSI_READ_FORMAT_CAPACITY)
                        {
                            glMscDataBuffer[3] = 0x08;
                            glMscDataBuffer[8] = 0x02;
                            i = 4;
                        }

                        glMscDataBuffer[i++] = (uint8_t)(glLunNumBlks >> 24);
                        glMscDataBuffer[i++] = (uint8_t)(glLunNumBlks >> 16);
                        glMscDataBuffer[i++] = (uint8_t)(glLunNumBlks >> 8);
                        glMscDataBuffer[i++] = (uint8_t)(glLunNumBlks);

                        i += 2;
                        glMscDataBuffer[i++] = (uint8_t)(glLunBlkSize >> 8);
                        glMscDataBuffer[i++] = (uint8_t)(glLunBlkSize);

                        glMscState = CY_FX_MSC_STATE_DATA;
                        status = CyFxMscApplnSendDataToHost (glMscDataBuffer, dataLength);
                        if (status != CY_U3P_SUCCESS)
                        {
                            glMscCmdStatus  = 1;
                            glSensePtr = CY_FX_MSC_SENSE_CRC_ERROR;
                            CyU3PEventSet (&glMscAppEvent, CY_FX_MSC_SIBCB_EVENT_FLAG, CYU3P_EVENT_OR);
                        }
                    }
                    break;
        */
        case CY_FX_MSC_SCSI_REQUEST_SENSE:
        {
            if ((glMscCbwBuffer_CDROM[CY_FX_SCSI_RQSCMD_DESC_OFFSET] & CY_FX_SCSI_REQSENSE_DESC) != 0)
            {
                glMscCmdStatus_CDROM = 1;
                glSensePtr_CDROM = CY_FX_MSC_SENSE_INVALID_FIELD_IN_CBW;
                CyU3PEventSet(&glMscAppEvent_CDROM, CY_FX_MSC_SIBCB_EVENT_FLAG, CYU3P_EVENT_OR);
                break;
            }

            if (!CyFxMscApplnCheckCbwParams(lun, CyFalse, CyTrue, CyFalse, 18, &dataLength))
                break;

            CyU3PMemSet(glMscDataBuffer_CDROM, 0, 18);
            glMscDataBuffer_CDROM[0] = 0x70;
            glMscDataBuffer_CDROM[2] = glReqSenseCode_CDROM[glSensePtr_CDROM][0];
            glMscDataBuffer_CDROM[7] = 0x0A;
            glMscDataBuffer_CDROM[12] = glReqSenseCode_CDROM[glSensePtr_CDROM][1];
            glMscDataBuffer_CDROM[13] = glReqSenseCode_CDROM[glSensePtr_CDROM][2];

            glMscState_CDROM = CY_FX_MSC_STATE_DATA;
            if (glSensePtr_CDROM == CY_FX_MSC_SENSE_MEDIA_CHANGED)
                glSensePtr_CDROM = CY_FX_MSC_SENSE_OK;

            status = CyFxMscApplnSendDataToHost_CDROM(glMscDataBuffer_CDROM, dataLength);
            if (status != CY_U3P_SUCCESS)
            {
                glMscCmdStatus_CDROM = 1;
                glSensePtr_CDROM = CY_FX_MSC_SENSE_CRC_ERROR;
                CyU3PEventSet(&glMscAppEvent_CDROM, CY_FX_MSC_SIBCB_EVENT_FLAG, CYU3P_EVENT_OR);
            }
        }
        break;

        //No real support for Verify, Start/stop unit and Prevent/Allow Medium Removal commands.
    //case CY_FX_MSC_SCSI_VERIFY_10:
    //case CY_FX_MSC_SCSI_PREVENT_ALLOW_MEDIUM:
        case CY_FX_MSC_SCSI_TEST_UNIT_READY:
            CdRomDone(lun, CyTrue);
            break;
        /*
                case CY_FX_MSC_SCSI_START_STOP_UNIT:
                    {
                        if (!CyFxMscApplnCheckCbwParams (lun, CyFalse, CyFalse, CyFalse, 0, &dataLength))
                            break;

                        //Start/Stop the unit as desired by the user.
                        if ((glMscCbwBuffer[CY_FX_SCSI_STOPCMD_LOEJ_OFFSET] & CY_FX_SCSI_STOPCMD_LOEJ_VAL) != 0)
                        {
                            if ((glMscCbwBuffer[CY_FX_SCSI_STOPCMD_LOEJ_OFFSET] & CY_FX_SCSI_STOPCMD_START_VAL) == 0)
                                glLunStopped = CyTrue;
                            else
                                glLunStopped = CyFalse;
                        }

                        glMscState = CY_FX_MSC_STATE_STATUS;
                    }
                    break;

                case CY_FX_MSC_SCSI_MODE_SENSE_6:
                    {
                        HandleModeSenseCommand (lun);
                    }
                    break;

                case CY_FX_MSC_SCSI_READ_10:
                    {
                        startAddr = (((uint32_t)glMscCbwBuffer[17] << 24) | ((uint32_t)glMscCbwBuffer[18] << 16) |
                                ((uint32_t)glMscCbwBuffer[19] << 8) | ((uint32_t)glMscCbwBuffer[20]));
                        numBlks   = (((uint16_t)glMscCbwBuffer[22] << 8) | ((uint16_t)glMscCbwBuffer[23]));

                        if (!CyFxMscApplnCheckCbwParams (lun, CyTrue, CyTrue, CyTrue, (numBlks * glLunBlkSize),
                                    &dataLength))
                            break;

                        glMscState = CY_FX_MSC_STATE_DATA;
                        status = CyFxMscApplnSendDataToHost ((glMscStorageDeviceMemory + (startAddr * glLunBlkSize)),
                               (numBlks * glLunBlkSize));
                        if (status != CY_U3P_SUCCESS)
                        {
                            glMscCmdStatus  = 1;
                            glSensePtr = CY_FX_MSC_SENSE_CRC_ERROR;
                            CyU3PEventSet (&glMscAppEvent, CY_FX_MSC_SIBCB_EVENT_FLAG, CYU3P_EVENT_OR);
                        }
                        else
                            glMscResidue = 0;
                    }
                    break;

                case CY_FX_MSC_SCSI_WRITE_10:
                    {
                        startAddr = (((uint32_t)glMscCbwBuffer[17] << 24) | ((uint32_t)glMscCbwBuffer[18] << 16) |
                                ((uint32_t)glMscCbwBuffer[19] << 8) | ((uint32_t)glMscCbwBuffer[20]));
                        numBlks   = (((uint16_t)glMscCbwBuffer[22] << 8) | ((uint16_t)glMscCbwBuffer[23]));

                        if (!CyFxMscApplnCheckCbwParams (lun, CyTrue, CyFalse, CyTrue, (numBlks * glLunBlkSize),
                                    &dataLength))
                            break;

                        glMscState = CY_FX_MSC_STATE_DATA;
                        status = CyFxMscApplnReceiveUsbData (glMscStorageDeviceMemory + (startAddr * glLunBlkSize),
                                (numBlks * glLunBlkSize));
                        if (status != CY_U3P_SUCCESS)
                        {
                            glMscCmdStatus  = 1;
                            glSensePtr = CY_FX_MSC_SENSE_CRC_ERROR;
                            CyU3PEventSet (&glMscAppEvent, CY_FX_MSC_SIBCB_EVENT_FLAG, CYU3P_EVENT_OR);
                        }
                        else
                            glMscResidue = 0;
                    }
                    break;
        */
        default:
        {
            /* Unsupported command. */
            glMscCmdStatus_CDROM = 1;
            glSensePtr_CDROM = CY_FX_MSC_SENSE_INVALID_OP_CODE;
            CyU3PEventSet(&glMscAppEvent_CDROM, CY_FX_MSC_SIBCB_EVENT_FLAG, CYU3P_EVENT_OR);
        }
        break;
    }
}

void CyFxMscApplnSendCsw_CDROM(void)
{
    CyU3PDmaBuffer_t dmaBuf;
    CyU3PReturnStatus_t status;

    /* Compute the CSW information. */
    glMscCswBuffer_CDROM[0] = 'U';
    glMscCswBuffer_CDROM[1] = 'S';
    glMscCswBuffer_CDROM[2] = 'B';
    glMscCswBuffer_CDROM[3] = 'S';
    CyU3PMemCopy(glMscCswBuffer_CDROM + 4, glMscCbwBuffer_CDROM + 4, 4);       /* Copy the tag. */
    *((uint32_t*)(glMscCswBuffer_CDROM + 8)) = glMscResidue_CDROM;
    glMscCswBuffer_CDROM[12] = glMscCmdStatus_CDROM;

    dmaBuf.buffer = glMscCswBuffer_CDROM;
    dmaBuf.status = 0;
    dmaBuf.size = 32;
    dmaBuf.count = 13;
    status = CyU3PDmaChannelSetupSendBuffer(&glChHandleMscIn_CDROM, &dmaBuf);
    if (status == CY_U3P_SUCCESS)
        glMscState_CDROM = CY_FX_MSC_STATE_CSW;
    else
        CyU3PDmaChannelReset(&glChHandleMscIn_CDROM);
}

void CyFxMscApplnUSBEventCB_CDROM(CyU3PUsbEventType_t evtype, uint16_t evdata)
{
    uint16_t epPktSize = 64;    /* Full speed setting by default. */

    switch (evtype)
    {
        /* USB Suspend is handled solely by the power management thread (POWER.c) - see
         * CY_FX_PM_USB_SUSP_EVENT_FLAG. CyU3PSysEnterSuspendMode() must never be called from here. */

        case CY_U3P_USB_EVENT_DISCONNECT:
        case CY_U3P_USB_EVENT_RESET:
        case CY_U3P_USB_EVENT_CONNECT:
        {
            //CyU3PDebugPrint(4, "CDROM USB event %d received\r\n", evtype);
            CyU3PEventSet(&glMscAppEvent_CDROM, CY_FX_MSC_RESET_EVENT_FLAG, CYU3P_EVENT_OR);

            CyFxMscApplnResetDatapath_CDROM();
            glDevConfigured_CDROM = CyFalse;

            glLunStopped_CDROM = CyFalse;
        }
        break;

        case CY_U3P_USB_EVENT_SETCONF:
        {
            if (evdata == 0)
            {
                glDevConfigured_CDROM = CyFalse;
                break;
            }

            //CyU3PDebugPrint(4, "CDROM USB Set Config completed\r\n");

            /* Check if Set Config event is handled */
            if (glDevConfigured_CDROM == CyFalse)
            {
                glDevConfigured_CDROM = CyTrue;

                /* The max. packet size depends on connection speed. */
                switch (CyU3PUsbGetSpeed())
                {
                    case CY_U3P_FULL_SPEED:
                        epPktSize = 64;
                        break;
                    case CY_U3P_HIGH_SPEED:
                        epPktSize = 512;
                        break;
                    case CY_U3P_SUPER_SPEED:
                    default:
                        epPktSize = 1024;
                        break;
                }

                CyU3PDebugPrint(4, "CD-ROM: USB speed %d (1 full, 2 high, 3 super), bulk packet size %d\r\n", CyU3PUsbGetSpeed(), epPktSize);

                /* Update the packet size for the endpoints based on connection speed. */
                CyU3PSetEpPacketSize(CY_FX_EP_CD_ROM_OUT, epPktSize);
                CyU3PSetEpPacketSize(CY_FX_EP_CD_ROM_IN, epPktSize);

                /* Clear stall on the endpoints and reset the DMA channels. */
                CyU3PUsbStall(CY_FX_EP_CD_ROM_OUT, CyFalse, CyTrue);
                CyU3PUsbStall(CY_FX_EP_CD_ROM_IN, CyFalse, CyTrue);

                CyU3PDmaChannelReset(&glChHandleMscIn_CDROM);
                CyU3PDmaChannelReset(&glChHandleMscOut_CDROM);

                /* Restore the LUN to an active state. */
                glLunStopped_CDROM = CyFalse;

                /* Notify the thread to start waiting for a mass storage CBW. */
                CyU3PEventSet(&glMscAppEvent_CDROM, CY_FX_MSC_SETCONF_EVENT_FLAG, CYU3P_EVENT_OR);
            }
        }
        break;

        default:
            break;
    }
}

CyBool_t CyFxMscApplnLPMRqtCB_CDROM(CyU3PUsbLinkPowerMode link_mode)
{
    /* If we are in the middle of processing a mass storage command, keep reverting to U0 state. */
    return !glMscDriverBusy_CDROM;
}

/* Create the DMA channels required for the mass storage device operation. */
void CyFxMscApplnDmaInit_CDROM(void)
{
    CyU3PDmaChannelConfig_t dmaConfig;
    CyU3PReturnStatus_t status;

    //CyU3PDebugPrint(4, "CD-ROM init memory\r\n");

    /* The scratch buffers are allocated once; the channels are created again after every restart of the USB stack. */
    if (glMscCbwBuffer_CDROM == 0)
    {
        glMscCbwBuffer_CDROM = (uint8_t*)CyU3PDmaBufferAlloc(1024);
        glMscCswBuffer_CDROM = (uint8_t*)CyU3PDmaBufferAlloc(1024);
        glMscDataBuffer_CDROM = (uint8_t*)CyU3PDmaBufferAlloc(1024);
    }
    if ((glMscCbwBuffer_CDROM == 0) || (glMscCswBuffer_CDROM == 0) || (glMscDataBuffer_CDROM == 0))
    {
        //CyU3PDebugPrint(4, "Failed to allocate scratch buffer\r\n");
        CyFxAppErrorHandler(CY_U3P_ERROR_MEMORY_ERROR);
    }

    /* Both DMA channels are created with SuperSpeed parameters. The CyU3PSetEpPacketSize () API is used
       to reconfigure the endpoints to work with DMA channels with large buffers. */
    dmaConfig.size = 1024;
    dmaConfig.count = CY_FX_MSC_DMA_BUF_COUNT;
    dmaConfig.prodSckId = (CyU3PDmaSocketId_t)(CY_U3P_UIB_SOCKET_PROD_0 | CY_FX_EP_CD_ROM_OUT_SOCKET);
    dmaConfig.consSckId = CY_U3P_CPU_SOCKET_CONS;
    dmaConfig.dmaMode = CY_U3P_DMA_MODE_BYTE;
    dmaConfig.notification = CY_U3P_DMA_CB_RECV_CPLT;
    dmaConfig.cb = CyFxMscApplnDmaCb_CDROM;
    dmaConfig.prodHeader = 0;
    dmaConfig.prodFooter = 0;
    dmaConfig.consHeader = 0;
    dmaConfig.prodAvailCount = 0;

    status = CyU3PDmaChannelCreate(&glChHandleMscOut_CDROM, CY_U3P_DMA_TYPE_MANUAL_IN, &dmaConfig);
    if (status != CY_U3P_SUCCESS)
    {
        //CyU3PDebugPrint(4, "DMA channel create failed, code=%d\r\n", status);
        CyFxAppErrorHandler(status);
    }

    dmaConfig.prodSckId = CY_U3P_CPU_SOCKET_PROD;
    dmaConfig.consSckId = (CyU3PDmaSocketId_t)(CY_U3P_UIB_SOCKET_CONS_0 | CY_FX_EP_CD_ROM_IN_SOCKET);
    dmaConfig.notification = CY_U3P_DMA_CB_SEND_CPLT;
    dmaConfig.cb = CyFxMscApplnDmaCb_CDROM;

    status = CyU3PDmaChannelCreate(&glChHandleMscIn_CDROM, CY_U3P_DMA_TYPE_MANUAL_OUT, &dmaConfig);
    if (status != CY_U3P_SUCCESS)
    {
        //CyU3PDebugPrint(4, "DMA channel create failed, code=%d\r\n", status);
        CyFxAppErrorHandler(status);
    }
}

/* Destroys the DMA channels (before the USB stack is restarted). */
void CyFxMscApplnDmaDeInit_CDROM(void)
{
    glDevConfigured_CDROM = CyFalse;
    glMscState_CDROM = CY_FX_MSC_STATE_INACTIVE;
    CyU3PDmaChannelDestroy(&glChHandleMscOut_CDROM);
    CyU3PDmaChannelDestroy(&glChHandleMscIn_CDROM);
}

void CyFxMscAppInitVars_CDROM(void)
{
    glLunState_CDROM = CyTrue;
    glLunBlkSize_CDROM = 512;
    glLunNumBlks_CDROM = ((512 / 512) - 1);
    glSensePtr_CDROM = CY_FX_MSC_SENSE_DEVICE_RESET;
    glLunStopped_CDROM = CyFalse;
}


void CyFxMscApplnDmaCb_CDROM(
    CyU3PDmaChannel* handle,
    CyU3PDmaCbType_t   evtype,
    CyU3PDmaCBInput_t* input)
{
    CyU3PDmaBuffer_t dmaBuf = input->buffer_p;

    switch (evtype)
    {
        case CY_U3P_DMA_CB_RECV_CPLT:
        {
            if ((glMscState_CDROM == CY_FX_MSC_STATE_CBW) || (glMscState_CDROM == CY_FX_MSC_STATE_WAITING))
            {
                if ((dmaBuf.count != 31) || (dmaBuf.buffer[0] != 'U') || (dmaBuf.buffer[1] != 'S') ||
                    (dmaBuf.buffer[2] != 'B') || (dmaBuf.buffer[3] != 'C'))
                    glInPhaseError_CDROM = CyTrue;
                else
                    glInPhaseError_CDROM = CyFalse;
                CyU3PEventSet(&glMscAppEvent_CDROM, CY_FX_MSC_CBW_EVENT_FLAG, CYU3P_EVENT_OR);
            }
            else
            {
                CyU3PEventSet(&glMscAppEvent_CDROM, CY_FX_MSC_DATA_RECV_EVENT_FLAG, CYU3P_EVENT_OR);
            }
        }
        break;

        case CY_U3P_DMA_CB_SEND_CPLT:
            CyU3PEventSet(&glMscAppEvent_CDROM, CY_FX_MSC_DATASENT_EVENT_FLAG, CYU3P_EVENT_OR);
            break;

        default:
            break;
    }
}

void CyFxMscAppHandleMscTask_CDROM(void)
{
    CyU3PReturnStatus_t status;
    CyU3PDmaBuffer_t    dmaBuf;

    uint32_t evMask = CY_FX_MSC_SETCONF_EVENT_FLAG | CY_FX_MSC_RESET_EVENT_FLAG | CY_FX_MSC_CBW_EVENT_FLAG |
        CY_FX_MSC_DATASENT_EVENT_FLAG | CY_FX_MSC_SIBCB_EVENT_FLAG |
        CY_FX_MSC_DATA_RECV_EVENT_FLAG;
    uint32_t evStat;

    for (;;)
    {
        //CyU3PDebugPrint(4, "PROCESS?? CyFxMscAppHandleMscTask_CDROM()\r\n");
        status = CyU3PEventGet(&glMscAppEvent_CDROM, evMask, CYU3P_EVENT_OR_CLEAR, &evStat, CYU3P_WAIT_FOREVER);
        if (status != CY_U3P_SUCCESS)
        {
            //CyU3PDebugPrint(4, "NOT PROCESS CyFxMscAppHandleMscTask_CDROM() %d\r\n", status);
        }
        if (status == CY_U3P_SUCCESS)
        {
            //CyU3PDebugPrint(4, "PROCESS CyFxMscAppHandleMscTask_CDROM()\r\n");

            if (evStat & CY_FX_MSC_RESET_EVENT_FLAG)
            {
                glMscState_CDROM = CY_FX_MSC_STATE_INACTIVE;
            }

            if (evStat & CY_FX_MSC_SETCONF_EVENT_FLAG)
                glMscState_CDROM = CY_FX_MSC_STATE_CBW;

            if (evStat & CY_FX_MSC_CBW_EVENT_FLAG)
            {
                if (glInPhaseError_CDROM)
                {
                    /* Phase error: Send an error CSW and then stall both endpoints. */
                    glMscCmdStatus_CDROM = 2;
                    CyU3PUsbStall(CY_FX_EP_CD_ROM_IN, CyTrue, CyFalse);
                    CyU3PUsbStall(CY_FX_EP_CD_ROM_OUT, CyTrue, CyFalse);
                }
                else
                {
                    glMscDriverBusy_CDROM = CyTrue;
                    CyU3PUsbLPMDisable();

                    /* Make sure the link is brought back to active state at this stage. */
                    if (CyU3PUsbGetSpeed() == CY_U3P_SUPER_SPEED)
                        CyU3PUsbSetLinkPowerState(CyU3PUsbLPM_U0);
                    else
                        CyU3PUsb2Resume();

                    /* Parse CBW here. */
                    glMscState_CDROM = CY_FX_MSC_STATE_DATA;
                    CyFxMscApplnParseCbw_CDROM();
                }
            }

            if (evStat & CY_FX_MSC_DATA_RECV_EVENT_FLAG)
            {
                /* Data received for Mode Select command. */
                status = CyU3PDmaChannelWaitForRecvBuffer(&glChHandleMscOut_CDROM, &dmaBuf, CYU3P_NO_WAIT);
                //CyU3PDebugPrint(4, "WaitForRecv %d\r\n", status);
                if (status == CY_U3P_SUCCESS)
                {
                    glMscCmdStatus_CDROM = 0;
                }
                else
                {
                    glMscCmdStatus_CDROM = 1;
                    glSensePtr_CDROM = CY_FX_MSC_SENSE_CRC_ERROR;
                }

                glMscResidue_CDROM = 0;
                CyU3PEventSet(&glMscAppEvent_CDROM, CY_FX_MSC_DATASENT_EVENT_FLAG, CYU3P_EVENT_OR);
            }

            if (evStat & CY_FX_MSC_DATASENT_EVENT_FLAG)
            {
                /* If CSW has been sent, go to CBW state. */
                if (glMscState_CDROM == CY_FX_MSC_STATE_CSW)
                    glMscState_CDROM = CY_FX_MSC_STATE_CBW;

                /* If data has been sent, go to CSW state. */
                if (glMscState_CDROM == CY_FX_MSC_STATE_DATA)
                {
                    /* If we sent less data than the host wanted, stall the endpoint afterwards. */
                    if ((glMscResidue_CDROM != 0) && (glMscCmdStatus_CDROM == 0))
                        CyU3PUsbStall(CY_FX_EP_CD_ROM_IN, CyTrue, CyFalse);

                    glMscState_CDROM = CY_FX_MSC_STATE_STATUS;
                }
            }

            if (evStat & CY_FX_MSC_SIBCB_EVENT_FLAG)
            {
                /* Stall the endpoint if any data transfer is expected. */
                if ((glMscCmdStatus_CDROM) && (glMscResidue_CDROM))
                {
                    /* The DMA channel has already been reset. */
                    if (glCmdDirection_CDROM)
                        CyU3PUsbStall(CY_FX_EP_CD_ROM_IN, CyTrue, CyFalse);
                    else
                        CyU3PUsbStall(CY_FX_EP_CD_ROM_OUT, CyTrue, CyFalse);
                }
                else
                {
                    /* Wait for all of the data to be transferred to the host in the case of a read. */
                    if (glCmdDirection_CDROM)
                        CyU3PDmaChannelWaitForCompletion(&glChHandleMscIn_CDROM, CYU3P_WAIT_FOREVER);
                }

                /* Now move to the STATUS state to try and send the CSW. */
                glMscState_CDROM = CY_FX_MSC_STATE_STATUS;
            }

            if (glMscState_CDROM == CY_FX_MSC_STATE_CBW)
            {
                /* The command has been handled. Link power management stays OFF (U1 / U2 are refused, see CyFxMscApplnLPMRqtCB in main.c). */
                glMscDriverBusy_CDROM = CyFalse;

                dmaBuf.buffer = glMscCbwBuffer_CDROM;
                dmaBuf.status = 0;
                dmaBuf.count = 0;
                dmaBuf.size = 64;     /* CBW is expected to be smaller than 64 bytes. */
                status = CyU3PDmaChannelSetupRecvBuffer(&glChHandleMscOut_CDROM, &dmaBuf);
                if (status == CY_U3P_SUCCESS)
                {
                    glMscState_CDROM = CY_FX_MSC_STATE_WAITING;

                    /* Make sure the link is brought back to active state at this stage. */
                    if (CyU3PUsbGetSpeed() == CY_U3P_SUPER_SPEED)
                        CyU3PUsbSetLinkPowerState(CyU3PUsbLPM_U0);
                    else
                        CyU3PUsb2Resume();
                }
                else
                    CyU3PDmaChannelReset(&glChHandleMscOut_CDROM);
            }

            if (glMscState_CDROM == CY_FX_MSC_STATE_STATUS)
            {
                CyFxMscApplnSendCsw_CDROM();
            }
        }
    }
}

/*
 * Entry function for the mscAppThread
 */
void MscAppThread_Entry_CDROM(uint32_t input)
{
    uint32_t txApiRetStatus;

    /* Create FX MSC events */
    txApiRetStatus = CyU3PEventCreate(&glMscAppEvent_CDROM);
    if (txApiRetStatus != 0)
    {
        /* Error Handling */
        //CyU3PDebugPrint(4, "MSC CDROM   Appln Create Event Failed, Error Code = %d\n", txApiRetStatus);
        CyFxAppErrorHandler(CY_U3P_ERROR_MEMORY_ERROR);
    }

    /* Initialize global variables. */
    CyFxMscAppInitVars_CDROM();

    //CyU3PDebugPrint(4, "MSC CDROM   Appln enter loop\r\n");

    /* Do the MSC application main work. */
    CyFxMscAppHandleMscTask_CDROM();
}

