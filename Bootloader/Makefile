# RoboBoot (RoboticelaOS bootloader) — convenience targets.
#
# The build lives in the one project pipeline: scripts/robo. Run these from
# anywhere; they resolve paths from the repository root.
#
#   make            scripts/robo build-bootloader
#   make kernel     scripts/robo build-kernel
#   make image      scripts/robo build-image
#   make test       scripts/robo test-bootloader
#   make qemu       scripts/robo run-qemu
#   make clean      scripts/robo clean

ifeq ($(OS),Windows_NT)
  ROBO := powershell.exe -NoProfile -ExecutionPolicy Bypass -File ../scripts/pipeline/robo.ps1
else
  ROBO := pwsh -NoProfile -File ../scripts/pipeline/robo.ps1
endif

.PHONY: all assets host efi esp bootloader kernel image combine test clean distclean info version qemu qemu-test help

all host efi bootloader:
	@$(ROBO) build-bootloader

assets:
	@$(ROBO) build-assets

kernel:
	@$(ROBO) build-kernel

esp image combine:
	@$(ROBO) build-image

test:
	@$(ROBO) test-bootloader

qemu:
	@$(ROBO) run-qemu

qemu-test:
	@$(ROBO) test-qemu-kernel

clean:
	@$(ROBO) clean

distclean:
	@$(ROBO) clean --baked

info version:
	@$(ROBO) version

help:
	@$(ROBO) help
