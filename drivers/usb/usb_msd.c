/*
 * vib-OS - USB Mass Storage Driver
 * Implements Bulk-Only Transport (BOT)
 */

#include "drivers/usb/usb.h"
#include "mm/kmalloc.h"
#include "printk.h"

/* SCSI Commands */
#define SCSI_TEST_UNIT_READY 0x00
#define SCSI_REQUEST_SENSE 0x03
#define SCSI_INQUIRY 0x12
#define SCSI_READ_CAPACITY_10 0x25
#define SCSI_READ_10 0x28
#define SCSI_WRITE_10 0x2A

/* CBW/CSW Signatures */
#define USB_BOT_CBW_SIGNATURE 0x43425355
#define USB_BOT_CSW_SIGNATURE 0x53425355

/* Command Block Wrapper */
struct bot_cbw {
  uint32_t signature;
  uint32_t tag;
  uint32_t data_transfer_length;
  uint8_t flags;
  uint8_t lun;
  uint8_t cb_length;
  uint8_t cb[16];
} __attribute__((packed));

/* Command Status Wrapper */
struct bot_csw {
  uint32_t signature;
  uint32_t tag;
  uint32_t data_residue;
  uint8_t status;
} __attribute__((packed));

/* State */
static uint32_t usb_msd_tag = 1;

/* Functions */

// Defines stubs for now to link
// Real implementation would interact with xhci to send bulk transfers

int usb_msd_init(struct usb_device *dev) {
  printk(KERN_INFO "USB-MSD: Initializing Mass Storage Device\n");
  return 0;
}

int usb_msd_read_sector(struct usb_device *dev, uint32_t lba, void *buf) {
  printk(KERN_DEBUG "USB-MSD: Read LBA %d\n", lba);
  /*
   * 1. Send CBW with SCSI READ(10)
   * 2. Bulk IN data
   * 3. Read CSW
   */
  return 0;
}

int usb_msd_write_sector(struct usb_device *dev, uint32_t lba, void *buf) {
  printk(KERN_DEBUG "USB-MSD: Write LBA %d\n", lba);
  return 0;
}
