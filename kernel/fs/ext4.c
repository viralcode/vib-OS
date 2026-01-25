/*
 * vib-OS Kernel - ext4 Filesystem Driver
 * 
 * Read/write support for ext4 filesystem.
 */

#include "fs/vfs.h"
#include "printk.h"
#include "mm/kmalloc.h"
#include "types.h"

/* ===================================================================== */
/* ext4 Constants */
/* ===================================================================== */

#define EXT4_SUPER_MAGIC    0xEF53
#define EXT4_SUPERBLOCK_OFFSET  1024
#define EXT4_BLOCK_SIZE_MIN     1024
#define EXT4_BLOCK_SIZE_MAX     65536

#define EXT4_GOOD_OLD_REV       0
#define EXT4_DYNAMIC_REV        1

#define EXT4_FEATURE_INCOMPAT_64BIT         0x0080
#define EXT4_FEATURE_INCOMPAT_EXTENTS       0x0040
#define EXT4_FEATURE_INCOMPAT_FLEX_BG       0x0200

#define EXT4_S_IFREG    0x8000
#define EXT4_S_IFDIR    0x4000
#define EXT4_S_IFLNK    0xA000

#define EXT4_NDIR_BLOCKS    12
#define EXT4_IND_BLOCK      EXT4_NDIR_BLOCKS
#define EXT4_DIND_BLOCK     (EXT4_IND_BLOCK + 1)
#define EXT4_TIND_BLOCK     (EXT4_DIND_BLOCK + 1)
#define EXT4_N_BLOCKS       (EXT4_TIND_BLOCK + 1)

/* ===================================================================== */
/* ext4 On-disk Structures */
/* ===================================================================== */

struct ext4_superblock {
    uint32_t s_inodes_count;
    uint32_t s_blocks_count_lo;
    uint32_t s_r_blocks_count_lo;
    uint32_t s_free_blocks_count_lo;
    uint32_t s_free_inodes_count;
    uint32_t s_first_data_block;
    uint32_t s_log_block_size;
    uint32_t s_log_cluster_size;
    uint32_t s_blocks_per_group;
    uint32_t s_clusters_per_group;
    uint32_t s_inodes_per_group;
    uint32_t s_mtime;
    uint32_t s_wtime;
    uint16_t s_mnt_count;
    uint16_t s_max_mnt_count;
    uint16_t s_magic;
    uint16_t s_state;
    uint16_t s_errors;
    uint16_t s_minor_rev_level;
    uint32_t s_lastcheck;
    uint32_t s_checkinterval;
    uint32_t s_creator_os;
    uint32_t s_rev_level;
    uint16_t s_def_resuid;
    uint16_t s_def_resgid;
    /* Dynamic rev */
    uint32_t s_first_ino;
    uint16_t s_inode_size;
    uint16_t s_block_group_nr;
    uint32_t s_feature_compat;
    uint32_t s_feature_incompat;
    uint32_t s_feature_ro_compat;
    uint8_t  s_uuid[16];
    char     s_volume_name[16];
    char     s_last_mounted[64];
    uint32_t s_algorithm_usage_bitmap;
    /* Performance hints */
    uint8_t  s_prealloc_blocks;
    uint8_t  s_prealloc_dir_blocks;
    uint16_t s_reserved_gdt_blocks;
    /* Journaling */
    uint8_t  s_journal_uuid[16];
    uint32_t s_journal_inum;
    uint32_t s_journal_dev;
    uint32_t s_last_orphan;
    uint32_t s_hash_seed[4];
    uint8_t  s_def_hash_version;
    uint8_t  s_jnl_backup_type;
    uint16_t s_desc_size;
    uint32_t s_default_mount_opts;
    uint32_t s_first_meta_bg;
    uint32_t s_mkfs_time;
    uint32_t s_jnl_blocks[17];
    /* 64-bit support */
    uint32_t s_blocks_count_hi;
    uint32_t s_r_blocks_count_hi;
    uint32_t s_free_blocks_count_hi;
    uint16_t s_min_extra_isize;
    uint16_t s_want_extra_isize;
    uint32_t s_flags;
    uint16_t s_raid_stride;
    uint16_t s_mmp_interval;
    uint64_t s_mmp_block;
    uint32_t s_raid_stripe_width;
    uint8_t  s_log_groups_per_flex;
    uint8_t  s_checksum_type;
    uint16_t s_reserved_pad;
    uint64_t s_kbytes_written;
    /* ... more fields */
} __attribute__((packed));

struct ext4_group_desc {
    uint32_t bg_block_bitmap_lo;
    uint32_t bg_inode_bitmap_lo;
    uint32_t bg_inode_table_lo;
    uint16_t bg_free_blocks_count_lo;
    uint16_t bg_free_inodes_count_lo;
    uint16_t bg_used_dirs_count_lo;
    uint16_t bg_flags;
    uint32_t bg_exclude_bitmap_lo;
    uint16_t bg_block_bitmap_csum_lo;
    uint16_t bg_inode_bitmap_csum_lo;
    uint16_t bg_itable_unused_lo;
    uint16_t bg_checksum;
    /* 64-bit support */
    uint32_t bg_block_bitmap_hi;
    uint32_t bg_inode_bitmap_hi;
    uint32_t bg_inode_table_hi;
    uint16_t bg_free_blocks_count_hi;
    uint16_t bg_free_inodes_count_hi;
    uint16_t bg_used_dirs_count_hi;
    uint16_t bg_itable_unused_hi;
    uint32_t bg_exclude_bitmap_hi;
    uint16_t bg_block_bitmap_csum_hi;
    uint16_t bg_inode_bitmap_csum_hi;
    uint32_t bg_reserved;
} __attribute__((packed));

