#!/bin/bash
# Create bootable disk image for UnixOS
# Creates a GPT-partitioned disk image with EFI and root partitions

set -e

BUILD_DIR="${1:-build}"
IMAGE_DIR="${2:-image}"
IMAGE_NAME="unixos.img"
IMAGE_SIZE="1G"

# Colors
GREEN='\033[0;32m'
NC='\033[0m'

log() {
    echo -e "${GREEN}[IMAGE]${NC} $1"
}

# Create image directory
mkdir -p "$IMAGE_DIR"

IMAGE_PATH="$IMAGE_DIR/$IMAGE_NAME"

log "Creating disk image: $IMAGE_PATH ($IMAGE_SIZE)"

# Create empty disk image
dd if=/dev/zero of="$IMAGE_PATH" bs=1m count=1024 2>/dev/null

log "Creating GPT partition table..."

# On macOS, we need to use different tools
# Create a GPT partition table with EFI and root partitions

# Attach the disk image
DISK=$(hdiutil attach -nomount "$IMAGE_PATH" | head -1 | awk '{print $1}')

if [ -z "$DISK" ]; then
    log "Failed to attach disk image"
    exit 1
fi

log "Attached disk image as $DISK"

# Partition the disk
# Partition 1: EFI System Partition (500MB, FAT32)
# Partition 2: Root partition (remaining space, ext4-like but we'll use HFS+ for macOS compatibility during dev)

diskutil partitionDisk "$DISK" GPT \
    FAT32 EFI 500M \
    "Free Space" ROOT R \
    2>/dev/null || {
    # Fallback: create simpler structure
    log "Using fallback partition method..."
    diskutil eraseDisk GPT UnixOS "$DISK"
}

# Get partition identifiers
EFI_PART="${DISK}s1"
ROOT_PART="${DISK}s2"

log "EFI partition: $EFI_PART"
log "Root partition: $ROOT_PART"

# Mount EFI partition
EFI_MOUNT=$(mktemp -d)
mount -t msdos "$EFI_PART" "$EFI_MOUNT" 2>/dev/null || {
    log "Could not mount EFI partition directly, using diskutil..."
    diskutil mount "$EFI_PART"
    EFI_MOUNT="/Volumes/EFI"
}

log "EFI mounted at $EFI_MOUNT"

# Create EFI boot structure
mkdir -p "$EFI_MOUNT/EFI/BOOT"

# Copy kernel as EFI application (if it exists as EFI stub)
if [ -f "$BUILD_DIR/kernel/unixos.efi" ]; then
    cp "$BUILD_DIR/kernel/unixos.efi" "$EFI_MOUNT/EFI/BOOT/BOOTAA64.EFI"
    log "Copied kernel EFI stub"
elif [ -f "$BUILD_DIR/kernel/unixos.elf" ]; then
    # Create a simple boot configuration
    log "Creating boot configuration..."
    cat > "$EFI_MOUNT/EFI/BOOT/startup.nsh" << 'EOF'
@echo -off
echo UnixOS Boot Loader
echo Loading kernel...
\EFI\BOOT\kernel.elf
EOF
    cp "$BUILD_DIR/kernel/unixos.elf" "$EFI_MOUNT/EFI/BOOT/kernel.elf" 2>/dev/null || {
        log "Kernel not yet built, creating placeholder..."
        echo "UnixOS kernel placeholder" > "$EFI_MOUNT/EFI/BOOT/kernel.txt"
    }
else
    log "Kernel not yet built, creating boot structure only..."
    echo "UnixOS - Kernel not yet built" > "$EFI_MOUNT/EFI/BOOT/README.txt"
fi

# Create a simple boot info file
cat > "$EFI_MOUNT/EFI/BOOT/boot.json" << EOF
{
    "name": "UnixOS",
    "version": "0.1.0",
    "arch": "arm64",
    "kernel": "kernel.elf",
    "cmdline": "console=serial0 root=/dev/nvme0n1p2"
}
EOF

# Sync and unmount
sync

log "Unmounting partitions..."
umount "$EFI_MOUNT" 2>/dev/null || diskutil unmount "$EFI_PART" 2>/dev/null || true

# Detach disk image
hdiutil detach "$DISK" 2>/dev/null || {
    log "Disk may be in use, force detaching..."
    hdiutil detach "$DISK" -force
}

log "Boot image created successfully: $IMAGE_PATH"

# Show image info
ls -lh "$IMAGE_PATH"

echo ""
log "To test in QEMU: make qemu"
log "To write to USB: sudo dd if=$IMAGE_PATH of=/dev/diskX bs=4m"
