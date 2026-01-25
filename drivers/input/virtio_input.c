/*
 * Vib-OS - Virtio MMIO Mouse/Tablet Driver
 *
 * Based on VibeOS implementation for QEMU virt machine.
 * Uses virtio-tablet for absolute positioning (EV_ABS events).
 */

#include "printk.h"
#include "types.h"

/* ===================================================================== */
/* Virtio MMIO registers (QEMU virt machine) */
/* ===================================================================== */

#define VIRTIO_MMIO_BASE 0x0a000000
#define VIRTIO_MMIO_STRIDE 0x200

#define VIRTIO_MMIO_MAGIC 0x000
#define VIRTIO_MMIO_VERSION 0x004
#define VIRTIO_MMIO_DEVICE_ID 0x008
#define VIRTIO_MMIO_VENDOR_ID 0x00c
#define VIRTIO_MMIO_DEVICE_FEATURES 0x010
#define VIRTIO_MMIO_DRIVER_FEATURES 0x020
#define VIRTIO_MMIO_QUEUE_SEL 0x030
#define VIRTIO_MMIO_QUEUE_NUM_MAX 0x034
#define VIRTIO_MMIO_QUEUE_NUM 0x038
#define VIRTIO_MMIO_QUEUE_READY 0x044
#define VIRTIO_MMIO_QUEUE_NOTIFY 0x050
#define VIRTIO_MMIO_INTERRUPT_STATUS 0x060
#define VIRTIO_MMIO_INTERRUPT_ACK 0x064
#define VIRTIO_MMIO_STATUS 0x070
#define VIRTIO_MMIO_QUEUE_DESC_LOW 0x080
#define VIRTIO_MMIO_QUEUE_DESC_HIGH 0x084
#define VIRTIO_MMIO_QUEUE_AVAIL_LOW 0x090
#define VIRTIO_MMIO_QUEUE_AVAIL_HIGH 0x094
#define VIRTIO_MMIO_QUEUE_USED_LOW 0x0a0
#define VIRTIO_MMIO_QUEUE_USED_HIGH 0x0a4

#define VIRTIO_STATUS_ACK 1
#define VIRTIO_STATUS_DRIVER 2
#define VIRTIO_STATUS_DRIVER_OK 4
#define VIRTIO_STATUS_FEATURES_OK 8

#define VIRTIO_DEV_INPUT 18

/* Linux input event types */
#define EV_SYN 0x00
#define EV_KEY 0x01
#define EV_REL 0x02
#define EV_ABS 0x03

/* Absolute axis codes */
#define ABS_X 0x00
#define ABS_Y 0x01

/* Button codes */
#define BTN_LEFT 0x110
#define BTN_RIGHT 0x111
#define BTN_MIDDLE 0x112

/* Virtio input config */
#define VIRTIO_INPUT_CFG_SELECT 0x100
#define VIRTIO_INPUT_CFG_SUBSEL 0x101
#define VIRTIO_INPUT_CFG_SIZE 0x102
#define VIRTIO_INPUT_CFG_DATA 0x108
#define VIRTIO_INPUT_CFG_ID_NAME 0x01

/* Virtqueue structures */
typedef struct __attribute__((packed)) {
  uint64_t addr;
  uint32_t len;
  uint16_t flags;
  uint16_t next;
} virtq_desc_t;

typedef struct __attribute__((packed)) {
  uint16_t flags;
  uint16_t idx;
  uint16_t ring[16];
} virtq_avail_t;

typedef struct __attribute__((packed)) {
  uint32_t id;
  uint32_t len;
} virtq_used_elem_t;

typedef struct __attribute__((packed)) {
  uint16_t flags;
  uint16_t idx;
  virtq_used_elem_t ring[16];
} virtq_used_t;

/* Input event structure */
typedef struct __attribute__((packed)) {
  uint16_t type;
  uint16_t code;
  uint32_t value;
} virtio_input_event_t;

#define QUEUE_SIZE 16
#define DESC_F_WRITE 2

/* ===================================================================== */
/* State */
/* ===================================================================== */

static volatile uint32_t *mouse_base = 0;
static virtq_desc_t *desc = 0;
static virtq_avail_t *avail = 0;
static virtq_used_t *used = 0;
static virtio_input_event_t *events = 0;
static uint16_t last_used_idx = 0;