struct ext4_inode {
    uint16_t i_mode;
    uint16_t i_uid;
    uint32_t i_size_lo;
    uint32_t i_atime;
    uint32_t i_ctime;
    uint32_t i_mtime;
    uint32_t i_dtime;
    uint16_t i_gid;
    uint16_t i_links_count;
    uint32_t i_blocks_lo;
    uint32_t i_flags;
    uint32_t i_osd1;
    uint32_t i_block[EXT4_N_BLOCKS];
    uint32_t i_generation;
    uint32_t i_file_acl_lo;
    uint32_t i_size_hi;
    uint32_t i_obso_faddr;
    /* ... more fields */
} __attribute__((packed));

struct ext4_dir_entry {
    uint32_t inode;
    uint16_t rec_len;
    uint8_t  name_len;
    uint8_t  file_type;
    char     name[255];
} __attribute__((packed));

/* ===================================================================== */
/* ext4 In-memory Structures */
/* ===================================================================== */

struct ext4_fs {
    struct ext4_superblock sb;
    uint32_t block_size;
    uint32_t blocks_per_group;
    uint32_t inodes_per_group;
    uint32_t inode_size;
    uint32_t group_count;
    uint32_t desc_size;
    struct ext4_group_desc *group_descs;
    void *device;  /* Block device */
    /* Read function */
    int (*read_block)(void *device, uint64_t block, void *buf);
    int (*write_block)(void *device, uint64_t block, const void *buf);
};

/* ===================================================================== */
/* ext4 Functions */
/* ===================================================================== */

/* ===================================================================== */
/* Block I/O */
/* ===================================================================== */

static int ext4_read_block(struct ext4_fs *fs, uint64_t block, void *buf)
{
    if (fs->read_block) {
        return fs->read_block(fs->device, block, buf);
    }
    return -1;
}

static int ext4_write_block_raw(struct ext4_fs *fs, uint64_t block, const void *buf)
{
    if (fs->write_block) {
        return fs->write_block(fs->device, block, buf);
    }
    return -1;
}

/* ===================================================================== */
/* Block Bitmap Management */
/* ===================================================================== */

static uint64_t ext4_get_block_bitmap(struct ext4_fs *fs, uint32_t group)
{
    if (group >= fs->group_count) return 0;
    struct ext4_group_desc *gd = &fs->group_descs[group];
    uint64_t bitmap = gd->bg_block_bitmap_lo;
    if (fs->desc_size >= 64) {
        bitmap |= ((uint64_t)gd->bg_block_bitmap_hi << 32);
    }
    return bitmap;
}

static uint64_t ext4_get_inode_bitmap(struct ext4_fs *fs, uint32_t group)
{
    if (group >= fs->group_count) return 0;
    struct ext4_group_desc *gd = &fs->group_descs[group];
    uint64_t bitmap = gd->bg_inode_bitmap_lo;
    if (fs->desc_size >= 64) {
        bitmap |= ((uint64_t)gd->bg_inode_bitmap_hi << 32);
    }
    return bitmap;
}

static int ext4_alloc_block(struct ext4_fs *fs, uint32_t preferred_group)
{
    uint8_t *bitmap = kmalloc(fs->block_size);
    if (!bitmap) return -1;
    
    /* Try preferred group first, then scan all groups */
    for (uint32_t g = 0; g < fs->group_count; g++) {
        uint32_t group = (preferred_group + g) % fs->group_count;
        struct ext4_group_desc *gd = &fs->group_descs[group];
        
        /* Check if group has free blocks */
        uint32_t free_blocks = gd->bg_free_blocks_count_lo;
        if (fs->desc_size >= 64) {
            free_blocks |= ((uint32_t)gd->bg_free_blocks_count_hi << 16);
        }
        if (free_blocks == 0) continue;
        
        /* Read block bitmap */
        uint64_t bitmap_block = ext4_get_block_bitmap(fs, group);
        if (ext4_read_block(fs, bitmap_block, bitmap) < 0) continue;
        
        /* Find first free bit */
        for (uint32_t byte = 0; byte < fs->block_size; byte++) {
            if (bitmap[byte] == 0xFF) continue;
            
            for (int bit = 0; bit < 8; bit++) {
                if (!(bitmap[byte] & (1 << bit))) {
                    /* Found free block! */
                    uint32_t block_in_group = byte * 8 + bit;
                    if (block_in_group >= fs->blocks_per_group) break;
                    
                    /* Mark as allocated */
                    bitmap[byte] |= (1 << bit);
                    
                    /* Write bitmap back */
                    if (ext4_write_block_raw(fs, bitmap_block, bitmap) < 0) {
                        kfree(bitmap);
                        return -1;
                    }
                    
                    /* Update group descriptor */
                    gd->bg_free_blocks_count_lo--;
                    fs->sb.s_free_blocks_count_lo--;
                    
                    kfree(bitmap);
                    
                    /* Return absolute block number */
                    return fs->sb.s_first_data_block + 
                           group * fs->blocks_per_group + block_in_group;
                }
            }
        }
    }
    
    kfree(bitmap);
    return -1; /* No free blocks */
}

