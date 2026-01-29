/*
 * USB core glue (xHCI/EHCI + HID keyboard)
 */

#include "../include/usb.h"
#include "../include/gui.h"

/* Forward declarations */
void usb_xhci_init(void);
void usb_xhci_poll(void);
int usb_xhci_ready(void);

void usb_ehci_init(void);
void usb_ehci_poll(void);
int usb_ehci_ready(void);

static usb_keyboard_callback_t keyboard_cb = 0;
static const int usb_report_size = 8;

#define USB_REPORT_QUEUE_SIZE 16
typedef struct {
  uint8_t data[8];
  int len;
} usb_report_t;

static usb_report_t report_queue[USB_REPORT_QUEUE_SIZE];
static int report_head = 0;
static int report_tail = 0;

void usb_set_keyboard_callback(usb_keyboard_callback_t cb) {
  keyboard_cb = cb;
}

void usb_hid_handle_key(int key) {
  static int first_key = 1;
  if (first_key) {
    /* [6] ORANGE BLOCK - key decoded */
    for (int y = 0; y < 100; y++) {
      for (int x = 0; x < 100; x++) {
        debug_rect(x, 500 + y, 1, 1, 0xFFFF9900);
      }
    }
    serial_puts("[USB] First key: ");
    serial_puthex(key);
    serial_puts("\n");
    first_key = 0;
  }
  
  if (keyboard_cb) {
    keyboard_cb(key);
  } else {
    /* No callback registered - PINK BLOCK (error) */
    for (int y = 0; y < 100; y++) {
      for (int x = 0; x < 100; x++) {
        debug_rect(x, 500 + y, 1, 1, 0xFFFF00AA);
      }
    }
  }
}

void usb_submit_hid_report(const uint8_t *rep, int len) {
  if (!rep || len <= 0) {
    return;
  }
  
  static int first_report = 1;
  if (first_report) {
    /* AQUA block - report submitted to queue */
    for (int y = 0; y < 50; y++) {
      for (int x = 0; x < 50; x++) {
        debug_rect(110 + x, 50 + y, 1, 1, 0xFF00AAAA);
      }
    }
    
    serial_puts("[USB] First HID report received! len=");
    serial_puthex(len);
    serial_puts(" data=");
    for (int i = 0; i < len && i < 8; i++) {
      serial_puthex(rep[i]);
      serial_puts(" ");
    }
    serial_puts("\n");
    first_report = 0;
  }
  
  int next = (report_head + 1) % USB_REPORT_QUEUE_SIZE;
  if (next == report_tail) {
    return; /* drop when queue is full */
  }
  int copy_len = len;
  if (copy_len > usb_report_size) {
    copy_len = usb_report_size;
  }
  for (int i = 0; i < copy_len; i++) {
    report_queue[report_head].data[i] = rep[i];
  }
  report_queue[report_head].len = copy_len;
  report_head = next;
}

void usb_init(void) {
  usb_xhci_init();
  usb_ehci_init();
}

void usb_poll(void) {
  static int poll_count = 0;
  static int shown_poll = 0;
  
  poll_count++;
  if (poll_count == 1000 && !shown_poll) {
    /* Show that polling is happening - PINK block */
    for (int y = 0; y < 50; y++) {
      for (int x = 0; x < 50; x++) {
        debug_rect(110 + x, y, 1, 1, 0xFFFF00AA);
      }
    }
    shown_poll = 1;
  }
  
  if (usb_xhci_ready()) {
    usb_xhci_poll();
  }
  if (usb_ehci_ready()) {
    usb_ehci_poll();
  }

  while (report_tail != report_head) {
    usb_report_t *rep = &report_queue[report_tail];
    usb_hid_kbd_handle_report(rep->data, rep->len);
    report_tail = (report_tail + 1) % USB_REPORT_QUEUE_SIZE;
  }
}
