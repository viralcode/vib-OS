/*
 * Vib-OS User Media Library
 * 
 * Provides userspace access to media decoding capabilities.
 * For kernel-mode GUI apps, use kernel/include/media/media.h directly.
 */

#ifndef _USER_MEDIA_H
#define _USER_MEDIA_H

#include <stdint.h>
#include <stddef.h>

/* ===================================================================== */
/* Image Types */
/* ===================================================================== */

typedef struct {
    uint32_t width;
    uint32_t height;
    uint32_t *pixels;  /* 0x00RRGGBB format */
} image_t;

/* ===================================================================== */
/* Audio Types */
/* ===================================================================== */

typedef struct {
    int16_t *samples;       /* Interleaved PCM samples */
    uint32_t sample_count;  /* Number of samples per channel */
    uint32_t sample_rate;   /* Sample rate in Hz */
    uint8_t channels;       /* Number of channels */
} audio_t;

/* ===================================================================== */
/* Image Functions */
/* ===================================================================== */

/**
 * image_load_jpeg - Load a JPEG image from memory
 * @data: Pointer to JPEG data
 * @size: Size of JPEG data in bytes
 * @img: Output image structure
 * 
 * Return: 0 on success, negative on error
 */
int image_load_jpeg(const uint8_t *data, size_t size, image_t *img);

/**
 * image_load_jpeg_file - Load a JPEG image from file
 * @path: Path to JPEG file
 * @img: Output image structure
 * 
 * Return: 0 on success, negative on error
 */
int image_load_jpeg_file(const char *path, image_t *img);

/**
 * image_free - Free image resources
 * @img: Image to free
 */
void image_free(image_t *img);

/**
 * image_resize - Resize an image
 * @img: Source image
 * @out: Output image (will be allocated)
 * @new_width: Target width
 * @new_height: Target height
 * 
 * Return: 0 on success, negative on error
 */
int image_resize(const image_t *img, image_t *out, uint32_t new_width, uint32_t new_height);

/* ===================================================================== */
/* Audio Functions */
/* ===================================================================== */

/**
 * audio_load_mp3 - Load an MP3 file from memory
 * @data: Pointer to MP3 data
 * @size: Size of MP3 data in bytes
 * @audio: Output audio structure
 * 
 * Return: 0 on success, negative on error
 */
int audio_load_mp3(const uint8_t *data, size_t size, audio_t *audio);

/**
 * audio_load_mp3_file - Load an MP3 file from path
 * @path: Path to MP3 file
 * @audio: Output audio structure
 * 
 * Return: 0 on success, negative on error
 */
int audio_load_mp3_file(const char *path, audio_t *audio);

/**
 * audio_free - Free audio resources
 * @audio: Audio to free
 */
void audio_free(audio_t *audio);

/**
 * audio_play - Play audio through system audio
 * @audio: Audio to play
 * 
 * Return: 0 on success, negative on error
 */
int audio_play(const audio_t *audio);

/**
 * audio_stop - Stop current playback
 */
void audio_stop(void);

/* ===================================================================== */
/* Format Detection */
/* ===================================================================== */

/**
 * media_detect_format - Detect media format from data
 * @data: Pointer to media data
 * @size: Size of data
 * 
 * Return: Format string ("jpeg", "mp3", "unknown")
 */
const char *media_detect_format(const uint8_t *data, size_t size);

#endif /* _USER_MEDIA_H */
