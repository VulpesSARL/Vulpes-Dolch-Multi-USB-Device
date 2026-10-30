/* ---- The disk command set (SBC) on top of the media model (HDDMEDIA.c). Included by STORAGE.c before the CBW parser. ---------------- */

/* CDB byte n of the command in the CBW (the CDB starts at byte 15 of the CBW). */
#define CDB(Index)                       (glMscCbwBuffer_STORAGE[15 + (Index)])

#define HDD_SCSI_READ_16                 (0x88)
#define HDD_SCSI_WRITE_16                (0x8A)
#define HDD_SCSI_VERIFY_16               (0x8F)
#define HDD_SCSI_SYNC_CACHE_16           (0x91)
#define HDD_SCSI_WRITE_SAME_16           (0x93)
#define HDD_SCSI_SERVICE_ACTION_IN_16    (0x9E)
#define HDD_SCSI_READ_12                 (0xA8)
#define HDD_SCSI_WRITE_12                (0xAA)
#define HDD_SCSI_WRITE_SAME_10           (0x41)
#define HDD_SCSI_UNMAP                   (0x42)
#define HDD_SCSI_MODE_SELECT_10          (0x55)
#define HDD_SCSI_MODE_SENSE_10           (0x5A)
#define HDD_SCSI_READ_CAPACITY_10        (0x25)
#define HDD_SCSI_READ_FORMAT_CAPACITY    (0x23)

#define HDD_CHUNK_BYTES                  (32768)     /* Largest piece handed to the USB DMA channel at once (the size of the ring). */
#define HDD_USB_TIMEOUT_MS               (10000)     /* The OS stopped taking or sending data of a command. */
#define HDD_LONG_TIMEOUT_MS              (600000)    /* UNMAP / FORMAT of a big disk: the PC may need minutes, it is alive all that time. */
#define HDD_MAX_COUNT                    (0x7FFFFF)  /* Largest sector count of one read or write (4 GB), keeps the byte count in 32 bits. */
#define HDD_UNMAP_MAX_DESCRIPTORS        (16)
#define HDD_FORMAT_STEP_SECTORS          (0x40000000)

/* Ends the command with CHECK CONDITION and the given sense (any expected data phase is stalled by the SIBCB handling). */
static void HddFail (uint8_t Sense)
{
    glMscCmdStatus_STORAGE = 1;
    glSensePtr_STORAGE = Sense;
    CyU3PEventSet(&glMscAppEvent_STORAGE, CY_FX_MSC_SIBCB_EVENT_FLAG, CYU3P_EVENT_OR);
}

/* Sends Length bytes of the scratch buffer to the OS, cut to the allocation length of the CDB. */
static void HddRespond (uint8_t Lun, CyBool_t NeedMedia, uint32_t Length, uint32_t AllocLength)
{
    CyU3PReturnStatus_t Status;
    uint32_t DataLength;

    if (Length > AllocLength)
        Length = AllocLength;

    if (CyFxMscApplnCheckCbwParams_STORAGE(Lun, NeedMedia, CyTrue, CyFalse, Length, &DataLength) == CyFalse)
        return;

    glMscState_STORAGE = CY_FX_MSC_STATE_DATA;
    Status = CyFxMscApplnSendDataToHost_STORAGE(HddMediaScratch(), DataLength);
    if (Status != CY_U3P_SUCCESS)
        HddFail(CY_FX_MSC_SENSE_CRC_ERROR);
}

/* A command without a data phase succeeds. */
static void HddDone (uint8_t Lun, CyBool_t NeedMedia)
{
    uint32_t DataLength;

    if (CyFxMscApplnCheckCbwParams_STORAGE(Lun, NeedMedia, CyFalse, CyFalse, 0, &DataLength) == CyFalse)
        return;

    glMscState_STORAGE = CY_FX_MSC_STATE_STATUS;
}

/* For the commands that need the disk: fails the command the usual way (NOT READY / MEDIA CHANGED) when there is none. */
static CyBool_t HddMediumReady (uint8_t Lun)
{
    uint32_t Saved = glMscResidue_STORAGE;
    uint32_t DataLength;
    CyBool_t Ready;

    Ready = CyFxMscApplnCheckCbwParams_STORAGE(Lun, CyTrue, CyTrue, CyFalse, 8, &DataLength);

    /* The check takes the bytes it expects off the residue; the real response checks again, so give them back. */
    glMscResidue_STORAGE = Saved;
    return (Ready);
}

static void HddPut32 (uint8_t *Buffer, uint32_t Value)
{
    Buffer[0] = (uint8_t)(Value >> 24);
    Buffer[1] = (uint8_t)(Value >> 16);
    Buffer[2] = (uint8_t)(Value >> 8);
    Buffer[3] = (uint8_t)Value;
}

static void HddPut64 (uint8_t *Buffer, uint64_t Value)
{
    HddPut32(Buffer, (uint32_t)(Value >> 32));
    HddPut32(Buffer + 4, (uint32_t)Value);
}

static uint32_t HddGet16 (uint32_t Index)
{
    return (((uint32_t)CDB(Index) << 8) | CDB(Index + 1));
}

static uint32_t HddGet32 (uint32_t Index)
{
    return (((uint32_t)CDB(Index) << 24) | ((uint32_t)CDB(Index + 1) << 16) | ((uint32_t)CDB(Index + 2) << 8) | CDB(Index + 3));
}

static uint64_t HddGet64 (uint32_t Index)
{
    return (((uint64_t)HddGet32(Index) << 32) | HddGet32(Index + 4));
}

static uint32_t HddGetBe32 (const uint8_t *Buffer)
{
    return (((uint32_t)Buffer[0] << 24) | ((uint32_t)Buffer[1] << 16) | ((uint32_t)Buffer[2] << 8) | Buffer[3]);
}

/* The size of the disk in sectors. */
static uint64_t HddSectors (void)
{
    uint32_t Low;
    uint32_t High;

    HddMediaGetSectorCount(&Low, &High);
    return (((uint64_t)High << 32) | Low);
}

/* Whether Count sectors from Lba lie inside the disk. */
static CyBool_t HddRangeIsValid (uint64_t Lba, uint64_t Count)
{
    uint64_t Sectors = HddSectors();

    return (((Lba < Sectors) && (Count <= (Sectors - Lba))) ? CyTrue : CyFalse);
}

