#include "../include/block.h"
#include "../include/fat32.h"
#include "../include/kmalloc.h"
#include "../include/string.h"
#include "../include/vfs.h"

#define FAT32_MAX_IMPORT_DEPTH 8
#define FAT32_MAX_DIRECTORY_CLUSTERS 4096
#define FAT32_MAX_GPT_ENTRIES 128
#define FAT32_MAX_CLUSTER_SECTORS 128

struct fat32_fs {
  block_device_t *device;
  uint64_t volume_start;
  uint64_t volume_sectors;
  uint64_t fat_start;
  uint64_t data_start;
  uint32_t sectors_per_fat;
  uint32_t sectors_per_cluster;
  uint32_t cluster_count;
  uint32_t root_cluster;
  uint32_t cluster_bytes;
  char mount_path[64];
};

struct fat32_node {
  struct fat32_fs *fs;
  uint32_t first_cluster;
  uint32_t size;
  int is_directory;
};

static struct fat32_fs mounted_fs[BLOCK_MAX_DEVICES];
static size_t mounted_count;

static uint16_t le16(const uint8_t *p) {
  return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

static uint32_t le32(const uint8_t *p) {
  return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
         ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static int read_sector(block_device_t *device, uint64_t lba, uint8_t *buffer) {
  return block_read(device, lba, 1, buffer);
}

static int parse_bpb(struct fat32_fs *fs, uint64_t start, uint64_t available,
                     const uint8_t *boot) {
  if (boot[510] != 0x55 || boot[511] != 0xAA) return -1;
  uint16_t bytes_per_sector = le16(boot + 11);
  uint8_t spc = boot[13];
  uint16_t reserved = le16(boot + 14);
  uint8_t fats = boot[16];
  uint16_t total16 = le16(boot + 19);
  uint16_t fat16 = le16(boot + 22);
  uint32_t total32 = le32(boot + 32);
  uint32_t fat32 = le32(boot + 36);
  uint32_t root_cluster = le32(boot + 44) & 0x0FFFFFFFU;

  if (bytes_per_sector != BLOCK_SECTOR_SIZE || spc == 0 ||
      (spc & (spc - 1)) != 0 || spc > FAT32_MAX_CLUSTER_SECTORS ||
      reserved == 0 || fats == 0 || fats > 2 || fat16 != 0 || fat32 == 0 ||
      total16 != 0 || total32 == 0 || total32 > available || root_cluster < 2)
    return -1;

  uint64_t data_start_rel = (uint64_t)reserved + (uint64_t)fats * fat32;
  if (data_start_rel >= total32) return -1;
  uint64_t clusters = (total32 - data_start_rel) / spc;
  if (clusters == 0 || clusters > 0x0FFFFFF5U ||
      root_cluster >= clusters + 2)
    return -1;

  fs->volume_start = start;
  fs->volume_sectors = total32;
  fs->fat_start = start + reserved;
  fs->data_start = start + data_start_rel;
  fs->sectors_per_fat = fat32;
  fs->sectors_per_cluster = spc;
  fs->cluster_count = (uint32_t)clusters;
  fs->root_cluster = root_cluster;
  fs->cluster_bytes = (uint32_t)spc * BLOCK_SECTOR_SIZE;
  return 0;
}

static uint64_t cluster_lba(const struct fat32_fs *fs, uint32_t cluster) {
  return fs->data_start + (uint64_t)(cluster - 2) * fs->sectors_per_cluster;
}

static int next_cluster(struct fat32_fs *fs, uint32_t cluster,
                        uint32_t *next) {
  uint8_t sector[BLOCK_SECTOR_SIZE];
  uint64_t byte_offset = (uint64_t)cluster * 4;
  uint64_t fat_sector = fs->fat_start + byte_offset / BLOCK_SECTOR_SIZE;
  uint32_t in_sector = (uint32_t)(byte_offset % BLOCK_SECTOR_SIZE);
  if (fat_sector >= fs->volume_start + fs->volume_sectors ||
      read_sector(fs->device, fat_sector, sector) != 0)
    return -1;
  *next = le32(sector + in_sector) & 0x0FFFFFFFU;
  if (*next >= 0x0FFFFFF8U) return 1;
  if (*next < 2 || *next >= fs->cluster_count + 2 || *next == 0x0FFFFFF7U)
    return -1;
  return 0;
}

static ssize_t fat32_read_at(void *context, uint64_t offset, void *buffer,
                             size_t count) {
  struct fat32_node *node = (struct fat32_node *)context;
  if (!node || !node->fs || node->is_directory || (!buffer && count)) return -1;
  if (offset >= node->size) return 0;
  if (count > node->size - offset) count = (size_t)(node->size - offset);
  if (count == 0) return 0;
  if (node->first_cluster < 2 || node->first_cluster >= node->fs->cluster_count + 2)
    return -1;

  uint8_t *cluster_data = (uint8_t *)kmalloc(node->fs->cluster_bytes);
  if (!cluster_data) return -1;

  uint32_t cluster = node->first_cluster;
  uint64_t skip = offset / node->fs->cluster_bytes;
  if (skip > node->fs->cluster_count) {
    kfree(cluster_data);
    return -1;
  }
  for (uint64_t i = 0; i < skip; ++i) {
    uint32_t next;
    if (next_cluster(node->fs, cluster, &next) != 0) {
      kfree(cluster_data);
      return -1;
    }
    cluster = next;
  }

  size_t copied = 0;
  uint32_t steps = 0;
  while (copied < count && steps++ <= node->fs->cluster_count) {
    if (block_read(node->fs->device, cluster_lba(node->fs, cluster),
                   node->fs->sectors_per_cluster, cluster_data) != 0) {
      kfree(cluster_data);
      return -1;
    }
    size_t within = copied == 0 ? (size_t)(offset % node->fs->cluster_bytes) : 0;
    size_t amount = node->fs->cluster_bytes - within;
    if (amount > count - copied) amount = count - copied;
    memcpy((uint8_t *)buffer + copied, cluster_data + within, amount);
    copied += amount;
    if (copied < count) {
      uint32_t next;
      if (next_cluster(node->fs, cluster, &next) != 0) {
        kfree(cluster_data);
        return -1;
      }
      cluster = next;
    }
  }
  kfree(cluster_data);
  return copied == count ? (ssize_t)copied : -1;
}

static int make_short_name(const uint8_t *entry, char *name, size_t capacity) {
  size_t out = 0;
  int base_len = 8;
  while (base_len > 0 && entry[base_len - 1] == ' ') --base_len;
  int ext_len = 3;
  while (ext_len > 0 && entry[8 + ext_len - 1] == ' ') --ext_len;
  if (base_len == 0 || capacity < (size_t)base_len + (ext_len ? (size_t)ext_len + 2 : 1))
    return -1;
  for (int i = 0; i < base_len; ++i) {
    uint8_t ch = entry[i];
    if (ch == '/' || ch == '\\' || ch < 0x20) return -1;
    name[out++] = (char)ch;
  }
  if (ext_len) {
    name[out++] = '.';
    for (int i = 0; i < ext_len; ++i) {
      uint8_t ch = entry[8 + i];
      if (ch == '/' || ch == '\\' || ch < 0x20) return -1;
      name[out++] = (char)ch;
    }
  }
  name[out] = '\0';
  return 0;
}

static int append_path(const char *parent, const char *name, char *out,
                       size_t capacity) {
  size_t a = strlen(parent), b = strlen(name);
  if (a + b + 2 > capacity) return -1;
  memcpy(out, parent, a);
  if (a > 1 && out[a - 1] != '/') out[a++] = '/';
  else if (a == 1 && out[0] == '/') out[a++] = '\0';
  memcpy(out + a, name, b + 1);
  return 0;
}

static int import_directory(struct fat32_fs *fs, uint32_t first_cluster,
                            const char *path, unsigned int depth) {
  if (depth > FAT32_MAX_IMPORT_DEPTH || first_cluster < 2 ||
      first_cluster >= fs->cluster_count + 2)
    return -1;

  uint8_t *cluster_data = (uint8_t *)kmalloc(fs->cluster_bytes);
  if (!cluster_data) return -1;
  uint32_t cluster = first_cluster;
  int result = 0;

  for (uint32_t step = 0; step < FAT32_MAX_DIRECTORY_CLUSTERS; ++step) {
    if (block_read(fs->device, cluster_lba(fs, cluster),
                   fs->sectors_per_cluster, cluster_data) != 0) {
      result = -1;
      break;
    }
    for (uint32_t offset = 0; offset + 32 <= fs->cluster_bytes; offset += 32) {
      const uint8_t *entry = cluster_data + offset;
      if (entry[0] == 0) {
        kfree(cluster_data);
        return result;
      }
      if (entry[0] == 0xE5 || entry[11] == 0x0F || (entry[11] & 0x08))
        continue;
      if (entry[0] == '.' || (entry[0] == ' ')) continue;

      char name[16];
      if (make_short_name(entry, name, sizeof(name)) != 0) continue;
      char child_path[256];
      if (append_path(path, name, child_path, sizeof(child_path)) != 0)
        continue;

      int is_dir = (entry[11] & 0x10) != 0;
      uint32_t first = ((uint32_t)le16(entry + 20) << 16) |
                       le16(entry + 26);
      uint32_t size = le32(entry + 28);
      struct fat32_node *node = (struct fat32_node *)kmalloc(sizeof(*node));
      if (!node) {
        result = -1;
        break;
      }
      node->fs = fs;
      node->first_cluster = first & 0x0FFFFFFFU;
      node->size = size;
      node->is_directory = is_dir;
      if (vfs_add_readonly_entry(child_path, is_dir, size, node,
                                 is_dir ? NULL : fat32_read_at) != 0) {
        kfree(node);
        continue;
      }
      if (is_dir && depth < FAT32_MAX_IMPORT_DEPTH && node->first_cluster >= 2) {
        if (import_directory(fs, node->first_cluster, child_path, depth + 1) != 0)
          result = -1;
      }
    }
    if (result != 0) break;
    uint32_t next;
    int status = next_cluster(fs, cluster, &next);
    if (status == 1) break;
    if (status != 0) {
      result = -1;
      break;
    }
    cluster = next;
  }
  kfree(cluster_data);
  return result;
}

static int mount_at(struct fat32_fs *fs, uint64_t start, uint64_t available,
                    const uint8_t *boot) {
  if (parse_bpb(fs, start, available, boot) != 0) return -1;
  if (!vfs_exists("/mnt") && vfs_mkdir("/mnt") != 0) return -1;
  size_t n = strlen(fs->device->name);
  if (n + 6 >= sizeof(fs->mount_path)) return -1;
  strcpy(fs->mount_path, "/mnt/");
  strcat(fs->mount_path, fs->device->name);
  if (vfs_exists(fs->mount_path)) return -1;

  struct fat32_node *root = (struct fat32_node *)kmalloc(sizeof(*root));
  if (!root) return -1;
  root->fs = fs;
  root->first_cluster = fs->root_cluster;
  root->size = 0;
  root->is_directory = 1;
  if (vfs_add_readonly_entry(fs->mount_path, 1, 0, root, NULL) != 0) {
    kfree(root);
    return -1;
  }
  if (import_directory(fs, fs->root_cluster, fs->mount_path, 0) != 0)
    return -1;
  return 0;
}

static int try_start(block_device_t *device, uint64_t start,
                     uint64_t available, struct fat32_fs *fs) {
  uint8_t boot[BLOCK_SECTOR_SIZE];
  if (start >= device->sector_count || available == 0 ||
      available > device->sector_count - start ||
      read_sector(device, start, boot) != 0)
    return -1;
  memset(fs, 0, sizeof(*fs));
  fs->device = device;
  if (mount_at(fs, start, available, boot) == 0) return 0;
  return -1;
}

int fat32_mount_first_volume(block_device_t *device) {
  if (!device || mounted_count >= BLOCK_MAX_DEVICES) return -1;
  struct fat32_fs *fs = &mounted_fs[mounted_count];
  uint8_t sector[BLOCK_SECTOR_SIZE];
  if (read_sector(device, 0, sector) != 0) return -1;

  /* A FAT32 superfloppy volume has no partition table. */
  if (try_start(device, 0, device->sector_count, fs) == 0) {
    ++mounted_count;
    return 0;
  }

  /* Legacy MBR FAT32 partitions. */
  if (sector[510] == 0x55 && sector[511] == 0xAA) {
    for (unsigned int i = 0; i < 4; ++i) {
      const uint8_t *part = sector + 446 + i * 16;
      uint8_t type = part[4];
      uint64_t start = le32(part + 8);
      uint64_t length = le32(part + 12);
      if ((type == 0x0B || type == 0x0C) &&
          try_start(device, start, length, fs) == 0) {
        ++mounted_count;
        return 0;
      }
    }
  }

  /* GPT: inspect bounded entries and select the first valid FAT32 volume. */
  uint8_t header[BLOCK_SECTOR_SIZE];
  if (device->sector_count > 1 && read_sector(device, 1, header) == 0 &&
      memcmp(header, "EFI PART", 8) == 0) {
    uint64_t entries_lba = *(const uint64_t *)(const void *)(header + 72);
    uint32_t entries_count = le32(header + 80);
    uint32_t entry_size = le32(header + 84);
    if (entries_count > FAT32_MAX_GPT_ENTRIES)
      entries_count = FAT32_MAX_GPT_ENTRIES;
    if (entry_size >= 128 && entry_size <= BLOCK_SECTOR_SIZE &&
        (entry_size % 128) == 0 &&
        entries_lba < device->sector_count) {
      for (uint32_t i = 0; i < entries_count; ++i) {
        uint64_t byte = (uint64_t)i * entry_size;
        uint64_t entry_lba = entries_lba + byte / BLOCK_SECTOR_SIZE;
        uint32_t off = (uint32_t)(byte % BLOCK_SECTOR_SIZE);
        uint8_t entries_sector[BLOCK_SECTOR_SIZE];
        if (entry_lba >= device->sector_count ||
            read_sector(device, entry_lba, entries_sector) != 0)
          break;
        const uint8_t *entry = entries_sector + off;
        int empty = 1;
        for (unsigned int j = 0; j < 16; ++j) if (entry[j]) empty = 0;
        if (empty) continue;
        uint64_t start = *(const uint64_t *)(const void *)(entry + 32);
        uint64_t last = *(const uint64_t *)(const void *)(entry + 40);
        if (start >= device->sector_count || last < start ||
            last >= device->sector_count)
          continue;
        if (try_start(device, start, last - start + 1, fs) == 0) {
          ++mounted_count;
          return 0;
        }
      }
    }
  }
  return -1;
}
