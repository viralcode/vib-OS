/*
 * Vib-OS Kernel API for Userspace Applications
 * 
 * IMPORTANT: This structure MUST match user/lib/vibe.h exactly!
 * Programs like DOOM depend on the exact field layout.
 */

#ifndef KAPI_H
#define KAPI_H

#include "types.h"

/* Kernel API structure - must match vibe.h layout! */
typedef struct kapi {
    uint32_t version;

    /* Console I/O */
    void (*putc)(char c);
    void (*puts)(const char *s);
    void (*uart_puts)(const char *s);
    int  (*getc)(void);
    void (*set_color)(uint32_t fg, uint32_t bg);
    void (*clear)(void);
    void (*set_cursor)(int row, int col);
    void (*set_cursor_enabled)(int enabled);
    void (*print_int)(int n);
    void (*print_hex)(uint32_t n);
    void (*clear_to_eol)(void);
    void (*clear_region)(int row, int col, int w, int h);

    /* Keyboard */
    int  (*has_key)(void);

    /* Memory */
    void *(*malloc)(size_t size);
    void  (*free)(void *ptr);

    /* Filesystem */
    void *(*open)(const char *path);
    void  (*close)(void *file);
    int   (*read)(void *file, char *buf, size_t size, size_t offset);
    int   (*write)(void *file, const char *buf, size_t size);
    int   (*is_dir)(void *node);
    int   (*file_size)(void *node);
    void *(*create)(const char *path);
    void *(*mkdir_fn)(const char *path);
    int   (*delete)(const char *path);
    int   (*delete_dir)(const char *path);
    int   (*delete_recursive)(const char *path);
    int   (*rename)(const char *path, const char *newname);
    int   (*readdir)(void *dir, int index, char *name, size_t name_size, uint8_t *type);
    int   (*set_cwd)(const char *path);
    int   (*get_cwd)(char *buf, size_t size);

    /* Process */
    void (*exit)(int status);
    int  (*exec)(const char *path);
    int  (*exec_args)(const char *path, int argc, char **argv);
    void (*yield)(void);
    int  (*spawn)(const char *path);
    int  (*spawn_args)(const char *path, int argc, char **argv);

    /* Console info */
    int  (*console_rows)(void);
    int  (*console_cols)(void);

    /* Framebuffer (for GUI programs) */
    uint32_t *fb_base;
    uint32_t fb_width;
    uint32_t fb_height;
    void (*fb_put_pixel)(uint32_t x, uint32_t y, uint32_t color);
    void (*fb_fill_rect)(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t color);
    void (*fb_draw_char)(uint32_t x, uint32_t y, char c, uint32_t fg, uint32_t bg);
    void (*fb_draw_string)(uint32_t x, uint32_t y, const char *s, uint32_t fg, uint32_t bg);

    /* Font access */
    const uint8_t *font_data;

    /* Mouse */
    void (*mouse_get_pos)(int *x, int *y);
    uint8_t (*mouse_get_buttons)(void);
    void (*mouse_poll)(void);
    void (*mouse_set_pos)(int x, int y);
    void (*mouse_get_delta)(int *dx, int *dy);

    /* Window management (placeholders) */
    int  (*window_create)(int x, int y, int w, int h, const char *title);
    void (*window_destroy)(int wid);
    uint32_t *(*window_get_buffer)(int wid, int *w, int *h);
    int  (*window_poll_event)(int wid, int *event_type, int *data1, int *data2, int *data3);
    void (*window_invalidate)(int wid);
    void (*window_set_title)(int wid, const char *title);

    /* Stdio hooks */
    void (*stdio_putc)(char c);
    void (*stdio_puts)(const char *s);
    int  (*stdio_getc)(void);
    int  (*stdio_has_key)(void);

    /* System info */
    unsigned long (*get_uptime_ticks)(void);
    size_t (*get_mem_used)(void);
    size_t (*get_mem_free)(void);

    /* RTC */
    uint32_t (*get_timestamp)(void);
    void (*get_datetime)(int *year, int *month, int *day,
                         int *hour, int *minute, int *second, int *weekday);

    /* Power/timing */
    void (*wfi)(void);
    void (*sleep_ms)(uint32_t ms);

    /* Sound (placeholders) */
    int (*sound_play_wav)(const void *data, uint32_t size);
    void (*sound_stop)(void);
    int (*sound_is_playing)(void);
    int (*sound_play_pcm)(const void *data, uint32_t samples, uint8_t channels, uint32_t sample_rate);
    int (*sound_play_pcm_async)(const void *data, uint32_t samples, uint8_t channels, uint32_t sample_rate);
    void (*sound_pause)(void);
    int (*sound_resume)(void);
    int (*sound_is_paused)(void);

    /* Process info */
    int (*get_process_count)(void);
    int (*get_process_info)(int index, char *name, int name_size, int *state);

    /* Disk info */
    int (*get_disk_total)(void);
    int (*get_disk_free)(void);

    /* RAM info */
    size_t (*get_ram_total)(void);

    /* Debug memory info */
    uint64_t (*get_heap_start)(void);
    uint64_t (*get_heap_end)(void);
    uint64_t (*get_stack_ptr)(void);
    int (*get_alloc_count)(void);

    /* Networking (placeholders) */
    int (*net_ping)(uint32_t ip, uint16_t seq, uint32_t timeout_ms);
    void (*net_poll)(void);
    uint32_t (*net_get_ip)(void);
    void (*net_get_mac)(uint8_t *mac);
    uint32_t (*dns_resolve)(const char *hostname);

    /* TCP sockets (placeholders) */
    int (*tcp_connect)(uint32_t ip, uint16_t port);
    int (*tcp_send)(int sock, const void *data, uint32_t len);
    int (*tcp_recv)(int sock, void *buf, uint32_t maxlen);
    void (*tcp_close)(int sock);
    int (*tcp_is_connected)(int sock);

    /* TLS (placeholders) */
    int (*tls_connect)(uint32_t ip, uint16_t port, const char *hostname);
    int (*tls_send)(int sock, const void *data, uint32_t len);
    int (*tls_recv)(int sock, void *buf, uint32_t maxlen);
    void (*tls_close)(int sock);
    int (*tls_is_connected)(int sock);

    /* TTF (placeholders) */
    void *(*ttf_get_glyph)(int codepoint, int size, int style);
    int (*ttf_get_advance)(int codepoint, int size);
    int (*ttf_get_kerning)(int cp1, int cp2, int size);
    void (*ttf_get_metrics)(int size, int *ascent, int *descent, int *line_gap);
    int (*ttf_is_ready)(void);

    /* GPIO LED (placeholders) */
    void (*led_on)(void);
    void (*led_off)(void);
    void (*led_toggle)(void);
    int (*led_status)(void);

    /* Process control */
    int (*kill_process)(int pid);

    /* CPU info */
    const char *(*get_cpu_name)(void);
    uint32_t (*get_cpu_freq_mhz)(void);
    int (*get_cpu_cores)(void);

    /* USB (placeholders) */
    int (*usb_device_count)(void);
    int (*usb_device_info)(int idx, uint16_t *vid, uint16_t *pid, char *name, int name_len);

    /* Kernel log */
    size_t (*klog_read)(char *buf, size_t offset, size_t size);
    size_t (*klog_size)(void);

    /* Hardware double buffering (placeholders) */
    int (*fb_has_hw_double_buffer)(void);
    int (*fb_flip)(int buffer);
    uint32_t *(*fb_get_backbuffer)(void);

    /* DMA (placeholders) */
    int (*dma_available)(void);
    int (*dma_copy)(void *dst, const void *src, uint32_t len);
    int (*dma_copy_2d)(void *dst, uint32_t dst_pitch, const void *src, uint32_t src_pitch, uint32_t width, uint32_t height);
    int (*dma_fb_copy)(uint32_t *dst, const uint32_t *src, uint32_t width, uint32_t height);
    int (*dma_fill)(void *dst, uint32_t value, uint32_t len);
    
    /* Input Polling (Direct) */
    void (*input_poll)(void);
} kapi_t;

/* Initialize the kernel API */
void kapi_init(kapi_t *api);

/* Get the global kapi instance */
kapi_t *kapi_get(void);

/* Tick the timer */
void kapi_tick(void);

/* Launch an embedded application */
typedef int (*app_main_fn)(kapi_t *api, int argc, char **argv);
int app_run(const char *name, int argc, char **argv);

#endif /* KAPI_H */
