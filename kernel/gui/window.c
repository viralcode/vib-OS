/*
 * Vib-OS - GUI Windowing System
 *
 * Complete window manager with compositor and widget toolkit.
 */

#include "../core/process.h" /* For Doom launch */
#include "desktop.h"         /* Desktop manager */
#include "dock_icons.h"      /* Dock icons (PNG-based) */
#include "fs/vfs.h"          /* VFS headers */
#include "icons.h"           /* Icon bitmaps */
#include "media/media.h"
#include "mm/kmalloc.h"
#include "printk.h"
#include "toolbar_icons.h" /* Toolbar icons for image viewer */
#include "types.h"

struct window *gui_create_file_manager(int x, int y);
void gui_open_notepad(const char *path);

/* Terminal functions from terminal.c */
struct terminal;
extern struct terminal *term_get_active(void);
extern void term_set_active(struct terminal *term);
extern void term_handle_key(struct terminal *term, int key);
extern int term_get_input_len(struct terminal *t);
extern char term_get_input_char(struct terminal *t, int idx);
extern void term_render(struct terminal *term);
extern void term_set_content_pos(struct terminal *t, int x, int y);

/* ===================================================================== */
/* Display and Color */
/* ===================================================================== */

#define COLOR_BLACK 0x000000
#define COLOR_WHITE 0xFFFFFF
#define COLOR_RED 0xFF0000
#define COLOR_GREEN 0x00FF00
#define COLOR_BLUE 0x0000FF
#define COLOR_GRAY 0x808080
#define COLOR_DARK_GRAY 0x404040
#define COLOR_LIGHT_GRAY 0xC0C0C0

/* UI Theme Colors - Modern Premium Design */
#define THEME_BG 0x1A1A2E       /* Deep dark background */
#define THEME_FG 0xE4E4E7       /* Crisp light text */
#define THEME_ACCENT 0x6366F1   /* Indigo accent (modern) */
#define THEME_ACCENT2 0xEC4899  /* Pink accent */
#define THEME_TITLEBAR 0x27272A /* Dark zinc titlebar */
#define THEME_TITLEBAR_INACTIVE 0x3F3F46
#define THEME_BORDER 0x52525B       /* Zinc border */
#define THEME_BUTTON 0x3F3F46       /* Modern button background */
#define THEME_BUTTON_HOVER 0x525258 /* Button hover */

/* macOS Traffic Light Colors - Slightly desaturated for premium look */
#define COLOR_BTN_CLOSE 0xEF4444    /* Red */
#define COLOR_BTN_MINIMIZE 0xF59E0B /* Amber */
#define COLOR_BTN_ZOOM 0x22C55E     /* Green */

/* Menu Bar - Frosted glass effect */
#define COLOR_MENU_BG 0x18181B   /* Very dark */
#define COLOR_MENU_TEXT 0xFAFAFA /* White text */
#define MENU_BAR_HEIGHT 28

/* Dock - Modern glass dock */
#define COLOR_DOCK_BG 0x1F1F23     /* Dark dock */
#define COLOR_DOCK_BORDER 0x3F3F46 /* Subtle border */
#define COLOR_DOCK_GLASS 0x2A2A30  /* Glass effect layer */
#define DOCK_HEIGHT 70
/* ===================================================================== */
/* Wallpaper Manager                                                     */
/* ===================================================================== */
#define NUM_WALLPAPERS 10
static int current_wallpaper = 0; /* 0 = Landscape (default image) */

/* Wallpaper types: 0 = gradient, 1 = image */
/* Wallpaper types: 0 = gradient, 1 = image */
static struct {
  int type;           /* 0 = gradient, 1 = JPEG image */
  uint8_t tr, tg, tb; /* Gradient: Top color */
  uint8_t br, bg, bb; /* Gradient: Bottom color */
  const char *name;   /* Display name */
  const char *path;   /* Image path (for type=1) */
} wallpapers[NUM_WALLPAPERS] = {
    {1, 0, 0, 0, 0, 0, 0, "Landscape", "/Pictures/landscape.jpg"},
    {1, 0, 0, 0, 0, 0, 0, "Nature", "/Pictures/nature.jpg"},
    {1, 0, 0, 0, 0, 0, 0, "City", "/Pictures/city.jpg"},
    {1, 0, 0, 0, 0, 0, 0, "Portrait", "/Pictures/portrait.jpg"},
    {1, 0, 0, 0, 0, 0, 0, "Wallpaper", "/Pictures/wallpaper.jpg"},
    {0, 30, 27, 75, 15, 27, 62, "Indigo Night", NULL},
    {0, 20, 60, 100, 10, 30, 60, "Ocean Blue", NULL},
    {0, 60, 20, 60, 30, 15, 45, "Purple Haze", NULL},
    {0, 20, 20, 20, 5, 5, 10, "Midnight", NULL},
    {0, 80, 60, 30, 40, 30, 20, "Golden Hour", NULL},
};

/* Cached wallpaper image for desktop background */
static media_image_t wallpaper_image = {0, 0, NULL};
static int wallpaper_loaded = -1; /* Which wallpaper is currently loaded */

/* Cached thumbnails for Background Settings window */
static media_image_t thumbnail_cache[NUM_WALLPAPERS] = {{0}};
static int thumbnails_loaded = 0;

/* Load all thumbnails once */
static void load_thumbnails(void) {
  if (thumbnails_loaded)
    return;

  for (int i = 0; i < NUM_WALLPAPERS; i++) {
    if (wallpapers[i].type == 1 && wallpapers[i].path) {
      uint8_t *data = NULL;
      size_t size = 0;
      if (media_load_file(wallpapers[i].path, &data, &size) == 0) {
        media_decode_jpeg(data, size, &thumbnail_cache[i]);
        media_free_file(data);
      }
    }
  }
  thumbnails_loaded = 1;
}
/* Static buffer for wallpaper to avoid heap fragmentation (4MB for 1024x1024)
 */
static uint32_t wallpaper_buffer[1024 * 1024];

/* Load wallpaper image if needed */
static void wallpaper_ensure_loaded(void) {
  if (wallpapers[current_wallpaper].type != 1)
    return; /* Gradient, no load */
  if (wallpaper_loaded == current_wallpaper && wallpaper_image.pixels != NULL)
    return; /* Already loaded */

  /* Reset previous image state (don't free valid static buffer, just reuse it)
   */
  wallpaper_image.width = 0;
  wallpaper_image.height = 0;
  wallpaper_loaded = -1;

  /* Load new image */
  const char *path = wallpapers[current_wallpaper].path;
  uint8_t *data = NULL;
  size_t size = 0;

  if (media_load_file(path, &data, &size) == 0) {
    if (media_decode_jpeg_buffer(data, size, &wallpaper_image, wallpaper_buffer,
                                 sizeof(wallpaper_buffer)) == 0) {
      wallpaper_loaded = current_wallpaper;
    } else {
      /* Fallback to gradient if decode fails */
      wallpapers[current_wallpaper].type = 0;
      wallpaper_loaded = -1;
    }
    media_free_file(data);
  } else {
    /* Fallback to gradient if load fails */
    wallpapers[current_wallpaper].type = 0;
  }
}

/* Get wallpaper pixel color at position */
static uint32_t wallpaper_get_pixel(int x, int y, int height) {
  int idx = current_wallpaper;

  /* Image wallpaper */
  if (wallpapers[idx].type == 1 && wallpaper_image.pixels) {
    /* Scale image to fit screen */
    int img_x = (x * wallpaper_image.width) / 1024; /* Assuming 1024 width */
    int img_y = (y * wallpaper_image.height) / height;
    if (img_x >= 0 && img_x < (int)wallpaper_image.width && img_y >= 0 &&
        img_y < (int)wallpaper_image.height) {
      return wallpaper_image.pixels[img_y * wallpaper_image.width + img_x];
    }
  }

  /* Gradient fallback */
  int progress = (y * 256) / height;
  if (progress < 0)
    progress = 0;
  if (progress > 255)
    progress = 255;

  uint8_t r = wallpapers[idx].tr +
              ((wallpapers[idx].br - wallpapers[idx].tr) * progress) / 256;
  uint8_t g = wallpapers[idx].tg +
              ((wallpapers[idx].bg - wallpapers[idx].tg) * progress) / 256;
  uint8_t b = wallpapers[idx].tb +
              ((wallpapers[idx].bb - wallpapers[idx].tb) * progress) / 256;

  return (r << 16) | (g << 8) | b;
}

/* Calculator state (global for click handling) */
static long calc_display = 0;
static long calc_pending = 0;
static char calc_op = 0;
static int calc_clear_next = 0;

static void calc_button_click(char key) {
  if (key >= '0' && key <= '9') {
    int digit = key - '0';
    if (calc_clear_next) {
      calc_display = digit;
      calc_clear_next = 0;
    } else {
      calc_display = calc_display * 10 + digit;
    }
  } else if (key == 'C') {
    calc_display = 0;
    calc_pending = 0;
    calc_op = 0;
    calc_clear_next = 0;
  } else if (key == '=') {
    if (calc_op == '+')
      calc_display = calc_pending + calc_display;
    else if (calc_op == '-')
      calc_display = calc_pending - calc_display;
    else if (calc_op == '*')
      calc_display = calc_pending * calc_display;
    else if (calc_op == '/' && calc_display != 0)
      calc_display = calc_pending / calc_display;
    calc_op = 0;
    calc_clear_next = 1;
  } else if (key == '+' || key == '-' || key == '*' || key == '/') {
    if (calc_op) {
      /* Chain operations */
      if (calc_op == '+')
        calc_display = calc_pending + calc_display;
      else if (calc_op == '-')
        calc_display = calc_pending - calc_display;
      else if (calc_op == '*')
        calc_display = calc_pending * calc_display;
      else if (calc_op == '/' && calc_display != 0)
        calc_display = calc_pending / calc_display;
    }
    calc_pending = calc_display;
    calc_op = key;
    calc_clear_next = 1;
  }
}

/* Notepad state (global for keyboard input) */
#define NOTEPAD_MAX_TEXT 2048
static char notepad_text[NOTEPAD_MAX_TEXT];
static char notepad_filepath[256]; /* Track open file */
static int notepad_cursor = 0;

/* Rename State */
static char rename_text[256];
static char rename_path[512];
static int rename_cursor = 0;

/* System Clipboard */
#define CLIPBOARD_MAX 1024
static char clipboard_buffer[CLIPBOARD_MAX];
static int clipboard_len = 0;

/* Terminal state (global for keyboard input) */
#define TERM_INPUT_MAX 256
#define TERM_HISTORY_LINES 16
static char term_input[TERM_INPUT_MAX];
static int term_input_len = 0;
static char term_history[TERM_HISTORY_LINES][80];
static int term_history_count = 0;
static int term_scroll = 0;

/* Snake game state */
#define SNAKE_MAX_LEN 100
#define SNAKE_GRID_W 20
#define SNAKE_GRID_H 12
static int snake_x[SNAKE_MAX_LEN];
static int snake_y[SNAKE_MAX_LEN];
static int snake_len = 4;
static int snake_dir = 1; /* 0=up, 1=right, 2=down, 3=left */
static int snake_food_x = 10;
static int snake_food_y = 6;
static int snake_score = 0;
static int snake_game_over = 0;

/* Mouse state (global for hover effects) */
static int mouse_x = 512, mouse_y = 384;
static int mouse_buttons = 0;

/* Trig tables for Clock (fixed point 8.8, scale 256) */
/* 0..59 corresponds to 0..360 degrees clockwise from top */
/* x = sin(angle), y = -cos(angle) */
static const int clock_sin[60] = {
    0,    26,   53,   79,   104,  128,  150,  171,  189,  205,  219,  231,
    240,  248,  253,  256,  253,  248,  240,  231,  219,  205,  189,  171,
    150,  128,  104,  79,   53,   26,   0,    -26,  -53,  -79,  -104, -128,
    -150, -171, -189, -205, -219, -231, -240, -248, -253, -256, -253, -248,
    -240, -231, -219, -205, -189, -171, -150, -128, -104, -79,  -53,  -26};
static const int clock_cos[60] = {
    -256, -253, -248, -240, -231, -219, -205, -189, -171, -150, -128, -104,
    -79,  -53,  -26,  0,    26,   53,   79,   104,  128,  150,  171,  189,
    205,  219,  231,  240,  248,  253,  256,  253,  248,  240,  231,  219,
    205,  189,  171,  150,  128,  104,  79,   53,   26,   0,    -26,  -53,
    -79,  -104, -128, -150, -171, -189, -205, -219, -231, -240, -248, -253};

/* Initialize snake game */
static void snake_init(void) {
  snake_len = 4;
  snake_dir = 1;
  snake_score = 0;
  snake_game_over = 0;
  /* Start in middle */
  for (int i = 0; i < snake_len; i++) {
    snake_x[i] = 5 - i;
    snake_y[i] = 6;
  }
  snake_food_x = 15;
  snake_food_y = 6;
}

/* Move snake one step */
static void snake_move(void) {
  if (snake_game_over)
    return;

  /* Calculate new head position */
  int new_x = snake_x[0];
  int new_y = snake_y[0];

  switch (snake_dir) {
  case 0:
    new_y--;
    break; /* up */
  case 1:
    new_x++;
    break; /* right */
  case 2:
    new_y++;
    break; /* down */
  case 3:
    new_x--;
    break; /* left */
  }

  /* Wrap around */
  if (new_x < 0)
    new_x = SNAKE_GRID_W - 1;
  if (new_x >= SNAKE_GRID_W)
    new_x = 0;
  if (new_y < 0)
    new_y = SNAKE_GRID_H - 1;
  if (new_y >= SNAKE_GRID_H)
    new_y = 0;

  /* Check self-collision */
  for (int i = 0; i < snake_len; i++) {
    if (snake_x[i] == new_x && snake_y[i] == new_y) {
      snake_game_over = 1;
      return;
    }
  }

  /* Check food collision */
  int ate_food = (new_x == snake_food_x && new_y == snake_food_y);
  if (ate_food) {
    snake_score += 10;
    if (snake_len < SNAKE_MAX_LEN - 1) {
      snake_len++;
    }
    /* New food position (simple pseudo-random) */
    snake_food_x = (snake_food_x * 7 + 3) % SNAKE_GRID_W;
    snake_food_y = (snake_food_y * 5 + 7) % SNAKE_GRID_H;
  }

  /* Move body */
  for (int i = snake_len - 1; i > 0; i--) {
    snake_x[i] = snake_x[i - 1];
    snake_y[i] = snake_y[i - 1];
  }
  snake_x[0] = new_x;
  snake_y[0] = new_y;
}

/* Snake key handler */
static void snake_key(int key) {
  if (snake_game_over) {
    /* Any key restarts */
    snake_init();
    return;
  }

  int new_dir = snake_dir;

  /* Arrow keys (special codes from virtio keyboard) */
  if (key == 0x100 || key == 'w' || key == 'W')
    new_dir = 0; /* Up */
  else if (key == 0x103 || key == 'd' || key == 'D')
    new_dir = 1; /* Right */
  else if (key == 0x101 || key == 's' || key == 'S')
    new_dir = 2; /* Down */
  else if (key == 0x102 || key == 'a' || key == 'A')
    new_dir = 3; /* Left */

  /* Prevent 180-degree turns */
  if ((snake_dir == 0 && new_dir == 2) || (snake_dir == 2 && new_dir == 0) ||
      (snake_dir == 1 && new_dir == 3) || (snake_dir == 3 && new_dir == 1)) {
    return;
  }

  snake_dir = new_dir;
  snake_move(); /* Move immediately on key press */
}

static void notepad_key(int key) {
  /* Ctrl+C - Copy all text to clipboard */
  if (key == 3) { /* ASCII 3 = Ctrl+C */
    clipboard_len = 0;
    for (int i = 0; i < notepad_cursor && i < CLIPBOARD_MAX - 1; i++) {
      clipboard_buffer[i] = notepad_text[i];
      clipboard_len++;
    }
    clipboard_buffer[clipboard_len] = '\0';
    return;
  }

  /* Ctrl+V - Paste from clipboard */
  if (key == 22) { /* ASCII 22 = Ctrl+V */
    for (int i = 0; i < clipboard_len && notepad_cursor < NOTEPAD_MAX_TEXT - 1;
         i++) {
      notepad_text[notepad_cursor++] = clipboard_buffer[i];
    }
    notepad_text[notepad_cursor] = '\0';
    return;
  }

  /* Ctrl+A - Select all (copy to clipboard) */
  if (key == 1) { /* ASCII 1 = Ctrl+A */
    clipboard_len = 0;
    for (int i = 0; i < notepad_cursor && i < CLIPBOARD_MAX - 1; i++) {
      clipboard_buffer[i] = notepad_text[i];
      clipboard_len++;
    }
    clipboard_buffer[clipboard_len] = '\0';
    return;
  }

  if (key == '\b' || key == 127) { /* Backspace */
    if (notepad_cursor > 0) {
      notepad_cursor--;
      notepad_text[notepad_cursor] = '\0';
    }
  } else if (key >= 32 && key < 127) { /* Printable */
    if (notepad_cursor < NOTEPAD_MAX_TEXT - 1) {
      notepad_text[notepad_cursor++] = (char)key;
      notepad_text[notepad_cursor] = '\0';
    }
  } else if (key == '\n' || key == '\r') { /* Enter */
    if (notepad_cursor < NOTEPAD_MAX_TEXT - 1) {
      notepad_text[notepad_cursor++] = '\n';
      notepad_text[notepad_cursor] = '\0';
    }
  }
}

static void rename_key(int key) {
  if (key == '\b' || key == 127) { /* Backspace */
    if (rename_cursor > 0) {
      rename_cursor--;
      rename_text[rename_cursor] = '\0';
    }
  } else if (key >= 32 && key < 127) { /* Printable */
    if (rename_cursor < 255) {
      rename_text[rename_cursor++] = (char)key;
      rename_text[rename_cursor] = '\0';
    }
  }
}

/* Terminal key handler */
static void terminal_key(int key) {
  if (key == '\b' || key == 127) { /* Backspace */
    if (term_input_len > 0) {
      term_input_len--;
      term_input[term_input_len] = '\0';
    }
  } else if (key == '\n' || key == '\r') { /* Enter - execute command */
    if (term_input_len > 0) {
      /* Save to history */
      if (term_history_count < TERM_HISTORY_LINES) {
        for (int i = 0; i < term_input_len && i < 79; i++) {
          term_history[term_history_count][i] = term_input[i];
        }
        term_history[term_history_count]
                    [term_input_len < 79 ? term_input_len : 79] = '\0';
        term_history_count++;
      }

      /* Check for commands */
      if (term_input[0] == 'h' && term_input[1] == 'e' &&
          term_input[2] == 'l' && term_input[3] == 'p') {
        /* Help command */
      } else if (term_input[0] == 'c' && term_input[1] == 'l' &&
                 term_input[2] == 'e' && term_input[3] == 'a' &&
                 term_input[4] == 'r') {
        term_history_count = 0;
      }
      /* Clear input */
      term_input_len = 0;
      term_input[0] = '\0';
    }
  } else if (key >= 32 && key < 127) { /* Printable */
    if (term_input_len < TERM_INPUT_MAX - 1) {
      term_input[term_input_len++] = (char)key;
      term_input[term_input_len] = '\0';
    }
  }
}

/* ===================================================================== */
/* Display Driver Interface */
/* ===================================================================== */

struct display {
  uint32_t width;
  uint32_t height;
  uint32_t pitch;
  uint32_t bpp;
  uint32_t *framebuffer;
  uint32_t *backbuffer;
};

static struct display primary_display = {0};

/* ===================================================================== */
/* Basic Drawing Functions */
/* ===================================================================== */

static inline void draw_pixel(int x, int y, uint32_t color) {
  if (x < 0 || x >= (int)primary_display.width)
    return;
  if (y < 0 || y >= (int)primary_display.height)
    return;

  uint32_t *target = primary_display.backbuffer ? primary_display.backbuffer
                                                : primary_display.framebuffer;
  if (target) {
    target[y * (primary_display.pitch / 4) + x] = color;
  }
}

void gui_draw_rect(int x, int y, int w, int h, uint32_t color) {
  for (int row = y; row < y + h; row++) {
    for (int col = x; col < x + w; col++) {
      draw_pixel(col, row, color);
    }
  }
}

void gui_draw_rect_outline(int x, int y, int w, int h, uint32_t color,
                           int thickness) {
  /* Top */
  gui_draw_rect(x, y, w, thickness, color);
  /* Bottom */
  gui_draw_rect(x, y + h - thickness, w, thickness, color);
  /* Left */
  gui_draw_rect(x, y, thickness, h, color);
  /* Right */
  gui_draw_rect(x + w - thickness, y, thickness, h, color);
}

void gui_draw_line(int x0, int y0, int x1, int y1, uint32_t color) {
  int dx = (x1 > x0) ? (x1 - x0) : (x0 - x1);
  int dy = (y1 > y0) ? (y1 - y0) : (y0 - y1);
  int sx = (x0 < x1) ? 1 : -1;
  int sy = (y0 < y1) ? 1 : -1;
  int err = dx - dy;

  while (1) {
    draw_pixel(x0, y0, color);
    if (x0 == x1 && y0 == y1)
      break;
    int e2 = 2 * err;
    if (e2 > -dy) {
      err -= dy;
      x0 += sx;
    }
    if (e2 < dx) {
      err += dx;
      y0 += sy;
    }
  }
}

void gui_draw_circle(int cx, int cy, int r, uint32_t color, bool filled) {
  int x = 0, y = r;
  int d = 3 - 2 * r;

  while (y >= x) {
    if (filled) {
      gui_draw_line(cx - x, cy + y, cx + x, cy + y, color);
      gui_draw_line(cx - x, cy - y, cx + x, cy - y, color);
      gui_draw_line(cx - y, cy + x, cx + y, cy + x, color);
      gui_draw_line(cx - y, cy - x, cx + y, cy - x, color);
    } else {
      draw_pixel(cx + x, cy + y, color);
      draw_pixel(cx - x, cy + y, color);
      draw_pixel(cx + x, cy - y, color);
      draw_pixel(cx - x, cy - y, color);
      draw_pixel(cx + y, cy + x, color);
      draw_pixel(cx - y, cy + x, color);
      draw_pixel(cx + y, cy - x, color);
      draw_pixel(cx - y, cy - x, color);
    }
    x++;
    if (d > 0) {
      y--;
      d = d + 4 * (x - y) + 10;
    } else {
      d = d + 4 * x + 6;
    }
  }
}

