/*
 * VibCode x64 - Desktop Manager
 * Desktop icons, right-click menus, file operations
 */

#include "../include/gui.h"
#include "../include/kmalloc.h"
#include "../include/string.h"
#include "../include/vfs.h"

/* External functions */
extern void fb_fill_rect(int x, int y, int w, int h, uint32_t color);
extern void fb_put_pixel(int x, int y, uint32_t color);
extern void gui_draw_string(int x, int y, const char *str, uint32_t color);
extern void gui_draw_circle(int cx, int cy, int r, uint32_t color);
extern uint32_t screen_width, screen_height;

/* Desktop configuration */
#define DESKTOP_PATH "/Desktop"
#define DESKTOP_ICON_SIZE 64
#define DESKTOP_ICON_SPACING 90
#define DESKTOP_START_X 30
#define DESKTOP_START_Y 60
#define DESKTOP_MAX_ICONS 64

/* Icon types */
#define ICON_FILE   0
#define ICON_FOLDER 1
#define ICON_IMAGE  2
#define ICON_TEXT   3

/* Colors */
#define COLOR_ICON_BG       0x00000000
#define COLOR_ICON_SELECTED 0x4488CCAA
#define COLOR_MENU_BG       0x2D2D2D
#define COLOR_MENU_HOVER    0x0078D4
#define COLOR_MENU_TEXT     0xFFFFFF
#define COLOR_MENU_BORDER   0x5C5C5C

/* Desktop icon (internal) */
typedef struct {
  char name[64];
  char path[256];
  int type;
  int x, y;
  int selected;
} dsk_icon_t;

/* Context menu item */
typedef struct {
  char label[32];
  int enabled;
  int separator;
  void (*action)(void *ctx);
} menu_item_t;

/* Context menu */
typedef struct {
  int visible;
  int x, y;
  int width, height;
  int hover_index;
  menu_item_t items[16];
  int item_count;
  void *context;
  int on_icon; /* Was menu opened on an icon? */
} context_menu_t;

/* Desktop state */
static dsk_icon_t desktop_icons[DESKTOP_MAX_ICONS];
static int desktop_icon_count = 0;
static int desktop_selected = -1;
static context_menu_t ctx_menu = {0};

/* Clipboard */
static char clipboard_path[256] = {0};
static int clipboard_cut = 0;

/* ===================================================================== */
/* Icon Helpers                                                          */
/* ===================================================================== */

static int get_icon_type(const char *name) {
  int len = strlen(name);
  if (len > 4) {
    const char *ext = name + len - 4;
    if (strcmp(ext, ".txt") == 0) return ICON_TEXT;
    if (strcmp(ext, ".jpg") == 0 || strcmp(ext, ".png") == 0) return ICON_IMAGE;
  }
  return ICON_FILE;
}

/* ===================================================================== */
/* Desktop Icons                                                         */
/* ===================================================================== */

void desktop_refresh(void) {
  desktop_icon_count = 0;
  
  dirent_t entries[32];
  int count = vfs_readdir(DESKTOP_PATH, entries, 32);
  
  int col = 0, row = 0;
  for (int i = 0; i < count && desktop_icon_count < DESKTOP_MAX_ICONS; i++) {
    dsk_icon_t *icon = &desktop_icons[desktop_icon_count++];
    strncpy(icon->name, entries[i].name, 63);
    snprintf(icon->path, 256, "%s/%s", DESKTOP_PATH, entries[i].name);
    icon->type = entries[i].type ? ICON_FOLDER : get_icon_type(entries[i].name);
    icon->x = DESKTOP_START_X + col * DESKTOP_ICON_SPACING;
    icon->y = DESKTOP_START_Y + row * DESKTOP_ICON_SPACING;
    icon->selected = 0;
    
    row++;
    if (icon->y + DESKTOP_ICON_SPACING > (int)screen_height - 100) {
      row = 0;
      col++;
    }
  }
}

