#include "../include/ahci.h"
#include "../include/block.h"
#include "../include/mmio.h"
#include "../include/pci.h"
#include "../include/string.h"
#include "../include/types.h"

#define AHCI_PCI_CLASS 0x01
#define AHCI_PCI_SUBCLASS 0x06
#define AHCI_PCI_PROGIF 0x01
#define AHCI_ABAR_BAR 5
#define AHCI_MAX_PORTS 32
#define AHCI_TIMEOUT 50000000ULL

#define HBA_GHC 0x04
#define HBA_PI  0x0C
#define HBA_PORTS 0x100
#define PORT_STRIDE 0x80
#define PX_CLB 0x00
#define PX_FB  0x08
#define PX_IS  0x10
#define PX_CMD 0x18
#define PX_TFD 0x20
#define PX_SIG 0x24
#define PX_SSTS 0x28
#define PX_SERR 0x30
#define PX_CI 0x38

#define CMD_ST (1U << 0)
#define CMD_FRE (1U << 4)
#define CMD_FR (1U << 14)
#define CMD_CR (1U << 15)
#define TFD_BSY 0x80
#define TFD_DRQ 0x08
#define TFD_ERR 0x01
#define SIG_ATA 0x00000101U
#define FIS_REG_H2D 0x27
#define FIS_CMD (1U << 7)
#define ATA_IDENTIFY 0xEC
#define ATA_READ_DMA_EXT 0x25
#define ATA_WRITE_DMA_EXT 0x35
#define ATA_FLUSH_CACHE_EXT 0xEA

extern void serial_puts(const char *s);
extern void serial_puthex(uint64_t value);

struct ahci_cmd_header {
  uint16_t flags;
  uint16_t prdt_length;
  volatile uint32_t transferred;
  uint32_t table_base;
  uint32_t table_base_upper;
  uint32_t reserved[4];
} __attribute__((packed));

struct ahci_prdt_entry {
  uint32_t data_base;
  uint32_t data_base_upper;
  uint32_t reserved;
  uint32_t byte_count_and_ioc;
} __attribute__((packed));

struct ahci_cmd_table {
  uint8_t command_fis[64];
  uint8_t atapi_command[16];
  uint8_t reserved[48];
  struct ahci_prdt_entry prdt[1];
} __attribute__((packed, aligned(128)));

struct ahci_fis_reg_h2d {
  uint8_t fis_type;
  uint8_t flags;
  uint8_t command;
  uint8_t feature_low;
  uint8_t lba0;
  uint8_t lba1;
  uint8_t lba2;
  uint8_t device;
  uint8_t lba3;
  uint8_t lba4;
  uint8_t lba5;
  uint8_t feature_high;
  uint8_t count_low;
  uint8_t count_high;
  uint8_t icc;
  uint8_t control;
  uint8_t reserved[4];
} __attribute__((packed));

static struct ahci_cmd_header command_list[32] __attribute__((aligned(1024)));
static uint8_t received_fis[256] __attribute__((aligned(256)));
static struct ahci_cmd_table command_table __attribute__((aligned(128)));
static uint8_t dma_sector[BLOCK_SECTOR_SIZE] __attribute__((aligned(4096)));
static volatile uint8_t *active_port;
static block_device_t ahci_device;
static int controller_ready;

static uint64_t virt_to_phys(const void *pointer) {
  uint64_t address = (uint64_t)(uintptr_t)pointer;
  if (kernel_phys_base && kernel_virt_base && address >= kernel_virt_base)
    return kernel_phys_base + (address - kernel_virt_base);
  if (hhdm_offset && address >= hhdm_offset) return address - hhdm_offset;
  return address;
}

static uint32_t reg_read(volatile uint8_t *base, uint32_t offset) {
  return *(volatile uint32_t *)(base + offset);
}

static void reg_write(volatile uint8_t *base, uint32_t offset, uint32_t value) {
  *(volatile uint32_t *)(base + offset) = value;
}

static int wait_clear(volatile uint8_t *port, uint32_t offset, uint32_t mask) {
  for (uint64_t i = 0; i < AHCI_TIMEOUT; ++i) {
    if ((reg_read(port, offset) & mask) == 0) return 0;
  }
  return -1;
}

static int stop_port(volatile uint8_t *port) {
  uint32_t cmd = reg_read(port, PX_CMD);
  reg_write(port, PX_CMD, cmd & ~CMD_ST);
  if (wait_clear(port, PX_CMD, CMD_CR) != 0) return -1;
  cmd = reg_read(port, PX_CMD);
  reg_write(port, PX_CMD, cmd & ~CMD_FRE);
  if (wait_clear(port, PX_CMD, CMD_FR) != 0) return -1;
  return 0;
}

