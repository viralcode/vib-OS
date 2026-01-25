/*
 * UnixOS - Basic Shell
 * 
 * A minimal command interpreter for early testing.
 */

/* Syscall numbers */
#define SYS_read        63
#define SYS_write       64
#define SYS_exit        93
#define SYS_getpid      172
#define SYS_uname       160
#define SYS_sched_yield 124

/* Syscall wrappers */
static inline long syscall0(long nr)
{
    register long x0 __asm__("x0");
    register long x8 __asm__("x8") = nr;
    __asm__ volatile("svc #0" : "=r" (x0) : "r" (x8) : "memory");
    return x0;
}

static inline long syscall1(long nr, long a0)
{
    register long x0 __asm__("x0") = a0;
    register long x8 __asm__("x8") = nr;
    __asm__ volatile("svc #0" : "+r" (x0) : "r" (x8) : "memory");
    return x0;
}

static inline long syscall3(long nr, long a0, long a1, long a2)
{
    register long x0 __asm__("x0") = a0;
    register long x1 __asm__("x1") = a1;
    register long x2 __asm__("x2") = a2;
    register long x8 __asm__("x8") = nr;
    __asm__ volatile("svc #0" : "+r" (x0) : "r" (x1), "r" (x2), "r" (x8) : "memory");
    return x0;
}

/* String functions */
static int strlen(const char *s)
{
    int len = 0;
    while (*s++) len++;
    return len;
}

static int strcmp(const char *a, const char *b)
{
    while (*a && *b && *a == *b) {
        a++;
        b++;
    }
    return *a - *b;
}

static int strncmp(const char *a, const char *b, int n)
{
    while (n > 0 && *a && *b && *a == *b) {
        a++;
        b++;
        n--;
    }
    if (n == 0) return 0;
    return *a - *b;
}

/* I/O functions */
static void print(const char *msg)
{
    syscall3(SYS_write, 1, (long)msg, strlen(msg));
}

static void putchar(char c)
{
    syscall3(SYS_write, 1, (long)&c, 1);
}

static char getchar(void)
{
    char c;
    syscall3(SYS_read, 0, (long)&c, 1);
    return c;
}

static void print_num(long n)
{
    char buf[32];
    int i = 30;
    int neg = 0;
    
    if (n < 0) {
        neg = 1;
        n = -n;
    }
    
    if (n == 0) {
        buf[i--] = '0';
    }
    
    while (n > 0 && i >= 0) {
        buf[i--] = '0' + (n % 10);
        n /= 10;
    }
    
    if (neg && i >= 0) {
        buf[i--] = '-';
    }
    
    syscall3(SYS_write, 1, (long)&buf[i + 1], 30 - i);
}

/* Command buffer */
#define CMD_MAX 256
static char cmd_buf[CMD_MAX];
static int cmd_len;

/* Read a line of input */
static void readline(void)
{
    cmd_len = 0;
    
    while (cmd_len < CMD_MAX - 1) {
        char c = getchar();
        
        if (c == '\n' || c == '\r') {
            putchar('\n');
            break;
        }
        
        if (c == '\b' || c == 127) {  /* Backspace */
            if (cmd_len > 0) {
                cmd_len--;
                print("\b \b");
            }
            continue;
        }
        
        if (c >= 32 && c < 127) {  /* Printable */
            cmd_buf[cmd_len++] = c;
            putchar(c);
        }
    }
    
    cmd_buf[cmd_len] = '\0';
}

/* Built-in commands */
static void cmd_help(void)
{
    print("\nUnixOS Shell Commands:\n");
    print("  help     - Show this help\n");
    print("  uname    - Show system information\n");
    print("  whoami   - Show current user\n");
    print("  pid      - Show shell PID\n");
    print("  echo     - Print text\n");
    print("  clear    - Clear screen\n");
    print("  exit     - Exit shell\n");
    print("\n");
}

static void cmd_uname(void)
{
    struct {
        char sysname[65];
        char nodename[65];
        char release[65];
        char version[65];
        char machine[65];
        char domainname[65];
    } uts;
    
    if (syscall1(SYS_uname, (long)&uts) == 0) {
        print(uts.sysname);
        print(" ");
        print(uts.nodename);
        print(" ");
        print(uts.release);
        print(" ");
        print(uts.version);
        print(" ");
        print(uts.machine);
        print("\n");
    } else {
        print("uname failed\n");
    }
}

static void cmd_pid(void)
{
    long pid = syscall0(SYS_getpid);
    print("PID: ");
    print_num(pid);
    print("\n");
}

static void cmd_whoami(void)
{
    print("root\n");
}

static void cmd_echo(const char *args)
{
    if (args && *args) {
        print(args);
    }
    print("\n");
}

static void cmd_clear(void)
{
    /* ANSI escape sequence to clear screen */
    print("\033[2J\033[H");
}

/* Process command */
static void process_command(void)
{
    if (cmd_len == 0) {
        return;
    }
    
    /* Find first space for arguments */
    char *args = cmd_buf;
    while (*args && *args != ' ') args++;
    if (*args == ' ') {
        *args = '\0';
        args++;
        while (*args == ' ') args++;
    }
    
    if (strcmp(cmd_buf, "help") == 0 || strcmp(cmd_buf, "?") == 0) {
        cmd_help();
    }
    else if (strcmp(cmd_buf, "uname") == 0) {
        cmd_uname();
    }
    else if (strcmp(cmd_buf, "whoami") == 0) {
        cmd_whoami();
    }
    else if (strcmp(cmd_buf, "pid") == 0) {
        cmd_pid();
    }
    else if (strncmp(cmd_buf, "echo", 4) == 0) {
        cmd_echo(args);
    }
    else if (strcmp(cmd_buf, "clear") == 0) {
        cmd_clear();
    }
    else if (strcmp(cmd_buf, "exit") == 0) {
        print("Goodbye!\n");
        syscall1(SYS_exit, 0);
    }
    else {
        print("sh: ");
        print(cmd_buf);
        print(": command not found\n");
    }
}

/* Entry point */
void _start(void)
{
    print("\n");
    print("UnixOS Shell v0.1\n");
    print("Type 'help' for available commands.\n");
    print("\n");
    
    /* Main shell loop */
    while (1) {
        print("unixos# ");
        readline();
        process_command();
    }
}
