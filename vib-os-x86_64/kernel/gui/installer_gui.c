#include "../include/blockdev.h"
#include "../include/gpt.h"
#include "../include/gui.h"
#include "../include/installer.h"
#include "../include/installer_gui.h"
#include "../include/ps2.h"
#include "../include/string.h"

#define UI_SELECT 0
#define UI_REVIEW 1
#define UI_CONFIRM 2
#define UI_RUNNING 3
#define UI_SUCCESS 4
#define UI_ERROR 5

#define UI_BASE 0x1E1E2E
#define UI_SURFACE 0x313244
#define UI_TITLE 0x181825
#define UI_TEXT 0xCDD6F4
#define UI_SUBTEXT 0xA6ADC8
#define UI_BLUE 0x89B4FA
#define UI_GREEN 0xA6E3A1
#define UI_YELLOW 0xF9E2AF
#define UI_RED 0xF38BA8
#define UI_TEAL 0x94E2D5

static struct {
  int visible;
  int step;
  int selected_disk;
  int acknowledged;
  int result;
  int existing_esp;
  const char *phase;
} installer_ui;

typedef struct {
  int x, y, width, height, scale, margin;
  int title_height, list_top, row_height;
  int button_y, button_height, button_width;
  int left_button_x, right_button_x;
  int checkbox_y;
} installer_layout_t;

static int ui_scale_value(int value) {
  int scale = ui_scale > 0 ? ui_scale : 1;
  return value * scale;
}

static installer_layout_t installer_layout(void) {
  installer_layout_t l;
  int max_width, max_height, list_bottom, list_space, count;
  l.scale = ui_scale > 0 ? ui_scale : 1;
  l.margin = 24 * l.scale;
  max_width = (int)screen_width - 32 * l.scale;
  max_height = (int)screen_height - 28 * l.scale - 70 * l.scale - 24 * l.scale;
  if (max_width < 300 * l.scale) max_width = (int)screen_width - 8;
  if (max_height < 300 * l.scale) max_height = (int)screen_height - 8;
  l.width = 760 * l.scale;
  if (l.width > max_width) l.width = max_width;
  l.height = 620 * l.scale;
  if (l.height > max_height) l.height = max_height;
  if (l.width < 240) l.width = 240;
  if (l.height < 240) l.height = 240;
  l.x = ((int)screen_width - l.width) / 2;
  l.y = ((int)screen_height - l.height) / 2;
  if (l.y < 4 * l.scale) l.y = 4 * l.scale;
  l.title_height = 42 * l.scale;
  l.button_height = 40 * l.scale;
  l.button_width = 190 * l.scale;
  if (l.button_width * 2 + 3 * l.margin > l.width)
    l.button_width = (l.width - 3 * l.margin) / 2;
  if (l.button_width < 72 * l.scale) l.button_width = 72 * l.scale;
  l.button_y = l.y + l.height - 60 * l.scale;
  l.right_button_x = l.x + l.width - l.margin - l.button_width;
  l.left_button_x = l.right_button_x - l.button_width - 12 * l.scale;
  l.checkbox_y = l.button_y - 58 * l.scale;
  l.list_top = l.y + 112 * l.scale;
  list_bottom = l.button_y - 18 * l.scale;
  list_space = list_bottom - l.list_top;
  count = (int)blockdev_count();
  if (count > BLOCKDEV_MAX_DEVICES) count = BLOCKDEV_MAX_DEVICES;
  if (count > 0) {
    l.row_height = list_space / count;
    if (l.row_height > 54 * l.scale) l.row_height = 54 * l.scale;
    if (l.row_height < 28 * l.scale) l.row_height = 28 * l.scale;
  } else {
    l.row_height = 48 * l.scale;
  }
  return l;
}

static int disk_is_installable(uint32_t index) {
  blockdev_t *device = blockdev_get(index);
  return device && device->read && device->write && device->sector_size == 512 &&
         device->sector_count >= 70000ULL;
}

static void disk_line(uint32_t index, blockdev_t *device, char *out,
                      size_t out_size) {
  char index_text[16], size_text[24];
  uint64_t mib;
  utoa(index, index_text, 10);
  if (device->sector_size > 0 && device->sector_size <= 1048576U &&
      1048576U % device->sector_size == 0)
    mib = device->sector_count / (1048576U / device->sector_size);
  else
    mib = device->sector_count / 2048ULL;
  utoa(mib, size_text, 10);
  snprintf(out, out_size, "Disk %s  |  %s  |  %s MiB", index_text,
           device->model, size_text);
}

