/*
 * Vib-OS - GUI System Header
 */

#ifndef _GUI_H
#define _GUI_H

#include "types.h"

/* ===================================================================== */
/* Window System */
/* ===================================================================== */

struct window;
struct display;

/* Display */
int gui_init(uint32_t *framebuffer, uint32_t width, uint32_t height, uint32_t pitch);
struct display *gui_get_display(void);
void gui_compose(void);

/* Window management */
struct window *gui_create_window(const char *title, int x, int y, int w, int h);
void gui_destroy_window(struct window *win);
void gui_focus_window(struct window *win);

/* Drawing primitives */
void gui_draw_rect(int x, int y, int w, int h, uint32_t color);
void gui_draw_rect_outline(int x, int y, int w, int h, uint32_t color, int thickness);
void gui_draw_line(int x0, int y0, int x1, int y1, uint32_t color);
void gui_draw_circle(int cx, int cy, int r, uint32_t color, bool filled);
void gui_draw_char(int x, int y, char c, uint32_t fg, uint32_t bg);
void gui_draw_string(int x, int y, const char *str, uint32_t fg, uint32_t bg);

/* Input */
void gui_handle_mouse_event(int x, int y, int buttons);
void gui_handle_key_event(int keycode, bool pressed);
void gui_move_mouse(int dx, int dy);
void gui_draw_cursor(void);
void gui_open_image_viewer(const char *path);

/* ===================================================================== */
/* Terminal */
/* ===================================================================== */

struct terminal;

struct terminal *term_create(int x, int y, int cols, int rows);
void term_destroy(struct terminal *term);
void term_putc(struct terminal *term, char c);
void term_puts(struct terminal *term, const char *str);
void term_render(struct terminal *term);
void term_handle_key(struct terminal *term, int key);
struct terminal *term_get_active(void);
void term_set_active(struct terminal *term);

/* ===================================================================== */
/* Application Framework */
/* ===================================================================== */

typedef enum {
    APP_TYPE_TERMINAL,
    APP_TYPE_FILE_MANAGER,
    APP_TYPE_TEXT_EDITOR,
    APP_TYPE_IMAGE_VIEWER,
    APP_TYPE_BROWSER,
    APP_TYPE_SETTINGS,
    APP_TYPE_CUSTOM
} app_type_t;

struct application;

struct application *app_launch(const char *name, app_type_t type);
void app_close(struct application *app);
void app_update_all(void);
void app_draw_all(void);

/* Desktop */
void desktop_init(void);
void launcher_add_item(const char *name, const char *icon, app_type_t type);
void launcher_draw(void);
void launcher_handle_click(int x, int y);

#endif /* _GUI_H */
