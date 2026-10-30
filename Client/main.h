/* This file contains the constants used by the bulk source sink application example */

#ifndef _INCLUDED_CYFXBULKSRCSINK_H_
#define _INCLUDED_CYFXBULKSRCSINK_H_

#include "cyu3types.h"
#include "cyu3usbconst.h"
#include "cyu3externcstart.h"

#define CY_FX_EP_CD_ROM_OUT              0x01    /* EP 1 OUT */
#define CY_FX_EP_CD_ROM_IN               0x81    /* EP 1 IN */
#define CY_FX_EP_STORAGE_OUT             0x02    /* EP 2 OUT */
#define CY_FX_EP_STORAGE_IN              0x82    /* EP 2 IN */
#define CY_FX_EP_KEYBOARD_IN             0x83    /* EP 3 IN - HID reports of the ONE keyboard + mouse interface (report IDs 1 and 2) */
#define CY_FX_EP_MIDI_OUT                0x05    /* EP 5 OUT - USB-MIDI event packets from host */
#define CY_FX_EP_MIDI_IN                 0x85    /* EP 5 IN - USB-MIDI event packets to host */
#define CY_FX_EP_SERIAL_OUT              0x06    /* EP 6 OUT - CDC-ACM serial port data from the host */
#define CY_FX_EP_SERIAL_IN               0x86    /* EP 6 IN - CDC-ACM serial port data to the host */
#define CY_FX_EP_SERIAL_NOTIFY           0x87    /* EP 7 IN - CDC-ACM notifications (SERIAL_STATE), interrupt */

#define CY_FX_EP_CD_ROM_OUT_SOCKET        0x01    /* Socket 1 is producer */
#define CY_FX_EP_CD_ROM_IN_SOCKET         0x01    /* Socket 1 is consumer */
#define CY_FX_EP_STORAGE_OUT_SOCKET       0x02    /* Socket 2 is producer */
#define CY_FX_EP_STORAGE_IN_SOCKET        0x02    /* Socket 2 is consumer */
#define CY_FX_EP_KEYBOARD_IN_SOCKET       0x03    /* Socket 3 is consumer */
#define CY_FX_EP_MIDI_OUT_SOCKET          0x05    /* Socket 5 is producer */
#define CY_FX_EP_MIDI_IN_SOCKET           0x05    /* Socket 5 is consumer */
#define CY_FX_EP_SERIAL_OUT_SOCKET        0x06    /* Socket 6 is producer */
#define CY_FX_EP_SERIAL_IN_SOCKET         0x06    /* Socket 6 is consumer */
#define CY_FX_EP_SERIAL_NOTIFY_SOCKET     0x07    /* Socket 7 is consumer */

#define CY_FX_MSC_DMA_BUF_COUNT      (2)                       /* MSC channel buffer count */

#define CY_FX_MSC_THREAD_STACK       (0x1000)                  /* MSC application thread stack size */
#define CY_FX_MSC_THREAD_PRIORITY    (11)                      /* MSC application thread priority */

#define CY_FX_PM_THREAD_STACK        (0x400)                   /* Power management application thread stack size */
#define CY_FX_PM_THREAD_PRIORITY     (8)                       /* Power management application thread priority */

/* 0 (default): a USB suspend only puts the USB side to sleep (the USB core does that by itself); the CPU keeps running. 1: the
 * CPU is halted with CyU3PSysEnterSuspendMode as well, which also stops the GPIF link to the Host, so the Host sees the Client
 * as lost after 3.5 s and resets it. The only wake-up sources of the CPU suspend are USB activity, UART CTS,
 * VBUS and OTG ID, none of which the Host can drive. */
#define CY_FX_PM_ALLOW_CPU_SUSPEND   (0)

/* 1: the Client keeps its USB side switched off after start-up until the Host tells it to connect (control message CLIENT_USB); the Client
 * then shows up on the PC only once the Host is alive, and not at all while it is in update mode. 0: connect at start-up. */
#ifndef CY_FX_CLIENT_USB_WAIT_FOR_HOST
#define CY_FX_CLIENT_USB_WAIT_FOR_HOST (1)
#endif

