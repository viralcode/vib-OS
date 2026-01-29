/*
 * VibCode x64 - Media Library
 * JPEG decoding support for wallpapers
 */

#include "../include/media.h"
#include "../include/string.h"
#include "picojpeg.h"

/* Simple memory allocator for media - uses static buffer */
#define MEDIA_HEAP_SIZE (4 * 1024 * 1024) /* 4MB for images */
static uint8_t media_heap[MEDIA_HEAP_SIZE];
static size_t media_heap_used = 0;

static void *media_alloc(size_t size) {
  /* Align to 8 bytes */
  size = (size + 7) & ~7;
  if (media_heap_used + size > MEDIA_HEAP_SIZE) {
    return (void *)0;
  }
  void *ptr = &media_heap[media_heap_used];
  media_heap_used += size;
  return ptr;
}

static void media_free(void *ptr) {
  /* Simple bump allocator - no individual free support */
  (void)ptr;
}

static void media_reset(void) { media_heap_used = 0; }

/* --------------------------------------------------------------------- */
/* JPEG decoding (picojpeg)                                               */
/* --------------------------------------------------------------------- */

typedef struct {
  const uint8_t *data;
  size_t size;
  size_t offset;
} jpeg_mem_t;

static unsigned char jpeg_need_bytes(unsigned char *pBuf,
                                     unsigned char buf_size,
                                     unsigned char *pBytes_actually_read,
                                     void *pCallback_data) {
  jpeg_mem_t *mem = (jpeg_mem_t *)pCallback_data;
  if (!mem || mem->offset >= mem->size) {
    *pBytes_actually_read = 0;
    return 0;
  }

  size_t remaining = mem->size - mem->offset;
  size_t to_copy = remaining < buf_size ? remaining : buf_size;
  for (size_t i = 0; i < to_copy; i++) {
    pBuf[i] = mem->data[mem->offset + i];
  }

  mem->offset += to_copy;
  *pBytes_actually_read = (unsigned char)to_copy;
  return 0;
}

int media_decode_jpeg_buffer(const uint8_t *data, size_t size,
                             media_image_t *out, uint32_t *buffer,
                             size_t buffer_size) {
  if (!data || !size || !out)
    return -1;

  jpeg_mem_t mem = {data, size, 0};
  pjpeg_image_info_t info;
  unsigned char status = pjpeg_decode_init(&info, jpeg_need_bytes, &mem, 0);
  if (status) {
    return -1;
  }

  if (info.m_width <= 0 || info.m_height <= 0)
    return -1;

  /* Check for excessively large images */
  size_t pixel_count = (size_t)info.m_width * (size_t)info.m_height;
  if (pixel_count > 4 * 1024 * 1024) { /* 4M pixels max for kernel */
    return -1;
  }

  size_t required_bytes = pixel_count * sizeof(uint32_t);

  uint32_t *pixels = (void *)0;
  int allocated = 0;

  if (buffer) {
    if (buffer_size < required_bytes) {
      return -1;
    }
    pixels = buffer;
  } else {
    pixels = (uint32_t *)media_alloc(required_bytes);
    if (!pixels)
      return -1;
    allocated = 1;
  }

  int mcu_x = 0;
  int mcu_y = 0;
  while (1) {
    status = pjpeg_decode_mcu();
    if (status) {
      if (status == PJPG_NO_MORE_BLOCKS)
        break;
      if (allocated)
        media_free(pixels);
      return -1;
    }

    int mcu_width = info.m_MCUWidth;
    int mcu_height = info.m_MCUHeight;
    int blocks_per_row = mcu_width / 8;

    for (int y = 0; y < mcu_height; y++) {
      int yy = mcu_y * mcu_height + y;
      if (yy >= info.m_height)
        continue;
      for (int x = 0; x < mcu_width; x++) {
        int xx = mcu_x * mcu_width + x;
        if (xx >= info.m_width)
          continue;

        int block_x = x / 8;
        int block_y = y / 8;
        int block_index = block_y * blocks_per_row + block_x;
        int block_offset = block_index * 64;
        int pixel_offset = block_offset + (y % 8) * 8 + (x % 8);

        uint8_t r = info.m_pMCUBufR[pixel_offset];
        uint8_t g = info.m_pMCUBufG ? info.m_pMCUBufG[pixel_offset] : r;
        uint8_t b = info.m_pMCUBufB ? info.m_pMCUBufB[pixel_offset] : r;
        pixels[yy * info.m_width + xx] =
            ((uint32_t)r << 16) | ((uint32_t)g << 8) | b;
      }
    }

    mcu_x++;
    if (mcu_x == info.m_MCUSPerRow) {
      mcu_x = 0;
      mcu_y++;
    }
  }

  out->width = (uint32_t)info.m_width;
  out->height = (uint32_t)info.m_height;
  out->pixels = pixels;
  return 0;
}

int media_decode_jpeg(const uint8_t *data, size_t size, media_image_t *out) {
  return media_decode_jpeg_buffer(data, size, out, (void *)0, 0);
}

void media_free_image(media_image_t *image) {
  if (!image)
    return;
  if (image->pixels) {
    media_free(image->pixels);
    image->pixels = (void *)0;
  }
  image->width = 0;
  image->height = 0;
}
