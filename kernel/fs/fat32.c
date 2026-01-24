/*
 * Vib-OS Kernel - FAT32 Filesystem Driver
 *
 * Provides read/write support for FAT32 filesystems on block devices.
 * Supports long filenames (LFN) and persistent storage.
 */

#include "fs/vfs.h"
#include "drivers/block.h"
#include "mm/kmalloc.h"
#include "printk.h"
#include "types.h"

/* ===================================================================== */
/* FAT32 Constants */
/* ===================================================================== */

#define FAT32_SIGNATURE         0xAA55
#define FAT32_FSTYPE            "FAT32   "
#define FAT32_BOOT_SIGNATURE    0x29

#define FAT_ENTRY_FREE          0x00000000
#define FAT_ENTRY_RESERVED      0x00000001
#define FAT_ENTRY_BAD           0x0FFFFFF7
#define FAT_ENTRY_EOC           0x0FFFFFF8  /* End of chain marker */
#define FAT_ENTRY_MASK          0x0FFFFFFF  /* 28 bits for cluster number */

#define FAT_ATTR_READ_ONLY      0x01
#define FAT_ATTR_HIDDEN         0x02
#define FAT_ATTR_SYSTEM         0x04
#define FAT_ATTR_VOLUME_ID      0x08
#define FAT_ATTR_DIRECTORY      0x10
#define FAT_ATTR_ARCHIVE        0x20
#define FAT_ATTR_LONG_NAME      0x0F

#define FAT_DIR_ENTRY_SIZE      32
#define FAT_NAME_MAX            11          /* 8.3 format */
#define FAT_LFN_MAX             255

/* ===================================================================== */
/* FAT32 On-disk Structures */
/* ===================================================================== */

/* BIOS Parameter Block (BPB) - Boot Sector */
struct fat32_bpb {
    uint8_t  jmp_boot[3];           /* Jump instruction */
    char     oem_name[8];           /* OEM identifier */
    uint16_t bytes_per_sector;      /* Sector size (usually 512) */
    uint8_t  sectors_per_cluster;   /* Cluster size in sectors */
    uint16_t reserved_sectors;      /* Reserved sectors before FAT */
    uint8_t  num_fats;              /* Number of FAT copies */
    uint16_t root_entry_count;      /* Root entries (0 for FAT32) */
    uint16_t total_sectors_16;      /* Total sectors (0 for FAT32) */
    uint8_t  media_type;            /* Media descriptor */
    uint16_t fat_size_16;           /* FAT size in sectors (0 for FAT32) */
    uint16_t sectors_per_track;     /* Sectors per track */
    uint16_t num_heads;             /* Number of heads */
    uint32_t hidden_sectors;        /* Hidden sectors */
    uint32_t total_sectors_32;      /* Total sectors (FAT32) */
    /* FAT32 specific */
    uint32_t fat_size_32;           /* FAT size in sectors */
    uint16_t ext_flags;             /* Extended flags */
    uint16_t fs_version;            /* Filesystem version */
    uint32_t root_cluster;          /* Root directory cluster */
    uint16_t fs_info_sector;        /* FSInfo sector number */
    uint16_t backup_boot_sector;    /* Backup boot sector */
    uint8_t  reserved[12];
    uint8_t  drive_number;          /* BIOS drive number */
    uint8_t  reserved1;
    uint8_t  boot_signature;        /* Extended boot signature (0x29) */
    uint32_t volume_id;             /* Volume serial number */
    char     volume_label[11];      /* Volume label */
    char     fs_type[8];            /* Filesystem type string */
} __attribute__((packed));

/* FSInfo Structure */
struct fat32_fsinfo {
    uint32_t lead_sig;              /* 0x41615252 */
    uint8_t  reserved1[480];
    uint32_t struc_sig;             /* 0x61417272 */
    uint32_t free_count;            /* Free cluster count */
    uint32_t next_free;             /* Next free cluster hint */
    uint8_t  reserved2[12];
    uint32_t trail_sig;             /* 0xAA550000 */
} __attribute__((packed));

/* Directory Entry (8.3 short name) */
struct fat32_dir_entry {
    char     name[8];               /* Filename (space-padded) */
    char     ext[3];                /* Extension (space-padded) */
    uint8_t  attr;                  /* Attributes */
    uint8_t  nt_reserved;           /* Reserved for Windows NT */
    uint8_t  create_time_tenth;     /* Creation time (tenths of sec) */
    uint16_t create_time;           /* Creation time */
    uint16_t create_date;           /* Creation date */
    uint16_t access_date;           /* Last access date */
    uint16_t cluster_high;          /* High 16 bits of cluster */
    uint16_t modify_time;           /* Modification time */
    uint16_t modify_date;           /* Modification date */
    uint16_t cluster_low;           /* Low 16 bits of cluster */
    uint32_t file_size;             /* File size in bytes */
} __attribute__((packed));

/* Long Filename Entry */
struct fat32_lfn_entry {
    uint8_t  order;                 /* Order of this entry */
    uint16_t name1[5];              /* Characters 1-5 */
    uint8_t  attr;                  /* Always 0x0F */
    uint8_t  type;                  /* Reserved (0) */
    uint8_t  checksum;              /* Checksum of short name */
    uint16_t name2[6];              /* Characters 6-11 */
    uint16_t cluster;               /* Always 0 */
    uint16_t name3[2];              /* Characters 12-13 */
} __attribute__((packed));

/* ===================================================================== */
/* FAT32 In-memory Structures */
/* ===================================================================== */

struct fat32_fs {
    struct fat32_bpb bpb;           /* Boot sector data */
    uint32_t fat_start;             /* First FAT sector */
    uint32_t data_start;            /* First data sector */
    uint32_t root_cluster;          /* Root directory cluster */
    uint32_t cluster_size;          /* Bytes per cluster */
    uint32_t total_clusters;        /* Total data clusters */
    uint32_t *fat_cache;            /* Cached FAT table */
    uint32_t fat_cache_size;        /* Size of cached FAT */

    /* Block device access */
    int (*read_sectors)(uint64_t sector, uint32_t count, void *buf);
    int (*write_sectors)(uint64_t sector, uint32_t count, const void *buf);
    void *device;
};

