/*
 * UnixOS Kernel - System Call Implementation
 */

#include "syscall/syscall.h"
#include "sched/sched.h"
#include "fs/vfs.h"
#include "mm/kmalloc.h"
#include "printk.h"
#include "drivers/uart.h"
#include "arch/arch.h"

/* ===================================================================== */
/* File Descriptor Table */
/* ===================================================================== */

#define MAX_FDS 256

/* File descriptor entry */
struct fd_entry {
    struct file *file;
    int flags;
    int in_use;
};

/* Global FD table (per-process would be better, but simpler for now) */
static struct fd_entry fd_table[MAX_FDS];
static int fd_table_initialized = 0;

static void init_fd_table(void)
{
    if (fd_table_initialized) return;
    
    for (int i = 0; i < MAX_FDS; i++) {
        fd_table[i].file = NULL;
        fd_table[i].flags = 0;
        fd_table[i].in_use = 0;
    }
    
    /* Reserve stdin/stdout/stderr */
    fd_table[0].in_use = 1;  /* stdin */
    fd_table[1].in_use = 1;  /* stdout */
    fd_table[2].in_use = 1;  /* stderr */
    
    fd_table_initialized = 1;
}

static int alloc_fd(void)
{
    init_fd_table();
    for (int i = 3; i < MAX_FDS; i++) {
        if (!fd_table[i].in_use) {
            fd_table[i].in_use = 1;
            return i;
        }
    }
    return -1;
}

static void free_fd(int fd)
{
    if (fd >= 0 && fd < MAX_FDS) {
        fd_table[fd].file = NULL;
        fd_table[fd].flags = 0;
        fd_table[fd].in_use = 0;
    }
}

static struct file *get_file(int fd)
{
    if (fd < 0 || fd >= MAX_FDS || !fd_table[fd].in_use) {
        return NULL;
    }
    return fd_table[fd].file;
}

/* ===================================================================== */
/* System call table */
/* ===================================================================== */

typedef long (*syscall_fn_t)(uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t);

static syscall_fn_t syscall_table[NR_syscalls];

/* ===================================================================== */
/* System call implementations */
/* ===================================================================== */

static long sys_read(uint64_t fd, uint64_t buf, uint64_t count, uint64_t a3, uint64_t a4, uint64_t a5)
{
    (void)a3; (void)a4; (void)a5;
    
    init_fd_table();
    
    /* Handle stdin specially */
    if (fd == 0) {
        /* For now, stdin is not supported */
        return 0;
    }
    
    struct file *f = get_file((int)fd);
    if (!f) {
        return -EBADF;
    }
    
    return vfs_read(f, (char *)buf, count);
}

static long sys_write(uint64_t fd, uint64_t buf, uint64_t count, uint64_t a3, uint64_t a4, uint64_t a5)
{
    (void)a3; (void)a4; (void)a5;
    
    init_fd_table();
    
    /* Special case: stdout/stderr (fd 1 and 2) go to console */
    if (fd == 1 || fd == 2) {
        const char *str = (const char *)buf;
        for (size_t i = 0; i < count; i++) {
            uart_putc(str[i]);
        }
        return count;
    }
    
    struct file *f = get_file((int)fd);
    if (!f) {
        return -EBADF;
    }
    
    return vfs_write(f, (const char *)buf, count);
}

static long sys_openat(uint64_t dirfd, uint64_t pathname, uint64_t flags, uint64_t mode, uint64_t a4, uint64_t a5)
{
    (void)a4; (void)a5;
    (void)dirfd;  /* TODO: Handle relative paths with dirfd */
    
    init_fd_table();
    
    const char *path = (const char *)pathname;
    printk(KERN_DEBUG "sys_openat: '%s' flags=0x%llx mode=0%llo\n", path, (unsigned long long)flags, (unsigned long long)mode);
    
    /* Allocate file descriptor */
    int fd = alloc_fd();
    if (fd < 0) {
        return -EMFILE;  /* Too many open files */
    }
    
    /* Open the file */
    struct file *f = vfs_open(path, (int)flags, (mode_t)mode);
    if (!f) {
        free_fd(fd);
        return -ENOENT;
    }
    
    fd_table[fd].file = f;
    fd_table[fd].flags = (int)flags;
    
    return fd;
}

