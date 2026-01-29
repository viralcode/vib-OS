/*
 * VibCode x64 - Complete GUI Compositor
 * Desktop, Dock, Context Menu, Terminal, File Manager, Windows
 * With HD Wallpaper support and real dock icons
 */

#include "../include/font.h"
#include "../include/gui.h"
#include "../include/kmalloc.h"
#include "../include/media.h"
#include "../include/ps2.h"
#include "../include/usb.h"
#include "../include/string.h"
#include "../include/vfs.h"
#include "dock_icons.h"

/* External font data */
extern const uint8_t font_data[256][16];

/* External HD wallpaper JPEG data */
extern unsigned char hd_wallpaper_landscape_jpg[];
extern unsigned int hd_wallpaper_landscape_jpg_len;
extern unsigned char hd_wallpaper_nature_jpg[];
extern unsigned int hd_wallpaper_nature_jpg_len;
extern unsigned char hd_wallpaper_city_jpg[];
extern unsigned int hd_wallpaper_city_jpg_len;

/* ===================================================================== */
/* Theme Colors - Catppuccin Mocha inspired                              */
/* ===================================================================== */

#define THEME_BASE 0x1E1E2E
#define THEME_SURFACE 0x313244
#define THEME_OVERLAY 0x45475A
#define THEME_TEXT 0xCDD6F4
#define THEME_SUBTEXT 0xA6ADC8
#define THEME_LAVENDER 0xB4BEFE
#define THEME_BLUE 0x89B4FA
#define THEME_SAPPHIRE 0x74C7EC
#define THEME_SKY 0x89DCEB
#define THEME_TEAL 0x94E2D5
#define THEME_GREEN 0xA6E3A1
#define THEME_YELLOW 0xF9E2AF
#define THEME_PEACH 0xFAB387
#define THEME_MAROON 0xEBA0AC
#define THEME_RED 0xF38BA8
#define THEME_MAUVE 0xCBA6F7
#define THEME_PINK 0xF5C2E7
#define THEME_FLAMINGO 0xF2CDCD
#define THEME_ROSEWATER 0xF5E0DC

/* UI Element colors */
#define COLOR_WINDOW_BG THEME_BASE
#define COLOR_TITLE_BAR 0x181825
#define COLOR_MENU_BAR_BG 0x11111B
#define COLOR_DOCK_BG 0x1E1E28
#define COLOR_DOCK_BORDER 0x45475A
#define COLOR_CTX_MENU_BG 0x313244
#define COLOR_CTX_MENU_HOVER 0x45475A
#define COLOR_TERM_BG 0x1E1E2E
#define COLOR_TERM_FG 0xCDD6F4

/* UI Scale - dynamically set based on resolution */
int ui_scale = 1;
#define UI_SCALE_VAL(x) ((x) * ui_scale)

/* Redraw flags - declared early for keyboard callback */
static int needs_redraw = 1;
static int full_redraw = 1;

/* Dock Configuration - scaled at runtime */
static int dock_height = 70;
static int dock_icon_size = 48;
static int dock_padding = 10;
#define NUM_DOCK_ICONS 10

/* Menu Bar - scaled at runtime */
static int menu_bar_height = 28;

/* For compatibility with existing code using constants */
#define DOCK_HEIGHT dock_height
#define DOCK_ICON_SIZE dock_icon_size
#define DOCK_PADDING dock_padding
#define MENU_BAR_HEIGHT menu_bar_height

/* ===================================================================== */
/* Global State                                                          */
/* ===================================================================== */

/* Mouse state */
static int mouse_x = 400;
static int mouse_y = 300;
static int mouse_buttons = 0;
static int dock_hover_index = -1;
static int dock_smooth_sizes[NUM_DOCK_ICONS] = {0};

/* Window dragging state */
#define DRAG_NONE 0
#define DRAG_TERMINAL 1
#define DRAG_FILE_MANAGER 2
#define DRAG_CALCULATOR 3
#define DRAG_SNAKE 4
#define DRAG_NOTEPAD 5
#define DRAG_CLOCK 6
#define DRAG_HELP 7
#define DRAG_IMAGE_VIEWER 8
static int dragging_window = DRAG_NONE;
static int drag_offset_x = 0;
static int drag_offset_y = 0;

/* Window z-order tracking - higher index = on top */
#define WIN_FILE_MANAGER 0
#define WIN_TERMINAL 1
#define WIN_CALCULATOR 2
#define WIN_SNAKE 3
#define WIN_NOTEPAD 4
#define WIN_CLOCK 5
#define WIN_HELP 6
#define WIN_IMAGE_VIEWER 7
#define NUM_MANAGED_WINDOWS 8
static int window_z_order[NUM_MANAGED_WINDOWS] = {0, 1, 2, 3, 4, 5, 6, 7};
static int focused_window_id = WIN_TERMINAL; /* Terminal focused by default */

/* Bring window to front (highest z-order) */
static void bring_window_to_front(int win_id) {
  if (win_id < 0 || win_id >= NUM_MANAGED_WINDOWS) return;
  focused_window_id = win_id;
  
  /* Find current z of this window */
  int current_z = -1;
  for (int i = 0; i < NUM_MANAGED_WINDOWS; i++) {
    if (window_z_order[i] == win_id) {
      current_z = i;
      break;
    }
  }
  if (current_z < 0) return;
  
  /* Shift all windows above it down, put this one on top */
  for (int i = current_z; i < NUM_MANAGED_WINDOWS - 1; i++) {
    window_z_order[i] = window_z_order[i + 1];
  }
  window_z_order[NUM_MANAGED_WINDOWS - 1] = win_id;
}

/* Context menu state */
typedef struct {
  int visible;
  int x, y;
  int width, height;
  int hover_index;
  int item_count;
  struct {
    char label[32];
    int enabled;
    int separator;
  } items[16];
} context_menu_t;

static context_menu_t ctx_menu = {0};

/* Terminal state */
#define TERM_COLS 80
#define TERM_ROWS 24
#define TERM_MAX_INPUT 256

typedef struct {
  char chars[TERM_ROWS][TERM_COLS];
  uint8_t colors[TERM_ROWS][TERM_COLS];
  int cursor_x, cursor_y;
  int content_x, content_y; /* Screen position */
  int width, height;
  char input[TERM_MAX_INPUT];
  int input_len;
  char cwd[128];
  int visible;
} terminal_t;

static terminal_t terminal = {0};

/* File Manager state */
typedef struct {
  char current_path[256];
  struct {
    char name[64];
    int is_dir;
  } entries[32];
  int entry_count;
  int selected;
  int visible;
  int x, y, width, height;
} file_manager_t;

static file_manager_t file_manager = {0};

/* Simulated filesystem - matches reference OS */
static const struct {
  const char *path;
  const char *name;
  int is_dir;
} sim_files[] = {
  /* Root directories */
  {"/", "Documents", 1},
  {"/", "Downloads", 1},
  {"/", "Pictures", 1},
  {"/", "Desktop", 1},
  /* Desktop contents */
  {"/Desktop", "notes.txt", 0},
  {"/Desktop", "readme.txt", 0},
  {"/Desktop", "Projects", 1},
  /* Documents contents */
  {"/Documents", "report.txt", 0},
  /* Pictures contents */
  {"/Pictures", "landscape.jpg", 0},
  {"/Pictures", "nature.jpg", 0},
  {"/Pictures", "city.jpg", 0},
  {"/Pictures", "wallpaper.jpg", 0},
  /* Projects folder */
  {"/Desktop/Projects", "project1.txt", 0},
  {NULL, NULL, 0}
};

/* Window state - MAX_WINDOWS defined in gui.h */

typedef struct {
  int id;
  char title[64];
  int x, y, width, height;
  int visible;
  int focused;
  int type; /* 0=normal, 1=terminal, 2=filemanager, 3=settings */
} win_t;

static win_t windows[MAX_WINDOWS] = {0};
static int window_count = 0;
static int next_window_id = 1;

/* ===================================================================== */
/* Wallpaper Manager                                                     */
/* ===================================================================== */

#define NUM_WALLPAPERS 8
static int current_wallpaper = 0;

/* Wallpaper types: 0 = gradient, 1 = JPEG image */
static struct {
  int type;           /* 0 = gradient, 1 = JPEG image */
  uint8_t tr, tg, tb; /* Gradient: Top color */
  uint8_t br, bg, bb; /* Gradient: Bottom color */
  const char *name;   /* Display name */
} wallpapers[NUM_WALLPAPERS] = {
    {1, 0, 0, 0, 0, 0, 0, "Landscape"},
    {1, 0, 0, 0, 0, 0, 0, "Nature"},
    {1, 0, 0, 0, 0, 0, 0, "City"},
    {0, 30, 27, 75, 15, 27, 62, "Indigo Night"},
    {0, 20, 60, 100, 10, 30, 60, "Ocean Blue"},
    {0, 60, 20, 60, 30, 15, 45, "Purple Haze"},
    {0, 20, 20, 20, 5, 5, 10, "Midnight"},
    {0, 80, 60, 30, 40, 30, 20, "Golden Hour"},
};

/* Cached wallpaper image for desktop background */
static media_image_t wallpaper_image = {0, 0, (void *)0};
static int wallpaper_loaded = -1;

/* Static buffer for wallpaper to avoid heap fragmentation (4MB for 1024x1024) */
static uint32_t wallpaper_buffer[1024 * 1024];

/* Scaled wallpaper cache (screen-sized) */
static uint32_t *wallpaper_cache = (uint32_t *)0;
static size_t wallpaper_cache_size = 0;
static int wallpaper_cache_w = 0;
static int wallpaper_cache_h = 0;
static int wallpaper_cache_dirty = 1;

/* Load wallpaper image if needed */
static void wallpaper_ensure_loaded(void) {
  if (wallpapers[current_wallpaper].type != 1)
    return; /* Gradient, no load */
  if (wallpaper_loaded == current_wallpaper && wallpaper_image.pixels != (void *)0)
    return; /* Already loaded */

  /* Reset previous image state */
  wallpaper_image.width = 0;
  wallpaper_image.height = 0;
  wallpaper_loaded = -1;

  /* Select the right embedded JPEG data based on wallpaper index */
  const uint8_t *data = (void *)0;
  size_t size = 0;

  switch (current_wallpaper) {
  case 0:
    data = hd_wallpaper_landscape_jpg;
    size = hd_wallpaper_landscape_jpg_len;
    break;
  case 1:
    data = hd_wallpaper_nature_jpg;
    size = hd_wallpaper_nature_jpg_len;
    break;
  case 2:
    data = hd_wallpaper_city_jpg;
    size = hd_wallpaper_city_jpg_len;
    break;
  default:
    return; /* Not an image wallpaper */
  }

  if (data && size > 0) {
    if (media_decode_jpeg_buffer(data, size, &wallpaper_image, wallpaper_buffer,
                                 sizeof(wallpaper_buffer)) == 0) {
      wallpaper_loaded = current_wallpaper;
      wallpaper_cache_dirty = 1;
    } else {
      /* Fallback to gradient if decode fails */
      wallpapers[current_wallpaper].type = 0;
      wallpaper_loaded = -1;
      wallpaper_cache_dirty = 1;
    }
  }
}

