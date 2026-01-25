/*
 * Vib-OS Application Launcher
 * 
 * Provides kernel API and launches embedded applications
 */

#include "apps/kapi.h"
#include "printk.h"
#include "mm/kmalloc.h"

/* Display structure from window.c - MUST match exactly! */
struct display {
    uint32_t width;
    uint32_t height;
    uint32_t pitch;
    uint32_t bpp;
    uint32_t *framebuffer;
    uint32_t *backbuffer;
};

/* External references */
extern struct display *gui_get_display(void);
extern void mouse_get_position(int *x, int *y);
extern int mouse_get_buttons(void);
extern int uart_getc_nonblock(void);
extern void uart_putc(char c);

/* Timer ticks counter */
static volatile uint64_t uptime_ticks = 0;

/* Global kernel API instance */
static kapi_t global_kapi;

/* ===================================================================== */
/* KAPI Implementation Functions */
/* ===================================================================== */

static void kapi_putc(char c) {
    uart_putc(c);
}

static void kapi_puts(const char *s) {
    while (*s) uart_putc(*s++);
}

/* Input Ring Buffer */
#define KAPI_INPUT_BUF_SIZE 128
static volatile int k_input_buf[KAPI_INPUT_BUF_SIZE];
static volatile int k_input_r = 0;
static volatile int k_input_w = 0;

void kapi_sys_key_event(int key) {
    int next = (k_input_w + 1) % KAPI_INPUT_BUF_SIZE;
    if (next != k_input_r) {
        k_input_buf[k_input_w] = key;
        k_input_w = next;
    }
}

static int kapi_getc(void) {
    /* Check buffer first */
    if (k_input_r != k_input_w) {
        int key = k_input_buf[k_input_r];
        k_input_r = (k_input_r + 1) % KAPI_INPUT_BUF_SIZE;
        return key;
    }
    /* Fallback to UART */
    return uart_getc_nonblock();
}

static int kapi_has_key(void) {
    return uart_getc_nonblock() >= 0 ? 1 : 0;
}

static void kapi_clear(void) {
    /* Clear framebuffer to black */
    struct display *d = gui_get_display();
    if (d && d->framebuffer) {
        for (uint32_t i = 0; i < d->width * d->height; i++) {
            d->framebuffer[i] = 0;
        }
    }
}

static int kapi_get_key(void) {
    return uart_getc_nonblock();
}

static void kapi_mouse_get_pos(int *x, int *y) {
    mouse_get_position(x, y);
}

static uint8_t kapi_mouse_get_buttons(void) {
    return (uint8_t)mouse_get_buttons();
}

static int last_mouse_x = 0, last_mouse_y = 0;
static void kapi_mouse_get_delta(int *dx, int *dy) {
    int x, y;
    mouse_get_position(&x, &y);
    *dx = x - last_mouse_x;
    *dy = y - last_mouse_y;
    last_mouse_x = x;
    last_mouse_y = y;
}

/* Sound implementation - forwards to Intel HDA driver */
#include "drivers/intel_hda.h"

extern int intel_hda_play_pcm(const void *data, uint32_t samples, uint8_t channels, uint32_t sample_rate);

static int kapi_sound_play_wav(const void *data, uint32_t size) {
    /* TODO: Parse WAV header */
    return 0; 
}

static void kapi_sound_stop(void) {
    /* TODO: Stop playback */
}

static int kapi_sound_is_playing(void) {
    /* TODO: Check driver */
    return 0;
}

static int kapi_sound_play_pcm(const void *data, uint32_t samples, uint8_t channels, uint32_t sample_rate) {
    /* Forward to driver */
    /* Check if HDA is active? */
    return intel_hda_play_pcm(data, samples, channels, sample_rate);
}

static int kapi_sound_play_pcm_async(const void *data, uint32_t samples, uint8_t channels, uint32_t sample_rate) {
    /* Same as sync for now, just non-blocking if possible */
    return kapi_sound_play_pcm(data, samples, channels, sample_rate);
}

static void kapi_sound_pause(void) {}
static int kapi_sound_resume(void) { return 0; }
static int kapi_sound_is_paused(void) { return 0; }

static unsigned long kapi_get_uptime_ticks(void) {
    /* Read timer counter - architecture specific */
#ifdef ARCH_ARM64
    uint64_t cnt, freq;
    asm volatile("mrs %0, cntvct_el0" : "=r"(cnt));
    asm volatile("mrs %0, cntfrq_el0" : "=r"(freq));
    return (unsigned long)((cnt * 100ULL) / freq);
#elif defined(ARCH_X86_64) || defined(ARCH_X86)
    /* Use arch timer abstraction */
    extern uint64_t arch_timer_get_ticks(void);
    extern uint64_t arch_timer_get_frequency(void);
    uint64_t cnt = arch_timer_get_ticks();
    uint64_t freq = arch_timer_get_frequency();
    return (unsigned long)((cnt * 100ULL) / freq);
#else
    return 0;
#endif
}

