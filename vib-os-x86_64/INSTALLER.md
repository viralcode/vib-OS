# x86_64 AHCI, FAT32, and install-image milestone

The standalone Limine x86_64 kernel now has a sector-based block-device registry, a polling AHCI driver, and a read-only FAT32 mount exposed through the existing VFS. FAT32 directories and 8.3 short-name entries are imported into the VFS; file reads follow validated cluster chains. GPT and MBR partition starts are scanned, with superfloppy FAT32 accepted as a fallback.

This is intentionally a first usable slice, not production storage support. FAT32 long filenames, filesystem writes, journaling/recovery, hot-plug, multiple simultaneous AHCI requests, interrupts, USB mass storage, and boot recovery are not implemented. AHCI issues one bounded-polling DMA command at a time. The VFS mount is read-only even though the low-level block API has write/flush hooks.

## Build

From this directory:

```sh
make
```

## Create a UEFI disk image

Build the kernel, then supply a **new regular image-file path**:

```sh
make installer-image OUTPUT=/tmp/vibos-x86_64.img
```

The script creates a 128 MiB GPT image containing one FAT32 EFI System Partition, Limine's UEFI loader, the kernel, and its configuration. It refuses existing output paths and `/dev/*` targets. It uses no loop device, mount, `sudo`, firmware NVRAM changes, or raw-device writer. The output is a bootable development image, not a graphical installer for an existing computer.

Required host utilities for image creation: `sgdisk`, `mkfs.fat`, and mtools (`mmd`, `mcopy`).

## Test

With QEMU and OVMF installed:

```sh
make test-installer
```

The test creates a temporary disk image, boots it with OVMF and QEMU's emulated ICH9 AHCI controller, then requires the kernel to read `BOOT/KERNEL.ELF` through the AHCI block API, FAT32 parser, and VFS. The temporary image and firmware variables are deleted afterward. No host block device is attached.
