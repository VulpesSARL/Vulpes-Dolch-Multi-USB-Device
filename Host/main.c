#include "cyu3system.h"
#include "cyu3os.h"
#include "cyu3dma.h"
#include "cyu3error.h"
#include "main.h"
#include "GuardCore.h"
#include "cyu3usb.h"
#include "cyu3uart.h"
#include "cyu3gpio.h"
#include "cyu3utils.h"
#include "cyu3gpif.h"
#include "cyu3pib.h"
#include "LINK.h"
#include "BULK.h"
#include "GpifLinkCore.h"
#include "HOSTUPD.h"
#include "PERIPH.h"

FIRMWARE_VERSION_TAG_DEFINE (HostVersionTag);

/* This file should be included only once, as it contains structure definitions - including it in
 * multiple places can result in a linker error. Only the Host firmware includes this header. */
#include "gpif_host.h"

/* Sockets of the pass-through bench (CY_FX_GPIF_TEST 7): the lane sockets by default; -DCY_FX_PASS_WRITE_SOCKET=CY_U3P_PIB_SOCKET_2 and
 * -DCY_FX_PASS_READ_SOCKET=CY_U3P_PIB_SOCKET_3 move the bench to the second thread pair. */
#ifndef CY_FX_PASS_WRITE_SOCKET
#define CY_FX_PASS_WRITE_SOCKET     CY_FX_GPIF_WRITE_SOCKET
#endif
#ifndef CY_FX_PASS_READ_SOCKET
#define CY_FX_PASS_READ_SOCKET      CY_FX_GPIF_READ_SOCKET
#endif

CyU3PThread     bulkSrcSinkAppThread;    /* Application thread structure */
CyU3PThread     pcLinkThread;           /* Thread of the PC link (LINK.c). */
CyU3PDmaChannel glChHandleGpifWrite;     /* DMA MANUAL_OUT channel handle: CPU -> PIB socket 1 (GPIF write path to Client). */
CyU3PDmaChannel glChHandleGpifRead;      /* DMA MANUAL_IN channel handle: PIB socket 0 -> CPU (GPIF read path from Client). */

volatile uint32_t glSmInterruptCount = 0;  /* GPIF state machine interrupts seen (diagnostics). */
volatile CyBool_t glGpifReady = CyFalse;      /* PIB, GPIF state machine and the write channel are up (the client manager thread waits for this). */
CyBool_t glGpifReadCreated = CyFalse;    /* Whether the GPIF read channel exists. */
CyU3PDmaChannel *glPassWrapUpChannel = NULL;    /* Pass-through test (CY_FX_GPIF_TEST 7): the AUTO channel GPIF read socket -> USB IN, wrapped up when the Client's packet ended short. */
CyBool_t glGpifReadReady = CyFalse;      /* Whether the GPIF read channel exists (the GPIF event callback wraps it up). */
CyBool_t glIsApplnActive = CyFalse;      /* Whether the PC link endpoints are configured. */
CyBool_t glDataTransStarted = CyFalse;   /* Whether DMA transfer has been started after enumeration. */
CyBool_t StandbyModeEnable  = CyFalse;   /* Whether standby mode entry is enabled. */
CyBool_t TriggerStandbyMode = CyFalse;   /* Request to initiate standby entry. */
CyBool_t glForceLinkU2      = CyFalse;   /* Whether the device should try to initiate U2 mode. */

volatile uint32_t glEp0StatCount = 0;           /* Number of EP0 status events received. */
uint8_t glEp0Buffer[32] __attribute__ ((aligned (32))); /* Local buffer used for vendor command handling. */

/* Control request related variables. */
CyU3PEvent glBulkLpEvent;       /* Event group used to signal the thread that there is a pending request. */
uint32_t   gl_setupdat0;        /* Variable that holds the setupdat0 value (bmRequestType, bRequest and wValue). */
uint32_t   gl_setupdat1;        /* Variable that holds the setupdat1 value (wIndex and wLength). */
#define CYFX_USB_CTRL_TASK      (1 << 0)        /* Event that indicates that there is a pending USB control request. */
#define CYFX_USB_HOSTWAKE_TASK  (1 << 1)        /* Event that indicates the a Remote Wake should be attempted. */

/* Buffer used for USB event logs. */
uint8_t *gl_UsbLogBuffer = NULL;
#define CYFX_USBLOG_SIZE        (0x1000)

/* Timer Instance */
CyU3PTimer glLpmTimer;

/* GPIO used for testing IO state retention when switching from boot firmware to full firmware. */
#define FX3_GPIO_TEST_OUT               (50)
#define FX3_GPIO_TO_LOFLAG(gpio)        (1 << (gpio))
#define FX3_GPIO_TO_HIFLAG(gpio)        (1 << ((gpio) - 32))

/* Forward declaration - defined further down, used by helper functions above its definition. */
void CyFxAppErrorHandler (CyU3PReturnStatus_t apiRetStatus);


/* PIB DLL control register, used to tune the P-port clock phase for synchronous GPIF. Mirrors the
 * AutoMaster reference example (Other\USB CY Examples\GPIF Connection\FX3 Firmware\AutoMaster). */
#define CY_FX3_PIB_DLL_CTRL_REG         (*(uvint32_t *)(0xe0010028))

#define CY_FX3_PIB_DLL_ENABLE           (0x00000001)    /* Bit to enable the DLL. */
#define CY_FX3_PIB_DLL_HIGH_FREQ        (0x00000002)    /* Set DLL into high freq (> 70 MHz) mode. */
#define CY_FX3_PIB_DLL_LOCK_STAT        (0x00000004)    /* DLL lock status. */
#define CY_FX3_PIB_DLL_CORE_PHASE_POS   (4)             /* Position of core clock phase field. */
#define CY_FX3_PIB_DLL_SYNC_PHASE_POS   (8)             /* Position of sync clock phase field. */
#define CY_FX3_PIB_DLL_OP_PHASE_POS     (12)            /* Position of output clock phase field. */
#define CY_FX3_PIB_DLL_RESET_N          (0x40000000)    /* DLL reset request bit. */

/* Configure and enable the PIB DLL. corePhase/syncPhase/opPhase are 0-15, each step being 22.5
 * degrees of delay. Values below are copied from AutoMaster as a starting point - expected to
 * need re-tuning on real hardware for our specific board/clock config. */
CyBool_t
CyFxApplnSetPibDllParameters (
        CyBool_t isEnable,
        uint8_t  corePhase,
        uint8_t  syncPhase,
        uint8_t  opPhase
        )
{
    uint32_t lockWait;

    /* Disable DLL */
    CY_FX3_PIB_DLL_CTRL_REG &= ~(CY_FX3_PIB_DLL_ENABLE);
    CyU3PBusyWait (1);

    if (isEnable == CyFalse)
        return (CyTrue);

    /* Configure and enable the DLL. */
    CY_FX3_PIB_DLL_CTRL_REG = (
            ((corePhase & 0x0F) << CY_FX3_PIB_DLL_CORE_PHASE_POS) |
            ((syncPhase & 0x0F) << CY_FX3_PIB_DLL_SYNC_PHASE_POS) |
            ((opPhase & 0x0F)   << CY_FX3_PIB_DLL_OP_PHASE_POS) |
            CY_FX3_PIB_DLL_HIGH_FREQ |
            CY_FX3_PIB_DLL_ENABLE
            );

    /* Reset the DLL */
    CY_FX3_PIB_DLL_CTRL_REG &= ~(CY_FX3_PIB_DLL_RESET_N);
    CyU3PBusyWait (1);

    /* Clear Reset */
    CY_FX3_PIB_DLL_CTRL_REG |= CY_FX3_PIB_DLL_RESET_N;
    CyU3PBusyWait (1);

    /* Wait for DLL to lock, but not forever: some settings (and clock dividers) never lock, and a hung
     * start-up is worse than a report. Lock takes about 10 microseconds when it works. */
    for (lockWait = 0; lockWait < 10000; lockWait++)
    {
        if ((CY_FX3_PIB_DLL_CTRL_REG & CY_FX3_PIB_DLL_LOCK_STAT) != 0)
            return (CyTrue);
        CyU3PBusyWait (10);
    }

    return (CyFalse);
}

/* GPIF state machine event callback. */
void
HostGpifEventCB (
        CyU3PGpifEventType event,
        uint8_t             currentState
        )
{
    switch (event)
    {
        case CYU3P_GPIF_EVT_SM_INTERRUPT:
            /* The Master state machine raises this when a read from the Client ended before the DMA buffer was
             * full (RD_SHORT_PKT). Only a firmware wrap-up commits such a partial buffer to the CPU consumer; as
             * the stock AutoMaster does for its read channel. Without it a short Client -> Host buffer is never
             * delivered. */
            glSmInterruptCount++;
#if (CY_FX_GPIF_TEST == 7)
            /* Stock AutoMaster behaviour: the short Client packet is committed to the USB IN endpoint as a short USB packet. */
            if (glPassWrapUpChannel != NULL)
                CyU3PDmaChannelSetWrapUp (glPassWrapUpChannel);
#else
            if (glGpifReadReady == CyTrue)
                CyU3PDmaChannelSetWrapUp (&glChHandleGpifRead);
#endif
            break;

        default:
            break;
    }
}

