
#include "main.h"

#define VENDOR_ID 0x09,0x12
#define DEVICE_ID 0x14,0x35

#define MIDI_AUDIO								0x01
#define MIDI_AUDIO_CONTROL						0x01
#define MIDI_CS_INTERFACE						0x24
#define MIDI_CS_ENDPOINT						0x25
#define MIDI_STREAMING							0x3
#define MIDI_JACK_EMD							0x01
#define MIDI_JACK_EXT							0x02
#define USB_ENDPOINT_TYPE_BULK                 0x02
#define MIDI_BUFFER_SIZE_SMALL			0x40,0x00
#define MIDI_BUFFER_SIZE_BIG			0x00,0x04
#define MIDI_BUFFER_SIZE_HS			0x00,0x02

/* The serial port: a CDC-ACM function (interface association descriptor + communication interface with a notification endpoint + data interface
 * with a bulk pair), interfaces 3 and 4, endpoints 0x86 (interrupt IN), 0x05 / 0x85 (bulk). String 4 names the function and the communication
 * interface. 66 bytes at high and full speed, 84 bytes at super speed (companions). */
#define CDC_FUNCTION_STRING 0x04
#define CDC_COMM_INTERFACE 3
#define CDC_DATA_INTERFACE_NUMBER 4
#define CDC_COMMON(NotifyInterval) \
    0x08, 0x0B, CDC_COMM_INTERFACE, 0x02, 0x02, 0x02, 0x01, CDC_FUNCTION_STRING, \
    0x09, 0x04, CDC_COMM_INTERFACE, 0x00, 0x01, 0x02, 0x02, 0x01, CDC_FUNCTION_STRING, \
    0x05, 0x24, 0x00, 0x10, 0x01, \
    0x05, 0x24, 0x01, 0x00, CDC_DATA_INTERFACE_NUMBER, \
    0x04, 0x24, 0x02, 0x02, \
    0x05, 0x24, 0x06, CDC_COMM_INTERFACE, CDC_DATA_INTERFACE_NUMBER, \
    0x07, 0x05, CY_FX_EP_SERIAL_NOTIFY, 0x03, 0x10, 0x00, NotifyInterval,
#define CDC_DATA_INTERFACE \
    0x09, 0x04, CDC_DATA_INTERFACE_NUMBER, 0x00, 0x02, 0x0A, 0x00, 0x00, 0x00,
#define CDC_DESCRIPTORS_SS \
    CDC_COMMON(0x0B) \
    0x06, 0x30, 0x00, 0x00, 0x10, 0x00, \
    CDC_DATA_INTERFACE \
    0x07, 0x05, CY_FX_EP_SERIAL_OUT, 0x02, 0x00, 0x04, 0x00, \
    0x06, 0x30, 0x00, 0x00, 0x00, 0x00, \
    0x07, 0x05, CY_FX_EP_SERIAL_IN, 0x02, 0x00, 0x04, 0x00, \
    0x06, 0x30, 0x00, 0x00, 0x00, 0x00,
#define CDC_DESCRIPTORS_HS \
    CDC_COMMON(0x0B) \
    CDC_DATA_INTERFACE \
    0x07, 0x05, CY_FX_EP_SERIAL_OUT, 0x02, 0x00, 0x02, 0x00, \
    0x07, 0x05, CY_FX_EP_SERIAL_IN, 0x02, 0x00, 0x02, 0x00,
#define CDC_DESCRIPTORS_FS \
    CDC_COMMON(0x10) \
    CDC_DATA_INTERFACE \
    0x07, 0x05, CY_FX_EP_SERIAL_OUT, 0x02, 0x40, 0x00, 0x00, \
    0x07, 0x05, CY_FX_EP_SERIAL_IN, 0x02, 0x40, 0x00, 0x00,

