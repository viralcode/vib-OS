#!/bin/bash
#
# Vib-OS - Create FAT32 Disk Image
#
# Creates a RAW FAT32 disk image (no partition table) for persistent storage.
# Usage: ./create-disk-image.sh [size_mb] [output_path]
#

set -e

SIZE_MB=${1:-64}
OUTPUT_PATH=${2:-image/disk.img}

echo "==================================="
echo "Vib-OS Disk Image Creator"
echo "==================================="
echo "Size: ${SIZE_MB}MB"
echo "Output: ${OUTPUT_PATH}"
echo ""

# Create output directory if needed
mkdir -p "$(dirname "$OUTPUT_PATH")"

# Check if image already exists
if [ -f "$OUTPUT_PATH" ]; then
    echo "Disk image already exists: $OUTPUT_PATH"
    if [ -t 0 ]; then
        read -p "Overwrite? [y/N] " -n 1 -r
        echo
        if [[ ! $REPLY =~ ^[Yy]$ ]]; then
            echo "Aborted."
            exit 0
        fi
    fi
    rm -f "$OUTPUT_PATH"
fi

echo "Creating ${SIZE_MB}MB disk image..."
dd if=/dev/zero of="$OUTPUT_PATH" bs=1M count="$SIZE_MB" 2>/dev/null

# Format as raw FAT32 (no partition table) using mtools
OS=$(uname -s)

# Try mtools first (works on both macOS and Linux, creates raw FAT32)
if command -v mformat &> /dev/null; then
    echo "Formatting as raw FAT32 with mtools..."
    mformat -F -v VIBOS -i "$OUTPUT_PATH" ::
    echo "FAT32 filesystem created"
elif [ "$OS" = "Linux" ] && command -v mkfs.vfat &> /dev/null; then
    echo "Formatting as FAT32 with mkfs.vfat..."
    mkfs.vfat -F 32 -n VIBOS "$OUTPUT_PATH"
    echo "FAT32 filesystem created"
else
    echo "Error: No FAT32 formatting tool found!"
    echo ""
    echo "Install mtools:"
    if [ "$OS" = "Darwin" ]; then
        echo "  brew install mtools"
    else
        echo "  sudo apt install mtools"
    fi
    exit 1
fi

# Create sample files if mtools is available
if command -v mcopy &> /dev/null; then
    echo ""
    echo "Adding sample files..."
    
    # Create temp directory with sample content
    TMPDIR=$(mktemp -d)
    echo "Welcome to Vib-OS!" > "$TMPDIR/readme.txt"
    echo "This file is stored on a persistent FAT32 disk." >> "$TMPDIR/readme.txt"
    echo "Data will survive reboots!" >> "$TMPDIR/readme.txt"
    
    # Copy to disk image
    mcopy -i "$OUTPUT_PATH" "$TMPDIR/readme.txt" ::/README.TXT 2>/dev/null || true
    mmd -i "$OUTPUT_PATH" ::/DOCUMENTS 2>/dev/null || true
    
    rm -rf "$TMPDIR"
    echo "Sample files added"
    
    # Show contents
    echo ""
    echo "Disk contents:"
    mdir -i "$OUTPUT_PATH" ::
fi

echo ""
echo "==================================="
echo "Disk image created successfully!"
echo "==================================="
echo ""
echo "To use with QEMU:"
echo "  make run-disk"
echo ""