struct fat32_file {
    struct fat32_fs *fs;
    uint32_t start_cluster;         /* First cluster of file */
    uint32_t current_cluster;       /* Current cluster */
    uint32_t cluster_offset;        /* Offset within cluster */
    uint32_t file_size;             /* Total file size */
    uint32_t position;              /* Current position */
    uint8_t attr;                   /* File attributes */
    char name[FAT_LFN_MAX + 1];     /* Filename */
    /* Directory entry location for updating file size */
    uint32_t dir_cluster;           /* Cluster containing directory entry */
    uint32_t dir_entry_offset;      /* Byte offset of entry within cluster */
};

/* ===================================================================== */
/* Static data */
/* ===================================================================== */

static struct fat32_fs *mounted_fs = NULL;

/* ===================================================================== */
/* Forward Declarations */
/* ===================================================================== */

static void string_to_fat_name(const char *str, char *name, char *ext);

/* ===================================================================== */
/* Helper Functions */
/* ===================================================================== */

static inline uint32_t cluster_to_sector(struct fat32_fs *fs, uint32_t cluster)
{
    return fs->data_start + (cluster - 2) * fs->bpb.sectors_per_cluster;
}

static uint32_t get_cluster_entry(struct fat32_fs *fs, uint32_t cluster)
{
    /* Try cache first */
    if (fs->fat_cache && cluster < fs->fat_cache_size) {
        return fs->fat_cache[cluster] & FAT_ENTRY_MASK;
    }

    /* Fall back to reading from disk */
    uint32_t fat_offset = cluster * 4;  /* 4 bytes per FAT32 entry */
    uint32_t fat_sector = fs->fat_start + (fat_offset / fs->bpb.bytes_per_sector);
    uint32_t entry_offset = fat_offset % fs->bpb.bytes_per_sector;

    uint8_t sector_buf[512];  /* Stack buffer for single sector */
    if (fs->read_sectors(fat_sector, 1, sector_buf) < 0) {
        return FAT_ENTRY_EOC;  /* Return EOC on error */
    }

    uint32_t entry = *((uint32_t *)(sector_buf + entry_offset));
    return entry & FAT_ENTRY_MASK;
}

static int read_cluster(struct fat32_fs *fs, uint32_t cluster, void *buffer)
{
    if (cluster < 2) {
        return -1;
    }

    uint32_t sector = cluster_to_sector(fs, cluster);
    return fs->read_sectors(sector, fs->bpb.sectors_per_cluster, buffer);
}

static int write_cluster(struct fat32_fs *fs, uint32_t cluster, const void *buffer)
{
    if (cluster < 2) {
        return -1;
    }

    uint32_t sector = cluster_to_sector(fs, cluster);
    return fs->write_sectors(sector, fs->bpb.sectors_per_cluster, buffer);
}

static uint32_t next_cluster(struct fat32_fs *fs, uint32_t cluster)
{
    uint32_t entry = get_cluster_entry(fs, cluster);

    if (entry >= FAT_ENTRY_EOC) {
        return 0;   /* End of chain */
    }
    if (entry == FAT_ENTRY_FREE || entry == FAT_ENTRY_BAD) {
        return 0;   /* Invalid */
    }

    return entry;
}

/* Set a FAT entry (update both cache and disk) */
static int set_cluster_entry(struct fat32_fs *fs, uint32_t cluster, uint32_t value)
{
    if (cluster < 2 || cluster >= fs->total_clusters + 2) {
        return -1;
    }

    /* Update cache */
    if (fs->fat_cache && cluster < fs->fat_cache_size) {
        fs->fat_cache[cluster] = value;
    }

    /* Calculate sector and offset within FAT */
    uint32_t fat_offset = cluster * 4;  /* 4 bytes per entry in FAT32 */
    uint32_t fat_sector = fs->fat_start + (fat_offset / fs->bpb.bytes_per_sector);
    uint32_t entry_offset = fat_offset % fs->bpb.bytes_per_sector;

    /* Read FAT sector */
    uint8_t *sector_buf = kmalloc(fs->bpb.bytes_per_sector);
    if (!sector_buf) {
        return -1;
    }

    if (fs->read_sectors(fat_sector, 1, sector_buf) < 0) {
        kfree(sector_buf);
        return -1;
    }

    /* Update entry (preserve upper 4 bits) */
    uint32_t *entry_ptr = (uint32_t *)(sector_buf + entry_offset);
    *entry_ptr = (*entry_ptr & 0xF0000000) | (value & FAT_ENTRY_MASK);

    /* Write back to all FAT copies */
    for (int i = 0; i < fs->bpb.num_fats; i++) {
        uint32_t fat_copy_sector = fat_sector + i * fs->bpb.fat_size_32;
        if (fs->write_sectors(fat_copy_sector, 1, sector_buf) < 0) {
            kfree(sector_buf);
            return -1;
        }
    }

    kfree(sector_buf);
    return 0;
}

/* Allocate a free cluster */
static uint32_t allocate_cluster(struct fat32_fs *fs)
{
    /* Search for a free cluster */
    uint32_t search_limit = fs->total_clusters + 2;
    uint32_t checked = 0;

    for (uint32_t cluster = 2; cluster < search_limit; cluster++) {
        uint32_t entry = get_cluster_entry(fs, cluster);

        /* Debug: print first few entries being checked */
        if (checked < 5) {
            printk(KERN_DEBUG "FAT32: Cluster %u entry=0x%08x\n", cluster, entry);
        }
        checked++;

        if (entry == FAT_ENTRY_FREE) {
            /* Mark as end of chain */
            if (set_cluster_entry(fs, cluster, FAT_ENTRY_EOC | 0x0FFFFFF8) == 0) {
                printk(KERN_DEBUG "FAT32: Allocated cluster %u\n", cluster);
                return cluster;
            }
            printk(KERN_ERR "FAT32: Failed to mark cluster %u\n", cluster);
            return 0;  /* Failed to mark */
        }
    }

    printk(KERN_ERR "FAT32: No free clusters available (checked %u clusters, limit=%u)\n",
           checked, search_limit);
    return 0;  /* No free clusters */
}

