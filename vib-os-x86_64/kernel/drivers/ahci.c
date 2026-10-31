/*
 * Minimal synchronous AHCI driver for 512-byte SATA disks.
 *
 * The DMA objects below live in the kernel image's aligned BSS. Their physical
 * addresses are derived only for those statically linked objects, never for
 * arbitrary kmalloc buffers. One command slot and one staging buffer are shared
 * because this first implementation is synchronous and single-threaded.
 */
#include "../include/ahci.h"
#include "../include/blockdev.h"
#include "../include/pci.h"
#include "../include/mmio.h"
#include "../include/string.h"
#include "../include/types.h"

#define AHCI_PCI_CLASS       0x01
#define AHCI_PCI_SUBCLASS    0x06
#define AHCI_PCI_PROGIF      0x01
#define AHCI_MAX_HBAS        4
#define AHCI_MAX_DISKS       BLOCKDEV_MAX_DEVICES
#define AHCI_DMA_SECTORS     128U
#define AHCI_SECTOR_SIZE     512U
#define AHCI_WAIT_LIMIT      10000000U

#define HBA_CAP              0x00
#define HBA_GHC              0x04
#define HBA_PI               0x0C
#define HBA_CAP2             0x24
#define HBA_BOHC             0x28
#define HBA_PORT_BASE        0x100
#define HBA_PORT_STRIDE      0x80

#define PX_CLB               0x00
#define PX_CLBU              0x04
#define PX_FB                0x08
#define PX_FBU               0x0C
#define PX_IS                0x10
#define PX_IE                0x14
#define PX_CMD               0x18
#define PX_TFD               0x20
#define PX_SIG               0x24
#define PX_SSTS              0x28
#define PX_SERR              0x30
#define PX_CI                0x38

#define GHC_IE               (1U << 1)
#define GHC_HR               (1U << 0)
#define GHC_AE               (1U << 31)
#define BOHC_BOS             (1U << 0)
#define BOHC_OOS             (1U << 1)
#define BOHC_BB              (1U << 4)
#define PXCMD_ST              (1U << 0)
#define PXCMD_FRE             (1U << 4)
#define PXCMD_FR              (1U << 14)
#define PXCMD_CR              (1U << 15)
#define TFD_BSY               (1U << 7)
#define TFD_DRQ               (1U << 3)
#define TFD_ERR               (1U << 0)
#define PXIS_TFES             (1U << 30)
#define SATA_SIG_ATA          0x00000101U
#define ATA_IDENTIFY          0xEC
#define ATA_READ_DMA_EXT      0x25
#define ATA_WRITE_DMA_EXT     0x35
#define ATA_FLUSH_CACHE_EXT   0xEA
#define FIS_TYPE_REG_H2D      0x27
#define PCI_COMMAND_MEM       (1U << 1)
#define PCI_COMMAND_MASTER    (1U << 2)

extern void serial_puts(const char *s);
extern void serial_puthex(uint64_t value);

struct ahci_cmd_header {
  uint16_t flags;
  uint16_t prdt_length;
  uint32_t transferred;
  uint32_t table_base;
  uint32_t table_base_upper;
  uint32_t reserved[4];
} __attribute__((packed));

typedef struct ahci_prdt {
  uint32_t data_base;
  uint32_t data_base_upper;
  uint32_t reserved;
  uint32_t byte_count_flags;
} __attribute__((packed)) ahci_prdt_t;

typedef struct ahci_cmd_table {
  uint8_t fis[64];
  uint8_t atapi_command[16];
  uint8_t reserved[48];
  ahci_prdt_t prdt;
  uint8_t padding[112];
} __attribute__((packed, aligned(128))) ahci_cmd_table_t;

struct ahci_hba_state {
  volatile uint32_t *registers;
  uint32_t capabilities;
  uint32_t implemented_ports;
  uint8_t supports_64bit;
  uint8_t active;
};

struct ahci_disk_state {
  blockdev_t block;
  volatile uint32_t *port_registers;
  uint64_t sectors;
  uint8_t hba_index;
  uint8_t port_number;
  uint8_t active;
};

/* Each command list is exactly 1 KiB and is 1 KiB aligned. */
static struct ahci_cmd_header command_lists[AHCI_MAX_DISKS][32]
    __attribute__((aligned(1024)));
