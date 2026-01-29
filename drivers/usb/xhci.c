/*
 * Vib-OS - USB Host Controller Driver (XHCI)
 *
 * USB 3.0/3.1/3.2 Host Controller Interface.
 */

#include "mm/kmalloc.h"
#include "mm/pmm.h"
#include "mm/vmm.h"
#include "printk.h"
#include "types.h"

/* ===================================================================== */
/* XHCI Constants */
/* ===================================================================== */

#define XHCI_MAX_SLOTS 256
#define XHCI_MAX_PORTS 127
#define XHCI_MAX_INTERRUPTERS 1024

/* Capability Register offsets */
#define XHCI_CAPLENGTH 0x00
#define XHCI_HCIVERSION 0x02
#define XHCI_HCSPARAMS1 0x04
#define XHCI_HCSPARAMS2 0x08
#define XHCI_HCSPARAMS3 0x0C
#define XHCI_HCCPARAMS1 0x10
#define XHCI_DBOFF 0x14
#define XHCI_RTSOFF 0x18
#define XHCI_HCCPARAMS2 0x1C

/* Operational Register offsets (from OpBase) */
#define XHCI_USBCMD 0x00
#define XHCI_USBSTS 0x04
#define XHCI_PAGESIZE 0x08
#define XHCI_DNCTRL 0x14
#define XHCI_CRCR 0x18
#define XHCI_DCBAAP 0x30
#define XHCI_CONFIG 0x38

/* Port Register offsets */
#define XHCI_PORTSC 0x00
#define XHCI_PORTPMSC 0x04
#define XHCI_PORTLI 0x08
#define XHCI_PORTHLPMC 0x0C

/* USBCMD bits */
#define XHCI_CMD_RUN (1 << 0)
#define XHCI_CMD_HCRST (1 << 1)
#define XHCI_CMD_INTE (1 << 2)
#define XHCI_CMD_HSEE (1 << 3)

/* USBSTS bits */
#define XHCI_STS_HCH (1 << 0)
#define XHCI_STS_HSE (1 << 2)
#define XHCI_STS_EINT (1 << 3)
#define XHCI_STS_PCD (1 << 4)
#define XHCI_STS_CNR (1 << 11)

/* PORTSC bits */
#define XHCI_PORT_CCS (1 << 0)
#define XHCI_PORT_PED (1 << 1)
#define XHCI_PORT_OCA (1 << 3)
#define XHCI_PORT_PR (1 << 4)
#define XHCI_PORT_PP (1 << 9)
#define XHCI_PORT_CSC (1 << 17)
#define XHCI_PORT_PEC (1 << 18)
#define XHCI_PORT_WRC (1 << 19)
#define XHCI_PORT_OCC (1 << 20)
#define XHCI_PORT_PRC (1 << 21)

/* ===================================================================== */
/* TRB (Transfer Request Block) Types */
/* ===================================================================== */

#define TRB_TYPE_NORMAL 1
#define TRB_TYPE_SETUP 2
#define TRB_TYPE_DATA 3
#define TRB_TYPE_STATUS 4
#define TRB_TYPE_LINK 6
#define TRB_TYPE_EVENT_DATA 7
#define TRB_TYPE_NOOP 8
#define TRB_TYPE_ENABLE_SLOT 9
#define TRB_TYPE_DISABLE_SLOT 10
#define TRB_TYPE_ADDRESS_DEV 11
#define TRB_TYPE_CONFIG_EP 12
#define TRB_TYPE_EVAL_CTX 13
#define TRB_TYPE_RESET_EP 14
#define TRB_TYPE_STOP_EP 15
#define TRB_TYPE_SET_TR_DEQ 16
#define TRB_TYPE_RESET_DEV 17
#define TRB_TYPE_TRANSFER 32
#define TRB_TYPE_CMD_COMPLETE 33
#define TRB_TYPE_PORT_STATUS 34

struct xhci_trb {
  uint64_t param;
  uint32_t status;
  uint32_t control;
} __attribute__((packed));

/* ===================================================================== */
/* Device Context */
/* ===================================================================== */

struct xhci_slot_ctx {
  uint32_t field1;
  uint32_t field2;
  uint32_t tt_info;
  uint32_t state;
  uint32_t reserved[4];
} __attribute__((packed));

struct xhci_ep_ctx {
  uint32_t field1;
  uint32_t field2;
  uint64_t tr_dequeue;
  uint32_t tx_info;
  uint32_t reserved[3];
} __attribute__((packed));

/* ===================================================================== */
/* Driver State */
/* ===================================================================== */

struct xhci_device {
  volatile uint8_t *base;
  volatile uint8_t *op_base;
  volatile uint8_t *run_base;
  volatile uint8_t *db_base;

  uint32_t max_slots;
  uint32_t max_ports;
  uint32_t max_interrupters;
  uint32_t page_size;

