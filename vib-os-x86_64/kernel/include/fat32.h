/*
 * Minimal freestanding FAT32 filesystem support.
 *
 * Path components are UTF-8 and support VFAT long filenames (up to 255 UTF-16
 * code units, including surrogate pairs) as well as case-insensitive ASCII
 * 8.3 names. Unicode comparison folds ASCII letters only; locale-specific and
 * full Unicode case folding/normalization are not implemented. Malformed UTF-8,
 * FAT-forbidden component characters, and paths over the implementation's
 * fixed bounds are rejected. Timestamps, permissions, and concurrent access
 * are not supported. Formatting uses 512-byte sectors, two FATs, and
 * power-of-two clusters up to 32 KiB; only legal FAT32 volumes are permitted.
 */
#ifndef VIBOS_FAT32_H
#define VIBOS_FAT32_H

#include "types.h"
#include "blockdev.h"

/* Negative return values identify FAT32/API errors; device failures map to IO. */
#define FAT32_OK                 0
#define FAT32_ERR_INVALID       -1
#define FAT32_ERR_IO            -2
#define FAT32_ERR_NOT_FOUND     -3
#define FAT32_ERR_EXISTS        -4
#define FAT32_ERR_NO_SPACE      -5
#define FAT32_ERR_NOT_DIR       -6
#define FAT32_ERR_IS_DIR        -7
#define FAT32_ERR_CORRUPT       -8
#define FAT32_ERR_TOO_SMALL     -9
#define FAT32_ERR_UNSUPPORTED  -10
#define FAT32_ERR_BUFFER       -11

#define FAT32_SECTOR_SIZE 512U

typedef struct fat32_volume {
  blockdev_t *device;
  uint64_t partition_start_lba;
  uint64_t partition_sector_count;
  uint32_t total_sectors;
  uint32_t sectors_per_cluster;
  uint32_t reserved_sector_count;
  uint32_t fat_count;
  uint32_t sectors_per_fat;
  uint32_t root_cluster;
  uint32_t fsinfo_sector;
  uint32_t backup_boot_sector;
  uint32_t first_data_sector; /* Volume-relative sector number. */
  uint32_t cluster_count;
  bool mounted;
} fat32_volume_t;

/* Format the selected partition. Never clears the data area except root cluster. */
int fat32_format(blockdev_t *device, uint64_t partition_start_lba,
                 uint64_t partition_sector_count, const char *label);
int fat32_mount(blockdev_t *device, uint64_t partition_start_lba,
                uint64_t partition_sector_count, fat32_volume_t *volume);
int fat32_mkdir(fat32_volume_t *volume, const char *path);
int fat32_write_file(fat32_volume_t *volume, const char *path,
                     const void *data, uint32_t length);
int fat32_read_file(fat32_volume_t *volume, const char *path, void *out,
                    uint32_t capacity, uint32_t *out_length);

#endif /* VIBOS_FAT32_H */
