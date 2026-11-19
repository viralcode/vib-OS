#include "../include/block.h"
#include "../include/string.h"

static block_device_t *devices[BLOCK_MAX_DEVICES];
static size_t device_count;

void block_init(void) {
  memset(devices, 0, sizeof(devices));
  device_count = 0;
}

int block_register(block_device_t *device) {
  if (!device || !device->name[0] || device->sector_size != BLOCK_SECTOR_SIZE ||
      device->sector_count == 0 || !device->read) {
    return -1;
  }
  for (size_t i = 0; i < device_count; ++i) {
    if (strcmp(devices[i]->name, device->name) == 0) return -2;
  }
  if (device_count >= BLOCK_MAX_DEVICES) return -3;
  devices[device_count++] = device;
  return 0;
}

size_t block_device_count(void) { return device_count; }

block_device_t *block_device_at(size_t index) {
  return index < device_count ? devices[index] : NULL;
}

static int valid_range(block_device_t *device, uint64_t lba, uint32_t sectors,
                       const void *buffer) {
  if (!device || !buffer || sectors == 0 || device->sector_size != BLOCK_SECTOR_SIZE)
    return 0;
  if (lba >= device->sector_count || (uint64_t)sectors > device->sector_count - lba)
    return 0;
  return 1;
}

int block_read(block_device_t *device, uint64_t lba, uint32_t sectors,
               void *buffer) {
  if (!valid_range(device, lba, sectors, buffer) || !device->read) return -1;
  return device->read(device, lba, sectors, buffer);
}

int block_write(block_device_t *device, uint64_t lba, uint32_t sectors,
                void *buffer) {
  if (!valid_range(device, lba, sectors, buffer) || !device->write) return -1;
  return device->write(device, lba, sectors, buffer);
}

int block_flush(block_device_t *device) {
  if (!device || !device->flush) return -1;
  return device->flush(device);
}
