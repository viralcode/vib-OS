/*
 * Vib-OS - Application Framework
 *
 * Base framework for GUI applications.
 */

#include "mm/kmalloc.h"
#include "printk.h"
#include "types.h"

/* Forward declarations */
struct window;
struct terminal;

extern struct window *gui_create_window(const char *title, int x, int y, int w,
                                        int h);
extern void gui_destroy_window(struct window *win);
extern void gui_focus_window(struct window *win);
extern void gui_draw_rect(int x, int y, int w, int h, uint32_t color);
extern void gui_draw_string(int x, int y, const char *str, uint32_t fg,
                            uint32_t bg);
extern struct terminal *term_create(int x, int y, int cols, int rows);
extern void term_render(struct terminal *term);

/* ===================================================================== */
/* Application Types */
/* ===================================================================== */

typedef enum {
  APP_TYPE_TERMINAL,
  APP_TYPE_FILE_MANAGER,
  APP_TYPE_TEXT_EDITOR,
  APP_TYPE_IMAGE_VIEWER,
  APP_TYPE_BROWSER,
  APP_TYPE_SETTINGS,
  APP_TYPE_CALCULATOR,
  APP_TYPE_PAINT,
  APP_TYPE_HELP,
  APP_TYPE_CUSTOM
} app_type_t;

struct application {
  int id;
  char name[64];
  char icon[32];
  app_type_t type;
  struct window *main_window;
  void *app_data;

  /* Lifecycle callbacks */
  int (*on_init)(struct application *app);
  void (*on_update)(struct application *app);
  void (*on_draw)(struct application *app);
  void (*on_exit)(struct application *app);
};

#define MAX_APPS 32
static struct application apps[MAX_APPS];
static int app_count = 0;

/* ===================================================================== */
/* Built-in Applications */
/* ===================================================================== */

/* Terminal Application */
static int terminal_init(struct application *app) {
  app->main_window = gui_create_window("Terminal", 100, 100, 656, 424);
  if (!app->main_window)
    return -1;

  struct terminal *term = term_create(102 + 2, 100 + 30, 80, 24);
  app->app_data = term;

  /* Set as active terminal so keyboard input works */
  extern void term_set_active(struct terminal * term);
  term_set_active(term);

  return 0;
}

static void terminal_draw(struct application *app) {
  if (app->app_data) {
    term_render(app->app_data);
  }
}

/* File Manager Application */
static int file_manager_init(struct application *app) {
  app->main_window = gui_create_window("Files", 200, 150, 600, 400);
  return 0;
}

static void file_manager_draw(struct application *app) {
  if (!app->main_window)
    return;

  /* TODO: Draw file list */
  gui_draw_string(210, 190, "/ (Root)", 0xCDD6F4, 0x1E1E2E);
  gui_draw_string(210, 210, "  bin/", 0x89B4FA, 0x1E1E2E);
  gui_draw_string(210, 230, "  etc/", 0x89B4FA, 0x1E1E2E);
  gui_draw_string(210, 250, "  home/", 0x89B4FA, 0x1E1E2E);
  gui_draw_string(210, 270, "  usr/", 0x89B4FA, 0x1E1E2E);
  gui_draw_string(210, 290, "  var/", 0x89B4FA, 0x1E1E2E);
}

/* Settings Application */
static int settings_init(struct application *app) {
  app->main_window = gui_create_window("Settings", 250, 100, 500, 400);
  return 0;
}

static void settings_draw(struct application *app) {
  if (!app->main_window)
    return;

  int y = 140;
  gui_draw_string(260, y, "Display", 0xCDD6F4, 0x1E1E2E);
  y += 30;
  gui_draw_string(270, y, "Resolution: 1920x1080", 0x808080, 0x1E1E2E);
  y += 20;

  y += 20;
  gui_draw_string(260, y, "Sound", 0xCDD6F4, 0x1E1E2E);
  y += 30;
  gui_draw_string(270, y, "Volume: 80%", 0x808080, 0x1E1E2E);
  y += 20;

  y += 20;
  gui_draw_string(260, y, "Network", 0xCDD6F4, 0x1E1E2E);
  y += 30;
  gui_draw_string(270, y, "Status: Connected", 0x808080, 0x1E1E2E);
  y += 20;

  y += 20;
  gui_draw_string(260, y, "About", 0xCDD6F4, 0x1E1E2E);
  y += 30;
  gui_draw_string(270, y, "Vib-OS v0.3.0", 0x808080, 0x1E1E2E);
  y += 20;
  gui_draw_string(270, y, "ARM64 Operating System", 0x808080, 0x1E1E2E);
}

