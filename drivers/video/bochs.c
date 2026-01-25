/*
 * Vib-OS - Simple VGA/Bochs Display Driver
 * 
 * Uses QEMU's bochs-display or std-vga device for graphics output.
 * This is a simpler approach than virtio-gpu.
 */

#include "types.h"
#include "printk.h"

/* ===================================================================== */
/* Bochs VBE (VGA BIOS Extensions) Registers */
/* ===================================================================== */

/* PCI BAR0 for bochs-display: framebuffer (usually at 0x10000000) */
/* PCI BAR2 for bochs-display: MMIO registers */

#define VBE_DISPI_MMIO_BASE     0x10001000UL  /* Bochs VBE MMIO registers */
#define VBE_FRAMEBUFFER_BASE    0x10000000UL  /* Default framebuffer location */

/* VBE register offsets (16-bit registers at 0x500 + index*2) */
#define VBE_DISPI_INDEX_ID          0
#define VBE_DISPI_INDEX_XRES        1
#define VBE_DISPI_INDEX_YRES        2
#define VBE_DISPI_INDEX_BPP         3
#define VBE_DISPI_INDEX_ENABLE      4
#define VBE_DISPI_INDEX_BANK        5
#define VBE_DISPI_INDEX_VIRT_WIDTH  6
#define VBE_DISPI_INDEX_VIRT_HEIGHT 7
#define VBE_DISPI_INDEX_X_OFFSET    8
#define VBE_DISPI_INDEX_Y_OFFSET    9
#define VBE_DISPI_INDEX_VIDEO_MEM   10

/* VBE enable flags */
#define VBE_DISPI_DISABLED      0x00
#define VBE_DISPI_ENABLED       0x01
#define VBE_DISPI_LFB_ENABLED   0x40
#define VBE_DISPI_NOCLEARMEM    0x80

/* VBE ID values */
#define VBE_DISPI_ID0           0xB0C0
#define VBE_DISPI_ID1           0xB0C1
#define VBE_DISPI_ID2           0xB0C2
#define VBE_DISPI_ID3           0xB0C3
#define VBE_DISPI_ID4           0xB0C4
#define VBE_DISPI_ID5           0xB0C5

/* ===================================================================== */
/* Global State */
/* ===================================================================== */

static struct {
    volatile uint16_t *vbe_regs;
    volatile uint32_t *framebuffer;
    uint32_t width;
    uint32_t height;
    uint32_t bpp;
    uint32_t pitch;
    bool initialized;
} bochs_display = {0};

/* ===================================================================== */
/* Register Access */
/* ===================================================================== */

static void vbe_write(uint16_t index, uint16_t value)
{
    if (bochs_display.vbe_regs) {
        /* Bochs MMIO: index at offset 0x500, data at 0x501 */
        volatile uint16_t *idx = (volatile uint16_t *)((uintptr_t)bochs_display.vbe_regs + 0x500);
        volatile uint16_t *data = (volatile uint16_t *)((uintptr_t)bochs_display.vbe_regs + 0x501);
        *idx = index;
        *data = value;
    }
}

static uint16_t vbe_read(uint16_t index)
{
    if (bochs_display.vbe_regs) {
        volatile uint16_t *idx = (volatile uint16_t *)((uintptr_t)bochs_display.vbe_regs + 0x500);
        volatile uint16_t *data = (volatile uint16_t *)((uintptr_t)bochs_display.vbe_regs + 0x501);
        *idx = index;
        return *data;
    }
    return 0;
}

/* ===================================================================== */
/* Framebuffer Operations */
/* ===================================================================== */

void bochs_clear(uint32_t color)
{
    if (!bochs_display.initialized) return;
    
    uint32_t *fb = (uint32_t *)bochs_display.framebuffer;
    uint32_t pixels = bochs_display.width * bochs_display.height;
    
    for (uint32_t i = 0; i < pixels; i++) {
        fb[i] = color;
    }
}

void bochs_put_pixel(int x, int y, uint32_t color)
{
    if (!bochs_display.initialized) return;
    if (x < 0 || x >= (int)bochs_display.width) return;
    if (y < 0 || y >= (int)bochs_display.height) return;
    
    bochs_display.framebuffer[y * bochs_display.width + x] = color;
}

/* ===================================================================== */
/* Initialization */
/* ===================================================================== */

int bochs_init(uint32_t width, uint32_t height)
{
    printk(KERN_INFO "BOCHS: Initializing display %ux%u\n", width, height);
    
    /* Set up register and framebuffer pointers */
    bochs_display.vbe_regs = (volatile uint16_t *)VBE_DISPI_MMIO_BASE;
    bochs_display.framebuffer = (volatile uint32_t *)VBE_FRAMEBUFFER_BASE;
    
    /* Check for Bochs VBE */
    uint16_t vbe_id = vbe_read(VBE_DISPI_INDEX_ID);
    printk(KERN_INFO "BOCHS: VBE ID = 0x%04x\n", vbe_id);
    
    if (vbe_id < VBE_DISPI_ID0 || vbe_id > VBE_DISPI_ID5) {
        printk(KERN_ERR "BOCHS: VBE not detected (ID=0x%04x)\n", vbe_id);
        return -1;
    }
    
    /* Disable display during mode set */
    vbe_write(VBE_DISPI_INDEX_ENABLE, VBE_DISPI_DISABLED);
    
    /* Set resolution */
    vbe_write(VBE_DISPI_INDEX_XRES, width);
    vbe_write(VBE_DISPI_INDEX_YRES, height);
    vbe_write(VBE_DISPI_INDEX_BPP, 32);
    vbe_write(VBE_DISPI_INDEX_VIRT_WIDTH, width);
    vbe_write(VBE_DISPI_INDEX_VIRT_HEIGHT, height);
    vbe_write(VBE_DISPI_INDEX_X_OFFSET, 0);
    vbe_write(VBE_DISPI_INDEX_Y_OFFSET, 0);
    
    /* Enable display with LFB */
    vbe_write(VBE_DISPI_INDEX_ENABLE, VBE_DISPI_ENABLED | VBE_DISPI_LFB_ENABLED);
    
    bochs_display.width = width;
    bochs_display.height = height;
    bochs_display.bpp = 32;
    bochs_display.pitch = width * 4;
    bochs_display.initialized = true;
    
    printk(KERN_INFO "BOCHS: Display initialized, FB at 0x%lx\n", 
           (unsigned long)VBE_FRAMEBUFFER_BASE);
    
    /* Clear screen to dark blue */
    bochs_clear(0x1E1E2E);
    
    return 0;
}

/* Get framebuffer info */
void bochs_get_info(uint32_t **buffer, uint32_t *width, uint32_t *height)
{
    if (buffer) *buffer = (uint32_t *)bochs_display.framebuffer;
    if (width) *width = bochs_display.width;
    if (height) *height = bochs_display.height;
}
