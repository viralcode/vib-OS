/*
 * Vib-OS - virtio-gpu Driver (Simplified)
 *
 * Implements virtio-gpu for hardware-accelerated graphics in QEMU.
 * Compatible with the simple PCI API.
 */

#include "drivers/pci.h"
#include "mm/pmm.h"
#include "mm/vmm.h"
#include "printk.h"
#include "types.h"

/* ===================================================================== */
/* Virtio Configuration Space */
/* ===================================================================== */

/* Virtio Configuration Offsets (from BAR for modern devices) */
#define VIRTIO_PCI_CAP_COMMON_CFG 1
#define VIRTIO_PCI_CAP_NOTIFY_CFG 2
#define VIRTIO_PCI_CAP_ISR_CFG 3
#define VIRTIO_PCI_CAP_DEVICE_CFG 4

/* Common configuration offsets */
#define VIRTIO_PCI_COMMON_DFSELECT 0x00
#define VIRTIO_PCI_COMMON_DF 0x04
#define VIRTIO_PCI_COMMON_GFSELECT 0x08
#define VIRTIO_PCI_COMMON_GF 0x0C
#define VIRTIO_PCI_COMMON_MSIX 0x10
#define VIRTIO_PCI_COMMON_NUMQ 0x12
#define VIRTIO_PCI_COMMON_STATUS 0x14
#define VIRTIO_PCI_COMMON_CFGGEN 0x15
#define VIRTIO_PCI_COMMON_Q_SELECT 0x16
#define VIRTIO_PCI_COMMON_Q_SIZE 0x18
#define VIRTIO_PCI_COMMON_Q_MSIX 0x1A
#define VIRTIO_PCI_COMMON_Q_ENABLE 0x1C
#define VIRTIO_PCI_COMMON_Q_NOTIFY 0x1E
#define VIRTIO_PCI_COMMON_Q_DESC 0x20
#define VIRTIO_PCI_COMMON_Q_AVAIL 0x28
#define VIRTIO_PCI_COMMON_Q_USED 0x30

/* Device status bits */
#define VIRTIO_STATUS_ACKNOWLEDGE 0x01
#define VIRTIO_STATUS_DRIVER 0x02
#define VIRTIO_STATUS_DRIVER_OK 0x04
#define VIRTIO_STATUS_FEATURES_OK 0x08

/* GPU Feature bits */
#define VIRTIO_GPU_F_VIRGL (1 << 0) /* 3D support */
#define VIRTIO_GPU_F_EDID (1 << 1)  /* EDID */

/* GPU command types */
#define VIRTIO_GPU_CMD_GET_DISPLAY_INFO 0x0100
#define VIRTIO_GPU_CMD_RESOURCE_CREATE_2D 0x0101
#define VIRTIO_GPU_CMD_SET_SCANOUT 0x0103
#define VIRTIO_GPU_CMD_RESOURCE_FLUSH 0x0104
#define VIRTIO_GPU_CMD_TRANSFER_TO_HOST_2D 0x0105
#define VIRTIO_GPU_CMD_RESOURCE_ATTACH_BACKING 0x0106

#define VIRTIO_GPU_RESP_OK_NODATA 0x1100
#define VIRTIO_GPU_RESP_OK_DISPLAY_INFO 0x1101

/* Maximum number of scanouts */
#define VIRTIO_GPU_MAX_SCANOUTS 16

/* ===================================================================== */
/* Virtqueue Structures */
/* ===================================================================== */

typedef struct {
  uint64_t addr;
  uint32_t len;
  uint16_t flags;
  uint16_t next;
} __attribute__((packed)) virtq_desc_t;

typedef struct {
  uint16_t flags;
  uint16_t idx;
  uint16_t ring[];
} __attribute__((packed)) virtq_avail_t;

typedef struct {
  uint32_t id;
  uint32_t len;
} __attribute__((packed)) virtq_used_elem_t;

typedef struct {
  uint16_t flags;
  uint16_t idx;
  virtq_used_elem_t ring[];
} __attribute__((packed)) virtq_used_t;

/* ===================================================================== */
/* GPU Command Structures */
/* ===================================================================== */

typedef struct {
  uint32_t type;
  uint32_t flags;
  uint64_t fence_id;
  uint32_t ctx_id;
  uint32_t padding;
} __attribute__((packed)) virtio_gpu_ctrl_hdr_t;

typedef struct {
  virtio_gpu_ctrl_hdr_t hdr;
  uint32_t scanouts;
  uint32_t padding;
  struct {
    uint32_t x, y, width, height;
    uint32_t enabled;
    uint32_t flags;
  } pmodes[VIRTIO_GPU_MAX_SCANOUTS];
} __attribute__((packed)) virtio_gpu_display_info_t;

