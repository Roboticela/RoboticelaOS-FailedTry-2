# RoboBoot — original RoboticelaOS bootloader (delegates to scripts/build-bootloader.ps1).

ifeq ($(OS),Windows_NT)
  PS := powershell.exe -NoProfile -ExecutionPolicy Bypass -File
else
  PS := pwsh -NoProfile -File
endif

ROOT := $(abspath $(dir $(lastword $(MAKEFILE_LIST))))
REPO := $(abspath $(ROOT)/..)
BUILD := $(REPO)/scripts/build-bootloader.ps1

.PHONY: all assets host test efi esp qemu qemu-test clean distclean info

all:
	$(PS) "$(BUILD)" all

assets:
	$(PS) "$(BUILD)" assets

host:
	$(PS) "$(BUILD)" host

test:
	$(PS) "$(BUILD)" test

efi:
	$(PS) "$(BUILD)" efi

esp:
	$(PS) "$(BUILD)" image

qemu:
	$(PS) "$(BUILD)" qemu

qemu-test:
	$(PS) "$(BUILD)" qemu-test

clean:
	$(PS) "$(BUILD)" clean

distclean:
	$(PS) "$(BUILD)" distclean

info:
	$(PS) "$(BUILD)" info