#define MIDI_AC_INTERFACE	1
#define MIDI_INTERFACE 2

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
    0x32,0x00,                      /* Length of this descriptor and all sub descriptors */
    0x03,                           /* Number of device capability descriptors */

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
   0xFF,0x07,                      /* U2 Device Exit latency */

    /* Platform capability: Microsoft OS 2.0 descriptor set (28 bytes). The set itself is sent by the vendor request below. */
    0x1C,                           /* Descriptor size */
    CY_U3P_DEVICE_CAPB_DESCR,       /* Device capability type descriptor */
    0x05,                           /* Platform capability type */
    0x00,                           /* Reserved */
    0xDF,0x60,0xDD,0xD8,0x89,0x45,0xC7,0x4C,
    0x9C,0xD2,0x65,0x9D,0x9E,0x64,0x8A,0x9F,    /* MS OS 2.0 platform capability UUID D8DD60DF-4589-4CC7-9CD2-659D9E648A9F */
    0x00,0x00,0x03,0x06,            /* Windows version 8.1 (0x06030000) */
    (CY_FX_MSOS20_SET_LENGTH & 0xFF), (CY_FX_MSOS20_SET_LENGTH >> 8),    /* Length of the descriptor set */
    CY_FX_MSOS20_VENDOR_CODE,       /* Vendor request code that fetches the set */
    0x00                            /* No alternate enumeration */
};

/* Standard device qualifier descriptor */
const uint8_t CyFxUSBDeviceQualDscr[] __attribute__((aligned(32))) =
{
    0x0A,                           /* Descriptor size */
    CY_U3P_USB_DEVQUAL_DESCR,       /* Device qualifier descriptor type */
    0x00,0x02,                      /* USB 2.0 */
    0xEF,                           /* Device class */
    0x02,                           /* Device sub-class */
    0x01,                           /* Device protocol */
    0x40,                           /* Maxpacket size for EP0 : 64 bytes */
    0x01,                           /* Number of configurations */
    0x00                            /* Reserved */
};

