#include "cyu3system.h"
#include "cyu3os.h"
#include "cyu3dma.h"
#include "cyu3error.h"
#include "main.h"
#include "LINK.h"
#include "GpifLinkCore.h"
#include "PERIPH.h"

/* The Host's side of the Client: resets the Client board (which boots its application from its own EEPROM), runs the GPIF lane,
 * keeps the Client alive-checked with a PING every second, tells it whether the PC is connected (PC_LINK), tells the PC what state the
 * Client is in (STATUS) and routes every non-control message between the two links. The control channel of the GPIF lane is
 * private to Host and Client; everything on the other channels is passed through untouched. */

#define CLIENT_MANAGER_STACK         (0x1800)
#define CLIENT_MANAGER_PRIORITY      (8)
#define CLIENT_LINK_STACK            (0x1000)
#define CLIENT_PING_INTERVAL_MS      (1000)
#define CLIENT_LOST_MS               (3500)
#define CLIENT_POLL_MS               (50)
#define CLIENT_START_MS              (20000)    /* After a reset, the application has this long to announce itself. */
#define CLIENT_RETRY_MS              (5000)     /* Wait before trying a recovery again that could not even start. */
#define CLIENT_RECEIVE_BUFFERS       (4)

extern CyU3PDmaChannel glChHandleGpifWrite;
extern CyU3PDmaChannel glChHandleGpifRead;

static CyU3PThread ManagerThread;
static CyU3PThread GpifLinkThread;
static volatile CyBool_t ClientUp = CyFalse;
static volatile uint32_t LastClientTime = 0;
static volatile CyBool_t ResetRequestedByPc = CyFalse;    /* Set by HostRequestClientReset, taken by the manager thread. */
static volatile CyBool_t ClientUsbUp = CyFalse;           /* The Client says its USB side is connected to the PC. */
static volatile CyBool_t Recovering = CyFalse;            /* The manager thread is inside RecoverClient. */
static uint8_t ClientStatusBody[28];

static uint32_t ReadWord (const uint8_t *Data)
{
    return ((uint32_t)Data[0] | ((uint32_t)Data[1] << 8) | ((uint32_t)Data[2] << 16) | ((uint32_t)Data[3] << 24));
}

/* Sends a short control message to the Client without waiting. */
static CyBool_t SendControl (uint8_t Type, const uint8_t *Payload, uint32_t Length)
{
    uint32_t Block[16];
    uint32_t Total;

    Total = GpifLinkBuildMessage ((uint8_t *)Block, LINK_CHANNEL_CONTROL, Type, 0, Payload, Length);
    return (GpifLinkSend ((uint8_t *)Block, Total, 0));
}

static void SetClientState (uint8_t State)
{
    ClientStatusBody[0] = State;
    PcLinkSetClientStatus (ClientStatusBody, 0);
}

/* A verified message block from the Client (called in the GPIF link thread, must not block for long). */
static void OnClientMessage (const uint8_t *Block, uint32_t Length)
{
    uint32_t PayloadLength = ReadWord (Block + 4);
    const uint8_t *Payload = Block + LINK_MESSAGE_HEADER_SIZE;
    uint32_t Version;

    /* While the Client is not up (booting, reset) the bus is undriven and delivers junk: only its announcement is believed. */
    if ((ClientUp == CyFalse) && !((Block[0] == LINK_CHANNEL_CONTROL) && (Block[1] == LINK_CONTROL_CLIENT_STATUS)))
        return;

    LastClientTime = CyU3PGetTime ();

    if (Block[0] == LINK_CHANNEL_CONTROL)
    {
        if (Block[1] == LINK_CONTROL_CLIENT_RESET_REQUEST)
        {
            HostRequestClientReset ();
            return;
        }

        if ((Block[1] == LINK_CONTROL_CLIENT_STATUS) && (PayloadLength >= 4 + sizeof (ClientStatusBody)))
        {
            Version = ReadWord (Payload);
            CyU3PMemCopy (ClientStatusBody, (uint8_t *)(Payload + 4), sizeof (ClientStatusBody));
            ClientStatusBody[0] = LINK_CLIENT_RUNNING;
            ClientUp = CyTrue;
            HostPeriphClientState (CyTrue);
            ClientUsbUp = (ClientStatusBody[6] != 0) ? CyTrue : CyFalse;      /* byte 10 of the Client's status */
            PcLinkSetClientStatus (ClientStatusBody, Version);
            CyU3PDebugPrint (4, "Client: application is up (firmware 0x%x)\r\n", Version);
        }
        return;
    }

    /* MIDI and the serial port are the Host's own business (no PC application involved). */
    if ((Block[0] == 4) || (Block[0] == 5))
    {
        HostPeriphOnClientMessage (Block, Length);
        return;
    }

    PcLinkForwardToPc (Block, Length);
}

