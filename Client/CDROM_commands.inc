/* ---- The CD-ROM command set (MMC) on top of the media model (CDMEDIA.c). Included by CDROM.c before the CBW parser. -------------- */

/* CDB byte n of the command in the CBW (the CDB starts at byte 15 of the CBW). */
#define CDB(Index)                       (glMscCbwBuffer_CDROM[15 + (Index)])

#define CDROM_SCSI_MODE_SENSE_10         (0x5A)
#define CDROM_SCSI_READ_TOC              (0x43)
#define CDROM_SCSI_GET_EVENT_STATUS      (0x4A)
#define CDROM_SCSI_READ_DISC_INFO        (0x51)
#define CDROM_SCSI_READ_12               (0xA8)
#define CDROM_SCSI_READ_CD_MSF           (0xB9)
#define CDROM_SCSI_SET_CD_SPEED          (0xBB)
#define CDROM_SCSI_READ_CD               (0xBE)

#define CDROM_COOKED_SECTOR_BYTES        (2048)
#define CDROM_RAW_SECTOR_BYTES           (2352)
#define CDROM_CHUNK_BYTES                (32768)     /* Largest piece handed to the USB DMA channel at once. */
#define CDROM_USB_TIMEOUT_MS             (10000)     /* The OS stopped taking data of a read. */

/* Ends the command with CHECK CONDITION and the given sense (any expected data phase is stalled by the SIBCB handling). */
static void CdRomFail (uint8_t Sense)
{
    glMscCmdStatus_CDROM = 1;
    glSensePtr_CDROM = Sense;
    CyU3PEventSet(&glMscAppEvent_CDROM, CY_FX_MSC_SIBCB_EVENT_FLAG, CYU3P_EVENT_OR);
}

/* Sends Length bytes of the scratch buffer to the OS, cut to the allocation length of the CDB. */
static void CdRomRespond (uint8_t Lun, CyBool_t NeedMedia, uint32_t Length, uint32_t AllocLength)
{
    CyU3PReturnStatus_t Status;
    uint32_t DataLength;

    if (Length > AllocLength)
        Length = AllocLength;

    if (CyFxMscApplnCheckCbwParams(Lun, NeedMedia, CyTrue, CyFalse, Length, &DataLength) == CyFalse)
        return;

    glMscState_CDROM = CY_FX_MSC_STATE_DATA;
    Status = CyFxMscApplnSendDataToHost_CDROM(CdMediaScratch(), DataLength);
    if (Status != CY_U3P_SUCCESS)
        CdRomFail(CY_FX_MSC_SENSE_CRC_ERROR);
}

/* A command without a data phase succeeds. */
static void CdRomDone(uint8_t Lun, CyBool_t NeedMedia)
{
    uint32_t DataLength;

    if (CyFxMscApplnCheckCbwParams(Lun, NeedMedia, CyFalse, CyFalse, 0, &DataLength) == CyFalse)
        return;

    glMscState_CDROM = CY_FX_MSC_STATE_STATUS;
}

/* For the commands that read from the medium: fails the command the usual way (NOT READY / MEDIA CHANGED) when there is none. */
static CyBool_t CdRomMediumReady(uint8_t Lun)
{
    uint32_t Saved = glMscResidue_CDROM;
    uint32_t DataLength;
    CyBool_t Ready;

    Ready = CyFxMscApplnCheckCbwParams(Lun, CyTrue, CyTrue, CyFalse, 8, &DataLength);

    /* The check takes the bytes it expects off the residue; the real response checks again, so give them back. */
    glMscResidue_CDROM = Saved;
    return (Ready);
}

static void CdRomPut32(uint8_t *Buffer, uint32_t Value)
{
    Buffer[0] = (uint8_t)(Value >> 24);
    Buffer[1] = (uint8_t)(Value >> 16);
    Buffer[2] = (uint8_t)(Value >> 8);
    Buffer[3] = (uint8_t)Value;
}

static uint32_t CdRomGet16(uint32_t Index)
{
    return (((uint32_t)CDB(Index) << 8) | CDB(Index + 1));
}

