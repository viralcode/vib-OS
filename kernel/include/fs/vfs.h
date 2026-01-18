/*
 * UnixOS Kernel - Virtual Filesystem (VFS) Header
 */

#ifndef _FS_VFS_H
#define _FS_VFS_H

#include "types.h"

/* ===================================================================== */
/* File types and modes */
/* ===================================================================== */

/* File types (same as Linux) */
#define S_IFMT      0170000     /* Type mask */
#define S_IFSOCK    0140000     /* Socket */
#define S_IFLNK     0120000     /* Symbolic link */
#define S_IFREG     0100000     /* Regular file */
#define S_IFBLK     0060000     /* Block device */
#define S_IFDIR     0040000     /* Directory */
#define S_IFCHR     0020000     /* Character device */
#define S_IFIFO     0010000     /* FIFO */

/* Permission bits */
#define S_ISUID     04000       /* Set UID bit */
#define S_ISGID     02000       /* Set GID bit */
#define S_ISVTX     01000       /* Sticky bit */
#define S_IRWXU     00700       /* Owner RWX */
#define S_IRUSR     00400       /* Owner read */
#define S_IWUSR     00200       /* Owner write */
#define S_IXUSR     00100       /* Owner execute */
#define S_IRWXG     00070       /* Group RWX */
#define S_IRGRP     00040       /* Group read */
#define S_IWGRP     00020       /* Group write */
#define S_IXGRP     00010       /* Group execute */
#define S_IRWXO     00007       /* Other RWX */
#define S_IROTH     00004       /* Other read */
#define S_IWOTH     00002       /* Other write */
#define S_IXOTH     00001       /* Other execute */

/* Type check macros */
#define S_ISREG(m)  (((m) & S_IFMT) == S_IFREG)
#define S_ISDIR(m)  (((m) & S_IFMT) == S_IFDIR)
#define S_ISCHR(m)  (((m) & S_IFMT) == S_IFCHR)
#define S_ISBLK(m)  (((m) & S_IFMT) == S_IFBLK)
#define S_ISFIFO(m) (((m) & S_IFMT) == S_IFIFO)
#define S_ISLNK(m)  (((m) & S_IFMT) == S_IFLNK)
#define S_ISSOCK(m) (((m) & S_IFMT) == S_IFSOCK)

/* ===================================================================== */
/* Open flags */
/* ===================================================================== */

#define O_RDONLY        0x0000
#define O_WRONLY        0x0001
#define O_RDWR          0x0002
#define O_ACCMODE       0x0003

#define O_CREAT         0x0040
#define O_EXCL          0x0080
#define O_NOCTTY        0x0100
#define O_TRUNC         0x0200
#define O_APPEND        0x0400
#define O_NONBLOCK      0x0800
#define O_SYNC          0x1000
#define O_DIRECTORY     0x10000
#define O_CLOEXEC       0x80000

/* ===================================================================== */
/* Seek constants */
/* ===================================================================== */

#define SEEK_SET        0
#define SEEK_CUR        1
#define SEEK_END        2

/* ===================================================================== */
/* Limits */
/* ===================================================================== */

#define NAME_MAX        255
#define PATH_MAX        4096
#define MAX_OPEN_FILES  256
#define MAX_FILESYSTEMS 16
#define MAX_MOUNTS      64

/* ===================================================================== */
/* Error codes */
/* ===================================================================== */

#define EPERM           1
#define ENOENT          2
#define ESRCH           3
#define EINTR           4
#define EIO             5
#define ENXIO           6
#define E2BIG           7
#define ENOEXEC         8
#define EBADF           9
#define ECHILD          10
#define EAGAIN          11
#define ENOMEM          12
#define EACCES          13
#define EFAULT          14
#define ENOTBLK         15
#define EBUSY           16
#define EEXIST          17
#define EXDEV           18
#define ENODEV          19
#define ENOTDIR         20
#define EISDIR          21
#define EINVAL          22
#define ENFILE          23
#define EMFILE          24
#define ENOTTY          25
#define ETXTBSY         26
#define EFBIG           27
#define ENOSPC          28
#define ESPIPE          29
#define EROFS           30
#define EMLINK          31
#define EPIPE           32
#define ENOSYS          38

/* ===================================================================== */
/* Forward declarations */
/* ===================================================================== */

struct inode;
struct dentry;
struct file;
struct super_block;
struct file_system_type;

/* ===================================================================== */
/* File operations */
/* ===================================================================== */

struct file_operations {
    ssize_t (*read)(struct file *, char *, size_t, loff_t *);
    ssize_t (*write)(struct file *, const char *, size_t, loff_t *);
    loff_t (*llseek)(struct file *, loff_t, int);
    int (*open)(struct inode *, struct file *);
    int (*release)(struct inode *, struct file *);
    int (*readdir)(struct file *, void *, int (*)(void *, const char *, int, loff_t, ino_t, unsigned));
    int (*ioctl)(struct file *, unsigned int, unsigned long);
    int (*mmap)(struct file *, void *);
};

/* ===================================================================== */
/* Inode operations */
/* ===================================================================== */

