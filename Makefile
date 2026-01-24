# Vib-OS Master Makefile
# ARM64 OS for Apple Silicon and Raspberry Pi

# ============================================================================
# Configuration
# ============================================================================

# Target architecture
ARCH := arm64
TARGET := aarch64-elf

# Directories
ROOT_DIR := $(shell pwd)
BUILD_DIR := $(ROOT_DIR)/build
BOOT_DIR := $(ROOT_DIR)/boot
KERNEL_DIR := $(ROOT_DIR)/kernel
DRIVERS_DIR := $(ROOT_DIR)/drivers
LIBC_DIR := $(ROOT_DIR)/libc
USERSPACE_DIR := $(ROOT_DIR)/userspace
RUNTIMES_DIR := $(ROOT_DIR)/runtimes
IMAGE_DIR := $(ROOT_DIR)/image
SYSROOT := $(BUILD_DIR)/sysroot

# Detect OS
UNAME_S := $(shell uname -s)

# Toolchain - Support both macOS (Homebrew) and Linux (system/apt)
ifeq ($(UNAME_S),Darwin)
    # macOS with Homebrew
    LLVM_PATH ?= /opt/homebrew/opt/llvm/bin
    BREW_PATH ?= /opt/homebrew/bin
    export PATH := $(LLVM_PATH):$(BREW_PATH):$(PATH)
    CC := $(LLVM_PATH)/clang
    AS := $(LLVM_PATH)/clang
    LD := $(BREW_PATH)/ld.lld
    AR := $(LLVM_PATH)/llvm-ar
    OBJCOPY := $(LLVM_PATH)/llvm-objcopy
    OBJDUMP := $(LLVM_PATH)/llvm-objdump
else
    # Linux (Ubuntu/Debian/etc.) - use system LLVM or allow override
    LLVM_PATH ?= /usr/bin
    # Check if clang exists, otherwise use it with full path
    ifeq ($(shell which clang 2>/dev/null),)
        $(error "Clang not found! Run: sudo apt install clang lld")
    endif
    CC := clang
    AS := clang
    LD := ld.lld
    AR := llvm-ar
    OBJCOPY := llvm-objcopy
    OBJDUMP := llvm-objdump
endif

# Cross-compilation target
CROSS_TARGET := --target=aarch64-unknown-none-elf

# Compiler flags
# CPU target: generic works on QEMU and most ARM64 hardware
CFLAGS_COMMON := -std=gnu11 -Wall -Wextra -Wno-unused-function -ffreestanding -fstack-protector-strong \
                 -fno-pic -mcpu=cortex-a72 -O2 -g

CFLAGS_KERNEL := $(CFLAGS_COMMON) $(CROSS_TARGET) \
                 -I$(KERNEL_DIR)/include -I$(KERNEL_DIR) \
                 -mgeneral-regs-only \
                 -fno-builtin -nostdlib -nostdinc \
                 -DARCH_ARM64

CFLAGS_USER := -Wall -Wextra -O2 -g \
               --target=aarch64-linux-musl \
               --sysroot=$(SYSROOT)

LDFLAGS_KERNEL := -nostdlib -static -T $(KERNEL_DIR)/linker.ld

# QEMU configuration
QEMU := qemu-system-aarch64
QEMU_MACHINE := virt,gic-version=3
QEMU_CPU := max
QEMU_MEMORY := 4G
QEMU_FLAGS := -M $(QEMU_MACHINE) -cpu $(QEMU_CPU) -m $(QEMU_MEMORY) \
              -nographic -serial mon:stdio \
              -drive if=none,id=hd0,format=raw,file=$(IMAGE_DIR)/unixos.img \
              -device virtio-blk-device,drive=hd0

# ============================================================================
# Main Targets
# ============================================================================

.PHONY: all clean kernel drivers libc userspace runtimes image qemu qemu-debug test help run run-gui run-disk disk-image

all: kernel drivers libc userspace runtimes image
	@echo "=========================================="
	@echo "UnixOS build complete!"
	@echo "=========================================="
	@echo "Boot image: $(IMAGE_DIR)/unixos.img"
	@echo "Run 'make qemu' to test in emulator"

