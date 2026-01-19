/*
 * Vib-OS - Desktop Manager
 * 
 * macOS-style desktop with file/folder icons, right-click menus,
 * sorting, stacking, and full file operations.
 */

#include "types.h"
#include "printk.h"
#include "mm/kmalloc.h"
#include "fs/vfs.h"

/* External GUI functions */
extern void gui_draw_rect(int x, int y, int w, int h, uint32_t color);
extern void gui_draw_rect_outline(int x, int y, int w, int h, uint32_t color, int thickness);
extern void gui_draw_string(int x, int y, const char *str, uint32_t fg, uint32_t bg);
extern void gui_draw_char(int x, int y, char c, uint32_t fg, uint32_t bg);
extern void gui_draw_line(int x, int y, int x2, int y2, uint32_t color);

/* Forward declarations */
void desktop_sort_icons(void);
void desktop_arrange_icons(void);

/* ===================================================================== */
/* Desktop Constants */
/* ===================================================================== */

#define DESKTOP_PATH            "/Desktop"
#define DESKTOP_ICON_SIZE       64
#define DESKTOP_ICON_SPACING    90
#define DESKTOP_ICON_PADDING    20
#define DESKTOP_LABEL_HEIGHT    24
#define DESKTOP_MAX_ICONS       128
#define DESKTOP_START_X         20
#define DESKTOP_START_Y         50    /* Below menu bar */

/* Icon Types */
#define ICON_TYPE_FILE          0
#define ICON_TYPE_FOLDER        1
#define ICON_TYPE_IMAGE         2
#define ICON_TYPE_AUDIO         3
#define ICON_TYPE_TEXT          4
#define ICON_TYPE_APP           5

/* Sort Modes */
#define SORT_NAME               0
#define SORT_DATE               1
#define SORT_TYPE               2
#define SORT_SIZE               3

/* Colors */
#define COLOR_ICON_SELECTED     0x0078D4
#define COLOR_ICON_HOVER        0x3399FF
#define COLOR_MENU_BG           0x2D2D2D
#define COLOR_MENU_BORDER       0x5C5C5C
#define COLOR_MENU_HOVER        0x3C3C3C
#define COLOR_MENU_TEXT         0xFFFFFF
#define COLOR_LABEL_BG          0x00000080  /* Semi-transparent */

/* ===================================================================== */
/* Desktop Icon Structure */
/* ===================================================================== */

typedef struct desktop_icon {
    char name[64];
    char path[256];
    int type;
    int x, y;               /* Position on desktop */
    int grid_x, grid_y;     /* Grid slot */
    int selected;
    int stacked;            /* Part of a stack */
    uint64_t size;
    uint64_t mtime;
} desktop_icon_t;

/* ===================================================================== */
/* Context Menu Structure */
/* ===================================================================== */

typedef struct menu_item {
    char label[32];
    int enabled;
    int separator;          /* Draw separator after this item */
    void (*action)(void *ctx);
} menu_item_t;

typedef struct context_menu {
    int x, y;
    int width, height;
    int visible;
    int hover_index;
    menu_item_t items[16];
    int item_count;
    void *context;          /* Context data for actions */
} context_menu_t;

/* ===================================================================== */
/* Desktop State */
/* ===================================================================== */

static desktop_icon_t desktop_icons[DESKTOP_MAX_ICONS];
static int desktop_icon_count = 0;
static int desktop_sort_mode = SORT_NAME;
static int desktop_show_hidden = 0;
static int desktop_stacks_enabled = 0;
static int desktop_selected_count = 0;
static int desktop_last_click_x = 0;
static int desktop_last_click_y = 0;
static uint64_t desktop_last_click_time = 0;

/* Context menu */
static context_menu_t ctx_menu = {0};

/* Clipboard for copy/paste */
static char clipboard_path[256] = {0};
static int clipboard_is_cut = 0;

/* Dirty region tracking for performance */
typedef struct dirty_rect {
    int x, y, w, h;
    int valid;
} dirty_rect_t;

static dirty_rect_t dirty_regions[32];
static int dirty_count = 0;
static int full_redraw_needed = 1;

/* ===================================================================== */
/* Helper Functions */
/* ===================================================================== */

static int str_cmp(const char *a, const char *b)
{
    while (*a && *b && *a == *b) { a++; b++; }
    return (unsigned char)*a - (unsigned char)*b;
}

