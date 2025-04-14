#!/bin/bash -e

CONFIG_REPO=`pwd`/configs

pushd ..

make -C CROSSCON-Hypervisor/ \
	PLATFORM=rpi4 \
	CONFIG_BUILTIN=y \
	CONFIG_REPO=$CONFIG_REPO \
	CONFIG=rpi4-single-vTEE \
	OPTIMIZATIONS=0 \
        SDEES="sdSGX sdTZ" \
	CROSS_COMPILE=aarch64-none-elf- \
        clean

make -C CROSSCON-Hypervisor/ \
	PLATFORM=rpi4 \
	CONFIG_BUILTIN=y \
	CONFIG_REPO=$CONFIG_REPO \
	CONFIG=rpi4-single-vTEE \
	OPTIMIZATIONS=0 \
        SDEES="sdSGX sdTZ" \
	CROSS_COMPILE=aarch64-none-elf- \
        -j`nproc`
export SDCARD="${SDCARD:-/media/$USER/boot}"
cp -vr rpi4-ws/firmware/boot/start* "$SDCARD"
cp -uv CROSSCON-Hypervisor/bin/rpi4/builtin-configs/rpi4-single-vTEE/crossconhyp.bin "$SDCARD"

umount "$SDCARD"

popd
