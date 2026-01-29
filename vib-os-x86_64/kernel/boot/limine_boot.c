/*
 * UEFI Demo OS - Kernel Entry Point
 * Uses the Limine Boot Protocol for clean 64-bit entry
 */

#include "../include/gui.h"
#include "../include/limine.h"
#include "../include/idt.h"
#include "../include/acpi.h"
#include "../include/string.h"
#include "../include/types.h"

/* ========== Limine Requests ========== */

/* Place requests in dedicated section */
__attribute__((used, section(".limine_requests"))) static volatile uint64_t
    limine_requests_start_marker[4] = {0xf6b8f4b39de7d1ae, 0xfab91a6940fcb9cf,
                                       0x785c6ed015d3e316, 0x181e920a7852b9d9};

/* Base revision - Use 0 to get full physical memory mapping including device MMIO */
__attribute__((used, section(".limine_requests"))) static volatile uint64_t
    limine_base_revision[3] = {0xf9562b2d5c95a6c8, 0x6a7b384944536bdc, 0};

/* Framebuffer request */
__attribute__((
    used,
    section(
        ".limine_requests"))) static volatile struct limine_framebuffer_request
    framebuffer_request = {.id = {0xc7b1dd30df4c8b88, 0x0a82e883a194f07b,
                                  0x9d5827dcd881dd75, 0xa3148604f6fab11b},
                           .revision = 0,
                           .response = NULL};

/* RSDP request (ACPI root pointer) */
__attribute__((used,
               section(".limine_requests"))) static volatile struct limine_rsdp_request
    rsdp_request = {.id = {0xc7b1dd30df4c8b88, 0x0a82e883a194f07b,
                           0xc5e77b6b397e7b43, 0x27637845accdcf3c},
                    .revision = 0,
                    .response = NULL};

/* HHDM request (Higher Half Direct Map offset) */
__attribute__((used,
               section(".limine_requests"))) static volatile struct limine_hhdm_request
    hhdm_request = {.id = {0xc7b1dd30df4c8b88, 0x0a82e883a194f07b,
                           0x48dcf1cb8ad2b852, 0x63984e959a98244b},
                    .revision = 0,
                    .response = NULL};

/* Kernel address request (physical/virtual base) */
__attribute__((used,
               section(".limine_requests"))) static volatile struct limine_kernel_address_request
    kernel_address_request = {.id = {0xc7b1dd30df4c8b88, 0x0a82e883a194f07b,
                                     0x71ba76863cc55f63, 0xb2644a48c516a487},
                              .revision = 0,
                              .response = NULL};

/* Global HHDM offset for drivers to use */
uint64_t hhdm_offset = 0xFFFF800000000000ULL;
uint64_t kernel_phys_base = 0;
uint64_t kernel_virt_base = 0;

/* Debug framebuffer access */
uint32_t *g_fb_ptr = NULL;
uint32_t g_fb_width = 0;
uint32_t g_fb_height = 0;
uint32_t g_fb_pitch = 0;

void debug_rect(int x, int y, int w, int h, uint32_t color) {
  if (!g_fb_ptr) return;
  for (int dy = 0; dy < h; dy++) {
    if (y + dy >= (int)g_fb_height) break;
    volatile uint32_t *row = (volatile uint32_t *)((uint8_t *)g_fb_ptr + (y + dy) * g_fb_pitch);
    for (int dx = 0; dx < w; dx++) {
      if (x + dx >= (int)g_fb_width) break;
      row[x + dx] = color;
    }
  }
}

/* Request end marker */
__attribute__((used, section(".limine_requests"))) static volatile uint64_t
    limine_requests_end_marker[2] = {0xadc0e0531bb10d03, 0x9572709f31764c62};

/* ========== Kernel State ========== */

static struct limine_framebuffer *fb = NULL;

/* ========== Early Serial Debug ========== */

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
  while ((inb(COM1 + 5) & 0x20) == 0)
    ;
  outb(COM1, c);
}

