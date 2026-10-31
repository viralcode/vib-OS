/*
 * Freestanding GPT support for 512-byte block devices.
 * CRCs use the reflected IEEE CRC-32 polynomial required by UEFI GPT.
 */
#include "../include/gpt.h"
#include "../include/string.h"

#define GPT_SECTOR_SIZE       512U
#define GPT_HEADER_MIN_SIZE   92U
#define GPT_ENTRY_MIN_SIZE    128U
#define GPT_CREATE_ENTRIES    128U
#define GPT_CREATE_ENTRY_SIZE 128U
#define GPT_CREATE_ARRAY_SECTORS 32U
#define GPT_FIRST_ESP_LBA     2048ULL
#define GPT_ALIGN_SECTORS     2048ULL

/* Reused by the synchronous formatter to keep large buffers off the kernel stack. */
static uint8_t create_entries[GPT_CREATE_ENTRIES * GPT_CREATE_ENTRY_SIZE];

static const uint8_t gpt_signature[8] = {
  'E', 'F', 'I', ' ', 'P', 'A', 'R', 'T'
};

/* EFI System Partition GUID in its on-disk (mixed-endian) byte order. */
static const uint8_t esp_type_guid[16] = {
  0x28, 0x73, 0x2A, 0xC1, 0x1F, 0xF8, 0xD2, 0x11,
  0xBA, 0x4B, 0x00, 0xA0, 0xC9, 0x3E, 0xC9, 0x3B
};

typedef struct {
  uint64_t header_lba;
  uint64_t backup_lba;
  uint64_t first_usable;
  uint64_t last_usable;
  uint64_t entries_lba;
  uint32_t entry_count;
  uint32_t entry_size;
  uint32_t entries_crc;
} gpt_info_t;