/* ===================================================================== */
/* 8x16 Font - use external complete font */
/* ===================================================================== */

#define FONT_WIDTH 8
#define FONT_HEIGHT 16

/* External font data from font.c - 256 characters */
extern const uint8_t font_data[256][16];

void gui_draw_char(int x, int y, char c, uint32_t fg, uint32_t bg) {
  /* Clip to screen bounds */
  if (x < 0 || x + FONT_WIDTH > (int)primary_display.width || y < 0 ||
      y + FONT_HEIGHT > (int)primary_display.height) {
    return;
  }

  unsigned char idx = (unsigned char)c;
  const uint8_t *glyph = font_data[idx];

  for (int row = 0; row < FONT_HEIGHT; row++) {
    uint8_t line = glyph[row];
    for (int col = 0; col < FONT_WIDTH; col++) {
      uint32_t color = (line & (0x80 >> col)) ? fg : bg;
      draw_pixel(x + col, y + row, color);
    }
  }
}

void gui_draw_string(int x, int y, const char *str, uint32_t fg, uint32_t bg) {
  int start_x = x;
  while (*str) {
    if (*str == '\n') {
      x = start_x;
      y += FONT_HEIGHT;
    } else {
      /* Only draw if within screen bounds */
      if (x >= 0 && x + FONT_WIDTH <= (int)primary_display.width && y >= 0 &&
          y + FONT_HEIGHT <= (int)primary_display.height) {
        gui_draw_char(x, y, *str, fg, bg);
      }
      x += FONT_WIDTH;
    }
    str++;
  }
}

/* ===================================================================== */
/* Window System */
/* ===================================================================== */

#define MAX_WINDOWS 64
#define TITLEBAR_HEIGHT 28
#define BORDER_WIDTH 2

typedef enum {
  WINDOW_NORMAL,
  WINDOW_MINIMIZED,
  WINDOW_MAXIMIZED,
  WINDOW_FULLSCREEN
} window_state_t;

struct window {
  int id;
  char title[64];
  int x, y;
  int width, height;
  window_state_t state;
  bool visible;
  bool focused;
  bool has_titlebar;
  bool resizable;
  uint32_t *content_buffer;
  void *userdata;

  /* Saved position for restore from maximize */
  int saved_x, saved_y;
  int saved_width, saved_height;

  /* Callbacks */
  void (*on_draw)(struct window *win);
  void (*on_key)(struct window *win, int key);
  void (*on_mouse)(struct window *win, int x, int y, int buttons);
  void (*on_close)(struct window *win);

  struct window *next;
};

static struct window windows[MAX_WINDOWS];
static struct window *window_stack = NULL; /* Z-order, top is focused */
static struct window *focused_window = NULL;
static int next_window_id = 1;

/* Create a new window */
struct window *gui_create_window(const char *title, int x, int y, int w,
                                 int h) {
  /* Find free slot */
  struct window *win = NULL;
  for (int i = 0; i < MAX_WINDOWS; i++) {
    if (windows[i].id == 0) {
      win = &windows[i];
      break;
    }
  }

  if (!win) {
    printk(KERN_ERR "GUI: No free window slots\n");
    return NULL;
  }

  win->id = next_window_id++;
  for (int i = 0; i < 63 && title[i]; i++) {
    win->title[i] = title[i];
    win->title[i + 1] = '\0';
  }
  win->x = x;
  win->y = y;
  win->width = w;
  win->height = h;
  win->state = WINDOW_NORMAL;
  win->visible = true;
  win->focused = false;
  win->has_titlebar = true;
  win->resizable = true;

  /* Reset all callbacks and userdata - critical to prevent stale pointers */
  win->on_draw = NULL;
  win->on_mouse = NULL;
  win->on_key = NULL;
  win->on_close = NULL;
  win->userdata = NULL;

  /* Allocate content buffer */
  int content_h = h - TITLEBAR_HEIGHT - BORDER_WIDTH * 2;
  int content_w = w - BORDER_WIDTH * 2;
  win->content_buffer = kmalloc(content_w * content_h * 4);

  /* Add to stack */
  win->next = window_stack;
  window_stack = win;

  printk(KERN_INFO "GUI: Created window '%s' (%dx%d)\n", title, w, h);

  return win;
}

void gui_set_window_userdata(struct window *win, void *data) {
  if (win) {
    win->userdata = data;
  }
}

void gui_destroy_window(struct window *win) {
  if (!win || win->id == 0)
    return;

  if (win->on_close) {
    win->on_close(win);
  }

  /* Remove from stack */
  if (window_stack == win) {
    window_stack = win->next;
  } else {
    struct window *prev = window_stack;
    while (prev && prev->next != win) {
      prev = prev->next;
    }
    if (prev) {
      prev->next = win->next;
    }
  }

  if (win->content_buffer) {
    kfree(win->content_buffer);
  }

  win->id = 0;
}

void gui_focus_window(struct window *win) {
  if (!win)
    return;

  if (focused_window) {
    focused_window->focused = false;
  }

  /* Move to top of stack */
  if (window_stack != win) {
    struct window *prev = window_stack;
    while (prev && prev->next != win) {
      prev = prev->next;
    }
    if (prev) {
      prev->next = win->next;
      win->next = window_stack;
      window_stack = win;
    }
  }

  win->focused = true;
  focused_window = win;
}

/* Draw a filled circle (for traffic light buttons) */
static void draw_circle(int cx, int cy, int r, uint32_t color) {
  for (int y = -r; y <= r; y++) {
    for (int x = -r; x <= r; x++) {
      if (x * x + y * y <= r * r) {
        draw_pixel(cx + x, cy + y, color);
      }
    }
  }
}

/* Draw a single window */
/* ===================================================================== */
/* Render Helpers */
/* ===================================================================== */

extern struct file *vfs_open(const char *path, int flags, mode_t mode);
extern int vfs_close(struct file *file);
extern int vfs_readdir(struct file *file, void *ctx,
                       int (*filldir)(void *, const char *, int, loff_t, ino_t,
                                      unsigned));

/* Forward declaration */
/* Helper for string compare */
static int str_cmp(const char *s1, const char *s2) {
  while (*s1 && (*s1 == *s2)) {
    s1++;
    s2++;
  }
  return *(const unsigned char *)s1 - *(const unsigned char *)s2;
}

static char to_lower(char c) {
  if (c >= 'A' && c <= 'Z')
    return (char)(c + 32);
  return c;
}

static int str_ends_with_ci(const char *name, const char *ext) {
  if (!name || !ext)
    return 0;
  int nlen = 0;
  int elen = 0;
  while (name[nlen])
    nlen++;
  while (ext[elen])
    elen++;
  if (elen == 0 || nlen < elen)
    return 0;
  for (int i = 0; i < elen; i++) {
    if (to_lower(name[nlen - elen + i]) != to_lower(ext[i]))
      return 0;
  }
  return 1;
}

static void draw_icon(int x, int y, int size, const unsigned char *icon,
                      uint32_t fg_color, uint32_t bg_color);

struct fm_state {
  char path[256];
  char selected[256];
  int scroll_y;
};

struct image_viewer_state {
  media_image_t image;
};

/* Forward declarations for modern image viewer (defined later in file) */
struct modern_image_viewer_state;
static struct {
  media_image_t image;
  media_image_t rotated;
  int loaded;
  int zoom_pct;
  int offset_x;
  int offset_y;
  int dragging;
  int drag_start_x;
  int drag_start_y;
  char current_file[256];
  int current_image_index;
  int rotation;
  int fullscreen;
  int show_toolbar;
  int toolbar_timer;
  int crop_mode;
  int crop_x1, crop_y1;
  int crop_x2, crop_y2;
  /* Folder-based navigation */
  char folder_path[256];
  char file_list[32][64]; /* Up to 32 files, 64 chars each */
  int file_count;
  int file_index; /* Current index in folder */
} g_imgview = {0};
static void image_viewer_on_draw(struct window *win);
static void image_viewer_on_mouse(struct window *win, int x, int y,
                                  int buttons);

void gui_open_image_viewer(const char *path);
static void gui_play_mp3_file(const char *path);

/* Context for finding clicked item */
struct find_ctx {
  int target_slot_x;
  int target_slot_y;
  int cur_x;
  int cur_y;
  int start_x;
  int cur_slot;
  struct fm_state *st;
  int clicked;
  int click_x, click_y;
  int slot_w, slot_h;
  int win_w;
};

static int find_callback(void *ctx, const char *name, int len, loff_t off,
                         ino_t ino, unsigned type) {
  (void)off;
  (void)ino;
  struct find_ctx *fc = (struct find_ctx *)ctx;
  if (fc->clicked)
    return 0;

  if (name[0] == '.')
    return 0;

  /* Check if click is in this slot */
  if (fc->click_x >= fc->cur_x && fc->click_x < fc->cur_x + fc->slot_w &&
      fc->click_y >= fc->cur_y && fc->click_y < fc->cur_y + fc->slot_h) {

    /* HIT! */
    fc->clicked = 1;

    /* Handle selection */
    int i;
    for (i = 0; i < len && i < 255; i++)
      fc->st->selected[i] = name[i];
    fc->st->selected[i] = '\0';

    /* Handle Double Click (Primitive: if already selected, enter) */
    if (type == 4) { /* Directory */
      /* Append path */
      int plen = 0;
      while (fc->st->path[plen])
        plen++;
      /* Check if path ends in / */
      int need_slash = (plen > 0 && fc->st->path[plen - 1] != '/');
      if (plen + len + need_slash + 1 < 256) {
        if (need_slash)
          fc->st->path[plen++] = '/';
        for (i = 0; i < len; i++)
          fc->st->path[plen++] = name[i];
        fc->st->path[plen] = '\0';
        fc->st->selected[0] = '\0';
      }
    }
    return 1; /* Stop */
  }

  /* Advance */
  fc->cur_x += fc->slot_w;
  if (fc->cur_x + fc->slot_w > fc->start_x + fc->win_w) {
    fc->cur_x = fc->start_x;
    fc->cur_y += fc->slot_h;
  }
  return 0;
}

struct fm_ctx {
  struct window *win;
  int x, y;
  int start_x, start_y;
  int cur_x, cur_y;
  int max_x, max_y; /* Bounds for clipping */
  struct fm_state *state;
};

static int fm_render_callback(void *ctx, const char *name, int len,
                              loff_t offset, ino_t ino, unsigned type) {
  (void)offset;
  (void)ino;
  struct fm_ctx *c = (struct fm_ctx *)ctx;

  /* Skip . and .. */
  if (name[0] == '.')
    return 0;

  int icon_size = 32;
  int slot_w = 80;
  int slot_h = 70;

  int dx = c->cur_x;
  int dy = c->cur_y;

  /* Skip if icon would be outside visible content area */
  if (dy + slot_h > c->max_y) {
    /* Still advance position for proper layout calculation */
    c->cur_x += slot_w;
    if (c->cur_x + slot_w > c->max_x) {
      c->cur_x = c->start_x;
      c->cur_y += slot_h;
    }
    return 0; /* Don't draw, but continue iterating */
  }

  /* Select icon */
  /* Check for known extensions or just file vs dir */
  /* Simple check: type (DT_DIR included in ramfs.c as shifted mode) */
  /* VFS readdir passes mode >> 12. S_IFDIR is 0040000. >> 12 is 4. */
  /* S_IFREG is 0100000. >> 12 is 8 (010). */

  const unsigned char *bmp = icon_notepad; /* Default file */
  uint32_t color = 0xCCCCCC;

  /* S_IFDIR >> 12 is 4 */
  if (type == 4) {
    bmp = icon_files; /* Folder icon */
    color = 0x3B82F6;
  } else {
    /* Check extension */
    if (len > 4 && name[len - 4] == '.' && name[len - 3] == 't' &&
        name[len - 2] == 'x' && name[len - 1] == 't') {
      bmp = icon_notepad;
      color = 0xFFFFFF;
    } else if (str_ends_with_ci(name, ".jpg") ||
               str_ends_with_ci(name, ".jpeg")) {
      color = 0xF9E2AF;
    } else if (str_ends_with_ci(name, ".mp3")) {
      color = 0xA6E3A1;
    } else if (str_ends_with_ci(name, ".py")) {
      bmp = icon_python;
      color = 0xFFD43B; /* Python yellow */
    } else if (str_ends_with_ci(name, ".nano")) {
      bmp = icon_nano;
      color = 0x22C55E; /* NanoLang green */
    }
  }

  /* Check if selected */
  int is_selected = 0;
  if (c->state && str_cmp(c->state->selected, name) == 0) {
    is_selected = 1;
  }

  /* Selection Box */
  if (is_selected) {
    gui_draw_rect(dx + 2, dy + 2, slot_w - 4, slot_h - 4, 0x404050);
    gui_draw_rect_outline(dx + 2, dy + 2, slot_w - 4, slot_h - 4, 0x606080, 1);
  }

  /* Draw Icon */
  draw_icon(dx + (slot_w - icon_size) / 2, dy + 8, icon_size, bmp, color,
            is_selected ? 0x404050 : 0x1E1E2E);

  /* Draw Label */
  int lbl_len = len > 10 ? 10 : len;
  char lbl[12];
  for (int i = 0; i < lbl_len; i++)
    lbl[i] = name[i];
  lbl[lbl_len] = '\0';

  /* Center text */
  gui_draw_string(dx + (slot_w - lbl_len * 8) / 2, dy + icon_size + 12, lbl,
                  0xFFFFFF, is_selected ? 0x404050 : 0x1E1E2E);

  /* Advance position */
  c->cur_x += slot_w;
  if (c->cur_x + slot_w > c->max_x) {
    c->cur_x = c->start_x;
    c->cur_y += slot_h;
  }

  return 0;
}

/* File Manager Mouse Handler */
static void fm_on_mouse(struct window *win, int x, int y, int buttons) {
  struct fm_state *st = (struct fm_state *)win->userdata;
  (void)buttons;
  if (!st)
    return;

  /* Handle Toolbar Clicks */
  int toolbar_h = 40;

  /* Toolbar is drawn below titlebar */
  /* Relative Y: BORDER_WIDTH + TITLEBAR_HEIGHT */
  int tb_start_y = BORDER_WIDTH + TITLEBAR_HEIGHT;
  int tb_end_y = tb_start_y + toolbar_h;

  if (y >= tb_start_y && y < tb_end_y) {
    /* Back Button: x relative to window = BORDER_WIDTH + 10 */
    if (x >= BORDER_WIDTH + 10 && x < BORDER_WIDTH + 70) {
      /* Go to parent */
      int len = 0;
      while (st->path[len])
        len++;
      if (len > 1) { /* Not root */
        while (len > 0 && st->path[len - 1] != '/')
          len--;

        /* If we found the root slash at index 0 (len=1), keep it. */
        /* If we found a slash elsewhere (len>1), remove it (len--). */
        if (len > 1)
          len--;

        st->path[len] = '\0';
        st->selected[0] = '\0';
        if (len == 0) {
          st->path[0] = '/';
          st->path[1] = '\0';
        }
      }
    }

    /* New Folder: 80px offset */
    if (x >= BORDER_WIDTH + 80 && x < BORDER_WIDTH + 180) {
      /* Create "NewFolder" */
      char new_path[512];
      int p_len = 0;
      while (st->path[p_len]) {
        new_path[p_len] = st->path[p_len];
        p_len++;
      }
      if (new_path[p_len - 1] != '/') {
        new_path[p_len] = '/';
        p_len++;
      }

      const char *base = "NewFolder";
      for (int i = 0; base[i]; i++) {
        new_path[p_len] = base[i];
        p_len++;
      }
      new_path[p_len] = '\0';

      /* Try to create */
      extern int vfs_mkdir(const char *path, mode_t mode);
      vfs_mkdir(new_path, 0755);
    }

    /* New File: 190px offset */
    if (x >= BORDER_WIDTH + 190 && x < BORDER_WIDTH + 280) {
      /* Create "NewFile.txt" */
      /* ... (existing logic) ... */
      char new_path[512];
      int p_len = 0;
      while (st->path[p_len]) {
        new_path[p_len] = st->path[p_len];
        p_len++;
      }
      if (new_path[p_len - 1] != '/') {
        new_path[p_len] = '/';
        p_len++;
      }

      const char *base = "NewFile.txt";
      for (int i = 0; base[i]; i++) {
        new_path[p_len] = base[i];
        p_len++;
      }
      new_path[p_len] = '\0';

      /* Try to create */
      extern int vfs_create(const char *path, mode_t mode);
      vfs_create(new_path, 0644);
    }

    /* Rename: 290px offset */
    if (x >= BORDER_WIDTH + 290 && x < BORDER_WIDTH + 380) {
      if (st->selected[0]) {
        /* Build full path */
        char full_path[512];
        int idx = 0;
        int p_len = 0;
        while (st->path[p_len]) {
          full_path[idx++] = st->path[p_len++];
        }
        if (idx > 0 && full_path[idx - 1] != '/')
          full_path[idx++] = '/';
        else if (idx == 0)
          full_path[idx++] = '/';

        int s_len = 0;
        while (st->selected[s_len]) {
          full_path[idx++] = st->selected[s_len++];
        }
        full_path[idx] = '\0';

        extern void gui_open_rename(const char *path);
        gui_open_rename(full_path);
      }
    }

    return;
  }

  /* Handle Grid Clicks */
  struct file *dir = vfs_open(st->path, O_RDONLY, 0);
  if (!dir)
    return;

  /* Grid Clicks */
  /* Content starts below toolbar */
  int content_x = BORDER_WIDTH + 10;
  int content_y = BORDER_WIDTH + TITLEBAR_HEIGHT + 40 + 10;

  struct find_ctx fctx;
  fctx.cur_x = content_x;
  fctx.cur_y = content_y;
  fctx.start_x = content_x;
  fctx.st = st;
  fctx.clicked = 0;
  fctx.click_x = x;
  fctx.click_y = y;

  /* Initialize dimensions */
  fctx.slot_w = 80;
  fctx.slot_h = 70;
  fctx.win_w = win->width - 40; /* Match wrapped logic in render callback */
  fctx.slot_w = 80;
  fctx.slot_h = 70;
  fctx.win_w = win->width - 40;

  extern int vfs_readdir(
      struct file * file, void *ctx,
      int (*filldir)(void *, const char *, int, loff_t, ino_t, unsigned));
  vfs_readdir(dir, &fctx, find_callback);

  vfs_close(dir);

  if (fctx.clicked) {
    /* Check if it's a file (.txt) */
    int len = 0;
    while (st->selected[len])
      len++;

    /* Build full path once */
    char full_path[512];
    int idx = 0;
    int p_len = 0;
    while (st->path[p_len]) {
      full_path[idx++] = st->path[p_len++];
    }
    if (idx > 0 && full_path[idx - 1] != '/')
      full_path[idx++] = '/';
    else if (idx == 0)
      full_path[idx++] = '/';
    int s_len = 0;
    while (st->selected[s_len]) {
      full_path[idx++] = st->selected[s_len++];
    }
    full_path[idx] = '\0';

    if (str_ends_with_ci(st->selected, ".txt")) {
      gui_open_notepad(full_path);
    } else if (str_ends_with_ci(st->selected, ".jpg") ||
               str_ends_with_ci(st->selected, ".jpeg") ||
               str_ends_with_ci(st->selected, ".png")) {
      gui_open_image_viewer(full_path);
    } else if (str_ends_with_ci(st->selected, ".mp3")) {
      gui_play_mp3_file(full_path);
    } else if (str_ends_with_ci(st->selected, ".py") ||
               str_ends_with_ci(st->selected, ".nano")) {
      /* Python/NanoLang file - open new terminal and run it */
      extern struct terminal *term_create(int x, int y, int cols, int rows);
      extern void term_set_active(struct terminal * term);
      extern void term_puts(struct terminal * term, const char *str);
      extern void term_execute_command(struct terminal * term, const char *cmd);
      extern void term_set_content_pos(struct terminal * t, int x, int y);

      /* Stagger window positions */
      static int term_spawn_x = 120;
      static int term_spawn_y = 100;

      struct window *term_win =
          gui_create_window("Terminal", term_spawn_x, term_spawn_y, 500, 350);
      if (term_win) {
        /* Create terminal with position relative to window content area */
        int content_x = term_spawn_x + BORDER_WIDTH;
        int content_y = term_spawn_y + BORDER_WIDTH + TITLEBAR_HEIGHT;
        struct terminal *term = term_create(content_x, content_y, 60, 18);
        if (term) {
          /* Store terminal in window and set as active */
          term_win->userdata = term;
          term_set_active(term);
          term_set_content_pos(term, content_x, content_y);

          /* Build run command */
          char run_cmd[300] = "run ";
          int j = 4;
          for (int i = 0; full_path[i] && j < 298; i++) {
            run_cmd[j++] = full_path[i];
          }
          run_cmd[j] = '\0';
          /* Execute the run command */
          term_execute_command(term, run_cmd);
          term_puts(term, "\n\033[32mvib-os\033[0m:\033[34m~\033[0m$ ");
        }
      }

      /* Stagger next window */
      term_spawn_x = (term_spawn_x + 40) % 300 + 80;
      term_spawn_y = (term_spawn_y + 35) % 200 + 70;
    } else {
      /* Directory - Navigate if it's a dir */
      struct file *entry = vfs_open(full_path, O_RDONLY, 0);
      if (entry && entry->f_dentry && entry->f_dentry->d_inode &&
          S_ISDIR(entry->f_dentry->d_inode->i_mode)) {
        int p = 0;
        while (st->path[p])
          p++;
        if (st->path[p - 1] != '/') {
          st->path[p++] = '/';
        }
        int s = 0;
        while (st->selected[s]) {
          st->path[p++] = st->selected[s++];
        }
        st->path[p] = '\0';
        st->selected[0] = '\0';
      }
      if (entry)
        vfs_close(entry);
    }
  }
}