/* Queue memory - must be 4K aligned */
static uint8_t queue_mem[4096] __attribute__((aligned(4096)));
static virtio_input_event_t event_bufs[QUEUE_SIZE] __attribute__((aligned(16)));

/* Mouse state */
static int mouse_x = 16384; /* Raw 0-32767 */
static int mouse_y = 16384;
static uint8_t mouse_buttons = 0;

/* Keyboard state */
static volatile uint32_t *kbd_base = 0;
static virtq_desc_t *kbd_desc = 0;
static virtq_avail_t *kbd_avail = 0;
static virtq_used_t *kbd_used = 0;
static virtio_input_event_t *kbd_events = 0;
static uint16_t kbd_last_used_idx = 0;
static uint8_t kbd_queue_mem[4096] __attribute__((aligned(4096)));
static virtio_input_event_t kbd_event_bufs[QUEUE_SIZE]
    __attribute__((aligned(16)));

/* Keyboard callback */
static void (*gui_key_callback)(int key) = 0;

/* Modifier key states */
static int shift_held = 0;
static int ctrl_held = 0;

/* Linux keycode to ASCII mapping (not PS/2 scancodes!) */
/* virtio-keyboard sends Linux KEY_* codes, not PS/2 scancodes */
static const char keycode_to_ascii[128] = {
    0,    27,  '1',  '2',
    '3',  '4', '5',  '6', /* 0-7 */
    '7',  '8', '9',  '0',
    '-',  '=', '\b', '\t', /* 8-15: KEY_7..KEY_TAB */
    'q',  'w', 'e',  'r',
    't',  'y', 'u',  'i', /* 16-23: KEY_Q..KEY_I */
    'o',  'p', '[',  ']',
    '\n', 0,   'a',  's', /* 24-31: KEY_O..KEY_S */
    'd',  'f', 'g',  'h',
    'j',  'k', 'l',  ';', /* 32-39: KEY_D..KEY_SEMICOLON */
    '\'', '`', 0,    '\\',
    'z',  'x', 'c',  'v', /* 40-47: KEY_APOSTROPHE..KEY_V */
    'b',  'n', 'm',  ',',
    '.',  '/', 0,    '*', /* 48-55: KEY_B..KEY_KPASTERISK */
    0,    ' ', 0,    0,
    0,    0,   0,    0, /* 56-63: KEY_LEFTALT..KEY_F5 */
    0,    0,   0,    0,
    0,    0,   0,    '7', /* 64-71: KEY_F6..KEY_KP7 */
    '8',  '9', '-',  '4',
    '5',  '6', '+',  '1', /* 72-79: KEY_KP8..KEY_KP1 */
    '2',  '3', '0',  '.',
    0,    0,   0,    0, /* 80-87: KEY_KP2..KEY_F12 */
    0,    0,   0,    0,
    0,    0,   0,    0, /* 88-95 */
    0,    0,   0,    0,
    0,    0,   0,    0, /* 96-103 */
    0,    0,   0,    0,
    0,    0,   0,    0, /* 104-111 */
    0,    0,   0,    0,
    0,    0,   0,    0, /* 112-119 */
    0,    0,   0,    0,
    0,    0,   0,    0 /* 120-127 */
};

/* Shifted keycode to ASCII mapping for symbols */
static const char keycode_to_ascii_shifted[128] = {
    0,    27,  '!',  '@',
    '#',  '$', '%',  '^', /* 0-7: shift+1 = !, shift+2 = @, etc */
    '&',  '*', '(',  ')',
    '_',  '+', '\b', '\t', /* 8-15: shift+- = _, shift+= = + */
    'Q',  'W', 'E',  'R',
    'T',  'Y', 'U',  'I', /* 16-23: uppercase letters */
    'O',  'P', '{',  '}',
    '\n', 0,   'A',  'S', /* 24-31: shift+[ = {, shift+] = } */
    'D',  'F', 'G',  'H',
    'J',  'K', 'L',  ':', /* 32-39: shift+; = : */
    '"',  '~', 0,    '|',
    'Z',  'X', 'C',  'V', /* 40-47: shift+' = ", shift+` = ~, shift+\ = | */
    'B',  'N', 'M',  '<',
    '>',  '?', 0,    '*', /* 48-55: shift+, = <, shift+. = >, shift+/ = ? */
    0,    ' ', 0,    0,
    0,    0,   0,    0, /* 56-63 */
    0,    0,   0,    0,
    0,    0,   0,    '7', /* 64-71 */
    '8',  '9', '-',  '4',
    '5',  '6', '+',  '1', /* 72-79 */
    '2',  '3', '0',  '.',
    0,    0,   0,    0, /* 80-87 */
    0,    0,   0,    0,
    0,    0,   0,    0, /* 88-95 */
    0,    0,   0,    0,
    0,    0,   0,    0, /* 96-103 */
    0,    0,   0,    0,
    0,    0,   0,    0, /* 104-111 */
    0,    0,   0,    0,
    0,    0,   0,    0, /* 112-119 */
    0,    0,   0,    0,
    0,    0,   0,    0 /* 120-127 */
};

