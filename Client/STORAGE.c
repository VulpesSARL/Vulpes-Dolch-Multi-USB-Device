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
#include "HDDMEDIA.h"

#if (CLIENT_WITH_HDD == 0)

/* The bridge build: the interface exists in the descriptors, but nothing answers on its endpoints. */
CyBool_t glDevConfigured_STORAGE = CyFalse;
CyBool_t glInPhaseError_STORAGE = CyFalse;
CyU3PEvent glMscAppEvent_STORAGE;

void CyFxMscApplnResetDatapath_STORAGE (void)
{
}

void CyFxMscApplnUSBEventCB_STORAGE (CyU3PUsbEventType_t evtype, uint16_t evdata)
{
}

CyBool_t CyFxMscApplnLPMRqtCB_STORAGE (CyU3PUsbLinkPowerMode link_mode)
{
    return (CyTrue);
}

void CyFxMscApplnDmaInit_STORAGE (void)
{
}

void CyFxMscApplnDmaDeInit_STORAGE (void)
{
}

void MscAppThread_Entry_STORAGE (uint32_t input)
{
    CyU3PEventCreate (&glMscAppEvent_STORAGE);
    for (;;)
        CyU3PThreadSleep (1000);
}

#else
#include "GpifLinkCore.h"

/* Request Sense Lookup Table */
static const uint8_t glReqSenseCode_STORAGE[][3] =
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

#define HDD_SENSE_TARGET_FAILURE           (0x11)

/* The removable bit (RMB) of the disk. Windows treats a removable disk as a card reader: of a partitioned medium it mounts the first partition only (GPT with an EFI
 * partition in front, for example, shows nothing usable), so the default is 0 = a fixed disk, which shows all partitions. Consequences of 0: Windows only looks for
 * a medium when the device enumerates (an image loaded later is not noticed until the target PC rescans its disks, or the Client is replugged), it never asks for a
 * media eject (START STOP UNIT) and takes no PREVENT MEDIUM REMOVAL lock, so the eject of the disk from the tool is not refused by the target PC any more.
 * -DHDD_REMOVABLE=1 gives the old behaviour. */
#ifndef HDD_REMOVABLE
#define HDD_REMOVABLE (0)
#endif

