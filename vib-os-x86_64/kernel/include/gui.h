/*
 * VibCode x64 - GUI System Header
 */

#ifndef _GUI_H
#define _GUI_H

#include "types.h"

/* ========== Constants ========== */
#define ICON_SIZE 56
#define MAX_WINDOWS 16
#define MAX_TITLE_LEN 64
#define TITLE_BAR_HEIGHT 28
#define BUTTON_SIZE 12
#define BUTTON_MARGIN 12
#define BUTTON_SPACING 8

/* ========== Window State ========== */
typedef enum {
  WINDOW_NORMAL,
  WINDOW_MINIMIZED,
  WINDOW_MAXIMIZED
} window_state_t;

/* Forward declaration */
struct window_t;

/* ========== Window Type ========== */
typedef struct window_t {
  uint32_t id;
  char title[MAX_TITLE_LEN];
  int x, y;
  int width, height;
  bool visible;
  bool focused;
  window_state_t state;
  void (*on_draw)(struct window_t *win);
} window_t;

/* ========== Desktop Icon Type ========== */
typedef struct {
  const char *name;
  int x;
  int y;
  int is_dir;
} desktop_icon_t;

/* Screen dimensions (set by framebuffer) */
extern uint32_t screen_width;
extern uint32_t screen_height;
extern uint32_t screen_pitch;
extern uint32_t *framebuffer;
extern uint32_t *backbuffer;
/* Physical display size in millimeters (from EDID, if available) */
extern int screen_mm_width;
extern int screen_mm_height;

/* UI scale factor (1x, 2x, 3x) */
extern int ui_scale;

/* ========== Framebuffer Functions ========== */
void fb_init(void *fb_addr, uint32_t width, uint32_t height, uint32_t pitch);
void fb_put_pixel(int x, int y, color_t color);
void fb_fill_rect(int x, int y, int width, int height, color_t color);
void fb_draw_rect(int x, int y, int width, int height, color_t color);
void fb_swap_buffers(void);
void fb_fill_rect_alpha(int x, int y, int width, int height, color_t color);
void fb_fill_gradient_v(int x, int y, int width, int height, color_t c1,
                        color_t c2);

/* ========== Font Functions ========== */
void font_init(void);
void font_draw_string(int x, int y, const char *str, color_t color);
int font_string_width(const char *str);

/* ========== GUI Functions ========== */
void gui_init(void);
void gui_compose(void);
void gui_main_loop(void);

/* Direct framebuffer access for low-level debug */
extern uint32_t *g_fb_ptr;
extern uint32_t g_fb_width;
extern uint32_t g_fb_height;
extern uint32_t g_fb_pitch;

void debug_rect(int x, int y, int w, int h, uint32_t color);
void serial_puts(const char *s);
void serial_puthex(uint64_t val);

#endif /* _GUI_H */