/* Standard super speed configuration descriptor */
const uint8_t CyFxUSBSSConfigDscr1[] __attribute__((aligned(32))) =
{
    /* Config Descriptor */
    0x09, CY_U3P_USB_CONFIG_DESCR,
    0x02, 0x01,                     /* wTotalLength */
    0x05,                           /* Number of interfaces: PC link (WinUSB), MIDI audio control, MIDI streaming, serial port control, serial port data */
    0x01,                           /* Configuration number */
    0x00,                           /* Configuration string index */
    0x80,                           /* Config characteristics - Bus powered */
    0x32,                           /* Max power consumption of device (in 2mA unit) : 100mA */

    /* Interface Descriptor 0: the PC link - vendor specific class with a bulk OUT and a bulk IN endpoint, bound to the inbox
     * WinUSB driver through the Microsoft OS 2.0 descriptors. Interface 0 is the link, MIDI is 1 and 2,
     * the CDC-ACM serial port is 3 (control) and 4 (data). */
    0x09,                           /* Descriptor size */
    CY_U3P_USB_INTRFC_DESCR,        /* Interface Descriptor type */
    0x00,                           /* Interface number */
    0x00,                           /* Alternate setting number */
    0x04,                           /* Number of end points: the link pair and the dedicated bulk data pair */
    0xFF,                           /* Interface class : vendor specific */
    0x00,                           /* Interface sub class */
    0x00,                           /* Interface protocol code */
    0x03,                           /* Interface descriptor string index */

    /* Endpoint Descriptor: bulk OUT, PC -> Host */
    0x07,                           /* Descriptor size */
    CY_U3P_USB_ENDPNT_DESCR,        /* Endpoint Descriptor Type */
    CY_FX_EP_PRODUCER1,             /* Endpoint address and description */
    CY_U3P_USB_EP_BULK,             /* Bulk Endpoint type */
    0x00,0x04,                      /* Max packet size = 1024 bytes */
    0x00,                           /* Servicing interval: none for bulk */

    /* Super speed endpoint companion descriptor */
    0x06,                           /* Descriptor size */
    CY_U3P_SS_EP_COMPN_DESCR,       /* SS endpoint companion descriptor type */
    (CY_FX_LINK_EP_BURST - 1),      /* Max no. of packets in a burst (0-15) */
    0x00,                           /* Max streams for bulk EP = 0 (No streams) */
    0x00,0x00,                      /* Service interval for the EP : 0 for bulk */

    /* Endpoint Descriptor: bulk IN, Host -> PC */
    0x07,                           /* Descriptor size */
    CY_U3P_USB_ENDPNT_DESCR,        /* Endpoint Descriptor Type */
    CY_FX_EP_CONSUMER1,             /* Endpoint address and description */
    CY_U3P_USB_EP_BULK,             /* Bulk Endpoint type */
    0x00,0x04,                      /* Max packet size = 1024 bytes */
    0x00,                           /* Servicing interval: none for bulk */

    /* Super speed endpoint companion descriptor */
    0x06,                           /* Descriptor size */
    CY_U3P_SS_EP_COMPN_DESCR,       /* SS endpoint companion descriptor type */
    (CY_FX_LINK_EP_BURST - 1),      /* Max no. of packets in a burst (0-15) */
    0x00,                           /* Max streams for bulk EP = 0 (No streams) */
    0x00,0x00,                      /* Service interval for the EP : 0 for bulk */

    /* Endpoint Descriptor: bulk OUT 2, PC -> Host, the dedicated bulk data lane (GPIF threads 2 / 3) */
    0x07, CY_U3P_USB_ENDPNT_DESCR, CY_FX_EP_PRODUCER2, CY_U3P_USB_EP_BULK, 0x00,0x04, 0x00,
    0x06, CY_U3P_SS_EP_COMPN_DESCR, (CY_FX_LINK_EP_BURST - 1), 0x00, 0x00,0x00,
    /* Endpoint Descriptor: bulk IN 2, Host -> PC */
    0x07, CY_U3P_USB_ENDPNT_DESCR, CY_FX_EP_CONSUMER2, CY_U3P_USB_EP_BULK, 0x00,0x04, 0x00,
    0x06, CY_U3P_SS_EP_COMPN_DESCR, (CY_FX_LINK_EP_BURST - 1), 0x00, 0x00,0x00,

    //////////////////////////////

    //MIDI

    0x9, CY_U3P_USB_INTRFC_DESCR, MIDI_AC_INTERFACE, 0x0, 0, MIDI_AUDIO, MIDI_AUDIO_CONTROL, 0x0, 0x06,
    0x9, MIDI_CS_INTERFACE, 0x1, 0x00, 0x01, 0x09, 0x00, 0x1, MIDI_INTERFACE,
    0x9, CY_U3P_USB_INTRFC_DESCR, MIDI_INTERFACE, 0x0, 2, MIDI_AUDIO, MIDI_STREAMING, 0x0, 0x06,
    0x7, MIDI_CS_INTERFACE, 0x1, 0x00, 0x01, 0x41, 0x00,
    0x6, MIDI_CS_INTERFACE, 0x2, MIDI_JACK_EMD, 0x1, 0x6,
    0x9, MIDI_CS_INTERFACE, 0x3, MIDI_JACK_EXT, 0x4, 1, 1, 1, 0x0,
    0x6, MIDI_CS_INTERFACE, 0x2, MIDI_JACK_EXT, 0x2, 0x0,
    0x9, MIDI_CS_INTERFACE, 0x3, MIDI_JACK_EMD, 0x3, 1, 2, 1, 0x6,
    0x9, 0x5, CY_FX_EP_PRODUCER4, USB_ENDPOINT_TYPE_BULK, MIDI_BUFFER_SIZE_BIG, 0, 0, 0,

    /* Super speed endpoint companion descriptor for consumer EP */
    0x06,                           /* Descriptor size */
    CY_U3P_SS_EP_COMPN_DESCR,       /* SS endpoint companion descriptor type */
    (CY_FX_EP_BURST_LENGTH - 1),    /* Max no. of packets in a burst(0-15) - 0: burst 1 packet at a time */
    0x00,                           /* Max streams for bulk EP = 0 (No streams) */
    0x00, 0x00,                      /* Service interval for the EP : 0 for bulk */

    0x5, MIDI_CS_ENDPOINT, 0x1, 1, 1,
    0x9, 0x5, CY_FX_EP_CONSUMER4, USB_ENDPOINT_TYPE_BULK, MIDI_BUFFER_SIZE_BIG, 0, 0, 0,

    /* Super speed endpoint companion descriptor for consumer EP */
    0x06,                           /* Descriptor size */
    CY_U3P_SS_EP_COMPN_DESCR,       /* SS endpoint companion descriptor type */
    (CY_FX_EP_BURST_LENGTH - 1),    /* Max no. of packets in a burst(0-15) - 0: burst 1 packet at a time */
    0x00,                           /* Max streams for bulk EP = 0 (No streams) */
    0x00, 0x00,                      /* Service interval for the EP : 0 for bulk */

    0x5, MIDI_CS_ENDPOINT, 0x1, 1, 3,

    CDC_DESCRIPTORS_SS
};
/* Standard high speed configuration descriptor */
const uint8_t CyFxUSBHSConfigDscr1[] __attribute__((aligned(32))) =
{
    /* Config Descriptor */
    0x09, CY_U3P_USB_CONFIG_DESCR,
    0xCC, 0x00,                     /* wTotalLength */
    0x05,                           /* Number of interfaces: PC link (WinUSB), MIDI audio control, MIDI streaming, serial port control, serial port data */
    0x01,                           /* Configuration number */
    0x00,                           /* Configuration string index */
    0x80,                           /* Config characteristics - Bus powered */
    0x32,                           /* Max power consumption of device (in 2mA unit) : 100mA */

    /* Interface Descriptor 0: the PC link - vendor specific class with a bulk OUT and a bulk IN endpoint, bound to the inbox
     * WinUSB driver through the Microsoft OS 2.0 descriptors. Interface 0 is the link, MIDI is 1 and 2,
     * the CDC-ACM serial port is 3 (control) and 4 (data). */
    0x09,                           /* Descriptor size */
    CY_U3P_USB_INTRFC_DESCR,        /* Interface Descriptor type */
    0x00,                           /* Interface number */
    0x00,                           /* Alternate setting number */
    0x04,                           /* Number of end points: the link pair and the dedicated bulk data pair */
    0xFF,                           /* Interface class : vendor specific */
    0x00,                           /* Interface sub class */
    0x00,                           /* Interface protocol code */
    0x03,                           /* Interface descriptor string index */

    /* Endpoint Descriptor: bulk OUT, PC -> Host */
    0x07,                           /* Descriptor size */
    CY_U3P_USB_ENDPNT_DESCR,        /* Endpoint Descriptor Type */
    CY_FX_EP_PRODUCER1,             /* Endpoint address and description */
    CY_U3P_USB_EP_BULK,             /* Bulk Endpoint type */
    0x00,0x02,                      /* Max packet size */
    0x00,                           /* Servicing interval: none for bulk */

    /* Endpoint Descriptor: bulk IN, Host -> PC */
    0x07,                           /* Descriptor size */
    CY_U3P_USB_ENDPNT_DESCR,        /* Endpoint Descriptor Type */
    CY_FX_EP_CONSUMER1,             /* Endpoint address and description */
    CY_U3P_USB_EP_BULK,             /* Bulk Endpoint type */
    0x00,0x02,                      /* Max packet size */
    0x00,                           /* Servicing interval: none for bulk */

    /* Endpoint Descriptor: bulk OUT 2, PC -> Host, the dedicated bulk data lane (GPIF threads 2 / 3) */
    0x07, CY_U3P_USB_ENDPNT_DESCR, CY_FX_EP_PRODUCER2, CY_U3P_USB_EP_BULK, 0x00,0x02, 0x00,
    /* Endpoint Descriptor: bulk IN 2, Host -> PC */
    0x07, CY_U3P_USB_ENDPNT_DESCR, CY_FX_EP_CONSUMER2, CY_U3P_USB_EP_BULK, 0x00,0x02, 0x00,

    //////////////////////////////

    //MIDI

    0x9, CY_U3P_USB_INTRFC_DESCR, MIDI_AC_INTERFACE, 0x0, 0, MIDI_AUDIO, MIDI_AUDIO_CONTROL, 0x0, 0x06,
    0x9, MIDI_CS_INTERFACE, 0x1, 0x00, 0x01, 0x09, 0x00, 0x1, MIDI_INTERFACE,
    0x9, CY_U3P_USB_INTRFC_DESCR, MIDI_INTERFACE, 0x0, 2, MIDI_AUDIO, MIDI_STREAMING, 0x0, 0x06,
    0x7, MIDI_CS_INTERFACE, 0x1, 0x00, 0x01, 0x41, 0x00,
    0x6, MIDI_CS_INTERFACE, 0x2, MIDI_JACK_EMD, 0x1, 0x6,
    0x9, MIDI_CS_INTERFACE, 0x3, MIDI_JACK_EXT, 0x4, 1, 1, 1, 0x0,
    0x6, MIDI_CS_INTERFACE, 0x2, MIDI_JACK_EXT, 0x2, 0x0,
    0x9, MIDI_CS_INTERFACE, 0x3, MIDI_JACK_EMD, 0x3, 1, 2, 1, 0x6,
    0x9, 0x5, CY_FX_EP_PRODUCER4, USB_ENDPOINT_TYPE_BULK, MIDI_BUFFER_SIZE_HS, 0, 0, 0,

    0x5, MIDI_CS_ENDPOINT, 0x1, 1, 1,
    0x9, 0x5, CY_FX_EP_CONSUMER4, USB_ENDPOINT_TYPE_BULK, MIDI_BUFFER_SIZE_HS, 0, 0, 0,

    0x5, MIDI_CS_ENDPOINT, 0x1, 1, 3,

    CDC_DESCRIPTORS_HS
};
/* Standard full speed configuration descriptor */
const uint8_t CyFxUSBFSConfigDscr1[] __attribute__((aligned(32))) =
{
    /* Config Descriptor */
    0x09, CY_U3P_USB_CONFIG_DESCR,
    0xCC, 0x00,                     /* wTotalLength */
    0x05,                           /* Number of interfaces: PC link (WinUSB), MIDI audio control, MIDI streaming, serial port control, serial port data */
    0x01,                           /* Configuration number */
    0x00,                           /* Configuration string index */
    0x80,                           /* Config characteristics - Bus powered */
    0x32,                           /* Max power consumption of device (in 2mA unit) : 100mA */

    /* Interface Descriptor 0: the PC link - vendor specific class with a bulk OUT and a bulk IN endpoint, bound to the inbox
     * WinUSB driver through the Microsoft OS 2.0 descriptors. Interface 0 is the link, MIDI is 1 and 2,
     * the CDC-ACM serial port is 3 (control) and 4 (data). */
    0x09,                           /* Descriptor size */
    CY_U3P_USB_INTRFC_DESCR,        /* Interface Descriptor type */
    0x00,                           /* Interface number */
    0x00,                           /* Alternate setting number */
    0x04,                           /* Number of end points: the link pair and the dedicated bulk data pair */
    0xFF,                           /* Interface class : vendor specific */
    0x00,                           /* Interface sub class */
    0x00,                           /* Interface protocol code */
    0x03,                           /* Interface descriptor string index */

    /* Endpoint Descriptor: bulk OUT, PC -> Host */
    0x07,                           /* Descriptor size */
    CY_U3P_USB_ENDPNT_DESCR,        /* Endpoint Descriptor Type */
    CY_FX_EP_PRODUCER1,             /* Endpoint address and description */
    CY_U3P_USB_EP_BULK,             /* Bulk Endpoint type */
    0x40,0x00,                      /* Max packet size */
    0x00,                           /* Servicing interval: none for bulk */

    /* Endpoint Descriptor: bulk IN, Host -> PC */
    0x07,                           /* Descriptor size */
    CY_U3P_USB_ENDPNT_DESCR,        /* Endpoint Descriptor Type */
    CY_FX_EP_CONSUMER1,             /* Endpoint address and description */
    CY_U3P_USB_EP_BULK,             /* Bulk Endpoint type */
    0x40,0x00,                      /* Max packet size */
    0x00,                           /* Servicing interval: none for bulk */

    /* Endpoint Descriptor: bulk OUT 2, PC -> Host, the dedicated bulk data lane (GPIF threads 2 / 3) */
    0x07, CY_U3P_USB_ENDPNT_DESCR, CY_FX_EP_PRODUCER2, CY_U3P_USB_EP_BULK, 0x40,0x00, 0x00,
    /* Endpoint Descriptor: bulk IN 2, Host -> PC */
    0x07, CY_U3P_USB_ENDPNT_DESCR, CY_FX_EP_CONSUMER2, CY_U3P_USB_EP_BULK, 0x40,0x00, 0x00,

    //////////////////////////////

    //MIDI

    0x9, CY_U3P_USB_INTRFC_DESCR, MIDI_AC_INTERFACE, 0x0, 0, MIDI_AUDIO, MIDI_AUDIO_CONTROL, 0x0, 0x06,
    0x9, MIDI_CS_INTERFACE, 0x1, 0x00, 0x01, 0x09, 0x00, 0x1, MIDI_INTERFACE,
    0x9, CY_U3P_USB_INTRFC_DESCR, MIDI_INTERFACE, 0x0, 2, MIDI_AUDIO, MIDI_STREAMING, 0x0, 0x06,
    0x7, MIDI_CS_INTERFACE, 0x1, 0x00, 0x01, 0x41, 0x00,
    0x6, MIDI_CS_INTERFACE, 0x2, MIDI_JACK_EMD, 0x1, 0x6,
    0x9, MIDI_CS_INTERFACE, 0x3, MIDI_JACK_EXT, 0x4, 1, 1, 1, 0x0,
    0x6, MIDI_CS_INTERFACE, 0x2, MIDI_JACK_EXT, 0x2, 0x0,
    0x9, MIDI_CS_INTERFACE, 0x3, MIDI_JACK_EMD, 0x3, 1, 2, 1, 0x6,
    0x9, 0x5, CY_FX_EP_PRODUCER4, USB_ENDPOINT_TYPE_BULK, MIDI_BUFFER_SIZE_SMALL, 0, 0, 0,

    0x5, MIDI_CS_ENDPOINT, 0x1, 1, 1,
    0x9, 0x5, CY_FX_EP_CONSUMER4, USB_ENDPOINT_TYPE_BULK, MIDI_BUFFER_SIZE_SMALL, 0, 0, 0,

    0x5, MIDI_CS_ENDPOINT, 0x1, 1, 3,

    CDC_DESCRIPTORS_FS
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
    0x42,                           /* Descriptor size */
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
    'o',0x00,
    's',0x00,
    't',0x00,
};


