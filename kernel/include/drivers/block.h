/*
 * Vib-OS Kernel - Block Device Interface
 *
 * Abstraction layer for block devices (virtio-blk, NVMe, etc.)
 * Provides a unified API for filesystem drivers.
 */

#ifndef _DRIVERS_BLOCK_H
#define _DRIVERS_BLOCK_H

#include "types.h"

/* ===================================================================== */
/* Constants */
/* ===================================================================== */

#define BLOCK_SECTOR_SIZE       512
#define BLOCK_MAX_DEVICES       8
#define BLOCK_NAME_MAX          32

/* ===================================================================== */
/* Block device structure */
/* ===================================================================== */

struct block_device;

/* Block device operations */
struct block_ops {
    /* Read sectors from device */
    int (*read)(struct block_device *dev, uint64_t sector, 
                uint32_t count, void *buffer);

    /* Write sectors to device */
    int (*write)(struct block_device *dev, uint64_t sector,
                 uint32_t count, const void *buffer);

    /* Flush cached writes */
    int (*flush)(struct block_device *dev);

    /* Get device info */
    int (*get_info)(struct block_device *dev, void *info);
};

/* Block device */
struct block_device {
    char name[BLOCK_NAME_MAX];          /* Device name (e.g., "vda") */
    uint64_t capacity;                  /* Total sectors */
    uint32_t sector_size;               /* Bytes per sector */
    uint32_t flags;                     /* Device flags */
    const struct block_ops *ops;        /* Operations */
    void *private;                      /* Driver-specific data */
};

/* Device flags */
#define BLOCK_FLAG_READONLY     (1 << 0)
#define BLOCK_FLAG_REMOVABLE    (1 << 1)

/* ===================================================================== */
/* Block device registration */
/* ===================================================================== */

/**
 * block_register - Register a block device
 * @dev: Block device structure to register
 * Returns: 0 on success, negative error code on failure
 */
int block_register(struct block_device *dev);

/**
 * block_unregister - Unregister a block device
 * @dev: Block device to unregister
 */
void block_unregister(struct block_device *dev);

/**
 * block_get_device - Get a block device by name
 * @name: Device name
 * Returns: Pointer to device, or NULL if not found
 */
struct block_device *block_get_device(const char *name);

/**
 * block_get_device_by_index - Get a block device by index
 * @index: Device index (0-based)
 * Returns: Pointer to device, or NULL if invalid index
 */
struct block_device *block_get_device_by_index(int index);

/* ===================================================================== */
/* Block I/O functions */
/* ===================================================================== */

/**
 * block_read - Read sectors from a block device
 * @dev: Block device
 * @sector: Starting sector number
 * @count: Number of sectors to read
 * @buffer: Destination buffer (must be count * sector_size bytes)
 * Returns: 0 on success, negative error code on failure
 */
int block_read(struct block_device *dev, uint64_t sector,
               uint32_t count, void *buffer);

/**
 * block_write - Write sectors to a block device
 * @dev: Block device
 * @sector: Starting sector number
 * @count: Number of sectors to write
 * @buffer: Source buffer (must be count * sector_size bytes)
 * Returns: 0 on success, negative error code on failure
 */
int block_write(struct block_device *dev, uint64_t sector,
                uint32_t count, const void *buffer);

/**
 * block_flush - Flush cached data to device
 * @dev: Block device
 * Returns: 0 on success, negative error code on failure
 */
int block_flush(struct block_device *dev);

/* ===================================================================== */
/* VirtIO Block Driver API */
/* ===================================================================== */

/* Initialize the VirtIO block driver */
int virtio_blk_init(void);

/* Read sectors */
int virtio_blk_read(uint64_t sector, uint32_t count, void *buffer);

/* Write sectors */
int virtio_blk_write(uint64_t sector, uint32_t count, const void *buffer);

/* Flush pending writes */
int virtio_blk_flush(void);

/* Get disk capacity in sectors */
uint64_t virtio_blk_get_capacity(void);

/* Get sector size in bytes */
uint32_t virtio_blk_get_sector_size(void);

/* Check if device is ready */
bool virtio_blk_is_ready(void);

#endif /* _DRIVERS_BLOCK_H */
