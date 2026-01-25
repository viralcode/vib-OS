/*
 * tPNG - Minimal PNG decoder for kernel use
 *
 * Based on tPNG by Johnathan Corkery (https://github.com/jcorks/tPNG)
 * and TINFL by Rich Geldreich.
 *
 * Adapted for Vib-OS kernel - uses kmalloc/kfree instead of libc functions.
 *
 * MIT/Apache-2.0 Licensed - see original source for full license.
 */

#ifndef _KERNEL_TPNG_H
#define _KERNEL_TPNG_H

#include "types.h"

/*
 * Decode a PNG image to RGBA pixel data.
 *
 * @param data     Raw PNG file data
 * @param size     Size of the PNG data in bytes
 * @param width    Output: width of the decoded image
 * @param height   Output: height of the decoded image
 * @return         Pointer to RGBA pixel data (caller must kfree), or NULL on
 * error
 */
uint8_t *tpng_decode(const uint8_t *data, uint32_t size, uint32_t *width,
                     uint32_t *height);

#endif /* _KERNEL_TPNG_H */