static void image_viewer_on_close(struct window *win) {
  if (!win || !win->userdata)
    return;
  struct image_viewer_state *st = (struct image_viewer_state *)win->userdata;
  media_free_image(&st->image);
  kfree(st);
  win->userdata = NULL;
}

static void draw_image_viewer(struct window *win, int content_x, int content_y,
                              int content_w, int content_h) {
  if (!win || !win->userdata)
    return;
  struct image_viewer_state *st = (struct image_viewer_state *)win->userdata;
  if (!st->image.pixels || st->image.width == 0 || st->image.height == 0)
    return;

  int img_w = (int)st->image.width;
  int img_h = (int)st->image.height;
  int draw_w = img_w;
  int draw_h = img_h;

  if (draw_w > content_w) {
    draw_w = content_w;
    draw_h = (img_h * draw_w) / img_w;
  }
  if (draw_h > content_h) {
    draw_h = content_h;
    draw_w = (img_w * draw_h) / img_h;
  }
  if (draw_w <= 0 || draw_h <= 0)
    return;

  int offset_x = content_x + (content_w - draw_w) / 2;
  int offset_y = content_y + (content_h - draw_h) / 2;

  for (int y = 0; y < draw_h; y++) {
    int src_y = (y * img_h) / draw_h;
    for (int x = 0; x < draw_w; x++) {
      int src_x = (x * img_w) / draw_w;
      uint32_t color = st->image.pixels[src_y * img_w + src_x];
      draw_pixel(offset_x + x, offset_y + y, color);
    }
  }
}

void gui_open_image_viewer(const char *path) {
  if (!path)
    return;

  /* Load image file */
  uint8_t *data = NULL;
  size_t size = 0;
  if (media_load_file(path, &data, &size) != 0) {
    printk("Image Viewer: Failed to read %s\n", path);
    return;
  }

  /* Free previous image if loaded */
  if (g_imgview.loaded) {
    media_free_image(&g_imgview.image);
    g_imgview.loaded = 0;
  }

  /* Decode new image into global state - detect format by magic bytes */
  int decode_ret = -1;
  /* PNG magic: 0x89 'P' 'N' 'G' */
  if (size >= 4 && data[0] == 0x89 && data[1] == 'P' && data[2] == 'N' &&
      data[3] == 'G') {
    decode_ret = media_decode_png(data, size, &g_imgview.image);
    if (decode_ret != 0) {
      printk("Image Viewer: PNG decode failed\n");
    }
  } else {
    /* Assume JPEG */
    decode_ret = media_decode_jpeg(data, size, &g_imgview.image);
    if (decode_ret != 0) {
      printk("Image Viewer: JPEG decode failed\n");
    }
  }
  if (decode_ret != 0) {
    media_free_file(data);
    return;
  }
  media_free_file(data);

  /* Set up viewer state */
  g_imgview.loaded = 1;
  g_imgview.zoom_pct = 0; /* Auto-fit */
  g_imgview.offset_x = 0;
  g_imgview.offset_y = 0;
  g_imgview.rotation = 0;
  g_imgview.fullscreen = 0;
  g_imgview.current_image_index = -1; /* -1 means file-loaded, not bootstrap */

  /* Extract folder path and filename */
  int i = 0;
  int last_slash = -1;
  while (path[i]) {
    if (path[i] == '/')
      last_slash = i;
    i++;
  }

  /* Copy folder path */
  for (int j = 0; j <= last_slash && j < 255; j++) {
    g_imgview.folder_path[j] = path[j];
  }
  g_imgview.folder_path[last_slash + 1] = '\0';

  /* Copy filename */
  const char *filename = path + last_slash + 1;
  i = 0;
  while (filename[i] && i < 255) {
    g_imgview.current_file[i] = filename[i];
    i++;
  }
  g_imgview.current_file[i] = '\0';

  /* Scan folder for image files - use hardcoded list for /Pictures */
  g_imgview.file_count = 0;
  g_imgview.file_index = 0;

  /* Known image files in Pictures folder */
  static const char *pictures_files[] = {
      "test.png",      "pig.jpg",    "city.jpg",     "nature.jpg",
      "wallpaper.jpg", "square.jpg", "portrait.jpg", "landscape.jpg"};
  int num_pictures = sizeof(pictures_files) / sizeof(pictures_files[0]);

  /* Check if we're in Pictures folder */
  int is_pictures =
      (g_imgview.folder_path[0] == '/' && g_imgview.folder_path[1] == 'P' &&
       g_imgview.folder_path[2] == 'i' && g_imgview.folder_path[3] == 'c');

  if (is_pictures) {
    for (int j = 0; j < num_pictures && g_imgview.file_count < 32; j++) {
      /* Copy filename to list */
      int k = 0;
      while (pictures_files[j][k] && k < 63) {
        g_imgview.file_list[g_imgview.file_count][k] = pictures_files[j][k];
        k++;
      }
      g_imgview.file_list[g_imgview.file_count][k] = '\0';

      /* Check if this is current file */
      int match = 1;
      for (int m = 0; filename[m] || pictures_files[j][m]; m++) {
        if (filename[m] != pictures_files[j][m]) {
          match = 0;
          break;
        }
      }
      if (match) {
        g_imgview.file_index = g_imgview.file_count;
      }
      g_imgview.file_count++;
    }
  }

  printk("Image Viewer: Loaded %s (%dx%d) - %d images in folder\n",
         g_imgview.current_file, g_imgview.image.width, g_imgview.image.height,
         g_imgview.file_count);

  /* Create modern viewer window */
  struct window *win = gui_create_window("Image Viewer", 80, 60, 800, 600);
  if (win) {
    win->on_draw = image_viewer_on_draw;
    win->on_mouse = image_viewer_on_mouse;
  }
}

static void gui_play_mp3_file(const char *path) {
  if (!path)
    return;

  uint8_t *data = NULL;
  size_t size = 0;
  if (media_load_file(path, &data, &size) != 0) {
    printk("Audio: Failed to read %s\n", path);
    return;
  }

  media_audio_t audio;
  audio.samples = NULL;
  audio.sample_count = 0;
  audio.sample_rate = 0;
  audio.channels = 0;

  if (media_decode_mp3(data, size, &audio) != 0) {
    printk("Audio: MP3 decode failed\n");
    media_free_file(data);
    return;
  }
  media_free_file(data);

  extern int intel_hda_play_pcm(const void *data, uint32_t samples,
                                uint8_t channels, uint32_t sample_rate);
  intel_hda_play_pcm(audio.samples, audio.sample_count, audio.channels,
                     audio.sample_rate);
  media_free_audio(&audio);
}

