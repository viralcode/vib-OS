/*
 * UEFI Demo OS - Desktop Environment
 * Draws the desktop background, icons, and dock
 */

#include "../include/gui.h"
#include "../include/string.h"

/* Desktop icon definitions */
static desktop_icon_t desktop_icons[] = {
    {"Documents", 40, 60, 1},
    {"Pictures", 40, 170, 1},
    {"System", 40, 280, 0},
    {"Terminal", 40, 390, 0},
};

#define NUM_ICONS (sizeof(desktop_icons) / sizeof(desktop_icons[0]))

/* Gradient colors for background */
#define BG_COLOR_TOP MAKE_COLOR(25, 25, 50)    /* Dark purple-blue */
#define BG_COLOR_BOTTOM MAKE_COLOR(50, 30, 80) /* Deep purple */

/* Declare the gradient fill function */
extern void fb_fill_gradient_v(int x, int y, int width, int height,
                               color_t color1, color_t color2);
extern void fb_fill_rect_alpha(int x, int y, int width, int height,
                               color_t color);

/* ========== Desktop Drawing ========== */

void desktop_init(void) {
  /* Initialize desktop icons with proper positions */
  for (int i = 0; i < (int)NUM_ICONS; i++) {
    desktop_icons[i].x = 40;
    desktop_icons[i].y = 60 + i * 110;
  }
}

/* Draw a simple folder icon */
static void draw_folder_icon(int x, int y, bool selected) {
  color_t folder_color =
      selected ? MAKE_COLOR(100, 180, 255) : MAKE_COLOR(70, 150, 230);
  color_t tab_color =
      selected ? MAKE_COLOR(80, 160, 240) : MAKE_COLOR(60, 130, 200);

  /* Folder tab */
  fb_fill_rect(x, y, 24, 8, tab_color);

  /* Folder body */
  fb_fill_rect(x, y + 6, ICON_SIZE, ICON_SIZE - 20, folder_color);

  /* Folder shine */
  fb_fill_rect(x + 2, y + 8, ICON_SIZE - 4, 4, MAKE_ARGB(60, 255, 255, 255));
}

/* Draw a simple file icon */
static void draw_file_icon(int x, int y, bool selected) {
  color_t file_color =
      selected ? MAKE_COLOR(220, 220, 230) : MAKE_COLOR(200, 200, 210);
  color_t corner_color =
      selected ? MAKE_COLOR(180, 180, 190) : MAKE_COLOR(160, 160, 170);

  /* File body */
  fb_fill_rect(x + 8, y, ICON_SIZE - 24, ICON_SIZE - 14, file_color);

  /* Folded corner effect */
  fb_fill_rect(x + ICON_SIZE - 24, y, 8, 8, corner_color);

  /* Text lines simulation */
  for (int i = 0; i < 3; i++) {
    fb_fill_rect(x + 14, y + 18 + i * 8, ICON_SIZE - 36, 2,
                 MAKE_COLOR(150, 150, 160));
  }
}

void desktop_draw_icons(void) {
  for (int i = 0; i < (int)NUM_ICONS; i++) {
    desktop_icon_t *icon = &desktop_icons[i];

    /* Draw icon */
    if (icon->is_dir) {
      draw_folder_icon(icon->x, icon->y, false);
    } else {
      draw_file_icon(icon->x, icon->y, false);
    }

    /* Draw label with shadow */
    int label_width = font_string_width(icon->name);
    int label_x = icon->x + (ICON_SIZE - label_width) / 2;
    int label_y = icon->y + ICON_SIZE - 8;

    /* Shadow */
    font_draw_string(label_x + 1, label_y + 1, icon->name,
                     MAKE_ARGB(128, 0, 0, 0));
    /* Text */
    font_draw_string(label_x, label_y, icon->name, COLOR_WHITE);
  }
}

void desktop_draw(void) {
  /* Draw gradient background */
  fb_fill_gradient_v(0, 0, screen_width, screen_height, BG_COLOR_TOP,
                     BG_COLOR_BOTTOM);

  /* Draw icons */
  desktop_draw_icons();
}