/* Zero out a cluster */
static int zero_cluster(struct fat32_fs *fs, uint32_t cluster)
{
    uint8_t *buf = kzalloc(fs->cluster_size, GFP_KERNEL);
    if (!buf) {
        return -1;
    }

    int ret = write_cluster(fs, cluster, buf);
    kfree(buf);
    return ret;
}

/* Find a free directory entry slot in a directory */
static int find_free_dir_entry(struct fat32_fs *fs, uint32_t dir_cluster,
                                uint32_t *out_cluster, int *out_index)
{
    uint8_t *cluster_buf = kmalloc(fs->cluster_size);
    if (!cluster_buf) {
        return -1;
    }

    uint32_t cluster = dir_cluster;
    uint32_t prev_cluster = 0;

    while (cluster != 0 && cluster < FAT_ENTRY_EOC) {
        if (read_cluster(fs, cluster, cluster_buf) < 0) {
            kfree(cluster_buf);
            return -1;
        }

        struct fat32_dir_entry *entries = (struct fat32_dir_entry *)cluster_buf;
        int num_entries = fs->cluster_size / FAT_DIR_ENTRY_SIZE;

        for (int i = 0; i < num_entries; i++) {
            /* Check for free or deleted entry */
            if (entries[i].name[0] == 0x00 || (uint8_t)entries[i].name[0] == 0xE5) {
                *out_cluster = cluster;
                *out_index = i;
                kfree(cluster_buf);
                return 0;
            }
        }

        prev_cluster = cluster;
        cluster = next_cluster(fs, cluster);
    }

    /* No free entry found - need to allocate a new cluster */
    kfree(cluster_buf);

    uint32_t new_cluster = allocate_cluster(fs);
    if (new_cluster == 0) {
        return -1;
    }

    /* Link to previous cluster */
    if (set_cluster_entry(fs, prev_cluster, new_cluster) < 0) {
        return -1;
    }

    /* Zero the new cluster */
    if (zero_cluster(fs, new_cluster) < 0) {
        return -1;
    }

    *out_cluster = new_cluster;
    *out_index = 0;
    return 0;
}

/* Add a directory entry to a directory */
static int add_dir_entry(struct fat32_fs *fs, uint32_t dir_cluster,
                          const char *name, uint8_t attr, uint32_t file_cluster,
                          uint32_t file_size, uint32_t *out_cluster,
                          uint32_t *out_offset)
{
    uint32_t entry_cluster;
    int entry_index;

    if (find_free_dir_entry(fs, dir_cluster, &entry_cluster, &entry_index) < 0) {
        return -1;
    }

    /* Read the cluster containing the entry */
    uint8_t *cluster_buf = kmalloc(fs->cluster_size);
    if (!cluster_buf) {
        return -1;
    }

    if (read_cluster(fs, entry_cluster, cluster_buf) < 0) {
        kfree(cluster_buf);
        return -1;
    }

    /* Create the directory entry */
    struct fat32_dir_entry *entries = (struct fat32_dir_entry *)cluster_buf;
    struct fat32_dir_entry *entry = &entries[entry_index];

    /* Clear entry */
    for (size_t i = 0; i < sizeof(struct fat32_dir_entry); i++) {
        ((uint8_t *)entry)[i] = 0;
    }

    /* Set 8.3 name */
    string_to_fat_name(name, entry->name, entry->ext);

    /* Set attributes */
    entry->attr = attr;
    entry->nt_reserved = 0;

    /* Set cluster */
    entry->cluster_high = (file_cluster >> 16) & 0xFFFF;
    entry->cluster_low = file_cluster & 0xFFFF;

    /* Set size (0 for directories) */
    entry->file_size = (attr & FAT_ATTR_DIRECTORY) ? 0 : file_size;

    /* Set timestamps (use a fixed date for simplicity: 2024-01-01 12:00:00) */
    /* Time: hours=12, minutes=0, seconds=0 -> (12<<11)|(0<<5)|0 = 0x6000 */
    /* Date: year=2024-1980=44, month=1, day=1 -> (44<<9)|(1<<5)|1 = 0x5821 */
    entry->create_time = 0x6000;
    entry->create_date = 0x5821;
    entry->modify_time = 0x6000;
    entry->modify_date = 0x5821;
    entry->access_date = 0x5821;

    /* Write back */
    if (write_cluster(fs, entry_cluster, cluster_buf) < 0) {
        kfree(cluster_buf);
        return -1;
    }

    /* Return entry location */
    if (out_cluster) *out_cluster = entry_cluster;
    if (out_offset) *out_offset = entry_index * FAT_DIR_ENTRY_SIZE;

    kfree(cluster_buf);
    return 0;
}

/* Update file size in directory entry on disk */
static int update_dir_entry_size(struct fat32_file *ff)
{
    struct fat32_fs *fs = ff->fs;
    if (!fs || ff->dir_cluster == 0) {
        return -1;  /* No directory entry location stored */
    }

    /* Read the cluster containing the directory entry */
    uint8_t *cluster_buf = kmalloc(fs->cluster_size);
    if (!cluster_buf) {
        return -1;
    }

    if (read_cluster(fs, ff->dir_cluster, cluster_buf) < 0) {
        kfree(cluster_buf);
        return -1;
    }

    /* Get the directory entry */
    struct fat32_dir_entry *entry = (struct fat32_dir_entry *)
        (cluster_buf + ff->dir_entry_offset);

    /* Update the file size */
    entry->file_size = ff->file_size;

    /* Update modification timestamp */
    entry->modify_time = 0x6000;  /* 12:00:00 */
    entry->modify_date = 0x5821;  /* 2024-01-01 */

    /* Write back to disk */
    if (write_cluster(fs, ff->dir_cluster, cluster_buf) < 0) {
        kfree(cluster_buf);
        return -1;
    }

    printk("FAT32: Updated dir entry size to %u bytes\n", ff->file_size);
    kfree(cluster_buf);
    return 0;
}