static void draw_window(struct window *win) {
  // ... rest of function ...
  if (!win->visible)
    return;

  int x = win->x, y = win->y;
  int w = win->width, h = win->height;

  /* Draw border */
  gui_draw_rect_outline(x, y, w, h, THEME_BORDER, BORDER_WIDTH);

  if (win->has_titlebar) {
    /* Draw title bar - Modern dark with subtle gradient effect */
    uint32_t titlebar_color =
        win->focused ? THEME_TITLEBAR : THEME_TITLEBAR_INACTIVE;

    /* Fill titlebar base */
    gui_draw_rect(x + BORDER_WIDTH, y + BORDER_WIDTH, w - BORDER_WIDTH * 2,
                  TITLEBAR_HEIGHT, titlebar_color);

    /* Subtle highlight at top (gradient simulation) */
    gui_draw_rect(x + BORDER_WIDTH, y + BORDER_WIDTH, w - BORDER_WIDTH * 2, 1,
                  titlebar_color + 0x101010);

    /* Bottom separator line */
    gui_draw_rect(x + BORDER_WIDTH, y + BORDER_WIDTH + TITLEBAR_HEIGHT - 1,
                  w - BORDER_WIDTH * 2, 1, 0x18181B);

    /* Traffic light buttons on LEFT side - Modern rounded */
    int btn_cx = x + BORDER_WIDTH + 16; /* First circle center X */
    int btn_cy = y + BORDER_WIDTH + TITLEBAR_HEIGHT / 2; /* Center Y */
    int btn_r = 6;                                       /* Button radius */

    /* Close button - Red */
    draw_circle(btn_cx, btn_cy, btn_r, COLOR_BTN_CLOSE);
    /* Draw X icon */
    for (int i = -2; i <= 2; i++) {
      draw_pixel(btn_cx + i, btn_cy + i, 0x7F1D1D);
      draw_pixel(btn_cx + i, btn_cy - i, 0x7F1D1D);
    }

    /* Minimize button - Amber */
    btn_cx += 18;
    draw_circle(btn_cx, btn_cy, btn_r, COLOR_BTN_MINIMIZE);
    /* Draw - icon */
    for (int i = -2; i <= 2; i++) {
      draw_pixel(btn_cx + i, btn_cy, 0x78350F);
    }

    /* Zoom button - Green */
    btn_cx += 18;
    draw_circle(btn_cx, btn_cy, btn_r, COLOR_BTN_ZOOM);
    /* Draw + icon */
    for (int i = -2; i <= 2; i++) {
      draw_pixel(btn_cx + i, btn_cy, 0x14532D);
      draw_pixel(btn_cx, btn_cy + i, 0x14532D);
    }

    /* Window title - centered with modern font styling */
    int title_len = 0;
    for (const char *p = win->title; *p; p++)
      title_len++;
    int title_x = x + (w - title_len * 8) / 2;
    gui_draw_string(title_x, y + BORDER_WIDTH + 7, win->title,
                    win->focused ? 0xFAFAFA : 0x9CA3AF, titlebar_color);
  }

  /* Draw content area */
  int content_x = x + BORDER_WIDTH;
  int content_y = y + BORDER_WIDTH + (win->has_titlebar ? TITLEBAR_HEIGHT : 0);
  int content_w = w - BORDER_WIDTH * 2;
  int content_h =
      h - BORDER_WIDTH * 2 - (win->has_titlebar ? TITLEBAR_HEIGHT : 0);

  gui_draw_rect(content_x, content_y, content_w, content_h, THEME_BG);

  /* Draw window-specific content based on title */
  /* Calculator - Modern Design */
  if (win->title[0] == 'C' && win->title[1] == 'a' && win->title[2] == 'l') {
    /* Modern dark background */
    gui_draw_rect(content_x, content_y, content_w, content_h, 0x1C1C1E);

    /* Display area - gradient effect (dark to slightly lighter) */
    int disp_h = 70;
    gui_draw_rect(content_x + 12, content_y + 12, content_w - 24, disp_h,
                  0x2C2C2E);
    gui_draw_rect(content_x + 12, content_y + 12, content_w - 24, 2, 0x3C3C3E);

    /* Display value - large right-aligned */
    char display[16];
    long v = calc_display;
    int is_neg = 0;
    if (v < 0) {
      is_neg = 1;
      v = -v;
    }
    int idx = 0;
    if (v == 0) {
      display[idx++] = '0';
    } else {
      char tmp[16];
      int ti = 0;
      while (v > 0 && ti < 14) {
        tmp[ti++] = '0' + (v % 10);
        v /= 10;
      }
      if (is_neg)
        display[idx++] = '-';
      while (ti > 0) {
        display[idx++] = tmp[--ti];
      }
    }
    display[idx] = '\0';
    /* Draw display value with simulated large font (double-draw) */
    int text_x = content_x + content_w - 20 - idx * 12;
    int text_y = content_y + 40;
    gui_draw_string(text_x, text_y, display, 0xFFFFFF, 0x2C2C2E);
    gui_draw_string(text_x + 1, text_y, display, 0xFFFFFF, 0x2C2C2E);

    /* Button grid - 4x5 layout with proper spacing */
    static const char *btns[5][4] = {{"C", "+/-", "%", "/"},
                                     {"7", "8", "9", "*"},
                                     {"4", "5", "6", "-"},
                                     {"1", "2", "3", "+"},
                                     {"0", "0", ".", "="}};

    int grid_x = content_x + 12;
    int grid_y = content_y + disp_h + 20;
    int grid_w = content_w - 24;
    int grid_h = content_h - disp_h - 32;
    int bw = (grid_w - 12) / 4; /* 3 gaps of 4px */
    int bh = (grid_h - 16) / 5; /* 4 gaps of 4px */
    int gap = 4;

    for (int row = 0; row < 5; row++) {
      for (int col = 0; col < 4; col++) {
        /* Skip duplicate "0" cell */
        if (row == 4 && col == 1)
          continue;

        int bx = grid_x + col * (bw + gap);
        int by = grid_y + row * (bh + gap);
        int btn_w = bw;

        /* "0" button spans 2 columns */
        if (row == 4 && col == 0) {
          btn_w = bw * 2 + gap;
        }

        /* Button colors */
        uint32_t bg, fg;
        char btn_char = btns[row][col][0];

        if (btn_char == '/' || btn_char == '*' || btn_char == '-' ||
            btn_char == '+' || btn_char == '=') {
          /* Orange operator buttons */
          bg = 0xFF9F0A;
          fg = 0xFFFFFF;
        } else if (btn_char == 'C' || btn_char == '+' || btn_char == '%') {
          /* Light gray function buttons */
          bg = 0xA5A5A5;
          fg = 0x000000;
        } else {
          /* Dark gray number buttons */
          bg = 0x333333;
          fg = 0xFFFFFF;
        }

        /* Draw button with rounded effect (lighter top edge) */
        gui_draw_rect(bx, by, btn_w, bh, bg);
        gui_draw_rect(bx, by, btn_w, 2, (bg & 0xFEFEFE) + 0x202020);

        /* Center text in button */
        const char *label = btns[row][col];
        int label_len = 0;
        while (label[label_len])
          label_len++;
        int tx = bx + (btn_w - label_len * 8) / 2;
        int ty = by + (bh - 16) / 2;
        gui_draw_string(tx, ty, label, fg, bg);
      }
    }
  }
  /* File Manager */
  else if (win->title[0] == 'F' && win->title[1] == 'i' &&
           win->title[2] == 'l') {
    int yy = content_y;
    int toolbar_h = 40;

    /* Toolbar */
    gui_draw_rect(content_x, yy, content_w, toolbar_h, 0x2A2A35);
    gui_draw_line(content_x, yy + toolbar_h, content_x + content_w,
                  yy + toolbar_h, 0x404050);

    /* Back Button */
    gui_draw_rect(content_x + 10, yy + 8, 60, 24, 0x404050);
    gui_draw_string(content_x + 22, yy + 12, "Back", 0xFFFFFF, 0x404050);

    /* New Folder Button */
    gui_draw_rect(content_x + 80, yy + 8, 100, 24, 0x404050);
    gui_draw_string(content_x + 90, yy + 12, "New Folder", 0xFFFFFF, 0x404050);

    /* New File Button */
    gui_draw_rect(content_x + 190, yy + 8, 90, 24, 0x404050);
    gui_draw_string(content_x + 200, yy + 12, "New File", 0xFFFFFF, 0x404050);

    /* Rename Button */
    gui_draw_rect(content_x + 290, yy + 8, 90, 24, 0x404050);
    gui_draw_string(content_x + 300, yy + 12, "Rename", 0xFFFFFF, 0x404050);

    yy += toolbar_h;

    struct fm_state *st = (struct fm_state *)win->userdata;
    const char *path = st ? st->path : "/";

    gui_draw_string(content_x + 10, yy + 4, "Location:", 0xAAAAAA, THEME_BG);
    gui_draw_string(content_x + 90, yy + 4, path, 0xFFFFFF, THEME_BG);

    yy += 20;

    /* Grid container */
    struct fm_ctx ctx;
    ctx.win = win;
    ctx.start_x = content_x + 10;
    ctx.start_y = yy;
    ctx.cur_x = ctx.start_x;
    ctx.cur_y = ctx.start_y;
    ctx.max_x = content_x + content_w - 10; /* Right edge bound */
    ctx.max_y = content_y + content_h;      /* Bottom edge bound */
    ctx.state = st;

    /* Open VFS */
    struct file *dir = vfs_open(path, O_RDONLY, 0);
    if (dir) {
      vfs_readdir(dir, &ctx, fm_render_callback);
      vfs_close(dir);
    } else {
      gui_draw_string(content_x + 20, yy + 20, "Failed to open directory",
                      0xFF0000, 0x1E1E2E);
    }
  }
  /* Paint */
  else if (win->title[0] == 'P' && win->title[1] == 'a') {
    /* Toolbar */
    gui_draw_rect(content_x, content_y, content_w, 32, 0x404040);
    gui_draw_string(content_x + 8, content_y + 8,
                    "Brush [O]  Line [/]  Color:", 0xFFFFFF, 0x404040);
    /* Color palette */
    gui_draw_rect(content_x + 200, content_y + 4, 20, 20, 0xFF0000);
    gui_draw_rect(content_x + 224, content_y + 4, 20, 20, 0x00FF00);
    gui_draw_rect(content_x + 248, content_y + 4, 20, 20, 0x0000FF);
    gui_draw_rect(content_x + 272, content_y + 4, 20, 20, 0x000000);
    /* Canvas */
    gui_draw_rect(content_x + 4, content_y + 36, content_w - 8, content_h - 44,
                  0xFFFFFF);
  }
  /* Browser */
  else if (win->title[0] == 'B' && win->title[1] == 'r' &&
           win->title[2] == 'o') {
    /* Toolbar Background */
    gui_draw_rect(content_x, content_y, content_w, 40, 0xDDDDDD);

    /* Address Bar */
    gui_draw_rect(content_x + 80, content_y + 8, content_w - 96, 24, 0xFFFFFF);
    gui_draw_rect_outline(content_x + 80, content_y + 8, content_w - 96, 24,
                          0xA0A0A0, 1);
    gui_draw_string(content_x + 88, content_y + 12, "http://vib-os.org",
                    0x333333, 0xFFFFFF);

    /* Navigation Buttons */
    gui_draw_string(content_x + 12, content_y + 12, "<", 0x555555, 0xDDDDDD);
    gui_draw_string(content_x + 35, content_y + 12, ">", 0x555555, 0xDDDDDD);
    gui_draw_string(content_x + 58, content_y + 12, "@", 0x555555,
                    0xDDDDDD); /* Refresh */

    /* Web Content Area */
    gui_draw_rect(content_x, content_y + 40, content_w, content_h - 40,
                  0xFFFFFF);

    /* Mock Page Content */
    gui_draw_string(content_x + 20, content_y + 60, "Welcome to VibBrowser",
                    0x000000, 0xFFFFFF);
    gui_draw_rect(content_x + 20, content_y + 78, 200, 2,
                  0x007AFF); /* Underline */

    gui_draw_string(content_x + 20, content_y + 90, "Status:", 0x555555,
                    0xFFFFFF);
    gui_draw_string(content_x + 80, content_y + 90, "Networking Enabled",
                    0x00AA00, 0xFFFFFF);

    gui_draw_string(content_x + 20, content_y + 110, "IP Addr:", 0x555555,
                    0xFFFFFF);
    gui_draw_string(content_x + 80, content_y + 110, "10.0.2.15 (DHCP)",
                    0x333333, 0xFFFFFF);

    /* Fake links */
    gui_draw_string(content_x + 20, content_y + 150, "- Latest News", 0x007AFF,
                    0xFFFFFF);
    gui_draw_string(content_x + 20, content_y + 170, "- Documentation",
                    0x007AFF, 0xFFFFFF);
    gui_draw_string(content_x + 20, content_y + 190, "- Source Code", 0x007AFF,
                    0xFFFFFF);
  }
  /* Image Viewer */
  else if (win->title[0] == 'I' && win->title[1] == 'm' &&
           win->title[2] == 'a') {
    draw_image_viewer(win, content_x, content_y, content_w, content_h);
  }
  /* Help */
  else if (win->title[0] == 'H' && win->title[1] == 'e') {
    int yy = content_y + 10;
    gui_draw_string(content_x + 10, yy, "Vib-OS Help", 0x89B4FA, THEME_BG);
    yy += 24;
    gui_draw_string(content_x + 10, yy, "Mouse:", 0xF9E2AF, THEME_BG);
    yy += 18;
    gui_draw_string(content_x + 20, yy, "- Click dock to launch apps", 0xCDD6F4,
                    THEME_BG);
    yy += 16;
    gui_draw_string(content_x + 20, yy, "- Drag titlebars to move", 0xCDD6F4,
                    THEME_BG);
    yy += 16;
    gui_draw_string(content_x + 20, yy, "- Click red button to close", 0xCDD6F4,
                    THEME_BG);
    yy += 24;
    gui_draw_string(content_x + 10, yy, "Terminal:", 0xF9E2AF, THEME_BG);
    yy += 18;
    gui_draw_string(content_x + 20, yy, "- Type 'help' for commands", 0xCDD6F4,
                    THEME_BG);
    yy += 16;
    gui_draw_string(content_x + 20, yy, "- Type 'neofetch' for info", 0xCDD6F4,
                    THEME_BG);
  }
  /* About window */
  else if (win->title[0] == 'A' && win->title[1] == 'b' &&
           win->title[2] == 'o') {
    int yy = content_y + 20;
    int center_x = content_x + content_w / 2;

    /* OS Logo - large @ symbol centered */
    gui_draw_string(center_x - 20, yy, "@ @", 0x89B4FA, THEME_BG);
    yy += 32;

    /* OS Name - large and centered */
    gui_draw_string(center_x - 40, yy, "Vib-OS", 0xFFFFFF, THEME_BG);
    yy += 24;

    /* Version */
    gui_draw_string(center_x - 68, yy, "Version 0.5.0", 0xA6ADC8, THEME_BG);
    yy += 28;

    /* System info box */
    gui_draw_rect(content_x + 20, yy, content_w - 40, 80, 0x252535);
    yy += 10;
    gui_draw_string(content_x + 30, yy, "Architecture:  ARM64", 0xCDD6F4,
                    0x252535);
    yy += 18;
    gui_draw_string(content_x + 30, yy, "Kernel:        Vib Kernel 0.5",
                    0xCDD6F4, 0x252535);
    yy += 18;
    gui_draw_string(content_x + 30, yy, "Memory:        252 MB", 0xCDD6F4,
                    0x252535);
    yy += 18;
    gui_draw_string(content_x + 30, yy, "Display:       1024 x 768", 0xCDD6F4,
                    0x252535);
    yy += 28;

    /* Copyright */
    gui_draw_string(content_x + 30, yy, "(c) 2026 Vib-OS Project", 0x6C7086,
                    THEME_BG);
  }
  /* Settings window */
  else if (win->title[0] == 'S' && win->title[1] == 'e' &&
           win->title[2] == 't') {
    int yy = content_y + 12;

    /* Header */
    gui_draw_string(content_x + 12, yy, "System Settings", 0xFFFFFF, THEME_BG);
    yy += 28;

    /* Display section */
    gui_draw_rect(content_x + 10, yy, content_w - 20, 60, 0x252535);
    gui_draw_string(content_x + 20, yy + 8, "Display", 0x89B4FA, 0x252535);
    gui_draw_string(content_x + 20, yy + 28, "Resolution: 1024 x 768", 0xCDD6F4,
                    0x252535);
    gui_draw_string(content_x + 20, yy + 44, "Color Depth: 32-bit", 0xCDD6F4,
                    0x252535);
    yy += 70;

    /* Sound section */
    gui_draw_rect(content_x + 10, yy, content_w - 20, 44, 0x252535);
    gui_draw_string(content_x + 20, yy + 8, "Sound", 0x89B4FA, 0x252535);
    gui_draw_string(content_x + 20, yy + 26, "Audio: Disabled", 0x6C7086,
                    0x252535);
    yy += 54;

    /* Network section */
    gui_draw_rect(content_x + 10, yy, content_w - 20, 44, 0x252535);
    gui_draw_string(content_x + 20, yy + 8, "Network", 0x89B4FA, 0x252535);
    gui_draw_string(content_x + 20, yy + 26, "Status: Not connected", 0x6C7086,
                    0x252535);
    yy += 54;

    /* About button */
    gui_draw_rect(content_x + 10, yy, 100, 28, 0x3B82F6);
    gui_draw_string(content_x + 24, yy + 6, "About...", 0xFFFFFF, 0x3B82F6);
  }
  /* Clock window */
  else if (win->title[0] == 'C' && win->title[1] == 'l' &&
           win->title[2] == 'o') {
    int center_x = content_x + content_w / 2;
    int center_y = content_y + content_h / 2;
    int radius = 60;

    /* Clock face - white circle */
    for (int dy = -radius; dy <= radius; dy++) {
      for (int dx = -radius; dx <= radius; dx++) {
        if (dx * dx + dy * dy <= radius * radius) {
          uint32_t color = (dx * dx + dy * dy <= (radius - 3) * (radius - 3))
                               ? 0xFFFFFF
                               : 0x3B82F6;
          draw_pixel(center_x + dx, center_y + dy, color);
        }
      }
    }

    /* Hour markers */
    for (int h = 0; h < 12; h++) {
      /* Simple markers at 12, 3, 6, 9 positions */
      int mx = center_x + (h == 3 ? 50 : (h == 9 ? -50 : 0));
      int my = center_y + (h == 0 ? -50 : (h == 6 ? 50 : 0));
      if (h == 0 || h == 3 || h == 6 || h == 9) {
        gui_draw_rect(mx - 3, my - 3, 6, 6, 0x3B82F6);
      }
    }

    /* Clock hands - hour (short) pointing to ~10:10 */
    for (int i = 0; i < 25; i++) {
      draw_pixel(center_x - i / 2, center_y - i * 3 / 4, 0x222222);
      draw_pixel(center_x - i / 2 + 1, center_y - i * 3 / 4, 0x222222);
    }
    /* Minute hand (long) */
    for (int i = 0; i < 40; i++) {
      draw_pixel(center_x + i / 3, center_y - i, 0x444444);
    }
    /* Center dot */
    for (int dy = -3; dy <= 3; dy++) {
      for (int dx = -3; dx <= 3; dx++) {
        if (dx * dx + dy * dy <= 9) {
          draw_pixel(center_x + dx, center_y + dy, 0xEF4444);
        }
      }
    }

    /* Time display below */
    gui_draw_string(center_x - 28, center_y + radius + 15, "12:10", 0xFFFFFF,
                    THEME_BG);
  }
  /* Game window */
  else if (win->title[0] == 'G' && win->title[1] == 'a' &&
           win->title[2] == 'm') {
    int yy = content_y + 15;

    /* Header */
    if (snake_game_over) {
      gui_draw_string(content_x + 12, yy, "GAME OVER! Press any key", 0xEF4444,
                      THEME_BG);
    } else {
      gui_draw_string(content_x + 12, yy, "Snake Game - WASD to move", 0x89B4FA,
                      THEME_BG);
    }
    yy += 28;

    /* Game area with border */
    int game_w = content_w - 24;
    int game_h = 180;
    int game_x = content_x + 12;
    int game_y = yy;
    gui_draw_rect_outline(game_x, game_y, game_w, game_h, 0x3B82F6, 2);
    gui_draw_rect(game_x + 2, game_y + 2, game_w - 4, game_h - 4, 0x101018);

    /* Calculate cell size */
    int cell_w = (game_w - 4) / SNAKE_GRID_W;
    int cell_h = (game_h - 4) / SNAKE_GRID_H;

    /* Draw food */
    int fx = game_x + 2 + snake_food_x * cell_w + 2;
    int fy = game_y + 2 + snake_food_y * cell_h + 2;
    gui_draw_rect(fx, fy, cell_w - 4, cell_h - 4, 0xEF4444);

    /* Draw snake */
    for (int i = 0; i < snake_len; i++) {
      int sx = game_x + 2 + snake_x[i] * cell_w + 1;
      int sy = game_y + 2 + snake_y[i] * cell_h + 1;
      uint32_t color = (i == 0) ? 0x22C55E : 0x16A34A; /* Head is brighter */
      if (i == snake_len - 1)
        color = 0x15803D; /* Tail is darker */
      gui_draw_rect(sx, sy, cell_w - 2, cell_h - 2, color);
    }

    /* Score display */
    char score_str[32];
    score_str[0] = 'S';
    score_str[1] = 'c';
    score_str[2] = 'o';
    score_str[3] = 'r';
    score_str[4] = 'e';
    score_str[5] = ':';
    score_str[6] = ' ';
    /* Convert score to string */
    int s = snake_score;
    int pos = 7;
    if (s == 0) {
      score_str[pos++] = '0';
    } else {
      int temp[10], ti = 0;
      while (s > 0) {
        temp[ti++] = s % 10;
        s /= 10;
      }
      while (ti > 0) {
        score_str[pos++] = '0' + temp[--ti];
      }
    }
    score_str[pos] = '\0';

    yy += game_h + 8;
    gui_draw_string(game_x + 8, yy - 16, score_str, 0xFFFFFF, 0x101018);

    /* Controls hint */
    gui_draw_string(content_x + 12, yy, "WASD or Arrow keys", 0x6C7086,
                    THEME_BG);
  }
  /* Terminal - use term_render from terminal.c for proper output display */
  else if (win->title[0] == 'T' && win->title[1] == 'e' &&
           win->title[2] == 'r') {
    /* Use window's own terminal if available, otherwise fall back to active */
    struct terminal *term = (struct terminal *)win->userdata;
    if (!term) {
      term = term_get_active();
    }
    if (term) {
      /* Update terminal's content area to match window position */
      term_set_content_pos(term, content_x, content_y);
      term_render(term);
    } else {
      /* Fallback if no terminal */
      gui_draw_string(content_x + 10, content_y + 10,
                      "Terminal not initialized", 0xFF0000, THEME_BG);
    }
  }
  /* Notepad - Modern Design with Full Toolbar */

  else if ((win->title[0] == 'N' && win->title[1] == 'o' &&
            win->title[2] == 't') ||
           (win->title[0] == 'R' && win->title[1] == 'e' &&
            win->title[2] == 'n')) {

    /* Modern dark toolbar */
    int toolbar_h = 36;
    gui_draw_rect(content_x, content_y, content_w, toolbar_h, 0x2D2D30);

    /* Toolbar buttons with modern styling */
    int btn_y = content_y + 6;
    int btn_h = 24;
    int btn_spacing = 4;
    int bx = content_x + 8;

    /* File operations group */
    /* New button */
    gui_draw_rect(bx, btn_y, 50, btn_h, 0x3E3E42);
    gui_draw_rect(bx, btn_y, 50, 1, 0x505054);
    gui_draw_string(bx + 13, btn_y + 5, "New", 0xCCCCCC, 0x3E3E42);
    bx += 50 + btn_spacing;

    /* Open button */
    gui_draw_rect(bx, btn_y, 50, btn_h, 0x3E3E42);
    gui_draw_rect(bx, btn_y, 50, 1, 0x505054);
    gui_draw_string(bx + 10, btn_y + 5, "Open", 0xCCCCCC, 0x3E3E42);
    bx += 50 + btn_spacing;

    /* Save button - highlighted */
    gui_draw_rect(bx, btn_y, 50, btn_h, 0x0E639C);
    gui_draw_rect(bx, btn_y, 50, 1, 0x1E7BB8);
    gui_draw_string(bx + 10, btn_y + 5, "Save", 0xFFFFFF, 0x0E639C);
    bx += 50 + btn_spacing;

    /* Separator */
    bx += 8;
    gui_draw_rect(bx, btn_y + 2, 1, btn_h - 4, 0x505054);
    bx += 12;

    /* Edit operations group */
    /* Cut button */
    gui_draw_rect(bx, btn_y, 42, btn_h, 0x3E3E42);
    gui_draw_rect(bx, btn_y, 42, 1, 0x505054);
    gui_draw_string(bx + 10, btn_y + 5, "Cut", 0xCCCCCC, 0x3E3E42);
    bx += 42 + btn_spacing;

    /* Copy button */
    gui_draw_rect(bx, btn_y, 50, btn_h, 0x3E3E42);
    gui_draw_rect(bx, btn_y, 50, 1, 0x505054);
    gui_draw_string(bx + 10, btn_y + 5, "Copy", 0xCCCCCC, 0x3E3E42);
    bx += 50 + btn_spacing;

    /* Paste button */
    gui_draw_rect(bx, btn_y, 55, btn_h, 0x3E3E42);
    gui_draw_rect(bx, btn_y, 55, 1, 0x505054);
    gui_draw_string(bx + 8, btn_y + 5, "Paste", 0xCCCCCC, 0x3E3E42);

    /* Text editing area with modern styling */
    int text_area_y = content_y + toolbar_h + 2;
    int status_h = 22;
    int text_area_h = content_h - toolbar_h - status_h - 4;

    /* Text area background with subtle border */
    gui_draw_rect(content_x + 4, text_area_y, content_w - 8, text_area_h,
                  0x1E1E1E);
    gui_draw_rect(content_x + 4, text_area_y, content_w - 8, 1, 0x3C3C3C);
    gui_draw_rect(content_x + 4, text_area_y, 1, text_area_h, 0x3C3C3C);

    /* Line number gutter */
    int gutter_w = 40;
    gui_draw_rect(content_x + 5, text_area_y + 1, gutter_w - 2, text_area_h - 2,
                  0x252526);

    /* Draw line numbers */
    int line_num = 1;
    int max_lines = (text_area_h - 8) / 16;
    for (int i = 0; i < max_lines && i < 20; i++) {
      char num_str[4] = {0};
      int n = line_num + i;
      if (n < 10) {
        num_str[0] = '0' + n;
      } else {
        num_str[0] = '0' + (n / 10);
        num_str[1] = '0' + (n % 10);
      }
      gui_draw_string(content_x + 20, text_area_y + 4 + i * 16, num_str,
                      0x858585, 0x252526);
    }

    /* Draw text with syntax-like highlighting */
    int tx = content_x + 8 + gutter_w;
    int ty = text_area_y + 4;
    int max_x = content_x + content_w - 12;
    int max_y = text_area_y + text_area_h - 8;

    char *target_text = (win->title[0] == 'N') ? notepad_text : rename_text;
    int target_cursor = (win->title[0] == 'N') ? notepad_cursor : rename_cursor;

    int char_count = 0;
    int line_count = 1;
    for (int i = 0; i < target_cursor && ty < max_y; i++) {
      char c = target_text[i];
      if (c == '\n') {
        tx = content_x + 8 + gutter_w;
        ty += 16;
        line_count++;
      } else {
        gui_draw_char(tx, ty, c, 0xD4D4D4, 0x1E1E1E);
        tx += 8;
        char_count++;
        if (tx >= max_x) {
          tx = content_x + 8 + gutter_w;
          ty += 16;
        }
      }
    }

    /* Cursor with blink effect */
    if (win->focused) {
      gui_draw_rect(tx, ty, 2, 14, 0x569CD6);
    }

    /* Status bar */
    int status_y = content_y + content_h - status_h;
    gui_draw_rect(content_x, status_y, content_w, status_h, 0x007ACC);

    /* Status text */
    char status_text[64] = "Ln ";
    int si = 3;
    if (line_count < 10) {
      status_text[si++] = '0' + line_count;
    } else {
      status_text[si++] = '0' + (line_count / 10);
      status_text[si++] = '0' + (line_count % 10);
    }
    status_text[si++] = ',';
    status_text[si++] = ' ';
    status_text[si++] = 'C';
    status_text[si++] = 'o';
    status_text[si++] = 'l';
    status_text[si++] = ' ';
    int col = char_count % 50 + 1;
    if (col < 10) {
      status_text[si++] = '0' + col;
    } else {
      status_text[si++] = '0' + (col / 10);
      status_text[si++] = '0' + (col % 10);
    }
    status_text[si] = '\0';
    gui_draw_string(content_x + 12, status_y + 4, status_text, 0xFFFFFF,
                    0x007ACC);

    /* File type indicator */
    gui_draw_string(content_x + content_w - 60, status_y + 4, "UTF-8", 0xFFFFFF,
                    0x007ACC);
  }
  /* Snake Game */
  else if (win->title[0] == 'S' && win->title[1] == 'n' &&
           win->title[2] == 'a') {
    /* Calculate cell size based on content area */
    int cell_w = (content_w - 20) / SNAKE_GRID_W;
    int cell_h = (content_h - 40) / SNAKE_GRID_H;
    if (cell_w > cell_h)
      cell_w = cell_h;
    else
      cell_h = cell_w;

    int grid_x = content_x + (content_w - cell_w * SNAKE_GRID_W) / 2;
    int grid_y = content_y + 30;

    /* Draw score */
    char score_str[32];
    int si = 0;
    score_str[si++] = 'S';
    score_str[si++] = 'c';
    score_str[si++] = 'o';
    score_str[si++] = 'r';
    score_str[si++] = 'e';
    score_str[si++] = ':';
    score_str[si++] = ' ';
    int s = snake_score;
    if (s == 0)
      score_str[si++] = '0';
    else {
      char tmp[8];
      int ti = 0;
      while (s > 0) {
        tmp[ti++] = '0' + (s % 10);
        s /= 10;
      }
      while (ti > 0)
        score_str[si++] = tmp[--ti];
    }
    score_str[si] = '\0';
    gui_draw_string(content_x + 10, content_y + 8, score_str, 0xF9E2AF,
                    THEME_BG);

    /* Draw grid background */
    gui_draw_rect(grid_x - 2, grid_y - 2, cell_w * SNAKE_GRID_W + 4,
                  cell_h * SNAKE_GRID_H + 4, 0x1E1E2E);

    /* Draw snake body */
    for (int i = 0; i < snake_len; i++) {
      int sx = grid_x + snake_x[i] * cell_w + 1;
      int sy = grid_y + snake_y[i] * cell_h + 1;
      uint32_t color = (i == 0) ? 0x94E2D5 : 0xA6E3A1; /* Head vs body */
      gui_draw_rect(sx, sy, cell_w - 2, cell_h - 2, color);
    }

    /* Draw food */
    int fx = grid_x + snake_food_x * cell_w + 1;
    int fy = grid_y + snake_food_y * cell_h + 1;
    gui_draw_rect(fx, fy, cell_w - 2, cell_h - 2, 0xF38BA8);

    /* Game over message */
    if (snake_game_over) {
      gui_draw_string(content_x + content_w / 2 - 40,
                      content_y + content_h - 30, "GAME OVER!", 0xF38BA8,
                      THEME_BG);
      gui_draw_string(content_x + content_w / 2 - 60,
                      content_y + content_h - 14, "Press R to restart",
                      0x6C7086, THEME_BG);
    } else {
      gui_draw_string(content_x + 10, content_y + content_h - 14,
                      "Arrow keys to move", 0x6C7086, THEME_BG);
    }
  }
  /* Clock */
  else if (win->title[0] == 'C' && win->title[1] == 'l' &&
           win->title[2] == 'o') {
    int cx = content_x + content_w / 2;
    int cy = content_y + content_h / 2;
    int r = (content_w < content_h ? content_w : content_h) / 2 - 16;

    /* Draw Clock Face */
    gui_draw_circle(cx, cy, r, 0xF0F0F0, true);  /* Face */
    gui_draw_circle(cx, cy, r, 0x808080, false); /* Outline */
    gui_draw_circle(cx, cy, 3, 0x000000, true);  /* Center dot */

    /* Hour markings */
    for (int i = 0; i < 12; i++) {
      int idx = i * 5;
      int x1 = cx + (r - 10) * clock_sin[idx] / 256;
      int y1 = cy + (r - 10) * clock_cos[idx] / 256;
      int x2 = cx + r * clock_sin[idx] / 256;
      int y2 = cy + r * clock_cos[idx] / 256;
      gui_draw_line(x1, y1, x2, y2, 0x303030);
    }

    /* Get time from PL031 RTC at 0x09010000 (QEMU virt) */
    /* This provides Unix timestamp (seconds since 1970) */
    volatile uint32_t *pl031_data = (volatile uint32_t *)0x09010000;
    uint64_t secs = *pl031_data;

    /* Apply timezone offset (e.g. -5 for EST) */
    /* Default to UTC for now, or maybe -5 for user */
    int tz_offset = -5;
    secs += tz_offset * 3600;

    int s = secs % 60;
    int m = (secs / 60) % 60;
    int h = (secs / 3600) % 12;
    if (h == 0)
      h = 12;

    /* Hour Hand */
    int h_idx = (h * 5 + m / 12) % 60;
    int hx = cx + (r * 60 / 100) * clock_sin[h_idx] / 256;
    int hy = cy + (r * 60 / 100) * clock_cos[h_idx] / 256;
    gui_draw_line(cx, cy, hx, hy, 0x202020); /* Simple line for now */
    /* Draw thicker by drawing parallel lines? Simple line is fine for low-res
     */

    /* Minute Hand */
    int mx = cx + (r * 85 / 100) * clock_sin[m] / 256;
    int my = cy + (r * 85 / 100) * clock_cos[m] / 256;
    gui_draw_line(cx, cy, mx, my, 0x404040);

    /* Second Hand */
    int sx = cx + (r * 90 / 100) * clock_sin[s] / 256;
    int sy = cy + (r * 90 / 100) * clock_cos[s] / 256;
    gui_draw_line(cx, cy, sx, sy, 0xD02020); /* Red */
  }

  /* Background Settings Window */
  else if (win->title[0] == 'B' && win->title[1] == 'a' &&
           win->title[2] == 'c') {
    /* Header */
    gui_draw_rect(content_x, content_y, content_w, 40, 0x27272A);
    gui_draw_string(content_x + 15, content_y + 12, "Choose Wallpaper",
                    0xFAFAFA, 0x27272A);

    /* Wallpaper grid - 5 columns, 2 rows */
    int grid_x = content_x + 15;
    int grid_y = content_y + 55;
    int thumb_w = 65;
    int thumb_h = 45;
    int gap = 10;

    for (int i = 0; i < NUM_WALLPAPERS; i++) {
      int col = i % 5;
      int row = i / 5;
      int tx = grid_x + col * (thumb_w + gap);
      int ty = grid_y + row * (thumb_h + gap + 20);

      /* Draw preview based on type */
      if (wallpapers[i].type == 1) {
        /* Use cached thumbnail */
        load_thumbnails(); /* Load once */
        media_image_t *thumb_img = &thumbnail_cache[i];

        if (thumb_img->pixels && thumb_img->width > 0) {
          /* Draw scaled image from cache */
          for (int py = 0; py < thumb_h; py++) {
            for (int px = 0; px < thumb_w; px++) {
              int src_x = (px * thumb_img->width) / thumb_w;
              int src_y = (py * thumb_img->height) / thumb_h;
              if (src_x < (int)thumb_img->width &&
                  src_y < (int)thumb_img->height) {
                uint32_t pixel =
                    thumb_img->pixels[src_y * thumb_img->width + src_x];
                draw_pixel(tx + px, ty + py, pixel);
              }
            }
          }
        } else {
          /* Fallback - gray with "?" */
          gui_draw_rect(tx, ty, thumb_w, thumb_h, 0x3A3A4A);
          gui_draw_char(tx + thumb_w / 2 - 4, ty + thumb_h / 2 - 8, '?',
                        0x888888, 0x3A3A4A);
        }
      } else {
        /* Gradient wallpaper - draw gradient preview */
        for (int py = 0; py < thumb_h; py++) {
          int progress = (py * 256) / thumb_h;
          uint8_t pr = wallpapers[i].tr +
                       ((wallpapers[i].br - wallpapers[i].tr) * progress) / 256;
          uint8_t pg = wallpapers[i].tg +
                       ((wallpapers[i].bg - wallpapers[i].tg) * progress) / 256;
          uint8_t pb = wallpapers[i].tb +
                       ((wallpapers[i].bb - wallpapers[i].tb) * progress) / 256;
          uint32_t pcolor = (pr << 16) | (pg << 8) | pb;
          gui_draw_rect(tx, ty + py, thumb_w, 1, pcolor);
        }
      }

      /* Border - highlight if selected */
      uint32_t border_color = (i == current_wallpaper) ? 0x6366F1 : 0x52525B;
      gui_draw_rect(tx - 2, ty - 2, thumb_w + 4, 2, border_color);
      gui_draw_rect(tx - 2, ty + thumb_h, thumb_w + 4, 2, border_color);
      gui_draw_rect(tx - 2, ty - 2, 2, thumb_h + 4, border_color);
      gui_draw_rect(tx + thumb_w, ty - 2, 2, thumb_h + 4, border_color);

      /* Label */
      gui_draw_string(tx, ty + thumb_h + 4, wallpapers[i].name, 0xA1A1AA,
                      THEME_BG);
    }

    /* Current selection text */
    gui_draw_string(content_x + 15, content_y + content_h - 30,
                    "Click to select wallpaper", 0x71717A, THEME_BG);
  }

  /* Call window's draw callback if set */
  if (win->on_draw) {
    win->on_draw(win);
  }

  /* Draw resize grip in bottom-right corner */
  {
    int gx = x + w - 14;
    int gy = y + h - 14;
    uint32_t grip_color = win->focused ? 0x888888 : 0x666666;
    /* Draw diagonal grip lines (macOS style) */
    for (int i = 0; i < 3; i++) {
      int offset = i * 4;
      /* Diagonal line from bottom-left to top-right */
      gui_draw_line(gx + offset, gy + 10, gx + 10, gy + offset, grip_color);
    }
  }
}

