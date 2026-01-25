/*
 * UnixOS - Minimal C Library Implementation
 * Signal Functions
 */

#include "../include/signal.h"
#include "../include/string.h"

/* ===================================================================== */
/* Signal set manipulation functions */
/* ===================================================================== */

int sigemptyset(sigset_t *set)
{
    if (!set) return -1;
    *set = 0;
    return 0;
}

int sigfillset(sigset_t *set)
{
    if (!set) return -1;
    *set = ~(sigset_t)0;
    return 0;
}

int sigaddset(sigset_t *set, int sig)
{
    if (!set || sig < 1 || sig > 64) return -1;
    *set |= (1UL << (sig - 1));
    return 0;
}

int sigdelset(sigset_t *set, int sig)
{
    if (!set || sig < 1 || sig > 64) return -1;
    *set &= ~(1UL << (sig - 1));
    return 0;
}

int sigismember(const sigset_t *set, int sig)
{
    if (!set || sig < 1 || sig > 64) return -1;
    return (*set & (1UL << (sig - 1))) ? 1 : 0;
}

/* ===================================================================== */
/* signal() - simplified wrapper around sigaction */
/* ===================================================================== */

void (*signal(int sig, void (*handler)(int)))(int)
{
    struct sigaction act, oldact;
    
    memset(&act, 0, sizeof(act));
    act.sa_handler = handler;
    sigemptyset(&act.sa_mask);
    act.sa_flags = SA_RESTART;
    
    if (sigaction(sig, &act, &oldact) < 0) {
        return SIG_ERR;
    }
    
    return oldact.sa_handler;
}
