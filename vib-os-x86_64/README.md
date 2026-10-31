# UEFI Demo OS

A bare-metal x86_64 OS that boots from UEFI and shows a graphical desktop. 

## Current Status

**What works:**
- Boots on real hardware and QEMU
- Framebuffer graphics
- GUI with windows and desktop
- JPEG wallpapers
- PS/2/USB keyboard and mouse input on tested hardware
- AHCI SATA disk discovery, GPT and FAT32 storage support

## Building

### Prerequisites

**macOS (Homebrew):**
```bash
brew install llvm xorriso qemu
```

**Linux (apt):**
```bash
sudo apt install clang lld xorriso qemu-system-x86
```

### Build

Using Make:
```bash
make
```

Or the shell script (handles Limine download):
```bash
./build.sh
```

This creates `uefi-demo.iso`.

### First build notes

First build downloads Limine 8.6.0 (~2MB). Takes a minute.

## Running

### QEMU (UEFI mode)

```bash
make run
```

Or manually:
```bash
qemu-system-x86_64 -M q35 -m 512M -cdrom uefi-demo.iso \
    -bios /opt/homebrew/share/qemu/edk2-x86_64-code.fd -serial stdio
```

Linux OVMF path is usually `/usr/share/OVMF/OVMF_CODE.fd`

### QEMU (BIOS fallback)

```bash
make run-bios
```

### Real Hardware (USB)

**Find your USB device first** (don't nuke your main drive):
```bash
# macOS
diskutil list

# Linux  
lsblk
```

Flash it:
```bash
# macOS (unmount first)
diskutil unmountDisk /dev/diskN
sudo dd if=uefi-demo.iso of=/dev/rdiskN bs=4m status=progress

# Linux
sudo dd if=uefi-demo.iso of=/dev/sdX bs=4M status=progress
```

Boot from USB in UEFI mode. Legacy/CSM boot should work too.

## Installing to a SATA disk

On the live ISO, open **Installer** from the dock. Select a detected disk, review the target and layout, then check the acknowledgement box and click **Erase & install** to begin. Browsing and review do not write to the disk.

**The install erases and repartitions the entire selected disk.** It creates one GPT EFI System Partition, formats it as FAT32, and copies the UEFI fallback loader, installed kernel, and installed Limine configuration. This is not a secure erase. Back up anything you need and verify the disk model/capacity before confirming. Only AHCI SATA disks are supported; installation creates a UEFI-bootable system and does not install a legacy BIOS boot path.

The ISO carries separate live and installed kernels. The installed kernel does not include the GUI installer, the terminal `install`/`format` commands, or installer payload loading; the installer remains available on the live ISO for later reinstalls.

## Project Structure

```
kernel/
├── boot/         # Limine entry point
├── drivers/      # framebuffer, PS/2, USB, PCI, ACPI, AHCI
├── fs/           # VFS, GPT, FAT32/LFN, live installer
├── gui/          # compositor, desktop apps and live installer wizard
├── media/        # jpeg decoder, wallpapers
├── mm/           # memory allocation
└── include/      # headers

limine.conf       # live bootloader config and installer payloads
limine-installed.conf # installed-system config (no installer payloads)
Makefile          # main build
build.sh          # alternative build script
```

## Known Issues

1. **SATA/AHCI only** - NVMe, USB mass-storage installation targets, and non-512-byte logical sectors are not supported by the installer.

2. **UEFI installation only** - the ISO also supports legacy boot, but installed disks are not configured for legacy BIOS.

3. **Build requires LLVM** - cross-compilation with x86_64-elf-gcc works but Makefile prefers clang.

## Cleaning

```bash
make clean      # remove build artifacts
make distclean  # also remove downloaded Limine
```