static int ext4_free_block(struct ext4_fs *fs, uint64_t block)
{
    if (block < fs->sb.s_first_data_block) return -1;
    
    uint64_t rel_block = block - fs->sb.s_first_data_block;
    uint32_t group = rel_block / fs->blocks_per_group;
    uint32_t index = rel_block % fs->blocks_per_group;
    
    if (group >= fs->group_count) return -1;
    
    uint8_t *bitmap = kmalloc(fs->block_size);
    if (!bitmap) return -1;
    
    uint64_t bitmap_block = ext4_get_block_bitmap(fs, group);
    if (ext4_read_block(fs, bitmap_block, bitmap) < 0) {
        kfree(bitmap);
        return -1;
    }
    
    /* Clear bit */
    uint32_t byte = index / 8;
    uint32_t bit = index % 8;
    bitmap[byte] &= ~(1 << bit);
    
    /* Write back */
    if (ext4_write_block_raw(fs, bitmap_block, bitmap) < 0) {
        kfree(bitmap);
        return -1;
    }
    
    /* Update counts */
    fs->group_descs[group].bg_free_blocks_count_lo++;
    fs->sb.s_free_blocks_count_lo++;
    
    kfree(bitmap);
    return 0;
}

/* ===================================================================== */
/* Inode Bitmap Management */
/* ===================================================================== */

static int ext4_alloc_inode(struct ext4_fs *fs, uint32_t preferred_group)
{
    uint8_t *bitmap = kmalloc(fs->block_size);
    if (!bitmap) return -1;
    
    for (uint32_t g = 0; g < fs->group_count; g++) {
        uint32_t group = (preferred_group + g) % fs->group_count;
        struct ext4_group_desc *gd = &fs->group_descs[group];
        
        /* Check if group has free inodes */
        uint32_t free_inodes = gd->bg_free_inodes_count_lo;
        if (fs->desc_size >= 64) {
            free_inodes |= ((uint32_t)gd->bg_free_inodes_count_hi << 16);
        }
        if (free_inodes == 0) continue;
        
        /* Read inode bitmap */
        uint64_t bitmap_block = ext4_get_inode_bitmap(fs, group);
        if (ext4_read_block(fs, bitmap_block, bitmap) < 0) continue;
        
        /* Find first free bit */
        for (uint32_t byte = 0; byte < fs->inodes_per_group / 8; byte++) {
            if (bitmap[byte] == 0xFF) continue;
            
            for (int bit = 0; bit < 8; bit++) {
                if (!(bitmap[byte] & (1 << bit))) {
                    uint32_t inode_in_group = byte * 8 + bit;
                    if (inode_in_group >= fs->inodes_per_group) break;
                    
                    /* Mark as allocated */
                    bitmap[byte] |= (1 << bit);
                    
                    /* Write bitmap back */
                    if (ext4_write_block_raw(fs, bitmap_block, bitmap) < 0) {
                        kfree(bitmap);
                        return -1;
                    }
                    
                    /* Update counts */
                    gd->bg_free_inodes_count_lo--;
                    fs->sb.s_free_inodes_count--;
                    
                    kfree(bitmap);
                    
                    /* Return inode number (1-based) */
                    return group * fs->inodes_per_group + inode_in_group + 1;
                }
            }
        }
    }
    
    kfree(bitmap);
    return -1; /* No free inodes */
}

static int ext4_free_inode(struct ext4_fs *fs, uint32_t ino)
{
    if (ino == 0) return -1;
    
    uint32_t group = (ino - 1) / fs->inodes_per_group;
    uint32_t index = (ino - 1) % fs->inodes_per_group;
    
    if (group >= fs->group_count) return -1;
    
    uint8_t *bitmap = kmalloc(fs->block_size);
    if (!bitmap) return -1;
    
    uint64_t bitmap_block = ext4_get_inode_bitmap(fs, group);
    if (ext4_read_block(fs, bitmap_block, bitmap) < 0) {
        kfree(bitmap);
        return -1;
    }
    
    /* Clear bit */
    uint32_t byte = index / 8;
    uint32_t bit = index % 8;
    bitmap[byte] &= ~(1 << bit);
    
    /* Write back */
    if (ext4_write_block_raw(fs, bitmap_block, bitmap) < 0) {
        kfree(bitmap);
        return -1;
    }
    
    /* Update counts */
    fs->group_descs[group].bg_free_inodes_count_lo++;
    fs->sb.s_free_inodes_count++;
    
    kfree(bitmap);
    return 0;
}

/* ===================================================================== */
/* Inode I/O */
/* ===================================================================== */

