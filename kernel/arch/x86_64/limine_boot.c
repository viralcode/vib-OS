/*
 * Vib-OS Limine boot entry for x86_64
 *
 * Based on working-os pattern that boots successfully on real hardware.
 * Uses the Limine Boot Protocol for clean 64-bit entry.
 */

#include "types.h"

/* ========== Limine Structures ========== */

struct limine_framebuffer {
    void *address;
    uint64_t width;
    uint64_t height;
    uint64_t pitch;
    uint16_t bpp;
    uint8_t memory_model;
    uint8_t red_mask_size;
    uint8_t red_mask_shift;
    uint8_t green_mask_size;
    uint8_t green_mask_shift;
    uint8_t blue_mask_size;
    uint8_t blue_mask_shift;
    uint8_t unused[7];
    uint64_t edid_size;
    void *edid;
    uint64_t mode_count;
    void **modes;
};

struct limine_framebuffer_response {
    uint64_t revision;
    uint64_t framebuffer_count;
    struct limine_framebuffer **framebuffers;
};

struct limine_framebuffer_request {
    uint64_t id[4];
    uint64_t revision;
    struct limine_framebuffer_response *response;
};

/* ========== Limine Requests ========== */

/* Place requests in dedicated section - using direct magic values like working-os */
__attribute__((used, section(".limine_requests")))
static volatile uint64_t limine_requests_start_marker[4] = {
    0xf6b8f4b39de7d1ae, 0xfab91a6940fcb9cf,
    0x785c6ed015d3e316, 0x181e920a7852b9d9
};

/* Base revision 2 - like working-os */
__attribute__((used, section(".limine_requests")))
static volatile uint64_t limine_base_revision[3] = {
    0xf9562b2d5c95a6c8, 0x6a7b384944536bdc, 2
};

/* Framebuffer request */
__attribute__((used, section(".limine_requests")))
static volatile struct limine_framebuffer_request framebuffer_request = {
    .id = {0xc7b1dd30df4c8b88, 0x0a82e883a194f07b,
           0x9d5827dcd881dd75, 0xa3148604f6fab11b},
    .revision = 0,
    .response = 0
};

/* Request end marker */
__attribute__((used, section(".limine_requests")))
static volatile uint64_t limine_requests_end_marker[2] = {
    0xadc0e0531bb10d03, 0x9572709f31764c62
};

/* ========== Serial Debug (COM1) ========== */

#define COM1 0x3F8

static inline void outb(uint16_t port, uint8_t val) {
    __asm__ volatile("outb %0, %1" : : "a"(val), "Nd"(port));
}

