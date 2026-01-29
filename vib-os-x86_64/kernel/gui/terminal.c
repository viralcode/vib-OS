/*
 * VibCode x64 - VT100 Terminal Emulator
 */

#include "../include/gui.h"
#include "../include/kmalloc.h"
#include "../include/string.h"
#include "../include/vfs.h"

/* Terminal configuration */
#define TERM_COLS 80
#define TERM_ROWS 24
#define TERM_CHAR_W 8
#define TERM_CHAR_H 16
#define TERM_PADDING 4

/* Terminal colors (ANSI) */
static const uint32_t term_colors[16] = {
    0x1E1E2E, /* 0 - Black */
    0xF38BA8, /* 1 - Red */
    0xA6E3A1, /* 2 - Green */
    0xF9E2AF, /* 3 - Yellow */
    0x89B4FA, /* 4 - Blue */
    0xCBA6F7, /* 5 - Magenta */
    0x94E2D5, /* 6 - Cyan */
    0xCDD6F4, /* 7 - White */
    0x585B70, /* 8 - Bright Black */
    0xF38BA8, /* 9 - Bright Red */
    0xA6E3A1, /* 10 - Bright Green */
    0xF9E2AF, /* 11 - Bright Yellow */
    0x89B4FA, /* 12 - Bright Blue */
    0xCBA6F7, /* 13 - Bright Magenta */
    0x94E2D5, /* 14 - Bright Cyan */
    0xFFFFFF, /* 15 - Bright White */
};

/* Terminal state */
typedef struct terminal {
  char chars[TERM_ROWS * TERM_COLS];
  uint8_t fg_colors[TERM_ROWS * TERM_COLS];
  uint8_t bg_colors[TERM_ROWS * TERM_COLS];
  int cursor_x, cursor_y;
  int visible;
  int content_x, content_y;
  int width, height;
  uint8_t current_fg, current_bg;
  
  /* Escape sequence */
  int in_escape;
  char escape_buf[32];
  int escape_len;
  
  /* Input */
  char input_buf[256];
  int input_len;
  char cwd[256];
  
  /* History */
  char history[16][256];
  int history_count;
  int history_pos;
} terminal_t;

static terminal_t *active_term = NULL;

/* External functions */
extern void fb_fill_rect(int x, int y, int w, int h, uint32_t color);
extern void fb_put_pixel(int x, int y, uint32_t color);
extern const uint8_t font_data[256][16];

/* Draw character */
static void term_draw_char(int x, int y, char c, uint32_t fg, uint32_t bg) {
  fb_fill_rect(x, y, TERM_CHAR_W, TERM_CHAR_H, bg);
  if (c < 32 || c > 126) c = ' ';
  const uint8_t *glyph = font_data[(uint8_t)c];
  for (int row = 0; row < 16; row++) {
    uint8_t bits = glyph[row];
    for (int col = 0; col < 8; col++) {
      if (bits & (0x80 >> col)) {
        fb_put_pixel(x + col, y + row, fg);
      }
    }
  }
}

/* Clear line */
static void term_clear_line(terminal_t *term, int row) {
  for (int col = 0; col < TERM_COLS; col++) {
    int idx = row * TERM_COLS + col;
    term->chars[idx] = ' ';
    term->fg_colors[idx] = term->current_fg;
    term->bg_colors[idx] = term->current_bg;
  }
}

/* Scroll up */
static void term_scroll_up(terminal_t *term) {
  for (int row = 0; row < TERM_ROWS - 1; row++) {
    memcpy(&term->chars[row * TERM_COLS], &term->chars[(row + 1) * TERM_COLS], TERM_COLS);
    memcpy(&term->fg_colors[row * TERM_COLS], &term->fg_colors[(row + 1) * TERM_COLS], TERM_COLS);
    memcpy(&term->bg_colors[row * TERM_COLS], &term->bg_colors[(row + 1) * TERM_COLS], TERM_COLS);
  }
  term_clear_line(term, TERM_ROWS - 1);
}

/* Newline */
static void term_newline(terminal_t *term) {
  term->cursor_x = 0;
  term->cursor_y++;
  if (term->cursor_y >= TERM_ROWS) {
    term_scroll_up(term);
    term->cursor_y = TERM_ROWS - 1;
  }
}

