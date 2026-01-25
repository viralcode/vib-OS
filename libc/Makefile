# UnixOS libc Makefile

CC = clang
AS = clang
AR = llvm-ar
RANLIB = llvm-ranlib

TARGET = --target=aarch64-linux-musl

CFLAGS = $(TARGET) \
         -ffreestanding \
         -fno-builtin \
         -nostdinc \
         -I./include \
         -O2 -Wall -Wextra

ASFLAGS = $(TARGET) \
          -ffreestanding

BUILD_DIR = ../build/libc
SYSROOT = ../build/sysroot

# Source files
C_SOURCES = src/syscall.c \
            src/string.c \
            src/stdlib.c \
            src/stdio.c \
            src/signal.c \
            src/errno.c

ASM_SOURCES = crt/crt0.S

# Object files
C_OBJECTS = $(patsubst src/%.c,$(BUILD_DIR)/%.o,$(C_SOURCES))
ASM_OBJECTS = $(patsubst crt/%.S,$(BUILD_DIR)/%.o,$(ASM_SOURCES))

OBJECTS = $(C_OBJECTS) $(ASM_OBJECTS)

# Output library
LIBC = $(BUILD_DIR)/libc.a
CRT0 = $(BUILD_DIR)/crt0.o

.PHONY: all clean install

all: $(BUILD_DIR) $(LIBC) $(CRT0)
	@echo "[LIBC] Build complete"

$(BUILD_DIR):
	@mkdir -p $(BUILD_DIR)
	@mkdir -p $(SYSROOT)/usr/lib
	@mkdir -p $(SYSROOT)/usr/include

$(BUILD_DIR)/%.o: src/%.c
	@echo "[CC] $<"
	@$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/%.o: crt/%.S
	@echo "[AS] $<"
	@$(AS) $(ASFLAGS) -c $< -o $@

$(LIBC): $(C_OBJECTS)
	@echo "[AR] $@"
	@$(AR) rcs $@ $^
	@$(RANLIB) $@

$(CRT0): crt/crt0.S | $(BUILD_DIR)
	@echo "[CRT] $@"
	@$(AS) $(ASFLAGS) -c $< -o $@

install: $(LIBC) $(CRT0)
	@echo "[INSTALL] Installing to sysroot"
	@cp $(LIBC) $(SYSROOT)/usr/lib/
	@cp $(CRT0) $(SYSROOT)/usr/lib/
	@cp -r include/* $(SYSROOT)/usr/include/

clean:
	@rm -rf $(BUILD_DIR)
