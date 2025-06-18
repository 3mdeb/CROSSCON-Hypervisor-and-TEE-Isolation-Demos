#!/bin/bash -e

# Bind Linux Image and device tree

cd "$ROOT"
dtc -I dts -O dtb rpi4-ws/rpi4-minimal.dts > rpi4-ws/rpi4-minimal.dtb
dtc -I dts -O dtb rpi4-ws/rpi4-minimal2.dts > rpi4-ws/rpi4-minimal2.dtb

cd "$ROOT/lloader"
rm -f linux-rpi4.bin
rm -f linux-rpi4.elf
make  \
	IMAGE=../linux/build-aarch64/arch/arm64/boot/Image \
	DTB=../rpi4-ws/rpi4-minimal.dtb \
	TARGET=linux-rpi4.bin \
	CROSS_COMPILE=aarch64-none-elf- \
	ARCH=aarch64

rm -f linux2-rpi4.bin
rm -f linux2-rpi4.elf
make  \
	IMAGE=../linux/build-aarch64/arch/arm64/boot/Image \
	DTB=../rpi4-ws/rpi4-minimal2.dtb \
	TARGET=linux2-rpi4.bin \
	CROSS_COMPILE=aarch64-none-elf- \
	ARCH=aarch64

cd "$ROOT/rpi4-ws"
CONFIG=rpi4-minimal-2 ./build_hypervisor.sh
