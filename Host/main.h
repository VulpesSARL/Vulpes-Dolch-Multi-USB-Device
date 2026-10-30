/* This file contains the constants used by the bulk source sink application example */

#ifndef _INCLUDED_CYFXBULKSRCSINK_H_
#define _INCLUDED_CYFXBULKSRCSINK_H_

#include "cyu3types.h"
#include "cyu3usbconst.h"
#include "cyu3externcstart.h"

/* Endpoint and socket definitions for the bulk source sink application */

/* To change the producer and consumer EP enter the appropriate EP numbers for the #defines.
 * In the case of IN endpoints enter EP number along with the direction bit.
 * For eg. EP 6 IN endpoint is 0x86
 *     and EP 6 OUT endpoint is 0x06.
 * To change sockets mention the appropriate socket number in the #defines. */

/* Note: For USB 2.0 the endpoints and corresponding sockets are one-to-one mapped
         i.e. EP 1 is mapped to UIB socket 1 and EP 2 to socket 2 so on */

#define CY_FX_EP_PRODUCER1               0x01    /* EP 1 OUT */
#define CY_FX_EP_CONSUMER1               0x81    /* EP 1 IN */

#define CY_FX_EP_PRODUCER2               0x02    /* EP 2 OUT */
#define CY_FX_EP_CONSUMER2               0x82    /* EP 2 IN */

#define CY_FX_EP_PRODUCER3               0x03    /* EP 3 OUT */
#define CY_FX_EP_CONSUMER3               0x83    /* EP 3 IN */

#define CY_FX_EP_SERIAL_OUT              0x05    /* EP 5 OUT - CDC-ACM serial port data from the PC */
#define CY_FX_EP_SERIAL_IN               0x85    /* EP 5 IN - CDC-ACM serial port data to the PC */
#define CY_FX_EP_SERIAL_NOTIFY           0x86    /* EP 6 IN - CDC-ACM notifications (interrupt) */
#define CY_FX_USB_SERIAL_COMM_INTF       3
#define CY_FX_EP_PRODUCER4               0x04    /* EP 4 OUT */
#define CY_FX_EP_CONSUMER4               0x84    /* EP 4 IN */

#define CY_FX_EP_PRODUCER_SOCKET        CY_U3P_UIB_SOCKET_PROD_1    /* Socket 1 is producer */
#define CY_FX_EP_CONSUMER_SOCKET        CY_U3P_UIB_SOCKET_CONS_1    /* Socket 1 is consumer */

/* GPIO used to drive Reset on the downstream GPIF Client board. This must only ever pull low to assert reset and go Hi-Z otherwise - the Client
 * board already has a pull-up on this net from its own physical reset-button circuit, so driving
 * it high would fight the pull-up / short against the button. */
#define CY_FX_CLIENT_RESET_GPIO          (22)

/* GPIF thread/socket assignment, per the master_read_write_sync.cydsn state machine model
 * (gpif2model.xml): Thread0/IN_DATA is the Client->Host read path, Thread1/DR_DATA is the
 * Host->Client write path. */
#define CY_FX_GPIF_READ_SOCKET           CY_U3P_PIB_SOCKET_0
#define CY_FX_GPIF_WRITE_SOCKET          CY_U3P_PIB_SOCKET_1

/* The GPIF lane (GPIF\GpifLinkCore.h): one message per 16 KB buffer, 3 buffers per direction. */
#ifdef GPIF_LINK_BUFFER_BYTES
#define CY_FX_GPIF_DMA_BUF_SIZE          (GPIF_LINK_BUFFER_BYTES)
#else
#define CY_FX_GPIF_DMA_BUF_SIZE          (16384)
#endif
#ifndef CY_FX_GPIF_DMA_BUF_COUNT
#define CY_FX_GPIF_DMA_BUF_COUNT         (3)
#endif

/* 1: the Host accepts the debug commands of the control channel (LINK_CONTROL_DEBUG_RESET_HOST, ...). Development builds keep this on;
 * a production build passes USER_CFLAGS="-DCY_FX_DEBUG_COMMANDS=0". */
#ifndef CY_FX_DEBUG_COMMANDS
#define CY_FX_DEBUG_COMMANDS             (1)
#endif

/* 1: the Host pulses the Client's reset line when it starts (the Client then boots its application from its own EEPROM). 0 leaves a Client alone that is already running. */
#ifndef CY_FX_CLIENT_RESET_AT_START
#define CY_FX_CLIENT_RESET_AT_START      (1)
#endif

