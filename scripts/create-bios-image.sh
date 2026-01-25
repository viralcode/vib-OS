#!/bin/bash
# Create BIOS bootable disk image for x86
# Works on both macOS and Linux

set -e

BUILD_DIR="${1:-build/x86}"
IMAGE_DIR="${2:-image}"
IMAGE_NAME="vibos-x86.img"
IMAGE_SIZE="100M"

# Colors
GREEN='\033[0;32m'
NC='\033[0m'

log() {
    echo -e "${GREEN}[BIOS-IMAGE]${NC} $1"
}

# Create image directory
mkdir -p "$IMAGE_DIR"

IMAGE_PATH="$IMAGE_DIR/$IMAGE_NAME"

log "Creating BIOS disk image: $IMAGE_PATH ($IMAGE_SIZE)"

# Create empty disk image
dd if=/dev/zero of="$IMAGE_PATH" bs=1M count=100 2>/dev/null

log "Installing bootloader..."

# Write stage 1 bootloader (MBR)
if [ -f "$BUILD_DIR/boot/stage1.bin" ]; then
    dd if="$BUILD_DIR/boot/stage1.bin" of="$IMAGE_PATH" bs=512 count=1 conv=notrunc 2>/dev/null
    log "Stage 1 bootloader installed"
else
    log "WARNING: Stage 1 bootloader not found"
fi

# Write stage 2 bootloader
if [ -f "$BUILD_DIR/boot/stage2.bin" ]; then
    dd if="$BUILD_DIR/boot/stage2.bin" of="$IMAGE_PATH" bs=512 seek=1 conv=notrunc 2>/dev/null
    log "Stage 2 bootloader installed"
else
    log "WARNING: Stage 2 bootloader not found"
fi

# Write kernel
if [ -f "$BUILD_DIR/kernel/vibos-x86.elf" ]; then
    # Write kernel starting at sector 32 (16KB offset)
    dd if="$BUILD_DIR/kernel/vibos-x86.elf" of="$IMAGE_PATH" bs=512 seek=32 conv=notrunc 2>/dev/null
    log "Kernel installed"
else
    log "WARNING: Kernel not found"
fi

log "BIOS boot image created successfully: $IMAGE_PATH"
ls -lh "$IMAGE_PATH"

echo ""
log "To test in QEMU: make ARCH=x86 qemu"
log "To write to USB: sudo dd if=$IMAGE_PATH of=/dev/sdX bs=4M status=progress"