static long sys_close(uint64_t fd, uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5)
{
    (void)a1; (void)a2; (void)a3; (void)a4; (void)a5;
    
    init_fd_table();
    
    /* Don't close stdin/stdout/stderr */
    if (fd < 3) {
        return 0;
    }
    
    struct file *f = get_file((int)fd);
    if (!f) {
        return -EBADF;
    }
    
    vfs_close(f);
    free_fd((int)fd);
    
    return 0;
}

static long sys_lseek(uint64_t fd, uint64_t offset, uint64_t whence, uint64_t a3, uint64_t a4, uint64_t a5)
{
    (void)a3; (void)a4; (void)a5;
    
    init_fd_table();
    
    struct file *f = get_file((int)fd);
    if (!f) {
        return -EBADF;
    }
    
    return vfs_lseek(f, (loff_t)offset, (int)whence);
}

static long sys_exit(uint64_t error_code, uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5)
{
    (void)a1; (void)a2; (void)a3; (void)a4; (void)a5;
    
    printk(KERN_INFO "sys_exit: code=%llu\n", (unsigned long long)error_code);
    exit_task((int)error_code);
    
    /* Never reached */
    return 0;
}

static long sys_exit_group(uint64_t error_code, uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5)
{
    return sys_exit(error_code, a1, a2, a3, a4, a5);
}

static long sys_getpid(uint64_t a0, uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5)
{
    (void)a0; (void)a1; (void)a2; (void)a3; (void)a4; (void)a5;
    
    struct task_struct *current = get_current();
    return current ? current->pid : -1;
}

static long sys_getppid(uint64_t a0, uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5)
{
    (void)a0; (void)a1; (void)a2; (void)a3; (void)a4; (void)a5;
    
    struct task_struct *current = get_current();
    if (current && current->parent) {
        return current->parent->pid;
    }
    return 0;
}

static long sys_getuid(uint64_t a0, uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5)
{
    (void)a0; (void)a1; (void)a2; (void)a3; (void)a4; (void)a5;
    
    struct task_struct *current = get_current();
    return current ? current->uid : 0;
}

static long sys_getgid(uint64_t a0, uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5)
{
    (void)a0; (void)a1; (void)a2; (void)a3; (void)a4; (void)a5;
    
    struct task_struct *current = get_current();
    return current ? current->gid : 0;
}

static long sys_gettid(uint64_t a0, uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5)
{
    (void)a0; (void)a1; (void)a2; (void)a3; (void)a4; (void)a5;
    
    struct task_struct *current = get_current();
    return current ? current->pid : -1;
}

/* Userspace heap management - dedicated region for userspace processes */
#define USER_HEAP_START 0x10000000UL  /* 256MB mark */
#define USER_HEAP_SIZE  0x04000000UL  /* 64MB heap */
static uint64_t user_brk_current = USER_HEAP_START;
static uint64_t user_mmap_current = USER_HEAP_START + USER_HEAP_SIZE / 2;  /* mmap from middle */

static long sys_brk(uint64_t brk, uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5)
{
    (void)a1; (void)a2; (void)a3; (void)a4; (void)a5;
    
    /* If brk is 0 or less than start, return current brk */
    if (brk == 0 || brk < USER_HEAP_START) {
        return user_brk_current;
    }
    
    /* Check bounds */
    if (brk > USER_HEAP_START + USER_HEAP_SIZE / 2) {
        /* Would overlap with mmap region */
        return user_brk_current;
    }
    
    /* Extend brk */
    user_brk_current = brk;
    return user_brk_current;
}