static uint8_t received_fis[AHCI_MAX_DISKS][256]
    __attribute__((aligned(256)));
static ahci_cmd_table_t command_tables[AHCI_MAX_DISKS]
    __attribute__((aligned(128)));
static uint8_t dma_buffer[AHCI_DMA_SECTORS * AHCI_SECTOR_SIZE]
    __attribute__((aligned(4096)));
static struct ahci_hba_state hbas[AHCI_MAX_HBAS];
static struct ahci_disk_state disks[AHCI_MAX_DISKS];
static uint32_t hba_count;
static uint32_t disk_count;
static int initialized;

static uint64_t virt_to_phys_static(const void *ptr) {
  uint64_t virt = (uint64_t)(uintptr_t)ptr;
  if (kernel_phys_base && kernel_virt_base && virt >= kernel_virt_base) {
    return kernel_phys_base + (virt - kernel_virt_base);
  }
  if (hhdm_offset && virt >= hhdm_offset) return virt - hhdm_offset;
  return 0;
}

static int dma_address_valid(uint64_t address, uint32_t supports_64bit) {
  if (!address) return 0;
  if (!supports_64bit && (address >> 32)) return 0;
  return 1;
}

static void wait_pause(void) {
  __asm__ volatile("pause" ::: "memory");
}

static int wait_mask_clear(volatile uint32_t *reg, uint32_t mask) {
  for (uint32_t i = 0; i < AHCI_WAIT_LIMIT; i++) {
    if ((*reg & mask) == 0) return 0;
    wait_pause();
  }
  return -1;
}

static int stop_port(volatile uint32_t *port) {
  uint32_t cmd = port[PX_CMD / 4];
  if (cmd & PXCMD_ST) {
    port[PX_CMD / 4] = cmd & ~PXCMD_ST;
    if (wait_mask_clear(&port[PX_CMD / 4], PXCMD_CR) != 0) return -1;
  }
  cmd = port[PX_CMD / 4];
  if (cmd & PXCMD_FRE) {
    port[PX_CMD / 4] = cmd & ~PXCMD_FRE;
    if (wait_mask_clear(&port[PX_CMD / 4], PXCMD_FR) != 0) return -1;
  }
  return 0;
}

static int start_port(volatile uint32_t *port) {
  uint32_t cmd = port[PX_CMD / 4];
  port[PX_CMD / 4] = cmd | PXCMD_FRE;
  __asm__ volatile("mfence" ::: "memory");
  port[PX_CMD / 4] = port[PX_CMD / 4] | PXCMD_ST;
  return 0;
}

static int setup_port(struct ahci_hba_state *hba, volatile uint32_t *port,
                      uint32_t disk_index) {
  uint64_t clb_phys = virt_to_phys_static(command_lists[disk_index]);
  uint64_t fb_phys = virt_to_phys_static(received_fis[disk_index]);
  uint64_t table_phys = virt_to_phys_static(&command_tables[disk_index]);
  if (!dma_address_valid(clb_phys, hba->supports_64bit) ||
      !dma_address_valid(fb_phys, hba->supports_64bit) ||
      !dma_address_valid(table_phys, hba->supports_64bit) ||
      !dma_address_valid(virt_to_phys_static(dma_buffer), hba->supports_64bit)) {
    return -1;
  }
  if (stop_port(port) != 0) return -1;

  memset(command_lists[disk_index], 0, sizeof(command_lists[disk_index]));
  memset(received_fis[disk_index], 0, sizeof(received_fis[disk_index]));
  memset(&command_tables[disk_index], 0, sizeof(command_tables[disk_index]));

  port[PX_CLB / 4] = (uint32_t)clb_phys;
  port[PX_CLBU / 4] = (uint32_t)(clb_phys >> 32);
  port[PX_FB / 4] = (uint32_t)fb_phys;
  port[PX_FBU / 4] = (uint32_t)(fb_phys >> 32);
  port[PX_IE / 4] = 0; /* synchronous polling; no AHCI IRQ handler yet */
  port[PX_IS / 4] = 0xFFFFFFFFU;
  port[PX_SERR / 4] = 0xFFFFFFFFU;

  struct ahci_cmd_header *header = &command_lists[disk_index][0];
  header->flags = 5; /* Register FIS is 5 DWORDs (20 bytes). */
  header->prdt_length = 1;
  header->table_base = (uint32_t)table_phys;
  header->table_base_upper = (uint32_t)(table_phys >> 32);
  __asm__ volatile("mfence" ::: "memory");
  return start_port(port);
}