/* Create and start the GPIF read-path DMA channel (PIB socket 0 -> CPU) that receives the Client's lane buffers.
 * As soon as this channel exists the Master state machine is free to read from the Client whenever the Client
 * signals it has data. */
void
CreateGpifReadChannel (void)
{
    CyU3PReturnStatus_t     apiRetStatus;
    CyU3PDmaChannelConfig_t dmaCfg;

    CyU3PMemSet ((uint8_t *)&dmaCfg, 0, sizeof (dmaCfg));
    dmaCfg.size  = CY_FX_GPIF_DMA_BUF_SIZE;
    dmaCfg.count = CY_FX_GPIF_DMA_BUF_COUNT;
    dmaCfg.prodSckId = CY_FX_GPIF_READ_SOCKET;
    dmaCfg.consSckId = CY_U3P_CPU_SOCKET_CONS;
    dmaCfg.dmaMode = CY_U3P_DMA_MODE_BYTE;
    dmaCfg.notification = 0;
    dmaCfg.cb = NULL;

    apiRetStatus = CyU3PDmaChannelCreate (&glChHandleGpifRead, CY_U3P_DMA_TYPE_MANUAL_IN, &dmaCfg);
    if (apiRetStatus != CY_U3P_SUCCESS)
    {
        CyU3PDebugPrint (4, "CyU3PDmaChannelCreate (GPIF read) failed, Error code = %d\n", apiRetStatus);
        CyFxAppErrorHandler (apiRetStatus);
    }

    apiRetStatus = CyU3PDmaChannelSetXfer (&glChHandleGpifRead, 0);
    if (apiRetStatus != CY_U3P_SUCCESS)
    {
        CyU3PDebugPrint (4, "CyU3PDmaChannelSetXfer (GPIF read) failed, Error code = %d\n", apiRetStatus);
        CyFxAppErrorHandler (apiRetStatus);
    }

    glGpifReadReady = (CY_FX_GPIF_READ_WRAPUP == 1) ? CyTrue : CyFalse;
    glGpifReadCreated = CyTrue;
}

/* Destroys the GPIF read channel (before the Client is reset: while it exists the Master reads whenever the
 * Client has data). Safe when it does not exist. */
void
DestroyGpifReadChannel (void)
{
    if (glGpifReadCreated == CyFalse)
        return;

    glGpifReadReady = CyFalse;
    CyU3PDmaChannelDestroy (&glChHandleGpifRead);
    glGpifReadCreated = CyFalse;
}

/* Throws away whatever the GPIF write channel still holds, so stale lane buffers cannot reach a freshly reset Client. */
void
ResetGpifWriteChannel (void)
{
    CyU3PReturnStatus_t apiRetStatus;

    apiRetStatus = CyU3PDmaChannelReset (&glChHandleGpifWrite);
    if (apiRetStatus != CY_U3P_SUCCESS)
    {
        CyU3PDebugPrint (4, "CyU3PDmaChannelReset (GPIF write) failed, Error code = %d\r\n", apiRetStatus);
        return;
    }

    apiRetStatus = CyU3PDmaChannelSetXfer (&glChHandleGpifWrite, 0);
    if (apiRetStatus != CY_U3P_SUCCESS)
        CyU3PDebugPrint (4, "CyU3PDmaChannelSetXfer (GPIF write) after reset failed, Error code = %d\r\n", apiRetStatus);
}

/* Drive the Client Reset GPIO low, holding the downstream GPIF Client board in reset. */
void
ClientResetAssert (void)
{
    CyU3PGpioSimpleSetValue (CY_FX_CLIENT_RESET_GPIO, CyFalse);
}

/* Release the Client Reset GPIO back to Hi-Z, letting the Client board's own pull-up bring it out of reset. */
void
ClientResetRelease (void)
{
    CyU3PGpioSimpleSetValue (CY_FX_CLIENT_RESET_GPIO, CyTrue);
}

/* Application Error Handler */
void
CyFxAppErrorHandler (
        CyU3PReturnStatus_t apiRetStatus    /* API return status */
        )
{
    /* Disconnects from USB and resets the device. */
    GuardFatal (apiRetStatus);
}

/* The guard thread watches this: the PC configured the device. */
CyBool_t
GuardIsConfigured (
        void)
{
    return (glIsApplnActive);
}

/* This function initializes the debug module. The debug prints
 * are routed to the UART and can be seen using a UART console
 * running at 115200 baud rate. */
void
CyFxBulkSrcSinkApplnDebugInit (void)
{
    CyU3PGpioClock_t  gpioClock;
    CyU3PUartConfig_t uartConfig;
    CyU3PReturnStatus_t apiRetStatus = CY_U3P_SUCCESS;

    /* Initialize the GPIO block. If we are transitioning from the boot app, we can verify whether the GPIO
       state is retained. */
    gpioClock.fastClkDiv = 2;
    gpioClock.slowClkDiv = 32;
    gpioClock.simpleDiv  = CY_U3P_GPIO_SIMPLE_DIV_BY_16;
    gpioClock.clkSrc     = CY_U3P_SYS_CLK_BY_2;
    gpioClock.halfDiv    = 0;
    apiRetStatus = CyU3PGpioInit (&gpioClock, NULL);

    /* When FX3 is restarting from standby mode, the GPIO block would already be ON and need not be started
       again. */
    if ((apiRetStatus != 0) && (apiRetStatus != CY_U3P_ERROR_ALREADY_STARTED))
    {
        CyFxAppErrorHandler(apiRetStatus);
    }
    else
    {
        /* Set the test GPIO as an output and update the value to 0. */
        CyU3PGpioSimpleConfig_t testConf = {CyFalse, CyTrue, CyTrue, CyFalse, CY_U3P_GPIO_NO_INTR};

        apiRetStatus = CyU3PGpioSetSimpleConfig (FX3_GPIO_TEST_OUT, &testConf);
        if (apiRetStatus != 0)
            CyFxAppErrorHandler (apiRetStatus);

        /* Configure the Client Reset GPIO. Idle state is Hi-Z (driveHighEn = CyFalse) so the
         * Client's own pull-up holds the line high; only ClientResetAssert() actively pulls it low. */
        CyU3PGpioSimpleConfig_t resetConf = {CyTrue, CyTrue, CyFalse, CyFalse, CY_U3P_GPIO_NO_INTR};

        apiRetStatus = CyU3PGpioSetSimpleConfig (CY_FX_CLIENT_RESET_GPIO, &resetConf);
        if (apiRetStatus != 0)
            CyFxAppErrorHandler (apiRetStatus);
    }

    /* Initialize the UART for printing debug messages */
    apiRetStatus = CyU3PUartInit();
    if (apiRetStatus != CY_U3P_SUCCESS)
    {
        /* Error handling */
        CyFxAppErrorHandler(apiRetStatus);
    }

    /* Set UART configuration */
    CyU3PMemSet ((uint8_t *)&uartConfig, 0, sizeof (uartConfig));
    uartConfig.baudRate = CY_U3P_UART_BAUDRATE_115200;
    uartConfig.stopBit = CY_U3P_UART_ONE_STOP_BIT;
    uartConfig.parity = CY_U3P_UART_NO_PARITY;
    uartConfig.txEnable = CyTrue;
    uartConfig.rxEnable = CyFalse;
    uartConfig.flowCtrl = CyFalse;
    uartConfig.isDma = CyTrue;

    apiRetStatus = CyU3PUartSetConfig (&uartConfig, NULL);
    if (apiRetStatus != CY_U3P_SUCCESS)
    {
        CyFxAppErrorHandler(apiRetStatus);
    }

    /* Set the UART transfer to a really large value. */
    apiRetStatus = CyU3PUartTxSetBlockXfer (0xFFFFFFFF);
    if (apiRetStatus != CY_U3P_SUCCESS)
    {
        CyFxAppErrorHandler(apiRetStatus);
    }

    /* Initialize the debug module. */
    apiRetStatus = CyU3PDebugInit (CY_U3P_LPP_SOCKET_UART_CONS, 8);
    if (apiRetStatus != CY_U3P_SUCCESS)
    {
        CyFxAppErrorHandler(apiRetStatus);
    }

    CyU3PDebugPreamble(CyFalse);
}

