#ifndef VIBOS_BLOCK_H
#define VIBOS_BLOCK_H

#include "types.h"

#define BLOCK_SECTOR_SIZE 512U
#define BLOCK_MAX_DEVICES 8

typedef struct block_device block_device_t;
typedef int (*block_transfer_fn)(block_device_t *device, uint64_t lba,
                                uint32_t sector_count, void *buffer);
typedef int (*block_flush_fn)(block_device_t *device);

struct block_device {
  char name[16];
  uint32_t sector_size;
  uint64_t sector_count;
  void *private_data;
  block_transfer_fn read;
  block_transfer_fn write;
  block_flush_fn flush;
};

void block_init(void);
int block_register(block_device_t *device);
size_t block_device_count(void);
block_device_t *block_device_at(size_t index);
int block_read(block_device_t *device, uint64_t lba, uint32_t sectors,
               void *buffer);
int block_write(block_device_t *device, uint64_t lba, uint32_t sectors,
                void *buffer);
int block_flush(block_device_t *device);

#endif
