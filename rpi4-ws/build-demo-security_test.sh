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

# build hypervisor
CONFIG_REPO="$ROOT/rpi4-ws/configs"
CONFIG=rpi4-minimal-2
cd "$ROOT"

make -C CROSSCON-Hypervisor/ \
    PLATFORM=rpi4 \
    CONFIG_BUILTIN=y \
    CONFIG_REPO="$CONFIG_REPO" \
    CONFIG="$CONFIG" \
    OPTIMIZATIONS=0 \
        SDEES="sdSGX sdTZ" \
    CROSS_COMPILE=aarch64-none-elf- \
        clean

make -C CROSSCON-Hypervisor/ \
    PLATFORM=rpi4 \
    CONFIG_BUILTIN=y \
    CONFIG_REPO="$CONFIG_REPO" \
    CONFIG="$CONFIG" \
    OPTIMIZATIONS=0 \
        SDEES="sdSGX sdTZ" \
    CROSS_COMPILE=aarch64-none-elf- \
        -j "$(nproc)"
