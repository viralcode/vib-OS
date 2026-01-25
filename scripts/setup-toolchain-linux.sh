#!/bin/bash
# Vib-OS Toolchain Setup Script for Linux (Ubuntu/Debian)
# Installs all dependencies required to build Vib-OS on Linux

set -e

echo "========================================"
echo "Vib-OS Toolchain Setup for Linux"
echo "========================================"
echo ""

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

log_info() {
    echo -e "${GREEN}[INFO]${NC} $1"
}

log_warn() {
    echo -e "${YELLOW}[WARN]${NC} $1"
}

log_error() {
    echo -e "${RED}[ERROR]${NC} $1"
}

# Check if running on Linux
if [[ "$(uname)" != "Linux" ]]; then
    log_error "This script is designed for Linux. Use setup-toolchain.sh for macOS."
    exit 1
fi

# Check for apt package manager (Ubuntu/Debian)
if ! command -v apt &> /dev/null; then
    log_error "This script requires apt package manager (Ubuntu/Debian)"
    log_info "For other distributions, please manually install: clang, lld, qemu-system-arm, make"
    exit 1
fi

# Update package list
log_info "Updating package list..."
sudo apt update

# Install LLVM toolchain and cross-compilation tools
log_info "Installing LLVM toolchain and cross-compilation tools..."
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

# Install ARM64 cross-compiler (optional, LLVM is primary)
log_info "Installing ARM64 GNU toolchain (optional)..."
sudo apt install -y gcc-aarch64-linux-gnu binutils-aarch64-linux-gnu || {
    log_warn "ARM64 GCC not available, will use LLVM (recommended)"
}

# Verify toolchain installation
log_info "Verifying toolchain installation..."
echo ""

echo -n "Clang: "
if clang --version | head -1; then
    echo -e "${GREEN}✓${NC}"
else
    echo -e "${RED}✗${NC}"
    exit 1
fi

echo -n "LLD Linker: "
if ld.lld --version | head -1; then
    echo -e "${GREEN}✓${NC}"
else
    echo -e "${RED}✗${NC}"
    exit 1
fi

echo -n "QEMU ARM64: "
if qemu-system-aarch64 --version | head -1; then
    echo -e "${GREEN}✓${NC}"
else
    echo -e "${RED}✗${NC}"
    exit 1
fi

echo -n "Make: "
if make --version | head -1; then
    echo -e "${GREEN}✓${NC}"
else
    echo -e "${RED}✗${NC}"
    exit 1
fi

# Download and setup musl libc source
MUSL_VERSION="1.2.4"
MUSL_DIR="$(pwd)/libc"
if [ ! -f "$MUSL_DIR/configure" ]; then
    log_info "Downloading musl libc source..."
    mkdir -p "$MUSL_DIR"
    wget -q "https://musl.libc.org/releases/musl-${MUSL_VERSION}.tar.gz" -O /tmp/musl.tar.gz
    tar -xzf /tmp/musl.tar.gz -C "$MUSL_DIR" --strip-components=1
    rm /tmp/musl.tar.gz
    log_info "musl libc source downloaded to $MUSL_DIR"
else
    log_info "musl libc source already present"
fi

# Create toolchain configuration file
log_info "Creating toolchain configuration..."
cat > toolchain.mk << 'EOF'
# Auto-generated toolchain configuration for Linux
# Source this file or include in Makefile

# LLVM paths (Linux system paths)
LLVM_PATH := /usr/bin

# Toolchain binaries
export CC := $(LLVM_PATH)/clang
export CXX := $(LLVM_PATH)/clang++
export LD := $(LLVM_PATH)/ld.lld
export AR := $(LLVM_PATH)/llvm-ar
export AS := $(LLVM_PATH)/clang
export OBJCOPY := $(LLVM_PATH)/llvm-objcopy
export OBJDUMP := $(LLVM_PATH)/llvm-objdump
export STRIP := $(LLVM_PATH)/llvm-strip
export NM := $(LLVM_PATH)/llvm-nm
export RANLIB := $(LLVM_PATH)/llvm-ranlib

# Add to PATH
export PATH := $(LLVM_PATH):$(PATH)
EOF

# Create shell configuration for manual builds
cat > toolchain.env << 'EOF'
#!/bin/bash
# Source this file before manual builds
# Usage: source toolchain.env

export LLVM_PATH="/usr/bin"
export PATH="$LLVM_PATH:$PATH"

export CC="$LLVM_PATH/clang"
export CXX="$LLVM_PATH/clang++"
export LD="$LLVM_PATH/ld.lld"
export AR="$LLVM_PATH/llvm-ar"
export AS="$LLVM_PATH/clang"
export OBJCOPY="$LLVM_PATH/llvm-objcopy"
export OBJDUMP="$LLVM_PATH/llvm-objdump"

echo "Toolchain configured for ARM64 cross-compilation on Linux"
EOF

chmod +x toolchain.env

echo ""
echo "========================================"
echo "Toolchain setup complete!"
echo "========================================"
echo ""
echo "Next steps:"
echo "  1. Build OS: make kernel"
echo "  2. Run in QEMU: make run"
echo "  3. Run with GUI: make run-gui"
echo ""
echo "Optional: Source environment for manual builds"
echo "  source toolchain.env"
echo ""