/* ===================================================================== */
/* Driver State */
/* ===================================================================== */

typedef struct {
  pci_device_t *pci;
  volatile uint8_t *common_cfg;  /* Common config BAR */
  volatile uint8_t *notify_base; /* Notify BAR */
  volatile uint8_t *device_cfg;  /* Device-specific config */
  uint32_t notify_offset_mult;

  /* Virtqueues */
  virtq_desc_t *controlq_desc;
  virtq_avail_t *controlq_avail;
  virtq_used_t *controlq_used;
  uint16_t controlq_size;
  uint16_t controlq_last_used;

  /* Display info */
  uint32_t width;
  uint32_t height;
  bool has_virgl; /* 3D acceleration */
  bool initialized;
} virtio_gpu_device_t;

static virtio_gpu_device_t vgpu_dev;

/* ===================================================================== */
/* MMIO Helpers */
/* ===================================================================== */

static inline void vgpu_write8(volatile uint8_t *base, uint32_t offset,
                               uint8_t val) {
  *(volatile uint8_t *)(base + offset) = val;
}

static inline void vgpu_write16(volatile uint8_t *base, uint32_t offset,
                                uint16_t val) {
  *(volatile uint16_t *)(base + offset) = val;
}

static inline void vgpu_write32(volatile uint8_t *base, uint32_t offset,
                                uint32_t val) {
  *(volatile uint32_t *)(base + offset) = val;
}

static inline void vgpu_write64(volatile uint8_t *base, uint32_t offset,
                                uint64_t val) {
  *(volatile uint64_t *)(base + offset) = val;
}

static inline uint8_t vgpu_read8(volatile uint8_t *base, uint32_t offset) {
  return *(volatile uint8_t *)(base + offset);
}

static inline uint16_t vgpu_read16(volatile uint8_t *base, uint32_t offset) {
  return *(volatile uint16_t *)(base + offset);
}

static inline uint32_t vgpu_read32(volatile uint8_t *base, uint32_t offset) {
  return *(volatile uint32_t *)(base + offset);
}

/* ===================================================================== */
/* Virtqueue Operations */
/* ===================================================================== */

static int vgpu_alloc_virtqueue(uint16_t size) {
  /* Allocate descriptor table, available ring, and used ring */
  size_t desc_size = sizeof(virtq_desc_t) * size;
  size_t avail_size = sizeof(uint16_t) * (3 + size);
  size_t used_size = sizeof(uint16_t) * 3 + sizeof(virtq_used_elem_t) * size;

  size_t total = desc_size + avail_size + used_size;
  total = (total + 4095) & ~4095; /* Page align */

  void *mem = (void *)pmm_alloc_pages(total / 4096);
  if (!mem)
    return -1;

  /* Zero the memory */
  for (size_t i = 0; i < total; i++) {
    ((uint8_t *)mem)[i] = 0;
  }

  vgpu_dev.controlq_desc = (virtq_desc_t *)mem;
  vgpu_dev.controlq_avail = (virtq_avail_t *)((uint8_t *)mem + desc_size);
  vgpu_dev.controlq_used =
      (virtq_used_t *)((uint8_t *)mem + desc_size + avail_size);
  vgpu_dev.controlq_size = size;
  vgpu_dev.controlq_last_used = 0;

  return 0;
}

/* ===================================================================== */
/* Capability Parsing */
/* ===================================================================== */

/* Helper to read a BAR from config space, allocating if needed */
static uint64_t vgpu_read_bar(pci_device_t *pci, uint8_t bar_num) {
  uint16_t bar_offset = PCI_BAR0 + (bar_num * 4);
  uint32_t bar_raw = pci_read32(pci->bus, pci->slot, pci->func, bar_offset);
  uint32_t flags = bar_raw & 0xF;
  bool is_64bit = (flags & 0x4);

  /* Check if already assigned */
  uint64_t addr = bar_raw & 0xFFFFFFF0;
  if (is_64bit) {
    uint32_t bar_high =
        pci_read32(pci->bus, pci->slot, pci->func, bar_offset + 4);
    addr |= ((uint64_t)bar_high << 32);
  }

  if (addr != 0) {
    return addr;
  }

  /* Need to allocate - size the BAR */
  pci_write32(pci->bus, pci->slot, pci->func, bar_offset, 0xFFFFFFFF);
  uint32_t size_val = pci_read32(pci->bus, pci->slot, pci->func, bar_offset);
  pci_write32(pci->bus, pci->slot, pci->func, bar_offset, bar_raw);

  if (size_val == 0 || size_val == 0xFFFFFFFF) {
    return 0;
  }

  uint32_t size_mask = size_val & 0xFFFFFFF0;
  uint32_t size = (~size_mask) + 1;
  if (size == 0)
    size = 0x4000;

  /* Use a safe MMIO address above what PCI init uses */
  static uint64_t vgpu_mmio_base = 0x10100000;
  vgpu_mmio_base = (vgpu_mmio_base + size - 1) & ~((uint64_t)size - 1);
  addr = vgpu_mmio_base;

  pci_write32(pci->bus, pci->slot, pci->func, bar_offset,
              (uint32_t)addr | (flags & 0xF));
  if (is_64bit) {
    pci_write32(pci->bus, pci->slot, pci->func, bar_offset + 4,
                (uint32_t)(addr >> 32));
  }

  vgpu_mmio_base += size;

  printk("VGPU: Allocated BAR%d at 0x%llx (size 0x%x)\n", bar_num, addr, size);
  return addr;
}

