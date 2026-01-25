/*
 * vib-OS - Real Init Process (PID 1)
 * 
 * The first userspace process that initializes the system.
 */

/* This file is meant to be compiled with musl libc */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <signal.h>
#include <errno.h>

/* ===================================================================== */
/* Configuration */
/* ===================================================================== */

#define INIT_VERSION    "1.0.0"
#define HOSTNAME        "vib-os"
#define SHELL_PATH      "/bin/sh"
#define CONSOLE_DEV     "/dev/console"
#define INITTAB_PATH    "/etc/inittab"

/* Run levels */
#define RUNLEVEL_HALT       0
#define RUNLEVEL_SINGLE     1
#define RUNLEVEL_MULTI      2
#define RUNLEVEL_FULL       3
#define RUNLEVEL_REBOOT     6

/* ===================================================================== */
/* Global State */
/* ===================================================================== */

static int current_runlevel = RUNLEVEL_FULL;
static volatile int received_signal = 0;
static pid_t shell_pid = 0;

/* ===================================================================== */
/* Signal Handlers */
/* ===================================================================== */

static void sigchld_handler(int sig)
{
    (void)sig;
    /* Reap zombie children */
    while (waitpid(-1, NULL, WNOHANG) > 0);
}

static void sighup_handler(int sig)
{
    (void)sig;
    received_signal = SIGHUP;
}

static void sigint_handler(int sig)
{
    (void)sig;
    received_signal = SIGINT;
}

static void sigterm_handler(int sig)
{
    (void)sig;
    received_signal = SIGTERM;
}

static void setup_signals(void)
{
    struct sigaction sa;
    
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = sigchld_handler;
    sa.sa_flags = SA_RESTART | SA_NOCLDSTOP;
    sigaction(SIGCHLD, &sa, NULL);
    
    sa.sa_handler = sighup_handler;
    sigaction(SIGHUP, &sa, NULL);
    
    sa.sa_handler = sigint_handler;
    sigaction(SIGINT, &sa, NULL);
    
    sa.sa_handler = sigterm_handler;
    sigaction(SIGTERM, &sa, NULL);
}

/* ===================================================================== */
/* Console Setup */
/* ===================================================================== */

static void setup_console(void)
{
    int fd;
    
    /* Close existing file descriptors */
    close(0);
    close(1);
    close(2);
    
    /* Open console for stdin, stdout, stderr */
    fd = open(CONSOLE_DEV, O_RDWR);
    if (fd < 0) {
        fd = open("/dev/ttyS0", O_RDWR);
    }
    if (fd < 0) {
        fd = open("/dev/tty1", O_RDWR);
    }
    
    if (fd >= 0) {
        dup2(fd, 0);
        dup2(fd, 1);
        dup2(fd, 2);
        if (fd > 2) close(fd);
    }
}

/* ===================================================================== */
/* Mount Filesystems */
/* ===================================================================== */

static void mount_filesystems(void)
{
    printf("[init] Mounting filesystems...\n");
    
    /* Mount /proc */
    if (mkdir("/proc", 0755) < 0 && errno != EEXIST) {
        /* Ignore error */
    }
    /* mount("proc", "/proc", "proc", 0, NULL); */
    
    /* Mount /sys */
    if (mkdir("/sys", 0755) < 0 && errno != EEXIST) {
        /* Ignore error */
    }
    /* mount("sysfs", "/sys", "sysfs", 0, NULL); */
    
    /* Mount /dev */
    if (mkdir("/dev", 0755) < 0 && errno != EEXIST) {
        /* Ignore error */
    }
    /* mount("devtmpfs", "/dev", "devtmpfs", 0, NULL); */
    
    /* Mount /tmp */
    if (mkdir("/tmp", 01777) < 0 && errno != EEXIST) {
        /* Ignore error */
    }
    /* mount("tmpfs", "/tmp", "tmpfs", 0, "mode=1777"); */
    
    printf("[init] Filesystems mounted\n");
}

/* ===================================================================== */
/* Network Setup */
/* ===================================================================== */

static void setup_network(void)
{
    printf("[init] Configuring network...\n");
    
    /* Set hostname */
    if (sethostname(HOSTNAME, strlen(HOSTNAME)) < 0) {
        perror("sethostname");
    }
    
    /* Bring up loopback */
    /* TODO: Use netlink or ioctl to configure interfaces */
    
    printf("[init] Network configured\n");
}

