/*
 * VibeOS Userspace Library
 *
 * Programs receive a pointer to kernel API and call functions directly.
 * No syscalls needed - Win3.1 style!
 */

#ifndef _VIBE_H
#define _VIBE_H

typedef unsigned long size_t;
typedef unsigned char uint8_t;
typedef unsigned short uint16_t;
typedef unsigned int uint32_t;
typedef unsigned long uint64_t;
typedef signed short int16_t;

// Kernel API structure (must match kernel/kapi.h)
typedef struct kapi {
    uint32_t version;

    // Console I/O
    void (*putc)(char c);
    void (*puts)(const char *s);
    void (*uart_puts)(const char *s);  // Direct UART output
    int  (*getc)(void);
    void (*set_color)(uint32_t fg, uint32_t bg);
    void (*clear)(void);
    void (*set_cursor)(int row, int col);
    void (*set_cursor_enabled)(int enabled);  // Enable/disable text cursor
    void (*print_int)(int n);
    void (*print_hex)(uint32_t n);
    void (*clear_to_eol)(void);      // Fast clear from cursor to end of line
    void (*clear_region)(int row, int col, int w, int h);  // Fast rectangular clear

    // Keyboard
    int  (*has_key)(void);

    // Memory
    void *(*malloc)(size_t size);
    void  (*free)(void *ptr);

    // Filesystem
    void *(*open)(const char *path);
    void  (*close)(void *file);
    int   (*read)(void *file, char *buf, size_t size, size_t offset);
    int   (*write)(void *file, const char *buf, size_t size);
    int   (*is_dir)(void *node);
    int   (*file_size)(void *node);   // Get file size in bytes
    void *(*create)(const char *path);
    void *(*mkdir)(const char *path);
    int   (*delete)(const char *path);
    int   (*delete_dir)(const char *path);  // Delete empty directory
    int   (*delete_recursive)(const char *path);  // Delete file or dir recursively
    int   (*rename)(const char *path, const char *newname);
    int   (*readdir)(void *dir, int index, char *name, size_t name_size, uint8_t *type);
    int   (*set_cwd)(const char *path);
    int   (*get_cwd)(char *buf, size_t size);

    // Process
    void (*exit)(int status);
    int  (*exec)(const char *path);   // Run another program (waits for completion)
    int  (*exec_args)(const char *path, int argc, char **argv);  // Run with arguments
    void (*yield)(void);              // Give up CPU to other processes
    int  (*spawn)(const char *path);  // Start a new process (returns immediately)
    int  (*spawn_args)(const char *path, int argc, char **argv);  // Spawn with arguments

    // Console info
    int  (*console_rows)(void);       // Get number of console rows
    int  (*console_cols)(void);       // Get number of console columns

    // Framebuffer (for GUI programs)
    uint32_t *fb_base;
    uint32_t fb_width;
    uint32_t fb_height;
    void (*fb_put_pixel)(uint32_t x, uint32_t y, uint32_t color);
    void (*fb_fill_rect)(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t color);
    void (*fb_draw_char)(uint32_t x, uint32_t y, char c, uint32_t fg, uint32_t bg);
    void (*fb_draw_string)(uint32_t x, uint32_t y, const char *s, uint32_t fg, uint32_t bg);

    // Font access (for custom rendering)
    const uint8_t *font_data;        // 256 chars, 16 bytes each (8x16 bitmap)

    // Mouse (for GUI programs)
    void (*mouse_get_pos)(int *x, int *y);
    uint8_t (*mouse_get_buttons)(void);
    void (*mouse_poll)(void);
    void (*mouse_set_pos)(int x, int y);
    void (*mouse_get_delta)(int *dx, int *dy);

    // Window management (for desktop apps)
    // These are set by the desktop window server, not the kernel
    int  (*window_create)(int x, int y, int w, int h, const char *title);
    void (*window_destroy)(int wid);
    uint32_t *(*window_get_buffer)(int wid, int *w, int *h);
    int  (*window_poll_event)(int wid, int *event_type, int *data1, int *data2, int *data3);
    void (*window_invalidate)(int wid);
    void (*window_set_title)(int wid, const char *title);

    // Stdio hooks (for terminal emulator)
    // If set, shell uses these instead of console I/O
    void (*stdio_putc)(char c);          // Write a character
    void (*stdio_puts)(const char *s);   // Write a string
    int  (*stdio_getc)(void);            // Read a character (-1 if none)
    int  (*stdio_has_key)(void);         // Check if input available

    // System info
    unsigned long (*get_uptime_ticks)(void);  // Get timer tick count (100 ticks/sec)
    size_t (*get_mem_used)(void);             // Get used memory in bytes
    size_t (*get_mem_free)(void);             // Get free memory in bytes

    // RTC (Real Time Clock)
    uint32_t (*get_timestamp)(void);     // Unix timestamp (seconds since 1970)
    void (*get_datetime)(int *year, int *month, int *day,
                         int *hour, int *minute, int *second, int *weekday);

    // Power management / timing
    void (*wfi)(void);                   // Wait for interrupt (low power sleep)
    void (*sleep_ms)(uint32_t ms);       // Sleep for at least ms milliseconds

    // Sound
    int (*sound_play_wav)(const void *data, uint32_t size);  // Play WAV from memory (legacy)
    void (*sound_stop)(void);                                 // Stop playback
    int (*sound_is_playing)(void);                           // Check if playing
    int (*sound_play_pcm)(const void *data, uint32_t samples, uint8_t channels, uint32_t sample_rate);  // Play raw S16LE PCM (blocking)
    int (*sound_play_pcm_async)(const void *data, uint32_t samples, uint8_t channels, uint32_t sample_rate);  // Play raw S16LE PCM (non-blocking)
    void (*sound_pause)(void);                               // Pause playback (can resume)
    int (*sound_resume)(void);                               // Resume paused playback
    int (*sound_is_paused)(void);                            // Check if paused

    // Process info (for sysmon)
    int (*get_process_count)(void);                          // Number of active processes
    int (*get_process_info)(int index, char *name, int name_size, int *state);  // Get process info by index

    // Disk info
    int (*get_disk_total)(void);                             // Total disk space in KB
    int (*get_disk_free)(void);                              // Free disk space in KB

    // RAM info
    size_t (*get_ram_total)(void);                           // Total RAM in bytes

    // Debug memory info
    uint64_t (*get_heap_start)(void);                        // Heap start address
    uint64_t (*get_heap_end)(void);                          // Heap end address
    uint64_t (*get_stack_ptr)(void);                         // Current stack pointer
    int (*get_alloc_count)(void);                            // Number of allocations

    // Networking
    int (*net_ping)(uint32_t ip, uint16_t seq, uint32_t timeout_ms);  // Ping an IP, returns 0 on success
    void (*net_poll)(void);                                           // Process incoming packets
    uint32_t (*net_get_ip)(void);                                     // Get our IP address
    void (*net_get_mac)(uint8_t *mac);                               // Get our MAC address (6 bytes)
    uint32_t (*dns_resolve)(const char *hostname);                   // Resolve hostname to IP, returns 0 on failure

    // TCP sockets
    int (*tcp_connect)(uint32_t ip, uint16_t port);                  // Connect to server, returns socket or -1
    int (*tcp_send)(int sock, const void *data, uint32_t len);       // Send data, returns bytes sent or -1
    int (*tcp_recv)(int sock, void *buf, uint32_t maxlen);           // Receive data, returns bytes or 0/-1
    void (*tcp_close)(int sock);                                      // Close connection
    int (*tcp_is_connected)(int sock);                               // Check if connected

    // TLS (HTTPS) sockets
    int (*tls_connect)(uint32_t ip, uint16_t port, const char *hostname);  // Connect with TLS
    int (*tls_send)(int sock, const void *data, uint32_t len);             // Send encrypted
    int (*tls_recv)(int sock, void *buf, uint32_t maxlen);                 // Receive decrypted
    void (*tls_close)(int sock);                                           // Close TLS
    int (*tls_is_connected)(int sock);                                     // Check connected

    // TrueType font rendering
    void *(*ttf_get_glyph)(int codepoint, int size, int style);  // Returns ttf_glyph_t*
    int (*ttf_get_advance)(int codepoint, int size);
    int (*ttf_get_kerning)(int cp1, int cp2, int size);
    void (*ttf_get_metrics)(int size, int *ascent, int *descent, int *line_gap);
    int (*ttf_is_ready)(void);

    // GPIO LED (for Pi only, no-op on QEMU)
    void (*led_on)(void);
    void (*led_off)(void);
    void (*led_toggle)(void);
    int (*led_status)(void);

    // Process control
    int (*kill_process)(int pid);

    // CPU info
    const char *(*get_cpu_name)(void);
    uint32_t (*get_cpu_freq_mhz)(void);
    int (*get_cpu_cores)(void);

    // USB device list
    int (*usb_device_count)(void);
    int (*usb_device_info)(int idx, uint16_t *vid, uint16_t *pid,
                           char *name, int name_len);

    // Kernel log (dmesg)
    size_t (*klog_read)(char *buf, size_t offset, size_t size);  // Read from kernel log
    size_t (*klog_size)(void);                                   // Get log size

    // Hardware double buffering (Pi only)
    int (*fb_has_hw_double_buffer)(void);    // Returns 1 if hardware double buffering available
    int (*fb_flip)(int buffer);              // Switch visible buffer (0 or 1), returns 0 on success
    uint32_t *(*fb_get_backbuffer)(void);    // Get pointer to current backbuffer

    // DMA (Direct Memory Access) - hardware accelerated memory copies (Pi only)
    int (*dma_available)(void);              // Returns 1 if DMA is available
    int (*dma_copy)(void *dst, const void *src, uint32_t len);  // 1D copy
    int (*dma_copy_2d)(void *dst, uint32_t dst_pitch,           // 2D blit
                       const void *src, uint32_t src_pitch,
                       uint32_t width, uint32_t height);
    int (*dma_fb_copy)(uint32_t *dst, const uint32_t *src,      // Full framebuffer copy
                       uint32_t width, uint32_t height);
    int (*dma_fill)(void *dst, uint32_t value, uint32_t len);   // Fill with 32-bit value

    /* Input Polling (Direct) */
    void (*input_poll)(void);
} kapi_t;

