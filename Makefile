SHELL := /bin/sh
.DEFAULT_GOAL := help
.NOTPARALLEL:

.PHONY: help verify doctor ready host-tools usb-tool loader-package loader \
	prepare-linux rootfs kernel payload x11 busybox rootfs-from-source \
	kernel-from-source payload-from-source all

help:
	@echo "XD-B8600 Linux build targets"
	@echo
	@echo "  make verify              verify supplied artifacts and source syntax"
	@echo "  make doctor              check full-build host prerequisites"
	@echo "  make ready               build USB client + stage supplied loader"
	@echo "  make payload             rebuild kernel with supplied BusyBox/X11"
	@echo "  make all                 run the complete pinned payload/loader build"
	@echo
	@echo "Individual targets:"
	@echo "  make x11                 build Xfbdev, AwesomeWM and desktop utilities"
	@echo "  make busybox             reproduce the static big-endian BusyBox exactly"
	@echo "  make loader              rebuild LNX03 exactly with pinned devkitSH4"
	@echo "  make loader-package      stage the supplied LNX03 without a compiler"
	@echo "  make usb-tool            build the native libexword transfer client"
	@echo
	@echo "Set JOBS=N to control target-build parallelism. See docs/BUILDING.md."

verify:
	@./scripts/check-source-tree.sh
	@./scripts/verify-artifacts.sh

doctor:
	@./scripts/doctor.sh

ready: verify usb-tool loader-package

host-tools: usb-tool

usb-tool:
	@./scripts/build-libexword.sh

loader-package:
	@./scripts/stage-loader-artifact.sh

loader:
	@./scripts/build-loader.sh

prepare-linux:
	@if [ -f build/linux-6.1/Makefile ]; then \
		echo "Using prepared build/linux-6.1"; \
	else \
		./scripts/prepare-linux.sh; \
	fi

rootfs:
	@if [ -x build/rootfs/bin/busybox ]; then \
		echo "Using assembled build/rootfs"; \
	else \
		./scripts/assemble-rootfs.sh; \
	fi

kernel: prepare-linux rootfs
	@./scripts/build-kernel.sh

payload: kernel
	@./scripts/build-payload.sh

x11:
	@./scripts/build-x11.sh

busybox: prepare-linux
	@./scripts/build-busybox.sh

rootfs-from-source: busybox
	@if [ -x build/rootfs-from-source/bin/busybox ]; then \
		echo "Using assembled build/rootfs-from-source"; \
	else \
		./scripts/assemble-rootfs.sh build/rootfs-from-source build/busybox; \
	fi

kernel-from-source: prepare-linux rootfs-from-source
	@ROOTFS=build/rootfs-from-source \
		KERNEL_OUTPUT=build/kernel-from-source \
		./scripts/build-kernel.sh

payload-from-source: kernel-from-source x11
	@./scripts/build-payload.sh \
		build/kernel-from-source/arch/sh/boot/zImage \
		build/x11-xterm.sqfs build/LINUX-from-source.PAY

all: verify payload-from-source loader
	@echo
	@echo "Complete pinned build: build/LINUX-from-source.PAY"
	@echo "Loader packages: build/exword-data/{ja,cn}/LNX03"
