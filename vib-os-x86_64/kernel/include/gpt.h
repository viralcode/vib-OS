/*
 * GPT partition-table reader and single-ESP creator.
 * Partition indices are zero-based GPT entry indices; LBA endpoints are inclusive.
 */
#ifndef VIBOS_GPT_H
#define VIBOS_GPT_H

#include "types.h"
#include "blockdev.h"

typedef struct {
  uint64_t first_lba;
  uint64_t last_lba;
  uint8_t type_guid[16];
  uint8_t unique_guid[16];
  char name[37];
} gpt_partition_t;

/* Success and error codes returned by all public GPT operations. */
#define GPT_OK                 0
#define GPT_ERR_INVALID       -1  /* Invalid argument or device descriptor. */
#define GPT_ERR_SECTOR_SIZE   -2  /* GPT operations require 512-byte sectors. */
#define GPT_ERR_IO            -3  /* Block-device read, write, or flush failed. */
#define GPT_ERR_MBR           -4  /* Missing or invalid protective MBR. */
#define GPT_ERR_CORRUPT       -5  /* No valid GPT header and partition array. */
#define GPT_ERR_INDEX         -6  /* Requested entry index is out of range. */
#define GPT_ERR_NOT_FOUND     -7  /* No matching allocated partition exists. */
#define GPT_ERR_TOO_SMALL     -8  /* Device cannot hold the requested GPT/ESP. */
#define GPT_ERR_OVERFLOW      -9  /* GPT geometry arithmetic would overflow. */

/* Read one allocated GPT entry by zero-based entry index. */
int gpt_read_partition(blockdev_t *device, uint32_t index,
                       gpt_partition_t *out);

/* Find and return the first EFI System Partition. */
int gpt_find_esp(blockdev_t *device, gpt_partition_t *out);

/* Destructively replace the disk contents with a protective MBR and GPT
 * containing one aligned EFI System Partition. Never call without explicit
 * authorization to erase/repartition the target device.
 */
int gpt_create_single_esp(blockdev_t *device, gpt_partition_t *out);

#endif /* VIBOS_GPT_H */