/* Key callback (forward declaration) */
static void (*key_callback)(int key) = 0;

/* Screen dimensions */
#define SCREEN_WIDTH 1024
#define SCREEN_HEIGHT 768

/* ===================================================================== */
/* MMIO Helpers */
/* ===================================================================== */

static void mmio_barrier(void) {
#ifdef ARCH_ARM64
  asm volatile("dsb sy" ::: "memory");
#elif defined(ARCH_X86_64) || defined(ARCH_X86)
  asm volatile("mfence" ::: "memory");
#endif
}

static uint32_t mmio_read32(volatile uint32_t *addr) {
  uint32_t val = *addr;
  mmio_barrier();
  return val;
}

static void mmio_write32(volatile uint32_t *addr, uint32_t val) {
  mmio_barrier();
  *addr = val;
  mmio_barrier();
}

/* ===================================================================== */
/* Find Virtio Tablet Device */
/* ===================================================================== */

static volatile uint32_t *find_virtio_tablet(void) {
  for (int i = 0; i < 32; i++) {
    volatile uint32_t *base =
        (volatile uint32_t *)(uintptr_t)(VIRTIO_MMIO_BASE +
                                         i * VIRTIO_MMIO_STRIDE);
    volatile uint8_t *base8 =
        (volatile uint8_t *)(uintptr_t)(VIRTIO_MMIO_BASE +
                                        i * VIRTIO_MMIO_STRIDE);

    uint32_t magic = mmio_read32(base + VIRTIO_MMIO_MAGIC / 4);
    uint32_t device_id = mmio_read32(base + VIRTIO_MMIO_DEVICE_ID / 4);

    if (magic != 0x74726976 || device_id != VIRTIO_DEV_INPUT) {
      continue;
    }

    /* Check device name for "Tablet" */
    base8[VIRTIO_INPUT_CFG_SELECT] = VIRTIO_INPUT_CFG_ID_NAME;
    base8[VIRTIO_INPUT_CFG_SUBSEL] = 0;
    mmio_barrier();

    uint8_t size = base8[VIRTIO_INPUT_CFG_SIZE];
    char name[32] = {0};
    for (int j = 0; j < 31 && j < size; j++) {
      name[j] = base8[VIRTIO_INPUT_CFG_DATA + j];
    }

    printk(KERN_INFO "MOUSE: Found input device: %s\n", name);

    /* Look for "Tablet" */
    if (name[0] == 'Q' && name[5] == 'V' && name[12] == 'T') {
      return base;
    }
  }

  return 0;
}

/* ===================================================================== */
/* Mouse Polling */
/* ===================================================================== */

void mouse_poll(void) {
  if (!mouse_base || !used) {
    return;
  }

  mmio_barrier();
  uint16_t current_used = used->idx;

  while (last_used_idx != current_used) {
    uint16_t idx = last_used_idx % QUEUE_SIZE;
    uint32_t desc_idx = used->ring[idx].id;

    virtio_input_event_t *ev = &events[desc_idx];

    /* Process event */
    if (ev->type == EV_ABS) {
      if (ev->code == ABS_X) {
        mouse_x = ev->value;
      } else if (ev->code == ABS_Y) {
        mouse_y = ev->value;
      }
    } else if (ev->type == EV_KEY) {
      int pressed = (ev->value != 0);
      if (ev->code == BTN_LEFT) {
        if (pressed)
          mouse_buttons |= 1;
        else
          mouse_buttons &= ~1;
      } else if (ev->code == BTN_RIGHT) {
        if (pressed)
          mouse_buttons |= 2;
        else
          mouse_buttons &= ~2;
      }
    }

    /* Re-add descriptor to available ring */
    uint16_t avail_idx = avail->idx % QUEUE_SIZE;
    avail->ring[avail_idx] = desc_idx;
    avail->idx++;

    last_used_idx++;
  }

  /* Notify device */
  mmio_write32(mouse_base + VIRTIO_MMIO_QUEUE_NOTIFY / 4, 0);
  mmio_write32(mouse_base + VIRTIO_MMIO_INTERRUPT_ACK / 4,
               mmio_read32(mouse_base + VIRTIO_MMIO_INTERRUPT_STATUS / 4));
}