#define CY_FX_MSC_CLR_STALL_IN_EVENT_FLAG       (1 << 0)       /* MSC application Clear Stall IN Event Flag */
#define CY_FX_MSC_RESET_EVENT_FLAG              (1 << 1)       /* USB reset event. */
#define CY_FX_MSC_SETCONF_EVENT_FLAG            (1 << 2)       /* SET Config event */
#define CY_FX_MSC_CBW_EVENT_FLAG                (1 << 3)       /* MSC CBW received. */
#define CY_FX_MSC_DATASENT_EVENT_FLAG           (1 << 4)       /* Data has been sent to the host. */
#define CY_FX_MSC_SIBCB_EVENT_FLAG              (1 << 5)       /* SIB event callback flag. */
#define CY_FX_MSC_DATA_RECV_EVENT_FLAG          (1 << 7)       /* Data received on USB endpoint. */

/* USB Suspend is a device-wide condition, handled solely by the power management thread
 * (POWER.c) - CyU3PSysEnterSuspendMode() must never be called from more than one thread. */
#define CY_FX_PM_USB_SUSP_EVENT_FLAG            (1 << 0)       /* USB Suspend event flag. */

#define CY_FX_MSC_USB_REQ_MASK                  (0x60)         /* Request type mask */
#define CY_FX_MSC_USB_STANDARD_REQ              (0x00)         /* Standard request type */
#define CY_FX_MSC_USB_CLASS_REQ                 (0x20)         /* Class request type */
#define CY_FX_MSC_USB_VENDOR_REQ                (0x40)         /* Vendor request type */

#define CY_FX_MSC_GET_MAX_LUN_REQ               (0xFE)         /* MSC Get Max LUN request */
#define CY_FX_MSC_BOT_RESET_REQ                 (0xFF)         /* MSC BOT Reset request */
#define CY_FX_MSC_USB_REQ_WINDEX_MASK           (0x0000FFFF)   /* USB Request wIndex mask */

#define CY_FX_EP_BURST_LENGTH                   (4)


/* Interface numbers. The disk is the LAST interface: while no disk is loaded the configuration descriptors end before it (main.c builds those
 * copies), so none of the other numbers move when it comes and goes. */
#define CY_FX_USB_MSC_INTF1                      (0x00)    /* CD-ROM */
#define CY_FX_USB_HID_INTF                       (0x01)    /* keyboard + mouse in ONE interface (report IDs 1 / 2) */
#define CY_FX_USB_KEYBOARD_INTF                  CY_FX_USB_HID_INTF
#define CY_FX_USB_MIDI_CONTROL_INTF              (0x02)
#define CY_FX_USB_MIDI_STREAMING_INTF            (0x03)
#define CY_FX_USB_SERIAL_COMM_INTF               (0x04)
#define CY_FX_USB_SERIAL_DATA_INTF               (0x05)
#define CY_FX_USB_MSC_INTF2                      (0x06)    /* disk */
#define CY_FX_USB_LAST_INTF                      (0x06)
#define CY_FX_USB_LAST_INTF_NO_DISK              (0x05)

/* Bytes of the disk interface (interface descriptor + its two endpoints, with the companion descriptors at SuperSpeed) at the end of the
 * configuration descriptors. */
#define CY_FX_DISK_BLOCK_BYTES_SS                (35)
#define CY_FX_DISK_BLOCK_BYTES_HS_FS             (23)

/* Descriptor Types */
#define CY_FX_BOS_DSCR_TYPE                     (15)
#define CY_FX_DEVICE_CAPB_DSCR_TYPE             (16)
#define CY_FX_SS_EP_COMPN_DSCR_TYPE             (48)
#define CY_FX_HID_DSCR_TYPE                     (0x21)
#define CY_FX_HID_REPORT_DSCR_TYPE              (0x22)
#define CY_FX_HID_REPORT_DSCR_LENGTH            (0x7B)          /* bytes of CyFxUSBHidReportDscr (keyboard 65 + mouse 58) */

/* HID class-specific requests (USB HID spec section 7.2) */
#define CY_FX_HID_GET_REPORT                    (0x01)
#define CY_FX_HID_GET_IDLE                      (0x02)
#define CY_FX_HID_GET_PROTOCOL                  (0x03)
#define CY_FX_HID_SET_REPORT                    (0x09)
#define CY_FX_HID_SET_IDLE                      (0x0A)
#define CY_FX_HID_SET_PROTOCOL                  (0x0B)

