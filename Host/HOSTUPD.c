#include "cyu3system.h"
#include "cyu3os.h"
#include "cyu3error.h"
#include "cyu3usb.h"
#include "GpifLinkCore.h"
#include "UpdateCore.h"
#include "GuardCore.h"
#include "LINK.h"
#include "HOSTUPD.h"
#include "main.h"

static uint32_t State = UPDATE_STATE_NORMAL;
static uint32_t Pass = 0;
static uint32_t FileLength = 0;
static uint32_t Received = 0;
static uint32_t LastPercent = 0;
static uint32_t LastActivity = 0;

static uint32_t ReadWord (const uint8_t *Data)
{
    return ((uint32_t)Data[0] | ((uint32_t)Data[1] << 8) | ((uint32_t)Data[2] << 16) | ((uint32_t)Data[3] << 24));
}

static void WriteWord (uint8_t *Data, uint32_t Value)
{
    Data[0] = (uint8_t)(Value & 0xFF);
    Data[1] = (uint8_t)((Value >> 8) & 0xFF);
    Data[2] = (uint8_t)((Value >> 16) & 0xFF);
    Data[3] = (uint8_t)(Value >> 24);
}

static void SendState (uint32_t NewState, uint32_t Detail, uint32_t Extra)
{
    uint8_t Payload[12];

    State = NewState;
    WriteWord (Payload, NewState);
    WriteWord (Payload + 4, Detail);
    WriteWord (Payload + 8, Extra);
    PcLinkReply (UPDATE_HOST_CHANNEL, UPDATE_MSG_STATE, 0, Payload, sizeof (Payload));
}

static void Fail (uint32_t Code)
{
    CyU3PDebugPrint (4, "Host update failed, code %d\r\n", Code);
    UpdateStreamAbort ();
    SendState (UPDATE_STATE_FAILED, Code, 0);
}

uint32_t HostUpdateIdleMs (void)
{
    return (CyU3PGetTime () - LastActivity);
}

void HostUpdateReset (void)
{
    UpdateWriteEnable (CyFalse);
    UpdateStreamAbort ();
    State = UPDATE_STATE_NORMAL;

    /* The Client was put into update mode with the Host: it comes back by a reset pulse (USB off until the Host says go, like after any boot). */
    HostRequestClientReset ();
}

/* The Host is not in update mode: the Client's update messages are not passed on. */
void HostUpdateRefuseClient (void)
{
    uint8_t Payload[12];

    WriteWord (Payload, UPDATE_STATE_NORMAL);
    WriteWord (Payload + 4, UPDATE_ERR_STATE);
    WriteWord (Payload + 8, 0);
    PcLinkReply (UPDATE_CHANNEL, UPDATE_MSG_STATE, 0, Payload, sizeof (Payload));
}

/* Puts the Client into update mode as well. */
static void EnterClientUpdateMode (void)
{
    uint32_t Block[8];
    uint32_t Total = GpifLinkBuildMessage ((uint8_t *)Block, UPDATE_CHANNEL, UPDATE_MSG_ENTER, 0, NULL, 0);

    HostForwardToClient ((uint8_t *)Block, Total);
}

CyBool_t HostUpdateIsActive (void)
{
    return ((State != UPDATE_STATE_NORMAL) ? CyTrue : CyFalse);
}

static void HandleBegin (const uint8_t *Payload, uint32_t Length)
{
    uint32_t Result;
    uint32_t NewPass = (Length >= 8) ? ReadWord (Payload + 4) : 0;

    if ((State == UPDATE_STATE_NORMAL) || (State == UPDATE_STATE_DONE) || (Length < 8) ||
        ((NewPass == UPDATE_STREAM_PROGRAM) && (State != UPDATE_STATE_VERIFIED)))
    {
        SendState (State, UPDATE_ERR_STATE, 0);
        return;
    }

    FileLength = ReadWord (Payload);
    Result = UpdateStreamBegin (FileLength, NewPass);
    if (Result != UPDATE_OK)
    {
        Fail (Result);
        return;
    }

    Pass = NewPass;
    Received = 0;
    LastPercent = 0;
    SendState ((Pass == UPDATE_STREAM_PROGRAM) ? UPDATE_STATE_PROGRAMMING : UPDATE_STATE_RECEIVING, 0, 0);
}

