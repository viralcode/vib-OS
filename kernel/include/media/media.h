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
void media_free_image(media_image_t *image);

int media_decode_mp3(const uint8_t *data, size_t size, media_audio_t *out);
void media_free_audio(media_audio_t *audio);

/* STB Image support - supports JPEG, PNG, BMP, TGA, PSD, GIF, etc. */
int media_decode_image(const uint8_t *data, size_t size, media_image_t *out);
int media_decode_png(const uint8_t *data, size_t size, media_image_t *out);
int media_decode_bmp(const uint8_t *data, size_t size, media_image_t *out);
int media_encode_png(const media_image_t *image, uint8_t **out_data, size_t *out_size);

/* STB Image wrappers */
unsigned char* stbi_load_from_memory_wrapper(const unsigned char *buffer, int len, 
                                              int *x, int *y, int *channels_in_file, 
                                              int desired_channels);
void stbi_image_free_wrapper(void *data);
const char* stbi_failure_reason_wrapper(void);

#endif /* _KERNEL_MEDIA_H */
