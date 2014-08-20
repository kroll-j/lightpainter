#
#             LUFA Library
#     Copyright (C) Dean Camera, 2014.
#
#  dean [at] fourwalledcubicle [dot] com
#           www.lufa-lib.org
#
# --------------------------------------
#         LUFA Project Makefile.
# --------------------------------------

# Run "make help" for target help.

MCU          = atmega32u4
ARCH         = AVR8
BOARD        = USER
F_CPU        = 16000000
F_USB        = $(F_CPU)
OPTIMIZATION = s
TARGET       = main
SRC          = $(wildcard *.c) $(wildcard lufa/*.c) $(LUFA_SRC_USB) $(LUFA_SRC_USBCLASS)
LUFA_PATH    = $(HOME)/src/lufa-LUFA-140302/LUFA
CC_FLAGS     = -DUSE_LUFA_CONFIG_HEADER -IConfig/ -I. -Ilufa
LD_FLAGS     =

SERIAL=/dev/ttyACM0
upload: all
	echo reset > $(SERIAL); sleep .5
	while ! stat $(SERIAL) >/dev/null ; do sleep .5; done
	-avrdude -p m32u4 -c avr109 -P $(SERIAL) -U flash:w:$(TARGET).hex & (sleep 10; killall avrdude)

# Default target
all:

# Include LUFA build script makefiles
include $(LUFA_PATH)/Build/lufa_core.mk
include $(LUFA_PATH)/Build/lufa_sources.mk
include $(LUFA_PATH)/Build/lufa_build.mk
include $(LUFA_PATH)/Build/lufa_cppcheck.mk
include $(LUFA_PATH)/Build/lufa_doxygen.mk
include $(LUFA_PATH)/Build/lufa_dfu.mk
include $(LUFA_PATH)/Build/lufa_hid.mk
include $(LUFA_PATH)/Build/lufa_avrdude.mk
include $(LUFA_PATH)/Build/lufa_atprogram.mk
