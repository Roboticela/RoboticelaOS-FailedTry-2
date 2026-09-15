# RoboticelaOS — top-level convenience targets.
#
# There is exactly one build pipeline: scripts/robo. This Makefile only gives
# `make` users a familiar way to reach it, so nothing here can drift from what
# the pipeline actually does.
#
#   make                scripts/robo build-all
#   make check          scripts/robo check
#   make test           scripts/robo test-all --skip-qemu
#   make test-all       scripts/robo test-all
#   make deploy         scripts/robo deploy
#   make run            scripts/robo run-qemu
#   make clean          scripts/robo clean
#   make help           scripts/robo help

ifeq ($(OS),Windows_NT)
  ROBO := powershell.exe -NoProfile -ExecutionPolicy Bypass -File scripts/pipeline/robo.ps1
else
  ROBO := pwsh -NoProfile -File scripts/pipeline/robo.ps1
endif

ROOT := $(abspath $(dir $(lastword $(MAKEFILE_LIST))))

.PHONY: all check configure build-all build-assets build-bootloader build-kernel build-userland \
        build-runtime build-image test test-all test-bootloader test-kernel test-runtime \
        test-qemu-kernel test-qemu-display test-e2e qemu run run-kernel run-runtime \
        deploy validate-deployment clean distclean rebuild info version help

all build-all:
	@cd "$(ROOT)" && $(ROBO) build-all

check configure:
	@cd "$(ROOT)" && $(ROBO) check

build-assets:
	@cd "$(ROOT)" && $(ROBO) build-assets

host bootloader build-bootloader:
	@cd "$(ROOT)" && $(ROBO) build-bootloader

efi image esp combine build-image:
	@cd "$(ROOT)" && $(ROBO) build-image

kernel build-kernel:
	@cd "$(ROOT)" && $(ROBO) build-kernel

userland build-userland:
	@cd "$(ROOT)" && $(ROBO) build-userland

runtime build-runtime:
	@cd "$(ROOT)" && $(ROBO) build-runtime

test:
	@cd "$(ROOT)" && $(ROBO) test-all --skip-qemu

test-all:
	@cd "$(ROOT)" && $(ROBO) test-all

test-bootloader:
	@cd "$(ROOT)" && $(ROBO) test-bootloader

test-kernel:
	@cd "$(ROOT)" && $(ROBO) test-kernel

test-runtime:
	@cd "$(ROOT)" && $(ROBO) test-runtime

test-qemu-kernel qemu-test:
	@cd "$(ROOT)" && $(ROBO) test-qemu-kernel

test-qemu-display:
	@cd "$(ROOT)" && $(ROBO) test-qemu-display

test-e2e:
	@cd "$(ROOT)" && $(ROBO) test-e2e

qemu run-kernel:
	@cd "$(ROOT)" && $(ROBO) run-qemu

run run-runtime:
	@cd "$(ROOT)" && $(ROBO) run-runtime

deploy:
	@cd "$(ROOT)" && $(ROBO) deploy

validate-deployment:
	@cd "$(ROOT)" && $(ROBO) validate-deployment

clean:
	@cd "$(ROOT)" && $(ROBO) clean

distclean:
	@cd "$(ROOT)" && $(ROBO) clean --baked

rebuild:
	@cd "$(ROOT)" && $(ROBO) rebuild

info version:
	@cd "$(ROOT)" && $(ROBO) version

help:
	@cd "$(ROOT)" && $(ROBO) help