// TTF glyph info (returned by ttf_get_glyph)
typedef struct {
    uint8_t *bitmap;     // Grayscale bitmap (0-255), do not free
    int width;           // Bitmap width
    int height;          // Bitmap height
    int xoff;            // X offset from cursor
    int yoff;            // Y offset from cursor (negative = above baseline)
    int advance;         // Cursor advance after glyph
} ttf_glyph_t;

// TTF font style flags
#define TTF_STYLE_NORMAL  0
#define TTF_STYLE_BOLD    1
#define TTF_STYLE_ITALIC  2

// TTF font sizes
#define TTF_SIZE_SMALL   12
#define TTF_SIZE_NORMAL  16
#define TTF_SIZE_LARGE   24
#define TTF_SIZE_XLARGE  32

// Window event types
#define WIN_EVENT_NONE       0
#define WIN_EVENT_MOUSE_DOWN 1
#define WIN_EVENT_MOUSE_UP   2
#define WIN_EVENT_MOUSE_MOVE 3
#define WIN_EVENT_KEY        4
#define WIN_EVENT_CLOSE      5
#define WIN_EVENT_FOCUS      6
#define WIN_EVENT_UNFOCUS    7
#define WIN_EVENT_RESIZE     8

// Mouse button masks
#define MOUSE_BTN_LEFT   0x01
#define MOUSE_BTN_RIGHT  0x02
#define MOUSE_BTN_MIDDLE 0x04

