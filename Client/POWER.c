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

/* CyU3PSysEnterSuspendMode() is a device-wide power transition (halts the CPU core and
 * reparks clocks/PLLs) - it must only ever be called from a single thread, because several
 * threads reacting to the same USB suspend event corrupt the firmware state in a timing-dependent
 * way. This thread is the sole owner of that call. */

void CyFxPmApplnUSBEventCB(CyU3PUsbEventType_t evtype, uint16_t evdata)
{
    switch (evtype)
    {
        case CY_U3P_USB_EVENT_SUSPEND:
            CyU3PEventSet(&glPmAppEvent, CY_FX_PM_USB_SUSP_EVENT_FLAG, CYU3P_EVENT_OR);
            break;

        default:
            break;
    }
}

void CyFxPmAppHandleTask(void)
{
    CyU3PReturnStatus_t status;
    uint32_t evStat;
    uint16_t wakeReason;

    for (;;)
    {
        status = CyU3PEventGet(&glPmAppEvent, CY_FX_PM_USB_SUSP_EVENT_FLAG, CYU3P_EVENT_OR_CLEAR, &evStat, CYU3P_WAIT_FOREVER);
        if (status == CY_U3P_SUCCESS)
        {
            if (evStat & CY_FX_PM_USB_SUSP_EVENT_FLAG)
            {
#if (CY_FX_PM_ALLOW_CPU_SUSPEND == 1)
                /* Place FX3S in Low Power Suspend mode, with USB bus activity as the wakeup source. */
                status = CyU3PSysEnterSuspendMode(CY_U3P_SYS_USB_BUS_ACTVTY_WAKEUP_SRC, 0, &wakeReason);
                CyU3PDebugPrint(4, "CyFxPmAppHandleTask() EnterSuspendMode returned %d, wakeup reason=%d\r\n", status, wakeReason);
#else
                /* The USB side sleeps on its own; the CPU keeps running so the GPIF link to the Host stays alive. */
                wakeReason = 0;
                status = CY_U3P_SUCCESS;
                CyU3PDebugPrint(4, "USB suspend: the CPU keeps running (CY_FX_PM_ALLOW_CPU_SUSPEND is 0)\r\n");
#endif
            }
        }
    }
}

void PmAppThread_Entry(uint32_t input)
{
    uint32_t txApiRetStatus;

    /* Create the power management event group. */
    txApiRetStatus = CyU3PEventCreate(&glPmAppEvent);
    if (txApiRetStatus != 0)
    {
        CyU3PDebugPrint(4, "PM Appln Create Event Failed, Error Code = %d\n", txApiRetStatus);
        CyFxAppErrorHandler(CY_U3P_ERROR_MEMORY_ERROR);
    }

    CyFxPmAppHandleTask();
}
