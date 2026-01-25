/*
 * Vib-OS - virtio-gpu Framebuffer Driver
 * 
 * Simple framebuffer using virtio-gpu for QEMU/UTM display.
 */

#include "types.h"
#include "printk.h"
#include "mm/vmm.h"
#include "mm/pmm.h"

/* ===================================================================== */
/* virtio-gpu MMIO Base Addresses */
/* ===================================================================== */

/* QEMU virt machine virtio MMIO base (can have multiple devices) */
#define VIRTIO_MMIO_BASE        0x0A000000UL
#define VIRTIO_MMIO_SIZE        0x200

/* virtio magic value */
#define VIRTIO_MAGIC            0x74726976  /* "virt" */

/* Device types */
#define VIRTIO_DEV_NET          1
#define VIRTIO_DEV_BLK          2
#define VIRTIO_DEV_CONSOLE      3
#define VIRTIO_DEV_GPU          16

/* MMIO Register offsets */
#define VIRTIO_MMIO_MAGIC           0x000
#define VIRTIO_MMIO_VERSION         0x004
#define VIRTIO_MMIO_DEVICE_ID       0x008
#define VIRTIO_MMIO_VENDOR_ID       0x00C
#define VIRTIO_MMIO_DEVICE_FEATURES 0x010
#define VIRTIO_MMIO_DRIVER_FEATURES 0x020
#define VIRTIO_MMIO_QUEUE_SEL       0x030
#define VIRTIO_MMIO_QUEUE_NUM_MAX   0x034
#define VIRTIO_MMIO_QUEUE_NUM       0x038
#define VIRTIO_MMIO_QUEUE_READY     0x044
#define VIRTIO_MMIO_QUEUE_NOTIFY    0x050
#define VIRTIO_MMIO_INTERRUPT_STATUS 0x060
#define VIRTIO_MMIO_INTERRUPT_ACK   0x064
#define VIRTIO_MMIO_STATUS          0x070
#define VIRTIO_MMIO_QUEUE_DESC_LOW  0x080
#define VIRTIO_MMIO_QUEUE_DESC_HIGH 0x084
#define VIRTIO_MMIO_QUEUE_DRIVER_LOW 0x090
#define VIRTIO_MMIO_QUEUE_DRIVER_HIGH 0x094
#define VIRTIO_MMIO_QUEUE_DEVICE_LOW 0x0A0
#define VIRTIO_MMIO_QUEUE_DEVICE_HIGH 0x0A4

/* Device status bits */
#define VIRTIO_STATUS_ACK       1
#define VIRTIO_STATUS_DRIVER    2
#define VIRTIO_STATUS_DRIVER_OK 4
#define VIRTIO_STATUS_FEATURES_OK 8

/* ===================================================================== */
/* Simple Framebuffer (without full virtio-gpu) */
/* ===================================================================== */

/* For QEMU virt with ramfb or simple-framebuffer */
#define SIMPLE_FB_BASE      0x0C000000UL  /* ramfb memory */
#define SIMPLE_FB_WIDTH     1024
#define SIMPLE_FB_HEIGHT    768
#define SIMPLE_FB_BPP       32

static struct {
    uint32_t *buffer;
    uint32_t width;
    uint32_t height;
    uint32_t pitch;
    bool initialized;
} framebuffer = {0};

/* ===================================================================== */
/* Framebuffer Operations */
/* ===================================================================== */

void fb_clear(uint32_t color)
{
    if (!framebuffer.initialized) return;
    
    for (uint32_t y = 0; y < framebuffer.height; y++) {
        for (uint32_t x = 0; x < framebuffer.width; x++) {
            framebuffer.buffer[y * framebuffer.width + x] = color;
        }
    }
}

void fb_put_pixel(int x, int y, uint32_t color)
{
    if (!framebuffer.initialized) return;
    if (x < 0 || x >= (int)framebuffer.width) return;
    if (y < 0 || y >= (int)framebuffer.height) return;
    
    framebuffer.buffer[y * framebuffer.width + x] = color;
}

void fb_fill_rect(int x, int y, int w, int h, uint32_t color)
{
    for (int row = y; row < y + h; row++) {
        for (int col = x; col < x + w; col++) {
            fb_put_pixel(col, row, color);
        }
    }
}

