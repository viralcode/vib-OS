/*
 * vib-OS - USB Core Headers
 */

#ifndef _DRIVERS_USB_USB_H
#define _DRIVERS_USB_USB_H

#include "types.h"

/* USB Device Structure */
struct usb_device {
  uint8_t bus_id;
  uint8_t dev_addr;
  uint8_t speed;
  uint16_t vendor_id;
  uint16_t product_id;
  void *controller; // e.g. xhci_controller*
  void *data;       // Driver specific data
};

/* Standard Descriptors */
struct usb_device_descriptor {
  uint8_t bLength;
  uint8_t bDescriptorType;
  uint16_t bcdUSB;
  uint8_t bDeviceClass;
  uint8_t bDeviceSubClass;
  uint8_t bDeviceProtocol;
  uint8_t bMaxPacketSize0;
  uint16_t idVendor;
  uint16_t idProduct;
  uint16_t bcdDevice;
  uint8_t iManufacturer;
  uint8_t iProduct;
  uint8_t iSerialNumber;
  uint8_t bNumConfigurations;
} __attribute__((packed));

/* USB Init Functions */
int usb_msd_init(struct usb_device *dev);
int usb_hid_init(struct usb_device *dev);

#endif /* _DRIVERS_USB_USB_H */