/* Callback funtion for the timer expiry notification. */
void TimerCb(void)
{
    /* Enable the low power mode transition on timer expiry */
    CyU3PUsbLPMEnable();
}

#if ((CY_FX_GPIF_TEST == 7) || (CY_FX_GPIF_TEST == 9))
#include "GPIFBENCH.inc"      /* the pass-through benches of the GPIF tests, not part of the public source tree */
#endif

static volatile CyBool_t UpdateUsbMode;      /* defined with its value further down */

/* This function starts the application. This is called
 * when a SET_CONF event is received from the USB host. It configures the
 * PC link's bulk endpoints for the connected speed and starts the
 * link layer (LINK.c), which takes over the endpoints from here. */
void
CyFxBulkSrcSinkApplnStart (
        void)
{
    uint16_t packetSize = 0;
    CyU3PEpConfig_t epCfg;
    CyU3PReturnStatus_t apiRetStatus = CY_U3P_SUCCESS;
    CyU3PUSBSpeed_t usbSpeed = CyU3PUsbGetSpeed();

    switch (usbSpeed)
    {
    case CY_U3P_FULL_SPEED:
        packetSize = 64;
        break;

    case CY_U3P_HIGH_SPEED:
        packetSize = 512;
        break;

    case CY_U3P_SUPER_SPEED:
        packetSize = 1024;
        break;

    default:
        CyU3PDebugPrint (4, "Error! Invalid USB speed.\r\n");
        CyFxAppErrorHandler (CY_U3P_ERROR_FAILURE);
        break;
    }

    CyU3PMemSet ((uint8_t *)&epCfg, 0, sizeof (epCfg));
    epCfg.enable = CyTrue;
    epCfg.epType = CY_U3P_USB_EP_BULK;
    epCfg.burstLen = (usbSpeed == CY_U3P_SUPER_SPEED) ? CY_FX_LINK_EP_BURST : 1;
    epCfg.streams = 0;
    epCfg.pcktSize = packetSize;
    epCfg.isoPkts = 0;

    /* Bulk OUT endpoint: PC -> Host */
    apiRetStatus = CyU3PSetEpConfig(CY_FX_EP_PRODUCER1, &epCfg);
    if (apiRetStatus != CY_U3P_SUCCESS)
    {
        CyU3PDebugPrint (4, "CyU3PSetEpConfig (OUT) failed, Error code = %d\r\n", apiRetStatus);
        CyFxAppErrorHandler (apiRetStatus);
    }

    /* Bulk IN endpoint: Host -> PC */
    apiRetStatus = CyU3PSetEpConfig(CY_FX_EP_CONSUMER1, &epCfg);
    if (apiRetStatus != CY_U3P_SUCCESS)
    {
        CyU3PDebugPrint (4, "CyU3PSetEpConfig (IN) failed, Error code = %d\r\n", apiRetStatus);
        CyFxAppErrorHandler (apiRetStatus);
    }

    CyU3PUsbFlushEp(CY_FX_EP_PRODUCER1);
    CyU3PUsbFlushEp(CY_FX_EP_CONSUMER1);

#if ((CY_FX_GPIF_TEST == 7) || (CY_FX_GPIF_TEST == 9))
    PassthroughStart ();
#else
    /* The link layer creates the two MANUAL channels between these endpoints and the CPU. */
    if (PcLinkStart () == CyFalse)
    {
        CyU3PDebugPrint (4, "PcLinkStart failed\r\n");
        CyFxAppErrorHandler (CY_U3P_ERROR_FAILURE);
    }

    /* The dedicated bulk data lane: second endpoint pair + AUTO channel to GPIF thread 2. */
    HostBulkStart ();

    /* MIDI and the serial port: no PC application involved, bridged to the Client by the worker thread (PERIPH.c). In the update mode (one single
     * device, interface 0 only) they do not exist. */
    if (UpdateUsbMode == CyFalse)
        HostPeriphUsbStart ();
#endif

    /* The link needs low latency and never idles on purpose, so keep the low power link states off. */
    CyU3PUsbLPMDisable ();

    CyU3PDebugPrint (4, "PC link endpoints up (bulk): speed %d, packet size %d, burst %d\r\n",
            usbSpeed, packetSize, epCfg.burstLen);

    /* Update the flag so that the application thread is notified of this. */
    glIsApplnActive = CyTrue;
}

/* This function stops the application
. This shall be called whenever a RESET
 * or DISCONNECT event is received from the USB host. The endpoints are
 * disabled and the DMA pipe is destroyed by this function. */
void
CyFxBulkSrcSinkApplnStop (
        void)
{
    CyU3PEpConfig_t epCfg;
    CyU3PReturnStatus_t apiRetStatus = CY_U3P_SUCCESS;

    /* Update the flag so that the application thread is notified of this. */
    glIsApplnActive = CyFalse;

    /* Stop the link layer and destroy its channels. */
#if ((CY_FX_GPIF_TEST == 7) || (CY_FX_GPIF_TEST == 9))
    PassthroughStop ();
#else
    HostPeriphUsbStop ();
    HostBulkStop ();
    PcLinkStop ();
#endif

    /* Flush the endpoint memory */
    CyU3PUsbFlushEp(CY_FX_EP_PRODUCER1);
    CyU3PUsbFlushEp(CY_FX_EP_CONSUMER1);

    /* Disable endpoints. */
    CyU3PMemSet ((uint8_t *)&epCfg, 0, sizeof (epCfg));
    epCfg.enable = CyFalse;

    /* Producer endpoint configuration. */
    apiRetStatus = CyU3PSetEpConfig(CY_FX_EP_PRODUCER1, &epCfg);
    if (apiRetStatus != CY_U3P_SUCCESS)
    {
        CyU3PDebugPrint (4, "CyU3PSetEpConfig failed, Error code = %d\r\n", apiRetStatus);
        CyFxAppErrorHandler (apiRetStatus);
    }

    /* Consumer endpoint configuration. */
    apiRetStatus = CyU3PSetEpConfig(CY_FX_EP_CONSUMER1, &epCfg);
    if (apiRetStatus != CY_U3P_SUCCESS)
    {
        CyU3PDebugPrint (4, "CyU3PSetEpConfig failed, Error code = %d\r\n", apiRetStatus);
        CyFxAppErrorHandler (apiRetStatus);
    }
}
/* ---- Update mode of the USB side: the Host shows itself as one single WinUSB device ("Vulpes Dolch - Multi Device Update Mode"), with the same
 * vendor / product ID and serial number as in normal operation - only the product name tells the two apart ----------------------------------- */

#define CYFX_USB_SWITCH_TASK        (1 << 2)        /* Event that asks the application thread to change the USB mode. */

#define UPDATE_DESCRIPTOR_BYTES     (704)
#define UPDATE_OFFSET_DEVICE_SS     (0)
#define UPDATE_OFFSET_DEVICE_HS     (32)
#define UPDATE_OFFSET_BOS           (64)
#define UPDATE_OFFSET_CONFIG_SS     (128)
#define UPDATE_OFFSET_CONFIG_HS     (192)
#define UPDATE_OFFSET_CONFIG_FS     (224)
#define UPDATE_OFFSET_PRODUCT       (256)
#define UPDATE_OFFSET_MSOS          (352)
#define UPDATE_MSOS_LENGTH          (162)           /* Device level set: header (10) + compatible ID (20) + registry property (132). */
#define UPDATE_BOS_BYTES            (0x32)
#define UPDATE_BOS_LENGTH_OFFSET    (46)
#define UPDATE_CONFIG_SS_BYTES      (44)            /* Configuration + interface 0 + its two endpoints (with companion descriptors). */
#define UPDATE_CONFIG_HS_BYTES      (32)

static uint8_t *UpdateDescriptors = NULL;
static volatile CyBool_t UpdateUsbMode = CyFalse;      /* The update mode descriptors are the ones the PC sees. */
static volatile CyBool_t UpdateUsbWanted = CyFalse;    /* What the application thread should switch to. */

