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

/* Motion-JPEG video (.mjv): "MJV1" magic, u32 width/height/fps/frame_count,
 * frame table of u32 offset + u32 size pairs, then concatenated baseline
 * JPEG frames. All integers little-endian. See kernel/media/create_mjv.py. */
typedef struct {
  uint8_t *data; /* whole file, owned by this handle */
  size_t size;
  uint32_t width;
  uint32_t height;
  uint32_t fps;
  uint32_t frame_count;
  uint32_t *frame_pixels; /* reusable decode buffer, width*height */
} media_video_t;

int media_video_open(const char *path, media_video_t *out);
/* Decodes one frame into the handle's reusable buffer; out->pixels aliases
 * that buffer and stays valid until the next get_frame or close. */
int media_video_get_frame(media_video_t *video, uint32_t index,
                          media_image_t *out);
void media_video_close(media_video_t *video);

#endif /* _KERNEL_MEDIA_H */
