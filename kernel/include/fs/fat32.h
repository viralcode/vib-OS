/*
 * vib-OS - FAT32 Filesystem Header
 */

#ifndef _FS_FAT32_H
#define _FS_FAT32_H

#include "fs/vfs.h"
#include "types.h"

/* FAT32 Boot Sector / BPB */
struct fat32_bpb {
  uint8_t jmp_boot[3];
  uint8_t oem_name[8];
  uint16_t bytes_per_sector;
  uint8_t sectors_per_cluster;
  uint16_t reserved_sectors;
  uint8_t num_fats;
  uint16_t root_entry_count;
  uint16_t total_sectors_16;
  uint8_t media;
  uint16_t fat_size_16;
  uint16_t sectors_per_track;
  uint16_t num_heads;
  uint32_t hidden_sectors;
  uint32_t total_sectors_32;

  /* FAT32 Extended */
  uint32_t fat_size_32;
  uint16_t ext_flags;
  uint16_t fs_version;
  uint32_t root_cluster;
  uint16_t fs_info;
  uint16_t backup_boot_sector;
  uint8_t reserved[12];
  uint8_t drive_number;
  uint8_t reserved1;
  uint8_t boot_signature;
  uint32_t vol_id;
  uint8_t vol_label[11];
  uint8_t fs_type[8];
} __attribute__((packed));

/* FAT32 Directory Entry */
struct fat32_dir_entry {
  uint8_t name[11];
  uint8_t attr;
  uint8_t nt_res;
  uint8_t create_time_tenth;
  uint16_t create_time;
  uint16_t create_date;
  uint16_t last_access_date;
  uint16_t first_cluster_hi;
  uint16_t write_time;
  uint16_t write_date;
  uint16_t first_cluster_lo;
  uint32_t file_size;
} __attribute__((packed));

/* FAT32 LFN Entry */
struct fat32_lfn_entry {
  uint8_t order;
  uint16_t name1[5];
  uint8_t attr;
  uint8_t type;
  uint8_t checksum;
  uint16_t name2[6];
  uint16_t cluster_lo;
  uint16_t name3[2];
} __attribute__((packed));

/* Attributes */
#define ATTR_READ_ONLY 0x01
#define ATTR_HIDDEN 0x02
#define ATTR_SYSTEM 0x04
#define ATTR_VOLUME_ID 0x08
#define ATTR_DIRECTORY 0x10
#define ATTR_ARCHIVE 0x20
#define ATTR_LONG_NAME                                                         \
  (ATTR_READ_ONLY | ATTR_HIDDEN | ATTR_SYSTEM | ATTR_VOLUME_ID)

/* Constants */
#define FAT32_EOC 0x0FFFFFF8
#define FAT32_BAD 0x0FFFFFF7
#define FAT32_FREE 0x00000000

/* Public Interface */
extern struct file_system_type fat32_fs_type;

#endif /* _FS_FAT32_H */