uint8_t HostRequestClientReset (void)
{
    if ((Recovering == CyTrue) || (ResetRequestedByPc == CyTrue))
        return (LINK_ERROR_BUSY);

    CyU3PDebugPrint (4, "Client: the PC asked for a Client reset\r\n");
    ResetRequestedByPc = CyTrue;
    return (0);
}

#ifdef INPUT_DIAG
/* Input path diagnostics: every block forwarded to the Client, how long the forward took, how many were slow or failed. */
static volatile uint32_t DiagForwards = 0;
static volatile uint32_t DiagSlow = 0;
static volatile uint32_t DiagFailed = 0;
static volatile uint32_t DiagMaxForward = 0;
extern volatile uint32_t PcLinkDiagMaxGap;
extern volatile uint32_t PcLinkDiagMaxHandle;
extern volatile uint32_t PcLinkDiagBlocks;
#endif

uint8_t HostForwardToClient (const uint8_t *Block, uint32_t Length)
{
#ifdef INPUT_DIAG
    uint32_t DiagStart = CyU3PGetTime ();
    uint32_t DiagTook;
#endif

    if (ClientUp == CyFalse)
        return (LINK_ERROR_CLIENT_ABSENT);

    if (GpifLinkSend (Block, Length, 2000) == CyFalse)
    {
#ifdef INPUT_DIAG
        DiagFailed++;
#endif
        CyU3PDebugPrint (4, "Client: could not forward a %d byte block to the Client (GPIF window full for 2 s)\r\n", Length);
        return (LINK_ERROR_BUSY);
    }

#ifdef INPUT_DIAG
    DiagTook = CyU3PGetTime () - DiagStart;
    DiagForwards++;
    if (DiagTook > 20)
        DiagSlow++;
    if (DiagTook > DiagMaxForward)
        DiagMaxForward = DiagTook;
#endif
    return (0);
}

/* Resets the Client board; it boots its application from its own EEPROM.
 * The Client reads its application out of its EEPROM for a couple of seconds after every reset. A reset in the middle of that read leaves the
 * EEPROM mid-transfer, and the Client's next boot then fails (it falls into the USB bootloader) until the EEPROM has lost its power. So the
 * reset line is never pulsed again before this time has passed since the last pulse - and the first pulse after the Host's own start waits too,
 * because at power-up the Client starts reading its EEPROM at the same moment the Host does. */
#define CLIENT_BOOT_WINDOW_MS   (8000)

static uint32_t LastResetTime = 0;
static CyBool_t ResetDone = CyFalse;

static void ResetClientBoard (void)
{
    uint32_t Waited;

    if (ResetDone == CyTrue)
        Waited = CyU3PGetTime () - LastResetTime;
    else
        Waited = CyU3PGetTime ();       /* time since the Host started */

    if (Waited < CLIENT_BOOT_WINDOW_MS)
    {
        CyU3PDebugPrint (4, "Client: reset postponed %d ms (the Client may still be reading its EEPROM)\r\n", CLIENT_BOOT_WINDOW_MS - Waited);
        CyU3PThreadSleep (CLIENT_BOOT_WINDOW_MS - Waited);
    }

    CyU3PDebugPrint (4, "Client: resetting the Client board\r\n");
    ClientResetAssert ();
    CyU3PThreadSleep (10);
    ClientResetRelease ();
    LastResetTime = CyU3PGetTime ();
    ResetDone = CyTrue;
}

/* The Client is gone (nothing heard for CLIENT_LOST_MS) or never announced itself: stop the GPIF lane, throw away everything it
 * still holds and reset the Client board. Runs in the manager thread. Returns CyFalse if the link thread could not be stopped
 * (nothing was changed then). */