static uint32_t CdRomGet32(uint32_t Index)
{
    return (((uint32_t)CDB(Index) << 24) | ((uint32_t)CDB(Index + 1) << 16) | ((uint32_t)CDB(Index + 2) << 8) | CDB(Index + 3));
}

/* READ CAPACITY(10): the last LBA and the block length. */
static void CdRomReadCapacity(uint8_t Lun)
{
    uint8_t *Buffer = CdMediaScratch();
    uint32_t LeadOut = CdMediaLeadOutLba();

    if (CdRomMediumReady(Lun) == CyFalse)
        return;

    CdRomPut32(Buffer, (LeadOut > 0) ? (LeadOut - 1) : 0);
    CdRomPut32(Buffer + 4, CDROM_COOKED_SECTOR_BYTES);
    CdRomRespond(Lun, CyTrue, 8, 8);
}

/* READ TOC/PMA/ATIP: formats 0, 1 and 2 from the blobs the PC built. */
static void CdRomReadToc(uint8_t Lun)
{
    uint8_t *Buffer = CdMediaScratch();
    const uint8_t *Blob;
    uint32_t BlobLength;
    uint32_t Format = CDB(2) & 0x0F;
    CyBool_t Msf = ((CDB(1) & 0x02) != 0) ? CyTrue : CyFalse;
    uint32_t StartTrack = CDB(6);
    uint32_t Alloc = CdRomGet16(7);
    uint32_t Kept;
    uint32_t Index;
    uint32_t Out;
    uint8_t Kind;

    if (CdRomMediumReady(Lun) == CyFalse)
        return;

    /* Older callers put the format into the two top bits of byte 9. */
    if ((Format == 0) && ((CDB(9) >> 6) != 0))
        Format = CDB(9) >> 6;

    if (Format == 0)
        Kind = (Msf == CyTrue) ? CDM_BLOB_TOC_MSF : CDM_BLOB_TOC_LBA;
    else if (Format == 1)
        Kind = (Msf == CyTrue) ? CDM_BLOB_SESSION_MSF : CDM_BLOB_SESSION_LBA;
    else if (Format == 2)
        Kind = CDM_BLOB_FULL_TOC;
    else
    {
        CdRomFail(CY_FX_MSC_SENSE_INVALID_FIELD_IN_CBW);
        return;
    }

    Blob = CdMediaGetBlob(Kind, &BlobLength);
    if ((Blob == NULL) || (BlobLength > 4096) || (BlobLength < 4))
    {
        CdRomFail(CY_FX_MSC_SENSE_INVALID_FIELD_IN_CBW);
        return;
    }

    CyU3PMemCopy(Buffer, (uint8_t *)Blob, BlobLength);

    /* Format 0 from a starting track on: keep the descriptors of that track and the following ones, and the lead-out. */
    if ((Format == 0) && (StartTrack != 0))
    {
        if ((StartTrack != 0xAA) && ((StartTrack < Buffer[2]) || (StartTrack > Buffer[3])))
        {
            CdRomFail(CY_FX_MSC_SENSE_INVALID_FIELD_IN_CBW);
            return;
        }

        Kept = 0;
        Out = 4;
        for (Index = 4; (Index + 8) <= BlobLength; Index += 8)
        {
            if (Buffer[Index + 2] >= StartTrack)
            {
                if (Out != Index)
                    CyU3PMemCopy(Buffer + Out, Buffer + Index, 8);
                Out += 8;
                Kept++;
            }
        }

        BlobLength = 4 + (8 * Kept);
        Buffer[0] = (uint8_t)((BlobLength - 2) >> 8);
        Buffer[1] = (uint8_t)(BlobLength - 2);
    }

    CdRomRespond(Lun, CyTrue, BlobLength, Alloc);
}

/* READ DISC INFORMATION. */
static void CdRomReadDiscInfo(uint8_t Lun)
{
    const uint8_t *Blob;
    uint32_t BlobLength;

    if (CdRomMediumReady(Lun) == CyFalse)
        return;

    Blob = CdMediaGetBlob(CDM_BLOB_DISC_INFO, &BlobLength);
    if ((Blob == NULL) || (BlobLength > 4096))
    {
        CdRomFail(CY_FX_MSC_SENSE_INVALID_FIELD_IN_CBW);
        return;
    }

    CyU3PMemCopy(CdMediaScratch(), (uint8_t *)Blob, BlobLength);
    CdRomRespond(Lun, CyTrue, BlobLength, CdRomGet16(7));
}