/* READ CAPACITY(10): the last LBA (all ones when it does not fit) and the block length. */
static void HddReadCapacity10 (uint8_t Lun)
{
    uint8_t *Buffer = HddMediaScratch();
    uint64_t Last = HddSectors() - 1;

    if (HddMediumReady(Lun) == CyFalse)
        return;

    HddPut32(Buffer, (Last > 0xFFFFFFFFULL) ? 0xFFFFFFFF : (uint32_t)Last);
    HddPut32(Buffer + 4, HDM_SECTOR_BYTES);
    HddRespond(Lun, CyTrue, 8, 8);
}

/* SERVICE ACTION IN(16) / READ CAPACITY(16): the 64 bit capacity, plus the thin provisioning bits (UNMAP supported, released blocks read as zero). */
static void HddServiceActionIn16 (uint8_t Lun)
{
    uint8_t *Buffer = HddMediaScratch();

    if ((CDB(1) & 0x1F) != 0x10)
    {
        HddFail(CY_FX_MSC_SENSE_INVALID_FIELD_IN_CBW);
        return;
    }

    if (HddMediumReady(Lun) == CyFalse)
        return;

    CyU3PMemSet(Buffer, 0, 32);
    HddPut64(Buffer, HddSectors() - 1);
    HddPut32(Buffer + 8, HDM_SECTOR_BYTES);
    Buffer[14] = 0xC0;                                  /* TPE (thin provisioned), TPRZ (released blocks read as zero) */
    HddRespond(Lun, CyTrue, 32, HddGet32(10));
}

/* READ FORMAT CAPACITIES (old style, still asked by Windows for removable disks): one descriptor. */
static void HddReadFormatCapacity (uint8_t Lun)
{
    uint8_t *Buffer = HddMediaScratch();
    uint64_t Sectors = (HddMediaIsPresent() == CyTrue) ? HddSectors() : 0;

    CyU3PMemSet(Buffer, 0, 12);
    Buffer[3] = 8;                                      /* capacity list length */
    HddPut32(Buffer + 4, (Sectors > 0xFFFFFFFFULL) ? 0xFFFFFFFF : (uint32_t)Sectors);
    Buffer[8] = (HddMediaIsPresent() == CyTrue) ? 0x02 : 0x03;      /* formatted media / no media present */
    Buffer[9] = 0;
    Buffer[10] = (uint8_t)(HDM_SECTOR_BYTES >> 8);
    Buffer[11] = (uint8_t)HDM_SECTOR_BYTES;
    HddRespond(Lun, CyFalse, 12, HddGet16(7));
}

/* MODE SENSE(6) and (10): the write protect bit, and a caching page that says the write cache is off (a write is only acknowledged once the PC
 * has it). */
static void HddModeSense (uint8_t Lun, CyBool_t Ten)
{
    uint8_t *Buffer = HddMediaScratch();
    uint32_t Page = CDB(2) & 0x3F;
    uint32_t Alloc = (Ten == CyTrue) ? HddGet16(7) : CDB(4);
    uint32_t HeaderLength = (Ten == CyTrue) ? 8 : 4;
    uint32_t Length = HeaderLength;
    uint8_t DeviceSpecific = (HddMediaIsReadOnly() == CyTrue) ? 0x80 : 0x00;

    CyU3PMemSet(Buffer, 0, 64);

    if ((Page == 0x08) || (Page == 0x3F))
    {
        Buffer[Length] = 0x08;
        Buffer[Length + 1] = 0x12;                      /* WCE = 0: no write cache; RCD = 0 */
        Length += 20;
    }

    if (Length == HeaderLength)
    {
        HddFail(CY_FX_MSC_SENSE_INVALID_FIELD_IN_CBW);
        return;
    }

    if (Ten == CyTrue)
    {
        Buffer[0] = (uint8_t)((Length - 2) >> 8);
        Buffer[1] = (uint8_t)(Length - 2);
        Buffer[3] = DeviceSpecific;
    }
    else
    {
        Buffer[0] = (uint8_t)(Length - 1);
        Buffer[2] = DeviceSpecific;
    }

    HddRespond(Lun, CyFalse, Length, Alloc);
}

/* MODE SELECT(6/10): nothing the OS could change; an empty parameter list is fine, a list is refused (the data phase is stalled). */
static void HddModeSelect (uint8_t Lun, CyBool_t Ten)
{
    uint32_t ParameterBytes = (Ten == CyTrue) ? HddGet16(7) : CDB(4);

    if (ParameterBytes == 0)
    {
        HddDone(Lun, CyFalse);
        return;
    }

    HddFail(CY_FX_MSC_SENSE_INVALID_FIELD_IN_CBW);
}

/* START STOP UNIT: ejecting (LoEj, no Start) is the OS asking the PC to give the disk back. */
static void HddStartStop (uint8_t Lun)
{
    uint32_t DataLength;

    if (CyFxMscApplnCheckCbwParams_STORAGE(Lun, CyFalse, CyFalse, CyFalse, 0, &DataLength) == CyFalse)
        return;

    if (((CDB(4) & 0x02) != 0) && ((CDB(4) & 0x01) == 0))
    {
        if (HddMediaEject() == CyFalse)
        {
            HddFail(CY_FX_MSC_SENSE_CANT_EJECT);
            return;
        }
    }

    glMscState_STORAGE = CY_FX_MSC_STATE_STATUS;
}

/* PREVENT ALLOW MEDIUM REMOVAL. */
static void HddPreventAllow (uint8_t Lun)
{
    uint32_t DataLength;

    if (CyFxMscApplnCheckCbwParams_STORAGE(Lun, CyFalse, CyFalse, CyFalse, 0, &DataLength) == CyFalse)
        return;

    HddMediaSetLock(((CDB(4) & 0x01) != 0) ? CyTrue : CyFalse);
    glMscState_STORAGE = CY_FX_MSC_STATE_STATUS;
}