void desktop_draw_icons(void) {
  for (int i = 0; i < desktop_icon_count; i++) {
    dsk_icon_t *icon = &desktop_icons[i];
    int x = icon->x;
    int y = icon->y;
    
    /* Selection highlight */
    if (icon->selected || i == desktop_selected) {
      fb_fill_rect(x - 4, y - 4, DESKTOP_ICON_SIZE + 8, 
                   DESKTOP_ICON_SIZE + 24, COLOR_ICON_SELECTED);
    }
    
    /* Icon graphic */
    if (icon->type == ICON_FOLDER) {
      /* Folder icon - yellow */
      fb_fill_rect(x + 8, y + 16, 48, 32, 0xFFCC00);
      fb_fill_rect(x + 8, y + 12, 24, 6, 0xFFAA00);
    } else if (icon->type == ICON_IMAGE) {
      /* Image icon - with picture */
      fb_fill_rect(x + 8, y + 8, 48, 40, 0xFFFFFF);
      fb_fill_rect(x + 12, y + 12, 40, 32, 0x88CCFF);
      gui_draw_circle(x + 24, y + 22, 6, 0xFFFF00); /* Sun */
      fb_fill_rect(x + 12, y + 34, 40, 10, 0x44AA44); /* Ground */
    } else if (icon->type == ICON_TEXT) {
      /* Text file icon */
      fb_fill_rect(x + 12, y + 8, 40, 44, 0xFFFFFF);
      fb_fill_rect(x + 16, y + 16, 32, 2, 0x333333);
      fb_fill_rect(x + 16, y + 22, 28, 2, 0x333333);
      fb_fill_rect(x + 16, y + 28, 30, 2, 0x333333);
      fb_fill_rect(x + 16, y + 34, 24, 2, 0x333333);
    } else {
      /* Generic file icon */
      fb_fill_rect(x + 12, y + 8, 40, 44, 0xE0E0E0);
      fb_fill_rect(x + 12, y + 8, 40, 1, 0xCCCCCC);
    }
    
    /* Label */
    int label_len = strlen(icon->name);
    if (label_len > 10) label_len = 10;
    int label_x = x + (DESKTOP_ICON_SIZE - label_len * 8) / 2;
    int label_y = y + DESKTOP_ICON_SIZE + 2;
    
    /* Label background */
    fb_fill_rect(label_x - 2, label_y - 1, label_len * 8 + 4, 14, 0x000000AA);
    
    /* Label text (truncated) */
    char label[12];
    strncpy(label, icon->name, 10);
    label[10] = '\0';
    if (strlen(icon->name) > 10) {
      label[8] = '.';
      label[9] = '.';
    }
    gui_draw_string(label_x, label_y, label, 0xFFFFFF);
  }
}

/* ===================================================================== */
/* Context Menu Actions                                                  */
/* ===================================================================== */

static void action_open(void *ctx) {
  (void)ctx;
  if (desktop_selected >= 0) {
    dsk_icon_t *icon = &desktop_icons[desktop_selected];
    /* TODO: Open file/folder */
    (void)icon;
  }
}

static void action_new_folder(void *ctx) {
  (void)ctx;
  static int folder_num = 1;
  char name[64], path[256];
  snprintf(name, 64, "New Folder %d", folder_num++);
  snprintf(path, 256, "%s/%s", DESKTOP_PATH, name);
  vfs_mkdir(path);
  desktop_refresh();
}

static void action_new_file(void *ctx) {
  (void)ctx;
  static int file_num = 1;
  char name[64], path[256];
  snprintf(name, 64, "document%d.txt", file_num++);
  snprintf(path, 256, "%s/%s", DESKTOP_PATH, name);
  vfs_create(path);
  desktop_refresh();
}

static void action_delete(void *ctx) {
  (void)ctx;
  if (desktop_selected >= 0) {
    dsk_icon_t *icon = &desktop_icons[desktop_selected];
    vfs_delete(icon->path);
    desktop_selected = -1;
    desktop_refresh();
  }
}

static void action_rename(void *ctx) {
  (void)ctx;
  /* TODO: Inline rename */
}

static void action_copy(void *ctx) {
  (void)ctx;
  if (desktop_selected >= 0) {
    strncpy(clipboard_path, desktop_icons[desktop_selected].path, 255);
    clipboard_cut = 0;
  }
}

static void action_cut(void *ctx) {
  (void)ctx;
  if (desktop_selected >= 0) {
    strncpy(clipboard_path, desktop_icons[desktop_selected].path, 255);
    clipboard_cut = 1;
  }
}