/* GET CONFIGURATION: a fixed answer, with or without a disc. The table after the 8 byte header is not a proper
   profile list on purpose: Windows reads the entries as features (0x0040 = BD Read first) and therefore shows the drive as a BD-ROM drive. */
static const uint8_t CdRomGetConfigTable[28] =
{
    0x00, 0x00, 0x00, 0x18,                             /* Length (BE) */
    0x00, 0x00,                                         /* Reserved */
    0x00, 0x00,                                         /* Current profile (0) */

    0x00, 0x40, 0x00, 0x00,                             /* BD ROM */
    0x00, 0x10, 0x00, 0x00,                             /* DVD ROM */
    0x00, 0x08, 0x00, 0x00,                             /* CD ROM */
    0x00, 0x02, 0x00, 0x00,                             /* Removable disk */

    0x00, 0x01,                                         /* Feature: core */
    0x0B,                                               /* Version */
    0x00                                                /* Additional length */
};

static void CdRomGetConfiguration(uint8_t Lun)
{
    uint8_t *Buffer = CdMediaScratch();
    uint32_t Request = CDB(1) & 0x03;
    uint32_t Alloc = CdRomGet16(7);
    uint32_t Length = sizeof(CdRomGetConfigTable);

    CyU3PMemCopy(Buffer, (uint8_t *)CdRomGetConfigTable, Length);
    if (Request == 2)
    {
        /* Single feature request: same quirk as the original, byte for byte. */
        Buffer[9] = (uint8_t)(sizeof(CdRomGetConfigTable) >> 24);
        Buffer[10] = (uint8_t)(sizeof(CdRomGetConfigTable) >> 16);
        Buffer[11] = (uint8_t)(sizeof(CdRomGetConfigTable) >> 8);
        Buffer[12] = (uint8_t)(sizeof(CdRomGetConfigTable));
        Length = 12;
    }

    CdRomRespond(Lun, CyFalse, Length, Alloc);
}

/* GET EVENT STATUS NOTIFICATION (polled): the media class tells the OS about insertions and removals. */
static void CdRomGetEventStatus(uint8_t Lun)
{
    uint8_t *Buffer = CdMediaScratch();
    uint32_t Alloc = CdRomGet16(7);
    uint32_t Length;

    if ((CDB(1) & 0x01) == 0)
    {
        CdRomFail(CY_FX_MSC_SENSE_INVALID_FIELD_IN_CBW);
        return;
    }

    CyU3PMemSet(Buffer, 0, 8);
    Buffer[3] = 0x10;                                   /* Supported event classes: media. */

    if ((CDB(4) & 0x10) == 0)
    {
        Buffer[1] = 2;
        Buffer[2] = 0x80;                               /* No event class requested that we know: NEA. */
        Length = 4;
    }
    else
    {
        Buffer[1] = 6;
        Buffer[2] = 0x04;                               /* Media class. */
        Buffer[4] = CdMediaTakeMediaEvent();
        Buffer[5] = (CdMediaIsPresent() == CyTrue) ? 0x02 : 0x00;
        Length = 8;
    }

    CdRomRespond(Lun, CyFalse, Length, Alloc);
}