/* Process escape sequence */
static void term_process_escape(terminal_t *term) {
  if (term->escape_len < 1) return;
  
  if (term->escape_buf[0] == '[') {
    char cmd = term->escape_buf[term->escape_len - 1];
    int params[8] = {0};
    int param_count = 0;
    int num = 0;
    int in_num = 0;
    
    for (int i = 1; i < term->escape_len - 1 && param_count < 8; i++) {
      char c = term->escape_buf[i];
      if (c >= '0' && c <= '9') {
        num = num * 10 + (c - '0');
        in_num = 1;
      } else if (c == ';') {
        if (in_num) params[param_count++] = num;
        num = 0;
        in_num = 0;
      }
    }
    if (in_num) params[param_count++] = num;
    
    switch (cmd) {
      case 'A': /* Cursor Up */
        term->cursor_y -= (params[0] > 0) ? params[0] : 1;
        if (term->cursor_y < 0) term->cursor_y = 0;
        break;
      case 'B': /* Cursor Down */
        term->cursor_y += (params[0] > 0) ? params[0] : 1;
        if (term->cursor_y >= TERM_ROWS) term->cursor_y = TERM_ROWS - 1;
        break;
      case 'C': /* Cursor Forward */
        term->cursor_x += (params[0] > 0) ? params[0] : 1;
        if (term->cursor_x >= TERM_COLS) term->cursor_x = TERM_COLS - 1;
        break;
      case 'D': /* Cursor Back */
        term->cursor_x -= (params[0] > 0) ? params[0] : 1;
        if (term->cursor_x < 0) term->cursor_x = 0;
        break;
      case 'H': case 'f': /* Cursor Position */
        term->cursor_y = (params[0] > 0) ? params[0] - 1 : 0;
        term->cursor_x = (param_count > 1 && params[1] > 0) ? params[1] - 1 : 0;
        if (term->cursor_y >= TERM_ROWS) term->cursor_y = TERM_ROWS - 1;
        if (term->cursor_x >= TERM_COLS) term->cursor_x = TERM_COLS - 1;
        break;
      case 'J': /* Erase Display */
        if (params[0] == 2) {
          for (int row = 0; row < TERM_ROWS; row++) term_clear_line(term, row);
          term->cursor_x = 0;
          term->cursor_y = 0;
        }
        break;
      case 'K': /* Erase Line */
        for (int col = term->cursor_x; col < TERM_COLS; col++) {
          term->chars[term->cursor_y * TERM_COLS + col] = ' ';
        }
        break;
      case 'm': /* SGR */
        for (int i = 0; i < param_count; i++) {
          int p = params[i];
          if (p == 0) { term->current_fg = 7; term->current_bg = 0; }
          else if (p >= 30 && p <= 37) term->current_fg = p - 30;
          else if (p >= 40 && p <= 47) term->current_bg = p - 40;
          else if (p >= 90 && p <= 97) term->current_fg = p - 90 + 8;
          else if (p >= 100 && p <= 107) term->current_bg = p - 100 + 8;
          else if (p == 1) term->current_fg |= 8; /* Bold = bright */
        }
        break;
    }
  }
  term->in_escape = 0;
  term->escape_len = 0;
}

/* Put character */
void term_putc(terminal_t *term, char c) {
  if (!term) return;
  
  if (term->in_escape) {
    term->escape_buf[term->escape_len++] = c;
    if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '~') {
      term_process_escape(term);
    } else if (term->escape_len >= 31) {
      term->in_escape = 0;
      term->escape_len = 0;
    }
    return;
  }
  
  switch (c) {
    case '\033': term->in_escape = 1; term->escape_len = 0; break;
    case '\n': term_newline(term); break;
    case '\r': term->cursor_x = 0; break;
    case '\b': if (term->cursor_x > 0) term->cursor_x--; break;
    case '\t': term->cursor_x = (term->cursor_x + 8) & ~7;
               if (term->cursor_x >= TERM_COLS) term_newline(term); break;
    default:
      if (c >= 32 && c < 127) {
        int idx = term->cursor_y * TERM_COLS + term->cursor_x;
        term->chars[idx] = c;
        term->fg_colors[idx] = term->current_fg;
        term->bg_colors[idx] = term->current_bg;
        term->cursor_x++;
        if (term->cursor_x >= TERM_COLS) term_newline(term);
      }
      break;
  }
}

/* Put string */
void term_puts_t(terminal_t *term, const char *str) {
  while (*str) term_putc(term, *str++);
}