/* The standard INQUIRY response of the disk: a direct access block device, removable or not (HDD_REMOVABLE). */
uint8_t CyFxMscScsiInquiryData_STORAGE[] = {
        0x00,       /* PQ and PDT: direct access block device */
        ((HDD_REMOVABLE == 1) ? 0x80 : 0x00),   /* RMB: removable medium */
        0x06,       /* Version: SPC-4 (0 = "no standard" kept Windows from using READ CAPACITY(16)?) */
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
        ' ',
        ' ',
        'D',        /* Product Id (16 bytes, space padded) */
        'o',
        'l',
        'c',
        'h',
        ' ',
        'H',
        'D',
        'D',
        ' ',
        ' ',
        ' ',
        ' ',
        ' ',
        ' ',
        ' ',

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

#define HDD_INQ_PCODE_LBP                       (0xB2u)

static const uint8_t CyFxMscSupportedPageList_STORAGE[] __attribute__((aligned(32))) = {
    0x00,                                       /* PDT = 0. */
    CY_FX_SCSI_INQCMD_PCODE_LIST,               /* Page code = 0. */
    0x00,                                       /* Reserved. */
    0x05,                                       /* Number of page codes that follow. */
    CY_FX_SCSI_INQCMD_PCODE_LIST,
    CY_FX_SCSI_INQCMD_PCODE_SERNUM,
    CY_FX_SCSI_INQCMD_PCODE_DEVIDENT,
    CY_FX_SCSI_INQCMD_PCODE_BLKLIMITS,
    HDD_INQ_PCODE_LBP,
};

/* Different from the CD-ROM's, so the OS never takes the two functions for one device. */
static const uint8_t CyFxMscScsiSerialNumber_STORAGE[] __attribute__((aligned(32))) = {
    0x00,                                       /* PDT = 0. */
    CY_FX_SCSI_INQCMD_PCODE_SERNUM,             /* Page code = 0x80. */
    0x00,                                       /* Reserved. */
    0x0C,                                       /* Page length. */
    'V', 'D', 'H', 'D',
    '0', '0', '0', '0',
    '0', '0', '0', '1'
};

static uint8_t CyFxMscScsiDevIdent_STORAGE[] __attribute__((aligned(32))) = {
    0x00,                                       /* PDT = 0. */
    CY_FX_SCSI_INQCMD_PCODE_DEVIDENT,           /* Page code = 0x83. */
    0x00,                                       /* MSB (PAGE LENGTH) = 0x00. */
    0x0C,                                       /* LSB (PAGE LENGTH) = 15 - 3 = 12. */

    0x01, /* Protocol ID & Code Set */
    0x02, /* PIV & Association & Identifier Type */
    0x00, /* Reserved */
    0x08, /* ID Length (n-3) */
    'V', 'D', 'H', 'D', 'D', 'I', 'S', '0' /* Text: VDHDDIS0 (the last character is the LUN) */
};

/* Block limits: at most 128 sectors (64 KB) per command; UNMAP supported with up to 16 descriptors of any size. */
static const uint8_t CyFxMscScsiBlkLimits_STORAGE[] __attribute__((aligned(32))) = {
    0x00,                                       /* PDT = 0. */
    CY_FX_SCSI_INQCMD_PCODE_BLKLIMITS,          /* Page code = 0xB0. */
    0x00,                                       /* Reserved. */
    0x3C,                                       /* Page length (64 - 4). */
    0x00,                                       /* WSNZ = 0. */
    0x00,                                       /* Maximum compare and write length. */
    0x00, 0x00,                                 /* Optimal transfer length granularity. */
    0x00, 0x00, 0x00, 0x80,                     /* Maximum transfer length: 128 sectors. */
    0x00, 0x00, 0x00, 0x80,                     /* Optimal transfer length: 128 sectors. */
    0x00, 0x00, 0x00, 0x00,                     /* Maximum prefetch length. */
    0x7F, 0xFF, 0xFF, 0xFF,                     /* Maximum UNMAP LBA count. */
    0x00, 0x00, 0x00, 0x10,                     /* Maximum UNMAP block descriptor count. */
    0x00, 0x00, 0x00, 0x08,                     /* Optimal UNMAP granularity: 8 sectors (4 KB). */
    0x00, 0x00, 0x00, 0x00,                     /* UNMAP granularity alignment, not valid. */
    0x00, 0x00, 0x00, 0x00, 0x7F, 0xFF, 0xFF, 0xFF,   /* Maximum WRITE SAME length. */
    0x00, 0x00, 0x00, 0x00,                     /* Reserved. */
    0x00, 0x00, 0x00, 0x00,                     /* Reserved. */
    0x00, 0x00, 0x00, 0x00,                     /* Reserved. */
    0x00, 0x00, 0x00, 0x00,                     /* Reserved. */
    0x00, 0x00, 0x00, 0x00                      /* Reserved. */
};

/* Logical block provisioning: thin provisioned (the PC's image may be a sparse VHDX), UNMAP and WRITE SAME release space, and released
 * blocks read back as zeros. With this the OS formats by releasing instead of writing every sector. */
static const uint8_t CyFxMscScsiLbpPage_STORAGE[] __attribute__((aligned(32))) = {
    0x00,                                       /* PDT = 0. */
    HDD_INQ_PCODE_LBP,                          /* Page code = 0xB2. */
    0x00,                                       /* Reserved. */
    0x04,                                       /* Page length. */
    0x00,                                       /* Threshold exponent: no thresholds. */
    0xE4,                                       /* LBPU, LBPWS, LBPWS10, LBPRZ. */
    0x02,                                       /* Provisioning type: thin provisioned. */
    0x00                                        /* Reserved. */
};

static CyBool_t glLunState_STORAGE;                   /* Whether the LUN has valid storage. */
static CyBool_t glLunStopped_STORAGE;                 /* Whether each LUN has been stopped by the user. */
static uint8_t  glSensePtr_STORAGE = CY_FX_MSC_SENSE_DEVICE_RESET;

CyBool_t         glDevConfigured_STORAGE = CyFalse;              /* Whether the device has been configured. */
CyBool_t         glInPhaseError_STORAGE = CyFalse;              /* Whether a phase error condition has been detected. */
static CyFxMscFuncState glMscState_STORAGE = CY_FX_MSC_STATE_INACTIVE;

static CyU3PDmaChannel     glChHandleMscOut_STORAGE;                    /* DMA channel for OUT endpoint. */
static CyU3PDmaChannel     glChHandleMscIn_STORAGE;                     /* DMA channel for IN endpoint. */
static volatile CyBool_t   glChannelsReady_STORAGE = CyFalse;           /* The channels exist: only while the disk interface is part of the USB configuration. */

CyU3PEvent  glMscAppEvent_STORAGE;                               /* MSC application Event group */

uint8_t* glMscCbwBuffer_STORAGE = 0;                                   /* Scratch buffer used for CBW. */
uint8_t* glMscCswBuffer_STORAGE = 0;                                   /* Scratch buffer used for CSW. */
uint8_t* glMscDataBuffer_STORAGE = 0;                                   /* Scratch buffer used for query commands. */

static uint8_t  glCmdDirection_STORAGE = 0;                            /* SCSI Command Direction */
static uint8_t  glMscCmdStatus_STORAGE = 0;                            /* MSC command status. */
static uint8_t  glMscCmdLun_STORAGE = 0;                            /* Current MSC LUN. */
static uint32_t glMscResidue_STORAGE = 0;                            /* Residue length for CSW */
static CyBool_t glMscDriverBusy_STORAGE = CyFalse;

void CyFxMscApplnResetDatapath_STORAGE(void)
{
    //CyU3PDebugPrint(4, "CyFxMscApplnResetDatapath_STORAGE()\r\n");

    /* Without the disk interface there are no endpoints and no channels to reset. */
    if (glChannelsReady_STORAGE == CyFalse)
    {
        glSensePtr_STORAGE = CY_FX_MSC_SENSE_DEVICE_RESET;
        glInPhaseError_STORAGE = CyFalse;
        glMscDriverBusy_STORAGE = CyFalse;
        return;
    }

	CyU3PUsbSetEpNak(CY_FX_EP_STORAGE_OUT, CyTrue);
    CyU3PUsbSetEpNak(CY_FX_EP_STORAGE_IN, CyTrue);
    CyU3PBusyWait(120);

    CyU3PDmaChannelReset(&glChHandleMscOut_STORAGE);
    CyU3PDmaChannelReset(&glChHandleMscIn_STORAGE);

    CyU3PUsbFlushEp(CY_FX_EP_STORAGE_OUT);
    CyU3PUsbStall(CY_FX_EP_STORAGE_OUT, CyFalse, CyTrue);

    CyU3PUsbFlushEp(CY_FX_EP_STORAGE_IN);
    CyU3PUsbStall(CY_FX_EP_STORAGE_IN, CyFalse, CyTrue);

    /* Request Sense Index */
    glSensePtr_STORAGE = CY_FX_MSC_SENSE_DEVICE_RESET;

    CyU3PUsbSetEpNak(CY_FX_EP_STORAGE_OUT, CyFalse);
    CyU3PUsbSetEpNak(CY_FX_EP_STORAGE_IN, CyFalse);

    glInPhaseError_STORAGE = CyFalse;
    glMscDriverBusy_STORAGE = CyFalse;

    //CyU3PDebugPrint(4, "CyFxMscApplnResetDatapath_STORAGE() -- DONE\r\n");
}

/* Function to initiate sending of data to the USB host. */
CyU3PReturnStatus_t CyFxMscApplnSendDataToHost_STORAGE(uint8_t* data, uint32_t length)
{
    CyU3PDmaBuffer_t    dmaBuf;
    CyU3PReturnStatus_t status;

    /* Prepare the DMA Buffer */
    dmaBuf.buffer = data;
    dmaBuf.status = 0;
    dmaBuf.size = (length + 15) & 0xFFF0;      /* Round up to a multiple of 16.  */
    dmaBuf.count = length;

    status = CyU3PDmaChannelSetupSendBuffer(&glChHandleMscIn_STORAGE, &dmaBuf);
    return status;
}

/* Function to initiate sending of data to the USB host. */
CyU3PReturnStatus_t CyFxMscApplnReceiveUsbData_STORAGE(uint8_t* data, uint32_t length)
{
    CyU3PDmaBuffer_t    dmaBuf;
    CyU3PReturnStatus_t status;

    /* Prepare the DMA Buffer */
    dmaBuf.buffer = data;
    dmaBuf.status = 0;
    dmaBuf.size = (length + 15) & 0xFFF0;      /* Round up to a multiple of 16.  */
    dmaBuf.count = length;

    status = CyU3PDmaChannelSetupRecvBuffer(&glChHandleMscOut_STORAGE, &dmaBuf);
    return status;
}


/* Common function to check all CBW parameters against expected values. */
CyBool_t CyFxMscApplnCheckCbwParams_STORAGE(
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
    if ((isReadExpected != (glCmdDirection_STORAGE == 0x80)) || ((isDataCmd) && (expLength != glMscResidue_STORAGE)) ||
        ((expLength != 0) && (glMscResidue_STORAGE == 0)) || ((expLength == 0) && (glMscResidue_STORAGE != 0)))
    {
        glMscCmdStatus_STORAGE = 1;
        glSensePtr_STORAGE = CY_FX_MSC_SENSE_INVALID_PARAMETER;
        CyU3PEventSet(&glMscAppEvent_STORAGE, CY_FX_MSC_SIBCB_EVENT_FLAG, CYU3P_EVENT_OR);
        return CyFalse;
    }

    /* Verify that the LUN is ready for commands that report the LUN state or do data transfers. */
    if (lunState)
    {
        if (glSensePtr_STORAGE != CY_FX_MSC_SENSE_MEDIA_CHANGED)
        {
            if ((!glLunState_STORAGE) || (glLunStopped_STORAGE))
            {
                glMscCmdStatus_STORAGE = 1;
                glSensePtr_STORAGE = CY_FX_MSC_SENSE_NO_MEDIA;
                CyU3PEventSet(&glMscAppEvent_STORAGE, CY_FX_MSC_SIBCB_EVENT_FLAG, CYU3P_EVENT_OR);
                return CyFalse;
            }
            else
            {
                glSensePtr_STORAGE = CY_FX_MSC_SENSE_OK;
            }
        }
        else
        {
            glMscCmdStatus_STORAGE = 1;
            CyU3PEventSet(&glMscAppEvent_STORAGE, CY_FX_MSC_SIBCB_EVENT_FLAG, CYU3P_EVENT_OR);
            return CyFalse;
        }
    }

    /* Calculate the size of data to be transferred in the case of non-data commands. */
    if (!isDataCmd)
    {
        if (expLength > glMscResidue_STORAGE)
        {
            *actLength_p = glMscResidue_STORAGE;
            glMscResidue_STORAGE = 0;
        }
        else
        {
            *actLength_p = expLength;
            glMscResidue_STORAGE -= expLength;
        }
    }

    return CyTrue;
}

void HandleModeSenseCommand_STORAGE(uint8_t lun)
{
    CyU3PReturnStatus_t status;
    uint32_t            dataLength;

    glMscDataBuffer_STORAGE[0] = 0x03;
    glMscDataBuffer_STORAGE[1] = 0x00;
    glMscDataBuffer_STORAGE[2] = 0x00;
    glMscDataBuffer_STORAGE[3] = 0x00;

    dataLength = 4;
    if (!CyFxMscApplnCheckCbwParams_STORAGE(lun, CyTrue, CyTrue, CyFalse, dataLength, &dataLength))
        return;

    glMscState_STORAGE = CY_FX_MSC_STATE_DATA;
    status = CyFxMscApplnSendDataToHost_STORAGE(glMscDataBuffer_STORAGE, dataLength);
    if (status != CY_U3P_SUCCESS)
    {
        glMscCmdStatus_STORAGE = 1;
        glSensePtr_STORAGE = CY_FX_MSC_SENSE_CRC_ERROR;
        CyU3PEventSet(&glMscAppEvent_STORAGE, CY_FX_MSC_SIBCB_EVENT_FLAG, CYU3P_EVENT_OR);
    }
}

#include "STORAGE_commands.inc"

/* Parse the received CBW and handle the command. */
void CyFxMscApplnParseCbw_STORAGE(void)
{
    CyU3PReturnStatus_t status;
    uint8_t cmd = glMscCbwBuffer_STORAGE[15];
    uint8_t lun = glMscCbwBuffer_STORAGE[13] & 0x0F;
    uint32_t dataLength;

    uint8_t* inqData_p = 0;
    uint32_t inqDataLen = 0;

    /* Direction of data transfer. */
    glCmdDirection_STORAGE = glMscCbwBuffer_STORAGE[12] & 0x80;

    /* Expected transfer length. */
    glMscResidue_STORAGE = *((uint32_t*)(glMscCbwBuffer_STORAGE + 8));
    glMscCmdStatus_STORAGE = 0;

    /* Invalid LUN. */
    if (lun != 0)
    {
        glMscCmdStatus_STORAGE = 1;
        CyU3PEventSet(&glMscAppEvent_STORAGE, CY_FX_MSC_SIBCB_EVENT_FLAG, CYU3P_EVENT_OR);
        return;
    }

    glMscCmdLun_STORAGE = lun;

    /* The medium may have been inserted or taken out by the PC since the last command: raise the matching UNIT ATTENTION and follow its state. */
    {
        uint32_t changes = HddMediaTakeChanges();

        if ((changes & HDM_CHANGE_INSERTED) != 0)
            glSensePtr_STORAGE = CY_FX_MSC_SENSE_MEDIA_CHANGED;
        else if ((changes & HDM_CHANGE_REMOVED) != 0)
            glSensePtr_STORAGE = CY_FX_MSC_SENSE_NO_MEDIA;

        glLunState_STORAGE = HddMediaIsPresent();
    }

    /* Execute the command */
    switch (cmd)
    {
        case CY_FX_MSC_SCSI_INQUIRY:
        {
            if ((glMscCbwBuffer_STORAGE[CY_FX_SCSI_INQCMD_EVPD_OFFSET] & CY_FX_SCSI_INQCMD_EVPD_MASK) != 0)
            {
                /* Request for Vital Product Data. */
                switch (glMscCbwBuffer_STORAGE[CY_FX_SCSI_INQCMD_PCODE_OFFSET])
                {
                    case CY_FX_SCSI_INQCMD_PCODE_LIST:
                        /* List of supported Vital Product Data Pages. */
                        inqData_p = (uint8_t*)CyFxMscSupportedPageList_STORAGE;
                        inqDataLen = sizeof(CyFxMscSupportedPageList_STORAGE);
                        break;

                    case CY_FX_SCSI_INQCMD_PCODE_SERNUM:
                        /* Unit Serial Number Page. */
                        inqData_p = (uint8_t*)CyFxMscScsiSerialNumber_STORAGE;
                        inqDataLen = sizeof(CyFxMscScsiSerialNumber_STORAGE);
                        break;

                    case CY_FX_SCSI_INQCMD_PCODE_DEVIDENT:
                        /* Device Identification Page. */
                        inqData_p = (uint8_t*)CyFxMscScsiDevIdent_STORAGE;
                        inqDataLen = sizeof(CyFxMscScsiDevIdent_STORAGE);

                        /* Update the last byte based on the LUN. */
                        CyFxMscScsiDevIdent_STORAGE[inqDataLen - 1] = '0' + lun;
                        break;

                    case CY_FX_SCSI_INQCMD_PCODE_BLKLIMITS:
                        /* Block Limits Page. */
                        inqData_p = (uint8_t*)CyFxMscScsiBlkLimits_STORAGE;
                        inqDataLen = sizeof(CyFxMscScsiBlkLimits_STORAGE);
                        break;

                    case HDD_INQ_PCODE_LBP:
                        /* Logical Block Provisioning Page. */
                        inqData_p = (uint8_t*)CyFxMscScsiLbpPage_STORAGE;
                        inqDataLen = sizeof(CyFxMscScsiLbpPage_STORAGE);
                        break;

                    default:
                        /* No other VPD pages supported. */
                        glMscCmdStatus_STORAGE = 1;
                        glSensePtr_STORAGE = CY_FX_MSC_SENSE_INVALID_FIELD_IN_CBW;
                        CyU3PEventSet(&glMscAppEvent_STORAGE, CY_FX_MSC_SIBCB_EVENT_FLAG, CYU3P_EVENT_OR);
                        break;
                }

                if (glMscCmdStatus_STORAGE != 0)
                    break;
            }
            else
            {
                /* Page code must be zero if Vital Product Data is not being requested. */
                if (glMscCbwBuffer_STORAGE[CY_FX_SCSI_INQCMD_PCODE_OFFSET] != 0)
                {
                    glMscCmdStatus_STORAGE = 1;
                    glSensePtr_STORAGE = CY_FX_MSC_SENSE_INVALID_PARAMETER;
                    CyU3PEventSet(&glMscAppEvent_STORAGE, CY_FX_MSC_SIBCB_EVENT_FLAG, CYU3P_EVENT_OR);
                    break;
                }

                inqDataLen = CY_FX_SCSI_INQUIRY_DATALEN;
                inqData_p = (uint8_t*)CyFxMscScsiInquiryData_STORAGE;
            }

            if (!CyFxMscApplnCheckCbwParams_STORAGE(lun, CyFalse, CyTrue, CyFalse,
                inqDataLen, &dataLength))
                break;

            glMscState_STORAGE = CY_FX_MSC_STATE_DATA;
            status = CyFxMscApplnSendDataToHost_STORAGE(inqData_p, dataLength);

            if (status != CY_U3P_SUCCESS)
            {
                glMscCmdStatus_STORAGE = 1;
                glSensePtr_STORAGE = CY_FX_MSC_SENSE_CRC_ERROR;
                CyU3PEventSet(&glMscAppEvent_STORAGE, CY_FX_MSC_SIBCB_EVENT_FLAG, CYU3P_EVENT_OR);
            }
        }
        break;
        case HDD_SCSI_READ_CAPACITY_10:
            HddReadCapacity10(lun);
            break;

        case HDD_SCSI_SERVICE_ACTION_IN_16:
            HddServiceActionIn16(lun);
            break;

        case HDD_SCSI_READ_FORMAT_CAPACITY:
            HddReadFormatCapacity(lun);
            break;

        case CY_FX_MSC_SCSI_MODE_SENSE_6:
            HddModeSense(lun, CyFalse);
            break;

        case HDD_SCSI_MODE_SENSE_10:
            HddModeSense(lun, CyTrue);
            break;

        case CY_FX_MSC_SCSI_MODE_SELECT_6:
            HddModeSelect(lun, CyFalse);
            break;

        case HDD_SCSI_MODE_SELECT_10:
            HddModeSelect(lun, CyTrue);
            break;

        case CY_FX_MSC_SCSI_START_STOP_UNIT:
            HddStartStop(lun);
            break;

        case CY_FX_MSC_SCSI_PREVENT_ALLOW_MEDIUM:
            HddPreventAllow(lun);
            break;

        case CY_FX_MSC_SCSI_TEST_UNIT_READY:
            HddDone(lun, CyTrue);
            break;

        case CY_FX_MSC_SCSI_SEND_DIAG:
            HddDone(lun, CyFalse);
            break;

        case CY_FX_MSC_SCSI_READ_10:
            HddStreamRead(lun, HddGet32(2), HddGet16(7));
            break;

        case HDD_SCSI_READ_12:
            HddStreamRead(lun, HddGet32(2), HddGet32(6));
            break;

        case HDD_SCSI_READ_16:
            HddStreamRead(lun, HddGet64(2), HddGet32(10));
            break;

        case CY_FX_MSC_SCSI_WRITE_10:
            HddStreamWrite(lun, HddGet32(2), HddGet16(7));
            break;

        case HDD_SCSI_WRITE_12:
            HddStreamWrite(lun, HddGet32(2), HddGet32(6));
            break;

        case HDD_SCSI_WRITE_16:
            HddStreamWrite(lun, HddGet64(2), HddGet32(10));
            break;

        case CY_FX_MSC_SCSI_VERIFY_10:
            HddVerify(lun, HddGet32(2), HddGet16(7));
            break;

        case HDD_SCSI_VERIFY_16:
            HddVerify(lun, HddGet64(2), HddGet32(10));
            break;

        case CY_FX_MSC_SCSI_SYNCCACHE_10:
        case HDD_SCSI_SYNC_CACHE_16:
            HddSyncCache(lun);
            break;

        case HDD_SCSI_UNMAP:
            HddUnmap(lun);
            break;

        case HDD_SCSI_WRITE_SAME_10:
            HddWriteSame(lun, HddGet32(2), HddGet16(7));
            break;

        case HDD_SCSI_WRITE_SAME_16:
            HddWriteSame(lun, HddGet64(2), HddGet32(10));
            break;

        case CY_FX_MSC_SCSI_FORMAT_UNIT:
            HddFormatUnit(lun);
            break;

        case CY_FX_MSC_SCSI_REQUEST_SENSE:
        {
            if ((glMscCbwBuffer_STORAGE[CY_FX_SCSI_RQSCMD_DESC_OFFSET] & CY_FX_SCSI_REQSENSE_DESC) != 0)
            {
                glMscCmdStatus_STORAGE = 1;
                glSensePtr_STORAGE = CY_FX_MSC_SENSE_INVALID_FIELD_IN_CBW;
                CyU3PEventSet(&glMscAppEvent_STORAGE, CY_FX_MSC_SIBCB_EVENT_FLAG, CYU3P_EVENT_OR);
                break;
            }

            if (!CyFxMscApplnCheckCbwParams_STORAGE(lun, CyFalse, CyTrue, CyFalse, 18, &dataLength))
                break;

            CyU3PMemSet(glMscDataBuffer_STORAGE, 0, 18);
            glMscDataBuffer_STORAGE[0] = 0x70;
            glMscDataBuffer_STORAGE[2] = glReqSenseCode_STORAGE[glSensePtr_STORAGE][0];
            glMscDataBuffer_STORAGE[7] = 0x0A;
            glMscDataBuffer_STORAGE[12] = glReqSenseCode_STORAGE[glSensePtr_STORAGE][1];
            glMscDataBuffer_STORAGE[13] = glReqSenseCode_STORAGE[glSensePtr_STORAGE][2];

            glMscState_STORAGE = CY_FX_MSC_STATE_DATA;
            if (glSensePtr_STORAGE == CY_FX_MSC_SENSE_MEDIA_CHANGED)
                glSensePtr_STORAGE = CY_FX_MSC_SENSE_OK;

            status = CyFxMscApplnSendDataToHost_STORAGE(glMscDataBuffer_STORAGE, dataLength);
            if (status != CY_U3P_SUCCESS)
            {
                glMscCmdStatus_STORAGE = 1;
                glSensePtr_STORAGE = CY_FX_MSC_SENSE_CRC_ERROR;
                CyU3PEventSet(&glMscAppEvent_STORAGE, CY_FX_MSC_SIBCB_EVENT_FLAG, CYU3P_EVENT_OR);
            }
        }
        break;

        default:
        {
            /* Unsupported command. */
            CyU3PDebugPrint(4, "HDD: unsupported opcode 0x%x\r\n", cmd);
            glMscCmdStatus_STORAGE = 1;
            glSensePtr_STORAGE = CY_FX_MSC_SENSE_INVALID_OP_CODE;
            CyU3PEventSet(&glMscAppEvent_STORAGE, CY_FX_MSC_SIBCB_EVENT_FLAG, CYU3P_EVENT_OR);
        }
        break;
    }
}

void CyFxMscApplnSendCsw_STORAGE(void)
{
    CyU3PDmaBuffer_t dmaBuf;
    CyU3PReturnStatus_t status;

    /* Compute the CSW information. */
    glMscCswBuffer_STORAGE[0] = 'U';
    glMscCswBuffer_STORAGE[1] = 'S';
    glMscCswBuffer_STORAGE[2] = 'B';
    glMscCswBuffer_STORAGE[3] = 'S';
    CyU3PMemCopy(glMscCswBuffer_STORAGE + 4, glMscCbwBuffer_STORAGE + 4, 4);       /* Copy the tag. */
    *((uint32_t*)(glMscCswBuffer_STORAGE + 8)) = glMscResidue_STORAGE;
    glMscCswBuffer_STORAGE[12] = glMscCmdStatus_STORAGE;

    dmaBuf.buffer = glMscCswBuffer_STORAGE;
    dmaBuf.status = 0;
    dmaBuf.size = 32;
    dmaBuf.count = 13;
    status = CyU3PDmaChannelSetupSendBuffer(&glChHandleMscIn_STORAGE, &dmaBuf);
    if (status == CY_U3P_SUCCESS)
        glMscState_STORAGE = CY_FX_MSC_STATE_CSW;
    else
        CyU3PDmaChannelReset(&glChHandleMscIn_STORAGE);
}

void CyFxMscApplnUSBEventCB_STORAGE(CyU3PUsbEventType_t evtype, uint16_t evdata)
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
            CyU3PEventSet(&glMscAppEvent_STORAGE, CY_FX_MSC_RESET_EVENT_FLAG, CYU3P_EVENT_OR);

            CyFxMscApplnResetDatapath_STORAGE();
            glDevConfigured_STORAGE = CyFalse;

            glLunStopped_STORAGE = CyFalse;
        }
        break;

        case CY_U3P_USB_EVENT_SETCONF:
        {
            if (evdata == 0)
            {
                glDevConfigured_STORAGE = CyFalse;
                break;
            }

            //CyU3PDebugPrint(4, "CDROM USB Set Config completed\r\n");

            /* Check if Set Config event is handled */
            if ((glDevConfigured_STORAGE == CyFalse) && (glChannelsReady_STORAGE == CyTrue))
            {
                glDevConfigured_STORAGE = CyTrue;

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

                CyU3PDebugPrint(4, "HDD: USB speed %d (1 full, 2 high, 3 super), bulk packet size %d\r\n", CyU3PUsbGetSpeed(), epPktSize);

                /* Update the packet size for the endpoints based on connection speed. */
                CyU3PSetEpPacketSize(CY_FX_EP_STORAGE_OUT, epPktSize);
                CyU3PSetEpPacketSize(CY_FX_EP_STORAGE_IN, epPktSize);

                /* Clear stall on the endpoints and reset the DMA channels. */
                CyU3PUsbStall(CY_FX_EP_STORAGE_OUT, CyFalse, CyTrue);
                CyU3PUsbStall(CY_FX_EP_STORAGE_IN, CyFalse, CyTrue);

                CyU3PDmaChannelReset(&glChHandleMscIn_STORAGE);
                CyU3PDmaChannelReset(&glChHandleMscOut_STORAGE);

                /* Restore the LUN to an active state. */
                glLunStopped_STORAGE = CyFalse;

                /* Notify the thread to start waiting for a mass storage CBW. */
                CyU3PEventSet(&glMscAppEvent_STORAGE, CY_FX_MSC_SETCONF_EVENT_FLAG, CYU3P_EVENT_OR);
            }
        }
        break;

        default:
            break;
    }
}