/* Builds the descriptors of the update mode as patched copies of the normal ones (once). */
static CyBool_t BuildUpdateDescriptors (void)
{
    static const char Name[] = "Vulpes Dolch - Multi Device Update Mode";
    uint8_t *Base;
    uint32_t Index;

    if (UpdateDescriptors != NULL)
        return (CyTrue);

    Base = (uint8_t *)CyU3PDmaBufferAlloc (UPDATE_DESCRIPTOR_BYTES);
    if (Base == NULL)
        return (CyFalse);
    CyU3PMemSet (Base, 0, UPDATE_DESCRIPTOR_BYTES);

    CyU3PMemCopy (Base + UPDATE_OFFSET_DEVICE_SS, (uint8_t *)CyFxUSB30DeviceDscr, 18);
    CyU3PMemCopy (Base + UPDATE_OFFSET_DEVICE_HS, (uint8_t *)CyFxUSB20DeviceDscr, 18);

    CyU3PMemCopy (Base + UPDATE_OFFSET_BOS, (uint8_t *)CyFxUSBBOSDscr, UPDATE_BOS_BYTES);
    Base[UPDATE_OFFSET_BOS + UPDATE_BOS_LENGTH_OFFSET] = (uint8_t)(UPDATE_MSOS_LENGTH & 0xFF);
    Base[UPDATE_OFFSET_BOS + UPDATE_BOS_LENGTH_OFFSET + 1] = (uint8_t)(UPDATE_MSOS_LENGTH >> 8);

    CyU3PMemCopy (Base + UPDATE_OFFSET_CONFIG_SS, (uint8_t *)CyFxUSBSSConfigDscr1, UPDATE_CONFIG_SS_BYTES);
    Base[UPDATE_OFFSET_CONFIG_SS + 2] = UPDATE_CONFIG_SS_BYTES;
    Base[UPDATE_OFFSET_CONFIG_SS + 3] = 0;
    Base[UPDATE_OFFSET_CONFIG_SS + 4] = 1;
    Base[UPDATE_OFFSET_CONFIG_SS + 13] = 2;     /* the cut keeps only the link pair of interface 0 */
    CyU3PMemCopy (Base + UPDATE_OFFSET_CONFIG_HS, (uint8_t *)CyFxUSBHSConfigDscr1, UPDATE_CONFIG_HS_BYTES);
    Base[UPDATE_OFFSET_CONFIG_HS + 2] = UPDATE_CONFIG_HS_BYTES;
    Base[UPDATE_OFFSET_CONFIG_HS + 3] = 0;
    Base[UPDATE_OFFSET_CONFIG_HS + 4] = 1;
    Base[UPDATE_OFFSET_CONFIG_HS + 13] = 2;
    CyU3PMemCopy (Base + UPDATE_OFFSET_CONFIG_FS, (uint8_t *)CyFxUSBFSConfigDscr1, UPDATE_CONFIG_HS_BYTES);
    Base[UPDATE_OFFSET_CONFIG_FS + 2] = UPDATE_CONFIG_HS_BYTES;
    Base[UPDATE_OFFSET_CONFIG_FS + 3] = 0;
    Base[UPDATE_OFFSET_CONFIG_FS + 4] = 1;
    Base[UPDATE_OFFSET_CONFIG_FS + 13] = 2;

    Base[UPDATE_OFFSET_PRODUCT] = (uint8_t)(2 + 2 * (sizeof (Name) - 1));
    Base[UPDATE_OFFSET_PRODUCT + 1] = CY_U3P_USB_STRING_DESCR;
    for (Index = 0; Index < (sizeof (Name) - 1); Index++)
        Base[UPDATE_OFFSET_PRODUCT + 2 + 2 * Index] = (uint8_t)Name[Index];

    /* The set for a device with a single function: no configuration / function subsets, just the compatible ID and the interface GUID. */
    CyU3PMemCopy (Base + UPDATE_OFFSET_MSOS, (uint8_t *)CyFxMsOs20Dscr, 10);
    Base[UPDATE_OFFSET_MSOS + 8] = (uint8_t)(UPDATE_MSOS_LENGTH & 0xFF);
    Base[UPDATE_OFFSET_MSOS + 9] = (uint8_t)(UPDATE_MSOS_LENGTH >> 8);
    CyU3PMemCopy (Base + UPDATE_OFFSET_MSOS + 10, (uint8_t *)CyFxMsOs20Dscr + 26, UPDATE_MSOS_LENGTH - 10);

    UpdateDescriptors = Base;
    return (CyTrue);
}

/* Callbacks of the USB stack (defined further down). */
CyBool_t CyFxBulkSrcSinkApplnUSBSetupCB (uint32_t setupdat0, uint32_t setupdat1);
void CyFxBulkSrcSinkApplnUSBEventCB (CyU3PUsbEventType_t evtype, uint16_t evdata);

/* Sets every descriptor of the USB side for the normal or the update mode (all of them, because a restarted USB stack has forgotten them). */
static void ApplyUsbDescriptors (CyBool_t UpdateMode)
{
    if (UpdateMode == CyTrue)
    {
        CyU3PUsbSetDesc (CY_U3P_USB_SET_SS_DEVICE_DESCR, 0, UpdateDescriptors + UPDATE_OFFSET_DEVICE_SS);
        CyU3PUsbSetDesc (CY_U3P_USB_SET_HS_DEVICE_DESCR, 0, UpdateDescriptors + UPDATE_OFFSET_DEVICE_HS);
        CyU3PUsbSetDesc (CY_U3P_USB_SET_SS_BOS_DESCR, 0, UpdateDescriptors + UPDATE_OFFSET_BOS);
        CyU3PUsbSetDesc (CY_U3P_USB_SET_SS_CONFIG_DESCR, 0, UpdateDescriptors + UPDATE_OFFSET_CONFIG_SS);
        CyU3PUsbSetDesc (CY_U3P_USB_SET_HS_CONFIG_DESCR, 0, UpdateDescriptors + UPDATE_OFFSET_CONFIG_HS);
        CyU3PUsbSetDesc (CY_U3P_USB_SET_FS_CONFIG_DESCR, 0, UpdateDescriptors + UPDATE_OFFSET_CONFIG_FS);
        CyU3PUsbSetDesc (CY_U3P_USB_SET_STRING_DESCR, 2, UpdateDescriptors + UPDATE_OFFSET_PRODUCT);
    }
    else
    {
        CyU3PUsbSetDesc (CY_U3P_USB_SET_SS_DEVICE_DESCR, 0, (uint8_t *)CyFxUSB30DeviceDscr);
        CyU3PUsbSetDesc (CY_U3P_USB_SET_HS_DEVICE_DESCR, 0, (uint8_t *)CyFxUSB20DeviceDscr);
        CyU3PUsbSetDesc (CY_U3P_USB_SET_SS_BOS_DESCR, 0, (uint8_t *)CyFxUSBBOSDscr);
        CyU3PUsbSetDesc (CY_U3P_USB_SET_SS_CONFIG_DESCR, 0, (uint8_t *)CyFxUSBSSConfigDscr1);
        CyU3PUsbSetDesc (CY_U3P_USB_SET_HS_CONFIG_DESCR, 0, (uint8_t *)CyFxUSBHSConfigDscr1);
        CyU3PUsbSetDesc (CY_U3P_USB_SET_FS_CONFIG_DESCR, 0, (uint8_t *)CyFxUSBFSConfigDscr1);
        CyU3PUsbSetDesc (CY_U3P_USB_SET_STRING_DESCR, 2, (uint8_t *)CyFxUSBProductDscr);
    }

    CyU3PUsbSetDesc (CY_U3P_USB_SET_STRING_DESCR, 0, (uint8_t *)CyFxUSBStringLangIDDscr);
    CyU3PUsbSetDesc (CY_U3P_USB_SET_STRING_DESCR, 1, (uint8_t *)CyFxUSBManufactureDscr);
    CyU3PUsbSetDesc (CY_U3P_USB_SET_STRING_DESCR, 3, (uint8_t *)CyFxUSBInterfaceDesc1);
    CyU3PUsbSetDesc (CY_U3P_USB_SET_STRING_DESCR, 4, (uint8_t *)CyFxUSBInterfaceDesc2);
    CyU3PUsbSetDesc (CY_U3P_USB_SET_STRING_DESCR, 6, (uint8_t *)CyFxUSBInterfaceDesc4);
    CyU3PUsbSetDesc (CY_U3P_USB_SET_STRING_DESCR, 7, (uint8_t *)CyFxUsbSN);
}

/* Switches the USB side to the update mode (or back): the whole USB stack is stopped and started again - the same state as after a reset of the
 * device, which the PC always picks up - and the device then connects with the other descriptors. Runs in the application thread. */
