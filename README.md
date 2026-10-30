# Vulpes Dolch - Multi USB Device

A small hardware project that lets one computer control another one through a single USB cable. It is built from two
Cypress/Infineon **EZ-USB FX3** boards (`CYUSB3KIT-003`) that are joined by their GPIF II interface:

* The **Host** board is connected to the controlling PC. It is the GPIF master and the only thing that PC talks to (WinUSB, plus a
  MIDI port and a serial port that need no software at all).
* The **Client** board is connected to the target PC. It shows up there as ordinary hardware:
  * a CD-ROM / BD-ROM drive (disc images: ISO, BIN/CUE, or a folder that is mounted as a disc),
  * a removable disk (VHDX and other disk images, optionally read-only),
  * a keyboard and a mouse (HID, boot protocol compatible),
  * a USB-MIDI device and a serial port (CDC-ACM), both passed straight through to the Host side.

The Client works on USB 1.1, 2 and 3. The target PC sees nothing of the controlling PC: images, keyboard and mouse are fed
through the Host board. The Client's firmware lives in its own EEPROM; the Host resets it and watches over it.

## What is in here

| Folder | Content |
|---|---|
| `Host`, `Client` | The two firmwares (C, FX3 SDK 1.3, Eclipse projects and a plain `makefile`) |
| `GPIF` | The GPIF II state machines (GPIF II Designer projects) and the code shared by both firmwares |
| `Update` | The signed firmware update container (HMAC-SHA256) shared by the firmwares and the PC tool |
| `Version` | The build stamp `Major.Minor.YYMM.DDXX` |
| `Host-Tool` | The PC side (.NET 10): `Vulpes-Dolch-Host-Common` (library), `Vulpes-Dolch-Host-Service` (Windows service), `Vulpes-Dolch-Host-Tool` (GUI), `Vulpes-Dolch-Host-CMD` (command line for firmware updates), `Vulpes-Dolch-Stamp` (version stamp tool), `Vulpes-Dolch-Installer` (WiX 6 project that builds the per-machine MSI, see below) |
| `Pin Cutting.png` | Which pins of the interposing connector between the two boards have to be cut |
| `Build-Production.sh` | Builds everything and makes the images and update files |

## Compatibility

What has been tried. "Host" is the PC the Host board is plugged into (the controlling PC), "Client" is the PC the Client board is plugged into
(the target PC).

**Host**

| Device | USB Controller | Speed | OS | State | Quirks |
|---|---|---|---|---|---|
| Dell OptiPlex 7010 | Intel 7 Series/C216 USB 3.0 eXtensible Host Controller | USB 3 | Windows Server 2022 | everything works | Enumeration sometimes flaky |
| Dell OptiPlex 7010 | Intel 7 Series/C216 USB 3.0 eXtensible Host Controller | USB 3 | Windows 10 IoT Enterprise LTSC | everything works | Enumeration sometimes flaky |
| Dell OptiPlex 7010 | Intel 7 Series/C216 USB Enhanced Host Controller | USB 2 | Windows Server 2022 | everything works | (none) |
| Dell OptiPlex 7010 | Intel 7 Series/C216 USB Enhanced Host Controller | USB 2 | Windows 10 IoT Enterprise LTSC | everything works | (none) |
| Supermicro H13SSL-NT | AMD USB 3.10 eXtensible Host Controller | USB 3 | Windows 11 Enterprise | everything works | (none) |

**Client**

| Device | USB Controller | Speed | OS | Mass Storage | CD-ROM | MIDI | Mouse/Keyboard | Serial Port | Quirks |
|---|---|---|---|---|---|---|---|---|---|
| Dell OptiPlex 7010 | Intel 7 Series/C216 USB 3.0 eXtensible Host Controller | USB 3 | Windows Server 2022 | yes | yes | yes | yes | yes | Enumeration sometimes flaky |
| Dell OptiPlex 7010 | Intel 7 Series/C216 USB 3.0 eXtensible Host Controller | USB 3 | Windows 10 IoT Enterprise LTSC | yes | yes | yes | yes | yes | Enumeration sometimes flaky |
| Dell OptiPlex 7010 | Intel 7 Series/C216 USB Enhanced Host Controller | USB 2 | Windows Server 2022 | yes | yes | yes | yes | yes | (none) |
| Dell OptiPlex 7010 | Intel 7 Series/C216 USB Enhanced Host Controller | USB 2 | Windows 10 IoT Enterprise LTSC | yes | yes | yes | yes | yes | (none) |
| Intel SE440BX-2 mainboard | Intel PIIX4E USB Controller | USB 1.1 | Windows Server 2003 | yes | yes | yes | yes | no driver | (none) |
| Kyocera ECOSYS M6026cdn (printer) | unknown | USB 2 | Kyocera firmware | yes | no use | no use | keyboard only | no use | Storage must be FAT32, with or without an MBR partition table |

