/*
 * VibCode x64 - Simple Virtual Filesystem
 */

#ifndef _VFS_H
#define _VFS_H

#include "types.h"

/* File modes */
#define S_IFMT   0170000  /* Type mask */
#define S_IFREG  0100000  /* Regular file */
#define S_IFDIR  0040000  /* Directory */
#define S_ISREG(m) (((m) & S_IFMT) == S_IFREG)
#define S_ISDIR(m) (((m) & S_IFMT) == S_IFDIR)

/* Open flags */
#define O_RDONLY    0x0000
#define O_WRONLY    0x0001
#define O_RDWR      0x0002
#define O_CREAT     0x0100
#define O_TRUNC     0x0200
#define O_APPEND    0x0400

/* Seek modes */
#define SEEK_SET    0
#define SEEK_CUR    1
#define SEEK_END    2

/* Error codes */
#define ENOENT      2   /* No such file or directory */
#define EIO         5   /* I/O error */
#define EBADF       9   /* Bad file descriptor */
#define ENOMEM      12  /* Out of memory */
#define EEXIST      17  /* File exists */
#define ENOTDIR     20  /* Not a directory */
#define EISDIR      21  /* Is a directory */
#define EINVAL      22  /* Invalid argument */
#define ENOSPC      28  /* No space left */
#define ENAMETOOLONG 36 /* File name too long */
#define ENOTEMPTY   39  /* Directory not empty */

/* Maximum values */
#define MAX_PATH    256
#define MAX_NAME    64
#define MAX_FILES   256
#define MAX_OPEN    32

/* Directory entry */
typedef struct {
  char name[MAX_NAME];
  int type;           /* 0 = file, 1 = directory */
  size_t size;
} dirent_t;

/* File handle */
typedef struct {
  int valid;
  int inode;          /* Index in filesystem */
  int flags;
  size_t pos;         /* Current position */
} file_t;

/* Initialize VFS */
void vfs_init(void);

/* Seed initial filesystem content */
void vfs_seed_content(void);

/* File operations */
file_t *vfs_open(const char *path, int flags);
void vfs_close(file_t *file);
ssize_t vfs_read(file_t *file, void *buf, size_t count);
ssize_t vfs_write(file_t *file, const void *buf, size_t count);
int vfs_seek(file_t *file, int offset, int whence);

/* Directory operations */
int vfs_mkdir(const char *path);
int vfs_readdir(const char *path, dirent_t *entries, int max_entries);

/* File info */
int vfs_stat(const char *path, size_t *size, int *is_dir);
int vfs_exists(const char *path);

/* File management */
int vfs_create(const char *path);
int vfs_delete(const char *path);
int vfs_rename(const char *oldpath, const char *newpath);

#endif /* _VFS_H */
