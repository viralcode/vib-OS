/*
 * vib-OS - Boot Configuration and Splash Screen
 * 
 * Configurable boot with splash screen and boot target selection.
 */

#include "types.h"
#include "printk.h"

/* ===================================================================== */
/* Boot Configuration */
/* ===================================================================== */

#define BOOT_TIMEOUT_DEFAULT    5
#define BOOT_TARGET_KERNEL      0
#define BOOT_TARGET_RECOVERY    1
#define BOOT_TARGET_SHELL       2

struct boot_config {
    uint32_t magic;             /* 0xB007C0DE */
    uint32_t version;
    uint32_t timeout_seconds;
    uint32_t default_target;
    bool show_splash;
    bool verbose_boot;
    bool debug_mode;
    char kernel_cmdline[256];
    char default_kernel[128];
    char recovery_kernel[128];
    /* Display settings */
    uint32_t splash_bg_color;
    uint32_t splash_fg_color;
    uint32_t progress_color;
};

static struct boot_config boot_cfg = {
    .magic = 0xB007C0DE,
    .version = 1,
    .timeout_seconds = BOOT_TIMEOUT_DEFAULT,
    .default_target = BOOT_TARGET_KERNEL,
    .show_splash = true,
    .verbose_boot = false,
    .debug_mode = false,
    .kernel_cmdline = "console=ttyS0 rootwait",
    .default_kernel = "/boot/vib-os.elf",
    .recovery_kernel = "/boot/recovery.elf",
    .splash_bg_color = 0x1A1A2E,   /* Dark blue-gray */
    .splash_fg_color = 0xE94560,   /* Accent pink */
    .progress_color = 0x16213E,   /* Progress bar background */
};

/* ===================================================================== */
/* Boot Menu */
/* ===================================================================== */

struct boot_entry {
    char name[64];
    char path[128];
    char cmdline[256];
    bool is_default;
};

#define MAX_BOOT_ENTRIES 8
static struct boot_entry boot_entries[MAX_BOOT_ENTRIES];
static int num_boot_entries = 0;

int boot_add_entry(const char *name, const char *path, const char *cmdline)
{
    if (num_boot_entries >= MAX_BOOT_ENTRIES) return -1;
    
    struct boot_entry *entry = &boot_entries[num_boot_entries++];
    
    for (int i = 0; i < 63 && name[i]; i++) {
        entry->name[i] = name[i];
        entry->name[i + 1] = '\0';
    }
    
    for (int i = 0; i < 127 && path[i]; i++) {
        entry->path[i] = path[i];
        entry->path[i + 1] = '\0';
    }
    
    for (int i = 0; i < 255 && cmdline[i]; i++) {
        entry->cmdline[i] = cmdline[i];
        entry->cmdline[i + 1] = '\0';
    }
    
    entry->is_default = (num_boot_entries == 1);
    
    return 0;
}

/* ===================================================================== */
/* Splash Screen */
/* ===================================================================== */

/* ASCII art logo */
static const char *splash_logo[] = {
    "",
    "        _  _         ___  ____  ",
    " __   _(_)| |__     / _ \\/ ___| ",
    " \\ \\ / / || '_ \\   | | | \\___ \\ ",
    "  \\ V /| || |_) |  | |_| |___) |",
    "   \\_/ |_||_.__/    \\___/|____/ ",
    "",
    "        ARM64 Operating System",
    "",
    NULL
};

static void draw_splash_text(void)
{
    printk("\n");
    
    for (int i = 0; splash_logo[i] != NULL; i++) {
        printk("%s\n", splash_logo[i]);
    }
    
    printk("\n");
}

static void draw_progress_bar(int percent)
{
    const int bar_width = 40;
    int filled = (bar_width * percent) / 100;
    
    printk("\r  [");
    for (int i = 0; i < bar_width; i++) {
        if (i < filled) {
            printk("=");
        } else if (i == filled) {
            printk(">");
        } else {
            printk(" ");
        }
    }
    printk("] %3d%%", percent);
}

/* ===================================================================== */
/* Boot Process */
/* ===================================================================== */

typedef void (*progress_callback_t)(const char *stage, int percent);

static progress_callback_t boot_progress_cb = NULL;

void boot_set_progress_callback(progress_callback_t cb)
{
    boot_progress_cb = cb;
}

void boot_report_progress(const char *stage, int percent)
{
    if (boot_cfg.show_splash) {
        draw_progress_bar(percent);
        if (boot_cfg.verbose_boot) {
            printk("  %s", stage);
        }
        printk("\n");
    }
    
    if (boot_progress_cb) {
        boot_progress_cb(stage, percent);
    }
}

/* Show boot menu and wait for selection */
int boot_show_menu(void)
{
    printk("\n");
    printk("  Boot Menu\n");
    printk("  =========\n\n");
    
    for (int i = 0; i < num_boot_entries; i++) {
        printk("    %d) %s%s\n", 
               i + 1, 
               boot_entries[i].name,
               boot_entries[i].is_default ? " [default]" : "");
    }
    
    printk("\n");
    printk("  Press 1-%d to select, or ENTER for default\n", num_boot_entries);
    printk("  Booting default in %d seconds...\n\n", boot_cfg.timeout_seconds);
    
    /* TODO: Implement actual input handling and timeout */
    
    /* Return default for now */
    for (int i = 0; i < num_boot_entries; i++) {
        if (boot_entries[i].is_default) {
            return i;
        }
    }
    
    return 0;
}

/* Main boot sequence */
void boot_init(void)
{
    printk(KERN_INFO "BOOT: Initializing boot manager\n");
    
    if (boot_cfg.show_splash) {
        draw_splash_text();
    }
    
    /* Add default boot entries */
    boot_add_entry("vib-OS", boot_cfg.default_kernel, boot_cfg.kernel_cmdline);
    boot_add_entry("Recovery Mode", boot_cfg.recovery_kernel, "single recovery");
    boot_add_entry("Debug Shell", "/bin/sh", "debug");
    
    printk(KERN_INFO "BOOT: %d boot entries configured\n", num_boot_entries);
}

/* Get boot configuration */
struct boot_config *boot_get_config(void)
{
    return &boot_cfg;
}

/* Set boot timeout */
void boot_set_timeout(uint32_t seconds)
{
    boot_cfg.timeout_seconds = seconds;
}

/* Set default target */
void boot_set_default(int target)
{
    for (int i = 0; i < num_boot_entries; i++) {
        boot_entries[i].is_default = (i == target);
    }
    boot_cfg.default_target = target;
}

/* Parse boot command line */
void boot_parse_cmdline(const char *cmdline)
{
    /* Copy to config */
    for (int i = 0; i < 255 && cmdline[i]; i++) {
        boot_cfg.kernel_cmdline[i] = cmdline[i];
        boot_cfg.kernel_cmdline[i + 1] = '\0';
    }
    
    /* Parse options */
    const char *p = cmdline;
    while (*p) {
        if (p[0] == 'v' && p[1] == 'e' && p[2] == 'r' && p[3] == 'b') {
            boot_cfg.verbose_boot = true;
        }
        if (p[0] == 'd' && p[1] == 'e' && p[2] == 'b' && p[3] == 'u') {
            boot_cfg.debug_mode = true;
        }
        if (p[0] == 'n' && p[1] == 'o' && p[2] == 's' && p[3] == 'p') {
            boot_cfg.show_splash = false;
        }
        p++;
    }
    
    printk(KERN_INFO "BOOT: Cmdline parsed - verbose=%d debug=%d splash=%d\n",
           boot_cfg.verbose_boot, boot_cfg.debug_mode, boot_cfg.show_splash);
}
