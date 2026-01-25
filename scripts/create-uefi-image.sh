#!/bin/bash
# Create UEFI bootable disk image for x86_64
# Works on both macOS and Linux

set -e

BUILD_DIR="${1:-build/x86_64}"
IMAGE_DIR="${2:-image}"
IMAGE_NAME="vibos-x86_64.img"
IMAGE_SIZE="100M"

# Colors
GREEN='\033[0;32m'
NC='\033[0m'

log() {
    echo -e "${GREEN}[UEFI-IMAGE]${NC} $1"
}

# Create image directory
mkdir -p "$IMAGE_DIR"

IMAGE_PATH="$IMAGE_DIR/$IMAGE_NAME"

log "Creating UEFI disk image: $IMAGE_PATH ($IMAGE_SIZE)"

# Detect OS
if [[ "$OSTYPE" == "darwin"* ]]; then
    # macOS
    log "Detected macOS"
    
    # Create empty disk image
    dd if=/dev/zero of="$IMAGE_PATH" bs=1m count=100 2>/dev/null
    
    # Attach the disk image
    DISK=$(hdiutil attach -nomount "$IMAGE_PATH" | head -1 | awk '{print $1}')
    
    if [ -z "$DISK" ]; then
        log "Failed to attach disk image"
        exit 1
    fi
    
    log "Attached disk image as $DISK"
    
    # Create GPT partition table with EFI partition
    diskutil partitionDisk "$DISK" GPT FAT32 EFI 100M 2>/dev/null || {
        log "Using fallback partition method..."
        diskutil eraseDisk GPT VibOS "$DISK"
    }
    
    # Get EFI partition
    EFI_PART="${DISK}s1"
    
    log "EFI partition: $EFI_PART"
    
    # Mount EFI partition
    EFI_MOUNT=$(mktemp -d)
    mount -t msdos "$EFI_PART" "$EFI_MOUNT" 2>/dev/null || {
        diskutil mount "$EFI_PART"
        EFI_MOUNT="/Volumes/EFI"
    }
    
    log "EFI mounted at $EFI_MOUNT"
    
    # Create EFI boot structure
    mkdir -p "$EFI_MOUNT/EFI/BOOT"
    
    # Copy kernel as EFI application
    if [ -f "$BUILD_DIR/kernel/vibos-x86_64.elf" ]; then
        cp "$BUILD_DIR/kernel/vibos-x86_64.elf" "$EFI_MOUNT/EFI/BOOT/BOOTX64.EFI"
        log "Copied kernel as BOOTX64.EFI"
    else
        log "Kernel not found, creating placeholder..."
        echo "Vib-OS x86_64 kernel placeholder" > "$EFI_MOUNT/EFI/BOOT/README.txt"
    fi
    
    # Sync and unmount
    sync
    umount "$EFI_MOUNT" 2>/dev/null || diskutil unmount "$EFI_PART" 2>/dev/null || true
    
    # Detach disk image
    hdiutil detach "$DISK" 2>/dev/null || hdiutil detach "$DISK" -force
    
else
    # Linux
    log "Detected Linux"
    
    # Create empty disk image
    dd if=/dev/zero of="$IMAGE_PATH" bs=1M count=100 2>/dev/null
    
    # Create partition table
    parted -s "$IMAGE_PATH" mklabel gpt
    parted -s "$IMAGE_PATH" mkpart ESP fat32 1MiB 100%
    parted -s "$IMAGE_PATH" set 1 esp on
    
    # Set up loop device
    LOOP_DEV=$(losetup -f)
    losetup -P "$LOOP_DEV" "$IMAGE_PATH"
    
    # Format EFI partition
    mkfs.fat -F32 "${LOOP_DEV}p1"
    
    # Mount EFI partition
    EFI_MOUNT=$(mktemp -d)
    mount "${LOOP_DEV}p1" "$EFI_MOUNT"
    
    log "EFI mounted at $EFI_MOUNT"
    
    # Create EFI boot structure
    mkdir -p "$EFI_MOUNT/EFI/BOOT"
    
    # Copy kernel as EFI application
    if [ -f "$BUILD_DIR/kernel/vibos-x86_64.elf" ]; then
        cp "$BUILD_DIR/kernel/vibos-x86_64.elf" "$EFI_MOUNT/EFI/BOOT/BOOTX64.EFI"
        log "Copied kernel as BOOTX64.EFI"
    else
        log "Kernel not found, creating placeholder..."
        echo "Vib-OS x86_64 kernel placeholder" > "$EFI_MOUNT/EFI/BOOT/README.txt"
    fi
    
    # Sync and unmount
    sync
    umount "$EFI_MOUNT"
    losetup -d "$LOOP_DEV"
fi

log "UEFI boot image created successfully: $IMAGE_PATH"
ls -lh "$IMAGE_PATH"

echo ""
log "To test in QEMU: make ARCH=x86_64 qemu"
log "To write to USB: sudo dd if=$IMAGE_PATH of=/dev/sdX bs=4M status=progress"