static inline uint8_t inb(uint16_t port) {
    uint8_t ret;
    __asm__ volatile("inb %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

static void serial_init(void) {
    outb(COM1 + 1, 0x00);
    outb(COM1 + 3, 0x80);
    outb(COM1 + 0, 0x03);
    outb(COM1 + 1, 0x00);
    outb(COM1 + 3, 0x03);
    outb(COM1 + 2, 0xC7);
    outb(COM1 + 4, 0x0B);
}

static void serial_putc(char c) {
    while ((inb(COM1 + 5) & 0x20) == 0);
    outb(COM1, c);
}

static void serial_puts(const char *s) {
    while (*s) {
        if (*s == '\n') serial_putc('\r');
        serial_putc(*s++);
    }
}

static void serial_puthex(uint64_t val) {
    const char *hex = "0123456789ABCDEF";
    serial_puts("0x");
    for (int i = 60; i >= 0; i -= 4) {
        serial_putc(hex[(val >> i) & 0xF]);
    }
}

/* ========== Globals ========== */

static struct limine_framebuffer *g_fb = 0;

/* ========== Framebuffer Info for kernel ========== */

int limine_get_framebuffer(uint32_t **buffer, uint32_t *width,
                           uint32_t *height, uint32_t *pitch) {
    if (!g_fb || !g_fb->address) {
        return -1;
    }
    if (buffer) *buffer = (uint32_t *)g_fb->address;
    if (width) *width = (uint32_t)g_fb->width;
    if (height) *height = (uint32_t)g_fb->height;
    if (pitch) *pitch = (uint32_t)g_fb->pitch;
    return 0;
}

/* ========== Direct Screen Test ========== */

static void draw_test_pattern(void *fb_addr, uint64_t width, uint64_t height, uint64_t pitch) {
    volatile uint8_t *fb = (volatile uint8_t *)fb_addr;

    serial_puts("Drawing test pattern...\n");

    /* Fill entire screen with a gradient - direct pixel writes */
    for (uint64_t y = 0; y < height; y++) {
        volatile uint32_t *row = (volatile uint32_t *)(fb + y * pitch);
        for (uint64_t x = 0; x < width; x++) {
            /* Create a nice gradient: purple to blue */
            uint8_t r = 50;
            uint8_t g = (y * 50 / height) + 20;
            uint8_t b = 100 + (y * 100 / height);
            row[x] = 0xFF000000 | (r << 16) | (g << 8) | b;
        }
    }

    serial_puts("Test pattern complete!\n");

    /* Draw a white rectangle in center as focus point */
    uint64_t cx = width / 2 - 100;
    uint64_t cy = height / 2 - 50;
    for (uint64_t y = cy; y < cy + 100 && y < height; y++) {
        volatile uint32_t *row = (volatile uint32_t *)(fb + y * pitch);
        for (uint64_t x = cx; x < cx + 200 && x < width; x++) {
            row[x] = 0xFFFFFFFF; /* White */
        }
    }

    /* Draw "VIB-OS" text approximation with colored blocks */
    uint64_t text_y = height / 2 - 20;
    uint64_t text_x = width / 2 - 80;
    volatile uint32_t *text_row = (volatile uint32_t *)(fb + text_y * pitch);

    /* V */
    for (int i = 0; i < 30; i++) text_row[text_x + i] = 0xFF00FF00;
    text_x += 35;
    /* I */
    for (int i = 0; i < 15; i++) text_row[text_x + i] = 0xFF00FF00;
    text_x += 20;
    /* B */
    for (int i = 0; i < 25; i++) text_row[text_x + i] = 0xFF00FF00;
    text_x += 30;
    /* - */
    for (int i = 0; i < 15; i++) text_row[text_x + i] = 0xFFFFFF00;
    text_x += 20;
    /* O */
    for (int i = 0; i < 25; i++) text_row[text_x + i] = 0xFF00FFFF;
    text_x += 30;
    /* S */
    for (int i = 0; i < 20; i++) text_row[text_x + i] = 0xFF00FFFF;
}

/* ========== Halt ========== */

static void halt(void) {
    for (;;) {
        __asm__ volatile("hlt");
    }
}

/* ========== Kernel Main Declaration ========== */

extern void kernel_main(void *dtb);
extern char __bss_start[];
extern char __bss_end[];

/* ========== Entry Point ========== */

void _start(void) {
    /* Initialize serial for debug output */
    serial_init();
    serial_puts("\n\n=== Vib-OS ===\n");
    serial_puts("Kernel entry point reached!\n");

    /* Clear BSS */
    for (char *p = __bss_start; p < __bss_end; ++p) {
        *p = 0;
    }

    /* Verify base revision was accepted */
    if (limine_base_revision[2] != 0) {
        serial_puts("ERROR: Limine base revision mismatch\n");
        serial_puts("Revision value: ");
        serial_puthex(limine_base_revision[2]);
        serial_puts("\n");
        halt();
    }
    serial_puts("Limine base revision OK\n");

    /* Get framebuffer */
    if (framebuffer_request.response == 0) {
        serial_puts("ERROR: No framebuffer response!\n");
        halt();
    }

    if (framebuffer_request.response->framebuffer_count < 1) {
        serial_puts("ERROR: No framebuffers available!\n");
        halt();
    }

    g_fb = framebuffer_request.response->framebuffers[0];

    serial_puts("Framebuffer acquired:\n");
    serial_puts("  Address: ");
    serial_puthex((uint64_t)g_fb->address);
    serial_puts("\n  Width: ");
    serial_puthex(g_fb->width);
    serial_puts("\n  Height: ");
    serial_puthex(g_fb->height);
    serial_puts("\n  Pitch: ");
    serial_puthex(g_fb->pitch);
    serial_puts("\n  BPP: ");
    serial_puthex(g_fb->bpp);
    serial_puts("\n");

    /* Direct screen test to verify framebuffer works */
    serial_puts("Starting direct framebuffer test...\n");
    draw_test_pattern(g_fb->address, g_fb->width, g_fb->height, g_fb->pitch);

    /* Wait a moment to see the test pattern */
    for (volatile int i = 0; i < 100000000; i++) {
        __asm__ volatile("nop");
    }

    serial_puts("Calling kernel_main...\n");

    /* Call kernel main - pass NULL for DTB on x86_64 */
    kernel_main(0);

    /* Should never reach here */
    serial_puts("kernel_main returned!\n");
    halt();
}
