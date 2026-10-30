
#include "main.h"

#define VENDOR_ID 0x09,0x12
#define DEVICE_ID 0x15,0x35


 /* Standard device descriptor for USB 3.0 */
const uint8_t CyFxUSB30DeviceDscr[] __attribute__((aligned(32))) =
{
    0x12,                           /* Descriptor size */
    CY_U3P_USB_DEVICE_DESCR,        /* Device descriptor type */
    0x10,0x03,                      /* USB 3.1 */
    0x00,                           /* Device class */
    0x00,                           /* Device sub-class */
    0x00,                           /* Device protocol */
    0x09,                           /* Maxpacket size for EP0 : 2^9 */
    VENDOR_ID,                      /* Vendor ID (LE) */
    DEVICE_ID,                      /* Product ID (LE ) */
    0x00,0x00,                      /* Device release number */
    0x01,                           /* Manufacture string index */
    0x02,                           /* Product string index */
    0x07,                           /* Serial number string index */
    0x01                            /* Number of configurations */
};

/* Standard device descriptor for USB 2.0 */
const uint8_t CyFxUSB20DeviceDscr[] __attribute__((aligned(32))) =
{
    0x12,                           /* Descriptor size */
    CY_U3P_USB_DEVICE_DESCR,        /* Device descriptor type */
    0x10,0x02,                      /* USB 2.10 */
    0x00,                           /* Device class */
    0x00,                           /* Device sub-class */
    0x00,                           /* Device protocol */
    0x40,                           /* Maxpacket size for EP0 : 64 bytes */
    VENDOR_ID,                      /* Vendor ID (LE) */
    DEVICE_ID,                      /* Product ID (LE) */
    0x00,0x00,                      /* Device release number */
    0x01,                           /* Manufacture string index */
    0x02,                           /* Product string index */
    0x07,                           /* Serial number string index */
    0x01                            /* Number of configurations */
};

/* Binary device object store descriptor */
const uint8_t CyFxUSBBOSDscr[] __attribute__((aligned(32))) =
{
    0x05,                           /* Descriptor size */
    CY_U3P_BOS_DESCR,               /* Device descriptor type */
    0x16,0x00,                      /* Length of this descriptor and all sub descriptors */
    0x02,                           /* Number of device capability descriptors */

    /* USB 2.0 extension */
    0x07,                           /* Descriptor size */
    CY_U3P_DEVICE_CAPB_DESCR,       /* Device capability type descriptor */
    CY_U3P_USB2_EXTN_CAPB_TYPE,     /* USB 2.0 extension capability type */
    0x1E,0x64,0x00,0x00,            /* Supported device level features: LPM support, BESL supported,
                                       Baseline BESL=400 us, Deep BESL=1000 us. */

   /* SuperSpeed device capability */
   0x0A,                           /* Descriptor size */
   CY_U3P_DEVICE_CAPB_DESCR,       /* Device capability type descriptor */
   CY_U3P_SS_USB_CAPB_TYPE,        /* SuperSpeed device capability type */
   0x00,                           /* Supported device level features  */
   0x0E,0x00,                      /* Speeds supported by the device : SS, HS and FS */
   0x03,                           /* Functionality support */
   0x0A,                           /* U1 Device Exit latency */
   0xFF,0x07                       /* U2 Device Exit latency */
};

/* Standard device qualifier descriptor */
const uint8_t CyFxUSBDeviceQualDscr[] __attribute__((aligned(32))) =
{
    0x0A,                           /* Descriptor size */
    CY_U3P_USB_DEVQUAL_DESCR,       /* Device qualifier descriptor type */
    0x00,0x02,                      /* USB 2.0 */
    0x00,                           /* Device class */
    0x00,                           /* Device sub-class */
    0x00,                           /* Device protocol */
    0x40,                           /* Maxpacket size for EP0 : 64 bytes */
    0x01,                           /* Number of configurations */
    0x00                            /* Reserved */
};


/* The serial port: a CDC-ACM function (interface association descriptor + communication interface with a notification endpoint + data interface
 * with a bulk pair), interfaces 6 and 7, endpoints 0x87 (interrupt IN), 0x06 / 0x86 (bulk). String 9 names the function and the communication
 * interface (Windows takes the COM port's friendly name from there). 66 bytes at high and full speed, 84 bytes at super speed (companions). */
#define CDC_FUNCTION_STRING 0x09
#define CDC_COMMON(BulkSizeLow, BulkSizeHigh, NotifyInterval) \
    0x08, 0x0B, CY_FX_USB_SERIAL_COMM_INTF, 0x02, 0x02, 0x02, 0x01, CDC_FUNCTION_STRING, \
    0x09, 0x04, CY_FX_USB_SERIAL_COMM_INTF, 0x00, 0x01, 0x02, 0x02, 0x01, CDC_FUNCTION_STRING, \
    0x05, 0x24, 0x00, 0x10, 0x01, \
    0x05, 0x24, 0x01, 0x00, CY_FX_USB_SERIAL_DATA_INTF, \
    0x04, 0x24, 0x02, 0x02, \
    0x05, 0x24, 0x06, CY_FX_USB_SERIAL_COMM_INTF, CY_FX_USB_SERIAL_DATA_INTF, \
    0x07, 0x05, CY_FX_EP_SERIAL_NOTIFY, 0x03, 0x10, 0x00, NotifyInterval,
#define CDC_DATA_INTERFACE \
    0x09, 0x04, CY_FX_USB_SERIAL_DATA_INTF, 0x00, 0x02, 0x0A, 0x00, 0x00, 0x00,
#define CDC_DESCRIPTORS_SS \
    CDC_COMMON(0x00, 0x04, 0x0B) \
    0x06, 0x30, 0x00, 0x00, 0x10, 0x00, \
    CDC_DATA_INTERFACE \
    0x07, 0x05, CY_FX_EP_SERIAL_OUT, 0x02, 0x00, 0x04, 0x00, \
    0x06, 0x30, 0x00, 0x00, 0x00, 0x00, \
    0x07, 0x05, CY_FX_EP_SERIAL_IN, 0x02, 0x00, 0x04, 0x00, \
    0x06, 0x30, 0x00, 0x00, 0x00, 0x00,
#define CDC_DESCRIPTORS_HS \
    CDC_COMMON(0x00, 0x02, 0x0B) \
    CDC_DATA_INTERFACE \
    0x07, 0x05, CY_FX_EP_SERIAL_OUT, 0x02, 0x00, 0x02, 0x00, \
    0x07, 0x05, CY_FX_EP_SERIAL_IN, 0x02, 0x00, 0x02, 0x00,
#define CDC_DESCRIPTORS_FS \
    CDC_COMMON(0x40, 0x00, 0x10) \
    CDC_DATA_INTERFACE \
    0x07, 0x05, CY_FX_EP_SERIAL_OUT, 0x02, 0x40, 0x00, 0x00, \
    0x07, 0x05, CY_FX_EP_SERIAL_IN, 0x02, 0x40, 0x00, 0x00,

