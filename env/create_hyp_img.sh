#!/bin/bash

IMAGE=crosscon-demo-img.img
MOUNT_DIR=/media/root/boot
ROOT=$(git -C "$(dirname "$(realpath $0)")" rev-parse --show-toplevel)
CONFIG_REPO="$ROOT/rpi4-ws/configs"
CONFIG_NAME="rpi4-single-vTEE"
C_PATH="/work/gcc-arm-11.2-2022.02-x86_64-aarch64-none-elf/bin:/work/gcc-arm-11.2-2022.02-x86_64-aarch64-none-linux-gnu/bin:$PATH"

# Function to clean up if script fails
cleanup() {
    echo "# Detaching loop device"
    umount "$MOUNT_DIR" 2>/dev/null || true
    losetup -d "$LOOP_DEV" 2>/dev/null || true
    echo "# Cleaning up, exiting..."
    exit 1
}


# Exit on failure
set -e

# Ensure cleanup happens if any command fails
trap cleanup ERR

# Parse args
for arg in "$@"; do
    case $arg in
        --config=*)
            CONFIG_NAME="${arg#*=}"
            shift
            ;;
        *)
            echo "Usage: $0 [--config=<config-name>]"
            echo "       Default config: rpi4-single-vTEE"
            exit 1
            ;;
    esac
done

# Change dir to root
cd $ROOT

echo "# Creating empty image"
sudo -u "$SUDO_USER" dd if=/dev/zero of="$IMAGE" bs=1M count=256

echo "# Associating the image with loop device"
LOOP_DEV=$(losetup --show -fP $IMAGE)
echo "# Loop device is $LOOP_DEV"

echo "# Partitioning the image"
sfdisk "$LOOP_DEV" <<EOF
label: dos
label-id: 0x12345678
device: $LOOP_DEV
unit: sectors

${LOOP_DEV}p1 : start=16384, type=c, bootable
EOF

sleep 2
echo "# Rereading partition table"
partprobe "$LOOP_DEV"
udevadm settle

echo "# Formatting partition"
mkfs -t vfat -n boot "${LOOP_DEV}p1" -v

echo "# Creating mount point"
mkdir -p $MOUNT_DIR

echo "# Mounting the partition at $MOUNT_DIR"
mount "${LOOP_DEV}p1" $MOUNT_DIR

echo "# Cleaning hypervisor"
sudo -u "$SUDO_USER" env PATH=$C_PATH make -C CROSSCON-Hypervisor/ \
    PLATFORM=rpi4 \
    CONFIG_BUILTIN=y \
    CONFIG_REPO=$CONFIG_REPO \
    CONFIG=$CONFIG_NAME \
    OPTIMIZATIONS=0 \
    SDEES='sdSGX sdTZ' \
    CROSS_COMPILE=aarch64-none-elf- \
    clean

echo "# Building hypervisor for $CONFIG_NAME configuration"
# We're running this as non-root to preserve ownership
sudo -u "$SUDO_USER" env PATH=$C_PATH make -C CROSSCON-Hypervisor/ \
    PLATFORM=rpi4 \
    CONFIG_BUILTIN=y \
    CONFIG_REPO=$CONFIG_REPO \
    CONFIG=$CONFIG_NAME \
    OPTIMIZATIONS=0 \
    SDEES="sdSGX sdTZ" \
    CROSS_COMPILE=aarch64-none-elf- \
    -j"$(nproc)"

echo "# Copying files"
cp -vr rpi4-ws/firmware/boot/* $MOUNT_DIR
cp -v rpi4-ws/config.txt $MOUNT_DIR
cp -v rpi4-ws/bin/bl31.bin $MOUNT_DIR
cp -v rpi4-ws/bin/u-boot.bin $MOUNT_DIR
cp -v lloader/linux-rpi4.bin $MOUNT_DIR
cp -vr rpi4-ws/firmware/boot/start* $MOUNT_DIR
cp -uv CROSSCON-Hypervisor/bin/rpi4/builtin-configs/rpi4-single-vTEE/crossconhyp.bin $MOUNT_DIR

echo "# Unmounting the image"
umount $MOUNT_DIR

echo "# Detaching loop device"
losetup -d "$LOOP_DEV"

echo "# Done!"
