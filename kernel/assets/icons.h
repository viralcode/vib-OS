/*
 * Embedded PNG icons header for vib-OS GUI
 */

#ifndef KERNEL_ICONS_H
#define KERNEL_ICONS_H

#include "types.h"

/* Toolbar icons (24x24 RGB PNG) */
extern const unsigned char *toolbar_icon_png_data[8];
extern const unsigned int toolbar_icon_png_sizes[8];

#define NUM_TOOLBAR_ICONS 8

/* Icon indices */
#define ICON_PREV 0
#define ICON_NEXT 1
#define ICON_ROTATE_CW 2
#define ICON_ROTATE_CCW 3
#define ICON_ZOOM_IN 4
#define ICON_ZOOM_OUT 5
#define ICON_FIT 6
#define ICON_FULLSCREEN 7

#endif /* KERNEL_ICONS_H */
