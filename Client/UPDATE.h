#ifndef UPDATE_H
#define UPDATE_H

#include "cyu3types.h"

/* Firmware update mode of the Client: the PC (through the Host) puts the Client into update mode, sends a signed update file, and the
 * Client programs its own boot EEPROM. Messages travel on channel UPDATE_CHANNEL; the file format and the programming are in
 * ..\Update\UpdateCore.c. The USB side of the Client is switched off while it is in update mode. */

#include "UpdateProtocol.h"

extern void UpdateInit (void);
extern CyBool_t UpdateIsActive (void);          /* The Client is in update mode (or on its way in). */
extern void ClientLinkRequestReset (void);      /* LINKCLIENT.c */
extern void UpdateOnMessage (const uint8_t *Block, uint32_t Length);

#endif
