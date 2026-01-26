/*
 * UnixOS Kernel - Main Entry Point
 *
 * This is the C entry point called from boot.S after basic
 * hardware initialization is complete.
 */

#include "arch/arch.h"
#include "drivers/pci.h"
#include "drivers/uart.h"
#include "drivers/virtio_9p.h"
#include "fs/vfs.h"
#include "media/seed_assets.h"
#include "mm/kmalloc.h"
#include "mm/pmm.h"
#include "mm/vmm.h"
#include "printk.h"
#include "sched/sched.h"
#include "types.h"

/* Kernel version */
#define VIBOS_VERSION_MAJOR 0
#define VIBOS_VERSION_MINOR 5
#define VIBOS_VERSION_PATCH 0

/* External symbols from linker script */
extern char __kernel_start[];
extern char __kernel_end[];
extern char __bss_start[];
extern char __bss_end[];

/* Forward declarations */
static void print_banner(void);
static void init_subsystems(void *dtb);
static void start_init_process(void);

/* RamFS helper APIs (ramfs.c) */
extern int ramfs_create_file_bytes(const char *path, mode_t mode,
                                   const uint8_t *data, size_t size);
extern int ramfs_create_file(const char *path, mode_t mode, const char *content);

static int str_ends_with_ci(const char *name, const char *ext) {
  if (!name || !ext)
    return 0;
  int nlen = 0;
  int elen = 0;
  while (name[nlen])
    nlen++;
  while (ext[elen])
    elen++;
  if (elen == 0 || nlen < elen)
    return 0;
  for (int i = 0; i < elen; i++) {
    char a = name[nlen - elen + i];
    char b = ext[i];
    if (a >= 'A' && a <= 'Z')
      a = (char)(a + 32);
    if (b >= 'A' && b <= 'Z')
      b = (char)(b + 32);
    if (a != b)
      return 0;
  }
  return 1;
}

static int str_starts_with(const char *s, const char *prefix) {
  if (!s || !prefix)
    return 0;
  for (int i = 0; prefix[i]; i++) {
    if (!s[i])
      return 0;
    if (s[i] != prefix[i])
      return 0;
  }
  return 1;
}

static int vfs_path_exists(const char *path) {
  struct file *f = vfs_open(path, O_RDONLY, 0);
  if (f) {
    vfs_close(f);
    return 1;
  }
  return 0;
}