/* Render terminal */
void term_render(terminal_t *term) {
  if (!term || !term->visible) return;
  
  int base_x = term->content_x + TERM_PADDING;
  int base_y = term->content_y + TERM_PADDING;
  
  /* Background */
  fb_fill_rect(term->content_x, term->content_y,
               TERM_COLS * TERM_CHAR_W + TERM_PADDING * 2,
               TERM_ROWS * TERM_CHAR_H + TERM_PADDING * 2, term_colors[0]);
  
  /* Characters */
  for (int row = 0; row < TERM_ROWS; row++) {
    for (int col = 0; col < TERM_COLS; col++) {
      int idx = row * TERM_COLS + col;
      term_draw_char(base_x + col * TERM_CHAR_W, base_y + row * TERM_CHAR_H,
                     term->chars[idx],
                     term_colors[term->fg_colors[idx] & 0xF],
                     term_colors[term->bg_colors[idx] & 0xF]);
    }
  }
  
  /* Cursor */
  static int blink = 0;
  blink++;
  if ((blink / 15) % 2 == 0) {
    int cx = base_x + term->cursor_x * TERM_CHAR_W;
    int cy = base_y + term->cursor_y * TERM_CHAR_H;
    fb_fill_rect(cx, cy + TERM_CHAR_H - 2, TERM_CHAR_W, 2, term_colors[7]);
  }
}

