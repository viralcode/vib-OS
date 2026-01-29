#!/bin/bash
# Build script for UEFI Demo OS
# Uses pre-built Limine binaries for macOS compatibility

set -e

# Configuration
LIMINE_VERSION="8.6.0"
BUILD_DIR="build"
ISO_ROOT="iso_root"
ISO_NAME="uefi-demo.iso"

echo "=== UEFI Demo OS Build Script ==="
echo ""

# Create build directories
mkdir -p "$BUILD_DIR"/{boot,lib,drivers,gui}

# Compile kernel
echo "[1/5] Compiling kernel..."

CC="clang"
CFLAGS="-target x86_64-unknown-none-elf -ffreestanding -fno-stack-protector \
        -fno-stack-check -fno-lto -fno-PIC -m64 -march=x86-64 \
        -mno-80387 -mno-mmx -mno-sse -mno-sse2 -mno-red-zone \
        -mcmodel=kernel -Ikernel/include -Wall -O2"

$CC $CFLAGS -c kernel/boot/limine_boot.c -o $BUILD_DIR/boot/limine_boot.o 2>/dev/null
$CC $CFLAGS -c kernel/lib/string.c -o $BUILD_DIR/lib/string.o 2>/dev/null
$CC $CFLAGS -c kernel/drivers/framebuffer.c -o $BUILD_DIR/drivers/framebuffer.o 2>/dev/null
$CC $CFLAGS -c kernel/gui/font.c -o $BUILD_DIR/gui/font.o 2>/dev/null
$CC $CFLAGS -c kernel/gui/desktop.c -o $BUILD_DIR/gui/desktop.o 2>/dev/null
$CC $CFLAGS -c kernel/gui/window.c -o $BUILD_DIR/gui/window.o 2>/dev/null
$CC $CFLAGS -c kernel/gui/compositor.c -o $BUILD_DIR/gui/compositor.o 2>/dev/null

echo "[2/5] Linking kernel..."
ld.lld -nostdlib -static -z max-page-size=0x1000 -T kernel/linker.ld \
    $BUILD_DIR/boot/limine_boot.o \
    $BUILD_DIR/lib/string.o \
    $BUILD_DIR/drivers/framebuffer.o \
    $BUILD_DIR/gui/font.o \
    $BUILD_DIR/gui/desktop.o \
    $BUILD_DIR/gui/window.o \
    $BUILD_DIR/gui/compositor.o \
    -o $BUILD_DIR/kernel.elf

echo "   Kernel: $BUILD_DIR/kernel.elf ($(ls -lh $BUILD_DIR/kernel.elf | awk '{print $5}'))"

# Download pre-built Limine from release tarball
echo "[3/5] Getting Limine bootloader..."
mkdir -p limine-bin
if [ ! -f "limine-bin/BOOTX64.EFI" ]; then
    echo "   Downloading Limine ${LIMINE_VERSION}..."
    # Download the tarball release and extract the pre-built binaries
    curl -sL "https://github.com/limine-bootloader/limine/releases/download/v${LIMINE_VERSION}/limine-${LIMINE_VERSION}.tar.xz" -o limine.tar.xz
    
    mkdir -p limine-extract
    tar -xf limine.tar.xz -C limine-extract --strip-components=1
    rm limine.tar.xz
    
    # The pre-built binaries are in the tarball
    # Copy what we need
    if [ -f "limine-extract/BOOTX64.EFI" ]; then
        cp limine-extract/BOOTX64.EFI limine-bin/
    fi
    if [ -f "limine-extract/limine-bios-cd.bin" ]; then
        cp limine-extract/limine-bios-cd.bin limine-bin/ 
    fi
    if [ -f "limine-extract/limine-uefi-cd.bin" ]; then
        cp limine-extract/limine-uefi-cd.bin limine-bin/
    fi
    
    rm -rf limine-extract
fi

# If no pre-built binaries in tarball, try to build a minimal UEFI stub
if [ ! -f "limine-bin/BOOTX64.EFI" ]; then
    echo "   Pre-built not found, downloading from assets..."
    # Try alternative: get just BOOTX64.EFI directly
    curl -sL "https://raw.githubusercontent.com/limine-bootloader/limine/v${LIMINE_VERSION}-binary-branch/BOOTX64.EFI" -o limine-bin/BOOTX64.EFI || true
