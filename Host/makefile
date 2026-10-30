
FX3FWROOT=../../..

all:compile

# gpif_host.h lives in the separate GPIF II Designer project folder, not alongside main.c.
USER_CFLAGS = -I../GPIF/Host.cydsn -I../GPIF -I../Update

include $(FX3FWROOT)/fw_build/fx3_fw/fx3_build_config.mak

# Debug messages work like #if DEBUG: a debug build (CYCONFOPT=fx3_debug, the default) prints, a release build (CYCONFOPT=fx3_release) is silent.
# The SDK defines DEBUG in every configuration, so the switch is ours: GPIF/NoDebug.h mutes CyU3PDebugPrint. FIRMWARE_DEBUG=1 keeps the messages in a release build.
ifeq ($(findstring release,$(CYCONFOPT)),release)
ifneq ($(FIRMWARE_DEBUG),1)
CCFLAGS += -include ../GPIF/NoDebug.h
endif
endif

MODULE = cyfxbulksrcsink

SOURCE= main.c 	\
	LINK.c	\
	BULK.c	\
	GPIFLINK.c	\
	GUARD.c	\
	HOSTUPD.c	\
	UPDCORE.c	\
	CLIENT.c	\
	PERIPH.c	\
	PERIPHSTREAM.c	\
	USB_DESC.c	\
	cyfxtx.c

# Test builds only (CY_FX_GPIF_TEST): the GPIF characterization code, not part of the public source tree.
ifneq ($(wildcard GPIFTEST.c),)
SOURCE += GPIFTEST.c
endif

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
