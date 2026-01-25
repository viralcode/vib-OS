/*
 * UnixOS Kernel - Signal Handling
 */

#ifndef _KERNEL_SIGNAL_H
#define _KERNEL_SIGNAL_H

#include "types.h"

/* ===================================================================== */
/* Signal numbers (POSIX) */
/* ===================================================================== */

#define SIGHUP      1   /* Hangup */
#define SIGINT      2   /* Interrupt (Ctrl+C) */
#define SIGQUIT     3   /* Quit */
#define SIGILL      4   /* Illegal instruction */
#define SIGTRAP     5   /* Trace/breakpoint trap */
#define SIGABRT     6   /* Abort */
#define SIGBUS      7   /* Bus error */
#define SIGFPE      8   /* Floating-point exception */
#define SIGKILL     9   /* Kill (cannot be caught) */
#define SIGUSR1     10  /* User-defined signal 1 */
#define SIGSEGV     11  /* Segmentation fault */
#define SIGUSR2     12  /* User-defined signal 2 */
#define SIGPIPE     13  /* Broken pipe */
#define SIGALRM     14  /* Alarm clock */
#define SIGTERM     15  /* Termination */
#define SIGSTKFLT   16  /* Stack fault */
#define SIGCHLD     17  /* Child status changed */
#define SIGCONT     18  /* Continue execution */
#define SIGSTOP     19  /* Stop (cannot be caught) */
#define SIGTSTP     20  /* Terminal stop */
#define SIGTTIN     21  /* Background read from tty */
#define SIGTTOU     22  /* Background write to tty */
#define SIGURG      23  /* Urgent data on socket */
#define SIGXCPU     24  /* CPU time limit exceeded */
#define SIGXFSZ     25  /* File size limit exceeded */
#define SIGVTALRM   26  /* Virtual alarm clock */
#define SIGPROF     27  /* Profiling timer expired */
#define SIGWINCH    28  /* Window size change */
#define SIGIO       29  /* I/O possible */
#define SIGPWR      30  /* Power failure */
#define SIGSYS      31  /* Bad system call */

#define NSIG        32  /* Number of signals */

/* ===================================================================== */
/* Signal action flags */
/* ===================================================================== */

#define SA_NOCLDSTOP    0x00000001
#define SA_NOCLDWAIT    0x00000002
#define SA_SIGINFO      0x00000004
#define SA_ONSTACK      0x08000000
#define SA_RESTART      0x10000000
#define SA_NODEFER      0x40000000
#define SA_RESETHAND    0x80000000

/* Special signal handlers */
#define SIG_DFL         ((void (*)(int))0)      /* Default action */
#define SIG_IGN         ((void (*)(int))1)      /* Ignore signal */
#define SIG_ERR         ((void (*)(int))-1)     /* Error return */

/* ===================================================================== */
/* Signal set type */
/* ===================================================================== */

typedef struct {
    unsigned long sig[1];  /* Bitmask for up to 64 signals */
} sigset_t;

/* Signal set operations */
#define sigemptyset(set)    ((set)->sig[0] = 0)
#define sigfillset(set)     ((set)->sig[0] = ~0UL)
#define sigaddset(set, n)   ((set)->sig[0] |= (1UL << ((n) - 1)))
#define sigdelset(set, n)   ((set)->sig[0] &= ~(1UL << ((n) - 1)))
#define sigismember(set, n) (((set)->sig[0] >> ((n) - 1)) & 1)

/* ===================================================================== */
/* Signal action structure */
/* ===================================================================== */

typedef void (*sighandler_t)(int);

struct sigaction {
    sighandler_t sa_handler;
    unsigned long sa_flags;
    void (*sa_restorer)(void);
    sigset_t sa_mask;
};

/* ===================================================================== */
/* Function declarations */
/* ===================================================================== */

/**
 * signal_init - Initialize signal handling
 */
void signal_init(void);

/**
 * send_signal - Send a signal to a process
 * @pid: Target process ID
 * @sig: Signal number
 * 
 * Return: 0 on success, negative on error
 */
int send_signal(pid_t pid, int sig);

/**
 * do_signal - Process pending signals for current task
 */
void do_signal(void);

#endif /* _KERNEL_SIGNAL_H */