help:
	@echo "UnixOS Build System"
	@echo "==================="
	@echo ""
	@echo "Build targets:"
	@echo "  all          - Build everything"
	@echo "  kernel       - Build kernel only"
	@echo "  drivers      - Build device drivers"
	@echo "  libc         - Build C library"
	@echo "  userspace    - Build userspace programs"
	@echo "  runtimes     - Build Python and Node.js"
	@echo "  image        - Create bootable disk image"
	@echo ""
	@echo "Test targets:"
	@echo "  qemu         - Run in QEMU emulator"
	@echo "  qemu-debug   - Run with GDB server"
	@echo "  run          - Run kernel in QEMU (text mode)"
	@echo "  run-gui      - Run kernel with GUI display"
	@echo "  run-disk     - Run with persistent FAT32 disk storage"
	@echo "  disk-image   - Create FAT32 disk image for persistence"
	@echo "  test         - Run test suite"
	@echo ""
	@echo "Utility targets:"
	@echo "  clean        - Remove build artifacts"
	@echo "  toolchain    - Install build dependencies"

# ============================================================================
# Directory Setup
# ============================================================================

$(BUILD_DIR):
	@mkdir -p $(BUILD_DIR)
	@mkdir -p $(BUILD_DIR)/kernel
	@mkdir -p $(BUILD_DIR)/drivers
	@mkdir -p $(BUILD_DIR)/libc
	@mkdir -p $(BUILD_DIR)/userspace
	@mkdir -p $(BUILD_DIR)/runtimes
	@mkdir -p $(SYSROOT)/usr/lib
	@mkdir -p $(SYSROOT)/usr/include
	@mkdir -p $(SYSROOT)/bin
	@mkdir -p $(SYSROOT)/sbin

$(IMAGE_DIR):
	@mkdir -p $(IMAGE_DIR)

# ============================================================================
# Kernel Build
# ============================================================================

KERNEL_SOURCES := $(shell find $(KERNEL_DIR) -name '*.c' -o -name '*.S' 2>/dev/null | grep -v '/x86_64/' | grep -v '/x86/')
# Also include ARM64-specific assembly
KERNEL_SOURCES += $(shell find $(KERNEL_DIR)/arch/arm64 -name '*.S' 2>/dev/null)
KERNEL_OBJECTS := $(patsubst $(KERNEL_DIR)/%.c,$(BUILD_DIR)/kernel/%.o,$(filter %.c,$(KERNEL_SOURCES)))
KERNEL_OBJECTS += $(patsubst $(KERNEL_DIR)/%.S,$(BUILD_DIR)/kernel/%.o,$(filter %.S,$(KERNEL_SOURCES)))

# Include drivers in the kernel
DRIVER_SOURCES := $(shell find $(DRIVERS_DIR) -name '*.c' 2>/dev/null)
DRIVER_OBJECTS := $(patsubst $(DRIVERS_DIR)/%.c,$(BUILD_DIR)/drivers/%.o,$(DRIVER_SOURCES))

ALL_KERNEL_OBJECTS := $(KERNEL_OBJECTS) $(DRIVER_OBJECTS)
KERNEL_BINARY := $(BUILD_DIR)/kernel/unixos.elf

kernel: $(BUILD_DIR) $(ALL_KERNEL_OBJECTS) $(KERNEL_BINARY)
	@echo "[KERNEL] Build complete: $(KERNEL_BINARY)"

$(BUILD_DIR)/kernel/%.o: $(KERNEL_DIR)/%.c
	@mkdir -p $(dir $@)
	@echo "[CC] $<"
	@# Media files need FP support, compile without -mgeneral-regs-only
	@if echo "$<" | grep -q "/media/"; then \
		$(CC) $(CFLAGS_COMMON) $(CROSS_TARGET) -mcpu=cortex-a72 -I$(KERNEL_DIR)/include -fno-builtin -nostdlib -nostdinc -c $< -o $@; \
	else \
		$(CC) $(CFLAGS_KERNEL) -c $< -o $@; \
	fi

