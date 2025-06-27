#!/bin/bash

set -e

# Linux with uart3
cd "$ROOT"
dtc -I dts -O dtb rpi4-ws/rpi4-minimal2.dts > rpi4-ws/rpi4-minimal2.dtb

cd "$ROOT/lloader"
rm -f linux2-rpi4.bin
rm -f linux2-rpi4.elf
make  \
	IMAGE=../linux/build-aarch64/arch/arm64/boot/Image \
	DTB=../rpi4-ws/rpi4-minimal2.dtb \
	TARGET=linux2-rpi4.bin \
	CROSS_COMPILE=aarch64-none-elf- \
	ARCH=aarch64

# bare metal app
cd "$ROOT/memory-separation"
make clean
make CROSS_COMPILE=aarch64-none-elf- CONFIG_TEXT_BASE=0x20200000

cd "$ROOT/rpi4-ws"
CONFIG=rpi4-baremetal ./build_hypervisor.sh