/* Maps the status of the PC's COMPLETE message to the sense the OS gets after a read. */
static uint8_t HddSenseForReadStatus (uint8_t Status)
{
    switch (Status)
    {
        case HDM_STATUS_NO_MEDIUM:
            return (CY_FX_MSC_SENSE_NO_MEDIA);
        case HDM_STATUS_OUT_OF_RANGE:
            return (CY_FX_MSC_SENSE_INVALID_LBA);
        case HDM_STATUS_FAILED:
            return (HDD_SENSE_TARGET_FAILURE);
        default:
            return (CY_FX_MSC_SENSE_READ_ERROR);
    }
}

/* And after a write, flush or unmap: anything but OK is a write fault, except the cases the OS can act on. */
static uint8_t HddSenseForWriteStatus (uint8_t Status)
{
    switch (Status)
    {
        case HDM_STATUS_NO_MEDIUM:
            return (CY_FX_MSC_SENSE_NO_MEDIA);
        case HDM_STATUS_OUT_OF_RANGE:
            return (CY_FX_MSC_SENSE_INVALID_LBA);
        default:
            return (CY_FX_MSC_SENSE_WRITE_FAULT);
    }
}

/* Whether the OS reset the bus (or the device lost its configuration) while a command was running. */
static CyBool_t HddUsbWasReset (void)
{
    uint32_t Events;

    return (((glDevConfigured_STORAGE == CyFalse) ||
            (CyU3PEventGet(&glMscAppEvent_STORAGE, CY_FX_MSC_RESET_EVENT_FLAG, CYU3P_EVENT_OR, &Events, CYU3P_NO_WAIT) == CY_U3P_SUCCESS)) ? CyTrue : CyFalse);
}

/* Where the Client spends its time per command, in 1 ms ticks, summed and printed after every 2 s of activity (diagnostics only). */
typedef struct
{
    uint32_t Start;
    uint32_t ReadCommands;
    uint32_t ReadKilobytes;
    uint32_t ReadTotalMs;
    uint32_t ReadFirstDataMs;
    uint32_t ReadWaitMs;
    uint32_t ReadUsbMs;
    uint32_t WriteCommands;
    uint32_t WriteKilobytes;
    uint32_t WriteTotalMs;
    uint32_t WriteUsbMs;
    uint32_t WriteSendMs;
    uint32_t WriteCompleteMs;
} HddStatsBlock;

static HddStatsBlock HddStats;

/* Prints and clears the sums when the window of 2 s is over. Called at the end of a successful read or write. */
static void HddStatsReport (void)
{
    uint32_t Now = CyU3PGetTime();
    uint32_t Window = Now - HddStats.Start;

    if (HddStats.Start == 0)
    {
        HddStats.Start = Now;
        return;
    }

    if (Window < 2000)
        return;

    if (HddStats.ReadCommands != 0)
    {
        CyU3PDebugPrint(4, "HDD stats %d ms: read %d cmd, %d KB, total %d ms\r\n", Window, HddStats.ReadCommands, HddStats.ReadKilobytes, HddStats.ReadTotalMs);
        CyU3PDebugPrint(4, "  read: first data %d ms, wait for data %d ms, usb out %d ms\r\n", HddStats.ReadFirstDataMs, HddStats.ReadWaitMs, HddStats.ReadUsbMs);
    }

    if (HddStats.WriteCommands != 0)
    {
        CyU3PDebugPrint(4, "HDD stats %d ms: write %d cmd, %d KB, total %d ms\r\n", Window, HddStats.WriteCommands, HddStats.WriteKilobytes, HddStats.WriteTotalMs);
        CyU3PDebugPrint(4, "  write: usb in %d ms, send to PC %d ms, wait complete %d ms\r\n", HddStats.WriteUsbMs, HddStats.WriteSendMs, HddStats.WriteCompleteMs);
    }

    CyU3PMemSet((uint8_t *)&HddStats, 0, sizeof(HddStats));
    HddStats.Start = CyU3PGetTime();
}

/* ---- Bulk reads over the dedicated data lane (GPIF thread 2 -> PIB socket 2): for a read of at least HDD_BULK_MIN_BYTES the whole 16 KB buffers of it do
 * not go through the ring. The mass-storage IN channel (a CPU channel that also carries the CSW) is replaced for the duration of the transfer by an AUTO
 * channel PIB socket 2 -> bulk IN, armed for exactly that many bytes before the READ_REQUEST goes out; the Host arms its side when the PC announces the
 * transfer, GPIF flow control holds the data until both ends are ready. The tail (less than 16 KB) and the COMPLETE still come as messages. ---- */
#define HDD_BULK_MIN_BYTES               (32768)
/* 0 (default) = reads always use the message lane: the bulk lane carries the data correctly but is no faster for 512 KB commands, because the
 * per-command channel swap costs as much as the shorter data phase saves. -DHDD_BULK_ENABLE=1 uses it (needs the Host's dedicated bulk lane). */
#ifndef HDD_BULK_ENABLE
#define HDD_BULK_ENABLE                  (0)
#endif
#define HDD_BULK_BUFFER_BYTES            (16384)
#define HDD_BULK_BUFFER_COUNT            (2)

extern void CyFxMscApplnDmaCb_STORAGE (CyU3PDmaChannel *handle, CyU3PDmaCbType_t evtype, CyU3PDmaCBInput_t *input);

static CyU3PDmaChannel HddBulkChannel;
static CyBool_t HddBulkUp = CyFalse;
static CyBool_t HddBulkDisabled = CyFalse;          /* Set when the channel could not be created (no DMA memory left): bulk reads are then not tried again. */

/* Puts the CPU channel of the IN endpoint back (same configuration as CyFxMscApplnDmaInit_STORAGE). */
static void HddBulkRestoreMsc (void)
{
    CyU3PDmaChannelConfig_t Config;
    CyU3PReturnStatus_t Status;

    CyU3PMemSet ((uint8_t *)&Config, 0, sizeof (Config));
    Config.size = 1024;
    Config.count = CY_FX_MSC_DMA_BUF_COUNT;
    Config.prodSckId = CY_U3P_CPU_SOCKET_PROD;
    Config.consSckId = (CyU3PDmaSocketId_t)(CY_U3P_UIB_SOCKET_CONS_0 | CY_FX_EP_STORAGE_IN_SOCKET);
    Config.dmaMode = CY_U3P_DMA_MODE_BYTE;
    Config.notification = CY_U3P_DMA_CB_SEND_CPLT;
    Config.cb = CyFxMscApplnDmaCb_STORAGE;
    Status = CyU3PDmaChannelCreate (&glChHandleMscIn_STORAGE, CY_U3P_DMA_TYPE_MANUAL_OUT, &Config);
    if (Status != CY_U3P_SUCCESS)
    {
        CyU3PDebugPrint (4, "HDD: the IN channel could not be created again, Error code = %d\r\n", Status);
        CyFxAppErrorHandler (Status);
    }
}