static int ext4_read_inode(struct ext4_fs *fs, uint32_t ino, struct ext4_inode *inode)
{
    if (ino == 0) return -1;
    
    uint32_t group = (ino - 1) / fs->inodes_per_group;
    uint32_t index = (ino - 1) % fs->inodes_per_group;
    
    if (group >= fs->group_count) return -1;
    
    struct ext4_group_desc *gd = &fs->group_descs[group];
    uint64_t inode_table = gd->bg_inode_table_lo;
    if (fs->desc_size >= 64) {
        inode_table |= ((uint64_t)gd->bg_inode_table_hi << 32);
    }
    
    uint64_t block_offset = (index * fs->inode_size) / fs->block_size;
    uint64_t offset_in_block = (index * fs->inode_size) % fs->block_size;
    
    uint8_t *buf = kmalloc(fs->block_size);
    if (!buf) return -1;
    
    if (ext4_read_block(fs, inode_table + block_offset, buf) < 0) {
        kfree(buf);
        return -1;
    }
    
    /* Copy inode */
    uint8_t *src = buf + offset_in_block;
    uint8_t *dst = (uint8_t *)inode;
    for (size_t i = 0; i < sizeof(struct ext4_inode); i++) {
        dst[i] = src[i];
    }
    
    kfree(buf);
    return 0;
}

static int ext4_write_inode(struct ext4_fs *fs, uint32_t ino, struct ext4_inode *inode)
{
    if (ino == 0) return -1;
    
    uint32_t group = (ino - 1) / fs->inodes_per_group;
    uint32_t index = (ino - 1) % fs->inodes_per_group;
    
    if (group >= fs->group_count) return -1;
    
    struct ext4_group_desc *gd = &fs->group_descs[group];
    uint64_t inode_table = gd->bg_inode_table_lo;
    if (fs->desc_size >= 64) {
        inode_table |= ((uint64_t)gd->bg_inode_table_hi << 32);
    }
    
    uint64_t block_offset = (index * fs->inode_size) / fs->block_size;
    uint64_t offset_in_block = (index * fs->inode_size) % fs->block_size;
    
    uint8_t *buf = kmalloc(fs->block_size);
    if (!buf) return -1;
    
    /* Read existing block */
    if (ext4_read_block(fs, inode_table + block_offset, buf) < 0) {
        kfree(buf);
        return -1;
    }
    
    /* Copy inode data into block */
    uint8_t *dst = buf + offset_in_block;
    uint8_t *src = (uint8_t *)inode;
    for (size_t i = 0; i < sizeof(struct ext4_inode); i++) {
        dst[i] = src[i];
    }
    
    /* Write block back */
    int ret = ext4_write_block_raw(fs, inode_table + block_offset, buf);
    kfree(buf);
    return ret;
}

/* ===================================================================== */
/* Block Mapping (get/set file blocks) */
/* ===================================================================== */

static uint64_t ext4_get_file_block(struct ext4_fs *fs, struct ext4_inode *inode, 
                                     uint64_t file_block)
{
    /* Direct blocks */
    if (file_block < EXT4_NDIR_BLOCKS) {
        return inode->i_block[file_block];
    }
    
    file_block -= EXT4_NDIR_BLOCKS;
    uint32_t ptrs_per_block = fs->block_size / 4;
    
    /* Single indirect block */
    if (file_block < ptrs_per_block) {
        if (inode->i_block[EXT4_IND_BLOCK] == 0) return 0;
        
        uint32_t *indirect = kmalloc(fs->block_size);
        if (!indirect) return 0;
        
        if (ext4_read_block(fs, inode->i_block[EXT4_IND_BLOCK], indirect) < 0) {
            kfree(indirect);
            return 0;
        }
        
        uint64_t block = indirect[file_block];
        kfree(indirect);
        return block;
    }
    
    file_block -= ptrs_per_block;
    
    /* Double indirect block */
    if (file_block < ptrs_per_block * ptrs_per_block) {
        if (inode->i_block[EXT4_DIND_BLOCK] == 0) return 0;
        
        uint32_t *dind = kmalloc(fs->block_size);
        if (!dind) return 0;
        
        if (ext4_read_block(fs, inode->i_block[EXT4_DIND_BLOCK], dind) < 0) {
            kfree(dind);
            return 0;
        }
        
        uint32_t ind_idx = file_block / ptrs_per_block;
        uint32_t ind_off = file_block % ptrs_per_block;
        
        if (dind[ind_idx] == 0) {
            kfree(dind);
            return 0;
        }
        
        uint32_t *ind = kmalloc(fs->block_size);
        if (!ind) {
            kfree(dind);
            return 0;
        }
        
        if (ext4_read_block(fs, dind[ind_idx], ind) < 0) {
            kfree(dind);
            kfree(ind);
            return 0;
        }
        
        uint64_t block = ind[ind_off];
        kfree(dind);
        kfree(ind);
        return block;
    }
    
    /* Triple indirect not implemented */
    return 0;
}

static int ext4_set_file_block(struct ext4_fs *fs, struct ext4_inode *inode,
                               uint64_t file_block, uint64_t disk_block,
                               uint32_t *ino_for_write)
{
    uint32_t group = (*ino_for_write - 1) / fs->inodes_per_group;
    
    /* Direct blocks */
    if (file_block < EXT4_NDIR_BLOCKS) {
        inode->i_block[file_block] = (uint32_t)disk_block;
        return 0;
    }
    
    file_block -= EXT4_NDIR_BLOCKS;
    uint32_t ptrs_per_block = fs->block_size / 4;
    
    /* Single indirect block */
    if (file_block < ptrs_per_block) {
        /* Allocate indirect block if needed */
        if (inode->i_block[EXT4_IND_BLOCK] == 0) {
            int new_block = ext4_alloc_block(fs, group);
            if (new_block < 0) return -1;
            inode->i_block[EXT4_IND_BLOCK] = new_block;
            
            /* Zero the new indirect block */
            uint8_t *zero = kmalloc(fs->block_size);
            if (zero) {
                for (size_t i = 0; i < fs->block_size; i++) zero[i] = 0;
                ext4_write_block_raw(fs, new_block, zero);
                kfree(zero);
            }
        }
        
        uint32_t *indirect = kmalloc(fs->block_size);
        if (!indirect) return -1;
        
        if (ext4_read_block(fs, inode->i_block[EXT4_IND_BLOCK], indirect) < 0) {
            kfree(indirect);
            return -1;
        }
        
        indirect[file_block] = (uint32_t)disk_block;
        
        int ret = ext4_write_block_raw(fs, inode->i_block[EXT4_IND_BLOCK], indirect);
        kfree(indirect);
        return ret;
    }
    
    /* Double/triple indirect not fully implemented for writes */
    return -1;
}