static void draw_button(const installer_layout_t *l, int x, const char *label,
                        uint32_t color, int enabled) {
  int y = l->button_y;
  int w = l->button_width;
  int h = l->button_height;
  uint32_t fill = enabled ? color : 0x45475A;
  int text_width = (int)strlen(label) * 8 * l->scale;
  fb_fill_rect(x, y, w, h, fill);
  fb_draw_rect(x, y, w, h, enabled ? 0xCDD6F4 : 0x6C7086);
  gui_draw_string(x + (w - text_width) / 2,
                  y + (h - 16 * l->scale) / 2, label,
                  enabled ? 0x11111B : 0xA6ADC8);
}

static void draw_wrapped_line(int x, int y, const char *text, uint32_t color,
                              int max_chars) {
  char line[128];
  size_t length = strlen(text);
  size_t offset = 0;
  int line_height = ui_scale_value(22);
  if (max_chars < 1) max_chars = 1;
  if (max_chars > (int)sizeof(line) - 1) max_chars = sizeof(line) - 1;
  while (offset < length) {
    size_t take = length - offset;
    if (take > (size_t)max_chars) {
      take = (size_t)max_chars;
      while (take > 0 && text[offset + take] != ' ') --take;
      if (take == 0) take = (size_t)max_chars;
    }
    memcpy(line, text + offset, take);
    line[take] = '\0';
    gui_draw_string(x, y, line, color);
    y += line_height;
    offset += take;
    while (text[offset] == ' ') ++offset;
  }
}

static void draw_header(const installer_layout_t *l, const char *title) {
  int x = l->x, y = l->y, w = l->width, h = l->height;
  int scale = l->scale;
  fb_fill_rect(x + 6 * scale, y + 6 * scale, w, h, 0x11111B);
  fb_fill_rect(x, y, w, h, UI_BASE);
  fb_draw_rect(x, y, w, h, 0x585B70);
  fb_fill_rect(x, y, w, l->title_height, UI_TITLE);
  fb_fill_rect(x + 14 * scale, y + 12 * scale, 14 * scale, 14 * scale,
               0xF38BA8);
  gui_draw_string(x + 17 * scale, y + 11 * scale, "x", 0x7F1D1D);
  gui_draw_string(x + 42 * scale, y + 12 * scale, title, UI_TEXT);
}

static void draw_selection(const installer_layout_t *l) {
  int x = l->x + l->margin;
  int right = l->x + l->width - l->margin;
  int count = (int)blockdev_count();
  int available_rows = (l->button_y - 18 * l->scale - l->list_top) /
                       (l->row_height > 0 ? l->row_height : 1);
  if (count > BLOCKDEV_MAX_DEVICES) count = BLOCKDEV_MAX_DEVICES;
  gui_draw_string(x, l->y + 58 * l->scale,
                  "Choose the SATA disk to install Vib-OS on.", UI_TEXT);
  gui_draw_string(x, l->y + 82 * l->scale,
                  "Selection and review are read-only; nothing is changed yet.",
                  UI_SUBTEXT);
  if (!installer_payload_ready()) {
    gui_draw_string(x, l->y + 94 * l->scale,
                    "Installer boot files are missing from this live image.", UI_RED);
  }
  if (count == 0) {
    gui_draw_string(x, l->list_top, "No AHCI SATA disks were detected.", UI_YELLOW);
  }
  if (available_rows > count) available_rows = count;
  for (int i = 0; i < available_rows; ++i) {
    int row_y = l->list_top + i * l->row_height;
    blockdev_t *device = blockdev_get((uint32_t)i);
    int supported = disk_is_installable((uint32_t)i);
    char line[144];
    if (!device) continue;
    if (installer_ui.selected_disk == i)
      fb_fill_rect(x - 4 * l->scale, row_y, right - x + 8 * l->scale,
                   l->row_height - 4 * l->scale, 0x3B4261);
    else
      fb_fill_rect(x - 4 * l->scale, row_y, right - x + 8 * l->scale,
                   l->row_height - 4 * l->scale, UI_SURFACE);
    fb_draw_rect(x - 4 * l->scale, row_y, right - x + 8 * l->scale,
                 l->row_height - 4 * l->scale,
                 supported ? 0x585B70 : 0x6C7086);
    disk_line((uint32_t)i, device, line, sizeof(line));
    gui_draw_string(x + 8 * l->scale, row_y + 8 * l->scale, line,
                    supported ? UI_TEXT : UI_SUBTEXT);
    if (!supported)
      gui_draw_string(right - 128 * l->scale, row_y + 8 * l->scale,
                      "Unsupported", UI_YELLOW);
  }
  draw_button(l, l->left_button_x, "Cancel", UI_SURFACE, 1);
  draw_button(l, l->right_button_x, "Review disk", UI_BLUE,
              count > 0 && installer_payload_ready() &&
                  installer_ui.selected_disk >= 0 &&
                  disk_is_installable((uint32_t)installer_ui.selected_disk));
}