static long sys_mmap(uint64_t addr, uint64_t len, uint64_t prot, uint64_t flags, uint64_t fd, uint64_t offset)
{
    (void)addr; (void)prot; (void)offset;
    
    /* Only support anonymous mappings for now */
    #define MAP_ANONYMOUS 0x20
    if (!(flags & MAP_ANONYMOUS) || (int64_t)fd != -1) {
        printk(KERN_DEBUG "sys_mmap: only anonymous mappings supported\n");
        return -ENOSYS;
    }
    
    /* Align len to page size */
    #define PAGE_SIZE 4096UL
    len = (len + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
    
    /* Check bounds */
    if (user_mmap_current + len > USER_HEAP_START + USER_HEAP_SIZE) {
        printk(KERN_WARNING "sys_mmap: out of memory\n");
        return -ENOMEM;
    }
    
    /* Allocate from mmap region */
    uint64_t result = user_mmap_current;
    user_mmap_current += len;
    
    /* Zero the memory */
    uint8_t *p = (uint8_t *)result;
    for (size_t i = 0; i < len; i++) p[i] = 0;
    
    return result;
}

static long sys_munmap(uint64_t addr, uint64_t len, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5)
{
    (void)addr; (void)len; (void)a2; (void)a3; (void)a4; (void)a5;
    
    /* For now, just no-op munmap - memory is not reclaimed */
    return 0;
}

static long sys_clone(uint64_t flags, uint64_t stack, uint64_t ptid, uint64_t tls, uint64_t ctid, uint64_t a5)
{
    (void)tls; (void)a5;
    
    printk(KERN_DEBUG "sys_clone: flags=0x%llx stack=0x%llx\n", 
           (unsigned long long)flags, (unsigned long long)stack);
    
    /* Get parent task's entry point from return address */
    /* For threads, the entry is typically set after clone returns */
    
    /* Create thread using scheduler */
    extern pid_t create_thread(void (*entry)(void *), void *arg, void *stack, uint32_t clone_flags);
    
    /* The entry point will be the instruction after the syscall */
    /* Stack is already set up by userspace */
    pid_t tid = create_thread(NULL, NULL, (void *)stack, (uint32_t)flags);
    
    if (tid < 0) {
        return -EAGAIN;
    }
    
    /* Store TID in parent if requested */
    if ((flags & CLONE_PARENT_SETTID) && ptid) {
        *(pid_t *)ptid = tid;
    }
    
    /* Store TID in child if requested */
    if ((flags & CLONE_CHILD_SETTID) && ctid) {
        *(pid_t *)ctid = tid;
    }
    
    return tid;
}

/* Forward declarations for ELF loader */
extern int elf_validate(const void *data, size_t size);
extern uint64_t elf_calc_size(const void *data, size_t size);
extern int elf_load_at(const void *data, size_t size, uint64_t load_base, void *info);

/* Architecture-specific function to jump to userspace */
extern void arch_enter_userspace(uint64_t entry, uint64_t sp, uint64_t argc, uint64_t argv);

/* Helper: copy string to user stack and return new stack pointer */
static uint64_t push_string_to_stack(uint64_t sp, const char *str)
{
    size_t len = 0;
    while (str[len]) len++;
    len++; /* Include null terminator */
    
    sp -= len;
    sp &= ~7ULL; /* 8-byte align */
    
    char *dest = (char *)sp;
    for (size_t i = 0; i < len; i++) {
        dest[i] = str[i];
    }
    return sp;
}

/* Helper: count strings in NULL-terminated array */
static int count_strings(char **arr)
{
    if (!arr) return 0;
    int count = 0;
    while (arr[count]) count++;
    return count;
}

static long sys_execve(uint64_t filename, uint64_t argv, uint64_t envp, uint64_t a3, uint64_t a4, uint64_t a5)
{
    (void)a3; (void)a4; (void)a5;
    
    const char *path = (const char *)filename;
    char **user_argv = (char **)argv;
    char **user_envp = (char **)envp;
    
    printk(KERN_INFO "sys_execve: loading '%s'\n", path);
    
    /* Open the file */
    struct file *f = vfs_open(path, O_RDONLY, 0);
    if (!f) {
        printk(KERN_ERR "sys_execve: cannot open '%s'\n", path);
        return -ENOENT;
    }
    
    /* Get file size via dentry->inode */
    size_t file_size = 0;
    if (f->f_dentry && f->f_dentry->d_inode) {
        file_size = f->f_dentry->d_inode->i_size;
    }
    if (file_size == 0 || file_size > 64 * 1024 * 1024) {
        vfs_close(f);
        return -ENOEXEC;
    }
    
    /* Allocate buffer and read file */
    uint8_t *buf = kmalloc(file_size);
    if (!buf) {
        vfs_close(f);
        return -ENOMEM;
    }
    
    ssize_t bytes_read = vfs_read(f, (char *)buf, file_size);
    vfs_close(f);
    
    if (bytes_read != (ssize_t)file_size) {
        kfree(buf);
        return -EIO;
    }
    
    /* Validate ELF */
    int ret = elf_validate(buf, file_size);
    if (ret != 0) {
        printk(KERN_ERR "sys_execve: invalid ELF (error %d)\n", ret);
        kfree(buf);
        return -ENOEXEC;
    }
    
    /* Calculate memory needed */
    uint64_t mem_size = elf_calc_size(buf, file_size);
    if (mem_size == 0) {
        kfree(buf);
        return -ENOEXEC;
    }
    
    /* Load at user code base */
    typedef struct {
        uint64_t entry;
        uint64_t load_base;
        uint64_t load_size;
    } elf_load_info_t;
    
    elf_load_info_t info;
    ret = elf_load_at(buf, file_size, USER_CODE_BASE, &info);
    kfree(buf);
    
    if (ret != 0) {
        printk(KERN_ERR "sys_execve: ELF load failed\n");
        return -ENOEXEC;
    }
    
    printk(KERN_INFO "sys_execve: loaded at 0x%llx, entry 0x%llx\n",
           (unsigned long long)info.load_base, (unsigned long long)info.entry);
    
    /* Get current task and set up for userspace execution */
    struct task_struct *current = get_current();
    if (!current) {
        return -ESRCH;
    }
    
    current->flags |= PF_USER;
    current->flags &= ~PF_KTHREAD;
    
    /* Update task name (extract basename from path) */
    const char *basename = path;
    for (const char *p = path; *p; p++) {
        if (*p == '/') basename = p + 1;
    }
    int i = 0;
    while (basename[i] && i < TASK_COMM_LEN - 1) {
        current->comm[i] = basename[i];
        i++;
    }
    current->comm[i] = '\0';
    
    /* Set up user stack */
    uint64_t user_sp = USER_STACK_TOP;
    
    /* Count argc and envp */
    int argc = count_strings(user_argv);
    int envc = count_strings(user_envp);
    
    /* Allocate space for string pointers on stack */
    /* Stack layout (grows down):
     *   [strings...]      - actual string data
     *   NULL              - end of envp
     *   envp[envc-1]      - environment pointers
     *   ...
     *   envp[0]
     *   NULL              - end of argv
     *   argv[argc-1]      - argument pointers
     *   ...
     *   argv[0]
     *   argc              - argument count
     *   <- SP points here
     */
    
    /* Push environment strings and collect pointers */
    uint64_t env_ptrs[64]; /* Max 64 env vars */
    for (int j = envc - 1; j >= 0; j--) {
        user_sp = push_string_to_stack(user_sp, user_envp[j]);
        env_ptrs[j] = user_sp;
    }
    
    /* Push argument strings and collect pointers */
    uint64_t arg_ptrs[64]; /* Max 64 args */
    for (int j = argc - 1; j >= 0; j--) {
        user_sp = push_string_to_stack(user_sp, user_argv[j]);
        arg_ptrs[j] = user_sp;
    }
    
    /* If no argv provided, use path as argv[0] */
    if (argc == 0) {
        user_sp = push_string_to_stack(user_sp, path);
        arg_ptrs[0] = user_sp;
        argc = 1;
    }
    
    /* Align stack to 16 bytes */
    user_sp &= ~15ULL;
    
    /* Push NULL terminator for envp */
    user_sp -= 8;
    *(uint64_t *)user_sp = 0;
    
    /* Push envp pointers */
    for (int j = envc - 1; j >= 0; j--) {
        user_sp -= 8;
        *(uint64_t *)user_sp = env_ptrs[j];
    }
    uint64_t envp_start = user_sp;
    
    /* Push NULL terminator for argv */
    user_sp -= 8;
    *(uint64_t *)user_sp = 0;
    
    /* Push argv pointers */
    for (int j = argc - 1; j >= 0; j--) {
        user_sp -= 8;
        *(uint64_t *)user_sp = arg_ptrs[j];
    }
    uint64_t argv_start = user_sp;
    
    /* Push argc */
    user_sp -= 8;
    *(uint64_t *)user_sp = argc;
    
    /* Final 16-byte alignment for ABI compliance */
    user_sp &= ~15ULL;
    
    printk(KERN_INFO "sys_execve: user stack at 0x%llx, argc=%d\n",
           (unsigned long long)user_sp, argc);
    
    /* Set up mm_struct for user address space if not present */
    if (!current->mm) {
        current->mm = kmalloc(sizeof(struct mm_struct));
        if (current->mm) {
            current->mm->pgd = NULL; /* Use kernel page tables for now */
            current->mm->start_code = info.load_base;
            current->mm->end_code = info.load_base + info.load_size;
            current->mm->start_data = 0;
            current->mm->end_data = 0;
            current->mm->start_brk = USER_HEAP_BASE;
            current->mm->brk = USER_HEAP_BASE;
            current->mm->start_stack = user_sp;
            current->mm->arg_start = argv_start;
            current->mm->arg_end = envp_start;
            current->mm->env_start = envp_start;
            current->mm->env_end = USER_STACK_TOP;
            atomic_set(&current->mm->users, 1);
            current->active_mm = current->mm;
        }
    }
    
    /* Jump to userspace - this function does not return on success */
    arch_enter_userspace(info.entry, user_sp, argc, argv_start);
    
    /* Should not reach here */
    return -EFAULT;
}

static long sys_uname(uint64_t buf, uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5)
{
    (void)a1; (void)a2; (void)a3; (void)a4; (void)a5;
    
    struct utsname {
        char sysname[65];
        char nodename[65];
        char release[65];
        char version[65];
        char machine[65];
        char domainname[65];
    };
    
    struct utsname *uts = (struct utsname *)buf;
    
    /* Copy strings (simple implementation) */
    const char *sysname = "UnixOS";
    const char *nodename = "localhost";
    const char *release = "0.1.0";
    const char *version = "0.1.0-arm64";
    const char *machine = "aarch64";
    const char *domain = "";
    
    for (int i = 0; i < 64 && sysname[i]; i++) uts->sysname[i] = sysname[i];
    uts->sysname[64] = 0;
    for (int i = 0; i < 64 && nodename[i]; i++) uts->nodename[i] = nodename[i];
    uts->nodename[64] = 0;
    for (int i = 0; i < 64 && release[i]; i++) uts->release[i] = release[i];
    uts->release[64] = 0;
    for (int i = 0; i < 64 && version[i]; i++) uts->version[i] = version[i];
    uts->version[64] = 0;
    for (int i = 0; i < 64 && machine[i]; i++) uts->machine[i] = machine[i];
    uts->machine[64] = 0;
    for (int i = 0; i < 64 && domain[i]; i++) uts->domainname[i] = domain[i];
    uts->domainname[64] = 0;
    
    return 0;
}

static long sys_sched_yield(uint64_t a0, uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5)
{
    (void)a0; (void)a1; (void)a2; (void)a3; (void)a4; (void)a5;
    
    schedule();
    return 0;
}

static long sys_nanosleep(uint64_t req, uint64_t rem, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5)
{
    (void)rem; (void)a2; (void)a3; (void)a4; (void)a5;
    
    /* TODO: Implement proper sleep */
    (void)req;
    
    return 0;
}

static long sys_not_implemented(uint64_t a0, uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5)
{
    (void)a0; (void)a1; (void)a2; (void)a3; (void)a4; (void)a5;
    return -ENOSYS;
}

/* Sound System Call */
#include "drivers/intel_hda.h"

static long sys_sound_play(uint64_t data, uint64_t samples, uint64_t channels, uint64_t rate, uint64_t a4, uint64_t a5)
{
    (void)a4; (void)a5;
    
    /* Check pointer validity (basic) */
    if (data < 0x10000000) return -EFAULT; /* Below userspace heap? Adjust check as needed */
    
    /* Call HDA driver */
    /* Note: data is a user virtual address. HDA DMA needs physical. 
       However, our kernel mapping is currently flat/identity or we mapped userspace?
       The syscall receives arguments. pointer is userspace VA.
       Current 'kmalloc' logic in HDA uses a kernel buffer and copies data.
       So we need to access user memory.
       For now, assuming shared address space or we can read it.
    */
    
    return intel_hda_play_pcm((const void *)data, (uint32_t)samples, (uint8_t)channels, (uint32_t)rate);
}


/* ===================================================================== */
/* Syscall initialization */
/* ===================================================================== */

void syscall_init(void)
{
    printk(KERN_INFO "SYSCALL: Initializing system call table\n");
    
    /* Initialize all to not implemented */
    for (int i = 0; i < NR_syscalls; i++) {
        syscall_table[i] = sys_not_implemented;
    }
    
    /* Register implemented syscalls */
    syscall_table[SYS_read] = sys_read;
    syscall_table[SYS_write] = sys_write;
    syscall_table[SYS_openat] = sys_openat;
    syscall_table[SYS_close] = sys_close;
    syscall_table[SYS_lseek] = sys_lseek;
    syscall_table[SYS_exit] = sys_exit;
    syscall_table[SYS_exit_group] = sys_exit_group;
    syscall_table[SYS_getpid] = sys_getpid;
    syscall_table[SYS_getppid] = sys_getppid;
    syscall_table[SYS_getuid] = sys_getuid;
    syscall_table[SYS_geteuid] = sys_getuid;
    syscall_table[SYS_getgid] = sys_getgid;
    syscall_table[SYS_getegid] = sys_getgid;
    syscall_table[SYS_gettid] = sys_gettid;
    syscall_table[SYS_brk] = sys_brk;
    syscall_table[SYS_mmap] = sys_mmap;
    syscall_table[SYS_munmap] = sys_munmap;
    syscall_table[SYS_clone] = sys_clone;
    syscall_table[SYS_execve] = sys_execve;
    syscall_table[SYS_uname] = sys_uname;
    syscall_table[SYS_sched_yield] = sys_sched_yield;
    syscall_table[SYS_nanosleep] = sys_nanosleep;
    
    printk(KERN_INFO "SYSCALL: System call table initialized\n");
}

/* ===================================================================== */
/* Syscall dispatcher */
/* ===================================================================== */

long handle_syscall(struct pt_regs *regs)
{
    /* ARM64 syscall convention:
     * x8 = syscall number
     * x0-x5 = arguments
     * x0 = return value
     */
    
    uint64_t nr = regs->regs[8];
    
    if (nr >= NR_syscalls) {
        printk(KERN_WARNING "SYSCALL: Invalid syscall number %llu\n", (unsigned long long)nr);
        return -ENOSYS;
    }
    
    syscall_fn_t fn = syscall_table[nr];
    
    return fn(regs->regs[0], regs->regs[1], regs->regs[2],
              regs->regs[3], regs->regs[4], regs->regs[5]);
}

/* ===================================================================== */
/* Exception handler */
/* ===================================================================== */

void handle_sync_exception(struct pt_regs *regs)
{
    /* Read exception syndrome register - architecture specific */
    uint32_t ec, iss;
    
#ifdef ARCH_ARM64
    uint64_t esr;
    asm volatile("mrs %0, esr_el1" : "=r" (esr));
    
    ec = (esr >> 26) & 0x3F;  /* Exception class */
    iss = esr & 0x1FFFFFF;    /* Instruction specific syndrome */
#elif defined(ARCH_X86_64) || defined(ARCH_X86)
    /* x86 uses interrupt numbers instead of ESR */
    ec = 0;  /* Not used on x86 */
    iss = 0;
#endif
    
    switch (ec) {
        case 0x15:  /* SVC instruction from AArch64 */
            /* System call - handled separately */
            break;
            
        case 0x20:  /* Instruction abort from lower EL */
        case 0x21:  /* Instruction abort from same EL */
            printk(KERN_EMERG "Instruction abort at PC=0x%llx\n", 
                   (unsigned long long)arch_context_get_pc(regs));
            panic("Instruction abort");
            break;
            
        case 0x24:  /* Data abort from lower EL */
        case 0x25:  /* Data abort from same EL */
            {
#ifdef ARCH_ARM64
                uint64_t far;
                asm volatile("mrs %0, far_el1" : "=r" (far));
                printk(KERN_EMERG "Data abort at PC=0x%llx, FAR=0x%llx\n", 
                       (unsigned long long)arch_context_get_pc(regs), (unsigned long long)far);
#elif defined(ARCH_X86_64) || defined(ARCH_X86)
                uint64_t cr2;
                asm volatile("mov %%cr2, %0" : "=r"(cr2));
                printk(KERN_EMERG "Page fault at PC=0x%llx, CR2=0x%llx\n",
                       (unsigned long long)arch_context_get_pc(regs), (unsigned long long)cr2);
#endif
                panic("Data abort");
            }
            break;
            
        case 0x00:  /* Unknown reason */
            printk(KERN_EMERG "Unknown exception at PC=0x%llx\n", 
                   (unsigned long long)arch_context_get_pc(regs));
            panic("Unknown exception");
            break;
            
        default:
            printk(KERN_EMERG "Unhandled exception class 0x%x, ISS=0x%x\n", ec, iss);
            printk(KERN_EMERG "PC=0x%llx\n", 
                   (unsigned long long)arch_context_get_pc(regs));
            panic("Unhandled exception");
            break;
    }
}