/* ===================================================================== */
/* Directory Operations */
/* ===================================================================== */

static int ext4_add_dir_entry(struct ext4_fs *fs, uint32_t dir_ino, 
                               const char *name, uint32_t ino, uint8_t file_type)
{
    struct ext4_inode dir_inode;
    if (ext4_read_inode(fs, dir_ino, &dir_inode) < 0) return -1;
    
    uint8_t name_len = 0;
    while (name[name_len] && name_len < 255) name_len++;
    
    /* Entry size: inode(4) + rec_len(2) + name_len(1) + file_type(1) + name */
    uint16_t entry_size = 8 + name_len;
    entry_size = (entry_size + 3) & ~3; /* 4-byte align */
    
    uint8_t *block_buf = kmalloc(fs->block_size);
    if (!block_buf) return -1;
    
    /* Scan directory blocks for space */
    uint64_t dir_size = dir_inode.i_size_lo;
    uint64_t num_blocks = (dir_size + fs->block_size - 1) / fs->block_size;
    
    for (uint64_t b = 0; b < num_blocks; b++) {
        uint64_t disk_block = ext4_get_file_block(fs, &dir_inode, b);
        if (disk_block == 0) continue;
        
        if (ext4_read_block(fs, disk_block, block_buf) < 0) continue;
        
        /* Scan entries in this block */
        uint32_t offset = 0;
        while (offset < fs->block_size) {
            struct ext4_dir_entry *de = (struct ext4_dir_entry *)(block_buf + offset);
            
            if (de->rec_len == 0) break;
            
            /* Calculate actual entry size */
            uint16_t actual_size = 8 + de->name_len;
            actual_size = (actual_size + 3) & ~3;
            
            /* Check if there's slack space after this entry */
            if (de->rec_len > actual_size) {
                uint16_t slack = de->rec_len - actual_size;
                if (slack >= entry_size) {
                    /* Found space! Shrink current entry and add new one */
                    de->rec_len = actual_size;
                    
                    struct ext4_dir_entry *new_de = (struct ext4_dir_entry *)(block_buf + offset + actual_size);
                    new_de->inode = ino;
                    new_de->rec_len = slack;
                    new_de->name_len = name_len;
                    new_de->file_type = file_type;
                    for (int i = 0; i < name_len; i++) {
                        new_de->name[i] = name[i];
                    }
                    
                    /* Write block back */
                    int ret = ext4_write_block_raw(fs, disk_block, block_buf);
                    kfree(block_buf);
                    return ret;
                }
            }
            
            offset += de->rec_len;
        }
    }
    
    /* No space in existing blocks, allocate a new one */
    uint32_t group = (dir_ino - 1) / fs->inodes_per_group;
    int new_block = ext4_alloc_block(fs, group);
    if (new_block < 0) {
        kfree(block_buf);
        return -1;
    }
    
    /* Zero new block and add entry */
    for (size_t i = 0; i < fs->block_size; i++) block_buf[i] = 0;
    
    struct ext4_dir_entry *de = (struct ext4_dir_entry *)block_buf;
    de->inode = ino;
    de->rec_len = fs->block_size; /* Takes entire block */
    de->name_len = name_len;
    de->file_type = file_type;
    for (int i = 0; i < name_len; i++) {
        de->name[i] = name[i];
    }
    
    if (ext4_write_block_raw(fs, new_block, block_buf) < 0) {
        ext4_free_block(fs, new_block);
        kfree(block_buf);
        return -1;
    }
    
    /* Update directory inode */
    uint64_t new_file_block = num_blocks;
    if (ext4_set_file_block(fs, &dir_inode, new_file_block, new_block, &dir_ino) < 0) {
        ext4_free_block(fs, new_block);
        kfree(block_buf);
        return -1;
    }
    
    dir_inode.i_size_lo += fs->block_size;
    dir_inode.i_blocks_lo += fs->block_size / 512;
    
    int ret = ext4_write_inode(fs, dir_ino, &dir_inode);
    kfree(block_buf);
    return ret;
}

/* ===================================================================== */
/* File Write */
/* ===================================================================== */