static void draw_review(const installer_layout_t *l) {
  int x = l->x + l->margin;
  int y = l->y + 68 * l->scale;
  int max_chars = (l->width - 2 * l->margin) / (8 * l->scale);
  blockdev_t *device = installer_ui.selected_disk < 0 ? NULL :
      blockdev_get((uint32_t)installer_ui.selected_disk);
  char line[144];
  if (!device) {
    gui_draw_string(x, y, "The selected disk is no longer available.", UI_RED);
  } else {
    disk_line((uint32_t)installer_ui.selected_disk, device, line, sizeof(line));
    gui_draw_string(x, y, "Review the selected target", UI_BLUE);
    y += 32 * l->scale;
    gui_draw_string(x, y, line, UI_TEXT);
    y += 36 * l->scale;
    draw_wrapped_line(x, y,
        "This will erase and repartition the entire selected disk. Existing partitions and files will be lost.",
        UI_RED, max_chars);
    y += 72 * l->scale;
    draw_wrapped_line(x, y,
        "Vib-OS will create one GPT EFI System Partition, format it as FAT32, and copy the UEFI boot loader, installed kernel, and boot configuration.",
        UI_TEXT, max_chars);
    y += 94 * l->scale;
    gui_draw_string(x, y,
        "The installed system boots in UEFI mode only; legacy BIOS is not installed.",
        UI_YELLOW);
    y += 28 * l->scale;
    gui_draw_string(x, y,
        "This is not a secure erase. Back up anything you need before continuing.",
        UI_SUBTEXT);
    y += 34 * l->scale;
    gui_draw_string(x, y, "No disk changes have been made.", UI_GREEN);
  }
  draw_button(l, l->left_button_x, "Back", UI_SURFACE, 1);
  draw_button(l, l->right_button_x, "Continue", UI_BLUE,
              device && disk_is_installable((uint32_t)installer_ui.selected_disk) &&
                  installer_payload_ready());
}

static void draw_confirmation(const installer_layout_t *l) {
  int x = l->x + l->margin;
  int y = l->y + 70 * l->scale;
  int box = 22 * l->scale;
  blockdev_t *device = installer_ui.selected_disk < 0 ? NULL :
      blockdev_get((uint32_t)installer_ui.selected_disk);
  char line[144];
  gui_draw_string(x, y, "Final confirmation", UI_RED);
  y += 38 * l->scale;
  if (device) {
    disk_line((uint32_t)installer_ui.selected_disk, device, line, sizeof(line));
    gui_draw_string(x, y, line, UI_TEXT);
    y += 40 * l->scale;
  }
  draw_wrapped_line(x, y,
      installer_ui.existing_esp
          ? "An existing GPT EFI System Partition was detected. The complete disk and all partitions will be repartitioned. Stop if this is not the correct target."
          : "The complete disk will be repartitioned. Stop here if you are not certain this is the correct target.",
      UI_YELLOW, (l->width - 2 * l->margin) / (8 * l->scale));
  fb_fill_rect(x, l->checkbox_y, box, box,
               installer_ui.acknowledged ? 0x166534 : UI_SURFACE);
  fb_draw_rect(x, l->checkbox_y, box, box,
               installer_ui.acknowledged ? UI_GREEN : 0xA6ADC8);
  if (installer_ui.acknowledged)
    gui_draw_string(x + 4 * l->scale, l->checkbox_y + 2 * l->scale, "OK",
                    UI_GREEN);
  gui_draw_string(x + box + 10 * l->scale, l->checkbox_y + 2 * l->scale,
                  "I understand this erases all data on the selected disk.", UI_TEXT);
  draw_button(l, l->left_button_x, "Cancel", UI_SURFACE, 1);
  draw_button(l, l->right_button_x, "Erase & install", UI_RED,
              installer_ui.acknowledged && device &&
                  disk_is_installable((uint32_t)installer_ui.selected_disk) &&
                  installer_payload_ready());
}