static void kapi_sleep_ms(uint32_t ms) {
    /* Simple busy-wait sleep */
    for (volatile uint32_t i = 0; i < ms * 10000; i++) { }
}

static void *kapi_malloc(size_t size) {
    return kmalloc(size);
}

static void kapi_free(void *ptr) {
    kfree(ptr);
}

/* VFS compat functions from vfs_compat.c */
extern void *vfs_lookup(const char *path);
extern int vfs_read_compat(void *node, char *buf, unsigned int size, unsigned int offset);
extern int vfs_is_dir(void *node);

/* Get file size from vfs_node_t */
static int get_vfs_file_size(void *node) {
    if (!node) return 0;
    /* vfs_node_t has size field - we cast and access it */
    struct { char *name; int type; unsigned int size; } *n = node;
    return (int)n->size;
}

/* File I/O implemented with VFS */
static void *kapi_open(const char *path) {
    if (!path) return NULL;
    
    /* VFS lookup handles all path aliases internally */
    void *file = vfs_lookup(path);
    if (file) {
        printk(KERN_INFO "[KAPI] open: %s -> found\\n", path);
        return file;
    }
    
    /* Try without leading slash as fallback */
    if (path[0] == '/') {
        file = vfs_lookup(path + 1);
        if (file) return file;
    }
    
    printk(KERN_WARNING "[KAPI] open: %s -> NOT FOUND\\n", path);
    return NULL;
}

static void kapi_close(void *handle) {
    (void)handle;
    /* VFS nodes don't need explicit close */
}

static int kapi_read(void *handle, char *buf, size_t count, size_t offset) {
    if (!handle || !buf) return -1;
    return vfs_read_compat(handle, buf, (unsigned int)count, (unsigned int)offset);
}

static int kapi_write(void *handle, const char *buf, size_t count) {
    (void)handle; (void)buf; (void)count;
    return -1; /* Read-only for now */
}

static int kapi_file_size(void *handle) {
    return get_vfs_file_size(handle);
}

static int kapi_create(const char *path) {
    (void)path;
    return -1;
}

static int kapi_delete(const char *path) {
    (void)path;
    return -1;
}

static int kapi_rename(const char *old, const char *new) {
    (void)old; (void)new;
    return -1;
}

static void kapi_exit(int status) {
    printk(KERN_INFO "[APP] Exit with status %d\n", status);
    /* Return to kernel - in real userspace, this would terminate the process */
}

/* Run an app and wait for completion */
static int kapi_exec(const char *path) {
    printk(KERN_INFO "[KAPI] exec: %s\n", path);
    return app_run(path, 0, 0);
}

/* Run an app in background */
static int kapi_spawn(const char *path) {
    printk(KERN_INFO "[KAPI] spawn: %s\n", path);
    /* For now, same as exec - no true multitasking yet */
    return app_run(path, 0, 0);
}

/* Yield CPU to other tasks */
static void kapi_yield(void) {
    /* Placeholder - would call scheduler */
    for (volatile int i = 0; i < 1000; i++) { }
}

static void kapi_uart_puts(const char *s) {
    while (*s) uart_putc(*s++);
}

/* ===================================================================== */
/* Initialize Kernel API */
/* ===================================================================== */