/* 6 = the GPIF block link (GpifLinkCore.c) with fault injection, against the Client running CLIENT_TEST_MODE=6.
 * 5 = one-direction streaming speed in both directions (Client runs CLIENT_TEST_MODE=5).
 * 4 = long echo run with the permanent DLL setting (error rate, speed), then short buffers of several sizes.
 * 3 = like 2, but the Client echoes what the Host sends, so every DLL setting is judged on both directions at once.
 * 2 = sweep the PIB DLL phase settings while the Client streams the test pattern (GPIFTEST.c, RunGpifDllSweep).
 * 1 = at start-up run the GPIF characterization test (GPIFTEST.c, shared pattern in ..\GPIF\GpifTest.h) before USB starts:
 * it waits for the Client-GpifTest firmware, receives its test buffers, then sends the same ones. Test builds only;
 * pass -DCY_FX_GPIF_TEST=1 on the make command line. */
#ifndef CY_FX_GPIF_TEST
#define CY_FX_GPIF_TEST                  (0)
#endif

/* Whether the Master state machine's "short packet" interrupt wraps up the GPIF read channel (the stock AutoMaster does).
 * 0 on purpose: it fires after every full buffer and injects empty buffers (and PIB errors) between the real ones,
 * and the GPIF lane only ever moves full buffers anyway. */
#ifndef CY_FX_GPIF_READ_WRAPUP
#define CY_FX_GPIF_READ_WRAPUP           (0)
#endif

extern void DestroyGpifReadChannel (void);
extern void ResetGpifWriteChannel (void);
extern void ClientManagerInit (void);
extern CyBool_t glGpifReadCreated;
extern volatile CyBool_t glGpifReady;
extern void RunGpifTest (void);
extern void RunGpifLinkTest (void);
extern void RunGpifStream (void);
extern void RunGpifDllSweep (void);
extern void RunGpifEchoSweep (void);
extern void RunGpifSoak (void);
extern CyBool_t CyFxApplnSetPibDllParameters (CyBool_t isEnable, uint8_t corePhase, uint8_t syncPhase, uint8_t opPhase);
extern void CreateGpifReadChannel (void);
/* 1: the Update Mode USB device connects at high speed only (no SuperSpeed link training). */
#ifndef CY_FX_UPDATE_MODE_HIGH_SPEED_ONLY
#define CY_FX_UPDATE_MODE_HIGH_SPEED_ONLY    (1)
#endif

extern void CyFxHostRequestUsbMode (CyBool_t UpdateMode);      /* update mode: one single USB device, or back to the normal ones */
extern void ClientResetAssert (void);
extern void ClientResetRelease (void);

#define CY_FX_BULKSRCSINK_DMA_TX_SIZE        (0)        /* DMA transfer size is set to infinite */
#define CY_FX_BULKSRCSINK_THREAD_STACK       (0x1000)   /* Bulk loop application thread stack size */
#define CY_FX_BULKSRCSINK_THREAD_PRIORITY    (8)        /* Bulk loop application thread priority */
/* The PC <-> Host link (interface 0, vendor specific class, bound to WinUSB through the Microsoft OS 2.0 descriptors): one message block per bulk transfer in each direction, ended by a short packet. */
#define CY_FX_LINK_EP_BURST                   (4)             /* Packets per burst on the super speed link endpoints. */
#define CY_FX_LINK_DMA_BUF_SIZE               (17408)         /* Holds the largest block (16384 bytes + 4 padding); a multiple of 1 KB (burst 4 x 1 KB) and of every packet size. */
#define CY_FX_LINK_DMA_BUF_COUNT              (2)             /* Buffers per direction (3 before the bulk data lane took 32 KB of the DMA area). */
#define CY_FX_MSOS20_VENDOR_CODE              (0x21)          /* bRequest that fetches the Microsoft OS 2.0 descriptor set (named in the BOS platform capability). */
#define CY_FX_MSOS20_DESCRIPTOR_INDEX         (0x07)          /* wIndex of that request. */
#define CY_FX_MSOS20_SET_LENGTH               (178)           /* Bytes in CyFxMsOs20Dscr. */

/* Burst length in 1 KB packets for the bulk (MIDI) endpoints in the super speed descriptors. */
#define CY_FX_EP_BURST_LENGTH                   (1)
/* Extern definitions for the USB Descriptors */
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
extern const uint8_t CyFxMsOs20Dscr[];
extern const uint8_t CyFxUSBInterfaceDesc1[];
extern const uint8_t CyFxUSBInterfaceDesc2[];
extern const uint8_t CyFxUSBInterfaceDesc4[];
extern const uint8_t CyFxUsbSN[];

#include <cyu3externcend.h>

#endif /* _INCLUDED_CYFXBULKSRCSINK_H_ */

/*[]*/
