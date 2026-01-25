/*
 * Vib-OS - PCI Driver Implementation
 *
 * Scans ECAM to find devices. Supports virtio-gpu and Intel HDA.
 */

#include "drivers/pci.h"
#include "drivers/intel_hda.h"
#include "printk.h"
#include "types.h"

/* Device list */
static pci_device_t device_pool[16];
static int device_count = 0;
static pci_device_t *device_list = NULL;

/* MMIO allocation for unassigned BARs */
static uint64_t next_mmio_base = 0x10000000;

/* Helper to calculate ECAM address */
/* Bus 8 bits, Device 5 bits, Function 3 bits, Offset 12 bits */
static volatile uint32_t *pci_ecam_addr(uint8_t bus, uint8_t slot, uint8_t func,
                                        uint8_t offset) {
  uint64_t addr = PCI_ECAM_BASE | ((uint64_t)bus << 20) |
                  ((uint64_t)slot << 15) | ((uint64_t)func << 12) |
                  (offset & 0xFFF);
  return (volatile uint32_t *)addr;
}

uint32_t pci_read32(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset) {
  volatile uint32_t *addr = pci_ecam_addr(bus, slot, func, offset);
  return *addr;
}

void pci_write32(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset,
                 uint32_t value) {
  volatile uint32_t *addr = pci_ecam_addr(bus, slot, func, offset);
  *addr = value;
}

/* Find a device by vendor/device ID */
pci_device_t *pci_find_device(uint16_t vendor, uint16_t device) {
  pci_device_t *dev = device_list;
  while (dev) {
    if (dev->vendor_id == vendor && dev->device_id == device) {
      return dev;
    }
    dev = dev->next;
  }
  return NULL;
}

/* Allocate BAR if unassigned */
static uint64_t pci_alloc_bar(uint8_t bus, uint8_t slot, uint8_t func,
                              uint8_t bar_offset) {
  uint32_t bar_raw = pci_read32(bus, slot, func, bar_offset);
  uint32_t flags = bar_raw & 0xF;

  /* Check if BAR is already assigned with valid address */
  if ((bar_raw & 0xFFFFFFF0) != 0 && bar_raw != 0xFFFFFFFF) {
    return bar_raw & 0xFFFFFFF0;
  }

  /* BAR is unassigned - try to size and allocate */
  pci_write32(bus, slot, func, bar_offset, 0xFFFFFFFF);
  uint32_t size_val = pci_read32(bus, slot, func, bar_offset);
  pci_write32(bus, slot, func, bar_offset, bar_raw); /* Restore */

  /* Check if BAR responds to sizing */
  if (size_val == 0 || size_val == 0xFFFFFFFF) {
    return 0; /* BAR not implemented */
  }

  /* Calculate size from response */
  uint32_t size_mask = size_val & 0xFFFFFFF0;
  uint32_t size = (~size_mask) + 1;
  if (size == 0 || size > 0x10000000)
    size = 0x4000; /* Default to 16KB if invalid */

  /* Check if 64-bit BAR */
  bool is_64bit = (flags & 0x4);

  /* Align allocation */
  next_mmio_base = (next_mmio_base + size - 1) & ~(size - 1);
  uint64_t addr = next_mmio_base;

  /* Write new address */
  pci_write32(bus, slot, func, bar_offset, (uint32_t)addr | (flags & 0xF));

  /* Handle 64-bit BAR */
  if (is_64bit) {
    pci_write32(bus, slot, func, bar_offset + 4, (uint32_t)(addr >> 32));
  }

  next_mmio_base += size;

  printk("PCI:   [%02x:%02x.%x] BAR@0x%02x allocated at 0x%llx (size 0x%x)\n",
         bus, slot, func, bar_offset, addr, size);
  return addr;
}

void pci_init(void) {
  printk("PCI: Initializing High ECAM scan at 0x%llx...\n", PCI_ECAM_BASE);

  /* Brute force scan of Bus 0 */
  /* QEMU virt usually puts devices on Bus 0 */
  for (int slot = 0; slot < 32; slot++) {
    /* Check vendor */
    uint32_t vendor_dev = pci_read32(0, slot, 0, PCI_VENDOR_ID);
    uint16_t vendor = vendor_dev & 0xFFFF;
    uint16_t device = (vendor_dev >> 16) & 0xFFFF;

    if (vendor != 0xFFFF && vendor != 0x0000) {
      printk("PCI: Found %04x:%04x at 00:%02x.0\n", vendor, device, slot);

      /* Allocate device from pool */
      if (device_count >= 16) {
        printk("PCI: Device pool full!\n");
        continue;
      }

      pci_device_t *pci_dev = &device_pool[device_count++];
      pci_dev->bus = 0;
      pci_dev->slot = slot;
      pci_dev->func = 0;
      pci_dev->vendor_id = vendor;
      pci_dev->device_id = device;

      /* Read class info */
      uint32_t class_rev = pci_read32(0, slot, 0, PCI_CLASS_REV);
      pci_dev->class_code = (class_rev >> 24) & 0xFF;
      pci_dev->subclass = (class_rev >> 16) & 0xFF;

      /* Read BAR0 */
      pci_dev->bar0 = pci_alloc_bar(0, slot, 0, PCI_BAR0);
      pci_dev->bar1 = pci_alloc_bar(0, slot, 0, PCI_BAR1);
      pci_dev->bar2 = pci_alloc_bar(0, slot, 0, PCI_BAR2);

      /* Read Interrupt Line */
      uint32_t irq_line = pci_read32(0, slot, 0, PCI_INTERRUPT);
      pci_dev->irq = irq_line & 0xFF;

      /* Add to linked list */
      pci_dev->next = device_list;
      device_list = pci_dev;

      /* Check if it's Intel HDA */
      if (vendor == HDA_VENDOR_ID && device == HDA_DEVICE_ID) {
        printk("PCI: Found Inteal HDA Audio Controller!\n");
        printk("PCI: HDA BAR0=0x%llx, IRQ=%d\n", pci_dev->bar0, pci_dev->irq);
        intel_hda_init(pci_dev);
      }

      /* Check if it's virtio-gpu */
      if (vendor == PCI_VENDOR_VIRTIO && device == PCI_DEVICE_VIRTIO_GPU) {
        printk("PCI: Found virtio-gpu device!\n");
        printk("PCI: virtio-gpu BAR0=0x%llx\n", pci_dev->bar0);
      }
    }
  }
  printk("PCI: Scan complete (%d devices found).\n", device_count);
}