static int issue_command(struct ahci_disk_state *disk, uint8_t command,
                         uint64_t lba, uint16_t count, int write_data,
                         uint32_t transfer_bytes) {
  if (!disk || !disk->active || transfer_bytes > sizeof(dma_buffer)) return -1;
  if ((command == ATA_READ_DMA_EXT || command == ATA_WRITE_DMA_EXT ||
       command == ATA_IDENTIFY) && (!count || !transfer_bytes)) return -1;
  volatile uint32_t *port = disk->port_registers;
  if (wait_mask_clear(&port[PX_TFD / 4], TFD_BSY | TFD_DRQ) != 0) return -1;
  if (port[PX_CI / 4] & 1U) return -1;

  struct ahci_cmd_header *header = &command_lists[disk - disks][0];
  ahci_cmd_table_t *table = &command_tables[disk - disks];
  uint64_t buffer_phys = virt_to_phys_static(dma_buffer);
  if (!buffer_phys || (buffer_phys >> 32 && !hbas[disk->hba_index].supports_64bit))
    return -1;

  memset(table, 0, sizeof(*table));
  header->flags = (uint16_t)(5U | (write_data ? (1U << 6) : 0));
  header->prdt_length = transfer_bytes ? 1 : 0;
  header->transferred = 0;
  if (transfer_bytes) {
    table->prdt.data_base = (uint32_t)buffer_phys;
    table->prdt.data_base_upper = (uint32_t)(buffer_phys >> 32);
    table->prdt.byte_count_flags = (transfer_bytes - 1U) | (1U << 31);
  }

  uint8_t *fis = table->fis;
  fis[0] = FIS_TYPE_REG_H2D;
  fis[1] = 0x80; /* command */
  fis[2] = command;
  if (command == ATA_READ_DMA_EXT || command == ATA_WRITE_DMA_EXT) {
    fis[4] = (uint8_t)lba;
    fis[5] = (uint8_t)(lba >> 8);
    fis[6] = (uint8_t)(lba >> 16);
    fis[7] = 0x40; /* LBA mode */
    fis[8] = (uint8_t)(lba >> 24);
    fis[9] = (uint8_t)(lba >> 32);
    fis[10] = (uint8_t)(lba >> 40);
    fis[12] = (uint8_t)count;
    fis[13] = (uint8_t)(count >> 8);
  }
  port[PX_IS / 4] = 0xFFFFFFFFU;
  port[PX_SERR / 4] = 0xFFFFFFFFU;
  __asm__ volatile("mfence" ::: "memory");
  port[PX_CI / 4] = 1U;

  for (uint32_t i = 0; i < AHCI_WAIT_LIMIT; i++) {
    uint32_t is = port[PX_IS / 4];
    if (is & PXIS_TFES) {
      port[PX_IS / 4] = is;
      return -1;
    }
    if ((port[PX_CI / 4] & 1U) == 0) {
      __asm__ volatile("mfence" ::: "memory");
      uint32_t tfd = port[PX_TFD / 4];
      port[PX_IS / 4] = 0xFFFFFFFFU;
      if (tfd & TFD_ERR) return -1;
      return 0;
    }
    wait_pause();
  }
  return -1;
}

static int ahci_read_sectors(blockdev_t *block, uint64_t lba,
                             uint32_t count, void *buffer) {
  struct ahci_disk_state *disk = (struct ahci_disk_state *)block->context;
  if (!disk || !buffer || !count || lba >= disk->sectors ||
      (uint64_t)count > disk->sectors - lba) return -1;
  uint8_t *out = (uint8_t *)buffer;
  while (count) {
    uint16_t chunk = (uint16_t)(count > AHCI_DMA_SECTORS ? AHCI_DMA_SECTORS : count);
    uint32_t bytes = (uint32_t)chunk * AHCI_SECTOR_SIZE;
    if (issue_command(disk, ATA_READ_DMA_EXT, lba, chunk, 0, bytes) != 0)
      return -1;
    memcpy(out, dma_buffer, bytes);
    out += bytes;
    lba += chunk;
    count -= chunk;
  }
  return 0;
}