static void import_hostshare_videos(void) {
  /* Always rewrite a status file to avoid stale info across builds. */
  ramfs_create_file("/Videos/HOSTSHARE.txt", 0644,
                    "Hostshare: probing...\n");

  /* Hostshare is an optional QEMU dev feature; no-op if absent. */
  if (virtio_9p_init() != 0) {
    printk(KERN_INFO "Hostshare: virtio-9p not available\n");
    ramfs_create_file(
        "/Videos/HOSTSHARE.txt", 0644,
        "Hostshare not available.\n\n"
        "This feature requires running Vib-OS via:\n"
        "  make run-gui\n"
        "or\n"
        "  make run-gpu\n\n"
        "and placing files on the host in:\n"
        "  hostshare/Videos/\n\n"
        "Note: Vib-OS video playback supports MJPEG AVI only.\n");
    return;
  }

  /* Optional: import a small test file for quick sanity checks */
  {
    uint8_t *tdata = NULL;
    size_t tsz = 0;
    if (virtio_9p_read_file("Videos/test.txt", &tdata, &tsz, 64 * 1024) == 0 &&
        tdata && tsz > 0) {
      const char *dst = "/Videos/test.txt";
      if (vfs_path_exists(dst)) {
        dst = "/Videos/host_test.txt";
      }
      ramfs_create_file_bytes(dst, 0644, tdata, tsz);
      kfree(tdata);
    } else if (tdata) {
      kfree(tdata);
    }
  }

  /* Report mount tag */
  {
    const char *tag = virtio_9p_get_tag();
    if (tag && tag[0]) {
      char msg[128];
      int p = 0;
      const char *a = "Hostshare tag: ";
      for (int i = 0; a[i] && p < 127; i++)
        msg[p++] = a[i];
      for (int i = 0; tag[i] && p < 127; i++)
        msg[p++] = tag[i];
      msg[p++] = '\n';
      msg[p] = '\0';
      ramfs_create_file("/Videos/HOSTSHARE_TAG.txt", 0644, msg);
    }
  }

  uint8_t *list = NULL;
  size_t list_sz = 0;
  if (virtio_9p_read_file("Videos/videos.txt", &list, &list_sz, 64 * 1024) !=
      0) {
    printk(KERN_INFO "Hostshare: no Videos/videos.txt\n");
    ramfs_create_file(
        "/Videos/HOSTSHARE.txt", 0644,
        "Hostshare is available, but no playlist was found.\n\n"
        "Create this file on the host:\n"
        "  hostshare/Videos/videos.txt\n\n"
        "Put MJPEG AVI files in:\n"
        "  hostshare/Videos/\n\n"
        "Then list them in videos.txt, one per line, like:\n"
        "  Videos/myclip.avi\n");
    return;
  }

  /* Save playlist into RamFS for debugging. */
  {
    size_t cap = (list_sz < 32768) ? list_sz : 32768;
    char *tmp = (char *)kmalloc(cap + 1, GFP_KERNEL);
    if (tmp) {
      for (size_t i = 0; i < cap; i++) {
        char c = (char)list[i];
        if (c == '\0')
          c = ' ';
        tmp[i] = c;
      }
      tmp[cap] = '\0';
      ramfs_create_file("/Videos/HOSTSHARE_VIDEOS.txt", 0644, tmp);
      kfree(tmp);
    }
  }

  int imported = 0;
  int attempted = 0;
  int failed = 0;
  int line_len = 0;
  char line[128];

  for (size_t i = 0; i <= list_sz; i++) {
    char c = (i < list_sz) ? (char)list[i] : '\n';
    if (c == '\r')
      continue;
    if (c != '\n') {
      if (line_len < (int)sizeof(line) - 1)
        line[line_len++] = c;
      continue;
    }

    line[line_len] = '\0';
    line_len = 0;

    /* Trim leading spaces */
    int s = 0;
    while (line[s] == ' ' || line[s] == '\t')
      s++;
    if (line[s] == '\0' || line[s] == '#')
      continue;

    /* Trim trailing spaces */
    int e = s;
    while (line[e])
      e++;
    while (e > s && (line[e - 1] == ' ' || line[e - 1] == '\t'))
      e--;
    line[e] = '\0';

    const char *src = line + s;
    if (!str_ends_with_ci(src, ".avi"))
      continue;
    attempted++;

    /* Destination name: /Videos/<basename> (avoid collisions)
     *
     * If the source is a host-converted MJPEG output:
     *   Videos/_mjpg/<name>_mjpg.avi
     * then import it as:
     *   /Videos/<name>.avi
     * so it looks like the original file inside Vib-OS. */
    const char *base = src;
    for (const char *p = src; *p; p++)
      if (*p == '/')
        base = p + 1;
    if (*base == '\0')
      continue;

    char fixed_base[128];
    const char *use_base = base;
    if (str_starts_with(src, "Videos/_mjpg/") &&
        str_ends_with_ci(base, "_mjpg.avi")) {
      int blen = 0;
      while (base[blen] && blen < (int)sizeof(fixed_base) - 1)
        blen++;
      /* strip "_mjpg.avi" (9 chars) then add ".avi" */
      if (blen > 9) {
        int keep = blen - 9;
        if (keep + 4 < (int)sizeof(fixed_base)) {
          int bi = 0;
          for (; bi < keep; bi++)
            fixed_base[bi] = base[bi];
          fixed_base[bi++] = '.';
          fixed_base[bi++] = 'a';
          fixed_base[bi++] = 'v';
          fixed_base[bi++] = 'i';
          fixed_base[bi] = '\0';
          use_base = fixed_base;
        }
      }
    }

    char dst[256];
    int di = 0;
    const char *prefix = "/Videos/";
    for (int j = 0; prefix[j] && di < (int)sizeof(dst) - 1; j++)
      dst[di++] = prefix[j];

    /* Copy basename */
    int bi = 0;
    while (use_base[bi] && di < (int)sizeof(dst) - 1) {
      dst[di++] = use_base[bi++];
    }
    dst[di] = '\0';

    if (vfs_path_exists(dst)) {
      /* prefix with host_ */
      di = 0;
      for (int j = 0; prefix[j] && di < (int)sizeof(dst) - 1; j++)
        dst[di++] = prefix[j];
      const char *hp = "host_";
      for (int j = 0; hp[j] && di < (int)sizeof(dst) - 1; j++)
        dst[di++] = hp[j];
      bi = 0;
      while (use_base[bi] && di < (int)sizeof(dst) - 1) {
        dst[di++] = use_base[bi++];
      }
      dst[di] = '\0';
    }

    uint8_t *data = NULL;
    size_t sz = 0;
    if (virtio_9p_read_file(src, &data, &sz, 64u * 1024u * 1024u) != 0) {
      printk(KERN_WARNING "Hostshare: failed to read %s\n", src);
      failed++;
      continue;
    }

    if (sz == 0) {
      kfree(data);
      continue;
    }

    int rc = ramfs_create_file_bytes(dst, 0644, data, sz);
    kfree(data);
    if (rc == 0) {
      imported++;
    } else {
      failed++;
    }

    if (imported >= 10) {
      printk(KERN_INFO "Hostshare: import limit reached\n");
      break;
    }
  }

  kfree(list);
  printk(KERN_INFO "Hostshare: imported %d video(s) into /Videos\n", imported);
  {
    char msg[256];
    int pos = 0;
    const char *p1 = "Hostshare OK.\n";
    for (int i = 0; p1[i] && pos < 255; i++)
      msg[pos++] = p1[i];
    const char *p2 = "Imported videos: ";
    for (int i = 0; p2[i] && pos < 255; i++)
      msg[pos++] = p2[i];
    /* crude decimal */
    int n = imported;
    char tmp[16];
    int ti = 0;
    if (n == 0)
      tmp[ti++] = '0';
    while (n > 0 && ti < 15) {
      tmp[ti++] = (char)('0' + (n % 10));
      n /= 10;
    }
    for (int i = ti - 1; i >= 0 && pos < 255; i--)
      msg[pos++] = tmp[i];
    msg[pos++] = '\n';

    const char *p3 = "Playlist entries: ";
    for (int i = 0; p3[i] && pos < 255; i++)
      msg[pos++] = p3[i];
    n = attempted;
    ti = 0;
    if (n == 0)
      tmp[ti++] = '0';
    while (n > 0 && ti < 15) {
      tmp[ti++] = (char)('0' + (n % 10));
      n /= 10;
    }
    for (int i = ti - 1; i >= 0 && pos < 255; i--)
      msg[pos++] = tmp[i];
    msg[pos++] = '\n';

    const char *p4 = "Failed imports: ";
    for (int i = 0; p4[i] && pos < 255; i++)
      msg[pos++] = p4[i];
    n = failed;
    ti = 0;
    if (n == 0)
      tmp[ti++] = '0';
    while (n > 0 && ti < 15) {
      tmp[ti++] = (char)('0' + (n % 10));
      n /= 10;
    }
    for (int i = ti - 1; i >= 0 && pos < 255; i--)
      msg[pos++] = tmp[i];
    msg[pos++] = '\n';
    msg[pos] = '\0';
    ramfs_create_file("/Videos/HOSTSHARE.txt", 0644, msg);
  }
}