/* Device Capability Type Codes */
#define CY_FX_WIRELESS_USB_CAPB_TYPE            (1)
#define CY_FX_USB2_EXTN_CAPB_TYPE               (2)
#define CY_FX_SS_USB_CAPB_TYPE                  (3)
#define CY_FX_CONTAINER_ID_CAPBD_TYPE           (4)

typedef enum
{
    CY_FX_CBW_CMD_PASSED = 0,
    CY_FX_CBW_CMD_FAILED,
    CY_FX_CBW_CMD_PHASE_ERROR,
    CY_FX_CBW_CMD_MSC_RESET
} CyFxMscCswReturnStatus_t;

/* Current state of the MSC function state machine. */
typedef enum
{
    CY_FX_MSC_STATE_INACTIVE = 0,               /* Inactive state, waiting for SET_CONFIG. */
    CY_FX_MSC_STATE_CBW,                        /* Waiting to queue a CBW command. */
    CY_FX_MSC_STATE_WAITING,                    /* Waiting to receive a CBW command. */
    CY_FX_MSC_STATE_DATA,                       /* Waiting to complete data transfer for a command. */
    CY_FX_MSC_STATE_STATUS,                     /* Waiting to send CSW for a command. */
    CY_FX_MSC_STATE_CSW                         /* Waiting for host to read out CSW packet. */
} CyFxMscFuncState;

#define CY_FX_MSC_CBW_MAX_COUNT                 (31u)
#define CY_FX_MSC_CSW_MAX_COUNT                 (13u)
#define CY_FX_MSC_REPONSE_DATA_MAX_COUNT        (18u)

#define CY_FX_SCSI_INQUIRY_DATALEN              (96)
#define CY_FX_SCSI_INQCMD_EVPD_OFFSET           (16u)
#define CY_FX_SCSI_INQCMD_EVPD_MASK             (0x01u)
#define CY_FX_SCSI_INQCMD_PCODE_OFFSET          (17u)

#define CY_FX_SCSI_INQCMD_PCODE_LIST            (0x00u)
#define CY_FX_SCSI_INQCMD_PCODE_SERNUM          (0x80u)
#define CY_FX_SCSI_INQCMD_PCODE_DEVIDENT        (0x83u)
#define CY_FX_SCSI_INQCMD_PCODE_BLKLIMITS       (0xB0u)

#define CY_FX_SCSI_RQSCMD_DESC_OFFSET           (16u)
#define CY_FX_SCSI_REQSENSE_DESC                (0x01u)

#define CY_FX_SCSI_STOPCMD_LOEJ_OFFSET          (19u)
#define CY_FX_SCSI_STOPCMD_LOEJ_VAL             (0x02u)
#define CY_FX_SCSI_STOPCMD_START_VAL            (0x01u)

#define CY_FX_SCSI_READCAP16_SERVACT_OFFSET     (16u)
#define CY_FX_SCSI_READCAP16_SERVACT_VALUE      (0x10u)

#define CY_FX_SCSI_MODESENS_DBD_OFFSET          (16u)
#define CY_FX_SCSI_MODESENS_DBD_VALUE           (0x08u)
#define CY_FX_SCSI_MODESENS_PC_OFFSET           (17u)
#define CY_FX_SCSI_MODESENS_PC_MASK             (0xC0)
#define CY_FX_SCSI_MODESENS_PC_CURRENT          (0x00)
#define CY_FX_SCSI_MODESENS_PC_CHANGE           (0x40)
#define CY_FX_SCSI_MODESENS_PC_DEFAULT          (0x80)
#define CY_FX_SCSI_MODESENS_PC_SAVED            (0xC0)

#define CY_FX_SCSI_MODESENS_PAGE_MASK           (0x3F)
#define CY_FX_SCSI_MODESENS_PAGE_ALL            (0x3F)
#define CY_FX_SCSI_MODESENS_PAGE_EXCP           (0x1C)
#define CY_FX_SCSI_MODESENS_PAGE_CACHE          (0x08)