/* Simple Text Editor */
static int editor_init(struct application *app) {
  app->main_window = gui_create_window("Text Editor", 150, 80, 700, 500);
  return 0;
}

static void editor_draw(struct application *app) {
  if (!app->main_window)
    return;

  /* Toolbar */
  gui_draw_rect(152, 112, 696, 30, 0x313244);
  gui_draw_string(160, 118, "File  Edit  View  Help", 0xCDD6F4, 0x313244);

  /* Status bar */
  gui_draw_rect(152, 550, 696, 24, 0x313244);
  gui_draw_string(160, 554, "Line 1, Col 1 | UTF-8", 0x808080, 0x313244);
}

/* Calculator Application */
typedef struct {
  long value;
  long pending;
  char op;
  int clear_next;
} calc_state_t;

static calc_state_t calc_state = {0, 0, 0, 0};

static int calculator_init(struct application *app) {
  app->main_window = gui_create_window("Calculator", 300, 100, 200, 270);
  calc_state.value = 0;
  calc_state.pending = 0;
  calc_state.op = 0;
  calc_state.clear_next = 0;
  return 0;
}

static void long_to_str(long val, char *buf) {
  int idx = 0;
  char tmp[20];
  int is_neg = 0;

  if (val < 0) {
    is_neg = 1;
    val = -val;
  }

  if (val == 0) {
    buf[0] = '0';
    buf[1] = '\0';
    return;
  }

  while (val > 0 && idx < 18) {
    tmp[idx++] = '0' + (val % 10);
    val /= 10;
  }

  int bidx = 0;
  if (is_neg)
    buf[bidx++] = '-';
  while (idx > 0) {
    buf[bidx++] = tmp[--idx];
  }
  buf[bidx] = '\0';
}

static void calculator_draw(struct application *app) {
  if (!app->main_window)
    return;

  int base_x = 302; /* Window x + border */
  int base_y = 132; /* Window y + titlebar + border */

  /* Display */
  gui_draw_rect(base_x + 4, base_y, 190, 30, 0xFFFFFF);

  char display[20];
  long_to_str(calc_state.value, display);
  int len = 0;
  while (display[len])
    len++;
  gui_draw_string(base_x + 190 - len * 8, base_y + 8, display, 0x000000,
                  0xFFFFFF);

  /* Button labels */
  static const char *btns[4][4] = {{"7", "8", "9", "/"},
                                   {"4", "5", "6", "*"},
                                   {"1", "2", "3", "-"},
                                   {"C", "0", "=", "+"}};

  int btn_w = 42;
  int btn_h = 36;
  int pad = 4;

  for (int row = 0; row < 4; row++) {
    for (int col = 0; col < 4; col++) {
      int bx = base_x + pad + col * (btn_w + pad);
      int by = base_y + 36 + row * (btn_h + pad);

      /* Button color */
      uint32_t bg = 0xE0E0E0;
      uint32_t fg = 0x000000;
      char c = btns[row][col][0];
      if (c == '/' || c == '*' || c == '-' || c == '+') {
        bg = 0xFF9500; /* Orange for operators */
        fg = 0xFFFFFF;
      } else if (c == 'C') {
        bg = 0xAAAAAA;
      }

      gui_draw_rect(bx, by, btn_w, btn_h, bg);
      gui_draw_string(bx + (btn_w - 8) / 2, by + (btn_h - 16) / 2,
                      btns[row][col], fg, bg);
    }
  }
}

/* Paint Application */
static int paint_init(struct application *app) {
  app->main_window = gui_create_window("Paint", 150, 80, 500, 400);
  return 0;
}