static void HandleData (const uint8_t *Payload, uint32_t Length)
{
    uint32_t Result;
    uint32_t Percent;

    if (((State != UPDATE_STATE_RECEIVING) && (State != UPDATE_STATE_PROGRAMMING)) || (Length < 4))
    {
        SendState (State, UPDATE_ERR_STATE, 0);
        return;
    }

    if (ReadWord (Payload) != Received)
    {
        Fail (UPDATE_ERR_OFFSET);
        return;
    }

    Result = UpdateStreamFeed (Payload + 4, Length - 4);
    if (Result != UPDATE_OK)
    {
        Fail (Result);
        return;
    }

    Received += Length - 4;

    if (Pass == UPDATE_STREAM_PROGRAM)
    {
        Percent = (Received / 256) * 100 / ((FileLength / 256) + 1);
        if (Percent >= (LastPercent + 10))
        {
            LastPercent = Percent;
            SendState (UPDATE_STATE_PROGRAMMING, Percent, 0);
        }
    }
}

static void HandleEnd (void)
{
    uint32_t Result;
    uint32_t WasPass = Pass;

    if ((State != UPDATE_STATE_RECEIVING) && (State != UPDATE_STATE_PROGRAMMING))
    {
        SendState (State, UPDATE_ERR_STATE, 0);
        return;
    }

    Result = UpdateStreamEnd ();
    if (Result != UPDATE_OK)
    {
        Fail (Result);
        return;
    }

    if (WasPass == UPDATE_STREAM_VERIFY)
        SendState (UPDATE_STATE_VERIFIED, 0, 0);
    else
    {
        CyU3PDebugPrint (4, "Host update: EEPROM programmed and verified, %d page writes\r\n", UpdatePagesWritten ());
        SendState (UPDATE_STATE_DONE, 0, 0);
    }
}

void HostUpdateOnMessage (const uint8_t *Block, uint32_t Length)
{
    uint8_t Type = Block[1];
    uint32_t PayloadLength = ReadWord (Block + 4);
    const uint8_t *Payload = Block + 8;

    LastActivity = CyU3PGetTime ();

    switch (Type)
    {
    case UPDATE_MSG_ENTER:
        if (State == UPDATE_STATE_NORMAL)
        {
            /* The self test runs in every build; only its message disappears in a production build. */
            CyBool_t SelfTest = UpdateSelfTest ();
            CyU3PDebugPrint (4, "Host update: entering update mode, signature self test %s\r\n", (SelfTest == CyTrue) ? "passed" : "FAILED");
            (void)SelfTest;
            State = UPDATE_STATE_UPDATE_MODE;
            UpdateWriteEnable (CyTrue);       /* the only place where the Host allows EEPROM writes: update mode */
            SendState (State, 0, 0);
            EnterClientUpdateMode ();          /* both devices go into update mode */
            CyFxHostRequestUsbMode (CyTrue);      /* the PC sees one single "Update Mode" device from now on */
            break;
        }
        SendState (State, 0, 0);
        break;

    case UPDATE_MSG_LEAVE:
        if (State == UPDATE_STATE_PROGRAMMING)
        {
            SendState (State, UPDATE_ERR_STATE, 0);
            break;
        }
        HostUpdateReset ();
        SendState (UPDATE_STATE_NORMAL, 0, 0);
        CyFxHostRequestUsbMode (CyFalse);
        break;

    case UPDATE_MSG_BEGIN:
        HandleBegin (Payload, PayloadLength);
        break;

    case UPDATE_MSG_DATA:
        HandleData (Payload, PayloadLength);
        break;

    case UPDATE_MSG_END:
        HandleEnd ();
        break;

    case UPDATE_MSG_ABORT:
        UpdateStreamAbort ();
        if (State != UPDATE_STATE_NORMAL)
            State = UPDATE_STATE_UPDATE_MODE;
        SendState (State, 0, 0);
        break;

    case UPDATE_MSG_QUERY:
        SendState (State, (State == UPDATE_STATE_RECEIVING) ? Received : 0, 0);
        break;

    case UPDATE_MSG_RESTART:
        if (State == UPDATE_STATE_PROGRAMMING)
        {
            SendState (State, UPDATE_ERR_STATE, 0);
            break;
        }
        SendState (State, 0, 0);
        CyU3PThreadSleep (300);
        CyU3PDebugPrint (4, "Host update: restarting\r\n");
        if ((PayloadLength >= 1) && (Payload[0] == 1))
            GuardRequestWatchdogReset ();
        else
        {
            CyU3PConnectState (CyFalse, CyTrue);
            CyU3PDeviceReset (CyFalse);
        }
        break;

    default:
        SendState (State, UPDATE_ERR_STATE, 0);
        break;
    }
}