/* Standard super speed configuration descriptor - USB 3.0 */
const uint8_t CyFxUSBSSConfigDscr1[] __attribute__((aligned(32))) =
{
    /* Config Descriptor */
    0x09, CY_U3P_USB_CONFIG_DESCR,
    0x17, 0x01,                     /* wTotalLength */
    0x07,                           /* Number of interfaces */
    0x01,                           /* Configuration number */
    0x00,                           /* Configuration string index */
    0x80,                           /* Config characteristics - Bus powered */
    0x32,                           /* Max power consumption of device (in 8mA unit) : 400mA */

    //CD-ROM Drive
    0x09,                   // bLength
    0x04,                   // bDescriptorType (Interface)
    0x00,                   // bInterfaceNumber
    0x00,                   // bAlternateSetting
    0x02,                   // bNumEndpoints
    0x08,                   // bInterfaceClass (Mass Storage)
    0x06,                   // bInterfaceSubClass (SCSI transparent)
    0x50,                   // bInterfaceProtocol (Bulk-Only)
    0x03,                   // iInterface

    // Endpoint Descriptor (Bulk IN)
    0x07,                   // bLength
    0x05,                   // bDescriptorType (Endpoint)
    CY_FX_EP_CD_ROM_IN,     // bEndpointAddress (IN endpoint 1)
    0x02,                   // bmAttributes (Bulk)
    0x00, 0x04,             // wMaxPacketSize
    0x00,                   // bInterval

    /* Super speed endpoint companion descriptor for consumer EP */
    0x06,                           /* Descriptor size */
    CY_U3P_SS_EP_COMPN_DESCR,       /* SS endpoint companion descriptor type */
    (CY_FX_EP_BURST_LENGTH - 1),    /* Max no. of packets in a burst(0-15) - 0: burst 1 packet at a time */
    0x00,                           /* Max streams for bulk EP = 0 (No streams) */
    0x00,0x00,                      /* Service interval for the EP : 0 for bulk */

    // Endpoint Descriptor (Bulk OUT)
    0x07,                   // bLength
    0x05,                   // bDescriptorType (Endpoint)
    CY_FX_EP_CD_ROM_OUT,     // bEndpointAddress (OUT endpoint 1)
    0x02,                   // bmAttributes (Bulk)
    0x00, 0x04,             // wMaxPacketSize
    0x00,                   // bInterval

    /* Super speed endpoint companion descriptor for consumer EP */
    0x06,                           /* Descriptor size */
    CY_U3P_SS_EP_COMPN_DESCR,       /* SS endpoint companion descriptor type */
    (CY_FX_EP_BURST_LENGTH - 1),    /* Max no. of packets in a burst(0-15) - 0: burst 1 packet at a time */
    0x00,                           /* Max streams for bulk EP = 0 (No streams) */
    0x00,0x00,                      /* Service interval for the EP : 0 for bulk */

    ///////////////////////

    //Keyboard and mouse in ONE HID interface (report ID 1 keyboard, report ID 2 mouse; boot interface, keyboard protocol)
    0x09,                   // bLength
    0x04,                   // bDescriptorType (Interface)
    CY_FX_USB_KEYBOARD_INTF, // bInterfaceNumber
    0x00,                   // bAlternateSetting
    0x01,                   // bNumEndpoints
    0x03,                   // bInterfaceClass (HID)
    0x01,                   // bInterfaceSubClass (Boot Interface)
    0x01,                   // bInterfaceProtocol (Keyboard)
    0x05,                   // iInterface

    // HID Descriptor
    0x09,                   // bLength
    CY_FX_HID_DSCR_TYPE,    // bDescriptorType (HID)
    0x11, 0x01,             // bcdHID 1.11
    0x00,                   // bCountryCode
    0x01,                   // bNumDescriptors
    CY_FX_HID_REPORT_DSCR_TYPE, // bDescriptorType (Report)
    CY_FX_HID_REPORT_DSCR_LENGTH, 0x00, // wDescriptorLength (keyboard + mouse report descriptor)

    // Endpoint Descriptor (Interrupt IN)
    0x07,                   // bLength
    0x05,                   // bDescriptorType (Endpoint)
    CY_FX_EP_KEYBOARD_IN,   // bEndpointAddress (IN endpoint 3)
    0x03,                   // bmAttributes (Interrupt)
    0x10, 0x00,             // wMaxPacketSize (16 bytes: report ID + 8 byte keyboard report, or report ID + 4 byte mouse report)
    0x04,                   // bInterval (2^(4-1)*125us = 1ms)

    /* Super speed endpoint companion descriptor for interrupt EP */
    0x06,                           /* Descriptor size */
    CY_U3P_SS_EP_COMPN_DESCR,       /* SS endpoint companion descriptor type */
    0x00,                           /* Max no. of packets in a burst - single packet */
    0x00,                           /* Reserved for interrupt endpoints */
    0x10,0x00,                      /* wBytesPerInterval (16 bytes) */

    ///////////////////////

    //MIDI Audio Control interface (groups the MIDIStreaming interface below)
    0x09,                   // bLength
    0x04,                   // bDescriptorType (Interface)
    CY_FX_USB_MIDI_CONTROL_INTF, // bInterfaceNumber
    0x00,                   // bAlternateSetting
    0x00,                   // bNumEndpoints
    0x01,                   // bInterfaceClass (Audio)
    0x01,                   // bInterfaceSubClass (Audio Control)
    0x00,                   // bInterfaceProtocol
    0x08,                   // iInterface

    // Class-specific AC Interface Header Descriptor
    0x09,                   // bLength
    0x24,                   // bDescriptorType (CS_INTERFACE)
    0x01,                   // bDescriptorSubtype (HEADER)
    0x00, 0x01,             // bcdADC 1.00
    0x09, 0x00,             // wTotalLength (9 bytes, this descriptor only)
    0x01,                   // bInCollection (1 streaming interface)
    CY_FX_USB_MIDI_STREAMING_INTF, // baInterfaceNr(1)

    ///////////////////////

    //MIDI Streaming interface (1 embedded IN jack, 1 embedded OUT jack)
    0x09,                   // bLength
    0x04,                   // bDescriptorType (Interface)
    CY_FX_USB_MIDI_STREAMING_INTF, // bInterfaceNumber
    0x00,                   // bAlternateSetting
    0x02,                   // bNumEndpoints
    0x01,                   // bInterfaceClass (Audio)
    0x03,                   // bInterfaceSubClass (MIDIStreaming)
    0x00,                   // bInterfaceProtocol
    0x08,                   // iInterface

    // Class-specific MS Interface Header Descriptor
    0x07,                   // bLength
    0x24,                   // bDescriptorType (CS_INTERFACE)
    0x01,                   // bDescriptorSubtype (MS_HEADER)
    0x00, 0x01,             // bcdMSC 1.00
    0x16, 0x00,             // wTotalLength (22 bytes: header + both jacks)

    // MIDI IN Jack Descriptor (Embedded) - fed by the Bulk OUT endpoint below
    0x06,                   // bLength
    0x24,                   // bDescriptorType (CS_INTERFACE)
    0x02,                   // bDescriptorSubtype (MIDI_IN_JACK)
    0x01,                   // bJackType (EMBEDDED)
    0x01,                   // bJackID (1)
    0x00,                   // iJack

    // MIDI OUT Jack Descriptor (Embedded) - feeds the Bulk IN endpoint below
    0x09,                   // bLength
    0x24,                   // bDescriptorType (CS_INTERFACE)
    0x03,                   // bDescriptorSubtype (MIDI_OUT_JACK)
    0x01,                   // bJackType (EMBEDDED)
    0x02,                   // bJackID (2)
    0x01,                   // bNrInputPins (1)
    0x01,                   // BaSourceID(1) - sourced from MIDI IN Jack 1
    0x01,                   // BaSourcePin(1)
    0x00,                   // iJack

    // Endpoint Descriptor (Bulk OUT) - MIDI data from host to device
    0x07,                   // bLength
    0x05,                   // bDescriptorType (Endpoint)
    CY_FX_EP_MIDI_OUT,      // bEndpointAddress (OUT endpoint 5)
    0x02,                   // bmAttributes (Bulk)
    0x00, 0x04,             // wMaxPacketSize
    0x00,                   // bInterval

    /* Super speed endpoint companion descriptor for producer EP */
    0x06,                           /* Descriptor size */
    CY_U3P_SS_EP_COMPN_DESCR,       /* SS endpoint companion descriptor type */
    (CY_FX_EP_BURST_LENGTH - 1),    /* Max no. of packets in a burst(0-15) - 0: burst 1 packet at a time */
    0x00,                           /* Max streams for bulk EP = 0 (No streams) */
    0x00,0x00,                      /* Service interval for the EP : 0 for bulk */

    // Class-specific MS Bulk OUT Endpoint Descriptor
    0x05,                   // bLength
    0x25,                   // bDescriptorType (CS_ENDPOINT)
    0x01,                   // bDescriptorSubtype (MS_GENERAL)
    0x01,                   // bNumEmbMIDIJack
    0x01,                   // BaAssocJackID(1) - MIDI IN Jack 1

    // Endpoint Descriptor (Bulk IN) - MIDI data from device to host
    0x07,                   // bLength
    0x05,                   // bDescriptorType (Endpoint)
    CY_FX_EP_MIDI_IN,       // bEndpointAddress (IN endpoint 5)
    0x02,                   // bmAttributes (Bulk)
    0x00, 0x04,             // wMaxPacketSize
    0x00,                   // bInterval

    /* Super speed endpoint companion descriptor for consumer EP */
    0x06,                           /* Descriptor size */
    CY_U3P_SS_EP_COMPN_DESCR,       /* SS endpoint companion descriptor type */
    (CY_FX_EP_BURST_LENGTH - 1),    /* Max no. of packets in a burst(0-15) - 0: burst 1 packet at a time */
    0x00,                           /* Max streams for bulk EP = 0 (No streams) */
    0x00,0x00,                      /* Service interval for the EP : 0 for bulk */

    // Class-specific MS Bulk IN Endpoint Descriptor
    0x05,                   // bLength
    0x25,                   // bDescriptorType (CS_ENDPOINT)
    0x01,                   // bDescriptorSubtype (MS_GENERAL)
    0x01,                   // bNumEmbMIDIJack
    0x02,                   // BaAssocJackID(1) - MIDI OUT Jack 2

    ///////////////////////

    CDC_DESCRIPTORS_SS

    ///////////////////////

    //Disk (the LAST interface: it is left out of the configuration while no disk is loaded)
    0x09,                   // bLength
    0x04,                   // bDescriptorType (Interface)
    CY_FX_USB_MSC_INTF2,    // bInterfaceNumber
    0x00,                   // bAlternateSetting
    0x02,                   // bNumEndpoints
    0x08,                   // bInterfaceClass (Mass Storage)
    0x06,                   // bInterfaceSubClass (SCSI transparent)
    0x50,                   // bInterfaceProtocol (Bulk-Only)
    0x04,                   // iInterface

    // Endpoint Descriptor (Bulk IN)
    0x07,                   // bLength
    0x05,                   // bDescriptorType (Endpoint)
    CY_FX_EP_STORAGE_IN,     // bEndpointAddress (IN endpoint 2)
    0x02,                   // bmAttributes (Bulk)
    0x00, 0x04,             // wMaxPacketSize
    0x00,                   // bInterval

    /* Super speed endpoint companion descriptor for consumer EP */
    0x06,                           /* Descriptor size */
    CY_U3P_SS_EP_COMPN_DESCR,       /* SS endpoint companion descriptor type */
    (CY_FX_EP_BURST_LENGTH - 1),    /* Max no. of packets in a burst(0-15) - 0: burst 1 packet at a time */
    0x00,                           /* Max streams for bulk EP = 0 (No streams) */
    0x00,0x00,                      /* Service interval for the EP : 0 for bulk */

    // Endpoint Descriptor (Bulk OUT)
    0x07,                   // bLength
    0x05,                   // bDescriptorType (Endpoint)
    CY_FX_EP_STORAGE_OUT,     // bEndpointAddress (OUT endpoint 2)
    0x02,                   // bmAttributes (Bulk)
    0x00, 0x04,             // wMaxPacketSize
    0x00,                   // bInterval

    /* Super speed endpoint companion descriptor for consumer EP */
    0x06,                           /* Descriptor size */
    CY_U3P_SS_EP_COMPN_DESCR,       /* SS endpoint companion descriptor type */
    (CY_FX_EP_BURST_LENGTH - 1),    /* Max no. of packets in a burst(0-15) - 0: burst 1 packet at a time */
    0x00,                           /* Max streams for bulk EP = 0 (No streams) */
    0x00,0x00,                      /* Service interval for the EP : 0 for bulk */
};