static void paint_draw(struct application *app) {
  if (!app->main_window)
    return;

  int base_x = 152;
  int base_y = 112;

  /* Canvas area */
  gui_draw_rect(base_x + 4, base_y + 40, 490, 320, 0xFFFFFF);

  /* Toolbar */
  gui_draw_rect(base_x + 4, base_y, 490, 36, 0x404040);
  gui_draw_string(base_x + 10, base_y + 10,
                  "Brush: [O]  Line: [/]  Rect: [#]  Color: ", 0xFFFFFF,
                  0x404040);

  /* Color palette */
  int colors[] = {0xFF0000, 0x00FF00, 0x0000FF, 0xFFFF00, 0x000000};
  for (int i = 0; i < 5; i++) {
    gui_draw_rect(base_x + 360 + i * 24, base_y + 6, 20, 20, colors[i]);
  }

  /* Instructions */
  gui_draw_string(base_x + 150, base_y + 180, "Click and drag to draw!",
                  0x888888, 0xFFFFFF);
}

/* Help Application */
static int help_init(struct application *app) {
  app->main_window = gui_create_window("Help", 200, 100, 400, 350);
  return 0;
}

static void help_draw(struct application *app) {
  if (!app->main_window)
    return;

  int base_x = 202;
  int base_y = 132;
  int y = base_y;

  gui_draw_string(base_x + 10, y, "Vib-OS Help", 0x89B4FA, 0x1E1E2E);
  y += 24;
  gui_draw_string(base_x + 10, y, "================", 0x89B4FA, 0x1E1E2E);
  y += 24;

  gui_draw_string(base_x + 10, y, "Mouse:", 0xF9E2AF, 0x1E1E2E);
  y += 20;
  gui_draw_string(base_x + 20, y, "- Click dock icons to launch apps", 0xCDD6F4,
                  0x1E1E2E);
  y += 16;
  gui_draw_string(base_x + 20, y, "- Drag window title bars to move", 0xCDD6F4,
                  0x1E1E2E);
  y += 16;
  gui_draw_string(base_x + 20, y, "- Click window close button to exit",
                  0xCDD6F4, 0x1E1E2E);
  y += 24;

  gui_draw_string(base_x + 10, y, "Terminal:", 0xF9E2AF, 0x1E1E2E);
  y += 20;
  gui_draw_string(base_x + 20, y, "- Type 'help' for commands", 0xCDD6F4,
                  0x1E1E2E);
  y += 16;
  gui_draw_string(base_x + 20, y, "- Type 'neofetch' for system info", 0xCDD6F4,
                  0x1E1E2E);
  y += 24;

  gui_draw_string(base_x + 10, y, "Dock Apps:", 0xF9E2AF, 0x1E1E2E);
  y += 20;
  gui_draw_string(base_x + 20, y, "Terminal, Files, Calculator, Paint",
                  0xCDD6F4, 0x1E1E2E);
  y += 24;

  gui_draw_string(base_x + 10, y, "Copyright 2026 Vib-OS Project", 0x585B70,
                  0x1E1E2E);
}

/* ===================================================================== */
/* Application Launcher */
/* ===================================================================== */

struct application *app_launch(const char *name, app_type_t type) {
  if (app_count >= MAX_APPS) {
    printk(KERN_ERR "APP: Max applications reached\n");
    return NULL;
  }

  struct application *app = &apps[app_count++];
  app->id = app_count;

  for (int i = 0; i < 63 && name[i]; i++) {
    app->name[i] = name[i];
    app->name[i + 1] = '\0';
  }

  app->type = type;
  app->app_data = NULL;