/* Stub functions for unimplemented features */
static void stub_void(void) {}
static void stub_void_int(int x) { (void)x; }
static int stub_int(void) { return 0; }
static int stub_int_int(int x) { (void)x; return 0; }
static uint32_t stub_uint32(void) { return 0; }
static void stub_set_color(uint32_t fg, uint32_t bg) { (void)fg; (void)bg; }
static void stub_set_cursor(int r, int c) { (void)r; (void)c; }
static void stub_clear_region(int r, int c, int w, int h) { (void)r; (void)c; (void)w; (void)h; }
static int stub_is_dir(void *n) { (void)n; return 0; }
static void *stub_ptr_path(const char *p) { (void)p; return NULL; }
static int stub_delete_path(const char *p) { (void)p; return -1; }
static int stub_readdir(void *d, int i, char *n, size_t ns, uint8_t *t) { (void)d; (void)i; (void)n; (void)ns; (void)t; return -1; }
static int stub_set_cwd(const char *p) { (void)p; return -1; }
static int stub_get_cwd(char *b, size_t s) { (void)b; (void)s; return -1; }
static int stub_exec_args(const char *p, int a, char **v) { (void)p; (void)a; (void)v; return -1; }
static int stub_spawn_args(const char *p, int a, char **v) { (void)p; (void)a; (void)v; return -1; }
static int stub_console_size(void) { return 25; }
static void stub_fb_pixel(uint32_t x, uint32_t y, uint32_t c) { (void)x; (void)y; (void)c; }
static void stub_fb_rect(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t c) { (void)x; (void)y; (void)w; (void)h; (void)c; }
static void stub_fb_char(uint32_t x, uint32_t y, char c, uint32_t fg, uint32_t bg) { (void)x; (void)y; (void)c; (void)fg; (void)bg; }
static void stub_fb_string(uint32_t x, uint32_t y, const char *s, uint32_t fg, uint32_t bg) { (void)x; (void)y; (void)s; (void)fg; (void)bg; }
static void stub_mouse_poll(void) {}
static void stub_mouse_set(int x, int y) { (void)x; (void)y; }
static int stub_win_create(int x, int y, int w, int h, const char *t) { (void)x; (void)y; (void)w; (void)h; (void)t; return -1; }
static void stub_win_destroy(int w) { (void)w; }
static uint32_t *stub_win_buf(int w, int *pw, int *ph) { (void)w; (void)pw; (void)ph; return NULL; }
static int stub_win_poll(int w, int *t, int *d1, int *d2, int *d3) { (void)w; (void)t; (void)d1; (void)d2; (void)d3; return 0; }
static void stub_win_inv(int w) { (void)w; }
static void stub_win_title(int w, const char *t) { (void)w; (void)t; }
static size_t stub_mem_info(void) { return 0; }
static uint32_t stub_timestamp(void) { return 0; }
static void stub_datetime(int *y, int *m, int *d, int *h, int *mi, int *s, int *w) { (void)y; (void)m; (void)d; (void)h; (void)mi; (void)s; (void)w; }
static void stub_wfi(void) { 
#ifdef ARCH_ARM64
    asm volatile("wfi");
#elif defined(ARCH_X86_64) || defined(ARCH_X86)
    asm volatile("hlt");
#endif
}
static int stub_sound(const void *d, uint32_t s) { (void)d; (void)s; return -1; }
static int stub_sound_pcm(const void *d, uint32_t s, uint8_t c, uint32_t r) { (void)d; (void)s; (void)c; (void)r; return -1; }
static int stub_proc_info(int i, char *n, int ns, int *st) { (void)i; (void)n; (void)ns; (void)st; return 0; }
static uint64_t stub_heap_addr(void) { return 0; }
static int stub_net_ping(uint32_t ip, uint16_t seq, uint32_t to) { (void)ip; (void)seq; (void)to; return -1; }
static uint32_t stub_net_ip(void) { return 0; }
static void stub_net_mac(uint8_t *m) { (void)m; }
static uint32_t stub_dns(const char *h) { (void)h; return 0; }
static int stub_tcp_connect(uint32_t ip, uint16_t port) { (void)ip; (void)port; return -1; }
static int stub_tcp_send(int s, const void *d, uint32_t l) { (void)s; (void)d; (void)l; return -1; }
static int stub_tcp_recv(int s, void *b, uint32_t m) { (void)s; (void)b; (void)m; return -1; }
static void stub_tcp_close(int s) { (void)s; }
static int stub_tls_connect(uint32_t ip, uint16_t port, const char *h) { (void)ip; (void)port; (void)h; return -1; }
static void *stub_ttf_glyph(int c, int s, int st) { (void)c; (void)s; (void)st; return NULL; }
static int stub_ttf_adv(int c, int s) { (void)c; (void)s; return 0; }
static int stub_ttf_kern(int c1, int c2, int s) { (void)c1; (void)c2; (void)s; return 0; }
static void stub_ttf_metrics(int s, int *a, int *d, int *lg) { (void)s; (void)a; (void)d; (void)lg; }
static int stub_usb_info(int i, uint16_t *v, uint16_t *p, char *n, int nl) { (void)i; (void)v; (void)p; (void)n; (void)nl; return 0; }
static size_t stub_klog_read(char *b, size_t o, size_t s) { (void)b; (void)o; (void)s; return 0; }
static size_t stub_klog_size(void) { return 0; }
static uint32_t *stub_backbuf(void) { return NULL; }
static int stub_dma_copy(void *d, const void *s, uint32_t l) { (void)d; (void)s; (void)l; return -1; }
static int stub_dma_2d(void *d, uint32_t dp, const void *s, uint32_t sp, uint32_t w, uint32_t h) { (void)d; (void)dp; (void)s; (void)sp; (void)w; (void)h; return -1; }
static int stub_dma_fb(uint32_t *d, const uint32_t *s, uint32_t w, uint32_t h) { (void)d; (void)s; (void)w; (void)h; return -1; }
static int stub_dma_fill(void *d, uint32_t v, uint32_t l) { (void)d; (void)v; (void)l; return -1; }
static const char *stub_cpu_name(void) { return "ARM Cortex-A72"; }

