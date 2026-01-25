#!/bin/bash
# ============================================================================
# Vib-OS - Create Bootable ISO for VirtualBox/VMware
# ============================================================================
# Creates a bootable ISO image that can be used with:
# - VirtualBox (ARM64 or x86_64 emulation)
# - VMware Fusion
# - UTM (macOS)
# - QEMU
#
# Usage:
#   ./create-iso.sh
#
# ============================================================================

set -e

BUILD_DIR="$(pwd)/build"
ISO_DIR="${BUILD_DIR}/iso"
OUTPUT_ISO="${BUILD_DIR}/vib-os.iso"

KERNEL_ELF="${BUILD_DIR}/kernel/unixos.elf"

echo "============================================"
echo "Vib-OS ISO Creator"
echo "============================================"

# Check if kernel exists
if [ ! -f "${KERNEL_ELF}" ]; then
    echo "[ERROR] Kernel not found at ${KERNEL_ELF}"
    echo "Please run 'make kernel' first."
    exit 1
fi

# Create ISO directory structure
echo "[CREATE] Setting up ISO structure..."
rm -rf "${ISO_DIR}"
mkdir -p "${ISO_DIR}/boot/grub"
mkdir -p "${ISO_DIR}/EFI/BOOT"

# Copy kernel
echo "[COPY] Copying kernel..."
cp "${KERNEL_ELF}" "${ISO_DIR}/boot/vib-os.elf"

# Create GRUB configuration
echo "[CONFIG] Creating GRUB configuration..."
cat > "${ISO_DIR}/boot/grub/grub.cfg" << 'EOF'
set timeout=5
set default=0

menuentry "Vib-OS" {
    echo "Loading Vib-OS kernel..."
    multiboot2 /boot/vib-os.elf
    boot
}

menuentry "Vib-OS (Debug Mode)" {
    echo "Loading Vib-OS kernel (debug)..."
    multiboot2 /boot/vib-os.elf debug verbose
    boot
}

menuentry "Vib-OS (Recovery)" {
    echo "Loading Vib-OS recovery..."
    multiboot2 /boot/vib-os.elf single recovery
    boot
}
EOF

# Create EFI boot configuration
echo "[CONFIG] Creating EFI boot configuration..."
cat > "${ISO_DIR}/EFI/BOOT/startup.nsh" << 'EOF'
@echo off
echo Loading Vib-OS...
\boot\vib-os.elf
EOF

# Create ISO using xorriso or mkisofs
echo "[BUILD] Creating ISO image..."

if command -v xorriso &> /dev/null; then
    # Use xorriso (preferred)
    xorriso -as mkisofs \
        -R -J \
        -V "VIB-OS" \
        -o "${OUTPUT_ISO}" \
        "${ISO_DIR}"
elif command -v mkisofs &> /dev/null; then
    # Use mkisofs
    mkisofs -R -J \
        -V "VIB-OS" \
        -o "${OUTPUT_ISO}" \
        "${ISO_DIR}"
elif command -v hdiutil &> /dev/null; then
    # macOS hdiutil
    hdiutil makehybrid -iso -joliet \
        -o "${OUTPUT_ISO}" \
        "${ISO_DIR}"
else
    echo "[ERROR] No ISO creation tool found."
    echo "Install xorriso: brew install xorriso"
    exit 1
fi

echo ""
echo "============================================"
echo "ISO Created Successfully!"
echo "============================================"
echo ""
echo "Output: ${OUTPUT_ISO}"
echo "Size: $(du -h "${OUTPUT_ISO}" | cut -f1)"
echo ""
echo "============================================"
echo "VirtualBox Instructions (ARM64 Host)"
echo "============================================"
echo ""
echo "1. Open VirtualBox"
echo "2. Click 'New' to create a new VM"
echo "3. Settings:"
echo "   - Name: Vib-OS"
echo "   - Type: Other"
echo "   - Version: Other/Unknown (64-bit)"
echo "   - Memory: 2048 MB or more"
echo "   - Hard disk: Skip (use ISO only)"
echo ""
echo "4. Go to Settings > System > Processor"
echo "   - Enable PAE/NX if available"
echo ""
echo "5. Go to Settings > Storage"
echo "   - Click empty disk icon"
echo "   - Click disk icon on right"
echo "   - Choose 'Choose a disk file...'"
echo "   - Select: ${OUTPUT_ISO}"
echo ""
echo "6. Click 'Start' to boot!"
echo ""
echo "============================================"
echo "Alternative: Use UTM on macOS (ARM64 native)"
echo "============================================"
echo ""
echo "1. Download UTM from https://mac.getutm.app/"
echo "2. Create new VM with 'Emulate' option"
echo "3. Architecture: ARM64 (aarch64)"
echo "4. System: QEMU ARM Virtual Machine"
echo "5. Add the ISO as Boot ISO"
echo "6. Start the VM"
echo ""
echo "============================================"
echo "Alternative: QEMU (Recommended for Testing)"
echo "============================================"
echo ""
echo "Run directly without ISO:"
echo "  qemu-system-aarch64 -M virt,gic-version=3 -cpu max -m 4G \\"
echo "      -nographic -kernel build/kernel/unixos.elf"
echo ""
echo "Or boot from ISO:"
echo "  qemu-system-aarch64 -M virt,gic-version=3 -cpu max -m 4G \\"
echo "      -nographic -cdrom ${OUTPUT_ISO}"
echo ""
