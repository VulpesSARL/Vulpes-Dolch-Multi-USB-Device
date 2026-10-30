#ifndef HOSTUPD_H
#define HOSTUPD_H

#include "cyu3types.h"
#include "UpdateProtocol.h"

/* Firmware update of the Host itself (the Host's boot EEPROM). The PC talks to it on channel UPDATE_HOST_CHANNEL, over the normal PC link: it
 * sends the update file twice (pass 1 only checks it, pass 2 programs while it streams in, see UpdateCore.h). */

/* A verified message block for the update channel (called in the PC link thread; it may take a while, the EEPROM is slow). */
extern void HostUpdateOnMessage (const uint8_t *Block, uint32_t Length);

#define HOST_UPDATE_IDLE_MS     (600000)    /* Update mode without any message for this long: back to normal. */

/* Milliseconds since the last update message. */
extern uint32_t HostUpdateIdleMs (void);

/* Forget everything: back to the normal state (the USB side is switched back by the caller). */
extern void HostUpdateReset (void);

/* The Host is not in update mode: answer a Client update message with a refusal instead of passing it on. */
extern void HostUpdateRefuseClient (void);

/* The Host is in update mode. */
extern CyBool_t HostUpdateIsActive (void);

#endif