/* Replaces the IN channel by the AUTO channel for Bytes bytes. Returns CyFalse (with the IN channel in place again) when that was not possible. */
static CyBool_t HddBulkBegin (uint32_t Bytes)
{
    CyU3PDmaChannelConfig_t Config;
    CyU3PReturnStatus_t Status;

    if (HddBulkDisabled == CyTrue)
        return (CyFalse);

    CyU3PDmaChannelDestroy (&glChHandleMscIn_STORAGE);

    CyU3PMemSet ((uint8_t *)&Config, 0, sizeof (Config));
    Config.size = HDD_BULK_BUFFER_BYTES;
    Config.count = HDD_BULK_BUFFER_COUNT;
    Config.prodSckId = CY_U3P_PIB_SOCKET_2;
    Config.consSckId = (CyU3PDmaSocketId_t)(CY_U3P_UIB_SOCKET_CONS_0 | CY_FX_EP_STORAGE_IN_SOCKET);
    Config.dmaMode = CY_U3P_DMA_MODE_BYTE;
    Status = CyU3PDmaChannelCreate (&HddBulkChannel, CY_U3P_DMA_TYPE_AUTO, &Config);
    if (Status == CY_U3P_ERROR_MEMORY_ERROR)
    {
        /* Not two free 16 KB buffers: a single one still works (fill and drain then follow each other). */
        Config.count = 1;
        Status = CyU3PDmaChannelCreate (&HddBulkChannel, CY_U3P_DMA_TYPE_AUTO, &Config);
    }
    if (Status == CY_U3P_SUCCESS)
    {
        HddBulkUp = CyTrue;
        Status = CyU3PDmaChannelSetXfer (&HddBulkChannel, Bytes);
    }

    if (Status != CY_U3P_SUCCESS)
    {
        CyU3PDebugPrint (4, "HDD: the bulk channel for %d bytes could not be set up, Error code = %d\r\n", Bytes, Status);
        if (HddBulkUp == CyTrue)
        {
            CyU3PDmaChannelDestroy (&HddBulkChannel);
            HddBulkUp = CyFalse;
        }
        HddBulkRestoreMsc ();
        if (Status == CY_U3P_ERROR_MEMORY_ERROR)
            HddBulkDisabled = CyTrue;
        return (CyFalse);
    }

    return (CyTrue);
}

/* Waits until the whole transfer went out on the IN endpoint. Returns 0 when it did, 1 when the bus was reset, 2 when the PC gave up, 3 on timeout. */
static uint8_t HddBulkWait (void)
{
    uint32_t Start = CyU3PGetTime ();
    CyU3PReturnStatus_t Status;

    for (;;)
    {
        Status = CyU3PDmaChannelWaitForCompletion (&HddBulkChannel, 20);
        if (Status == CY_U3P_SUCCESS)
            return (0);

        if (Status != CY_U3P_ERROR_TIMEOUT)
            return (2);

        if (HddUsbWasReset () == CyTrue)
            return (1);

        if ((HddMediaIsComplete () == CyTrue) && (HddMediaStatus () != HDM_STATUS_OK))
            return (2);

        if ((CyU3PGetTime () - Start) > HDM_TIMEOUT_MS)
            return (3);
    }
}

/* Removes the AUTO channel and brings the CPU channel back. */
static void HddBulkEnd (void)
{
    if (HddBulkUp == CyTrue)
    {
        CyU3PDmaChannelDestroy (&HddBulkChannel);
        HddBulkUp = CyFalse;
        HddBulkRestoreMsc ();
    }
}

/* Reads Count sectors from the PC and streams them to the OS. Every sector goes through the PC; the only buffer is the ring in HDDMEDIA.c.
 * Pieces handed to USB are multiples of 1024 bytes except the last, so no short packet ends the transfer early. */
