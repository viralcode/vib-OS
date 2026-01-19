/*
 * vib-OS Kernel - Signal Handling
 *
 * POSIX signal implementation for process control.
 */

#include "printk.h"
#include "sched/sched.h"
#include "types.h"

/* ===================================================================== */
/* Signal Definitions (POSIX compatible) */
/* ===================================================================== */

#define SIGHUP 1
#define SIGINT 2
#define SIGQUIT 3
#define SIGILL 4
#define SIGTRAP 5
#define SIGABRT 6
#define SIGBUS 7
#define SIGFPE 8
#define SIGKILL 9
#define SIGUSR1 10
#define SIGSEGV 11
#define SIGUSR2 12
#define SIGPIPE 13
#define SIGALRM 14
#define SIGTERM 15
#define SIGSTKFLT 16
#define SIGCHLD 17
#define SIGCONT 18
#define SIGSTOP 19
#define SIGTSTP 20
#define SIGTTIN 21
#define SIGTTOU 22
#define SIGURG 23
#define SIGXCPU 24
#define SIGXFSZ 25
#define SIGVTALRM 26
#define SIGPROF 27
#define SIGWINCH 28
#define SIGIO 29
#define SIGPWR 30
#define SIGSYS 31

#define NSIG 32

#define SIG_DFL ((sighandler_t)0)
#define SIG_IGN ((sighandler_t)1)
#define SIG_ERR ((sighandler_t) - 1)

/* ===================================================================== */
/* Types */
/* ===================================================================== */

typedef void (*sighandler_t)(int);

typedef uint64_t sigset_t;

struct sigaction {
  sighandler_t sa_handler;
  sigset_t sa_mask;
  int sa_flags;
  void (*sa_restorer)(void);
};

/* Signal info for pending signals */
struct siginfo {
  int si_signo;
  int si_code;
  pid_t si_pid;
  uid_t si_uid;
  void *si_addr;
};

/* Per-task signal state */
struct signal_struct {
  sigset_t pending;
  sigset_t blocked;
  struct sigaction actions[NSIG];
};

/* ===================================================================== */
/* Signal Functions */
/* ===================================================================== */

/* Default signal handlers */
static void sig_default_term(int sig) {
  struct task_struct *task = get_current();
  printk(KERN_INFO "Signal %d: terminating PID %d\n", sig, task->pid);
  exit_task(sig);
}

static void sig_default_ignore(int sig) {
  (void)sig;
  /* Do nothing */
}

static void sig_default_stop(int sig) {
  struct task_struct *task = get_current();
  printk(KERN_INFO "Signal %d: stopping PID %d\n", sig, task->pid);
  task->state = TASK_STOPPED;
  schedule();
}

static void sig_default_cont(int sig) {
  (void)sig;
  struct task_struct *task = get_current();
  task->state = TASK_RUNNING;
}

/* Initialize signal handling for a task */
void signal_init(struct task_struct *task) {
  if (!task->signals) {
    /* Allocate signal struct - for now use static pool */
    static struct signal_struct sig_pool[64];
    static int sig_pool_idx = 0;

    if (sig_pool_idx < 64) {
      task->signals = &sig_pool[sig_pool_idx++];
    } else {
      return;
    }
  }

  task->signals->pending = 0;
  task->signals->blocked = 0;

  /* Set default handlers */
  for (int i = 0; i < NSIG; i++) {
    task->signals->actions[i].sa_handler = SIG_DFL;
    task->signals->actions[i].sa_mask = 0;
    task->signals->actions[i].sa_flags = 0;
  }
}

/* Send a signal to a task */
int kill_task(struct task_struct *task, int sig) {
  if (sig < 0 || sig >= NSIG)
    return -1;
  if (!task || !task->signals)
    return -1;

  /* Set signal as pending */
  task->signals->pending |= (1UL << sig);

  /* Wake up task if it's sleeping */
  if (task->state == TASK_INTERRUPTIBLE) {
    wake_up_process(task);
  }

  return 0;
}

/* Check and handle pending signals */
void do_signal(struct task_struct *task) {
  if (!task || !task->signals)
    return;

  sigset_t pending = task->signals->pending & ~task->signals->blocked;

  if (pending == 0)
    return;

  /* Handle each pending signal */
  for (int sig = 1; sig < NSIG; sig++) {
    if (!(pending & (1UL << sig)))
      continue;

    /* Clear pending bit */
    task->signals->pending &= ~(1UL << sig);

    struct sigaction *action = &task->signals->actions[sig];

    if (action->sa_handler == SIG_IGN) {
      sig_default_ignore(sig);
    } else if (action->sa_handler == SIG_DFL) {
      /* Default action based on signal */
      switch (sig) {
      case SIGCHLD:
      case SIGURG:
      case SIGWINCH:
        sig_default_ignore(sig);
        break;

      case SIGCONT:
        sig_default_cont(sig);
        break;

      case SIGSTOP:
      case SIGTSTP:
      case SIGTTIN:
      case SIGTTOU:
        sig_default_stop(sig);
        break;

      default:
        sig_default_term(sig);
        break;
      }
    } else {
      /* Call user-defined handler */
      /* TODO: Set up signal trampoline for userspace */
      action->sa_handler(sig);
    }
  }
}

/* Block/unblock signals */
int sigprocmask(int how, const sigset_t *set, sigset_t *oldset) {
  struct task_struct *task = get_current();
  if (!task->signals)
    return -1;

  if (oldset) {
    *oldset = task->signals->blocked;
  }

  if (set) {
    switch (how) {
    case 0: /* SIG_BLOCK */
      task->signals->blocked |= *set;
      break;
    case 1: /* SIG_UNBLOCK */
      task->signals->blocked &= ~(*set);
      break;
    case 2: /* SIG_SETMASK */
      task->signals->blocked = *set;
      break;
    default:
      return -1;
    }
  }

  /* SIGKILL and SIGSTOP cannot be blocked */
  task->signals->blocked &= ~((1UL << SIGKILL) | (1UL << SIGSTOP));

  return 0;
}

/* Install signal handler */
int sigaction_syscall(int sig, const struct sigaction *act,
                      struct sigaction *oldact) {
  struct task_struct *task = get_current();

  if (sig < 1 || sig >= NSIG)
    return -1;
  if (sig == SIGKILL || sig == SIGSTOP)
    return -1; /* Cannot catch */
  if (!task->signals)
    return -1;

  if (oldact) {
    *oldact = task->signals->actions[sig];
  }

  if (act) {
    task->signals->actions[sig] = *act;
  }

  return 0;
}

/* ===================================================================== */
/* waitpid implementation */
/* ===================================================================== */

pid_t do_waitpid(pid_t pid, int *wstatus, int options) {
  struct task_struct *current = get_current();

  (void)options;

  /* Find child process */
  /* TODO: Implement proper child tracking */

  /* For demonstration */
  if (wstatus) {
    *wstatus = 0; /* Normal exit */
  }

  /* Block until child exits */
  /* TODO: Implement proper wait queue */
  current->state = TASK_INTERRUPTIBLE;
  schedule();

  return pid;
}
