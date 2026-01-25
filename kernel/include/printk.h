/*
 * UnixOS Kernel - Kernel Print Functions
 */

#ifndef _KERNEL_PRINTK_H
#define _KERNEL_PRINTK_H

#include "types.h"

/* ===================================================================== */
/* Log levels (compatible with Linux) */
/* ===================================================================== */

#define KERN_EMERG      "<0>"   /* System is unusable */
#define KERN_ALERT      "<1>"   /* Action must be taken immediately */
#define KERN_CRIT       "<2>"   /* Critical conditions */
#define KERN_ERR        "<3>"   /* Error conditions */
#define KERN_WARNING    "<4>"   /* Warning conditions */
#define KERN_NOTICE     "<5>"   /* Normal but significant condition */
#define KERN_INFO       "<6>"   /* Informational */
#define KERN_DEBUG      "<7>"   /* Debug-level messages */

/* Default log level */
#define KERN_DEFAULT    KERN_WARNING

/* ===================================================================== */
/* Function declarations */
/* ===================================================================== */

/**
 * printk - Kernel print function
 * @fmt: Format string (printf-style)
 * 
 * Prints a formatted message to the kernel console.
 * Supports log level prefixes (e.g., KERN_INFO "message").
 * 
 * Return: Number of characters printed
 */
int printk(const char *fmt, ...) __attribute__((format(printf, 1, 2)));

/**
 * vprintk - Kernel print with va_list
 * @fmt: Format string
 * @args: Variable argument list
 * 
 * Return: Number of characters printed
 */
int vprintk(const char *fmt, __builtin_va_list args);

/**
 * panic - Halt the system with error message
 * @msg: Panic message
 * 
 * This function never returns.
 */
void panic(const char *msg) __noreturn;

/* ===================================================================== */
/* Early boot console (before full driver init) */
/* ===================================================================== */

/**
 * early_printk - Print during very early boot
 * @fmt: Format string
 * 
 * Uses minimal hardware access, for debugging before console is ready.
 */
int early_printk(const char *fmt, ...) __attribute__((format(printf, 1, 2)));

/* ===================================================================== */
/* Kernel assertion */
/* ===================================================================== */

#define BUG() do { \
    printk(KERN_CRIT "BUG at %s:%d in %s()\n", __FILE__, __LINE__, __func__); \
    panic("BUG!"); \
} while (0)

#define BUG_ON(condition) do { \
    if (unlikely(condition)) { \
        BUG(); \
    } \
} while (0)

#define WARN_ON(condition) ({ \
    int __warn_cond = !!(condition); \
    if (unlikely(__warn_cond)) { \
        printk(KERN_WARNING "WARNING at %s:%d in %s()\n", \
               __FILE__, __LINE__, __func__); \
    } \
    __warn_cond; \
})

/* Debug print macro - only active in debug builds */
#ifdef DEBUG
#define pr_debug(fmt, ...) \
    printk(KERN_DEBUG "%s: " fmt, __func__, ##__VA_ARGS__)
#else
#define pr_debug(fmt, ...) ((void)0)
#endif

/* Convenience macros */
#define pr_emerg(fmt, ...)   printk(KERN_EMERG fmt, ##__VA_ARGS__)
#define pr_alert(fmt, ...)   printk(KERN_ALERT fmt, ##__VA_ARGS__)
#define pr_crit(fmt, ...)    printk(KERN_CRIT fmt, ##__VA_ARGS__)
#define pr_err(fmt, ...)     printk(KERN_ERR fmt, ##__VA_ARGS__)
#define pr_warn(fmt, ...)    printk(KERN_WARNING fmt, ##__VA_ARGS__)
#define pr_notice(fmt, ...)  printk(KERN_NOTICE fmt, ##__VA_ARGS__)
#define pr_info(fmt, ...)    printk(KERN_INFO fmt, ##__VA_ARGS__)

#endif /* _KERNEL_PRINTK_H */