/* MODE SENSE(6) and (10): the CD capabilities page (0x2A) and an empty error recovery page (0x01). */
static void CdRomModeSense(uint8_t Lun, CyBool_t Ten)
{
    uint8_t *Buffer = CdMediaScratch();
    uint32_t Page = CDB(2) & 0x3F;
    uint32_t Alloc = (Ten == CyTrue) ? CdRomGet16(7) : CDB(4);
    uint32_t HeaderLength = (Ten == CyTrue) ? 8 : 4;
    uint32_t Length = HeaderLength;
    uint32_t Index;

    CyU3PMemSet(Buffer, 0, 64);

    if ((Page == 0x01) || (Page == 0x3F))
    {
        Buffer[Length] = 0x01;
        Buffer[Length + 1] = 0x0A;
        Length += 12;
    }

    if ((Page == 0x2A) || (Page == 0x3F))
    {
        Buffer[Length] = 0x2A;
        Buffer[Length + 1] = 0x12;
        Buffer[Length + 4] = 0x71;                      /* Audio play, Mode 2 form 1 and 2, multi-session. */
        Buffer[Length + 5] = 0x03;                      /* CD-DA commands, stream accurate. */
        Buffer[Length + 6] = 0x29 | ((CdMediaIsLocked() == CyTrue) ? 0x02 : 0x00);   /* Tray, eject, lock supported, lock state. */
        Buffer[Length + 8] = 0x1B;                      /* Maximum read speed 7056 KB/s. */
        Buffer[Length + 9] = 0x90;
        Buffer[Length + 10] = 0x01;                     /* 256 volume levels. */
        Buffer[Length + 12] = 0x04;                     /* 1 MB buffer. */
        Buffer[Length + 14] = 0x1B;                     /* Current read speed. */
        Buffer[Length + 15] = 0x90;
        Length += 20;
    }

    if (Length == HeaderLength)
    {
        CdRomFail(CY_FX_MSC_SENSE_INVALID_FIELD_IN_CBW);
        return;
    }

    if (Ten == CyTrue)
    {
        Buffer[0] = (uint8_t)((Length - 2) >> 8);
        Buffer[1] = (uint8_t)(Length - 2);
    }
    else
    {
        Buffer[0] = (uint8_t)(Length - 1);
    }

    /* Medium type: 0x01 data CD, 0x02 audio CD, 0x03 mixed, 0 when empty. */
    Index = (CdMediaIsPresent() == CyTrue) ? (CdMediaDiscType() == 0 ? 1 : (CdMediaDiscType() == 1 ? 2 : 3)) : 0;
    Buffer[(Ten == CyTrue) ? 2 : 1] = (uint8_t)Index;

    CdRomRespond(Lun, CyFalse, Length, Alloc);
}

/* START STOP UNIT: ejecting (LoEj, no Start) is the OS asking the PC to give the disc back. */
static void CdRomStartStop(uint8_t Lun)
{
    uint32_t DataLength;

    if (CyFxMscApplnCheckCbwParams(Lun, CyFalse, CyFalse, CyFalse, 0, &DataLength) == CyFalse)
        return;

    if (((CDB(4) & 0x02) != 0) && ((CDB(4) & 0x01) == 0))
    {
        if (CdMediaEject() == CyFalse)
        {
            CdRomFail(CY_FX_MSC_SENSE_CANT_EJECT);
            return;
        }
    }

    glMscState_CDROM = CY_FX_MSC_STATE_STATUS;
}

/* PREVENT ALLOW MEDIUM REMOVAL. */
static void CdRomPreventAllow(uint8_t Lun)
{
    uint32_t DataLength;

    if (CyFxMscApplnCheckCbwParams(Lun, CyFalse, CyFalse, CyFalse, 0, &DataLength) == CyFalse)
        return;

    CdMediaSetLock(((CDB(4) & 0x01) != 0) ? CyTrue : CyFalse);
    glMscState_CDROM = CY_FX_MSC_STATE_STATUS;
}

/* Maps the status of the PC's COMPLETE message to the sense the OS gets. */
static uint8_t CdRomSenseForStatus(uint8_t Status)
{
    switch (Status)
    {
        case CDM_STATUS_NO_MEDIUM:
            return (CY_FX_MSC_SENSE_NO_MEDIA);
        case CDM_STATUS_OUT_OF_RANGE:
            return (CY_FX_MSC_SENSE_INVALID_LBA);
        case CDM_STATUS_BAD_FORMAT:
            return (CDROM_SENSE_ILLEGAL_MODE_FOR_TRACK);
        case CDM_STATUS_FAILED:
            return (CDROM_SENSE_TARGET_FAILURE);
        default:
            return (CY_FX_MSC_SENSE_READ_ERROR);
    }
}