static void draw_running(const installer_layout_t *l) {
  int x = l->x + l->margin;
  int y = l->y + 100 * l->scale;
  int bar_width = l->width - 2 * l->margin;
  gui_draw_string(x, y, "Installing Vib-OS", UI_BLUE);
  y += 40 * l->scale;
  gui_draw_string(x, y, installer_ui.phase ? installer_ui.phase :
                  "Starting installation...", UI_TEXT);
  y += 46 * l->scale;
  fb_fill_rect(x, y, bar_width, 18 * l->scale, UI_SURFACE);
  fb_fill_rect(x, y, bar_width * 3 / 4, 18 * l->scale, UI_TEAL);
  gui_draw_string(x, y + 28 * l->scale,
      "Keep the computer powered on while disk operations run.", UI_SUBTEXT);
  gui_draw_string(x, y + 56 * l->scale,
      "The installer verifies copied boot files before reporting success.",
      UI_SUBTEXT);
}

static void draw_result(const installer_layout_t *l, int success) {
  int x = l->x + l->margin;
  int y = l->y + 100 * l->scale;
  if (success) {
    gui_draw_string(x, y, "Installation complete", UI_GREEN);
    y += 40 * l->scale;
    draw_wrapped_line(x, y,
        "The installed kernel, UEFI boot loader, and configuration were copied and read-back verified.",
        UI_TEXT, (l->width - 2 * l->margin) / (8 * l->scale));
    y += 70 * l->scale;
    gui_draw_string(x, y,
        "Reboot and choose the target disk's UEFI boot entry.", UI_SUBTEXT);
    y += 28 * l->scale;
    gui_draw_string(x, y,
        "Legacy BIOS boot is not installed. Remove the USB before rebooting.",
        UI_YELLOW);
  } else {
    gui_draw_string(x, y, "Installation failed", UI_RED);
    y += 40 * l->scale;
    draw_wrapped_line(x, y, installer_error_string(installer_ui.result),
        UI_TEXT, (l->width - 2 * l->margin) / (8 * l->scale));
    y += 70 * l->scale;
    gui_draw_string(x, y,
        "The disk may be partially modified and may not be bootable.", UI_YELLOW);
  }
  draw_button(l, l->left_button_x, "Close", UI_SURFACE, 1);
}

void installer_gui_open(void) {
  int count = (int)blockdev_count();
  installer_ui.visible = 1;
  installer_ui.step = UI_SELECT;
  installer_ui.selected_disk = count > 0 ? 0 : -1;
  installer_ui.acknowledged = 0;
  installer_ui.result = INSTALLER_OK;
  installer_ui.existing_esp = 0;
}

int installer_gui_is_open(void) {
  return installer_ui.visible;
}

void installer_gui_report_progress(const char *phase) {
  installer_ui.phase = phase;
  if (installer_ui.visible) gui_redraw_now();
}

void installer_gui_draw(void) {
  installer_layout_t l;
  if (!installer_ui.visible) return;
  l = installer_layout();
  draw_header(&l, "Vib-OS Installer");
  switch (installer_ui.step) {
  case UI_SELECT: draw_selection(&l); break;
  case UI_REVIEW: draw_review(&l); break;
  case UI_CONFIRM: draw_confirmation(&l); break;
  case UI_RUNNING: draw_running(&l); break;
  case UI_SUCCESS: draw_result(&l, 1); break;
  case UI_ERROR: draw_result(&l, 0); break;
  default: break;
  }
}

static int inside(int x, int y, int left, int top, int width, int height) {
  return x >= left && x < left + width && y >= top && y < top + height;
}

