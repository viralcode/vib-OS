/*
 * UEFI Demo OS - Type Definitions
 * Freestanding type definitions for kernel development
 */

#ifndef _TYPES_H
#define _TYPES_H

/* Standard integer types */
typedef unsigned char uint8_t;
typedef unsigned short uint16_t;
typedef unsigned int uint32_t;
typedef unsigned long long uint64_t;

typedef signed char int8_t;
typedef signed short int16_t;
typedef signed int int32_t;
typedef signed long long int64_t;

/* Size types */
typedef uint64_t size_t;
typedef int64_t ssize_t;
typedef uint64_t uintptr_t;
typedef int64_t intptr_t;

/* Boolean type (using unsigned char for freestanding) */
typedef unsigned char bool;
#define true 1
#define false 0

/* NULL pointer */
#define NULL ((void *)0)

/* Useful macros */
#define UNUSED(x) (void)(x)

/* Higher Half Direct Map offset (set by bootloader) */
extern uint64_t hhdm_offset;

/* Kernel physical/virtual base (from Limine) */
extern uint64_t kernel_phys_base;
extern uint64_t kernel_virt_base;

/* Color type for framebuffer */
typedef uint32_t color_t;

/* Make ARGB color */
#define MAKE_COLOR(r, g, b) (0xFF000000 | ((r) << 16) | ((g) << 8) | (b))
#define MAKE_ARGB(a, r, g, b) (((a) << 24) | ((r) << 16) | ((g) << 8) | (b))

/* Common colors */
#define COLOR_BLACK MAKE_COLOR(0, 0, 0)
#define COLOR_WHITE MAKE_COLOR(255, 255, 255)
#define COLOR_RED MAKE_COLOR(255, 59, 48)
#define COLOR_YELLOW MAKE_COLOR(255, 204, 0)
#define COLOR_GREEN MAKE_COLOR(40, 205, 65)
#define COLOR_BLUE MAKE_COLOR(0, 122, 255)
#define COLOR_GRAY MAKE_COLOR(142, 142, 147)
#define COLOR_DARK_GRAY MAKE_COLOR(44, 44, 46)
#define COLOR_LIGHT_GRAY MAKE_COLOR(209, 209, 214)

/* UI Colors - Modern macOS-like */
#define COLOR_TITLE_BAR MAKE_COLOR(56, 56, 58)
#define COLOR_TITLE_BAR_ACTIVE MAKE_COLOR(80, 80, 82)
#define COLOR_WINDOW_BG MAKE_COLOR(30, 30, 32)
#define COLOR_MENU_BAR MAKE_ARGB(220, 40, 40, 42)
#define COLOR_MENU_TEXT MAKE_COLOR(255, 255, 255)

#endif /* _TYPES_H */