struct inode_operations {
    struct dentry *(*lookup)(struct inode *, struct dentry *);
    int (*create)(struct inode *, struct dentry *, mode_t);
    int (*mkdir)(struct inode *, struct dentry *, mode_t);
    int (*rmdir)(struct inode *, struct dentry *);
    int (*unlink)(struct inode *, struct dentry *);
    int (*link)(struct dentry *, struct inode *, struct dentry *);
    int (*symlink)(struct inode *, struct dentry *, const char *);
    int (*rename)(struct inode *, struct dentry *, struct inode *, struct dentry *);
    int (*readlink)(struct dentry *, char *, int);
    int (*setattr)(struct dentry *, void *);
    int (*getattr)(struct dentry *, void *);
};

/* ===================================================================== */
/* Super block operations */
/* ===================================================================== */

struct super_operations {
    struct inode *(*alloc_inode)(struct super_block *);
    void (*destroy_inode)(struct inode *);
    int (*write_inode)(struct inode *, int);
    void (*delete_inode)(struct inode *);
    void (*put_super)(struct super_block *);
    int (*sync_fs)(struct super_block *, int);
    int (*statfs)(struct super_block *, void *);
};

/* ===================================================================== */
/* Inode structure */
/* ===================================================================== */

struct inode {
    ino_t i_ino;
    mode_t i_mode;
    nlink_t i_nlink;
    uid_t i_uid;
    gid_t i_gid;
    dev_t i_rdev;
    loff_t i_size;
    struct timespec i_atime;
    struct timespec i_mtime;
    struct timespec i_ctime;
    blkcnt_t i_blocks;
    blksize_t i_blksize;
    
    const struct inode_operations *i_op;
    const struct file_operations *i_fop;
    struct super_block *i_sb;
    
    atomic_t i_count;
    uint32_t i_flags;
    void *i_private;
};

/* ===================================================================== */
/* Directory entry structure */
/* ===================================================================== */

struct dentry {
    char d_name[NAME_MAX + 1];
    struct inode *d_inode;
    struct dentry *d_parent;
    struct dentry *d_child;     /* First child */
    struct dentry *d_sibling;   /* Next sibling */
    struct super_block *d_sb;
    atomic_t d_count;
};

/* ===================================================================== */
/* File structure */
/* ===================================================================== */

struct file {
    struct dentry *f_dentry;
    const struct file_operations *f_op;
    loff_t f_pos;
    uint32_t f_flags;
    mode_t f_mode;
    atomic_t f_count;
    void *private_data;
};

/* ===================================================================== */
/* Super block structure */
/* ===================================================================== */

struct super_block {
    dev_t s_dev;
    uint32_t s_blocksize;
    uint32_t s_blocksize_bits;
    loff_t s_maxbytes;
    struct file_system_type *s_type;
    const struct super_operations *s_op;
    struct dentry *s_root;
    char s_id[32];
    void *s_fs_info;
};

/* ===================================================================== */
/* Filesystem type */
/* ===================================================================== */

struct file_system_type {
    const char *name;
    int fs_flags;
    struct super_block *(*mount)(struct file_system_type *, int, const char *, void *);
    void (*kill_sb)(struct super_block *);
    struct file_system_type *next;
};

/* ===================================================================== */
/* Mount structure */
/* ===================================================================== */

struct vfsmount {
    struct dentry *mnt_root;
    struct super_block *mnt_sb;
    struct dentry *mnt_mountpoint;
    struct vfsmount *mnt_parent;
    char mnt_devname[64];
};

/* ===================================================================== */
/* Function declarations */
/* ===================================================================== */

/**
 * vfs_init - Initialize the VFS layer
 */
void vfs_init(void);

/**
 * register_filesystem - Register a filesystem type
 */
int register_filesystem(struct file_system_type *fs);

/**
 * vfs_open - Open a file
 */
struct file *vfs_open(const char *path, int flags, mode_t mode);

/**
 * vfs_close - Close a file
 */
int vfs_close(struct file *file);

/**
 * vfs_readdir - Read directory entries
 */
int vfs_readdir(struct file *file, void *ctx, int (*filldir)(void *, const char *, int, loff_t, ino_t, unsigned));

/**
 * vfs_read - Read from a file
 */
ssize_t vfs_read(struct file *file, char *buf, size_t count);

/**
 * vfs_write - Write to a file
 */
ssize_t vfs_write(struct file *file, const char *buf, size_t count);

/**
 * vfs_lseek - Seek in a file
 */
loff_t vfs_lseek(struct file *file, loff_t offset, int whence);

/* Additional declarations - Write to a file
 */
ssize_t vfs_write(struct file *file, const char *buf, size_t count);

/**
 * vfs_lseek - Seek in a file
 */
loff_t vfs_lseek(struct file *file, loff_t offset, int whence);

/**
 * vfs_mkdir - Create a directory
 */
int vfs_mkdir(const char *path, mode_t mode);

/**
 * vfs_rmdir - Remove a directory
 */
int vfs_rmdir(const char *path);

/**
 * vfs_unlink - Remove a file
 */
int vfs_unlink(const char *path);

/**
 * vfs_rename - Rename a file or directory
 */
int vfs_rename(const char *oldpath, const char *newpath);

/**
 * vfs_mount - Mount a filesystem
 */
int vfs_mount(const char *source, const char *target, const char *fstype, unsigned long flags, const void *data);

/**
 * vfs_umount - Unmount a filesystem
 */
int vfs_umount(const char *target);

#endif /* _FS_VFS_H */
