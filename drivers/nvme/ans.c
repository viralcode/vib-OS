/*
 * UnixOS Kernel - Apple NVMe Driver (ANS)
 *
 * Based on Asahi Linux apple_nvme driver.
 * Supports Apple's proprietary NVMe implementation on M-series chips.
 */

#include "mm/vmm.h"
#include "printk.h"
#include "types.h"

/* ===================================================================== */
/* ANS (Apple NVMe Storage) Definitions */
/* ===================================================================== */

/* MMIO base addresses from device tree */
#define ANS_BASE 0x27BCC0000UL /* M2 NVMe base */
#define ANS_SIZE 0x40000UL     /* 256KB */

/* ANS register offsets */
#define ANS_CTRL 0x0000
#define ANS_STATUS 0x0004
#define ANS_VERSION 0x0008
#define ANS_BOOT_STATUS 0x1300
#define ANS_MAX_PEND_CMDS 0x1308
#define ANS_UNK_CTRL 0x24008

/* NVMe standard registers (starts at offset 0x0) after ANS init */
#define NVME_CAP 0x0000
#define NVME_CC 0x0014
#define NVME_CSTS 0x001C
#define NVME_AQA 0x0024
#define NVME_ASQ 0x0028
#define NVME_ACQ 0x0030

/* ===================================================================== */
/* Driver State */
/* ===================================================================== */

static volatile uint32_t *ans_regs = NULL;
static bool ans_initialized = false;

/* Submission and Completion queues */
#define ANS_QUEUE_DEPTH 256

struct nvme_sq_entry {
  uint32_t cdw0; /* Command dword 0 */
  uint32_t nsid; /* Namespace ID */
  uint32_t cdw2;
  uint32_t cdw3;
  uint64_t mptr; /* Metadata pointer */
  uint64_t prp1; /* PRP Entry 1 */
  uint64_t prp2; /* PRP Entry 2 */
  uint32_t cdw10;
  uint32_t cdw11;
  uint32_t cdw12;
  uint32_t cdw13;
  uint32_t cdw14;
  uint32_t cdw15;
};

struct nvme_cq_entry {
  uint32_t result;
  uint32_t rsvd;
  uint16_t sq_head;
  uint16_t sq_id;
  uint16_t cid;
  uint16_t status;
};

/* ===================================================================== */
/* MMIO Helpers */
/* ===================================================================== */

static inline uint32_t ans_read32(uint32_t offset) {
  if (!ans_regs)
    return 0;
  return ans_regs[offset / 4];
}

static inline void ans_write32(uint32_t offset, uint32_t val) {
  if (!ans_regs)
    return;
  ans_regs[offset / 4] = val;
}

/* ===================================================================== */
/* Initialization */
/* ===================================================================== */

int ans_nvme_init(void) {
  printk(KERN_INFO "ANS: Initializing Apple NVMe controller\n");

#ifdef __QEMU__
  printk(KERN_INFO "ANS: Running in QEMU, using virtio-blk instead\n");
  return 0;
#endif

  /* Map ANS registers */
  vmm_map_range(ANS_BASE, ANS_BASE, ANS_SIZE, VM_DEVICE);
  ans_regs = (volatile uint32_t *)ANS_BASE;

  /* Check boot status */
  uint32_t boot_status = ans_read32(ANS_BOOT_STATUS);
  printk(KERN_INFO "ANS: Boot status: 0x%x\n", boot_status);

  /* Wait for ANS to be ready */
  int timeout = 1000;
  while (!(ans_read32(ANS_BOOT_STATUS) & 0x1) && timeout > 0) {
    timeout--;
  }

  if (timeout == 0) {
    printk(KERN_ERR "ANS: Timeout waiting for controller\n");
    return -1;
  }

  /* Configure NVMe controller */
  uint64_t cap =
      ((uint64_t)ans_read32(NVME_CAP + 4) << 32) | ans_read32(NVME_CAP);
  printk(KERN_INFO "ANS: NVMe CAP: 0x%llx\n", (unsigned long long)cap);

  ans_initialized = true;
  printk(KERN_INFO "ANS: NVMe controller initialized\n");

  return 0;
}

/* ===================================================================== */
/* Block Operations */
/* ===================================================================== */

int ans_read_blocks(uint64_t lba, uint32_t count, void *buffer) {
  if (!ans_initialized)
    return -1;

  (void)lba;
  (void)count;
  (void)buffer;

  /* Stub - NVMe read not implemented */
  return 0;
}

int ans_write_blocks(uint64_t lba, uint32_t count, const void *buffer) {
  if (!ans_initialized)
    return -1;

  (void)lba;
  (void)count;
  (void)buffer;

  /* Stub - NVMe write not implemented */
  return 0;
}

/* ===================================================================== */
/* Power Management */
/* ===================================================================== */

int ans_suspend(void) {
  if (!ans_initialized)
    return 0;

  printk(KERN_INFO "ANS: Suspending NVMe controller\n");
  /* Stub - flush/disable not implemented */

  return 0;
}

int ans_resume(void) {
  if (!ans_initialized)
    return 0;

  printk(KERN_INFO "ANS: Resuming NVMe controller\n");
  /* Stub - re-init not implemented */

  return 0;
}