static int ext4_write_file(struct ext4_fs *fs, uint32_t ino, const void *buf,
                           size_t offset, size_t len)
{
    struct ext4_inode inode;
    if (ext4_read_inode(fs, ino, &inode) < 0) return -1;
    
    uint8_t *block_buf = kmalloc(fs->block_size);
    if (!block_buf) return -1;
    
    uint32_t group = (ino - 1) / fs->inodes_per_group;
    size_t bytes_written = 0;
    
    while (bytes_written < len) {
        uint64_t file_block = (offset + bytes_written) / fs->block_size;
        uint64_t block_offset = (offset + bytes_written) % fs->block_size;
        
        /* Get or allocate disk block */
        uint64_t disk_block = ext4_get_file_block(fs, &inode, file_block);
        if (disk_block == 0) {
            /* Allocate new block */
            int new_block = ext4_alloc_block(fs, group);
            if (new_block < 0) break;
            
            disk_block = new_block;
            if (ext4_set_file_block(fs, &inode, file_block, disk_block, &ino) < 0) {
                ext4_free_block(fs, new_block);
                break;
            }
            
            inode.i_blocks_lo += fs->block_size / 512;
            
            /* Zero new block */
            for (size_t i = 0; i < fs->block_size; i++) block_buf[i] = 0;
        } else {
            /* Read existing block */
            if (ext4_read_block(fs, disk_block, block_buf) < 0) break;
        }
        
        /* Calculate how much to write */
        size_t to_write = fs->block_size - block_offset;
        if (to_write > len - bytes_written) {
            to_write = len - bytes_written;
        }
        
        /* Copy data */
        const uint8_t *src = (const uint8_t *)buf + bytes_written;
        for (size_t i = 0; i < to_write; i++) {
            block_buf[block_offset + i] = src[i];
        }
        
        /* Write block */
        if (ext4_write_block_raw(fs, disk_block, block_buf) < 0) break;
        
        bytes_written += to_write;
    }
    
    /* Update file size if necessary */
    if (offset + bytes_written > inode.i_size_lo) {
        inode.i_size_lo = offset + bytes_written;
    }
    
    /* Update timestamps */
    inode.i_mtime = 0; /* Should be current time */
    
    ext4_write_inode(fs, ino, &inode);
    
    kfree(block_buf);
    return bytes_written;
}

/* ===================================================================== */
/* File Creation */
/* ===================================================================== */

static int ext4_create_file(struct ext4_fs *fs, uint32_t parent_ino,
                            const char *name, uint16_t mode)
{
    /* Allocate new inode */
    uint32_t parent_group = (parent_ino - 1) / fs->inodes_per_group;
    int new_ino = ext4_alloc_inode(fs, parent_group);
    if (new_ino < 0) {
        printk(KERN_ERR "EXT4: Failed to allocate inode\n");
        return -1;
    }
    
    /* Initialize inode */
    struct ext4_inode inode;
    for (size_t i = 0; i < sizeof(inode); i++) {
        ((uint8_t *)&inode)[i] = 0;
    }
    
    inode.i_mode = mode;
    inode.i_links_count = 1;
    inode.i_uid = 0;
    inode.i_gid = 0;
    
    /* Write inode */
    if (ext4_write_inode(fs, new_ino, &inode) < 0) {
        ext4_free_inode(fs, new_ino);
        return -1;
    }
    
    /* Add directory entry */
    uint8_t file_type = (mode & EXT4_S_IFDIR) ? 2 : 1; /* 2=dir, 1=file */
    if (ext4_add_dir_entry(fs, parent_ino, name, new_ino, file_type) < 0) {
        ext4_free_inode(fs, new_ino);
        return -1;
    }
    
    /* If creating directory, add . and .. entries */
    if (mode & EXT4_S_IFDIR) {
        ext4_add_dir_entry(fs, new_ino, ".", new_ino, 2);
        ext4_add_dir_entry(fs, new_ino, "..", parent_ino, 2);
        
        /* Update parent link count */
        struct ext4_inode parent;
        if (ext4_read_inode(fs, parent_ino, &parent) == 0) {
            parent.i_links_count++;
            ext4_write_inode(fs, parent_ino, &parent);
        }
    }
    
    return new_ino;
}

/* ===================================================================== */
/* Superblock Sync */
/* ===================================================================== */

static int ext4_sync_superblock(struct ext4_fs *fs)
{
    /* Superblock is at block 0 offset 1024, or block 1 if block_size=1024 */
    uint8_t *buf = kmalloc(fs->block_size);
    if (!buf) return -1;
    
    uint64_t sb_block = (fs->block_size == 1024) ? 1 : 0;
    
    /* Read block containing superblock */
    if (ext4_read_block(fs, sb_block, buf) < 0) {
        kfree(buf);
        return -1;
    }
    
    /* Copy superblock to buffer at correct offset */
    uint32_t sb_offset = (fs->block_size == 1024) ? 0 : 1024;
    uint8_t *src = (uint8_t *)&fs->sb;
    for (size_t i = 0; i < sizeof(struct ext4_superblock); i++) {
        buf[sb_offset + i] = src[i];
    }
    
    /* Write back */
    int ret = ext4_write_block_raw(fs, sb_block, buf);
    kfree(buf);
    return ret;
}

