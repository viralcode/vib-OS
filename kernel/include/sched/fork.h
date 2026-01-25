/*
 * UnixOS Kernel - Fork/Exec Header
 */

#ifndef _SCHED_FORK_H
#define _SCHED_FORK_H

#include "types.h"

/* Clone flags (subset of Linux clone flags) */
#define CLONE_VM        0x00000100  /* Share virtual memory */
#define CLONE_FS        0x00000200  /* Share filesystem info */
#define CLONE_FILES     0x00000400  /* Share file descriptors */
#define CLONE_SIGHAND   0x00000800  /* Share signal handlers */
#define CLONE_THREAD    0x00010000  /* Share thread group */
#define CLONE_NEWNS     0x00020000  /* New mount namespace */

/**
 * do_fork - Create a child process
 * @flags: Clone flags
 * 
 * Returns child PID to parent, 0 to child, negative on error.
 */
long do_fork(unsigned long flags);

/**
 * do_execve - Execute a new program
 * @filename: Path to executable
 * @argv: Argument vector (NULL terminated)
 * @envp: Environment vector (NULL terminated)
 * 
 * Returns 0 on success, negative on error.
 * On success, does not return to caller (new program runs).
 */
long do_execve(const char *filename, char *const argv[], char *const envp[]);

/**
 * task_entry_stub - Entry point for forked tasks
 * @arg: Argument passed to task
 */
void task_entry_stub(void *arg);

#endif /* _SCHED_FORK_H */
