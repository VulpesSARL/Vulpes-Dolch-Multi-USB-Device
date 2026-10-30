#include "cyu3system.h"
#include "cyu3os.h"
#include "cyu3usb.h"
#include "cyu3error.h"
#include "GpifLinkCore.h"
#include "GuardCore.h"

static volatile uint32_t ConnectTime = 0;
static volatile CyBool_t WasConfigured = CyFalse;
static CyU3PThread GuardThread;
static volatile CyBool_t WatchdogResetRequested = CyFalse;

void GuardRequestWatchdogReset (void)
{
    WatchdogResetRequested = CyTrue;
    CyU3PConnectState (CyFalse, CyTrue);
}

void GuardFatal (CyU3PReturnStatus_t Status)
{
    CyU3PDebugPrint (4, "Fatal error %d, resetting\r\n", Status);
    CyU3PConnectState (CyFalse, CyTrue);
#if (GUARD_FATAL_WAIT_FOR_RESET == 0)
    CyU3PDeviceReset (CyFalse);
#endif
    /* A thread that spins starves the guard thread, so the watchdog resets the chip within seconds (and the Host's supervision pulses the Client's reset line sooner). */
    for (;;)
        ;
}

void GuardUsbConnected (void)
{
    ConnectTime = CyU3PGetTime ();
    if (ConnectTime == 0)
        ConnectTime = 1;
}

void GuardThreadEntry (uint32_t Input)
{
    uint32_t LastBeat = 0;
    uint32_t StaleTicks = 0;
    uint32_t Beat;
    uint32_t Tries = 0;
    CyBool_t Configured;

    CyU3PSysWatchDogConfigure (CyTrue, GUARD_WATCHDOG_MS);

    for (;;)
    {
        CyU3PThreadSleep (GUARD_TICK_MS);

        /* The link thread has to keep running once it ever ran; a thread stuck in a loop or a wait for good stops the watchdog clear. */
        Beat = GpifLinkHeartbeat ();
        if (Beat != 0)
        {
            if (Beat == LastBeat)
                StaleTicks++;
            else
                StaleTicks = 0;
            LastBeat = Beat;
        }

        if ((StaleTicks < GUARD_LINK_STALE_TICKS) && (WatchdogResetRequested == CyFalse))
            CyU3PSysWatchDogClear ();

        /* Connected for a while without the PC configuring us: connect again, a few times, so the PC's controller sees a new attach. */
        Configured = GuardIsConfigured ();
        if (Configured == CyTrue)
        {
            Tries = 0;
            WasConfigured = CyTrue;
            continue;
        }

        if ((WasConfigured == CyTrue) && (ConnectTime != 0))
        {
            WasConfigured = CyFalse;
            ConnectTime = CyU3PGetTime ();
        }

        if ((ConnectTime == 0) || (Tries >= GUARD_ATTACH_TRIES))
            continue;

        if ((CyU3PGetTime () - ConnectTime) > GUARD_ATTACH_MS)
        {
            Tries++;
            CyU3PDebugPrint (4, "Not configured by the PC, connecting again (%d)\r\n", Tries);
            CyU3PConnectState (CyFalse, CyTrue);
            CyU3PThreadSleep (1000);

            /* Always SuperSpeed again: a high speed only fallback would leave the device on USB 2 after a lost SuperSpeed attach, and only a
             * power cycle would bring SuperSpeed back. */
            CyU3PConnectState (CyTrue, CyTrue);
            ConnectTime = CyU3PGetTime ();
        }
    }
}

void GuardStart (void)
{
    void *Stack = CyU3PMemAlloc (GUARD_THREAD_STACK);

    if (Stack == NULL)
        return;

    CyU3PThreadCreate (&GuardThread, "27:Guard", GuardThreadEntry, 0, Stack, GUARD_THREAD_STACK, GUARD_THREAD_PRIORITY, GUARD_THREAD_PRIORITY,
            CYU3P_NO_TIME_SLICE, CYU3P_AUTO_START);
}