/* Standard high speed configuration descriptor - USB 2.0 */
const uint8_t CyFxUSBHSConfigDscr1[] __attribute__((aligned(32))) =
{
    /* Config Descriptor */
    0x09, CY_U3P_USB_CONFIG_DESCR,
    0xDB, 0x00,                     /* wTotalLength */
    0x07,                           /* Number of interfaces */
    0x01,                           /* Configuration number */
    0x00,                           /* Configuration string index */
    0x80,                           /* Config characteristics - Bus powered */
    0x32,                           /* Max power consumption of device (in 2mA unit) : 100mA */

    //CD-ROM Drive
    0x09,                   // bLength
    0x04,                   // bDescriptorType (Interface)
    0x00,                   // bInterfaceNumber
    0x00,                   // bAlternateSetting
    0x02,                   // bNumEndpoints
    0x08,                   // bInterfaceClass (Mass Storage)
    0x06,                   // bInterfaceSubClass (SCSI transparent)
    0x50,                   // bInterfaceProtocol (Bulk-Only)
    0x03,                   // iInterface

    // Endpoint Descriptor (Bulk IN)
    0x07,                   // bLength
    0x05,                   // bDescriptorType (Endpoint)
    CY_FX_EP_CD_ROM_IN,     // bEndpointAddress (IN endpoint 1)
    0x02,                   // bmAttributes (Bulk)
    0x00, 0x02,             // wMaxPacketSize
    0x00,                   // bInterval

    // Endpoint Descriptor (Bulk OUT)
    0x07,                   // bLength
    0x05,                   // bDescriptorType (Endpoint)
    CY_FX_EP_CD_ROM_OUT,     // bEndpointAddress (OUT endpoint 1)
    0x02,                   // bmAttributes (Bulk)
    0x00, 0x02,             // wMaxPacketSize
    0x00,                   // bInterval

    ///////////////////////

    //Keyboard and mouse in ONE HID interface (report ID 1 keyboard, report ID 2 mouse; boot interface, keyboard protocol)
    0x09,                   // bLength
    0x04,                   // bDescriptorType (Interface)
    CY_FX_USB_KEYBOARD_INTF, // bInterfaceNumber
    0x00,                   // bAlternateSetting
    0x01,                   // bNumEndpoints
    0x03,                   // bInterfaceClass (HID)
    0x01,                   // bInterfaceSubClass (Boot Interface)
    0x01,                   // bInterfaceProtocol (Keyboard)
    0x05,                   // iInterface

    // HID Descriptor
    0x09,                   // bLength
    CY_FX_HID_DSCR_TYPE,    // bDescriptorType (HID)
    0x11, 0x01,             // bcdHID 1.11
    0x00,                   // bCountryCode
    0x01,                   // bNumDescriptors
    CY_FX_HID_REPORT_DSCR_TYPE, // bDescriptorType (Report)
    CY_FX_HID_REPORT_DSCR_LENGTH, 0x00, // wDescriptorLength (keyboard + mouse report descriptor)

    // Endpoint Descriptor (Interrupt IN)
    0x07,                   // bLength
    0x05,                   // bDescriptorType (Endpoint)
    CY_FX_EP_KEYBOARD_IN,   // bEndpointAddress (IN endpoint 3)
    0x03,                   // bmAttributes (Interrupt)
    0x10, 0x00,             // wMaxPacketSize (16 bytes: report ID + 8 byte keyboard report, or report ID + 4 byte mouse report)
    0x04,                   // bInterval (2^(4-1)*125us = 1ms)

    ///////////////////////

    //MIDI Audio Control interface (groups the MIDIStreaming interface below)
    0x09,                   // bLength
    0x04,                   // bDescriptorType (Interface)
    CY_FX_USB_MIDI_CONTROL_INTF, // bInterfaceNumber
    0x00,                   // bAlternateSetting
    0x00,                   // bNumEndpoints
    0x01,                   // bInterfaceClass (Audio)
    0x01,                   // bInterfaceSubClass (Audio Control)
    0x00,                   // bInterfaceProtocol
    0x08,                   // iInterface

    // Class-specific AC Interface Header Descriptor
    0x09,                   // bLength
    0x24,                   // bDescriptorType (CS_INTERFACE)
    0x01,                   // bDescriptorSubtype (HEADER)
    0x00, 0x01,             // bcdADC 1.00
    0x09, 0x00,             // wTotalLength (9 bytes, this descriptor only)
    0x01,                   // bInCollection (1 streaming interface)
    CY_FX_USB_MIDI_STREAMING_INTF, // baInterfaceNr(1)

    ///////////////////////

    //MIDI Streaming interface (1 embedded IN jack, 1 embedded OUT jack)
    0x09,                   // bLength
    0x04,                   // bDescriptorType (Interface)
    CY_FX_USB_MIDI_STREAMING_INTF, // bInterfaceNumber
    0x00,                   // bAlternateSetting
    0x02,                   // bNumEndpoints
    0x01,                   // bInterfaceClass (Audio)
    0x03,                   // bInterfaceSubClass (MIDIStreaming)
    0x00,                   // bInterfaceProtocol
    0x08,                   // iInterface

    // Class-specific MS Interface Header Descriptor
    0x07,                   // bLength
    0x24,                   // bDescriptorType (CS_INTERFACE)
    0x01,                   // bDescriptorSubtype (MS_HEADER)
    0x00, 0x01,             // bcdMSC 1.00
    0x16, 0x00,             // wTotalLength (22 bytes: header + both jacks)

    // MIDI IN Jack Descriptor (Embedded) - fed by the Bulk OUT endpoint below
    0x06,                   // bLength
    0x24,                   // bDescriptorType (CS_INTERFACE)
    0x02,                   // bDescriptorSubtype (MIDI_IN_JACK)
    0x01,                   // bJackType (EMBEDDED)
    0x01,                   // bJackID (1)
    0x00,                   // iJack

    // MIDI OUT Jack Descriptor (Embedded) - feeds the Bulk IN endpoint below
    0x09,                   // bLength
    0x24,                   // bDescriptorType (CS_INTERFACE)
    0x03,                   // bDescriptorSubtype (MIDI_OUT_JACK)
    0x01,                   // bJackType (EMBEDDED)
    0x02,                   // bJackID (2)
    0x01,                   // bNrInputPins (1)
    0x01,                   // BaSourceID(1) - sourced from MIDI IN Jack 1
    0x01,                   // BaSourcePin(1)
    0x00,                   // iJack

    // Endpoint Descriptor (Bulk OUT) - MIDI data from host to device
    0x07,                   // bLength
    0x05,                   // bDescriptorType (Endpoint)
    CY_FX_EP_MIDI_OUT,      // bEndpointAddress (OUT endpoint 5)
    0x02,                   // bmAttributes (Bulk)
    0x00, 0x02,             // wMaxPacketSize
    0x00,                   // bInterval

    // Class-specific MS Bulk OUT Endpoint Descriptor
    0x05,                   // bLength
    0x25,                   // bDescriptorType (CS_ENDPOINT)
    0x01,                   // bDescriptorSubtype (MS_GENERAL)
    0x01,                   // bNumEmbMIDIJack
    0x01,                   // BaAssocJackID(1) - MIDI IN Jack 1

    // Endpoint Descriptor (Bulk IN) - MIDI data from device to host
    0x07,                   // bLength
    0x05,                   // bDescriptorType (Endpoint)
    CY_FX_EP_MIDI_IN,       // bEndpointAddress (IN endpoint 5)
    0x02,                   // bmAttributes (Bulk)
    0x00, 0x02,             // wMaxPacketSize
    0x00,                   // bInterval

    // Class-specific MS Bulk IN Endpoint Descriptor
    0x05,                   // bLength
    0x25,                   // bDescriptorType (CS_ENDPOINT)
    0x01,                   // bDescriptorSubtype (MS_GENERAL)
    0x01,                   // bNumEmbMIDIJack
    0x02,                   // BaAssocJackID(1) - MIDI OUT Jack 2

    ///////////////////////

    CDC_DESCRIPTORS_HS

    ///////////////////////

    //Disk (the LAST interface: it is left out of the configuration while no disk is loaded)
    0x09,                   // bLength
    0x04,                   // bDescriptorType (Interface)
    CY_FX_USB_MSC_INTF2,    // bInterfaceNumber
    0x00,                   // bAlternateSetting
    0x02,                   // bNumEndpoints
    0x08,                   // bInterfaceClass (Mass Storage)
    0x06,                   // bInterfaceSubClass (SCSI transparent)
    0x50,                   // bInterfaceProtocol (Bulk-Only)
    0x04,                   // iInterface

    // Endpoint Descriptor (Bulk IN)
    0x07,                   // bLength
    0x05,                   // bDescriptorType (Endpoint)
    CY_FX_EP_STORAGE_IN,     // bEndpointAddress (IN endpoint 2)
    0x02,                   // bmAttributes (Bulk)
    0x00, 0x02,             // wMaxPacketSize
    0x00,                   // bInterval

    // Endpoint Descriptor (Bulk OUT)
    0x07,                   // bLength
    0x05,                   // bDescriptorType (Endpoint)
    CY_FX_EP_STORAGE_OUT,     // bEndpointAddress (OUT endpoint 2)
    0x02,                   // bmAttributes (Bulk)
    0x00, 0x02,             // wMaxPacketSize
    0x00,                   // bInterval
};