/* SCSI Commands */
#define CY_FX_MSC_SCSI_TEST_UNIT_READY          (0x00u)
#define CY_FX_MSC_SCSI_REQUEST_SENSE            (0x03u)
#define CY_FX_MSC_SCSI_FORMAT_UNIT              (0x04u)
#define CY_FX_MSC_SCSI_INQUIRY                  (0x12u)
#define CY_FX_MSC_SCSI_GET_CONFIG               (0x46u)
#define CY_FX_MSC_SCSI_MODE_SELECT_6            (0x15u)
#define CY_FX_MSC_SCSI_MODE_SENSE_6             (0x1Au)
#define CY_FX_MSC_SCSI_START_STOP_UNIT          (0x1Bu)
#define CY_FX_MSC_SCSI_SEND_DIAG                (0x1Du)
#define CY_FX_MSC_SCSI_PREVENT_ALLOW_MEDIUM     (0x1Eu)
#define CY_FX_MSC_SCSI_READ_FORMAT_CAPACITY     (0x23u)
#define CY_FX_MSC_SCSI_READ_CAPACITY            (0x25u)
#define CY_FX_MSC_SCSI_READ_10                  (0x28u)
#define CY_FX_MSC_SCSI_WRITE_10                 (0x2Au)
#define CY_FX_MSC_SCSI_VERIFY_10                (0x2Fu)
#define CY_FX_MSC_SCSI_SYNCCACHE_10             (0x35u)
#define CY_FX_MSC_SCSI_READCAP_16               (0x9Eu)

/* Sense Codes */
#define CY_FX_MSC_SENSE_OK                      (0x00)
#define CY_FX_MSC_SENSE_CRC_ERROR               (0x01)
#define CY_FX_MSC_SENSE_INVALID_FIELD_IN_CBW    (0x02)
#define CY_FX_MSC_SENSE_NO_MEDIA                (0x03)
#define CY_FX_MSC_SENSE_WRITE_FAULT             (0x04)
#define CY_FX_MSC_SENSE_READ_ERROR              (0x05)
#define CY_FX_MSC_SENSE_ADDR_NOT_FOUND          (0x06)
#define CY_FX_MSC_SENSE_INVALID_OP_CODE         (0x07)
#define CY_FX_MSC_SENSE_INVALID_LBA             (0x08)
#define CY_FX_MSC_SENSE_INVALID_PARAMETER       (0x09)
#define CY_FX_MSC_SENSE_CANT_EJECT              (0x0A)
#define CY_FX_MSC_SENSE_MEDIA_CHANGED           (0x0B)
#define CY_FX_MSC_SENSE_DEVICE_RESET            (0x0C)
#define CY_FX_MSC_SENSE_WRITE_PROTECT           (0x0D)
#define CY_FX_MSC_SENSE_INIT_REQUIRED           (0x0E)
#define CY_FX_MSC_SENSE_NO_SAVEPAGE_SUPPORT     (0x0F)

#define CY_FX_USB_SETUP_REQ_TYPE_MASK   (uint32_t)(0x000000FF)  /* Setup Request Type Mask */
#define CY_FX_USB_SETUP_REQ_MASK        (uint32_t)(0x0000FF00)  /* Setup Request Mask */


/* Extern definitions for the USB Enumeration descriptors */
extern const uint8_t CyFxUSB20DeviceDscr[];
extern const uint8_t CyFxUSB30DeviceDscr[];
extern const uint8_t CyFxUSBDeviceQualDscr[];
extern const uint8_t CyFxUSBFSConfigDscr1[];
extern const uint8_t CyFxUSBHSConfigDscr1[];
extern const uint8_t CyFxUSBSSConfigDscr1[];
extern const uint8_t CyFxUSBBOSDscr[];
extern const uint8_t CyFxUSBStringLangIDDscr[];
extern const uint8_t CyFxUSBManufactureDscr[];
extern const uint8_t CyFxUSBProductDscr[];
extern const uint8_t CyFxUsbOSDscr[];
extern const uint8_t CyFxUSBHidReportDscr[];
extern const uint8_t CyFxUSBInterfaceDesc1[];
extern const uint8_t CyFxUSBInterfaceDesc2[];
extern const uint8_t CyFxUSBInterfaceDesc3[];
extern const uint8_t CyFxUSBInterfaceDesc4[];
extern const uint8_t CyFxUSBInterfaceDesc5[];
extern const uint8_t CyFxUSBInterfaceDesc6[];
extern const uint8_t CyFxUsbSN[];

#include <cyu3externcend.h>

#endif /* _INCLUDED_CYFXBULKSRCSINK_H_ */

/*[]*/