static int str_cmp_nocase(const char *a, const char *b)
{
    while (*a && *b) {
        char ca = (*a >= 'A' && *a <= 'Z') ? *a + 32 : *a;
        char cb = (*b >= 'A' && *b <= 'Z') ? *b + 32 : *b;
        if (ca != cb) return ca - cb;
        a++; b++;
    }
    return (unsigned char)*a - (unsigned char)*b;
}

static void str_copy(char *dst, const char *src, int max)
{
    int i = 0;
    while (src[i] && i < max - 1) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = '\0';
}

static int str_ends_with(const char *str, const char *suffix)
{
    int len_str = 0, len_suf = 0;
    while (str[len_str]) len_str++;
    while (suffix[len_suf]) len_suf++;
    if (len_suf > len_str) return 0;
    
    for (int i = 0; i < len_suf; i++) {
        char a = str[len_str - len_suf + i];
        char b = suffix[i];
        if (a >= 'A' && a <= 'Z') a += 32;
        if (b >= 'A' && b <= 'Z') b += 32;
        if (a != b) return 0;
    }
    return 1;
}

static int get_icon_type(const char *name, int is_dir)
{
    if (is_dir) return ICON_TYPE_FOLDER;
    if (str_ends_with(name, ".jpg") || str_ends_with(name, ".jpeg") ||
        str_ends_with(name, ".png") || str_ends_with(name, ".bmp"))
        return ICON_TYPE_IMAGE;
    if (str_ends_with(name, ".mp3") || str_ends_with(name, ".wav") ||
        str_ends_with(name, ".ogg"))
        return ICON_TYPE_AUDIO;
    if (str_ends_with(name, ".txt") || str_ends_with(name, ".md") ||
        str_ends_with(name, ".c") || str_ends_with(name, ".h"))
        return ICON_TYPE_TEXT;
    if (str_ends_with(name, ".app") || str_ends_with(name, ".exe"))
        return ICON_TYPE_APP;
    return ICON_TYPE_FILE;
}

/* ===================================================================== */
/* Dirty Region Tracking */
/* ===================================================================== */

void desktop_mark_dirty(int x, int y, int w, int h)
{
    if (dirty_count < 32) {
        dirty_regions[dirty_count].x = x;
        dirty_regions[dirty_count].y = y;
        dirty_regions[dirty_count].w = w;
        dirty_regions[dirty_count].h = h;
        dirty_regions[dirty_count].valid = 1;
        dirty_count++;
    } else {
        /* Too many dirty regions, trigger full redraw */
        full_redraw_needed = 1;
    }
}

void desktop_mark_full_redraw(void)
{
    full_redraw_needed = 1;
    dirty_count = 0;
}

int desktop_needs_redraw(void)
{
    return full_redraw_needed || dirty_count > 0;
}

void desktop_clear_dirty(void)
{
    dirty_count = 0;
    full_redraw_needed = 0;
}

/* ===================================================================== */
/* Desktop Icon Drawing */
/* ===================================================================== */

static void draw_folder_icon(int x, int y, int size, uint32_t color)
{
    int w = size;
    int h = size * 3 / 4;
    int tab_w = w / 3;
    int tab_h = h / 6;
    int y_start = y + (size - h) / 2;
    
    /* Folder tab */
    gui_draw_rect(x, y_start, tab_w, tab_h, color);
    
    /* Folder body */
    gui_draw_rect(x, y_start + tab_h - 2, w, h - tab_h + 2, color);
    
    /* Darker front */
    gui_draw_rect(x + 2, y_start + tab_h + h/4, w - 4, h/2, 
                  (color & 0xFEFEFE) >> 1);
}

static void draw_file_icon(int x, int y, int size, uint32_t color)
{
    int w = size * 3 / 4;
    int h = size;
    int fold = size / 4;
    int x_start = x + (size - w) / 2;
    
    /* Main document */
    gui_draw_rect(x_start, y, w - fold, h, color);
    gui_draw_rect(x_start + w - fold, y + fold, fold, h - fold, color);
    
    /* Folded corner */
    gui_draw_rect(x_start + w - fold, y, fold, fold, 
                  (color & 0xFEFEFE) >> 1);
    
    /* Lines on document */
    uint32_t line_color = (color & 0xFEFEFE) >> 1;
    for (int i = 0; i < 4; i++) {
        int ly = y + h/3 + i * (h/8);
        gui_draw_rect(x_start + 4, ly, w - fold - 8, 2, line_color);
    }
}