  /* Device Context Base Address Array */
  uint64_t *dcbaa;
  phys_addr_t dcbaa_phys;

  /* Command Ring */
  struct xhci_trb *cmd_ring;
  phys_addr_t cmd_ring_phys;
  int cmd_ring_enqueue;
  bool cmd_ring_cycle;

  /* Event Ring */
  struct xhci_trb *event_ring;
  phys_addr_t event_ring_phys;
  int event_ring_dequeue;
  bool event_ring_cycle;

  /* Port info */
  struct {
    bool connected;
    uint8_t speed;
    uint8_t slot_id;
  } ports[XHCI_MAX_PORTS];
};

static struct xhci_device xhci = {0};

/* ===================================================================== */
/* MMIO Access */
/* ===================================================================== */

static inline uint32_t xhci_cap_read32(uint32_t offset) {
  return *(volatile uint32_t *)(xhci.base + offset);
}

static inline uint32_t xhci_op_read32(uint32_t offset) {
  return *(volatile uint32_t *)(xhci.op_base + offset);
}

static inline void xhci_op_write32(uint32_t offset, uint32_t val) {
  *(volatile uint32_t *)(xhci.op_base + offset) = val;
}

static inline void xhci_op_write64(uint32_t offset, uint64_t val) {
  *(volatile uint64_t *)(xhci.op_base + offset) = val;
}

/* ===================================================================== */
/* Ring Operations */
/* ===================================================================== */

static void xhci_ring_cmd(uint64_t param, uint32_t status, uint32_t control) {
  struct xhci_trb *trb = &xhci.cmd_ring[xhci.cmd_ring_enqueue];

  trb->param = param;
  trb->status = status;
  trb->control = control | (xhci.cmd_ring_cycle ? 1 : 0);

  xhci.cmd_ring_enqueue++;

  /* Handle ring wrap */
  if (xhci.cmd_ring_enqueue >= 63) {
    /* Insert link TRB */
    struct xhci_trb *link = &xhci.cmd_ring[63];
    link->param = xhci.cmd_ring_phys;
    link->status = 0;
    link->control =
        (TRB_TYPE_LINK << 10) | (xhci.cmd_ring_cycle ? 1 : 0) | (1 << 1);

    xhci.cmd_ring_enqueue = 0;
    xhci.cmd_ring_cycle = !xhci.cmd_ring_cycle;
  }

  /* Ring doorbell */
  *(volatile uint32_t *)(xhci.db_base) = 0;
}

/* ===================================================================== */
/* Controller Operations */
/* ===================================================================== */

static int xhci_reset(void) {
  printk(KERN_INFO "XHCI: Resetting controller\n");

  /* Stop controller */
  uint32_t cmd = xhci_op_read32(XHCI_USBCMD);
  xhci_op_write32(XHCI_USBCMD, cmd & ~XHCI_CMD_RUN);

  /* Wait for halt */
  int timeout = 1000;
  while (!(xhci_op_read32(XHCI_USBSTS) & XHCI_STS_HCH) && timeout > 0) {
    timeout--;
  }

  if (timeout == 0) {
    printk(KERN_ERR "XHCI: Failed to halt\n");
    return -1;
  }

  /* Reset */
  xhci_op_write32(XHCI_USBCMD, XHCI_CMD_HCRST);

  timeout = 1000;
  while ((xhci_op_read32(XHCI_USBCMD) & XHCI_CMD_HCRST) && timeout > 0) {
    timeout--;
  }

  if (timeout == 0) {
    printk(KERN_ERR "XHCI: Reset timeout\n");
    return -1;
  }

  /* Wait for CNR to clear */
  timeout = 1000;
  while ((xhci_op_read32(XHCI_USBSTS) & XHCI_STS_CNR) && timeout > 0) {
    timeout--;
  }

  printk(KERN_INFO "XHCI: Reset complete\n");
  return 0;
}

static int xhci_setup_rings(void) {
  /* Allocate DCBAA */
  xhci.dcbaa_phys = pmm_alloc_page();
  xhci.dcbaa = (uint64_t *)xhci.dcbaa_phys;
  for (int i = 0; i <= xhci.max_slots; i++) {
    xhci.dcbaa[i] = 0;
  }

  /* Set DCBAAP */
  xhci_op_write64(XHCI_DCBAAP, xhci.dcbaa_phys);

  /* Allocate Command Ring */
  xhci.cmd_ring_phys = pmm_alloc_page();
  xhci.cmd_ring = (struct xhci_trb *)xhci.cmd_ring_phys;
  xhci.cmd_ring_enqueue = 0;
  xhci.cmd_ring_cycle = true;

  /* Set CRCR */
  xhci_op_write64(XHCI_CRCR, xhci.cmd_ring_phys | 1);

  /* Allocate Event Ring */
  xhci.event_ring_phys = pmm_alloc_page();
  xhci.event_ring = (struct xhci_trb *)xhci.event_ring_phys;
  xhci.event_ring_dequeue = 0;
  xhci.event_ring_cycle = true;

  printk(KERN_INFO "XHCI: Rings initialized\n");
  return 0;
}

