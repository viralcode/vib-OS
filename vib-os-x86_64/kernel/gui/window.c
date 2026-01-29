/*
 * UEFI Demo OS - Window Manager
 * macOS-style windows with traffic light buttons
 */

#include "../include/gui.h"
#include "../include/string.h"

/* Window storage */
static window_t windows[MAX_WINDOWS];
static int window_count = 0;
static uint32_t next_window_id = 1;

/* Declare alpha fill function */
extern void fb_fill_rect_alpha(int x, int y, int width, int height,
                               color_t color);

/* ========== Window Creation ========== */

void window_init(void) {
  memset(windows, 0, sizeof(windows));
  window_count = 0;
  next_window_id = 1;
}

window_t *window_create(const char *title, int x, int y, int width,
                        int height) {
  if (window_count >= MAX_WINDOWS) {
    return NULL;
  }

  /* Find empty slot */
  window_t *win = NULL;
  for (int i = 0; i < MAX_WINDOWS; i++) {
    if (windows[i].id == 0) {
      win = &windows[i];
      break;
    }
  }

  if (!win)
    return NULL;

  /* Initialize window */
  memset(win, 0, sizeof(window_t));
  win->id = next_window_id++;
  strncpy(win->title, title, MAX_TITLE_LEN - 1);
  win->title[MAX_TITLE_LEN - 1] = '\0';
  win->x = x;
  win->y = y;
  win->width = width;
  win->height = height;
  win->visible = true;
  win->focused = true;
  win->state = WINDOW_NORMAL;

  window_count++;
  return win;
}

void window_destroy(window_t *win) {
  if (!win || win->id == 0)
    return;

  win->id = 0;
  win->visible = false;
  window_count--;
}

/* ========== Window Drawing ========== */

/* Draw traffic light button */
static void draw_button(int cx, int cy, color_t color, bool hovered) {
  int r = BUTTON_SIZE / 2;

  /* Simple circle approximation using filled rectangle with rounded look */
  for (int py = -r; py <= r; py++) {
    for (int px = -r; px <= r; px++) {
      if (px * px + py * py <= r * r) {
        color_t c = color;
        /* Add highlight effect */
        if (py < 0 && px < 0) {
          uint8_t cr = (c >> 16) & 0xFF;
          uint8_t cg = (c >> 8) & 0xFF;
          uint8_t cb = c & 0xFF;
          cr = cr + 30 > 255 ? 255 : cr + 30;
          cg = cg + 30 > 255 ? 255 : cg + 30;
          cb = cb + 30 > 255 ? 255 : cb + 30;
          c = MAKE_COLOR(cr, cg, cb);
        }
        fb_put_pixel(cx + px, cy + py, c);
      }
    }
  }
}

/* Draw window shadow */
static void draw_window_shadow(int x, int y, int width, int height) {
  /* Draw soft shadow offset to bottom-right */
  for (int i = 1; i <= 8; i++) {
    uint8_t alpha = 40 - i * 4;
    color_t shadow = MAKE_ARGB(alpha, 0, 0, 0);

    /* Bottom shadow */
    fb_fill_rect_alpha(x + i, y + height + i - 4, width, 4, shadow);
    /* Right shadow */
    fb_fill_rect_alpha(x + width + i - 4, y + i, 4, height, shadow);
  }
}

void window_draw(window_t *win) {
  if (!win || !win->visible)
    return;

  int x = win->x;
  int y = win->y;
  int w = win->width;
  int h = win->height;

  /* Draw shadow first */
  draw_window_shadow(x, y, w, h);

  /* Window background */
  fb_fill_rect(x, y, w, h, COLOR_WINDOW_BG);

  /* Title bar */
  color_t title_bg = win->focused ? COLOR_TITLE_BAR_ACTIVE : COLOR_TITLE_BAR;
  fb_fill_rect(x, y, w, TITLE_BAR_HEIGHT, title_bg);

  /* Title bar bottom border */
  fb_fill_rect(x, y + TITLE_BAR_HEIGHT - 1, w, 1, MAKE_COLOR(40, 40, 42));

  /* Traffic light buttons */
  int btn_y = y + TITLE_BAR_HEIGHT / 2;
  int btn_x = x + BUTTON_MARGIN;

  /* Close (Red) */
  draw_button(btn_x + BUTTON_SIZE / 2, btn_y, COLOR_RED, false);
  btn_x += BUTTON_SIZE + BUTTON_SPACING;

  /* Minimize (Yellow) */
  draw_button(btn_x + BUTTON_SIZE / 2, btn_y, COLOR_YELLOW, false);
  btn_x += BUTTON_SIZE + BUTTON_SPACING;

  /* Maximize (Green) */
  draw_button(btn_x + BUTTON_SIZE / 2, btn_y, COLOR_GREEN, false);

  /* Title text (centered) */
  int title_width = font_string_width(win->title);
  int title_x = x + (w - title_width) / 2;
  int title_y = y + (TITLE_BAR_HEIGHT - 16) / 2;
  font_draw_string(title_x, title_y, win->title, MAKE_COLOR(230, 230, 230));

  /* Window border */
  fb_draw_rect(x, y, w, h, MAKE_COLOR(60, 60, 62));

  /* Call content renderer if defined */
  if (win->on_draw) {
    win->on_draw(win);
  }
}

void window_draw_all(void) {
  /* Draw windows from bottom to top (reverse order for proper Z-ordering) */
  for (int i = MAX_WINDOWS - 1; i >= 0; i--) {
    if (windows[i].id != 0 && windows[i].visible) {
      window_draw(&windows[i]);
    }
  }
}

/* ========== Window Focus ========== */

void window_focus(window_t *win) {
  if (!win)
    return;

  /* Unfocus all windows */
  for (int i = 0; i < MAX_WINDOWS; i++) {
    windows[i].focused = false;
  }

  /* Focus the target window */
  win->focused = true;
}

/* ========== Get Window by Index ========== */

window_t *window_get(int index) {
  if (index < 0 || index >= MAX_WINDOWS) {
    return NULL;
  }
  if (windows[index].id == 0) {
    return NULL;
  }
  return &windows[index];
}
