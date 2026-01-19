/*
 * Vib-OS - QEMU ramfb Display Driver
 *
 * Implements the ramfb (RAM framebuffer) protocol for QEMU display.
 * This uses QEMU's fw_cfg interface to configure a simple framebuffer.
 */

#include "printk.h"
#include "types.h"

/* ===================================================================== */
/* QEMU fw_cfg Interface (ARM64 MMIO) */
/* ===================================================================== */

/* fw_cfg MMIO base address on ARM virt machine */
#define FW_CFG_BASE 0x09020000UL

/* fw_cfg registers */
#define FW_CFG_DATA 0x00
#define FW_CFG_SELECTOR 0x08
#define FW_CFG_DMA 0x10

/* fw_cfg selectors */
#define FW_CFG_SIGNATURE 0x0000
#define FW_CFG_ID 0x0001
#define FW_CFG_FILE_DIR 0x0019

/* ramfb config file name */
#define RAMFB_CFG_FILE "etc/ramfb"

/* ===================================================================== */
/* ramfb Configuration Structure */
/* ===================================================================== */

/* DRM fourcc format codes */
#define DRM_FORMAT_XRGB8888 0x34325258 /* XR24 little-endian */
#define DRM_FORMAT_RGB888 0x34324752   /* RG24 */

struct ramfb_cfg {
  uint64_t addr;   /* Framebuffer physical address (big-endian) */
  uint32_t fourcc; /* Pixel format (big-endian) */
  uint32_t flags;  /* Flags (big-endian) */
  uint32_t width;  /* Width in pixels (big-endian) */
  uint32_t height; /* Height in pixels (big-endian) */
  uint32_t stride; /* Bytes per line (big-endian) */
} __attribute__((packed));

/* fw_cfg file directory entry */
struct fw_cfg_file {
  uint32_t size;
  uint16_t select;
  uint16_t reserved;
  char name[56];
} __attribute__((packed));

/* ===================================================================== */
/* Byte Order Conversion (big-endian for fw_cfg) */
/* ===================================================================== */

static inline uint16_t bswap16(uint16_t x) {
  return ((x & 0xFF) << 8) | ((x >> 8) & 0xFF);
}

static inline uint32_t bswap32(uint32_t x) {
  return ((x & 0xFF) << 24) | ((x & 0xFF00) << 8) | ((x >> 8) & 0xFF00) |
         ((x >> 24) & 0xFF);
}

static inline uint64_t bswap64(uint64_t x) {
  return ((uint64_t)bswap32(x & 0xFFFFFFFF) << 32) | bswap32(x >> 32);
}

/* ===================================================================== */
/* fw_cfg Access Functions */
/* ===================================================================== */

static volatile uint8_t *fw_cfg_base = (volatile uint8_t *)FW_CFG_BASE;

static void fw_cfg_select(uint16_t key) {
  *(volatile uint16_t *)(fw_cfg_base + FW_CFG_SELECTOR) = bswap16(key);
}

static uint8_t fw_cfg_read8(void) {
  return *(volatile uint8_t *)(fw_cfg_base + FW_CFG_DATA);
}

static void fw_cfg_read(void *buf, size_t len) {
  uint8_t *p = (uint8_t *)buf;
  for (size_t i = 0; i < len; i++) {
    p[i] = fw_cfg_read8();
  }
}

static void fw_cfg_write(const void *buf, size_t len) {
  const uint8_t *p = (const uint8_t *)buf;
  for (size_t i = 0; i < len; i++) {
    *(volatile uint8_t *)(fw_cfg_base + FW_CFG_DATA) = p[i];
  }
}

/* ===================================================================== */
/* ramfb Driver */
/* ===================================================================== */

static uint16_t ramfb_selector = 0;