/* Standard full speed configuration descriptor - USB 1.1 */
const uint8_t CyFxUSBFSConfigDscr1[] __attribute__((aligned(32))) =
{
    /* Config Descriptor */
    0x09, CY_U3P_USB_CONFIG_DESCR,
    0xDB, 0x00,                     /* wTotalLength */
    0x07,                           /* Number of interfaces */
    0x01,                           /* Configuration number */
    0x00,                           /* Configuration string index */
    0x80,                           /* Config characteristics - Bus powered */
    0x32,                           /* Max power consumption of device (in 2mA unit) : 100mA */

    //CD-ROM Drive
    0x09,                   // bLength
    0x04,                   // bDescriptorType (Interface)
    0x00,                   // bInterfaceNumber
    0x00,                   // bAlternateSetting
    0x02,                   // bNumEndpoints
    0x08,                   // bInterfaceClass (Mass Storage)
    0x06,                   // bInterfaceSubClass (SCSI transparent)
    0x50,                   // bInterfaceProtocol (Bulk-Only)
    0x03,                   // iInterface

    // Endpoint Descriptor (Bulk IN)
    0x07,                   // bLength
    0x05,                   // bDescriptorType (Endpoint)
    CY_FX_EP_CD_ROM_IN,     // bEndpointAddress (IN endpoint 1)
    0x02,                   // bmAttributes (Bulk)
    0x40, 0x00,             // wMaxPacketSize
    0x00,                   // bInterval

    // Endpoint Descriptor (Bulk OUT)
    0x07,                   // bLength
    0x05,                   // bDescriptorType (Endpoint)
    CY_FX_EP_CD_ROM_OUT,     // bEndpointAddress (OUT endpoint 1)
    0x02,                   // bmAttributes (Bulk)
    0x40, 0x00,             // wMaxPacketSize
    0x00,                   // bInterval

    //////////////////////////////

    //Keyboard and mouse in ONE HID interface (report ID 1 keyboard, report ID 2 mouse; boot interface, keyboard protocol)
    0x09,                   // bLength
    0x04,                   // bDescriptorType (Interface)
    CY_FX_USB_KEYBOARD_INTF, // bInterfaceNumber
    0x00,                   // bAlternateSetting
    0x01,                   // bNumEndpoints
    0x03,                   // bInterfaceClass (HID)
    0x01,                   // bInterfaceSubClass (Boot Interface)
    0x01,                   // bInterfaceProtocol (Keyboard)
    0x05,                   // iInterface

    // HID Descriptor
    0x09,                   // bLength
    CY_FX_HID_DSCR_TYPE,    // bDescriptorType (HID)
    0x11, 0x01,             // bcdHID 1.11
    0x00,                   // bCountryCode
    0x01,                   // bNumDescriptors
    CY_FX_HID_REPORT_DSCR_TYPE, // bDescriptorType (Report)
    CY_FX_HID_REPORT_DSCR_LENGTH, 0x00, // wDescriptorLength (keyboard + mouse report descriptor)

    // Endpoint Descriptor (Interrupt IN)
    0x07,                   // bLength
    0x05,                   // bDescriptorType (Endpoint)
    CY_FX_EP_KEYBOARD_IN,   // bEndpointAddress (IN endpoint 3)
    0x03,                   // bmAttributes (Interrupt)
    0x10, 0x00,             // wMaxPacketSize (16 bytes: report ID + 8 byte keyboard report, or report ID + 4 byte mouse report)
    0x02,                   // bInterval (2ms)

    //////////////////////////////

    //MIDI Audio Control interface (groups the MIDIStreaming interface below)
    0x09,                   // bLength
    0x04,                   // bDescriptorType (Interface)
    CY_FX_USB_MIDI_CONTROL_INTF, // bInterfaceNumber
    0x00,                   // bAlternateSetting
    0x00,                   // bNumEndpoints
    0x01,                   // bInterfaceClass (Audio)
    0x01,                   // bInterfaceSubClass (Audio Control)
    0x00,                   // bInterfaceProtocol
    0x08,                   // iInterface

    // Class-specific AC Interface Header Descriptor
    0x09,                   // bLength
    0x24,                   // bDescriptorType (CS_INTERFACE)
    0x01,                   // bDescriptorSubtype (HEADER)
    0x00, 0x01,             // bcdADC 1.00
    0x09, 0x00,             // wTotalLength (9 bytes, this descriptor only)
    0x01,                   // bInCollection (1 streaming interface)
    CY_FX_USB_MIDI_STREAMING_INTF, // baInterfaceNr(1)

    //////////////////////////////

    //MIDI Streaming interface (1 embedded IN jack, 1 embedded OUT jack)
    0x09,                   // bLength
    0x04,                   // bDescriptorType (Interface)
    CY_FX_USB_MIDI_STREAMING_INTF, // bInterfaceNumber
    0x00,                   // bAlternateSetting
    0x02,                   // bNumEndpoints
    0x01,                   // bInterfaceClass (Audio)
    0x03,                   // bInterfaceSubClass (MIDIStreaming)
    0x00,                   // bInterfaceProtocol
    0x08,                   // iInterface

    // Class-specific MS Interface Header Descriptor
    0x07,                   // bLength
    0x24,                   // bDescriptorType (CS_INTERFACE)
    0x01,                   // bDescriptorSubtype (MS_HEADER)
    0x00, 0x01,             // bcdMSC 1.00
    0x16, 0x00,             // wTotalLength (22 bytes: header + both jacks)

    // MIDI IN Jack Descriptor (Embedded) - fed by the Bulk OUT endpoint below
    0x06,                   // bLength
    0x24,                   // bDescriptorType (CS_INTERFACE)
    0x02,                   // bDescriptorSubtype (MIDI_IN_JACK)
    0x01,                   // bJackType (EMBEDDED)
    0x01,                   // bJackID (1)
    0x00,                   // iJack

    // MIDI OUT Jack Descriptor (Embedded) - feeds the Bulk IN endpoint below
    0x09,                   // bLength
    0x24,                   // bDescriptorType (CS_INTERFACE)
    0x03,                   // bDescriptorSubtype (MIDI_OUT_JACK)
    0x01,                   // bJackType (EMBEDDED)
    0x02,                   // bJackID (2)
    0x01,                   // bNrInputPins (1)
    0x01,                   // BaSourceID(1) - sourced from MIDI IN Jack 1
    0x01,                   // BaSourcePin(1)
    0x00,                   // iJack

    // Endpoint Descriptor (Bulk OUT) - MIDI data from host to device
    0x07,                   // bLength
    0x05,                   // bDescriptorType (Endpoint)
    CY_FX_EP_MIDI_OUT,      // bEndpointAddress (OUT endpoint 5)
    0x02,                   // bmAttributes (Bulk)
    0x40, 0x00,             // wMaxPacketSize
    0x00,                   // bInterval

    // Class-specific MS Bulk OUT Endpoint Descriptor
    0x05,                   // bLength
    0x25,                   // bDescriptorType (CS_ENDPOINT)
    0x01,                   // bDescriptorSubtype (MS_GENERAL)
    0x01,                   // bNumEmbMIDIJack
    0x01,                   // BaAssocJackID(1) - MIDI IN Jack 1

    // Endpoint Descriptor (Bulk IN) - MIDI data from device to host
    0x07,                   // bLength
    0x05,                   // bDescriptorType (Endpoint)
    CY_FX_EP_MIDI_IN,       // bEndpointAddress (IN endpoint 5)
    0x02,                   // bmAttributes (Bulk)
    0x40, 0x00,             // wMaxPacketSize
    0x00,                   // bInterval

    // Class-specific MS Bulk IN Endpoint Descriptor
    0x05,                   // bLength
    0x25,                   // bDescriptorType (CS_ENDPOINT)
    0x01,                   // bDescriptorSubtype (MS_GENERAL)
    0x01,                   // bNumEmbMIDIJack
    0x02,                   // BaAssocJackID(1) - MIDI OUT Jack 2

    //////////////////////////////

    CDC_DESCRIPTORS_FS

    ///////////////////////

    //Disk (the LAST interface: it is left out of the configuration while no disk is loaded)
    0x09,                   // bLength
    0x04,                   // bDescriptorType (Interface)
    CY_FX_USB_MSC_INTF2,    // bInterfaceNumber
    0x00,                   // bAlternateSetting
    0x02,                   // bNumEndpoints
    0x08,                   // bInterfaceClass (Mass Storage)
    0x06,                   // bInterfaceSubClass (SCSI transparent)
    0x50,                   // bInterfaceProtocol (Bulk-Only)
    0x04,                   // iInterface

    // Endpoint Descriptor (Bulk IN)
    0x07,                   // bLength
    0x05,                   // bDescriptorType (Endpoint)
    CY_FX_EP_STORAGE_IN,     // bEndpointAddress (IN endpoint 2)
    0x02,                   // bmAttributes (Bulk)
    0x40, 0x00,             // wMaxPacketSize
    0x00,                   // bInterval

    // Endpoint Descriptor (Bulk OUT)
    0x07,                   // bLength
    0x05,                   // bDescriptorType (Endpoint)
    CY_FX_EP_STORAGE_OUT,     // bEndpointAddress (OUT endpoint 2)
    0x02,                   // bmAttributes (Bulk)
    0x40, 0x00,             // wMaxPacketSize
    0x00,                   // bInterval
};

