/*
 * Minimal block-device registry and bounds-checking wrappers.
 */
#include "../include/blockdev.h"
#include "../include/string.h"

#define BLOCKDEV_EINVAL (-1)
#define BLOCKDEV_ENOSPC (-2)
#define BLOCKDEV_ENODEV (-3)
#define BLOCKDEV_EIO (-4)

static blockdev_t *registered[BLOCKDEV_MAX_DEVICES];
static uint32_t registered_count;

int blockdev_register(blockdev_t *device) {
  if (!device || !device->read || !device->sector_count ||
      !device->sector_size) {
    return BLOCKDEV_EINVAL;
  }
  for (uint32_t i = 0; i < registered_count; i++) {
    if (registered[i] == device) return (int)i;
  }
  if (registered_count >= BLOCKDEV_MAX_DEVICES) return BLOCKDEV_ENOSPC;
  registered[registered_count] = device;
  return (int)registered_count++;
}

uint32_t blockdev_count(void) { return registered_count; }

blockdev_t *blockdev_get(uint32_t index) {
  if (index >= registered_count) return NULL;
  return registered[index];
}

static int valid_range(blockdev_t *device, uint64_t lba,
                       uint32_t count, const void *buffer) {
  if (!device || !buffer || !count || !device->read ||
      !device->sector_size || lba >= device->sector_count ||
      (uint64_t)count > device->sector_count - lba) {
    return 0;
  }
  return 1;
}

int blockdev_read(blockdev_t *device, uint64_t lba,
                  uint32_t sector_count, void *buffer) {
  if (!valid_range(device, lba, sector_count, buffer)) return BLOCKDEV_EINVAL;
  return device->read(device, lba, sector_count, buffer);
}

int blockdev_write(blockdev_t *device, uint64_t lba,
                   uint32_t sector_count, const void *buffer) {
  if (!valid_range(device, lba, sector_count, buffer) || !device->write)
    return BLOCKDEV_EINVAL;
  return device->write(device, lba, sector_count, (void *)buffer);
}

int blockdev_flush(blockdev_t *device) {
  if (!device) return BLOCKDEV_EINVAL;
  if (!device->flush) return 0;
  return device->flush(device);
}