static CyBool_t RecoverClient (void)
{
    CyU3PDebugPrint (4, "Client: recovering - pausing the GPIF link, then reset\r\n");
    Recovering = CyTrue;
    SetClientState (LINK_CLIENT_BOOTING);

    if (GpifLinkPause (1000) == CyFalse)
    {
        CyU3PDebugPrint (4, "Client: the GPIF link thread did not stop, recovery postponed\r\n");
        Recovering = CyFalse;
        return (CyFalse);
    }

    DestroyGpifReadChannel ();
    ResetGpifWriteChannel ();
    GpifLinkReset ();

    ResetClientBoard ();
    CreateGpifReadChannel ();

    GpifLinkResume ();
    LastClientTime = CyU3PGetTime ();
    Recovering = CyFalse;
    return (CyTrue);
}

static void ManagerEntry (uint32_t Input)
{
    GpifLinkConfig LinkConfig;
    void *Stack;
    uint32_t Now;
    uint32_t LastPing = 0;
    uint32_t LastUsbRequest = 0;
    uint32_t AwaitStart = 0;
    uint32_t LastStatsTime = 0;
    uint32_t LastSkips = 0;
#ifdef INPUT_DIAG
    uint32_t DiagPrinted = 0;
#endif
    uint32_t RecoverAt = 0;
    CyBool_t AwaitingClient = CyFalse;
    CyBool_t RecoveryWanted = CyFalse;
    uint8_t PcLinkSent = 0xFF;
    uint8_t PcLinkWanted;

    CyU3PMemSet (ClientStatusBody, 0, sizeof (ClientStatusBody));

    /* This thread starts before the application thread has set up the PIB, the GPIF state machine and the write channel. */
    while (glGpifReady == CyFalse)
        CyU3PThreadSleep (10);

    SetClientState (LINK_CLIENT_BOOTING);
#if (CY_FX_CLIENT_RESET_AT_START == 1)
    ResetClientBoard ();
#endif
    AwaitingClient = CyTrue;

    if (glGpifReadCreated == CyFalse)
        CreateGpifReadChannel ();

    LinkConfig.ReadChannel = &glChHandleGpifRead;
    LinkConfig.WriteChannel = &glChHandleGpifWrite;
    LinkConfig.OnMessage = OnClientMessage;
    if (GpifLinkInit (&LinkConfig) == CyFalse)
    {
        CyU3PDebugPrint (4, "Client: GpifLinkInit failed\r\n");
        for (;;)
            CyU3PThreadSleep (1000);
    }

    Stack = CyU3PMemAlloc (CLIENT_LINK_STACK);
    CyU3PThreadCreate (&GpifLinkThread, "24:Gpif_link", GpifLinkThreadEntry, 0, Stack, CLIENT_LINK_STACK, 11, 11,
            CYU3P_NO_TIME_SLICE, CYU3P_AUTO_START);       /* below the guard thread (10): it runs flat out under bulk traffic */

    if (ClientStatusBody[0] == LINK_CLIENT_BOOTING)
        SetClientState (LINK_CLIENT_ABSENT);
    CyU3PDebugPrint (4, "Client: GPIF link running, waiting for the Client application\r\n");
    AwaitStart = CyU3PGetTime ();

    for (;;)
    {
        CyU3PThreadSleep (CLIENT_POLL_MS);
        Now = CyU3PGetTime ();

#ifdef INPUT_DIAG
        /* Printed every 2 s while blocks are being forwarded; the windows start over after each line. */
        if (((Now - DiagPrinted) >= 2000) && (DiagForwards != 0))
        {
            DiagPrinted = Now;
            CyU3PDebugPrint (4, "DIAG fwd %d slow %d fail %d max %d ms | pclink blocks %d maxhandle %d ms maxgap %d ms\r\n", DiagForwards, DiagSlow, DiagFailed,
                    DiagMaxForward, PcLinkDiagBlocks, PcLinkDiagMaxHandle, PcLinkDiagMaxGap);
            DiagForwards = 0;
            DiagSlow = 0;
            DiagFailed = 0;
            DiagMaxForward = 0;
            PcLinkDiagBlocks = 0;
            PcLinkDiagMaxHandle = 0;
            PcLinkDiagMaxGap = 0;
        }
#endif

        /* The lane's counters, printed every 10 s while buffers without a plausible message header were seen (a floating bus, damage). */
        if ((Now - LastStatsTime) >= 10000)
        {
            const GpifLinkStats *Stats = GpifLinkGetStats ();

            LastStatsTime = Now;
            if (Stats->HeaderSkips != LastSkips)
            {
                LastSkips = Stats->HeaderSkips;
                CyU3PDebugPrint (4, "Lane: %d buffers received, %d delivered, %d invalid so far\r\n", Stats->BuffersReceived, Stats->BlocksDelivered, Stats->HeaderSkips);
            }
        }

        /* The PC wants the Client reset: handled exactly like a Client that was lost. */
        if (ResetRequestedByPc == CyTrue)
        {
            ResetRequestedByPc = CyFalse;
            ClientUp = CyFalse;
            HostPeriphClientState (CyFalse);
            AwaitingClient = CyFalse;
            SetClientState (LINK_CLIENT_LOST);
            PcLinkSent = 0xFF;
            RecoveryWanted = CyTrue;
            RecoverAt = Now;
        }

        if (ClientUp == CyTrue)
        {
            AwaitingClient = CyFalse;

            if ((Now - LastClientTime) > CLIENT_LOST_MS)
            {
                CyU3PDebugPrint (4, "Client: nothing from the Client for %d ms, marked lost\r\n", CLIENT_LOST_MS);
                ClientUp = CyFalse;
                HostPeriphClientState (CyFalse);
                SetClientState (LINK_CLIENT_LOST);
                PcLinkSent = 0xFF;
                RecoveryWanted = CyTrue;
                RecoverAt = Now;
                continue;
            }

            if ((Now - LastPing) >= CLIENT_PING_INTERVAL_MS)
            {
                LastPing = Now;
                SendControl (LINK_CONTROL_PING, (const uint8_t *)&Now, 4);
            }

            /* The Client keeps its USB side off until told: the Host alive and the Client up is all it takes, no PC application needed. */
            if ((ClientUsbUp == CyFalse) && ((Now - LastUsbRequest) >= 1000))
            {
                uint8_t Go = 1;

                LastUsbRequest = Now;
                SendControl (LINK_CONTROL_CLIENT_USB, &Go, 1);
            }

            /* Keep the Client informed about whether a PC is attached. */
            PcLinkWanted = (PcLinkIsSessionOpen () == CyTrue) ? 1 : 0;
            if (PcLinkWanted != PcLinkSent)
            {
                if (SendControl (LINK_CONTROL_PC_LINK, &PcLinkWanted, 1) == CyTrue)
                    PcLinkSent = PcLinkWanted;
            }
        }
        else
        {
            /* Reset, but the application never announced itself: try again. */
            if ((AwaitingClient == CyTrue) && ((Now - AwaitStart) > CLIENT_START_MS))
            {
                CyU3PDebugPrint (4, "Client: the application did not announce itself within %d ms\r\n", CLIENT_START_MS);
                AwaitingClient = CyFalse;
                SetClientState (LINK_CLIENT_LOST);
                RecoveryWanted = CyTrue;
                RecoverAt = Now;
            }

            if ((RecoveryWanted == CyTrue) && ((int32_t)(Now - RecoverAt) >= 0))
            {
                if (RecoverClient () == CyTrue)
                {
                    RecoveryWanted = CyFalse;
                    AwaitingClient = CyTrue;
                    AwaitStart = CyU3PGetTime ();
                    PcLinkSent = 0xFF;
                    LastPing = 0;
                    CyU3PDebugPrint (4, "Client: reset again, waiting for the Client application\r\n");
                }
                else
                {
                    RecoverAt = Now + CLIENT_RETRY_MS;
                }
            }
        }
    }
}
void ClientManagerInit (void)
{
    void *Stack = CyU3PMemAlloc (CLIENT_MANAGER_STACK);

    CyU3PThreadCreate (&ManagerThread, "23:Client_manager", ManagerEntry, 0, Stack, CLIENT_MANAGER_STACK,
            CLIENT_MANAGER_PRIORITY, CLIENT_MANAGER_PRIORITY, CYU3P_NO_TIME_SLICE, CYU3P_AUTO_START);
}