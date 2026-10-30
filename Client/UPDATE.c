#include "cyu3system.h"
#include "cyu3os.h"
#include "cyu3dma.h"
#include "cyu3error.h"
#include "cyu3usb.h"
#include "GpifLinkCore.h"
#include "UpdateCore.h"
#include "UPDATE.h"
#include "CDMEDIA.h"
#include "HDDMEDIA.h"

#define UPDATE_THREAD_STACK         (0x800)
#define UPDATE_THREAD_PRIORITY      (9)
#define UPDATE_IDLE_RESET_MS        (600000)    /* Nobody talks to the Client in update mode for this long: reset it, so it boots normally. */

#define COMMAND_NONE                (0)
#define COMMAND_ENTER               (1)
#define COMMAND_COMMIT              (2)
#define COMMAND_RESTART             (3)

extern CyBool_t ClientLinkSend (uint8_t Channel, uint8_t Type, uint16_t Tag, const uint8_t *Payload, uint32_t Length, uint32_t TimeoutMs);

static CyU3PThread UpdateThread;
static CyU3PSemaphore Wake;
static volatile uint32_t Command = COMMAND_NONE;
static volatile uint32_t State = UPDATE_STATE_NORMAL;
static volatile uint32_t LastActivity = 0;
static UpdateFile_t File;
static UpdateHeader_t Header;
static uint32_t Received = 0;
static uint32_t Allocated = 0;

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

/* Tells the PC the state; TimeoutMs 0 for the link thread. */
static void SendState (uint32_t NewState, uint32_t Detail, uint32_t Extra, uint32_t TimeoutMs)
{
    uint8_t Payload[12];

    State = NewState;
    WriteWord (Payload, NewState);
    WriteWord (Payload + 4, Detail);
    WriteWord (Payload + 8, Extra);
    ClientLinkSend (UPDATE_CHANNEL, UPDATE_MSG_STATE, 0, Payload, sizeof (Payload), TimeoutMs);
}

static void Fail (uint32_t Code, uint32_t TimeoutMs)
{
    CyU3PDebugPrint (4, "Update: failed, code %d\r\n", Code);
    SendState (UPDATE_STATE_FAILED, Code, 0, TimeoutMs);
}

static void ProgressCallback (uint32_t Percent)
{
    SendState (UPDATE_STATE_PROGRAMMING, Percent, 0, 100);
}

static void Touch (void)
{
    LastActivity = CyU3PGetTime ();
}

/* ---- Link thread: the messages ---------------------------------------------------------------------------------------------- */

static void HandleBegin (const uint8_t *Payload, uint32_t Length)
{
    uint32_t FileLength;
    uint32_t Needed;
    uint32_t Index;

    if ((State == UPDATE_STATE_NORMAL) || (State == UPDATE_STATE_PROGRAMMING) || (State == UPDATE_STATE_DONE))
    {
        SendState (State, UPDATE_ERR_STATE, 0, 0);
        return;
    }

    FileLength = (Length >= 4) ? ReadWord (Payload) : 0;
    if ((FileLength <= UPDATE_HEADER_BYTES) || (FileLength > (UPDATE_MAX_CHUNKS * UPDATE_CHUNK_BYTES)))
    {
        Fail (UPDATE_ERR_LENGTH, 0);
        return;
    }

    Needed = (FileLength + UPDATE_CHUNK_BYTES - 1) / UPDATE_CHUNK_BYTES;
    for (Index = Allocated; Index < Needed; Index++)
    {
        File.Chunk[Index] = (uint8_t *)CyU3PDmaBufferAlloc (UPDATE_CHUNK_BYTES);
        if (File.Chunk[Index] == NULL)
        {
            Fail (UPDATE_ERR_MEMORY, 0);
            return;
        }
        Allocated = Index + 1;
    }

    File.FileLength = FileLength;
    Received = 0;
    SendState (UPDATE_STATE_RECEIVING, 0, 0, 0);
}