static int ahci_write_sectors(blockdev_t *block, uint64_t lba,
                              uint32_t count, void *buffer) {
  struct ahci_disk_state *disk = (struct ahci_disk_state *)block->context;
  if (!disk || !buffer || !count || lba >= disk->sectors ||
      (uint64_t)count > disk->sectors - lba) return -1;
  const uint8_t *in = (const uint8_t *)buffer;
  while (count) {
    uint16_t chunk = (uint16_t)(count > AHCI_DMA_SECTORS ? AHCI_DMA_SECTORS : count);
    uint32_t bytes = (uint32_t)chunk * AHCI_SECTOR_SIZE;
    memcpy(dma_buffer, in, bytes);
    if (issue_command(disk, ATA_WRITE_DMA_EXT, lba, chunk, 1, bytes) != 0)
      return -1;
    in += bytes;
    lba += chunk;
    count -= chunk;
  }
  return 0;
}

static int ahci_flush(blockdev_t *block) {
  struct ahci_disk_state *disk = (struct ahci_disk_state *)block->context;
  if (!disk || !disk->active) return -1;
  return issue_command(disk, ATA_FLUSH_CACHE_EXT, 0, 0, 0, 0);
}

static uint64_t identify_capacity(const uint16_t *id) {
  if ((id[83] & 0xC000U) == 0x4000U && (id[83] & (1U << 10))) {
    return ((uint64_t)id[103] << 48) | ((uint64_t)id[102] << 32) |
           ((uint64_t)id[101] << 16) | id[100];
  }
  if (id[49] & (1U << 9)) return ((uint64_t)id[61] << 16) | id[60];
  return 0;
}

static int identify_sector_size(const uint16_t *id) {
  uint16_t word106 = id[106];
  if ((word106 & 0xC000U) == 0x4000U && (word106 & (1U << 12))) {
    uint32_t words = (uint32_t)id[117] | ((uint32_t)id[118] << 16);
    if (!words || words > 2048U) return -1;
    return (int)(words * 2U);
  }
  return AHCI_SECTOR_SIZE;
}

static void identify_model(const uint16_t *id, char *out, uint32_t out_size) {
  if (!out || out_size == 0) return;
  uint32_t pos = 0;
  for (uint32_t i = 27; i <= 46 && pos + 1 < out_size; i++) {
    uint8_t hi = (uint8_t)(id[i] >> 8);
    uint8_t lo = (uint8_t)id[i];
    out[pos++] = (char)hi;
    if (pos + 1 < out_size) out[pos++] = (char)lo;
  }
  while (pos && out[pos - 1] == ' ') pos--;
  out[pos] = '\0';
}

static int identify_disk(struct ahci_disk_state *disk, uint32_t disk_index,
                         struct ahci_hba_state *hba, volatile uint32_t *port) {
  if (setup_port(hba, port, disk_index) != 0) return -1;
  disk->port_registers = port;
  disk->hba_index = (uint8_t)(hba - hbas);
  disk->active = 1;
  memset(dma_buffer, 0, AHCI_SECTOR_SIZE);
  /* IDENTIFY uses a 512-byte data-in transfer and does not use the LBA/count fields. */
  if (issue_command(disk, ATA_IDENTIFY, 0, 1, 0, AHCI_SECTOR_SIZE) != 0) {
    disk->active = 0;
    return -1;
  }
  const uint16_t *id = (const uint16_t *)dma_buffer;
  uint64_t sectors = identify_capacity(id);
  int sector_size = identify_sector_size(id);
  if (!sectors || sector_size != AHCI_SECTOR_SIZE) {
    disk->active = 0;
    return -1;
  }
  disk->sectors = sectors;
  disk->block.sector_count = sectors;
  disk->block.sector_size = AHCI_SECTOR_SIZE;
  identify_model(id, disk->block.model, sizeof(disk->block.model));
  if (!disk->block.model[0]) strcpy(disk->block.model, "SATA disk");
  disk->block.read = ahci_read_sectors;
  disk->block.write = ahci_write_sectors;
  disk->block.flush = ahci_flush;
  disk->block.context = disk;
  return 0;
}

