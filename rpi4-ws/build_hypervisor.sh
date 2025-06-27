#!/bin/bash

set -e

CONFIG_REPO="$ROOT/rpi4-ws/configs"
cd "$ROOT"

if [ -z "$CONFIG" ]; then
    echo "Set CONFIG variable to config you want to use e.g. 'rpi4-single-vTEE'"
    return 1
fi

make -C CROSSCON-Hypervisor/ \
    PLATFORM=rpi4 \
    CONFIG_BUILTIN=y \
    CONFIG_REPO=$CONFIG_REPO \
    CONFIG="$CONFIG" \
    OPTIMIZATIONS=0 \
        SDEES="sdSGX sdTZ" \
    CROSS_COMPILE=aarch64-none-elf- \
        clean

make -C CROSSCON-Hypervisor/ \
    PLATFORM=rpi4 \
    CONFIG_BUILTIN=y \
    CONFIG_REPO=$CONFIG_REPO \
    CONFIG="$CONFIG" \
    OPTIMIZATIONS=0 \
        SDEES="sdSGX sdTZ" \
    CROSS_COMPILE=aarch64-none-elf- \
        -j "$(nproc)"