static uint16_t read_le16(const uint8_t *p) {
  return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

static uint32_t read_le32(const uint8_t *p) {
  return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
         ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static uint64_t read_le64(const uint8_t *p) {
  return (uint64_t)read_le32(p) | ((uint64_t)read_le32(p + 4) << 32);
}

static void write_le32(uint8_t *p, uint32_t value) {
  p[0] = (uint8_t)value;
  p[1] = (uint8_t)(value >> 8);
  p[2] = (uint8_t)(value >> 16);
  p[3] = (uint8_t)(value >> 24);
}

static void write_le64(uint8_t *p, uint64_t value) {
  write_le32(p, (uint32_t)value);
  write_le32(p + 4, (uint32_t)(value >> 32));
}

static uint32_t crc32_update(uint32_t crc, const uint8_t *data, size_t length) {
  size_t i;
  uint32_t bit;
  for (i = 0; i < length; ++i) {
    crc ^= data[i];
    for (bit = 0; bit < 8; ++bit)
      crc = (crc >> 1) ^ (0xEDB88320U & (uint32_t)-(int32_t)(crc & 1U));
  }
  return crc;
}

static uint32_t crc32(const uint8_t *data, size_t length) {
  return crc32_update(0xFFFFFFFFU, data, length) ^ 0xFFFFFFFFU;
}

static bool bytes_are_zero(const uint8_t *bytes, size_t length) {
  size_t i;
  for (i = 0; i < length; ++i)
    if (bytes[i] != 0)
      return false;
  return true;
}

/* Validate the mandatory 512-byte MBR and its protective partition entry. */
static int validate_protective_mbr(blockdev_t *device) {
  uint8_t sector[GPT_SECTOR_SIZE];
  uint32_t i;
  bool found = false;
  if (blockdev_read(device, 0, 1, sector) != 0)
    return GPT_ERR_IO;
  if (sector[510] != 0x55 || sector[511] != 0xAA)
    return GPT_ERR_MBR;
  for (i = 0; i < 4; ++i) {
    const uint8_t *entry = sector + 446 + i * 16;
    if (entry[4] == 0xEE && read_le32(entry + 8) == 1 &&
        read_le32(entry + 12) != 0)
      found = true;
  }
  return found ? GPT_OK : GPT_ERR_MBR;
}

/* CRC the exact GPT array byte count, streaming one sector at a time. */
static int validate_array_crc(blockdev_t *device, const gpt_info_t *info) {
  uint8_t sector[GPT_SECTOR_SIZE];
  uint64_t byte_count = (uint64_t)info->entry_count * info->entry_size;
  uint64_t sectors = byte_count / GPT_SECTOR_SIZE;
  uint64_t remaining = byte_count;
  uint64_t i;
  uint32_t crc = 0xFFFFFFFFU;
  if (byte_count % GPT_SECTOR_SIZE)
    ++sectors;
  for (i = 0; i < sectors; ++i) {
    size_t amount = remaining > GPT_SECTOR_SIZE ? GPT_SECTOR_SIZE : (size_t)remaining;
    if (blockdev_read(device, info->entries_lba + i, 1, sector) != 0)
      return GPT_ERR_IO;
    crc = crc32_update(crc, sector, amount);
    remaining -= amount;
  }
  crc ^= 0xFFFFFFFFU;
  return crc == info->entries_crc ? GPT_OK : GPT_ERR_CORRUPT;
}

/* Validate either header candidate, including geometry and its array CRC. */
static int load_gpt_candidate(blockdev_t *device, uint64_t lba,
                              uint64_t expected_backup, gpt_info_t *info) {
  uint8_t sector[GPT_SECTOR_SIZE];
  uint8_t header_copy[GPT_SECTOR_SIZE];
  uint32_t header_size;
  uint32_t saved_crc;
  uint64_t array_bytes;
  uint64_t array_sectors;
  uint64_t array_last;
  int rc;

  if (lba >= device->sector_count || blockdev_read(device, lba, 1, sector) != 0)
    return GPT_ERR_IO;
  if (memcmp(sector, gpt_signature, sizeof(gpt_signature)) != 0)
    return GPT_ERR_CORRUPT;
  header_size = read_le32(sector + 12);
  if (header_size < GPT_HEADER_MIN_SIZE || header_size > GPT_SECTOR_SIZE ||
      read_le32(sector + 20) != 0)
    return GPT_ERR_CORRUPT;
  saved_crc = read_le32(sector + 16);
  memcpy(header_copy, sector, header_size);
  memset(header_copy + 16, 0, 4);
  if (crc32(header_copy, header_size) != saved_crc)
    return GPT_ERR_CORRUPT;

  info->header_lba = read_le64(sector + 24);
  info->backup_lba = read_le64(sector + 32);
  info->first_usable = read_le64(sector + 40);
  info->last_usable = read_le64(sector + 48);
  info->entries_lba = read_le64(sector + 72);
  info->entry_count = read_le32(sector + 80);
  info->entry_size = read_le32(sector + 84);
  info->entries_crc = read_le32(sector + 88);

  if (info->header_lba != lba || info->backup_lba != expected_backup ||
      info->backup_lba >= device->sector_count || info->backup_lba == lba ||
      info->first_usable < 2 || info->first_usable > info->last_usable ||
      info->last_usable >= device->sector_count - 1 ||
      info->entry_count == 0 || info->entry_size < GPT_ENTRY_MIN_SIZE ||
      (info->entry_size & 7U) != 0)
    return GPT_ERR_CORRUPT;

  if ((uint64_t)info->entry_count > (~(uint64_t)0) / info->entry_size)
    return GPT_ERR_OVERFLOW;
  array_bytes = (uint64_t)info->entry_count * info->entry_size;
  array_sectors = array_bytes / GPT_SECTOR_SIZE;
  if (array_bytes % GPT_SECTOR_SIZE)
    ++array_sectors;
  if (array_sectors == 0 || info->entries_lba >= device->sector_count ||
      array_sectors > device->sector_count - info->entries_lba)
    return GPT_ERR_CORRUPT;
  array_last = info->entries_lba + array_sectors - 1;
  if (lba == 1) {
    if (info->entries_lba <= lba || array_last >= info->first_usable ||
        array_last >= expected_backup)
      return GPT_ERR_CORRUPT;
  } else {
    if (info->entries_lba <= info->last_usable || array_last >= lba ||
        info->entries_lba <= expected_backup)
      return GPT_ERR_CORRUPT;
  }

  rc = validate_array_crc(device, info);
  return rc;
}

static int load_valid_gpt(blockdev_t *device, gpt_info_t *info) {
  int primary_rc;
  int backup_rc;
  if (device == NULL || info == NULL || device->read == NULL ||
      device->sector_count < 3)
    return GPT_ERR_INVALID;
  if (device->sector_size != GPT_SECTOR_SIZE)
    return GPT_ERR_SECTOR_SIZE;
  primary_rc = validate_protective_mbr(device);
  if (primary_rc != GPT_OK)
    return primary_rc;

  primary_rc = load_gpt_candidate(device, 1, device->sector_count - 1, info);
  if (primary_rc == GPT_OK)
    return GPT_OK;
  backup_rc = load_gpt_candidate(device, device->sector_count - 1, 1, info);
  if (backup_rc == GPT_OK)
    return GPT_OK;
  if (primary_rc == GPT_ERR_IO && backup_rc == GPT_ERR_IO)
    return GPT_ERR_IO;
  if (primary_rc == GPT_ERR_OVERFLOW && backup_rc == GPT_ERR_OVERFLOW)
    return GPT_ERR_OVERFLOW;
  return GPT_ERR_CORRUPT;
}

static int read_entry(blockdev_t *device, const gpt_info_t *info,
                      uint32_t index, uint8_t *entry) {
  uint64_t offset = (uint64_t)index * info->entry_size;
  uint64_t lba = info->entries_lba + offset / GPT_SECTOR_SIZE;
  uint32_t in_sector = (uint32_t)(offset % GPT_SECTOR_SIZE);
  uint32_t wanted = info->entry_size < GPT_ENTRY_MIN_SIZE ?
                    info->entry_size : GPT_ENTRY_MIN_SIZE;
  uint8_t sector[GPT_SECTOR_SIZE];
  uint32_t copied = 0;
  while (copied < wanted) {
    uint32_t chunk = GPT_SECTOR_SIZE - in_sector;
    if (chunk > wanted - copied)
      chunk = wanted - copied;
    if (blockdev_read(device, lba, 1, sector) != 0)
      return GPT_ERR_IO;
    memcpy(entry + copied, sector + in_sector, chunk);
    copied += chunk;
    ++lba;
    in_sector = 0;
  }
  return GPT_OK;
}

static int decode_entry(const uint8_t *entry, const gpt_info_t *info,
                        gpt_partition_t *out) {
  uint32_t i;
  if (bytes_are_zero(entry, 16))
    return GPT_ERR_NOT_FOUND;
  out->first_lba = read_le64(entry + 32);
  out->last_lba = read_le64(entry + 40);
  if (out->first_lba > out->last_lba ||
      out->first_lba < info->first_usable ||
      out->last_lba > info->last_usable)
    return GPT_ERR_CORRUPT;
  memcpy(out->type_guid, entry, 16);
  memcpy(out->unique_guid, entry + 16, 16);
  for (i = 0; i < 36; ++i) {
    uint16_t ch = read_le16(entry + 56 + i * 2);
    if (ch == 0)
      break;
    out->name[i] = (ch >= 0x20 && ch <= 0x7E) ? (char)ch : '?';
  }
  out->name[i] = '\0';
  return GPT_OK;
}

int gpt_read_partition(blockdev_t *device, uint32_t index,
                       gpt_partition_t *out) {
  gpt_info_t info;
  uint8_t entry[GPT_ENTRY_MIN_SIZE];
  int rc;
  if (out == NULL)
    return GPT_ERR_INVALID;
  rc = load_valid_gpt(device, &info);
  if (rc != GPT_OK)
    return rc;
  if (index >= info.entry_count)
    return GPT_ERR_INDEX;
  /* GPT entries may exceed 128 bytes; their defined fields occupy the prefix. */
  rc = read_entry(device, &info, index, entry);
  if (rc != GPT_OK)
    return rc;
  return decode_entry(entry, &info, out);
}

int gpt_find_esp(blockdev_t *device, gpt_partition_t *out) {
  gpt_info_t info;
  uint8_t entry[GPT_ENTRY_MIN_SIZE];
  uint32_t index;
  int rc;
  if (out == NULL)
    return GPT_ERR_INVALID;
  rc = load_valid_gpt(device, &info);
  if (rc != GPT_OK)
    return rc;
  for (index = 0; index < info.entry_count; ++index) {
    rc = read_entry(device, &info, index, entry);
    if (rc != GPT_OK)
      return rc;
    if (memcmp(entry, esp_type_guid, sizeof(esp_type_guid)) == 0) {
      rc = decode_entry(entry, &info, out);
      if (rc == GPT_OK)
        return GPT_OK;
      if (rc != GPT_ERR_NOT_FOUND)
        return rc;
    }
  }
  return GPT_ERR_NOT_FOUND;
}

/* A small avalanche mixer used only to make nonzero GUIDs less predictable. */
static uint64_t mix64(uint64_t x) {
  x ^= x >> 30;
  x *= 0xBF58476D1CE4E5B9ULL;
  x ^= x >> 27;
  x *= 0x94D049BB133111EBULL;
  x ^= x >> 31;
  return x;
}

static uint64_t timestamp_seed(void) {
  uint32_t low;
  uint32_t high;
#if defined(__x86_64__) || defined(__i386__)
  __asm__ volatile ("rdtsc" : "=a" (low), "=d" (high));
  return ((uint64_t)high << 32) | low;
#else
  low = 0;
  high = 0;
  return ((uint64_t)high << 32) | low;
#endif
}

static void make_guid(uint8_t guid[16], uint64_t seed, uint64_t salt) {
  uint64_t a = mix64(seed ^ salt ^ 0x9E3779B97F4A7C15ULL);
  uint64_t b = mix64(seed + salt + 0xD1B54A32D192ED03ULL);
  uint32_t i;
  for (i = 0; i < 8; ++i) {
    guid[i] = (uint8_t)(a >> (i * 8));
    guid[i + 8] = (uint8_t)(b >> (i * 8));
  }
  guid[7] = (uint8_t)((guid[7] & 0x0F) | 0x40); /* UUID version 4 */
  guid[8] = (uint8_t)((guid[8] & 0x3F) | 0x80); /* RFC 4122 variant */
  if (bytes_are_zero(guid, 16))
    guid[0] = 1;
}

static void create_header(uint8_t sector[GPT_SECTOR_SIZE],
                          uint64_t current_lba, uint64_t backup_lba,
                          uint64_t first_usable, uint64_t last_usable,
                          uint64_t entries_lba, const uint8_t disk_guid[16],
                          uint32_t entries_crc) {
  memset(sector, 0, GPT_SECTOR_SIZE);
  memcpy(sector, gpt_signature, sizeof(gpt_signature));
  write_le32(sector + 8, 0x00010000U);
  write_le32(sector + 12, GPT_HEADER_MIN_SIZE);
  write_le64(sector + 24, current_lba);
  write_le64(sector + 32, backup_lba);
  write_le64(sector + 40, first_usable);
  write_le64(sector + 48, last_usable);
  memcpy(sector + 56, disk_guid, 16);
  write_le64(sector + 72, entries_lba);
  write_le32(sector + 80, GPT_CREATE_ENTRIES);
  write_le32(sector + 84, GPT_CREATE_ENTRY_SIZE);
  write_le32(sector + 88, entries_crc);
  write_le32(sector + 16, crc32(sector, GPT_HEADER_MIN_SIZE));
}

int gpt_create_single_esp(blockdev_t *device, gpt_partition_t *out) {
  uint8_t mbr[GPT_SECTOR_SIZE];
  uint8_t header[GPT_SECTOR_SIZE];
  uint8_t *entries = create_entries;
  uint8_t disk_guid[16];
  uint8_t partition_guid[16];
  uint64_t sectors;
  uint64_t backup_entries_lba;
  uint64_t last_usable;
  uint64_t end_exclusive;
  uint64_t seed;
  uint64_t partition_last;
  uint32_t entries_crc;
  uint32_t protective_count;
  int rc;

  if (device == NULL || device->read == NULL || device->write == NULL || out == NULL)
    return GPT_ERR_INVALID;
  if (device->sector_size != GPT_SECTOR_SIZE)
    return GPT_ERR_SECTOR_SIZE;
  sectors = device->sector_count;
  /* 4,129 sectors is the smallest geometry with a full aligned 1 MiB ESP. */
  if (sectors < 4129ULL)
    return GPT_ERR_TOO_SMALL;
  if (sectors > (~(uint64_t)0) - 1ULL)
    return GPT_ERR_OVERFLOW;

  backup_entries_lba = sectors - GPT_CREATE_ARRAY_SECTORS - 1ULL;
  last_usable = backup_entries_lba - 1ULL;
  end_exclusive = ((last_usable + 1ULL) / GPT_ALIGN_SECTORS) * GPT_ALIGN_SECTORS;
  if (end_exclusive <= GPT_FIRST_ESP_LBA)
    return GPT_ERR_TOO_SMALL;
  partition_last = end_exclusive - 1ULL;
  if (partition_last > last_usable)
    return GPT_ERR_OVERFLOW;

  seed = timestamp_seed() ^ sectors ^ (uint64_t)(uintptr_t)device;
  make_guid(disk_guid, seed, 0xA0761D6478BD642FULL);
  make_guid(partition_guid, seed, 0xE7037ED1A0B428DBULL);

  memset(entries, 0, GPT_CREATE_ENTRIES * GPT_CREATE_ENTRY_SIZE);
  memcpy(entries, esp_type_guid, 16);
  memcpy(entries + 16, partition_guid, 16);
  write_le64(entries + 32, GPT_FIRST_ESP_LBA);
  write_le64(entries + 40, partition_last);
  /* GPT names are 36 UTF-16LE code units. */
  entries[56] = 'E';
  entries[58] = 'F';
  entries[60] = 'I';
  entries[62] = ' ';
  entries[64] = 'S';
  entries[66] = 'y';
  entries[68] = 's';
  entries[70] = 't';
  entries[72] = 'e';
  entries[74] = 'm';
  entries_crc = crc32(entries, GPT_CREATE_ENTRIES * GPT_CREATE_ENTRY_SIZE);

  /* Backup metadata is written first; the protective MBR is the final commit marker. */
  if (blockdev_write(device, backup_entries_lba, GPT_CREATE_ARRAY_SECTORS,
                     entries) != 0)
    return GPT_ERR_IO;
  create_header(header, sectors - 1ULL, 1, 34, last_usable,
                backup_entries_lba, disk_guid, entries_crc);
  if (blockdev_write(device, sectors - 1ULL, 1, header) != 0)
    return GPT_ERR_IO;
  if (blockdev_write(device, 2, GPT_CREATE_ARRAY_SECTORS, entries) != 0)
    return GPT_ERR_IO;
  create_header(header, 1, sectors - 1ULL, 34, last_usable, 2,
                disk_guid, entries_crc);
  if (blockdev_write(device, 1, 1, header) != 0)
    return GPT_ERR_IO;

  memset(mbr, 0, sizeof(mbr));
  mbr[446 + 1] = 0x00;
  mbr[446 + 2] = 0x02;
  mbr[446 + 3] = 0x00;
  mbr[446 + 4] = 0xEE;
  mbr[446 + 5] = 0xFF;
  mbr[446 + 6] = 0xFF;
  mbr[446 + 7] = 0xFF;
  write_le32(mbr + 446 + 8, 1);
  protective_count = (sectors - 1ULL > 0xFFFFFFFFULL) ? 0xFFFFFFFFU :
                     (uint32_t)(sectors - 1ULL);
  write_le32(mbr + 446 + 12, protective_count);
  mbr[510] = 0x55;
  mbr[511] = 0xAA;
  if (blockdev_write(device, 0, 1, mbr) != 0)
    return GPT_ERR_IO;
  rc = blockdev_flush(device);
  if (rc != 0)
    return GPT_ERR_IO;

  out->first_lba = GPT_FIRST_ESP_LBA;
  out->last_lba = partition_last;
  memcpy(out->type_guid, esp_type_guid, 16);
  memcpy(out->unique_guid, partition_guid, 16);
  memcpy(out->name, "EFI System", 11);
  return GPT_OK;
}
