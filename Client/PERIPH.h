#ifndef _INCLUDED_CLIENT_PERIPH_H_
#define _INCLUDED_CLIENT_PERIPH_H_

#include "cyu3types.h"
#include "cyu3usb.h"

/* The Client's keyboard, mouse, MIDI and serial port (PERIPH.c). */

extern void ClientPeriphInit (void);                                            /* once, after the GPIF lane runs */
extern void ClientPeriphDmaInit (void);                                         /* after the endpoints are configured (again after every restart of the USB stack) */
extern void ClientPeriphDmaDeInit (void);                                       /* before the USB stack is stopped */
extern void ClientPeriphUsbEvent (CyU3PUsbEventType_t EventType, uint16_t EventData);
extern void ClientPeriphOnMessage (const uint8_t *Block, uint32_t Length);      /* channels 3, 4, 5 (lane thread) */
extern void ClientPeriphAnnounce (void);                                        /* after the announcement to the Host (helper thread) */
extern void ClientPeriphPcLink (uint8_t Up);                                    /* the PC application came or went */
extern void ClientPeriphKeyboardLeds (uint8_t Leds);
extern CyBool_t ClientPeriphCdcSetup (uint8_t Request, uint16_t Value, uint16_t Length);
extern CyBool_t ClientPeriphSetReport (uint16_t Length);
extern void ClientPeriphSetProtocol (uint8_t Protocol);                         /* HID SET_PROTOCOL: 0 boot, 1 report */
extern uint8_t ClientPeriphGetProtocol (void);
extern uint8_t ClientPeriphUsbSpeed (void);                                     /* 0 = the target PC has not configured the device (cable pulled, not enumerated) */

#endif