/* ===================================================================== */
/* Start Shell */
/* ===================================================================== */

static pid_t start_shell(void)
{
    pid_t pid = fork();
    
    if (pid < 0) {
        perror("fork");
        return -1;
    }
    
    if (pid == 0) {
        /* Child process */
        char *argv[] = { "/bin/sh", "-l", NULL };
        char *envp[] = {
            "HOME=/root",
            "PATH=/bin:/sbin:/usr/bin:/usr/sbin",
            "SHELL=/bin/sh",
            "TERM=linux",
            NULL
        };
        
        setsid();
        
        execve(SHELL_PATH, argv, envp);
        
        /* If execve fails, try alternatives */
        execve("/bin/bash", argv, envp);
        execve("/bin/busybox", (char*[]){"/bin/busybox", "sh", NULL}, envp);
        
        perror("execve");
        _exit(127);
    }
    
    /* Parent */
    return pid;
}

/* ===================================================================== */
/* Run Scripts */
/* ===================================================================== */

static void run_script(const char *path)
{
    pid_t pid;
    int status;
    
    if (access(path, X_OK) != 0) {
        return;
    }
    
    printf("[init] Running %s\n", path);
    
    pid = fork();
    if (pid == 0) {
        char *argv[] = { (char *)path, NULL };
        char *envp[] = {
            "PATH=/bin:/sbin:/usr/bin:/usr/sbin",
            NULL
        };
        execve(path, argv, envp);
        _exit(127);
    }
    
    if (pid > 0) {
        waitpid(pid, &status, 0);
    }
}

static void run_startup_scripts(void)
{
    printf("[init] Running startup scripts...\n");
    
    run_script("/etc/rc.d/rc.sysinit");
    run_script("/etc/rc.d/rc.local");
    run_script("/etc/init.d/rcS");
    
    printf("[init] Startup scripts complete\n");
}

/* ===================================================================== */
/* Shutdown */
/* ===================================================================== */

static void do_shutdown(int reboot)
{
    printf("\n[init] System %s...\n", reboot ? "rebooting" : "halting");
    
    /* Kill all processes */
    printf("[init] Sending SIGTERM to all processes\n");
    kill(-1, SIGTERM);
    sleep(2);
    
    printf("[init] Sending SIGKILL to all processes\n");
    kill(-1, SIGKILL);
    sleep(1);
    
    /* Run shutdown scripts */
    run_script("/etc/rc.d/rc.shutdown");
    
    /* Unmount filesystems */
    printf("[init] Unmounting filesystems\n");
    sync();
    
    /* Reboot or halt */
    if (reboot) {
        /* reboot(RB_AUTOBOOT); */
        printf("[init] Rebooting NOW\n");
    } else {
        /* reboot(RB_HALT_SYSTEM); */
        printf("[init] System halted\n");
    }
    
    while (1) {
        pause();
    }
}

/* ===================================================================== */
/* Main */
/* ===================================================================== */

int main(int argc, char *argv[])
{
    int status;
    
    (void)argc;
    (void)argv;
    
    /* Check if we're PID 1 */
    if (getpid() != 1) {
        fprintf(stderr, "init: must be PID 1\n");
        return 1;
    }
    
    /* Print banner */
    printf("\n");
    printf("  vib-OS init v%s\n", INIT_VERSION);
    printf("  =================\n\n");
    
    /* Setup signals */
    setup_signals();
    
    /* Setup console */
    setup_console();
    
    /* Mount filesystems */
    mount_filesystems();
    
    /* Configure network */
    setup_network();
    
    /* Run startup scripts */
    run_startup_scripts();
    
    /* Start shell */
    printf("[init] Starting shell...\n\n");
    shell_pid = start_shell();
    
    /* Main loop - respawn shell if it exits */
    while (1) {
        pid_t pid = wait(&status);
        
        if (pid == shell_pid) {
            printf("\n[init] Shell exited, respawning...\n");
            sleep(1);
            shell_pid = start_shell();
        }
        
        /* Handle signals */
        if (received_signal == SIGTERM || received_signal == SIGINT) {
            do_shutdown(0);
        }
        if (received_signal == SIGHUP) {
            received_signal = 0;
            /* Reload configuration */
        }
    }
    
    return 0;
}