/* Convert 8.3 name to readable format */
static void fat_name_to_string(const char *name, const char *ext, char *out)
{
    int i, j = 0;

    /* Copy name, trimming trailing spaces */
    for (i = 0; i < 8 && name[i] != ' '; i++) {
        out[j++] = (name[i] >= 'A' && name[i] <= 'Z') ?
                   name[i] + 32 : name[i];  /* Lowercase */
    }

    /* Add extension if present */
    if (ext[0] != ' ') {
        out[j++] = '.';
        for (i = 0; i < 3 && ext[i] != ' '; i++) {
            out[j++] = (ext[i] >= 'A' && ext[i] <= 'Z') ?
                       ext[i] + 32 : ext[i];
        }
    }

    out[j] = '\0';
}

/* Convert string to 8.3 format */
static void string_to_fat_name(const char *str, char *name, char *ext)
{
    int i = 0, j = 0;

    /* Initialize with spaces */
    for (i = 0; i < 8; i++) name[i] = ' ';
    for (i = 0; i < 3; i++) ext[i] = ' ';

    i = 0;

    /* Copy name part */
    while (str[i] && str[i] != '.' && j < 8) {
        char c = str[i++];
        if (c >= 'a' && c <= 'z') c -= 32;  /* Uppercase */
        name[j++] = c;
    }

    /* Skip dot */
    if (str[i] == '.') i++;

    /* Copy extension */
    j = 0;
    while (str[i] && j < 3) {
        char c = str[i++];
        if (c >= 'a' && c <= 'z') c -= 32;
        ext[j++] = c;
    }
}

/* ===================================================================== */
/* Directory Operations */
/* ===================================================================== */

static int fat32_find_entry(struct fat32_fs *fs, uint32_t dir_cluster,
                             const char *name, struct fat32_dir_entry *entry,
                             uint32_t *out_cluster, uint32_t *out_offset)
{
    uint8_t *cluster_buf = kmalloc(fs->cluster_size);
    if (!cluster_buf) {
        return -1;
    }

    char target_name[12], target_ext[4];
    string_to_fat_name(name, target_name, target_ext);

    uint32_t cluster = dir_cluster;

    while (cluster != 0 && cluster < FAT_ENTRY_EOC) {
        if (read_cluster(fs, cluster, cluster_buf) < 0) {
            kfree(cluster_buf);
            return -1;
        }

        struct fat32_dir_entry *entries = (struct fat32_dir_entry *)cluster_buf;
        int num_entries = fs->cluster_size / FAT_DIR_ENTRY_SIZE;

        for (int i = 0; i < num_entries; i++) {
            /* Skip free entries */
            if (entries[i].name[0] == 0x00) {
                /* No more entries */
                kfree(cluster_buf);
                return -1;
            }
            if ((uint8_t)entries[i].name[0] == 0xE5) {
                /* Deleted entry */
                continue;
            }
            if (entries[i].attr == FAT_ATTR_LONG_NAME) {
                /* Long filename entry - skip for now */
                continue;
            }
            if (entries[i].attr & FAT_ATTR_VOLUME_ID) {
                continue;
            }

            /* Compare names */
            int match = 1;
            for (int j = 0; j < 8; j++) {
                if (entries[i].name[j] != target_name[j]) {
                    match = 0;
                    break;
                }
            }
            if (match) {
                for (int j = 0; j < 3; j++) {
                    if (entries[i].ext[j] != target_ext[j]) {
                        match = 0;
                        break;
                    }
                }
            }

            if (match) {
                /* Found it */
                for (size_t j = 0; j < sizeof(struct fat32_dir_entry); j++) {
                    ((uint8_t *)entry)[j] = ((uint8_t *)&entries[i])[j];
                }
                /* Return directory entry location */
                if (out_cluster) *out_cluster = cluster;
                if (out_offset) *out_offset = i * FAT_DIR_ENTRY_SIZE;
                kfree(cluster_buf);
                return 0;
            }
        }

        cluster = next_cluster(fs, cluster);
    }

    kfree(cluster_buf);
    return -1;  /* Not found */
}

static int fat32_list_dir(struct fat32_fs *fs, uint32_t dir_cluster,
                           void *ctx,
                           int (*callback)(void *ctx, const char *name,
                                           uint8_t attr, uint32_t size,
                                           uint32_t cluster))
{
    uint8_t *cluster_buf = kmalloc(fs->cluster_size);
    if (!cluster_buf) {
        return -1;
    }

    uint32_t cluster = dir_cluster;
    int count = 0;

    while (cluster != 0 && cluster < FAT_ENTRY_EOC) {
        if (read_cluster(fs, cluster, cluster_buf) < 0) {
            break;
        }

        struct fat32_dir_entry *entries = (struct fat32_dir_entry *)cluster_buf;
        int num_entries = fs->cluster_size / FAT_DIR_ENTRY_SIZE;

        for (int i = 0; i < num_entries; i++) {
            if (entries[i].name[0] == 0x00) {
                goto done;
            }
            if ((uint8_t)entries[i].name[0] == 0xE5) {
                continue;
            }
            if (entries[i].attr == FAT_ATTR_LONG_NAME) {
                continue;
            }
            if (entries[i].attr & FAT_ATTR_VOLUME_ID) {
                continue;
            }

            /* Convert name */
            char name[13];
            fat_name_to_string(entries[i].name, entries[i].ext, name);

            uint32_t entry_cluster = ((uint32_t)entries[i].cluster_high << 16) |
                                     entries[i].cluster_low;

            if (callback) {
                callback(ctx, name, entries[i].attr, entries[i].file_size,
                         entry_cluster);
            }
            count++;
        }

        cluster = next_cluster(fs, cluster);
    }

done:
    kfree(cluster_buf);
    return count;
}

/* ===================================================================== */
/* File Operations */
/* ===================================================================== */

