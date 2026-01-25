/*
 * Vib-OS libc - sys/stat.h
 */

#ifndef _SYS_STAT_H
#define _SYS_STAT_H

#include <sys/types.h>

/* File types */
#define S_IFMT   0170000  /* Mask */
#define S_IFSOCK 0140000  /* Socket */
#define S_IFLNK  0120000  /* Symbolic link */
#define S_IFREG  0100000  /* Regular file */
#define S_IFBLK  0060000  /* Block device */
#define S_IFDIR  0040000  /* Directory */
#define S_IFCHR  0020000  /* Character device */
#define S_IFIFO  0010000  /* FIFO */

/* Permission bits */
#define S_ISUID  04000  /* Set UID */
#define S_ISGID  02000  /* Set GID */
#define S_ISVTX  01000  /* Sticky bit */

#define S_IRWXU  00700  /* Owner RWX */
#define S_IRUSR  00400  /* Owner R */
#define S_IWUSR  00200  /* Owner W */
#define S_IXUSR  00100  /* Owner X */

#define S_IRWXG  00070  /* Group RWX */
#define S_IRGRP  00040  /* Group R */
#define S_IWGRP  00020  /* Group W */
#define S_IXGRP  00010  /* Group X */

#define S_IRWXO  00007  /* Others RWX */
#define S_IROTH  00004  /* Others R */
#define S_IWOTH  00002  /* Others W */
#define S_IXOTH  00001  /* Others X */

/* Type test macros */
#define S_ISREG(m)  (((m) & S_IFMT) == S_IFREG)
#define S_ISDIR(m)  (((m) & S_IFMT) == S_IFDIR)
#define S_ISCHR(m)  (((m) & S_IFMT) == S_IFCHR)
#define S_ISBLK(m)  (((m) & S_IFMT) == S_IFBLK)
#define S_ISFIFO(m) (((m) & S_IFMT) == S_IFIFO)
#define S_ISLNK(m)  (((m) & S_IFMT) == S_IFLNK)
#define S_ISSOCK(m) (((m) & S_IFMT) == S_IFSOCK)

struct stat {
    dev_t     st_dev;
    ino_t     st_ino;
    mode_t    st_mode;
    nlink_t   st_nlink;
    uid_t     st_uid;
    gid_t     st_gid;
    dev_t     st_rdev;
    off_t     st_size;
    blksize_t st_blksize;
    blkcnt_t  st_blocks;
    time_t    st_atime;
    time_t    st_mtime;
    time_t    st_ctime;
};

int stat(const char *path, struct stat *buf);
int fstat(int fd, struct stat *buf);
int lstat(const char *path, struct stat *buf);
int mkdir(const char *path, mode_t mode);
int chmod(const char *path, mode_t mode);
mode_t umask(mode_t mask);

#endif /* _SYS_STAT_H */
