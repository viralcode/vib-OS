/*
 * Vib-OS - Media helpers (JPEG/MP3 decoding)
 */

#include "media/media.h"
#include "fs/vfs.h"
#include "mm/kmalloc.h"
#include "printk.h"
#include "types.h"
#include "sandbox/sandbox.h"

/* --------------------------------------------------------------------- */
/* File loading                                                          */
/* --------------------------------------------------------------------- */

int media_load_file(const char *path, uint8_t **out_data, size_t *out_size) {
  if (!path || !out_data || !out_size)
    return -EINVAL;

  struct file *f = vfs_open(path, O_RDONLY, 0);
  if (!f)
    return -ENOENT;

  struct inode *inode = f->f_dentry ? f->f_dentry->d_inode : NULL;
  if (!inode || inode->i_size <= 0) {
    vfs_close(f);
    return -EINVAL;
  }

  size_t size = (size_t)inode->i_size;
  uint8_t *buf = (uint8_t *)kmalloc(size, GFP_KERNEL);
  if (!buf) {
    vfs_close(f);
    return -ENOMEM;
  }

  ssize_t read_bytes = vfs_read(f, (char *)buf, size);
  vfs_close(f);

  if (read_bytes < 0) {
    kfree(buf);
    return (int)read_bytes;
  }

  *out_data = buf;
  *out_size = (size_t)read_bytes;
  return 0;
}

void media_free_file(uint8_t *data) {
  if (data)
    kfree(data);
}

/* --------------------------------------------------------------------- */
/* JPEG decoding (picojpeg)                                               */
/* --------------------------------------------------------------------- */

#include "picojpeg.h"

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
    return -EINVAL;

  jpeg_mem_t mem = {data, size, 0};
  pjpeg_image_info_t info;
  unsigned char status = pjpeg_decode_init(&info, jpeg_need_bytes, &mem, 0);
  if (status) {
    printk(KERN_ERR "JPEG: decode_init failed (%u)\n", status);
    return -EINVAL;
  }

  if (info.m_width <= 0 || info.m_height <= 0)
    return -EINVAL;

  /* Check for integer overflow in pixel count calculation */
  if ((size_t)info.m_width > SIZE_MAX / (size_t)info.m_height) {
    printk(KERN_ERR "JPEG: dimensions too large (integer overflow)\n");
    return -EINVAL;
  }

  size_t pixel_count = (size_t)info.m_width * (size_t)info.m_height;

  /* Prevent excessively large allocations (64MB max image - 4K support) */
  if (pixel_count > 16 * 1024 * 1024) {
    printk(KERN_ERR "JPEG: image too large (%zu pixels)\n", pixel_count);
    return -EINVAL;
  }

  size_t required_bytes = pixel_count * sizeof(uint32_t);

  uint32_t *pixels = NULL;
  bool allocated = false;

  if (buffer) {
    if (buffer_size < required_bytes) {
      printk(KERN_ERR "JPEG: buffer too small (need %d, got %d)\n",
             (int)required_bytes, (int)buffer_size);
      return -ENOMEM;
    }
    pixels = buffer;
  } else {
    pixels = (uint32_t *)kmalloc(required_bytes, GFP_KERNEL);
    if (!pixels)
      return -ENOMEM;
    allocated = true;
  }

  int mcu_x = 0;
  int mcu_y = 0;
  while (1) {
    status = pjpeg_decode_mcu();
    if (status) {
      if (status == PJPG_NO_MORE_BLOCKS)
        break;
      printk(KERN_ERR "JPEG: decode_mcu failed (%u)\n", status);
      if (allocated)
        kfree(pixels);
      return -EINVAL;
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
  return media_decode_jpeg_buffer(data, size, out, NULL, 0);
}

void media_free_image(media_image_t *image) {
  if (!image)
    return;
  if (image->pixels) {
    kfree(image->pixels);
    image->pixels = NULL;
  }
  image->width = 0;
  image->height = 0;
}

/* --------------------------------------------------------------------- */
/* MP3 decoding (minimp3)                                                 */
/* --------------------------------------------------------------------- */

#define MINIMP3_IMPLEMENTATION
#define MINIMP3_NO_STDIO
#define MINIMP3_NO_SIMD
#define malloc(sz) kmalloc((sz))
#define free(ptr) kfree((ptr))
#define realloc(ptr, sz) krealloc((ptr), (sz), 0)
#include "minimp3_ex.h"
#undef malloc
#undef free
#undef realloc

int media_decode_mp3(const uint8_t *data, size_t size, media_audio_t *out) {
  if (!data || !size || !out)
    return -EINVAL;

  mp3dec_t dec;
  mp3dec_file_info_t info;
  int ret = mp3dec_load_buf(&dec, data, size, &info, NULL, NULL);
  if (ret < 0 || !info.buffer || !info.samples) {
    return -EINVAL;
  }

  out->samples = (int16_t *)info.buffer;
  out->sample_count =
      (uint32_t)(info.samples / (info.channels ? info.channels : 1));
  out->sample_rate = (uint32_t)info.hz;
  out->channels = (uint8_t)info.channels;
  return 0;
}

void media_free_audio(media_audio_t *audio) {
  if (!audio)
    return;
  if (audio->samples) {
    kfree(audio->samples);
    audio->samples = NULL;
  }
  audio->sample_count = 0;
  audio->sample_rate = 0;
  audio->channels = 0;
}

/* --------------------------------------------------------------------- */
/* PNG decoding (tPNG)                                                    */
/* --------------------------------------------------------------------- */

#include "tpng.h"

int media_decode_png(const uint8_t *data, size_t size, media_image_t *out) {
  if (!data || !size || !out)
    return -EINVAL;

  uint32_t width = 0, height = 0;
  uint8_t *rgba = tpng_decode(data, (uint32_t)size, &width, &height);

  if (!rgba || width == 0 || height == 0) {
    printk(KERN_ERR "PNG: decode failed\n");
    if (rgba)
      kfree(rgba);
    return -EINVAL;
  }

  /* Check for excessively large images (16M pixels max, same as JPEG) */
  size_t pixel_count = (size_t)width * (size_t)height;
  if (pixel_count > 16 * 1024 * 1024) {
    printk(KERN_ERR "PNG: image too large (%zu pixels)\n", pixel_count);
    kfree(rgba);
    return -EINVAL;
  }

  /* Convert RGBA (uint8_t*) to 0x00RRGGBB (uint32_t*) format */
  uint32_t *pixels =
      (uint32_t *)kmalloc(pixel_count * sizeof(uint32_t), GFP_KERNEL);
  if (!pixels) {
    kfree(rgba);
    return -ENOMEM;
  }

  for (size_t i = 0; i < pixel_count; i++) {
    uint8_t r = rgba[i * 4 + 0];
    uint8_t g = rgba[i * 4 + 1];
    uint8_t b = rgba[i * 4 + 2];
    /* Alpha is in rgba[i * 4 + 3] but we ignore it for now */
    pixels[i] = ((uint32_t)r << 16) | ((uint32_t)g << 8) | b;
  }

  kfree(rgba);

  out->width = width;
  out->height = height;
  out->pixels = pixels;
  return 0;
}

/* --------------------------------------------------------------------- */
/* BMP decoding (uncompressed 24/32bpp)                                   */
/* --------------------------------------------------------------------- */

static uint32_t bmp_read_le32(const uint8_t *p) {
  return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) |
         ((uint32_t)p[3] << 24);
}

