# Building Vib-OS on Ubuntu/Linux

This guide helps you build Vib-OS on Ubuntu or other Linux distributions.

## Quick Start

```bash
# Install dependencies (automated)
./scripts/setup-toolchain-linux.sh

# Build kernel
make kernel

# Run in QEMU (terminal mode)
make run

# Run with GUI
make run-gui
```

## Manual Installation

If the automated script doesn't work, install dependencies manually:

```bash
sudo apt update
sudo apt install -y \
    clang \
    lld \
    llvm \
    llvm-runtime \
    libc6-dev \
    build-essential \
    make \
    cmake \
    ninja-build \
    nasm \
    qemu-system-arm \
    qemu-efi-aarch64 \
    ovmf \
    python3 \
    wget \
    curl \
    git \
    dosfstools \
    e2fsprogs \
    xorriso \
    mtools

# Optional: ARM64 GCC toolchain
sudo apt install -y gcc-aarch64-linux-gnu binutils-aarch64-linux-gnu
```

## Verify Installation

```bash
# Check Clang version
clang --version

# Check LLD linker
ld.lld --version

# Check QEMU ARM64
qemu-system-aarch64 --version
```

## Build Commands

```bash
# Build everything
make all

# Build kernel only
make kernel

# Build drivers
make drivers

# Build C library
make libc

# Build userspace programs
make userspace

# Create boot image
make image
```

## Run in QEMU

```bash
# Terminal-only mode
make run

# With GUI display (virtio input)
make run-gui

# With boot image
make qemu

# Debug mode (GDB on port 1234)
make qemu-debug
```

## Troubleshooting

### Missing LLVM tools
If you get "clang: command not found":
```bash
sudo apt install clang lld llvm
```

### QEMU not found
```bash
sudo apt install qemu-system-arm qemu-efi-aarch64
```

### Build errors
Make sure you have all dependencies installed:
```bash
sudo apt install build-essential make cmake nasm
```

### Permission denied on setup script
```bash
chmod +x scripts/setup-toolchain-linux.sh
./scripts/setup-toolchain-linux.sh
```

## Platform Differences

The Makefile now auto-detects your OS:
- **macOS**: Uses Homebrew paths (`/opt/homebrew/opt/llvm/bin`)
- **Linux**: Uses system paths (`/usr/bin`)

You can override toolchain paths via environment variables:
```bash
export LLVM_PATH=/custom/path/llvm/bin
make kernel
```

## CPU Target

The build uses `-mcpu=cortex-a72` which works on:
- QEMU virt machine (default)
- Raspberry Pi 4
- Most ARM64 development boards

For Apple Silicon, use macOS build with `-mcpu=apple-m2` (modify Makefile if needed).

## Next Steps

See [AGENTS.md](AGENTS.md) for detailed development guide.