/* Get wallpaper pixel color at position */
static uint32_t wallpaper_get_pixel(int x, int y, int width, int height) {
  int idx = current_wallpaper;

  /* Image wallpaper */
  if (wallpapers[idx].type == 1 && wallpaper_image.pixels) {
    /* Scale image to fit screen */
    int img_x = (x * (int)wallpaper_image.width) / width;
    int img_y = (y * (int)wallpaper_image.height) / height;
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

/* Cycle to next wallpaper */
static void wallpaper_next(void) {
  current_wallpaper = (current_wallpaper + 1) % NUM_WALLPAPERS;
  if (wallpapers[current_wallpaper].type == 1) {
    wallpaper_ensure_loaded();
  }
  wallpaper_cache_dirty = 1;
}

/* Set specific wallpaper */
static void wallpaper_set(int index) {
  if (index >= 0 && index < NUM_WALLPAPERS) {
    current_wallpaper = index;
    if (wallpapers[current_wallpaper].type == 1) {
      wallpaper_ensure_loaded();
    }
    wallpaper_cache_dirty = 1;
  }
}

static void wallpaper_build_cache(void) {
  size_t needed = (size_t)screen_width * screen_height * sizeof(uint32_t);
  if (needed == 0)
    return;

  if (wallpaper_cache == (void *)0 || needed > wallpaper_cache_size ||
      wallpaper_cache_w != (int)screen_width ||
      wallpaper_cache_h != (int)screen_height) {
    if (wallpaper_cache) {
      kfree(wallpaper_cache);
    }
    wallpaper_cache = (uint32_t *)kmalloc(needed);
    wallpaper_cache_size = needed;
    wallpaper_cache_w = (int)screen_width;
    wallpaper_cache_h = (int)screen_height;
  }

  if (!wallpaper_cache)
    return;

  for (uint32_t y = 0; y < screen_height; y++) {
    for (uint32_t x = 0; x < screen_width; x++) {
      wallpaper_cache[y * screen_width + x] =
          wallpaper_get_pixel(x, y, screen_width, screen_height);
    }
  }

  wallpaper_cache_dirty = 0;
}

/* Dock icons info - matching test folder */
static const char *dock_labels[] = {"Terminal", "Files", "Calculator", "Notes", "Settings",
                                    "Clock", "Snake", "Help", "Browser", "DOOM"};

/* Icon background colors for macOS Big Sur style dock */
static const uint32_t dock_icon_colors[] = {
    0x1E1E1E, /* 0 - Terminal - dark gray/black */
    0x3B82F6, /* 1 - Files - blue */
    0xFF9500, /* 2 - Calculator - orange */
    0xFFCC00, /* 3 - Notes - yellow */
    0x8E8E93, /* 4 - Settings - gray */
    0x000000, /* 5 - Clock - black */
    0x34D399, /* 6 - Snake - teal green */
    0x3B82F6, /* 7 - Help - blue */
    0x0EA5E9, /* 8 - Browser - sky blue */
    0xCC0000, /* 9 - DOOM - red */
};

/* ===================================================================== */
/* Drawing Helpers                                                       */
/* ===================================================================== */

/* Draw character with foreground color */
void gui_draw_char(int x, int y, char c, uint32_t fg) {
  if (c < 0)
    c = 0;
  const uint8_t *glyph = font_data[(uint8_t)c];
  int scale = ui_scale > 0 ? ui_scale : 1;
  for (int row = 0; row < 16; row++) {
    uint8_t bits = glyph[row];
    for (int col = 0; col < 8; col++) {
      if (bits & (0x80 >> col)) {
        int px = x + col * scale;
        int py = y + row * scale;
        if (scale == 1) {
          fb_put_pixel(px, py, fg);
        } else {
          fb_fill_rect(px, py, scale, scale, fg);
        }
      }
    }
  }
}

/* Draw string */
void gui_draw_string(int x, int y, const char *str, uint32_t color) {
  int scale = ui_scale > 0 ? ui_scale : 1;
  while (*str) {
    if (*str == '\n') {
      y += 16 * scale;
    } else {
      gui_draw_char(x, y, *str, color);
      x += 8 * scale;
    }
    str++;
  }
}

/* Draw string with background */
void gui_draw_string_bg(int x, int y, const char *str, uint32_t fg,
                        uint32_t bg) {
  int scale = ui_scale > 0 ? ui_scale : 1;
  while (*str) {
    fb_fill_rect(x, y, 8 * scale, 16 * scale, bg);
    gui_draw_char(x, y, *str, fg);
    x += 8 * scale;
    str++;
  }
}

/* Draw line (Bresenham) */
void gui_draw_line(int x0, int y0, int x1, int y1, uint32_t color) {
  int dx = x1 - x0;
  int dy = y1 - y0;
  int sx = dx > 0 ? 1 : -1;
  int sy = dy > 0 ? 1 : -1;
  dx = dx < 0 ? -dx : dx;
  dy = dy < 0 ? -dy : dy;

  int err = (dx > dy ? dx : -dy) / 2;

  while (1) {
    fb_put_pixel(x0, y0, color);
    if (x0 == x1 && y0 == y1)
      break;
    int e2 = err;
    if (e2 > -dx) {
      err -= dy;
      x0 += sx;
    }
    if (e2 < dy) {
      err += dx;
      y0 += sy;
    }
  }
}

/* Draw filled circle */
void gui_draw_circle(int cx, int cy, int r, uint32_t color) {
  for (int y = -r; y <= r; y++) {
    for (int x = -r; x <= r; x++) {
      if (x * x + y * y <= r * r) {
        fb_put_pixel(cx + x, cy + y, color);
      }
    }
  }
}

/* Draw traffic light buttons with icons (macOS style) */
static void draw_traffic_lights(int x, int y, int title_h) {
  int btn_cx = x + UI_SCALE_VAL(20);
  int btn_cy = y + UI_SCALE_VAL(16);
  int btn_r = UI_SCALE_VAL(6);
  int icon_size = UI_SCALE_VAL(2);  /* Icon line thickness */
  
  /* Close button - Red with X icon */
  gui_draw_circle(btn_cx, btn_cy, btn_r, THEME_RED);
  for (int i = -icon_size; i <= icon_size; i++) {
    fb_put_pixel(btn_cx + i, btn_cy + i, 0x7F1D1D);  /* Dark red X */
    fb_put_pixel(btn_cx + i, btn_cy - i, 0x7F1D1D);
  }
  
  /* Minimize button - Yellow with - icon */
  btn_cx += UI_SCALE_VAL(20);
  gui_draw_circle(btn_cx, btn_cy, btn_r, THEME_YELLOW);
  for (int i = -icon_size; i <= icon_size; i++) {
    fb_put_pixel(btn_cx + i, btn_cy, 0x78350F);  /* Dark amber - */
  }
  
  /* Maximize/Zoom button - Green with + icon */
  btn_cx += UI_SCALE_VAL(20);
  gui_draw_circle(btn_cx, btn_cy, btn_r, THEME_GREEN);
  for (int i = -icon_size; i <= icon_size; i++) {
    fb_put_pixel(btn_cx + i, btn_cy, 0x14532D);  /* Dark green + */
    fb_put_pixel(btn_cx, btn_cy + i, 0x14532D);
  }
}

/* Draw rounded rectangle */
void gui_draw_rounded_rect(int x, int y, int w, int h, int r, uint32_t color) {
  /* Main body */
  fb_fill_rect(x + r, y, w - 2 * r, h, color);
  fb_fill_rect(x, y + r, w, h - 2 * r, color);

  /* Corners */
  for (int py = 0; py < r; py++) {
    for (int px = 0; px < r; px++) {
      if ((r - px - 1) * (r - px - 1) + (r - py - 1) * (r - py - 1) <= r * r) {
        fb_put_pixel(x + px, y + py, color);
        fb_put_pixel(x + w - 1 - px, y + py, color);
        fb_put_pixel(x + px, y + h - 1 - py, color);
        fb_put_pixel(x + w - 1 - px, y + h - 1 - py, color);
      }
    }
  }
}

/* ===================================================================== */
/* Dock with Magnification                                               */
/* ===================================================================== */

/* Get dock icon data pointer by index - matching dock_labels order */
static const uint32_t *get_dock_icon_data(int icon_idx) {
  switch (icon_idx) {
  case 0:
    return dock_icon_terminal;    /* Terminal */
  case 1:
    return dock_icon_folder;      /* Files */
  case 2:
    return dock_icon_calculator;  /* Calculator */
  case 3:
    return dock_icon_notes;       /* Notes */
  case 4:
    return dock_icon_settings;    /* Settings */
  case 5:
    return dock_icon_clock;       /* Clock */
  case 6:
    return dock_icon_snake;       /* Snake */
  case 7:
    return dock_icon_help;        /* Help */
  case 8:
    return dock_icon_web;         /* Browser */
  case 9:
    return dock_icon_doom;        /* DOOM */
  default:
    return dock_icon_terminal;
  }
}

static void draw_dock_icon(int x, int y, int size, int icon_idx,
                           uint32_t color) {
  (void)color; /* Not used when drawing real icons */

  const uint32_t *icon_data = get_dock_icon_data(icon_idx);

  /* Scale the 48x48 icon to the requested size */
  for (int py = 0; py < size; py++) {
    int src_y = (py * DOCK_ICON_BITMAP_SIZE) / size;
    for (int px = 0; px < size; px++) {
      int src_x = (px * DOCK_ICON_BITMAP_SIZE) / size;
      uint32_t pixel = icon_data[src_y * DOCK_ICON_BITMAP_SIZE + src_x];

      /* Check alpha (0xFFFFFFFF = white, 0x00000000 = transparent) */
      if (pixel == 0xFFFFFFFF) {
        fb_put_pixel(x + px, y + py, 0xFFFFFFFF);
      } else if (pixel != 0x00000000) {
        /* Semi-transparent or colored pixel */
        fb_put_pixel(x + px, y + py, pixel);
      }
      /* Skip fully transparent pixels */
    }
  }
}

static void draw_dock(void) {
  int base_y = screen_height - DOCK_HEIGHT + UI_SCALE_VAL(6);
  int total_w =
      NUM_DOCK_ICONS * (DOCK_ICON_SIZE + DOCK_PADDING) - DOCK_PADDING + UI_SCALE_VAL(32);
  int dock_x = (screen_width - total_w) / 2;
  int dock_h = DOCK_HEIGHT - UI_SCALE_VAL(12);

  /* Check if mouse is near dock */
  int mouse_in_dock = (mouse_y >= (int)screen_height - DOCK_HEIGHT - UI_SCALE_VAL(40));
  int hovered_idx = -1;

  /* Calculate magnification for each icon */
  int icon_sizes[NUM_DOCK_ICONS];
  int render_x[NUM_DOCK_ICONS];
  
  for (int i = 0; i < NUM_DOCK_ICONS; i++) {
    int target = DOCK_ICON_SIZE;
    int base_center_x = dock_x + UI_SCALE_VAL(16) + i * (DOCK_ICON_SIZE + DOCK_PADDING) +
                        DOCK_ICON_SIZE / 2;
    
    if (mouse_in_dock) {
      int dist = mouse_x - base_center_x;
      if (dist < 0)
        dist = -dist;
      
      int magnify_range = UI_SCALE_VAL(120);
      if (dist < magnify_range) {
        /* Quadratic ease magnification */
        int scale = (magnify_range - dist) * 256 / magnify_range;
        scale = scale * scale / 256;
        target += UI_SCALE_VAL(32) * scale / 256;  /* Max +32px at 1x */
        
        if (dist < DOCK_ICON_SIZE / 2 + UI_SCALE_VAL(5))
          hovered_idx = i;
      }
    }
    
    /* Smooth animation */
    if (dock_smooth_sizes[i] < target) {
      dock_smooth_sizes[i] += UI_SCALE_VAL(4);
      if (dock_smooth_sizes[i] > target)
        dock_smooth_sizes[i] = target;
    } else if (dock_smooth_sizes[i] > target) {
      dock_smooth_sizes[i] -= UI_SCALE_VAL(4);
      if (dock_smooth_sizes[i] < target)
        dock_smooth_sizes[i] = target;
    }
    icon_sizes[i] = dock_smooth_sizes[i];
  }

  /* Calculate dynamic total width and positions */
  int dynamic_w = 0;
  for (int i = 0; i < NUM_DOCK_ICONS; i++) {
    dynamic_w += icon_sizes[i];
    if (i < NUM_DOCK_ICONS - 1)
      dynamic_w += DOCK_PADDING;
  }
  dynamic_w += UI_SCALE_VAL(32); /* Padding */
  
  int dynamic_dock_x = (screen_width - dynamic_w) / 2;
  int cur_x = dynamic_dock_x + UI_SCALE_VAL(16);
  for (int i = 0; i < NUM_DOCK_ICONS; i++) {
    render_x[i] = cur_x;
    cur_x += icon_sizes[i] + DOCK_PADDING;
  }

  /* Draw dock background */
  gui_draw_rounded_rect(dynamic_dock_x - 1, base_y - 1, dynamic_w + 2, dock_h + 2, UI_SCALE_VAL(16), 0x2A2A3A);
  gui_draw_rounded_rect(dynamic_dock_x, base_y, dynamic_w, dock_h, UI_SCALE_VAL(15), 0x1E1E28);
  
  /* Highlight line */
  for (int i = dynamic_dock_x + UI_SCALE_VAL(14); i < dynamic_dock_x + dynamic_w - UI_SCALE_VAL(14); i++) {
    fb_put_pixel(i, base_y + 1, 0x3A3A4A);
  }

  /* Draw icons with colored backgrounds */
  int center_y = base_y + dock_h / 2;
  
  for (int i = 0; i < NUM_DOCK_ICONS; i++) {
    int size = icon_sizes[i];
    int x = render_x[i];
    int y = center_y - size / 2 - (size - DOCK_ICON_SIZE) / 3; /* Rise up when magnified */
    
    /* Draw colored rounded background */
    int icon_r = size / 5;
    uint32_t bg_color = dock_icon_colors[i];
    
    /* Draw rounded rectangle background */
    fb_fill_rect(x + icon_r, y, size - 2 * icon_r, size, bg_color);
    fb_fill_rect(x, y + icon_r, size, size - 2 * icon_r, bg_color);
    
    /* Draw corners */
    for (int dy = -icon_r; dy <= icon_r; dy++) {
      for (int dx = -icon_r; dx <= icon_r; dx++) {
        if (dx * dx + dy * dy <= icon_r * icon_r) {
          fb_put_pixel(x + icon_r + dx, y + icon_r + dy, bg_color);
          fb_put_pixel(x + size - icon_r - 1 + dx, y + icon_r + dy, bg_color);
          fb_put_pixel(x + icon_r + dx, y + size - icon_r - 1 + dy, bg_color);
          fb_put_pixel(x + size - icon_r - 1 + dx, y + size - icon_r - 1 + dy, bg_color);
        }
      }
    }
    
    /* Top highlight */
    for (int px = x + icon_r; px < x + size - icon_r; px++) {
      uint32_t hl = (bg_color & 0xFEFEFE) + 0x202020;
      fb_put_pixel(px, y + 2, hl);
    }
    
    /* Draw the icon bitmap on top */
    const uint32_t *icon_data = get_dock_icon_data(i);
    int bmp_size = size * 3 / 4;
    int offset = (size - bmp_size) / 2;
    
    for (int py = 0; py < bmp_size; py++) {
      int src_y = (py * DOCK_ICON_BITMAP_SIZE) / bmp_size;
      for (int px = 0; px < bmp_size; px++) {
        int src_x = (px * DOCK_ICON_BITMAP_SIZE) / bmp_size;
        uint32_t pixel = icon_data[src_y * DOCK_ICON_BITMAP_SIZE + src_x];
        
        /* Check alpha channel */
        uint8_t alpha = (pixel >> 24) & 0xFF;
        if (alpha > 128) {
          fb_put_pixel(x + offset + px, y + offset + py, pixel & 0xFFFFFF);
        }
      }
    }
  }
  
  /* Draw hover label */
  if (hovered_idx >= 0) {
    const char *label = dock_labels[hovered_idx];
    int label_w = font_string_width(label) + UI_SCALE_VAL(16);
    int label_h = UI_SCALE_VAL(24);
    int label_x = render_x[hovered_idx] + icon_sizes[hovered_idx] / 2 - label_w / 2;
    int label_y = base_y - UI_SCALE_VAL(40);
    
    gui_draw_rounded_rect(label_x, label_y, label_w, label_h, UI_SCALE_VAL(6), 0x303040);
    gui_draw_string(label_x + UI_SCALE_VAL(8), label_y + (label_h - FONT_HEIGHT * ui_scale) / 2, label, 0xFFFFFF);
    
    /* Triangle pointer */
    int tri_x = label_x + label_w / 2;
    int tri_y = label_y + label_h;
    int tri_size = UI_SCALE_VAL(5);
    for (int t = 0; t < tri_size; t++) {
      for (int j = -t; j <= t; j++) {
        fb_put_pixel(tri_x + j, tri_y + t, 0x303040);
      }
    }
  }
}

/* ===================================================================== */
/* Context Menu                                                          */
/* ===================================================================== */

static void ctx_menu_clear(void) {
  ctx_menu.item_count = 0;
  ctx_menu.hover_index = -1;
}

static void ctx_menu_add(const char *label, int enabled) {
  if (ctx_menu.item_count >= 16)
    return;
  strncpy(ctx_menu.items[ctx_menu.item_count].label, label, 31);
  ctx_menu.items[ctx_menu.item_count].label[31] = '\0';
  ctx_menu.items[ctx_menu.item_count].enabled = enabled;
  ctx_menu.items[ctx_menu.item_count].separator = 0;
  ctx_menu.item_count++;
}

static void ctx_menu_add_separator(void) {
  if (ctx_menu.item_count >= 16)
    return;
  ctx_menu.items[ctx_menu.item_count].label[0] = '\0';
  ctx_menu.items[ctx_menu.item_count].separator = 1;
  ctx_menu.item_count++;
}

static void ctx_menu_show(int x, int y) {
  ctx_menu_clear();
  ctx_menu_add("Open", 1);
  ctx_menu_add("Open With...", 1);
  ctx_menu_add_separator();
  ctx_menu_add("New Folder", 1);
  ctx_menu_add("New File", 1);
  ctx_menu_add_separator();
  ctx_menu_add("Cut", 1);
  ctx_menu_add("Copy", 1);
  ctx_menu_add("Paste", 0);
  ctx_menu_add_separator();
  ctx_menu_add("Rename", 1);
  ctx_menu_add("Delete", 1);
  ctx_menu_add_separator();
  ctx_menu_add("Properties", 1);

  ctx_menu.x = x;
  ctx_menu.y = y;
  ctx_menu.width = UI_SCALE_VAL(160);
  ctx_menu.height = ctx_menu.item_count * UI_SCALE_VAL(24) + UI_SCALE_VAL(8);
  ctx_menu.visible = 1;

  /* Adjust if off screen */
  if (ctx_menu.x + ctx_menu.width > (int)screen_width) {
    ctx_menu.x = screen_width - ctx_menu.width - UI_SCALE_VAL(4);
  }
  if (ctx_menu.y + ctx_menu.height > (int)screen_height) {
    ctx_menu.y = screen_height - ctx_menu.height - UI_SCALE_VAL(4);
  }
}

static void draw_context_menu(void) {
  if (!ctx_menu.visible)
    return;

  int x = ctx_menu.x;
  int y = ctx_menu.y;
  int w = ctx_menu.width;
  int h = ctx_menu.height;

  /* Shadow */
  fb_fill_rect(x + UI_SCALE_VAL(4), y + UI_SCALE_VAL(4), w, h, 0x11111B);

  /* Background */
  gui_draw_rounded_rect(x, y, w, h, UI_SCALE_VAL(8), COLOR_CTX_MENU_BG);

  /* Items */
  int item_h = UI_SCALE_VAL(24);
  int item_y = y + UI_SCALE_VAL(4);
  for (int i = 0; i < ctx_menu.item_count; i++) {
    if (ctx_menu.items[i].separator) {
      fb_fill_rect(x + UI_SCALE_VAL(8), item_y + UI_SCALE_VAL(10), w - UI_SCALE_VAL(16), 1, THEME_OVERLAY);
      item_y += item_h;
      continue;
    }

    /* Hover highlight */
    if (i == ctx_menu.hover_index) {
      gui_draw_rounded_rect(x + UI_SCALE_VAL(4), item_y + UI_SCALE_VAL(2), w - UI_SCALE_VAL(8), UI_SCALE_VAL(20), UI_SCALE_VAL(4),
                            COLOR_CTX_MENU_HOVER);
    }

    uint32_t color = ctx_menu.items[i].enabled ? THEME_TEXT : THEME_SUBTEXT;
    gui_draw_string(x + UI_SCALE_VAL(12), item_y + UI_SCALE_VAL(4), ctx_menu.items[i].label, color);

    item_y += item_h;
  }
}

/* ===================================================================== */
/* Terminal                                                              */
/* ===================================================================== */

static void term_init(void) {
  int font_w = FONT_WIDTH * ui_scale;
  int font_h = FONT_HEIGHT * ui_scale;
  terminal.width = TERM_COLS * font_w + UI_SCALE_VAL(20);
  terminal.height = TERM_ROWS * font_h + UI_SCALE_VAL(40);
  terminal.content_x = (screen_width - terminal.width) / 2;
  terminal.content_y = (screen_height - terminal.height) / 2;
  terminal.cursor_x = 0;
  terminal.cursor_y = 0;
  terminal.input_len = 0;
  strcpy(terminal.cwd, "/");
  terminal.visible = 0;

  /* Clear screen */
  for (int r = 0; r < TERM_ROWS; r++) {
    for (int c = 0; c < TERM_COLS; c++) {
      terminal.chars[r][c] = ' ';
      terminal.colors[r][c] = 7;
    }
  }
}

static void term_scroll(void) {
  for (int r = 0; r < TERM_ROWS - 1; r++) {
    memcpy(terminal.chars[r], terminal.chars[r + 1], TERM_COLS);
    memcpy(terminal.colors[r], terminal.colors[r + 1], TERM_COLS);
  }
  for (int c = 0; c < TERM_COLS; c++) {
    terminal.chars[TERM_ROWS - 1][c] = ' ';
    terminal.colors[TERM_ROWS - 1][c] = 7;
  }
}

static void term_putc(char c, uint8_t color) {
  if (c == '\n') {
    terminal.cursor_x = 0;
    terminal.cursor_y++;
    if (terminal.cursor_y >= TERM_ROWS) {
      term_scroll();
      terminal.cursor_y = TERM_ROWS - 1;
    }
  } else if (c == '\r') {
    terminal.cursor_x = 0;
  } else {
    terminal.chars[terminal.cursor_y][terminal.cursor_x] = c;
    terminal.colors[terminal.cursor_y][terminal.cursor_x] = color;
    terminal.cursor_x++;
    if (terminal.cursor_x >= TERM_COLS) {
      terminal.cursor_x = 0;
      terminal.cursor_y++;
      if (terminal.cursor_y >= TERM_ROWS) {
        term_scroll();
        terminal.cursor_y = TERM_ROWS - 1;
      }
    }
  }
}

static void term_puts(const char *str, uint8_t color) {
  while (*str) {
    term_putc(*str++, color);
  }
}

static void term_prompt(void) {
  term_puts("vib-os", 10); /* Green */
  term_puts(":", 7);       /* White */
  term_puts("~", 12);      /* Blue */
  term_puts("$ ", 7);      /* White */
}

static void term_execute(const char *cmd) {
  term_putc('\n', 7);

  if (strcmp(cmd, "ls") == 0) {
    /* List files in current directory */
    for (int i = 0; sim_files[i].path != NULL; i++) {
      if (strcmp(sim_files[i].path, terminal.cwd) == 0) {
        if (sim_files[i].is_dir) {
          term_puts(sim_files[i].name, 12); /* Blue */
        } else {
          term_puts(sim_files[i].name, 7);
        }
        term_puts("  ", 7);
      }
    }
    term_putc('\n', 7);
  } else if (strcmp(cmd, "pwd") == 0) {
    term_puts(terminal.cwd, 7);
    term_putc('\n', 7);
  } else if (strcmp(cmd, "clear") == 0) {
    for (int r = 0; r < TERM_ROWS; r++) {
      for (int c = 0; c < TERM_COLS; c++) {
        terminal.chars[r][c] = ' ';
      }
    }
    terminal.cursor_x = 0;
    terminal.cursor_y = 0;
  } else if (strcmp(cmd, "help") == 0) {
    term_puts("Vib-OS Terminal v2.0\n", 14);
    term_puts("File Commands:\n", 11);
    term_puts("  ls        - List directory contents\n", 7);
    term_puts("  cd <dir>  - Change directory\n", 7);
    term_puts("  pwd       - Print working directory\n", 7);
    term_puts("  cat <f>   - Display file contents\n", 7);
    term_puts("System:\n", 11);
    term_puts("  neofetch  - System info\n", 7);
    term_puts("  uname     - Show OS info\n", 7);
    term_puts("  id        - Show user/group info\n", 7);
    term_puts("  hostname  - Show hostname\n", 7);
    term_puts("  free      - Memory usage\n", 7);
    term_puts("  ps        - Process list\n", 7);
    term_puts("  clear     - Clear screen\n", 7);
    term_puts("  help      - This help message\n", 7);
  } else if (strcmp(cmd, "uname") == 0 || strcmp(cmd, "uname -a") == 0) {
    term_puts("Vib-OS 1.0.0 x86_64\n", 11);
  } else if (strcmp(cmd, "neofetch") == 0) {
    term_puts("       _  _         ___  ____  \n", 14);
    term_puts(" __   _(_)| |__     / _ \\/ ___| \n", 14);
    term_puts(" \\ \\ / / || '_ \\   | | | \\___ \\ \n", 14);
    term_puts("  \\ V /| || |_) |  | |_| |___) |\n", 14);
    term_puts("   \\_/ |_||_.__/    \\___/|____/ \n", 14);
    term_puts("\n", 7);
    term_puts("OS:      ", 11);
    term_puts("Vib-OS 1.0.0\n", 7);
    term_puts("Host:    ", 11);
    term_puts("QEMU x86_64 Virtual Machine\n", 7);
    term_puts("Kernel:  ", 11);
    term_puts("1.0.0-x86_64\n", 7);
    term_puts("Uptime:  ", 11);
    term_puts("0 mins\n", 7);
    term_puts("Shell:   ", 11);
    term_puts("vsh 1.0\n", 7);
    term_puts("Memory:  ", 11);
    term_puts("12 MB / 512 MB\n", 7);
    term_puts("CPU:     ", 11);
    term_puts("QEMU Virtual CPU\n", 7);
  } else if (strcmp(cmd, "id") == 0) {
    term_puts("uid=0(root) gid=0(root) groups=0(root)\n", 7);
  } else if (strcmp(cmd, "hostname") == 0) {
    term_puts("vib-os\n", 7);
  } else if (strcmp(cmd, "free") == 0) {
    term_puts("              total        used        free\n", 7);
    term_puts("Mem:         512 MB       12 MB      500 MB\n", 7);
    term_puts("Swap:          0 MB        0 MB        0 MB\n", 7);
  } else if (strcmp(cmd, "ps") == 0) {
    term_puts("  PID TTY          TIME CMD\n", 7);
    term_puts("    1 ?        00:00:00 init\n", 7);
    term_puts("    2 ?        00:00:00 kthread\n", 7);
    term_puts("   10 tty1     00:00:00 shell\n", 7);
  } else if (strcmp(cmd, "whoami") == 0) {
    term_puts("root\n", 7);
  } else if (strncmp(cmd, "cd ", 3) == 0) {
    const char *dir = cmd + 3;
    while (*dir == ' ')
      dir++;
    if (strcmp(dir, "..") == 0) {
      /* Go up */
      char *last = terminal.cwd + strlen(terminal.cwd) - 1;
      while (last > terminal.cwd && *last != '/')
        last--;
      if (last > terminal.cwd)
        *last = '\0';
      else
        strcpy(terminal.cwd, "/");
    } else if (strcmp(dir, "/") == 0) {
      strcpy(terminal.cwd, "/");
    } else {
      /* Check if dir exists */
      int found = 0;
      for (int i = 0; sim_files[i].path != NULL; i++) {
        if (strcmp(sim_files[i].path, terminal.cwd) == 0 &&
            strcmp(sim_files[i].name, dir) == 0 && sim_files[i].is_dir) {
          found = 1;
          if (strcmp(terminal.cwd, "/") == 0) {
            strcpy(terminal.cwd + 1, dir);
          } else {
            strcat(terminal.cwd, "/");
            strcat(terminal.cwd, dir);
          }
          break;
        }
      }
      if (!found) {
        term_puts("cd: no such directory: ", 9);
        term_puts(dir, 9);
        term_putc('\n', 7);
      }
    }
  } else if (strlen(cmd) > 0) {
    term_puts("command not found: ", 9);
    term_puts(cmd, 9);
    term_putc('\n', 7);
  }

  term_prompt();
}

static void draw_terminal(void) {
  if (!terminal.visible)
    return;

  int x = terminal.content_x;
  int y = terminal.content_y;
  int w = terminal.width;
  int h = terminal.height;
  int title_h = UI_SCALE_VAL(32);
  int font_w = FONT_WIDTH * ui_scale;
  int font_h = FONT_HEIGHT * ui_scale;

  /* Window shadow */
  fb_fill_rect(x + UI_SCALE_VAL(6), y + UI_SCALE_VAL(6), w, h, 0x11111B);

  /* Window background */
  fb_fill_rect(x, y, w, h, COLOR_TERM_BG);

  /* Title bar */
  fb_fill_rect(x, y, w, title_h, COLOR_TITLE_BAR);

  /* Traffic lights with icons */
  draw_traffic_lights(x, y, title_h);

  /* Title */
  gui_draw_string(x + w / 2 - (font_string_width("Terminal") / 2),
                  y + (title_h - font_h) / 2, "Terminal", THEME_TEXT);

  /* Terminal content */
  int tx = x + UI_SCALE_VAL(10);
  int ty = y + title_h + UI_SCALE_VAL(6);

  for (int r = 0; r < TERM_ROWS; r++) {
    for (int c = 0; c < TERM_COLS; c++) {
      char ch = terminal.chars[r][c];
      if (ch != ' ') {
        uint32_t color = THEME_TEXT;
        switch (terminal.colors[r][c]) {
        case 9:
          color = THEME_RED;
          break;
        case 10:
          color = THEME_GREEN;
          break;
        case 11:
          color = THEME_YELLOW;
          break;
        case 12:
          color = THEME_BLUE;
          break;
        case 13:
          color = THEME_MAUVE;
          break;
        case 14:
          color = THEME_SAPPHIRE;
          break;
        }
        gui_draw_char(tx + c * font_w, ty + r * font_h, ch, color);
      }
    }
  }

  /* Input buffer display - draw starting at cursor position */
  for (int i = 0; i < terminal.input_len; i++) {
    int cx = terminal.cursor_x + i;
    if (cx >= 0 && cx < TERM_COLS) {
      gui_draw_char(tx + cx * font_w, ty + terminal.cursor_y * font_h,
                    terminal.input[i],
                    THEME_TEXT);
    }
  }

  /* Cursor - blinks after the input text */
  static int cursor_blink = 0;
  cursor_blink++;
  int cursor_draw_x = terminal.cursor_x + terminal.input_len;
  if (cursor_draw_x < TERM_COLS && (cursor_blink / 15) % 2 == 0) {
    fb_fill_rect(tx + cursor_draw_x * font_w,
                 ty + terminal.cursor_y * font_h + (font_h - UI_SCALE_VAL(2)),
                 font_w, UI_SCALE_VAL(2), THEME_TEXT);
  }
}

/* ===================================================================== */
/* File Manager                                                          */
/* ===================================================================== */

static void fm_init(void) {
  file_manager.width = 450;
  file_manager.height = 350;
  file_manager.x = 80;
  file_manager.y = 50;
  strcpy(file_manager.current_path, "/");
  file_manager.selected = -1;
  file_manager.visible = 0;
}

static void fm_refresh(void) {
  file_manager.entry_count = 0;

  /* Add parent directory */
  if (strcmp(file_manager.current_path, "/") != 0) {
    strcpy(file_manager.entries[0].name, "..");
    file_manager.entries[0].is_dir = 1;
    file_manager.entry_count = 1;
  }

  /* Add entries for current path */
  for (int i = 0; sim_files[i].path != NULL && file_manager.entry_count < 32;
       i++) {
    if (strcmp(sim_files[i].path, file_manager.current_path) == 0) {
      strcpy(file_manager.entries[file_manager.entry_count].name,
             sim_files[i].name);
      file_manager.entries[file_manager.entry_count].is_dir =
          sim_files[i].is_dir;
      file_manager.entry_count++;
    }
  }
}

static void draw_file_manager(void) {
  if (!file_manager.visible)
    return;

  int x = file_manager.x;
  int y = file_manager.y;
  int w = file_manager.width;
  int h = file_manager.height;
  int title_h = UI_SCALE_VAL(32);
  int toolbar_h = UI_SCALE_VAL(40);

  /* Window shadow */
  fb_fill_rect(x + UI_SCALE_VAL(6), y + UI_SCALE_VAL(6), w, h, 0x11111B);

  /* Window background */
  fb_fill_rect(x, y, w, h, THEME_BASE);

  /* Title bar */
  fb_fill_rect(x, y, w, title_h, COLOR_TITLE_BAR);

  /* Traffic lights with icons */
  draw_traffic_lights(x, y, title_h);

  /* Title */
  gui_draw_string(x + UI_SCALE_VAL(80),
                  y + (title_h - FONT_HEIGHT * ui_scale) / 2,
                  "File Manager", THEME_TEXT);

  /* Toolbar */
  fb_fill_rect(x, y + title_h, w, toolbar_h, 0x2A2A35);
  fb_fill_rect(x, y + title_h + toolbar_h - 1, w, UI_SCALE_VAL(1), 0x404050);
  
  /* Back button */
  fb_fill_rect(x + UI_SCALE_VAL(10), y + title_h + UI_SCALE_VAL(8),
               UI_SCALE_VAL(60), UI_SCALE_VAL(24), 0x404050);
  gui_draw_string(x + UI_SCALE_VAL(22), y + title_h + UI_SCALE_VAL(12),
                  "Back", THEME_TEXT);
  
  /* New Folder button */
  fb_fill_rect(x + UI_SCALE_VAL(80), y + title_h + UI_SCALE_VAL(8),
               UI_SCALE_VAL(90), UI_SCALE_VAL(24), 0x404050);
  gui_draw_string(x + UI_SCALE_VAL(88), y + title_h + UI_SCALE_VAL(12),
                  "New Folder", THEME_TEXT);
  
  /* New File button */
  fb_fill_rect(x + UI_SCALE_VAL(180), y + title_h + UI_SCALE_VAL(8),
               UI_SCALE_VAL(80), UI_SCALE_VAL(24), 0x404050);
  gui_draw_string(x + UI_SCALE_VAL(190), y + title_h + UI_SCALE_VAL(12),
                  "New File", THEME_TEXT);

  /* Location bar */
  int loc_y = y + title_h + toolbar_h + UI_SCALE_VAL(2);
  gui_draw_string(x + UI_SCALE_VAL(10), loc_y + UI_SCALE_VAL(4),
                  "Location:", THEME_SUBTEXT);
  gui_draw_string(x + UI_SCALE_VAL(90), loc_y + UI_SCALE_VAL(4),
                  file_manager.current_path, THEME_TEXT);

  /* File list - grid style */
  int list_y = loc_y + UI_SCALE_VAL(24);
  int icon_size = UI_SCALE_VAL(48);
  int item_w = UI_SCALE_VAL(90);  /* Wider items for text */
  int item_h = UI_SCALE_VAL(80);  /* Taller items for spacing */
  int padding = UI_SCALE_VAL(20);
  int cols = (w - padding) / item_w;
  if (cols < 1) cols = 1;
  if (cols > 6) cols = 6;  /* Limit columns */
  
  for (int i = 0; i < file_manager.entry_count; i++) {
    int col = i % cols;
    int row = i / cols;
    int ix = x + UI_SCALE_VAL(10) + col * item_w;
    int iy = list_y + row * item_h;
    
    if (iy > y + h - 30)
      break;

    /* Selection highlight */
    if (i == file_manager.selected) {
      gui_draw_rounded_rect(ix, iy, item_w - UI_SCALE_VAL(8),
                            item_h - UI_SCALE_VAL(8), UI_SCALE_VAL(6),
                            THEME_OVERLAY);
    }

    /* Icon - centered */
    int icon_x = ix + (item_w - UI_SCALE_VAL(8) - UI_SCALE_VAL(32)) / 2;
    int icon_y = iy + UI_SCALE_VAL(4);
    
    if (file_manager.entries[i].is_dir) {
      /* Folder icon - larger */
      fb_fill_rect(icon_x, icon_y + UI_SCALE_VAL(8),
                   UI_SCALE_VAL(32), UI_SCALE_VAL(24), THEME_YELLOW);
      fb_fill_rect(icon_x, icon_y + UI_SCALE_VAL(4),
                   UI_SCALE_VAL(14), UI_SCALE_VAL(6), THEME_PEACH);
    } else {
      /* File icon */
      fb_fill_rect(icon_x + UI_SCALE_VAL(4), icon_y + UI_SCALE_VAL(2),
                   UI_SCALE_VAL(24), UI_SCALE_VAL(30), THEME_ROSEWATER);
      /* Lines on file */
      fb_fill_rect(icon_x + UI_SCALE_VAL(8), icon_y + UI_SCALE_VAL(10),
                   UI_SCALE_VAL(16), UI_SCALE_VAL(2), THEME_BASE);
      fb_fill_rect(icon_x + UI_SCALE_VAL(8), icon_y + UI_SCALE_VAL(16),
                   UI_SCALE_VAL(16), UI_SCALE_VAL(2), THEME_BASE);
      fb_fill_rect(icon_x + UI_SCALE_VAL(8), icon_y + UI_SCALE_VAL(22),
                   UI_SCALE_VAL(12), UI_SCALE_VAL(2), THEME_BASE);
    }

    /* Name - centered below icon, truncated to fit */
    char name_buf[12];
    int name_len = strlen(file_manager.entries[i].name);
    int max_chars = (item_w - UI_SCALE_VAL(10)) / (FONT_WIDTH * ui_scale);
    if (max_chars > 10) max_chars = 10;
    if (max_chars < 3) max_chars = 3;
    
    if (name_len > max_chars) {
      memcpy(name_buf, file_manager.entries[i].name, max_chars - 2);
      name_buf[max_chars - 2] = '.';
      name_buf[max_chars - 1] = '.';
      name_buf[max_chars] = '\0';
    } else {
      strcpy(name_buf, file_manager.entries[i].name);
    }
    int text_w = font_string_width(name_buf);
    int text_x = ix + (item_w - UI_SCALE_VAL(10) - text_w) / 2;
    if (text_x < ix) text_x = ix;
    uint32_t color = file_manager.entries[i].is_dir ? THEME_BLUE : THEME_TEXT;
    gui_draw_string(text_x, iy + UI_SCALE_VAL(50), name_buf, color);
  }
}

/* ===================================================================== */
/* Image Viewer                                                          */
/* ===================================================================== */

#include "toolbar_icons.h"

typedef struct {
  int visible;
  int x, y, width, height;
  int img_width, img_height;
  int rotation;      /* 0, 90, 180, 270 */
  int zoom;          /* Percentage 10-400, 0=fit */
  int offset_x, offset_y;
  int current_image; /* Index in image list */
  int fullscreen;
  char current_path[256];
  int hover_button;  /* For toolbar hover effect */
} image_viewer_t;

static image_viewer_t img_viewer = {0, 200, 100, 600, 450, 0, 0, 0, 0, 0, 0, 0, 0, "", -1};

/* Sample images (for demo) */
static const char *sample_images[] = {
  "landscape.jpg", "nature.jpg", "city.jpg", "wallpaper.jpg", NULL
};

static void draw_toolbar_icon(int x, int y, int icon_idx, int hover) {
  /* Button background */
  int btn_size = UI_SCALE_VAL(36);
  uint32_t btn_color = hover ? 0x4B5563 : 0x374151;
  gui_draw_rounded_rect(x, y, btn_size, btn_size, UI_SCALE_VAL(4), btn_color);
  
  /* Draw icon (24x24 scaled and centered in button) */
  if (icon_idx >= 0 && icon_idx < 8) {
    const uint32_t *icon = toolbar_icons[icon_idx];
    int icon_draw_size = UI_SCALE_VAL(TOOLBAR_ICON_SIZE);
    int ix = x + (btn_size - icon_draw_size) / 2;
    int iy = y + (btn_size - icon_draw_size) / 2;
    
    /* Scale the icon */
    for (int py = 0; py < icon_draw_size; py++) {
      int src_y = py * TOOLBAR_ICON_SIZE / icon_draw_size;
      for (int px = 0; px < icon_draw_size; px++) {
        int src_x = px * TOOLBAR_ICON_SIZE / icon_draw_size;
        uint32_t pixel = icon[src_y * TOOLBAR_ICON_SIZE + src_x];
        if (pixel != 0) {
          fb_put_pixel(ix + px, iy + py, 0xE5E7EB);
        }
      }
    }
  }
}

static void draw_image_viewer(void) {
  if (!img_viewer.visible) return;
  
  int x = img_viewer.x;
  int y = img_viewer.y;
  int w = img_viewer.width;
  int h = img_viewer.height;
  int title_h = UI_SCALE_VAL(32);
  int font_h = FONT_HEIGHT * ui_scale;
  
  /* Window shadow */
  fb_fill_rect(x + UI_SCALE_VAL(6), y + UI_SCALE_VAL(6), w, h, 0x11111B);
  
  /* Window background - dark for image viewing */
  fb_fill_rect(x, y, w, h, 0x0D0D0D);
  
  /* Title bar */
  fb_fill_rect(x, y, w, title_h, COLOR_TITLE_BAR);
  
  /* Traffic lights with icons */
  draw_traffic_lights(x, y, title_h);
  
  /* Title */
  char title[64] = "Image Viewer";
  if (sample_images[img_viewer.current_image]) {
    snprintf(title, 64, "Image Viewer - %s", sample_images[img_viewer.current_image]);
  }
  gui_draw_string(x + UI_SCALE_VAL(80), y + (title_h - font_h) / 2, title, THEME_TEXT);
  
  /* Image content area */
  int toolbar_h = UI_SCALE_VAL(48);
  int content_y = y + title_h;
  int content_h = h - title_h - toolbar_h - UI_SCALE_VAL(16);
  
  /* Draw placeholder image representation */
  int img_area_x = x + UI_SCALE_VAL(20);
  int img_area_y = content_y + UI_SCALE_VAL(20);
  int img_area_w = w - UI_SCALE_VAL(40);
  int img_area_h = content_h - UI_SCALE_VAL(40);
  
  /* Image placeholder - draw a nice gradient/pattern */
  for (int py = 0; py < img_area_h; py++) {
    for (int px = 0; px < img_area_w; px++) {
      int real_x = img_area_x + px;
      int real_y = img_area_y + py;
      
      /* Create a nice gradient pattern to represent image */
      int r = 30 + (py * 50 / img_area_h);
      int g = 40 + (px * 30 / img_area_w);
      int b = 60 + ((px + py) * 20 / (img_area_w + img_area_h));
      
      fb_put_pixel(real_x, real_y, (r << 16) | (g << 8) | b);
    }
  }
  
  /* Draw image info */
  char info[64];
  snprintf(info, 64, "%d x %d  |  %d%%  |  %d deg", 
           800, 600, img_viewer.zoom ? img_viewer.zoom : 100, img_viewer.rotation);
  int info_w = font_string_width(info);
  int info_h = UI_SCALE_VAL(20);
  fb_fill_rect(x + (w - info_w - UI_SCALE_VAL(16)) / 2, content_y + content_h - UI_SCALE_VAL(30), info_w + UI_SCALE_VAL(16), info_h, 0x1F2937);
  gui_draw_string(x + (w - info_w) / 2, content_y + content_h - UI_SCALE_VAL(30) + (info_h - font_h) / 2, info, 0x9CA3AF);
  
  /* Floating toolbar at bottom */
  int toolbar_w = UI_SCALE_VAL(520);
  int toolbar_x = x + (w - toolbar_w) / 2;
  int toolbar_y = y + h - toolbar_h - UI_SCALE_VAL(8);
  
  /* Toolbar background with glassmorphism effect */
  fb_fill_rect(toolbar_x, toolbar_y, toolbar_w, toolbar_h, 0x1F1F28);
  fb_fill_rect(toolbar_x, toolbar_y, toolbar_w, 1, 0x3F3F48);
  gui_draw_rounded_rect(toolbar_x, toolbar_y, toolbar_w, toolbar_h, UI_SCALE_VAL(8), 0x252530);
  
  /* Toolbar buttons */
  int btn_size = UI_SCALE_VAL(36);
  int btn_spacing = UI_SCALE_VAL(8);
  int btn_x = toolbar_x + UI_SCALE_VAL(12);
  int btn_y = toolbar_y + UI_SCALE_VAL(6);
  
  /* Previous */
  draw_toolbar_icon(btn_x, btn_y, TOOLBAR_ICON_PREV, img_viewer.hover_button == 0);
  btn_x += btn_size + btn_spacing;
  
  /* Next */
  draw_toolbar_icon(btn_x, btn_y, TOOLBAR_ICON_NEXT, img_viewer.hover_button == 1);
  btn_x += btn_size + btn_spacing + UI_SCALE_VAL(16); /* Extra gap */
  
  /* Rotate CW */
  draw_toolbar_icon(btn_x, btn_y, TOOLBAR_ICON_ROTATE_CW, img_viewer.hover_button == 2);
  btn_x += btn_size + btn_spacing;
  
  /* Rotate CCW */
  draw_toolbar_icon(btn_x, btn_y, TOOLBAR_ICON_ROTATE_CCW, img_viewer.hover_button == 3);
  btn_x += btn_size + btn_spacing + UI_SCALE_VAL(16);
  
  /* Zoom In */
  draw_toolbar_icon(btn_x, btn_y, TOOLBAR_ICON_ZOOM_IN, img_viewer.hover_button == 4);
  btn_x += btn_size + btn_spacing;
  
  /* Zoom Out */
  draw_toolbar_icon(btn_x, btn_y, TOOLBAR_ICON_ZOOM_OUT, img_viewer.hover_button == 5);
  btn_x += btn_size + btn_spacing + UI_SCALE_VAL(16);
  
  /* Fit */
  draw_toolbar_icon(btn_x, btn_y, TOOLBAR_ICON_FIT, img_viewer.hover_button == 6);
  btn_x += btn_size + btn_spacing;
  
  /* Fullscreen */
  draw_toolbar_icon(btn_x, btn_y, TOOLBAR_ICON_FULLSCREEN, img_viewer.hover_button == 7);
}

static void image_viewer_next(void) {
  img_viewer.current_image++;
  if (sample_images[img_viewer.current_image] == NULL) {
    img_viewer.current_image = 0;
  }
}

static void image_viewer_prev(void) {
  if (img_viewer.current_image > 0) {
    img_viewer.current_image--;
  } else {
    /* Find last image */
    int i = 0;
    while (sample_images[i] != NULL) i++;
    img_viewer.current_image = i > 0 ? i - 1 : 0;
  }
}

static void image_viewer_rotate_cw(void) {
  img_viewer.rotation = (img_viewer.rotation + 90) % 360;
}

static void image_viewer_rotate_ccw(void) {
  img_viewer.rotation = (img_viewer.rotation + 270) % 360;
}

static void image_viewer_zoom_in(void) {
  if (img_viewer.zoom == 0) img_viewer.zoom = 100;
  img_viewer.zoom += 25;
  if (img_viewer.zoom > 400) img_viewer.zoom = 400;
}

static void image_viewer_zoom_out(void) {
  if (img_viewer.zoom == 0) img_viewer.zoom = 100;
  img_viewer.zoom -= 25;
  if (img_viewer.zoom < 10) img_viewer.zoom = 10;
}

static void image_viewer_fit(void) {
  img_viewer.zoom = 0;
  img_viewer.offset_x = 0;
  img_viewer.offset_y = 0;
}

/* ===================================================================== */
/* Calculator                                                            */
/* ===================================================================== */

typedef struct {
  int visible;
  int x, y, width, height;
  long display;
  long pending;
  char op;
  int clear_next;
} calculator_t;

static calculator_t calc = {0, 650, 100, 200, 300, 0, 0, 0, 0};

static void calc_button_click(char key) {
  if (key >= '0' && key <= '9') {
    int digit = key - '0';
    if (calc.clear_next) {
      calc.display = digit;
      calc.clear_next = 0;
    } else {
      calc.display = calc.display * 10 + digit;
    }
  } else if (key == 'C') {
    calc.display = 0;
    calc.pending = 0;
    calc.op = 0;
    calc.clear_next = 0;
  } else if (key == '=') {
    if (calc.op == '+')
      calc.display = calc.pending + calc.display;
    else if (calc.op == '-')
      calc.display = calc.pending - calc.display;
    else if (calc.op == '*')
      calc.display = calc.pending * calc.display;
    else if (calc.op == '/' && calc.display != 0)
      calc.display = calc.pending / calc.display;
    calc.op = 0;
    calc.clear_next = 1;
  } else if (key == '+' || key == '-' || key == '*' || key == '/') {
    calc.pending = calc.display;
    calc.op = key;
    calc.clear_next = 1;
  }
}

static void draw_calculator(void) {
  if (!calc.visible)
    return;

  int x = calc.x;
  int y = calc.y;
  int w = calc.width;
  int h = calc.height;
  int title_h = UI_SCALE_VAL(32);

  /* Window shadow */
  fb_fill_rect(x + UI_SCALE_VAL(6), y + UI_SCALE_VAL(6), w, h, 0x11111B);

  /* Window background */
  fb_fill_rect(x, y, w, h, THEME_BASE);

  /* Title bar */
  fb_fill_rect(x, y, w, title_h, COLOR_TITLE_BAR);
  draw_traffic_lights(x, y, title_h);
  gui_draw_string(x + w / 2 - font_string_width("Calculator") / 2, y + (title_h - FONT_HEIGHT * ui_scale) / 2, "Calculator", THEME_TEXT);

  /* Display */
  int disp_h = UI_SCALE_VAL(40);
  fb_fill_rect(x + UI_SCALE_VAL(10), y + title_h + UI_SCALE_VAL(10), w - UI_SCALE_VAL(20), disp_h, 0x2A2A30);
  char display_buf[20];
  itoa(calc.display, display_buf, 10);
  int disp_text_w = font_string_width(display_buf);
  gui_draw_string(x + w - UI_SCALE_VAL(20) - disp_text_w, y + title_h + UI_SCALE_VAL(10) + (disp_h - FONT_HEIGHT * ui_scale) / 2, display_buf, THEME_TEXT);

  /* Buttons (4x5 grid) */
  static const char buttons[] = "C+-/789*456-123+0.=";
  int btn_w = UI_SCALE_VAL(40), btn_h = UI_SCALE_VAL(35);
  int btn_pad = UI_SCALE_VAL(8);
  int start_x = x + UI_SCALE_VAL(15);
  int start_y = y + title_h + disp_h + UI_SCALE_VAL(25);

  for (int row = 0; row < 5; row++) {
    for (int col = 0; col < 4; col++) {
      int idx = row * 4 + col;
      if (idx >= 19)
        break;

      int bx = start_x + col * (btn_w + btn_pad);
      int by = start_y + row * (btn_h + btn_pad);

      /* Button background */
      uint32_t btn_color = THEME_SURFACE;
      char c = buttons[idx];
      if (c >= '0' && c <= '9')
        btn_color = THEME_OVERLAY;
      else if (c == '=')
        btn_color = THEME_BLUE;
      else if (c == 'C')
        btn_color = THEME_RED;

      gui_draw_rounded_rect(bx, by, btn_w, btn_h, UI_SCALE_VAL(4), btn_color);

      /* Button label */
      gui_draw_char(bx + (btn_w - FONT_WIDTH * ui_scale) / 2, by + (btn_h - FONT_HEIGHT * ui_scale) / 2, c, THEME_TEXT);
    }
  }
}

/* ===================================================================== */
/* Snake Game                                                            */
/* ===================================================================== */

#define SNAKE_MAX_LEN 100
#define SNAKE_GRID_W 20
#define SNAKE_GRID_H 15

typedef struct {
  int visible;
  int x, y, width, height;
  int snake_x[SNAKE_MAX_LEN];
  int snake_y[SNAKE_MAX_LEN];
  int len;
  int dir;
  int food_x, food_y;
  int score;
  int game_over;
  int tick;
} snake_game_t;

static snake_game_t snake = {0};

static void snake_init(void) {
  /* Only set position/size if not already set by gui_init */
  if (snake.width == 0) {
    snake.x = UI_SCALE_VAL(100);
    snake.y = UI_SCALE_VAL(100);
    snake.width = UI_SCALE_VAL(360);
    snake.height = UI_SCALE_VAL(320);
  }
  /* Reset game state */
  snake.len = 4;
  snake.dir = 1;
  snake.score = 0;
  snake.game_over = 0;
  snake.tick = 0;

  for (int i = 0; i < snake.len; i++) {
    snake.snake_x[i] = 10 - i;
    snake.snake_y[i] = 7;
  }
  snake.food_x = 15;
  snake.food_y = 7;
}

static void snake_move(void) {
  if (snake.game_over || !snake.visible)
    return;

  int new_x = snake.snake_x[0];
  int new_y = snake.snake_y[0];

  switch (snake.dir) {
  case 0:
    new_y--;
    break;
  case 1:
    new_x++;
    break;
  case 2:
    new_y++;
    break;
  case 3:
    new_x--;
    break;
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
  for (int i = 0; i < snake.len; i++) {
    if (snake.snake_x[i] == new_x && snake.snake_y[i] == new_y) {
      snake.game_over = 1;
      return;
    }
  }

  /* Check food */
  if (new_x == snake.food_x && new_y == snake.food_y) {
    snake.score += 10;
    if (snake.len < SNAKE_MAX_LEN - 1)
      snake.len++;
    snake.food_x = (snake.food_x * 7 + 3) % SNAKE_GRID_W;
    snake.food_y = (snake.food_y * 5 + 7) % SNAKE_GRID_H;
  }

  /* Move body */
  for (int i = snake.len - 1; i > 0; i--) {
    snake.snake_x[i] = snake.snake_x[i - 1];
    snake.snake_y[i] = snake.snake_y[i - 1];
  }
  snake.snake_x[0] = new_x;
  snake.snake_y[0] = new_y;
}

static void draw_snake_game(void) {
  if (!snake.visible)
    return;

  int x = snake.x;
  int y = snake.y;
  int w = snake.width;
  int h = snake.height;
  int title_h = UI_SCALE_VAL(32);

  /* Update game every few frames */
  snake.tick++;
  if (snake.tick >= 8) {
    snake.tick = 0;
    snake_move();
  }

  /* Window shadow */
  fb_fill_rect(x + UI_SCALE_VAL(6), y + UI_SCALE_VAL(6), w, h, 0x11111B);

  /* Window background */
  fb_fill_rect(x, y, w, h, THEME_BASE);

  /* Title bar */
  fb_fill_rect(x, y, w, title_h, COLOR_TITLE_BAR);
  draw_traffic_lights(x, y, title_h);
  gui_draw_string(x + w / 2 - font_string_width("Snake") / 2, y + (title_h - FONT_HEIGHT * ui_scale) / 2, "Snake", THEME_TEXT);

  /* Score */
  char score_buf[20] = "Score: ";
  itoa(snake.score, score_buf + 7, 10);
  gui_draw_string(x + UI_SCALE_VAL(10), y + title_h + UI_SCALE_VAL(8), score_buf, THEME_YELLOW);

  /* Game area */
  int game_x = x + UI_SCALE_VAL(10);
  int game_y = y + title_h + UI_SCALE_VAL(28);
  int game_area_w = w - UI_SCALE_VAL(20);
  int game_area_h = h - title_h - UI_SCALE_VAL(48);
  int cell_w = game_area_w / SNAKE_GRID_W;
  int cell_h = game_area_h / SNAKE_GRID_H;

  fb_fill_rect(game_x, game_y, SNAKE_GRID_W * cell_w, SNAKE_GRID_H * cell_h,
               0x181820);

  /* Draw food */
  fb_fill_rect(game_x + snake.food_x * cell_w + 2,
               game_y + snake.food_y * cell_h + 2, cell_w - 4, cell_h - 4,
               THEME_RED);

  /* Draw snake */
  for (int i = 0; i < snake.len; i++) {
    uint32_t color = (i == 0) ? THEME_TEAL : THEME_GREEN;
    fb_fill_rect(game_x + snake.snake_x[i] * cell_w + 1,
                 game_y + snake.snake_y[i] * cell_h + 1, cell_w - 2, cell_h - 2,
                 color);
  }

  /* Game over */
  if (snake.game_over) {
    gui_draw_string(x + w / 2 - font_string_width("GAME OVER!") / 2, y + h / 2, "GAME OVER!", THEME_RED);
    gui_draw_string(x + w / 2 - font_string_width("Press R to restart") / 2, y + h / 2 + UI_SCALE_VAL(20), "Press R to restart",
                    THEME_SUBTEXT);
  }
}

/* ===================================================================== */
/* Notepad                                                               */
/* ===================================================================== */

#define NOTEPAD_MAX_TEXT 2048

typedef struct {
  int visible;
  int x, y, width, height;
  char text[NOTEPAD_MAX_TEXT];
  int cursor;
} notepad_t;

static notepad_t notepad = {0, 100, 80, 400, 350, "", 0};

static void notepad_key(int key) {
  if (!notepad.visible)
    return;

  if (key == '\b' || key == 127) {
    /* Backspace */
    if (notepad.cursor > 0) {
      notepad.cursor--;
      notepad.text[notepad.cursor] = '\0';
    }
  } else if (key == '\n' || key == '\r') {
    /* Newline */
    if (notepad.cursor < NOTEPAD_MAX_TEXT - 1) {
      notepad.text[notepad.cursor++] = '\n';
      notepad.text[notepad.cursor] = '\0';
    }
  } else if (key >= 32 && key < 127 && notepad.cursor < NOTEPAD_MAX_TEXT - 1) {
    /* Printable character */
    notepad.text[notepad.cursor++] = (char)key;
    notepad.text[notepad.cursor] = '\0';
  }
}

static void draw_notepad(void) {
  if (!notepad.visible)
    return;

  int x = notepad.x;
  int y = notepad.y;
  int w = notepad.width;
  int h = notepad.height;
  int title_h = UI_SCALE_VAL(32);
  int font_w = FONT_WIDTH * ui_scale;
  int font_h = FONT_HEIGHT * ui_scale;

  /* Window shadow */
  fb_fill_rect(x + UI_SCALE_VAL(6), y + UI_SCALE_VAL(6), w, h, 0x11111B);

  /* Window background */
  fb_fill_rect(x, y, w, h, 0xFFFBE6); /* Notepad yellow background */

  /* Title bar */
  fb_fill_rect(x, y, w, title_h, COLOR_TITLE_BAR);
  draw_traffic_lights(x, y, title_h);
  gui_draw_string(x + w / 2 - font_string_width("Notepad") / 2, y + (title_h - font_h) / 2, "Notepad", THEME_TEXT);

  /* Text area */
  int text_x = x + UI_SCALE_VAL(10);
  int text_y = y + title_h + UI_SCALE_VAL(10);
  int max_cols = (w - UI_SCALE_VAL(20)) / font_w;
  int line = 0;
  int col = 0;
  
  for (int i = 0; i < notepad.cursor && notepad.text[i]; i++) {
    if (notepad.text[i] == '\n') {
      line++;
      col = 0;
    } else {
      gui_draw_char(text_x + col * font_w, text_y + line * font_h, notepad.text[i], 0x333333);
      col++;
      if (col >= max_cols) {
        col = 0;
        line++;
      }
    }
    if (text_y + line * font_h > y + h - UI_SCALE_VAL(20))
      break;
  }

  /* Cursor */
  static int blink = 0;
  blink++;
  if ((blink / 15) % 2 == 0) {
    fb_fill_rect(text_x + col * font_w, text_y + line * font_h, UI_SCALE_VAL(2), font_h, 0x333333);
  }
}

/* ===================================================================== */
/* Analog Clock                                                          */
/* ===================================================================== */

typedef struct {
  int visible;
  int x, y, size;
  int hours, minutes, seconds;
} clock_widget_t;

static clock_widget_t analog_clock = {0, 700, 100, 180, 10, 10, 30};

/* Sine/cosine lookup for clock hands (fixed point, scale 256) */
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

static void draw_clock_widget(void) {
  if (!analog_clock.visible)
    return;

  int title_h = UI_SCALE_VAL(32);
  int cx = analog_clock.x + analog_clock.size / 2;
  int cy = analog_clock.y + analog_clock.size / 2 + title_h / 2;
  int r = analog_clock.size / 2 - UI_SCALE_VAL(10);

  /* Window shadow */
  fb_fill_rect(analog_clock.x + UI_SCALE_VAL(6), analog_clock.y + UI_SCALE_VAL(6), analog_clock.size, analog_clock.size + title_h, 0x11111B);

  /* Window background */
  fb_fill_rect(analog_clock.x, analog_clock.y, analog_clock.size, analog_clock.size + title_h, THEME_BASE);

  /* Title bar */
  fb_fill_rect(analog_clock.x, analog_clock.y, analog_clock.size, title_h, COLOR_TITLE_BAR);
  draw_traffic_lights(analog_clock.x, analog_clock.y, title_h);
  gui_draw_string(analog_clock.x + analog_clock.size / 2 - font_string_width("Clock") / 2, analog_clock.y + (title_h - FONT_HEIGHT * ui_scale) / 2, "Clock", THEME_TEXT);

  /* Clock face - white circle */
  gui_draw_circle(cx, cy, r, 0xFFFFFF);
  gui_draw_circle(cx, cy, r - 2, 0xF0F0F0);

  /* Hour markers */
  for (int i = 0; i < 12; i++) {
    int idx = i * 5;
    int marker_inner = r - UI_SCALE_VAL(8);
    int marker_outer = r - UI_SCALE_VAL(2);
    int x1 = cx + marker_inner * clock_sin[idx] / 256;
    int y1 = cy + marker_inner * clock_cos[idx] / 256;
    int x2 = cx + marker_outer * clock_sin[idx] / 256;
    int y2 = cy + marker_outer * clock_cos[idx] / 256;
    gui_draw_line(x1, y1, x2, y2, 0x333333);
  }

  /* Update time (simple incrementing for demo) */
  static int tick = 0;
  tick++;
  if (tick >= 30) {
    tick = 0;
    analog_clock.seconds++;
    if (analog_clock.seconds >= 60) {
      analog_clock.seconds = 0;
      analog_clock.minutes++;
      if (analog_clock.minutes >= 60) {
        analog_clock.minutes = 0;
        analog_clock.hours++;
        if (analog_clock.hours >= 12)
          analog_clock.hours = 0;
      }
    }
  }

  int h = analog_clock.hours;
  int m = analog_clock.minutes;
  int s = analog_clock.seconds;

  /* Hour hand */
  int h_idx = (h * 5 + m / 12) % 60;
  int hx = cx + (r * 50 / 100) * clock_sin[h_idx] / 256;
  int hy = cy + (r * 50 / 100) * clock_cos[h_idx] / 256;
  for (int t = -2; t <= 2; t++) {
    gui_draw_line(cx + t, cy, hx + t, hy, 0x333333);
  }

  /* Minute hand */
  int mx = cx + (r * 70 / 100) * clock_sin[m] / 256;
  int my = cy + (r * 70 / 100) * clock_cos[m] / 256;
  for (int t = -1; t <= 1; t++) {
    gui_draw_line(cx + t, cy, mx + t, my, 0x555555);
  }

  /* Second hand */
  int sx = cx + (r * 80 / 100) * clock_sin[s] / 256;
  int sy = cy + (r * 80 / 100) * clock_cos[s] / 256;
  gui_draw_line(cx, cy, sx, sy, 0xFF0000);

  /* Center dot */
  gui_draw_circle(cx, cy, UI_SCALE_VAL(4), 0xFF0000);
}

/* ===================================================================== */
/* Help Window                                                           */
/* ===================================================================== */

typedef struct {
  int visible;
  int x, y, width, height;
} help_window_t;

static help_window_t help_win = {0, 200, 120, 450, 380};

static void draw_help_window(void) {
  if (!help_win.visible)
    return;

  int x = help_win.x;
  int y = help_win.y;
  int w = help_win.width;
  int h = help_win.height;
  int title_h = UI_SCALE_VAL(32);
  int font_h = FONT_HEIGHT * ui_scale;
  int line_h = UI_SCALE_VAL(18);
  int section_gap = UI_SCALE_VAL(32);
  int line_gap = UI_SCALE_VAL(24);

  /* Window shadow */
  fb_fill_rect(x + UI_SCALE_VAL(6), y + UI_SCALE_VAL(6), w, h, 0x11111B);

  /* Window background */
  fb_fill_rect(x, y, w, h, THEME_BASE);

  /* Title bar */
  fb_fill_rect(x, y, w, title_h, COLOR_TITLE_BAR);
  draw_traffic_lights(x, y, title_h);
  gui_draw_string(x + w / 2 - font_string_width("Help & About") / 2, y + (title_h - font_h) / 2, "Help & About", THEME_TEXT);

  /* Content */
  int ty = y + title_h + UI_SCALE_VAL(18);
  gui_draw_string(x + UI_SCALE_VAL(20), ty, "Vib-os Desktop", THEME_BLUE);
  ty += line_gap;
  gui_draw_string(x + UI_SCALE_VAL(20), ty, "Version 1.0", THEME_SUBTEXT);
  ty += section_gap;
  
  gui_draw_string(x + UI_SCALE_VAL(20), ty, "Keyboard Shortcuts:", THEME_TEXT);
  ty += line_gap;
  gui_draw_string(x + UI_SCALE_VAL(30), ty, "F1-F8    Change wallpaper", THEME_SUBTEXT);
  ty += line_h;
  gui_draw_string(x + UI_SCALE_VAL(30), ty, "F9       Calculator", THEME_SUBTEXT);
  ty += line_h;
  gui_draw_string(x + UI_SCALE_VAL(30), ty, "F10      Snake game", THEME_SUBTEXT);
  ty += line_h;
  gui_draw_string(x + UI_SCALE_VAL(30), ty, "F11      Notepad", THEME_SUBTEXT);
  ty += line_h;
  gui_draw_string(x + UI_SCALE_VAL(30), ty, "F12      Clock", THEME_SUBTEXT);
  ty += line_h;
  gui_draw_string(x + UI_SCALE_VAL(30), ty, "ESC      Close windows", THEME_SUBTEXT);
  ty += section_gap;
  
  gui_draw_string(x + UI_SCALE_VAL(20), ty, "Snake Controls:", THEME_TEXT);
  ty += line_gap;
  gui_draw_string(x + UI_SCALE_VAL(30), ty, "WASD / Arrow keys to move", THEME_SUBTEXT);
  ty += line_h;
  gui_draw_string(x + UI_SCALE_VAL(30), ty, "R to restart after game over", THEME_SUBTEXT);
  ty += section_gap;
  
  gui_draw_string(x + UI_SCALE_VAL(20), ty, "Click dock icons to open apps", THEME_TEXT);
}

/* ===================================================================== */
/* Desktop Background                                                    */
/* ===================================================================== */

static void draw_wallpaper(void) {
  /* Ensure wallpaper image is loaded if needed */
  if (wallpapers[current_wallpaper].type == 1) {
    wallpaper_ensure_loaded();
  }

  if (wallpaper_cache_dirty || !wallpaper_cache) {
    wallpaper_build_cache();
  }

  if (!wallpaper_cache)
    return;

  /* Copy cached wallpaper into backbuffer */
  for (uint32_t y = 0; y < screen_height; y++) {
    uint64_t *src = (uint64_t *)(wallpaper_cache + y * screen_width);
    uint64_t *dst = (uint64_t *)(backbuffer + y * screen_width);
    size_t count64 = screen_width / 2;
    size_t i = 0;
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
    if (screen_width & 1) {
      uint32_t *dst32 = (uint32_t *)dst;
      uint32_t *src32 = (uint32_t *)src;
      dst32[screen_width - 1] = src32[screen_width - 1];
    }
  }
}

/* ===================================================================== */
/* Desktop Icons - desktop_icon_t defined in gui.h                       */
/* ===================================================================== */

static desktop_icon_t desktop_icons[] = {
    {"Documents", 40, 60, 1},
    {"Pictures", 40, 160, 1},
    {"Downloads", 40, 260, 1},
    {"Terminal", 40, 360, 0},
};
#define NUM_DESKTOP_ICONS 4

static void draw_desktop_icons(void) {
  for (int i = 0; i < NUM_DESKTOP_ICONS; i++) {
    int x = desktop_icons[i].x * ui_scale;
    int y = desktop_icons[i].y * ui_scale;

    /* Icon */
    if (desktop_icons[i].is_dir) {
      fb_fill_rect(x + UI_SCALE_VAL(12), y + UI_SCALE_VAL(10),
                   UI_SCALE_VAL(40), UI_SCALE_VAL(32), THEME_YELLOW);
      fb_fill_rect(x + UI_SCALE_VAL(12), y + UI_SCALE_VAL(6),
                   UI_SCALE_VAL(20), UI_SCALE_VAL(6), THEME_PEACH);
    } else {
      fb_fill_rect(x + UI_SCALE_VAL(16), y + UI_SCALE_VAL(4),
                   UI_SCALE_VAL(32), UI_SCALE_VAL(40), THEME_SURFACE);
      fb_draw_rect(x + UI_SCALE_VAL(16), y + UI_SCALE_VAL(4),
                   UI_SCALE_VAL(32), UI_SCALE_VAL(40), THEME_OVERLAY);
      if (strcmp(desktop_icons[i].name, "Terminal") == 0) {
        gui_draw_char(x + UI_SCALE_VAL(26), y + UI_SCALE_VAL(18), '>',
                      THEME_GREEN);
      } else if (strcmp(desktop_icons[i].name, "Settings") == 0) {
        gui_draw_circle(x + UI_SCALE_VAL(32), y + UI_SCALE_VAL(24),
                        UI_SCALE_VAL(10), THEME_LAVENDER);
        gui_draw_circle(x + UI_SCALE_VAL(32), y + UI_SCALE_VAL(24),
                        UI_SCALE_VAL(4), THEME_BASE);
      }
    }

    /* Label */
    int label_w = strlen(desktop_icons[i].name) * FONT_WIDTH * ui_scale;
    gui_draw_string(x + UI_SCALE_VAL(32) - label_w / 2,
                    y + UI_SCALE_VAL(52), desktop_icons[i].name,
                    THEME_TEXT);
  }
}

/* ===================================================================== */
/* Menu Bar - macOS Big Sur style (matching test folder)                 */
/* ===================================================================== */

static int menu_open = 0;

static void draw_menu_bar(void) {
  /* Glossy menu bar - gradient from dark to slightly lighter (like test folder) */
  for (int y = 0; y < MENU_BAR_HEIGHT; y++) {
    int brightness = 45 + (y * 10) / MENU_BAR_HEIGHT; /* 45 to 55 */
    uint32_t color = (brightness << 16) | (brightness << 8) | (brightness + 5);
    for (int x = 0; x < (int)screen_width; x++) {
      fb_put_pixel(x, y, color);
    }
  }
  
  /* Bottom highlight line */
  for (int x = 0; x < (int)screen_width; x++) {
    fb_put_pixel(x, MENU_BAR_HEIGHT - 1, 0x606060);
  }

  /* Apple logo (using @ as placeholder, bold white) */
  int font_h = FONT_HEIGHT * ui_scale;
  int text_y = (MENU_BAR_HEIGHT - font_h) / 2;
  gui_draw_string(UI_SCALE_VAL(14), text_y, "@", 0xFFFFFF);
  
  /* App name - bold */
  gui_draw_string(UI_SCALE_VAL(36), text_y, "Vib-os", 0xFFFFFF);

  /* Right side - Clock */
  int clock_w = font_string_width("12:00");
  gui_draw_string((int)screen_width - clock_w - UI_SCALE_VAL(16), text_y,
                  "12:00", 0xFFFFFF);
  
  /* WiFi icon area */
  int wx = (int)screen_width - UI_SCALE_VAL(86);
  int wy = text_y + UI_SCALE_VAL(6);
  fb_fill_rect(wx, wy + UI_SCALE_VAL(6), UI_SCALE_VAL(2), UI_SCALE_VAL(2),
               0xFFFFFF);
  fb_fill_rect(wx - UI_SCALE_VAL(2), wy + UI_SCALE_VAL(3), UI_SCALE_VAL(6),
               UI_SCALE_VAL(2), 0xFFFFFF);
  fb_fill_rect(wx - UI_SCALE_VAL(4), wy, UI_SCALE_VAL(10), UI_SCALE_VAL(2),
               0xFFFFFF);

  /* Draw dropdown if open */
  if (menu_open == 1) {
    int dd_x = UI_SCALE_VAL(8);
    int dd_y = MENU_BAR_HEIGHT;
    int dd_w = UI_SCALE_VAL(160);
    int dd_h = UI_SCALE_VAL(80);
    
    /* Shadow */
    fb_fill_rect(dd_x + UI_SCALE_VAL(3), dd_y + UI_SCALE_VAL(3), dd_w, dd_h,
                 0x151520);
    
    /* Background */
    fb_fill_rect(dd_x, dd_y, dd_w, dd_h, 0x404050);
    fb_draw_rect(dd_x, dd_y, dd_w, dd_h, 0x606070);
    
    /* Menu items */
    gui_draw_string(dd_x + UI_SCALE_VAL(12), dd_y + UI_SCALE_VAL(10),
                    "About Vib-os", 0xFFFFFF);
    fb_fill_rect(dd_x + UI_SCALE_VAL(8), dd_y + UI_SCALE_VAL(30),
                 dd_w - UI_SCALE_VAL(16), UI_SCALE_VAL(1), 0x606070);
    gui_draw_string(dd_x + UI_SCALE_VAL(12), dd_y + UI_SCALE_VAL(38),
                    "Settings...", 0xFFFFFF);
    fb_fill_rect(dd_x + UI_SCALE_VAL(8), dd_y + UI_SCALE_VAL(56),
                 dd_w - UI_SCALE_VAL(16), UI_SCALE_VAL(1), 0x606070);
    gui_draw_string(dd_x + UI_SCALE_VAL(12), dd_y + UI_SCALE_VAL(62),
                    "Restart", 0xFFFFFF);
  }
}

/* ===================================================================== */
/* Mouse Cursor                                                          */
/* ===================================================================== */

static const uint8_t cursor_bitmap[16][16] = {
    {1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
    {1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
    {1, 2, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
    {1, 2, 2, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
    {1, 2, 2, 2, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
    {1, 2, 2, 2, 2, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
    {1, 2, 2, 2, 2, 2, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0},
    {1, 2, 2, 2, 2, 2, 2, 1, 0, 0, 0, 0, 0, 0, 0, 0},
    {1, 2, 2, 2, 2, 2, 2, 2, 1, 0, 0, 0, 0, 0, 0, 0},
    {1, 2, 2, 2, 2, 2, 2, 2, 2, 1, 0, 0, 0, 0, 0, 0},
    {1, 2, 2, 2, 2, 2, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0},
    {1, 2, 2, 2, 1, 2, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0},
    {1, 2, 2, 1, 0, 1, 2, 1, 0, 0, 0, 0, 0, 0, 0, 0},
    {1, 2, 1, 0, 0, 1, 2, 1, 0, 0, 0, 0, 0, 0, 0, 0},
    {1, 1, 0, 0, 0, 0, 1, 2, 1, 0, 0, 0, 0, 0, 0, 0},
    {1, 0, 0, 0, 0, 0, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0},
};

static void draw_cursor(int x, int y) {
  int scale = ui_scale > 0 ? ui_scale : 1;
  for (int py = 0; py < 16; py++) {
    for (int px = 0; px < 16; px++) {
      uint8_t p = cursor_bitmap[py][px];
      if (p == 1 || p == 2) {
        color_t col = (p == 1) ? 0xFF000000 : 0xFFFFFFFF;
        if (scale == 1) {
          fb_put_pixel(x + px, y + py, col);
        } else {
          fb_fill_rect(x + px * scale, y + py * scale, scale, scale, col);
        }
      }
    }
  }
}

/* ===================================================================== */
/* GUI Initialization                                                    */
/* ===================================================================== */

/* Keyboard callback for handling input */
static void keyboard_callback(int key) {
  static int first_cb = 1;
  if (first_cb) {
    /* [7] PURPLE BLOCK - callback invoked */
    for (int y = 0; y < 100; y++) {
      for (int x = 0; x < 100; x++) {
        fb_fill_rect(x, 600 + y, 1, 1, 0xFFAA00FF);
      }
    }
    fb_swap_buffers();
    first_cb = 0;
  }
  
  /* Any key press triggers redraw */
  needs_redraw = 1;
  
  /* F1-F8: Change wallpaper */
  if (key >= KEY_F1 && key <= KEY_F8) {
    wallpaper_set(key - KEY_F1);
    return;
  }

  /* F9: Toggle Calculator */
  if (key == KEY_F9) {
    calc.visible = !calc.visible;
    return;
  }

  /* F10: Toggle Snake Game */
  if (key == KEY_F10) {
    if (!snake.visible) {
      snake_init();
    }
    snake.visible = !snake.visible;
    return;
  }

  /* F11: Toggle Notepad */
  if (key == KEY_F11) {
    notepad.visible = !notepad.visible;
    return;
  }

  /* F12: Toggle Clock */
  if (key == KEY_F12) {
    analog_clock.visible = !analog_clock.visible;
    return;
  }

  /* H: Toggle Help (when no text input active) */
  if ((key == 'h' || key == 'H') && !terminal.visible && !notepad.visible && !calc.visible) {
    help_win.visible = !help_win.visible;
    return;
  }

  /* ESC: Close windows/context menu */
  if (key == KEY_ESC) {
    ctx_menu.visible = 0;
    calc.visible = 0;
    snake.visible = 0;
    notepad.visible = 0;
    analog_clock.visible = 0;
    help_win.visible = 0;
    return;
  }

  /* Snake game controls */
  if (snake.visible) {
    if (snake.game_over && (key == 'r' || key == 'R')) {
      snake_init();
      snake.visible = 1;
      return;
    }
    if (key == KEY_UP || key == 'w' || key == 'W') {
      if (snake.dir != 2)
        snake.dir = 0;
      return;
    }
    if (key == KEY_DOWN || key == 's' || key == 'S') {
      if (snake.dir != 0)
        snake.dir = 2;
      return;
    }
    if (key == KEY_LEFT || key == 'a' || key == 'A') {
      if (snake.dir != 1)
        snake.dir = 3;
      return;
    }
    if (key == KEY_RIGHT || key == 'd' || key == 'D') {
      if (snake.dir != 3)
        snake.dir = 1;
      return;
    }
  }

  /* Calculator input */
  if (calc.visible) {
    if ((key >= '0' && key <= '9') || key == '+' || key == '-' || key == '*' ||
        key == '/' || key == '=' || key == 'c' || key == 'C' || key == '\n') {
      if (key == '\n')
        key = '=';
      if (key == 'c')
        key = 'C';
      calc_button_click((char)key);
      return;
    }
  }

  /* Notepad input */
  if (notepad.visible) {
    notepad_key(key);
    return;
  }

  /* Space: Cycle wallpaper (when nothing else is focused) */
  if (key == ' ' && !terminal.visible && !snake.visible && !calc.visible && !notepad.visible) {
    wallpaper_next();
    return;
  }

  /* Terminal input handling */
  if (terminal.visible) {
    static int first_term_key = 1;
    if (first_term_key) {
      /* [8] LIME BLOCK - terminal got key */
      for (int y = 0; y < 100; y++) {
        for (int x = 0; x < 100; x++) {
          fb_fill_rect(x, 700 + y, 1, 1, 0xFF88FF00);
        }
      }
      fb_swap_buffers();
      first_term_key = 0;
    }
    
    if (key == '\n' || key == '\r') {
      /* Execute command */
      terminal.input[terminal.input_len] = '\0';
      term_execute(terminal.input);
      terminal.input_len = 0;
    } else if (key == '\b' || key == 127) {
      /* Backspace */
      if (terminal.input_len > 0) {
        terminal.input_len--;
        terminal.input[terminal.input_len] = '\0';
      }
    } else if (key >= 32 && key < 127 && terminal.input_len < TERM_MAX_INPUT - 1) {
      static int first_char = 1;
      if (first_char) {
        /* [9] BRIGHT GREEN BLOCK - char added to buffer */
        for (int y = 0; y < 100; y++) {
          for (int x = 0; x < 100; x++) {
            fb_fill_rect(x, 800 + y, 1, 1, 0xFF00FF00);
          }
        }
        fb_swap_buffers();
        first_char = 0;
      }
      
      /* Add character to input */
      terminal.input[terminal.input_len++] = (char)key;
      terminal.input[terminal.input_len] = '\0';
    }
  }
}

void gui_init(void) {
  /* Calculate UI scale from EDID if available, otherwise fall back to resolution */
  int dpi_x = 0;
  int dpi_y = 0;
  if (screen_mm_width > 0 && screen_mm_height > 0) {
    /* DPI = pixels / inches; inches = mm / 25.4 */
    dpi_x = (int)((screen_width * 254 + (screen_mm_width * 10) / 2) /
                  (screen_mm_width * 10));
    dpi_y = (int)((screen_height * 254 + (screen_mm_height * 10) / 2) /
                  (screen_mm_height * 10));
  }

  if (dpi_x > 0 && dpi_y > 0) {
    int dpi = (dpi_x + dpi_y) / 2;
    /* 96 DPI baseline. Use integer scaling for crisp fonts. */
    ui_scale = (dpi + 48) / 96;
  } else {
    /* Fallback: resolution-based heuristic */
    if (screen_width >= 2560 || screen_height >= 1440) {
      ui_scale = 2;
    } else {
      ui_scale = 1;
    }
  }
  if (ui_scale < 1)
    ui_scale = 1;
  if (ui_scale > 3)
    ui_scale = 3;

  /* Scale UI sizes */
  int base_dock_icon = 48;
  int base_dock_height = 70;
  int base_menu_height = 28;
  int base_dock_padding = 10;

  dock_icon_size = base_dock_icon * ui_scale;
  dock_height = base_dock_height * ui_scale;
  menu_bar_height = base_menu_height * ui_scale;
  dock_padding = base_dock_padding * ui_scale;
  
  /* Initialize VFS first */
  vfs_init();
  vfs_seed_content();

  font_init();
  term_init();
  fm_init();

  /* Initialize PS/2 keyboard and mouse with retry */
  int ps2_ok = ps2_init();
  if (ps2_ok != 0) {
    /* Retry once more */
    ps2_init();
  }
  ps2_set_screen_bounds(screen_width, screen_height);
  /* Faster mouse on high-DPI screens */
  ps2_set_mouse_scale(ui_scale >= 2 ? (ui_scale + 1) : 2);
  ps2_set_keyboard_callback(keyboard_callback);
  usb_set_keyboard_callback(keyboard_callback);

  /* Initialize dock smooth sizes */
  for (int i = 0; i < NUM_DOCK_ICONS; i++) {
    dock_smooth_sizes[i] = dock_icon_size;
  }

  /* Load initial wallpaper (Landscape JPEG) */
  current_wallpaper = 0;
  wallpaper_ensure_loaded();

  /* Scale and position windows based on ui_scale */
  
  /* Scale terminal */
  terminal.width = UI_SCALE_VAL(650);
  terminal.height = UI_SCALE_VAL(420);
  if (terminal.width > (int)screen_width - 100) terminal.width = screen_width - 100;
  if (terminal.height > (int)screen_height - 200) terminal.height = screen_height - 200;
  terminal.content_x = (screen_width - terminal.width) / 2;
  terminal.content_y = (screen_height - terminal.height) / 2;
  
  /* Scale file manager */
  file_manager.width = 500 * ui_scale;
  file_manager.height = 400 * ui_scale;
  if (file_manager.width > (int)screen_width - 100) file_manager.width = screen_width - 100;
  if (file_manager.height > (int)screen_height - 200) file_manager.height = screen_height - 200;
  file_manager.x = UI_SCALE_VAL(80);
  file_manager.y = UI_SCALE_VAL(60);
  
  /* Scale calculator */
  calc.width = UI_SCALE_VAL(220);
  calc.height = UI_SCALE_VAL(340);
  calc.x = screen_width - calc.width - UI_SCALE_VAL(50);
  calc.y = UI_SCALE_VAL(100);
  
  /* Scale snake game */
  snake.width = UI_SCALE_VAL(360);
  snake.height = UI_SCALE_VAL(320);
  snake.x = UI_SCALE_VAL(50);
  snake.y = UI_SCALE_VAL(50);
  
  /* Scale notepad */
  notepad.width = UI_SCALE_VAL(450);
  notepad.height = UI_SCALE_VAL(380);
  notepad.x = UI_SCALE_VAL(150);
  notepad.y = UI_SCALE_VAL(100);
  
  /* Scale clock */
  analog_clock.size = UI_SCALE_VAL(200);
  analog_clock.x = UI_SCALE_VAL(100);
  analog_clock.y = UI_SCALE_VAL(50);
  
  /* Scale help window */
  help_win.width = UI_SCALE_VAL(500);
  help_win.height = UI_SCALE_VAL(420);
  help_win.x = screen_width - help_win.width - UI_SCALE_VAL(50);
  help_win.y = UI_SCALE_VAL(80);
  
  /* Scale image viewer */
  img_viewer.width = UI_SCALE_VAL(650);
  img_viewer.height = UI_SCALE_VAL(500);
  img_viewer.x = (screen_width - img_viewer.width) / 2;
  img_viewer.y = (screen_height - img_viewer.height) / 2;

  /* Initialize USB (xHCI/EHCI) for keyboard input - with visual progress */
  /* Yellow marker = starting USB init */
  fb_fill_rect(190, 10, 50, 10, 0xFFFFFF00);
  fb_swap_buffers();

  /* Orange marker = starting xHCI init */
  fb_fill_rect(250, 10, 50, 10, 0xFFFF9900);
  fb_swap_buffers();
  usb_xhci_init();
  /* Green marker = xHCI init done */
  fb_fill_rect(310, 10, 50, 10, 0xFF00FF00);
  fb_swap_buffers();

  /* Magenta marker = starting EHCI init */
  fb_fill_rect(370, 10, 50, 10, 0xFFFF00FF);
  fb_swap_buffers();
  usb_ehci_init();
  /* Cyan marker = EHCI init done */
  fb_fill_rect(430, 10, 50, 10, 0xFF00FFFF);
  fb_swap_buffers();

  /* Show terminal and file manager as demo */
  terminal.visible = 1;
  term_puts("Vib-OS Terminal v1.0\n", 14); /* Cyan */
  term_puts("Type 'help' for commands, 'neofetch' for system info.\n\n", 7);
  term_prompt();

  file_manager.visible = 1;
  fm_refresh();

  mouse_x = screen_width / 2;
  mouse_y = screen_height / 2;
}

/* ===================================================================== */
/* Main Composition Loop - with dirty region optimization                */
/* ===================================================================== */

static uint64_t frame_count = 0;
static int prev_buttons = 0;
static int last_mouse_x = 0, last_mouse_y = 0;

/* Dirty region tracking - only copy changed areas to framebuffer */
#define MAX_DIRTY_REGIONS 32
static struct { int x, y, w, h, valid; } dirty_regions[MAX_DIRTY_REGIONS];
static int dirty_count = 0;

static void mark_dirty(int x, int y, int w, int h) {
  if (dirty_count < MAX_DIRTY_REGIONS) {
    dirty_regions[dirty_count].x = x;
    dirty_regions[dirty_count].y = y;
    dirty_regions[dirty_count].w = w;
    dirty_regions[dirty_count].h = h;
    dirty_regions[dirty_count].valid = 1;
    dirty_count++;
  } else {
    full_redraw = 1;
  }
}

/* Copy only a specific region from backbuffer to framebuffer */
static void blit_region(int x, int y, int w, int h) {
  if (!backbuffer || !framebuffer) return;
  
  /* Clip to screen */
  if (x < 0) { w += x; x = 0; }
  if (y < 0) { h += y; y = 0; }
  if (x + w > (int)screen_width) w = screen_width - x;
  if (y + h > (int)screen_height) h = screen_height - y;
  if (w <= 0 || h <= 0) return;
  
  /* Copy region line by line using 64-bit operations */
  for (int row = y; row < y + h; row++) {
    uint64_t *src = (uint64_t *)(backbuffer + row * screen_width + x);
    volatile uint64_t *dst = (volatile uint64_t *)((uint8_t*)framebuffer + row * screen_pitch + x * 4);
    int count = w / 2;
    for (int i = 0; i < count; i++) {
      dst[i] = src[i];
    }
    if (w & 1) {
      volatile uint32_t *dst32 = (volatile uint32_t *)dst;
      uint32_t *src32 = (uint32_t *)src;
      dst32[w - 1] = src32[w - 1];
    }
  }
}

/* Smart buffer swap - only copy dirty regions or full screen */
static void smart_swap_buffers(void) {
  if (!backbuffer || !framebuffer) return;
  
  if (full_redraw || dirty_count == 0) {
    /* Full screen copy - pitch-aware */
    for (uint32_t row = 0; row < screen_height; row++) {
      uint64_t *src = (uint64_t *)(backbuffer + row * screen_width);
      volatile uint64_t *dst =
          (volatile uint64_t *)((uint8_t *)framebuffer + row * screen_pitch);
      size_t count64 = screen_width / 2;
      size_t i = 0;
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
      if (screen_width & 1) {
        volatile uint32_t *dst32 = (volatile uint32_t *)dst;
        uint32_t *src32 = (uint32_t *)src;
        dst32[screen_width - 1] = src32[screen_width - 1];
      }
    }
    full_redraw = 0;
  } else {
    /* Partial update - only dirty regions */
    for (int d = 0; d < dirty_count; d++) {
      if (dirty_regions[d].valid) {
        blit_region(dirty_regions[d].x, dirty_regions[d].y,
                    dirty_regions[d].w, dirty_regions[d].h);
      }
    }
  }
  
  dirty_count = 0;
  __asm__ volatile("mfence" ::: "memory");
}

/* Handle mouse clicks */
static void handle_mouse_click(int x, int y, int button) {
  /* Check menu bar */
  if (y < MENU_BAR_HEIGHT) {
    if (x < 90) {
      menu_open = menu_open ? 0 : 1;
    } else {
      menu_open = 0;
    }
    return;
  }
  
  /* Check menu dropdown */
  if (menu_open && y >= MENU_BAR_HEIGHT &&
      y < MENU_BAR_HEIGHT + UI_SCALE_VAL(100) &&
      x < UI_SCALE_VAL(188)) {
    int rel_y = y - MENU_BAR_HEIGHT;
    if (rel_y >= UI_SCALE_VAL(8) && rel_y < UI_SCALE_VAL(30)) {
      /* About clicked */
      help_win.visible = 1;
    } else if (rel_y >= UI_SCALE_VAL(38) && rel_y < UI_SCALE_VAL(58)) {
      /* Settings clicked */
      file_manager.visible = 1;
    }
    menu_open = 0;
    return;
  }
  
  /* Close menu if clicking elsewhere */
  menu_open = 0;
  
  /* Check dock clicks */
  int dock_y = screen_height - DOCK_HEIGHT + UI_SCALE_VAL(6);
  if (y >= dock_y && y < (int)screen_height) {
    int dock_w = NUM_DOCK_ICONS * (DOCK_ICON_SIZE + DOCK_PADDING) + UI_SCALE_VAL(32);
    int dock_x = (screen_width - dock_w) / 2;
    
    if (x >= dock_x && x < dock_x + dock_w) {
      int rel_x = x - dock_x - UI_SCALE_VAL(16);
      int icon_idx = rel_x / (DOCK_ICON_SIZE + DOCK_PADDING);
      
      if (icon_idx >= 0 && icon_idx < NUM_DOCK_ICONS) {
        switch (icon_idx) {
          case 0: terminal.visible = !terminal.visible; break;         /* Terminal */
          case 1: file_manager.visible = !file_manager.visible; break; /* Files */
          case 2: calc.visible = !calc.visible; break;                 /* Calculator */
          case 3: notepad.visible = !notepad.visible; break;           /* Notes */
          case 4: /* Settings - toggle file manager */ file_manager.visible = !file_manager.visible; break;
          case 5: analog_clock.visible = !analog_clock.visible; break; /* Clock */
          case 6: snake.visible = !snake.visible; if (snake.visible) snake_init(); break; /* Snake */
          case 7: help_win.visible = !help_win.visible; break;         /* Help */
          case 8: img_viewer.visible = !img_viewer.visible; bring_window_to_front(WIN_IMAGE_VIEWER); break; /* Images/Browser */
          case 9: /* DOOM - not implemented */ break;
          default: break;
        }
      }
      return;
    }
  }
  
  /* Check context menu */
  if (ctx_menu.visible) {
    if (x >= ctx_menu.x && x < ctx_menu.x + ctx_menu.width &&
        y >= ctx_menu.y && y < ctx_menu.y + ctx_menu.height) {
      /* Click on menu item */
      int item_h = UI_SCALE_VAL(24);
      int item_y = ctx_menu.y + UI_SCALE_VAL(4);
      for (int i = 0; i < ctx_menu.item_count; i++) {
        if (y >= item_y && y < item_y + item_h - UI_SCALE_VAL(2) && ctx_menu.items[i].enabled) {
          ctx_menu.visible = 0;
          /* Handle action */
          if (strcmp(ctx_menu.items[i].label, "New Folder") == 0) {
            static int folder_num = 1;
            char path[64];
            snprintf(path, 64, "/Desktop/New Folder %d", folder_num++);
            vfs_mkdir(path);
          } else if (strcmp(ctx_menu.items[i].label, "New Text Document") == 0) {
            static int file_num = 1;
            char path[64];
            snprintf(path, 64, "/Desktop/document%d.txt", file_num++);
            vfs_create(path);
          } else if (strcmp(ctx_menu.items[i].label, "Refresh") == 0) {
            fm_refresh();
          } else if (strcmp(ctx_menu.items[i].label, "Open Terminal") == 0) {
            terminal.visible = 1;
          } else if (strcmp(ctx_menu.items[i].label, "Change Background") == 0) {
            wallpaper_next();
          }
          return;
        }
        item_y += item_h;
        if (ctx_menu.items[i].separator) item_y += UI_SCALE_VAL(8);
      }
    }
    ctx_menu.visible = 0;
    return;
  }
  
  /* Right click on desktop - show full context menu like test folder */
  if (button == 2 && y > MENU_BAR_HEIGHT && y < (int)screen_height - DOCK_HEIGHT) {
    ctx_menu.visible = 1;
    ctx_menu.x = x;
    ctx_menu.y = y;
    ctx_menu.width = UI_SCALE_VAL(180);
    ctx_menu.item_count = 0;
    
    /* New items */
    strcpy(ctx_menu.items[ctx_menu.item_count].label, "New Folder");
    ctx_menu.items[ctx_menu.item_count].enabled = 1;
    ctx_menu.items[ctx_menu.item_count++].separator = 0;
    
    strcpy(ctx_menu.items[ctx_menu.item_count].label, "New Text Document");
    ctx_menu.items[ctx_menu.item_count].enabled = 1;
    ctx_menu.items[ctx_menu.item_count++].separator = 1;
    
    /* Clipboard */
    strcpy(ctx_menu.items[ctx_menu.item_count].label, "Paste");
    ctx_menu.items[ctx_menu.item_count].enabled = 0;
    ctx_menu.items[ctx_menu.item_count++].separator = 1;
    
    /* Sort options */
    strcpy(ctx_menu.items[ctx_menu.item_count].label, "Sort by Name");
    ctx_menu.items[ctx_menu.item_count].enabled = 1;
    ctx_menu.items[ctx_menu.item_count++].separator = 0;
    
    strcpy(ctx_menu.items[ctx_menu.item_count].label, "Sort by Type");
    ctx_menu.items[ctx_menu.item_count].enabled = 1;
    ctx_menu.items[ctx_menu.item_count++].separator = 1;
    
    /* Actions */
    strcpy(ctx_menu.items[ctx_menu.item_count].label, "Refresh");
    ctx_menu.items[ctx_menu.item_count].enabled = 1;
    ctx_menu.items[ctx_menu.item_count++].separator = 0;
    
    strcpy(ctx_menu.items[ctx_menu.item_count].label, "Open Terminal");
    ctx_menu.items[ctx_menu.item_count].enabled = 1;
    ctx_menu.items[ctx_menu.item_count++].separator = 1;
    
    strcpy(ctx_menu.items[ctx_menu.item_count].label, "Change Background");
    ctx_menu.items[ctx_menu.item_count].enabled = 1;
    ctx_menu.items[ctx_menu.item_count++].separator = 0;
    
    /* Calculate height with separators */
    ctx_menu.height = UI_SCALE_VAL(8);
    for (int i = 0; i < ctx_menu.item_count; i++) {
      ctx_menu.height += UI_SCALE_VAL(24);
      if (ctx_menu.items[i].separator) ctx_menu.height += UI_SCALE_VAL(8);
    }
    
    /* Keep on screen */
    if (ctx_menu.x + ctx_menu.width > (int)screen_width)
      ctx_menu.x = screen_width - ctx_menu.width - UI_SCALE_VAL(4);
    if (ctx_menu.y + ctx_menu.height > (int)screen_height - DOCK_HEIGHT)
      ctx_menu.y = screen_height - DOCK_HEIGHT - ctx_menu.height;
    
    return;
  }
  
  /* File Manager clicks - buttons and file selection */
  if (file_manager.visible) {
    int fx = file_manager.x;
    int fy = file_manager.y;
    int fw = file_manager.width;
    int fh = file_manager.height;
    
    /* Check if click is inside file manager window */
    if (x >= fx && x < fx + fw && y >= fy && y < fy + fh) {
      bring_window_to_front(WIN_FILE_MANAGER);
      
      /* Traffic light buttons (circles at y+16) */
      int btn_cy = fy + 16;
      int btn_r = 8;
      
      /* Close button (red) at x+16 */
      int close_cx = fx + 16;
      if ((x - close_cx) * (x - close_cx) + (y - btn_cy) * (y - btn_cy) <= btn_r * btn_r) {
        file_manager.visible = 0;
        return;
      }
      
      /* Minimize button (yellow) at x+36 */
      int min_cx = fx + 36;
      if ((x - min_cx) * (x - min_cx) + (y - btn_cy) * (y - btn_cy) <= btn_r * btn_r) {
        file_manager.visible = 0; /* Hide window */
        return;
      }
      
      /* Maximize button (green) at x+56 */
      int max_cx = fx + 56;
      if ((x - max_cx) * (x - max_cx) + (y - btn_cy) * (y - btn_cy) <= btn_r * btn_r) {
        /* Toggle maximize */
        static int fm_maximized = 0;
        static int fm_saved_x, fm_saved_y, fm_saved_w, fm_saved_h;
        if (fm_maximized) {
          file_manager.x = fm_saved_x;
          file_manager.y = fm_saved_y;
          file_manager.width = fm_saved_w;
          file_manager.height = fm_saved_h;
          fm_maximized = 0;
        } else {
          fm_saved_x = file_manager.x;
          fm_saved_y = file_manager.y;
          fm_saved_w = file_manager.width;
          fm_saved_h = file_manager.height;
          file_manager.x = 0;
          file_manager.y = MENU_BAR_HEIGHT;
          file_manager.width = screen_width;
          file_manager.height = screen_height - MENU_BAR_HEIGHT - DOCK_HEIGHT;
          fm_maximized = 1;
        }
        return;
      }
      
      /* Toolbar buttons - below title bar (y+32) */
      int toolbar_y = fy + 40;
      int toolbar_btn_h = 24;
      
      /* Back button: x+10, 60 wide */
      if (x >= fx + 10 && x < fx + 70 && y >= toolbar_y && y < toolbar_y + toolbar_btn_h) {
        /* Go up one directory */
        if (strcmp(file_manager.current_path, "/") != 0) {
          int len = strlen(file_manager.current_path);
          while (len > 0 && file_manager.current_path[len - 1] != '/') len--;
          if (len > 1) len--; /* Remove trailing slash unless root */
          file_manager.current_path[len] = '\0';
          if (len == 0) {
            strcpy(file_manager.current_path, "/");
          }
          file_manager.selected = -1;
          fm_refresh();
        }
        return;
      }
      
      /* New Folder button: x+80, 90 wide */
      if (x >= fx + 80 && x < fx + 170 && y >= toolbar_y && y < toolbar_y + toolbar_btn_h) {
        /* Create new folder */
        static int folder_num = 1;
        char path[256];
        int plen = strlen(file_manager.current_path);
        if (plen == 1 && file_manager.current_path[0] == '/') {
          snprintf(path, 256, "/NewFolder%d", folder_num++);
        } else {
          snprintf(path, 256, "%s/NewFolder%d", file_manager.current_path, folder_num++);
        }
        vfs_mkdir(path);
        fm_refresh();
        return;
      }
      
      /* New File button: x+180, 80 wide */
      if (x >= fx + 180 && x < fx + 260 && y >= toolbar_y && y < toolbar_y + toolbar_btn_h) {
        /* Create new file */
        static int file_num = 1;
        char path[256];
        int plen = strlen(file_manager.current_path);
        if (plen == 1 && file_manager.current_path[0] == '/') {
          snprintf(path, 256, "/NewFile%d.txt", file_num++);
        } else {
          snprintf(path, 256, "%s/NewFile%d.txt", file_manager.current_path, file_num++);
        }
        vfs_create(path);
        fm_refresh();
        return;
      }
      
      /* File list area - grid items (must match draw_file_manager) */
      int title_h = UI_SCALE_VAL(32);
      int toolbar_h = UI_SCALE_VAL(40);
      int loc_h = UI_SCALE_VAL(26);
      int list_y = fy + title_h + toolbar_h + loc_h;
      int item_w = UI_SCALE_VAL(90);
      int item_h = UI_SCALE_VAL(80);
      int fm_padding = UI_SCALE_VAL(20);
      int cols = (fw - fm_padding) / item_w;
      if (cols < 1) cols = 1;
      if (cols > 6) cols = 6;
      
      if (y >= list_y && y < fy + fh) {
        int rel_x = x - fx - UI_SCALE_VAL(10);
        int rel_y = y - list_y;
        if (rel_x >= 0 && rel_x < cols * item_w) {
          int col = rel_x / item_w;
          int row = rel_y / item_h;
          int idx = row * cols + col;
          
          if (idx >= 0 && idx < file_manager.entry_count) {
            file_manager.selected = idx;
            
            /* Single click enters directories */
            if (file_manager.entries[idx].is_dir) {
              /* Enter directory */
              int plen = strlen(file_manager.current_path);
              if (plen == 1 && file_manager.current_path[0] == '/') {
                char new_path[256];
                snprintf(new_path, 256, "/%s", file_manager.entries[idx].name);
                strcpy(file_manager.current_path, new_path);
              } else {
                strcat(file_manager.current_path, "/");
                strcat(file_manager.current_path, file_manager.entries[idx].name);
              }
              file_manager.selected = -1;
              fm_refresh();
            }
          }
        }
        return;
      }
      
      return; /* Click was elsewhere in window */
    }
  }
  
  /* Desktop icon clicks */
  for (int i = 0; i < NUM_DESKTOP_ICONS; i++) {
    int ix = desktop_icons[i].x * ui_scale;
    int iy = desktop_icons[i].y * ui_scale;
    int iw = UI_SCALE_VAL(64);
    int ih = UI_SCALE_VAL(70);
    if (x >= ix && x < ix + iw && y >= iy && y < iy + ih) {
      if (strcmp(desktop_icons[i].name, "Terminal") == 0) {
        terminal.visible = !terminal.visible;
      } else if (desktop_icons[i].is_dir) {
        /* Open folder in file manager */
        file_manager.visible = 1;
        bring_window_to_front(WIN_FILE_MANAGER);
        char path[256];
        snprintf(path, 256, "/%s", desktop_icons[i].name);
        strcpy(file_manager.current_path, path);
        fm_refresh();
      }
      return;
    }
  }
}

/* Window geometry constants (matching reference, unscaled base) */
#define WIN_BORDER_WIDTH 2
#define WIN_TITLEBAR_HEIGHT 28

/* Generic window click handler - handles traffic lights for ANY window 
   Returns: 0=not handled, 1=close clicked, 2=minimize clicked, 3=maximize clicked, 4=titlebar drag */
static int handle_window_titlebar_click(int mx, int my, int wx, int wy, int ww, int wh) {
  /* Check if click is inside window */
  if (mx < wx || mx >= wx + ww || my < wy || my >= wy + wh) {
    return 0;
  }
  
  /* Traffic light button centers - matching reference exactly */
  int border = UI_SCALE_VAL(WIN_BORDER_WIDTH);
  int title_h = UI_SCALE_VAL(WIN_TITLEBAR_HEIGHT);
  int btn_cy = wy + border + title_h / 2;
  int btn_r = UI_SCALE_VAL(8);
  
  /* Close button (red) at x + BORDER_WIDTH + 18 = x + 20 */
  int close_cx = wx + border + UI_SCALE_VAL(18);
  int dx = mx - close_cx;
  int dy = my - btn_cy;
  if (dx*dx + dy*dy <= btn_r*btn_r) {
    return 1; /* Close */
  }
  
  /* Minimize button (yellow) at close + 20 */
  int min_cx = close_cx + UI_SCALE_VAL(20);
  dx = mx - min_cx;
  if (dx*dx + dy*dy <= btn_r*btn_r) {
    return 2; /* Minimize */
  }
  
  /* Maximize button (green) at min + 20 */
  int max_cx = min_cx + UI_SCALE_VAL(20);
  dx = mx - max_cx;
  if (dx*dx + dy*dy <= btn_r*btn_r) {
    return 3; /* Maximize */
  }
  
  /* Title bar drag (after traffic lights) */
  if (my >= wy + border && my < wy + border + title_h) {
    if (mx >= wx + border + UI_SCALE_VAL(70)) {
      return 4; /* Start drag */
    }
  }
  
  return 0; /* Not a titlebar action */
}

/* Check if point is in title bar of a window */
static int check_title_bar_click(int mx, int my, int wx, int wy, int ww, int visible, int *out_drag_type, int drag_type) {
  if (!visible) return 0;
  /* Title bar is first 32 pixels of window, but skip first 70 pixels (traffic lights) */
  int title_h = UI_SCALE_VAL(32);
  if (mx >= wx + UI_SCALE_VAL(70) && mx < wx + ww && my >= wy && my < wy + title_h) {
    *out_drag_type = drag_type;
    return 1;
  }
  return 0;
}

/* Poll input and check if redraw needed */
static void gui_poll_input(void) {
  ps2_poll();
  
  int new_x = ps2_get_mouse_x();
  int new_y = ps2_get_mouse_y();
  int buttons = ps2_get_mouse_buttons();
  int left_held = buttons & 1;
  int left_release = !(buttons & 1) && (prev_buttons & 1);
  
  /* Handle window dragging */
  if (dragging_window != DRAG_NONE && left_held) {
    int new_win_x = new_x - drag_offset_x;
    int new_win_y = new_y - drag_offset_y;
    
    /* Clamp to screen bounds */
    if (new_win_y < MENU_BAR_HEIGHT) new_win_y = MENU_BAR_HEIGHT;
    if (new_win_y > (int)screen_height - DOCK_HEIGHT - 32) 
      new_win_y = screen_height - DOCK_HEIGHT - 32;
    if (new_win_x < 0) new_win_x = 0;
    if (new_win_x > (int)screen_width - 100) new_win_x = screen_width - 100;
    
    /* Update window position based on which window is being dragged */
    switch (dragging_window) {
      case DRAG_TERMINAL:
        terminal.content_x = new_win_x;
        terminal.content_y = new_win_y;
        break;
      case DRAG_FILE_MANAGER:
        file_manager.x = new_win_x;
        file_manager.y = new_win_y;
        break;
      case DRAG_CALCULATOR:
        calc.x = new_win_x;
        calc.y = new_win_y;
        break;
      case DRAG_SNAKE:
        snake.x = new_win_x;
        snake.y = new_win_y;
        break;
      case DRAG_NOTEPAD:
        notepad.x = new_win_x;
        notepad.y = new_win_y;
        break;
      case DRAG_CLOCK:
        analog_clock.x = new_win_x;
        analog_clock.y = new_win_y;
        break;
      case DRAG_HELP:
        help_win.x = new_win_x;
        help_win.y = new_win_y;
        break;
      case DRAG_IMAGE_VIEWER:
        img_viewer.x = new_win_x;
        img_viewer.y = new_win_y;
        break;
    }
    full_redraw = 1;
    needs_redraw = 1;
  }
  
  /* Release dragging on mouse up */
  if (left_release) {
    dragging_window = DRAG_NONE;
  }
  
  /* Check if mouse moved */
  if (new_x != last_mouse_x || new_y != last_mouse_y) {
    /* Mark old and new cursor positions as dirty */
    int cursor_size = UI_SCALE_VAL(20);
    mark_dirty(last_mouse_x, last_mouse_y, cursor_size, cursor_size);
    mark_dirty(new_x, new_y, cursor_size, cursor_size);
    
    /* Mark dock area if mouse is near - include space for magnified icons and labels */
    int dock_extend = UI_SCALE_VAL(80); /* Space for magnification + label */
    if (new_y > (int)screen_height - DOCK_HEIGHT - dock_extend || 
        last_mouse_y > (int)screen_height - DOCK_HEIGHT - dock_extend) {
      mark_dirty(0, screen_height - DOCK_HEIGHT - dock_extend, screen_width, DOCK_HEIGHT + dock_extend + 10);
    }
    
    last_mouse_x = new_x;
    last_mouse_y = new_y;
    needs_redraw = 1;
  }
  
  mouse_x = new_x;
  mouse_y = new_y;
  
  /* Detect clicks */
  int left_click = (buttons & 1) && !(prev_buttons & 1);
  int right_click = (buttons & 2) && !(prev_buttons & 2);
  
  if (left_click) {
    int clicked_window = 0;
    
    /* Window data for unified handling */
    struct { int *visible; int *x; int *y; int *w; int *h; int win_id; int drag_id; } windows[] = {
      {&terminal.visible, &terminal.content_x, &terminal.content_y, &terminal.width, &terminal.height, WIN_TERMINAL, DRAG_TERMINAL},
      {&file_manager.visible, &file_manager.x, &file_manager.y, &file_manager.width, &file_manager.height, WIN_FILE_MANAGER, DRAG_FILE_MANAGER},
      {&calc.visible, &calc.x, &calc.y, &calc.width, &calc.height, WIN_CALCULATOR, DRAG_CALCULATOR},
      {&snake.visible, &snake.x, &snake.y, &snake.width, &snake.height, WIN_SNAKE, DRAG_SNAKE},
      {&notepad.visible, &notepad.x, &notepad.y, &notepad.width, &notepad.height, WIN_NOTEPAD, DRAG_NOTEPAD},
      {&analog_clock.visible, &analog_clock.x, &analog_clock.y, &analog_clock.size, &analog_clock.size, WIN_CLOCK, DRAG_CLOCK},
      {&help_win.visible, &help_win.x, &help_win.y, &help_win.width, &help_win.height, WIN_HELP, DRAG_HELP},
      {&img_viewer.visible, &img_viewer.x, &img_viewer.y, &img_viewer.width, &img_viewer.height, WIN_IMAGE_VIEWER, DRAG_IMAGE_VIEWER},
    };
    int num_windows = sizeof(windows) / sizeof(windows[0]);
    
    /* Check windows in z-order (topmost first) */
    for (int z = NUM_MANAGED_WINDOWS - 1; z >= 0 && !clicked_window; z--) {
      int win_id = window_z_order[z];
      
      /* Find matching window data */
      for (int w = 0; w < num_windows; w++) {
        if (windows[w].win_id != win_id || !*windows[w].visible) continue;
        
        int wx = *windows[w].x;
        int wy = *windows[w].y;
        int ww = *windows[w].w;
        int wh = *windows[w].h;
        
        /* Check if click is inside this window */
        if (mouse_x < wx || mouse_x >= wx + ww || mouse_y < wy || mouse_y >= wy + wh) continue;
        
        /* Click is inside this window - bring to front */
        bring_window_to_front(win_id);
        
        /* Check traffic light buttons (matching reference positions) */
        int border = UI_SCALE_VAL(WIN_BORDER_WIDTH);
        int title_h = UI_SCALE_VAL(WIN_TITLEBAR_HEIGHT);
        int btn_cy = wy + border + title_h / 2;
        int btn_r = UI_SCALE_VAL(8);
        
        /* Close button (red) at x + 20 */
        int close_cx = wx + border + UI_SCALE_VAL(18);
        int dx = mouse_x - close_cx;
        int dy = mouse_y - btn_cy;
        if (dx*dx + dy*dy <= btn_r*btn_r) {
          *windows[w].visible = 0;
          clicked_window = 1;
          break;
        }
        
        /* Minimize button (yellow) at x + 40 */
        int min_cx = close_cx + UI_SCALE_VAL(20);
        dx = mouse_x - min_cx;
        if (dx*dx + dy*dy <= btn_r*btn_r) {
          *windows[w].visible = 0;
          clicked_window = 1;
          break;
        }
        
        /* Maximize button (green) at x + 60 */
        int max_cx = min_cx + UI_SCALE_VAL(20);
        dx = mouse_x - max_cx;
        if (dx*dx + dy*dy <= btn_r*btn_r) {
          /* Toggle maximize for this window */
          static int maximized[8] = {0};
          static int saved[8][4];
          if (maximized[w]) {
            *windows[w].x = saved[w][0];
            *windows[w].y = saved[w][1];
            *windows[w].w = saved[w][2];
            *windows[w].h = saved[w][3];
            maximized[w] = 0;
          } else {
            saved[w][0] = wx; saved[w][1] = wy;
            saved[w][2] = ww; saved[w][3] = wh;
            *windows[w].x = 0;
            *windows[w].y = MENU_BAR_HEIGHT;
            *windows[w].w = screen_width;
            *windows[w].h = screen_height - MENU_BAR_HEIGHT - DOCK_HEIGHT;
            maximized[w] = 1;
          }
          clicked_window = 1;
          break;
        }
        
        /* Check for title bar drag (after traffic lights area) */
        if (mouse_y >= wy + border && mouse_y < wy + border + title_h) {
          if (mouse_x >= wx + border + UI_SCALE_VAL(70)) {
            dragging_window = windows[w].drag_id;
            drag_offset_x = mouse_x - wx;
            drag_offset_y = mouse_y - wy;
            clicked_window = 1;
            break;
          }
        }
        
        /* Window-specific content handling */
        if (win_id == WIN_FILE_MANAGER) {
          int fm_title_h = UI_SCALE_VAL(32);
          int fm_toolbar_top = wy + fm_title_h + UI_SCALE_VAL(8);
          int fm_toolbar_bottom = fm_toolbar_top + UI_SCALE_VAL(24);
          /* Toolbar buttons */
          if (mouse_y >= fm_toolbar_top && mouse_y < fm_toolbar_bottom) {
            if (mouse_x >= wx + UI_SCALE_VAL(10) &&
                mouse_x < wx + UI_SCALE_VAL(70)) {
              /* Back button */
              if (strcmp(file_manager.current_path, "/") != 0) {
                int len = strlen(file_manager.current_path);
                while (len > 0 && file_manager.current_path[len-1] != '/') len--;
                if (len > 1) len--;
                file_manager.current_path[len] = '\0';
                if (len == 0) strcpy(file_manager.current_path, "/");
                file_manager.selected = -1;
                fm_refresh();
              }
            } else if (mouse_x >= wx + UI_SCALE_VAL(80) &&
                       mouse_x < wx + UI_SCALE_VAL(170)) {
              /* New Folder */
              static int fnum = 1;
              char path[256];
              if (file_manager.current_path[0] == '/' && file_manager.current_path[1] == '\0')
                snprintf(path, 256, "/NewFolder%d", fnum++);
              else
                snprintf(path, 256, "%s/NewFolder%d", file_manager.current_path, fnum++);
              vfs_mkdir(path);
              fm_refresh();
            } else if (mouse_x >= wx + UI_SCALE_VAL(180) &&
                       mouse_x < wx + UI_SCALE_VAL(260)) {
              /* New File */
              static int fnm = 1;
              char path[256];
              if (file_manager.current_path[0] == '/' && file_manager.current_path[1] == '\0')
                snprintf(path, 256, "/NewFile%d.txt", fnm++);
              else
                snprintf(path, 256, "%s/NewFile%d.txt", file_manager.current_path, fnm++);
              vfs_create(path);
              fm_refresh();
            }
          }
          /* File list grid (must match draw_file_manager) */
          else if (mouse_y >= wy + UI_SCALE_VAL(100)) {
            int item_w = UI_SCALE_VAL(90);
            int item_h = UI_SCALE_VAL(80);
            int cols = (ww - UI_SCALE_VAL(20)) / item_w;
            if (cols < 1) cols = 1;
            if (cols > 6) cols = 6;
            int rel_x = mouse_x - wx - UI_SCALE_VAL(10);
            int rel_y = mouse_y - wy - UI_SCALE_VAL(100);
            if (rel_x >= 0) {
              int idx = (rel_y / item_h) * cols + (rel_x / item_w);
              if (idx >= 0 && idx < file_manager.entry_count) {
                file_manager.selected = idx;
                if (file_manager.entries[idx].is_dir) {
                  if (file_manager.current_path[0] == '/' && file_manager.current_path[1] == '\0') {
                    char np[256];
                    snprintf(np, 256, "/%s", file_manager.entries[idx].name);
                    strcpy(file_manager.current_path, np);
                  } else {
                    strcat(file_manager.current_path, "/");
                    strcat(file_manager.current_path, file_manager.entries[idx].name);
                  }
                  file_manager.selected = -1;
                  fm_refresh();
                }
              }
            }
          }
        }
        
        clicked_window = 1;
        break;
      }
    }
    
    /* If no window clicked, check other UI elements */
    if (!clicked_window) {
      handle_mouse_click(mouse_x, mouse_y, 1);
    }
    needs_redraw = 1;
    full_redraw = 1;
  }
  if (right_click) {
    handle_mouse_click(mouse_x, mouse_y, 2);
    needs_redraw = 1;
    full_redraw = 1;
  }
  
  prev_buttons = buttons;
  mouse_buttons = buttons;
}

/* Draw everything to backbuffer */
static void gui_draw_all(void) {
  /* Update context menu hover */
  if (ctx_menu.visible) {
    int old_hover = ctx_menu.hover_index;
    ctx_menu.hover_index = -1;
    if (mouse_x >= ctx_menu.x && mouse_x < ctx_menu.x + ctx_menu.width) {
      int item_y = ctx_menu.y + 8;
      for (int i = 0; i < ctx_menu.item_count; i++) {
        if (mouse_y >= item_y && mouse_y < item_y + 20) {
          ctx_menu.hover_index = i;
          break;
        }
        item_y += 24;
        if (ctx_menu.items[i].separator) item_y += 8;
      }
    }
    if (old_hover != ctx_menu.hover_index) {
      mark_dirty(ctx_menu.x, ctx_menu.y, ctx_menu.width + 4, ctx_menu.height + 4);
    }
  }

  /* Draw everything to backbuffer */
  draw_wallpaper();
  draw_desktop_icons();
  
  /* Draw windows in z-order (back to front) */
  for (int z = 0; z < NUM_MANAGED_WINDOWS; z++) {
    int win_id = window_z_order[z];
    switch (win_id) {
      case WIN_FILE_MANAGER: draw_file_manager(); break;
      case WIN_TERMINAL: draw_terminal(); break;
      case WIN_CALCULATOR: draw_calculator(); break;
      case WIN_SNAKE: draw_snake_game(); break;
      case WIN_NOTEPAD: draw_notepad(); break;
      case WIN_CLOCK: draw_clock_widget(); break;
      case WIN_HELP: draw_help_window(); break;
      case WIN_IMAGE_VIEWER: draw_image_viewer(); break;
    }
  }
  
  draw_dock();
  draw_menu_bar();
  draw_context_menu();
  draw_cursor(mouse_x, mouse_y);
}

void gui_compose(void) {
  gui_poll_input();

  if (wallpaper_cache_dirty) {
    full_redraw = 1;
    needs_redraw = 1;
  }
  
  if (needs_redraw) {
    gui_draw_all();
    smart_swap_buffers();
    needs_redraw = 0;
    frame_count++;
  }
}

void gui_main_loop(void) {
  /* Initial full redraw */
  full_redraw = 1;
  needs_redraw = 1;
  
  while (1) {
    /* Poll input aggressively - USB legacy emulation requires frequent polling */
    for (int poll = 0; poll < 20; poll++) {
      ps2_poll();
      usb_poll();
      for (volatile int d = 0; d < 100; d++) __asm__ volatile("pause");
    }
    
    gui_compose();
    
    /* Minimal yield - just a few pause instructions */
    for (volatile int i = 0; i < 50; i++) {
      __asm__ volatile("pause");
    }
  }
}