#ifndef CDROM_SCSI_READ_SUB_CHANNEL
#define CDROM_SCSI_READ_SUB_CHANNEL      (0x42)
#endif
#ifndef CDROM_SCSI_READ_HEADER
#define CDROM_SCSI_READ_HEADER           (0x44)
#endif
#ifndef CDROM_SCSI_MODE_SELECT_10
#define CDROM_SCSI_MODE_SELECT_10        (0x55)
#endif

/* READ SUB-CHANNEL: audio is only ever read digitally (no analogue playback), so nothing is "playing": the audio status says there is nothing to
 * report, and the position is the start of the first track. MCN and ISRC are not on the image, so they are reported as not valid. */
static void CdRomReadSubChannel(uint8_t Lun)
{
    uint8_t *Buffer = CdMediaScratch();
    CyBool_t Msf = ((CDB(1) & 0x02) != 0) ? CyTrue : CyFalse;
    CyBool_t SubQ = ((CDB(2) & 0x40) != 0) ? CyTrue : CyFalse;
    uint32_t Format = CDB(3);
    uint32_t Alloc = CdRomGet16(7);
    uint32_t Length = 4;

    if (CdRomMediumReady(Lun) == CyFalse)
        return;

    if ((SubQ == CyTrue) && ((Format < 1) || (Format > 3)))
    {
        CdRomFail(CY_FX_MSC_SENSE_INVALID_FIELD_IN_CBW);
        return;
    }

    CyU3PMemSet(Buffer, 0, 32);
    Buffer[1] = 0x15;                                   /* audio status: no current status to return */
    if (SubQ == CyTrue)
    {
        Buffer[4] = (uint8_t)Format;
        if (Format == 1)
        {
            Buffer[5] = 0x14;                           /* ADR 1, data track */
            Buffer[6] = 1;                              /* track 1 */
            Buffer[7] = 1;                              /* index 1 */
            if (Msf == CyTrue)
                Buffer[10] = 2;                         /* absolute address 00:02:00 (LBA 0); the relative address stays 0 */
            Length = 16;
        }
        else
        {
            Buffer[6] = (Format == 3) ? 1 : 0;
            Length = 24;                                /* MCN / ISRC block, MCVal / TCVal = 0: not valid */
        }
    }

    Buffer[2] = (uint8_t)((Length - 4) >> 8);
    Buffer[3] = (uint8_t)(Length - 4);
    CdRomRespond(Lun, CyTrue, Length, Alloc);
}

/* READ HEADER: data mode 1 and the address of the sector. */
static void CdRomReadHeader(uint8_t Lun)
{
    uint8_t *Buffer = CdMediaScratch();
    CyBool_t Msf = ((CDB(1) & 0x02) != 0) ? CyTrue : CyFalse;
    uint32_t Lba = CdRomGet32(2);
    uint32_t Alloc = CdRomGet16(7);
    uint32_t Frames;

    if (CdRomMediumReady(Lun) == CyFalse)
        return;

    if (Lba >= CdMediaLeadOutLba())
    {
        CdRomFail(CY_FX_MSC_SENSE_INVALID_LBA);
        return;
    }

    CyU3PMemSet(Buffer, 0, 8);
    Buffer[0] = 1;
    if (Msf == CyTrue)
    {
        Frames = Lba + 150;
        Buffer[5] = (uint8_t)(Frames / (75 * 60));
        Buffer[6] = (uint8_t)((Frames / 75) % 60);
        Buffer[7] = (uint8_t)(Frames % 75);
    }
    else
        CdRomPut32(Buffer + 4, Lba);

    CdRomRespond(Lun, CyTrue, 8, Alloc);
}

/* MODE SELECT(6/10): this drive has nothing the OS could change; an empty parameter list is fine, a list is refused (the data phase is stalled). */
static void CdRomModeSelect(uint8_t Lun, CyBool_t Ten)
{
    uint32_t ParameterBytes = (Ten == CyTrue) ? CdRomGet16(7) : CDB(4);

    if (ParameterBytes == 0)
    {
        CdRomDone(Lun, CyFalse);
        return;
    }

    CdRomFail(CY_FX_MSC_SENSE_INVALID_FIELD_IN_CBW);
}

