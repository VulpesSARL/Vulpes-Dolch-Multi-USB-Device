#include "cyu3system.h"
#include "cyu3os.h"
#include "cyu3dma.h"
#include "cyu3error.h"
#include "cyu3usb.h"
#include "cyu3usbconst.h"
#include "main.h"
#include "LINK.h"
#include "BULK.h"

#define BULK_BUFFER_BYTES       (16384)
#define BULK_BUFFER_COUNT       (2)

static CyU3PDmaChannel BulkToClient;                 /* bulk OUT 2 -> PIB socket 2 */
static CyBool_t BulkUp = CyFalse;
static CyBool_t EndpointsUp = CyFalse;

CyBool_t HostBulkStart (void)
{
    CyU3PEpConfig_t EpConfig;
    CyU3PDmaChannelConfig_t Config;
    CyU3PUSBSpeed_t Speed = CyU3PUsbGetSpeed ();
    CyU3PReturnStatus_t Status;
    uint16_t PacketSize;

    if (BulkUp == CyTrue)
        return (CyTrue);

    if (Speed == CY_U3P_SUPER_SPEED)
        PacketSize = 1024;
    else if (Speed == CY_U3P_HIGH_SPEED)
        PacketSize = 512;
    else
        PacketSize = 64;

    CyU3PMemSet ((uint8_t *)&EpConfig, 0, sizeof (EpConfig));
    EpConfig.enable = CyTrue;
    EpConfig.epType = CY_U3P_USB_EP_BULK;
    EpConfig.burstLen = (Speed == CY_U3P_SUPER_SPEED) ? CY_FX_LINK_EP_BURST : 1;
    EpConfig.streams = 0;
    EpConfig.pcktSize = PacketSize;
    EpConfig.isoPkts = 0;
    Status = CyU3PSetEpConfig (CY_FX_EP_PRODUCER2, &EpConfig);
    if (Status == CY_U3P_SUCCESS)
        Status = CyU3PSetEpConfig (CY_FX_EP_CONSUMER2, &EpConfig);
    if (Status != CY_U3P_SUCCESS)
    {
        CyU3PDebugPrint (4, "Bulk lane: endpoint setup failed, Error code = %d\r\n", Status);
        return (CyFalse);
    }
    EndpointsUp = CyTrue;
    CyU3PUsbFlushEp (CY_FX_EP_PRODUCER2);
    CyU3PUsbFlushEp (CY_FX_EP_CONSUMER2);

    CyU3PMemSet ((uint8_t *)&Config, 0, sizeof (Config));
    Config.size = BULK_BUFFER_BYTES;
    Config.count = BULK_BUFFER_COUNT;
    Config.prodSckId = CY_U3P_UIB_SOCKET_PROD_2;
    Config.consSckId = CY_U3P_PIB_SOCKET_2;
    Config.dmaMode = CY_U3P_DMA_MODE_BYTE;
    Status = CyU3PDmaChannelCreate (&BulkToClient, CY_U3P_DMA_TYPE_AUTO, &Config);
    if (Status != CY_U3P_SUCCESS)
    {
        CyU3PDebugPrint (4, "Bulk lane: channel create failed, Error code = %d\r\n", Status);
        return (CyFalse);
    }

    BulkUp = CyTrue;
    return (CyTrue);
}

void HostBulkStop (void)
{
    CyU3PEpConfig_t EpConfig;

    if (BulkUp == CyTrue)
    {
        CyU3PDmaChannelDestroy (&BulkToClient);
        BulkUp = CyFalse;
    }

    if (EndpointsUp == CyTrue)
    {
        CyU3PUsbFlushEp (CY_FX_EP_PRODUCER2);
        CyU3PUsbFlushEp (CY_FX_EP_CONSUMER2);
        CyU3PMemSet ((uint8_t *)&EpConfig, 0, sizeof (EpConfig));
        EpConfig.enable = CyFalse;
        CyU3PSetEpConfig (CY_FX_EP_PRODUCER2, &EpConfig);
        CyU3PSetEpConfig (CY_FX_EP_CONSUMER2, &EpConfig);
        EndpointsUp = CyFalse;
    }
}

uint8_t HostBulkArm (uint32_t Bytes)
{
    CyU3PReturnStatus_t Status;

    if (BulkUp == CyFalse)
        return (LINK_ERROR_UNKNOWN_TYPE);

    if ((Bytes == 0) || ((Bytes & (BULK_BUFFER_BYTES - 1)) != 0))
        return (LINK_ERROR_BAD_LENGTH);

    /* Whatever an earlier transfer left unfinished is thrown away. */
    CyU3PDmaChannelReset (&BulkToClient);
    Status = CyU3PDmaChannelSetXfer (&BulkToClient, Bytes);
    if (Status != CY_U3P_SUCCESS)
    {
        CyU3PDebugPrint (4, "Bulk lane: arming %d bytes failed, Error code = %d\r\n", Bytes, Status);
        return (LINK_ERROR_BUSY);
    }

    return (0);
}
