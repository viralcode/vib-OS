#!/bin/bash
# ============================================================================
# vib-OS - Node.js 20 LTS Cross-Compilation Script
# ============================================================================
# Cross-compiles Node.js 20 for ARM64 vib-OS.
#
# Prerequisites:
# - LLVM/Clang toolchain
# - ICU, OpenSSL, zlib built for target
#
# ============================================================================

set -e

NODE_VERSION="20.10.0"
NODE_URL="https://nodejs.org/dist/v${NODE_VERSION}/node-v${NODE_VERSION}.tar.xz"
BUILD_DIR="$(pwd)/build"
SYSROOT="$(pwd)/../sysroot"
INSTALL_PREFIX="${SYSROOT}/usr"

# Toolchain
export CC="/opt/homebrew/opt/llvm/bin/clang"
export CXX="/opt/homebrew/opt/llvm/bin/clang++"
export AR="/opt/homebrew/opt/llvm/bin/llvm-ar"
export LINK="${CXX}"

TARGET="aarch64-linux-musl"
CFLAGS="-target ${TARGET} --sysroot=${SYSROOT} -O2 -fPIC"
CXXFLAGS="${CFLAGS}"
LDFLAGS="-target ${TARGET} --sysroot=${SYSROOT} -L${SYSROOT}/lib"

export CFLAGS CXXFLAGS LDFLAGS

echo "============================================"
echo "vib-OS Node.js 20 LTS Cross-Compilation"
echo "============================================"
echo "Version: ${NODE_VERSION}"
echo "Target: ${TARGET}"
echo "Sysroot: ${SYSROOT}"
echo "============================================"

# Create directories
mkdir -p "${BUILD_DIR}"
mkdir -p "${INSTALL_PREFIX}"

cd "${BUILD_DIR}"

# Download Node.js if not present
if [ ! -f "node-v${NODE_VERSION}.tar.xz" ]; then
    echo "[DOWNLOAD] Downloading Node.js ${NODE_VERSION}..."
    curl -LO "${NODE_URL}"
fi

# Extract
if [ ! -d "node-v${NODE_VERSION}" ]; then
    echo "[EXTRACT] Extracting Node.js..."
    tar xJf "node-v${NODE_VERSION}.tar.xz"
fi

cd "node-v${NODE_VERSION}"

echo "[CONFIGURE] Configuring Node.js..."

# Configure for cross-compilation
./configure \
    --prefix="${INSTALL_PREFIX}" \
    --dest-cpu=arm64 \
    --dest-os=linux \
    --cross-compiling \
    --with-arm-float-abi=hard \
    --with-intl=small-icu \
    --without-npm \
    --partly-static \
    --shared-openssl \
    --shared-openssl-includes="${SYSROOT}/usr/include" \
    --shared-openssl-libpath="${SYSROOT}/usr/lib" \
    --shared-zlib \
    --shared-zlib-includes="${SYSROOT}/usr/include" \
    --shared-zlib-libpath="${SYSROOT}/usr/lib"

echo "[BUILD] Building Node.js (this will take a while)..."
make -j$(sysctl -n hw.ncpu)

echo "[INSTALL] Installing Node.js..."
make install DESTDIR="${SYSROOT}"

echo ""
echo "============================================"
echo "Node.js 20 Build Complete!"
echo "============================================"
echo ""
echo "Node.js installed to: ${INSTALL_PREFIX}"
echo ""
echo "Test with:"
echo "  ${INSTALL_PREFIX}/bin/node --version"
echo ""