/*
 * kernel_main - Main kernel entry point
 * @dtb: Pointer to device tree blob passed by bootloader
 *
 * This function never returns. After initialization, it either:
 * 1. Starts the init process and enters the scheduler
 * 2. Panics if initialization fails
 */
void kernel_main(void *dtb) {
  /* Initialize early console for debugging */
  uart_early_init();

  /* Print boot banner */
  print_banner();

  (void)dtb; /* Suppress unused warning */
  (void)__kernel_start;
  (void)__kernel_end;

  /* Initialize all kernel subsystems */
  init_subsystems(dtb);

  printk(KERN_INFO "All subsystems initialized successfully\n");
  printk(KERN_INFO "Starting init process...\n\n");

  /* Start the first userspace process */
  start_init_process();

  /* This point should never be reached */
  panic("kernel_main returned unexpectedly!");
}

/*
 * print_banner - Display kernel boot banner
 */
static void print_banner(void) {
  printk("\n");
  printk("        _  _         ___  ____  \n");
  printk(" __   _(_)| |__     / _ \\/ ___| \n");
  printk(" \\ \\ / / || '_ \\   | | | \\___ \\ \n");
  printk("  \\ V /| || |_) |  | |_| |___) |\n");
  printk("   \\_/ |_||_.__/    \\___/|____/ \n");
  printk("\n");
  printk("Vib-OS v%d.%d.%d - ARM64 with GUI\n", VIBOS_VERSION_MAJOR,
         VIBOS_VERSION_MINOR, VIBOS_VERSION_PATCH);
  printk("A Unix-like operating system for ARM64\n");
  printk("Copyright (c) 2026 Vib-OS Project\n");
  printk("\n");
}