static void HandleData (const uint8_t *Payload, uint32_t Length)
{
    uint32_t Offset;
    uint32_t Count;
    uint32_t Piece;
    const uint8_t *Data;

    if ((State != UPDATE_STATE_RECEIVING) || (Length < 4))
    {
        SendState (State, UPDATE_ERR_STATE, 0, 0);
        return;
    }

    Offset = ReadWord (Payload);
    Data = Payload + 4;
    Count = Length - 4;
    if (Offset != Received)
    {
        Fail (UPDATE_ERR_OFFSET, 0);
        return;
    }
    if ((Received + Count) > File.FileLength)
    {
        Fail (UPDATE_ERR_LENGTH, 0);
        return;
    }

    while (Count > 0)
    {
        Piece = UPDATE_CHUNK_BYTES - (Received % UPDATE_CHUNK_BYTES);
        if (Piece > Count)
            Piece = Count;
        CyU3PMemCopy (&File.Chunk[Received / UPDATE_CHUNK_BYTES][Received % UPDATE_CHUNK_BYTES], (uint8_t *)Data, Piece);
        Received += Piece;
        Data += Piece;
        Count -= Piece;
    }
}

static void HandleEnd (void)
{
    uint32_t Result;

    if (State != UPDATE_STATE_RECEIVING)
    {
        SendState (State, UPDATE_ERR_STATE, 0, 0);
        return;
    }

    if (Received != File.FileLength)
    {
        Fail (UPDATE_ERR_LENGTH, 0);
        return;
    }

    Result = UpdateVerifyFile (&File, UPDATE_TARGET_CLIENT, &Header);
    if (Result != UPDATE_OK)
    {
        Fail (Result, 0);
        return;
    }

    CyU3PDebugPrint (4, "Update: file checked, image version %d, %d bytes (%d in the file)\r\n", Header.ImageVersion, Header.ImageLength, File.FileLength);
    SendState (UPDATE_STATE_VERIFIED, 0, Header.ImageVersion, 0);
}

void UpdateOnMessage (const uint8_t *Block, uint32_t Length)
{
    uint8_t Type = Block[1];
    uint32_t PayloadLength = ReadWord (Block + 4);
    const uint8_t *Payload = Block + 8;

    Touch ();

    switch (Type)
    {
    case UPDATE_MSG_ENTER:
        if (State == UPDATE_STATE_NORMAL)
        {
            Command = COMMAND_ENTER;
            CyU3PSemaphorePut (&Wake);
        }
        else
            SendState (State, 0, 0, 0);
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

    case UPDATE_MSG_COMMIT:
        if (State != UPDATE_STATE_VERIFIED)
        {
            SendState (State, UPDATE_ERR_STATE, 0, 0);
            break;
        }
        State = UPDATE_STATE_PROGRAMMING;
        Command = COMMAND_COMMIT;
        CyU3PSemaphorePut (&Wake);
        break;

    case UPDATE_MSG_RESTART:
        if (State == UPDATE_STATE_PROGRAMMING)
        {
            SendState (State, UPDATE_ERR_STATE, 0, 0);
            break;
        }
        Command = COMMAND_RESTART;
        CyU3PSemaphorePut (&Wake);
        break;

    case UPDATE_MSG_ABORT:
        if ((State == UPDATE_STATE_RECEIVING) || (State == UPDATE_STATE_VERIFIED) || (State == UPDATE_STATE_FAILED))
        {
            Received = 0;
            SendState (UPDATE_STATE_UPDATE_MODE, 0, 0, 0);
        }
        else
            SendState (State, UPDATE_ERR_STATE, 0, 0);
        break;

    case UPDATE_MSG_QUERY:
        SendState (State, (State == UPDATE_STATE_RECEIVING) ? Received : 0, 0, 0);
        break;

    default:
        SendState (State, UPDATE_ERR_STATE, 0, 0);
        break;
    }
}

/* ---- Update thread: the slow parts ------------------------------------------------------------------------------------------ */