/* Standard language ID string descriptor */
const uint8_t CyFxUSBStringLangIDDscr[] __attribute__((aligned(32))) =
{
    0x04,                           /* Descriptor size */
    CY_U3P_USB_STRING_DESCR,        /* Device descriptor type */
    0x09,0x04                       /* Language ID supported */
};

/* Standard manufacturer string descriptor */
const uint8_t CyFxUSBManufactureDscr[] __attribute__((aligned(32))) =
{
    0xE,                           /* Descriptor size */
    CY_U3P_USB_STRING_DESCR,        /* Device descriptor type */
    'V',0x00,
    'u',0x00,
    'l',0x00,
    'p',0x00,
    'e',0x00,
    's',0x00
};

/* Standard product string descriptor */
const uint8_t CyFxUSBProductDscr[] __attribute__((aligned(32))) =
{
    0x46,                           /* Descriptor size */
    CY_U3P_USB_STRING_DESCR,        /* Device descriptor type */
    'V',0x00,
    'u',0x00,
    'l',0x00,
    'p',0x00,
    'e',0x00,
    's',0x00,
    ' ',0x00,
    'D',0x00,
    'o',0x00,
    'l',0x00,
    'c',0x00,
    'h',0x00,
    ' ',0x00,
    '-',0x00,
    ' ',0x00,
    'M',0x00,
    'u',0x00,
    'l',0x00,
    't',0x00,
    'i',0x00,
    ' ',0x00,
    'D',0x00,
    'e',0x00,
    'v',0x00,
    'i',0x00,
    'c',0x00,
    'e',0x00,
    ' ',0x00,
    'C',0x00,
    'l',0x00,
    'i',0x00,
    'e',0x00,
    'n',0x00,
    't',0x00,
};