"Enumeration sometimes flaky" means: after the device was plugged in or re-enumerated (after a firmware update or a disk change, for
example) the USB 3 port occasionally shows nothing at all until the device is unplugged and plugged in again or the USB controller is reset.
It happens with these controllers only, not with the USB 2 ports of the same computers. The serial port needs no driver on Windows 10 and
Windows Server 2022 (the inbox `usbser`); on Windows Server 2003 there is no driver for it without writing an INF.

## Building

You need:

* the **EZ-USB FX3 SDK 1.3** with its ARM GCC toolchain (`cs-make` and `elf2img` on the `PATH`; the SDK installer sets
  `ARMGCC_INSTALL_PATH` and `FX3_INSTALL_PATH`). The SDK is not part of this repository.
* the **.NET 10 SDK**.
* Git Bash (or any bash) on Windows.

```
bash Build-Production.sh
```

This builds the PC tools, stamps the version, builds both firmwares without debug output and writes
`Production/HostProd.img|.vdu` and `Production/ClientProd.img|.vdu`. `FX3_SDK=<path>` selects the SDK folder (GNU make cannot
handle spaces, use the 8.3 name), `NO_STAMP=1` keeps the version stamp. A firmware with all debug messages is built by calling
`cs-make` in `Host` or `Client` without `CYCONFOPT=fx3_release`.

The `.img` files are loaded into the board's I2C EEPROM (for example with the SDK's Control Center). Later updates go through
the Host (`Vulpes-Dolch-Host-CMD update-host` / `update-client`).

## The installer

`Host-Tool/Vulpes-Dolch-Installer` publishes the GUI and the service self-contained (.NET 10, win-x64), signs the binaries and the MSI with a command called
`vulpessign`, and builds a per-machine 64 bit MSI that installs and starts the service. The version is in `Version.wxi`. Without a signing script:

```
dotnet build Host-Tool/Vulpes-Dolch-Installer/Vulpes-Dolch-Installer.wixproj -c Release -p:SkipSigning=true
```

The MSI ends up in `Host-Tool/bin/Release`. The project needs the WiX 6 SDK from NuGet.

## The update key - read this before you build

Firmware update files are signed with a 32 byte key that is compiled into both firmwares (`Update/UpdateKey.h`) and read by the
PC tool (`Update/UpdateKey.hex`). **The key in this repository is public** (the bytes 1 to 32): anybody can sign an update for a
device that carries it. For your own devices create a private one first:

```
Vulpes-Dolch-Host-CMD keygen
```

(delete the two public key files before; `keygen` refuses to overwrite them) and keep the new files out of version control.
Devices only accept update files signed with the key they were built with.

## Hardware notes

* Wiring and the pins to cut on the connector between the boards: see `Pin Cutting.png`. Reset of the Client, the LEDs and the
  extra GPIF flags need point-to-point wires; the pin assignments are in the firmware (`Host/main.h`, `GPIF/*.cydsn`).
* The USB descriptors use the vendor ID `0x1209` with test product IDs. If you build devices to give away, get your own ID.
* On some computers (seen on Intel 7 series boards) a USB 3 device is missing after it was re-enumerated until the USB controller
  is reset. That is a problem of those controllers, not of the firmware.

## License

AGPL-3.0, with the additional terms in section 18 - see `LICENSE`. The files `cyfxtx.c` and `cyfx_gcc_startup.S` in `Host` and
`Client` are copies from the Cypress/Infineon FX3 SDK (copyright Cypress Semiconductor / Infineon); they are not covered by this
license but by the SDK's own license.