CyBool_t CyFxMscApplnLPMRqtCB_STORAGE(CyU3PUsbLinkPowerMode link_mode)
{
    /* If we are in the middle of processing a mass storage command, keep reverting to U0 state. */
    return !glMscDriverBusy_STORAGE;
}

/* Create the DMA channels required for the mass storage device operation. */
void CyFxMscApplnDmaInit_STORAGE(void)
{
    CyU3PDmaChannelConfig_t dmaConfig;
    CyU3PReturnStatus_t status;

    //CyU3PDebugPrint(4, "HDD init memory\r\n");

    /* The scratch buffers are allocated once; the channels come and go with the disk interface. */
    if (glMscCbwBuffer_STORAGE == 0)
    {
        glMscCbwBuffer_STORAGE = (uint8_t*)CyU3PDmaBufferAlloc(1024);
        glMscCswBuffer_STORAGE = (uint8_t*)CyU3PDmaBufferAlloc(1024);
        glMscDataBuffer_STORAGE = (uint8_t*)CyU3PDmaBufferAlloc(1024);
    }
    if ((glMscCbwBuffer_STORAGE == 0) || (glMscCswBuffer_STORAGE == 0) || (glMscDataBuffer_STORAGE == 0))
    {
        //CyU3PDebugPrint(4, "Failed to allocate scratch buffer\r\n");
        CyFxAppErrorHandler(CY_U3P_ERROR_MEMORY_ERROR);
    }

    /* Both DMA channels are created with SuperSpeed parameters. The CyU3PSetEpPacketSize () API is used
       to reconfigure the endpoints to work with DMA channels with large buffers. */
    dmaConfig.size = 1024;
    dmaConfig.count = CY_FX_MSC_DMA_BUF_COUNT;
    dmaConfig.prodSckId = (CyU3PDmaSocketId_t)(CY_U3P_UIB_SOCKET_PROD_0 | CY_FX_EP_STORAGE_OUT_SOCKET);
    dmaConfig.consSckId = CY_U3P_CPU_SOCKET_CONS;
    dmaConfig.dmaMode = CY_U3P_DMA_MODE_BYTE;
    dmaConfig.notification = CY_U3P_DMA_CB_RECV_CPLT;
    dmaConfig.cb = CyFxMscApplnDmaCb_STORAGE;
    dmaConfig.prodHeader = 0;
    dmaConfig.prodFooter = 0;
    dmaConfig.consHeader = 0;
    dmaConfig.prodAvailCount = 0;

    status = CyU3PDmaChannelCreate(&glChHandleMscOut_STORAGE, CY_U3P_DMA_TYPE_MANUAL_IN, &dmaConfig);
    if (status != CY_U3P_SUCCESS)
    {
        //CyU3PDebugPrint(4, "DMA channel create failed, code=%d\r\n", status);
        CyFxAppErrorHandler(status);
    }

    dmaConfig.prodSckId = CY_U3P_CPU_SOCKET_PROD;
    dmaConfig.consSckId = (CyU3PDmaSocketId_t)(CY_U3P_UIB_SOCKET_CONS_0 | CY_FX_EP_STORAGE_IN_SOCKET);
    dmaConfig.notification = CY_U3P_DMA_CB_SEND_CPLT;
    dmaConfig.cb = CyFxMscApplnDmaCb_STORAGE;

    status = CyU3PDmaChannelCreate(&glChHandleMscIn_STORAGE, CY_U3P_DMA_TYPE_MANUAL_OUT, &dmaConfig);
    if (status != CY_U3P_SUCCESS)
    {
        //CyU3PDebugPrint(4, "DMA channel create failed, code=%d\r\n", status);
        CyFxAppErrorHandler(status);
    }

    glChannelsReady_STORAGE = CyTrue;
}