/* Find the ramfb config file in fw_cfg */
static int ramfb_find_cfg(void) {
  uint32_t count;

  /* Read file directory */
  fw_cfg_select(FW_CFG_FILE_DIR);
  fw_cfg_read(&count, sizeof(count));
  count = bswap32(count);

  for (uint32_t i = 0; i < count && i < 100; i++) {
    struct fw_cfg_file entry;
    fw_cfg_read(&entry, sizeof(entry));

    /* Check for ramfb config */
    if (entry.name[0] == 'e' && entry.name[1] == 't' && entry.name[2] == 'c' &&
        entry.name[3] == '/' && entry.name[4] == 'r' && entry.name[5] == 'a' &&
        entry.name[6] == 'm' && entry.name[7] == 'f' && entry.name[8] == 'b') {
      ramfb_selector = bswap16(entry.select);
      printk(KERN_INFO "RAMFB: Found at selector 0x%04x\n", ramfb_selector);
      return 0;
    }
  }

  printk(KERN_ERR "RAMFB: Config file not found\n");
  return -1;
}

/* Configure ramfb with our framebuffer */
int ramfb_setup(uint64_t fb_addr, uint32_t width, uint32_t height,
                uint32_t stride) {
  printk(KERN_INFO "RAMFB: Configuring display %ux%u\n", width, height);

  /* Find the ramfb config selector */
  if (ramfb_selector == 0) {
    if (ramfb_find_cfg() < 0) {
      return -1;
    }
  }

  /* Prepare configuration (all values big-endian) */
  static struct ramfb_cfg cfg __attribute__((aligned(4096)));
  cfg.addr = bswap64(fb_addr);
  cfg.fourcc = bswap32(DRM_FORMAT_XRGB8888);
  cfg.flags = 0;
  cfg.width = bswap32(width);
  cfg.height = bswap32(height);
  cfg.stride = bswap32(stride);

  /* Use DMA for write - required by modern QEMU */
  static volatile struct {
    uint32_t control;
    uint32_t length;
    uint64_t address;
  } __attribute__((packed, aligned(4096))) dma;

  /* FW_CFG_DMA_CTL_SELECT = 0x08, FW_CFG_DMA_CTL_WRITE = 0x10 */
  /* Control = (selector << 16) | SELECT | WRITE */
  dma.control = bswap32((ramfb_selector << 16) | 0x08 | 0x10);
  dma.length = bswap32(sizeof(cfg));
  dma.address = bswap64((uint64_t)(uintptr_t)&cfg);

  /* Trigger DMA write by writing address to DMA register */
  volatile uint64_t *dma_reg = (volatile uint64_t *)(fw_cfg_base + FW_CFG_DMA);
  uint64_t dma_addr = (uint64_t)(uintptr_t)&dma;
  *dma_reg = bswap64(dma_addr);

  /* Small delay for DMA to complete */
  for (volatile int i = 0; i < 100000; i++) {
  }

  printk(KERN_INFO "RAMFB: Display configured at 0x%lx (DMA)\n",
         (unsigned long)fb_addr);

  return 0;
}

/* ===================================================================== */
/* Public Interface */
/* ===================================================================== */

int ramfb_init(uint32_t *framebuffer, uint32_t width, uint32_t height) {
  printk(KERN_INFO "RAMFB: Initializing QEMU ramfb display\n");

  /* Verify fw_cfg is available */
  fw_cfg_select(FW_CFG_SIGNATURE);
  char sig[4];
  fw_cfg_read(sig, 4);

  if (sig[0] != 'Q' || sig[1] != 'E' || sig[2] != 'M' || sig[3] != 'U') {
    printk(KERN_ERR "RAMFB: fw_cfg not available (sig=%c%c%c%c)\n", sig[0],
           sig[1], sig[2], sig[3]);
    return -1;
  }

  printk(KERN_INFO "RAMFB: fw_cfg detected\n");

  /* Configure ramfb with our framebuffer */
  uint32_t stride = width * 4; /* 32bpp */
  return ramfb_setup((uint64_t)(uintptr_t)framebuffer, width, height, stride);
}