static int start_port(volatile uint8_t *port) {
  if (wait_clear(port, PX_CMD, CMD_CR | CMD_FR) != 0) return -1;
  uint32_t cmd = reg_read(port, PX_CMD);
  reg_write(port, PX_CMD, cmd | CMD_FRE);
  reg_write(port, PX_CMD, cmd | CMD_FRE | CMD_ST);
  return 0;
}

static int issue_command(uint8_t command, uint64_t lba, uint16_t count,
                         int write_to_disk, int has_data) {
  if (!active_port) return -1;
  if (wait_clear(active_port, PX_TFD, TFD_BSY | TFD_DRQ) != 0) return -1;

  memset(&command_list[0], 0, sizeof(command_list[0]));
  memset(&command_table, 0, sizeof(command_table));
  struct ahci_cmd_header *header = &command_list[0];
  header->flags = 5 | (write_to_disk ? (1U << 6) : 0);
  header->prdt_length = has_data ? 1 : 0;
  header->table_base = (uint32_t)virt_to_phys(&command_table);
  header->table_base_upper = (uint32_t)(virt_to_phys(&command_table) >> 32);

  if (has_data) {
    uint64_t buffer_phys = virt_to_phys(dma_sector);
    command_table.prdt[0].data_base = (uint32_t)buffer_phys;
    command_table.prdt[0].data_base_upper = (uint32_t)(buffer_phys >> 32);
    command_table.prdt[0].byte_count_and_ioc = (BLOCK_SECTOR_SIZE - 1) | (1U << 31);
  }

  struct ahci_fis_reg_h2d *fis =
      (struct ahci_fis_reg_h2d *)command_table.command_fis;
  fis->fis_type = FIS_REG_H2D;
  fis->flags = FIS_CMD;
  fis->command = command;
  fis->device = 1U << 6;
  fis->lba0 = (uint8_t)lba;
  fis->lba1 = (uint8_t)(lba >> 8);
  fis->lba2 = (uint8_t)(lba >> 16);
  fis->lba3 = (uint8_t)(lba >> 24);
  fis->lba4 = (uint8_t)(lba >> 32);
  fis->lba5 = (uint8_t)(lba >> 40);
  fis->count_low = (uint8_t)count;
  fis->count_high = (uint8_t)(count >> 8);

  reg_write(active_port, PX_IS, 0xFFFFFFFFU);
  reg_write(active_port, PX_SERR, 0xFFFFFFFFU);
  __sync_synchronize();
  reg_write(active_port, PX_CI, reg_read(active_port, PX_CI) | 1U);

  for (uint64_t i = 0; i < AHCI_TIMEOUT; ++i) {
    if ((reg_read(active_port, PX_CI) & 1U) == 0) {
      if (reg_read(active_port, PX_TFD) & TFD_ERR) return -1;
      return 0;
    }
  }
  return -1;
}

static int ahci_transfer(block_device_t *device, uint64_t lba,
                         uint32_t sector_count, void *buffer, int write) {
  if (!device || device != &ahci_device || !buffer || sector_count == 0 ||
      lba >= device->sector_count ||
      (uint64_t)sector_count > device->sector_count - lba)
    return -1;

  uint8_t *bytes = (uint8_t *)buffer;
  for (uint32_t i = 0; i < sector_count; ++i) {
    if (write) memcpy(dma_sector, bytes + (size_t)i * BLOCK_SECTOR_SIZE,
                      BLOCK_SECTOR_SIZE);
    if (issue_command(write ? ATA_WRITE_DMA_EXT : ATA_READ_DMA_EXT,
                      lba + i, 1, write, 1) != 0)
      return -1;
    if (!write) memcpy(bytes + (size_t)i * BLOCK_SECTOR_SIZE, dma_sector,
                       BLOCK_SECTOR_SIZE);
  }
  return 0;
}

static int ahci_read(block_device_t *device, uint64_t lba, uint32_t count,
                     void *buffer) {
  return ahci_transfer(device, lba, count, buffer, 0);
}

static int ahci_write(block_device_t *device, uint64_t lba, uint32_t count,
                      void *buffer) {
  return ahci_transfer(device, lba, count, buffer, 1);
}

static int ahci_flush(block_device_t *device) {
  if (!device || device != &ahci_device) return -1;
  return issue_command(ATA_FLUSH_CACHE_EXT, 0, 0, 0, 0);
}

