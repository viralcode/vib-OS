/*
 * UnixOS Kernel - Scheduler Header
 */

#ifndef _SCHED_SCHED_H
#define _SCHED_SCHED_H

#include "types.h"
#include "mm/vmm.h"

/* ===================================================================== */
/* Process states */
/* ===================================================================== */

typedef enum {
    TASK_RUNNING,       /* Currently running or ready to run */
    TASK_INTERRUPTIBLE, /* Sleeping, can be woken by signal */
    TASK_UNINTERRUPTIBLE, /* Sleeping, cannot be interrupted */
    TASK_STOPPED,       /* Stopped by signal */
    TASK_ZOMBIE,        /* Terminated, waiting for parent */
    TASK_DEAD           /* Being removed */
} task_state_t;

/* ===================================================================== */
/* Process priorities */
/* ===================================================================== */

#define PRIO_MIN            -20
#define PRIO_MAX            19
#define PRIO_DEFAULT        0

#define NICE_MIN            -20
#define NICE_MAX            19

/* ===================================================================== */
/* CPU context for ARM64 */
/* ===================================================================== */

struct cpu_context {
    uint64_t x19;
    uint64_t x20;
    uint64_t x21;
    uint64_t x22;
    uint64_t x23;
    uint64_t x24;
    uint64_t x25;
    uint64_t x26;
    uint64_t x27;
    uint64_t x28;
    uint64_t fp;        /* x29 - frame pointer */
    uint64_t sp;        /* Stack pointer */
    uint64_t pc;        /* Program counter (return address) */
};

/* ===================================================================== */
/* Simple list helper */
/* ===================================================================== */

struct list_head {
    struct list_head *next;
    struct list_head *prev;
};

/* ===================================================================== */
/* Task structure */
/* ===================================================================== */

#define TASK_COMM_LEN       16

struct task_struct {
    /* Scheduling info */
    volatile task_state_t state;
    int prio;
    int static_prio;
    int nice;
    
    /* Identifiers */
    pid_t pid;
    pid_t tgid;         /* Thread group ID */
    uid_t uid;
    gid_t gid;
    
    /* Process name */
    char comm[TASK_COMM_LEN];
    
    /* CPU context for context switching */
    struct cpu_context cpu_context;
    
    /* Memory management */
    struct mm_struct *mm;           /* User address space */
    struct mm_struct *active_mm;    /* Current address space */
    
    /* Kernel stack */
    void *stack;
    size_t stack_size;
    
    /* Process hierarchy */
    struct task_struct *parent;
    struct list_head children;
    struct list_head sibling;
    
    /* Scheduler links */
    struct task_struct *next;       /* Run queue next */
    struct task_struct *prev;       /* Run queue prev */
    
    /* Timing */
    uint64_t start_time;
    uint64_t utime;     /* User time */
    uint64_t stime;     /* System time */
    
    /* Exit info */
    int exit_code;
    int exit_signal;
    
    /* Signal handling */
    struct signal_struct *signals;
    uint64_t pending_signals;
    uint64_t blocked_signals;
    
    /* Flags */
    uint32_t flags;
};

/* Task flags */
#define PF_KTHREAD          (1 << 0)    /* Kernel thread */
#define PF_EXITING          (1 << 1)    /* Being killed */
#define PF_IDLE             (1 << 2)    /* Idle task */
#define PF_USER             (1 << 3)    /* User process (runs at EL0) */
#define PF_FORKNOEXEC       (1 << 4)    /* Forked but not yet exec'd */
#define PF_THREAD           (1 << 5)    /* This is a thread (shares mm with parent) */

/* User process memory layout */
#define USER_STACK_TOP      0x7FFFFFFFF000ULL   /* Top of user stack */
#define USER_STACK_SIZE     (2 * 1024 * 1024)   /* 2MB user stack */
#define USER_CODE_BASE      0x400000ULL         /* User code start */
#define USER_HEAP_BASE      0x10000000ULL       /* User heap start */
#define USER_MMAP_BASE      0x7F0000000000ULL   /* mmap region */

/* Clone flags for threading */
#define CLONE_VM            0x00000100  /* Share memory space */
#define CLONE_FS            0x00000200  /* Share filesystem info */
#define CLONE_FILES         0x00000400  /* Share file descriptors */
#define CLONE_SIGHAND       0x00000800  /* Share signal handlers */
#define CLONE_THREAD        0x00010000  /* Same thread group */
#define CLONE_PARENT_SETTID 0x00100000  /* Set TID in parent */
#define CLONE_CHILD_CLEARTID 0x00200000 /* Clear TID in child on exit */
#define CLONE_CHILD_SETTID  0x01000000  /* Set TID in child */

/* ===================================================================== */
/* Per-CPU run queue */
/* ===================================================================== */

struct rq {
    struct task_struct *current;    /* Currently running task */
    struct task_struct *idle;       /* Idle task */
    struct task_struct *head;       /* Run queue head */
    struct task_struct *tail;       /* Run queue tail */
    unsigned int nr_running;        /* Number of runnable tasks */
    uint64_t clock;                 /* Run queue clock */
};

/* ===================================================================== */
/* Function declarations */
/* ===================================================================== */

/**
 * sched_init - Initialize the scheduler
 */
void sched_init(void);

/**
 * schedule - Invoke the scheduler
 * 
 * Selects the next task to run and performs context switch.
 */
void schedule(void);

/**
 * wake_up_process - Wake up a sleeping process
 * @task: Task to wake up
 * 
 * Return: 1 if task was woken, 0 if already running
 */
int wake_up_process(struct task_struct *task);

/**
 * create_task - Create a new task
 * @entry: Entry point function
 * @arg: Argument to pass to entry
 * @flags: Task creation flags
 * 
 * Return: Pointer to new task, or NULL on failure
 */
struct task_struct *create_task(void (*entry)(void *), void *arg, uint32_t flags);

/**
 * create_thread - Create a new thread (shares memory with parent)
 * @entry: Entry point function
 * @arg: Argument to pass to entry
 * @stack: User stack pointer (top of stack)
 * @clone_flags: Clone flags (CLONE_VM, CLONE_THREAD, etc.)
 * 
 * Return: TID of new thread, or negative on failure
 */
pid_t create_thread(void (*entry)(void *), void *arg, void *stack, uint32_t clone_flags);

/**
 * get_task_by_pid - Find a task by PID/TID
 * @pid: Process/Thread ID
 * 
 * Return: Task pointer or NULL if not found
 */
struct task_struct *get_task_by_pid(pid_t pid);

/**
 * sched_kill_task - Send termination signal to a task (scheduler API)
 * @pid: Task ID to kill
 * 
 * Return: 0 on success, negative on error
 */
int sched_kill_task(pid_t pid);

/**
 * exit_task - Terminate current task
 * @code: Exit code
 */
void exit_task(int code) __noreturn;

/**
 * get_current - Get current running task
 * 
 * Return: Pointer to current task
 */
struct task_struct *get_current(void);

/**
 * context_switch - Switch to a new task
 * @prev: Previous task
 * @next: Next task to run
 */
void context_switch(struct task_struct *prev, struct task_struct *next);

/* Assembly helper for context switch */
void cpu_switch_to(struct task_struct *prev, struct task_struct *next);

#endif /* _SCHED_SCHED_H */
