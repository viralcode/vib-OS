/*
 * VibCode x64 - Framebuffer Driver with Double Buffering
 * Eliminates flashing by rendering to backbuffer first
 * Uses Write-Combining (WC) for fast framebuffer access on real hardware
 */

#include "../include/gui.h"
#include "../include/string.h"
#include "../include/wc.h"

/* Maximum supported resolution (4K) - uses ~33MB memory */
#define MAX_FB_WIDTH 3840
#define MAX_FB_HEIGHT 2160

/* Global framebuffer state */
uint32_t screen_width = 0;
uint32_t screen_height = 0;
uint32_t screen_pitch = 0;    /* In BYTES for display framebuffer */
uint32_t *framebuffer = NULL; /* Hardware display */
uint32_t *backbuffer = NULL;  /* In-memory drawing target */
int screen_mm_width = 0;
int screen_mm_height = 0;

/* Static backbuffer - drawing happens here, then copied to display */
static uint32_t static_backbuffer[MAX_FB_WIDTH * MAX_FB_HEIGHT];
static uint32_t backbuffer_pitch = 0; /* In BYTES */

/* ========== Initialization ========== */

/* Toggle write-combining (WC) for framebuffer */
#define FB_ENABLE_WC 0

void fb_init(void *fb_addr, uint32_t width, uint32_t height, uint32_t pitch) {
  framebuffer = (uint32_t *)fb_addr;
  screen_width = width;
  screen_height = height;
  screen_pitch = pitch;

  /* Setup backbuffer - fixed 4 bytes per pixel pitch */
  backbuffer = static_backbuffer;
  backbuffer_pitch = width * 4;

  /* Enable Write-Combining for faster framebuffer writes (if supported) */
  if (FB_ENABLE_WC) {
    size_t fb_size = (size_t)pitch * height;
    if (wc_set_framebuffer((uint64_t)fb_addr, fb_size) != 0) {
      /* Fall back silently if WC is not available */
    }
  }

  /* Clear backbuffer to black */
  for (uint32_t i = 0; i < width * height; i++) {
    backbuffer[i] = 0xFF000000;
  }

  /* Clear display to show initialization worked */
  volatile uint8_t *fb_bytes = (volatile uint8_t *)fb_addr;
  for (uint32_t y = 0; y < height; y++) {
    volatile uint32_t *row = (volatile uint32_t *)(fb_bytes + y * pitch);
    for (uint32_t x = 0; x < width; x++) {
      row[x] = 0xFF000000; /* Black */
    }
  }
}

/* Get pixel pointer in BACKBUFFER (simple row-major layout) */
static inline uint32_t *get_bb_pixel_ptr(int x, int y) {
  return backbuffer + y * screen_width + x;
}

/* ========== Primitive Operations (draw to backbuffer) ========== */

void fb_put_pixel(int x, int y, color_t color) {
  if (x < 0 || x >= (int)screen_width || y < 0 || y >= (int)screen_height) {
    return;
  }
  uint32_t *pixel = get_bb_pixel_ptr(x, y);
  *pixel = color;
}

void fb_fill_rect(int x, int y, int width, int height, color_t color) {
  /* Clipping */
  int x1 = x < 0 ? 0 : x;
  int y1 = y < 0 ? 0 : y;
  int x2 = (x + width) > (int)screen_width ? (int)screen_width : (x + width);
  int y2 =
      (y + height) > (int)screen_height ? (int)screen_height : (y + height);

  for (int py = y1; py < y2; py++) {
    uint32_t *row = backbuffer + py * screen_width;
    for (int px = x1; px < x2; px++) {
      row[px] = color;
    }
  }
}

void fb_draw_rect(int x, int y, int width, int height, color_t color) {
  /* Top and bottom edges */
  for (int px = x; px < x + width; px++) {
    fb_put_pixel(px, y, color);
    fb_put_pixel(px, y + height - 1, color);
  }
  /* Left and right edges */
  for (int py = y; py < y + height; py++) {
    fb_put_pixel(x, py, color);
    fb_put_pixel(x + width - 1, py, color);
  }
}

/* ========== Buffer Swap - Ultra-fast unrolled copy ========== */