static void draw_image_icon(int x, int y, int size)
{
    /* Blue background with mountain/sun */
    gui_draw_rect(x + 4, y + 4, size - 8, size - 8, 0x87CEEB);
    
    /* Sun */
    gui_draw_rect(x + size - 20, y + 10, 10, 10, 0xFFD700);
    
    /* Mountain */
    for (int i = 0; i < 20; i++) {
        int mw = i * 2;
        gui_draw_rect(x + size/2 - mw/2, y + size - 15 - i, mw, 1, 0x228B22);
    }
}

static void draw_audio_icon(int x, int y, int size)
{
    /* Music note */
    int cx = x + size / 2;
    int cy = y + size / 2;
    
    /* Note head */
    gui_draw_rect(cx - 8, cy + 10, 12, 8, 0xFF6B6B);
    gui_draw_rect(cx + 4, cy + 5, 12, 8, 0xFF6B6B);
    
    /* Note stem */
    gui_draw_rect(cx + 2, cy - 15, 3, 28, 0x333333);
    gui_draw_rect(cx + 14, cy - 20, 3, 28, 0x333333);
    
    /* Flag */
    gui_draw_rect(cx + 5, cy - 15, 12, 3, 0x333333);
}

static void draw_desktop_icon(desktop_icon_t *icon)
{
    int x = icon->x;
    int y = icon->y;
    
    /* Selection highlight */
    if (icon->selected) {
        gui_draw_rect(x - 4, y - 4, DESKTOP_ICON_SIZE + 8, 
                      DESKTOP_ICON_SIZE + DESKTOP_LABEL_HEIGHT + 8,
                      COLOR_ICON_SELECTED);
    }
    
    /* Draw icon based on type */
    switch (icon->type) {
        case ICON_TYPE_FOLDER:
            draw_folder_icon(x, y, DESKTOP_ICON_SIZE, 0x4FC3F7);
            break;
        case ICON_TYPE_IMAGE:
            draw_image_icon(x, y, DESKTOP_ICON_SIZE);
            break;
        case ICON_TYPE_AUDIO:
            draw_audio_icon(x, y, DESKTOP_ICON_SIZE);
            break;
        case ICON_TYPE_TEXT:
            draw_file_icon(x, y, DESKTOP_ICON_SIZE, 0xFFFFFF);
            break;
        default:
            draw_file_icon(x, y, DESKTOP_ICON_SIZE, 0xE0E0E0);
            break;
    }
    
    /* Draw label with background */
    int label_x = x + DESKTOP_ICON_SIZE / 2;
    int label_y = y + DESKTOP_ICON_SIZE + 4;
    
    /* Truncate name if too long */
    char display_name[16];
    int len = 0;
    while (icon->name[len] && len < 12) {
        display_name[len] = icon->name[len];
        len++;
    }
    if (icon->name[len]) {
        display_name[len++] = '.';
        display_name[len++] = '.';
    }
    display_name[len] = '\0';
    
    /* Center label */
    int text_width = len * 8;
    int text_x = label_x - text_width / 2;
    
    /* Label background */
    gui_draw_rect(text_x - 2, label_y, text_width + 4, 14, 0x000000);
    
    /* Label text */
    gui_draw_string(text_x, label_y + 2, display_name, 0xFFFFFF, 0x000000);
}

/* ===================================================================== */
/* Context Menu */
/* ===================================================================== */

static void menu_action_open(void *ctx);
static void menu_action_new_folder(void *ctx);
static void menu_action_new_file(void *ctx);
static void menu_action_rename(void *ctx);
static void menu_action_delete(void *ctx);
static void menu_action_copy(void *ctx);
static void menu_action_cut(void *ctx);
static void menu_action_paste(void *ctx);
static void menu_action_sort_name(void *ctx);
static void menu_action_sort_date(void *ctx);
static void menu_action_sort_type(void *ctx);
static void menu_action_refresh(void *ctx);

static void ctx_menu_add_item(const char *label, void (*action)(void *), int enabled)
{
    if (ctx_menu.item_count >= 16) return;
    menu_item_t *item = &ctx_menu.items[ctx_menu.item_count++];
    str_copy(item->label, label, 32);
    item->action = action;
    item->enabled = enabled;
    item->separator = 0;
}

static void ctx_menu_add_separator(void)
{
    if (ctx_menu.item_count > 0) {
        ctx_menu.items[ctx_menu.item_count - 1].separator = 1;
    }
}

