/*
 * vib-OS - USB HID Driver
 * Implements Human Interface Device Class
 */

#include "drivers/usb/usb.h"
#include "mm/kmalloc.h"
#include "printk.h"

/* HID Requests */
#define HID_GET_REPORT 0x01
#define HID_GET_IDLE 0x02
#define HID_GET_PROTOCOL 0x03
#define HID_SET_REPORT 0x09
#define HID_SET_IDLE 0x0A
#define HID_SET_PROTOCOL 0x0B

/* Functions */

int usb_hid_init(struct usb_device *dev) {
  printk(KERN_INFO "USB-HID: Initializing HID Device\n");

  // 1. Get HID Descriptor
  // 2. Set Idle (0)
  // 3. Set Protocol (Boot Protocol = 0) if supported
  // 4. Start Interrupt IN polling

  return 0;
}

void usb_hid_irq(struct usb_device *dev) {
  // Handle interrupt IN data (keypress, mouse move)
  printk(KERN_DEBUG "USB-HID: Interrupt received\n");
}