$(BUILD_DIR)/kernel/%.o: $(KERNEL_DIR)/%.S
	@mkdir -p $(dir $@)
	@echo "[AS] $<"
	@$(AS) $(CFLAGS_KERNEL) -c $< -o $@

$(BUILD_DIR)/drivers/%.o: $(DRIVERS_DIR)/%.c
	@mkdir -p $(dir $@)
	@echo "[CC] $<"
	@$(CC) $(CFLAGS_KERNEL) -I$(KERNEL_DIR)/include -c $< -o $@

$(KERNEL_BINARY): $(ALL_KERNEL_OBJECTS)
	@echo "[LD] $@"
	@$(LD) $(LDFLAGS_KERNEL) -o $@ $^

# ============================================================================
# Drivers Build
# ============================================================================

DRIVER_SOURCES := $(shell find $(DRIVERS_DIR) -name '*.c' 2>/dev/null)
DRIVER_OBJECTS := $(patsubst $(DRIVERS_DIR)/%.c,$(BUILD_DIR)/drivers/%.o,$(DRIVER_SOURCES))

drivers: $(BUILD_DIR) $(DRIVER_OBJECTS)
	@echo "[DRIVERS] Build complete"

$(BUILD_DIR)/drivers/%.o: $(DRIVERS_DIR)/%.c
	@mkdir -p $(dir $@)
	@echo "[CC] $<"
	@$(CC) $(CFLAGS_KERNEL) -I$(DRIVERS_DIR)/include -c $< -o $@

# ============================================================================
# C Library Build
# ============================================================================

libc: $(BUILD_DIR)
	@echo "[LIBC] Building musl libc..."
	@if [ -f $(LIBC_DIR)/Makefile ]; then \
		$(MAKE) -C $(LIBC_DIR) DESTDIR=$(SYSROOT) install; \
	else \
		echo "[LIBC] Source not yet configured"; \
	fi

# ============================================================================
# Userspace Build
# ============================================================================

userspace: $(BUILD_DIR) libc
	@echo "[USERSPACE] Building userspace programs..."
	@if [ -f $(USERSPACE_DIR)/Makefile ]; then \
		$(MAKE) -C $(USERSPACE_DIR) SYSROOT=$(SYSROOT); \
	else \
		echo "[USERSPACE] Source not yet configured"; \
	fi

# ============================================================================
# Runtimes Build (Python, Node.js)
# ============================================================================

runtimes: $(BUILD_DIR) libc
	@echo "[RUNTIMES] Building Python and Node.js..."
	@if [ -f $(RUNTIMES_DIR)/Makefile ]; then \
		$(MAKE) -C $(RUNTIMES_DIR) SYSROOT=$(SYSROOT); \
	else \
		echo "[RUNTIMES] Source not yet configured"; \
	fi

# ============================================================================
# Boot Image Creation
# ============================================================================

image: $(IMAGE_DIR) kernel drivers
	@echo "[IMAGE] Creating bootable disk image..."
	@./scripts/create-boot-image.sh $(BUILD_DIR) $(IMAGE_DIR)
	@echo "[IMAGE] Created: $(IMAGE_DIR)/unixos.img"

# ============================================================================
# QEMU Testing
# ============================================================================

qemu: kernel
	@echo "[QEMU] Starting UnixOS in emulator (direct kernel boot)..."
	@$(QEMU) -M virt,gic-version=3 -cpu max -m 4G \
		-nographic \
		-kernel $(BUILD_DIR)/kernel/unixos.elf

qemu-uefi: image
	@echo "[QEMU] Starting UnixOS with UEFI boot..."
	@echo "[QEMU] Note: Requires UEFI firmware (AAVMF)"
	@if [ ! -f /usr/share/qemu-efi-aarch64/QEMU_EFI.fd ]; then \
		echo "[ERROR] UEFI firmware not found. Install qemu-efi-aarch64 package."; \
		echo "[INFO] Using direct kernel boot instead. Run 'make qemu'"; \
		exit 1; \
	fi
	@$(QEMU) -M virt,gic-version=3 -cpu max -m 4G \
		-nographic \
		-drive if=pflash,format=raw,readonly=on,file=/usr/share/qemu-efi-aarch64/QEMU_EFI.fd \
		-drive if=none,id=hd0,format=raw,file=$(IMAGE_DIR)/unixos.img \
		-device virtio-blk-device,drive=hd0