/* ===================================================================== */
/* Desktop with Menu Bar and Dock */
/* ===================================================================== */

/* Menu dropdown state */
static int menu_open = 0; /* 0=closed, 1=Apple menu open */

static void draw_menu_bar(void) {
  /* Glossy menu bar - gradient from dark to slightly lighter */
  for (int y = 0; y < MENU_BAR_HEIGHT; y++) {
    int brightness = 45 + (y * 10) / MENU_BAR_HEIGHT; /* 45 to 55 */
    uint32_t color = (brightness << 16) | (brightness << 8) | (brightness + 5);
    for (int x = 0; x < (int)primary_display.width; x++) {
      draw_pixel(x, y, color);
    }
  }
  /* Bottom highlight line */
  for (int x = 0; x < (int)primary_display.width; x++) {
    draw_pixel(x, MENU_BAR_HEIGHT - 1, 0x606060);
  }

  /* Apple logo (using @ as placeholder, bold white) */
  gui_draw_string(14, 6, "@", 0xFFFFFF, 0x2D2D35);

  /* Vib-OS name (bold) */
  gui_draw_string(36, 6, "Vib-OS", 0xFFFFFF, 0x303038);

  /* Clock on right - compute from PL031 RTC */
  {
    /* Read PL031 RTC at 0x09010000 */
    volatile uint32_t *pl031_data = (volatile uint32_t *)0x09010000;
    uint64_t secs = *pl031_data;

    /* Timezone offset */
    int tz_offset = -5;
    secs += tz_offset * 3600;

    /* Convert to HH:MM */
    int hrs = (secs / 3600) % 24;
    int mins = (secs / 60) % 60;

    char time_str[6];
    time_str[0] = '0' + (hrs / 10);
    time_str[1] = '0' + (hrs % 10);
    time_str[2] = ':';
    time_str[3] = '0' + (mins / 10);
    time_str[4] = '0' + (mins % 10);
    time_str[5] = '\0';

    gui_draw_string(primary_display.width - 52, 6, time_str, 0xFFFFFF,
                    0x3E3E55);
  }

  /* WiFi Icon (Static Connected) */
  {
    int wx = primary_display.width - 86;
    int wy = 12;
    /* Draw arcs using simple lines/pixels */
    /* Center dot */
    gui_draw_rect(wx, wy + 6, 2, 2, 0xFFFFFF);
    /* Middle arc */
    gui_draw_line(wx - 3, wy + 3, wx, wy, 0xFFFFFF);
    gui_draw_line(wx, wy, wx + 3, wy + 3, 0xFFFFFF);
    /* Top arc */
    gui_draw_line(wx - 6, wy, wx, wy - 3, 0xFFFFFF);
    gui_draw_line(wx, wy - 3, wx + 6, wy, 0xFFFFFF);
  }

  /* WiFi Icon (Static Connected) */
  {
    int wx = primary_display.width - 86;
    int wy = 12;
    /* Draw arcs using simple lines/pixels */
    /* Center dot */
    gui_draw_rect(wx, wy + 6, 2, 2, 0xFFFFFF);
    /* Middle arc */
    gui_draw_line(wx - 3, wy + 3, wx, wy, 0xFFFFFF);
    gui_draw_line(wx, wy, wx + 3, wy + 3, 0xFFFFFF);
    /* Top arc */
    gui_draw_line(wx - 6, wy, wx, wy - 3, 0xFFFFFF);
    gui_draw_line(wx, wy - 3, wx + 6, wy, 0xFFFFFF);
  }

  /* Draw dropdown if open */
  if (menu_open == 1) {
    int dropdown_x = 8;
    int dropdown_y = MENU_BAR_HEIGHT;
    int dropdown_w = 160;
    int dropdown_h = 80;

    /* Dropdown shadow */
    gui_draw_rect(dropdown_x + 3, dropdown_y + 3, dropdown_w, dropdown_h,
                  0x151520);

    /* Dropdown background */
    gui_draw_rect(dropdown_x, dropdown_y, dropdown_w, dropdown_h, 0x404050);
    gui_draw_rect_outline(dropdown_x, dropdown_y, dropdown_w, dropdown_h,
                          0x606070, 1);

    /* Menu items */
    gui_draw_string(dropdown_x + 12, dropdown_y + 10, "About Vib-OS", 0xFFFFFF,
                    0x404050);

    /* Separator line */
    for (int i = dropdown_x + 8; i < dropdown_x + dropdown_w - 8; i++) {
      draw_pixel(i, dropdown_y + 32, 0x555565);
    }

    gui_draw_string(dropdown_x + 12, dropdown_y + 40, "Settings...", 0xCCCCCC,
                    0x404050);
    gui_draw_string(dropdown_x + 12, dropdown_y + 58, "Restart", 0xCCCCCC,
                    0x404050);
  }
}

/* Dock icons */
#include "icons.h"

static const char *dock_labels[] = {"Term",  "Files", "Calc",  "Notes", "Set",
                                    "Clock", "DOOM",  "Snake", "Help",  "Web"};
#define NUM_DOCK_ICONS 10
#define DOCK_ICON_SIZE 44  /* Slightly smaller for more icons */
#define DOCK_ICON_MARGIN 4 /* Padding inside dock pill */
#define DOCK_PADDING 8     /* Space between icons */

/* Draw a 32x32 bitmap icon scaled to display size */
static void draw_icon(int x, int y, int size, const unsigned char *bitmap,
                      uint32_t fg, uint32_t bg) {
  for (int py = 0; py < 32; py++) {
    int draw_y = y + (py * size) / 32;
    for (int px = 0; px < 32; px++) {
      int draw_x = x + (px * size) / 32;
      uint32_t color = bitmap[py * 32 + px] ? fg : bg;
      /* Draw a small block for scaling */
      int next_x = x + ((px + 1) * size) / 32;
      int next_y = y + ((py + 1) * size) / 32;
      for (int dy = draw_y; dy < next_y; dy++) {
        for (int dx = draw_x; dx < next_x; dx++) {
          draw_pixel(dx, dy, color);
        }
      }
    }
  }
}

/* Draw rounded rectangle helper */
static void draw_rounded_rect(int x, int y, int w, int h, int r,
                              uint32_t color) {
  /* Main body */
  gui_draw_rect(x + r, y, w - 2 * r, h, color);
  gui_draw_rect(x, y + r, r, h - 2 * r, color);
  gui_draw_rect(x + w - r, y + r, r, h - 2 * r, color);

  /* Corners */
  for (int cy = -r; cy <= r; cy++) {
    for (int cx = -r; cx <= r; cx++) {
      if (cx * cx + cy * cy <= r * r) {
        draw_pixel(x + r + cx, y + r + cy, color);
        draw_pixel(x + w - r - 1 + cx, y + r + cy, color);
        draw_pixel(x + r + cx, y + h - r - 1 + cy, color);
        draw_pixel(x + w - r - 1 + cx, y + h - r - 1 + cy, color);
      }
    }
  }
}

/* Icon background colors for modern macOS Big Sur style */
static const uint32_t icon_colors[] = {
    0x1E1E1E, /* Terminal - dark gray/black */
    0x3B82F6, /* Files - blue */
    0xFF9500, /* Calculator - orange */
    0xFFCC00, /* Notepad - yellow */
    0x8E8E93, /* Settings - gray */
    0x000000, /* Clock - black */
    0xB91C1C, /* DOOM - dark red */
    0x34D399, /* Snake - teal green */
    0x3B82F6, /* Help - blue */
    0x0EA5E9, /* Browser - sky blue */
};

/* Draw a filled circle */
static void draw_filled_circle(int cx, int cy, int r, uint32_t color) {
  for (int y = -r; y <= r; y++) {
    for (int x = -r; x <= r; x++) {
      if (x * x + y * y <= r * r) {
        draw_pixel(cx + x, cy + y, color);
      }
    }
  }
}

/* Draw Terminal icon - simple bold >_ */
static void draw_icon_terminal(int x, int y, int size) {
  int m = size / 5;
  int cx = x + size / 3;
  int cy = y + size / 2;
  /* Bold > shape */
  for (int t = -2; t <= 2; t++) {
    gui_draw_line(cx - m / 2, cy - m + t, cx + m / 2, cy + t, 0x00FF00);
    gui_draw_line(cx + m / 2, cy + t, cx - m / 2, cy + m + t, 0x00FF00);
  }
  /* Solid underscore */
  gui_draw_rect(x + size / 2 + 2, cy + m / 2, size / 4, 4, 0xFFFFFF);
}

/* Draw Files icon - simple bold folder */
static void draw_icon_files(int x, int y, int size) {
  int m = size / 6;
  /* Main folder body */
  gui_draw_rect(x + m, y + m * 2, size - m * 2, size - m * 3, 0xFFFFFF);
  /* Tab on top */
  gui_draw_rect(x + m, y + m, size / 3, m + 2, 0xFFFFFF);
}

/* Draw Calculator icon - simple = symbol */
static void draw_icon_calc(int x, int y, int size) {
  int m = size / 5;
  /* Simple = symbol - two horizontal bars */
  gui_draw_rect(x + m, y + size / 2 - m / 2 - 3, size - m * 2, 4, 0xFFFFFF);
  gui_draw_rect(x + m, y + size / 2 + m / 2, size - m * 2, 4, 0xFFFFFF);
}

/* Draw Notes icon - simple paper */
static void draw_icon_notes(int x, int y, int size) {
  int m = size / 6;
  /* Paper rectangle */
  gui_draw_rect(x + m, y + m / 2, size - m * 2, size - m, 0xFFFFFF);
  /* 3 simple lines */
  gui_draw_rect(x + m * 2, y + m * 2, size - m * 4, 2, 0x888800);
  gui_draw_rect(x + m * 2, y + m * 3, size - m * 4, 2, 0x888800);
  gui_draw_rect(x + m * 2, y + m * 4, size - m * 4, 2, 0x888800);
}

/* Draw Settings icon - simple gear */
static void draw_icon_settings(int x, int y, int size) {
  int cx = x + size / 2;
  int cy = y + size / 2;
  int r = size / 4;
  /* Center circle */
  draw_filled_circle(cx, cy, r, 0xFFFFFF);
  /* 4 rectangles as gear teeth */
  gui_draw_rect(cx - 2, cy - r - 4, 5, 5, 0xFFFFFF);
  gui_draw_rect(cx + r, cy - 2, 5, 5, 0xFFFFFF);
  gui_draw_rect(cx - 2, cy + r, 5, 5, 0xFFFFFF);
  gui_draw_rect(cx - r - 4, cy - 2, 5, 5, 0xFFFFFF);
}

/* Draw Clock icon - simple circle + hands */
static void draw_icon_clock(int x, int y, int size) {
  int cx = x + size / 2;
  int cy = y + size / 2;
  int r = size / 3;
  /* White face */
  draw_filled_circle(cx, cy, r, 0xFFFFFF);
  /* Simple hour hand */
  gui_draw_rect(cx - 1, cy - r + 4, 3, r - 4, 0x000000);
  /* Simple minute hand */
  gui_draw_rect(cx, cy - 2, r - 2, 3, 0x555555);
  /* Center */
  draw_filled_circle(cx, cy, 3, 0xFF0000);
}

/* Draw DOOM icon - simple skull/face */
static void draw_icon_doom(int x, int y, int size) {
  int cx = x + size / 2;
  int cy = y + size / 2;
  int r = size / 3;
  /* Face */
  draw_filled_circle(cx, cy, r, 0xFFFFFF);
  /* Two eyes */
  draw_filled_circle(cx - r / 3, cy - r / 4, 4, 0xFF0000);
  draw_filled_circle(cx + r / 3, cy - r / 4, 4, 0xFF0000);
  /* Mouth */
  gui_draw_rect(cx - r / 2, cy + r / 4, r, 4, 0x000000);
}

/* Draw Snake icon - simple S shape */
static void draw_icon_snake(int x, int y, int size) {
  int r = size / 8;
  /* Body - 3 circles */
  draw_filled_circle(x + size / 4, y + size / 2, r, 0xFFFFFF);
  draw_filled_circle(x + size / 2, y + size / 2 - r, r, 0xFFFFFF);
  draw_filled_circle(x + size * 3 / 4, y + size / 2, r + 2, 0x00FF00);
  /* Eye */
  draw_filled_circle(x + size * 3 / 4 + 2, y + size / 2 - 2, 2, 0x000000);
}

/* Draw Help icon - simple ? */
static void draw_icon_help(int x, int y, int size) {
  int cx = x + size / 2;
  int cy = y + size / 2;
  int r = size / 3;
  /* White circle */
  draw_filled_circle(cx, cy, r, 0xFFFFFF);
  /* Blue ? - just stem and dot */
  gui_draw_rect(cx - 2, cy - r / 2, 5, r / 2 + 2, 0x007AFF);
  draw_filled_circle(cx, cy + r / 3, 3, 0x007AFF);
}

/* Draw Web icon - simple globe */
static void draw_icon_web(int x, int y, int size) {
  int cx = x + size / 2;
  int cy = y + size / 2;
  int r = size / 3;
  /* White circle */
  draw_filled_circle(cx, cy, r, 0xFFFFFF);
  /* Horizontal line */
  gui_draw_rect(cx - r + 2, cy - 1, r * 2 - 4, 3, 0x3399FF);
  /* Vertical line */
  gui_draw_rect(cx - 1, cy - r + 2, 3, r * 2 - 4, 0x3399FF);
}

/* Draw dock with hover animations - using vector icons */
static void draw_dock(void) {
  int mouse_active = (mouse_y >= primary_display.height - DOCK_HEIGHT - 40);

  /* 1. Calculate target sizes for all icons based on magnification */
  int icon_sizes[NUM_DOCK_ICONS];
  static int smooth_sizes[NUM_DOCK_ICONS] = {0};

  /* Initial base positions for hit testing (fixed grid for stability) */
  int base_dock_w =
      NUM_DOCK_ICONS * (DOCK_ICON_SIZE + DOCK_PADDING) - DOCK_PADDING + 32;
  int base_dock_x = (primary_display.width - base_dock_w) / 2;
  int base_y = primary_display.height - DOCK_HEIGHT + 6;

  int max_magnify = 42;    /* Max magnify */
  int magnify_range = 140; /* Wider range for wave */
  int hovered_idx = -1;

  for (int i = 0; i < NUM_DOCK_ICONS; i++) {
    int target = DOCK_ICON_SIZE;
    /* Use fixed base positions for hit test stability so icons don't run away
     */
    int base_center_x = base_dock_x + 16 + i * (DOCK_ICON_SIZE + DOCK_PADDING) +
                        DOCK_ICON_SIZE / 2;

    if (mouse_active) {
      int dist = mouse_x - base_center_x;
      if (dist < 0)
        dist = -dist;

      if (dist < magnify_range) {
        /* Sine wave magnification: scale = (1 - dist/range)^2 */
        int scale = (magnify_range - dist) * 256 / magnify_range;
        scale = scale * scale / 256; /* Quadratic ease */
        target += max_magnify * scale / 256;

        if (dist < DOCK_ICON_SIZE / 2 + 5)
          hovered_idx = i;
      }
    }

    /* Smooth interpolation */
    if (smooth_sizes[i] == 0)
      smooth_sizes[i] = DOCK_ICON_SIZE;
    int diff = target - smooth_sizes[i];
    if (diff > 0)
      smooth_sizes[i] += (diff > 8) ? 8 : diff;
    else if (diff < 0)
      smooth_sizes[i] += (diff < -8) ? -8 : diff;

    icon_sizes[i] = smooth_sizes[i];
  }

  /* 2. Calculate dynamic total width */
  int total_content_w = 0;
  for (int i = 0; i < NUM_DOCK_ICONS; i++) {
    total_content_w += icon_sizes[i];
    if (i < NUM_DOCK_ICONS - 1)
      total_content_w += DOCK_PADDING;
  }
  int dock_content_w = total_content_w; /* Used by old code too */
  int dock_w = total_content_w + 32;    /* Padding */
  int dock_h = DOCK_HEIGHT - 12;
  int dock_x = (primary_display.width - dock_w) / 2;
  int dock_y = base_y;

  /* 3. Draw Background behind everything */
  draw_rounded_rect(dock_x - 1, dock_y - 1, dock_w + 2, dock_h + 2, 16,
                    0x2A2A3A);
  draw_rounded_rect(dock_x, dock_y, dock_w, dock_h, 15, 0x1E1E28);
  /* Highlights */
  for (int i = dock_x + 14; i < dock_x + dock_w - 14; i++) {
    draw_pixel(i, dock_y + 1, 0x3A3A4A);
    draw_pixel(i, dock_y + dock_h - 1, 0x14141A);
  }

  /* 4. Determine Draw Order (Small -> Large) so large icons draw ON TOP of
   * neighbors */
  int draw_order[NUM_DOCK_ICONS];
  for (int i = 0; i < NUM_DOCK_ICONS; i++)
    draw_order[i] = i;

  /* Bubble sort by size (stable) */
  for (int i = 0; i < NUM_DOCK_ICONS - 1; i++) {
    for (int j = 0; j < NUM_DOCK_ICONS - i - 1; j++) {
      if (icon_sizes[draw_order[j]] > icon_sizes[draw_order[j + 1]]) {
        int temp = draw_order[j];
        draw_order[j] = draw_order[j + 1];
        draw_order[j + 1] = temp;
      }
    }
  }

  /* 5. Draw Icons */
  int center_y = dock_y + dock_h / 2;
  int curr_x = dock_x + 16;

  /* Calculate render centers first - strictly left-to-right based on dynamic
   * width */
  int render_centers[NUM_DOCK_ICONS];
  for (int i = 0; i < NUM_DOCK_ICONS; i++) {
    render_centers[i] = curr_x + icon_sizes[i] / 2;
    curr_x += icon_sizes[i] + DOCK_PADDING;
  }

  for (int k = 0; k < NUM_DOCK_ICONS; k++) {
    int i = draw_order[k]; /* Draw in sorted order */
    int size = icon_sizes[i];
    int cx = render_centers[i];
    int cy = center_y - (size - DOCK_ICON_SIZE) / 2; /* Move up as it grows */

    int draw_x = cx - size / 2;
    int draw_y = cy - size / 2;

    int icon_r = size / 5;
    uint32_t bg_color = icon_colors[i];

    /* Icon Background */
    gui_draw_rect(draw_x + icon_r, draw_y, size - 2 * icon_r, size, bg_color);
    gui_draw_rect(draw_x, draw_y + icon_r, size, size - 2 * icon_r, bg_color);
    /* Corners */
    for (int dy = -icon_r; dy <= icon_r; dy++) {
      for (int dx = -icon_r; dx <= icon_r; dx++) {
        if (dx * dx + dy * dy <= icon_r * icon_r) {
          draw_pixel(draw_x + icon_r + dx, draw_y + icon_r + dy, bg_color);
          draw_pixel(draw_x + size - icon_r - 1 + dx, draw_y + icon_r + dy,
                     bg_color);
          draw_pixel(draw_x + icon_r + dx, draw_y + size - icon_r - 1 + dy,
                     bg_color);
          draw_pixel(draw_x + size - icon_r - 1 + dx,
                     draw_y + size - icon_r - 1 + dy, bg_color);
        }
      }
    }

    /* Top Highlight */
    for (int x = draw_x + icon_r; x < draw_x + size - icon_r; x++) {
      draw_pixel(x, draw_y + 2, bg_color + 0x202020);
      draw_pixel(x, draw_y + 3, bg_color + 0x202020);
    }

    /* Bitmap Icon */
    if (i < 10) {
      const uint32_t *icon_data = dock_icons[i];
      int bmp_size = size * 3 / 4;
      int offset = (size - bmp_size) / 2;
      for (int dy = 0; dy < bmp_size; dy++) {
        for (int dx = 0; dx < bmp_size; dx++) {
          int sx = dx * DOCK_ICON_BITMAP_SIZE / bmp_size;
          int sy = dy * DOCK_ICON_BITMAP_SIZE / bmp_size;
          if (sx >= DOCK_ICON_BITMAP_SIZE)
            sx = DOCK_ICON_BITMAP_SIZE - 1;
          if (sy >= DOCK_ICON_BITMAP_SIZE)
            sy = DOCK_ICON_BITMAP_SIZE - 1;

          uint32_t px = icon_data[sy * DOCK_ICON_BITMAP_SIZE + sx];
          if ((px >> 24) > 128) {
            draw_pixel(draw_x + offset + dx, draw_y + offset + dy,
                       px & 0xFFFFFF);
          }
        }
      }
    }
  }

  /* Draw label for hovered item on top */
  if (hovered_idx >= 0) {
    const char *label = dock_labels[hovered_idx];
    int idx_x = render_centers[hovered_idx];

    int label_len = 0;
    while (label[label_len])
      label_len++;
    int label_w = label_len * 8 + 16;
    int label_h = 24;
    int label_x = idx_x - label_w / 2;
    int label_y = base_y - 45; /* Fixed height above dock */

    draw_rounded_rect(label_x, label_y, label_w, label_h, 6, 0x303040);
    gui_draw_rect_outline(label_x, label_y, label_w, label_h, 0x505060, 1);
    gui_draw_string(label_x + 8, label_y + 4, label, 0xFFFFFF, 0x303040);

    /* Triangle */
    int tri_x = label_x + label_w / 2;
    int tri_y = label_y + label_h;
    for (int i = 0; i < 4; i++) {
      for (int j = -i; j <= i; j++) {
        draw_pixel(tri_x + j, tri_y + i, 0x303040);
      }
    }
  }
}

