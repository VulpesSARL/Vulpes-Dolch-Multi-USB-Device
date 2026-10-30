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
#include "GuardCore.h"
#include "PERIPH.h"
#include "UPDATE.h"
#include "../Version/FirmwareVersionTag.h"

FIRMWARE_VERSION_TAG_DEFINE (ClientVersionTag);

static CyU3PThread mscAppThread_CDROM;	                        /* MSC application thread structure */
static CyU3PThread mscAppThread_STORAGE;	                        /* MSC application thread structure */
static CyU3PThread pmAppThread;	                            /* Power management application thread structure */

extern void ClientLinkInit(void);

/*
 * Main function
 */
int main(void)
{
    CyU3PReturnStatus_t status;
    CyU3PIoMatrixConfig_t io_cfg;
    CyU3PSysClockConfig_t clockConfig;

    /* Initialize the device */
    /* The default clock configuration (what NULL selects) with the 403.2 MHz system clock of the SDK samples: the GPIF link to the
     * Host needs it, and the CPU gets 4 % faster. */
    clockConfig.setSysClk400  = CyTrue;
    clockConfig.cpuClkDiv     = 2;
    clockConfig.dmaClkDiv     = 2;
    clockConfig.mmioClkDiv    = 2;
    clockConfig.useStandbyClk = CyTrue;
    clockConfig.clkSrc        = CY_U3P_SYS_CLK;
    status = CyU3PDeviceInit(&clockConfig);
    if (status != CY_U3P_SUCCESS)
    {
        goto handle_fatal_error;
    }

    /* Initialize the caches. Enable instruction cache and keep data cache disabled.
     * The data cache is useful only when there is a large amount of CPU based memory
     * accesses. When used in simple cases, it can decrease performance due to large
     * number of cache flushes and cleans and also it adds to the complexity of the
     * code. */
    /* Data cache OFF: with it on the DMA driver cleans / invalidates every 16 KB buffer on commit and discard; the lane copies every byte
     * through the CPU and runs 44 % faster without the cache. */
    status = CyU3PDeviceCacheControl(CyTrue, CyFalse, CyFalse);
    if (status != CY_U3P_SUCCESS)
    {
        goto handle_fatal_error;
    }

    /* Configure the IO matrix for the device.
     * Pport is not GPIF 32bit; and no GPIO is currently required.
     */
    io_cfg.isDQ32Bit = CyTrue;
    io_cfg.s0Mode = CY_U3P_SPORT_INACTIVE;
    io_cfg.s1Mode = CY_U3P_SPORT_INACTIVE;
    io_cfg.gpioSimpleEn[0] = 0;
    io_cfg.gpioSimpleEn[1] = 0;
    io_cfg.gpioComplexEn[0] = 0;
    io_cfg.gpioComplexEn[1] = 0;
    io_cfg.useUart = CyTrue;
    io_cfg.useI2C = CyTrue;      /* The update mode programs the boot EEPROM. */
    io_cfg.useI2S = CyFalse;
    io_cfg.useSpi = CyFalse;
    io_cfg.lppMode = CY_U3P_IO_MATRIX_LPP_DEFAULT;
    status = CyU3PDeviceConfigureIOMatrix(&io_cfg);
    if (status != CY_U3P_SUCCESS)
    {
        goto handle_fatal_error;
    }

    /* This is a non returnable call for initializing the RTOS kernel */
    CyU3PKernelEntry();

    /* Dummy return to make the compiler happy */
    return 0;

handle_fatal_error:
    /* Cannot recover from this error. */
    while (1);
}

static volatile CyBool_t UsbDiskInterface = CyFalse;        /* The USB configuration of the moment contains the disk interface. */
static volatile CyBool_t UsbDiskWanted = CyFalse;           /* It should contain it (a disk is loaded). */
static volatile CyBool_t UsbRestartRequested = CyFalse;     /* The helper thread is to restart the USB stack. */

static volatile CyBool_t UsbConnected = CyFalse;      /* The USB connection was switched on. */
static uint16_t UsbLastInterface (void);

/* MSC application error handler */
void CyFxAppErrorHandler(CyU3PReturnStatus_t apiRetStatus)
{
    /* Disconnects from USB and resets the device; the Host notices the silent Client and resets it. */
    GuardFatal(apiRetStatus);
}

/* The guard thread watches this: the PC has configured the device. */
CyBool_t GuardIsConfigured(void)
{
    return (glDevConfigured_CDROM);
}