void desktop_show_context_menu(int x, int y, int on_icon)
{
    ctx_menu.item_count = 0;
    ctx_menu.x = x;
    ctx_menu.y = y;
    ctx_menu.hover_index = -1;
    ctx_menu.visible = 1;
    
    if (on_icon) {
        /* Context menu for selected icon(s) */
        ctx_menu_add_item("Open", menu_action_open, 1);
        ctx_menu_add_separator();
        ctx_menu_add_item("Rename", menu_action_rename, desktop_selected_count == 1);
        ctx_menu_add_item("Delete", menu_action_delete, 1);
        ctx_menu_add_separator();
        ctx_menu_add_item("Copy", menu_action_copy, 1);
        ctx_menu_add_item("Cut", menu_action_cut, 1);
    } else {
        /* Context menu for desktop background */
        ctx_menu_add_item("New Folder", menu_action_new_folder, 1);
        ctx_menu_add_item("New File", menu_action_new_file, 1);
        ctx_menu_add_separator();
        ctx_menu_add_item("Paste", menu_action_paste, clipboard_path[0] != '\0');
        ctx_menu_add_separator();
        ctx_menu_add_item("Sort by Name", menu_action_sort_name, 1);
        ctx_menu_add_item("Sort by Date", menu_action_sort_date, 1);
        ctx_menu_add_item("Sort by Type", menu_action_sort_type, 1);
        ctx_menu_add_separator();
        ctx_menu_add_item("Refresh", menu_action_refresh, 1);
    }
    
    /* Calculate menu size */
    ctx_menu.width = 160;
    ctx_menu.height = ctx_menu.item_count * 24 + 8;
    
    /* Add space for separators */
    for (int i = 0; i < ctx_menu.item_count; i++) {
        if (ctx_menu.items[i].separator) {
            ctx_menu.height += 8;
        }
    }
    
    desktop_mark_dirty(x, y, ctx_menu.width + 4, ctx_menu.height + 4);
}

void desktop_hide_context_menu(void)
{
    if (ctx_menu.visible) {
        desktop_mark_dirty(ctx_menu.x, ctx_menu.y, 
                          ctx_menu.width + 4, ctx_menu.height + 4);
        ctx_menu.visible = 0;
    }
}

void draw_context_menu(void)
{
    if (!ctx_menu.visible) return;
    
    int x = ctx_menu.x;
    int y = ctx_menu.y;
    int w = ctx_menu.width;
    
    /* Shadow */
    gui_draw_rect(x + 4, y + 4, w, ctx_menu.height, 0x000000);
    
    /* Background */
    gui_draw_rect(x, y, w, ctx_menu.height, COLOR_MENU_BG);
    gui_draw_rect_outline(x, y, w, ctx_menu.height, COLOR_MENU_BORDER, 1);
    
    /* Items */
    int item_y = y + 4;
    for (int i = 0; i < ctx_menu.item_count; i++) {
        menu_item_t *item = &ctx_menu.items[i];
        
        /* Hover highlight */
        if (i == ctx_menu.hover_index && item->enabled) {
            gui_draw_rect(x + 2, item_y, w - 4, 22, COLOR_MENU_HOVER);
        }
        
        /* Text */
        uint32_t text_color = item->enabled ? COLOR_MENU_TEXT : 0x808080;
        gui_draw_string(x + 12, item_y + 4, item->label, text_color, 
                       (i == ctx_menu.hover_index && item->enabled) ? 
                       COLOR_MENU_HOVER : COLOR_MENU_BG);
        
        item_y += 24;
        
        /* Separator */
        if (item->separator) {
            gui_draw_line(x + 8, item_y, x + w - 8, item_y, 0x555555);
            item_y += 8;
        }
    }
}

int desktop_context_menu_click(int mx, int my)
{
    if (!ctx_menu.visible) return 0;
    
    /* Check if click is inside menu */
    if (mx < ctx_menu.x || mx >= ctx_menu.x + ctx_menu.width ||
        my < ctx_menu.y || my >= ctx_menu.y + ctx_menu.height) {
        desktop_hide_context_menu();
        return 1;  /* Consumed click to close menu */
    }
    
    /* Find clicked item */
    int item_y = ctx_menu.y + 4;
    for (int i = 0; i < ctx_menu.item_count; i++) {
        menu_item_t *item = &ctx_menu.items[i];
        
        if (my >= item_y && my < item_y + 24) {
            if (item->enabled && item->action) {
                item->action(ctx_menu.context);
            }
            desktop_hide_context_menu();
            return 1;
        }
        
        item_y += 24;
        if (item->separator) item_y += 8;
    }
    
    return 1;
}