static ssize_t fat32_file_read(struct file *file, char *buf, size_t count,
                                loff_t *pos)
{
    struct fat32_file *ff = (struct fat32_file *)file->private_data;
    if (!ff || !ff->fs) {
        return -1;
    }

    if (*pos >= ff->file_size) {
        return 0;
    }

    if (*pos + count > ff->file_size) {
        count = ff->file_size - *pos;
    }

    struct fat32_fs *fs = ff->fs;
    size_t bytes_read = 0;
    uint8_t *cluster_buf = kmalloc(fs->cluster_size);
    if (!cluster_buf) {
        return -1;
    }

    /* Navigate to the correct cluster */
    uint32_t cluster_index = *pos / fs->cluster_size;
    uint32_t cluster_offset = *pos % fs->cluster_size;

    uint32_t cluster = ff->start_cluster;
    for (uint32_t i = 0; i < cluster_index && cluster != 0; i++) {
        cluster = next_cluster(fs, cluster);
    }

    while (bytes_read < count && cluster != 0 && cluster < FAT_ENTRY_EOC) {
        if (read_cluster(fs, cluster, cluster_buf) < 0) {
            break;
        }

        size_t to_read = fs->cluster_size - cluster_offset;
        if (to_read > count - bytes_read) {
            to_read = count - bytes_read;
        }

        for (size_t i = 0; i < to_read; i++) {
            buf[bytes_read + i] = cluster_buf[cluster_offset + i];
        }

        bytes_read += to_read;
        cluster_offset = 0;
        cluster = next_cluster(fs, cluster);
    }

    kfree(cluster_buf);
    *pos += bytes_read;

    return bytes_read;
}

static ssize_t fat32_file_write(struct file *file, const char *buf,
                                 size_t count, loff_t *pos)
{
    struct fat32_file *ff = (struct fat32_file *)file->private_data;
    if (!ff || !ff->fs) {
        return -1;
    }

    struct fat32_fs *fs = ff->fs;
    size_t bytes_written = 0;
    uint8_t *cluster_buf = kmalloc(fs->cluster_size);
    if (!cluster_buf) {
        return -1;
    }

    /* Navigate to the correct cluster */
    uint32_t cluster_index = *pos / fs->cluster_size;
    uint32_t cluster_offset = *pos % fs->cluster_size;

    uint32_t cluster = ff->start_cluster;

    for (uint32_t i = 0; i < cluster_index && cluster != 0; i++) {
        cluster = next_cluster(fs, cluster);
    }

    /* TODO: Allocate new clusters if needed */
    /* For now, only support writing within existing file bounds */

    while (bytes_written < count && cluster != 0 && cluster < FAT_ENTRY_EOC) {
        /* Read existing cluster data */
        if (cluster_offset > 0 ||
            (count - bytes_written) < fs->cluster_size) {
            if (read_cluster(fs, cluster, cluster_buf) < 0) {
                break;
            }
        }

        size_t to_write = fs->cluster_size - cluster_offset;
        if (to_write > count - bytes_written) {
            to_write = count - bytes_written;
        }

        for (size_t i = 0; i < to_write; i++) {
            cluster_buf[cluster_offset + i] = buf[bytes_written + i];
        }

        if (write_cluster(fs, cluster, cluster_buf) < 0) {
            break;
        }

        bytes_written += to_write;
        cluster_offset = 0;
        cluster = next_cluster(fs, cluster);
    }

    kfree(cluster_buf);
    *pos += bytes_written;

    /* Update file size if extended */
    if (*pos > ff->file_size) {
        ff->file_size = *pos;
        /* Update directory entry on disk */
        update_dir_entry_size(ff);
    }

    return bytes_written;
}

static int fat32_file_open(struct inode *vfs_inode, struct file *file)
{
    file->private_data = vfs_inode->i_private;

    /* Handle O_TRUNC - reset file size to 0 */
    if (file->f_flags & O_TRUNC) {
        struct fat32_file *ff = (struct fat32_file *)file->private_data;
        if (ff) {
            ff->file_size = 0;
            ff->position = 0;
            ff->current_cluster = ff->start_cluster;
            ff->cluster_offset = 0;
            /* Update directory entry on disk */
            update_dir_entry_size(ff);
            printk(KERN_DEBUG "FAT32: Truncated file to 0 bytes\n");
        }
    }

    return 0;
}

static int fat32_file_release(struct inode *vfs_inode, struct file *file)
{
    (void)vfs_inode;
    /* Don't free private_data here - it's managed by the inode */
    file->private_data = NULL;
    return 0;
}

static const struct file_operations fat32_file_ops = {
    .read = fat32_file_read,
    .write = fat32_file_write,
    .open = fat32_file_open,
    .release = fat32_file_release,
    .llseek = NULL,
    .readdir = NULL,
    .ioctl = NULL,
    .mmap = NULL,
};

/* ===================================================================== */
/* Directory Operations (VFS integration) */
/* ===================================================================== */

struct fat32_readdir_ctx {
    void *user_ctx;
    int (*filldir)(void *, const char *, int, loff_t, ino_t, unsigned);
    loff_t pos;
};

static int fat32_readdir_callback(void *ctx, const char *name,
                                   uint8_t attr, uint32_t size,
                                   uint32_t cluster)
{
    struct fat32_readdir_ctx *rctx = (struct fat32_readdir_ctx *)ctx;
    (void)size;

    unsigned type = (attr & FAT_ATTR_DIRECTORY) ? (S_IFDIR >> 12) : (S_IFREG >> 12);
    int name_len = 0;
    while (name[name_len]) name_len++;

    if (rctx->filldir) {
        rctx->filldir(rctx->user_ctx, name, name_len, rctx->pos++,
                      cluster, type);
    }

    return 0;
}

static int fat32_dir_readdir(struct file *file, void *ctx,
                              int (*filldir)(void *, const char *, int,
                                             loff_t, ino_t, unsigned))
{
    struct fat32_file *ff = (struct fat32_file *)file->private_data;
    if (!ff || !ff->fs) {
        return -1;
    }

    struct fat32_readdir_ctx rctx = {
        .user_ctx = ctx,
        .filldir = filldir,
        .pos = 0
    };

    /* Add . and .. entries */
    if (filldir) {
        filldir(ctx, ".", 1, rctx.pos++, ff->start_cluster, S_IFDIR >> 12);
        filldir(ctx, "..", 2, rctx.pos++, ff->start_cluster, S_IFDIR >> 12);
    }

    return fat32_list_dir(ff->fs, ff->start_cluster, &rctx,
                           fat32_readdir_callback);
}

static const struct file_operations fat32_dir_ops = {
    .read = NULL,
    .write = NULL,
    .open = fat32_file_open,
    .release = fat32_file_release,
    .llseek = NULL,
    .readdir = fat32_dir_readdir,
    .ioctl = NULL,
    .mmap = NULL,
};

