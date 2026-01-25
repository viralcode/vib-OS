/*
 * Vib-OS libc - fcntl.h
 */

#ifndef _FCNTL_H
#define _FCNTL_H

#include <sys/types.h>

/* Open flags */
#define O_RDONLY    00000000
#define O_WRONLY    00000001
#define O_RDWR      00000002
#define O_ACCMODE   00000003

#define O_CREAT     00000100
#define O_EXCL      00000200
#define O_NOCTTY    00000400
#define O_TRUNC     00001000
#define O_APPEND    00002000
#define O_NONBLOCK  00004000
#define O_SYNC      00010000
#define O_DSYNC     O_SYNC
#define O_RSYNC     O_SYNC

#define O_DIRECTORY 00200000
#define O_NOFOLLOW  00400000
#define O_CLOEXEC   02000000

/* fcntl commands */
#define F_DUPFD     0
#define F_GETFD     1
#define F_SETFD     2
#define F_GETFL     3
#define F_SETFL     4
#define F_GETLK     5
#define F_SETLK     6
#define F_SETLKW    7

#define FD_CLOEXEC  1

int open(const char *path, int flags, ...);
int creat(const char *path, mode_t mode);
int fcntl(int fd, int cmd, ...);

#endif /* _FCNTL_H */