static int vgpu_parse_capabilities(pci_device_t *pci) {
  /* Check if capabilities list exists */
  uint16_t status = pci_read16(pci->bus, pci->slot, pci->func, PCI_STATUS);
  if (!(status & PCI_STATUS_CAP_LIST)) {
    printk("VGPU: No PCI capabilities list\n");
    return -1;
  }

  /* Get first capability pointer */
  uint8_t cap_ptr = pci_read8(pci->bus, pci->slot, pci->func, PCI_CAPABILITIES);
  cap_ptr &= 0xFC; /* Mask lower 2 bits */

  printk("VGPU: Parsing capabilities (first cap at 0x%02x)\n", cap_ptr);

  /* Walk capabilities list */
  int found_common = 0;
  int ttl = 48;

  while (cap_ptr && ttl-- > 0) {
    uint8_t cap_id = pci_read8(pci->bus, pci->slot, pci->func, cap_ptr);

    if (cap_id == PCI_CAP_ID_VNDR) { /* Vendor-specific = virtio */
      uint8_t cfg_type = pci_read8(pci->bus, pci->slot, pci->func, cap_ptr + 3);
      uint8_t bar_num = pci_read8(pci->bus, pci->slot, pci->func, cap_ptr + 4);
      uint32_t offset = pci_read32(pci->bus, pci->slot, pci->func, cap_ptr + 8);

      /* Read (or allocate) the BAR */
      uint64_t bar_addr = vgpu_read_bar(pci, bar_num);

      if (bar_addr == 0) {
        cap_ptr = pci_read8(pci->bus, pci->slot, pci->func, cap_ptr + 1);
        cap_ptr &= 0xFC;
        continue;
      }

      volatile uint8_t *cfg_base = (volatile uint8_t *)(bar_addr + offset);

      switch (cfg_type) {
      case VIRTIO_PCI_CAP_COMMON_CFG:
        vgpu_dev.common_cfg = cfg_base;
        found_common = 1;
        printk("VGPU: Found common config at BAR%d+0x%x\n", bar_num, offset);
        break;
      case VIRTIO_PCI_CAP_NOTIFY_CFG:
        vgpu_dev.notify_base = cfg_base;
        vgpu_dev.notify_offset_mult =
            pci_read32(pci->bus, pci->slot, pci->func, cap_ptr + 16);
        printk("VGPU: Found notify config at BAR%d+0x%x\n", bar_num, offset);
        break;
      case VIRTIO_PCI_CAP_DEVICE_CFG:
        vgpu_dev.device_cfg = cfg_base;
        printk("VGPU: Found device config at BAR%d+0x%x\n", bar_num, offset);
        break;
      }
    }

    cap_ptr = pci_read8(pci->bus, pci->slot, pci->func, cap_ptr + 1);
    cap_ptr &= 0xFC;
  }

  return found_common ? 0 : -1;
}

/* ===================================================================== */
/* Device Initialization */
/* ===================================================================== */

