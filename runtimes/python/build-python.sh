#!/bin/bash
# ============================================================================
# vib-OS - CPython 3.12 Cross-Compilation Script
# ============================================================================
# Cross-compiles Python 3.12 for ARM64 vib-OS with musl libc.
#
# Prerequisites:
# - LLVM/Clang toolchain
# - musl libc built and installed
# - Host Python 3.12 for bootstrapping
#
# ============================================================================

set -e

PYTHON_VERSION="3.12.1"
PYTHON_URL="https://www.python.org/ftp/python/${PYTHON_VERSION}/Python-${PYTHON_VERSION}.tar.xz"
BUILD_DIR="$(pwd)/build"
SYSROOT="$(pwd)/../sysroot"
INSTALL_PREFIX="${SYSROOT}/usr"

# Toolchain
export CC="/opt/homebrew/opt/llvm/bin/clang"
export CXX="/opt/homebrew/opt/llvm/bin/clang++"
export AR="/opt/homebrew/opt/llvm/bin/llvm-ar"
export RANLIB="/opt/homebrew/opt/llvm/bin/llvm-ranlib"
export STRIP="/opt/homebrew/opt/llvm/bin/llvm-strip"

# Target flags
TARGET="aarch64-linux-musl"
CFLAGS="-target ${TARGET} --sysroot=${SYSROOT} -O2 -g -fPIC"
CXXFLAGS="${CFLAGS}"
LDFLAGS="-target ${TARGET} --sysroot=${SYSROOT} -L${SYSROOT}/lib"

export CFLAGS CXXFLAGS LDFLAGS

echo "============================================"
echo "vib-OS CPython 3.12 Cross-Compilation"
echo "============================================"
echo "Version: ${PYTHON_VERSION}"
echo "Target: ${TARGET}"
echo "Sysroot: ${SYSROOT}"
echo "============================================"

# Create directories
mkdir -p "${BUILD_DIR}"
mkdir -p "${INSTALL_PREFIX}"

cd "${BUILD_DIR}"

# Download Python if not present
if [ ! -f "Python-${PYTHON_VERSION}.tar.xz" ]; then
    echo "[DOWNLOAD] Downloading Python ${PYTHON_VERSION}..."
    curl -LO "${PYTHON_URL}"
fi

# Extract
if [ ! -d "Python-${PYTHON_VERSION}" ]; then
    echo "[EXTRACT] Extracting Python..."
    tar xJf "Python-${PYTHON_VERSION}.tar.xz"
fi

cd "Python-${PYTHON_VERSION}"

# Build host Python first for bootstrapping
if [ ! -f "../hostpython/python" ]; then
    echo "[BUILD] Building host Python for bootstrapping..."
    mkdir -p ../hostpython
    ./configure --prefix=$(pwd)/../hostpython
    make -j$(sysctl -n hw.ncpu) python
    cp python ../hostpython/
    make distclean
fi

echo "[CONFIGURE] Configuring cross-compiled Python..."

# Configure for cross-compilation
./configure \
    --host=${TARGET} \
    --build=x86_64-apple-darwin \
    --prefix="${INSTALL_PREFIX}" \
    --enable-shared \
    --with-system-ffi \
    --with-openssl="${SYSROOT}/usr" \
    --disable-ipv6 \
    --without-ensurepip \
    ac_cv_file__dev_ptmx=no \
    ac_cv_file__dev_ptc=no \
    PYTHON_FOR_BUILD="../hostpython/python"

echo "[BUILD] Building Python..."
make -j$(sysctl -n hw.ncpu) HOSTPYTHON=../hostpython/python

echo "[INSTALL] Installing Python..."
make install DESTDIR="${SYSROOT}"

echo ""
echo "============================================"
echo "CPython 3.12 Build Complete!"
echo "============================================"
echo ""
echo "Python installed to: ${INSTALL_PREFIX}"
echo ""
echo "Test with:"
echo "  ${INSTALL_PREFIX}/bin/python3 --version"
echo ""
