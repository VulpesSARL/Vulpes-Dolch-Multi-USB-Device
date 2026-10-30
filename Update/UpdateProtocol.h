#ifndef UPDATE_PROTOCOL_H
#define UPDATE_PROTOCOL_H

/* Messages of the firmware update, shared by the Client (channel UPDATE_CHANNEL, handled by the Client) and the Host (channel UPDATE_HOST_CHANNEL,
 * handled by the Host itself). The PC side is Vulpes-Dolch-Host-Common\Update\UpdateMessages.cs. */

#define UPDATE_CHANNEL              (6)         /* Messages for the Client; the Host passes them on. */
#define UPDATE_HOST_CHANNEL         (7)         /* Messages for the Host. */

/* PC -> device */
#define UPDATE_MSG_ENTER            (0x01)      /* Enter update mode. */
#define UPDATE_MSG_BEGIN            (0x02)      /* Client: u32 file length. Host: u32 file length, u32 pass (1 verify, 2 program). */
#define UPDATE_MSG_DATA             (0x03)      /* u32 offset, then bytes; offsets must follow each other without gaps. */
#define UPDATE_MSG_END              (0x04)      /* The whole file was sent. */
#define UPDATE_MSG_COMMIT           (0x05)      /* Client only: program the EEPROM with the checked file. */
#define UPDATE_MSG_RESTART          (0x06)      /* Client: the Client asks the Host to reset it. Host: u8 method (0 = CyU3PDeviceReset, 1 = let the watchdog fire). */
#define UPDATE_MSG_ABORT            (0x07)      /* Forget the transfer, stay in update mode. */
#define UPDATE_MSG_QUERY            (0x08)      /* Answer with the state. */
#define UPDATE_MSG_LEAVE            (0x09)      /* Host only: leave update mode without a restart (the normal USB devices come back). */

/* device -> PC */
#define UPDATE_MSG_STATE            (0x81)      /* u32 state, u32 detail, u32 extra. */

#define UPDATE_STATE_NORMAL         (0)
#define UPDATE_STATE_UPDATE_MODE    (1)         /* detail = 0 */
#define UPDATE_STATE_RECEIVING      (2)         /* detail = bytes received so far */
#define UPDATE_STATE_VERIFIED       (3)         /* detail = 0, extra = image version of the file */
#define UPDATE_STATE_PROGRAMMING    (4)         /* detail = percent */
#define UPDATE_STATE_DONE           (5)
#define UPDATE_STATE_FAILED         (6)         /* detail = UPDATE_ERR_* code */

#endif