/* Execute command */
void term_execute(terminal_t *term, const char *cmd) {
  /* Skip whitespace */
  while (*cmd == ' ') cmd++;
  if (*cmd == '\0') return;
  
  /* Save to history */
  if (term->history_count < 16) {
    strncpy(term->history[term->history_count++], cmd, 255);
  }
  
  term_puts_t(term, "\n");
  
  if (strncmp(cmd, "help", 4) == 0) {
    term_puts_t(term, "\033[1;36mVib-OS Terminal v2.0\033[0m\n");
    term_puts_t(term, "\033[33mFile Commands:\033[0m\n");
    term_puts_t(term, "  ls        - List directory contents\n");
    term_puts_t(term, "  cd <dir>  - Change directory\n");
    term_puts_t(term, "  pwd       - Print working directory\n");
    term_puts_t(term, "  cat <f>   - Display file contents\n");
    term_puts_t(term, "  touch <f> - Create empty file\n");
    term_puts_t(term, "  mkdir <d> - Create directory\n");
    term_puts_t(term, "  rm <f>    - Remove file\n");
    term_puts_t(term, "\033[33mSystem:\033[0m\n");
    term_puts_t(term, "  neofetch  - System info\n");
    term_puts_t(term, "  uname     - Show OS info\n");
    term_puts_t(term, "  id        - Show user/group info\n");
    term_puts_t(term, "  hostname  - Show hostname\n");
    term_puts_t(term, "  history   - Show command history\n");
    term_puts_t(term, "  free      - Memory usage\n");
    term_puts_t(term, "  ps        - Process list\n");
    term_puts_t(term, "  clear     - Clear screen\n");
    term_puts_t(term, "  help      - This help message\n");
  } else if (strncmp(cmd, "clear", 5) == 0) {
    for (int row = 0; row < TERM_ROWS; row++) term_clear_line(term, row);
    term->cursor_x = 0;
    term->cursor_y = 0;
  } else if (strncmp(cmd, "ls", 2) == 0) {
    const char *path = term->cwd[0] ? term->cwd : "/";
    if (cmd[2] == ' ' && cmd[3]) path = cmd + 3;
    
    dirent_t entries[32];
    int count = vfs_readdir(path, entries, 32);
    if (count >= 0) {
      for (int i = 0; i < count; i++) {
        if (entries[i].type == 1) {
          term_puts_t(term, "\033[1;34m");
          term_puts_t(term, entries[i].name);
          term_puts_t(term, "/\033[0m  ");
        } else {
          term_puts_t(term, entries[i].name);
          term_puts_t(term, "  ");
        }
      }
      term_puts_t(term, "\n");
    } else {
      term_puts_t(term, "ls: cannot access directory\n");
    }
  } else if (strncmp(cmd, "pwd", 3) == 0) {
    term_puts_t(term, term->cwd[0] ? term->cwd : "/");
    term_puts_t(term, "\n");
  } else if (strncmp(cmd, "cd ", 3) == 0) {
    const char *path = cmd + 3;
    while (*path == ' ') path++;
    
    char target[256];
    if (path[0] == '/') {
      strncpy(target, path, 255);
    } else if (strcmp(path, "..") == 0) {
      strncpy(target, term->cwd, 255);
      int len = strlen(target);
      while (len > 1 && target[len-1] != '/') len--;
      if (len > 1) len--;
      target[len] = '\0';
      if (target[0] == '\0') strcpy(target, "/");
    } else {
      snprintf(target, 256, "%s/%s", term->cwd[0] ? term->cwd : "", path);
    }
    
    if (vfs_exists(target)) {
      strncpy(term->cwd, target, 255);
    } else {
      term_puts_t(term, "cd: no such directory: ");
      term_puts_t(term, path);
      term_puts_t(term, "\n");
    }
  } else if (strncmp(cmd, "cat ", 4) == 0) {
    const char *filename = cmd + 4;
    while (*filename == ' ') filename++;
    
    char path[256];
    if (filename[0] == '/') {
      strncpy(path, filename, 255);
    } else {
      snprintf(path, 256, "%s/%s", term->cwd[0] ? term->cwd : "", filename);
    }
    
    file_t *f = vfs_open(path, O_RDONLY);
    if (f) {
      char buf[512];
      ssize_t n;
      while ((n = vfs_read(f, buf, 511)) > 0) {
        buf[n] = '\0';
        term_puts_t(term, buf);
      }
      vfs_close(f);
      term_puts_t(term, "\n");
    } else {
      term_puts_t(term, "cat: ");
      term_puts_t(term, filename);
      term_puts_t(term, ": No such file\n");
    }
  } else if (strncmp(cmd, "touch ", 6) == 0) {
    const char *filename = cmd + 6;
    char path[256];
    snprintf(path, 256, "%s/%s", term->cwd[0] ? term->cwd : "", filename);
    if (vfs_create(path) >= 0) {
      term_puts_t(term, "Created: ");
      term_puts_t(term, filename);
      term_puts_t(term, "\n");
    } else {
      term_puts_t(term, "touch: failed\n");
    }
  } else if (strncmp(cmd, "mkdir ", 6) == 0) {
    const char *dirname = cmd + 6;
    char path[256];
    snprintf(path, 256, "%s/%s", term->cwd[0] ? term->cwd : "", dirname);
    if (vfs_mkdir(path) >= 0) {
      term_puts_t(term, "Created directory: ");
      term_puts_t(term, dirname);
      term_puts_t(term, "\n");
    } else {
      term_puts_t(term, "mkdir: failed\n");
    }
  } else if (strncmp(cmd, "rm ", 3) == 0) {
    const char *filename = cmd + 3;
    char path[256];
    snprintf(path, 256, "%s/%s", term->cwd[0] ? term->cwd : "", filename);
    if (vfs_delete(path) >= 0) {
      term_puts_t(term, "Removed: ");
      term_puts_t(term, filename);
      term_puts_t(term, "\n");
    } else {
      term_puts_t(term, "rm: failed\n");
    }
  } else if (strncmp(cmd, "echo ", 5) == 0) {
    term_puts_t(term, cmd + 5);
    term_puts_t(term, "\n");
  } else if (strncmp(cmd, "neofetch", 8) == 0) {
    term_puts_t(term, "\033[36m");
    term_puts_t(term, "       _  _         ___  ____  \n");
    term_puts_t(term, " __   _(_)| |__     / _ \\/ ___| \n");
    term_puts_t(term, " \\ \\ / / || '_ \\   | | | \\___ \\ \n");
    term_puts_t(term, "  \\ V /| || |_) |  | |_| |___) |\n");
    term_puts_t(term, "   \\_/ |_||_.__/    \\___/|____/ \n");
    term_puts_t(term, "\033[0m\n");
    term_puts_t(term, "\033[33mOS:\033[0m      Vib-OS 0.5.0\n");
    term_puts_t(term, "\033[33mHost:\033[0m    Bare Metal x86_64\n");
    term_puts_t(term, "\033[33mKernel:\033[0m  0.5.0-x86_64\n");
    term_puts_t(term, "\033[33mUptime:\033[0m  0 mins\n");
    term_puts_t(term, "\033[33mShell:\033[0m   vsh 1.0\n");
    term_puts_t(term, "\033[33mMemory:\033[0m  64 MB heap\n");
    term_puts_t(term, "\033[33mCPU:\033[0m     x86_64\n");
  } else if (strncmp(cmd, "uname", 5) == 0) {
    if (strstr(cmd, "-a")) {
      term_puts_t(term, "Vib-os 1.0.0 x86_64\n");
    } else {
      term_puts_t(term, "Vib-os\n");
    }
  } else if (strncmp(cmd, "free", 4) == 0) {
    term_puts_t(term, "              total        used        free\n");
    char buf[64];
    snprintf(buf, 64, "Mem:       %8lu  %8lu  %8lu\n", 
             (unsigned long)(64*1024*1024), 
             kmalloc_get_used(), 
             kmalloc_get_free());
    term_puts_t(term, buf);
  } else if (strncmp(cmd, "ps", 2) == 0) {
    term_puts_t(term, "  PID TTY          TIME CMD\n");
    term_puts_t(term, "    1 ?        00:00:00 kernel\n");
    term_puts_t(term, "    2 tty1     00:00:00 shell\n");
  } else if (strncmp(cmd, "history", 7) == 0) {
    for (int i = 0; i < term->history_count; i++) {
      char num[8];
      snprintf(num, 8, "%4d  ", i + 1);
      term_puts_t(term, num);
      term_puts_t(term, term->history[i]);
      term_puts_t(term, "\n");
    }
  } else if (strncmp(cmd, "whoami", 6) == 0) {
    term_puts_t(term, "root\n");
  } else if (strncmp(cmd, "id", 2) == 0) {
    term_puts_t(term, "uid=0(root) gid=0(root) groups=0(root)\n");
  } else if (strncmp(cmd, "hostname", 8) == 0) {
    term_puts_t(term, "vib-os\n");
  } else if (strncmp(cmd, "date", 4) == 0) {
    term_puts_t(term, "Thu Jan 23 00:00:00 UTC 2025\n");
  } else if (strncmp(cmd, "uptime", 6) == 0) {
    term_puts_t(term, " 00:00:00 up 0 min, 1 user\n");
  } else {
    term_puts_t(term, cmd);
    term_puts_t(term, ": command not found\n");
  }
}