// Special key codes (must match kernel/keyboard.c)
#define KEY_UP     0x100
#define KEY_DOWN   0x101
#define KEY_LEFT   0x102
#define KEY_RIGHT  0x103
#define KEY_HOME   0x104
#define KEY_END    0x105
#define KEY_DELETE 0x106
#define KEY_PGUP   0x107
#define KEY_PGDN   0x108

// Colors (must match kernel fb.h - these are RGB values)
#define COLOR_BLACK   0x00000000
#define COLOR_WHITE   0x00FFFFFF
#define COLOR_RED     0x00FF0000
#define COLOR_GREEN   0x0000FF00
#define COLOR_BLUE    0x000000FF
#define COLOR_CYAN    0x0000FFFF
#define COLOR_MAGENTA 0x00FF00FF
#define COLOR_YELLOW  0x00FFFF00
#define COLOR_AMBER   0x00FFBF00

// NULL pointer
#ifndef NULL
#define NULL ((void *)0)
#endif

// Network helper: make IP address from bytes
#define MAKE_IP(a,b,c,d) (((uint32_t)(a)<<24)|((uint32_t)(b)<<16)|((uint32_t)(c)<<8)|(uint32_t)(d))

// ============ String Functions ============

static inline size_t strlen(const char *s) {
    size_t len = 0;
    while (s[len]) len++;
    return len;
}