static int init_hba(const pci_device_info_t *pci) {
  uint32_t bar5 = pci->bar[5];
  if (!bar5 || bar5 == 0xFFFFFFFFU || (bar5 & 1U)) return -1;
  uint32_t bar_type = (bar5 >> 1) & 3U;
  if (bar_type == 2U || bar_type == 3U) return -1; /* BAR5 cannot be paired. */
  uint64_t abar_phys = (uint64_t)(bar5 & ~0x0FU);
  if (!abar_phys || abar_phys >= (1ULL << 52)) return -1;

  /* Set PCI memory-space and bus-master bits; do not echo write-1-to-clear status. */
  uint32_t command_status = pci_read32(pci->bus, pci->device, pci->function, 0x04);
  uint32_t command = (command_status & 0xFFFFU) | PCI_COMMAND_MEM | PCI_COMMAND_MASTER;
  pci_write32(pci->bus, pci->device, pci->function, 0x04, command);

  uint64_t mmio = mmio_map_range(abar_phys, 0x2200);
  if (!mmio) return -1;
  struct ahci_hba_state *hba = &hbas[hba_count];
  hba->registers = (volatile uint32_t *)(uintptr_t)mmio;
  hba->capabilities = hba->registers[HBA_CAP / 4];
  hba->implemented_ports = hba->registers[HBA_PI / 4];
  hba->supports_64bit = (hba->capabilities >> 31) & 1U;
  hba->active = 1;

  uint32_t cap2 = hba->registers[HBA_CAP2 / 4];
  if (cap2 & 1U) { /* BIOS/OS handoff supported. */
    hba->registers[HBA_BOHC / 4] |= BOHC_OOS;
    __asm__ volatile("mfence" ::: "memory");
    uint32_t i;
    for (i = 0; i < AHCI_WAIT_LIMIT; i++) {
      if ((hba->registers[HBA_BOHC / 4] & (BOHC_BOS | BOHC_BB)) == 0) break;
      wait_pause();
    }
    if (i == AHCI_WAIT_LIMIT) {
      hba->active = 0;
      return -1;
    }
  }

  uint32_t ghc = hba->registers[HBA_GHC / 4];
  ghc &= ~GHC_IE;
  ghc |= GHC_AE;
  hba->registers[HBA_GHC / 4] = ghc;
  hba->registers[0x08 / 4] = 0xFFFFFFFFU; /* Clear global interrupt status. */
  __asm__ volatile("mfence" ::: "memory");

  uint32_t hba_index = hba_count++;
  for (uint32_t port_no = 0; port_no < 32 && disk_count < AHCI_MAX_DISKS; port_no++) {
    if (!(hba->implemented_ports & (1U << port_no))) continue;
    volatile uint32_t *port = (volatile uint32_t *)((uintptr_t)hba->registers +
                            HBA_PORT_BASE + port_no * HBA_PORT_STRIDE);
    uint32_t ssts = port[PX_SSTS / 4];
    uint32_t signature = port[PX_SIG / 4];
    if ((ssts & 0x0FU) != 3U || ((ssts >> 8) & 0x0FU) != 1U ||
        signature != SATA_SIG_ATA) continue;
    struct ahci_disk_state *disk = &disks[disk_count];
    memset(disk, 0, sizeof(*disk));
    disk->hba_index = (uint8_t)hba_index;
    disk->port_number = (uint8_t)port_no;
    if (identify_disk(disk, disk_count, hba, port) != 0) continue;
    uint32_t name_len = sizeof(disk->block.name);
    if (name_len > 4) {
      disk->block.name[0] = 's';
      disk->block.name[1] = 'd';
      disk->block.name[2] = (char)('a' + disk_count);
      disk->block.name[3] = '\0';
    }
    if (blockdev_register(&disk->block) >= 0) disk_count++;
    else disk->active = 0;
  }
  return 0;
}

int ahci_init(void) {
  if (initialized) return (int)disk_count;
  initialized = 1;
  pci_device_info_t controllers[AHCI_MAX_HBAS];
  int found = pci_scan_class(AHCI_PCI_CLASS, AHCI_PCI_SUBCLASS,
                             AHCI_PCI_PROGIF, controllers, AHCI_MAX_HBAS);
  if (found < 0) return 0;
  if (found > AHCI_MAX_HBAS) found = AHCI_MAX_HBAS;
  for (int i = 0; i < found && hba_count < AHCI_MAX_HBAS; i++) {
    (void)init_hba(&controllers[i]);
  }
  return (int)disk_count;
}