/* Print prompt */
void term_prompt(terminal_t *term) {
  term_puts_t(term, "\033[32mvib-os\033[0m:\033[34m~\033[0m$ ");
}

/* Handle keyboard input */
void term_handle_key(terminal_t *term, int key) {
  if (!term) return;
  
  if (key == '\n' || key == '\r') {
    term->input_buf[term->input_len] = '\0';
    term_execute(term, term->input_buf);
    term->input_len = 0;
    term_prompt(term);
  } else if (key == '\b' || key == 127) {
    if (term->input_len > 0) {
      term->input_len--;
      term->cursor_x--;
      term->chars[term->cursor_y * TERM_COLS + term->cursor_x] = ' ';
    }
  } else if (key >= 32 && key < 127 && term->input_len < 255) {
    term->input_buf[term->input_len++] = (char)key;
    term_putc(term, (char)key);
  }
}

/* Create terminal */
terminal_t *term_create(int x, int y) {
  terminal_t *term = kzalloc(sizeof(terminal_t));
  if (!term) return NULL;
  
  term->content_x = x;
  term->content_y = y;
  term->width = TERM_COLS * TERM_CHAR_W + TERM_PADDING * 2;
  term->height = TERM_ROWS * TERM_CHAR_H + TERM_PADDING * 2;
  term->current_fg = 7;
  term->current_bg = 0;
  term->visible = 1;
  strcpy(term->cwd, "/home/user");
  
  for (int i = 0; i < TERM_ROWS * TERM_COLS; i++) {
    term->chars[i] = ' ';
    term->fg_colors[i] = 7;
    term->bg_colors[i] = 0;
  }
  
  /* Welcome message (matching test version) */
  term_puts_t(term, "\033[1;36mVib-OS Terminal v1.0\033[0m\n");
  term_puts_t(term, "Type '\033[33mhelp\033[0m' for commands, "
                    "'\033[33mneofetch\033[0m' for system info.\n\n");
  term_puts_t(term, "\033[32mvib-os\033[0m:\033[34m~\033[0m$ ");
  
  return term;
}

/* Get/set active terminal */
terminal_t *term_get_active(void) { return active_term; }
void term_set_active(terminal_t *term) { active_term = term; }
