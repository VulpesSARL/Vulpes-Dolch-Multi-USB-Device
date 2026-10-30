/* The dedicated bulk data lane of the Host (second bulk endpoint pair 0x02 / 0x82 and GPIF thread 2 / 3 of the four-thread state machines, see
 * GPIF\Study-4-Threads). Bulk data of the PC moves through an AUTO DMA channel without the CPU: bulk OUT 2 -> PIB socket 2 -> GPIF -> the Client. The
 * PC announces every transfer with the control message BULK_ARM; the message lane (LINK.c, CLIENT.c) stays untouched. */

#ifndef _INCLUDED_BULK_H_
#define _INCLUDED_BULK_H_

#include "cyu3types.h"
#include "cyu3externcstart.h"

/* Configures the endpoints 0x02 / 0x82 for the current USB speed and creates the AUTO channel PC -> Client (called with the link endpoints). */
extern CyBool_t HostBulkStart (void);

/* Destroys the channel and disables the endpoints. */
extern void HostBulkStop (void);

/* Arms the next transfer PC -> Client of Bytes bytes (a multiple of 16384). Returns 0 or a LINK_ERROR_* code. */
extern uint8_t HostBulkArm (uint32_t Bytes);

#include "cyu3externcend.h"

#endif /* _INCLUDED_BULK_H_ */