/* Interface descriptor */
const uint8_t CyFxUSBInterfaceDesc1[] __attribute__((aligned(32))) =
{
    0x54,                           /* Descriptor size */
    CY_U3P_USB_STRING_DESCR,        /* Device descriptor type */
    'V',0x00,
    'u',0x00,
    'l',0x00,
    'p',0x00,
    'e',0x00,
    's',0x00,
    ' ',0x00,
    'D',0x00,
    'o',0x00,
    'l',0x00,
    'c',0x00,
    'h',0x00,
    ' ',0x00,
    '-',0x00,
    ' ',0x00,
    'M',0x00,
    'u',0x00,
    'l',0x00,
    't',0x00,
    'i',0x00,
    ' ',0x00,
    'D',0x00,
    'e',0x00,
    'v',0x00,
    'i',0x00,
    'c',0x00,
    'e',0x00,
    ' ',0x00,
    'C',0x00,
    'D',0x00,
    '-',0x00,
    'R',0x00,
    'O',0x00,
    'M',0x00,
    ' ',0x00,
    'C',0x00,
    'l',0x00,
    'i',0x00,
    'e',0x00,
    'n',0x00,
    't',0x00,
};


/* Interface descriptor */
const uint8_t CyFxUSBInterfaceDesc2[] __attribute__((aligned(32))) =
{
    0x4E,                           /* Descriptor size */
    CY_U3P_USB_STRING_DESCR,        /* Device descriptor type */
    'V',0x00,
    'u',0x00,
    'l',0x00,
    'p',0x00,
    'e',0x00,
    's',0x00,
    ' ',0x00,
    'D',0x00,
    'o',0x00,
    'l',0x00,
    'c',0x00,
    'h',0x00,
    ' ',0x00,
    '-',0x00,
    ' ',0x00,
    'M',0x00,
    'u',0x00,
    'l',0x00,
    't',0x00,
    'i',0x00,
    ' ',0x00,
    'D',0x00,
    'e',0x00,
    'v',0x00,
    'i',0x00,
    'c',0x00,
    'e',0x00,
    ' ',0x00,
    'H',0x00,
    'D',0x00,
    'D',0x00,
    ' ',0x00,
    'C',0x00,
    'l',0x00,
    'i',0x00,
    'e',0x00,
    'n',0x00,
    't',0x00,
};

