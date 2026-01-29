/*
 * Minimal EHCI bring-up + HID boot keyboard (polling)
 */

#include "../include/pci.h"
#include "../include/idt.h"
#include "../include/kmalloc.h"
#include "../include/usb.h"
#include "../include/mmio.h"
#include "../include/string.h"
#include "../include/gui.h"

#define EHCI_QH_HORIZ_QH 0x2
#define EHCI_QTD_T 0x1

#define EHCI_TOKEN_ACTIVE (1U << 7)
#define EHCI_TOKEN_PID_OUT (0U << 8)
#define EHCI_TOKEN_PID_IN (1U << 8)
#define EHCI_TOKEN_PID_SETUP (2U << 8)
#define EHCI_TOKEN_CERR (3U << 10)
#define EHCI_TOKEN_IOC (1U << 15)
#define EHCI_TOKEN_BYTES(x) ((uint32_t)(x) << 16)
#define EHCI_TOKEN_DT (1U << 31)

typedef struct __attribute__((aligned(32))) {
  uint32_t next;
  uint32_t alt_next;
  uint32_t token;
  uint32_t buf[5];
  uint32_t buf_hi[5];
} ehci_qtd_t;

typedef struct __attribute__((aligned(32))) {
  uint32_t horiz;
  uint32_t ep_char;
  uint32_t ep_cap;
  uint32_t curr_qtd;
  uint32_t next;
  uint32_t alt_next;
  uint32_t token;
  uint32_t buf[5];
  uint32_t buf_hi[5];
} ehci_qh_t;

static int ehci_present = 0;
static uint64_t ehci_mmio = 0;
static uint8_t ehci_irq = 0xFF;
static volatile int ehci_irq_seen = 0;

static uint32_t ehci_op_base = 0;
static uint32_t ehci_ports = 0;

static ehci_qh_t *async_head = NULL;
static ehci_qtd_t *ctrl_qtds = NULL;
static uint32_t *frame_list = NULL;
static ehci_qh_t *intr_qh = NULL;
static ehci_qtd_t *intr_qtd = NULL;
static uint8_t *intr_buf = NULL;
static uint16_t intr_mps = 8;
static uint8_t intr_ep = 1;
static uint8_t intr_interval = 4;
static uint8_t interface_num = 0;
static uint8_t config_value = 1;
static uint8_t device_addr = 1;
static uint16_t ctrl_mps = 8;

static uint8_t ctrl_setup_pkt[8];

static void usb_ehci_irq_handler(interrupt_frame_t *frame) {
  UNUSED(frame);
  ehci_irq_seen = 1;
}

static inline uint32_t ehci_read32(uint32_t off) {
  volatile uint32_t *reg = (volatile uint32_t *)(uintptr_t)(ehci_mmio + off);
  return *reg;
}

static inline void ehci_write32(uint32_t off, uint32_t val) {
  volatile uint32_t *reg = (volatile uint32_t *)(uintptr_t)(ehci_mmio + off);
  *reg = val;
}

static void *ehci_alloc_aligned(size_t size, size_t align) {
  size_t total = size + align;
  uint8_t *raw = (uint8_t *)kzalloc(total);
  if (!raw) {
    return NULL;
  }
  uintptr_t addr = (uintptr_t)raw;
  uintptr_t aligned = (addr + (align - 1)) & ~(align - 1);
  return (void *)aligned;
}

static void ehci_delay(int loops) {
  for (volatile int i = 0; i < loops; i++) {
    __asm__ volatile("pause");
  }
}

static inline uint64_t ehci_phys_addr(const void *ptr) {
  uint64_t v = (uint64_t)(uintptr_t)ptr;
  if (kernel_phys_base && kernel_virt_base && v >= kernel_virt_base) {
    return kernel_phys_base + (v - kernel_virt_base);
  }
  if (v >= hhdm_offset) {
    return v - hhdm_offset;
  }
  return v;
}

static inline uint32_t ehci_phys32(const void *ptr) {
  uint64_t phys = ehci_phys_addr(ptr);
  if (phys > 0xFFFFFFFFULL) {
    return 0;
  }
  return (uint32_t)phys;
}