static uint16_t bmp_read_le16(const uint8_t *p) {
  return (uint16_t)((uint32_t)p[0] | ((uint32_t)p[1] << 8));
}

int media_decode_bmp(const uint8_t *data, size_t size, media_image_t *out) {
  if (!data || !out || size < 54 || data[0] != 'B' || data[1] != 'M')
    return -EINVAL;

  uint32_t pixel_offset = bmp_read_le32(data + 10);
  uint32_t dib_size = bmp_read_le32(data + 14);

  /* BITMAPINFOHEADER (40) or the larger V4/V5 headers that extend it */
  if (dib_size < 40 || (size_t)14 + dib_size > size) {
    printk(KERN_ERR "BMP: unsupported DIB header (%u bytes)\n", dib_size);
    return -EINVAL;
  }

  int32_t width = (int32_t)bmp_read_le32(data + 18);
  int32_t height = (int32_t)bmp_read_le32(data + 22);
  uint16_t planes = bmp_read_le16(data + 26);
  uint16_t bpp = bmp_read_le16(data + 28);
  uint32_t compression = bmp_read_le32(data + 30);

  int top_down = 0;
  if (height < 0) {
    top_down = 1;
    height = -height;
  }

  /* BI_RGB only; BI_BITFIELDS (3) is accepted for 32bpp since the
   * standard BGRA masks match our channel extraction anyway */
  if (planes != 1 || (bpp != 24 && bpp != 32) ||
      (compression != 0 && !(compression == 3 && bpp == 32))) {
    printk(KERN_ERR "BMP: unsupported format (%u bpp, compression %u)\n", bpp,
           compression);
    return -EINVAL;
  }

  if (width <= 0 || height <= 0 ||
      (size_t)width > SIZE_MAX / (size_t)height) {
    printk(KERN_ERR "BMP: bad dimensions\n");
    return -EINVAL;
  }

  size_t pixel_count = (size_t)width * (size_t)height;
  if (pixel_count > 16 * 1024 * 1024) {
    printk(KERN_ERR "BMP: image too large (%zu pixels)\n", pixel_count);
    return -EINVAL;
  }

  size_t bytes_per_px = bpp / 8;
  size_t stride = ((size_t)width * bytes_per_px + 3) & ~(size_t)3;
  if (pixel_offset > size || stride > (size - pixel_offset) / (size_t)height) {
    printk(KERN_ERR "BMP: pixel data out of bounds\n");
    return -EINVAL;
  }

  uint32_t *pixels =
      (uint32_t *)kmalloc(pixel_count * sizeof(uint32_t), GFP_KERNEL);
  if (!pixels)
    return -ENOMEM;

  for (int32_t y = 0; y < height; y++) {
    int32_t src_y = top_down ? y : height - 1 - y;
    const uint8_t *row = data + pixel_offset + (size_t)src_y * stride;
    for (int32_t x = 0; x < width; x++) {
      const uint8_t *px = row + (size_t)x * bytes_per_px;
      pixels[(size_t)y * width + x] =
          ((uint32_t)px[2] << 16) | ((uint32_t)px[1] << 8) | px[0];
    }
  }

  out->width = (uint32_t)width;
  out->height = (uint32_t)height;
  out->pixels = pixels;
  return 0;
}
