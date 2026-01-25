/*
 * UnixOS - Minimal C Library (libc)
 * 
 * Provides basic C runtime support for userspace programs.
 * Based on musl libc design.
 */

#ifndef _LIBC_UNISTD_H
#define _LIBC_UNISTD_H

/* Standard file descriptors */
#define STDIN_FILENO    0
#define STDOUT_FILENO   1
#define STDERR_FILENO   2

/* Types */
typedef long ssize_t;
typedef unsigned long size_t;
typedef int pid_t;
typedef unsigned int uid_t;
typedef unsigned int gid_t;
typedef long off_t;

/* NULL */
#ifndef NULL
#define NULL ((void *)0)
#endif

/* ===================================================================== */
/* System call wrappers */
/* ===================================================================== */

ssize_t read(int fd, void *buf, size_t count);
ssize_t write(int fd, const void *buf, size_t count);
int open(const char *pathname, int flags, ...);
int close(int fd);
off_t lseek(int fd, off_t offset, int whence);

pid_t fork(void);
int execve(const char *pathname, char *const argv[], char *const envp[]);
void _exit(int status) __attribute__((noreturn));

pid_t getpid(void);
pid_t getppid(void);
uid_t getuid(void);
uid_t geteuid(void);
gid_t getgid(void);
gid_t getegid(void);

int chdir(const char *path);
char *getcwd(char *buf, size_t size);

int pipe(int pipefd[2]);
int dup(int oldfd);
int dup2(int oldfd, int newfd);

unsigned int sleep(unsigned int seconds);
int usleep(unsigned int usec);

/* ===================================================================== */
/* Seek whence values */
/* ===================================================================== */

#define SEEK_SET    0
#define SEEK_CUR    1
#define SEEK_END    2

/* ===================================================================== */
/* Access mode flags */
/* ===================================================================== */

#define F_OK    0   /* Test for existence */
#define X_OK    1   /* Test for execute permission */
#define W_OK    2   /* Test for write permission */
#define R_OK    4   /* Test for read permission */

/* ===================================================================== */
/* Additional system calls */
/* ===================================================================== */

int access(const char *path, int mode);
int sethostname(const char *name, size_t len);
pid_t setsid(void);
void sync(void);
int fsync(int fd);
int unlink(const char *path);
int rmdir(const char *path);
int link(const char *oldpath, const char *newpath);
int symlink(const char *target, const char *linkpath);
ssize_t readlink(const char *path, char *buf, size_t bufsiz);
int isatty(int fd);
int brk(void *addr);
void *sbrk(long increment);
long sysconf(int name);

#endif /* _LIBC_UNISTD_H */