int desktop_context_menu_hover(int mx, int my)
{
    if (!ctx_menu.visible) return 0;
    
    int old_hover = ctx_menu.hover_index;
    ctx_menu.hover_index = -1;
    
    if (mx >= ctx_menu.x && mx < ctx_menu.x + ctx_menu.width &&
        my >= ctx_menu.y && my < ctx_menu.y + ctx_menu.height) {
        
        int item_y = ctx_menu.y + 4;
        for (int i = 0; i < ctx_menu.item_count; i++) {
            if (my >= item_y && my < item_y + 24) {
                ctx_menu.hover_index = i;
                break;
            }
            item_y += 24;
            if (ctx_menu.items[i].separator) item_y += 8;
        }
    }
    
    if (old_hover != ctx_menu.hover_index) {
        desktop_mark_dirty(ctx_menu.x, ctx_menu.y, 
                          ctx_menu.width + 4, ctx_menu.height + 4);
    }
    
    return ctx_menu.visible;
}

/* ===================================================================== */
/* Desktop File Operations */
/* ===================================================================== */

static int dir_scan_callback(void *ctx, const char *name, int len, 
                              loff_t offset, ino_t ino, unsigned type);

void desktop_refresh(void)
{
    /* Clear current icons */
    desktop_icon_count = 0;
    desktop_selected_count = 0;
    
    /* Ensure Desktop directory exists */
    struct file *dir = vfs_open(DESKTOP_PATH, O_RDONLY, 0);
    if (!dir) {
        /* Create Desktop directory */
        vfs_mkdir(DESKTOP_PATH, 0755);
        dir = vfs_open(DESKTOP_PATH, O_RDONLY, 0);
    }
    
    if (dir) {
        /* vfs_readdir(file, ctx, filldir) */
        vfs_readdir(dir, NULL, dir_scan_callback);
        vfs_close(dir);
    }
    
    /* Sort icons */
    desktop_sort_icons();
    
    /* Arrange in grid */
    desktop_arrange_icons();
    
    desktop_mark_full_redraw();
    printk(KERN_INFO "DESKTOP: Found %d items on desktop\n", desktop_icon_count);
}

static int dir_scan_callback(void *ctx, const char *name, int len, 
                              loff_t offset, ino_t ino, unsigned type)
{
    (void)ctx; (void)offset; (void)ino;
    
    /* Skip . and .. */
    if (name[0] == '.' && (len == 1 || (len == 2 && name[1] == '.'))) {
        return 0;
    }
    
    /* Skip hidden files unless enabled */
    if (!desktop_show_hidden && name[0] == '.') {
        return 0;
    }
    
    if (desktop_icon_count >= DESKTOP_MAX_ICONS) {
        return -1;  /* Stop scanning */
    }
    
    desktop_icon_t *icon = &desktop_icons[desktop_icon_count];
    
    /* Copy name */
    int i = 0;
    while (i < len && i < 63) {
        icon->name[i] = name[i];
        i++;
    }
    icon->name[i] = '\0';
    
    /* Build full path */
    str_copy(icon->path, DESKTOP_PATH, 256);
    int plen = 0;
    while (icon->path[plen]) plen++;
    icon->path[plen++] = '/';
    i = 0;
    while (i < len && plen < 255) {
        icon->path[plen++] = name[i++];
    }
    icon->path[plen] = '\0';
    
    /* Determine type */
    int is_dir = (type == 4);  /* DT_DIR */
    icon->type = get_icon_type(icon->name, is_dir);
    
    icon->selected = 0;
    icon->stacked = 0;
    icon->size = 0;
    icon->mtime = 0;
    
    desktop_icon_count++;
    return 0;
}

void desktop_sort_icons(void)
{
    /* Simple bubble sort */
    for (int i = 0; i < desktop_icon_count - 1; i++) {
        for (int j = 0; j < desktop_icon_count - i - 1; j++) {
            desktop_icon_t *a = &desktop_icons[j];
            desktop_icon_t *b = &desktop_icons[j + 1];
            int swap = 0;
            
            switch (desktop_sort_mode) {
                case SORT_NAME:
                    swap = str_cmp_nocase(a->name, b->name) > 0;
                    break;
                case SORT_TYPE:
                    if (a->type != b->type) {
                        swap = a->type > b->type;
                    } else {
                        swap = str_cmp_nocase(a->name, b->name) > 0;
                    }
                    break;
                case SORT_DATE:
                    swap = a->mtime < b->mtime;
                    break;
                case SORT_SIZE:
                    swap = a->size < b->size;
                    break;
            }
            
            if (swap) {
                desktop_icon_t tmp = *a;
                *a = *b;
                *b = tmp;
            }
        }
    }
}