static inline int strcmp(const char *a, const char *b) {
    while (*a && *b && *a == *b) {
        a++;
        b++;
    }
    return *a - *b;
}

static inline int strncmp(const char *a, const char *b, size_t n) {
    while (n > 0 && *a && *b && *a == *b) {
        a++;
        b++;
        n--;
    }
    if (n == 0) return 0;
    return *a - *b;
}

static inline char *strcpy(char *dst, const char *src) {
    char *d = dst;
    while ((*d++ = *src++));
    return dst;
}

static inline char *strncpy_safe(char *dst, const char *src, size_t n) {
    size_t i;
    for (i = 0; i < n - 1 && src[i]; i++) {
        dst[i] = src[i];
    }
    dst[i] = '\0';
    return dst;
}

static inline char *strcat(char *dst, const char *src) {
    char *d = dst;
    while (*d) d++;
    while ((*d++ = *src++));
    return dst;
}

static inline void *memset(void *s, int c, size_t n) {
    unsigned char *p = (unsigned char *)s;
    while (n--) *p++ = (unsigned char)c;
    return s;
}

static inline void *memcpy(void *dst, const void *src, size_t n) {
    unsigned char *d = (unsigned char *)dst;
    const unsigned char *s = (const unsigned char *)src;
    while (n--) *d++ = *s++;
    return dst;
}

// Fast 64-bit aligned memset for 32-bit values (fills 2 pixels at a time)
static inline void memset32_fast(uint32_t *dst, uint32_t val, size_t count) {
    // Use 64-bit stores when aligned and count is sufficient
    if (((size_t)dst & 7) == 0 && count >= 2) {
        uint64_t pattern = ((uint64_t)val << 32) | val;
        uint64_t *d64 = (uint64_t *)dst;
        size_t n64 = count / 2;
        for (size_t i = 0; i < n64; i++) {
            d64[i] = pattern;
        }
        // Handle odd pixel at end
        if (count & 1) {
            dst[count - 1] = val;
        }
    } else {
        // Fallback for unaligned or small counts
        for (size_t i = 0; i < count; i++) {
            dst[i] = val;
        }
    }
}

