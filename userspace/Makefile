# UnixOS Userspace Makefile

CC = clang
CFLAGS = -target aarch64-unknown-none-elf \
         -ffreestanding -fno-builtin \
         -nostdlib -nostdinc \
         -I../build/sysroot/usr/include \
         -I../libc/include \
         -O2 -Wall -Wextra

LD = ld.lld
LDFLAGS = -nostdlib -static

BUILD_DIR = ../build/userspace
SYSROOT = ../build/sysroot
LIBC_DIR = ../build/libc

# CRT and libc for linking
CRT0 = $(LIBC_DIR)/crt0.o
LIBC = $(LIBC_DIR)/libc.a

.PHONY: all clean init shell coreutils dirs

all: dirs init shell coreutils

# Ensure build directories exist
dirs:
	@mkdir -p $(BUILD_DIR)/bin
	@mkdir -p $(BUILD_DIR)/sbin

# Init program
init: dirs
	@echo "[BUILD] init"
	@$(CC) $(CFLAGS) -c init/init.c -o $(BUILD_DIR)/init.o
	@$(LD) $(LDFLAGS) -o $(BUILD_DIR)/sbin/init $(CRT0) $(BUILD_DIR)/init.o $(LIBC)

# Shell
shell: dirs
	@echo "[BUILD] shell"
	@$(CC) $(CFLAGS) -c shell/shell.c -o $(BUILD_DIR)/shell.o
	@$(LD) $(LDFLAGS) -o $(BUILD_DIR)/bin/sh $(BUILD_DIR)/shell.o

# Coreutils
coreutils: dirs
	@echo "[BUILD] coreutils (placeholder)"
	# Will be built when implemented

clean:
	rm -rf $(BUILD_DIR)
