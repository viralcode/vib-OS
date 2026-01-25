# DOOM for Vib-OS - Makefile
# Builds doomgeneric as a Vib-OS userspace program

# Toolchain - Use LLVM/Clang like the kernel build
LLVM_PATH := /opt/homebrew/opt/llvm/bin
BREW_PATH := /opt/homebrew/bin

CC := $(LLVM_PATH)/clang
AS := $(LLVM_PATH)/clang
LD := $(BREW_PATH)/ld.lld
OBJCOPY := $(LLVM_PATH)/llvm-objcopy

# Directories (updated for Vib-OS structure)
DOOM_SRC = doomgeneric/doomgeneric
USER_LIB = ../../lib
BUILD_DIR = build

# Output
TARGET = $(BUILD_DIR)/doom

# Compiler flags for ARM64 freestanding
CFLAGS = --target=aarch64-unknown-none-elf -ffreestanding -nostdlib -nostdinc \
         -mcpu=cortex-a72 -fPIE -O0 \
         -Wall -Wno-unused-variable -Wno-unused-function \
         -Wno-unused-but-set-variable -Wno-sometimes-uninitialized \
         -Wno-error -w \
         -Iinclude -I$(DOOM_SRC) -I$(USER_LIB) -I. \
         -include doom_libc.h

# Assembler flags
ASFLAGS = --target=aarch64-unknown-none-elf -mcpu=cortex-a72

# Linker flags (fixed path)
LDFLAGS = -pie -T ../../linker.ld

# Files to exclude (platform-specific and sound)
EXCLUDE_FILES = \
    doomgeneric_sdl.c \
    doomgeneric_win.c \
    doomgeneric_xlib.c \
    doomgeneric_allegro.c \
    doomgeneric_emscripten.c \
    doomgeneric_linuxvt.c \
    doomgeneric_soso.c \
    doomgeneric_sosox.c \
    i_sdlmusic.c \
    i_sdlsound.c \
    i_sound.c \
    i_allegromusic.c \
    i_allegrosound.c \
    gusconf.c \
    mus2mid.c \
    memio.c \
    icon.c \
    i_cdmus.c \
    i_joystick.c

# Get all doomgeneric source files
ALL_DOOM_SRCS = $(wildcard $(DOOM_SRC)/*.c)

# Filter out excluded files
DOOM_SRCS = $(filter-out $(addprefix $(DOOM_SRC)/,$(EXCLUDE_FILES)),$(ALL_DOOM_SRCS))

# Object files
DOOM_OBJS = $(patsubst $(DOOM_SRC)/%.c,$(BUILD_DIR)/doom_%.o,$(DOOM_SRCS))

# Local source files
LOCAL_SRCS = doomgeneric_vibeos.c doom_libc.c
LOCAL_OBJS = $(patsubst %.c,$(BUILD_DIR)/%.o,$(LOCAL_SRCS))

# CRT object
CRT_OBJ = $(BUILD_DIR)/crt0.o

# All objects
ALL_OBJS = $(CRT_OBJ) $(LOCAL_OBJS) $(DOOM_OBJS)

.PHONY: all clean

all: $(TARGET)
	@echo "DOOM built successfully: $(TARGET)"

$(TARGET): $(ALL_OBJS) | $(BUILD_DIR)
	@echo "Linking $@..."
	$(LD) $(LDFLAGS) $^ -o $@

# Compile doomgeneric sources
$(BUILD_DIR)/doom_%.o: $(DOOM_SRC)/%.c | $(BUILD_DIR)
	@echo "CC $<"
	@$(CC) $(CFLAGS) -c $< -o $@

# Compile local sources
$(BUILD_DIR)/%.o: %.c | $(BUILD_DIR)
	@echo "CC $<"
	@$(CC) $(CFLAGS) -c $< -o $@

# Compile CRT
$(BUILD_DIR)/crt0.o: $(USER_LIB)/crt0.S | $(BUILD_DIR)
	@echo "AS $<"
	@$(AS) $(ASFLAGS) -c $< -o $@

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

clean:
	rm -rf $(BUILD_DIR)

# Debug: show what files we're compiling
show-files:
	@echo "DOOM sources ($(words $(DOOM_SRCS)) files):"
	@for f in $(DOOM_SRCS); do echo "  $$f"; done
	@echo ""
	@echo "Excluded files:"
	@for f in $(EXCLUDE_FILES); do echo "  $$f"; done
