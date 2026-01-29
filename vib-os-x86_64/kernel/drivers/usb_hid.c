/*
 * USB HID keyboard (boot protocol) handling
 */

#include "../include/types.h"
#include "../include/ps2.h"
#include "../include/gui.h"

void usb_hid_handle_key(int key);

static uint8_t prev_keys[6] = {0};
static int caps_lock = 0;
static int report_blink = 0;

static const char hid_to_ascii[128] = {
    0,  0,  0,  0, 'a', 'b', 'c', 'd', 'e', 'f', 'g', 'h', 'i', 'j', 'k', 'l',
    'm','n','o','p','q','r','s','t','u','v','w','x','y','z',
    '1','2','3','4','5','6','7','8','9','0',
    '\n', 0, 0, '\t', ' ', '-', '=', '[', ']', '\\', '#', ';', '\'', '`', ',', '.', '/'
};

static const char hid_to_ascii_shift[128] = {
    0,  0,  0,  0, 'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H', 'I', 'J', 'K', 'L',
    'M','N','O','P','Q','R','S','T','U','V','W','X','Y','Z',
    '!','@','#','$','%','^','&','*','(',')',
    '\n', 0, 0, '\t', ' ', '_', '+', '{', '}', '|', '~', ':', '"', '~', '<', '>', '?'
};

void usb_hid_kbd_handle_report(const uint8_t *rep, int len) {
  static int call_count = 0;
  call_count++;
  
  /* Show if this function is EVER called - TEAL block */
  static int shown_called = 0;
  if (call_count == 1 && !shown_called) {
    for (int y = 0; y < 50; y++) {
      for (int x = 0; x < 50; x++) {
        debug_rect(110 + x, 100 + y, 1, 1, 0xFF00AAFF);
      }
    }
    shown_called = 1;
  }
  
  if (!rep || len < 8) {
    /* Invalid report - show RED block */
    static int shown_invalid = 0;
    if (!shown_invalid) {
      for (int y = 0; y < 50; y++) {
        for (int x = 0; x < 50; x++) {
          debug_rect(110 + x, 150 + y, 1, 1, 0xFFFF0000);
        }
      }
      shown_invalid = 1;
    }
    return;
  }

  static int first_handle = 1;
  if (first_handle) {
    /* [5] MAGENTA BLOCK - valid report processed */
    for (int y = 0; y < 100; y++) {
      for (int x = 0; x < 100; x++) {
        debug_rect(x, 400 + y, 1, 1, 0xFFFF00FF);
      }
    }
    first_handle = 0;
  }

  uint8_t mods = rep[0];
  int shift = (mods & 0x22) != 0; /* LSHIFT or RSHIFT */

  /* Check newly pressed keys */
  static int shown_key_found = 0;
  for (int i = 2; i < 8; i++) {
    uint8_t key = rep[i];
    
    if (key != 0 && !shown_key_found) {
      /* BROWN block - non-zero key in report */
      for (int y = 0; y < 50; y++) {
        for (int x = 0; x < 50; x++) {
          debug_rect(110 + x, 200 + y, 1, 1, 0xFF884400);
        }
      }
      shown_key_found = 1;
    }
    
    if (key == 0) continue;

    int already = 0;
    for (int j = 0; j < 6; j++) {
      if (prev_keys[j] == key) {
        already = 1;
        break;
      }
    }
    if (already) continue;

    int out = 0;
    switch (key) {
    case 0x28: out = '\n'; break;            /* Enter */
    case 0x29: out = KEY_ESC; break;         /* Escape */
    case 0x2A: out = '\b'; break;            /* Backspace */
    case 0x2B: out = '\t'; break;            /* Tab */
    case 0x39:                               /* Caps Lock */
      caps_lock = !caps_lock;
      break;
    case 0x3A: out = KEY_F1; break;
    case 0x3B: out = KEY_F2; break;
    case 0x3C: out = KEY_F3; break;
    case 0x3D: out = KEY_F4; break;
    case 0x3E: out = KEY_F5; break;
    case 0x3F: out = KEY_F6; break;
    case 0x40: out = KEY_F7; break;
    case 0x41: out = KEY_F8; break;
    case 0x42: out = KEY_F9; break;
    case 0x43: out = KEY_F10; break;
    case 0x44: out = KEY_F11; break;
    case 0x45: out = KEY_F12; break;
    case 0x4F: out = KEY_RIGHT; break;
    case 0x50: out = KEY_LEFT; break;
    case 0x51: out = KEY_DOWN; break;
    case 0x52: out = KEY_UP; break;
    default:
      if (key >= 0x04 && key <= 0x1D) {
        char base = (char)('a' + (key - 0x04));
        int upper = shift ^ caps_lock;
        out = upper ? (base - 32) : base;
      } else if (key < 128) {
        out = shift ? hid_to_ascii_shift[key] : hid_to_ascii[key];
      }
      break;
    }

    if (out) {
      usb_hid_handle_key(out);
    }
  }

  for (int i = 0; i < 6; i++) {
    prev_keys[i] = rep[i + 2];
  }
}