/* Interface descriptor */
const uint8_t CyFxUSBInterfaceDesc3[] __attribute__((aligned(32))) =
{
    0x58,                           /* Descriptor size */
    CY_U3P_USB_STRING_DESCR,        /* Device descriptor type */
    'V',0x00,
    'u',0x00,
    'l',0x00,
    'p',0x00,
    'e',0x00,
    's',0x00,
    ' ',0x00,
    'D',0x00,
    'o',0x00,
    'l',0x00,
    'c',0x00,
    'h',0x00,
    ' ',0x00,
    '-',0x00,
    ' ',0x00,
    'M',0x00,
    'u',0x00,
    'l',0x00,
    't',0x00,
    'i',0x00,
    ' ',0x00,
    'D',0x00,
    'e',0x00,
    'v',0x00,
    'i',0x00,
    'c',0x00,
    'e',0x00,
    ' ',0x00,
    'K',0x00,
    'e',0x00,
    'y',0x00,
    'b',0x00,
    'o',0x00,
    'a',0x00,
    'r',0x00,
    'd',0x00,
    ' ',0x00,
    'C',0x00,
    'l',0x00,
    'i',0x00,
    'e',0x00,
    'n',0x00,
    't',0x00,
};

/* Interface descriptor */
const uint8_t CyFxUSBInterfaceDesc4[] __attribute__((aligned(32))) =
{
    0x54,                           /* Descriptor size */
    CY_U3P_USB_STRING_DESCR,        /* Device descriptor type */
    'V',0x00,
    'u',0x00,
    'l',0x00,
    'p',0x00,
    'e',0x00,
    's',0x00,
    ' ',0x00,
    'D',0x00,
    'o',0x00,
    'l',0x00,
    'c',0x00,
    'h',0x00,
    ' ',0x00,
    '-',0x00,
    ' ',0x00,
    'M',0x00,
    'u',0x00,
    'l',0x00,
    't',0x00,
    'i',0x00,
    ' ',0x00,
    'D',0x00,
    'e',0x00,
    'v',0x00,
    'i',0x00,
    'c',0x00,
    'e',0x00,
    ' ',0x00,
    'M',0x00,
    'o',0x00,
    'u',0x00,
    's',0x00,
    'e',0x00,
    ' ',0x00,
    'C',0x00,
    'l',0x00,
    'i',0x00,
    'e',0x00,
    'n',0x00,
    't',0x00,
};

/* Interface descriptor */
const uint8_t CyFxUSBInterfaceDesc5[] __attribute__((aligned(32))) =
{
    0x50,                           /* Descriptor size */
    CY_U3P_USB_STRING_DESCR,        /* Device descriptor type */
    'V',0x00,
    'u',0x00,
    'l',0x00,
    'p',0x00,
    'e',0x00,
    's',0x00,
    ' ',0x00,
    'D',0x00,
    'o',0x00,
    'l',0x00,
    'c',0x00,
    'h',0x00,
    ' ',0x00,
    '-',0x00,
    ' ',0x00,
    'M',0x00,
    'u',0x00,
    'l',0x00,
    't',0x00,
    'i',0x00,
    ' ',0x00,
    'D',0x00,
    'e',0x00,
    'v',0x00,
    'i',0x00,
    'c',0x00,
    'e',0x00,
    ' ',0x00,
    'M',0x00,
    'I',0x00,
    'D',0x00,
    'I',0x00,
    ' ',0x00,
    'C',0x00,
    'l',0x00,
    'i',0x00,
    'e',0x00,
    'n',0x00,
    't',0x00,
};

/* Interface descriptor: the serial port ("Vulpes Dolch - Multi Device Serial Client"). */
const uint8_t CyFxUSBInterfaceDesc6[] __attribute__((aligned(32))) =
{
    0x54,                           /* Descriptor size */
    CY_U3P_USB_STRING_DESCR,        /* String descriptor type */
    'V',0x00, 'u',0x00, 'l',0x00, 'p',0x00, 'e',0x00, 's',0x00, ' ',0x00, 'D',0x00, 'o',0x00, 'l',0x00, 'c',0x00, 'h',0x00,
    ' ',0x00, '-',0x00, ' ',0x00, 'M',0x00, 'u',0x00, 'l',0x00, 't',0x00, 'i',0x00, ' ',0x00, 'D',0x00, 'e',0x00, 'v',0x00,
    'i',0x00, 'c',0x00, 'e',0x00, ' ',0x00, 'S',0x00, 'e',0x00, 'r',0x00, 'i',0x00, 'a',0x00, 'l',0x00, ' ',0x00, 'C',0x00,
    'l',0x00, 'i',0x00, 'e',0x00, 'n',0x00, 't',0x00,
};

/* Microsoft OS Descriptor. */
const uint8_t CyFxUsbOSDscr[] __attribute__((aligned(32))) =
{
    0x0E,
    CY_U3P_USB_STRING_DESCR,
    'O', 0x00,
    'S', 0x00,
    ' ', 0x00,
    'D', 0x00,
    'e', 0x00,
    's', 0x00,
    'c', 0x00
};