int installer_gui_handle_click(int x, int y, int button) {
  installer_layout_t l;
  int close_x, close_y;
  if (!installer_ui.visible) return 0;
  if (button != 1) return 1;
  l = installer_layout();
  close_x = l.x + 14 * l.scale;
  close_y = l.y + 10 * l.scale;
  if (inside(x, y, close_x, close_y, 18 * l.scale, 18 * l.scale)) {
    if (installer_ui.step != UI_RUNNING) installer_ui.visible = 0;
    return 1;
  }
  if (installer_ui.step == UI_SELECT) {
    int count = (int)blockdev_count();
    int available_rows = (l.button_y - 18 * l.scale - l.list_top) /
                         (l.row_height > 0 ? l.row_height : 1);
    if (count > BLOCKDEV_MAX_DEVICES) count = BLOCKDEV_MAX_DEVICES;
    if (available_rows > count) available_rows = count;
    for (int i = 0; i < available_rows; ++i) {
      int row_y = l.list_top + i * l.row_height;
      if (inside(x, y, l.x + l.margin - 4 * l.scale, row_y,
                 l.width - 2 * l.margin + 8 * l.scale,
                 l.row_height - 4 * l.scale)) {
        installer_ui.selected_disk = i;
        installer_ui.acknowledged = 0;
        return 1;
      }
    }
    if (inside(x, y, l.left_button_x, l.button_y, l.button_width,
               l.button_height)) {
      installer_ui.visible = 0;
      return 1;
    }
    if (inside(x, y, l.right_button_x, l.button_y, l.button_width,
               l.button_height) && installer_payload_ready() &&
        installer_ui.selected_disk >= 0 &&
        disk_is_installable((uint32_t)installer_ui.selected_disk)) {
      installer_ui.step = UI_REVIEW;
      return 1;
    }
  } else if (installer_ui.step == UI_REVIEW) {
    if (inside(x, y, l.left_button_x, l.button_y, l.button_width,
               l.button_height)) {
      installer_ui.step = UI_SELECT;
      return 1;
    }
    if (inside(x, y, l.right_button_x, l.button_y, l.button_width,
               l.button_height) && installer_ui.selected_disk >= 0 &&
        disk_is_installable((uint32_t)installer_ui.selected_disk) &&
        installer_payload_ready()) {
      gpt_partition_t esp;
      blockdev_t *device = blockdev_get((uint32_t)installer_ui.selected_disk);
      installer_ui.existing_esp = device && gpt_find_esp(device, &esp) == GPT_OK;
      installer_ui.acknowledged = 0;
      installer_ui.step = UI_CONFIRM;
      return 1;
    }
  } else if (installer_ui.step == UI_CONFIRM) {
    if (inside(x, y, l.x + l.margin, l.checkbox_y,
               l.width - 2 * l.margin, 28 * l.scale)) {
      installer_ui.acknowledged = !installer_ui.acknowledged;
      return 1;
    }
    if (inside(x, y, l.left_button_x, l.button_y, l.button_width,
               l.button_height)) {
      installer_ui.step = UI_REVIEW;
      installer_ui.acknowledged = 0;
      return 1;
    }
    if (inside(x, y, l.right_button_x, l.button_y, l.button_width,
               l.button_height) && installer_ui.acknowledged &&
        installer_ui.selected_disk >= 0 &&
        disk_is_installable((uint32_t)installer_ui.selected_disk) &&
        installer_payload_ready()) {
      installer_ui.step = UI_RUNNING;
      installer_ui.phase = "Checking target disk...";
      gui_redraw_now();
      installer_ui.result = installer_install_with_progress(
          (uint32_t)installer_ui.selected_disk, installer_gui_report_progress);
      installer_ui.step = installer_ui.result == INSTALLER_OK ? UI_SUCCESS : UI_ERROR;
      installer_ui.phase = NULL;
      gui_redraw_now();
      return 1;
    }
  } else if (installer_ui.step == UI_SUCCESS || installer_ui.step == UI_ERROR) {
    if (inside(x, y, l.left_button_x, l.button_y, l.button_width,
               l.button_height)) {
      installer_ui.visible = 0;
      installer_ui.step = UI_SELECT;
      installer_ui.acknowledged = 0;
      return 1;
    }
  }
  return 1;
}

int installer_gui_handle_key(int key) {
  if (!installer_ui.visible) return 0;
  if (key == KEY_ESC && installer_ui.step != UI_RUNNING) {
    installer_ui.visible = 0;
    installer_ui.step = UI_SELECT;
    installer_ui.acknowledged = 0;
  }
  return 1;
}