void desktop_arrange_icons(void)
{
    /* Arrange icons in a grid from top-left */
    int x = DESKTOP_START_X;
    int y = DESKTOP_START_Y;
    int max_y = 600;  /* Approximate, should use screen height */
    
    for (int i = 0; i < desktop_icon_count; i++) {
        desktop_icons[i].x = x;
        desktop_icons[i].y = y;
        desktop_icons[i].grid_x = x / DESKTOP_ICON_SPACING;
        desktop_icons[i].grid_y = y / DESKTOP_ICON_SPACING;
        
        y += DESKTOP_ICON_SPACING;
        if (y + DESKTOP_ICON_SIZE + DESKTOP_LABEL_HEIGHT > max_y) {
            y = DESKTOP_START_Y;
            x += DESKTOP_ICON_SPACING;
        }
    }
}

/* ===================================================================== */
/* Menu Actions */
/* ===================================================================== */

static void menu_action_open(void *ctx)
{
    (void)ctx;
    for (int i = 0; i < desktop_icon_count; i++) {
        if (desktop_icons[i].selected) {
            printk(KERN_INFO "DESKTOP: Opening %s\n", desktop_icons[i].path);
            
            if (desktop_icons[i].type == ICON_TYPE_FOLDER) {
                /* Open in file manager */
                extern struct window *gui_create_file_manager_at(int x, int y, const char *path);
                /* gui_create_file_manager_at(200, 100, desktop_icons[i].path); */
            } else if (desktop_icons[i].type == ICON_TYPE_IMAGE) {
                extern void gui_open_image_viewer(const char *path);
                gui_open_image_viewer(desktop_icons[i].path);
            } else if (desktop_icons[i].type == ICON_TYPE_TEXT) {
                extern void gui_open_notepad(const char *path);
                gui_open_notepad(desktop_icons[i].path);
            } else if (desktop_icons[i].type == ICON_TYPE_AUDIO) {
                extern void gui_play_mp3_file(const char *path);
                /* gui_play_mp3_file(desktop_icons[i].path); */
            }
        }
    }
}

static void menu_action_new_folder(void *ctx)
{
    (void)ctx;
    static int folder_num = 1;
    char name[64];
    char path[256];
    
    /* Generate unique name */
    while (1) {
        /* Build name */
        char num_str[16];
        int n = folder_num++;
        int idx = 0;
        if (n == 0) {
            num_str[idx++] = '0';
        } else {
            char tmp[16];
            int ti = 0;
            while (n > 0) {
                tmp[ti++] = '0' + (n % 10);
                n /= 10;
            }
            while (ti > 0) {
                num_str[idx++] = tmp[--ti];
            }
        }
        num_str[idx] = '\0';
        
        str_copy(name, "New Folder ", 64);
        int i = 11;
        int j = 0;
        while (num_str[j] && i < 63) {
            name[i++] = num_str[j++];
        }
        name[i] = '\0';
        
        /* Build path */
        str_copy(path, DESKTOP_PATH, 256);
        int plen = 0;
        while (path[plen]) plen++;
        path[plen++] = '/';
        i = 0;
        while (name[i] && plen < 255) {
            path[plen++] = name[i++];
        }
        path[plen] = '\0';
        
        /* Check if exists */
        struct file *f = vfs_open(path, O_RDONLY, 0);
        if (!f) break;
        vfs_close(f);
    }
    
    /* Create folder */
    if (vfs_mkdir(path, 0755) == 0) {
        printk(KERN_INFO "DESKTOP: Created folder %s\n", path);
        desktop_refresh();
    }
}

