/*
 * VibCode x64 - Media Library Header
 * JPEG decoding support for wallpapers
 */

#ifndef _MEDIA_H
#define _MEDIA_H

#include "types.h"

/* Image structure for decoded images */
typedef struct {
  uint32_t width;
  uint32_t height;
  uint32_t *pixels; /* 0x00RRGGBB format */
} media_image_t;

/* Decode JPEG from memory buffer into provided pixel buffer
 * Returns 0 on success, negative error code on failure
 * If buffer is NULL, allocates memory internally */
int media_decode_jpeg_buffer(const uint8_t *data, size_t size,
                             media_image_t *out, uint32_t *buffer,
                             size_t buffer_size);

/* Decode JPEG from memory buffer (allocates pixel buffer) */
int media_decode_jpeg(const uint8_t *data, size_t size, media_image_t *out);

/* Free image pixel data */
void media_free_image(media_image_t *image);

#endif /* _MEDIA_H */