/* Request BIOS->OS ownership if legacy support is enabled */
static void ehci_legacy_handoff(uint8_t bus, uint8_t device, uint8_t function) {
  uint32_t hccparams = ehci_read32(0x08);
  uint8_t eecp = (uint8_t)((hccparams >> 8) & 0xFF);
  if (eecp == 0) {
    return;
  }
  
  while (eecp && eecp < 0xFF) {
    uint32_t cap = pci_read32(bus, device, function, eecp);
    uint8_t cap_id = (uint8_t)(cap & 0xFF);
    uint8_t next = (uint8_t)((cap >> 8) & 0xFF);
    
    if (cap_id == 1) { /* EECP USB Legacy Support */
      /* Force OS ownership */
      uint32_t legsup = pci_read32(bus, device, function, eecp);
      pci_write32(bus, device, function, eecp, legsup | (1U << 24)); /* Set OS Owned */
      
      /* Wait for BIOS to release */
      int timeout = 1000;
      while (timeout-- > 0) {
        uint32_t cur = pci_read32(bus, device, function, eecp);
        if ((cur & (1U << 16)) == 0) {
          break;
        }
        ehci_delay(1000);
      }
      
      /* Force clear BIOS ownership if it didn't release */
      legsup = pci_read32(bus, device, function, eecp);
      if (legsup & (1U << 16)) {
        pci_write32(bus, device, function, eecp, legsup & ~(1U << 16));
      }
      
      /* Disable ALL legacy SMI sources */
      pci_write32(bus, device, function, eecp + 4, 0);
      
      /* Draw debug marker: ownership taken */
      debug_rect(120, 0, 10, 10, 0xFFFFFF00);
      return;
    }
    
    if (next == 0) {
      break;
    }
    eecp = next;
  }
}

static int ehci_fill_qtd(ehci_qtd_t *qtd, uint32_t pid, const void *buf, uint32_t len, uint32_t toggle) {
  memset(qtd, 0, sizeof(*qtd));
  qtd->next = EHCI_QTD_T;
  qtd->alt_next = EHCI_QTD_T;
  qtd->token = EHCI_TOKEN_ACTIVE | EHCI_TOKEN_CERR | EHCI_TOKEN_BYTES(len) | pid | toggle;
  uint64_t addr = buf ? ehci_phys_addr(buf) : 0;
  if (buf && addr == 0) {
    return -1;
  }
  for (int i = 0; i < 5; i++) {
    qtd->buf[i] = (uint32_t)addr;
    addr = (addr & ~0xFFFULL) + 0x1000;
  }
  return 0;
}

static void ehci_init_qh(ehci_qh_t *qh, uint8_t addr, uint8_t ep, uint16_t mps, uint8_t speed) {
  memset(qh, 0, sizeof(*qh));
  uint32_t phys = ehci_phys32(qh);
  qh->horiz = ((phys & ~0x1F) | EHCI_QH_HORIZ_QH);
  qh->ep_char = addr | (ep << 8) | (speed << 12) | (1U << 14) | (mps << 16) | (1U << 15);
  qh->ep_cap = 0;
  qh->curr_qtd = 0;
  qh->next = EHCI_QTD_T;
  qh->alt_next = EHCI_QTD_T;
  qh->token = 0;
}

static int ehci_wait_qtd_done(ehci_qtd_t *qtd, int timeout) {
  while (timeout-- > 0) {
    if ((qtd->token & EHCI_TOKEN_ACTIVE) == 0) {
      return 0;
    }
    ehci_delay(5000);
  }
  return -1;
}