/* Serial Number */
const uint8_t CyFxUsbSN[] __attribute__((aligned(32))) =
{
    0x40,
    CY_U3P_USB_STRING_DESCR,
    '2', 0x00,
    'S', 0x00,
    'E', 0x00,
    'R', 0x00,
    'I', 0x00,
    'A', 0x00,
    'L', 0x00,
    'S', 0x00,
    'E', 0x00,
    'R', 0x00,
    'I', 0x00,
    'A', 0x00,
    'L', 0x00,
    'S', 0x00,
    'E', 0x00,
    'R', 0x00,
    'I', 0x00,
    'A', 0x00,
    'L', 0x00,
    'S', 0x00,
    'E', 0x00,
    'R', 0x00,
    'I', 0x00,
    'A', 0x00,
    'L', 0x00,
    'S', 0x00,
    'E', 0x00,
    'R', 0x00,
    'I', 0x00,
    'A', 0x00,
    'L', 0x00
};

/* HID Report Descriptor of the ONE HID interface: Boot Keyboard (USB HID spec, Appendix B.1) as report ID 1, Mouse with wheel as report ID 2.
 * In report protocol every input report starts with its report ID (keyboard: 1 + 8 bytes, mouse: 1 + 4 bytes), the LED output report is
 * ID 1 + 1 byte. In boot protocol (BIOS) the keyboard report is the plain 8 bytes without an ID, and there is no mouse. */
const uint8_t CyFxUSBHidReportDscr[] __attribute__((aligned(32))) =
{
        0x05, 0x01,                    // USAGE_PAGE (Generic Desktop)
        0x09, 0x06,                    // USAGE (Keyboard)
        0xa1, 0x01,                    // COLLECTION (Application)
        0x85, 0x01,                    //   REPORT_ID (1)
        0x05, 0x07,                    //   USAGE_PAGE (Keyboard/Keypad)
        0x19, 0xe0,                    //   USAGE_MINIMUM (Keyboard LeftControl)
        0x29, 0xe7,                    //   USAGE_MAXIMUM (Keyboard Right GUI)
        0x15, 0x00,                    //   LOGICAL_MINIMUM (0)
        0x25, 0x01,                    //   LOGICAL_MAXIMUM (1)
        0x75, 0x01,                    //   REPORT_SIZE (1)
        0x95, 0x08,                    //   REPORT_COUNT (8)
        0x81, 0x02,                    //   INPUT (Data,Var,Abs) - modifier byte
        0x95, 0x01,                    //   REPORT_COUNT (1)
        0x75, 0x08,                    //   REPORT_SIZE (8)
        0x81, 0x01,                    //   INPUT (Cnst,Ary,Abs) - reserved byte
        0x95, 0x05,                    //   REPORT_COUNT (5)
        0x75, 0x01,                    //   REPORT_SIZE (1)
        0x05, 0x08,                    //   USAGE_PAGE (LEDs)
        0x19, 0x01,                    //   USAGE_MINIMUM (Num Lock)
        0x29, 0x05,                    //   USAGE_MAXIMUM (Kana)
        0x91, 0x02,                    //   OUTPUT (Data,Var,Abs) - LED report
        0x95, 0x01,                    //   REPORT_COUNT (1)
        0x75, 0x03,                    //   REPORT_SIZE (3)
        0x91, 0x01,                    //   OUTPUT (Cnst,Ary,Abs) - LED report padding
        0x95, 0x06,                    //   REPORT_COUNT (6)
        0x75, 0x08,                    //   REPORT_SIZE (8)
        0x15, 0x00,                    //   LOGICAL_MINIMUM (0)
        0x25, 0x65,                    //   LOGICAL_MAXIMUM (101)
        0x05, 0x07,                    //   USAGE_PAGE (Keyboard/Keypad)
        0x19, 0x00,                    //   USAGE_MINIMUM (0)
        0x29, 0x65,                    //   USAGE_MAXIMUM (101)
        0x81, 0x00,                    //   INPUT (Data,Ary,Abs) - key array (6 bytes)
        0xc0,                           // END_COLLECTION

        /* Mouse with wheel (Boot Mouse layout from USB HID spec, Appendix B.2, extended with a 4th "wheel" byte) */
        0x05, 0x01,                    // USAGE_PAGE (Generic Desktop)
        0x09, 0x02,                    // USAGE (Mouse)
        0xa1, 0x01,                    // COLLECTION (Application)
        0x85, 0x02,                    //   REPORT_ID (2)
        0x09, 0x01,                    //   USAGE (Pointer)
        0xa1, 0x00,                    //   COLLECTION (Physical)
        0x05, 0x09,                    //     USAGE_PAGE (Button)
        0x19, 0x01,                    //     USAGE_MINIMUM (Button 1)
        0x29, 0x03,                    //     USAGE_MAXIMUM (Button 3)
        0x15, 0x00,                    //     LOGICAL_MINIMUM (0)
        0x25, 0x01,                    //     LOGICAL_MAXIMUM (1)
        0x95, 0x03,                    //     REPORT_COUNT (3)
        0x75, 0x01,                    //     REPORT_SIZE (1)
        0x81, 0x02,                    //     INPUT (Data,Var,Abs) - 3 button bits
        0x95, 0x01,                    //     REPORT_COUNT (1)
        0x75, 0x05,                    //     REPORT_SIZE (5)
        0x81, 0x01,                    //     INPUT (Cnst,Ary,Abs) - 5 bit padding
        0x05, 0x01,                    //     USAGE_PAGE (Generic Desktop)
        0x09, 0x30,                    //     USAGE (X)
        0x09, 0x31,                    //     USAGE (Y)
        0x15, 0x81,                    //     LOGICAL_MINIMUM (-127)
        0x25, 0x7f,                    //     LOGICAL_MAXIMUM (127)
        0x75, 0x08,                    //     REPORT_SIZE (8)
        0x95, 0x02,                    //     REPORT_COUNT (2)
        0x81, 0x06,                    //     INPUT (Data,Var,Rel) - X, Y
        0x09, 0x38,                    //     USAGE (Wheel)
        0x95, 0x01,                    //     REPORT_COUNT (1)
        0x81, 0x06,                    //     INPUT (Data,Var,Rel) - Wheel
        0xc0,                           //   END_COLLECTION
        0xc0                            // END_COLLECTION
};

/* Place this buffer as the last buffer so that no other variable / code shares
 * the same cache line. Do not add any other variables / arrays in this file.
 * This will lead to variables sharing the same cache line. */
const uint8_t CyFxUsbDscrAlignBuffer[32] __attribute__((aligned(32)));

/* [ ] */



#ifdef VISUALSTUDIO

int Get_CyFxUSBHSConfigDscr1_SZ()
{
    return(sizeof(CyFxUSBHSConfigDscr1));
}

int Get_CyFxUSBSSConfigDscr1_SZ()
{
    return(sizeof(CyFxUSBSSConfigDscr1));
}

int Get_CyFxUSBFSConfigDscr1_SZ()
{
    return(sizeof(CyFxUSBFSConfigDscr1));
}

#endif