/*
 * init_subsystems - Initialize all kernel subsystems
 * @dtb: Device tree blob for hardware discovery
 */
static void init_subsystems(void *dtb) {
  int ret;

  /* ================================================================= */
  /* Phase 1: Core Hardware */
  /* ================================================================= */

  printk(KERN_INFO "[INIT] Phase 1: Core Hardware\n");

  /* Parse device tree for hardware information */
  printk(KERN_INFO "  Parsing device tree...\n");
  (void)dtb; /* TODO: dtb_parse(dtb); */

  /* Initialize interrupt controller */
  printk(KERN_INFO "  Initializing interrupt controller...\n");
  arch_irq_init();

  /* Initialize system timer */
  printk(KERN_INFO "  Initializing timer...\n");
  arch_timer_init();

  /* ================================================================= */
  /* Phase 2: Memory Management */
  /* ================================================================= */

  printk(KERN_INFO "[INIT] Phase 2: Memory Management\n");

  /* Initialize physical memory manager */
  printk(KERN_INFO "  Initializing physical memory manager...\n");
  ret = pmm_init();
  if (ret < 0) {
    panic("Failed to initialize physical memory manager!");
  }
  printk(KERN_INFO "  About to init VMM...\n");

  /* Initialize virtual memory manager */
  printk(KERN_INFO "  Initializing virtual memory manager...\n");
  ret = vmm_init();
  if (ret < 0) {
    panic("Failed to initialize virtual memory manager!");
  }

  /* Initialize kernel heap */
  printk(KERN_INFO "  Initializing kernel heap...\n");
  extern void kmalloc_init(void);
  kmalloc_init();

  /* ================================================================= */
  /* Phase 3: Process Management */
  /* ================================================================= */

  printk(KERN_INFO "[INIT] Phase 3: Process Management\n");

  /* Initialize scheduler */
  printk(KERN_INFO "  Initializing scheduler...\n");
  sched_init();

  /* Initialize process subsystem */
  printk(KERN_INFO "  Initializing process subsystem...\n");
  extern void process_init(void);
  process_init();

  /* ================================================================= */
  /* Phase 4: Filesystems */
  /* ================================================================= */

  printk(KERN_INFO "[INIT] Phase 4: Filesystems\n");

  /* Initialize Virtual Filesystem */
  printk(KERN_INFO "  Initializing VFS...\n");
  /* Initialize Virtual Filesystem */
  printk(KERN_INFO "  Initializing VFS...\n");
  vfs_init();

  /* Initialize and Register RamFS */
  printk(KERN_INFO "  Initializing RamFS...\n");
  extern int ramfs_init(void);
  ramfs_init();

  /* Mount root filesystem */
  printk(KERN_INFO "  Mounting root filesystem...\n");
  if (vfs_mount("ramfs", "/", "ramfs", 0, NULL) != 0) {
    panic("Failed to mount root filesystem!");
  }

  /* Populate filesystem with sample data */
  extern int ramfs_create_dir(const char *path, mode_t mode);
  extern int ramfs_create_file(const char *path, mode_t mode,
                               const char *content);
  extern int ramfs_create_file_bytes(const char *path, mode_t mode,
                                     const uint8_t *data, size_t size);

  ramfs_create_dir("Documents", 0755);
  ramfs_create_dir("Downloads", 0755);
  ramfs_create_dir("Pictures", 0755);
  ramfs_create_dir("Videos", 0755);
  ramfs_create_dir("Host", 0755);
  ramfs_create_dir("System", 0755);
  ramfs_create_dir("Desktop", 0755);

  /* Seed Desktop with sample files and folders */
  ramfs_create_file("/Desktop/notes.txt", 0644,
                    "Welcome to Vib-OS!\n\nThis is your desktop - right-click "
                    "for options!\n");
  ramfs_create_file("/Desktop/readme.txt", 0644,
                    "Vib-OS Desktop Manager\n\n- Double-click to open files\n- "
                    "Right-click for context menu\n");

  /* Create a subfolder on Desktop */
  extern int vfs_mkdir(const char *path, mode_t mode);
  vfs_mkdir("/Videos/Host", 0755);
  vfs_mkdir("/Desktop/Projects", 0755);
  ramfs_create_file("readme.txt", 0644,
                    "Welcome to Vib-OS!\nThis is a real file in RamFS.");
  ramfs_create_file("todo.txt", 0644,
                    "- Implement Browser\n- Fix Bugs\n- Sleep");
  ramfs_create_file_bytes("sample.mp3", 0644, vib_seed_mp3, vib_seed_mp3_len);
  ramfs_create_file(
      "Videos/README.txt", 0644,
      "Vib-OS Video Player (MJPEG AVI)\n\n"
      "Vib-OS currently plays MJPEG-in-AVI (video only).\n\n"
      "Quick test:\n"
      "  /Videos/demo.avi\n\n"
      "Hostshare (QEMU virtio-9p):\n"
      "  /Videos/Host/  (hostshare/Videos)\n"
      "  /Host/         (hostshare root)\n\n"
      "Drop AVI files into hostshare/Videos/ on the host, then run:\n"
      "  make run-gui\n"
      "or\n"
      "  make run-gpu\n\n"
      "Non-MJPEG AVIs will be auto-converted to MJPEG on the host, and imported\n"
      "into /Videos/ under their original names.\n\n"
      "Controls:\n"
      "  Space / P  - Play/Pause\n"
      "  [ / ]      - Seek -/+ 5 seconds\n"
      "  B / N      - Seek -/+ 1 second\n"
      "  F          - Fullscreen (ESC to exit)\n");

  /* Seed a small MJPEG demo AVI for first boot */
  ramfs_create_file_bytes("Videos/demo.avi", 0644, vib_seed_demo_avi,
                          vib_seed_demo_avi_len);

  /* Import additional videos from the hostshare (QEMU virtio-9p) if present. */
  import_hostshare_videos();

  /* Add baseline JPEG images to Pictures directory */
  extern const unsigned char bootstrap_landscape_jpg[];
  extern const unsigned int bootstrap_landscape_jpg_len;
  extern const unsigned char bootstrap_portrait_jpg[];
  extern const unsigned int bootstrap_portrait_jpg_len;
  extern const unsigned char bootstrap_square_jpg[];
  extern const unsigned int bootstrap_square_jpg_len;
  extern const unsigned char bootstrap_wallpaper_jpg[];
  extern const unsigned int bootstrap_wallpaper_jpg_len;
  extern const unsigned char bootstrap_wallpaper_png[];
  extern const unsigned int bootstrap_wallpaper_png_len;
  /* Real photos from the internet */
  extern const unsigned char bootstrap_nature_jpg[];
  extern const unsigned int bootstrap_nature_jpg_len;
  extern const unsigned char bootstrap_city_jpg[];
  extern const unsigned int bootstrap_city_jpg_len;
  extern const unsigned char bootstrap_httpbin_jpg[];
  extern const unsigned int bootstrap_httpbin_jpg_len;

  /* HD Wallpapers (high quality) */
  extern const unsigned char hd_wallpaper_landscape_jpg[];
  extern const unsigned int hd_wallpaper_landscape_jpg_len;
  extern const unsigned char hd_wallpaper_nature_jpg[];
  extern const unsigned int hd_wallpaper_nature_jpg_len;
  extern const unsigned char hd_wallpaper_city_jpg[];
  extern const unsigned int hd_wallpaper_city_jpg_len;

  /* Use HD wallpapers for main images */
  ramfs_create_file_bytes("Pictures/landscape.jpg", 0644,
                          hd_wallpaper_landscape_jpg,
                          hd_wallpaper_landscape_jpg_len);
  ramfs_create_file_bytes("Pictures/portrait.jpg", 0644, bootstrap_portrait_jpg,
                          bootstrap_portrait_jpg_len);
  ramfs_create_file_bytes("Pictures/square.jpg", 0644, bootstrap_square_jpg,
                          bootstrap_square_jpg_len);
  ramfs_create_file_bytes("Pictures/wallpaper.jpg", 0644,
                          bootstrap_wallpaper_jpg, bootstrap_wallpaper_jpg_len);
  ramfs_create_file_bytes("Pictures/wallpaper.png", 0644,
                          bootstrap_wallpaper_png, bootstrap_wallpaper_png_len);
  /* HD Photos */
  ramfs_create_file_bytes("Pictures/nature.jpg", 0644, hd_wallpaper_nature_jpg,
                          hd_wallpaper_nature_jpg_len);
  ramfs_create_file_bytes("Pictures/city.jpg", 0644, hd_wallpaper_city_jpg,
                          hd_wallpaper_city_jpg_len);
  ramfs_create_file_bytes("Pictures/pig.jpg", 0644, bootstrap_httpbin_jpg,
                          bootstrap_httpbin_jpg_len);

  /* Add PNG test image to Pictures */
  extern const unsigned char bootstrap_test_png[];
  extern const unsigned int bootstrap_test_png_len;
  ramfs_create_file_bytes("Pictures/test.png", 0644, bootstrap_test_png,
                          bootstrap_test_png_len);

  /* Mount proc, sys, dev (placeholders) */
  printk(KERN_INFO "  Mounting procfs...\n");

  /* Create examples directory with language demo files */
  ramfs_create_dir("examples", 0755);

  /* Python demo files */
  ramfs_create_file("examples/hello.py", 0644,
                    "# Hello World in Python for Vib-OS\n"
                    "# Run with: run hello.py\n\n"
                    "def greet(name):\n"
                    "    return 'Hello, ' + name + '!'\n\n"
                    "def main():\n"
                    "    print('Welcome to Vib-OS Python Demo')\n"
                    "    message = greet('Vib-OS User')\n"
                    "    print(message)\n\n"
                    "if __name__ == '__main__':\n"
                    "    main()\n");

  ramfs_create_file("examples/fibonacci.py", 0644,
                    "# Fibonacci Sequence in Python\n"
                    "# Run with: run fibonacci.py\n\n"
                    "def fibonacci(n):\n"
                    "    if n <= 0: return []\n"
                    "    fib = [0, 1]\n"
                    "    for i in range(2, n):\n"
                    "        fib.append(fib[i-1] + fib[i-2])\n"
                    "    return fib\n\n"
                    "print(fibonacci(10))\n");

  /* NanoLang demo files */
  ramfs_create_file("examples/hello.nano", 0644,
                    "// Hello World in NanoLang\n"
                    "// Run with: run hello.nano\n\n"
                    "fn greet(name: str) -> str {\n"
                    "    return 'Hello, ' + name + '!';\n"
                    "}\n\n"
                    "fn main() {\n"
                    "    print('Welcome to NanoLang');\n"
                    "    let msg = greet('Vib-OS');\n"
                    "    print(msg);\n"
                    "}\n");

  ramfs_create_file("examples/calculator.nano", 0644,
                    "// Calculator in NanoLang\n"
                    "fn add(a: int, b: int) -> int { return a + b; }\n"
                    "fn main() {\n"
                    "    print('42 + 7 = ');\n"
                    "    print(add(42, 7));\n"
                    "}\n");

  printk(KERN_INFO "  Mounting sysfs...\n");
  printk(KERN_INFO "  Mounting devfs...\n");

  /* ================================================================= */
  /* Phase 5: Device Drivers & GUI */
  /* ================================================================= */

  printk(KERN_INFO "[INIT] Phase 5: Device Drivers\n");

  /* Initialize framebuffer driver */
  printk(KERN_INFO "  Loading framebuffer driver...\n");
  extern int fb_init(void);
  extern void fb_get_info(uint32_t **buffer, uint32_t *width, uint32_t *height);
  fb_init();

  /* Initialize GUI windowing system */
  printk(KERN_INFO "  Initializing GUI...\n");
  extern int gui_init(uint32_t *framebuffer, uint32_t width, uint32_t height,
                      uint32_t pitch);
  extern struct window *gui_create_window(const char *title, int x, int y,
                                          int w, int h);
  extern void gui_compose(void);
  extern void gui_draw_cursor(void);

  uint32_t *fb_buffer;
  uint32_t fb_width, fb_height;
  fb_get_info(&fb_buffer, &fb_width, &fb_height);

  if (fb_buffer) {
    gui_init(fb_buffer, fb_width, fb_height, fb_width * 4);

    /* Create demo windows with working terminal */
    extern struct window *gui_create_file_manager(int x, int y);
    gui_create_window("Terminal", 50, 50, 400, 300);

    /* Create and set active terminal so keyboard input works */
    {
      extern struct terminal *term_create(int x, int y, int cols, int rows);
      extern void term_set_active(struct terminal * term);
      struct terminal *term = term_create(52, 80, 48, 15);
      if (term) {
        term_set_active(term);
      }
    }

    gui_create_file_manager(200, 100);

    /* Compose and display desktop */
    gui_compose();
    gui_draw_cursor();

    printk(KERN_INFO "  GUI desktop ready!\n");
  }

  /* Initialize PCI bus and detect devices (including Audio) */
  printk(KERN_INFO "  Initializing PCI bus...\n");
  extern void pci_init(void);
  pci_init();

  /* Initialize GPU driver (virtio-gpu for QEMU acceleration) */
  printk(KERN_INFO "  Initializing GPU driver...\n");
  extern int virtio_gpu_init(pci_device_t * pci);
  extern pci_device_t *pci_find_device(uint16_t vendor, uint16_t device);
  pci_device_t *gpu = pci_find_device(0x1AF4, 0x1050); /* virtio-gpu */
  if (gpu) {
    if (virtio_gpu_init(gpu) == 0) {
      printk(KERN_INFO "  GPU: virtio-gpu initialized with 3D acceleration\n");
    } else {
      printk(KERN_INFO "  GPU: virtio-gpu init failed\n");
    }
  } else {
    printk(KERN_INFO "  GPU: No virtio-gpu found (software rendering)\n");
  }

  printk(KERN_INFO "  Loading keyboard driver...\n");
  printk(KERN_INFO "  Loading NVMe driver...\n");
  printk(KERN_INFO "  Loading USB driver...\n");
  printk(KERN_INFO "  Loading network driver...\n");
  extern void tcpip_init(void);
  extern int virtio_net_init(void);
  tcpip_init();
  virtio_net_init();

  /* ================================================================= */
  /* Phase 6: Enable Interrupts */
  /* ================================================================= */

  printk(KERN_INFO "[INIT] Enabling interrupts...\n");
  /* Enable interrupts */
  arch_irq_enable();

  printk(KERN_INFO "[INIT] Kernel initialization complete!\n\n");
}