static int ehci_control_transfer(uint8_t addr, uint8_t req_type, uint8_t req,
                                 uint16_t value, uint16_t index, void *data, uint16_t len) {
  if (!async_head || !ctrl_qtds) {
    return -1;
  }

  ctrl_setup_pkt[0] = req_type;
  ctrl_setup_pkt[1] = req;
  ctrl_setup_pkt[2] = (uint8_t)value;
  ctrl_setup_pkt[3] = (uint8_t)(value >> 8);
  ctrl_setup_pkt[4] = (uint8_t)index;
  ctrl_setup_pkt[5] = (uint8_t)(index >> 8);
  ctrl_setup_pkt[6] = (uint8_t)len;
  ctrl_setup_pkt[7] = (uint8_t)(len >> 8);

  ehci_init_qh(async_head, addr, 0, ctrl_mps, 0x00);
  async_head->next = ehci_phys32(&ctrl_qtds[0]);

  if (ehci_fill_qtd(&ctrl_qtds[0], EHCI_TOKEN_PID_SETUP, ctrl_setup_pkt, 8, 0) != 0) {
    return -1;
  }
  ctrl_qtds[0].next = ehci_phys32(&ctrl_qtds[1]);

  if (len > 0) {
    uint32_t pid = (req_type & 0x80) ? EHCI_TOKEN_PID_IN : EHCI_TOKEN_PID_OUT;
    uint32_t toggle = EHCI_TOKEN_DT;
    if (ehci_fill_qtd(&ctrl_qtds[1], pid, data, len, toggle) != 0) {
      return -1;
    }
    ctrl_qtds[1].next = ehci_phys32(&ctrl_qtds[2]);
  } else {
    ctrl_qtds[1].next = ehci_phys32(&ctrl_qtds[2]);
  }

  uint32_t status_pid = (req_type & 0x80) ? EHCI_TOKEN_PID_OUT : EHCI_TOKEN_PID_IN;
  if (ehci_fill_qtd(&ctrl_qtds[2], status_pid, NULL, 0, EHCI_TOKEN_DT) != 0) {
    return -1;
  }
  ctrl_qtds[2].next = EHCI_QTD_T;
  ctrl_qtds[2].token |= EHCI_TOKEN_IOC;

  async_head->next = ehci_phys32(&ctrl_qtds[0]);
  async_head->alt_next = EHCI_QTD_T;
  async_head->token = EHCI_TOKEN_ACTIVE;

  if (ehci_wait_qtd_done(&ctrl_qtds[2], 20000) != 0) {
    return -1;
  }
  return 0;
}

static int ehci_parse_hid(uint8_t *buf, uint16_t len, uint8_t *out_ep, uint16_t *out_mps,
                          uint8_t *out_interval, uint8_t *out_iface, uint8_t *out_cfg) {
  uint16_t i = 0;
  uint8_t cur_iface = 0xFF;
  uint8_t cur_cfg = 1;
  while (i + 2 <= len) {
    uint8_t dlen = buf[i];
    uint8_t dtype = buf[i + 1];
    if (dlen == 0 || i + dlen > len) {
      break;
    }
    if (dtype == 2 && dlen >= 9) {
      cur_cfg = buf[i + 5];
    } else if (dtype == 4 && dlen >= 9) {
      uint8_t cls = buf[i + 5];
      uint8_t sub = buf[i + 6];
      uint8_t proto = buf[i + 7];
      if (cls == 3 && sub == 1 && proto == 1) {
        cur_iface = buf[i + 2];
      } else {
        cur_iface = 0xFF;
      }
    } else if (dtype == 5 && dlen >= 7) {
      if (cur_iface != 0xFF) {
        uint8_t addr = buf[i + 2];
        uint8_t attr = buf[i + 3];
        if ((addr & 0x80) && ((attr & 0x3) == 3)) {
          *out_ep = addr & 0x0F;
          *out_mps = (uint16_t)(buf[i + 4] | (buf[i + 5] << 8));
          *out_interval = buf[i + 6];
          *out_iface = cur_iface;
          *out_cfg = cur_cfg;
          return 0;
        }
      }
    }
    i = (uint16_t)(i + dlen);
  }
  return -1;
}