/* ===================================================================== */
/* Mouse API */
/* ===================================================================== */

void mouse_get_position(int *x, int *y) {
  mouse_poll();

  /* Scale from 0-32767 to screen dimensions */
  if (x)
    *x = (mouse_x * SCREEN_WIDTH) / 32768;
  if (y)
    *y = (mouse_y * SCREEN_HEIGHT) / 32768;
}

int mouse_get_buttons(void) {
  mouse_poll();
  return mouse_buttons;
}

/* ===================================================================== */
/* Initialization */
/* ===================================================================== */

int mouse_init(void) {
  printk(KERN_INFO "MOUSE: Initializing virtio-tablet...\n");

  mouse_base = find_virtio_tablet();
  if (!mouse_base) {
    printk(KERN_WARNING "MOUSE: No virtio tablet found\n");
    return -1;
  }

  /* Reset device */
  mmio_write32(mouse_base + VIRTIO_MMIO_STATUS / 4, 0);
  while (mmio_read32(mouse_base + VIRTIO_MMIO_STATUS / 4) != 0) {
    asm volatile("nop");
  }

  /* Acknowledge */
  mmio_write32(mouse_base + VIRTIO_MMIO_STATUS / 4, VIRTIO_STATUS_ACK);
  mmio_write32(mouse_base + VIRTIO_MMIO_STATUS / 4,
               VIRTIO_STATUS_ACK | VIRTIO_STATUS_DRIVER);

  /* Accept no special features */
  mmio_write32(mouse_base + VIRTIO_MMIO_DRIVER_FEATURES / 4, 0);
  mmio_write32(mouse_base + VIRTIO_MMIO_STATUS / 4,
               VIRTIO_STATUS_ACK | VIRTIO_STATUS_DRIVER |
                   VIRTIO_STATUS_FEATURES_OK);

  /* Setup queue 0 */
  mmio_write32(mouse_base + VIRTIO_MMIO_QUEUE_SEL / 4, 0);

  uint32_t max_queue = mmio_read32(mouse_base + VIRTIO_MMIO_QUEUE_NUM_MAX / 4);
  if (max_queue < QUEUE_SIZE) {
    printk(KERN_WARNING "MOUSE: Queue too small\n");
    return -1;
  }

  mmio_write32(mouse_base + VIRTIO_MMIO_QUEUE_NUM / 4, QUEUE_SIZE);

  /* Setup queue memory */
  desc = (virtq_desc_t *)queue_mem;
  avail = (virtq_avail_t *)(queue_mem + QUEUE_SIZE * sizeof(virtq_desc_t));
  used = (virtq_used_t *)(queue_mem + 2048);
  events = event_bufs;

  /* Set queue addresses */
  uint64_t desc_addr = (uint64_t)(uintptr_t)desc;
  uint64_t avail_addr = (uint64_t)(uintptr_t)avail;
  uint64_t used_addr = (uint64_t)(uintptr_t)used;

  mmio_write32(mouse_base + VIRTIO_MMIO_QUEUE_DESC_LOW / 4,
               (uint32_t)desc_addr);
  mmio_write32(mouse_base + VIRTIO_MMIO_QUEUE_DESC_HIGH / 4,
               (uint32_t)(desc_addr >> 32));
  mmio_write32(mouse_base + VIRTIO_MMIO_QUEUE_AVAIL_LOW / 4,
               (uint32_t)avail_addr);
  mmio_write32(mouse_base + VIRTIO_MMIO_QUEUE_AVAIL_HIGH / 4,
               (uint32_t)(avail_addr >> 32));
  mmio_write32(mouse_base + VIRTIO_MMIO_QUEUE_USED_LOW / 4,
               (uint32_t)used_addr);
  mmio_write32(mouse_base + VIRTIO_MMIO_QUEUE_USED_HIGH / 4,
               (uint32_t)(used_addr >> 32));

  /* Initialize descriptors */
  for (int i = 0; i < QUEUE_SIZE; i++) {
    desc[i].addr = (uint64_t)(uintptr_t)&events[i];
    desc[i].len = sizeof(virtio_input_event_t);
    desc[i].flags = DESC_F_WRITE;
    desc[i].next = 0;
  }

  /* Fill available ring */
  avail->flags = 0;
  for (int i = 0; i < QUEUE_SIZE; i++) {
    avail->ring[i] = i;
  }
  avail->idx = QUEUE_SIZE;

  /* Queue ready */
  mmio_write32(mouse_base + VIRTIO_MMIO_QUEUE_READY / 4, 1);

  /* Driver OK */
  mmio_write32(mouse_base + VIRTIO_MMIO_STATUS / 4,
               VIRTIO_STATUS_ACK | VIRTIO_STATUS_DRIVER |
                   VIRTIO_STATUS_FEATURES_OK | VIRTIO_STATUS_DRIVER_OK);

  /* Notify device */
  mmio_write32(mouse_base + VIRTIO_MMIO_QUEUE_NOTIFY / 4, 0);

  /* Check status */
  uint32_t status = mmio_read32(mouse_base + VIRTIO_MMIO_STATUS / 4);
  if (status & 0x40) {
    printk(KERN_WARNING "MOUSE: Device reported failure!\n");
    return -1;
  }

  printk(KERN_INFO "MOUSE: Virtio tablet initialized!\n");
  return 0;
}