static void SwitchUsbMode (CyBool_t UpdateMode)
{
    CyU3PReturnStatus_t Status;

    if ((UpdateMode == CyTrue) && (BuildUpdateDescriptors () == CyFalse))
    {
        CyU3PDebugPrint (4, "Update mode: no memory for the USB descriptors\r\n");
        return;
    }

    /* A disconnect that the Host starts itself raises no USB event: stop the PC link by hand, or the guard still believes it is configured. */
    if (glIsApplnActive)
        CyFxBulkSrcSinkApplnStop ();

    CyU3PConnectState (CyFalse, CyTrue);
    CyU3PThreadSleep (100);

    CyU3PUsbStop ();
    Status = CyU3PUsbStart ();
    if ((Status != CY_U3P_SUCCESS) && (Status != CY_U3P_ERROR_NO_REENUM_REQUIRED))
    {
        CyU3PDebugPrint (4, "USB restart failed, Error code = %d\r\n", Status);
        CyFxAppErrorHandler (Status);
    }

    CyU3PUsbRegisterSetupCallback (CyFxBulkSrcSinkApplnUSBSetupCB, CyTrue);
    CyU3PUsbRegisterEventCallback (CyFxBulkSrcSinkApplnUSBEventCB);
    ApplyUsbDescriptors (UpdateMode);
    if (gl_UsbLogBuffer)
        CyU3PUsbInitEventLog (gl_UsbLogBuffer, CYFX_USBLOG_SIZE);

    UpdateUsbMode = UpdateMode;

    /* The update mode needs no SuperSpeed; some USB 3 host controllers lose the SuperSpeed re-attach of the Host. */
    CyU3PConnectState (CyTrue, ((UpdateMode == CyTrue) && (CY_FX_UPDATE_MODE_HIGH_SPEED_ONLY == 1)) ? CyFalse : CyTrue);
    GuardUsbConnected ();
}

/* Asks the application thread to switch the USB side to (or back from) the update mode; may be called from any thread. */
void CyFxHostRequestUsbMode (CyBool_t UpdateMode)
{
    UpdateUsbWanted = UpdateMode;
    CyU3PEventSet (&glBulkLpEvent, CYFX_USB_SWITCH_TASK, CYU3P_EVENT_OR);
}

/* Callback to handle the USB setup requests. */
CyBool_t
CyFxBulkSrcSinkApplnUSBSetupCB (
        uint32_t setupdat0, /* SETUP Data 0 */
        uint32_t setupdat1  /* SETUP Data 1 */
    )
{
    /* Fast enumeration is used. Only requests addressed to the interface, class,
     * vendor and unknown control requests are received by this function.
     * This application does not support any class or vendor requests. */

    uint8_t  bRequest, bReqType;
    uint8_t  bType, bTarget;
    uint16_t wValue, wIndex;
    CyBool_t isHandled = CyFalse;

    /* Decode the fields from the setup request. */
    bReqType = (setupdat0 & CY_U3P_USB_REQUEST_TYPE_MASK);
    bType    = (bReqType & CY_U3P_USB_TYPE_MASK);
    bTarget  = (bReqType & CY_U3P_USB_TARGET_MASK);
    bRequest = ((setupdat0 & CY_U3P_USB_REQUEST_MASK) >> CY_U3P_USB_REQUEST_POS);
    wValue   = ((setupdat0 & CY_U3P_USB_VALUE_MASK)   >> CY_U3P_USB_VALUE_POS);
    wIndex   = ((setupdat1 & CY_U3P_USB_INDEX_MASK)   >> CY_U3P_USB_INDEX_POS);

    if (bType == CY_U3P_USB_STANDARD_RQT)
    {
        /* Handle SET_FEATURE(FUNCTION_SUSPEND) and CLEAR_FEATURE(FUNCTION_SUSPEND)
         * requests here. It should be allowed to pass if the device is in configured
         * state and failed otherwise. */
        if ((bTarget == CY_U3P_USB_TARGET_INTF) && ((bRequest == CY_U3P_USB_SC_SET_FEATURE)
                    || (bRequest == CY_U3P_USB_SC_CLEAR_FEATURE)) && (wValue == 0))
        {
            if (glIsApplnActive)
            {
                CyU3PUsbAckSetup ();

                /* The link never goes into a low power state on purpose (see the Client's LPM callback), so the request is only acknowledged. */
                if (bRequest == CY_U3P_USB_SC_SET_FEATURE)
                    glDataTransStarted = CyFalse;
                glForceLinkU2 = CyFalse;
            }
            else
                CyU3PUsbStall (0, CyTrue, CyFalse);

            isHandled = CyTrue;
        }

        /* CLEAR_FEATURE request for endpoint is always passed to the setup callback
         * regardless of the enumeration model used. When a clear feature is received,
         * the previous transfer has to be flushed and cleaned up. This is done at the
         * protocol level. Since this is just a loopback operation, there is no higher
         * level protocol. So flush the EP memory and reset the DMA channel associated
         * with it. If there are more than one EP associated with the channel reset both
         * the EPs. The endpoint stall and toggle / sequence number is also expected to be
         * reset. Return CyFalse to make the library clear the stall and reset the endpoint
         * toggle. Or invoke the CyU3PUsbStall (ep, CyFalse, CyTrue) and return CyTrue.
         * Here we are clearing the stall. */
        if ((bTarget == CY_U3P_USB_TARGET_ENDPT) && (bRequest == CY_U3P_USB_SC_CLEAR_FEATURE)
                && (wValue == CY_U3P_USBX_FS_EP_HALT))
        {
            if (glIsApplnActive)
            {
                if ((wIndex == CY_FX_EP_PRODUCER1) || (wIndex == CY_FX_EP_CONSUMER1))
                {
                    /* Both link endpoints belong to one link, so recover both together. The link starts over
                     * with a fresh session (the PC has to send HELLO again). */
                    CyU3PUsbSetEpNak (CY_FX_EP_PRODUCER1, CyTrue);
                    CyU3PUsbSetEpNak (CY_FX_EP_CONSUMER1, CyTrue);
                    CyU3PBusyWait (125);

                    PcLinkStop ();
                    CyU3PUsbFlushEp(CY_FX_EP_PRODUCER1);
                    CyU3PUsbFlushEp(CY_FX_EP_CONSUMER1);
                    CyU3PUsbResetEp (CY_FX_EP_PRODUCER1);
                    CyU3PUsbResetEp (CY_FX_EP_CONSUMER1);
                    CyU3PUsbSetEpNak (CY_FX_EP_PRODUCER1, CyFalse);
                    CyU3PUsbSetEpNak (CY_FX_EP_CONSUMER1, CyFalse);

                    PcLinkStart ();
                    CyU3PUsbStall (wIndex, CyFalse, CyTrue);
                    isHandled = CyTrue;
                    CyU3PUsbAckSetup ();
                }
            }        }
    }

    /* CDC-ACM class requests of the serial port (communication interface 3). */
    if ((bType == CY_U3P_USB_CLASS_RQT) && (bTarget == CY_U3P_USB_TARGET_INTF) && (wIndex == CY_FX_USB_SERIAL_COMM_INTF) && (UpdateUsbMode == CyFalse))
    {
        uint16_t wLengthClass = ((setupdat1 & CY_U3P_USB_LENGTH_MASK) >> CY_U3P_USB_LENGTH_POS);

        if (HostPeriphCdcSetup ((uint8_t)bRequest, wValue, wLengthClass) == CyTrue)
            return (CyTrue);
    }

    /* The Microsoft OS 2.0 descriptor set (named in the BOS platform capability): lets Windows bind interface 0 to WinUSB. */
    if ((bType == CY_U3P_USB_VENDOR_RQT) && (bTarget == CY_U3P_USB_TARGET_DEVICE) && (bRequest == CY_FX_MSOS20_VENDOR_CODE) &&
            (wIndex == CY_FX_MSOS20_DESCRIPTOR_INDEX) && ((bReqType & 0x80) != 0))
    {
        uint16_t wLength = ((setupdat1 & CY_U3P_USB_LENGTH_MASK) >> CY_U3P_USB_LENGTH_POS);

        if (UpdateUsbMode == CyTrue)
            CyU3PUsbSendEP0Data ((wLength < UPDATE_MSOS_LENGTH) ? wLength : UPDATE_MSOS_LENGTH, UpdateDescriptors + UPDATE_OFFSET_MSOS);
        else
            CyU3PUsbSendEP0Data ((wLength < CY_FX_MSOS20_SET_LENGTH) ? wLength : CY_FX_MSOS20_SET_LENGTH, (uint8_t *)CyFxMsOs20Dscr);
        return (CyTrue);
    }

    if ((bType == CY_U3P_USB_VENDOR_RQT) && (bTarget == CY_U3P_USB_TARGET_DEVICE))
    {
        /* We set an event here
 and let the application thread below handle these requests.
         * isHandled needs to be set to True, so that the driver does not stall EP0. */
        isHandled = CyTrue;
        gl_setupdat0 = setupdat0;
        gl_setupdat1 = setupdat1;
        CyU3PEventSet (&glBulkLpEvent, CYFX_USB_CTRL_TASK, CYU3P_EVENT_OR);
    }

    return isHandled;
}