void kapi_init(kapi_t *api) {
    struct display *d = gui_get_display();
    
    /* Zero entire struct first */
    char *p = (char *)api;
    for (size_t i = 0; i < sizeof(kapi_t); i++) p[i] = 0;
    
    api->version = 1;

    /* Console I/O - in order per vibe.h */
    api->putc = kapi_putc;
    api->puts = kapi_puts;
    api->uart_puts = kapi_uart_puts;
    api->getc = kapi_getc;
    api->set_color = stub_set_color;
    api->clear = kapi_clear;
    api->set_cursor = stub_set_cursor;
    api->set_cursor_enabled = stub_void_int;
    api->print_int = NULL;  /* app uses printf instead */
    api->print_hex = NULL;
    api->clear_to_eol = stub_void;
    api->clear_region = stub_clear_region;

    /* Keyboard */
    api->has_key = kapi_has_key;

    /* Memory */
    api->malloc = kapi_malloc;
    api->free = kapi_free;

    /* Filesystem */
    api->open = kapi_open;
    api->close = kapi_close;
    api->read = kapi_read;
    api->write = kapi_write;
    api->is_dir = stub_is_dir;
    api->file_size = kapi_file_size;
    api->create = stub_ptr_path;
    api->mkdir_fn = stub_ptr_path;
    api->delete = stub_delete_path;
    api->delete_dir = stub_delete_path;
    api->delete_recursive = stub_delete_path;
    api->rename = kapi_rename;
    api->readdir = stub_readdir;
    api->set_cwd = stub_set_cwd;
    api->get_cwd = stub_get_cwd;

    /* Process */
    api->exit = kapi_exit;
    api->exec = kapi_exec;
    api->exec_args = stub_exec_args;
    api->yield = kapi_yield;
    api->spawn = kapi_spawn;
    api->spawn_args = stub_spawn_args;

    /* Console info */
    api->console_rows = stub_console_size;
    api->console_cols = stub_console_size;

    /* Framebuffer */
    api->fb_base = d ? d->framebuffer : NULL;
    api->fb_width = d ? d->width : 0;
    api->fb_height = d ? d->height : 0;
    api->fb_put_pixel = stub_fb_pixel;
    api->fb_fill_rect = stub_fb_rect;
    api->fb_draw_char = stub_fb_char;
    api->fb_draw_string = stub_fb_string;

    /* Font */
    api->font_data = NULL;

    /* Mouse */
    api->mouse_get_pos = kapi_mouse_get_pos;
    api->mouse_get_buttons = kapi_mouse_get_buttons;
    api->mouse_poll = stub_mouse_poll;
    api->mouse_set_pos = stub_mouse_set;
    api->mouse_get_delta = kapi_mouse_get_delta;

    /* Windows */
    api->window_create = stub_win_create;
    api->window_destroy = stub_win_destroy;
    api->window_get_buffer = stub_win_buf;
    api->window_poll_event = stub_win_poll;
    api->window_invalidate = stub_win_inv;
    api->window_set_title = stub_win_title;

    /* Stdio hooks - NULL means use console */
    api->stdio_putc = NULL;
    api->stdio_puts = NULL;
    api->stdio_getc = NULL;
    api->stdio_has_key = NULL;

    extern void input_poll(void);
    api->input_poll = input_poll;

    /* System info */
    api->get_uptime_ticks = kapi_get_uptime_ticks;
    api->get_mem_used = stub_mem_info;
    api->get_mem_free = stub_mem_info;

    /* RTC */
    api->get_timestamp = stub_timestamp;
    api->get_datetime = stub_datetime;

    /* Power/timing */
    api->wfi = stub_wfi;
    api->sleep_ms = kapi_sleep_ms;

    /* Sound */
    /* Sound */
    api->sound_play_wav = kapi_sound_play_wav;
    api->sound_stop = kapi_sound_stop;
    api->sound_is_playing = kapi_sound_is_playing;
    api->sound_play_pcm = kapi_sound_play_pcm;
    api->sound_play_pcm_async = kapi_sound_play_pcm_async;
    api->sound_pause = kapi_sound_pause;
    api->sound_resume = kapi_sound_resume;
    api->sound_is_paused = kapi_sound_is_paused;

    /* Process info */
    api->get_process_count = stub_int;
    api->get_process_info = stub_proc_info;

    /* Disk info */
    api->get_disk_total = stub_int;
    api->get_disk_free = stub_int;

    /* RAM info */
    api->get_ram_total = stub_mem_info;

    /* Debug memory */
    api->get_heap_start = stub_heap_addr;
    api->get_heap_end = stub_heap_addr;
    api->get_stack_ptr = stub_heap_addr;
    api->get_alloc_count = stub_int;

    /* Network */
    api->net_ping = stub_net_ping;
    api->net_poll = stub_void;
    api->net_get_ip = stub_net_ip;
    api->net_get_mac = stub_net_mac;
    api->dns_resolve = stub_dns;

    /* TCP */
    api->tcp_connect = stub_tcp_connect;
    api->tcp_send = stub_tcp_send;
    api->tcp_recv = stub_tcp_recv;
    api->tcp_close = stub_tcp_close;
    api->tcp_is_connected = stub_int_int;

    /* TLS */
    api->tls_connect = stub_tls_connect;
    api->tls_send = stub_tcp_send;
    api->tls_recv = stub_tcp_recv;
    api->tls_close = stub_tcp_close;
    api->tls_is_connected = stub_int_int;

    /* TTF */
    api->ttf_get_glyph = stub_ttf_glyph;
    api->ttf_get_advance = stub_ttf_adv;
    api->ttf_get_kerning = stub_ttf_kern;
    api->ttf_get_metrics = stub_ttf_metrics;
    api->ttf_is_ready = stub_int;

    /* LED */
    api->led_on = stub_void;
    api->led_off = stub_void;
    api->led_toggle = stub_void;
    api->led_status = stub_int;

    /* Process control */
    api->kill_process = stub_int_int;

    /* CPU info */
    api->get_cpu_name = stub_cpu_name;
    api->get_cpu_freq_mhz = stub_uint32;
    api->get_cpu_cores = stub_int;

    /* USB */
    api->usb_device_count = stub_int;
    api->usb_device_info = stub_usb_info;

    /* Kernel log */
    api->klog_read = stub_klog_read;
    api->klog_size = stub_klog_size;

    /* HW double buffer */
    api->fb_has_hw_double_buffer = stub_int;
    api->fb_flip = stub_int_int;
    api->fb_get_backbuffer = stub_backbuf;

    /* DMA */
    api->dma_available = stub_int;
    api->dma_copy = stub_dma_copy;
    api->dma_copy_2d = stub_dma_2d;
    api->dma_fb_copy = stub_dma_fb;
    api->dma_fill = stub_dma_fill;

    printk(KERN_INFO "[KAPI] Kernel API initialized (fb=%dx%d)\\n", api->fb_width, api->fb_height);
    printk(KERN_INFO "[KAPI] fb_base = 0x%lx\\n", (unsigned long)(uintptr_t)api->fb_base);
}

