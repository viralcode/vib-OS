/*
 * Minimal PCI enumeration (bus 0-255, device 0-31, function 0-7)
 */

#include "../include/pci.h"

#define PCI_CONFIG_ADDR 0xCF8
#define PCI_CONFIG_DATA 0xCFC

static inline void outl(uint16_t port, uint32_t val) {
  __asm__ volatile("outl %0, %1" : : "a"(val), "Nd"(port));
}

static inline uint32_t inl(uint16_t port) {
  uint32_t ret;
  __asm__ volatile("inl %1, %0" : "=a"(ret) : "Nd"(port));
  return ret;
}

static inline uint32_t pci_config_addr(uint8_t bus, uint8_t device,
                                       uint8_t function, uint8_t offset) {
  return (uint32_t)(0x80000000U |
                    ((uint32_t)bus << 16) |
                    ((uint32_t)device << 11) |
                    ((uint32_t)function << 8) |
                    (offset & 0xFC));
}

uint32_t pci_read32(uint8_t bus, uint8_t device, uint8_t function, uint8_t offset) {
  outl(PCI_CONFIG_ADDR, pci_config_addr(bus, device, function, offset));
  return inl(PCI_CONFIG_DATA);
}

void pci_write32(uint8_t bus, uint8_t device, uint8_t function,
                 uint8_t offset, uint32_t value) {
  outl(PCI_CONFIG_ADDR, pci_config_addr(bus, device, function, offset));
  outl(PCI_CONFIG_DATA, value);
}

static uint16_t pci_read16(uint8_t bus, uint8_t device, uint8_t function, uint8_t offset) {
  uint32_t val = pci_read32(bus, device, function, offset);
  return (uint16_t)((val >> ((offset & 2) * 8)) & 0xFFFF);
}

static uint8_t pci_read8(uint8_t bus, uint8_t device, uint8_t function, uint8_t offset) {
  uint32_t val = pci_read32(bus, device, function, offset);
  return (uint8_t)((val >> ((offset & 3) * 8)) & 0xFF);
}

static void pci_fill_bars(pci_device_info_t *info) {
  for (int i = 0; i < 6; i++) {
    info->bar[i] = pci_read32(info->bus, info->device, info->function, 0x10 + i * 4);
  }
}

int pci_scan_class(uint8_t class_code, uint8_t subclass, uint8_t prog_if,
                   pci_device_info_t *out, int max_out) {
  int found = 0;
  for (uint16_t bus = 0; bus < 256; bus++) {
    for (uint8_t device = 0; device < 32; device++) {
      for (uint8_t function = 0; function < 8; function++) {
        uint16_t vendor = pci_read16(bus, device, function, 0x00);
        if (vendor == 0xFFFF) {
          if (function == 0) {
            break; /* no device */
          }
          continue;
        }

        uint8_t cls = pci_read8(bus, device, function, 0x0B);
        uint8_t sub = pci_read8(bus, device, function, 0x0A);
        uint8_t prog = pci_read8(bus, device, function, 0x09);

        if (cls == class_code && sub == subclass &&
            (prog_if == 0xFF || prog == prog_if)) {
          if (out && found < max_out) {
            pci_device_info_t *info = &out[found];
            info->bus = (uint8_t)bus;
            info->device = device;
            info->function = function;
            info->vendor_id = vendor;
            info->device_id = pci_read16(bus, device, function, 0x02);
            info->class_code = cls;
            info->subclass = sub;
            info->prog_if = prog;
            pci_fill_bars(info);
          }
          found++;
        }

        /* If not multi-function, skip remaining functions */
        if (function == 0) {
          uint8_t header = pci_read8(bus, device, function, 0x0E);
          if ((header & 0x80) == 0) {
            break;
          }
        }
      }
    }
  }
  return found;
}
