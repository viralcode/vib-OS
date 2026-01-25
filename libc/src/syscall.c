/*
 * UnixOS - Minimal C Library Implementation
 * System Call Interface
 */

#include "../include/unistd.h"
#include "../include/sys/types.h"
#include "../include/sys/stat.h"
#include "../include/sys/wait.h"
#include "../include/signal.h"
#include "../include/fcntl.h"
#include "../include/errno.h"

/* ===================================================================== */
/* ARM64 syscall numbers (Linux ABI) */
/* ===================================================================== */

#define __NR_getcwd         17
#define __NR_dup            23
#define __NR_dup3           24
#define __NR_fcntl          25
#define __NR_ioctl          29
#define __NR_mknodat        33
#define __NR_mkdirat        34
#define __NR_unlinkat       35
#define __NR_symlinkat      36
#define __NR_linkat         37
#define __NR_renameat       38
#define __NR_chdir          49
#define __NR_fchdir         50
#define __NR_openat         56
#define __NR_close          57
#define __NR_pipe2          59
#define __NR_lseek          62
#define __NR_read           63
#define __NR_write          64
#define __NR_readv          65
#define __NR_writev         66
#define __NR_readlinkat     78
#define __NR_fstatat        79
#define __NR_fstat          80
#define __NR_sync           81
#define __NR_fsync          82
#define __NR_faccessat      48
#define __NR_exit           93
#define __NR_exit_group     94
#define __NR_nanosleep      101
#define __NR_kill           129
#define __NR_tgkill         131
#define __NR_sigaction      134
#define __NR_sigprocmask    135
#define __NR_rt_sigaction   134
#define __NR_rt_sigprocmask 135
#define __NR_rt_sigreturn   139
#define __NR_setpgid        154
#define __NR_getpgid        155
#define __NR_setsid         157
#define __NR_uname          160
#define __NR_sethostname    161
#define __NR_getpid         172
#define __NR_getppid        173
#define __NR_getuid         174
#define __NR_geteuid        175
#define __NR_getgid         176
#define __NR_getegid        177
#define __NR_gettid         178
#define __NR_sysinfo        179
#define __NR_brk            214
#define __NR_clone          220
#define __NR_execve         221
#define __NR_wait4          260

/* AT_FDCWD for *at syscalls */
#define AT_FDCWD            -100
#define AT_REMOVEDIR        0x200

/* ===================================================================== */
/* Inline syscall macros for ARM64 */
/* ===================================================================== */

static inline long __syscall0(long n)
{
    register long x0 __asm__("x0");
    register long x8 __asm__("x8") = n;
    __asm__ volatile("svc #0" : "=r"(x0) : "r"(x8) : "memory");
    return x0;
}

static inline long __syscall1(long n, long a)
{
    register long x0 __asm__("x0") = a;
    register long x8 __asm__("x8") = n;
    __asm__ volatile("svc #0" : "+r"(x0) : "r"(x8) : "memory");
    return x0;
}

static inline long __syscall2(long n, long a, long b)
{
    register long x0 __asm__("x0") = a;
    register long x1 __asm__("x1") = b;
    register long x8 __asm__("x8") = n;
    __asm__ volatile("svc #0" : "+r"(x0) : "r"(x1), "r"(x8) : "memory");
    return x0;
}

static inline long __syscall3(long n, long a, long b, long c)
{
    register long x0 __asm__("x0") = a;
    register long x1 __asm__("x1") = b;
    register long x2 __asm__("x2") = c;
    register long x8 __asm__("x8") = n;
    __asm__ volatile("svc #0" : "+r"(x0) : "r"(x1), "r"(x2), "r"(x8) : "memory");
    return x0;
}

static inline long __syscall4(long n, long a, long b, long c, long d)
{
    register long x0 __asm__("x0") = a;
    register long x1 __asm__("x1") = b;
    register long x2 __asm__("x2") = c;
    register long x3 __asm__("x3") = d;
    register long x8 __asm__("x8") = n;
    __asm__ volatile("svc #0" : "+r"(x0) : "r"(x1), "r"(x2), "r"(x3), "r"(x8) : "memory");
    return x0;
}

static inline long __syscall5(long n, long a, long b, long c, long d, long e)
{
    register long x0 __asm__("x0") = a;
    register long x1 __asm__("x1") = b;
    register long x2 __asm__("x2") = c;
    register long x3 __asm__("x3") = d;
    register long x4 __asm__("x4") = e;
    register long x8 __asm__("x8") = n;
    __asm__ volatile("svc #0" : "+r"(x0) : "r"(x1), "r"(x2), "r"(x3), "r"(x4), "r"(x8) : "memory");
    return x0;
}