/* This is the callback function to handle the USB events. */
void
CyFxBulkSrcSinkApplnUSBEventCB (
        CyU3PUsbEventType_t evtype, /* Event type */
        uint16_t            evdata  /* Event data */
        )
{
    CyU3PDebugPrint (2, "USB EVENT: %d %d\r\n", evtype, evdata);

    switch (evtype)
    {
    case CY_U3P_USB_EVENT_CONNECT:
        break;

    case CY_U3P_USB_EVENT_SETCONF:
        /* If the application is already active
         * stop it before re-enabling. */
        if (glIsApplnActive)
        {
            CyFxBulkSrcSinkApplnStop ();
        }

        /* Start the source sink function. */
        CyFxBulkSrcSinkApplnStart ();
        break;

    case CY_U3P_USB_EVENT_RESET:
    case CY_U3P_USB_EVENT_DISCONNECT:
        glForceLinkU2 = CyFalse;

        /* Stop the source sink function. */
        if (glIsApplnActive)
        {
            CyFxBulkSrcSinkApplnStop ();
        }

        glDataTransStarted = CyFalse;
        break;

    case CY_U3P_USB_EVENT_EP0_STAT_CPLT:
        glEp0StatCount++;
        break;

    case CY_U3P_USB_EVENT_VBUS_REMOVED:
        if (StandbyModeEnable)
        {
            TriggerStandbyMode = CyTrue;
            StandbyModeEnable  = CyFalse;
        }
        break;

    default:
        break;
    }
}

/* Callback function to handle LPM requests from the USB 3.0 host. This function is invoked by the API
   whenever a state change from U0 -> U1 or U0 -> U2 happens. If we return CyTrue from this function, the
   FX3 device is retained in the low power state. If we return CyFalse, the FX3 device immediately tries
   to trigger an exit back to U0.

   This callback is not registered (the FX3 driver then refuses every U1/U2 request), and if it ever is, it refuses too: the link never goes into
   a low power state on purpose.
 */
CyBool_t
CyFxBulkSrcSinkApplntCB (
        CyU3PUsbLinkPowerMode link_mode)
{
    return CyFalse;
}

/* This function initializes the USB Module, sets the enumeration descriptors.
 * This function does not start the bulk streaming and this is done only when
 * SET_CONF event is received. */