/* ===================================================================== */
/* Inode Operations */
/* ===================================================================== */

static struct inode_operations fat32_inode_ops;

static struct dentry *fat32_lookup(struct inode *dir, struct dentry *dentry)
{
    struct fat32_file *dir_file = (struct fat32_file *)dir->i_private;
    if (!dir_file || !dir_file->fs) {
        return NULL;
    }

    struct fat32_dir_entry entry;
    uint32_t entry_cluster = 0, entry_offset = 0;
    if (fat32_find_entry(dir_file->fs, dir_file->start_cluster,
                          dentry->d_name, &entry,
                          &entry_cluster, &entry_offset) < 0) {
        return NULL;
    }

    /* Create fat32_file for this entry */
    struct fat32_file *ff = kzalloc(sizeof(struct fat32_file), GFP_KERNEL);
    if (!ff) {
        return NULL;
    }

    ff->fs = dir_file->fs;
    ff->start_cluster = ((uint32_t)entry.cluster_high << 16) | entry.cluster_low;
    ff->current_cluster = ff->start_cluster;
    ff->file_size = entry.file_size;
    ff->attr = entry.attr;
    ff->position = 0;
    ff->cluster_offset = 0;
    ff->dir_cluster = entry_cluster;
    ff->dir_entry_offset = entry_offset;

    /* Copy name */
    int i;
    for (i = 0; i < NAME_MAX && dentry->d_name[i]; i++) {
        ff->name[i] = dentry->d_name[i];
    }
    ff->name[i] = '\0';

    /* Create VFS inode */
    struct inode *inode = kzalloc(sizeof(struct inode), GFP_KERNEL);
    if (!inode) {
        kfree(ff);
        return NULL;
    }

    inode->i_ino = ff->start_cluster;
    inode->i_mode = (entry.attr & FAT_ATTR_DIRECTORY) ?
                    (S_IFDIR | 0755) : (S_IFREG | 0644);
    if (entry.attr & FAT_ATTR_READ_ONLY) {
        inode->i_mode &= ~0222;  /* Remove write permissions */
    }
    inode->i_size = entry.file_size;
    inode->i_sb = dir->i_sb;
    inode->i_op = &fat32_inode_ops;
    inode->i_fop = (entry.attr & FAT_ATTR_DIRECTORY) ?
                   &fat32_dir_ops : &fat32_file_ops;
    inode->i_private = ff;

    dentry->d_inode = inode;

    return NULL;  /* Success - dentry populated */
}

static int fat32_create(struct inode *dir, struct dentry *dentry, mode_t mode)
{
    struct fat32_file *dir_file = (struct fat32_file *)dir->i_private;
    if (!dir_file || !dir_file->fs) {
        return -EINVAL;
    }

    struct fat32_fs *fs = dir_file->fs;
    (void)mode;  /* FAT32 doesn't use Unix permissions */

    /* Check if file already exists */
    struct fat32_dir_entry existing;
    if (fat32_find_entry(fs, dir_file->start_cluster, dentry->d_name, &existing, NULL, NULL) == 0) {
        return -EEXIST;
    }

    /* Allocate a cluster for the file (even empty files get one cluster) */
    uint32_t file_cluster = allocate_cluster(fs);
    if (file_cluster == 0) {
        return -ENOSPC;
    }

    /* Zero the cluster */
    if (zero_cluster(fs, file_cluster) < 0) {
        /* TODO: Free the allocated cluster on error */
        return -EIO;
    }

    /* Add directory entry */
    uint32_t entry_cluster = 0, entry_offset = 0;
    if (add_dir_entry(fs, dir_file->start_cluster, dentry->d_name,
                       FAT_ATTR_ARCHIVE, file_cluster, 0,
                       &entry_cluster, &entry_offset) < 0) {
        /* TODO: Free the allocated cluster on error */
        return -EIO;
    }

    /* Create fat32_file structure for the new file */
    struct fat32_file *ff = kzalloc(sizeof(struct fat32_file), GFP_KERNEL);
    if (!ff) {
        return -ENOMEM;
    }

    ff->fs = fs;
    ff->start_cluster = file_cluster;
    ff->current_cluster = file_cluster;
    ff->file_size = 0;
    ff->attr = FAT_ATTR_ARCHIVE;
    ff->position = 0;
    ff->cluster_offset = 0;
    ff->dir_cluster = entry_cluster;
    ff->dir_entry_offset = entry_offset;

    /* Copy name */
    int i;
    for (i = 0; i < NAME_MAX && dentry->d_name[i]; i++) {
        ff->name[i] = dentry->d_name[i];
    }
    ff->name[i] = '\0';

    /* Create VFS inode */
    struct inode *inode = kzalloc(sizeof(struct inode), GFP_KERNEL);
    if (!inode) {
        kfree(ff);
        return -ENOMEM;
    }

    inode->i_ino = file_cluster;
    inode->i_mode = S_IFREG | 0644;
    inode->i_size = 0;
    inode->i_sb = dir->i_sb;
    inode->i_op = &fat32_inode_ops;
    inode->i_fop = &fat32_file_ops;
    inode->i_private = ff;

    dentry->d_inode = inode;

    printk(KERN_INFO "FAT32: Created file '%s' at cluster %u\n",
           dentry->d_name, file_cluster);

    return 0;
}