/* Destroys the DMA channels (the disk interface leaves the USB configuration, or the USB stack is restarted). */
void CyFxMscApplnDmaDeInit_STORAGE(void)
{
    if (glChannelsReady_STORAGE == CyFalse)
        return;

    glChannelsReady_STORAGE = CyFalse;
    glDevConfigured_STORAGE = CyFalse;
    glMscState_STORAGE = CY_FX_MSC_STATE_INACTIVE;
    CyU3PDmaChannelDestroy(&glChHandleMscOut_STORAGE);
    CyU3PDmaChannelDestroy(&glChHandleMscIn_STORAGE);
}

void CyFxMscAppInitVars_STORAGE(void)
{
    glLunState_STORAGE = CyTrue;
    glSensePtr_STORAGE = CY_FX_MSC_SENSE_DEVICE_RESET;
    glLunStopped_STORAGE = CyFalse;
}


void CyFxMscApplnDmaCb_STORAGE(
    CyU3PDmaChannel* handle,
    CyU3PDmaCbType_t   evtype,
    CyU3PDmaCBInput_t* input)
{
    CyU3PDmaBuffer_t dmaBuf = input->buffer_p;

    switch (evtype)
    {
        case CY_U3P_DMA_CB_RECV_CPLT:
        {
            if ((glMscState_STORAGE == CY_FX_MSC_STATE_CBW) || (glMscState_STORAGE == CY_FX_MSC_STATE_WAITING))
            {
                if ((dmaBuf.count != 31) || (dmaBuf.buffer[0] != 'U') || (dmaBuf.buffer[1] != 'S') ||
                    (dmaBuf.buffer[2] != 'B') || (dmaBuf.buffer[3] != 'C'))
                    glInPhaseError_STORAGE = CyTrue;
                else
                    glInPhaseError_STORAGE = CyFalse;
                CyU3PEventSet(&glMscAppEvent_STORAGE, CY_FX_MSC_CBW_EVENT_FLAG, CYU3P_EVENT_OR);
            }
            else
            {
                CyU3PEventSet(&glMscAppEvent_STORAGE, CY_FX_MSC_DATA_RECV_EVENT_FLAG, CYU3P_EVENT_OR);
            }
        }
        break;

        case CY_U3P_DMA_CB_SEND_CPLT:
            CyU3PEventSet(&glMscAppEvent_STORAGE, CY_FX_MSC_DATASENT_EVENT_FLAG, CYU3P_EVENT_OR);
            break;

        default:
            break;
    }
}