/* ===================================================================== */
/* Application Registry - Embedded Apps */
/* ===================================================================== */

/* Tick counter for timing */
void kapi_tick(void) {
    uptime_ticks++;
}

/* Get the global kapi */
kapi_t *kapi_get(void) {
    static int initialized = 0;
    if (!initialized) {
        kapi_init(&global_kapi);
        initialized = 1;
    }
    return &global_kapi;
}

/* ===================================================================== */
/* Demo Application: Clock */
/* ===================================================================== */
static int clock_app_main(kapi_t *api, int argc, char **argv) {
    (void)argc; (void)argv;
    
    api->puts("\n=== Vib-OS Clock ===\n");
    
    if (!api->fb_base) {
        api->puts("No framebuffer available\n");
        return -1;
    }
    
    /* Draw clock interface */
    int cx = api->fb_width / 2;
    int cy = api->fb_height / 2;
    int radius = 100;
    
    /* Draw clock face (circle) */
    for (int angle = 0; angle < 360; angle++) {
        /* Simplified circle using fixed-point math */
        int x = cx + (radius * (angle % 90 < 45 ? angle % 45 : 45 - (angle % 45))) / 45;
        int y = cy + (radius * (45 - (angle % 45))) / 45;
        if (y >= 0 && y < (int)api->fb_height && x >= 0 && x < (int)api->fb_width) {
            api->fb_base[y * api->fb_width + x] = 0xFFFFFF;
        }
    }
    
    /* Draw hour markers */
    for (int h = 0; h < 12; h++) {
        int mx = cx + ((h < 6 ? h : 12 - h) * radius / 6);
        int my = cy - (h < 3 || h > 9 ? radius - 10 : (h == 6 ? -radius + 10 : 0));
        if (my >= 0 && my < (int)api->fb_height && mx >= 0 && mx < (int)api->fb_width) {
            api->fb_base[my * api->fb_width + mx] = 0xFFFF00;
        }
    }
    
    /* Draw center dot */
    for (int dy = -3; dy <= 3; dy++) {
        for (int dx = -3; dx <= 3; dx++) {
            int px = cx + dx;
            int py = cy + dy;
            if (py >= 0 && py < (int)api->fb_height && px >= 0 && px < (int)api->fb_width) {
                api->fb_base[py * api->fb_width + px] = 0xFF0000;
            }
        }
    }
    
    /* Draw clock hands based on uptime */
    uint64_t ticks = api->get_uptime_ticks();
    int seconds = (ticks / 100) % 60;
    int minutes = (ticks / 6000) % 60;
    int hours = (ticks / 360000) % 12;
    
    /* Second hand (red, long) */
    for (int i = 0; i < radius - 10; i++) {
        int sx = cx + (i * (seconds % 30 < 15 ? seconds % 30 : 30 - seconds % 30)) / 30;
        int sy = cy - (i * (seconds < 30 ? 1 : -1));
        if (sy >= 0 && sy < (int)api->fb_height && sx >= 0 && sx < (int)api->fb_width) {
            api->fb_base[sy * api->fb_width + sx] = 0xFF0000;
        }
    }
    
    api->puts("Clock drawn! Uptime: ");
    char buf[32];
    int idx = 0;
    buf[idx++] = '0' + (hours / 10);
    buf[idx++] = '0' + (hours % 10);
    buf[idx++] = ':';
    buf[idx++] = '0' + (minutes / 10);
    buf[idx++] = '0' + (minutes % 10);
    buf[idx++] = ':';
    buf[idx++] = '0' + (seconds / 10);
    buf[idx++] = '0' + (seconds % 10);
    buf[idx++] = '\n';
    buf[idx] = '\0';
    api->puts(buf);
    
    return 0;
}