/* ===================================================================== */
/* Keyboard Functions */
/* ===================================================================== */

static volatile uint32_t *find_virtio_keyboard(void) {
  for (int i = 0; i < 32; i++) {
    volatile uint32_t *base =
        (volatile uint32_t *)(uintptr_t)(VIRTIO_MMIO_BASE +
                                         i * VIRTIO_MMIO_STRIDE);
    volatile uint8_t *base8 =
        (volatile uint8_t *)(uintptr_t)(VIRTIO_MMIO_BASE +
                                        i * VIRTIO_MMIO_STRIDE);

    uint32_t magic = mmio_read32(base + VIRTIO_MMIO_MAGIC / 4);
    uint32_t device_id = mmio_read32(base + VIRTIO_MMIO_DEVICE_ID / 4);

    if (magic != 0x74726976 || device_id != VIRTIO_DEV_INPUT) {
      continue;
    }

    /* Check device name for "Keyboard" */
    base8[VIRTIO_INPUT_CFG_SELECT] = VIRTIO_INPUT_CFG_ID_NAME;
    base8[VIRTIO_INPUT_CFG_SUBSEL] = 0;
    mmio_barrier();

    uint8_t size = base8[VIRTIO_INPUT_CFG_SIZE];
    char name[32] = {0};
    for (int j = 0; j < 31 && j < size; j++) {
      name[j] = base8[VIRTIO_INPUT_CFG_DATA + j];
    }

    printk(KERN_INFO "KEYBOARD: Checking device: %s\n", name);

    /* Look for "Keyboard" or "keyboard" anywhere in name */
    int found_kbd = 0;
    for (int j = 0; name[j] && name[j + 7]; j++) {
      if ((name[j] == 'K' || name[j] == 'k') &&
          (name[j + 1] == 'e' || name[j + 1] == 'E') &&
          (name[j + 2] == 'y' || name[j + 2] == 'Y')) {
        found_kbd = 1;
        break;
      }
    }

    if (found_kbd) {
      printk(KERN_INFO "KEYBOARD: Found: %s\n", name);
      return base;
    }
  }

  return 0;
}

