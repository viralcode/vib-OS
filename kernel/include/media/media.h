#ifndef _KERNEL_MEDIA_H
#define _KERNEL_MEDIA_H

#include "types.h"

typedef struct {
  uint32_t width;
  uint32_t height;
  uint32_t *pixels; /* 0x00RRGGBB */
} media_image_t;

typedef struct {
  int16_t *samples;      /* interleaved PCM */
  uint32_t sample_count; /* per-channel samples */
  uint32_t sample_rate;
  uint8_t channels;
} media_audio_t;

int media_load_file(const char *path, uint8_t **out_data, size_t *out_size);
void media_free_file(uint8_t *data);

int media_decode_jpeg(const uint8_t *data, size_t size, media_image_t *out);
int media_decode_jpeg_buffer(const uint8_t *data, size_t size,
                             media_image_t *out, uint32_t *buffer,
                             size_t buffer_size);
void media_free_image(media_image_t *image);

int media_decode_mp3(const uint8_t *data, size_t size, media_audio_t *out);
void media_free_audio(media_audio_t *audio);

int media_decode_png(const uint8_t *data, size_t size, media_image_t *out);

#endif /* _KERNEL_MEDIA_H */