/* This function initializes the Debug Module for the MSC Application */
void CyFxMscApplnDebugInit()
{
    CyU3PUartConfig_t uartConfig;
    CyU3PReturnStatus_t apiRetStatus;

    /* Initialize the UART for printing debug messages */
    apiRetStatus = CyU3PUartInit();
    if (apiRetStatus != CY_U3P_SUCCESS)
    {
        /* Error Handling */
        CyFxAppErrorHandler(apiRetStatus);
    }

    /* Set UART Configuration */
    uartConfig.baudRate = CY_U3P_UART_BAUDRATE_115200;
    uartConfig.stopBit = CY_U3P_UART_ONE_STOP_BIT;
    uartConfig.parity = CY_U3P_UART_NO_PARITY;
    uartConfig.txEnable = CyTrue;
    uartConfig.rxEnable = CyFalse;
    uartConfig.flowCtrl = CyFalse;
    uartConfig.isDma = CyTrue;

    /* Set the UART configuration */
    apiRetStatus = CyU3PUartSetConfig(&uartConfig, NULL);
    if (apiRetStatus != CY_U3P_SUCCESS)
    {
        /* Error Handling */
        CyFxAppErrorHandler(apiRetStatus);
    }

    /* Set the UART transfer */
    apiRetStatus = CyU3PUartTxSetBlockXfer(0xFFFFFFFF);
    if (apiRetStatus != CY_U3P_SUCCESS)
    {
        /* Error Handling */
        CyFxAppErrorHandler(apiRetStatus);
    }

    /* Initialize the Debug application */
    apiRetStatus = CyU3PDebugInit(CY_U3P_LPP_SOCKET_UART_CONS, 8);
    if (apiRetStatus != CY_U3P_SUCCESS)
    {
        /* Error Handling */
        CyFxAppErrorHandler(apiRetStatus);
    }

    CyU3PDebugPreamble(CyFalse);
    CyU3PDebugPrint(4, "\r\n\r\n\r\nMSC DEMO - Debug Init Done\r\n\r\n\r\n");
    CyU3PDebugPrint(4, "Client firmware %s\r\n", FIRMWARE_VERSION_TAG_TEXT (ClientVersionTag));
    FIRMWARE_VERSION_TAG_KEEP (ClientVersionTag);
}

