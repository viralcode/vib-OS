/*
 * Minimal USB core interface (xHCI/EHCI + HID keyboard)
 */

#ifndef _USB_H
#define _USB_H

#include "types.h"

typedef void (*usb_keyboard_callback_t)(int key);

void usb_init(void);
void usb_poll(void);

void usb_set_keyboard_callback(usb_keyboard_callback_t cb);
void usb_hid_kbd_handle_report(const uint8_t *rep, int len);
void usb_submit_hid_report(const uint8_t *rep, int len);

/* Expose controller init for staged debugging */
void usb_xhci_init(void);
void usb_ehci_init(void);

#endif /* _USB_H */
