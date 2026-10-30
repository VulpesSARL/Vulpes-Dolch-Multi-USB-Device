#!/bin/bash
# Builds the Host and the Client firmware for production (release configuration, no debug messages), stamps the version and makes the EEPROM
# images and the signed update files. Nothing is flashed. Run it with Git Bash (or any bash) on Windows.
#
# Needs: the Cypress/Infineon EZ-USB FX3 SDK 1.3 and its ARM GCC toolchain (cs-make and elf2img on the PATH), the .NET 10 SDK.
# Output: Production/HostProd.img|.vdu and Production/ClientProd.img|.vdu
#
# Options (environment variables):
#   FX3_SDK=/c/PROGRA~2/Cypress/EZ-USB~1/1.3   the SDK folder; GNU make cannot handle spaces, so use the 8.3 short name (dir /x)
#   NO_STAMP=1          keep the version stamp as it is (reproducible build)
#   DEBUG_COMMANDS=0    leave the debug commands out of the Host (reset-host, crash, fault injection)
set -e
Root="$(cd "$(dirname "$0")" && pwd)"
Sdk="${FX3_SDK:-/c/PROGRA~2/Cypress/EZ-USB~1/1.3}"
Tools="$Root/Host-Tool/bin/Release"
Extra=""
if [ -n "$DEBUG_COMMANDS" ]; then Extra="CY_FX_DEBUG_COMMANDS=$DEBUG_COMMANDS"; fi

dotnet build "$Root/Host-Tool/Vulpes-Dolch-Host-Tool.sln" -c Release

if [ -z "$NO_STAMP" ]; then
    "$Tools/Vulpes-Dolch-Stamp.exe" "$(cd "$Root/Version" && pwd -W)" --cs "$(cd "$Root/Host-Tool/Vulpes-Dolch-Host-Common" && pwd -W)/BuildVersion.cs"
fi

Build()
{
    local Dir="$1" Module="$2" Name="$3" Target="$4"
    cd "$Root/$Dir"
    rm -f ./*.o ./*.elf
    if [ -n "$Extra" ] && [ "$Dir" = "Host" ]; then
        cs-make FX3FWROOT="$Sdk" CYCONFOPT=fx3_release CYSDKVERSION=1_3_4 USER_CFLAGS="-I../GPIF/Host.cydsn -I../GPIF -I../Update -D$Extra"
    else
        cs-make FX3FWROOT="$Sdk" CYCONFOPT=fx3_release CYSDKVERSION=1_3_4
    fi
    elf2img.exe -i "$Module.elf" -o "$Name.img" -i2cconf 0x2C -v
    mkdir -p "$Root/Production"
    cp "$Name.img" "$Root/Production/$Name.img"
    "$Tools/Vulpes-Dolch-Host-CMD.exe" update-pack "$(pwd -W)/$Name.img" "$(cd "$Root/Production" && pwd -W)/$Name.vdu" --target "$Target"
}

Build Host cyfxbulksrcsink HostProd host
Build Client vulpes-dolch-usb-multi-client ClientProd client
cd "$Root/Production"
"$Tools/Vulpes-Dolch-Host-CMD.exe" image-version "$(pwd -W)/HostProd.vdu"
"$Tools/Vulpes-Dolch-Host-CMD.exe" image-version "$(pwd -W)/ClientProd.vdu"
ls -l "$Root/Production"