/* Callback to handle the USB Setup Requests and Mass Storage Class requests */
CyBool_t CyFxMscApplnUSBSetupCB(
    uint32_t setupdat0, /* SETUP Data 0 */
    uint32_t setupdat1  /* SETUP Data 1 */
)
{
    CyBool_t isHandled = CyFalse;
    uint8_t  ep0Buf[2];
    uint8_t  hidReportBuf[9] = { 0 };

    uint8_t  bRequest, bReqType;
    uint8_t  bType, bTarget;
    uint16_t wValue, wIndex, wLength;

    /* Decode the fields from the setup request. */
    bReqType = (setupdat0 & CY_U3P_USB_REQUEST_TYPE_MASK);
    bType = (bReqType & CY_U3P_USB_TYPE_MASK);
    bTarget = (bReqType & CY_U3P_USB_TARGET_MASK);
    bRequest = ((setupdat0 & CY_U3P_USB_REQUEST_MASK) >> CY_U3P_USB_REQUEST_POS);
    wValue = ((setupdat0 & CY_U3P_USB_VALUE_MASK) >> CY_U3P_USB_VALUE_POS);
    wIndex = ((setupdat1 & CY_U3P_USB_INDEX_MASK) >> CY_U3P_USB_INDEX_POS);
    wLength = ((setupdat1 & CY_U3P_USB_LENGTH_MASK) >> CY_U3P_USB_LENGTH_POS);

    //CyU3PDebugPrint(4, "CyFx    Setup: bReqType=%d bType=%d Target=%d bRequest=%d wValue=%d wIndex=%d wLength=%d\r\n", bReqType, bType, bTarget, bRequest, wValue, wIndex, wLength);

    /* Some setup requests have to be handled in a non-standard way for this device. */
    if (bType == CY_U3P_USB_STANDARD_RQT)
    {
        switch (bRequest)
        {
            case CY_U3P_USB_SC_CLEAR_FEATURE:
                /* Handle EP RESET requests based on interface state. */
                if ((bTarget == CY_U3P_USB_TARGET_ENDPT) && (
                    (wIndex == CY_FX_EP_CD_ROM_IN) || (wIndex == CY_FX_EP_CD_ROM_OUT)))
                {
                    /* In case of phase error, the stall condition can only be cleared by bus reset or
                       mass storage reset. */
                    if (!glInPhaseError_CDROM)
                        CyU3PUsbStall(wIndex, CyFalse, CyTrue);
                    else
                    {
                        /* Even though we cannot clear the STALL condition, the sequence number should be cleared. */
                        if (CyU3PUsbGetSpeed() == CY_U3P_SUPER_SPEED)
                            CyU3PUsbSetEpSeqNum(wIndex, 0);
                    }

                    CyU3PUsbAckSetup();
                    isHandled = CyTrue;
                }
                if ((bTarget == CY_U3P_USB_TARGET_ENDPT) && (
                    (wIndex == CY_FX_EP_STORAGE_IN) || (wIndex == CY_FX_EP_STORAGE_OUT)))
                {
                    /* In case of phase error, the stall condition can only be cleared by bus reset or
                       mass storage reset. */
                    if (!glInPhaseError_STORAGE)
                        CyU3PUsbStall(wIndex, CyFalse, CyTrue);
                    else
                    {
                        /* Even though we cannot clear the STALL condition, the sequence number should be cleared. */
                        if (CyU3PUsbGetSpeed() == CY_U3P_SUPER_SPEED)
                            CyU3PUsbSetEpSeqNum(wIndex, 0);
                    }

                    CyU3PUsbAckSetup();
                    isHandled = CyTrue;
                }
                if ((bTarget == CY_U3P_USB_TARGET_ENDPT) && (
                    (wIndex == CY_FX_EP_KEYBOARD_IN) ||
                    (wIndex == CY_FX_EP_MIDI_IN) || (wIndex == CY_FX_EP_MIDI_OUT) ||
                    (wIndex == CY_FX_EP_SERIAL_IN) || (wIndex == CY_FX_EP_SERIAL_OUT) || (wIndex == CY_FX_EP_SERIAL_NOTIFY)))
                {
                    /* No phase-error concept applies to the HID/MIDI endpoints - a plain stall clear
                       is sufficient. */
                    CyU3PUsbStall(wIndex, CyFalse, CyTrue);
                    CyU3PUsbAckSetup();
                    isHandled = CyTrue;
                }
                /* No break */
            case CY_U3P_USB_SC_SET_FEATURE:
                /* Handle SET_FEATURE(FUNCTION_SUSPEND) and CLEAR_FEATURE(FUNCTION_SUSPEND)
                 * requests here. It should be allowed to pass if the device is in configured
                 * state and failed otherwise. */
                if ((bTarget == CY_U3P_USB_TARGET_INTF) && (wValue == 0))
                {
                    if (glDevConfigured_CDROM)
                        CyU3PUsbAckSetup();
                    else
                        CyU3PUsbStall(0, CyTrue, CyFalse);

                    isHandled = CyTrue;
                }
                if ((bTarget == CY_U3P_USB_TARGET_INTF) && (wValue == 1))
                {
                    if (glDevConfigured_STORAGE)
                        CyU3PUsbAckSetup();
                    else
                        CyU3PUsbStall(0, CyTrue, CyFalse);

                    isHandled = CyTrue;
                }
                break;

                /* We need to handle all Interface specific requests here. */
            case CY_U3P_USB_SC_GET_STATUS:
                if (bTarget == CY_U3P_USB_TARGET_INTF)
                {
                    /* We support interfaces 0 through 6 (CD-ROM, keyboard + mouse, MIDI Audio Control,
                     * MIDI Streaming, serial port control and data, and the disk while one is loaded). */
                    if (wIndex <= UsbLastInterface())
                    {
                        ep0Buf[0] = 0;
                        ep0Buf[1] = 0;
                        CyU3PUsbSendEP0Data(0x02, ep0Buf);
                    }
                    else
                        CyU3PUsbStall(0, CyTrue, CyFalse);

                    isHandled = CyTrue;
                }
                break;

            case CY_U3P_USB_SC_SET_INTERFACE:
                isHandled = CyTrue;
                if ((wValue == 0) && (wIndex <= UsbLastInterface()))
                    CyU3PUsbAckSetup();
                else
                    CyU3PUsbStall(0, CyTrue, CyFalse);
                break;

            case CY_U3P_USB_SC_GET_INTERFACE:
                isHandled = CyTrue;
                if (wIndex <= UsbLastInterface())
                {
                    ep0Buf[0] = 0;
                    CyU3PUsbSendEP0Data(0x01, ep0Buf);
                }
                else
                    CyU3PUsbStall(0, CyTrue, CyFalse);
                break;

            case CY_U3P_USB_SC_GET_DESCRIPTOR:
                /* The HID Report descriptor is not part of the standard descriptor set
                 * that the USB core serves automatically, so it must be handled here. */
                if ((bTarget == CY_U3P_USB_TARGET_INTF) && ((wValue >> 8) == CY_FX_HID_REPORT_DSCR_TYPE))
                {
                    isHandled = CyTrue;
                    if (wIndex == CY_FX_USB_HID_INTF)
                        CyU3PUsbSendEP0Data(CY_FX_HID_REPORT_DSCR_LENGTH, (uint8_t*)CyFxUSBHidReportDscr);
                    else
                        CyU3PUsbStall(0, CyTrue, CyFalse);
                }
                break;

            default:
                break;
        }
    }

    /* HID class specific request handling for the one Keyboard + Mouse interface. */
    if ((bType == CY_FX_MSC_USB_CLASS_REQ) && (bTarget == CY_U3P_USB_TARGET_INTF) && (wIndex == CY_FX_USB_HID_INTF))
    {
        switch (bRequest)
        {
            case CY_FX_HID_SET_REPORT:
                isHandled = CyTrue;
                if (wLength == 0)
                    CyU3PUsbAckSetup();
                else
                {
                    /* The output report with the keyboard LEDs (num lock, caps lock, ...): report ID 1 + 1 byte (1 byte in boot protocol). */
                    if (ClientPeriphSetReport(wLength) == CyFalse)
                        CyU3PUsbStall(0, CyTrue, CyFalse);
                }
                break;

            case CY_FX_HID_SET_PROTOCOL:
                isHandled = CyTrue;
                if (wLength == 0)
                {
                    ClientPeriphSetProtocol((uint8_t)wValue);
                    CyU3PUsbAckSetup();
                }
                else
                    CyU3PUsbStall(0, CyTrue, CyFalse);
                break;

            case CY_FX_HID_SET_IDLE:
                isHandled = CyTrue;
                if (wLength == 0)
                    CyU3PUsbAckSetup();
                else
                    CyU3PUsbStall(0, CyTrue, CyFalse);
                break;

            case CY_FX_HID_GET_IDLE:
                isHandled = CyTrue;
                ep0Buf[0] = 0;
                CyU3PUsbSendEP0Data(0x01, ep0Buf);
                break;

            case CY_FX_HID_GET_PROTOCOL:
                isHandled = CyTrue;
                ep0Buf[0] = ClientPeriphGetProtocol();      /* 0 boot protocol, 1 report protocol */
                CyU3PUsbSendEP0Data(0x01, ep0Buf);
                break;

            case CY_FX_HID_GET_REPORT:
                isHandled = CyTrue;
                if (ClientPeriphGetProtocol() == 0)
                {
                    CyU3PUsbSendEP0Data(0x08, hidReportBuf + 1);        /* boot keyboard report, no ID */
                }
                else if ((wValue & 0xFF) == 2)
                {
                    hidReportBuf[0] = 2;
                    CyU3PUsbSendEP0Data(0x05, hidReportBuf);            /* mouse: ID + 4 bytes */
                }
                else
                {
                    hidReportBuf[0] = 1;
                    CyU3PUsbSendEP0Data(0x09, hidReportBuf);            /* keyboard: ID + 8 bytes */
                }
                break;

            default:
                break;
        }
    }

    /* CDC-ACM class requests of the serial port's communication interface. */
    if ((bType == CY_FX_MSC_USB_CLASS_REQ) && (bTarget == CY_U3P_USB_TARGET_INTF) && (wIndex == CY_FX_USB_SERIAL_COMM_INTF))
    {
        if (ClientPeriphCdcSetup(bRequest, wValue, wLength) == CyTrue)
            isHandled = CyTrue;
    }

    /* MSC class specific request handling. */
    if (bType == CY_FX_MSC_USB_CLASS_REQ)
    {
        //CyU3PDebugPrint(4, "CyFx    Setup: bType == CY_FX_MSC_USB_CLASS_REQ - bTarget=%d wIndex=%d wValue=%d\r\n", bTarget, wIndex, wValue);

    	if ((bTarget == CY_U3P_USB_TARGET_INTF) &&
            (wIndex == CY_FX_USB_MSC_INTF1 || wIndex == CY_FX_USB_MSC_INTF2) && (wValue == 0 || wValue == 1))
        {
            /* Get MAX LUN Request */
            if (bRequest == CY_FX_MSC_GET_MAX_LUN_REQ)
            {
                if (wLength == 1)
                {
                    isHandled = CyTrue;
                    ep0Buf[0] = 0;      /* Single LUN. */
                    CyU3PUsbSendEP0Data(0x01, ep0Buf);
                }
            }

            /* BOT Reset Request */
            if (bRequest == CY_FX_MSC_BOT_RESET_REQ)
            {
                isHandled = CyTrue;
                glInPhaseError_CDROM = CyFalse;
                glInPhaseError_STORAGE = CyFalse;

                if (wLength == 0)
                {
                    CyU3PUsbAckSetup();
                    CyFxMscApplnResetDatapath_CDROM();
                    CyFxMscApplnResetDatapath_STORAGE();

                    /* Inform the thread to start waiting for USB CBWs again. */
                    CyU3PEventSet(&glMscAppEvent_CDROM, CY_FX_MSC_SETCONF_EVENT_FLAG, CYU3P_EVENT_OR);
                    CyU3PEventSet(&glMscAppEvent_STORAGE, CY_FX_MSC_SETCONF_EVENT_FLAG, CYU3P_EVENT_OR);
                }
                else
                {
                    CyU3PUsbStall(0x00, CyTrue, CyFalse);
                    CyU3PUsbStall(0x01, CyTrue, CyFalse);
                }
            }
        }
    }

    /*
    if (isHandled == CyTrue)
        CyU3PDebugPrint(4, "CyFx    Setup: HANDLED=true\r\n");
    else
        CyU3PDebugPrint(4, "CyFx    Setup: HANDLED=false\r\n");
    */

    return isHandled;
}