/* ===================================================================== */
/* Demo Application: Snake Game */
/* ===================================================================== */
#define SNAKE_GRID_SIZE 20
#define SNAKE_MAX_LEN 100

static int snake_app_main(kapi_t *api, int argc, char **argv) {
    (void)argc; (void)argv;
    
    api->puts("\n=== Vib-OS Snake ===\n");
    api->puts("Use mouse to control direction!\n");
    
    if (!api->fb_base) {
        api->puts("No framebuffer available\n");
        return -1;
    }
    
    /* Snake state */
    int snake_x[SNAKE_MAX_LEN];
    int snake_y[SNAKE_MAX_LEN];
    int snake_len = 5;
    int dir_x = 1, dir_y = 0;
    
    /* Initialize snake in center */
    int grid_w = api->fb_width / SNAKE_GRID_SIZE;
    int grid_h = api->fb_height / SNAKE_GRID_SIZE;
    int start_x = grid_w / 2;
    int start_y = grid_h / 2;
    
    for (int i = 0; i < snake_len; i++) {
        snake_x[i] = start_x - i;
        snake_y[i] = start_y;
    }
    
    /* Food position */
    int food_x = start_x + 5;
    int food_y = start_y;
    
    int score = 0;
    int game_over = 0;
    
    /* Game loop - run for limited iterations */
    for (int frame = 0; frame < 200 && !game_over; frame++) {
        /* Clear screen to dark */
        for (uint32_t y = 0; y < api->fb_height; y++) {
            for (uint32_t x = 0; x < api->fb_width; x++) {
                api->fb_base[y * api->fb_width + x] = 0x1E1E2E;
            }
        }
        
        /* Check mouse for direction */
        int mx, my;
        api->mouse_get_pos(&mx, &my);
        int head_px = snake_x[0] * SNAKE_GRID_SIZE;
        int head_py = snake_y[0] * SNAKE_GRID_SIZE;
        
        /* Change direction based on mouse relative to head */
        if (mx > head_px + SNAKE_GRID_SIZE && dir_x == 0) { dir_x = 1; dir_y = 0; }
        else if (mx < head_px - SNAKE_GRID_SIZE && dir_x == 0) { dir_x = -1; dir_y = 0; }
        else if (my > head_py + SNAKE_GRID_SIZE && dir_y == 0) { dir_x = 0; dir_y = 1; }
        else if (my < head_py - SNAKE_GRID_SIZE && dir_y == 0) { dir_x = 0; dir_y = -1; }
        
        /* Move snake */
        int new_x = snake_x[0] + dir_x;
        int new_y = snake_y[0] + dir_y;
        
        /* Wrap around */
        if (new_x < 0) new_x = grid_w - 1;
        if (new_x >= grid_w) new_x = 0;
        if (new_y < 0) new_y = grid_h - 1;
        if (new_y >= grid_h) new_y = 0;
        
        /* Self collision check */
        for (int i = 0; i < snake_len; i++) {
            if (snake_x[i] == new_x && snake_y[i] == new_y) {
                game_over = 1;
                break;
            }
        }
        
        if (!game_over) {
            /* Move body */
            for (int i = snake_len - 1; i > 0; i--) {
                snake_x[i] = snake_x[i-1];
                snake_y[i] = snake_y[i-1];
            }
            snake_x[0] = new_x;
            snake_y[0] = new_y;
            
            /* Check food */
            if (snake_x[0] == food_x && snake_y[0] == food_y) {
                score++;
                if (snake_len < SNAKE_MAX_LEN) snake_len++;
                /* New food position */
                food_x = (food_x + 7) % grid_w;
                food_y = (food_y + 11) % grid_h;
            }
        }
        
        /* Draw snake */
        for (int i = 0; i < snake_len; i++) {
            uint32_t color = (i == 0) ? 0x00FF00 : 0x00AA00;  /* Head brighter */
            int sx = snake_x[i] * SNAKE_GRID_SIZE;
            int sy = snake_y[i] * SNAKE_GRID_SIZE;
            for (int dy = 1; dy < SNAKE_GRID_SIZE - 1; dy++) {
                for (int dx = 1; dx < SNAKE_GRID_SIZE - 1; dx++) {
                    int px = sx + dx;
                    int py = sy + dy;
                    if ((uint32_t)py < api->fb_height && (uint32_t)px < api->fb_width) {
                        api->fb_base[py * api->fb_width + px] = color;
                    }
                }
            }
        }
        
        /* Draw food (red) */
        for (int dy = 2; dy < SNAKE_GRID_SIZE - 2; dy++) {
            for (int dx = 2; dx < SNAKE_GRID_SIZE - 2; dx++) {
                int px = food_x * SNAKE_GRID_SIZE + dx;
                int py = food_y * SNAKE_GRID_SIZE + dy;
                if ((uint32_t)py < api->fb_height && (uint32_t)px < api->fb_width) {
                    api->fb_base[py * api->fb_width + px] = 0xFF0000;
                }
            }
        }
        
        api->sleep_ms(100);  /* Game speed */
    }
    
    /* Show score */
    api->puts("Game Over! Score: ");
    char sbuf[16];
    sbuf[0] = '0' + (score / 10);
    sbuf[1] = '0' + (score % 10);
    sbuf[2] = '\n';
    sbuf[3] = '\0';
    api->puts(sbuf);
    
    return 0;
}