void fb_swap_buffers(void) {
  if (!backbuffer || !framebuffer)
    return;

  /* Pitch-aware full copy (framebuffer pitch may differ from width * 4) */
  for (uint32_t y = 0; y < screen_height; y++) {
    uint64_t *src =
        (uint64_t *)(backbuffer + y * (backbuffer_pitch / 4));
    volatile uint64_t *dst =
        (volatile uint64_t *)((uint8_t *)framebuffer + y * screen_pitch);
    size_t count64 = screen_width / 2;
    size_t i = 0;
    size_t fast_count = count64 & ~7UL;
    for (; i < fast_count; i += 8) {
      dst[i] = src[i];
      dst[i + 1] = src[i + 1];
      dst[i + 2] = src[i + 2];
      dst[i + 3] = src[i + 3];
      dst[i + 4] = src[i + 4];
      dst[i + 5] = src[i + 5];
      dst[i + 6] = src[i + 6];
      dst[i + 7] = src[i + 7];
    }
    for (; i < count64; i++) {
      dst[i] = src[i];
    }
    if (screen_width & 1) {
      volatile uint32_t *dst32 = (volatile uint32_t *)dst;
      uint32_t *src32 = (uint32_t *)src;
      dst32[screen_width - 1] = src32[screen_width - 1];
    }
  }

  /* Memory barrier to ensure all writes are visible */
  __asm__ volatile("mfence" ::: "memory");
}

/* ========== Alpha Blending ========== */

static inline color_t blend_pixel(color_t bg, color_t fg) {
  uint32_t alpha = (fg >> 24) & 0xFF;

  if (alpha == 255)
    return fg;
  if (alpha == 0)
    return bg;

  uint32_t inv_alpha = 255 - alpha;

  uint32_t r =
      (((fg >> 16) & 0xFF) * alpha + ((bg >> 16) & 0xFF) * inv_alpha) / 255;
  uint32_t g =
      (((fg >> 8) & 0xFF) * alpha + ((bg >> 8) & 0xFF) * inv_alpha) / 255;
  uint32_t b = ((fg & 0xFF) * alpha + (bg & 0xFF) * inv_alpha) / 255;

  return 0xFF000000 | (r << 16) | (g << 8) | b;
}

void fb_put_pixel_alpha(int x, int y, color_t color) {
  if (x < 0 || x >= (int)screen_width || y < 0 || y >= (int)screen_height) {
    return;
  }
  uint32_t *pixel = get_bb_pixel_ptr(x, y);
  *pixel = blend_pixel(*pixel, color);
}

void fb_fill_rect_alpha(int x, int y, int width, int height, color_t color) {
  int x1 = x < 0 ? 0 : x;
  int y1 = y < 0 ? 0 : y;
  int x2 = (x + width) > (int)screen_width ? (int)screen_width : (x + width);
  int y2 =
      (y + height) > (int)screen_height ? (int)screen_height : (y + height);

  for (int py = y1; py < y2; py++) {
    uint32_t *row = backbuffer + py * screen_width;
    for (int px = x1; px < x2; px++) {
      row[px] = blend_pixel(row[px], color);
    }
  }
}

/* ========== Gradient Fill ========== */

void fb_fill_gradient_v(int x, int y, int width, int height, color_t color1,
                        color_t color2) {
  int x1 = x < 0 ? 0 : x;
  int y1 = y < 0 ? 0 : y;
  int x2 = (x + width) > (int)screen_width ? (int)screen_width : (x + width);
  int y2 =
      (y + height) > (int)screen_height ? (int)screen_height : (y + height);

  /* Extract color components */
  uint8_t r1 = (color1 >> 16) & 0xFF;
  uint8_t g1 = (color1 >> 8) & 0xFF;
  uint8_t b1 = color1 & 0xFF;

  uint8_t r2 = (color2 >> 16) & 0xFF;
  uint8_t g2 = (color2 >> 8) & 0xFF;
  uint8_t b2 = color2 & 0xFF;

  for (int py = y1; py < y2; py++) {
    /* Calculate interpolation factor (0-255) */
    int t = ((py - y) * 255) / (height > 0 ? height : 1);

    uint8_t r = r1 + ((r2 - r1) * t) / 255;
    uint8_t g = g1 + ((g2 - g1) * t) / 255;
    uint8_t b = b1 + ((b2 - b1) * t) / 255;

    color_t row_color = MAKE_COLOR(r, g, b);

    uint32_t *row = backbuffer + py * screen_width;
    for (int px = x1; px < x2; px++) {
      row[px] = row_color;
    }
  }
}