void CyFxMscApplnResetDatapath(void)
{
	CyFxMscApplnResetDatapath_CDROM();
	CyFxMscApplnResetDatapath_STORAGE();
}

/* This is the Callback function to handle the USB Events */
void CyFxMscApplnUSBEventCB(   
    CyU3PUsbEventType_t evtype, /* Event type */    
    uint16_t            evdata  /* Event data */
)
{
    //CyU3PDebugPrint(4, "CyFxMscApplnUSBEventCB: evtype=%d evdata=%d\r\n", evtype, evdata);

	CyFxMscApplnUSBEventCB_CDROM (evtype, evdata);
	CyFxMscApplnUSBEventCB_STORAGE (evtype, evdata);
	ClientPeriphUsbEvent (evtype, evdata);
	CyFxPmApplnUSBEventCB (evtype, evdata);

    //CyU3PDebugPrint(4, "CyFxMscApplnUSBEventCB: evtype=%d evdata=%d -- DONE\r\n", evtype, evdata);
}

/* Callback function to handle LPM requests from the USB 3.0 host. This function is invoked by the API
   whenever a state change from U0 -> U1 or U0 -> U2 happens. If we return CyTrue from this function, the
   FX3S device is retained in the low power state. If we return CyFalse, the FX3S device immediately tries
   to trigger an exit back to U0.
 */
CyBool_t CyFxMscApplnLPMRqtCB(CyU3PUsbLinkPowerMode link_mode)
{
    /* The link never goes into U1 / U2: with the low power states allowed, some USB 3 host controllers poll the HID interrupt endpoint
     * only about once per second, so keys arrive seconds late. A key must reach the target PC at once, so every request is refused. */
    return (CyFalse);
}

