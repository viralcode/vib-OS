#include "../include/fat32.h"

#define FAT32_MIN_CLUSTERS 65525U
#define FAT32_MAX_CLUSTER  0x0FFFFFF5U
#define FAT32_EOC          0x0FFFFFFFU
#define FAT32_EOC_MIN      0x0FFFFFF8U
#define FAT32_BAD_CLUSTER   0x0FFFFFF7U
#define FAT32_ATTR_DIR     0x10U
#define FAT32_ATTR_VOLUME  0x08U
#define FAT32_ATTR_LFN     0x0FU
#define FAT32_ZERO_CHUNK_SECTORS 128U

/* Formatter scratch area: keep the 64 KiB zero buffer off the kernel stack. */
static uint8_t fat32_zero_buffer[FAT32_ZERO_CHUNK_SECTORS * FAT32_SECTOR_SIZE];

static uint16_t rd16(const uint8_t *p) {
  return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

static uint32_t rd32(const uint8_t *p) {
  return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
         ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static void wr16(uint8_t *p, uint16_t v) {
  p[0] = (uint8_t)v;
  p[1] = (uint8_t)(v >> 8);
}

static void wr32(uint8_t *p, uint32_t v) {
  p[0] = (uint8_t)v;
  p[1] = (uint8_t)(v >> 8);
  p[2] = (uint8_t)(v >> 16);
  p[3] = (uint8_t)(v >> 24);
}

static void bytes_zero(void *dst, uint32_t count) {
  uint8_t *p = (uint8_t *)dst;
  uint32_t i;
  for (i = 0; i < count; ++i) p[i] = 0;
}

static void bytes_copy(void *dst, const void *src, uint32_t count) {
  uint8_t *d = (uint8_t *)dst;
  const uint8_t *s = (const uint8_t *)src;
  uint32_t i;
  for (i = 0; i < count; ++i) d[i] = s[i];
}

static int valid_device_range(blockdev_t *device, uint64_t start,
                              uint64_t count) {
  if (!device || !device->read || !device->write || !count ||
      device->sector_size != FAT32_SECTOR_SIZE)
    return FAT32_ERR_INVALID;
  if (start > device->sector_count || count > device->sector_count - start)
    return FAT32_ERR_INVALID;
  return FAT32_OK;
}

static int raw_read(blockdev_t *device, uint64_t lba, void *buffer) {
  return blockdev_read(device, lba, 1, buffer) == 0 ? FAT32_OK : FAT32_ERR_IO;
}

static int raw_write(blockdev_t *device, uint64_t lba, const void *buffer) {
  return blockdev_write(device, lba, 1, buffer) == 0 ? FAT32_OK : FAT32_ERR_IO;
}

static int volume_read(const fat32_volume_t *v, uint64_t rel, void *buffer) {
  if (!v || !v->mounted || !v->device || rel >= v->total_sectors ||
      rel >= v->partition_sector_count ||
      v->partition_start_lba > v->device->sector_count ||
      rel > v->device->sector_count - v->partition_start_lba)
    return FAT32_ERR_INVALID;
  return raw_read(v->device, v->partition_start_lba + rel, buffer);
}

static int volume_write(const fat32_volume_t *v, uint64_t rel,
                        const void *buffer) {
  if (!v || !v->mounted || !v->device || rel >= v->total_sectors ||
      rel >= v->partition_sector_count ||
      v->partition_start_lba > v->device->sector_count ||
      rel > v->device->sector_count - v->partition_start_lba)
    return FAT32_ERR_INVALID;
  return raw_write(v->device, v->partition_start_lba + rel, buffer);
}

static int volume_flush(const fat32_volume_t *v) {
  if (!v || !v->mounted || !v->device) return FAT32_ERR_INVALID;
  return blockdev_flush(v->device) == 0 ? FAT32_OK : FAT32_ERR_IO;
}

static bool is_power_of_two(uint32_t n) {
  return n != 0 && (n & (n - 1U)) == 0;
}

static int make_label(const char *label, uint8_t out[11]) {
  uint32_t i = 0;
  uint32_t n = 0;
  static const char default_label[] = "VIBOS";
  if (!label) label = default_label;
  while (label[n]) {
    unsigned char c = (unsigned char)label[n];
    if (n >= 11 || c < 0x21 || c > 0x7E || c == '"' || c == '*' ||
        c == '+' || c == ',' || c == '.' || c == '/' || c == ':' ||
        c == ';' || c == '<' || c == '=' || c == '>' || c == '?' ||
        c == '[' || c == '\\' || c == ']' || c == '|')
      return FAT32_ERR_INVALID;
    ++n;
  }
  for (i = 0; i < 11; ++i) out[i] = (uint8_t)' ';
  for (i = 0; i < n; ++i) {
    unsigned char c = (unsigned char)label[i];
    out[i] = (uint8_t)((c >= 'a' && c <= 'z') ? c - ('a' - 'A') : c);
  }
  return FAT32_OK;
}

static void set_fsinfo(uint8_t sector[FAT32_SECTOR_SIZE]) {
  bytes_zero(sector, FAT32_SECTOR_SIZE);
  wr32(sector + 0, 0x41615252U);
  wr32(sector + 484, 0x61417272U);
  /* Unknown is the defined sentinel; writes need not maintain FSInfo hints. */
  wr32(sector + 488, 0xFFFFFFFFU);
  wr32(sector + 492, 0xFFFFFFFFU);
  wr32(sector + 508, 0xAA550000U);
}

int fat32_format(blockdev_t *device, uint64_t partition_start_lba,
                 uint64_t partition_sector_count, const char *label) {
  uint8_t sector[FAT32_SECTOR_SIZE];
  uint8_t volume_label[11];
  uint32_t sectors_per_fat = 1;
  uint32_t sectors_per_cluster = 64;
  uint32_t clusters = 0;
  uint32_t required_fat;
  uint32_t i;
  uint32_t copy;
  uint64_t fat_start;
  uint64_t data_start;
  int status;

  status = valid_device_range(device, partition_start_lba,
                              partition_sector_count);
  if (status != FAT32_OK) return status;
  if (partition_start_lba > 0xFFFFFFFFULL ||
      partition_sector_count > 0xFFFFFFFFULL)
    return FAT32_ERR_UNSUPPORTED;
  if (partition_sector_count <= 32U + 2U)
    return FAT32_ERR_TOO_SMALL;
  status = make_label(label, volume_label);
  if (status != FAT32_OK) return status;

  /* Prefer standard 32 KiB clusters, reducing them for small FAT32 volumes. */
  for (;;) {
    sectors_per_fat = 1;
    for (i = 0; i < 32; ++i) {
      uint64_t overhead = 32ULL + 2ULL * sectors_per_fat;
      uint64_t data_sectors;
      if (overhead >= partition_sector_count) return FAT32_ERR_TOO_SMALL;
      data_sectors = partition_sector_count - overhead;
      clusters = (uint32_t)(data_sectors / sectors_per_cluster);
      required_fat = (uint32_t)((((uint64_t)clusters + 2ULL) * 4ULL + 511ULL) /
                                FAT32_SECTOR_SIZE);
      if (required_fat == sectors_per_fat) break;
      sectors_per_fat = required_fat;
    }
    if (i == 32) return FAT32_ERR_INVALID;
    if (clusters < FAT32_MIN_CLUSTERS) {
      if (sectors_per_cluster == 1) return FAT32_ERR_TOO_SMALL;
      sectors_per_cluster >>= 1;
      continue;
    }
    if (clusters > FAT32_MAX_CLUSTER) {
      if (sectors_per_cluster >= 128) return FAT32_ERR_UNSUPPORTED;
      sectors_per_cluster <<= 1;
      continue;
    }
    break;
  }

  /* Zero only the two FAT regions, using bounded multi-sector writes. */
  bytes_zero(fat32_zero_buffer, sizeof(fat32_zero_buffer));
  fat_start = 32ULL;
  for (copy = 0; copy < 2; ++copy) {
    for (i = 0; i < sectors_per_fat;) {
      uint32_t count = sectors_per_fat - i;
      if (count > FAT32_ZERO_CHUNK_SECTORS)
        count = FAT32_ZERO_CHUNK_SECTORS;
      if (blockdev_write(device, partition_start_lba + fat_start +
                         (uint64_t)copy * sectors_per_fat + i,
                         count, fat32_zero_buffer) != 0)
        return FAT32_ERR_IO;
      i += count;
    }
  }

  /* The reserved entries and root-directory cluster are the only FAT entries
   * initialized; the rest of the data region is deliberately left untouched. */
  status = raw_read(device, partition_start_lba + 32ULL, sector);
  if (status != FAT32_OK) return status;
  wr32(sector + 0, 0x0FFFFFF8U);
  wr32(sector + 4, 0xFFFFFFFFU);
  wr32(sector + 8, FAT32_EOC);
  status = raw_write(device, partition_start_lba + 32ULL, sector);
  if (status != FAT32_OK) return status;
  status = raw_write(device, partition_start_lba + 32ULL + sectors_per_fat,
                     sector);
  if (status != FAT32_OK) return status;

  /* Construct a standards-compatible FAT32 BPB and boot record. */
  bytes_zero(sector, FAT32_SECTOR_SIZE);
  sector[0] = 0xEB; sector[1] = 0x58; sector[2] = 0x90;
  bytes_copy(sector + 3, "VIBOS   ", 8);
  wr16(sector + 11, FAT32_SECTOR_SIZE);
  sector[13] = (uint8_t)sectors_per_cluster;
  wr16(sector + 14, 32);                  /* reserved sectors */
  sector[16] = 2;                          /* two mirrored FATs */
  wr16(sector + 17, 0);                    /* FAT32 has no fixed root */
  wr16(sector + 19, 0);
  sector[21] = 0xF8;
  wr16(sector + 22, 0);
  wr16(sector + 24, 63);
  wr16(sector + 26, 255);
  wr32(sector + 28, (uint32_t)partition_start_lba);
  wr32(sector + 32, (uint32_t)partition_sector_count);
  wr32(sector + 36, sectors_per_fat);
  wr16(sector + 40, 0);
  wr16(sector + 42, 0);
  wr32(sector + 44, 2);
  wr16(sector + 48, 1);
  wr16(sector + 50, 6);
  sector[64] = 0x80;
  sector[66] = 0x29;
  wr32(sector + 67, (uint32_t)(partition_start_lba ^
                                partition_sector_count ^ 0x5649424FU));
  bytes_copy(sector + 71, volume_label, 11);
  bytes_copy(sector + 82, "FAT32   ", 8);
  sector[510] = 0x55;
  sector[511] = 0xAA;
  status = raw_write(device, partition_start_lba, sector);
  if (status != FAT32_OK) return status;
  status = raw_write(device, partition_start_lba + 6ULL, sector);
  if (status != FAT32_OK) return status;

  set_fsinfo(sector);
  status = raw_write(device, partition_start_lba + 1ULL, sector);
  if (status != FAT32_OK) return status;
  status = raw_write(device, partition_start_lba + 7ULL, sector);
  if (status != FAT32_OK) return status;

  /* Initialize root cluster 2 with a volume-label entry and end marker. */
  bytes_zero(sector, FAT32_SECTOR_SIZE);
  bytes_copy(sector, volume_label, 11);
  sector[11] = FAT32_ATTR_VOLUME;
  data_start = 32ULL + 2ULL * sectors_per_fat;
  status = raw_write(device, partition_start_lba + data_start, sector);
  if (status != FAT32_OK) return status;
  if (device->flush && blockdev_flush(device) != 0) return FAT32_ERR_IO;
  return FAT32_OK;
}

int fat32_mount(blockdev_t *device, uint64_t partition_start_lba,
                uint64_t partition_sector_count, fat32_volume_t *volume) {
  uint8_t boot[FAT32_SECTOR_SIZE];
  uint8_t fsinfo[FAT32_SECTOR_SIZE];
  uint32_t bps, spc, reserved, fats, fat16, fat32, total;
  uint32_t root, fsinfo_sector, backup, first_data, clusters;
  uint32_t extflags;
  uint64_t fat_area;
  int status;

  if (!volume) return FAT32_ERR_INVALID;
  volume->mounted = false;
  status = valid_device_range(device, partition_start_lba,
                              partition_sector_count);
  if (status != FAT32_OK) return status;
  if (partition_sector_count > 0xFFFFFFFFULL) return FAT32_ERR_UNSUPPORTED;
  status = raw_read(device, partition_start_lba, boot);
  if (status != FAT32_OK) return status;
  if (boot[510] != 0x55 || boot[511] != 0xAA) return FAT32_ERR_CORRUPT;

  bps = rd16(boot + 11);
  spc = boot[13];
  reserved = rd16(boot + 14);
  fats = boot[16];
  fat16 = rd16(boot + 22);
  total = rd16(boot + 19);
  if (total == 0) total = rd32(boot + 32);
  fat32 = rd32(boot + 36);
  root = rd32(boot + 44) & 0x0FFFFFFFU;
  fsinfo_sector = rd16(boot + 48);
  backup = rd16(boot + 50);
  extflags = rd16(boot + 40);

  if (bps != FAT32_SECTOR_SIZE || !is_power_of_two(spc) || spc > 128 ||
      reserved < 2 || fats == 0 || fats > 2 || fat16 != 0 || fat32 == 0 ||
      rd16(boot + 17) != 0 || rd16(boot + 42) != 0)
    return FAT32_ERR_UNSUPPORTED;
  if (extflags & 0x0080U) return FAT32_ERR_UNSUPPORTED;
  if (total == 0 || total > partition_sector_count ||
      total > 0xFFFFFFFFU || fsinfo_sector >= reserved ||
      backup >= reserved)
    return FAT32_ERR_CORRUPT;
  fat_area = (uint64_t)fats * fat32;
  if ((uint64_t)reserved + fat_area >= total) return FAT32_ERR_CORRUPT;
  first_data = reserved + (uint32_t)fat_area;
  clusters = (total - first_data) / spc;
  if (clusters < FAT32_MIN_CLUSTERS || clusters > FAT32_MAX_CLUSTER ||
      (uint64_t)fat32 * (FAT32_SECTOR_SIZE / 4U) <
          (uint64_t)clusters + 2ULL ||
      root < 2 || root > clusters + 1U)
    return FAT32_ERR_CORRUPT;

  /* Validate the primary FSInfo signatures when present. */
  status = raw_read(device, partition_start_lba + fsinfo_sector, fsinfo);
  if (status != FAT32_OK) return status;
  if (rd32(fsinfo + 0) != 0x41615252U ||
      rd32(fsinfo + 484) != 0x61417272U ||
      rd32(fsinfo + 508) != 0xAA550000U)
    return FAT32_ERR_CORRUPT;

  volume->device = device;
  volume->partition_start_lba = partition_start_lba;
  volume->partition_sector_count = partition_sector_count;
  volume->total_sectors = total;
  volume->sectors_per_cluster = spc;
  volume->reserved_sector_count = reserved;
  volume->fat_count = fats;
  volume->sectors_per_fat = fat32;
  volume->root_cluster = root;
  volume->fsinfo_sector = fsinfo_sector;
  volume->backup_boot_sector = backup;
  volume->first_data_sector = first_data;
  volume->cluster_count = clusters;
  volume->mounted = true;
  return FAT32_OK;
}

static int valid_volume(const fat32_volume_t *v) {
  uint64_t data_start;
  if (!v || !v->mounted || !v->device || v->device->sector_size != 512 ||
      !v->device->read || !v->device->write || v->sectors_per_cluster == 0 ||
      !is_power_of_two(v->sectors_per_cluster) ||
      v->sectors_per_cluster > 128 || v->cluster_count < FAT32_MIN_CLUSTERS ||
      v->cluster_count > FAT32_MAX_CLUSTER ||
      v->fat_count == 0 || v->fat_count > 2 || v->sectors_per_fat == 0)
    return FAT32_ERR_INVALID;
  if (v->partition_start_lba > v->device->sector_count ||
      v->partition_sector_count > v->device->sector_count -
                                  v->partition_start_lba ||
      v->total_sectors > v->partition_sector_count ||
      v->total_sectors == 0)
    return FAT32_ERR_CORRUPT;
  data_start = (uint64_t)v->reserved_sector_count +
               (uint64_t)v->fat_count * v->sectors_per_fat;
  if (v->reserved_sector_count == 0 || data_start >= v->total_sectors ||
      data_start != v->first_data_sector ||
      (v->total_sectors - data_start) / v->sectors_per_cluster !=
          v->cluster_count ||
      v->root_cluster < 2 || v->root_cluster > v->cluster_count + 1U ||
      (uint64_t)v->sectors_per_fat * (FAT32_SECTOR_SIZE / 4U) <
          (uint64_t)v->cluster_count + 2ULL)
    return FAT32_ERR_CORRUPT;
  return FAT32_OK;
}

static int cluster_sector(const fat32_volume_t *v, uint32_t cluster,
                          uint32_t sector_index, uint64_t *rel_out) {
  uint64_t rel;
  if (!v || !rel_out || cluster < 2 || cluster > v->cluster_count + 1U ||
      sector_index >= v->sectors_per_cluster)
    return FAT32_ERR_CORRUPT;
  rel = (uint64_t)v->first_data_sector +
        (uint64_t)(cluster - 2U) * v->sectors_per_cluster + sector_index;
  if (rel >= v->total_sectors || rel >= v->partition_sector_count)
    return FAT32_ERR_CORRUPT;
  *rel_out = rel;
  return FAT32_OK;
}

static int fat_get(const fat32_volume_t *v, uint32_t cluster,
                   uint32_t *value) {
  uint8_t sector[FAT32_SECTOR_SIZE];
  uint64_t byte_offset, rel;
  uint32_t offset;
  int status;
  if (!value || cluster > v->cluster_count + 1U) return FAT32_ERR_CORRUPT;
  byte_offset = (uint64_t)cluster * 4ULL;
  if (byte_offset / FAT32_SECTOR_SIZE >= v->sectors_per_fat)
    return FAT32_ERR_CORRUPT;
  rel = (uint64_t)v->reserved_sector_count +
        byte_offset / FAT32_SECTOR_SIZE;
  offset = (uint32_t)(byte_offset % FAT32_SECTOR_SIZE);
  status = volume_read(v, rel, sector);
  if (status != FAT32_OK) return status;
  *value = rd32(sector + offset) & 0x0FFFFFFFU;
  return FAT32_OK;
}

static int fat_set(const fat32_volume_t *v, uint32_t cluster,
                   uint32_t value) {
  uint8_t sector[FAT32_SECTOR_SIZE];
  uint64_t byte_offset, rel;
  uint32_t offset, copy;
  int status;
  if (cluster > v->cluster_count + 1U || value > 0x0FFFFFFFU)
    return FAT32_ERR_CORRUPT;
  byte_offset = (uint64_t)cluster * 4ULL;
  if (byte_offset / FAT32_SECTOR_SIZE >= v->sectors_per_fat)
    return FAT32_ERR_CORRUPT;
  offset = (uint32_t)(byte_offset % FAT32_SECTOR_SIZE);
  for (copy = 0; copy < v->fat_count; ++copy) {
    rel = (uint64_t)v->reserved_sector_count +
          (uint64_t)copy * v->sectors_per_fat +
          byte_offset / FAT32_SECTOR_SIZE;
    status = volume_read(v, rel, sector);
    if (status != FAT32_OK) return status;
    wr32(sector + offset, (rd32(sector + offset) & 0xF0000000U) | value);
    status = volume_write(v, rel, sector);
    if (status != FAT32_OK) return status;
  }
  return FAT32_OK;
}

static int next_cluster(const fat32_volume_t *v, uint32_t cluster,
                        uint32_t *next, bool *at_end) {
  uint32_t value;
  int status = fat_get(v, cluster, &value);
  if (status != FAT32_OK) return status;
  if (value >= FAT32_EOC_MIN) {
    *at_end = true;
    *next = 0;
    return FAT32_OK;
  }
  if (value == 0 || value == 1 || value == FAT32_BAD_CLUSTER ||
      value < 2 || value > v->cluster_count + 1U)
    return FAT32_ERR_CORRUPT;
  *at_end = false;
  *next = value;
  return FAT32_OK;
}

static int zero_cluster(const fat32_volume_t *v, uint32_t cluster) {
  uint8_t sector[FAT32_SECTOR_SIZE];
  uint32_t i;
  uint64_t rel;
  int status;
  bytes_zero(sector, FAT32_SECTOR_SIZE);
  for (i = 0; i < v->sectors_per_cluster; ++i) {
    status = cluster_sector(v, cluster, i, &rel);
    if (status != FAT32_OK) return status;
    status = volume_write(v, rel, sector);
    if (status != FAT32_OK) return status;
  }
  return FAT32_OK;
}

static int allocate_cluster(const fat32_volume_t *v, uint32_t *hint,
                            uint32_t *out) {
  uint32_t max_cluster = v->cluster_count + 1U;
  uint32_t start = (*hint >= 2 && *hint <= max_cluster) ? *hint : 2U;
  uint32_t pass, candidate, last;
  uint32_t value;
  int status;
  for (pass = 0; pass < 2; ++pass) {
    candidate = pass == 0 ? start : 2U;
    last = pass == 0 ? max_cluster : start - 1U;
    if (last < candidate) continue;
    for (; candidate <= last; ++candidate) {
      status = fat_get(v, candidate, &value);
      if (status != FAT32_OK) return status;
      if (value == 0) {
        status = fat_set(v, candidate, FAT32_EOC);
        if (status != FAT32_OK) return status;
        status = zero_cluster(v, candidate);
        if (status != FAT32_OK) {
          (void)fat_set(v, candidate, 0);
          return status;
        }
        *out = candidate;
        *hint = candidate == max_cluster ? 2U : candidate + 1U;
        return FAT32_OK;
      }
    }
  }
  return FAT32_ERR_NO_SPACE;
}

static int free_chain(const fat32_volume_t *v, uint32_t first) {
  uint32_t cluster = first, next, count = 0;
  bool end;
  int status;
  while (cluster >= 2) {
    if (++count > v->cluster_count) return FAT32_ERR_CORRUPT;
    status = next_cluster(v, cluster, &next, &end);
    if (status != FAT32_OK) return status;
    status = fat_set(v, cluster, 0);
    if (status != FAT32_OK) return status;
    if (end) return FAT32_OK;
    cluster = next;
  }
  return first == 0 ? FAT32_OK : FAT32_ERR_CORRUPT;
}

#define FAT32_MAX_NAME_UNITS 255U
#define FAT32_MAX_LFN_SLOTS 20U
#define FAT32_MAX_PATH_BYTES 4096U
#define FAT32_MAX_PATH_COMPONENTS 128U

typedef struct fat32_name {
  uint16_t units[FAT32_MAX_NAME_UNITS];
  uint16_t length;
  uint8_t short_name[11];
  bool short_valid;
} fat32_name_t;

typedef struct fat32_dir_slot {
  uint64_t sector_rel;
  uint16_t offset;
  uint8_t entry[32];
} fat32_dir_slot_t;

typedef struct fat32_lfn_state {
  uint16_t units[FAT32_MAX_LFN_SLOTS * 13U];
  uint8_t total;
  uint8_t expected;
  uint8_t checksum;
  bool active;
} fat32_lfn_state_t;

static bool short_char_allowed(unsigned char c) {
  if ((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')) return true;
  switch (c) {
    case '$': case '%': case '\'': case '-': case '_': case '@': case '~':
    case '`': case '!': case '(': case ')': case '{': case '}': case '^':
    case '#': case '&': return true;
    default: return false;
  }
}

static int utf8_next(const char **cursor, uint32_t *codepoint) {
  const uint8_t *p = (const uint8_t *)*cursor;
  uint32_t cp;
  uint8_t a = p[0];
  if (a < 0x80U) { cp = a; *cursor += 1; }
  else if (a >= 0xC2U && a <= 0xDFU) {
    if ((p[1] & 0xC0U) != 0x80U) return FAT32_ERR_INVALID;
    cp = ((uint32_t)(a & 0x1FU) << 6) | (p[1] & 0x3FU);
    *cursor += 2;
  } else if (a >= 0xE0U && a <= 0xEFU) {
    if (p[1] == 0 || p[2] == 0 || (p[1] & 0xC0U) != 0x80U ||
        (p[2] & 0xC0U) != 0x80U || (a == 0xE0U && p[1] < 0xA0U) ||
        (a == 0xEDU && p[1] >= 0xA0U)) return FAT32_ERR_INVALID;
    cp = ((uint32_t)(a & 0x0FU) << 12) |
         ((uint32_t)(p[1] & 0x3FU) << 6) | (p[2] & 0x3FU);
    *cursor += 3;
  } else if (a >= 0xF0U && a <= 0xF4U) {
    if (p[1] == 0 || p[2] == 0 || p[3] == 0 ||
        (p[1] & 0xC0U) != 0x80U || (p[2] & 0xC0U) != 0x80U ||
        (p[3] & 0xC0U) != 0x80U || (a == 0xF0U && p[1] < 0x90U) ||
        (a == 0xF4U && p[1] >= 0x90U)) return FAT32_ERR_INVALID;
    cp = ((uint32_t)(a & 7U) << 18) | ((uint32_t)(p[1] & 0x3FU) << 12) |
         ((uint32_t)(p[2] & 0x3FU) << 6) | (p[3] & 0x3FU);
    *cursor += 4;
  } else return FAT32_ERR_INVALID;
  *codepoint = cp;
  return FAT32_OK;
}

static int name_component(const char **cursor, fat32_name_t *name) {
  const char *p = *cursor;
  uint32_t base = 0, ext = 0, i, cp;
  bool in_ext = false, direct = true;
  bytes_zero(name, sizeof(*name));
  for (i = 0; i < 11; ++i) name->short_name[i] = ' ';
  while (*p == '/') ++p;
  if (!*p) { *cursor = p; return 0; }
  while (*p && *p != '/') {
    unsigned char c;
    if (utf8_next(&p, &cp) != FAT32_OK) return FAT32_ERR_INVALID;
    if (cp == 0 || cp < 0x20U || cp == 0x7FU || cp == '"' || cp == '*' ||
        cp == '/' || cp == ':' || cp == '<' || cp == '>' || cp == '?' ||
        cp == '\\' || cp == '|') return FAT32_ERR_INVALID;
    if (cp == '.') {
      if (in_ext || base == 0 || name->length >= FAT32_MAX_NAME_UNITS)
        return FAT32_ERR_INVALID;
      name->units[name->length++] = (uint16_t)'.';
      in_ext = true;
      continue;
    }
    if (cp > 0xFFFFU) {
      if (name->length > FAT32_MAX_NAME_UNITS - 2U) return FAT32_ERR_INVALID;
      cp -= 0x10000U;
      name->units[name->length++] = (uint16_t)(0xD800U + (cp >> 10));
      name->units[name->length++] = (uint16_t)(0xDC00U + (cp & 0x3FFU));
      direct = false;
    } else {
      if (name->length >= FAT32_MAX_NAME_UNITS) return FAT32_ERR_INVALID;
      name->units[name->length++] = (uint16_t)cp;
      if (cp >= 0x80U) direct = false;
    }
    if (cp == ' ') direct = false;
    if (cp < 0x80U) {
      c = (unsigned char)cp;
      if (c >= 'a' && c <= 'z') c = (unsigned char)(c - ('a' - 'A'));
      if (!short_char_allowed(c)) direct = false;
      if (in_ext) {
        if (++ext > 3) direct = false;
        else name->short_name[8U + ext - 1U] = c;
      } else {
        if (++base > 8) direct = false;
        else name->short_name[base - 1U] = c;
      }
    } else {
      if (in_ext) ++ext; else ++base;
    }
  }
  if (!name->length || (name->units[name->length - 1U] == ' ' ||
                        name->units[name->length - 1U] == '.') ||
      (name->length == 1U && name->units[0] == '.') ||
      (name->length == 2U && name->units[0] == '.' && name->units[1] == '.'))
    return FAT32_ERR_INVALID;
  if (in_ext && ext == 0) direct = false;
  while (*p == '/') ++p;
  *cursor = p;
  name->short_valid = direct && base >= 1U && base <= 8U && ext <= 3U;
  return 1;
}

static bool short_name_equal(const uint8_t *a, const uint8_t *b) {
  uint32_t i;
  for (i = 0; i < 11U; ++i) if (a[i] != b[i]) return false;
  return true;
}

static uint16_t fold_unit(uint16_t c) {
  return c >= 'a' && c <= 'z' ? (uint16_t)(c - ('a' - 'A')) : c;
}

static bool unicode_name_equal(const uint16_t *a, uint32_t an,
                               const fat32_name_t *b) {
  uint32_t i;
  if (an != b->length) return false;
  for (i = 0; i < an; ++i)
    if (fold_unit(a[i]) != fold_unit(b->units[i])) return false;
  return true;
}

static uint8_t lfn_checksum(const uint8_t name[11]) {
  uint8_t sum = 0;
  uint32_t i;
  for (i = 0; i < 11U; ++i)
    sum = (uint8_t)(((sum & 1U) ? 0x80U : 0U) + (sum >> 1) + name[i]);
  return sum;
}

static void lfn_reset(fat32_lfn_state_t *state) {
  state->active = false;
  state->total = 0;
  state->expected = 0;
  state->checksum = 0;
}

static void lfn_feed(fat32_lfn_state_t *state, const uint8_t entry[32]) {
  static const uint8_t offsets[13] = {
    1, 3, 5, 7, 9, 14, 16, 18, 20, 22, 24, 28, 30
  };
  uint8_t ord_byte = entry[0], ordinal = ord_byte & 0x1FU;
  uint32_t i, base;
  bool last = (ord_byte & 0x40U) != 0;
  if ((ord_byte & 0xA0U) != 0 || ordinal == 0 || ordinal > FAT32_MAX_LFN_SLOTS ||
      entry[11] != FAT32_ATTR_LFN || entry[12] != 0 ||
      rd16(entry + 26) != 0 || (last && ordinal == 0)) {
    lfn_reset(state);
    return;
  }
  if (last) {
    lfn_reset(state);
    state->active = true;
    state->total = ordinal;
    state->expected = ordinal;
    state->checksum = entry[13];
  }
  if (!state->active || ordinal != state->expected ||
      entry[13] != state->checksum || (last != (ordinal == state->total))) {
    lfn_reset(state);
    return;
  }
  base = (uint32_t)(ordinal - 1U) * 13U;
  for (i = 0; i < 13U; ++i) state->units[base + i] = rd16(entry + offsets[i]);
  --state->expected;
}

static bool lfn_matches(fat32_lfn_state_t *state, const uint8_t short_entry[32],
                        const fat32_name_t *query) {
  uint32_t capacity, length, i;
  bool terminated = false;
  if (!state->active || state->expected != 0 ||
      state->checksum != lfn_checksum(short_entry)) return false;
  capacity = (uint32_t)state->total * 13U;
  length = capacity;
  for (i = 0; i < capacity; ++i) {
    uint16_t c = state->units[i];
    if (!terminated && c == 0) { length = i; terminated = true; continue; }
    if (terminated) { if (c != 0xFFFFU) return false; continue; }
    if (c == 0xFFFFU) return false;
  }
  if (length == 0 || length > FAT32_MAX_NAME_UNITS) return false;
  for (i = 0; i < length; ++i) {
    uint16_t c = state->units[i];
    if (c >= 0xD800U && c <= 0xDBFFU) {
      if (++i >= length || state->units[i] < 0xDC00U || state->units[i] > 0xDFFFU)
        return false;
    } else if (c >= 0xDC00U && c <= 0xDFFFU) return false;
  }
  return unicode_name_equal(state->units, length, query);
}

static uint32_t entry_cluster(const uint8_t entry[32]) {
  return ((uint32_t)rd16(entry + 20) << 16) | rd16(entry + 26);
}

/* Scan a directory as a logical slot stream. LFN state survives sector/cluster
 * edges, but any deleted, volume, or malformed record breaks the sequence. */
static int directory_scan(const fat32_volume_t *v, uint32_t directory,
                          const fat32_name_t *query, const uint8_t alias[11],
                          bool alias_only, fat32_dir_slot_t *found,
                          bool *has_found, uint32_t *tail, uint32_t *end_slot) {
  uint8_t sector[FAT32_SECTOR_SIZE];
  fat32_lfn_state_t lfn;
  uint32_t cluster = directory, next, si, ei, count = 0, slot_index = 0;
  bool end, matched;
  uint64_t rel;
  int status;
  *has_found = false;
  lfn_reset(&lfn);
  while (cluster >= 2) {
    if (++count > v->cluster_count) return FAT32_ERR_CORRUPT;
    for (si = 0; si < v->sectors_per_cluster; ++si) {
      status = cluster_sector(v, cluster, si, &rel);
      if (status != FAT32_OK) return status;
      status = volume_read(v, rel, sector);
      if (status != FAT32_OK) return status;
      for (ei = 0; ei < FAT32_SECTOR_SIZE / 32U; ++ei, ++slot_index) {
        uint8_t *entry = sector + ei * 32U;
        fat32_dir_slot_t current;
        if (entry[0] == 0) {
          *tail = cluster;
          *end_slot = slot_index;
          return FAT32_OK;
        }
        if (entry[11] == FAT32_ATTR_LFN) { lfn_feed(&lfn, entry); continue; }
        if (entry[0] == 0xE5U || (entry[11] & FAT32_ATTR_VOLUME) != 0) {
          lfn_reset(&lfn);
          continue;
        }
        current.sector_rel = rel;
        current.offset = (uint16_t)(ei * 32U);
        bytes_copy(current.entry, entry, 32);
        matched = alias && short_name_equal(entry, alias);
        if (!alias_only && query && !matched)
          matched = lfn_matches(&lfn, entry, query);
        if (matched && !*has_found) { *found = current; *has_found = true; }
        lfn_reset(&lfn);
      }
    }
    status = next_cluster(v, cluster, &next, &end);
    if (status != FAT32_OK) return status;
    if (end) {
      *tail = cluster;
      *end_slot = slot_index;
      return FAT32_OK;
    }
    cluster = next;
  }
  return FAT32_ERR_CORRUPT;
}

static int directory_lookup(const fat32_volume_t *v, uint32_t directory,
                            const fat32_name_t *name, fat32_dir_slot_t *slot) {
  bool found;
  uint32_t tail, end_slot;
  int status = directory_scan(v, directory, name,
      name->short_valid ? name->short_name : NULL, false, slot, &found,
      &tail, &end_slot);
  if (status != FAT32_OK) return status;
  return found ? FAT32_OK : FAT32_ERR_NOT_FOUND;
}

static int alias_exists(const fat32_volume_t *v, uint32_t directory,
                        const uint8_t alias[11], bool *exists) {
  fat32_dir_slot_t found;
  uint32_t tail, end_slot;
  int status = directory_scan(v, directory, NULL, alias, true, &found, exists,
                              &tail, &end_slot);
  return status;
}

static int choose_short_alias(const fat32_volume_t *v, uint32_t directory,
                              const fat32_name_t *name, uint8_t alias[11]) {
  uint8_t base[8], ext[3];
  uint32_t i, j, base_n = 0, ext_n = 0, cp, n, digits, keep;
  bool after_dot = false, exists;
  int status;
  if (name->short_valid) {
    bytes_copy(alias, name->short_name, 11);
    status = alias_exists(v, directory, alias, &exists);
    if (status != FAT32_OK) return status;
    if (!exists) return FAT32_OK;
  }
  for (i = 0; i < 8U; ++i) base[i] = '_';
  for (i = 0; i < 3U; ++i) ext[i] = ' ';
  for (i = 0; i < name->length; ++i) {
    cp = name->units[i];
    if (cp == '.') { after_dot = true; continue; }
    if (cp >= 'a' && cp <= 'z') cp -= 'a' - 'A';
    if (after_dot) {
      if (ext_n < 3U) ext[ext_n++] =
          cp < 0x80U && short_char_allowed((unsigned char)cp) ? (uint8_t)cp : '_';
    } else {
      if (base_n < 8U) base[base_n++] =
          cp < 0x80U && short_char_allowed((unsigned char)cp) ? (uint8_t)cp : '_';
    }
  }
  if (!base_n) base[base_n++] = '_';
  for (n = 1; n <= 999999U; ++n) {
    uint32_t tmp = n;
    digits = 1;
    while (tmp >= 10U) { ++digits; tmp /= 10U; }
    keep = 7U - digits;
    for (i = 0; i < 11U; ++i) alias[i] = ' ';
    for (i = 0; i < base_n && i < keep; ++i) alias[i] = base[i];
    alias[keep] = '~';
    tmp = n;
    for (j = 0; j < digits; ++j) {
      alias[keep + digits - j] = (uint8_t)('0' + tmp % 10U);
      tmp /= 10U;
    }
    for (i = 0; i < ext_n; ++i) alias[8U + i] = ext[i];
    status = alias_exists(v, directory, alias, &exists);
    if (status != FAT32_OK) return status;
    if (!exists) return FAT32_OK;
  }
  return FAT32_ERR_NO_SPACE;
}

static int resolve_parent(const fat32_volume_t *v, const char *path,
                          uint32_t *parent, fat32_name_t *target) {
  const char *cursor = path;
  fat32_name_t current, next_name;
  int result;
  uint32_t directory = v->root_cluster, components = 0;
  fat32_dir_slot_t slot;
  uint32_t bytes = 0;
  if (!path || !parent || !target) return FAT32_ERR_INVALID;
  while (bytes < FAT32_MAX_PATH_BYTES && path[bytes]) ++bytes;
  if (bytes == FAT32_MAX_PATH_BYTES) return FAT32_ERR_INVALID;
  result = name_component(&cursor, &current);
  if (result <= 0) return FAT32_ERR_INVALID;
  ++components;
  for (;;) {
    result = name_component(&cursor, &next_name);
    if (result == 0) {
      *parent = directory;
      *target = current;
      return FAT32_OK;
    }
    if (result < 0) return result;
    if (++components > FAT32_MAX_PATH_COMPONENTS) return FAT32_ERR_INVALID;
    result = directory_lookup(v, directory, &current, &slot);
    if (result != FAT32_OK) return result;
    if ((slot.entry[11] & FAT32_ATTR_DIR) == 0) return FAT32_ERR_NOT_DIR;
    directory = entry_cluster(slot.entry);
    if (directory < 2 || directory > v->cluster_count + 1U)
      return FAT32_ERR_CORRUPT;
    current = next_name;
  }
}

static int write_directory_slot(const fat32_volume_t *v,
                                const fat32_dir_slot_t *slot,
                                const uint8_t entry[32]) {
  uint8_t sector[FAT32_SECTOR_SIZE];
  int status = volume_read(v, slot->sector_rel, sector);
  if (status != FAT32_OK) return status;
  bytes_copy(sector + slot->offset, entry, 32);
  return volume_write(v, slot->sector_rel, sector);
}

static int directory_slot_at(const fat32_volume_t *v, uint32_t directory,
                             uint32_t index, bool grow,
                             fat32_dir_slot_t *slot) {
  uint32_t slots_per_cluster = v->sectors_per_cluster * 16U;
  uint32_t cluster = directory, next, count = 0, si;
  uint64_t rel;
  bool end;
  int status;
  while (index >= slots_per_cluster) {
    index -= slots_per_cluster;
    if (++count > v->cluster_count) return FAT32_ERR_CORRUPT;
    status = next_cluster(v, cluster, &next, &end);
    if (status != FAT32_OK) return status;
    if (end) {
      uint32_t hint = 2, added;
      if (!grow) return FAT32_ERR_NO_SPACE;
      status = allocate_cluster(v, &hint, &added);
      if (status != FAT32_OK) return status;
      status = fat_set(v, cluster, added);
      if (status != FAT32_OK) { (void)fat_set(v, added, 0); return status; }
      next = added;
    }
    cluster = next;
  }
  si = index / 16U;
  status = cluster_sector(v, cluster, si, &rel);
  if (status != FAT32_OK) return status;
  slot->sector_rel = rel;
  slot->offset = (uint16_t)((index % 16U) * 32U);
  return FAT32_OK;
}

static int directory_append(const fat32_volume_t *v, uint32_t directory,
                            const fat32_name_t *name, const uint8_t alias[11],
                            const uint8_t short_entry[32]) {
  fat32_dir_slot_t found_slot, write_slot;
  uint32_t tail, end_index, total, i;
  uint8_t lfn_entry[32], marker[32];
  bool found;
  int status;
  status = directory_scan(v, directory, name, alias, false, &found_slot, &found,
                          &tail, &end_index);
  if (status != FAT32_OK) return status;
  if (found) return FAT32_ERR_EXISTS;
  total = name->short_valid && short_name_equal(name->short_name, alias) ? 0U :
          (name->length + 12U) / 13U;
  if (total > FAT32_MAX_LFN_SLOTS) return FAT32_ERR_INVALID;
  /* Reserve the record run and a fresh end marker before modifying entries. */
  status = directory_slot_at(v, directory, end_index + total, true, &write_slot);
  if (status != FAT32_OK) return status;
  status = directory_slot_at(v, directory, end_index + total + 1U, true, &write_slot);
  if (status != FAT32_OK) return status;
  bytes_zero(marker, sizeof(marker));
  status = directory_slot_at(v, directory, end_index + total + 1U, false,
                             &write_slot);
  if (status != FAT32_OK) return status;
  status = write_directory_slot(v, &write_slot, marker);
  if (status != FAT32_OK) return status;
  for (i = 0; i < total; ++i) {
    uint32_t ordinal = total - i, j, unit_index;
    bytes_zero(lfn_entry, sizeof(lfn_entry));
    lfn_entry[0] = (uint8_t)(ordinal | (ordinal == total ? 0x40U : 0U));
    lfn_entry[11] = FAT32_ATTR_LFN;
    lfn_entry[12] = 0;
    lfn_entry[13] = lfn_checksum(alias);
    wr16(lfn_entry + 26, 0);
    for (j = 0; j < 13U; ++j) {
      static const uint8_t offsets[13] = {1,3,5,7,9,14,16,18,20,22,24,28,30};
      unit_index = (ordinal - 1U) * 13U + j;
      if (unit_index < name->length) wr16(lfn_entry + offsets[j], name->units[unit_index]);
      else if (unit_index == name->length) wr16(lfn_entry + offsets[j], 0);
      else wr16(lfn_entry + offsets[j], 0xFFFFU);
    }
    status = directory_slot_at(v, directory, end_index + i, false, &write_slot);
    if (status != FAT32_OK) return status;
    status = write_directory_slot(v, &write_slot, lfn_entry);
    if (status != FAT32_OK) return status;
  }
  status = directory_slot_at(v, directory, end_index + total, false, &write_slot);
  if (status != FAT32_OK) return status;
  status = write_directory_slot(v, &write_slot, short_entry);
  return status;
}

static void make_dot_entry(uint8_t entry[32], bool parent, uint32_t cluster) {
  uint32_t i;
  for (i = 0; i < 32; ++i) entry[i] = 0;
  for (i = 0; i < 11; ++i) entry[i] = (uint8_t)' ';
  entry[0] = '.';
  if (parent) entry[1] = '.';
  entry[11] = FAT32_ATTR_DIR;
  wr16(entry + 20, (uint16_t)(cluster >> 16));
  wr16(entry + 26, (uint16_t)cluster);
}

int fat32_mkdir(fat32_volume_t *volume, const char *path) {
  fat32_name_t target;
  uint8_t alias[11], entry[32];
  uint8_t sector[FAT32_SECTOR_SIZE];
  fat32_dir_slot_t existing;
  uint32_t parent, cluster, hint = 2;
  uint64_t rel;
  int status = valid_volume(volume);
  if (status != FAT32_OK) return status;
  status = resolve_parent(volume, path, &parent, &target);
  if (status != FAT32_OK) return status;
  status = directory_lookup(volume, parent, &target, &existing);
  if (status == FAT32_OK) return FAT32_ERR_EXISTS;
  if (status != FAT32_ERR_NOT_FOUND) return status;
  status = choose_short_alias(volume, parent, &target, alias);
  if (status != FAT32_OK) return status;
  status = allocate_cluster(volume, &hint, &cluster);
  if (status != FAT32_OK) return status;

  /* Add . and .. to the new directory's first sector. */
  status = cluster_sector(volume, cluster, 0, &rel);
  if (status != FAT32_OK) {
    (void)free_chain(volume, cluster);
    return status;
  }
  bytes_zero(sector, FAT32_SECTOR_SIZE);
  make_dot_entry(sector, false, cluster);
  make_dot_entry(sector + 32, true,
                 parent == volume->root_cluster ? 0 : parent);
  status = volume_write(volume, rel, sector);
  if (status != FAT32_OK) {
    (void)free_chain(volume, cluster);
    return status;
  }

  bytes_zero(entry, sizeof(entry));
  bytes_copy(entry, alias, 11);
  entry[11] = FAT32_ATTR_DIR;
  wr16(entry + 20, (uint16_t)(cluster >> 16));
  wr16(entry + 26, (uint16_t)cluster);
  status = directory_append(volume, parent, &target, alias, entry);
  if (status != FAT32_OK) {
    (void)free_chain(volume, cluster);
    return status;
  }
  return volume_flush(volume);
}

int fat32_write_file(fat32_volume_t *volume, const char *path,
                     const void *data, uint32_t length) {
  fat32_name_t name;
  uint8_t alias[11], entry[32], sector[FAT32_SECTOR_SIZE];
  fat32_dir_slot_t old_slot, found;
  const uint8_t *input = (const uint8_t *)data;
  uint32_t parent, old_first = 0, first = 0, previous = 0;
  uint32_t needed, hint = 2, i, si, cluster, amount, tail, end_index;
  uint32_t offset = 0;
  uint64_t rel;
  bool had_old = false, has_found;
  int status = valid_volume(volume);
  if (status != FAT32_OK) return status;
  if (length && !data) return FAT32_ERR_INVALID;
  status = resolve_parent(volume, path, &parent, &name);
  if (status != FAT32_OK) return status;
  status = directory_scan(volume, parent, &name,
      name.short_valid ? name.short_name : NULL, false, &found, &has_found,
      &tail, &end_index);
  if (status != FAT32_OK) return status;
  if (has_found) {
    old_slot = found;
    if (found.entry[11] & FAT32_ATTR_DIR) return FAT32_ERR_IS_DIR;
    old_first = entry_cluster(found.entry);
    if (old_first != 0 && (old_first < 2 ||
        old_first > volume->cluster_count + 1U)) return FAT32_ERR_CORRUPT;
    had_old = true;
  }

  needed = length == 0 ? 0 :
      (uint32_t)(((uint64_t)length +
       ((uint64_t)volume->sectors_per_cluster * FAT32_SECTOR_SIZE) - 1ULL) /
       ((uint64_t)volume->sectors_per_cluster * FAT32_SECTOR_SIZE));
  for (i = 0; i < needed; ++i) {
    status = allocate_cluster(volume, &hint, &cluster);
    if (status != FAT32_OK) {
      if (first) (void)free_chain(volume, first);
      return status;
    }
    if (!first) first = cluster;
    if (previous) {
      status = fat_set(volume, previous, cluster);
      if (status != FAT32_OK) {
        (void)fat_set(volume, cluster, 0);
        if (first != cluster) (void)free_chain(volume, first);
        else (void)fat_set(volume, first, 0);
        return status;
      }
    }
    previous = cluster;
  }

  /* Write complete sectors, zero-padding the last sector and any slack. */
  for (cluster = first, i = 0; i < needed; ++i) {
    uint32_t next = 0;
    bool at_end = true;
    if (i + 1U < needed) {
      status = next_cluster(volume, cluster, &next, &at_end);
      if (status != FAT32_OK || at_end) {
        if (status == FAT32_OK) status = FAT32_ERR_CORRUPT;
        (void)free_chain(volume, first);
        return status;
      }
    }
    for (si = 0; si < volume->sectors_per_cluster; ++si) {
      bytes_zero(sector, FAT32_SECTOR_SIZE);
      amount = length - offset;
      if (amount > FAT32_SECTOR_SIZE) amount = FAT32_SECTOR_SIZE;
      if (amount) bytes_copy(sector, input + offset, amount);
      status = cluster_sector(volume, cluster, si, &rel);
      if (status == FAT32_OK) status = volume_write(volume, rel, sector);
      if (status != FAT32_OK) {
        if (first) (void)free_chain(volume, first);
        return status;
      }
      offset += amount;
    }
    if (i + 1U < needed) cluster = next;
  }

  bytes_zero(entry, sizeof(entry));
  if (had_old) bytes_copy(entry, found.entry, 11);
  else {
    status = choose_short_alias(volume, parent, &name, alias);
    if (status != FAT32_OK) {
      if (first) (void)free_chain(volume, first);
      return status;
    }
    bytes_copy(entry, alias, 11);
  }
  entry[11] = 0x20;
  wr16(entry + 20, (uint16_t)(first >> 16));
  wr16(entry + 26, (uint16_t)first);
  wr32(entry + 28, length);
  if (had_old) {
    status = write_directory_slot(volume, &old_slot, entry);
  } else {
    status = directory_append(volume, parent, &name, alias, entry);
  }
  if (status != FAT32_OK) {
    if (first) (void)free_chain(volume, first);
    return status;
  }
  status = volume_flush(volume);
  if (status != FAT32_OK) return status;
  if (had_old && old_first && old_first != first) {
    status = free_chain(volume, old_first);
    if (status != FAT32_OK) return status;
    return volume_flush(volume);
  }
  return FAT32_OK;
}

int fat32_read_file(fat32_volume_t *volume, const char *path, void *out,
                    uint32_t capacity, uint32_t *out_length) {
  fat32_name_t name;
  uint8_t sector[FAT32_SECTOR_SIZE];
  fat32_dir_slot_t slot;
  uint32_t parent, length, cluster, offset = 0, expected, i, si, amount;
  uint32_t next;
  uint64_t rel;
  bool end;
  int status = valid_volume(volume);
  if (out_length) *out_length = 0;
  if (status != FAT32_OK) return status;
  if (!out_length) return FAT32_ERR_INVALID;
  status = resolve_parent(volume, path, &parent, &name);
  if (status != FAT32_OK) return status;
  status = directory_lookup(volume, parent, &name, &slot);
  if (status != FAT32_OK) return status;
  if (slot.entry[11] & FAT32_ATTR_DIR) return FAT32_ERR_IS_DIR;
  length = rd32(slot.entry + 28);
  if (length > capacity || (length && !out)) return FAT32_ERR_BUFFER;
  if (length == 0) {
    *out_length = 0;
    return FAT32_OK;
  }
  cluster = entry_cluster(slot.entry);
  if (cluster < 2 || cluster > volume->cluster_count + 1U)
    return FAT32_ERR_CORRUPT;
  expected = (uint32_t)(((uint64_t)length +
      ((uint64_t)volume->sectors_per_cluster * FAT32_SECTOR_SIZE) - 1ULL) /
      ((uint64_t)volume->sectors_per_cluster * FAT32_SECTOR_SIZE));

  for (i = 0; i < expected; ++i) {
    if (cluster < 2 || cluster > volume->cluster_count + 1U)
      return FAT32_ERR_CORRUPT;
    for (si = 0; si < volume->sectors_per_cluster && offset < length; ++si) {
      status = cluster_sector(volume, cluster, si, &rel);
      if (status != FAT32_OK) return status;
      status = volume_read(volume, rel, sector);
      if (status != FAT32_OK) return status;
      amount = length - offset;
      if (amount > FAT32_SECTOR_SIZE) amount = FAT32_SECTOR_SIZE;
      bytes_copy((uint8_t *)out + offset, sector, amount);
      offset += amount;
    }
    if (i + 1U < expected) {
      status = next_cluster(volume, cluster, &next, &end);
      if (status != FAT32_OK) return status;
      if (end) return FAT32_ERR_CORRUPT;
      cluster = next;
    }
  }
  if (offset != length) return FAT32_ERR_CORRUPT;
  *out_length = length;
  return FAT32_OK;
}