fi

# Still no luck? Let's create a simple EFI boot structure
if [ ! -f "limine-bin/BOOTX64.EFI" ]; then
    echo ""
    echo "WARNING: Could not download pre-built Limine UEFI binary."
    echo "You may need to build Limine manually or use QEMU with direct kernel boot."
    echo ""
    echo "For now, creating a QEMU-compatible disk image..."
    
    # Create a simple bootable image for QEMU testing
    mkdir -p "$ISO_ROOT"/boot
    cp $BUILD_DIR/kernel.elf "$ISO_ROOT"/boot/kernel.elf
    cp limine.conf "$ISO_ROOT"/boot/limine.conf
    
    echo ""
    echo "To test in QEMU with direct kernel boot:"
    echo "  qemu-system-x86_64 -M q35 -m 512M -kernel $BUILD_DIR/kernel.elf -serial stdio"
    echo ""
    exit 0
fi

echo "   Limine bootloader ready!"

# Create ISO structure
echo "[4/5] Creating ISO structure..."
rm -rf "$ISO_ROOT"
mkdir -p "$ISO_ROOT"/boot
mkdir -p "$ISO_ROOT"/EFI/BOOT

# Copy kernel
cp $BUILD_DIR/kernel.elf "$ISO_ROOT"/boot/kernel.elf

# Copy Limine config to multiple locations (Limine searches several paths)
cp limine.conf "$ISO_ROOT"/boot/limine.conf
cp limine.conf "$ISO_ROOT"/limine.conf
mkdir -p "$ISO_ROOT"/limine
cp limine.conf "$ISO_ROOT"/limine/limine.conf

# Copy UEFI bootloader and config together (Limine looks here first!)
cp limine-bin/BOOTX64.EFI "$ISO_ROOT"/EFI/BOOT/BOOTX64.EFI
cp limine.conf "$ISO_ROOT"/EFI/BOOT/limine.conf

# Copy CD boot files if available
[ -f limine-bin/limine-bios-cd.bin ] && cp limine-bin/limine-bios-cd.bin "$ISO_ROOT"/boot/
[ -f limine-bin/limine-uefi-cd.bin ] && cp limine-bin/limine-uefi-cd.bin "$ISO_ROOT"/boot/

# Create ISO
echo "[5/5] Creating bootable ISO..."

# Try with full BIOS+UEFI support first
if [ -f "$ISO_ROOT/boot/limine-bios-cd.bin" ] && [ -f "$ISO_ROOT/boot/limine-uefi-cd.bin" ]; then
    xorriso -as mkisofs \
        -b boot/limine-bios-cd.bin \
        -no-emul-boot -boot-load-size 4 -boot-info-table \
        --efi-boot boot/limine-uefi-cd.bin \
        -efi-boot-part --efi-boot-image --protective-msdos-label \
        "$ISO_ROOT" -o "$ISO_NAME" 2>/dev/null
else
    # UEFI-only ISO
    xorriso -as mkisofs \
        -e EFI/BOOT/BOOTX64.EFI \
        -no-emul-boot \
        -isohybrid-gpt-basdat \
        "$ISO_ROOT" -o "$ISO_NAME" 2>/dev/null || \
    xorriso -as mkisofs \
        "$ISO_ROOT" -o "$ISO_NAME"
fi

echo ""
echo "============================================"
echo "  SUCCESS! Created: $ISO_NAME"
echo "  Size: $(ls -lh $ISO_NAME | awk '{print $5}')"
echo "============================================"
echo ""
echo "To test in QEMU (UEFI):"
echo "  qemu-system-x86_64 -M q35 -m 512M -cdrom $ISO_NAME \\"
echo "    -bios /opt/homebrew/share/qemu/edk2-x86_64-code.fd -serial stdio"
echo ""
echo "To flash to USB (BE CAREFUL!):"
echo "  sudo dd if=$ISO_NAME of=/dev/rdiskN bs=4m status=progress"
