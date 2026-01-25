/*
 * UnixOS Kernel - Kernel Print Implementation
 */

#include "printk.h"
#include "drivers/uart.h"
#include "stdarg.h"

/* ===================================================================== */
/* Internal buffer and state */
/* ===================================================================== */

#define PRINTK_BUFFER_SIZE  1024

static char printk_buffer[PRINTK_BUFFER_SIZE];

/* ===================================================================== */
/* Helper functions for number formatting */
/* ===================================================================== */

static void reverse(char *str, int len)
{
    int i = 0, j = len - 1;
    while (i < j) {
        char tmp = str[i];
        str[i] = str[j];
        str[j] = tmp;
        i++;
        j--;
    }
}

static int itoa(long long val, char *buf, int base, int is_signed, int uppercase)
{
    static const char *digits_lower = "0123456789abcdef";
    static const char *digits_upper = "0123456789ABCDEF";
    const char *digits = uppercase ? digits_upper : digits_lower;
    
    int i = 0;
    int negative = 0;
    unsigned long long uval;
    
    if (is_signed && val < 0) {
        negative = 1;
        uval = -val;
    } else {
        uval = val;
    }
    
    if (uval == 0) {
        buf[i++] = '0';
    }
    
    while (uval != 0) {
        buf[i++] = digits[uval % base];
        uval /= base;
    }
    
    if (negative) {
        buf[i++] = '-';
    }
    
    buf[i] = '\0';
    reverse(buf, i);
    
    return i;
}

static int utoa(unsigned long long val, char *buf, int base, int uppercase)
{
    return itoa(val, buf, base, 0, uppercase);
}

/* ===================================================================== */
/* Simple vsnprintf implementation */
/* ===================================================================== */

static int kvsnprintf(char *buf, size_t size, const char *fmt, va_list args)
{
    char *p = buf;
    char *end = buf + size - 1;
    char numbuf[32];
    
    while (*fmt && p < end) {
        if (*fmt != '%') {
            *p++ = *fmt++;
            continue;
        }
        
        fmt++;  /* Skip '%' */
        
        /* Handle flags and width (simplified) */
        int zero_pad = 0;
        int width = 0;
        int is_long = 0;
        int is_long_long = 0;
        
        while (*fmt == '0') {
            zero_pad = 1;
            fmt++;
        }
        
        while (*fmt >= '0' && *fmt <= '9') {
            width = width * 10 + (*fmt - '0');
            fmt++;
        }
        
        /* Handle length modifiers */
        if (*fmt == 'l') {
            is_long = 1;
            fmt++;
            if (*fmt == 'l') {
                is_long_long = 1;
                fmt++;
            }
        } else if (*fmt == 'z') {
            is_long = 1;  /* size_t */
            fmt++;
        }
        
        /* Handle format specifier */
        int len;
        switch (*fmt) {
            case 'd':
            case 'i': {
                long long val;
                if (is_long_long)
                    val = va_arg(args, long long);
                else if (is_long)
                    val = va_arg(args, long);
                else
                    val = va_arg(args, int);
                len = itoa(val, numbuf, 10, 1, 0);
                for (int i = len; i < width; i++)
                    if (p < end) *p++ = zero_pad ? '0' : ' ';
                for (int i = 0; i < len && p < end; i++)
                    *p++ = numbuf[i];
                break;
            }
            
            case 'u': {
                unsigned long long val;
                if (is_long_long)
                    val = va_arg(args, unsigned long long);
                else if (is_long)
                    val = va_arg(args, unsigned long);
                else
                    val = va_arg(args, unsigned int);
                len = utoa(val, numbuf, 10, 0);
                for (int i = len; i < width; i++)
                    if (p < end) *p++ = zero_pad ? '0' : ' ';
                for (int i = 0; i < len && p < end; i++)
                    *p++ = numbuf[i];
                break;
            }
            
            case 'x':
            case 'X': {
                unsigned long long val;
                if (is_long_long)
                    val = va_arg(args, unsigned long long);
                else if (is_long)
                    val = va_arg(args, unsigned long);
                else
                    val = va_arg(args, unsigned int);
                len = utoa(val, numbuf, 16, *fmt == 'X');
                for (int i = len; i < width; i++)
                    if (p < end) *p++ = zero_pad ? '0' : ' ';
                for (int i = 0; i < len && p < end; i++)
                    *p++ = numbuf[i];
                break;
            }
            
            case 'p': {
                unsigned long val = (unsigned long)va_arg(args, void *);
                if (p < end) *p++ = '0';
                if (p < end) *p++ = 'x';
                len = utoa(val, numbuf, 16, 0);
                /* Pad to 16 chars for 64-bit pointers */
                for (int i = len; i < 16; i++)
                    if (p < end) *p++ = '0';
                for (int i = 0; i < len && p < end; i++)
                    *p++ = numbuf[i];
                break;
            }
            
            case 's': {
                const char *str = va_arg(args, const char *);
                if (!str) str = "(null)";
                while (*str && p < end)
                    *p++ = *str++;
                break;
            }
            
            case 'c': {
                char c = (char)va_arg(args, int);
                if (p < end) *p++ = c;
                break;
            }
            
            case '%':
                if (p < end) *p++ = '%';
                break;
            
            default:
                /* Unknown format, print as-is */
                if (p < end) *p++ = '%';
                if (p < end) *p++ = *fmt;
                break;
        }
        
        fmt++;
    }
    
    *p = '\0';
    return p - buf;
}

/* ===================================================================== */
/* Public functions */
/* ===================================================================== */

int vprintk(const char *fmt, va_list args)
{
    int len;
    const char *p = fmt;
    int level = 4;  /* Default: KERN_WARNING */
    
    /* Parse log level if present */
    if (p[0] == '<' && p[1] >= '0' && p[1] <= '7' && p[2] == '>') {
        level = p[1] - '0';
        p += 3;
    }
    (void)level;  /* TODO: Use level for filtering */
    
    /* Format the message */
    len = kvsnprintf(printk_buffer, PRINTK_BUFFER_SIZE, p, args);
    
    /* Output to console (UART for now) */
    uart_puts(printk_buffer);
    
    return len;
}

int printk(const char *fmt, ...)
{
    va_list args;
    int ret;
    
    va_start(args, fmt);
    ret = vprintk(fmt, args);
    va_end(args);
    
    return ret;
}

int early_printk(const char *fmt, ...)
{
    va_list args;
    int ret;
    
    va_start(args, fmt);
    ret = kvsnprintf(printk_buffer, PRINTK_BUFFER_SIZE, fmt, args);
    va_end(args);
    
    /* Direct UART output, no buffering */
    uart_puts(printk_buffer);
    
    return ret;
}