static void HddStreamRead (uint8_t Lun, uint64_t Lba, uint32_t Count)
{
    CyU3PReturnStatus_t Status;
    uint32_t Total = Count * HDM_SECTOR_BYTES;
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
    uint32_t CmdStart = CyU3PGetTime();
    uint32_t BeginTime;
    uint32_t Mark;
    CyBool_t FirstSeen = CyFalse;
    uint32_t BulkBytes = 0;
    uint8_t BulkResult;

    if (Count > HDD_MAX_COUNT)
    {
        HddFail(CY_FX_MSC_SENSE_INVALID_FIELD_IN_CBW);
        return;
    }

    if (CyFxMscApplnCheckCbwParams_STORAGE(Lun, CyTrue, CyTrue, CyTrue, Total, &Events) == CyFalse)
        return;

    if (Count == 0)
    {
        glMscState_STORAGE = CY_FX_MSC_STATE_STATUS;
        return;
    }

    if (HddRangeIsValid(Lba, Count) == CyFalse)
    {
        HddFail(CY_FX_MSC_SENSE_INVALID_LBA);
        return;
    }

    /* Big reads: the whole 16 KB buffers of it over the bulk data lane (the IN channel is swapped before the request goes out). */
    if ((HDD_BULK_ENABLE == 1) && (Total >= HDD_BULK_MIN_BYTES))
    {
        BulkBytes = Total & ~((uint32_t)(HDD_BULK_BUFFER_BYTES - 1));
        if (HddBulkBegin(BulkBytes) == CyFalse)
            BulkBytes = 0;
    }
    HddMediaReadBulkHint(BulkBytes / HDD_BULK_BUFFER_BYTES);

    if (HddMediaReadBegin((uint32_t)Lba, (uint32_t)(Lba >> 32), Count, Total) == CyFalse)
    {
        HddBulkEnd();
        HddFail(CY_FX_MSC_SENSE_NO_MEDIA);
        return;
    }

    glMscState_STORAGE = CY_FX_MSC_STATE_DATA;
    LastProgress = CyU3PGetTime();
    BeginTime = LastProgress;

    if (BulkBytes != 0)
    {
        Mark = CyU3PGetTime();
        BulkResult = HddBulkWait();
        HddBulkEnd();
        HddStats.ReadUsbMs += CyU3PGetTime() - Mark;
        LastProgress = CyU3PGetTime();
        if (BulkResult == 0)
        {
            Sent += BulkBytes;
            FirstSeen = CyTrue;
        }
        else if (BulkResult == 1)
        {
            UsbReset = CyTrue;
            Cancel = CyTrue;
        }
        else
        {
            CyU3PDebugPrint(4, "HDD: bulk transfer of %d bytes at LBA %d failed (%d)\r\n", BulkBytes, (uint32_t)Lba, BulkResult);
            Sense = CY_FX_MSC_SENSE_READ_ERROR;
            Failed = CyTrue;
            Cancel = CyTrue;
        }
    }

    while ((Failed == CyFalse) && (UsbReset == CyFalse) && (Sent < Total))
    {
        Chunk = HddMediaReadPeek(&Pointer, HDD_CHUNK_BYTES);
        if (Chunk > (Total - Sent))
            Chunk = Total - Sent;
        if (Chunk < (Total - Sent))
            Chunk &= ~((uint32_t)1023);

        if (Chunk != 0)
        {
            if (FirstSeen == CyFalse)
            {
                FirstSeen = CyTrue;
                HddStats.ReadFirstDataMs += CyU3PGetTime() - BeginTime;
            }
            Mark = CyU3PGetTime();
            Status = CyFxMscApplnSendDataToHost_STORAGE(Pointer, Chunk);
            if (Status != CY_U3P_SUCCESS)
            {
                Sense = CY_FX_MSC_SENSE_CRC_ERROR;
                Failed = CyTrue;
                Cancel = CyTrue;
                continue;
            }

            Status = CyU3PEventGet(&glMscAppEvent_STORAGE, CY_FX_MSC_DATASENT_EVENT_FLAG | CY_FX_MSC_RESET_EVENT_FLAG, CYU3P_EVENT_OR_CLEAR, &Events,
                    HDD_USB_TIMEOUT_MS);
            if (Status != CY_U3P_SUCCESS)
            {
                /* The OS stopped reading mid-transfer. */
                CyU3PDmaChannelReset(&glChHandleMscIn_STORAGE);
                Sense = HDD_SENSE_TARGET_FAILURE;
                Failed = CyTrue;
                Cancel = CyTrue;
                continue;
            }

            if ((Events & CY_FX_MSC_RESET_EVENT_FLAG) != 0)
            {
                CyU3PEventSet(&glMscAppEvent_STORAGE, CY_FX_MSC_RESET_EVENT_FLAG, CYU3P_EVENT_OR);
                UsbReset = CyTrue;
                Cancel = CyTrue;
                continue;
            }

            HddStats.ReadUsbMs += CyU3PGetTime() - Mark;
            HddMediaReadConsume(Chunk);
            Sent += Chunk;
            LastProgress = CyU3PGetTime();
            continue;
        }

        /* Nothing can be sent yet. */
        if (HddMediaIsComplete() == CyTrue)
        {
            /* The PC has said everything it will say and it was not enough. */
            Sense = HddSenseForReadStatus(HddMediaStatus());
            Failed = CyTrue;
            continue;
        }

        Mark = CyU3PGetTime();
        Events = HddMediaWait(100);
        HddStats.ReadWaitMs += CyU3PGetTime() - Mark;
        if ((Events & HDM_WAIT_ABORT) != 0)
        {
            Sense = (HddMediaIsPresent() == CyTrue) ? CY_FX_MSC_SENSE_READ_ERROR : CY_FX_MSC_SENSE_NO_MEDIA;
            Failed = CyTrue;
            continue;
        }

        if (HddUsbWasReset() == CyTrue)
        {
            UsbReset = CyTrue;
            Cancel = CyTrue;
            continue;
        }

        if (((CyU3PGetTime() - LastProgress) > 3000) && (StallReported == CyFalse))
        {
            StallReported = CyTrue;
            CyU3PDebugPrint(4, "HDD: waiting 3 s for data, LBA %d count %d, sent %d of %d, received %d, complete %d\r\n",
                    (uint32_t)Lba, Count, Sent, Total, HddMediaReadReceived(), HddMediaIsComplete());
            GpifLinkPrintState();
        }

        if ((CyU3PGetTime() - LastProgress) > HDM_TIMEOUT_MS)
        {
            CyU3PDebugPrint(4, "HDD: no answer from the PC for %d ms, reading LBA %d failed\r\n", HDM_TIMEOUT_MS, (uint32_t)Lba);
            Sense = CY_FX_MSC_SENSE_READ_ERROR;
            Failed = CyTrue;
            Cancel = CyTrue;
        }
    }

    HddMediaReadEnd(Cancel);

    if (UsbReset == CyTrue)
    {
        /* The reset handling of the main loop takes over; no status is sent for a command the OS has abandoned. */
        glMscState_STORAGE = CY_FX_MSC_STATE_INACTIVE;
        return;
    }

    if (Failed == CyTrue)
    {
        CyU3PDebugPrint(4, "HDD: read of %d sectors at LBA %d failed, sense %d, sent %d of %d bytes, received %d, complete %d status %d\r\n",
                Count, (uint32_t)Lba, Sense, Sent, Total, HddMediaReadReceived(), HddMediaIsComplete(), HddMediaStatus());
        glMscResidue_STORAGE = Total - Sent;
        HddFail(Sense);
        return;
    }

    HddStats.ReadCommands++;
    HddStats.ReadKilobytes += Total >> 10;
    HddStats.ReadTotalMs += CyU3PGetTime() - CmdStart;
    HddStatsReport();

    glMscResidue_STORAGE = 0;
    glMscCmdStatus_STORAGE = 0;
    glMscState_STORAGE = CY_FX_MSC_STATE_STATUS;
}

/* Receives Length bytes of the data-out phase into Buffer (a multiple of 4). Returns 0 when they are in, 1 when the bus was reset, 2 when the data
 * did not come or the DMA failed (the OUT channel is then reset). */
