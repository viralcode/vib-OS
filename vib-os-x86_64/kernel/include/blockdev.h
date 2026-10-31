/*
 * Vib-OS x86_64 block-device interface.
 * All locations and counts are expressed in device logical sectors.
 */
#ifndef VIBOS_BLOCKDEV_H
#define VIBOS_BLOCKDEV_H

#include "types.h"

#define BLOCKDEV_MAX_DEVICES 8
#define BLOCKDEV_NAME_LEN 16
#define BLOCKDEV_MODEL_LEN 41

typedef struct blockdev blockdev_t;

typedef int (*blockdev_io_fn)(blockdev_t *device, uint64_t lba,
                              uint32_t sector_count, void *buffer);
typedef int (*blockdev_flush_fn)(blockdev_t *device);

struct blockdev {
  char name[BLOCKDEV_NAME_LEN];
  char model[BLOCKDEV_MODEL_LEN];
  uint64_t sector_count;
  uint32_t sector_size;
  void *context;
  blockdev_io_fn read;
  blockdev_io_fn write;
  blockdev_flush_fn flush;
};

/* Register a stable device descriptor; returns its index or a negative error. */
int blockdev_register(blockdev_t *device);
uint32_t blockdev_count(void);
blockdev_t *blockdev_get(uint32_t index);

/* Range-checked wrappers. Return 0 on success, negative on failure. */
int blockdev_read(blockdev_t *device, uint64_t lba,
                  uint32_t sector_count, void *buffer);
int blockdev_write(blockdev_t *device, uint64_t lba,
                   uint32_t sector_count, const void *buffer);
int blockdev_flush(blockdev_t *device);

#endif
