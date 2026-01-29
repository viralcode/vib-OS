/*
 * vib-OS - FAT32 Filesystem Implementation
 */

#include "fs/fat32.h"
#include "fs/vfs.h"
#include "mm/kmalloc.h"
#include "printk.h"
#include "string.h"

/* In-memory Superblock Info */
struct fat32_sb_info {
  uint32_t fat_start_sector;
  uint32_t data_start_sector;
  uint32_t sectors_per_cluster;
  uint32_t bytes_per_cluster;
  uint32_t root_cluster;
  uint32_t fat_size; // in sectors
  /* We implicitly assume 512 byte sectors for now, or read from BPB */
};

/* In-memory Inode Info */
struct fat32_inode_info {
  uint32_t first_cluster;
};

/* Forward declarations */
static struct file_operations fat32_file_ops;
static struct inode_operations fat32_dir_ops;

/* ===================================================================== */
/* Helper Functions */
/* ===================================================================== */

static uint32_t cluster_to_sector(struct fat32_sb_info *sbi, uint32_t cluster) {
  return sbi->data_start_sector + ((cluster - 2) * sbi->sectors_per_cluster);
}

// TODO: Implement read_sector via block device interface
// For now, these are stubs since we don't have block layer fully exposed in VFS
// yet
extern int block_read(void *dev, uint64_t sector, void *buf);
extern int block_write(void *dev, uint64_t sector, void *buf);

/* ===================================================================== */
/* File Operations */
/* ===================================================================== */

static ssize_t fat32_read(struct file *file, char *buf, size_t count,
                          loff_t *pos) {
  (void)file;
  (void)buf;
  (void)count;
  (void)pos;
  // Stub
  return 0;
}

static struct file_operations fat32_file_ops = {
    .read = fat32_read,
    // .write = fat32_write
};

/* ===================================================================== */
/* Inode Operations */
/* ===================================================================== */

static struct dentry *fat32_lookup(struct inode *dir, struct dentry *dentry) {
  (void)dir;
  (void)dentry;
  // Stub: Lookup name in directory
  return NULL;
}

static struct inode_operations fat32_dir_ops = {
    .lookup = fat32_lookup,
    // .create = fat32_create,
    // .mkdir = fat32_mkdir
};

/* ===================================================================== */
/* Superblock / Mount */
/* ===================================================================== */

static struct super_block *fat32_mount(struct file_system_type *fs_type,
                                       int flags, const char *dev_name,
                                       void *data) {
  (void)fs_type;
  (void)flags;
  (void)dev_name;
  (void)data;
  struct super_block *sb = kzalloc(sizeof(struct super_block), GFP_KERNEL);
  if (!sb)
    return NULL;

  struct fat32_sb_info *sbi = kzalloc(sizeof(struct fat32_sb_info), GFP_KERNEL);
  if (!sbi) {
    kfree(sb);
    return NULL;
  }
  sb->s_fs_info = sbi;

  // Read Boot Sector (Sector 0)
  struct fat32_bpb *bpb = kzalloc(512, GFP_KERNEL);
  // TODO: Read sector 0 from dev_name (device path)
  // For now we assume dev_name is mapped to a block device pointer somehow

  // Parse BPB
  // sbi->fat_start_sector = bpb->reserved_sectors;
  // ...

  // Setup root inode
  struct inode *root = kzalloc(sizeof(struct inode), GFP_KERNEL);
  root->i_sb = sb;
  root->i_mode = S_IFDIR | 0755;
  root->i_op = &fat32_dir_ops;
  root->i_fop = &fat32_file_ops; // Dirs communicate via file ops nicely in
                                 // unix? Usually readdir.

  struct dentry *root_dentry = kzalloc(sizeof(struct dentry), GFP_KERNEL);
  root_dentry->d_inode = root;
  root_dentry->d_sb = sb;
  root_dentry->d_parent = NULL; // Root

  sb->s_root = root_dentry;

  kfree(bpb);
  return sb;
}

struct file_system_type fat32_fs_type = {
    .name = "fat32", .mount = fat32_mount, .next = NULL};