static void DoEnter (void)
{
    CyBool_t Crypto = UpdateSelfTest ();
    uint32_t RingBytes = 0;
    uint8_t *Ring;
    uint32_t HddBytes = 0;
    uint8_t *HddRing;
    uint32_t Used;

    CyU3PDebugPrint (4, "Update: entering update mode, signature self test %s\r\n", (Crypto == CyTrue) ? "passed" : "FAILED");

    /* The Client's USB side goes away for the PC. */
    CyU3PConnectState (CyFalse, CyTrue);

    /* The CD-ROM's sector ring is not needed any more and is the biggest block of memory: the first chunks of the file go there. */
    Ring = CdMediaTakeBuffer (&RingBytes);
    while ((Ring != NULL) && (Allocated < UPDATE_MAX_CHUNKS) && (((Allocated + 1) * UPDATE_CHUNK_BYTES) <= RingBytes))
    {
        File.Chunk[Allocated] = Ring + Allocated * UPDATE_CHUNK_BYTES;
        Allocated++;
    }

    /* The HDD ring is not needed either: two more chunks. */
    HddRing = HddMediaTakeBuffer (&HddBytes);
    for (Used = 0; (HddRing != NULL) && (Allocated < UPDATE_MAX_CHUNKS) && (((Used + 1) * UPDATE_CHUNK_BYTES) <= HddBytes); Used++)
    {
        File.Chunk[Allocated] = HddRing + Used * UPDATE_CHUNK_BYTES;
        Allocated++;
    }

    /* The I2C master is not started here: it is only started when the EEPROM is really written (see DoCommit). */
    if (Crypto == CyFalse)
    {
        Fail (UPDATE_ERR_SIGNATURE, 1000);
        return;
    }

    UpdateWriteEnable (CyTrue);       /* the only place where the Client allows EEPROM writes: update mode */

    Received = 0;
    SendState (UPDATE_STATE_UPDATE_MODE, 0, 0, 1000);
}

static void DoCommit (void)
{
    uint32_t Result;

    CyU3PDebugPrint (4, "Update: programming the EEPROM\r\n");
    if (UpdateEepromInit () == CyFalse)
    {
        Fail (UPDATE_ERR_EEPROM, 1000);
        return;
    }
    Result = UpdateProgramEeprom (&File, &Header, ProgressCallback);
    UpdateEepromDeinit ();
    if (Result != UPDATE_OK)
    {
        Fail (Result, 1000);
        return;
    }

    CyU3PDebugPrint (4, "Update: EEPROM programmed and verified, %d page writes\r\n", UpdatePagesWritten ());
    SendState (UPDATE_STATE_DONE, 0, Header.ImageVersion, 1000);
}

static void UpdateThreadEntry (uint32_t Input)
{
    uint32_t Request;

    for (;;)
    {
        if (CyU3PSemaphoreGet (&Wake, 5000) != CY_U3P_SUCCESS)
        {
            /* Quiet: leave update mode on our own after a long silence (never while programming). */
            if ((State != UPDATE_STATE_NORMAL) && (State != UPDATE_STATE_PROGRAMMING) && ((CyU3PGetTime () - LastActivity) > UPDATE_IDLE_RESET_MS))
            {
                CyU3PDebugPrint (4, "Update: nobody there, resetting\r\n");
                ClientLinkRequestReset ();
                LastActivity = CyU3PGetTime ();
            }
            continue;
        }

        Request = Command;
        Command = COMMAND_NONE;
        switch (Request)
        {
        case COMMAND_ENTER:
            DoEnter ();
            break;

        case COMMAND_COMMIT:
            DoCommit ();
            break;

        case COMMAND_RESTART:
            CyU3PDebugPrint (4, "Update: restarting\r\n");
            CyU3PThreadSleep (300);
            ClientLinkRequestReset ();
            break;

        default:
            break;
        }
    }
}

CyBool_t UpdateIsActive (void)
{
    return ((State != UPDATE_STATE_NORMAL) ? CyTrue : CyFalse);
}

void UpdateInit (void)
{
    void *Stack = CyU3PMemAlloc (UPDATE_THREAD_STACK);

    CyU3PSemaphoreCreate (&Wake, 0);
    Touch ();
    if (Stack == NULL)
        return;

    CyU3PThreadCreate (&UpdateThread, "29:Update", UpdateThreadEntry, 0, Stack, UPDATE_THREAD_STACK, UPDATE_THREAD_PRIORITY, UPDATE_THREAD_PRIORITY,
            CYU3P_NO_TIME_SLICE, CYU3P_AUTO_START);
}