static int ehci_setup_keyboard(uint32_t port) {
  uint32_t portsc_off = ehci_op_base + 0x44 + (port * 4);
  uint32_t portsc = ehci_read32(portsc_off);
  if ((portsc & 0x1) == 0) {
    return -1;
  }

  ehci_write32(portsc_off, portsc | (1U << 8));
  ehci_delay(20000);
  portsc = ehci_read32(portsc_off);
  ehci_write32(portsc_off, portsc & ~(1U << 8));
  ehci_delay(20000);
  portsc = ehci_read32(portsc_off);
  if ((portsc & (1U << 2)) == 0) {
    return -1;
  }

  uint8_t *dev_desc = (uint8_t *)kzalloc(18);
  if (!dev_desc) {
    return -1;
  }
  if (ehci_control_transfer(0, 0x80, 6, 0x0100, 0, dev_desc, 8) != 0) {
    return -1;
  }
  uint8_t mps = dev_desc[7];
  if (mps == 0) {
    mps = 8;
  }
  ctrl_mps = mps;

  if (ehci_control_transfer(0, 0x00, 5, device_addr, 0, NULL, 0) != 0) {
    return -1;
  }
  ehci_delay(10000);

  if (ehci_control_transfer(device_addr, 0x80, 6, 0x0100, 0, dev_desc, 18) != 0) {
    return -1;
  }

  uint8_t *cfg_hdr = (uint8_t *)kzalloc(9);
  if (!cfg_hdr) {
    return -1;
  }
  if (ehci_control_transfer(device_addr, 0x80, 6, 0x0200, 0, cfg_hdr, 9) != 0) {
    return -1;
  }
  uint16_t total = (uint16_t)(cfg_hdr[2] | (cfg_hdr[3] << 8));
  if (total < 9 || total > 512) {
    return -1;
  }
  uint8_t *cfg = (uint8_t *)kzalloc(total);
  if (!cfg) {
    return -1;
  }
  if (ehci_control_transfer(device_addr, 0x80, 6, 0x0200, 0, cfg, total) != 0) {
    return -1;
  }
  if (ehci_parse_hid(cfg, total, &intr_ep, &intr_mps, &intr_interval, &interface_num, &config_value) != 0) {
    return -1;
  }

  if (ehci_control_transfer(device_addr, 0x00, 9, config_value, 0, NULL, 0) != 0) {
    return -1;
  }
  ehci_control_transfer(device_addr, 0x21, 0x0B, 0, interface_num, NULL, 0);
  ehci_control_transfer(device_addr, 0x21, 0x0A, 0, interface_num, NULL, 0);

  ehci_init_qh(intr_qh, device_addr, intr_ep, intr_mps, 0x00);
  intr_qh->ep_cap = (1U << 0);
  if (ehci_fill_qtd(intr_qtd, EHCI_TOKEN_PID_IN, intr_buf, intr_mps, EHCI_TOKEN_DT) != 0) {
    return -1;
  }
  intr_qtd->token |= EHCI_TOKEN_IOC;
  intr_qh->next = ehci_phys32(intr_qtd);

  uint32_t qh_link = ((ehci_phys32(intr_qh) & ~0x1F) | EHCI_QH_HORIZ_QH);
  for (int i = 0; i < 1024; i++) {
    frame_list[i] = qh_link;
  }

  return 0;
}