/* Simple 8x8 font for boot messages */
static const uint8_t font_8x8[128][8] = {
    ['A'] = {0x18, 0x3C, 0x66, 0x66, 0x7E, 0x66, 0x66, 0x00},
    ['B'] = {0x7C, 0x66, 0x66, 0x7C, 0x66, 0x66, 0x7C, 0x00},
    ['C'] = {0x3C, 0x66, 0x60, 0x60, 0x60, 0x66, 0x3C, 0x00},
    ['D'] = {0x78, 0x6C, 0x66, 0x66, 0x66, 0x6C, 0x78, 0x00},
    ['E'] = {0x7E, 0x60, 0x60, 0x7C, 0x60, 0x60, 0x7E, 0x00},
    ['F'] = {0x7E, 0x60, 0x60, 0x7C, 0x60, 0x60, 0x60, 0x00},
    ['G'] = {0x3C, 0x66, 0x60, 0x6E, 0x66, 0x66, 0x3C, 0x00},
    ['H'] = {0x66, 0x66, 0x66, 0x7E, 0x66, 0x66, 0x66, 0x00},
    ['I'] = {0x3C, 0x18, 0x18, 0x18, 0x18, 0x18, 0x3C, 0x00},
    ['O'] = {0x3C, 0x66, 0x66, 0x66, 0x66, 0x66, 0x3C, 0x00},
    ['S'] = {0x3C, 0x66, 0x70, 0x3C, 0x0E, 0x66, 0x3C, 0x00},
    ['V'] = {0x66, 0x66, 0x66, 0x66, 0x66, 0x3C, 0x18, 0x00},
    ['-'] = {0x00, 0x00, 0x00, 0x7E, 0x00, 0x00, 0x00, 0x00},
    ['i'] = {0x18, 0x00, 0x38, 0x18, 0x18, 0x18, 0x3C, 0x00},
    ['b'] = {0x60, 0x60, 0x7C, 0x66, 0x66, 0x66, 0x7C, 0x00},
    [' '] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
};

void fb_draw_char(int x, int y, char c, uint32_t fg, uint32_t bg)
{
    if (c < 0 || c > 127) c = ' ';
    
    for (int row = 0; row < 8; row++) {
        uint8_t line = font_8x8[(int)c][row];
        for (int col = 0; col < 8; col++) {
            uint32_t color = (line & (0x80 >> col)) ? fg : bg;
            fb_put_pixel(x + col, y + row, color);
        }
    }
}

void fb_draw_string(int x, int y, const char *str, uint32_t fg, uint32_t bg)
{
    while (*str) {
        fb_draw_char(x, y, *str++, fg, bg);
        x += 8;
    }
}

/* ===================================================================== */
/* Boot Splash Screen */
/* ===================================================================== */

void fb_show_splash(void)
{
    if (!framebuffer.initialized) return;
    
    /* Dark blue background */
    fb_clear(0x1E1E2E);
    
    /* Draw logo area */
    int cx = framebuffer.width / 2;
    int cy = framebuffer.height / 2 - 50;
    
    /* Simple "Vib-OS" text */
    fb_fill_rect(cx - 60, cy - 30, 120, 60, 0x89B4FA);  /* Blue box */
    fb_draw_string(cx - 28, cy - 4, "Vib-OS", 0xFFFFFF, 0x89B4FA);
    
    /* Boot message */
    fb_draw_string(cx - 60, cy + 50, "ARM64 Operating System", 0xCDD6F4, 0x1E1E2E);
    fb_draw_string(cx - 40, cy + 70, "Booting...", 0x808080, 0x1E1E2E);
}

/* ===================================================================== */
/* Initialization */
/* ===================================================================== */

static uint32_t virtio_read32(volatile uint8_t *base, uint32_t offset)
{
    return *(volatile uint32_t *)(base + offset);
}

static void virtio_write32(volatile uint8_t *base, uint32_t offset, uint32_t val)
{
    *(volatile uint32_t *)(base + offset) = val;
}

int fb_init(void)
{
    printk(KERN_INFO "FB: Initializing framebuffer\n");
    
    /* Use static buffer in BSS */
    static uint32_t static_framebuffer[1024 * 768] __attribute__((aligned(4096)));
    
    framebuffer.buffer = static_framebuffer;
    framebuffer.width = SIMPLE_FB_WIDTH;
    framebuffer.height = SIMPLE_FB_HEIGHT;
    framebuffer.pitch = SIMPLE_FB_WIDTH * 4;
    framebuffer.initialized = true;
    
    printk(KERN_INFO "FB: Framebuffer %ux%u at 0x%lx\n",
           framebuffer.width, framebuffer.height, (unsigned long)framebuffer.buffer);
    
    /* Clear to dark blue */
    fb_clear(0x1E1E2E);
    
    /* Configure QEMU ramfb to display our framebuffer */
    extern int ramfb_init(uint32_t *framebuffer, uint32_t width, uint32_t height);
    if (ramfb_init(framebuffer.buffer, framebuffer.width, framebuffer.height) == 0) {
        printk(KERN_INFO "FB: QEMU ramfb display connected\n");
    } else {
        printk(KERN_WARNING "FB: ramfb not available, display may not work\n");
    }
    
    /* Show boot splash */
    fb_show_splash();
    
    printk(KERN_INFO "FB: Initialization complete\n");
    
    return 0;
}

/* Get framebuffer info */
void fb_get_info(uint32_t **buffer, uint32_t *width, uint32_t *height)
{
    if (buffer) *buffer = framebuffer.buffer;
    if (width) *width = framebuffer.width;
    if (height) *height = framebuffer.height;
}
