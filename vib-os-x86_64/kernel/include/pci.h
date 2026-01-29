/*
 * Minimal PCI config space access and enumeration
 */

#ifndef _PCI_H
#define _PCI_H

#include "types.h"

typedef struct {
  uint8_t bus;
  uint8_t device;
  uint8_t function;
  uint16_t vendor_id;
  uint16_t device_id;
  uint8_t class_code;
  uint8_t subclass;
  uint8_t prog_if;
  uint32_t bar[6];
} pci_device_info_t;

uint32_t pci_read32(uint8_t bus, uint8_t device, uint8_t function, uint8_t offset);
void pci_write32(uint8_t bus, uint8_t device, uint8_t function, uint8_t offset, uint32_t value);

int pci_scan_class(uint8_t class_code, uint8_t subclass, uint8_t prog_if,
                   pci_device_info_t *out, int max_out);

#endif /* _PCI_H */
