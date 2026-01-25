#!/bin/bash
# Run UnixOS tests

set -e

echo "========================================"
echo "UnixOS Test Suite"
echo "========================================"

# Colors
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m'

PASSED=0
FAILED=0

run_test() {
    local name="$1"
    local cmd="$2"
    
    echo -n "  Testing $name... "
    if eval "$cmd" > /dev/null 2>&1; then
        echo -e "${GREEN}PASS${NC}"
        ((PASSED++))
    else
        echo -e "${RED}FAIL${NC}"
        ((FAILED++))
    fi
}

echo ""
echo "Build Tests"
echo "-----------"

run_test "Makefile exists" "[ -f Makefile ]"
run_test "Kernel directory" "[ -d kernel ]"
run_test "Boot directory" "[ -d boot ]"
run_test "Scripts executable" "[ -x scripts/setup-toolchain.sh ]"

echo ""
echo "Toolchain Tests"
echo "---------------"

run_test "Clang available" "which clang"
run_test "LLD available" "which ld.lld || [ -f /opt/homebrew/opt/llvm/bin/ld.lld ]"
run_test "QEMU ARM64" "which qemu-system-aarch64"

echo ""
echo "Kernel Tests"
echo "------------"

if [ -f build/kernel/unixos.elf ]; then
    run_test "Kernel binary exists" "[ -f build/kernel/unixos.elf ]"
    run_test "Kernel is ARM64" "file build/kernel/unixos.elf | grep -q 'ARM aarch64'"
else
    echo -e "  ${YELLOW}Kernel not yet built, skipping...${NC}"
fi

echo ""
echo "========================================"
echo "Results: ${GREEN}$PASSED passed${NC}, ${RED}$FAILED failed${NC}"
echo "========================================"

if [ $FAILED -gt 0 ]; then
    exit 1
fi