/* ===================================================================== */
/* Demo Application: System Monitor */
/* ===================================================================== */
static int sysmon_app_main(kapi_t *api, int argc, char **argv) {
    (void)argc; (void)argv;
    
    api->puts("\n=== Vib-OS System Monitor ===\n\n");
    
    /* Display system information */
    api->puts("SYSTEM INFO\n");
    api->puts("-----------\n");
    api->puts("OS:       Vib-OS v0.5.0\n");
    api->puts("Arch:     ARM64 (AArch64)\n");
    api->puts("Platform: QEMU virt\n\n");
    
    api->puts("DISPLAY\n");
    api->puts("-------\n");
    char buf[64];
    int idx = 0;
    api->puts("Resolution: ");
    idx = 0;
    uint32_t w = api->fb_width;
    buf[idx++] = '0' + (w / 1000) % 10;
    buf[idx++] = '0' + (w / 100) % 10;
    buf[idx++] = '0' + (w / 10) % 10;
    buf[idx++] = '0' + w % 10;
    buf[idx++] = 'x';
    uint32_t h = api->fb_height;
    buf[idx++] = '0' + (h / 1000) % 10;
    buf[idx++] = '0' + (h / 100) % 10;
    buf[idx++] = '0' + (h / 10) % 10;
    buf[idx++] = '0' + h % 10;
    buf[idx++] = '\n';
    buf[idx] = '\0';
    api->puts(buf);
    api->puts("Color:      32-bit ARGB\n");
    api->puts("Compositor: Double-buffered\n\n");
    
    api->puts("MEMORY\n");
    api->puts("------\n");
    api->puts("Heap:     8 MB\n");
    api->puts("PMM:      Buddy allocator\n\n");
    
    api->puts("UPTIME\n");
    api->puts("------\n");
    uint64_t ticks = api->get_uptime_ticks();
    int secs = (int)(ticks / 100);
    int mins = secs / 60;
    int hrs = mins / 60;
    secs %= 60;
    mins %= 60;
    
    idx = 0;
    buf[idx++] = '0' + (hrs / 10);
    buf[idx++] = '0' + (hrs % 10);
    buf[idx++] = ':';
    buf[idx++] = '0' + (mins / 10);
    buf[idx++] = '0' + (mins % 10);
    buf[idx++] = ':';
    buf[idx++] = '0' + (secs / 10);
    buf[idx++] = '0' + (secs % 10);
    buf[idx++] = '\n';
    buf[idx] = '\0';
    api->puts(buf);
    
    /* Draw system bars if framebuffer available */
    if (api->fb_base) {
        int bar_x = 50;
        int bar_y = api->fb_height - 150;
        int bar_w = 200;
        int bar_h = 20;
        
        /* CPU bar (simulated 45%) */
        for (int y = 0; y < bar_h; y++) {
            for (int x = 0; x < bar_w; x++) {
                int px = bar_x + x;
                int py = bar_y + y;
                uint32_t color = (x < bar_w * 45 / 100) ? 0x00FF00 : 0x333333;
                if ((uint32_t)py < api->fb_height && (uint32_t)px < api->fb_width) {
                    api->fb_base[py * api->fb_width + px] = color;
                }
            }
        }
        
        /* Memory bar (simulated 62%) */
        bar_y += 30;
        for (int y = 0; y < bar_h; y++) {
            for (int x = 0; x < bar_w; x++) {
                int px = bar_x + x;
                int py = bar_y + y;
                uint32_t color = (x < bar_w * 62 / 100) ? 0x00AAFF : 0x333333;
                if ((uint32_t)py < api->fb_height && (uint32_t)px < api->fb_width) {
                    api->fb_base[py * api->fb_width + px] = color;
                }
            }
        }
    }
    
    return 0;
}