/* Cached wallpaper for performance - gradient is expensive to recalculate */
static uint32_t *cached_wallpaper = NULL;
static int wallpaper_cached = 0;
static int wallpaper_cached_idx = -1; /* Which wallpaper is cached */

/* Draw wallpaper - supports both gradients and JPEG images */
static void draw_wallpaper(void) {
  int start_y = MENU_BAR_HEIGHT;
  /* Extend wallpaper all the way to bottom of screen (dock drawn on top) */
  int end_y = primary_display.height;
  int height = end_y - start_y;
  int width = primary_display.width;

  /* Check if we need to reload (wallpaper changed) */
  if (wallpaper_cached_idx != current_wallpaper) {
    wallpaper_cached = 0;
    wallpaper_cached_idx = current_wallpaper;
    /* Load image if needed */
    wallpaper_ensure_loaded();
  }

  /* Check if this is an image wallpaper */
  if (wallpapers[current_wallpaper].type == 1 && wallpaper_image.pixels) {
    /* Draw scaled JPEG image - simple nearest neighbor for reliability */
    uint32_t img_w = wallpaper_image.width;
    uint32_t img_h = wallpaper_image.height;
    uint32_t *pixels = wallpaper_image.pixels;

    /* Calculate scale factors (fixed point 16.16) */
    uint32_t scale_x = (img_w << 16) / width;
    uint32_t scale_y = (img_h << 16) / height;

    for (int y = start_y; y < end_y; y++) {
      uint32_t *line =
          primary_display.backbuffer + y * (primary_display.pitch / 4);
      uint32_t src_y = ((y - start_y) * scale_y) >> 16;
      if (src_y >= img_h)
        src_y = img_h - 1;
      uint32_t *src_row = pixels + src_y * img_w;

      for (int x = 0; x < width; x++) {
        uint32_t src_x = (x * scale_x) >> 16;
        if (src_x >= img_w)
          src_x = img_w - 1;
        line[x] = src_row[src_x];
      }
    }
    return;
  }

  /* Gradient wallpaper - use wallpaper_get_pixel */
  for (int y = start_y; y < end_y; y++) {
    uint32_t *line =
        primary_display.backbuffer + y * (primary_display.pitch / 4);
    uint32_t color = wallpaper_get_pixel(0, y - start_y, height);

    for (int x = 0; x < width; x++) {
      line[x] = color;
    }
  }
}

static void draw_desktop(void) {
  /* Draw beautiful gradient wallpaper */
  draw_wallpaper();

  /* Draw desktop icons */
  desktop_draw_icons();

  /* Draw menu bar at top (glass effect) */
  draw_menu_bar();

  /* Draw dock at bottom */
  draw_dock();
}

/* ===================================================================== */
/* Compositor - Draw everything with dirty region optimization */
/* ===================================================================== */

/* Dirty region tracking for compositor */
#define MAX_DIRTY_REGIONS 32
typedef struct {
  int x, y, w, h;
  int valid;
} compositor_dirty_rect_t;

static compositor_dirty_rect_t g_dirty_regions[MAX_DIRTY_REGIONS];
static int g_dirty_count = 0;
static int g_full_redraw = 1; /* Start with full redraw */
static int g_frame_count = 0;

/* Mark a region as needing update */
void compositor_mark_dirty(int x, int y, int w, int h) {
  if (g_dirty_count < MAX_DIRTY_REGIONS) {
    g_dirty_regions[g_dirty_count].x = x;
    g_dirty_regions[g_dirty_count].y = y;
    g_dirty_regions[g_dirty_count].w = w;
    g_dirty_regions[g_dirty_count].h = h;
    g_dirty_regions[g_dirty_count].valid = 1;
    g_dirty_count++;
  } else {
    g_full_redraw = 1;
  }
}

void compositor_mark_full_redraw(void) {
  g_full_redraw = 1;
  g_dirty_count = 0;
}

/* Optimized memcpy for scanlines */
static inline void fast_memcpy_line(uint32_t *dst, uint32_t *src, int width) {
  /* Use 64-bit copies for better performance */
  uint64_t *d64 = (uint64_t *)dst;
  uint64_t *s64 = (uint64_t *)src;
  int count = width / 2;

  for (int i = 0; i < count; i++) {
    d64[i] = s64[i];
  }

  /* Handle odd pixel */
  if (width & 1) {
    dst[width - 1] = src[width - 1];
  }
}

/* Copy a specific region from backbuffer to framebuffer */
static void blit_region(int x, int y, int w, int h) {
  if (!primary_display.backbuffer || !primary_display.framebuffer)
    return;

  /* Clip to screen bounds */
  if (x < 0) {
    w += x;
    x = 0;
  }
  if (y < 0) {
    h += y;
    y = 0;
  }
  if (x + w > (int)primary_display.width)
    w = primary_display.width - x;
  if (y + h > (int)primary_display.height)
    h = primary_display.height - y;
  if (w <= 0 || h <= 0)
    return;

  int pitch_pixels = primary_display.pitch / 4;

  for (int row = y; row < y + h; row++) {
    uint32_t *src = primary_display.backbuffer + row * pitch_pixels + x;
    uint32_t *dst = primary_display.framebuffer + row * pitch_pixels + x;
    fast_memcpy_line(dst, src, w);
  }
}

/* Forward declaration for cursor */
void gui_draw_cursor(void);

void gui_compose(void) {
  g_frame_count++;

  /* Draw desktop and taskbar */
  draw_desktop();

  /* Update Snake game state (throttled) */
  static int snake_tick = 0;
  if (++snake_tick >= 10) { /* Update every 10 frames */
    snake_tick = 0;
    snake_move();
  }

  /* Draw windows from bottom to top (reverse order) */
  struct window *draw_order[MAX_WINDOWS];
  int count = 0;
  for (struct window *win = window_stack; win && count < MAX_WINDOWS;
       win = win->next) {
    draw_order[count++] = win;
  }

  /* Draw in reverse (bottom to top) */
  for (int i = count - 1; i >= 0; i--) {
    draw_window(draw_order[i]);
  }

  /* Draw cursor to backbuffer BEFORE blit */
  gui_draw_cursor();

  /* Smart frame buffer update */
  if (primary_display.backbuffer && primary_display.framebuffer) {
    if (g_full_redraw || g_dirty_count == 0) {
      /* Full frame update - use ultra-fast unrolled copy */
      uint64_t *src = (uint64_t *)primary_display.backbuffer;
      uint64_t *dst = (uint64_t *)primary_display.framebuffer;
      size_t count64 = (primary_display.pitch * primary_display.height) / 8;
      size_t i = 0;

      /* Unrolled copy - 8 qwords (64 bytes / 16 pixels) per iteration */
      size_t fast_count = count64 & ~7UL;
      for (; i < fast_count; i += 8) {
        dst[i] = src[i];
        dst[i + 1] = src[i + 1];
        dst[i + 2] = src[i + 2];
        dst[i + 3] = src[i + 3];
        dst[i + 4] = src[i + 4];
        dst[i + 5] = src[i + 5];
        dst[i + 6] = src[i + 6];
        dst[i + 7] = src[i + 7];
      }
      for (; i < count64; i++) {
        dst[i] = src[i];
      }

      g_full_redraw = 0;
    } else {
      /* Partial update - only copy dirty regions */
      for (int d = 0; d < g_dirty_count; d++) {
        if (g_dirty_regions[d].valid) {
          blit_region(g_dirty_regions[d].x, g_dirty_regions[d].y,
                      g_dirty_regions[d].w, g_dirty_regions[d].h);
        }
      }
    }

    /* Memory barrier */
#ifdef ARCH_ARM64
    asm volatile("dsb sy" ::: "memory");
#elif defined(ARCH_X86_64) || defined(ARCH_X86)
    asm volatile("mfence" ::: "memory");
#endif
  }

  /* Clear dirty regions for next frame */
  g_dirty_count = 0;

  /* Force full redraw periodically to catch any missed updates */
  if ((g_frame_count & 0x3F) == 0) { /* Every 64 frames */
    g_full_redraw = 1;
  }
}

/* ===================================================================== */
/* Mouse Cursor (Mac-style arrow - drawn to backbuffer, no flicker) */
/* ===================================================================== */

#define CURSOR_WIDTH 12
#define CURSOR_HEIGHT 19