void usb_ehci_init(void) {
  pci_device_info_t devs[4];
  int count = pci_scan_class(0x0C, 0x03, 0x20, devs, 4);
  if (count <= 0) {
    return;
  }

  /* Get BAR0 - this is a physical address */
  uint32_t bar0 = devs[0].bar[0];
  if ((bar0 & 0x01) != 0) {
    return; /* I/O space, not supported */
  }
  
  /* Check for invalid BAR */
  if (bar0 == 0 || bar0 == 0xFFFFFFFF) {
    return; /* No device or disabled */
  }
  
  /* Convert physical BAR to virtual address using HHDM */
  uint64_t phys_addr = (uint64_t)(bar0 & ~0x0F);
  
  /* Sanity check physical address */
  if (phys_addr == 0 || phys_addr > 0x100000000000ULL) {
    return; /* Invalid or too high address */
  }
  
  /* Map MMIO region into virtual space */
  uint64_t mmio_base = mmio_map_range(phys_addr, 0x1000);
  if (!mmio_base) {
    return;
  }
  ehci_mmio = mmio_base;
  
  /* Enable bus mastering and memory space before MMIO access */
  uint32_t cmd = pci_read32(devs[0].bus, devs[0].device, devs[0].function, 0x04);
  cmd |= (1U << 2) | (1U << 1);
  pci_write32(devs[0].bus, devs[0].device, devs[0].function, 0x04, cmd);

  /* Legacy BIOS handoff (if enabled) */
  ehci_legacy_handoff(devs[0].bus, devs[0].device, devs[0].function);

  /* Test MMIO access - read capability register */
  uint32_t cap = ehci_read32(0x00);
  if (cap == 0xFFFFFFFF || cap == 0) {
    return; /* MMIO not responding */
  }
  
  ehci_present = 1;

  uint32_t irq_reg = pci_read32(devs[0].bus, devs[0].device, devs[0].function, 0x3C);
  ehci_irq = (uint8_t)(irq_reg & 0xFF);
  if (ehci_irq < 16) {
    idt_register_handler((uint8_t)(0x20 + ehci_irq), usb_ehci_irq_handler);
    pic_clear_mask(ehci_irq);
  }

  uint8_t caplen = (uint8_t)(cap & 0xFF);
  uint32_t hcs = ehci_read32(0x04);
  ehci_ports = (hcs >> 0) & 0x0F;
  ehci_op_base = caplen;

  ehci_write32(ehci_op_base + 0x00, 0);
  ehci_delay(20000);
  ehci_write32(ehci_op_base + 0x00, (1U << 1));
  ehci_delay(100000);

  async_head = (ehci_qh_t *)kzalloc(sizeof(ehci_qh_t));
  ctrl_qtds = (ehci_qtd_t *)kzalloc(sizeof(ehci_qtd_t) * 3);
  intr_qh = (ehci_qh_t *)kzalloc(sizeof(ehci_qh_t));
  intr_qtd = (ehci_qtd_t *)kzalloc(sizeof(ehci_qtd_t));
  intr_buf = (uint8_t *)kzalloc(8);
  frame_list = (uint32_t *)ehci_alloc_aligned(1024 * sizeof(uint32_t), 4096);
  if (!async_head || !ctrl_qtds || !intr_qh || !intr_qtd || !intr_buf || !frame_list) {
    return;
  }
  if (!ehci_phys32(async_head) || !ehci_phys32(ctrl_qtds) ||
      !ehci_phys32(intr_qh) || !ehci_phys32(intr_qtd) ||
      !ehci_phys32(frame_list) || !ehci_phys32(intr_buf)) {
    return;
  }
  memset(frame_list, 0, 1024 * sizeof(uint32_t));

  ehci_init_qh(async_head, 0, 0, 64, 0x00);
  async_head->horiz = ((ehci_phys32(async_head) & ~0x1F) | EHCI_QH_HORIZ_QH);
  ehci_write32(ehci_op_base + 0x18, ehci_phys32(async_head));
  ehci_write32(ehci_op_base + 0x14, ehci_phys32(frame_list));

  ehci_write32(ehci_op_base + 0x40, 1);
  ehci_write32(ehci_op_base + 0x00, (1U << 0) | (1U << 4) | (1U << 5));

  if (ehci_ports > 0) {
    for (uint32_t port = 0; port < ehci_ports; port++) {
      if (ehci_setup_keyboard(port) == 0) {
        break;
      }
    }
  }
}

int usb_ehci_ready(void) {
  return ehci_present;
}

void usb_ehci_poll(void) {
  if (!ehci_present || !intr_qtd) {
    return;
  }

  if ((intr_qtd->token & EHCI_TOKEN_ACTIVE) == 0) {
    usb_submit_hid_report(intr_buf, intr_mps);
    if (ehci_fill_qtd(intr_qtd, EHCI_TOKEN_PID_IN, intr_buf, intr_mps, EHCI_TOKEN_DT) != 0) {
      return;
    }
    intr_qtd->token |= EHCI_TOKEN_IOC;
    intr_qh->next = ehci_phys32(intr_qtd);
  }

  if (ehci_irq_seen) {
    ehci_irq_seen = 0;
  }
}