void serial_puts(const char *s) {
  while (*s) {
    if (*s == '\n')
      serial_putc('\r');
    serial_putc(*s++);
  }
}

void serial_puthex(uint64_t val) {
  const char *hex = "0123456789ABCDEF";
  serial_puts("0x");
  for (int i = 60; i >= 0; i -= 4) {
    serial_putc(hex[(val >> i) & 0xF]);
  }
}

/* Parse EDID to get physical size (mm). Returns 0 on success. */
static int edid_get_mm(const uint8_t *edid, uint64_t size,
                       int *out_mm_w, int *out_mm_h) {
  if (!edid || size < 128 || !out_mm_w || !out_mm_h) {
    return -1;
  }
  /* EDID header: 00 FF FF FF FF FF FF 00 */
  if (!(edid[0] == 0x00 && edid[1] == 0xFF && edid[2] == 0xFF &&
        edid[3] == 0xFF && edid[4] == 0xFF && edid[5] == 0xFF &&
        edid[6] == 0xFF && edid[7] == 0x00)) {
    return -1;
  }
  /* Bytes 21/22: horizontal/vertical size in cm */
  int cm_w = edid[21];
  int cm_h = edid[22];
  if (cm_w == 0 || cm_h == 0) {
    return -1;
  }
  *out_mm_w = cm_w * 10;
  *out_mm_h = cm_h * 10;
  return 0;
}

/* ========== Simple Halt ========== */

static void halt(void) {
  for (;;) {
    __asm__ volatile("hlt");
  }
}

/* ========== Direct Screen Test ========== */

/* Draw directly to framebuffer without any library functions */
static void direct_screen_test(void *fb_addr, uint64_t width, uint64_t height,
                               uint64_t pitch) {
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

  /* Draw "UEFI-OS" text approximation with colored blocks */
  uint64_t text_y = height / 2 - 20;
  uint64_t text_x = width / 2 - 80;
  volatile uint32_t *text_row = (volatile uint32_t *)(fb + text_y * pitch);

  /* U */
  for (int i = 0; i < 30; i++)
    text_row[text_x + i] = 0xFF00FF00;
  text_x += 35;
  /* E */
  for (int i = 0; i < 25; i++)
    text_row[text_x + i] = 0xFF00FF00;
  text_x += 30;
  /* F */
  for (int i = 0; i < 20; i++)
    text_row[text_x + i] = 0xFF00FF00;
  text_x += 25;
  /* I */
  for (int i = 0; i < 10; i++)
    text_row[text_x + i] = 0xFF00FF00;
  text_x += 15;
  /* - */
  for (int i = 0; i < 15; i++)
    text_row[text_x + i] = 0xFFFFFF00;
  text_x += 20;
  /* O */
  for (int i = 0; i < 25; i++)
    text_row[text_x + i] = 0xFF00FFFF;
  text_x += 30;
  /* S */
  for (int i = 0; i < 20; i++)
    text_row[text_x + i] = 0xFF00FFFF;
}

/* ========== Kernel Main ========== */