/* Configure the endpoints required for the mass storage device operation. */
void CyFxMscApplnConfigEndpoints(CyBool_t enable)
{
    CyU3PEpConfig_t epConf = { 0 };
    CyU3PReturnStatus_t status;

    //CyU3PDebugPrint(4, "CyFxMscApplnConfigEndpoints() %d\r\n", enable);

    if (enable)
    {
        epConf.enable = CyTrue;
        epConf.epType = CY_U3P_USB_EP_BULK;
        epConf.streams = 0;
        epConf.pcktSize = 1024;                     /* Configuring for super speed by default. */
        epConf.burstLen = 1;
        epConf.isoPkts = 0;
    }
    else
    {
        epConf.enable = CyFalse;
    }

    status = CyU3PSetEpConfig(CY_FX_EP_CD_ROM_OUT, &epConf);
    if (status != CY_U3P_SUCCESS)
    {
        CyU3PDebugPrint(4, "USB Set CD EP config failed, code=%d\r\n", status);
        CyFxAppErrorHandler(status);
    }

    status = CyU3PSetEpConfig(CY_FX_EP_CD_ROM_IN, &epConf);
    if (status != CY_U3P_SUCCESS)
    {
        CyU3PDebugPrint(4, "USB Set CD EP config failed, code=%d\r\n", status);
        CyFxAppErrorHandler(status);
    }

    /* The disk's endpoints only exist while the disk interface is part of the configuration. */
    if (UsbDiskInterface == CyFalse)
        epConf.enable = CyFalse;

    status = CyU3PSetEpConfig(CY_FX_EP_STORAGE_OUT, &epConf);
    if (status != CY_U3P_SUCCESS)
    {
        CyU3PDebugPrint(4, "USB Set STOR EP config failed, code=%d\r\n", status);
        CyFxAppErrorHandler(status);
    }

    status = CyU3PSetEpConfig(CY_FX_EP_STORAGE_IN, &epConf);
    if (status != CY_U3P_SUCCESS)
    {
        CyU3PDebugPrint(4, "USB Set STOR EP config failed, code=%d\r\n", status);
        CyFxAppErrorHandler(status);
    }

    if (enable)
    {
        epConf.enable = CyTrue;
        epConf.epType = CY_U3P_USB_EP_INTR;
        epConf.streams = 0;
        epConf.pcktSize = 16;                       /* Report ID + keyboard report (8 bytes) or report ID + mouse report (4 bytes). */
        epConf.burstLen = 1;
        epConf.isoPkts = 0;
    }
    else
    {
        epConf.enable = CyFalse;
    }

    status = CyU3PSetEpConfig(CY_FX_EP_KEYBOARD_IN, &epConf);
    if (status != CY_U3P_SUCCESS)
    {
        CyU3PDebugPrint(4, "USB Set KEYBOARD EP config failed, code=%d\r\n", status);
        CyFxAppErrorHandler(status);
    }

    if (enable)
    {
        epConf.enable = CyTrue;
        epConf.epType = CY_U3P_USB_EP_BULK;
        epConf.streams = 0;
        epConf.pcktSize = 1024;                     /* Configuring for super speed by default. */
        epConf.burstLen = 1;
        epConf.isoPkts = 0;
    }
    else
    {
        epConf.enable = CyFalse;
    }

    status = CyU3PSetEpConfig(CY_FX_EP_MIDI_OUT, &epConf);
    if (status != CY_U3P_SUCCESS)
    {
        CyU3PDebugPrint(4, "USB Set MIDI EP config failed, code=%d\r\n", status);
        CyFxAppErrorHandler(status);
    }

    status = CyU3PSetEpConfig(CY_FX_EP_MIDI_IN, &epConf);
    if (status != CY_U3P_SUCCESS)
    {
        CyU3PDebugPrint(4, "USB Set MIDI EP config failed, code=%d\r\n", status);
        CyFxAppErrorHandler(status);
    }

    /* Serial port (CDC-ACM): bulk data pair, same configuration as the MIDI pair. */
    status = CyU3PSetEpConfig(CY_FX_EP_SERIAL_OUT, &epConf);
    if (status != CY_U3P_SUCCESS)
    {
        CyU3PDebugPrint(4, "USB Set SERIAL EP config failed, code=%d\r\n", status);
        CyFxAppErrorHandler(status);
    }

    status = CyU3PSetEpConfig(CY_FX_EP_SERIAL_IN, &epConf);
    if (status != CY_U3P_SUCCESS)
    {
        CyU3PDebugPrint(4, "USB Set SERIAL EP config failed, code=%d\r\n", status);
        CyFxAppErrorHandler(status);
    }

    /* ... and its notification endpoint (interrupt IN, 16 bytes). */
    if (enable)
    {
        epConf.epType = CY_U3P_USB_EP_INTR;
        epConf.pcktSize = 16;
    }

    status = CyU3PSetEpConfig(CY_FX_EP_SERIAL_NOTIFY, &epConf);
    if (status != CY_U3P_SUCCESS)
    {
        CyU3PDebugPrint(4, "USB Set SERIAL notify EP config failed, code=%d\r\n", status);
        CyFxAppErrorHandler(status);
    }

    //CyU3PDebugPrint(4, "CyFxMscApplnConfigEndpoints() %d -- DONE\r\n", enable);
}

