#ifndef FIRMWARE_VERSION_TAG_H
#define FIRMWARE_VERSION_TAG_H

/* The firmware version as the PC application shows it. It is NOT part of any USB descriptor.
   FirmwareVersion.h is written by Vulpes-Dolch-Stamp ("Major.Minor.YYMM.DDXX", XX = build of the day).

   Besides the HELLO / CLIENT_STATUS messages (which carry FW_VERSION_PACKED), every image holds a 32 byte tag "VDFWTAG1" + packed version (u32, little endian)
   + the version text, so the PC tool can read the version out of an .img (update-pack) without running it.
   FIRMWARE_VERSION_TAG_DEFINE (Name) defines the tag; FIRMWARE_VERSION_TAG_TEXT (Name) is the text inside it (referencing it keeps the tag in the image). */
#include "FirmwareVersion.h"

#define FIRMWARE_VERSION_TAG_DEFINE(Name) \
    const struct { char Magic[8]; unsigned char Packed[4]; char Text[20]; } Name __attribute__((used)) = \
    { "VDFWTAG1", \
      { (unsigned char)(FW_VERSION_PACKED & 0xFF), (unsigned char)((FW_VERSION_PACKED >> 8) & 0xFF), \
        (unsigned char)((FW_VERSION_PACKED >> 16) & 0xFF), (unsigned char)((FW_VERSION_PACKED >> 24) & 0xFF) }, \
      FW_VERSION_STRING }

#define FIRMWARE_VERSION_TAG_TEXT(Name)      ((Name).Text)

/* Production builds have no debug print that references the tag, and the linker drops unreferenced data: this inline assembly reference from live code keeps it in the image. */
#define FIRMWARE_VERSION_TAG_KEEP(Name)      __asm__ volatile ("" : : "r" (&(Name)))

#endif
