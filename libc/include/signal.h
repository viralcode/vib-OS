/*
 * Vib-OS libc - signal.h
 */

#ifndef _SIGNAL_H
#define _SIGNAL_H

#include <sys/types.h>

/* Signal numbers */
#define SIGHUP      1
#define SIGINT      2
#define SIGQUIT     3
#define SIGILL      4
#define SIGTRAP     5
#define SIGABRT     6
#define SIGBUS      7
#define SIGFPE      8
#define SIGKILL     9
#define SIGUSR1     10
#define SIGSEGV     11
#define SIGUSR2     12
#define SIGPIPE     13
#define SIGALRM     14
#define SIGTERM     15
#define SIGSTKFLT   16
#define SIGCHLD     17
#define SIGCONT     18
#define SIGSTOP     19
#define SIGTSTP     20
#define SIGTTIN     21
#define SIGTTOU     22
#define SIGURG      23
#define SIGXCPU     24
#define SIGXFSZ     25
#define SIGVTALRM   26
#define SIGPROF     27
#define SIGWINCH    28
#define SIGIO       29
#define SIGPWR      30
#define SIGSYS      31

#define _NSIG       64

/* Signal action flags */
#define SA_NOCLDSTOP  0x00000001
#define SA_NOCLDWAIT  0x00000002
#define SA_SIGINFO    0x00000004
#define SA_RESTART    0x10000000
#define SA_NODEFER    0x40000000
#define SA_RESETHAND  0x80000000

/* Special signal handlers */
#define SIG_DFL ((void (*)(int))0)
#define SIG_IGN ((void (*)(int))1)
#define SIG_ERR ((void (*)(int))-1)

typedef unsigned long sigset_t;

union sigval {
    int sival_int;
    void *sival_ptr;
};

typedef struct {
    int si_signo;
    int si_errno;
    int si_code;
    pid_t si_pid;
    uid_t si_uid;
    void *si_addr;
    int si_status;
    union sigval si_value;
} siginfo_t;

struct sigaction {
    void (*sa_handler)(int);
    void (*sa_sigaction)(int, siginfo_t *, void *);
    sigset_t sa_mask;
    int sa_flags;
    void (*sa_restorer)(void);
};

int kill(pid_t pid, int sig);
int raise(int sig);
int sigaction(int sig, const struct sigaction *act, struct sigaction *oldact);
void (*signal(int sig, void (*handler)(int)))(int);
int sigemptyset(sigset_t *set);
int sigfillset(sigset_t *set);
int sigaddset(sigset_t *set, int sig);
int sigdelset(sigset_t *set, int sig);
int sigismember(const sigset_t *set, int sig);
int sigprocmask(int how, const sigset_t *set, sigset_t *oldset);
int pause(void);

#endif /* _SIGNAL_H */