static void action_paste(void *ctx) {
  (void)ctx;
  if (clipboard_path[0]) {
    /* Get filename from clipboard path */
    const char *name = clipboard_path;
    for (const char *p = clipboard_path; *p; p++) {
      if (*p == '/') name = p + 1;
    }
    
    char dest[256];
    snprintf(dest, 256, "%s/%s", DESKTOP_PATH, name);
    
    /* Read source file */
    file_t *src = vfs_open(clipboard_path, O_RDONLY);
    if (src) {
      file_t *dst = vfs_open(dest, O_CREAT | O_WRONLY);
      if (dst) {
        char buf[512];
        ssize_t n;
        while ((n = vfs_read(src, buf, 512)) > 0) {
          vfs_write(dst, buf, n);
        }
        vfs_close(dst);
      }
      vfs_close(src);
      
      if (clipboard_cut) {
        vfs_delete(clipboard_path);
        clipboard_path[0] = '\0';
      }
      
      desktop_refresh();
    }
  }
}

static void action_refresh(void *ctx) {
  (void)ctx;
  desktop_refresh();
}

static void action_sort_name(void *ctx) {
  (void)ctx;
  /* Simple bubble sort by name */
  for (int i = 0; i < desktop_icon_count - 1; i++) {
    for (int j = 0; j < desktop_icon_count - i - 1; j++) {
      if (strcmp(desktop_icons[j].name, desktop_icons[j+1].name) > 0) {
        dsk_icon_t tmp = desktop_icons[j];
        desktop_icons[j] = desktop_icons[j+1];
        desktop_icons[j+1] = tmp;
      }
    }
  }
  /* Reposition */
  int col = 0, row = 0;
  for (int i = 0; i < desktop_icon_count; i++) {
    desktop_icons[i].x = DESKTOP_START_X + col * DESKTOP_ICON_SPACING;
    desktop_icons[i].y = DESKTOP_START_Y + row * DESKTOP_ICON_SPACING;
    row++;
    if (desktop_icons[i].y + DESKTOP_ICON_SPACING > (int)screen_height - 100) {
      row = 0;
      col++;
    }
  }
}

/* ===================================================================== */
/* Context Menu                                                          */
/* ===================================================================== */

static void ctx_menu_add(const char *label, void (*action)(void *), int enabled) {
  if (ctx_menu.item_count >= 16) return;
  menu_item_t *item = &ctx_menu.items[ctx_menu.item_count++];
  strncpy(item->label, label, 31);
  item->action = action;
  item->enabled = enabled;
  item->separator = 0;
}

static void ctx_menu_add_separator(void) {
  if (ctx_menu.item_count > 0) {
    ctx_menu.items[ctx_menu.item_count - 1].separator = 1;
  }
}

void desktop_show_context_menu(int x, int y, int on_icon) {
  ctx_menu.item_count = 0;
  ctx_menu.x = x;
  ctx_menu.y = y;
  ctx_menu.hover_index = -1;
  ctx_menu.visible = 1;
  ctx_menu.on_icon = on_icon;
  
  if (on_icon) {
    ctx_menu_add("Open", action_open, 1);
    ctx_menu_add_separator();
    ctx_menu_add("Cut", action_cut, 1);
    ctx_menu_add("Copy", action_copy, 1);
    ctx_menu_add_separator();
    ctx_menu_add("Rename", action_rename, 1);
    ctx_menu_add("Delete", action_delete, 1);
  } else {
    ctx_menu_add("New Folder", action_new_folder, 1);
    ctx_menu_add("New Text File", action_new_file, 1);
    ctx_menu_add_separator();
    ctx_menu_add("Paste", action_paste, clipboard_path[0] != '\0');
    ctx_menu_add_separator();
    ctx_menu_add("Sort by Name", action_sort_name, 1);
    ctx_menu_add("Refresh", action_refresh, 1);
  }
  
  ctx_menu.width = 160;
  ctx_menu.height = ctx_menu.item_count * 24 + 8;
  
  /* Add separator height */
  for (int i = 0; i < ctx_menu.item_count; i++) {
    if (ctx_menu.items[i].separator) ctx_menu.height += 8;
  }
  
  /* Keep on screen */
  if (x + ctx_menu.width > (int)screen_width) {
    ctx_menu.x = screen_width - ctx_menu.width - 4;
  }
  if (y + ctx_menu.height > (int)screen_height - 80) {
    ctx_menu.y = screen_height - 80 - ctx_menu.height;
  }
}

void desktop_hide_context_menu(void) {
  ctx_menu.visible = 0;
}