static uint8_t HddArmUsb (uint8_t *Buffer, uint32_t Length)
{
    if (Buffer == NULL)
        return (2);

    if (CyFxMscApplnReceiveUsbData_STORAGE(Buffer, Length) != CY_U3P_SUCCESS)
    {
        CyU3PDmaChannelReset(&glChHandleMscOut_STORAGE);
        return (2);
    }

    return (0);
}

/* Waits for the piece armed with HddArmUsb (Length bytes). Same return values as HddReceiveUsb. */
static uint8_t HddAwaitUsb (uint32_t Length)
{
    CyU3PDmaBuffer_t DmaBuf;
    CyU3PReturnStatus_t Status;
    uint32_t Events;

    Status = CyU3PEventGet(&glMscAppEvent_STORAGE, CY_FX_MSC_DATA_RECV_EVENT_FLAG | CY_FX_MSC_RESET_EVENT_FLAG, CYU3P_EVENT_OR_CLEAR, &Events,
            HDD_USB_TIMEOUT_MS);
    if (Status != CY_U3P_SUCCESS)
    {
        CyU3PDmaChannelReset(&glChHandleMscOut_STORAGE);
        return (2);
    }

    if ((Events & CY_FX_MSC_RESET_EVENT_FLAG) != 0)
    {
        CyU3PEventSet(&glMscAppEvent_STORAGE, CY_FX_MSC_RESET_EVENT_FLAG, CYU3P_EVENT_OR);
        return (1);
    }

    Status = CyU3PDmaChannelWaitForRecvBuffer(&glChHandleMscOut_STORAGE, &DmaBuf, CYU3P_NO_WAIT);
    if ((Status != CY_U3P_SUCCESS) || (DmaBuf.count != Length))
    {
        CyU3PDmaChannelReset(&glChHandleMscOut_STORAGE);
        return (2);
    }

    return (0);
}

static uint8_t HddReceiveUsb (uint8_t *Buffer, uint32_t Length)
{
    uint8_t Result = HddArmUsb(Buffer, Length);

    if (Result != 0)
        return (Result);

    return (HddAwaitUsb(Length));
}

/* Waits up to the normal timeout for the PC to be there, for the commands that must reach it. Returns 0 when it is, 1 on a USB reset, 2 on timeout. */
static uint8_t HddWaitForPc (void)
{
    uint32_t Start = CyU3PGetTime();

    while (HddMediaPcIsUp() == CyFalse)
    {
        if (HddUsbWasReset() == CyTrue)
            return (1);

        if ((CyU3PGetTime() - Start) > HDM_TIMEOUT_MS)
            return (2);

        CyU3PThreadSleep(50);
    }

    return (0);
}

/* Ends a command that was abandoned (reset) or failed. RemainingBytes are the data-out bytes the OS did not get to send. */
static void HddEndFailed (uint8_t Sense, uint32_t RemainingBytes, uint8_t Reason)
{
    if (Reason == 1)
    {
        /* The reset handling of the main loop takes over; no status is sent for a command the OS has abandoned. */
        glMscState_STORAGE = CY_FX_MSC_STATE_INACTIVE;
        return;
    }

    glMscResidue_STORAGE = RemainingBytes;
    HddFail(Sense);
}

/* WRITE(10/12/16): the data goes to the PC in pieces and the OS gets its status only after the PC confirmed that everything is written. */
static void HddStreamWrite (uint8_t Lun, uint64_t Lba, uint32_t Count)
{
    uint32_t Total = Count * HDM_SECTOR_BYTES;
    uint32_t Done = 0;
    uint32_t Piece;
    uint32_t Events;
    uint8_t Result;
    uint32_t CmdStart = CyU3PGetTime();
    uint32_t Mark;
    uint32_t Slot = 0;
    uint32_t Next;

    if (Count > HDD_MAX_COUNT)
    {
        HddFail(CY_FX_MSC_SENSE_INVALID_FIELD_IN_CBW);
        return;
    }

    if (CyFxMscApplnCheckCbwParams_STORAGE(Lun, CyTrue, CyFalse, CyTrue, Total, &Events) == CyFalse)
        return;

    if (Count == 0)
    {
        glMscState_STORAGE = CY_FX_MSC_STATE_STATUS;
        return;
    }

    /* The write protect flag is enforced here, before a single byte is taken: the PC is never asked to write to a read-only disk. */
    if (HddMediaIsReadOnly() == CyTrue)
    {
        HddFail(CY_FX_MSC_SENSE_WRITE_PROTECT);
        return;
    }

    if (HddRangeIsValid(Lba, Count) == CyFalse)
    {
        HddFail(CY_FX_MSC_SENSE_INVALID_LBA);
        return;
    }

    glMscState_STORAGE = CY_FX_MSC_STATE_DATA;

    Result = HddWaitForPc();
    if (Result != 0)
    {
        HddEndFailed(CY_FX_MSC_SENSE_WRITE_FAULT, Total, Result);
        return;
    }

    if (HddMediaWriteBegin((uint32_t)Lba, (uint32_t)(Lba >> 32), Count) == CyFalse)
    {
        HddEndFailed(CY_FX_MSC_SENSE_WRITE_FAULT, Total, 2);
        return;
    }

    /* Every piece lands directly in a GPIF lane buffer (a manual channel hands out the same buffer until it is committed, so only one is held
     * at a time): arm, wait, commit - the next one is armed right after the commit while the GPIF drains the committed one. */
    Piece = (Total > HDM_WRITE_PIECE_BYTES) ? HDM_WRITE_PIECE_BYTES : Total;
    Result = HddArmUsb(HddMediaWriteBufferAt(0), Piece);
    if (Result != 0)
    {
        HddMediaRequestEnd();
        HddEndFailed(HDD_SENSE_TARGET_FAILURE, Total, Result);
        return;
    }

    while (Done < Total)
    {
        Piece = Total - Done;
        if (Piece > HDM_WRITE_PIECE_BYTES)
            Piece = HDM_WRITE_PIECE_BYTES;

        Mark = CyU3PGetTime();
        Result = HddAwaitUsb(Piece);
        HddStats.WriteUsbMs += CyU3PGetTime() - Mark;
        if (Result != 0)
        {
            HddMediaRequestEnd();
            HddEndFailed(HDD_SENSE_TARGET_FAILURE, Total - Done, Result);
            return;
        }

        Mark = CyU3PGetTime();
        if (HddMediaWriteSendAt(0, Piece) == CyFalse)
        {
            CyU3PDebugPrint(4, "HDD: write of %d sectors at LBA %d could not be sent to the PC\r\n", Count, (uint32_t)Lba);
            HddMediaRequestEnd();
            HddEndFailed(CY_FX_MSC_SENSE_WRITE_FAULT, Total - Done - Piece, 2);
            return;
        }

        HddStats.WriteSendMs += CyU3PGetTime() - Mark;
        Done += Piece;

        Next = Total - Done;
        if (Next != 0)
        {
            if (Next > HDM_WRITE_PIECE_BYTES)
                Next = HDM_WRITE_PIECE_BYTES;

            Result = HddArmUsb(HddMediaWriteBufferAt(0), Next);
            if (Result != 0)
            {
                HddMediaRequestEnd();
                HddEndFailed(HDD_SENSE_TARGET_FAILURE, Total - Done, Result);
                return;
            }
        }
    }

    Mark = CyU3PGetTime();
    Result = HddMediaWaitComplete(HDM_TIMEOUT_MS);
    HddStats.WriteCompleteMs += CyU3PGetTime() - Mark;
    if (Result != HDM_STATUS_OK)
    {
        CyU3PDebugPrint(4, "HDD: write of %d sectors at LBA %d failed, status %d\r\n", Count, (uint32_t)Lba, Result);
        glMscResidue_STORAGE = 0;
        HddFail(HddSenseForWriteStatus(Result));
        return;
    }

    HddStats.WriteCommands++;
    HddStats.WriteKilobytes += Total >> 10;
    HddStats.WriteTotalMs += CyU3PGetTime() - CmdStart;
    HddStatsReport();

    glMscResidue_STORAGE = 0;
    glMscCmdStatus_STORAGE = 0;
    glMscState_STORAGE = CY_FX_MSC_STATE_STATUS;
}