static void menu_action_new_file(void *ctx)
{
    (void)ctx;
    static int file_num = 1;
    char name[64];
    char path[256];
    
    /* Generate unique name */
    while (1) {
        char num_str[16];
        int n = file_num++;
        int idx = 0;
        if (n == 0) {
            num_str[idx++] = '0';
        } else {
            char tmp[16];
            int ti = 0;
            while (n > 0) {
                tmp[ti++] = '0' + (n % 10);
                n /= 10;
            }
            while (ti > 0) {
                num_str[idx++] = tmp[--ti];
            }
        }
        num_str[idx] = '\0';
        
        str_copy(name, "New File ", 64);
        int i = 9;
        int j = 0;
        while (num_str[j] && i < 59) {
            name[i++] = num_str[j++];
        }
        name[i++] = '.';
        name[i++] = 't';
        name[i++] = 'x';
        name[i++] = 't';
        name[i] = '\0';
        
        str_copy(path, DESKTOP_PATH, 256);
        int plen = 0;
        while (path[plen]) plen++;
        path[plen++] = '/';
        i = 0;
        while (name[i] && plen < 255) {
            path[plen++] = name[i++];
        }
        path[plen] = '\0';
        
        struct file *f = vfs_open(path, O_RDONLY, 0);
        if (!f) break;
        vfs_close(f);
    }
    
    /* Create file */
    struct file *f = vfs_open(path, O_CREAT | O_WRONLY, 0644);
    if (f) {
        vfs_close(f);
        printk(KERN_INFO "DESKTOP: Created file %s\n", path);
        desktop_refresh();
    }
}

static void menu_action_rename(void *ctx)
{
    (void)ctx;
    /* Find selected icon */
    for (int i = 0; i < desktop_icon_count; i++) {
        if (desktop_icons[i].selected) {
            /* TODO: Open rename dialog */
            printk(KERN_INFO "DESKTOP: Rename %s\n", desktop_icons[i].name);
            break;
        }
    }
}

static void menu_action_delete(void *ctx)
{
    (void)ctx;
    for (int i = 0; i < desktop_icon_count; i++) {
        if (desktop_icons[i].selected) {
            printk(KERN_INFO "DESKTOP: Deleting %s\n", desktop_icons[i].path);
            vfs_unlink(desktop_icons[i].path);
        }
    }
    desktop_refresh();
}

static void menu_action_copy(void *ctx)
{
    (void)ctx;
    for (int i = 0; i < desktop_icon_count; i++) {
        if (desktop_icons[i].selected) {
            str_copy(clipboard_path, desktop_icons[i].path, 256);
            clipboard_is_cut = 0;
            printk(KERN_INFO "DESKTOP: Copied %s\n", clipboard_path);
            break;  /* Only copy first selected */
        }
    }
}

static void menu_action_cut(void *ctx)
{
    (void)ctx;
    for (int i = 0; i < desktop_icon_count; i++) {
        if (desktop_icons[i].selected) {
            str_copy(clipboard_path, desktop_icons[i].path, 256);
            clipboard_is_cut = 1;
            printk(KERN_INFO "DESKTOP: Cut %s\n", clipboard_path);
            break;
        }
    }
}

static void menu_action_paste(void *ctx)
{
    (void)ctx;
    if (clipboard_path[0] == '\0') return;
    
    /* Extract filename from path */
    const char *filename = clipboard_path;
    for (const char *p = clipboard_path; *p; p++) {
        if (*p == '/') filename = p + 1;
    }
    
    /* Build destination path */
    char dest[256];
    str_copy(dest, DESKTOP_PATH, 256);
    int plen = 0;
    while (dest[plen]) plen++;
    dest[plen++] = '/';
    int i = 0;
    while (filename[i] && plen < 255) {
        dest[plen++] = filename[i++];
    }
    dest[plen] = '\0';
    
    printk(KERN_INFO "DESKTOP: Paste %s -> %s\n", clipboard_path, dest);
    
    /* TODO: Implement actual file copy/move */
    
    if (clipboard_is_cut) {
        /* vfs_rename(clipboard_path, dest); */
        clipboard_path[0] = '\0';
    }
    
    desktop_refresh();
}

static void menu_action_sort_name(void *ctx)
{
    (void)ctx;
    desktop_sort_mode = SORT_NAME;
    desktop_sort_icons();
    desktop_arrange_icons();
    desktop_mark_full_redraw();
}

static void menu_action_sort_date(void *ctx)
{
    (void)ctx;
    desktop_sort_mode = SORT_DATE;
    desktop_sort_icons();
    desktop_arrange_icons();
    desktop_mark_full_redraw();
}

static void menu_action_sort_type(void *ctx)
{
    (void)ctx;
    desktop_sort_mode = SORT_TYPE;
    desktop_sort_icons();
    desktop_arrange_icons();
    desktop_mark_full_redraw();
}