/* ===================================================================== */
/* Demo Application: Mandelbrot Fractal */
/* ===================================================================== */
static int mandelbrot_app_main(kapi_t *api, int argc, char **argv) {
    (void)argc; (void)argv;
    
    api->puts("\n=== Vib-OS Mandelbrot Viewer ===\n");
    api->puts("Rendering fractal...\n");
    
    if (!api->fb_base) {
        api->puts("No framebuffer available\n");
        return -1;
    }
    
    int width = api->fb_width;
    int height = api->fb_height;
    int max_iter = 50;
    
    /* Fixed-point math (16.16 format) */
    #define FP_SHIFT 16
    #define FP_ONE (1 << FP_SHIFT)
    #define FP_MUL(a, b) (((long long)(a) * (b)) >> FP_SHIFT)
    
    /* View: x from -2.5 to 1, y from -1 to 1 */
    int x_min = -2 * FP_ONE - FP_ONE / 2;  /* -2.5 */
    int x_max = 1 * FP_ONE;                 /* 1.0 */
    int y_min = -1 * FP_ONE;                /* -1.0 */
    int y_max = 1 * FP_ONE;                 /* 1.0 */
    
    int x_scale = (x_max - x_min) / width;
    int y_scale = (y_max - y_min) / height;
    
    /* Color palette */
    uint32_t palette[16] = {
        0x000764, 0x206BCB, 0xEDFFFF, 0xFFAA00,
        0x000200, 0x0C2161, 0x1E81B0, 0x76E5FC,
        0xFBFECC, 0xED8A0A, 0x9A0200, 0x280000,
        0x2D0070, 0x6600AA, 0x9900FF, 0xCC00FF
    };
    
    for (int py = 0; py < height; py++) {
        int y0 = y_min + py * y_scale;
        
        for (int px = 0; px < width; px++) {
            int x0 = x_min + px * x_scale;
            
            int x = 0, y = 0;
            int iter = 0;
            
            while (iter < max_iter) {
                int x2 = FP_MUL(x, x);
                int y2 = FP_MUL(y, y);
                
                if (x2 + y2 > 4 * FP_ONE) break;
                
                int xtemp = x2 - y2 + x0;
                y = 2 * FP_MUL(x, y) + y0;
                x = xtemp;
                iter++;
            }
            
            uint32_t color;
            if (iter == max_iter) {
                color = 0x000000;  /* Black for set */
            } else {
                color = palette[iter % 16];
            }
            
            api->fb_base[py * width + px] = color;
        }
        
        /* Yield every 50 rows to keep system responsive */
        if (py % 50 == 0) {
            api->yield();
        }
    }
    
    #undef FP_SHIFT
    #undef FP_ONE
    #undef FP_MUL
    
    api->puts("Fractal rendered!\n");
    return 0;
}

/* ===================================================================== */
/* Simple test app */
/* ===================================================================== */
static int test_app_main(kapi_t *api, int argc, char **argv) {
    (void)argc; (void)argv;
    
    api->puts("Hello from test app!\n");
    api->puts("Framebuffer: ");
    
    /* Draw a red rectangle on screen */
    if (api->fb_base) {
        for (int y = 100; y < 200; y++) {
            for (int x = 100; x < 300; x++) {
                api->fb_base[y * api->fb_width + x] = 0xFF0000;  /* Red */
            }
        }
        api->puts("Drew red rectangle!\n");
    }
    
    return 0;
}

/* ===================================================================== */
/* App Registry */
/* ===================================================================== */
typedef struct {
    const char *name;
    app_main_fn main_fn;
} app_entry_t;

static app_entry_t app_registry[] = {
    { "test",       test_app_main },
    { "clock",      clock_app_main },
    { "snake",      snake_app_main },
    { "sysmon",     sysmon_app_main },
    { "mandelbrot", mandelbrot_app_main },
    { NULL, NULL }
};

/* Run an embedded application by name */
int app_run(const char *name, int argc, char **argv) {
    printk(KERN_INFO "[APP] Running: %s\n", name);
    
    /* Find app in registry */
    for (int i = 0; app_registry[i].name != NULL; i++) {
        /* Simple strcmp */
        const char *a = name;
        const char *b = app_registry[i].name;
        while (*a && *b && *a == *b) { a++; b++; }
        if (*a == *b) {
            /* Match found */
            kapi_t *api = kapi_get();
            return app_registry[i].main_fn(api, argc, argv);
        }
    }
    
    printk(KERN_WARNING "[APP] App not found: %s\n", name);
    return -1;
}