static int fat32_mkdir(struct inode *dir, struct dentry *dentry, mode_t mode)
{
    struct fat32_file *dir_file = (struct fat32_file *)dir->i_private;
    if (!dir_file || !dir_file->fs) {
        return -EINVAL;
    }

    struct fat32_fs *fs = dir_file->fs;
    (void)mode;  /* FAT32 doesn't use Unix permissions */

    /* Check if directory already exists */
    struct fat32_dir_entry existing;
    if (fat32_find_entry(fs, dir_file->start_cluster, dentry->d_name, &existing, NULL, NULL) == 0) {
        return -EEXIST;
    }

    /* Allocate a cluster for the new directory */
    uint32_t new_cluster = allocate_cluster(fs);
    if (new_cluster == 0) {
        return -ENOSPC;
    }

    /* Zero the cluster */
    if (zero_cluster(fs, new_cluster) < 0) {
        return -EIO;
    }

    /* Create . and .. entries in the new directory */
    uint8_t *cluster_buf = kzalloc(fs->cluster_size, GFP_KERNEL);
    if (!cluster_buf) {
        return -ENOMEM;
    }

    struct fat32_dir_entry *entries = (struct fat32_dir_entry *)cluster_buf;

    /* "." entry - points to self */
    for (int i = 0; i < 8; i++) entries[0].name[i] = ' ';
    entries[0].name[0] = '.';
    for (int i = 0; i < 3; i++) entries[0].ext[i] = ' ';
    entries[0].attr = FAT_ATTR_DIRECTORY;
    entries[0].cluster_high = (new_cluster >> 16) & 0xFFFF;
    entries[0].cluster_low = new_cluster & 0xFFFF;
    entries[0].file_size = 0;
    entries[0].create_time = 0x6000;
    entries[0].create_date = 0x5821;
    entries[0].modify_time = 0x6000;
    entries[0].modify_date = 0x5821;

    /* ".." entry - points to parent */
    for (int i = 0; i < 8; i++) entries[1].name[i] = ' ';
    entries[1].name[0] = '.';
    entries[1].name[1] = '.';
    for (int i = 0; i < 3; i++) entries[1].ext[i] = ' ';
    entries[1].attr = FAT_ATTR_DIRECTORY;
    uint32_t parent_cluster = dir_file->start_cluster;
    /* Root directory has cluster 0 for ".." in FAT32 */
    if (parent_cluster == fs->root_cluster) {
        parent_cluster = 0;
    }
    entries[1].cluster_high = (parent_cluster >> 16) & 0xFFFF;
    entries[1].cluster_low = parent_cluster & 0xFFFF;
    entries[1].file_size = 0;
    entries[1].create_time = 0x6000;
    entries[1].create_date = 0x5821;
    entries[1].modify_time = 0x6000;
    entries[1].modify_date = 0x5821;

    /* Write the new directory cluster */
    if (write_cluster(fs, new_cluster, cluster_buf) < 0) {
        kfree(cluster_buf);
        return -EIO;
    }
    kfree(cluster_buf);

    /* Add entry in parent directory */
    if (add_dir_entry(fs, dir_file->start_cluster, dentry->d_name,
                       FAT_ATTR_DIRECTORY, new_cluster, 0, NULL, NULL) < 0) {
        return -EIO;
    }

    /* Create fat32_file structure for the new directory */
    struct fat32_file *ff = kzalloc(sizeof(struct fat32_file), GFP_KERNEL);
    if (!ff) {
        return -ENOMEM;
    }

    ff->fs = fs;
    ff->start_cluster = new_cluster;
    ff->current_cluster = new_cluster;
    ff->file_size = 0;
    ff->attr = FAT_ATTR_DIRECTORY;
    ff->position = 0;
    ff->cluster_offset = 0;

    /* Copy name */
    int i;
    for (i = 0; i < NAME_MAX && dentry->d_name[i]; i++) {
        ff->name[i] = dentry->d_name[i];
    }
    ff->name[i] = '\0';

    /* Create VFS inode */
    struct inode *inode = kzalloc(sizeof(struct inode), GFP_KERNEL);
    if (!inode) {
        kfree(ff);
        return -ENOMEM;
    }

    inode->i_ino = new_cluster;
    inode->i_mode = S_IFDIR | 0755;
    inode->i_size = 0;
    inode->i_sb = dir->i_sb;
    inode->i_op = &fat32_inode_ops;
    inode->i_fop = &fat32_dir_ops;
    inode->i_private = ff;

    dentry->d_inode = inode;

    printk(KERN_INFO "FAT32: Created directory '%s' at cluster %u\n",
           dentry->d_name, new_cluster);

    return 0;
}

static struct inode_operations fat32_inode_ops = {
    .lookup = fat32_lookup,
    .create = fat32_create,
    .mkdir = fat32_mkdir,
    .rmdir = NULL,
    .unlink = NULL,
    .rename = NULL,
};

/* ===================================================================== */
/* Mount Operations */
/* ===================================================================== */

static int fat32_read_sectors_wrapper(uint64_t sector, uint32_t count, void *buf)
{
    return virtio_blk_read(sector, count, buf);
}

static int fat32_write_sectors_wrapper(uint64_t sector, uint32_t count,
                                        const void *buf)
{
    return virtio_blk_write(sector, count, buf);
}

