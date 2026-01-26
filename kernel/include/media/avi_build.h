#ifndef _KERNEL_AVI_BUILD_H
#define _KERNEL_AVI_BUILD_H

#include "fs/vfs.h"
#include "types.h"

/*
 * Build a tiny MJPEG AVI file from a list of JPEG frames.
 *
 * Notes:
 * - Frames must be valid JPEG byte streams (as stored in movi '00dc' chunks).
 * - The output buffer is heap-allocated and must be freed with kfree().
 * - This is intended for generating a small built-in demo video at boot.
 */

int media_build_mjpeg_avi(uint8_t **out_data, size_t *out_size,
                          const uint8_t **frames,
                          const uint32_t *frame_sizes,
                          uint32_t frame_count, uint32_t width,
                          uint32_t height, uint32_t fps);

#endif /* _KERNEL_AVI_BUILD_H */