void desktop_draw_context_menu(void) {
  if (!ctx_menu.visible) return;
  
  int x = ctx_menu.x;
  int y = ctx_menu.y;
  int w = ctx_menu.width;
  int h = ctx_menu.height;
  
  /* Shadow */
  fb_fill_rect(x + 4, y + 4, w, h, 0x000000);
  
  /* Background */
  fb_fill_rect(x, y, w, h, COLOR_MENU_BG);
  
  /* Border */
  fb_fill_rect(x, y, w, 1, COLOR_MENU_BORDER);
  fb_fill_rect(x, y + h - 1, w, 1, COLOR_MENU_BORDER);
  fb_fill_rect(x, y, 1, h, COLOR_MENU_BORDER);
  fb_fill_rect(x + w - 1, y, 1, h, COLOR_MENU_BORDER);
  
  /* Items */
  int item_y = y + 4;
  for (int i = 0; i < ctx_menu.item_count; i++) {
    menu_item_t *item = &ctx_menu.items[i];
    
    /* Hover highlight */
    if (i == ctx_menu.hover_index && item->enabled) {
      fb_fill_rect(x + 2, item_y, w - 4, 22, COLOR_MENU_HOVER);
    }
    
    /* Text */
    uint32_t color = item->enabled ? COLOR_MENU_TEXT : 0x808080;
    gui_draw_string(x + 12, item_y + 4, item->label, color);
    
    item_y += 24;
    
    /* Separator */
    if (item->separator) {
      fb_fill_rect(x + 8, item_y, w - 16, 1, 0x555555);
      item_y += 8;
    }
  }
}

/* Handle context menu click */
int desktop_context_menu_click(int mx, int my) {
  if (!ctx_menu.visible) return 0;
  
  /* Outside menu? */
  if (mx < ctx_menu.x || mx >= ctx_menu.x + ctx_menu.width ||
      my < ctx_menu.y || my >= ctx_menu.y + ctx_menu.height) {
    desktop_hide_context_menu();
    return 1;
  }
  
  /* Find clicked item */
  int item_y = ctx_menu.y + 4;
  for (int i = 0; i < ctx_menu.item_count; i++) {
    int item_h = 24;
    if (ctx_menu.items[i].separator) item_h += 8;
    
    if (my >= item_y && my < item_y + 22) {
      if (ctx_menu.items[i].enabled && ctx_menu.items[i].action) {
        ctx_menu.items[i].action(ctx_menu.context);
      }
      desktop_hide_context_menu();
      return 1;
    }
    
    item_y += item_h;
  }
  
  return 1;
}

/* Update hover */
void desktop_context_menu_hover(int mx, int my) {
  if (!ctx_menu.visible) return;
  
  ctx_menu.hover_index = -1;
  
  if (mx < ctx_menu.x || mx >= ctx_menu.x + ctx_menu.width ||
      my < ctx_menu.y || my >= ctx_menu.y + ctx_menu.height) {
    return;
  }
  
  int item_y = ctx_menu.y + 4;
  for (int i = 0; i < ctx_menu.item_count; i++) {
    if (my >= item_y && my < item_y + 22) {
      ctx_menu.hover_index = i;
      return;
    }
    item_y += 24;
    if (ctx_menu.items[i].separator) item_y += 8;
  }
}

/* Handle desktop click */
int desktop_click(int mx, int my, int button) {
  /* First check context menu */
  if (ctx_menu.visible) {
    return desktop_context_menu_click(mx, my);
  }
  
  /* Check icon clicks */
  for (int i = 0; i < desktop_icon_count; i++) {
    dsk_icon_t *icon = &desktop_icons[i];
    if (mx >= icon->x && mx < icon->x + DESKTOP_ICON_SIZE &&
        my >= icon->y && my < icon->y + DESKTOP_ICON_SIZE + 16) {
      
      if (button == 1) { /* Left click */
        desktop_selected = i;
        return 1;
      } else if (button == 2) { /* Right click */
        desktop_selected = i;
        desktop_show_context_menu(mx, my, 1);
        return 1;
      }
    }
  }
  
  /* Click on empty desktop */
  if (button == 1) {
    desktop_selected = -1;
    return 0;
  } else if (button == 2) {
    desktop_selected = -1;
    desktop_show_context_menu(mx, my, 0);
    return 1;
  }
  
  return 0;
}

/* Initialize desktop */
void desktop_init(void) {
  /* Ensure Desktop directory exists */
  vfs_mkdir(DESKTOP_PATH);
  desktop_refresh();
}

/* Is context menu visible? */
int desktop_menu_visible(void) {
  return ctx_menu.visible;
}
