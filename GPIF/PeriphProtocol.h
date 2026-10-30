/* Message types of the peripheral channels (keyboard / mouse, MIDI, serial port) that ride the PC link and the GPIF lane next to the media
 * channels. Shared by the Host firmware, the Client firmware and (as a mirror, Protocol\PeripheralMessages.cs) the PC library; keep them in sync. */

#ifndef _INCLUDED_PERIPHPROTOCOL_H_
#define _INCLUDED_PERIPHPROTOCOL_H_

#define PERIPH_CHANNEL_KEYBOARD_MOUSE    (3)
#define PERIPH_CHANNEL_MIDI              (4)
#define PERIPH_CHANNEL_SERIAL            (5)

/* Channel 3, keyboard / mouse. PC -> Client: the report goes to the USB interrupt endpoint of the target PC as it is. No tags, no answers. */
#define KM_KEYBOARD_REPORT               (0x10)     /* 8 bytes: modifiers, reserved, 6 key codes (HID boot keyboard report). */
#define KM_MOUSE_REPORT                  (0x11)     /* 4 bytes: buttons, dx, dy, wheel (signed 8 bit, relative). */
#define KM_RELEASE_ALL                   (0x12)     /* No payload: all keys and buttons up. The Client also does this by itself when the PC is gone. */
#define KM_QUERY                         (0x13)     /* No payload: the Client answers with KM_STATE. */
/* Client -> PC. */
#define KM_LEDS                          (0x20)     /* 1 byte: the keyboard LEDs the target PC set (bit 0 num lock, 1 caps lock, 2 scroll lock, 3 compose, 4 kana). */
#define KM_STATE                         (0x21)     /* 4 bytes: keyboard configured, mouse configured (0/1), LEDs, USB suspended (0/1). */

/* Channels 4 (MIDI) and 5 (serial port): a byte stream with a window. The DATA payload is the bytes of one USB packet; the receiver announces the
 * bytes it has delivered to its USB side with CREDIT (a free-running 32 bit total, so a repeated or lost CREDIT does no harm) and the sender never
 * has more than the receiver's ring size in flight. */
#define STREAM_RESET                     (0x01)     /* Client -> Host after the Client started: forget everything, both rings are empty, all credit is back. */
#define STREAM_DATA                      (0x10)     /* The bytes. */
#define STREAM_CREDIT                    (0x11)     /* u32: total number of bytes the sender of this message has taken out of its ring so far. */
#define SERIAL_LINES                     (0x20)     /* Serial port only, 1 byte: the control lines of the sender's COM port (bit 0 DTR, bit 1 RTS). */

#define PERIPH_MIDI_RING_BYTES           (2048)     /* Receive ring of the MIDI stream on both boards (the window the other side may use). */
#define PERIPH_SERIAL_RING_BYTES         (4096)     /* Receive ring of the serial stream on both boards. */

#endif /* _INCLUDED_PERIPHPROTOCOL_H_ */
