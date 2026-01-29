#!/bin/bash
# Build Vib-OS for Limine bootloader and create bootable USB
set -e

cd "$(dirname "$0")/.."

echo "=== Building Vib-OS with Limine bootloader ==="

# Build kernel with Limine linker script (x86_64 only)
KERNEL_SOURCES=$(find kernel -name '*.c' \
  | grep -v '^kernel/arch/arm64/' \
  | grep -v '^kernel/arch/x86/')
KERNEL_ASM_SOURCES=$(find kernel -name '*.S' \
  | grep -v '^kernel/arch/arm64/' \
  | grep -v '^kernel/arch/x86/' \
  | grep -v '^kernel/arch/x86_64/boot.S')
DRIVER_SOURCES=$(find drivers -name '*.c' | grep -v '^drivers/uart/uart.c')

# Create build directory
mkdir -p build/limine

# Compiler flags
CC="/opt/homebrew/opt/llvm/bin/clang"
LD="/opt/homebrew/bin/ld.lld"

CFLAGS="--target=x86_64-unknown-none-elf -Wall -ffreestanding -fno-stack-protector -fno-pic -O2 -g -mcmodel=kernel -mno-red-zone -mno-mmx -mno-sse -mno-sse2 -Ikernel/include -Ikernel -Ikernel/arch/x86_64 -Itools/limine -nostdlib -nostdinc -fno-builtin -DARCH_X86_64"

LDFLAGS="-nostdlib -static -z max-page-size=0x1000 -T kernel/linker_x86_64_limine.ld -e _start"

compile_c_source() {
    local src="$1"
    local obj="build/limine/${src%.c}.o"
    mkdir -p "$(dirname "$obj")"
    if [ ! -f "$obj" ] || [ "$src" -nt "$obj" ]; then
        echo "  $src"
        $CC $CFLAGS -c "$src" -o "$obj"
    fi
}

echo "[CC] Compiling kernel sources..."
for src in $KERNEL_SOURCES; do
    compile_c_source "$src"
done

echo "[AS] Compiling kernel assembly sources..."
for src in $KERNEL_ASM_SOURCES; do
    local_obj="build/limine/${src%.S}.o"
    mkdir -p "$(dirname "$local_obj")"
    if [ ! -f "$local_obj" ] || [ "$src" -nt "$local_obj" ]; then
        echo "  $src"
        $CC $CFLAGS -c "$src" -o "$local_obj"
    fi
done

echo "[CC] Compiling driver sources..."
for src in $DRIVER_SOURCES; do
    compile_c_source "$src"
done

echo "[LD] Linking kernel..."
$LD $LDFLAGS -o build/limine/vibos-limine.elf $(find build/limine -name '*.o')

echo "[INFO] Kernel built: build/limine/vibos-limine.elf"
ls -lh build/limine/vibos-limine.elf