/* ===================================================================== */
/* Port Management */
/* ===================================================================== */

#include "drivers/usb/usb.h"

static void xhci_enumerate_device(int port) {
  printk(KERN_INFO "XHCI: Enumerating device on port %d\n", port + 1);

  /* TODO:
   * 1. Enable Slot
   * 2. Address Device
   * 3. Read Device Descriptor
   * 4. Read Configuration Descriptor
   */

  /* access to drivers */
  struct usb_device *dev = kzalloc(sizeof(struct usb_device), GFP_KERNEL);
  if (!dev)
    return;

  dev->controller = &xhci;
  dev->bus_id = 0;   // Bus 0
  dev->dev_addr = 0; // Pending

  /* Mock Dispatch for demonstration until descriptors are read */
  /* Checks would be based on bDeviceClass/bInterfaceClass */

  /* Attempt to load drivers */
  // In a real implementation we would match against descriptors
  // usb_hid_init(dev);
  // usb_msd_init(dev);
}

static void xhci_check_port(int port) {
  uint32_t portsc = *(volatile uint32_t *)(xhci.op_base + 0x400 + port * 0x10);

  bool connected = (portsc & XHCI_PORT_CCS) != 0;
  int speed = (portsc >> 10) & 0xF;

  xhci.ports[port].connected = connected;
  xhci.ports[port].speed = speed;

  if (connected) {
    const char *speed_str = "Unknown";
    switch (speed) {
    case 1:
      speed_str = "Full Speed (12 Mbps)";
      break;
    case 2:
      speed_str = "Low Speed (1.5 Mbps)";
      break;
    case 3:
      speed_str = "High Speed (480 Mbps)";
      break;
    case 4:
      speed_str = "Super Speed (5 Gbps)";
      break;
    case 5:
      speed_str = "Super Speed+ (10 Gbps)";
      break;
    }
    printk(KERN_INFO "XHCI: Port %d: Device connected - %s\n", port + 1,
           speed_str);

    /* Trigger enumeration */
    xhci_enumerate_device(port);
  }
}

/* ===================================================================== */
/* Initialization */
/* ===================================================================== */

int xhci_init(phys_addr_t mmio_base) {
  printk(KERN_INFO "XHCI: Initializing USB 3.x Host Controller\n");

  /* Map MMIO */
  xhci.base = (volatile uint8_t *)mmio_base;
  vmm_map_range(mmio_base, mmio_base, 0x10000, VM_DEVICE);

  /* Read capability registers */
  uint8_t cap_length = xhci.base[XHCI_CAPLENGTH];
  uint16_t hci_version = *(uint16_t *)(xhci.base + XHCI_HCIVERSION);
  uint32_t hcsparams1 = xhci_cap_read32(XHCI_HCSPARAMS1);

  xhci.max_slots = hcsparams1 & 0xFF;
  xhci.max_ports = (hcsparams1 >> 24) & 0xFF;
  xhci.max_interrupters = (hcsparams1 >> 8) & 0x7FF;

  printk(KERN_INFO "XHCI: Version %x.%x, %d slots, %d ports\n",
         hci_version >> 8, hci_version & 0xFF, xhci.max_slots, xhci.max_ports);

  /* Calculate register bases */
  xhci.op_base = xhci.base + cap_length;

  uint32_t rtsoff = xhci_cap_read32(XHCI_RTSOFF) & ~0x1F;
  xhci.run_base = xhci.base + rtsoff;

  uint32_t dboff = xhci_cap_read32(XHCI_DBOFF) & ~0x3;
  xhci.db_base = xhci.base + dboff;

  /* Get page size */
  xhci.page_size = xhci_op_read32(XHCI_PAGESIZE) << 12;

  /* Reset controller */
  if (xhci_reset() < 0) {
    return -1;
  }

  /* Set max slots */
  xhci_op_write32(XHCI_CONFIG, xhci.max_slots);

  /* Setup rings */
  if (xhci_setup_rings() < 0) {
    return -1;
  }

  /* Start controller */
  uint32_t cmd = xhci_op_read32(XHCI_USBCMD);
  xhci_op_write32(XHCI_USBCMD, cmd | XHCI_CMD_RUN | XHCI_CMD_INTE);

  /* Check ports */
  for (int i = 0; i < (int)xhci.max_ports; i++) {
    xhci_check_port(i);
  }

  printk(KERN_INFO "XHCI: Controller started\n");
  return 0;
}