void
CyFxBulkSrcSinkApplnInit (void)
{
    CyU3PReturnStatus_t apiRetStatus = CY_U3P_SUCCESS;
    CyBool_t no_renum = CyFalse;
    CyU3PPibClock_t pibClock;
    CyU3PDmaChannelConfig_t dmaCfg;

    /* Initialize the P-port block and load/start the GPIF master state machine before touching USB,
     * mirroring the AutoMaster reference example's init order (GPIF must be up before CyU3PUsbStart). */
    pibClock.clkDiv = 4;
    pibClock.clkSrc = CY_U3P_SYS_CLK;
    pibClock.isHalfDiv = CyFalse;
    pibClock.isDllEnable = CyFalse;    /* DLL is configured separately below via direct register access. */
    apiRetStatus = CyU3PPibInit (CyTrue, &pibClock);
    if (apiRetStatus != CY_U3P_SUCCESS)
    {
        CyU3PDebugPrint (4, "P-port initialization failed, Error code = %d\n", apiRetStatus);
        CyFxAppErrorHandler (apiRetStatus);
    }

    /* The DLL has to be on in master mode (AN87216 section 7: it sets the clock phase); with it off about 15 % of the words read from
     * the Client come out wrong. The phases are the middle of the good windows found with a GPIF echo sweep (both directions, PIB clock
     * divider 4): core 9-15 and 0-3, sync 13-15 and 0-4, output 10-15 and 0-2 are good, one parameter varied at a time around the
     * AN87216 setting (1, 0, 11). If the PIB clock or the system clock ever changes, sweep again. */
    if (CyFxApplnSetPibDllParameters (CyTrue, 14, 1, 14) == CyFalse)
        CyU3PDebugPrint (4, "PIB DLL did not lock\r\n");

    apiRetStatus = CyU3PGpifLoad (&CyFxGpifConfig);
    if (apiRetStatus != CY_U3P_SUCCESS)
    {
        CyU3PDebugPrint (4, "CyU3PGpifLoad failed, Error code = %d\n", apiRetStatus);
        CyFxAppErrorHandler (apiRetStatus);
    }

    CyU3PGpifSocketConfigure (0, CY_FX_GPIF_READ_SOCKET, 4, CyFalse, 1);
    CyU3PGpifSocketConfigure (1, CY_FX_GPIF_WRITE_SOCKET, 4, CyFalse, 1);
    CyU3PGpifRegisterCallback (HostGpifEventCB);

    apiRetStatus = CyU3PGpifSMStart (START, ALPHA_START);
    if (apiRetStatus != CY_U3P_SUCCESS)
    {
        CyU3PDebugPrint (4, "CyU3PGpifSMStart failed, Error code = %d\n", apiRetStatus);
        CyFxAppErrorHandler (apiRetStatus);
    }

#if ((CY_FX_GPIF_TEST != 7) && (CY_FX_GPIF_TEST != 9))
    /* Create the GPIF write-path DMA channel used to send lane buffers to the Client.
     * CPU produces buffer content; the PIB block consumes it out over GPIF. */
    CyU3PMemSet ((uint8_t *)&dmaCfg, 0, sizeof (dmaCfg));
    dmaCfg.size  = CY_FX_GPIF_DMA_BUF_SIZE;
    dmaCfg.count = CY_FX_GPIF_DMA_BUF_COUNT;
    dmaCfg.prodSckId = CY_U3P_CPU_SOCKET_PROD;
    dmaCfg.consSckId = CY_FX_GPIF_WRITE_SOCKET;
    dmaCfg.dmaMode = CY_U3P_DMA_MODE_BYTE;
    dmaCfg.notification = 0;
    dmaCfg.cb = NULL;
    dmaCfg.prodHeader = 0;
    dmaCfg.prodFooter = 0;
    dmaCfg.consHeader = 0;
    dmaCfg.prodAvailCount = 0;

    apiRetStatus = CyU3PDmaChannelCreate (&glChHandleGpifWrite, CY_U3P_DMA_TYPE_MANUAL_OUT, &dmaCfg);
    if (apiRetStatus != CY_U3P_SUCCESS)
    {
        CyU3PDebugPrint (4, "CyU3PDmaChannelCreate (GPIF write) failed, Error code = %d\n", apiRetStatus);
        CyFxAppErrorHandler (apiRetStatus);
    }

    apiRetStatus = CyU3PDmaChannelSetXfer (&glChHandleGpifWrite, 0);
    if (apiRetStatus != CY_U3P_SUCCESS)
    {
        CyU3PDebugPrint (4, "CyU3PDmaChannelSetXfer (GPIF write) failed, Error code = %d\n", apiRetStatus);
        CyFxAppErrorHandler (apiRetStatus);
    }

#endif /* CY_FX_GPIF_TEST != 7 and != 9 */


    glGpifReady = CyTrue;

#if (CY_FX_GPIF_TEST == 1)
    RunGpifTest ();
#elif (CY_FX_GPIF_TEST == 2)
    RunGpifDllSweep ();
#elif (CY_FX_GPIF_TEST == 3)
    RunGpifEchoSweep ();
#elif (CY_FX_GPIF_TEST == 4)
    RunGpifSoak ();
#elif (CY_FX_GPIF_TEST == 5)
    RunGpifStream ();
#elif (CY_FX_GPIF_TEST == 6)
    RunGpifLinkTest ();
#endif

    /* The fast enumeration is the easiest way to setup a USB connection,
     * where all enumeration phase is handled by the library. Only the
     * class / vendor requests need to be handled by the application. */
    CyU3PUsbRegisterSetupCallback(CyFxBulkSrcSinkApplnUSBSetupCB, CyTrue);

    /* Setup the callback to handle the USB events. */
    CyU3PUsbRegisterEventCallback(CyFxBulkSrcSinkApplnUSBEventCB);

    /* Start the USB functionality. */
    apiRetStatus = CyU3PUsbStart();
    if (apiRetStatus == CY_U3P_ERROR_NO_REENUM_REQUIRED)
        no_renum = CyTrue;
    else if (apiRetStatus != CY_U3P_SUCCESS)
    {
        CyU3PDebugPrint (4, "CyU3PUsbStart failed to Start, Error code = %d\n", apiRetStatus);
        CyFxAppErrorHandler(apiRetStatus);
    }

    /* Change GPIO state again. */
    CyU3PGpioSimpleSetValue (FX3_GPIO_TEST_OUT, CyTrue);

    /* Set the USB Enumeration descriptors */

    /* Super speed device descriptor. */
    apiRetStatus = CyU3PUsbSetDesc(CY_U3P_USB_SET_SS_DEVICE_DESCR, 0, (uint8_t *)CyFxUSB30DeviceDscr);
    if (apiRetStatus != CY_U3P_SUCCESS)
    {
        CyU3PDebugPrint (4, "USB set device descriptor failed, Error code = %d\n", apiRetStatus);
        CyFxAppErrorHandler(apiRetStatus);
    }

    /* High speed device descriptor. */
    apiRetStatus = CyU3PUsbSetDesc(CY_U3P_USB_SET_HS_DEVICE_DESCR, 0, (uint8_t *)CyFxUSB20DeviceDscr);
    if (apiRetStatus != CY_U3P_SUCCESS)
    {
        CyU3PDebugPrint (4, "USB set device descriptor failed, Error code = %d\n", apiRetStatus);
        CyFxAppErrorHandler(apiRetStatus);
    }

    /* BOS descriptor */
    apiRetStatus = CyU3PUsbSetDesc(CY_U3P_USB_SET_SS_BOS_DESCR, 0, (uint8_t *)CyFxUSBBOSDscr);
    if (apiRetStatus != CY_U3P_SUCCESS)
    {
        CyU3PDebugPrint (4, "USB set configuration descriptor failed, Error code = %d\n", apiRetStatus);
        CyFxAppErrorHandler(apiRetStatus);
    }

    /* Device qualifier descriptor */
    apiRetStatus = CyU3PUsbSetDesc(CY_U3P_USB_SET_DEVQUAL_DESCR, 0, (uint8_t *)CyFxUSBDeviceQualDscr);
    if (apiRetStatus != CY_U3P_SUCCESS)
    {
        CyU3PDebugPrint (4, "USB set device qualifier descriptor failed, Error code = %d\n", apiRetStatus);
        CyFxAppErrorHandler(apiRetStatus);
    }

    //=============================================

    /* Super speed configuration descriptor */
    apiRetStatus = CyU3PUsbSetDesc(CY_U3P_USB_SET_SS_CONFIG_DESCR, 0, (uint8_t *)CyFxUSBSSConfigDscr1);
    if (apiRetStatus != CY_U3P_SUCCESS)
    {
        CyU3PDebugPrint (4, "USB set configuration descriptor failed, Error code = %d\n", apiRetStatus);
        CyFxAppErrorHandler(apiRetStatus);
    }

    /* High speed configuration descriptor */
    apiRetStatus = CyU3PUsbSetDesc(CY_U3P_USB_SET_HS_CONFIG_DESCR, 0, (uint8_t *)CyFxUSBHSConfigDscr1);
    if (apiRetStatus != CY_U3P_SUCCESS)
    {
        CyU3PDebugPrint (4, "USB Set Other Speed Descriptor failed, Error Code = %d\n", apiRetStatus);
        CyFxAppErrorHandler(apiRetStatus);
    }

    /* Full speed configuration descriptor */
    apiRetStatus = CyU3PUsbSetDesc(CY_U3P_USB_SET_FS_CONFIG_DESCR, 0, (uint8_t *)CyFxUSBFSConfigDscr1);
    if (apiRetStatus != CY_U3P_SUCCESS)
    {
        CyU3PDebugPrint (4, "USB Set Configuration Descriptor failed, Error Code = %d\n", apiRetStatus);
        CyFxAppErrorHandler(apiRetStatus);
    }

    /* String descriptor 0 */
    apiRetStatus = CyU3PUsbSetDesc(CY_U3P_USB_SET_STRING_DESCR, 0, (uint8_t *)CyFxUSBStringLangIDDscr);
    if (apiRetStatus != CY_U3P_SUCCESS)
    {
        CyU3PDebugPrint (4, "USB set string descriptor failed, Error code = %d\n", apiRetStatus);
        CyFxAppErrorHandler(apiRetStatus);
    }

    /* String descriptor 1 */
    apiRetStatus = CyU3PUsbSetDesc(CY_U3P_USB_SET_STRING_DESCR, 1, (uint8_t *)CyFxUSBManufactureDscr);
    if (apiRetStatus != CY_U3P_SUCCESS)
    {
        CyU3PDebugPrint (4, "USB set string descriptor failed, Error code = %d\n", apiRetStatus);
        CyFxAppErrorHandler(apiRetStatus);
    }

    /* String descriptor 2 */
    apiRetStatus = CyU3PUsbSetDesc(CY_U3P_USB_SET_STRING_DESCR, 2, (uint8_t *)CyFxUSBProductDscr);
    if (apiRetStatus != CY_U3P_SUCCESS)
    {
        CyU3PDebugPrint (4, "USB set string descriptor failed, Error code = %d\n", apiRetStatus);
        CyFxAppErrorHandler(apiRetStatus);
    }

    /* String descriptor 3 */
    apiRetStatus = CyU3PUsbSetDesc(CY_U3P_USB_SET_STRING_DESCR, 3, (uint8_t *)CyFxUSBInterfaceDesc1);
    if (apiRetStatus != CY_U3P_SUCCESS)
    {
        CyU3PDebugPrint (4, "USB set string descriptor failed, Error code = %d\n", apiRetStatus);
        CyFxAppErrorHandler(apiRetStatus);
    }

    /* String descriptor 4 - the serial port */
    apiRetStatus = CyU3PUsbSetDesc(CY_U3P_USB_SET_STRING_DESCR, 4, (uint8_t *)CyFxUSBInterfaceDesc2);
    if (apiRetStatus != CY_U3P_SUCCESS)
    {
        CyU3PDebugPrint (4, "USB set string descriptor failed, Error code = %d\n", apiRetStatus);
        CyFxAppErrorHandler(apiRetStatus);
    }

    /* String descriptor 6 */
    apiRetStatus = CyU3PUsbSetDesc(CY_U3P_USB_SET_STRING_DESCR, 6, (uint8_t *)CyFxUSBInterfaceDesc4);
    if (apiRetStatus != CY_U3P_SUCCESS)
    {
        CyU3PDebugPrint (4, "USB set string descriptor failed, Error code = %d\n", apiRetStatus);
        CyFxAppErrorHandler(apiRetStatus);
    }

    /* String descriptor 7 - Serial Number */
    apiRetStatus = CyU3PUsbSetDesc(CY_U3P_USB_SET_STRING_DESCR, 7, (uint8_t *)CyFxUsbSN);
    if (apiRetStatus != CY_U3P_SUCCESS)
    {
        CyU3PDebugPrint (4, "USB set string descriptor failed, Error code = %d\n", apiRetStatus);
        CyFxAppErrorHandler(apiRetStatus);
    }


    /* Register a buffer into which the USB driver can log relevant events. */
    gl_UsbLogBuffer = (uint8_t *)CyU3PDmaBufferAlloc (CYFX_USBLOG_SIZE);
    if (gl_UsbLogBuffer)
        CyU3PUsbInitEventLog (gl_UsbLogBuffer, CYFX_USBLOG_SIZE);

    CyU3PDebugPrint (4, "About to connect to USB host\r\n");

    /* Connect the USB Pins with super speed operation enabled. */
    if (!no_renum) {

        apiRetStatus = CyU3PConnectState(CyTrue, CyTrue);
        if (apiRetStatus != CY_U3P_SUCCESS)
        {
            CyU3PDebugPrint (4, "USB Connect failed, Error code = %d\n", apiRetStatus);
            CyFxAppErrorHandler(apiRetStatus);
        }
        GuardUsbConnected ();
    }
    else
    {
        /* USB connection is already active. Configure the endpoints and DMA channels. */
        CyFxBulkSrcSinkApplnStart ();
    }

    CyU3PDebugPrint (8, "CyFxBulkSrcSinkApplnInit complete\r\n");
}

/*
 * De-initialize function for the USB block. Used to test USB Stop/Start functionality.
 */