int virtio_gpu_init(pci_device_t *pci) {
  if (!pci || vgpu_dev.initialized) {
    return -1;
  }

  printk("VGPU: Initializing virtio-gpu driver\n");
  printk("VGPU: Device at %02x:%02x.%x\n", pci->bus, pci->slot, pci->func);

  vgpu_dev.pci = pci;

  /* Enable PCI device */
  pci_enable_device(pci);

  /* Parse PCI capabilities to find virtio config areas */
  if (vgpu_parse_capabilities(pci) < 0) {
    printk("VGPU: Failed to parse capabilities\n");
    return -1;
  }

  if (!vgpu_dev.common_cfg) {
    printk("VGPU: Common config not found\n");
    return -1;
  }

  /* Reset device */
  vgpu_write8(vgpu_dev.common_cfg, VIRTIO_PCI_COMMON_STATUS, 0);

  /* Set ACKNOWLEDGE status */
  vgpu_write8(vgpu_dev.common_cfg, VIRTIO_PCI_COMMON_STATUS,
              VIRTIO_STATUS_ACKNOWLEDGE);

  /* Set DRIVER status */
  uint8_t status = vgpu_read8(vgpu_dev.common_cfg, VIRTIO_PCI_COMMON_STATUS);
  vgpu_write8(vgpu_dev.common_cfg, VIRTIO_PCI_COMMON_STATUS,
              status | VIRTIO_STATUS_DRIVER);

  /* Read features */
  vgpu_write32(vgpu_dev.common_cfg, VIRTIO_PCI_COMMON_DFSELECT, 0);
  uint32_t features = vgpu_read32(vgpu_dev.common_cfg, VIRTIO_PCI_COMMON_DF);

  vgpu_dev.has_virgl = (features & VIRTIO_GPU_F_VIRGL) != 0;
  printk("VGPU: Features: 0x%08x (virgl=%d)\n", features, vgpu_dev.has_virgl);

  /* Accept features */
  vgpu_write32(vgpu_dev.common_cfg, VIRTIO_PCI_COMMON_GFSELECT, 0);
  vgpu_write32(vgpu_dev.common_cfg, VIRTIO_PCI_COMMON_GF, features);

  /* Set FEATURES_OK */
  status = vgpu_read8(vgpu_dev.common_cfg, VIRTIO_PCI_COMMON_STATUS);
  vgpu_write8(vgpu_dev.common_cfg, VIRTIO_PCI_COMMON_STATUS,
              status | VIRTIO_STATUS_FEATURES_OK);

  /* Verify FEATURES_OK was set */
  status = vgpu_read8(vgpu_dev.common_cfg, VIRTIO_PCI_COMMON_STATUS);
  if (!(status & VIRTIO_STATUS_FEATURES_OK)) {
    printk("VGPU: Device did not accept features\n");
    return -1;
  }

  /* Get number of queues */
  uint16_t num_queues =
      vgpu_read16(vgpu_dev.common_cfg, VIRTIO_PCI_COMMON_NUMQ);
  printk("VGPU: Device has %d queues\n", num_queues);

  if (num_queues < 1) {
    printk("VGPU: No queues available\n");
    return -1;
  }

  /* Set up control queue (queue 0) */
  vgpu_write16(vgpu_dev.common_cfg, VIRTIO_PCI_COMMON_Q_SELECT, 0);
  uint16_t queue_size =
      vgpu_read16(vgpu_dev.common_cfg, VIRTIO_PCI_COMMON_Q_SIZE);
  printk("VGPU: Control queue size: %d\n", queue_size);

  if (queue_size > 256)
    queue_size = 256; /* Limit for safety */

  if (vgpu_alloc_virtqueue(queue_size) < 0) {
    printk("VGPU: Failed to allocate virtqueue\n");
    return -1;
  }

  /* Tell device about queue addresses */
  vgpu_write64(vgpu_dev.common_cfg, VIRTIO_PCI_COMMON_Q_DESC,
               (uint64_t)vgpu_dev.controlq_desc);
  vgpu_write64(vgpu_dev.common_cfg, VIRTIO_PCI_COMMON_Q_AVAIL,
               (uint64_t)vgpu_dev.controlq_avail);
  vgpu_write64(vgpu_dev.common_cfg, VIRTIO_PCI_COMMON_Q_USED,
               (uint64_t)vgpu_dev.controlq_used);

  /* Enable queue */
  vgpu_write16(vgpu_dev.common_cfg, VIRTIO_PCI_COMMON_Q_ENABLE, 1);

  /* Set DRIVER_OK */
  status = vgpu_read8(vgpu_dev.common_cfg, VIRTIO_PCI_COMMON_STATUS);
  vgpu_write8(vgpu_dev.common_cfg, VIRTIO_PCI_COMMON_STATUS,
              status | VIRTIO_STATUS_DRIVER_OK);

  printk("VGPU: Driver initialized (status=0x%02x)\n",
         status | VIRTIO_STATUS_DRIVER_OK);

  /* Default display size */
  vgpu_dev.width = 1024;
  vgpu_dev.height = 768;
  vgpu_dev.initialized = true;

  printk("VGPU: virtio-gpu ready (%dx%d, 3D=%s)\n", vgpu_dev.width,
         vgpu_dev.height, vgpu_dev.has_virgl ? "enabled" : "disabled");

  return 0;
}

/* ===================================================================== */
/* Public API */
/* ===================================================================== */

bool virtio_gpu_is_available(void) { return vgpu_dev.initialized; }

bool virtio_gpu_has_3d(void) {
  return vgpu_dev.initialized && vgpu_dev.has_virgl;
}

void virtio_gpu_get_display_size(uint32_t *width, uint32_t *height) {
  if (width)
    *width = vgpu_dev.initialized ? vgpu_dev.width : 0;
  if (height)
    *height = vgpu_dev.initialized ? vgpu_dev.height : 0;
}