  /* Set up callbacks based on type */
  switch (type) {
  case APP_TYPE_TERMINAL:
    app->on_init = terminal_init;
    app->on_draw = terminal_draw;
    break;
  case APP_TYPE_FILE_MANAGER:
    app->on_init = file_manager_init;
    app->on_draw = file_manager_draw;
    break;
  case APP_TYPE_SETTINGS:
    app->on_init = settings_init;
    app->on_draw = settings_draw;
    break;
  case APP_TYPE_TEXT_EDITOR:
    app->on_init = editor_init;
    app->on_draw = editor_draw;
    break;
  case APP_TYPE_CALCULATOR:
    app->on_init = calculator_init;
    app->on_draw = calculator_draw;
    break;
  case APP_TYPE_PAINT:
    app->on_init = paint_init;
    app->on_draw = paint_draw;
    break;
  case APP_TYPE_HELP:
    app->on_init = help_init;
    app->on_draw = help_draw;
    break;
  default:
    break;
  }

  /* Initialize */
  if (app->on_init) {
    if (app->on_init(app) < 0) {
      app_count--;
      return NULL;
    }
  }

  printk(KERN_INFO "APP: Launched '%s'\n", name);

  return app;
}

void app_close(struct application *app) {
  if (!app)
    return;

  if (app->on_exit) {
    app->on_exit(app);
  }

  if (app->main_window) {
    gui_destroy_window(app->main_window);
  }

  app->id = 0;
}

void app_update_all(void) {
  for (int i = 0; i < app_count; i++) {
    if (apps[i].id > 0 && apps[i].on_update) {
      apps[i].on_update(&apps[i]);
    }
  }
}

void app_draw_all(void) {
  for (int i = 0; i < app_count; i++) {
    if (apps[i].id > 0 && apps[i].on_draw) {
      apps[i].on_draw(&apps[i]);
    }
  }
}

/* ===================================================================== */
/* Desktop Launcher Items */
/* ===================================================================== */

struct launcher_item {
  char name[32];
  char icon[32];
  app_type_t type;
  int x, y;
};

#define MAX_LAUNCHER_ITEMS 16
static struct launcher_item launcher_items[MAX_LAUNCHER_ITEMS];
static int launcher_count = 0;

void launcher_add_item(const char *name, const char *icon, app_type_t type) {
  if (launcher_count >= MAX_LAUNCHER_ITEMS)
    return;

  struct launcher_item *item = &launcher_items[launcher_count];

  for (int i = 0; i < 31 && name[i]; i++) {
    item->name[i] = name[i];
    item->name[i + 1] = '\0';
  }

  for (int i = 0; i < 31 && icon[i]; i++) {
    item->icon[i] = icon[i];
    item->icon[i + 1] = '\0';
  }

  item->type = type;

  /* Position on desktop grid */
  item->x = 20 + (launcher_count % 6) * 100;
  item->y = 20 + (launcher_count / 6) * 100;

  launcher_count++;
}

void launcher_draw(void) {
  for (int i = 0; i < launcher_count; i++) {
    struct launcher_item *item = &launcher_items[i];

    /* Draw icon background */
    gui_draw_rect(item->x, item->y, 64, 64, 0x313244);

    /* Draw icon (placeholder) */
    gui_draw_string(item->x + 20, item->y + 24, item->icon, 0xFFFFFF, 0x313244);

    /* Draw name */
    gui_draw_string(item->x, item->y + 68, item->name, 0xCDD6F4, 0x1E1E2E);
  }
}

void launcher_handle_click(int x, int y) {
  for (int i = 0; i < launcher_count; i++) {
    struct launcher_item *item = &launcher_items[i];

    if (x >= item->x && x < item->x + 64 && y >= item->y && y < item->y + 100) {
      app_launch(item->name, item->type);
      return;
    }
  }
}

/* ===================================================================== */
/* Desktop Initialization */
/* ===================================================================== */

void desktop_init(void) {
  printk(KERN_INFO "DESKTOP: Initializing desktop environment\n");

  /* Add desktop icons */
  launcher_add_item("Terminal", ">_", APP_TYPE_TERMINAL);
  launcher_add_item("Files", "[]", APP_TYPE_FILE_MANAGER);
  launcher_add_item("Editor", "=", APP_TYPE_TEXT_EDITOR);
  launcher_add_item("Settings", "@", APP_TYPE_SETTINGS);

  printk(KERN_INFO "DESKTOP: %d launcher items created\n", launcher_count);
}