/* Classic Mac arrow: 1=black, 2=white, 0=transparent */
static const uint8_t cursor_data[CURSOR_HEIGHT][CURSOR_WIDTH] = {
    {1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0}, {1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
    {1, 2, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0}, {1, 2, 2, 1, 0, 0, 0, 0, 0, 0, 0, 0},
    {1, 2, 2, 2, 1, 0, 0, 0, 0, 0, 0, 0}, {1, 2, 2, 2, 2, 1, 0, 0, 0, 0, 0, 0},
    {1, 2, 2, 2, 2, 2, 1, 0, 0, 0, 0, 0}, {1, 2, 2, 2, 2, 2, 2, 1, 0, 0, 0, 0},
    {1, 2, 2, 2, 2, 2, 2, 2, 1, 0, 0, 0}, {1, 2, 2, 2, 2, 2, 2, 2, 2, 1, 0, 0},
    {1, 2, 2, 2, 2, 2, 2, 2, 2, 2, 1, 0}, {1, 2, 2, 2, 2, 2, 2, 1, 1, 1, 1, 1},
    {1, 2, 2, 2, 1, 2, 2, 1, 0, 0, 0, 0}, {1, 2, 2, 1, 0, 1, 2, 2, 1, 0, 0, 0},
    {1, 2, 1, 0, 0, 1, 2, 2, 1, 0, 0, 0}, {1, 1, 0, 0, 0, 0, 1, 2, 2, 1, 0, 0},
    {1, 0, 0, 0, 0, 0, 1, 2, 2, 1, 0, 0}, {0, 0, 0, 0, 0, 0, 0, 1, 2, 1, 0, 0},
    {0, 0, 0, 0, 0, 0, 0, 1, 1, 0, 0, 0},
};

/* Draw cursor directly to backbuffer - no save/restore needed since we redraw
 * every frame */
void gui_draw_cursor(void) {
  extern void mouse_get_position(int *x, int *y);
  int cx, cy;
  mouse_get_position(&cx, &cy);

  /* Update global mouse position for event handling */
  mouse_x = cx;
  mouse_y = cy;

  /* Draw cursor to backbuffer (not framebuffer!) */
  uint32_t *target = primary_display.backbuffer;
  if (!target)
    return;

  int pitch = primary_display.pitch / 4;

  for (int row = 0; row < CURSOR_HEIGHT; row++) {
    for (int col = 0; col < CURSOR_WIDTH; col++) {
      uint8_t pixel = cursor_data[row][col];
      if (pixel == 0)
        continue; /* Transparent */

      int px = cx + col;
      int py = cy + row;
      if (px >= 0 && px < (int)primary_display.width && py >= 0 &&
          py < (int)primary_display.height) {
        uint32_t color = (pixel == 1) ? 0x00000000 : 0x00FFFFFF;
        target[py * pitch + px] = color;
      }
    }
  }
}

void gui_move_mouse(int dx, int dy) {
  mouse_x += dx;
  mouse_y += dy;

  if (mouse_x < 0)
    mouse_x = 0;
  if (mouse_y < 0)
    mouse_y = 0;
  if (mouse_x >= (int)primary_display.width)
    mouse_x = primary_display.width - 1;
  if (mouse_y >= (int)primary_display.height)
    mouse_y = primary_display.height - 1;
}

void gui_set_mouse_buttons(int buttons) { mouse_buttons = buttons; }

void gui_handle_key_event(int key) {
  /* Check if desktop is doing inline rename - takes priority */
  extern int desktop_is_renaming(void);
  extern int desktop_handle_key(int key);
  if (desktop_is_renaming()) {
    if (desktop_handle_key(key))
      return; /* Desktop consumed the key */
  }

  /* Route key to focused window */
  if (focused_window && focused_window->visible) {
    /* Check if it's a Terminal window */
    if (focused_window->title[0] == 'T' && focused_window->title[1] == 'e' &&
        focused_window->title[2] == 'r') {
      /* Use file-scope terminal declarations */
      struct terminal *term = term_get_active();
      printk("KEY: term_get_active=%p, key=%d\n", term, key);
      if (term) {
        term_handle_key(term, key);
      }
    }
    /* Check if it's a Notepad window */
    else if (focused_window->title[0] == 'N' &&
             focused_window->title[1] == 'o' &&
             focused_window->title[2] == 't') {
      notepad_key(key);
    }
    /* Check if it's a Rename window */
    else if (focused_window->title[0] == 'R' &&
             focused_window->title[1] == 'e' &&
             focused_window->title[2] == 'n') {
      rename_key(key);
    }
    /* Check if it's a Game window */
    else if (focused_window->title[0] == 'S' &&
             focused_window->title[1] == 'n' &&
             focused_window->title[2] == 'a') {
      snake_key(key);
    }
    /* Check if it's an Image Viewer window */
    else if (focused_window->title[0] == 'I' &&
             focused_window->title[1] == 'm' &&
             focused_window->title[2] == 'a') {
      /* ESC key (27) - exit fullscreen */
      if (key == 27 && g_imgview.fullscreen) {
        g_imgview.fullscreen = 0;
        g_imgview.zoom_pct = 0;
        g_imgview.offset_x = 0;
        g_imgview.offset_y = 0;
      }
      /* F key - toggle fullscreen */
      else if (key == 'f' || key == 'F') {
        g_imgview.fullscreen = !g_imgview.fullscreen;
        g_imgview.zoom_pct = 0;
        g_imgview.offset_x = 0;
        g_imgview.offset_y = 0;
      }
      /* R key - rotate right */
      else if (key == 'r' || key == 'R') {
        g_imgview.rotation = (g_imgview.rotation + 90) % 360;
      }
      /* L key - rotate left */
      else if (key == 'l' || key == 'L') {
        g_imgview.rotation = (g_imgview.rotation + 270) % 360;
      }
    }
    /* Call window's key handler if set */
    if (focused_window->on_key) {
      focused_window->on_key(focused_window, key);
    }
  }
}

/* ===================================================================== */
/* Event Handling with Window Dragging and Resizing */
/* ===================================================================== */

/* Dragging state */
static struct window *dragging_window = 0;
static int drag_offset_x = 0, drag_offset_y = 0;
static int prev_buttons = 0;

/* Resizing state */
static struct window *resizing_window = 0;
#define RESIZE_NONE 0
#define RESIZE_RIGHT 1
#define RESIZE_BOTTOM 2
#define RESIZE_BOTTOM_RIGHT 3
#define RESIZE_LEFT 4
#define RESIZE_TOP 5
#define RESIZE_TOP_LEFT 6
#define RESIZE_TOP_RIGHT 7
#define RESIZE_BOTTOM_LEFT 8
static int resize_edge = RESIZE_NONE;
static int resize_start_x = 0, resize_start_y = 0;
static int resize_start_w = 0, resize_start_h = 0;
static int resize_start_win_x = 0, resize_start_win_y = 0;

#define RESIZE_BORDER                                                          \
  12 /* Pixel width of resize grab area - larger for easier grabbing */
#define MIN_WINDOW_WIDTH 150
#define MIN_WINDOW_HEIGHT 100

void gui_handle_mouse_event(int x, int y, int buttons) {
  int prev_x = mouse_x;
  int prev_y = mouse_y;
  mouse_x = x;
  mouse_y = y;

  int left_click = (buttons & 1) && !(prev_buttons & 1); /* Just pressed */
  int left_held = (buttons & 1);
  int left_release = !(buttons & 1) && (prev_buttons & 1);
  int right_click = (buttons & 2) && !(prev_buttons & 2); /* Right button */

  /* Handle context menu hover - ALWAYS call when menu visible */
  int menu_vis = desktop_is_context_menu_visible();
  if (menu_vis) {
    printk(KERN_INFO "MOUSE: Menu visible, calling hover at %d,%d\n", x, y);
    desktop_context_menu_hover(x, y);
    /* Force compositor to update */
    extern void compositor_mark_full_redraw(void);
    compositor_mark_full_redraw();
  }

  /* Track for double-click detection */
  static int last_click_x = 0, last_click_y = 0;
  static uint64_t last_click_time = 0;
  static int click_count = 0;

  /* Handle window dragging */
  if (dragging_window && left_held) {
    /* Move window with mouse */
    dragging_window->x = x - drag_offset_x;
    dragging_window->y = y - drag_offset_y;

    /* Clamp to screen */
    if (dragging_window->y < MENU_BAR_HEIGHT)
      dragging_window->y = MENU_BAR_HEIGHT;
    if (dragging_window->y >
        (int)primary_display.height - DOCK_HEIGHT - TITLEBAR_HEIGHT)
      dragging_window->y =
          primary_display.height - DOCK_HEIGHT - TITLEBAR_HEIGHT;
    if (dragging_window->x < 0)
      dragging_window->x = 0;
    if (dragging_window->x > (int)primary_display.width - 100)
      dragging_window->x = primary_display.width - 100;
  }

  /* Handle window resizing */
  if (resizing_window && left_held) {
    int dx = x - resize_start_x;
    int dy = y - resize_start_y;
    int new_w = resize_start_w;
    int new_h = resize_start_h;
    int new_x = resize_start_win_x;
    int new_y = resize_start_win_y;

    /* Calculate new dimensions based on which edge is being dragged */
    if (resize_edge == RESIZE_RIGHT || resize_edge == RESIZE_BOTTOM_RIGHT ||
        resize_edge == RESIZE_TOP_RIGHT) {
      new_w = resize_start_w + dx;
    }
    if (resize_edge == RESIZE_LEFT || resize_edge == RESIZE_BOTTOM_LEFT ||
        resize_edge == RESIZE_TOP_LEFT) {
      new_w = resize_start_w - dx;
      new_x = resize_start_win_x + dx;
    }
    if (resize_edge == RESIZE_BOTTOM || resize_edge == RESIZE_BOTTOM_RIGHT ||
        resize_edge == RESIZE_BOTTOM_LEFT) {
      new_h = resize_start_h + dy;
    }
    if (resize_edge == RESIZE_TOP || resize_edge == RESIZE_TOP_LEFT ||
        resize_edge == RESIZE_TOP_RIGHT) {
      new_h = resize_start_h - dy;
      new_y = resize_start_win_y + dy;
    }

    /* Enforce minimum size */
    if (new_w < MIN_WINDOW_WIDTH) {
      if (resize_edge == RESIZE_LEFT || resize_edge == RESIZE_BOTTOM_LEFT ||
          resize_edge == RESIZE_TOP_LEFT) {
        new_x = resize_start_win_x + resize_start_w - MIN_WINDOW_WIDTH;
      }
      new_w = MIN_WINDOW_WIDTH;
    }
    if (new_h < MIN_WINDOW_HEIGHT) {
      if (resize_edge == RESIZE_TOP || resize_edge == RESIZE_TOP_LEFT ||
          resize_edge == RESIZE_TOP_RIGHT) {
        new_y = resize_start_win_y + resize_start_h - MIN_WINDOW_HEIGHT;
      }
      new_h = MIN_WINDOW_HEIGHT;
    }

    /* Clamp to screen */
    if (new_y < MENU_BAR_HEIGHT)
      new_y = MENU_BAR_HEIGHT;
    if (new_x < 0)
      new_x = 0;

    resizing_window->x = new_x;
    resizing_window->y = new_y;
    resizing_window->width = new_w;
    resizing_window->height = new_h;
  }

  if (left_release) {
    dragging_window = 0;
    resizing_window = 0;
    resize_edge = RESIZE_NONE;
  }

  prev_buttons = buttons;

  /* Handle desktop right-click (context menu) - check BEFORE left_click gate */
  if (right_click) {
    printk(KERN_INFO "RIGHT-CLICK at %d,%d buttons=%d\n", x, y, buttons);
    /* Check if right-click is on desktop area (not on window, menu bar, or
     * dock) */
    int on_window = 0;
    for (struct window *win = window_stack; win; win = win->next) {
      if (!win->visible)
        continue;
      if (x >= win->x && x < win->x + win->width && y >= win->y &&
          y < win->y + win->height) {
        on_window = 1;
        break;
      }
    }

    if (!on_window && y > MENU_BAR_HEIGHT &&
        y < (int)primary_display.height - DOCK_HEIGHT) {
      printk(KERN_INFO
             "RIGHT-CLICK on desktop, calling desktop_handle_click\n");
      /* Right-click on desktop - handle in desktop manager */
      desktop_handle_click(x, y, 2, 0); /* button 2 = right */
      return;
    }
  }

  /* Handle desktop left-click for icon selection - check BEFORE window checks
   */
  if (left_click) {
    /* Check context menu first */
    if (desktop_is_context_menu_visible()) {
      if (desktop_context_menu_click(x, y)) {
        return;
      }
    }

    /* Check menu bar dropdown BEFORE desktop icons (dropdown overlaps desktop
     * area) */
    if (menu_open == 1 && y >= MENU_BAR_HEIGHT && y < MENU_BAR_HEIGHT + 80 &&
        x >= 8 && x < 168) {
      int dropdown_y = MENU_BAR_HEIGHT;
      int rel_y = y - dropdown_y;

      printk("DROPDOWN CLICK: x=%d y=%d rel_y=%d\\n", x, y, rel_y);

      /* About Vib-OS (y+10) */
      if (rel_y >= 2 && rel_y < 32) {
        printk("Opening About window\\n");
        gui_create_window("About", 280, 180, 420, 260);
        menu_open = 0;
        return;
      }
      /* Settings (y+40) */
      if (rel_y >= 32 && rel_y < 58) {
        printk("Opening Settings window\\n");
        gui_create_window("Settings", 200, 120, 380, 320);
        menu_open = 0;
        return;
      }
      /* Restart (y+58) */
      if (rel_y >= 58 && rel_y < 80) {
        printk("Restart requested\\n");
        extern void arch_halt(void);
        arch_halt();
        menu_open = 0;
        return;
      }
      /* Click in dropdown but not on item - close menu */
      menu_open = 0;
      return;
    }

    /* Check if click is on desktop area (not on window) */
    int on_window = 0;
    for (struct window *win = window_stack; win; win = win->next) {
      if (!win->visible)
        continue;
      if (x >= win->x && x < win->x + win->width && y >= win->y &&
          y < win->y + win->height) {
        on_window = 1;
        break;
      }
    }

    if (!on_window && y > MENU_BAR_HEIGHT &&
        y < (int)primary_display.height - DOCK_HEIGHT) {
      /* Track double-click */
      int dx = x - last_click_x;
      int dy = y - last_click_y;
      if (dx < 0)
        dx = -dx;
      if (dy < 0)
        dy = -dy;

      if (dx < 10 && dy < 10) {
        click_count++;
        if (click_count >= 2) {
          /* Double click - open item */
          desktop_handle_double_click(x, y);
          click_count = 0;
          return;
        }
      } else {
        click_count = 1;
      }
      last_click_x = x;
      last_click_y = y;

      /* Single click - select icon */
      int shift_held = 0;
      if (desktop_handle_click(x, y, 1, shift_held)) {
        return; /* Click was on desktop icon */
      }
    }
  }

  /* Check if clicking on a window */
  if (!left_click)
    return;

  /* Check menu bar and dropdown clicks */
  if (y < MENU_BAR_HEIGHT ||
      (menu_open && y < MENU_BAR_HEIGHT + 80 && x < 170)) {

    printk("MENU DEBUG: x=%d y=%d menu_open=%d MBH=%d\\n", x, y, menu_open,
           MENU_BAR_HEIGHT);

    /* If dropdown is open, check dropdown item clicks */
    if (menu_open == 1 && y >= MENU_BAR_HEIGHT && y < MENU_BAR_HEIGHT + 80 &&
        x >= 8 && x < 168) {
      int dropdown_y = MENU_BAR_HEIGHT;
      int rel_y = y - dropdown_y;

      printk("MENU CLICK: x=%d y=%d rel_y=%d dropdown_y=%d\\n", x, y, rel_y,
             dropdown_y);

      /* About Vib-OS (y+10) - expanded range */
      if (rel_y >= 2 && rel_y < 32) {
        printk("MENU: Opening About window\\n");
        gui_create_window("About", 280, 180, 420, 260);
        menu_open = 0;
        return;
      }
      /* Settings (y+40) - expanded range */
      if (rel_y >= 32 && rel_y < 58) {
        printk("MENU: Opening Settings window\\n");
        gui_create_window("Settings", 200, 120, 380, 320);
        menu_open = 0;
        return;
      }
      /* Restart (y+58) - expanded range */
      if (rel_y >= 58 && rel_y < 80) {
        printk("MENU: Restart requested\\n");
        extern void arch_halt(void);
        arch_halt();
        menu_open = 0;
        return;
      }
      menu_open = 0;
      return;
    }

    /* Menu bar clicks */
    if (y < MENU_BAR_HEIGHT) {
      /* Apple menu / Vib-OS logo area (x < 90) - toggle dropdown */
      if (x < 90) {
        menu_open = menu_open ? 0 : 1;
        return;
      }

      /* Close menu if clicking elsewhere on menu bar */
      menu_open = 0;
    }
    return;
  }

  /* Close menu if clicking elsewhere */
  if (menu_open) {
    menu_open = 0;
  }

  for (struct window *win = window_stack; win; win = win->next) {
    if (!win->visible)
      continue;

    if (x >= win->x && x < win->x + win->width && y >= win->y &&
        y < win->y + win->height) {

      gui_focus_window(win);

      /* Check for resize edges FIRST (on any visible window) */
      {
        int at_left = (x >= win->x && x < win->x + RESIZE_BORDER);
        int at_right = (x >= win->x + win->width - RESIZE_BORDER &&
                        x < win->x + win->width);
        int at_top = (y >= win->y && y < win->y + RESIZE_BORDER);
        int at_bottom = (y >= win->y + win->height - RESIZE_BORDER &&
                         y < win->y + win->height);

        /* Determine which edge/corner */
        int edge = RESIZE_NONE;
        if (at_bottom && at_right)
          edge = RESIZE_BOTTOM_RIGHT;
        else if (at_bottom && at_left)
          edge = RESIZE_BOTTOM_LEFT;
        else if (at_top && at_right)
          edge = RESIZE_TOP_RIGHT;
        else if (at_top && at_left)
          edge = RESIZE_TOP_LEFT;
        else if (at_right)
          edge = RESIZE_RIGHT;
        else if (at_bottom)
          edge = RESIZE_BOTTOM;
        else if (at_left)
          edge = RESIZE_LEFT;
        else if (at_top && !win->has_titlebar)
          edge = RESIZE_TOP;

        if (edge != RESIZE_NONE) {
          resizing_window = win;
          resize_edge = edge;
          resize_start_x = x;
          resize_start_y = y;
          resize_start_w = win->width;
          resize_start_h = win->height;
          resize_start_win_x = win->x;
          resize_start_win_y = win->y;
          return;
        }
      }

      /* Check for traffic light buttons (on LEFT side now) */
      if (win->has_titlebar) {
        int btn_cy = win->y + BORDER_WIDTH + TITLEBAR_HEIGHT / 2;
        int btn_r = 8; /* Click radius slightly larger than visual */

        /* Close button (first) */
        int close_cx = win->x + BORDER_WIDTH + 18;
        if ((x - close_cx) * (x - close_cx) + (y - btn_cy) * (y - btn_cy) <=
            btn_r * btn_r) {
          gui_destroy_window(win);
          return;
        }

        /* Minimize button (second) */
        int min_cx = close_cx + 20;
        if ((x - min_cx) * (x - min_cx) + (y - btn_cy) * (y - btn_cy) <=
            btn_r * btn_r) {
          win->visible = false;
          win->state = WINDOW_MINIMIZED;
          return;
        }

        /* Zoom/Maximize button (third) */
        int zoom_cx = min_cx + 20;
        if ((x - zoom_cx) * (x - zoom_cx) + (y - btn_cy) * (y - btn_cy) <=
            btn_r * btn_r) {
          if (win->state == WINDOW_MAXIMIZED) {
            /* Restore */
            win->x = win->saved_x;
            win->y = win->saved_y;
            win->width = win->saved_width;
            win->height = win->saved_height;
            win->state = WINDOW_NORMAL;
          } else {
            /* Maximize */
            win->saved_x = win->x;
            win->saved_y = win->y;
            win->saved_width = win->width;
            win->saved_height = win->height;
            win->x = 0;
            win->y = MENU_BAR_HEIGHT;
            win->width = primary_display.width;
            win->height =
                primary_display.height - MENU_BAR_HEIGHT - DOCK_HEIGHT;
            win->state = WINDOW_MAXIMIZED;
          }
          return;
        }

        /* Start dragging if clicking on title bar */
        if (y >= win->y + BORDER_WIDTH &&
            y < win->y + BORDER_WIDTH + TITLEBAR_HEIGHT &&
            x >= win->x + BORDER_WIDTH + 70) { /* After traffic lights */
          dragging_window = win;
          drag_offset_x = x - win->x;
          drag_offset_y = y - win->y;
          return;
        }
      }

      /* Handle clicks inside Calculator window */
      if (win->title[0] == 'C' && win->title[1] == 'a' &&
          win->title[2] == 'l') {
        /* Calculate content area */
        int content_x = win->x + BORDER_WIDTH;
        int content_y = win->y + BORDER_WIDTH + TITLEBAR_HEIGHT;
        int content_w = win->width - BORDER_WIDTH * 2;
        int content_h = win->height - BORDER_WIDTH * 2 - TITLEBAR_HEIGHT;

        /* Button layout - 5x4 grid matching render */
        static const char btns[5][4] = {
            {'C', 'N', '%', '/'}, /* N = +/- (negate) */
            {'7', '8', '9', '*'},
            {'4', '5', '6', '-'},
            {'1', '2', '3', '+'},
            {'0', '0', '.', '='}};

        int disp_h = 70;
        int grid_x = content_x + 12;
        int grid_y = content_y + disp_h + 20;
        int grid_w = content_w - 24;
        int grid_h = content_h - disp_h - 32;
        int bw = (grid_w - 12) / 4;
        int bh = (grid_h - 16) / 5;
        int gap = 4;

        /* Check if click is in button area */
        if (x >= grid_x && y >= grid_y) {
          int col = (x - grid_x) / (bw + gap);
          int row = (y - grid_y) / (bh + gap);
          if (row >= 0 && row < 5 && col >= 0 && col < 4) {
            char btn = btns[row][col];
            /* Handle special buttons */
            if (btn == 'N') {
              /* +/- button - negate display */
              calc_display = -calc_display;
            } else if (btn == '%') {
              /* Percent - divide by 100 */
              calc_display = calc_display / 100;
            } else if (btn == '.') {
              /* Decimal point - ignore for integers */
            } else {
              calc_button_click(btn);
            }
          }
        }
        break;
      }

      /* Handle clicks inside Background Settings window */
      if (win->title[0] == 'B' && win->title[1] == 'a' &&
          win->title[2] == 'c') {
        int content_x = win->x + BORDER_WIDTH;
        int content_y = win->y + BORDER_WIDTH + TITLEBAR_HEIGHT;

        /* Wallpaper grid layout (matching render) */
        int grid_x = content_x + 15;
        int grid_y = content_y + 55;
        int thumb_w = 65;
        int thumb_h = 45;
        int gap = 10;

        for (int i = 0; i < NUM_WALLPAPERS; i++) {
          int col = i % 5;
          int row = i / 5;
          int tx = grid_x + col * (thumb_w + gap);
          int ty = grid_y + row * (thumb_h + gap + 20);

          if (x >= tx && x < tx + thumb_w && y >= ty && y < ty + thumb_h) {
            current_wallpaper = i;
            wallpaper_ensure_loaded(); /* Load JPEG if needed */
            break;
          }
        }
        break;
      }

      if (win->on_mouse) {
        win->on_mouse(win, x - win->x, y - win->y, buttons);
      }
      break;
    }
  }

  /* Check dock click */
  int dock_content_w =
      NUM_DOCK_ICONS * (DOCK_ICON_SIZE + DOCK_PADDING) - DOCK_PADDING + 32;
  int dock_x = (primary_display.width - dock_content_w) / 2;
  int dock_y = primary_display.height - DOCK_HEIGHT + 6;
  int dock_h = DOCK_HEIGHT - 12;

  if (y >= dock_y && y < dock_y + dock_h) {
    int icon_x = dock_x + 16;
    int icon_y_start = dock_y + (dock_h - DOCK_ICON_SIZE) / 2;

    /* Window spawn position with staggering */
    static int spawn_x = 100;
    static int spawn_y = 80;

    for (int i = 0; i < NUM_DOCK_ICONS; i++) {
      if (x >= icon_x && x < icon_x + DOCK_ICON_SIZE && y >= icon_y_start &&
          y < icon_y_start + DOCK_ICON_SIZE) {
        /* Clicked on icon i - create window or run app */
        switch (i) {
        case 0: /* Terminal */
        {
          struct window *win =
              gui_create_window("Terminal", spawn_x, spawn_y, 450, 320);
          /* Create and set active terminal */
          extern struct terminal *term_create(int x, int y, int cols, int rows);
          extern void term_set_active(struct terminal * term);
          extern void term_set_content_pos(struct terminal * t, int x, int y);
          int content_x = spawn_x + BORDER_WIDTH;
          int content_y = spawn_y + BORDER_WIDTH + TITLEBAR_HEIGHT;
          struct terminal *term = term_create(content_x, content_y, 55, 16);
          if (win && term) {
            win->userdata = term; /* Store terminal in window */
            term_set_active(term);
            term_set_content_pos(term, content_x, content_y);
          }
          break;
        }
        case 1: /* Files */
          gui_create_file_manager(spawn_x + 30, spawn_y + 20);
          break;
        case 2: /* Calculator */
          gui_create_window("Calculator", spawn_x + 60, spawn_y + 40, 260, 380);
          break;
        case 3: /* Notepad */
          /* Call open with NULL to just open blank */
          extern void gui_open_notepad(const char *path);
          gui_open_notepad(NULL);
          break;
        case 4: /* Settings */
          gui_create_window("Settings", spawn_x + 20, spawn_y + 30, 380, 320);
          break;
        case 5: /* Clock */
          gui_create_window("Clock", spawn_x + 50, spawn_y + 40, 260, 200);
          break;
        case 6: /* DOOM - Direct call test */
        {
          printk("GUI: Direct DOOM call test...\n");
          extern void *vfs_lookup(const char *path);
          extern int vfs_read_compat(void *node, void *buf, unsigned int size,
                                     unsigned int offset);
          extern int elf_load_at(void *data, unsigned int size,
                                 uint64_t load_addr, void *info);

          extern void *kapi_get(void);

          void *file = vfs_lookup("/bin/doom");
          if (!file) {
            printk("DOOM not found\n");
            break;
          }

          char *data = kmalloc(900000);
          int bytes = vfs_read_compat(file, data, 900000, 0);
          printk("Read %d bytes\n", bytes);

          typedef struct {
            uint64_t entry;
            uint64_t load_base;
            uint64_t load_size;
          } elf_info_t;
          elf_info_t info = {0};
          if (elf_load_at(data, bytes, 0x44000000, &info) != 0) {
            printk("ELF load failed\n");
            kfree(data);
            break;
          }
          kfree(data);
          printk("Entry at 0x%llx\n", info.entry);

          typedef int (*entry_t)(void *, int, char **);
          entry_t doom_main = (entry_t)info.entry;
          void *api = kapi_get();
          char *argv[] = {"/bin/doom", 0};
          printk("Calling DOOM directly at 0x%llx...\n", (uint64_t)doom_main);
          int ret = doom_main(api, 1, argv);
          printk("DOOM returned %d\n", ret);
        } break;
        case 7: /* Snake */
        {
          snake_init();
          gui_create_window("Snake", spawn_x + 70, spawn_y + 50, 340, 280);
        } break;
        case 8: /* Help */
          gui_create_window("Help", spawn_x + 120, spawn_y + 80, 350, 280);
          break;
        case 9: /* Browser */
          gui_create_window("Browser", spawn_x + 150, spawn_y + 90, 600, 450);
          break;
        }
        spawn_x = (spawn_x + 40) % 250 + 80;
        spawn_y = (spawn_y + 30) % 150 + 60;
        return;
      }
      icon_x += DOCK_ICON_SIZE + DOCK_PADDING;
    }
  }
}

/* ===================================================================== */
/* Initialization */
/* ===================================================================== */

int gui_init(uint32_t *framebuffer, uint32_t width, uint32_t height,
             uint32_t pitch) {
  printk(KERN_INFO "GUI: Initializing windowing system\n");

  primary_display.framebuffer = framebuffer;
  primary_display.width = width;
  primary_display.height = height;
  primary_display.pitch = pitch;
  primary_display.bpp = 32;

  /* ============================================= */
  /* LOADING SCREEN - Show during initialization  */
  /* ============================================= */

  /* Fill with dark gradient background */
  for (int y = 0; y < (int)height; y++) {
    int progress = (y * 256) / height;
    uint8_t r = 15 + (progress * 10) / 256;
    uint8_t g = 15 + (progress * 5) / 256;
    uint8_t b = 30 + (progress * 25) / 256;
    uint32_t color = (r << 16) | (g << 8) | b;
    for (int x = 0; x < (int)width; x++) {
      framebuffer[y * (pitch / 4) + x] = color;
    }
  }

  /* Draw Vib-OS logo text (large, centered) */
  const char *logo = "Vib-OS";
  int logo_x =
      (width - 6 * 16) / 2; /* 6 chars, roughly 16px each for "big" text */
  int logo_y = height / 2 - 60;

  /* Draw each character larger (2x scale simulation) */
  for (int i = 0; logo[i]; i++) {
    int cx = logo_x + i * 20;
    /* Draw character with bold effect */
    gui_draw_char(cx, logo_y, logo[i], 0xFFFFFF, 0x000000);
    gui_draw_char(cx + 1, logo_y, logo[i], 0xFFFFFF, 0x000000);
    gui_draw_char(cx, logo_y + 1, logo[i], 0xFFFFFF, 0x000000);
    gui_draw_char(cx + 1, logo_y + 1, logo[i], 0xFFFFFF, 0x000000);
  }

  /* Draw version text */
  const char *version = "v1.0 - Modern Desktop Experience";
  int ver_x = (width - 33 * 8) / 2;
  int ver_y = logo_y + 40;
  gui_draw_string(ver_x, ver_y, version, 0x9CA3AF, 0x000000);

  /* Draw loading bar background */
  int bar_w = 300;
  int bar_h = 8;
  int bar_x = (width - bar_w) / 2;
  int bar_y = height / 2 + 40;
  gui_draw_rect(bar_x, bar_y, bar_w, bar_h, 0x27272A);
  gui_draw_rect(bar_x, bar_y, bar_w, 1, 0x3F3F46);

  /* Animate loading bar */
  const char *loading_msgs[] = {"Initializing hardware...",
                                "Loading desktop environment...",
                                "Starting services...", "Welcome to Vib-OS!"};

  for (int stage = 0; stage < 4; stage++) {
    /* Update progress bar */
    int fill = (bar_w * (stage + 1)) / 4;
    gui_draw_rect(bar_x + 1, bar_y + 1, fill - 2, bar_h - 2, 0x6366F1);

    /* Draw loading message */
    int msg_x = (width - 30 * 8) / 2;
    int msg_y = bar_y + 20;
    gui_draw_rect(msg_x - 10, msg_y - 2, 260, 20,
                  0x000000); /* Clear previous */
    gui_draw_string(msg_x, msg_y, loading_msgs[stage], 0xE4E4E7, 0x000000);

    /* Small delay (busy wait for effect) */
    for (volatile int d = 0; d < 2000000; d++)
      ;
  }

  /* Brief pause on "Welcome" */
  for (volatile int d = 0; d < 3000000; d++)
    ;

  /* ============================================= */
  /* END LOADING SCREEN                           */
  /* ============================================= */

  /* Register input callbacks */
  extern void input_set_gui_key_callback(void (*callback)(int key));
  extern void gui_handle_key_event(int key);
  input_set_gui_key_callback(gui_handle_key_event);

  /* Allocate backbuffer for double-buffering */
  primary_display.backbuffer = kmalloc(pitch * height);

  /* Clear windows */
  for (int i = 0; i < MAX_WINDOWS; i++) {
    windows[i].id = 0;
  }

  /* Initialize desktop manager */
  desktop_manager_init();

  printk(KERN_INFO "GUI: Display %ux%u initialized\n", width, height);

  return 0;
}

struct display *gui_get_display(void) { return &primary_display; }

uint32_t gui_get_screen_width(void) { return primary_display.width; }

uint32_t gui_get_screen_height(void) { return primary_display.height; }

struct window *gui_create_file_manager(int x, int y) {
  struct window *win = gui_create_window("File Manager", x, y, 450, 350);
  if (win) {
    struct fm_state *st = kmalloc(sizeof(struct fm_state));
    if (st) {
      st->path[0] = '/';
      st->path[1] = '\0';
      st->selected[0] = '\0';
      st->scroll_y = 0;
      win->userdata = st;
      win->on_mouse = fm_on_mouse;
    }
  }
  return win;
}

/* Create file manager at specific path */
struct window *gui_create_file_manager_path(int x, int y, const char *path) {
  /* Build title with path */
  char title[128] = "File Manager - ";
  int ti = 15;
  if (path) {
    for (int i = 0; path[i] && ti < 126; i++) {
      title[ti++] = path[i];
    }
  }
  title[ti] = '\0';

  struct window *win = gui_create_window(title, x, y, 450, 350);
  if (win) {
    struct fm_state *st = kmalloc(sizeof(struct fm_state));
    if (st) {
      /* Copy the provided path */
      if (path) {
        int i = 0;
        while (path[i] && i < 255) {
          st->path[i] = path[i];
          i++;
        }
        st->path[i] = '\0';
      } else {
        st->path[0] = '/';
        st->path[1] = '\0';
      }
      st->selected[0] = '\0';
      st->scroll_y = 0;
      win->userdata = st;
      win->on_mouse = fm_on_mouse;
    }
  }
  return win;
}

static void notepad_on_mouse(struct window *win, int x, int y, int buttons) {
  /* Check Save Button */
  /* Toolbar area */
  int content_y = BORDER_WIDTH + TITLEBAR_HEIGHT;
  if (y >= content_y && y < content_y + 30) {
    if (x >= BORDER_WIDTH + 10 && x < BORDER_WIDTH + 70) {
      /* Save clicked */
      if (notepad_filepath[0]) {
        /* Open for writing */
        struct file *f = vfs_open(notepad_filepath, O_RDWR | O_CREAT, 0644);
        if (f) {
          /* Determine length */
          int len = 0;
          while (notepad_text[len] && len < NOTEPAD_MAX_TEXT)
            len++;

          /* Write content */
          extern ssize_t vfs_write(struct file * file, const char *buf,
                                   size_t count);
          vfs_write(f, notepad_text, len);
          /* Reset file position if we want to ensure we wrote from start?
           * vfs_open sets pos 0. */

          /* Hack: Force truncation in ramfs? For now just overwrite. */

          vfs_close(f);

          printk("Notepad: Saved %d bytes to %s\n", len, notepad_filepath);
        }
      }
    }
  }
}

void gui_open_notepad(const char *path) {
  /* Clear existing state */
  notepad_text[0] = '\0';
  notepad_cursor = 0;
  notepad_filepath[0] = '\0';

  if (path) {
    /* Copy path */
    int i = 0;
    while (path[i] && i < 255) {
      notepad_filepath[i] = path[i];
      i++;
    }
    notepad_filepath[i] = '\0';

    /* Read file */
    struct file *f = vfs_open(path, O_RDONLY, 0);
    if (f) {
      /* Read up to max */
      extern ssize_t vfs_read(struct file * file, char *buf, size_t count);
      int bytes = vfs_read(f, notepad_text, NOTEPAD_MAX_TEXT - 1);
      if (bytes >= 0) {
        notepad_text[bytes] = '\0';
        if (bytes < NOTEPAD_MAX_TEXT)
          notepad_text[bytes] = '\0';
        notepad_cursor = bytes;
      }
      vfs_close(f);
    }
  }

  struct window *win = gui_create_window("Notepad", 150, 80, 450, 350);
  if (win) {
    win->on_mouse = notepad_on_mouse;
  }
}

static void rename_on_mouse(struct window *win, int x, int y, int buttons) {
  /* Check Save Button */
  int content_y = BORDER_WIDTH + TITLEBAR_HEIGHT;
  if (y >= content_y && y < content_y + 30) {
    if (x >= BORDER_WIDTH + 10 && x < BORDER_WIDTH + 70) {
      /* Save (Rename) clicked */
      if (rename_path[0] && rename_text[0]) {
        /* Construct new full path */
        char new_full_path[512];

        /* Extract parent dir from rename_path */
        int i = 0;
        int last_slash = -1;
        while (rename_path[i]) {
          new_full_path[i] = rename_path[i];
          if (rename_path[i] == '/')
            last_slash = i;
          i++;
        }

        /* Append new name after last slash */
        int idx = last_slash + 1;
        int t_idx = 0;
        while (rename_text[t_idx]) {
          new_full_path[idx++] = rename_text[t_idx++];
        }
        new_full_path[idx] = '\0';

        /* Call vfs_rename */
        extern int vfs_rename(const char *old, const char *new);
        int ret = vfs_rename(rename_path, new_full_path);

        if (ret == 0) {
          printk("Rename successful: %s -> %s\n", rename_path, new_full_path);
          win->visible = 0; /* Close window */
        } else {
          printk("Rename failed: %d\n", ret);
        }
      }
    }
  }
}

void gui_open_rename(const char *path) {
  /* Clear state */
  rename_text[0] = '\0';
  rename_cursor = 0;

  /* Copy path */
  int i = 0;
  while (path[i] && i < 511) {
    rename_path[i] = path[i];
    i++;
  }
  rename_path[i] = '\0';

  /* Pre-fill text with filename only */
  int last_slash = -1;
  i = 0;
  while (rename_path[i]) {
    if (rename_path[i] == '/')
      last_slash = i;
    i++;
  }

  const char *filename = rename_path + last_slash + 1;
  i = 0;
  while (filename[i] && i < 255) {
    rename_text[i] = filename[i];
    i++;
  }
  rename_text[i] = '\0';
  rename_cursor = i;

  struct window *win = gui_create_window("Rename", 200, 150, 300, 150);
  if (win) {
    win->on_mouse = rename_on_mouse;
  }
}

/* ===================================================================== */
/* Image Viewer                                                          */
/* ===================================================================== */

/* Bootstrap image declarations (defined in separate .c files) */
extern const unsigned char bootstrap_landscape_jpg[];
extern const unsigned int bootstrap_landscape_jpg_len;
extern const unsigned char bootstrap_portrait_jpg[];
extern const unsigned int bootstrap_portrait_jpg_len;
extern const unsigned char bootstrap_square_jpg[];
extern const unsigned int bootstrap_square_jpg_len;
extern const unsigned char bootstrap_wallpaper_jpg[];
extern const unsigned int bootstrap_wallpaper_jpg_len;
extern const unsigned char bootstrap_test_png[];
extern const unsigned int bootstrap_test_png_len;

/* g_imgview is already defined as extern earlier in the file */

#define NUM_BOOTSTRAP_IMAGES 5

static const unsigned char *get_bootstrap_image_data(int index) {
  switch (index) {
  case 0:
    return bootstrap_landscape_jpg;
  case 1:
    return bootstrap_portrait_jpg;
  case 2:
    return bootstrap_square_jpg;
  case 3:
    return bootstrap_wallpaper_jpg;
  case 4:
    return bootstrap_test_png;
  default:
    return NULL;
  }
}

static unsigned int get_bootstrap_image_len(int index) {
  switch (index) {
  case 0:
    return bootstrap_landscape_jpg_len;
  case 1:
    return bootstrap_portrait_jpg_len;
  case 2:
    return bootstrap_square_jpg_len;
  case 3:
    return bootstrap_wallpaper_jpg_len;
  case 4:
    return bootstrap_test_png_len;
  default:
    return 0;
  }
}

static const char *get_bootstrap_image_name(int index) {
  static const char *names[] = {"Landscape", "Portrait", "Square", "Wallpaper",
                                "PNG Test"};
  if (index >= 0 && index < NUM_BOOTSTRAP_IMAGES)
    return names[index];
  return "Unknown";
}

static void image_viewer_load_bootstrap(int index) {
  if (index < 0 || index >= NUM_BOOTSTRAP_IMAGES)
    return;

  /* Free previous image */
  if (g_imgview.loaded) {
    media_free_image(&g_imgview.image);
  }

  /* Decode image - detect format by magic bytes */
  const unsigned char *data = get_bootstrap_image_data(index);
  unsigned int len = get_bootstrap_image_len(index);
  int ret = -1;
  /* PNG magic: 0x89 'P' 'N' 'G' */
  if (len >= 4 && data[0] == 0x89 && data[1] == 'P' && data[2] == 'N' &&
      data[3] == 'G') {
    ret = media_decode_png(data, len, &g_imgview.image);
  } else {
    ret = media_decode_jpeg(data, len, &g_imgview.image);
  }

  if (ret == 0) {
    g_imgview.loaded = 1;
    g_imgview.zoom_pct = 100;
    g_imgview.offset_x = 0;
    g_imgview.offset_y = 0;
    g_imgview.current_image_index = index;

    int i = 0;
    const char *name = get_bootstrap_image_name(index);
    while (name[i] && i < 255) {
      g_imgview.current_file[i] = name[i];
      i++;
    }
    g_imgview.current_file[i] = '\0';

    printk(KERN_INFO "Image Viewer: Loaded %s (%dx%d)\n",
           get_bootstrap_image_name(index), g_imgview.image.width,
           g_imgview.image.height);
  } else {
    printk(KERN_ERR "Image Viewer: Failed to load image\n");
    g_imgview.loaded = 0;
  }
}

/* Load image from folder file list */
static void image_viewer_load_from_folder(int index) {
  if (index < 0 || index >= g_imgview.file_count)
    return;

  /* Build full path */
  char full_path[512];
  int pi = 0;
  for (int i = 0; g_imgview.folder_path[i] && pi < 500; i++) {
    full_path[pi++] = g_imgview.folder_path[i];
  }
  for (int i = 0; g_imgview.file_list[index][i] && pi < 511; i++) {
    full_path[pi++] = g_imgview.file_list[index][i];
  }
  full_path[pi] = '\0';

  /* Load image file */
  uint8_t *data = NULL;
  size_t size = 0;
  if (media_load_file(full_path, &data, &size) != 0) {
    printk(KERN_ERR "Image Viewer: Failed to read %s\n", full_path);
    return;
  }

  /* Free previous image */
  if (g_imgview.loaded) {
    media_free_image(&g_imgview.image);
  }

  /* Decode image */
  int ret = -1;
  if (size >= 4 && data[0] == 0x89 && data[1] == 'P' && data[2] == 'N' &&
      data[3] == 'G') {
    ret = media_decode_png(data, size, &g_imgview.image);
  } else {
    ret = media_decode_jpeg(data, size, &g_imgview.image);
  }
  media_free_file(data);

  if (ret == 0) {
    g_imgview.loaded = 1;
    g_imgview.zoom_pct = 100;
    g_imgview.offset_x = 0;
    g_imgview.offset_y = 0;
    g_imgview.file_index = index;
    g_imgview.current_image_index = -1; /* Mark as folder-loaded */

    /* Copy filename */
    int i = 0;
    while (g_imgview.file_list[index][i] && i < 255) {
      g_imgview.current_file[i] = g_imgview.file_list[index][i];
      i++;
    }
    g_imgview.current_file[i] = '\0';

    printk(KERN_INFO "Image Viewer: Loaded %s (%dx%d) [%d/%d]\n",
           g_imgview.current_file, g_imgview.image.width,
           g_imgview.image.height, index + 1, g_imgview.file_count);
  } else {
    printk(KERN_ERR "Image Viewer: Failed to decode %s\n", full_path);
    g_imgview.loaded = 0;
  }
}

static void image_viewer_on_draw(struct window *win) {
  int screen_w = primary_display.width;
  int screen_h = primary_display.height;

  /* Content area (below titlebar, inside borders) */
  int content_x = win->x + BORDER_WIDTH;
  int content_y = win->y + BORDER_WIDTH + TITLEBAR_HEIGHT;
  int content_w = win->width - 2 * BORDER_WIDTH;
  int content_h = win->height - 2 * BORDER_WIDTH - TITLEBAR_HEIGHT;

  /* In fullscreen mode, use entire screen */
  int draw_x = g_imgview.fullscreen ? 0 : content_x;
  int draw_y = g_imgview.fullscreen ? 0 : content_y;
  int draw_w = g_imgview.fullscreen ? screen_w : content_w;
  int draw_h = g_imgview.fullscreen ? screen_h : content_h;

  /* Dark cinematic background */
  uint32_t bg_color = 0x0D0D0D;
  for (int y = draw_y; y < draw_y + draw_h; y++) {
    for (int x = draw_x; x < draw_x + draw_w; x++) {
      draw_pixel(x, y, bg_color);
    }
  }

  if (!g_imgview.loaded) {
    /* Elegant "No image" message */
    const char *msg = "Drop an image or click Next";
    int msg_len = 27;
    int text_x = draw_x + (draw_w - msg_len * 8) / 2;
    int text_y = draw_y + draw_h / 2;
    gui_draw_string(text_x, text_y, msg, 0x6B7280, bg_color);
    return;
  }

  /* Calculate display dimensions with rotation */
  int img_w = (int)g_imgview.image.width;
  int img_h = (int)g_imgview.image.height;
  int rot = g_imgview.rotation;

  /* Swap dimensions for 90/270 rotation */
  if (rot == 90 || rot == 270) {
    int tmp = img_w;
    img_w = img_h;
    img_h = tmp;
  }

  /* Auto-fit image to screen */
  int toolbar_h = 56;
  int avail_h = draw_h - toolbar_h - 20;
  int avail_w = draw_w - 20;

  int zoom = g_imgview.zoom_pct;
  if (zoom == 0) {
    /* Auto-fit mode */
    int zoom_w = (avail_w * 100) / img_w;
    int zoom_h = (avail_h * 100) / img_h;
    zoom = (zoom_w < zoom_h) ? zoom_w : zoom_h;
    if (zoom > 100)
      zoom = 100; /* Don't upscale */
  }

  int scaled_w = img_w * zoom / 100;
  int scaled_h = img_h * zoom / 100;

  /* Center image */
  int img_x = draw_x + (draw_w - scaled_w) / 2 + g_imgview.offset_x;
  int img_y = draw_y + (avail_h - scaled_h) / 2 + 10 + g_imgview.offset_y;

  /* Draw image with rotation */
  int orig_w = (int)g_imgview.image.width;
  int orig_h = (int)g_imgview.image.height;

  for (int dy = 0; dy < scaled_h; dy++) {
    int screen_y = img_y + dy;
    if (screen_y < draw_y || screen_y >= draw_y + avail_h + 10)
      continue;

    for (int dx = 0; dx < scaled_w; dx++) {
      int screen_x = img_x + dx;
      if (screen_x < draw_x || screen_x >= draw_x + draw_w)
        continue;

      /* Unscale to get image coordinates */
      int ix = dx * 100 / zoom;
      int iy = dy * 100 / zoom;

      /* Apply rotation transform */
      int src_x, src_y;
      switch (rot) {
      case 90:
        src_x = iy;
        src_y = orig_h - 1 - ix;
        break;
      case 180:
        src_x = orig_w - 1 - ix;
        src_y = orig_h - 1 - iy;
        break;
      case 270:
        src_x = orig_w - 1 - iy;
        src_y = ix;
        break;
      default: /* 0 */
        src_x = ix;
        src_y = iy;
        break;
      }

      if (src_x >= 0 && src_x < orig_w && src_y >= 0 && src_y < orig_h) {
        uint32_t pixel = g_imgview.image.pixels[src_y * orig_w + src_x];
        draw_pixel(screen_x, screen_y, pixel);
      }
    }
  }

  /* ============================================= */
  /* MODERN FLOATING TOOLBAR                      */
  /* ============================================= */

  int tb_w = 520;
  int tb_h = 48;
  int tb_x = draw_x + (draw_w - tb_w) / 2;
  int tb_y = draw_y + draw_h - tb_h - 16;

  /* Glassmorphism toolbar background */
  for (int y = tb_y; y < tb_y + tb_h; y++) {
    for (int x = tb_x; x < tb_x + tb_w; x++) {
      /* Semi-transparent dark with blur effect simulation */
      int dist_y = (y - tb_y < tb_h / 2) ? (y - tb_y) : (tb_y + tb_h - y);
      int alpha = 200 + (dist_y * 30 / (tb_h / 2));
      if (alpha > 230)
        alpha = 230;
      uint32_t bg = ((alpha / 10) << 16) | ((alpha / 10) << 8) | (alpha / 8);
      draw_pixel(x, y, bg);
    }
  }

  /* Rounded corners (top) */
  int corner_r = 12;
  for (int cy = 0; cy < corner_r; cy++) {
    for (int cx = 0; cx < corner_r; cx++) {
      int dx = corner_r - cx;
      int dy = corner_r - cy;
      if (dx * dx + dy * dy > corner_r * corner_r) {
        draw_pixel(tb_x + cx, tb_y + cy, bg_color);
        draw_pixel(tb_x + tb_w - 1 - cx, tb_y + cy, bg_color);
      }
    }
  }

  /* Toolbar buttons */
  int btn_size = 36;
  int btn_spacing = 8;
  int btn_y = tb_y + (tb_h - btn_size) / 2;
  int btn_x = tb_x + 16;

  /* Button icons (using ASCII for now) */
  const char *icons[] = {"<", ">", "R", "L", "+", "-", "F", "X"};
  const char *labels[] = {"Prev",  "Next",  "Rot R", "Rot L",
                          "Zoom+", "Zoom-", "Fit",   "Full"};
  uint32_t btn_bg = 0x374151;
  uint32_t btn_hover = 0x4B5563;
  uint32_t icon_color = 0xE5E7EB;

  for (int i = 0; i < 8; i++) {
    /* Check hover */
    int is_hover = (mouse_x >= btn_x && mouse_x < btn_x + btn_size &&
                    mouse_y >= btn_y && mouse_y < btn_y + btn_size);
    uint32_t bg = is_hover ? btn_hover : btn_bg;

    /* Draw rounded button */
    int r = 8;
    for (int y = 0; y < btn_size; y++) {
      for (int x = 0; x < btn_size; x++) {
        int in_corner = 0;
        if (x < r && y < r && (r - x) * (r - x) + (r - y) * (r - y) > r * r)
          in_corner = 1;
        if (x >= btn_size - r && y < r &&
            (x - btn_size + r + 1) * (x - btn_size + r + 1) +
                    (r - y) * (r - y) >
                r * r)
          in_corner = 1;
        if (x < r && y >= btn_size - r &&
            (r - x) * (r - x) +
                    (y - btn_size + r + 1) * (y - btn_size + r + 1) >
                r * r)
          in_corner = 1;
        if (x >= btn_size - r && y >= btn_size - r &&
            (x - btn_size + r + 1) * (x - btn_size + r + 1) +
                    (y - btn_size + r + 1) * (y - btn_size + r + 1) >
                r * r)
          in_corner = 1;
        if (!in_corner) {
          draw_pixel(btn_x + x, btn_y + y, bg);
        }
      }
    }
    /* Draw pre-rendered RGBA icon from toolbar_icons.h */
    const uint32_t *icon_data = toolbar_icons[i];
    int icon_x = btn_x + (btn_size - TOOLBAR_ICON_SIZE) / 2;
    int icon_y = btn_y + (btn_size - TOOLBAR_ICON_SIZE) / 2;

    for (int iy = 0; iy < TOOLBAR_ICON_SIZE; iy++) {
      for (int ix = 0; ix < TOOLBAR_ICON_SIZE; ix++) {
        uint32_t pixel = icon_data[iy * TOOLBAR_ICON_SIZE + ix];
        uint8_t alpha = (pixel >> 24) & 0xFF;
        if (alpha > 0) {
          /* Simple alpha blending: if alpha > 128, draw white */
          if (alpha > 128) {
            draw_pixel(icon_x + ix, icon_y + iy, icon_color);
          }
        }
      }
    }
    btn_x += btn_size + btn_spacing;
  }

  /* Image info text */
  char info[64];
  int idx = 0;
  /* Dimensions */
  int w = (int)g_imgview.image.width;
  int h = (int)g_imgview.image.height;
  if (w >= 1000) {
    info[idx++] = '0' + (w / 1000) % 10;
  }
  if (w >= 100) {
    info[idx++] = '0' + (w / 100) % 10;
  }
  if (w >= 10) {
    info[idx++] = '0' + (w / 10) % 10;
  }
  info[idx++] = '0' + w % 10;
  info[idx++] = 'x';
  if (h >= 1000) {
    info[idx++] = '0' + (h / 1000) % 10;
  }
  if (h >= 100) {
    info[idx++] = '0' + (h / 100) % 10;
  }
  if (h >= 10) {
    info[idx++] = '0' + (h / 10) % 10;
  }
  info[idx++] = '0' + h % 10;
  info[idx++] = ' ';
  /* Rotation */
  if (rot > 0) {
    if (rot >= 100)
      info[idx++] = '0' + (rot / 100) % 10;
    if (rot >= 10)
      info[idx++] = '0' + (rot / 10) % 10;
    info[idx++] = '0' + rot % 10;
    info[idx++] = 176; /* degree symbol approximation */
  }
  info[idx] = '\0';

  gui_draw_string(tb_x + tb_w - 120, btn_y + 12, info, 0x9CA3AF, 0x1F2937);
}

static void image_viewer_on_mouse(struct window *win, int x, int y,
                                  int buttons) {
  /* x,y are already window-relative (0,0 = window top-left) */

  /* Content area within window (relative coords) */
  int content_x = BORDER_WIDTH;
  int content_y = BORDER_WIDTH + TITLEBAR_HEIGHT;
  int content_w = win->width - 2 * BORDER_WIDTH;
  int content_h = win->height - 2 * BORDER_WIDTH - TITLEBAR_HEIGHT;

  /* Toolbar position within content area */
  int tb_w = 520;
  int tb_h = 48;
  int tb_x = content_x + (content_w - tb_w) / 2;
  int tb_y = content_y + content_h - tb_h - 16;

  int btn_size = 36;
  int btn_spacing = 8;
  int btn_y_pos = tb_y + (tb_h - btn_size) / 2;
  int btn_x = tb_x + 16;

  /* Check each of the 8 toolbar buttons */
  for (int i = 0; i < 8; i++) {
    if (x >= btn_x && x < btn_x + btn_size && y >= btn_y_pos &&
        y < btn_y_pos + btn_size) {

      switch (i) {
      case 0: /* Previous */
        if (g_imgview.file_count > 0) {
          /* Folder-based navigation */
          int new_index = g_imgview.file_index - 1;
          if (new_index < 0)
            new_index = g_imgview.file_count - 1;
          image_viewer_load_from_folder(new_index);
        } else {
          /* Fallback to bootstrap images */
          if (g_imgview.current_image_index <= 0) {
            image_viewer_load_bootstrap(NUM_BOOTSTRAP_IMAGES - 1);
          } else {
            image_viewer_load_bootstrap(g_imgview.current_image_index - 1);
          }
        }
        break;

      case 1: /* Next */
        if (g_imgview.file_count > 0) {
          /* Folder-based navigation */
          int new_index = g_imgview.file_index + 1;
          if (new_index >= g_imgview.file_count)
            new_index = 0;
          image_viewer_load_from_folder(new_index);
        } else {
          /* Fallback to bootstrap images */
          if (g_imgview.current_image_index < 0 ||
              g_imgview.current_image_index >= NUM_BOOTSTRAP_IMAGES - 1) {
            image_viewer_load_bootstrap(0);
          } else {
            image_viewer_load_bootstrap(g_imgview.current_image_index + 1);
          }
        }
        break;

      case 2: /* Rotate Right (CW) */
        g_imgview.rotation = (g_imgview.rotation + 90) % 360;
        break;

      case 3: /* Rotate Left (CCW) */
        g_imgview.rotation = (g_imgview.rotation + 270) % 360;
        break;

      case 4: /* Zoom In */
        if (g_imgview.zoom_pct == 0) {
          g_imgview.zoom_pct = 100;
        }
        g_imgview.zoom_pct = g_imgview.zoom_pct * 125 / 100;
        if (g_imgview.zoom_pct > 400) {
          g_imgview.zoom_pct = 400;
        }
        break;

      case 5: /* Zoom Out */
        if (g_imgview.zoom_pct == 0) {
          g_imgview.zoom_pct = 100;
        }
        g_imgview.zoom_pct = g_imgview.zoom_pct * 80 / 100;
        if (g_imgview.zoom_pct < 10) {
          g_imgview.zoom_pct = 10;
        }
        break;

      case 6:                   /* Fit */
        g_imgview.zoom_pct = 0; /* Auto-fit mode */
        g_imgview.offset_x = 0;
        g_imgview.offset_y = 0;
        break;

      case 7: /* Fullscreen Toggle */
        g_imgview.fullscreen = !g_imgview.fullscreen;
        g_imgview.zoom_pct = 0; /* Reset to auto-fit */
        g_imgview.offset_x = 0;
        g_imgview.offset_y = 0;
        break;
      }
      return;
    }
    btn_x += btn_size + btn_spacing;
  }

  /* Pan disabled - image stays fixed */
  (void)buttons; /* Unused */
}

void gui_open_image_gallery(void) {
  struct window *win = gui_create_window("Image Gallery", 100, 80, 700, 550);
  if (win) {
    win->on_draw = image_viewer_on_draw;
    win->on_mouse = image_viewer_on_mouse;

    /* Load first image */
    if (!g_imgview.loaded) {
      image_viewer_load_bootstrap(0);
    }
  }
}
