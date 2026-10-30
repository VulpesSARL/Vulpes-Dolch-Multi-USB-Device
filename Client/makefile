
FX3FWROOT=../../..

all:compile

# gpif_device.h (GPIF II Designer project) and the shared GPIF block link source live outside this folder.
USER_CFLAGS = -I../GPIF/Device.cydsn -I../GPIF -I../Update

include $(FX3FWROOT)/fw_build/fx3_fw/fx3_build_config.mak

# Debug messages work like #if DEBUG: a debug build (CYCONFOPT=fx3_debug, the default) prints, a release build (CYCONFOPT=fx3_release) is silent.
# The SDK defines DEBUG in every configuration, so the switch is ours: GPIF/NoDebug.h mutes CyU3PDebugPrint. FIRMWARE_DEBUG=1 keeps the messages in a release build.
ifeq ($(findstring release,$(CYCONFOPT)),release)
ifneq ($(FIRMWARE_DEBUG),1)
CCFLAGS += -include ../GPIF/NoDebug.h
endif
endif

MODULE = vulpes-dolch-usb-multi-client

SOURCE= main.c 	\
	USB_DESC.c	\
	CDROM.c \
    STORAGE.c \
    POWER.c \
    GPIFLINK.c \
    GUARD.c \
    UPDATE.c \
    UPDCORE.c \
    LINKCLIENT.c \
    CDMEDIA.c \
    HDDMEDIA.c \
    PERIPH.c \
    PERIPHSTREAM.c \
	cyfxtx.c

ifeq ($(CYFXBUILD),arm)
SOURCE_ASM=cyfx_startup.S
else
SOURCE_ASM=cyfx_gcc_startup.S
endif

C_OBJECT=$(SOURCE:%.c=./%.o)
A_OBJECT=$(SOURCE_ASM:%.S=./%.o)

EXES = $(MODULE).$(EXEEXT)

$(MODULE).$(EXEEXT): $(A_OBJECT) $(C_OBJECT)
	$(LINK)

cyfxtx.c:
	cp $(FX3FWROOT)/fw_build/fx3_fw/cyfxtx.c .

cyfx_startup.S:
	cp $(FX3FWROOT)/fw_build/fx3_fw/cyfx_startup.S .

cyfx_gcc_startup.S:
	cp $(FX3FWROOT)/fw_build/fx3_fw/cyfx_gcc_startup.S .

$(C_OBJECT) : %.o : %.c
	$(COMPILE)

$(A_OBJECT) : %.o : %.S
	$(ASSEMBLE)

clean:
	rm -f ./$(MODULE).$(EXEEXT)
	rm -f ./$(MODULE).map
	rm -f ./*.o
	rm -f cyfxtx.c cyfx_startup.S cyfx_gcc_startup.S


compile: $(C_OBJECT) $(A_OBJECT) $(EXES)

#[]#
