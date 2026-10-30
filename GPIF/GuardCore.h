#ifndef GUARD_CORE_H
#define GUARD_CORE_H

#include "cyu3types.h"
#include "cyu3error.h"

/* Keeps a hung or confused firmware from sitting on the USB port: a fatal error disconnects USB and resets the device, a hardware watchdog
 * resets it when the scheduler or the GPIF link thread stops, and a device that was connected but never got configured by the PC is
 * disconnected and connected again a few times. Shared by Host and Client (RTOS API). The project provides GuardIsConfigured(). */

#define GUARD_WATCHDOG_MS       (8000)      /* Hardware reset when the guard thread does not clear the watchdog this often. */
#define GUARD_TICK_MS           (500)
#define GUARD_LINK_STALE_TICKS  (12)        /* The link thread made no pass for this many ticks (6 s): stop clearing the watchdog. */
#define GUARD_ATTACH_MS         (12000)     /* Connected, but no configuration from the PC for this long: connect again. */
#define GUARD_ATTACH_TRIES      (4)
#ifndef GUARD_FATAL_WAIT_FOR_RESET
#define GUARD_FATAL_WAIT_FOR_RESET (0)      /* 1: after a fatal error do not call CyU3PDeviceReset (the Client: a self-reset leaves it in the USB bootloader), just wait to be reset. */
#endif
#define GUARD_THREAD_STACK      (0x400)
#define GUARD_THREAD_PRIORITY   (10)        /* Below every working thread, so a thread that spins forever starves it and the watchdog fires. */

/* The PC configured the device (SET_CONFIGURATION seen and the application running); implemented by the project. */
extern CyBool_t GuardIsConfigured (void);

/* Fatal error: disconnect from USB and reset the whole device (does not return). */
extern void GuardFatal (CyU3PReturnStatus_t Status);

/* Call right after every successful CyU3PConnectState (CyTrue, ...) of the start-up. */
extern void GuardUsbConnected (void);

/* Creates and starts the guard thread (call from the application define function). */
extern void GuardStart (void);

/* Stops clearing the watchdog and disconnects USB: the device is reset by the hardware watchdog within GUARD_WATCHDOG_MS. */
extern void GuardRequestWatchdogReset (void);

extern void GuardThreadEntry (uint32_t Input);

#endif