/* Reads Count sectors from the PC and streams them to the OS. Every sector goes through the PC; the only buffer is the ring in CDMEDIA.c.
 * Pieces handed to USB are multiples of 1024 bytes except the last, so no short packet ends the transfer early. */
static void CdRomStreamRead(uint8_t Lun, uint32_t Lba, uint32_t Count, uint8_t Format)
{
    CyU3PReturnStatus_t Status;
    uint32_t SectorBytes = (Format == CDM_FORMAT_RAW) ? CDROM_RAW_SECTOR_BYTES : CDROM_COOKED_SECTOR_BYTES;
    uint32_t Total = Count * SectorBytes;
    uint32_t Sent = 0;
    uint32_t Chunk;
    uint32_t Events;
    uint32_t LastProgress;
    uint8_t *Pointer;
    uint8_t Sense = CY_FX_MSC_SENSE_READ_ERROR;
    CyBool_t Failed = CyFalse;
    CyBool_t Cancel = CyFalse;
    CyBool_t UsbReset = CyFalse;
    CyBool_t StallReported = CyFalse;
    uint32_t LeadOut = CdMediaLeadOutLba();

    if (CyFxMscApplnCheckCbwParams(Lun, CyTrue, CyTrue, CyTrue, Total, &Events) == CyFalse)
        return;

    if (Count == 0)
    {
        glMscState_CDROM = CY_FX_MSC_STATE_STATUS;
        return;
    }

    if ((Lba >= LeadOut) || (Count > (LeadOut - Lba)))
    {
        CdRomFail(CY_FX_MSC_SENSE_INVALID_LBA);
        return;
    }

    if (CdMediaReadBegin(Lba, Count, Format, Total) == CyFalse)
    {
        CdRomFail(CY_FX_MSC_SENSE_NO_MEDIA);
        return;
    }

    glMscState_CDROM = CY_FX_MSC_STATE_DATA;
    LastProgress = CyU3PGetTime();

    while ((Failed == CyFalse) && (UsbReset == CyFalse) && (Sent < Total))
    {
        Chunk = CdMediaReadPeek(&Pointer, CDROM_CHUNK_BYTES);
        if (Chunk > (Total - Sent))
            Chunk = Total - Sent;
        if (Chunk < (Total - Sent))
            Chunk &= ~((uint32_t)1023);

        if (Chunk != 0)
        {
            Status = CyFxMscApplnSendDataToHost_CDROM(Pointer, Chunk);
            if (Status != CY_U3P_SUCCESS)
            {
                Sense = CY_FX_MSC_SENSE_CRC_ERROR;
                Failed = CyTrue;
                Cancel = CyTrue;
                continue;
            }

            Status = CyU3PEventGet(&glMscAppEvent_CDROM, CY_FX_MSC_DATASENT_EVENT_FLAG | CY_FX_MSC_RESET_EVENT_FLAG, CYU3P_EVENT_OR_CLEAR, &Events,
                    CDROM_USB_TIMEOUT_MS);
            if (Status != CY_U3P_SUCCESS)
            {
                /* The OS stopped reading mid-transfer. */
                CyU3PDmaChannelReset(&glChHandleMscIn_CDROM);
                Sense = CDROM_SENSE_TARGET_FAILURE;
                Failed = CyTrue;
                Cancel = CyTrue;
                continue;
            }

            if ((Events & CY_FX_MSC_RESET_EVENT_FLAG) != 0)
            {
                CyU3PEventSet(&glMscAppEvent_CDROM, CY_FX_MSC_RESET_EVENT_FLAG, CYU3P_EVENT_OR);
                UsbReset = CyTrue;
                Cancel = CyTrue;
                continue;
            }

            CdMediaReadConsume(Chunk);
            Sent += Chunk;
            LastProgress = CyU3PGetTime();
            continue;
        }

        /* Nothing can be sent yet. */
        if (CdMediaReadIsComplete() == CyTrue)
        {
            /* The PC has said everything it will say and it was not enough. */
            Sense = CdRomSenseForStatus(CdMediaReadStatus());
            Failed = CyTrue;
            continue;
        }

        Events = CdMediaReadWait(100);
        if ((Events & CDM_READ_ABORT) != 0)
        {
            Sense = (CdMediaIsPresent() == CyTrue) ? CY_FX_MSC_SENSE_READ_ERROR : CY_FX_MSC_SENSE_NO_MEDIA;
            Failed = CyTrue;
            continue;
        }

        if ((glDevConfigured_CDROM == CyFalse) ||
            (CyU3PEventGet(&glMscAppEvent_CDROM, CY_FX_MSC_RESET_EVENT_FLAG, CYU3P_EVENT_OR, &Events, CYU3P_NO_WAIT) == CY_U3P_SUCCESS))
        {
            UsbReset = CyTrue;
            Cancel = CyTrue;
            continue;
        }

        if (((CyU3PGetTime() - LastProgress) > 3000) && (StallReported == CyFalse))
        {
            StallReported = CyTrue;
            CyU3PDebugPrint(4, "CD-ROM: waiting 3 s for data, LBA %d count %d, sent %d of %d, received %d, complete %d\r\n",
                    Lba, Count, Sent, Total, CdMediaReadReceived(), CdMediaReadIsComplete());
            GpifLinkPrintState();
        }

        if ((CyU3PGetTime() - LastProgress) > CDM_READ_TIMEOUT_MS)
        {
            CyU3PDebugPrint(4, "CD-ROM: no answer from the PC for %d ms, reading LBA %d failed\r\n", CDM_READ_TIMEOUT_MS, Lba);
            Sense = CY_FX_MSC_SENSE_READ_ERROR;
            Failed = CyTrue;
            Cancel = CyTrue;
        }
    }

    CdMediaReadEnd(Cancel);

    if (UsbReset == CyTrue)
    {
        /* The reset handling of the main loop takes over; no status is sent for a command the OS has abandoned. */
        glMscState_CDROM = CY_FX_MSC_STATE_INACTIVE;
        return;
    }

    if (Failed == CyTrue)
    {
        CyU3PDebugPrint(4, "CD-ROM: read of %d sectors at LBA %d failed, sense %d, sent %d of %d bytes, received %d, complete %d status %d\r\n",
                Count, Lba, Sense, Sent, Total, CdMediaReadReceived(), CdMediaReadIsComplete(), CdMediaReadStatus());
        glMscResidue_CDROM = Total - Sent;
        CdRomFail(Sense);
        return;
    }

    glMscResidue_CDROM = 0;
    glMscCmdStatus_CDROM = 0;
    glMscState_CDROM = CY_FX_MSC_STATE_STATUS;
}