static inline long __syscall6(long n, long a, long b, long c, long d, long e, long f)
{
    register long x0 __asm__("x0") = a;
    register long x1 __asm__("x1") = b;
    register long x2 __asm__("x2") = c;
    register long x3 __asm__("x3") = d;
    register long x4 __asm__("x4") = e;
    register long x5 __asm__("x5") = f;
    register long x8 __asm__("x8") = n;
    __asm__ volatile("svc #0" : "+r"(x0) : "r"(x1), "r"(x2), "r"(x3), "r"(x4), "r"(x5), "r"(x8) : "memory");
    return x0;
}

/* Helper to set errno on error */
static inline long __syscall_ret(long r)
{
    if (r < 0 && r > -4096) {
        errno = -r;
        return -1;
    }
    return r;
}

/* ===================================================================== */
/* Basic I/O System Calls */
/* ===================================================================== */

ssize_t read(int fd, void *buf, size_t count)
{
    return __syscall_ret(__syscall3(__NR_read, fd, (long)buf, count));
}

ssize_t write(int fd, const void *buf, size_t count)
{
    return __syscall_ret(__syscall3(__NR_write, fd, (long)buf, count));
}

int open(const char *pathname, int flags, ...)
{
    /* Use openat with AT_FDCWD */
    return __syscall_ret(__syscall4(__NR_openat, AT_FDCWD, (long)pathname, flags, 0644));
}

int creat(const char *pathname, mode_t mode)
{
    return open(pathname, O_CREAT | O_WRONLY | O_TRUNC, mode);
}

int close(int fd)
{
    return __syscall_ret(__syscall1(__NR_close, fd));
}

off_t lseek(int fd, off_t offset, int whence)
{
    return __syscall_ret(__syscall3(__NR_lseek, fd, offset, whence));
}

/* ===================================================================== */
/* Process Control */
/* ===================================================================== */

pid_t fork(void)
{
    /* Clone with SIGCHLD */
    return __syscall_ret(__syscall4(__NR_clone, 17, 0, 0, 0));  /* 17 = SIGCHLD */
}

int execve(const char *pathname, char *const argv[], char *const envp[])
{
    return __syscall_ret(__syscall3(__NR_execve, (long)pathname, (long)argv, (long)envp));
}

void _exit(int status)
{
    __syscall1(__NR_exit_group, status);
    __builtin_unreachable();
}

pid_t getpid(void)
{
    return __syscall0(__NR_getpid);
}

pid_t getppid(void)
{
    return __syscall0(__NR_getppid);
}

uid_t getuid(void)
{
    return __syscall0(__NR_getuid);
}

uid_t geteuid(void)
{
    return __syscall0(__NR_geteuid);
}

gid_t getgid(void)
{
    return __syscall0(__NR_getgid);
}

gid_t getegid(void)
{
    return __syscall0(__NR_getegid);
}

pid_t setsid(void)
{
    return __syscall_ret(__syscall0(__NR_setsid));
}

/* ===================================================================== */
/* Wait/Process Status */
/* ===================================================================== */

pid_t wait(int *status)
{
    return waitpid(-1, status, 0);
}

pid_t waitpid(pid_t pid, int *status, int options)
{
    return __syscall_ret(__syscall4(__NR_wait4, pid, (long)status, options, 0));
}

/* ===================================================================== */
/* Directory Operations */
/* ===================================================================== */

int chdir(const char *path)
{
    return __syscall_ret(__syscall1(__NR_chdir, (long)path));
}

char *getcwd(char *buf, size_t size)
{
    long ret = __syscall2(__NR_getcwd, (long)buf, size);
    if (ret < 0) {
        errno = -ret;
        return NULL;
    }
    return buf;
}

int mkdir(const char *path, mode_t mode)
{
    return __syscall_ret(__syscall3(__NR_mkdirat, AT_FDCWD, (long)path, mode));
}

int rmdir(const char *path)
{
    return __syscall_ret(__syscall3(__NR_unlinkat, AT_FDCWD, (long)path, AT_REMOVEDIR));
}

/* ===================================================================== */
/* File Operations */
/* ===================================================================== */

int unlink(const char *path)
{
    return __syscall_ret(__syscall3(__NR_unlinkat, AT_FDCWD, (long)path, 0));
}

int link(const char *oldpath, const char *newpath)
{
    return __syscall_ret(__syscall5(__NR_linkat, AT_FDCWD, (long)oldpath, AT_FDCWD, (long)newpath, 0));
}

int symlink(const char *target, const char *linkpath)
{
    return __syscall_ret(__syscall3(__NR_symlinkat, (long)target, AT_FDCWD, (long)linkpath));
}

ssize_t readlink(const char *path, char *buf, size_t bufsiz)
{
    return __syscall_ret(__syscall4(__NR_readlinkat, AT_FDCWD, (long)path, (long)buf, bufsiz));
}

int access(const char *path, int mode)
{
    return __syscall_ret(__syscall4(__NR_faccessat, AT_FDCWD, (long)path, mode, 0));
}