static int ext4_read_file(struct ext4_fs *fs, uint32_t ino, void *buf, 
                           size_t offset, size_t len)
{
    struct ext4_inode inode;
    
    if (ext4_read_inode(fs, ino, &inode) < 0) {
        return -1;
    }
    
    uint64_t file_size = inode.i_size_lo;
    if (offset >= file_size) return 0;
    if (offset + len > file_size) {
        len = file_size - offset;
    }
    
    uint8_t *block_buf = kmalloc(fs->block_size);
    if (!block_buf) return -1;
    
    size_t bytes_read = 0;
    
    while (bytes_read < len) {
        uint64_t file_block = (offset + bytes_read) / fs->block_size;
        uint64_t block_offset = (offset + bytes_read) % fs->block_size;
        
        uint64_t disk_block = ext4_get_file_block(fs, &inode, file_block);
        if (disk_block == 0) break;
        
        if (ext4_read_block(fs, disk_block, block_buf) < 0) {
            break;
        }
        
        size_t to_copy = fs->block_size - block_offset;
        if (to_copy > len - bytes_read) {
            to_copy = len - bytes_read;
        }
        
        for (size_t i = 0; i < to_copy; i++) {
            ((uint8_t *)buf)[bytes_read + i] = block_buf[block_offset + i];
        }
        
        bytes_read += to_copy;
    }
    
    kfree(block_buf);
    return bytes_read;
}

/* ===================================================================== */
/* VFS Integration */
/* ===================================================================== */

static struct ext4_fs *root_ext4 = NULL;

int ext4_mount(void *device, 
               int (*read_block)(void*, uint64_t, void*),
               int (*write_block)(void*, uint64_t, const void*))
{
    printk(KERN_INFO "EXT4: Mounting filesystem\n");
    
    struct ext4_fs *fs = kmalloc(sizeof(struct ext4_fs));
    if (!fs) return -1;
    
    fs->device = device;
    fs->read_block = read_block;
    fs->write_block = write_block;
    
    /* Read superblock */
    uint8_t sb_buf[1024];
    
    /* Superblock is at offset 1024 */
    if (read_block(device, 1, sb_buf) < 0) {
        kfree(fs);
        return -1;
    }
    
    /* Copy superblock */
    uint8_t *src = sb_buf;
    uint8_t *dst = (uint8_t *)&fs->sb;
    for (size_t i = 0; i < sizeof(struct ext4_superblock); i++) {
        dst[i] = src[i];
    }
    
    /* Verify magic */
    if (fs->sb.s_magic != EXT4_SUPER_MAGIC) {
        printk(KERN_ERR "EXT4: Invalid magic number\n");
        kfree(fs);
        return -1;
    }
    
    /* Calculate parameters */
    fs->block_size = 1024 << fs->sb.s_log_block_size;
    fs->blocks_per_group = fs->sb.s_blocks_per_group;
    fs->inodes_per_group = fs->sb.s_inodes_per_group;
    fs->inode_size = (fs->sb.s_rev_level >= EXT4_DYNAMIC_REV) 
                     ? fs->sb.s_inode_size : 128;
    
    uint64_t total_blocks = fs->sb.s_blocks_count_lo;
    if (fs->sb.s_feature_incompat & EXT4_FEATURE_INCOMPAT_64BIT) {
        total_blocks |= ((uint64_t)fs->sb.s_blocks_count_hi << 32);
    }
    
    fs->group_count = (total_blocks + fs->blocks_per_group - 1) / fs->blocks_per_group;
    fs->desc_size = (fs->sb.s_feature_incompat & EXT4_FEATURE_INCOMPAT_64BIT) 
                    ? fs->sb.s_desc_size : 32;
    
    printk(KERN_INFO "EXT4: Block size: %u\n", fs->block_size);
    printk(KERN_INFO "EXT4: Groups: %u\n", fs->group_count);
    printk(KERN_INFO "EXT4: Volume: %s\n", fs->sb.s_volume_name);
    
    /* Read group descriptors */
    size_t gd_size = fs->group_count * fs->desc_size;
    fs->group_descs = kmalloc(gd_size);
    if (!fs->group_descs) {
        kfree(fs);
        return -1;
    }
    
    uint64_t gd_block = (fs->block_size == 1024) ? 2 : 1;
    uint8_t *gd_buf = kmalloc(fs->block_size);
    if (!gd_buf) {
        kfree(fs->group_descs);
        kfree(fs);
        return -1;
    }
    
    /* Read all group descriptors */
    uint32_t gd_per_block = fs->block_size / fs->desc_size;
    uint32_t gd_blocks_needed = (fs->group_count + gd_per_block - 1) / gd_per_block;
    
    for (uint32_t b = 0; b < gd_blocks_needed; b++) {
        if (ext4_read_block(fs, gd_block + b, gd_buf) < 0) {
            printk(KERN_ERR "EXT4: Failed to read group descriptor block %llu\n", 
                   (unsigned long long)(gd_block + b));
            kfree(gd_buf);
            kfree(fs->group_descs);
            kfree(fs);
            return -1;
        }
        
        /* Copy descriptors from this block */
        uint32_t gd_in_block = gd_per_block;
        if (b == gd_blocks_needed - 1) {
            gd_in_block = fs->group_count - b * gd_per_block;
        }
        
        for (uint32_t g = 0; g < gd_in_block; g++) {
            uint32_t abs_g = b * gd_per_block + g;
            uint8_t *src = gd_buf + g * fs->desc_size;
            uint8_t *dst = (uint8_t *)&fs->group_descs[abs_g];
            for (size_t i = 0; i < fs->desc_size && i < sizeof(struct ext4_group_desc); i++) {
                dst[i] = src[i];
            }
        }
    }
    
    kfree(gd_buf);
    
    root_ext4 = fs;
    printk(KERN_INFO "EXT4: Filesystem mounted successfully (R/W)\n");
    printk(KERN_INFO "EXT4: Free blocks: %u, Free inodes: %u\n",
           fs->sb.s_free_blocks_count_lo, fs->sb.s_free_inodes_count);
    
    return 0;
}