/* READ CD: the main channel selection bits (CDB byte 9) decide between cooked user data and the whole 2352 byte sector. */
static void CdRomReadCd(uint8_t Lun, uint32_t Lba, uint32_t Count)
{
    uint32_t SectorType = (CDB(1) >> 2) & 0x07;
    uint32_t Channels = CDB(9) & 0xF8;

    if (CDB(10) != 0)
    {
        CdRomFail(CY_FX_MSC_SENSE_INVALID_FIELD_IN_CBW);                 /* Sub-channel data is not available. */
        return;
    }

    if (((SectorType == 1) && ((Channels & 0x10) != 0)) || (Channels == 0xF8))
        CdRomStreamRead(Lun, Lba, Count, CDM_FORMAT_RAW);
    else if (Channels == 0x10)
        CdRomStreamRead(Lun, Lba, Count, CDM_FORMAT_USER_DATA);
    else
        CdRomFail(CY_FX_MSC_SENSE_INVALID_FIELD_IN_CBW);
}

/* Absolute MSF (minutes, seconds, frames) to the LBA; the disc starts at 00:02:00. */
static uint32_t CdRomMsfToLba(uint32_t Minutes, uint32_t Seconds, uint32_t Frames)
{
    uint32_t Absolute = (((Minutes * 60) + Seconds) * 75) + Frames;

    return ((Absolute >= 150) ? (Absolute - 150) : 0);
}
