/*
 * Vib-OS Bitmap Font
 * 
 * 8x16 monospace font for GUI display
 * (Adapted from VibeOS)
 */

#ifndef FONT_H
#define FONT_H

#include "types.h"

#define FONT_WIDTH  8
#define FONT_HEIGHT 16

/* Font data - 256 characters, 16 bytes each (one byte per row) */
extern const uint8_t font_data[256][16];

#endif