int ext4_unmount(void)
{
    if (root_ext4) {
        /* Sync superblock before unmount */
        ext4_sync_superblock(root_ext4);
        
        if (root_ext4->group_descs) {
            kfree(root_ext4->group_descs);
        }
        kfree(root_ext4);
        root_ext4 = NULL;
    }
    return 0;
}

/* ===================================================================== */
/* Public API (called from VFS layer) */
/* ===================================================================== */

/**
 * ext4_vfs_read - Read from an ext4 file
 * @ino: Inode number
 * @buf: Buffer to read into
 * @offset: Offset in file
 * @len: Number of bytes to read
 * Returns: bytes read or negative error
 */
int ext4_vfs_read(uint32_t ino, void *buf, size_t offset, size_t len)
{
    if (!root_ext4) return -1;
    return ext4_read_file(root_ext4, ino, buf, offset, len);
}

/**
 * ext4_vfs_write - Write to an ext4 file
 * @ino: Inode number
 * @buf: Buffer to write from
 * @offset: Offset in file
 * @len: Number of bytes to write
 * Returns: bytes written or negative error
 */
int ext4_vfs_write(uint32_t ino, const void *buf, size_t offset, size_t len)
{
    if (!root_ext4) return -1;
    return ext4_write_file(root_ext4, ino, buf, offset, len);
}

/**
 * ext4_vfs_create - Create a new file
 * @parent_ino: Parent directory inode
 * @name: Filename
 * @mode: File mode (permissions + type)
 * Returns: new inode number or negative error
 */
int ext4_vfs_create(uint32_t parent_ino, const char *name, uint16_t mode)
{
    if (!root_ext4) return -1;
    return ext4_create_file(root_ext4, parent_ino, name, mode);
}

/**
 * ext4_vfs_mkdir - Create a new directory
 * @parent_ino: Parent directory inode
 * @name: Directory name
 * @mode: Directory mode
 * Returns: new inode number or negative error
 */
int ext4_vfs_mkdir(uint32_t parent_ino, const char *name, uint16_t mode)
{
    if (!root_ext4) return -1;
    return ext4_create_file(root_ext4, parent_ino, name, mode | EXT4_S_IFDIR);
}

/**
 * ext4_vfs_unlink - Remove a file (not implemented fully)
 * @parent_ino: Parent directory inode
 * @name: Filename to remove
 * Returns: 0 on success, negative error
 */
int ext4_vfs_unlink(uint32_t parent_ino, const char *name)
{
    (void)parent_ino;
    (void)name;
    if (!root_ext4) return -1;
    
    /* Full unlink requires:
     * 1. Find directory entry
     * 2. Decrement link count
     * 3. If link count == 0, free blocks and inode
     * 4. Remove directory entry
     */
    printk(KERN_WARNING "EXT4: unlink not fully implemented\n");
    return -1;
}

/**
 * ext4_vfs_truncate - Truncate file to given size
 * @ino: Inode number
 * @size: New file size
 * Returns: 0 on success
 */
int ext4_vfs_truncate(uint32_t ino, uint64_t size)
{
    if (!root_ext4) return -1;
    
    struct ext4_inode inode;
    if (ext4_read_inode(root_ext4, ino, &inode) < 0) return -1;
    
    uint64_t old_size = inode.i_size_lo;
    
    /* If shrinking, free excess blocks */
    if (size < old_size) {
        uint64_t new_blocks = (size + root_ext4->block_size - 1) / root_ext4->block_size;
        uint64_t old_blocks = (old_size + root_ext4->block_size - 1) / root_ext4->block_size;
        
        for (uint64_t b = new_blocks; b < old_blocks; b++) {
            uint64_t disk_block = ext4_get_file_block(root_ext4, &inode, b);
            if (disk_block != 0) {
                ext4_free_block(root_ext4, disk_block);
            }
        }
    }
    
    inode.i_size_lo = (uint32_t)size;
    inode.i_size_hi = (uint32_t)(size >> 32);
    
    return ext4_write_inode(root_ext4, ino, &inode);
}

/**
 * ext4_vfs_sync - Sync all pending writes to disk
 * Returns: 0 on success
 */
int ext4_vfs_sync(void)
{
    if (!root_ext4) return -1;
    return ext4_sync_superblock(root_ext4);
}

/**
 * ext4_vfs_stat - Get file information
 * @ino: Inode number
 * @size: Output file size
 * @mode: Output file mode
 * @links: Output link count
 * Returns: 0 on success
 */
int ext4_vfs_stat(uint32_t ino, uint64_t *size, uint16_t *mode, uint16_t *links)
{
    if (!root_ext4) return -1;
    
    struct ext4_inode inode;
    if (ext4_read_inode(root_ext4, ino, &inode) < 0) return -1;
    
    if (size) *size = inode.i_size_lo | ((uint64_t)inode.i_size_hi << 32);
    if (mode) *mode = inode.i_mode;
    if (links) *links = inode.i_links_count;
    
    return 0;
}