static void keyboard_poll(void) {
  if (!kbd_base || !kbd_used) {
    return;
  }

  mmio_barrier();
  uint16_t current_used = kbd_used->idx;

  while (kbd_last_used_idx != current_used) {
    uint16_t idx = kbd_last_used_idx % QUEUE_SIZE;
    uint32_t desc_idx = kbd_used->ring[idx].id;

    virtio_input_event_t *ev = &kbd_events[desc_idx];

    /* Process keyboard event */
    if (ev->type == EV_KEY) {
      /* Track shift key state */
      if (ev->code == 42 || ev->code == 54) { /* Left or Right Shift */
        shift_held = (ev->value != 0);        /* 1 = pressed, 0 = released */
      }

      /* Track Ctrl key state */
      if (ev->code == 29 || ev->code == 97) { /* Left or Right Ctrl */
        ctrl_held = (ev->value != 0);         /* 1 = pressed, 0 = released */
      }

      if (ev->value == 1) { /* Key press only */
        int processed = 0;
        int vibe_key = 0;

        /* Manual mapping for Special Keys */
        if (ev->code == 103)
          vibe_key = 0x100; /* KEY_UP */
        else if (ev->code == 108)
          vibe_key = 0x101; /* KEY_DOWN */
        else if (ev->code == 105)
          vibe_key = 0x102; /* KEY_LEFT */
        else if (ev->code == 106)
          vibe_key = 0x103; /* KEY_RIGHT */
        else if (ev->code == 29 || ev->code == 97)
          processed = 1; /* Don't send ctrl as a key, just track state */
        else if (ev->code == 42 || ev->code == 54)
          processed = 1; /* Don't send shift as a key, just track state */
        else if (ev->code == 28)
          vibe_key = '\n'; /* Enter */
        else if (ev->code == 57)
          vibe_key = ' '; /* Space */
        else if (ev->code == 1)
          vibe_key = 27; /* Esc */

        if (vibe_key) {
          if (key_callback)
            key_callback(vibe_key);
          if (gui_key_callback)
            gui_key_callback(vibe_key);
          processed = 1;
        }

        if (!processed && ev->code < 128) {
          char ascii;

          /* Handle Ctrl+key combinations */
          if (ctrl_held) {
            /* Ctrl+letter generates control character (1-26) */
            char base = keycode_to_ascii[ev->code];
            if (base >= 'a' && base <= 'z') {
              ascii = base - 'a' + 1; /* Ctrl+a=1, Ctrl+c=3, Ctrl+v=22, etc */
            } else if (base >= 'A' && base <= 'Z') {
              ascii = base - 'A' + 1;
            } else {
              ascii = 0; /* Don't process other Ctrl combinations */
            }
          } else if (shift_held) {
            ascii = keycode_to_ascii_shifted[ev->code];
          } else {
            ascii = keycode_to_ascii[ev->code];
          }

          /* Send ASCII to both callbacks */
          if (key_callback && ascii) {
            key_callback(ascii);
          }
          /* Send ASCII to GUI callback too (not raw keycode!) */
          if (gui_key_callback && ascii) {
            gui_key_callback(ascii);
          }
        }
      }
    }

    /* Re-add descriptor to available ring */
    uint16_t avail_idx = kbd_avail->idx % QUEUE_SIZE;
    kbd_avail->ring[avail_idx] = desc_idx;
    kbd_avail->idx++;

    kbd_last_used_idx++;
  }

  /* Notify device */
  mmio_write32(kbd_base + VIRTIO_MMIO_QUEUE_NOTIFY / 4, 0);
  mmio_write32(kbd_base + VIRTIO_MMIO_INTERRUPT_ACK / 4,
               mmio_read32(kbd_base + VIRTIO_MMIO_INTERRUPT_STATUS / 4));
}

