#!/bin/bash
# ============================================================================
# UnixOS - musl libc Cross-Compilation Script
# ============================================================================
# This script cross-compiles musl libc for ARM64 UnixOS.
# musl is a lightweight, standards-compliant C library.
#
# Prerequisites:
# - LLVM/Clang toolchain (installed via Homebrew)
# - Source code from https://musl.libc.org/
#
# Usage:
#   ./build-musl.sh
#
# ============================================================================

set -e

# Configuration
MUSL_VERSION="1.2.4"
MUSL_URL="https://musl.libc.org/releases/musl-${MUSL_VERSION}.tar.gz"
BUILD_DIR="$(pwd)/build"
INSTALL_PREFIX="$(pwd)/../sysroot"

# Toolchain
export CC="/opt/homebrew/opt/llvm/bin/clang"
export AR="/opt/homebrew/opt/llvm/bin/llvm-ar"
export RANLIB="/opt/homebrew/opt/llvm/bin/llvm-ranlib"
export CROSS_COMPILE="aarch64-linux-musl-"

# Target settings
export CFLAGS="-target aarch64-linux-musl -O2 -g -mcpu=apple-m2"
export LDFLAGS="-target aarch64-linux-musl"

echo "============================================"
echo "UnixOS musl libc Cross-Compilation"
echo "============================================"
echo "Version: ${MUSL_VERSION}"
echo "Build Directory: ${BUILD_DIR}"
echo "Install Prefix: ${INSTALL_PREFIX}"
echo "============================================"

# Create directories
mkdir -p "${BUILD_DIR}"
mkdir -p "${INSTALL_PREFIX}"

# Download musl if not present
cd "${BUILD_DIR}"
if [ ! -f "musl-${MUSL_VERSION}.tar.gz" ]; then
    echo "[DOWNLOAD] Downloading musl ${MUSL_VERSION}..."
    curl -LO "${MUSL_URL}"
fi

# Extract
if [ ! -d "musl-${MUSL_VERSION}" ]; then
    echo "[EXTRACT] Extracting musl..."
    tar xzf "musl-${MUSL_VERSION}.tar.gz"
fi

# Build
cd "musl-${MUSL_VERSION}"

echo "[CONFIGURE] Running configure..."
./configure \
    --target=aarch64-linux-musl \
    --prefix="${INSTALL_PREFIX}" \
    --syslibdir="${INSTALL_PREFIX}/lib" \
    --disable-shared \
    CC="${CC}" \
    AR="${AR}" \
    RANLIB="${RANLIB}" \
    CFLAGS="${CFLAGS}"

echo "[BUILD] Building musl..."
make -j$(sysctl -n hw.ncpu)

echo "[INSTALL] Installing musl..."
make install

echo ""
echo "============================================"
echo "musl libc Build Complete!"
echo "============================================"
echo ""
echo "Installed to: ${INSTALL_PREFIX}"
echo ""
echo "Contents:"
ls -la "${INSTALL_PREFIX}/lib/"*.a 2>/dev/null | head -10 || echo "  (no libraries yet)"
ls -la "${INSTALL_PREFIX}/include/" 2>/dev/null | head -10 || echo "  (no headers yet)"
echo ""
echo "To use in UnixOS builds:"
echo "  export CFLAGS=\"-target aarch64-linux-musl --sysroot=${INSTALL_PREFIX}\""
echo ""