int stat(const char *path, struct stat *buf)
{
    return __syscall_ret(__syscall4(__NR_fstatat, AT_FDCWD, (long)path, (long)buf, 0));
}

int fstat(int fd, struct stat *buf)
{
    return __syscall_ret(__syscall2(__NR_fstat, fd, (long)buf));
}

int lstat(const char *path, struct stat *buf)
{
    /* AT_SYMLINK_NOFOLLOW = 0x100 */
    return __syscall_ret(__syscall4(__NR_fstatat, AT_FDCWD, (long)path, (long)buf, 0x100));
}

/* ===================================================================== */
/* File Descriptor Operations */
/* ===================================================================== */

int pipe(int pipefd[2])
{
    return __syscall_ret(__syscall2(__NR_pipe2, (long)pipefd, 0));
}

int dup(int oldfd)
{
    return __syscall_ret(__syscall1(__NR_dup, oldfd));
}

int dup2(int oldfd, int newfd)
{
    if (oldfd == newfd) return newfd;
    return __syscall_ret(__syscall3(__NR_dup3, oldfd, newfd, 0));
}

int fcntl(int fd, int cmd, ...)
{
    /* Simplified: only handles commands without extra args */
    return __syscall_ret(__syscall3(__NR_fcntl, fd, cmd, 0));
}

int isatty(int fd)
{
    /* Use ioctl with TCGETS (0x5401) to check if it's a terminal */
    char termios[60];  /* struct termios is ~60 bytes */
    int ret = __syscall3(__NR_ioctl, fd, 0x5401, (long)termios);
    return ret == 0 ? 1 : 0;
}

/* ===================================================================== */
/* Signal System Calls */
/* ===================================================================== */

int kill(pid_t pid, int sig)
{
    return __syscall_ret(__syscall2(__NR_kill, pid, sig));
}

int raise(int sig)
{
    return kill(getpid(), sig);
}

int sigaction(int sig, const struct sigaction *act, struct sigaction *oldact)
{
    /* ARM64 uses rt_sigaction with sigset size of 8 bytes */
    return __syscall_ret(__syscall4(__NR_rt_sigaction, sig, (long)act, (long)oldact, 8));
}

int sigprocmask(int how, const sigset_t *set, sigset_t *oldset)
{
    return __syscall_ret(__syscall4(__NR_rt_sigprocmask, how, (long)set, (long)oldset, 8));
}

int pause(void)
{
    /* Use ppoll with NULL timeout as pause equivalent */
    sigset_t emptyset = 0;
    return __syscall_ret(__syscall4(__NR_rt_sigprocmask, 0, 0, 0, 8));
}

/* ===================================================================== */
/* Sync Operations */
/* ===================================================================== */

void sync(void)
{
    __syscall0(__NR_sync);
}

int fsync(int fd)
{
    return __syscall_ret(__syscall1(__NR_fsync, fd));
}

/* ===================================================================== */
/* System Information */
/* ===================================================================== */

int sethostname(const char *name, size_t len)
{
    return __syscall_ret(__syscall2(__NR_sethostname, (long)name, len));
}

long sysconf(int name)
{
    /* Return some sensible defaults */
    switch (name) {
        case 30: return 4096;    /* _SC_PAGESIZE */
        case 84: return 1;       /* _SC_NPROCESSORS_ONLN */
        default: return -1;
    }
}

/* ===================================================================== */
/* Memory Management */
/* ===================================================================== */

static void *__brk_cur = 0;

int brk(void *addr)
{
    void *newbrk = (void *)__syscall1(__NR_brk, (long)addr);
    __brk_cur = newbrk;
    return (newbrk < addr) ? -1 : 0;
}

void *sbrk(long increment)
{
    if (!__brk_cur) {
        __brk_cur = (void *)__syscall1(__NR_brk, 0);
    }
    
    if (increment == 0) {
        return __brk_cur;
    }
    
    void *old = __brk_cur;
    void *newbrk = (void *)__syscall1(__NR_brk, (long)__brk_cur + increment);
    
    if (newbrk == old) {
        errno = ENOMEM;
        return (void *)-1;
    }
    
    __brk_cur = newbrk;
    return old;
}

/* ===================================================================== */
/* Time Functions */
/* ===================================================================== */

unsigned int sleep(unsigned int seconds)
{
    struct {
        long tv_sec;
        long tv_nsec;
    } ts = { seconds, 0 }, rem = { 0, 0 };
    
    if (__syscall2(__NR_nanosleep, (long)&ts, (long)&rem) < 0) {
        return rem.tv_sec;
    }
    return 0;
}

int usleep(unsigned int usec)
{
    struct {
        long tv_sec;
        long tv_nsec;
    } ts = { usec / 1000000, (usec % 1000000) * 1000 };
    
    return __syscall_ret(__syscall2(__NR_nanosleep, (long)&ts, 0));
}