/* ---- The USB side. The disk is the LAST interface of the configuration and exists only while a disk is loaded: Windows then shows the disk in the Device
 * Manager only while an image is in, and every load, eject or swap is a fresh enumeration (a fixed disk gets no media change notification). Showing or
 * hiding the interface restarts the whole USB stack - the same sequence the Host uses to switch into its update mode: stop what depends on the stack,
 * disconnect, stop the stack, start it again, set every descriptor again, configure the endpoints, create the DMA channels, connect. ---------------- */

#define CY_FX_USB_RESTART_GAP_MS    (100)       /* How long the Client stays disconnected before the stack is stopped (the Host's value). */


/* The configuration descriptors without their last interface (the disk): patched copies of the full ones, built once. */
static uint8_t NoDiskConfigSS[320] __attribute__((aligned(32)));
static uint8_t NoDiskConfigHS[256] __attribute__((aligned(32)));
static uint8_t NoDiskConfigFS[256] __attribute__((aligned(32)));

static void BuildNoDiskDescriptor (uint8_t *Copy, const uint8_t *Full, uint32_t BlockBytes)
{
    uint32_t Bytes = (uint32_t)Full[2] | ((uint32_t)Full[3] << 8);

    Bytes -= BlockBytes;
    CyU3PMemCopy (Copy, (uint8_t *)Full, Bytes);
    Copy[2] = (uint8_t)(Bytes & 0xFF);
    Copy[3] = (uint8_t)(Bytes >> 8);
    Copy[4] = (uint8_t)(Copy[4] - 1);               /* bNumInterfaces */
}

static void SetDescriptor (CyU3PUSBSetDescType_t Type, uint8_t Index, const uint8_t *Descriptor)
{
    CyU3PReturnStatus_t Status = CyU3PUsbSetDesc (Type, Index, (uint8_t *)Descriptor);

    if (Status != CY_U3P_SUCCESS)
    {
        CyU3PDebugPrint (4, "USB Set Descriptor (type %d, index %d) failed, Error Code = %d\r\n", Type, Index, Status);
        CyFxAppErrorHandler (Status);
    }
}

/* Sets every descriptor (a restarted USB stack has forgotten them), with or without the disk interface. */
static void ApplyUsbDescriptors (CyBool_t Disk)
{
    SetDescriptor (CY_U3P_USB_SET_HS_DEVICE_DESCR, 0, CyFxUSB20DeviceDscr);
    SetDescriptor (CY_U3P_USB_SET_SS_DEVICE_DESCR, 0, CyFxUSB30DeviceDscr);
    SetDescriptor (CY_U3P_USB_SET_DEVQUAL_DESCR, 0, CyFxUSBDeviceQualDscr);
    SetDescriptor (CY_U3P_USB_SET_HS_CONFIG_DESCR, 0, (Disk == CyTrue) ? CyFxUSBHSConfigDscr1 : NoDiskConfigHS);
    SetDescriptor (CY_U3P_USB_SET_FS_CONFIG_DESCR, 0, (Disk == CyTrue) ? CyFxUSBFSConfigDscr1 : NoDiskConfigFS);
    SetDescriptor (CY_U3P_USB_SET_SS_CONFIG_DESCR, 0, (Disk == CyTrue) ? CyFxUSBSSConfigDscr1 : NoDiskConfigSS);
    SetDescriptor (CY_U3P_USB_SET_SS_BOS_DESCR, 0, CyFxUSBBOSDscr);

    SetDescriptor (CY_U3P_USB_SET_STRING_DESCR, 0, CyFxUSBStringLangIDDscr);
    SetDescriptor (CY_U3P_USB_SET_STRING_DESCR, 1, CyFxUSBManufactureDscr);
    SetDescriptor (CY_U3P_USB_SET_STRING_DESCR, 2, CyFxUSBProductDscr);
    SetDescriptor (CY_U3P_USB_SET_STRING_DESCR, 3, CyFxUSBInterfaceDesc1);
    SetDescriptor (CY_U3P_USB_SET_STRING_DESCR, 4, CyFxUSBInterfaceDesc2);
    SetDescriptor (CY_U3P_USB_SET_STRING_DESCR, 5, CyFxUSBInterfaceDesc3);
    SetDescriptor (CY_U3P_USB_SET_STRING_DESCR, 6, CyFxUSBInterfaceDesc4);
    SetDescriptor (CY_U3P_USB_SET_STRING_DESCR, 7, CyFxUsbSN);
    SetDescriptor (CY_U3P_USB_SET_STRING_DESCR, 8, CyFxUSBInterfaceDesc5);
    SetDescriptor (CY_U3P_USB_SET_STRING_DESCR, 9, CyFxUSBInterfaceDesc6);
}

