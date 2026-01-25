/*
 * Vib-OS - PCI Driver
 *
 * Basic PCI Express ECAM support for QEMU 'virt' machine
 */

#ifndef DRIVERS_PCI_H
#define DRIVERS_PCI_H

#include "types.h"

/* QEMU ARM64 'virt' machine PCI High ECAM Base */
/* Note: Depending on memory map, this might be 0x4010000000 or 0x3f000000 (Low
 * ECAM) */
/* For >3GB RAM, High ECAM is usually present. We'll check High first. */
#define PCI_ECAM_BASE 0x4010000000ULL
#define PCI_MMIO_BASE 0x10000000ULL /* Low PCI MMIO region */

/* Common PCI Register Offsets */
#define PCI_VENDOR_ID 0x00
#define PCI_DEVICE_ID 0x02
#define PCI_COMMAND 0x04
#define PCI_STATUS 0x06
#define PCI_REVISION 0x08
#define PCI_CLASS_REV 0x08
#define PCI_HEADER_TYPE 0x0E
#define PCI_BAR0 0x10
#define PCI_BAR1 0x14
#define PCI_BAR2 0x18
#define PCI_BAR3 0x1C
#define PCI_BAR4 0x20
#define PCI_BAR5 0x24
#define PCI_CAPABILITIES 0x34
#define PCI_INTERRUPT 0x3C

/* Status register bits */
#define PCI_STATUS_CAP_LIST 0x10 /* Capabilities list present */

/* Command bits */
#define PCI_CMD_IO 0x01
#define PCI_CMD_MEM 0x02
#define PCI_CMD_BUS_MASTER 0x04

/* Capability IDs */
#define PCI_CAP_ID_VNDR 0x09 /* Vendor-specific */

/* Virtio vendor/device IDs */
#define PCI_VENDOR_VIRTIO 0x1AF4
#define PCI_DEVICE_VIRTIO_GPU 0x1050

/* Device struct - extended to support GPU driver */
typedef struct pci_device {
  uint8_t bus;
  uint8_t slot;
  uint8_t func;
  uint16_t vendor_id;
  uint16_t device_id;
  uint8_t class_code;
  uint8_t subclass;
  uint64_t bar0;
  uint64_t bar1;
  uint64_t bar2;
  uint32_t irq;
  struct pci_device *next;
} pci_device_t;

/* Core PCI functions */
void pci_init(void);
uint32_t pci_read32(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset);
void pci_write32(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset,
                 uint32_t value);

/* Added helper functions for GPU/virtio drivers */
static inline uint8_t pci_read8(uint8_t bus, uint8_t slot, uint8_t func,
                                uint16_t offset) {
  uint64_t addr = PCI_ECAM_BASE | ((uint64_t)bus << 20) |
                  ((uint64_t)slot << 15) | ((uint64_t)func << 12) | offset;
  return *(volatile uint8_t *)addr;
}

static inline uint16_t pci_read16(uint8_t bus, uint8_t slot, uint8_t func,
                                  uint16_t offset) {
  uint64_t addr = PCI_ECAM_BASE | ((uint64_t)bus << 20) |
                  ((uint64_t)slot << 15) | ((uint64_t)func << 12) | offset;
  return *(volatile uint16_t *)addr;
}

static inline void pci_write8(uint8_t bus, uint8_t slot, uint8_t func,
                              uint16_t offset, uint8_t value) {
  uint64_t addr = PCI_ECAM_BASE | ((uint64_t)bus << 20) |
                  ((uint64_t)slot << 15) | ((uint64_t)func << 12) | offset;
  *(volatile uint8_t *)addr = value;
}

static inline void pci_write16(uint8_t bus, uint8_t slot, uint8_t func,
                               uint16_t offset, uint16_t value) {
  uint64_t addr = PCI_ECAM_BASE | ((uint64_t)bus << 20) |
                  ((uint64_t)slot << 15) | ((uint64_t)func << 12) | offset;
  *(volatile uint16_t *)addr = value;
}

/* Enable device memory and bus mastering */
static inline void pci_enable_device(pci_device_t *dev) {
  uint32_t cmd = pci_read32(dev->bus, dev->slot, dev->func, PCI_COMMAND);
  cmd |= PCI_CMD_MEM | PCI_CMD_BUS_MASTER;
  pci_write32(dev->bus, dev->slot, dev->func, PCI_COMMAND, cmd);
}

/* Find a device by vendor/device ID */
pci_device_t *pci_find_device(uint16_t vendor, uint16_t device);

#endif