static struct super_block *fat32_mount_internal(struct file_system_type *fs_type,
                                                 int flags, const char *dev_name,
                                                 void *data)
{
    (void)flags;
    (void)data;

    printk(KERN_INFO "FAT32: Mounting filesystem from %s\n", dev_name);

    /* Check if virtio-blk is available */
    if (!virtio_blk_is_ready()) {
        printk(KERN_ERR "FAT32: Block device not available\n");
        return NULL;
    }

    /* Allocate filesystem structure */
    struct fat32_fs *fs = kzalloc(sizeof(struct fat32_fs), GFP_KERNEL);
    if (!fs) {
        return NULL;
    }

    fs->read_sectors = fat32_read_sectors_wrapper;
    fs->write_sectors = fat32_write_sectors_wrapper;

    /* Read boot sector */
    uint8_t boot_sector[512];
    if (virtio_blk_read(0, 1, boot_sector) < 0) {
        printk(KERN_ERR "FAT32: Failed to read boot sector\n");
        kfree(fs);
        return NULL;
    }

    /* Copy BPB */
    for (size_t i = 0; i < sizeof(struct fat32_bpb); i++) {
        ((uint8_t *)&fs->bpb)[i] = boot_sector[i];
    }

    /* Verify signature */
    uint16_t signature = boot_sector[510] | (boot_sector[511] << 8);
    if (signature != FAT32_SIGNATURE) {
        printk(KERN_ERR "FAT32: Invalid boot sector signature\n");
        kfree(fs);
        return NULL;
    }

    /* Verify this is FAT32 */
    if (fs->bpb.fat_size_16 != 0) {
        printk(KERN_ERR "FAT32: Not a FAT32 filesystem\n");
        kfree(fs);
        return NULL;
    }

    /* Calculate filesystem parameters */
    fs->fat_start = fs->bpb.reserved_sectors;
    fs->data_start = fs->fat_start + fs->bpb.num_fats * fs->bpb.fat_size_32;
    fs->root_cluster = fs->bpb.root_cluster;
    fs->cluster_size = fs->bpb.bytes_per_sector * fs->bpb.sectors_per_cluster;

    uint32_t total_sectors = fs->bpb.total_sectors_32;
    uint32_t data_sectors = total_sectors - fs->data_start;
    fs->total_clusters = data_sectors / fs->bpb.sectors_per_cluster;

    printk(KERN_INFO "FAT32: Bytes/sector: %d\n", fs->bpb.bytes_per_sector);
    printk(KERN_INFO "FAT32: Sectors/cluster: %d\n", fs->bpb.sectors_per_cluster);
    printk(KERN_INFO "FAT32: Cluster size: %d bytes\n", fs->cluster_size);
    printk(KERN_INFO "FAT32: Total clusters: %d\n", fs->total_clusters);
    printk(KERN_INFO "FAT32: Root cluster: %d\n", fs->root_cluster);

    /* Load FAT into memory (for small disks) */
    /* For large disks, use demand-loading */
    uint32_t fat_sectors = fs->bpb.fat_size_32;
    uint32_t fat_bytes = fat_sectors * fs->bpb.bytes_per_sector;

    printk(KERN_INFO "FAT32: FAT size: %u sectors (%u bytes)\n",
           fat_sectors, fat_bytes);

    if (fat_bytes < 4 * 1024 * 1024) {  /* < 4MB FAT */
        fs->fat_cache = kmalloc(fat_bytes);
        if (fs->fat_cache) {
            fs->fat_cache_size = fat_bytes / 4;

            /* Read FAT in chunks to avoid issues with large reads */
            int success = 1;
            uint8_t *cache_ptr = (uint8_t *)fs->fat_cache;
            for (uint32_t s = 0; s < fat_sectors; s++) {
                if (virtio_blk_read(fs->fat_start + s, 1,
                                    cache_ptr + s * 512) < 0) {
                    success = 0;
                    break;
                }
            }

            if (!success) {
                printk(KERN_WARNING "FAT32: Failed to cache FAT\n");
                kfree(fs->fat_cache);
                fs->fat_cache = NULL;
                fs->fat_cache_size = 0;
            } else {
                printk(KERN_INFO "FAT32: FAT cached (%u entries)\n",
                       fs->fat_cache_size);
                /* Debug: show first few FAT entries */
                printk(KERN_DEBUG "FAT32: FAT[0]=0x%08x FAT[1]=0x%08x FAT[2]=0x%08x\n",
                       fs->fat_cache[0], fs->fat_cache[1], fs->fat_cache[2]);
            }
        } else {
            printk(KERN_WARNING "FAT32: Could not allocate FAT cache\n");
        }
    } else {
        printk(KERN_INFO "FAT32: FAT too large for caching, using on-demand reads\n");
    }

    /* Create superblock */
    static struct super_block sb;
    sb.s_blocksize = fs->cluster_size;
    sb.s_type = fs_type;
    sb.s_fs_info = fs;

    /* Create root file structure */
    struct fat32_file *root_file = kzalloc(sizeof(struct fat32_file), GFP_KERNEL);
    if (!root_file) {
        if (fs->fat_cache) kfree(fs->fat_cache);
        kfree(fs);
        return NULL;
    }

    root_file->fs = fs;
    root_file->start_cluster = fs->root_cluster;
    root_file->current_cluster = fs->root_cluster;
    root_file->file_size = 0;
    root_file->attr = FAT_ATTR_DIRECTORY;
    root_file->name[0] = '/';
    root_file->name[1] = '\0';

    /* Create VFS root inode */
    static struct inode vfs_root_inode;
    vfs_root_inode.i_ino = fs->root_cluster;
    vfs_root_inode.i_mode = S_IFDIR | 0755;
    vfs_root_inode.i_size = 0;
    vfs_root_inode.i_sb = &sb;
    vfs_root_inode.i_op = &fat32_inode_ops;
    vfs_root_inode.i_fop = &fat32_dir_ops;
    vfs_root_inode.i_private = root_file;

    /* Create root dentry */
    static struct dentry root_dentry;
    root_dentry.d_name[0] = '/';
    root_dentry.d_name[1] = '\0';
    root_dentry.d_inode = &vfs_root_inode;
    root_dentry.d_parent = &root_dentry;
    root_dentry.d_child = NULL;
    root_dentry.d_sibling = NULL;
    root_dentry.d_sb = &sb;

    sb.s_root = &root_dentry;
    mounted_fs = fs;

    printk(KERN_INFO "FAT32: Filesystem mounted successfully\n");

    return &sb;
}

static void fat32_kill_sb(struct super_block *sb)
{
    struct fat32_fs *fs = (struct fat32_fs *)sb->s_fs_info;
    if (fs) {
        if (fs->fat_cache) {
            kfree(fs->fat_cache);
        }
        kfree(fs);
    }
    mounted_fs = NULL;
    printk(KERN_INFO "FAT32: Filesystem unmounted\n");
}

/* ===================================================================== */
/* Filesystem Type */
/* ===================================================================== */

static struct file_system_type fat32_fs_type = {
    .name = "fat32",
    .fs_flags = 0,
    .mount = fat32_mount_internal,
    .kill_sb = fat32_kill_sb,
    .next = NULL,
};

/* ===================================================================== */
/* Initialization */
/* ===================================================================== */

int fat32_init(void)
{
    printk(KERN_INFO "FAT32: Registering FAT32 filesystem\n");
    return register_filesystem(&fat32_fs_type);
}

/* ===================================================================== */
/* Public API */
/* ===================================================================== */

struct fat32_fs *fat32_get_mounted(void)
{
    return mounted_fs;
}

int fat32_sync(void)
{
    if (!mounted_fs) {
        return -1;
    }

    /* Flush block device */
    return virtio_blk_flush();
}
