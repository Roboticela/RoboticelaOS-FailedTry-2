# RoboticelaOS — top-level build (wraps the PowerShell engine).
#
#   make              assets + host + EFI + ESP image
#   make bootloader   EFI + ESP image
#   make test
#   make qemu
#   make clean

ifeq ($(OS),Windows_NT)
  PS := powershell.exe -NoProfile -ExecutionPolicy Bypass -File
else
  PS := pwsh -NoProfile -File
endif

ROOT := $(abspath $(dir $(lastword $(MAKEFILE_LIST))))
BUILD := $(ROOT)/scripts/build-bootloader.ps1

.PHONY: all assets host efi image esp bootloader kernel test check qemu qemu-test run \
        clean distclean info help configure

all:
	$(PS) "$(BUILD)" all

configure:
	$(PS) "$(ROOT)/scripts/configure.ps1"

assets:
	$(PS) "$(BUILD)" assets

host:
	$(PS) "$(BUILD)" host

efi:
	$(PS) "$(BUILD)" efi

kernel:
	$(PS) "$(BUILD)" kernel

image esp:
	$(PS) "$(BUILD)" image

bootloader:
	$(PS) "$(BUILD)" bootloader

test check:
	$(PS) "$(BUILD)" test

qemu:
	$(PS) "$(BUILD)" qemu

qemu-test:
	$(PS) "$(BUILD)" qemu-test

run:
	$(PS) "$(BUILD)" run

clean:
	$(PS) "$(BUILD)" clean

distclean:
	$(PS) "$(BUILD)" distclean

info:
	$(PS) "$(BUILD)" info

help:
	$(PS) "$(BUILD)" help