void CyFxMscAppHandleMscTask_STORAGE(void)
{
    CyU3PReturnStatus_t status;
    CyU3PDmaBuffer_t    dmaBuf;

    uint32_t evMask = CY_FX_MSC_SETCONF_EVENT_FLAG | CY_FX_MSC_RESET_EVENT_FLAG | CY_FX_MSC_CBW_EVENT_FLAG |
        CY_FX_MSC_DATASENT_EVENT_FLAG | CY_FX_MSC_SIBCB_EVENT_FLAG |
        CY_FX_MSC_DATA_RECV_EVENT_FLAG;
    uint32_t evStat;

    for (;;)
    {
        //CyU3PDebugPrint(4, "PROCESS?? CyFxMscAppHandleMscTask_STORAGE()\r\n");
        status = CyU3PEventGet(&glMscAppEvent_STORAGE, evMask, CYU3P_EVENT_OR_CLEAR, &evStat, CYU3P_WAIT_FOREVER);
        if (status != CY_U3P_SUCCESS)
        {
            //CyU3PDebugPrint(4, "NOT PROCESS CyFxMscAppHandleMscTask_STORAGE() %d\r\n", status);
        }
        if (status == CY_U3P_SUCCESS)
        {
            //CyU3PDebugPrint(4, "PROCESS CyFxMscAppHandleMscTask_STORAGE()\r\n");

            if (evStat & CY_FX_MSC_RESET_EVENT_FLAG)
            {
                glMscState_STORAGE = CY_FX_MSC_STATE_INACTIVE;
            }

            if (evStat & CY_FX_MSC_SETCONF_EVENT_FLAG)
                glMscState_STORAGE = CY_FX_MSC_STATE_CBW;

            if (evStat & CY_FX_MSC_CBW_EVENT_FLAG)
            {
                if (glInPhaseError_STORAGE)
                {
                    /* Phase error: Send an error CSW and then stall both endpoints. */
                    glMscCmdStatus_STORAGE = 2;
                    CyU3PUsbStall(CY_FX_EP_STORAGE_IN, CyTrue, CyFalse);
                    CyU3PUsbStall(CY_FX_EP_STORAGE_OUT, CyTrue, CyFalse);
                }
                else
                {
                    glMscDriverBusy_STORAGE = CyTrue;
                    CyU3PUsbLPMDisable();

                    /* Make sure the link is brought back to active state at this stage. */
                    if (CyU3PUsbGetSpeed() == CY_U3P_SUPER_SPEED)
                        CyU3PUsbSetLinkPowerState(CyU3PUsbLPM_U0);
                    else
                        CyU3PUsb2Resume();

                    /* Parse CBW here. */
                    glMscState_STORAGE = CY_FX_MSC_STATE_DATA;
                    CyFxMscApplnParseCbw_STORAGE();
                }
            }

            if (evStat & CY_FX_MSC_DATA_RECV_EVENT_FLAG)
            {
                /* Data received for Mode Select command. */
                status = CyU3PDmaChannelWaitForRecvBuffer(&glChHandleMscOut_STORAGE, &dmaBuf, CYU3P_NO_WAIT);
                //CyU3PDebugPrint(4, "WaitForRecv %d\r\n", status);
                if (status == CY_U3P_SUCCESS)
                {
                    glMscCmdStatus_STORAGE = 0;
                }
                else
                {
                    glMscCmdStatus_STORAGE = 1;
                    glSensePtr_STORAGE = CY_FX_MSC_SENSE_CRC_ERROR;
                }

                glMscResidue_STORAGE = 0;
                CyU3PEventSet(&glMscAppEvent_STORAGE, CY_FX_MSC_DATASENT_EVENT_FLAG, CYU3P_EVENT_OR);
            }

            if (evStat & CY_FX_MSC_DATASENT_EVENT_FLAG)
            {
                /* If CSW has been sent, go to CBW state. */
                if (glMscState_STORAGE == CY_FX_MSC_STATE_CSW)
                    glMscState_STORAGE = CY_FX_MSC_STATE_CBW;

                /* If data has been sent, go to CSW state. */
                if (glMscState_STORAGE == CY_FX_MSC_STATE_DATA)
                {
                    /* If we sent less data than the host wanted, stall the endpoint afterwards. */
                    if ((glMscResidue_STORAGE != 0) && (glMscCmdStatus_STORAGE == 0))
                        CyU3PUsbStall(CY_FX_EP_STORAGE_IN, CyTrue, CyFalse);

                    glMscState_STORAGE = CY_FX_MSC_STATE_STATUS;
                }
            }

            if (evStat & CY_FX_MSC_SIBCB_EVENT_FLAG)
            {
                /* Stall the endpoint if any data transfer is expected. */
                if ((glMscCmdStatus_STORAGE) && (glMscResidue_STORAGE))
                {
                    /* The DMA channel has already been reset. */
                    if (glCmdDirection_STORAGE)
                        CyU3PUsbStall(CY_FX_EP_STORAGE_IN, CyTrue, CyFalse);
                    else
                        CyU3PUsbStall(CY_FX_EP_STORAGE_OUT, CyTrue, CyFalse);
                }
                else
                {
                    /* Wait for all of the data to be transferred to the host in the case of a read. */
                    if (glCmdDirection_STORAGE)
                        CyU3PDmaChannelWaitForCompletion(&glChHandleMscIn_STORAGE, CYU3P_WAIT_FOREVER);
                }

                /* Now move to the STATUS state to try and send the CSW. */
                glMscState_STORAGE = CY_FX_MSC_STATE_STATUS;
            }

            if (glMscState_STORAGE == CY_FX_MSC_STATE_CBW)
            {
                /* The command has been handled. Link power management stays OFF (U1 / U2 are refused, see CyFxMscApplnLPMRqtCB in main.c). */
                glMscDriverBusy_STORAGE = CyFalse;

                dmaBuf.buffer = glMscCbwBuffer_STORAGE;
                dmaBuf.status = 0;
                dmaBuf.count = 0;
                dmaBuf.size = 64;     /* CBW is expected to be smaller than 64 bytes. */
                status = CyU3PDmaChannelSetupRecvBuffer(&glChHandleMscOut_STORAGE, &dmaBuf);
                if (status == CY_U3P_SUCCESS)
                {
                    glMscState_STORAGE = CY_FX_MSC_STATE_WAITING;

                    /* Make sure the link is brought back to active state at this stage. */
                    if (CyU3PUsbGetSpeed() == CY_U3P_SUPER_SPEED)
                        CyU3PUsbSetLinkPowerState(CyU3PUsbLPM_U0);
                    else
                        CyU3PUsb2Resume();
                }
                else
                    CyU3PDmaChannelReset(&glChHandleMscOut_STORAGE);
            }

            if (glMscState_STORAGE == CY_FX_MSC_STATE_STATUS)
            {
                CyFxMscApplnSendCsw_STORAGE();
            }
        }
    }
}

/*
 * Entry function for the mscAppThread
 */
void MscAppThread_Entry_STORAGE(uint32_t input)
{
    uint32_t txApiRetStatus;

    /* Create FX MSC events */
    txApiRetStatus = CyU3PEventCreate(&glMscAppEvent_STORAGE);
    if (txApiRetStatus != 0)
    {
        /* Error Handling */
        //CyU3PDebugPrint(4, "MSC CDROM   Appln Create Event Failed, Error Code = %d\n", txApiRetStatus);
        CyFxAppErrorHandler(CY_U3P_ERROR_MEMORY_ERROR);
    }

    /* Initialize global variables. */
    CyFxMscAppInitVars_STORAGE();

    //CyU3PDebugPrint(4, "MSC CDROM   Appln enter loop\r\n");

    /* Do the MSC application main work. */
    CyFxMscAppHandleMscTask_STORAGE();
}

#endif /* CLIENT_WITH_HDD */