static void menu_action_refresh(void *ctx)
{
    (void)ctx;
    desktop_refresh();
}

/* ===================================================================== */
/* Desktop Event Handling */
/* ===================================================================== */

desktop_icon_t *desktop_icon_at(int x, int y)
{
    for (int i = 0; i < desktop_icon_count; i++) {
        desktop_icon_t *icon = &desktop_icons[i];
        if (x >= icon->x && x < icon->x + DESKTOP_ICON_SIZE &&
            y >= icon->y && y < icon->y + DESKTOP_ICON_SIZE + DESKTOP_LABEL_HEIGHT) {
            return icon;
        }
    }
    return NULL;
}

void desktop_select_icon(desktop_icon_t *icon, int add_to_selection)
{
    if (!add_to_selection) {
        /* Clear other selections */
        for (int i = 0; i < desktop_icon_count; i++) {
            if (desktop_icons[i].selected) {
                desktop_icons[i].selected = 0;
                desktop_mark_dirty(desktop_icons[i].x - 4, desktop_icons[i].y - 4,
                                  DESKTOP_ICON_SIZE + 8, 
                                  DESKTOP_ICON_SIZE + DESKTOP_LABEL_HEIGHT + 8);
            }
        }
        desktop_selected_count = 0;
    }
    
    if (icon && !icon->selected) {
        icon->selected = 1;
        desktop_selected_count++;
        desktop_mark_dirty(icon->x - 4, icon->y - 4,
                          DESKTOP_ICON_SIZE + 8,
                          DESKTOP_ICON_SIZE + DESKTOP_LABEL_HEIGHT + 8);
    }
}

void desktop_clear_selection(void)
{
    for (int i = 0; i < desktop_icon_count; i++) {
        if (desktop_icons[i].selected) {
            desktop_icons[i].selected = 0;
            desktop_mark_dirty(desktop_icons[i].x - 4, desktop_icons[i].y - 4,
                              DESKTOP_ICON_SIZE + 8,
                              DESKTOP_ICON_SIZE + DESKTOP_LABEL_HEIGHT + 8);
        }
    }
    desktop_selected_count = 0;
}

int desktop_handle_click(int x, int y, int button, int shift_held)
{
    /* Right click - context menu */
    if (button == 2) {  /* Right button */
        desktop_icon_t *icon = desktop_icon_at(x, y);
        if (icon) {
            if (!icon->selected) {
                desktop_select_icon(icon, 0);
            }
            desktop_show_context_menu(x, y, 1);
        } else {
            desktop_clear_selection();
            desktop_show_context_menu(x, y, 0);
        }
        return 1;
    }
    
    /* Left click */
    if (button == 1) {
        /* Check context menu first */
        if (ctx_menu.visible) {
            return desktop_context_menu_click(x, y);
        }
        
        desktop_icon_t *icon = desktop_icon_at(x, y);
        
        if (icon) {
            /* Check for double-click */
            /* TODO: Implement proper timing */
            
            desktop_select_icon(icon, shift_held);
        } else {
            /* Clicked on empty space */
            desktop_clear_selection();
        }
        
        return 1;
    }
    
    return 0;
}

int desktop_handle_double_click(int x, int y)
{
    desktop_icon_t *icon = desktop_icon_at(x, y);
    if (icon) {
        menu_action_open(NULL);
        return 1;
    }
    return 0;
}

/* ===================================================================== */
/* Desktop Drawing */
/* ===================================================================== */

void desktop_draw_icons(void)
{
    for (int i = 0; i < desktop_icon_count; i++) {
        draw_desktop_icon(&desktop_icons[i]);
    }
    
    /* Draw context menu on top */
    draw_context_menu();
}

/* ===================================================================== */
/* Initialization */
/* ===================================================================== */

void desktop_manager_init(void)
{
    printk(KERN_INFO "DESKTOP: Initializing desktop manager\n");
    
    desktop_icon_count = 0;
    desktop_selected_count = 0;
    ctx_menu.visible = 0;
    clipboard_path[0] = '\0';
    
    /* Load desktop contents */
    desktop_refresh();
}

/* ===================================================================== */
/* Public Getters */
/* ===================================================================== */

int desktop_get_icon_count(void)
{
    return desktop_icon_count;
}

int desktop_is_context_menu_visible(void)
{
    return ctx_menu.visible;
}
