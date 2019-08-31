
# GCC was installed by vscode extension:
GCC_ARM_PATH ?= $(HOME)/.particle/toolchains/gcc-arm/5.3.1/bin/
export GCC_ARM_PATH

# MODULAR_FIRMWARE to make clean remove the correct directory
MODULAR_FIRMWARE ?= y
export MODULAR_FIRMWARE

#DEBUG_BUILD ?= y
#export DEBUG_BUILD

PLATFORM ?= xenon
export PLATFORM

APPDIR = $(shell dirname $(realpath $(lastword $(MAKEFILE_LIST))))
export APPDIR

FIRMWARE_PATH ?= $(APPDIR)/../device-os/

all:
	$(MAKE) -C $(FIRMWARE_PATH)/modules all
#	$(MAKE) -C $(FIRMWARE_PATH)/main all

cc:
	particle compile --saveTo target/temperature.bin xenon

flash:
	particle usb start-listening && particle flash --serial target/temperature.bin -vv

clean:
	$(MAKE) -C $(FIRMWARE_PATH)/user clean
	-$(RM) -r target/