/* Entry function for the BulkSrcSinkAppThread. */
void
BulkSrcSinkAppThread_Entry (
        uint32_t input)
{
    CyU3PReturnStatus_t stat;
    uint32_t eventMask = CYFX_USB_CTRL_TASK | CYFX_USB_HOSTWAKE_TASK | CYFX_USB_SWITCH_TASK;   /* Events that we are interested in. */
    uint32_t eventStat;                                                 /* Current status of the events. */
    CyU3PUsbLinkPowerMode curState;

    /* Initialize the debug module */
    CyFxBulkSrcSinkApplnDebugInit();
    CyU3PDebugPrint (1, "\n\ndebug initialized\r\n");
    CyU3PDebugPrint (1, "Host firmware %s\r\n", FIRMWARE_VERSION_TAG_TEXT (HostVersionTag));
    FIRMWARE_VERSION_TAG_KEEP (HostVersionTag);

    /* Initialize the application */
    CyFxBulkSrcSinkApplnInit();

    /* Create a timer with 100 ms expiry to enable/disable LPM transitions */
    CyU3PTimerCreate (&glLpmTimer, TimerCb, 0, 100, 100, CYU3P_NO_ACTIVATE);

    for (;;)
    {
        /* Wakes at least every 10 ms so the USB 3.0 link can be taken out of low power modes. */
        stat = CyU3PEventGet (&glBulkLpEvent, eventMask, CYU3P_EVENT_OR_CLEAR, &eventStat, 10);
        if (stat == CY_U3P_SUCCESS)
        {
            /* Remote wakeup (or function wake on SuperSpeed) is requested. */
            if (eventStat & CYFX_USB_HOSTWAKE_TASK)
            {
                CyU3PThreadSleep (1000);
                if (CyU3PUsbGetSpeed () == CY_U3P_SUPER_SPEED)
                    stat = CyU3PUsbSendDevNotification (1, 0, 0);
                else
                    stat = CyU3PUsbDoRemoteWakeup ();

                if (stat != CY_U3P_SUCCESS)
                    CyU3PDebugPrint (2, "Remote wake attempt failed with code: %d\r\n", stat);
            }

            /* The update mode wants the USB side changed (the reply to the PC goes out first). */
            if (eventStat & CYFX_USB_SWITCH_TASK)
            {
                CyU3PThreadSleep (300);
                SwitchUsbMode (UpdateUsbWanted);
            }

            /* This device has no vendor requests; the PC link runs over the bulk endpoints. */
            if (eventStat & CYFX_USB_CTRL_TASK)
                CyU3PUsbStall (0, CyTrue, CyFalse);
        }

        /* Update mode that nobody uses any more: back to the normal USB side. */
        if ((UpdateUsbMode == CyTrue) && (HostUpdateIdleMs () > HOST_UPDATE_IDLE_MS))
        {
            HostUpdateReset ();
            SwitchUsbMode (CyFalse);
        }

        /* Force the USB 3.0 link to U2. */
        if (glForceLinkU2)
        {
            stat = CyU3PUsbGetLinkPowerState (&curState);
            while ((glForceLinkU2) && (stat == CY_U3P_SUCCESS) && (curState == CyU3PUsbLPM_U0))
            {
                /* Repeatedly try to go into U2 state.*/
                CyU3PUsbSetLinkPowerState (CyU3PUsbLPM_U2);
                CyU3PThreadSleep (5);
                stat = CyU3PUsbGetLinkPowerState (&curState);
            }
        }
    }
}

/* Application define function which creates the threads. */
void
CyFxApplicationDefine (
        void)
{
    void *ptr = NULL;
    uint32_t ret = CY_U3P_SUCCESS;

    /* Create an event flag group that will be used for signalling the application thread. */
    ret = CyU3PEventCreate (&glBulkLpEvent);
    if (ret != 0)
    {
        /* Loop indefinitely */
        while (1);
    }

#if (CY_FX_GPIF_TEST == 0)
    /* The Client side: the GPIF lane, keep-alive and the router towards the PC. */
    ClientManagerInit ();
#endif

    HostPeriphInit ();

    /* The PC link has its own thread. */
    PcLinkInit ();
    ptr = CyU3PMemAlloc (LINK_THREAD_STACK);
    ret = CyU3PThreadCreate (&pcLinkThread,
                          "22:Pc_link",
                          PcLinkThreadEntry,
                          0,
                          ptr,
                          LINK_THREAD_STACK,
                          LINK_THREAD_PRIORITY,
                          LINK_THREAD_PRIORITY,
                          CYU3P_NO_TIME_SLICE,
                          CYU3P_AUTO_START
                          );
    if (ret != 0)
    {
        while (1);
    }

    GuardStart ();

    /* Allocate the memory for the threads */
    ptr = CyU3PMemAlloc (CY_FX_BULKSRCSINK_THREAD_STACK);

    /* Create the thread for the application */
    ret = CyU3PThreadCreate (&bulkSrcSinkAppThread,                /* App thread structure */
                          "21:Bulk_src_sink",                      /* Thread ID and thread name */
                          BulkSrcSinkAppThread_Entry,              /* App thread entry function */
                          0,                                       /* No input parameter to thread */
                          ptr,                                     /* Pointer to the allocated thread stack */
                          CY_FX_BULKSRCSINK_THREAD_STACK,          /* App thread stack size */
                          CY_FX_BULKSRCSINK_THREAD_PRIORITY,       /* App thread priority */
                          CY_FX_BULKSRCSINK_THREAD_PRIORITY,       /* App thread priority */
                          CYU3P_NO_TIME_SLICE,                     /* No time slice for the application thread */
                          CYU3P_AUTO_START                         /* Start the thread immediately */
                          );

    /* Check the return code */
    if (ret != 0)
    {
        /* Thread Creation failed with the error code retThrdCreate */

        /* Add custom recovery or debug actions here */

        /* Application cannot continue */
        /* Loop indefinitely */
        while(1);
    }
}

/*
 * Main function
 */
int
main (void)
{
    CyU3PIoMatrixConfig_t io_cfg;
    CyU3PReturnStatus_t status = CY_U3P_SUCCESS;

    /* Initialize the device */
    CyU3PSysClockConfig_t clockConfig;
    /* 403.2 MHz system clock like the SDK samples (AutoMaster, gpiftousb): the SDK documents that a 32 bit GPIF at 100 MHz needs it.
     * The PIB clock is then 100.8 MHz (divider 4), the CPU 201.6 MHz. The PIB DLL phases depend on these clocks. */
    clockConfig.setSysClk400  = CyTrue;
    clockConfig.cpuClkDiv     = 2;
    clockConfig.dmaClkDiv     = 2;
    clockConfig.mmioClkDiv    = 2;
    clockConfig.useStandbyClk = CyFalse;
    clockConfig.clkSrc         = CY_U3P_SYS_CLK;
    status = CyU3PDeviceInit (&clockConfig);
    if (status != CY_U3P_SUCCESS)
    {
        goto handle_fatal_error;
    }

    /* Initialize the caches. The D-Cache is kept disabled. Enabling this will cause performance to drop,
       as the driver will start doing a lot of un-necessary cache clean/flush operations.
       Enable the D-Cache only if there is a need to process the data being transferred by firmware code.
     */
#ifndef GPIF_DCACHE
    /* Data cache OFF: the bulk path copies every byte through the CPU, and with the cache on the DMA driver
     * cleans / invalidates every 16 KB buffer on commit and discard (45-120 us each); with it off the same copy runs at 82 MB/s instead of 56 MB/s.
     * The code that works on data (update: SHA-256, LZSS) is slower without the cache, which a firmware update can afford. */
    status = CyU3PDeviceCacheControl (CyTrue, CyFalse, CyFalse);
#else
    status = CyU3PDeviceCacheControl (CyTrue, CyTrue, CyTrue);
#endif
    if (status != CY_U3P_SUCCESS)
    {
        goto handle_fatal_error;
    }

    /* Configure the IO matrix for the device. Our GPIF design (master_read_write_sync.cydsn) uses a
     * full 32-bit-wide data bus (confirmed against Firmwares\USB Multi\Connected GPIOs.txt), which
     * requires isDQ32Bit = CyTrue. Per the AutoMaster reference example, UART remains available
     * (useUart stays CyTrue) as long as lppMode is switched from UART_ONLY to DEFAULT alongside it -
     * these two must change together, not independently. */
    io_cfg.isDQ32Bit = CyTrue;
    io_cfg.s0Mode = CY_U3P_SPORT_INACTIVE;
    io_cfg.s1Mode = CY_U3P_SPORT_INACTIVE;
    io_cfg.useUart   = CyTrue;
    io_cfg.useI2C    = CyTrue;      /* The firmware update programs the boot EEPROM. */
    io_cfg.useI2S    = CyFalse;
    io_cfg.useSpi    = CyFalse;
    io_cfg.lppMode   = CY_U3P_IO_MATRIX_LPP_DEFAULT;

    /* Enable the GPIO which would have been setup by 2-stage booter, plus the Client Reset GPIO. */
    io_cfg.gpioSimpleEn[0]  = FX3_GPIO_TO_LOFLAG(CY_FX_CLIENT_RESET_GPIO);
    io_cfg.gpioSimpleEn[1]  = FX3_GPIO_TO_HIFLAG(FX3_GPIO_TEST_OUT);
    io_cfg.gpioComplexEn[0] = 0;
    io_cfg.gpioComplexEn[1] = 0;
    status = CyU3PDeviceConfigureIOMatrix (&io_cfg);
    if (status != CY_U3P_SUCCESS)
    {
        goto handle_fatal_error;
    }

    /* This is a non returnable call for initializing the RTOS kernel */
    CyU3PKernelEntry ();

    /* Dummy return to make the compiler happy */
    return 0;

handle_fatal_error:

    /* Cannot recover from this error. */
    while (1);
}

/* [ ] */