/* Starts the USB stack and everything that depends on it: callbacks, descriptors, endpoints and DMA channels. Also after CyU3PUsbStop. */
static void UsbStackUp (CyBool_t Disk)
{
    CyU3PReturnStatus_t Status;

    UsbDiskInterface = Disk;

    Status = CyU3PUsbStart ();
    if ((Status != CY_U3P_SUCCESS) && (Status != CY_U3P_ERROR_NO_REENUM_REQUIRED))
    {
        CyU3PDebugPrint (4, "USB Function Failed to Start, Error Code = %d\r\n", Status);
        CyFxAppErrorHandler (Status);
    }

    CyU3PUsbRegisterSetupCallback (CyFxMscApplnUSBSetupCB, CyTrue);
    CyU3PUsbRegisterEventCallback (CyFxMscApplnUSBEventCB);
    CyU3PUsbRegisterLPMRequestCallback (CyFxMscApplnLPMRqtCB);

    ApplyUsbDescriptors (Disk);

    /* Configure the USB endpoints as required. We configure for SuperSpeed and update at runtime as required. */
    CyFxMscApplnConfigEndpoints (CyTrue);

    /* Create the required DMA channels. */
    CyFxMscApplnDmaInit_CDROM ();
    if (Disk == CyTrue)
        CyFxMscApplnDmaInit_STORAGE ();
    ClientPeriphDmaInit ();
}

/* The last interface number the USB configuration of the moment has. */
static uint16_t UsbLastInterface (void)
{
    return ((UsbDiskInterface == CyTrue) ? CY_FX_USB_LAST_INTF : CY_FX_USB_LAST_INTF_NO_DISK);
}

void CyFxMscApplnInit (void)
{
    /* Setup the Callback to Handle the USB Setup Requests: see UsbStackUp. */
    BuildNoDiskDescriptor (NoDiskConfigSS, CyFxUSBSSConfigDscr1, CY_FX_DISK_BLOCK_BYTES_SS);
    BuildNoDiskDescriptor (NoDiskConfigHS, CyFxUSBHSConfigDscr1, CY_FX_DISK_BLOCK_BYTES_HS_FS);
    BuildNoDiskDescriptor (NoDiskConfigFS, CyFxUSBFSConfigDscr1, CY_FX_DISK_BLOCK_BYTES_HS_FS);

    /* No disk is loaded at start-up. */
    UsbStackUp (CyFalse);

#if (CY_FX_CLIENT_USB_WAIT_FOR_HOST == 1)
    CyU3PDebugPrint (4, "USB stays off until the Host says go\r\n");
#else
    CyFxClientUsbConnect ();
#endif
}

/* Stops the USB stack and starts it again with or without the disk interface; connected again if it was connected. Helper thread only. */
static void UsbRestart (CyBool_t Disk)
{
    CyBool_t WasConnected = UsbConnected;

    CyU3PDebugPrint (4, "USB restart, disk interface %d\r\n", Disk);

    /* A disconnect that the Client starts itself raises no USB event: tell every function by hand, as a real DISCONNECT would. */
    if (WasConnected == CyTrue)
        CyFxMscApplnUSBEventCB (CY_U3P_USB_EVENT_DISCONNECT, 0);

    ClientPeriphDmaDeInit ();
    CyFxMscApplnDmaDeInit_CDROM ();
    CyFxMscApplnDmaDeInit_STORAGE ();
    CyFxMscApplnConfigEndpoints (CyFalse);

    if (WasConnected == CyTrue)
    {
        CyU3PConnectState (CyFalse, CyTrue);
        CyU3PThreadSleep (CY_FX_USB_RESTART_GAP_MS);
    }

    CyU3PUsbStop ();
    UsbStackUp (Disk);

    if (WasConnected == CyTrue)
    {
#ifdef CLIENT_USB_HIGH_SPEED_ONLY
        CyU3PConnectState (CyTrue, CyFalse);
#else
        CyU3PConnectState (CyTrue, CyTrue);
#endif
        GuardUsbConnected ();
    }
}

/* The disk was loaded, ejected or swapped (any thread): the helper thread restarts the USB stack with or without the disk interface. */
void CyFxClientUsbDiskChanged (CyBool_t Present)
{
    UsbDiskWanted = Present;
    UsbRestartRequested = CyTrue;
    ClientLinkWakeHelper ();
}

CyBool_t CyFxClientUsbRestartPending (void)
{
    return (UsbRestartRequested);
}

/* Carries out a pending restart (helper thread). The update mode owns the USB side while it runs; the Host resets the Client when it is over. */
void CyFxClientUsbRestartRun (void)
{
    while (UsbRestartRequested == CyTrue)
    {
        UsbRestartRequested = CyFalse;

        if (UpdateIsActive () == CyTrue)
            continue;

        /* A disk that came and went before anything was shown: nothing to hide. */
        if ((UsbDiskWanted == CyFalse) && (UsbDiskInterface == CyFalse))
            continue;

        /* Not connected yet and nothing to change: the first connect takes it from here. */
        if ((UsbConnected == CyFalse) && (UsbDiskInterface == UsbDiskWanted))
            continue;

        UsbRestart (UsbDiskWanted);
    }
}

/* Connects the Client to the PC's USB (once). Called from the application at start-up, or on the Host's command from the link helper thread. */
void CyFxClientUsbConnect (void)
{
    CyU3PReturnStatus_t apiRetStatus;

    if (UsbConnected == CyTrue)
        return;

    /* A disk that was loaded before the first connect: the configuration is changed first. */
    if (UsbDiskInterface != UsbDiskWanted)
        UsbRestart (UsbDiskWanted);

    CyU3PDebugPrint (4, "Calling USB Connect\r\n");

    /* Enable USB connection to host. */
#ifdef CLIENT_USB_HIGH_SPEED_ONLY
    apiRetStatus = CyU3PConnectState(CyTrue, CyFalse);
#else
    apiRetStatus = CyU3PConnectState(CyTrue, CyTrue);
#endif
    if (apiRetStatus != CY_U3P_SUCCESS)
    {
        CyU3PDebugPrint(4, "USB Connect failed, Error Code = %d\n", apiRetStatus);
        CyFxAppErrorHandler(apiRetStatus);
    }
    GuardUsbConnected();
    UsbConnected = CyTrue;
}