// Fast 64-bit aligned memcpy (copies 8 bytes at a time)
static inline void memcpy64(void *dst, const void *src, size_t n) {
    // Use 64-bit loads/stores when both aligned
    if (((size_t)dst & 7) == 0 && ((size_t)src & 7) == 0 && n >= 8) {
        uint64_t *d = (uint64_t *)dst;
        const uint64_t *s = (const uint64_t *)src;
        size_t n64 = n / 8;
        for (size_t i = 0; i < n64; i++) {
            d[i] = s[i];
        }
        // Handle remainder bytes
        size_t rem = n & 7;
        if (rem) {
            uint8_t *d8 = (uint8_t *)(d + n64);
            const uint8_t *s8 = (const uint8_t *)(s + n64);
            for (size_t i = 0; i < rem; i++) {
                d8[i] = s8[i];
            }
        }
    } else {
        // Fallback for unaligned
        uint8_t *d = (uint8_t *)dst;
        const uint8_t *s = (const uint8_t *)src;
        while (n--) *d++ = *s++;
    }
}

// Check if character is whitespace
static inline int isspace(int c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r';
}

// Check if character is printable
static inline int isprint(int c) {
    return c >= 32 && c < 127;
}

// ============ Smart I/O Helpers ============
// These automatically use stdio hooks when available (for terminal emulator)
// Otherwise fall back to console I/O

// Print a character - uses stdio hooks if set, else console
static inline void vibe_putc(kapi_t *k, char c) {
    if (k->stdio_putc) k->stdio_putc(c);
    else k->putc(c);
}

// Print a string - uses stdio hooks if set, else console
static inline void vibe_puts(kapi_t *k, const char *s) {
    if (k->stdio_puts) k->stdio_puts(s);
    else k->puts(s);
}

// Read a character - uses stdio hooks if set, else console
static inline int vibe_getc(kapi_t *k) {
    if (k->stdio_getc) return k->stdio_getc();
    else return k->getc();
}

// Check if key available - uses stdio hooks if set, else console
static inline int vibe_has_key(kapi_t *k) {
    if (k->stdio_has_key) return k->stdio_has_key();
    else return k->has_key();
}

// Print an integer (decimal)
static inline void vibe_print_int(kapi_t *k, int n) {
    if (n < 0) {
        vibe_putc(k, '-');
        n = -n;
    }
    if (n == 0) {
        vibe_putc(k, '0');
        return;
    }
    char buf[12];
    int i = 0;
    while (n > 0) {
        buf[i++] = '0' + (n % 10);
        n /= 10;
    }
    while (i > 0) {
        vibe_putc(k, buf[--i]);
    }
}

// Print an unsigned integer (decimal)
static inline void vibe_print_uint(kapi_t *k, unsigned int n) {
    if (n == 0) {
        vibe_putc(k, '0');
        return;
    }
    char buf[12];
    int i = 0;
    while (n > 0) {
        buf[i++] = '0' + (n % 10);
        n /= 10;
    }
    while (i > 0) {
        vibe_putc(k, buf[--i]);
    }
}

// Print a hex number (no 0x prefix)
static inline void vibe_print_hex(kapi_t *k, uint32_t n) {
    if (n == 0) {
        vibe_putc(k, '0');
        return;
    }
    char buf[9];
    int i = 0;
    while (n > 0) {
        int digit = n & 0xF;
        buf[i++] = digit < 10 ? '0' + digit : 'a' + digit - 10;
        n >>= 4;
    }
    while (i > 0) {
        vibe_putc(k, buf[--i]);
    }
}

// Print size in human-readable format (KB, MB, GB)
static inline void vibe_print_size(kapi_t *k, uint64_t bytes) {
    if (bytes < 1024) {
        vibe_print_uint(k, (unsigned int)bytes);
        vibe_puts(k, " B");
    } else if (bytes < 1024 * 1024) {
        vibe_print_uint(k, (unsigned int)(bytes / 1024));
        vibe_puts(k, " KB");
    } else if (bytes < 1024ULL * 1024 * 1024) {
        vibe_print_uint(k, (unsigned int)(bytes / (1024 * 1024)));
        vibe_puts(k, " MB");
    } else {
        vibe_print_uint(k, (unsigned int)(bytes / (1024ULL * 1024 * 1024)));
        vibe_puts(k, " GB");
    }
}

#endif