/* VERIFY(10/16) without a byte comparison: the range must exist; the PC holds the data and verifies nothing the Client could add to. */
static void HddVerify (uint8_t Lun, uint64_t Lba, uint32_t Count)
{
    uint32_t DataLength;

    if ((CDB(1) & 0x02) != 0)
    {
        HddFail(CY_FX_MSC_SENSE_INVALID_FIELD_IN_CBW);              /* BYTCHK: comparing OS data with the disk is not supported. */
        return;
    }

    if (CyFxMscApplnCheckCbwParams_STORAGE(Lun, CyTrue, CyFalse, CyFalse, 0, &DataLength) == CyFalse)
        return;

    if ((Count != 0) && (HddRangeIsValid(Lba, Count) == CyFalse))
    {
        HddFail(CY_FX_MSC_SENSE_INVALID_LBA);
        return;
    }

    glMscState_STORAGE = CY_FX_MSC_STATE_STATUS;
}

/* SYNCHRONIZE CACHE: the Client holds no unwritten data (every write was confirmed by the PC before the OS got its status), so only the PC's own
 * file buffers matter; they are flushed when the PC is there. A missing PC is not an error: nothing is pending on the Client. */
static void HddSyncCache (uint8_t Lun)
{
    uint32_t DataLength;
    uint8_t Result;

    if (CyFxMscApplnCheckCbwParams_STORAGE(Lun, CyTrue, CyFalse, CyFalse, 0, &DataLength) == CyFalse)
        return;

    if ((HddMediaIsReadOnly() == CyFalse) && (HddMediaPcIsUp() == CyTrue) && (HddMediaFlushBegin() == CyTrue))
    {
        Result = HddMediaWaitComplete(HDM_TIMEOUT_MS);
        if ((Result != HDM_STATUS_OK) && (Result != HDM_STATUS_NONE))
        {
            HddFail(HddSenseForWriteStatus(Result));
            return;
        }
    }

    glMscState_STORAGE = CY_FX_MSC_STATE_STATUS;
}

/* Asks the PC to release (or zero) a range and waits for its answer. Returns the status of the COMPLETE, HDM_STATUS_NONE when the PC did not answer. */
static uint8_t HddReleaseRange (uint64_t Lba, uint64_t Count, uint8_t Mode, uint32_t TimeoutMs)
{
    if (HddMediaUnmapBegin((uint32_t)Lba, (uint32_t)(Lba >> 32), (uint32_t)Count, Mode) == CyFalse)
        return (HDM_STATUS_NONE);

    return (HddMediaWaitComplete(TimeoutMs));
}

/* UNMAP: the OS says which ranges are free (what a "format" or a delete with TRIM turns into). The PC releases them in the image - or writes zeros
 * where it cannot release - so a sparse VHDX can be compacted later. Released sectors read back as zero. */