/* Interface descriptor */
const uint8_t CyFxUSBInterfaceDesc1[] __attribute__((aligned(32))) =
{
    0x5E,                           /* Descriptor size */
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
    'a',0x00,
    'i',0x00,
    'n',0x00,
    ' ',0x00,
    'H',0x00,
    'o',0x00,
    's',0x00,
    't',0x00,
    ' ',0x00,
    'E',0x00,
    'n',0x00,
    'd',0x00,
    'p',0x00,
    'o',0x00,
    'i',0x00,
    'n',0x00,
    't',0x00,
};


/* Interface descriptor */
const uint8_t CyFxUSBInterfaceDesc4[] __attribute__((aligned(32))) =
{
    0x4C,                           /* Descriptor size */
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
    'H',0x00,
    'o',0x00,
    's',0x00,
    't',0x00,
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
/* Interface / function name: "Vulpes Dolch - Multi Device Serial Host". */
const uint8_t CyFxUSBInterfaceDesc2[] __attribute__((aligned(32))) =
{
    0x50,                           /* Descriptor size */
    CY_U3P_USB_STRING_DESCR,        /* String descriptor type */
    'V',0x00, 'u',0x00, 'l',0x00, 'p',0x00, 'e',0x00, 's',0x00, ' ',0x00, 'D',0x00, 'o',0x00, 'l',0x00, 'c',0x00, 'h',0x00, ' ',0x00, '-',0x00, ' ',0x00, 'M',0x00, 'u',0x00, 'l',0x00, 't',0x00, 'i',0x00, ' ',0x00, 'D',0x00, 'e',0x00, 'v',0x00, 'i',0x00, 'c',0x00, 'e',0x00, ' ',0x00, 'S',0x00, 'e',0x00, 'r',0x00, 'i',0x00, 'a',0x00, 'l',0x00, ' ',0x00, 'H',0x00, 'o',0x00, 's',0x00, 't',0x00
};

const uint8_t CyFxUsbSN[] __attribute__((aligned(32))) =
{
    0x40,
    CY_U3P_USB_STRING_DESCR,
    '1', 0x00,
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

/* Microsoft OS 2.0 descriptor set (178 bytes = CY_FX_MSOS20_SET_LENGTH): binds interface 0 to the inbox WinUSB driver and gives it the
 * device interface GUID the PC tool opens ({4D7E3A10-0F6B-4C85-9A2E-6B1D5F0C8E31}). The platform capability in the BOS descriptor
 * points the host at it; it is fetched with the vendor request CY_FX_MSOS20_VENDOR_CODE, wIndex 7. */
const uint8_t CyFxMsOs20Dscr[] __attribute__((aligned(32))) =
{
    /* Set header */
    0x0A,0x00, 0x00,0x00,           /* Length, MS_OS_20_SET_HEADER_DESCRIPTOR */
    0x00,0x00,0x03,0x06,            /* Windows version 8.1 */
    (CY_FX_MSOS20_SET_LENGTH & 0xFF), (CY_FX_MSOS20_SET_LENGTH >> 8),    /* Total length of the set */

    /* Configuration subset header */
    0x08,0x00, 0x01,0x00,           /* Length, MS_OS_20_SUBSET_HEADER_CONFIGURATION */
    0x00,                           /* Configuration value 0 = the first configuration */
    0x00,                           /* Reserved */
    0xA8,0x00,                      /* Length of the subset: 168 */

    /* Function subset header */
    0x08,0x00, 0x02,0x00,           /* Length, MS_OS_20_SUBSET_HEADER_FUNCTION */
    0x00,                           /* First interface of the function: 0 */
    0x00,                           /* Reserved */
    0xA0,0x00,                      /* Length of the subset: 160 */

    /* Compatible ID: WINUSB */
    0x14,0x00, 0x03,0x00,           /* Length 20, MS_OS_20_FEATURE_COMPATIBLE_ID */
    'W','I','N','U','S','B',0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,

    /* Registry property: DeviceInterfaceGUIDs (REG_MULTI_SZ) */
    0x84,0x00, 0x04,0x00,           /* Length 132, MS_OS_20_FEATURE_REG_PROPERTY */
    0x07,0x00,                      /* REG_MULTI_SZ */
    0x2A,0x00,                      /* Length of the name: 42 */
    'D',0,'e',0,'v',0,'i',0,'c',0,'e',0,'I',0,'n',0,'t',0,'e',0,'r',0,'f',0,'a',0,'c',0,'e',0,
    'G',0,'U',0,'I',0,'D',0,'s',0,0,0,
    0x50,0x00,                      /* Length of the data: 80 */
    '{',0,'4',0,'D',0,'7',0,'E',0,'3',0,'A',0,'1',0,'0',0,'-',0,'0',0,'F',0,'6',0,'B',0,'-',0,'4',0,'C',0,'8',0,'5',0,'-',0,
    '9',0,'A',0,'2',0,'E',0,'-',0,'6',0,'B',0,'1',0,'D',0,'5',0,'F',0,'0',0,'C',0,'8',0,'E',0,'3',0,'1',0,'}',0,0,0,
    0,0
};
/* Place this buffer as the last buffer so that no other variable / code shares
 * the same cache line. Do not add any other variables / arrays in this file.
 * This will lead to variables sharing the same cache line. */
const uint8_t CyFxUsbDscrAlignBuffer[32] __attribute__((aligned(32)));

/* [ ] */