static int identify_disk(uint64_t *sector_count) {
  memset(dma_sector, 0, sizeof(dma_sector));
  if (issue_command(ATA_IDENTIFY, 0, 0, 0, 1) != 0) return -1;
  const uint16_t *words = (const uint16_t *)dma_sector;
  if (words[83] & (1U << 10)) {
    *sector_count = (uint64_t)words[100] | ((uint64_t)words[101] << 16) |
                    ((uint64_t)words[102] << 32) | ((uint64_t)words[103] << 48);
  } else {
    *sector_count = (uint64_t)words[60] | ((uint64_t)words[61] << 16);
  }
  return *sector_count ? 0 : -1;
}

static int configure_port(volatile uint8_t *port, unsigned int port_number) {
  uint32_t status = reg_read(port, PX_SSTS);
  if ((status & 0x0FU) != 3U || ((status >> 8) & 0x0FU) != 1U ||
      reg_read(port, PX_SIG) != SIG_ATA)
    return -1;
  if (stop_port(port) != 0) return -1;

  memset(command_list, 0, sizeof(command_list));
  memset(received_fis, 0, sizeof(received_fis));
  memset(&command_table, 0, sizeof(command_table));
  uint64_t clb = virt_to_phys(command_list);
  uint64_t fb = virt_to_phys(received_fis);
  reg_write(port, PX_CLB, (uint32_t)clb);
  reg_write(port, PX_CLB + 4, (uint32_t)(clb >> 32));
  reg_write(port, PX_FB, (uint32_t)fb);
  reg_write(port, PX_FB + 4, (uint32_t)(fb >> 32));
  active_port = port;
  if (start_port(port) != 0) return -1;

  uint64_t sectors = 0;
  if (identify_disk(&sectors) != 0) {
    active_port = NULL;
    return -1;
  }

  memset(&ahci_device, 0, sizeof(ahci_device));
  ahci_device.name[0] = 's';
  ahci_device.name[1] = 'd';
  ahci_device.name[2] = 'a';
  ahci_device.sector_size = BLOCK_SECTOR_SIZE;
  ahci_device.sector_count = sectors;
  ahci_device.private_data = (void *)(uintptr_t)port_number;
  ahci_device.read = ahci_read;
  ahci_device.write = ahci_write;
  ahci_device.flush = ahci_flush;
  if (block_register(&ahci_device) != 0) {
    active_port = NULL;
    return -1;
  }
  return 0;
}

int ahci_init(void) {
  pci_device_info_t controllers[4];
  int found = pci_scan_class(AHCI_PCI_CLASS, AHCI_PCI_SUBCLASS,
                             AHCI_PCI_PROGIF, controllers, 4);
  if (found <= 0) {
    serial_puts("AHCI: no PCI controller found\n");
    return -1;
  }

  for (int c = 0; c < found && c < 4; ++c) {
    uint32_t raw_bar = controllers[c].bar[AHCI_ABAR_BAR];
    if (raw_bar == 0 || raw_bar == 0xFFFFFFFFU || (raw_bar & 1U)) continue;
    uint64_t abar_phys = raw_bar & ~0x0FU;

    uint32_t command_status = pci_read32(controllers[c].bus,
                                         controllers[c].device,
                                         controllers[c].function, 0x04);
    pci_write32(controllers[c].bus, controllers[c].device,
                controllers[c].function, 0x04,
                (command_status & 0xFFFFU) | 0x0006U);

    uint64_t abar_virt = mmio_map_range(abar_phys, 0x1100);
    if (!abar_virt) continue;
    volatile uint8_t *hba = (volatile uint8_t *)(uintptr_t)abar_virt;
    reg_write(hba, HBA_GHC, reg_read(hba, HBA_GHC) | (1U << 31));
    uint32_t implemented = reg_read(hba, HBA_PI);

    for (unsigned int i = 0; i < AHCI_MAX_PORTS; ++i) {
      if ((implemented & (1U << i)) == 0) continue;
      volatile uint8_t *port = hba + HBA_PORTS + i * PORT_STRIDE;
      if (configure_port(port, i) == 0) {
        controller_ready = 1;
        serial_puts("AHCI: SATA disk registered as ");
        serial_puts(ahci_device.name);
        serial_puts(" sectors=");
        serial_puthex(ahci_device.sector_count);
        serial_puts("\n");
        return 0;
      }
    }
  }
  serial_puts("AHCI: controller found, no usable SATA disk\n");
  return -1;
}