CyBool_t CyFxClientUsbIsConnected(void)
{
    return (UsbConnected);
}

/*
 * Application define function which creates the threads. This is called from
 * the tx_application _define function.
 */

void CyFxApplicationDefine(void)
{
    /* Initialize the Debug Module */
    CyFxMscApplnDebugInit();
    ClientLinkInit();
    CyFxMscApplnInit();

	uint32_t retThrdCreate = CY_U3P_ERROR_MEMORY_ERROR;
    void* ptr_cdrom;

    /* Allocate the memory for the threads */
    ptr_cdrom = CyU3PMemAlloc(CY_FX_MSC_THREAD_STACK);

    if (ptr_cdrom != 0)
    {
        /* Create the thread for the application */
        retThrdCreate = CyU3PThreadCreate(&mscAppThread_CDROM,       /* MSC App Thread structure */
            "25:MSC Application CD",                           /* Thread ID and Thread name */
            MscAppThread_Entry_CDROM,                             /* MSC App Thread Entry function */
            0,                                              /* No input parameter to thread */
            ptr_cdrom,                                            /* Pointer to the allocated thread stack */
            CY_FX_MSC_THREAD_STACK,                         /* MSC App Thread stack size */
            CY_FX_MSC_THREAD_PRIORITY,                      /* MSC App Thread priority */
            CY_FX_MSC_THREAD_PRIORITY,                      /* MSC App Thread priority */
            CYU3P_NO_TIME_SLICE,                            /* No time slice for the application thread */
            CYU3P_AUTO_START                                /* Start the Thread immediately */
        );
    }

    /* Check the return code */
    if (retThrdCreate != 0)
    {
        /* Thread Creation failed with the error code retThrdCreate */
        CyU3PDebugPrint(4, "CDROM Thread creation failed: %d\r\n", retThrdCreate);

        /* Add custom recovery or debug actions here */

        /* Application cannot continue */
        /* Loop indefinitely */
        while (1);
    }

    retThrdCreate = CY_U3P_ERROR_MEMORY_ERROR;
    void* ptr_storage;

    /* Allocate the memory for the threads */
    ptr_storage = CyU3PMemAlloc(CY_FX_MSC_THREAD_STACK);

    if (ptr_storage != 0)
    {
        /* Create the thread for the application */
        retThrdCreate = CyU3PThreadCreate(&mscAppThread_STORAGE,       /* MSC App Thread structure */
            "25:MSC Application STOR",                           /* Thread ID and Thread name */
            MscAppThread_Entry_STORAGE,                             /* MSC App Thread Entry function */
            0,                                              /* No input parameter to thread */
            ptr_storage,                                            /* Pointer to the allocated thread stack */
            CY_FX_MSC_THREAD_STACK,                         /* MSC App Thread stack size */
            CY_FX_MSC_THREAD_PRIORITY,                      /* MSC App Thread priority */
            CY_FX_MSC_THREAD_PRIORITY,                      /* MSC App Thread priority */
            CYU3P_NO_TIME_SLICE,                            /* No time slice for the application thread */
            CYU3P_AUTO_START                                /* Start the Thread immediately */
        );
    }

    /* Check the return code */
    if (retThrdCreate != 0)
    {
        /* Thread Creation failed with the error code retThrdCreate */
        CyU3PDebugPrint(4, "STORAGE Thread creation failed: %d\r\n", retThrdCreate);

        /* Add custom recovery or debug actions here */

        /* Application cannot continue */
        /* Loop indefinitely */
        while (1);
    }

    retThrdCreate = CY_U3P_ERROR_MEMORY_ERROR;
    void* ptr_pm;

    /* Allocate the memory for the thread */
    ptr_pm = CyU3PMemAlloc(CY_FX_PM_THREAD_STACK);

    if (ptr_pm != 0)
    {
        /* Create the thread for the application */
        retThrdCreate = CyU3PThreadCreate(&pmAppThread,             /* PM App Thread structure */
            "26:Power Management",                          /* Thread ID and Thread name */
            PmAppThread_Entry,                              /* PM App Thread Entry function */
            0,                                              /* No input parameter to thread */
            ptr_pm,                                         /* Pointer to the allocated thread stack */
            CY_FX_PM_THREAD_STACK,                          /* PM App Thread stack size */
            CY_FX_PM_THREAD_PRIORITY,                       /* PM App Thread priority */
            CY_FX_PM_THREAD_PRIORITY,                       /* PM App Thread priority */
            CYU3P_NO_TIME_SLICE,                            /* No time slice for the application thread */
            CYU3P_AUTO_START                                /* Start the Thread immediately */
        );
    }

    GuardStart();

    /* Check the return code */
    if (retThrdCreate != 0)
    {
        /* Thread Creation failed with the error code retThrdCreate */
        CyU3PDebugPrint(4, "PM Thread creation failed: %d\r\n", retThrdCreate);

        /* Add custom recovery or debug actions here */

        /* Application cannot continue */
        /* Loop indefinitely */
        while (1);
    }
}

/* [ ] */