qemu-debug: kernel
	@echo "[QEMU] Starting UnixOS with GDB server on port 1234..."
	@$(QEMU) -M virt,gic-version=3 -cpu max -m 4G \
		-nographic \
		-kernel $(BUILD_DIR)/kernel/unixos.elf \
		-s -S

# ============================================================================
# Testing
# ============================================================================

test: kernel
	@echo "[TEST] Running kernel tests..."
	@./scripts/run-tests.sh

# ============================================================================
# Run in QEMU
# ============================================================================

run: kernel
	@echo "[RUN] Starting Vib-OS in QEMU..."
	@qemu-system-aarch64 -M virt,gic-version=3 -cpu max -m 4G -nographic -kernel $(KERNEL_BINARY)

run-gui: kernel
	@echo "[RUN] Starting Vib-OS with GUI display..."
	@qemu-system-aarch64 -M virt,gic-version=3 \
		-cpu max -m 512M \
		-global virtio-mmio.force-legacy=false \
		-device ramfb \
		-device virtio-keyboard-device \
		-device virtio-tablet-device \
		-device virtio-net-device,netdev=net0 \
		-netdev user,id=net0 \
		-audiodev coreaudio,id=snd0 \
		-device intel-hda -device hda-duplex,audiodev=snd0 \
		-serial stdio \
		-kernel $(KERNEL_BINARY)

run-gpu: kernel
	@echo "[RUN] Starting Vib-OS with virtio-GPU acceleration..."
	@qemu-system-aarch64 -M virt,gic-version=3 \
		-cpu max -m 512M \
		-global virtio-mmio.force-legacy=false \
		-device ramfb \
		-device virtio-gpu-pci \
		-device virtio-keyboard-device \
		-device virtio-tablet-device \
		-device virtio-net-device,netdev=net0 \
		-netdev user,id=net0 \
		-audiodev coreaudio,id=snd0 \
		-device intel-hda -device hda-duplex,audiodev=snd0 \
		-serial stdio \
		-kernel $(KERNEL_BINARY)

# Run with persistent disk storage
run-disk: kernel disk-image
	@echo "[RUN] Starting Vib-OS with persistent disk..."
	@qemu-system-aarch64 -M virt,gic-version=3 \
		-cpu max -m 512M \
		-global virtio-mmio.force-legacy=false \
		-device ramfb \
		-device virtio-keyboard-device \
		-device virtio-tablet-device \
		-device virtio-blk-device,drive=hd0 \
		-drive id=hd0,if=none,format=raw,file=$(IMAGE_DIR)/disk.img \
		-device virtio-net-device,netdev=net0 \
		-netdev user,id=net0 \
		-serial stdio \
		-kernel $(KERNEL_BINARY)

# Create a FAT32 disk image
disk-image: $(IMAGE_DIR)
	@echo "[DISK] Creating FAT32 disk image..."
	@./scripts/create-disk-image.sh 64 $(IMAGE_DIR)/disk.img || \
		(dd if=/dev/zero of=$(IMAGE_DIR)/disk.img bs=1M count=64 2>/dev/null && \
		echo "[DISK] Created raw disk image (format manually if needed)")

# ============================================================================
# Toolchain Setup
# ============================================================================

toolchain:
	@echo "[TOOLCHAIN] Installing build dependencies..."
	@if [ "$(UNAME_S)" = "Darwin" ]; then \
		./scripts/setup-toolchain.sh; \
	else \
		./scripts/setup-toolchain-linux.sh; \
	fi

# ============================================================================
# Clean
# ============================================================================

clean:
	@echo "[CLEAN] Removing build artifacts..."
	@rm -rf $(BUILD_DIR)
	@rm -rf $(IMAGE_DIR)
	@echo "[CLEAN] Done"

distclean: clean
	@echo "[DISTCLEAN] Removing all generated files..."
	@rm -rf $(SYSROOT)
