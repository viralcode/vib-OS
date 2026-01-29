# UEFI Demo OS

A bare-metal x86_64 OS that boots from UEFI and shows a graphical desktop. 

## Current Status

**What works:**
- Boots on real hardware and QEMU
- Framebuffer graphics
- GUI with windows and desktop
- JPEG wallpapers

**What doesn't work:**
- **Keyboard input is broken** - PS/2 and USB HID drivers exist but aren't receiving input properly. IDT/interrupt handling probably needs debugging.

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

## Project Structure

```
kernel/
├── boot/         # Limine entry point
├── drivers/      # framebuffer, ps2, usb, acpi, pci
├── gui/          # window manager, compositor, font rendering
├── media/        # jpeg decoder, wallpapers
├── mm/           # memory allocation
└── include/      # headers

limine.conf       # bootloader config
Makefile          # main build
build.sh          # alternative build script
```

## Known Issues

1. **No keyboard** - the interrupt handler isn't wired up correctly. Check `drivers/idt.c` and `drivers/ps2.c`.

2. **Build requires LLVM** - cross-compilation with x86_64-elf-gcc works but Makefile prefers clang.

## Cleaning

```bash
make clean      # remove build artifacts
make distclean  # also remove downloaded Limine
```