static void HddUnmap (uint8_t Lun)
{
    uint8_t *Buffer = HddMediaScratch();
    uint32_t ParameterBytes = HddGet16(7);
    uint32_t Events;
    uint32_t DescriptorBytes;
    uint32_t Index;
    uint64_t Lba;
    uint32_t Count;
    uint8_t Result;

    if ((ParameterBytes > 512) || ((ParameterBytes != 0) && (ParameterBytes < 8)))
    {
        HddFail(CY_FX_MSC_SENSE_INVALID_FIELD_IN_CBW);
        return;
    }

    if (CyFxMscApplnCheckCbwParams_STORAGE(Lun, CyTrue, CyFalse, CyTrue, ParameterBytes, &Events) == CyFalse)
        return;

    if (ParameterBytes == 0)
    {
        glMscState_STORAGE = CY_FX_MSC_STATE_STATUS;
        return;
    }

    if (HddMediaIsReadOnly() == CyTrue)
    {
        HddFail(CY_FX_MSC_SENSE_WRITE_PROTECT);
        return;
    }

    glMscState_STORAGE = CY_FX_MSC_STATE_DATA;

    Result = HddReceiveUsb(Buffer, ParameterBytes);
    if (Result == 0)
        Result = HddWaitForPc();
    if (Result != 0)
    {
        HddEndFailed(HDD_SENSE_TARGET_FAILURE, 0, Result);
        return;
    }

    glMscResidue_STORAGE = 0;

    DescriptorBytes = (((uint32_t)Buffer[2] << 8) | Buffer[3]);
    if (((DescriptorBytes % 16) != 0) || ((DescriptorBytes + 8) > ParameterBytes) || ((DescriptorBytes / 16) > HDD_UNMAP_MAX_DESCRIPTORS))
    {
        HddFail(CY_FX_MSC_SENSE_INVALID_FIELD_IN_CBW);
        return;
    }

    for (Index = 0; Index < (DescriptorBytes / 16); Index++)
    {
        Lba = ((uint64_t)HddGetBe32(Buffer + 8 + (Index * 16)) << 32) | HddGetBe32(Buffer + 12 + (Index * 16));
        Count = HddGetBe32(Buffer + 16 + (Index * 16));
        if (Count == 0)
            continue;

        if (HddRangeIsValid(Lba, Count) == CyFalse)
        {
            HddFail(CY_FX_MSC_SENSE_INVALID_LBA);
            return;
        }

        Result = HddReleaseRange(Lba, Count, HDM_UNMAP_RELEASE, HDD_LONG_TIMEOUT_MS);
        if (Result != HDM_STATUS_OK)
        {
            CyU3PDebugPrint(4, "HDD: UNMAP of %d sectors at LBA %d failed, status %d\r\n", Count, (uint32_t)Lba, Result);
            HddFail(HddSenseForWriteStatus(Result));
            return;
        }
    }

    glMscCmdStatus_STORAGE = 0;
    glMscState_STORAGE = CY_FX_MSC_STATE_STATUS;
}

/* WRITE SAME(10/16): only the case that matters, a block of zeros over a range (how the OS clears or "formats" without sending every sector), is
 * supported; with the UNMAP bit the range is released, without it zeros are written. Another pattern is refused. */
static void HddWriteSame (uint8_t Lun, uint64_t Lba, uint32_t Count)
{
    uint8_t *Buffer = HddMediaScratch();
    uint32_t Events;
    uint32_t Index;
    uint8_t Mode = ((CDB(1) & 0x08) != 0) ? HDM_UNMAP_RELEASE : HDM_UNMAP_ZERO;
    uint8_t Result;

    if (CyFxMscApplnCheckCbwParams_STORAGE(Lun, CyTrue, CyFalse, CyTrue, HDM_SECTOR_BYTES, &Events) == CyFalse)
        return;

    if (HddMediaIsReadOnly() == CyTrue)
    {
        HddFail(CY_FX_MSC_SENSE_WRITE_PROTECT);
        return;
    }

    if ((Count != 0) && (HddRangeIsValid(Lba, Count) == CyFalse))
    {
        HddFail(CY_FX_MSC_SENSE_INVALID_LBA);
        return;
    }

    glMscState_STORAGE = CY_FX_MSC_STATE_DATA;

    Result = HddReceiveUsb(Buffer, HDM_SECTOR_BYTES);
    if (Result == 0)
        Result = HddWaitForPc();
    if (Result != 0)
    {
        HddEndFailed(HDD_SENSE_TARGET_FAILURE, 0, Result);
        return;
    }

    glMscResidue_STORAGE = 0;

    for (Index = 0; Index < (HDM_SECTOR_BYTES / 4); Index++)
    {
        if (((uint32_t *)Buffer)[Index] != 0)
        {
            HddFail(CY_FX_MSC_SENSE_INVALID_FIELD_IN_CBW);
            return;
        }
    }

    if (Count != 0)
    {
        Result = HddReleaseRange(Lba, Count, Mode, HDD_LONG_TIMEOUT_MS);
        if (Result != HDM_STATUS_OK)
        {
            HddFail(HddSenseForWriteStatus(Result));
            return;
        }
    }

    glMscCmdStatus_STORAGE = 0;
    glMscState_STORAGE = CY_FX_MSC_STATE_STATUS;
}

/* FORMAT UNIT: a disk image has nothing to format. The command succeeds at once on an empty (or already released) image; where the image holds data
 * the PC releases all of it (sparse VHDX: the blocks are freed so the file can be compacted later; otherwise zeros are written). The defect list
 * parameters, if the OS sends them, are read and ignored. */
static void HddFormatUnit (uint8_t Lun)
{
    uint8_t *Buffer = HddMediaScratch();
    uint32_t Events;
    uint32_t ListBytes = glMscResidue_STORAGE;
    uint64_t Done = 0;
    uint64_t Sectors;
    uint64_t Step;
    uint8_t Result;

    if ((CDB(1) & 0x10) == 0)
        ListBytes = 0;

    if (ListBytes > 4096)
    {
        HddFail(CY_FX_MSC_SENSE_INVALID_FIELD_IN_CBW);
        return;
    }

    if (CyFxMscApplnCheckCbwParams_STORAGE(Lun, CyTrue, CyFalse, CyTrue, ListBytes, &Events) == CyFalse)
        return;

    if (HddMediaIsReadOnly() == CyTrue)
    {
        HddFail(CY_FX_MSC_SENSE_WRITE_PROTECT);
        return;
    }

    glMscState_STORAGE = CY_FX_MSC_STATE_DATA;

    Result = 0;
    if (ListBytes != 0)
        Result = HddReceiveUsb(Buffer, ListBytes);
    if (Result == 0)
        Result = HddWaitForPc();
    if (Result != 0)
    {
        HddEndFailed(HDD_SENSE_TARGET_FAILURE, 0, Result);
        return;
    }

    glMscResidue_STORAGE = 0;
    Sectors = HddSectors();

    while (Done < Sectors)
    {
        Step = Sectors - Done;
        if (Step > HDD_FORMAT_STEP_SECTORS)
            Step = HDD_FORMAT_STEP_SECTORS;

        Result = HddReleaseRange(Done, Step, HDM_UNMAP_RELEASE, HDD_LONG_TIMEOUT_MS);
        if (Result != HDM_STATUS_OK)
        {
            CyU3PDebugPrint(4, "HDD: FORMAT UNIT failed at sector %d, status %d\r\n", (uint32_t)Done, Result);
            HddFail(HddSenseForWriteStatus(Result));
            return;
        }

        Done += Step;
    }

    glMscCmdStatus_STORAGE = 0;
    glMscState_STORAGE = CY_FX_MSC_STATE_STATUS;
}