/*
 * start_init_process - Start the first userspace process (PID 1)
 */

/* Global terminal pointer for keyboard callback */
static void *g_active_terminal = 0;

/* Keyboard callback wrapper */
/* Keyboard callback wrapper */
static void keyboard_handler(int key) {
  /* gui_handle_key_event is now called via gui_key_callback, not here */

  /* Send to KAPI input buffer for non-windowed apps (e.g. Doom) */
  extern void kapi_sys_key_event(int key);
  kapi_sys_key_event(key);
}

static void start_init_process(void) {
  /* Create and start init process */
  printk(KERN_INFO "Executing /sbin/init...\n");

  printk(KERN_INFO "Init process started (placeholder)\n");
  printk(KERN_INFO "System ready.\n\n");

  /* Set up input handling */
  extern int input_init(void);
  extern void input_poll(void);
  extern void input_set_key_callback(void (*callback)(int key));
  extern void gui_compose(void);
  extern void gui_draw_cursor(void);

  input_init();

  /* Connect keyboard input to terminal */
  input_set_key_callback(keyboard_handler);

  printk(KERN_INFO "GUI: Event loop started - type in terminal!\\n");

  /* Initial render */
  gui_compose();
  gui_draw_cursor();

  /* Main GUI event loop with proper flicker-free refresh */
  uint32_t frame = 0;
  int last_mx = 0, last_my = 0;
  int last_buttons = 0;
  int needs_redraw = 1; /* Initial draw */
  int cursor_only = 0;  /* Only cursor needs updating */

  /* Timer for periodic auto-refresh (33ms = 30 FPS for responsive UI) */
  uint64_t last_refresh = arch_timer_get_ms();
  const uint64_t REFRESH_MS = 33; /* 30 FPS - responsive mouse */

  while (1) {
    /* Poll virtio input devices (keyboard/mouse) - MUST call this! */
    input_poll();

    /* Poll for keyboard input from UART as well */
    extern int uart_getc_nonblock(void);
    extern void gui_handle_key_event(int key);
    int c = uart_getc_nonblock();
    if (c >= 0) {
      /* Route to focused window */
      gui_handle_key_event(c);
      needs_redraw = 1;
    }

    /* Poll input system (Keyboard & Mouse) */
    extern void input_poll(void);
    input_poll();

    /* Get mouse state (updated by input_poll) */
    extern void mouse_get_position(int *x, int *y);
    extern int mouse_get_buttons(void);
    extern void gui_handle_mouse_event(int x, int y, int buttons);

    int mx, my;
    mouse_get_position(&mx, &my);
    int mbuttons = mouse_get_buttons();

    /* Check if mouse changed */
    if (mx != last_mx || my != last_my || mbuttons != last_buttons) {
      /* Always call mouse event handler for hover support */
      gui_handle_mouse_event(mx, my, mbuttons);

      /* Always redraw on mouse move - cursor is now composited */
      needs_redraw = 1;

      last_mx = mx;
      last_my = my;
      last_buttons = mbuttons;
    }

    /* Periodic refresh for animations (5 FPS) */
    uint64_t now = arch_timer_get_ms();
    if (now - last_refresh >= REFRESH_MS) {
      last_refresh = now;
      needs_redraw = 1;
    }

    /* Redraw when needed - compose includes cursor drawing */
    if (needs_redraw) {
      gui_compose(); /* Cursor is drawn inside compose, before blit */
      needs_redraw = 0;
      cursor_only = 0;
    }

    frame++;
    (void)frame;

    /* Short yield - allows input polling without slowing mouse */
    for (volatile int i = 0; i < 500; i++) {
    }
  }
}

/*
 * panic - Halt the system with an error message
 * @msg: Error message to display
 */
void panic(const char *msg) {
  /* Disable interrupts */
  arch_irq_disable();

  printk(KERN_EMERG "\n");
  printk(KERN_EMERG "============================================\n");
  printk(KERN_EMERG "KERNEL PANIC!\n");
  printk(KERN_EMERG "============================================\n");
  printk(KERN_EMERG "%s\n", msg);
  printk(KERN_EMERG "============================================\n");
  printk(KERN_EMERG "System halted.\n");

  /* Infinite loop */
  arch_halt();
}
