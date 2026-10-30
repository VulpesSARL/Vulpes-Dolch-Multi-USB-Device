#ifndef _INCLUDED_HOST_PERIPH_H_
#define _INCLUDED_HOST_PERIPH_H_

#include "cyu3types.h"

/* The Host's MIDI interface and serial port (PERIPH.c): both are bridged to the Client through the GPIF lane without any PC application. */

extern void HostPeriphInit (void);                                              /* once at start-up: rings and the worker thread */
extern CyBool_t HostPeriphUsbStart (void);                                      /* the PC configured the device: endpoints and channels */
extern void HostPeriphUsbStop (void);                                           /* reset, disconnect or update mode: channels away */
extern void HostPeriphOnClientMessage (const uint8_t *Block, uint32_t Length);  /* channels 4 and 5 from the Client (lane thread) */
extern void HostPeriphClientState (CyBool_t Up);                                /* the Client announced itself / was lost */
extern CyBool_t HostPeriphCdcSetup (uint8_t Request, uint16_t Value, uint16_t Length);

#endif
