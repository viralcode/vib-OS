#!/bin/bash
# UnixOS Toolchain Setup Script
# Installs all dependencies required to build UnixOS on macOS

set -e

echo "========================================"
echo "UnixOS Toolchain Setup"
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

# Check if running on macOS
if [[ "$(uname)" != "Darwin" ]]; then
    log_error "This script is designed for macOS"
    exit 1
fi

# Check for Homebrew
if ! command -v brew &> /dev/null; then
    log_info "Installing Homebrew..."
    /bin/bash -c "$(curl -fsSL https://raw.githubusercontent.com/Homebrew/install/HEAD/install.sh)"
else
    log_info "Homebrew is already installed"
fi

# Update Homebrew
log_info "Updating Homebrew..."
brew update

# Install LLVM (cross-compiler for ARM64)
log_info "Installing LLVM toolchain..."
brew install llvm

# Add LLVM to PATH for this session
export PATH="/opt/homebrew/opt/llvm/bin:$PATH"

# Install additional build tools
log_info "Installing build tools..."
brew install \
    nasm \
    mtools \
    xorriso \
    qemu \
    make \
    cmake \
    ninja \
    python3 \
    wget \
    curl \
    git \
    dosfstools \
    e2fsprogs

# Install ARM64 cross-compilation tools
log_info "Installing ARM64 GNU toolchain..."
brew install aarch64-elf-gcc aarch64-elf-binutils || {
    log_warn "ARM64 GCC not available via Homebrew, will use LLVM"
}

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
# Auto-generated toolchain configuration
# Source this file or include in Makefile

# LLVM paths (Homebrew on Apple Silicon)
LLVM_PATH := /opt/homebrew/opt/llvm

# Toolchain binaries
export CC := $(LLVM_PATH)/bin/clang
export CXX := $(LLVM_PATH)/bin/clang++
export LD := $(LLVM_PATH)/bin/ld.lld
export AR := $(LLVM_PATH)/bin/llvm-ar
export AS := $(LLVM_PATH)/bin/clang
export OBJCOPY := $(LLVM_PATH)/bin/llvm-objcopy
export OBJDUMP := $(LLVM_PATH)/bin/llvm-objdump
export STRIP := $(LLVM_PATH)/bin/llvm-strip
export NM := $(LLVM_PATH)/bin/llvm-nm
export RANLIB := $(LLVM_PATH)/bin/llvm-ranlib

# Add to PATH
export PATH := $(LLVM_PATH)/bin:$(PATH)
EOF

# Create shell configuration for manual builds
cat > toolchain.env << 'EOF'
#!/bin/bash
# Source this file before manual builds
# Usage: source toolchain.env

export LLVM_PATH="/opt/homebrew/opt/llvm"
export PATH="$LLVM_PATH/bin:$PATH"

export CC="$LLVM_PATH/bin/clang"
export CXX="$LLVM_PATH/bin/clang++"
export LD="$LLVM_PATH/bin/ld.lld"
export AR="$LLVM_PATH/bin/llvm-ar"
export AS="$LLVM_PATH/bin/clang"
export OBJCOPY="$LLVM_PATH/bin/llvm-objcopy"
export OBJDUMP="$LLVM_PATH/bin/llvm-objdump"

echo "Toolchain configured for ARM64 cross-compilation"
EOF

chmod +x toolchain.env

# Verify installation
log_info "Verifying toolchain installation..."
echo ""

echo -n "LLVM/Clang: "
if /opt/homebrew/opt/llvm/bin/clang --version | head -1; then
    echo -e "${GREEN}✓${NC}"
else
    echo -e "${RED}✗${NC}"
fi

echo -n "LLD Linker: "
if /opt/homebrew/opt/llvm/bin/ld.lld --version | head -1; then
    echo -e "${GREEN}✓${NC}"
else
    echo -e "${RED}✗${NC}"
fi

echo -n "QEMU ARM64: "
if qemu-system-aarch64 --version | head -1; then
    echo -e "${GREEN}✓${NC}"
else
    echo -e "${RED}✗${NC}"
fi

echo -n "Make: "
if make --version | head -1; then
    echo -e "${GREEN}✓${NC}"
else
    echo -e "${RED}✗${NC}"
fi

echo ""
echo "========================================"
echo "Toolchain setup complete!"
echo "========================================"
echo ""
echo "Next steps:"
echo "  1. Source the environment: source toolchain.env"
echo "  2. Build the OS: make all"
echo "  3. Test in QEMU: make qemu"
echo ""