void _start(void) {
  /* Initialize serial for debug output */
  serial_init();
  serial_puts("\n\n=== UEFI Demo OS ===\n");
  serial_puts("Kernel entry point reached!\n");

  /* Initialize IDT/PIC (interrupts still disabled by default) */
  idt_init();
  pic_init();

  /* Verify base revision was accepted */
  if (limine_base_revision[2] != 0) {
    serial_puts("ERROR: Limine base revision mismatch\n");
    serial_puts("Revision value: ");
    serial_puthex(limine_base_revision[2]);
    serial_puts("\n");
    halt();
  }
  serial_puts("Limine base revision OK\n");

  /* Get HHDM offset for physical memory access */
  if (hhdm_request.response) {
    hhdm_offset = hhdm_request.response->offset;
    serial_puts("HHDM offset: ");
    serial_puthex(hhdm_offset);
    serial_puts("\n");
  }

  /* Get kernel physical/virtual base */
  if (kernel_address_request.response) {
    kernel_phys_base = kernel_address_request.response->physical_base;
    kernel_virt_base = kernel_address_request.response->virtual_base;
    serial_puts("Kernel phys base: ");
    serial_puthex(kernel_phys_base);
    serial_puts("\nKernel virt base: ");
    serial_puthex(kernel_virt_base);
    serial_puts("\n");
  }

  /* Get framebuffer */
  if (framebuffer_request.response == NULL) {
    serial_puts("ERROR: No framebuffer response!\n");
    halt();
  }

  if (framebuffer_request.response->framebuffer_count < 1) {
    serial_puts("ERROR: No framebuffers available!\n");
    halt();
  }

  fb = framebuffer_request.response->framebuffers[0];
  g_fb_ptr = (uint32_t *)fb->address;
  g_fb_width = (uint32_t)fb->width;
  g_fb_height = (uint32_t)fb->height;
  g_fb_pitch = (uint32_t)fb->pitch;

  /* Initialize ACPI if RSDP was provided */
  if (rsdp_request.response && rsdp_request.response->address) {
    acpi_init(rsdp_request.response->address);
  } else {
    acpi_init(NULL);
  }

  serial_puts("Framebuffer acquired:\n");
  serial_puts("  Address: ");
  serial_puthex((uint64_t)fb->address);
  serial_puts("\n  Width: ");
  serial_puthex(fb->width);
  serial_puts("\n  Height: ");
  serial_puthex(fb->height);
  serial_puts("\n  Pitch: ");
  serial_puthex(fb->pitch);
  serial_puts("\n  BPP: ");
  serial_puthex(fb->bpp);
  serial_puts("\n");

  /* Try to read physical display size from EDID */
  if (fb->edid && fb->edid_size >= 128) {
    int mm_w = 0, mm_h = 0;
    if (edid_get_mm((const uint8_t *)fb->edid, fb->edid_size, &mm_w, &mm_h) ==
        0) {
      screen_mm_width = mm_w;
      screen_mm_height = mm_h;
      serial_puts("  EDID size (mm): ");
      serial_puthex((uint64_t)mm_w);
      serial_puts(" x ");
      serial_puthex((uint64_t)mm_h);
      serial_puts("\n");
    }
  }

  /* First: Direct screen test to verify framebuffer works */
  serial_puts("Starting direct framebuffer test...\n");
  direct_screen_test(fb->address, fb->width, fb->height, fb->pitch);

  /* Visual checkpoint: RED = starting fb_init */
  {
    volatile uint32_t *row = (volatile uint32_t *)((uint8_t *)fb->address + 10 * fb->pitch);
    for (int i = 0; i < 50; i++) row[10 + i] = 0xFFFF0000;
  }

  serial_puts("Initializing framebuffer...\n");
  fb_init(fb->address, fb->width, fb->height, fb->pitch);

  /* Visual checkpoint: GREEN = fb_init done, starting gui_init */
  {
    volatile uint32_t *row = (volatile uint32_t *)((uint8_t *)fb->address + 10 * fb->pitch);
    for (int i = 0; i < 50; i++) row[70 + i] = 0xFF00FF00;
  }

  serial_puts("Initializing GUI...\n");
  gui_init();

  /* Visual checkpoint: BLUE = gui_init done, starting main loop */
  {
    volatile uint32_t *row = (volatile uint32_t *)((uint8_t *)fb->address + 10 * fb->pitch);
    for (int i = 0; i < 50; i++) row[130 + i] = 0xFF0000FF;
  }

  /* Enable interrupts globally - REQUIRED for USB/PS2 to work! */
  serial_puts("Enabling interrupts...\n");
  __asm__ volatile("sti");

  serial_puts("Starting main loop...\n");

  /* Main rendering loop */
  gui_main_loop();

  /* Should never reach here */
  halt();
}