static int keyboard_init(void) {
  printk(KERN_INFO "KEYBOARD: Initializing virtio-keyboard...\n");

  kbd_base = find_virtio_keyboard();
  if (!kbd_base) {
    printk(KERN_WARNING "KEYBOARD: No virtio keyboard found\n");
    return -1;
  }

  /* Reset device */
  mmio_write32(kbd_base + VIRTIO_MMIO_STATUS / 4, 0);
  while (mmio_read32(kbd_base + VIRTIO_MMIO_STATUS / 4) != 0) {
    asm volatile("nop");
  }

  /* Acknowledge */
  mmio_write32(kbd_base + VIRTIO_MMIO_STATUS / 4, VIRTIO_STATUS_ACK);
  mmio_write32(kbd_base + VIRTIO_MMIO_STATUS / 4,
               VIRTIO_STATUS_ACK | VIRTIO_STATUS_DRIVER);

  /* Accept no special features */
  mmio_write32(kbd_base + VIRTIO_MMIO_DRIVER_FEATURES / 4, 0);
  mmio_write32(kbd_base + VIRTIO_MMIO_STATUS / 4,
               VIRTIO_STATUS_ACK | VIRTIO_STATUS_DRIVER |
                   VIRTIO_STATUS_FEATURES_OK);

  /* Setup queue 0 */
  mmio_write32(kbd_base + VIRTIO_MMIO_QUEUE_SEL / 4, 0);

  uint32_t max_queue = mmio_read32(kbd_base + VIRTIO_MMIO_QUEUE_NUM_MAX / 4);
  if (max_queue < QUEUE_SIZE) {
    printk(KERN_WARNING "KEYBOARD: Queue too small\n");
    return -1;
  }

  mmio_write32(kbd_base + VIRTIO_MMIO_QUEUE_NUM / 4, QUEUE_SIZE);

  /* Setup queue memory */
  kbd_desc = (virtq_desc_t *)kbd_queue_mem;
  kbd_avail =
      (virtq_avail_t *)(kbd_queue_mem + QUEUE_SIZE * sizeof(virtq_desc_t));
  kbd_used = (virtq_used_t *)(kbd_queue_mem + 2048);
  kbd_events = kbd_event_bufs;

  /* Set queue addresses */
  uint64_t desc_addr = (uint64_t)(uintptr_t)kbd_desc;
  uint64_t avail_addr = (uint64_t)(uintptr_t)kbd_avail;
  uint64_t used_addr = (uint64_t)(uintptr_t)kbd_used;

  mmio_write32(kbd_base + VIRTIO_MMIO_QUEUE_DESC_LOW / 4, (uint32_t)desc_addr);
  mmio_write32(kbd_base + VIRTIO_MMIO_QUEUE_DESC_HIGH / 4,
               (uint32_t)(desc_addr >> 32));
  mmio_write32(kbd_base + VIRTIO_MMIO_QUEUE_AVAIL_LOW / 4,
               (uint32_t)avail_addr);
  mmio_write32(kbd_base + VIRTIO_MMIO_QUEUE_AVAIL_HIGH / 4,
               (uint32_t)(avail_addr >> 32));
  mmio_write32(kbd_base + VIRTIO_MMIO_QUEUE_USED_LOW / 4, (uint32_t)used_addr);
  mmio_write32(kbd_base + VIRTIO_MMIO_QUEUE_USED_HIGH / 4,
               (uint32_t)(used_addr >> 32));

  /* Initialize descriptors */
  for (int i = 0; i < QUEUE_SIZE; i++) {
    kbd_desc[i].addr = (uint64_t)(uintptr_t)&kbd_events[i];
    kbd_desc[i].len = sizeof(virtio_input_event_t);
    kbd_desc[i].flags = DESC_F_WRITE;
    kbd_desc[i].next = 0;
  }

  /* Fill available ring */
  kbd_avail->flags = 0;
  for (int i = 0; i < QUEUE_SIZE; i++) {
    kbd_avail->ring[i] = i;
  }
  kbd_avail->idx = QUEUE_SIZE;

  /* Queue ready */
  mmio_write32(kbd_base + VIRTIO_MMIO_QUEUE_READY / 4, 1);

  /* Driver OK */
  mmio_write32(kbd_base + VIRTIO_MMIO_STATUS / 4,
               VIRTIO_STATUS_ACK | VIRTIO_STATUS_DRIVER |
                   VIRTIO_STATUS_FEATURES_OK | VIRTIO_STATUS_DRIVER_OK);

  /* Notify device */
  mmio_write32(kbd_base + VIRTIO_MMIO_QUEUE_NOTIFY / 4, 0);

  printk(KERN_INFO "KEYBOARD: Virtio keyboard initialized!\n");
  return 0;
}

/* ===================================================================== */
/* Compatibility API for main.c */
/* ===================================================================== */

int input_init(void) {
  printk(KERN_INFO "INPUT: Initializing input system\n");
  mouse_init();
  keyboard_init();
  printk(KERN_INFO "INPUT: Ready\n");
  return 0;
}

void input_set_key_callback(void (*callback)(int key)) {
  key_callback = callback;
}

void input_set_gui_key_callback(void (*callback)(int key)) {
  gui_key_callback = callback;
}

void input_poll(void) {
  /* Poll UART for keyboard input */
  extern int uart_getc_nonblock(void);
  int c = uart_getc_nonblock();
  if (c >= 0 && key_callback) {
    key_callback(c);
  }

  /* Poll virtio keyboard */
  keyboard_poll();

  /* Poll mouse */
  mouse_poll();
}
