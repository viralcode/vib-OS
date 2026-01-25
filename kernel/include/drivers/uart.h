/*
 * UnixOS Kernel - UART Driver Header
 * Serial console for debugging and early boot output
 */

#ifndef _DRIVERS_UART_H
#define _DRIVERS_UART_H

#include "types.h"

/**
 * uart_early_init - Initialize UART with minimal setup
 * 
 * Called very early in boot before memory management.
 * Uses hardcoded addresses for Apple Silicon UART.
 */
void uart_early_init(void);

/**
 * uart_init - Full UART initialization
 * 
 * Called after memory management is available.
 * Sets up interrupts and buffering.
 */
void uart_init(void);

/**
 * uart_putc - Output a single character
 * @c: Character to output
 */
void uart_putc(char c);

/**
 * uart_puts - Output a string
 * @s: Null-terminated string
 */
void uart_puts(const char *s);

/**
 * uart_getc - Read a character (blocking)
 * 
 * Return: Character read from UART
 */
char uart_getc(void);

/**
 * uart_getc_nonblock - Read a character (non-blocking)
 * 
 * Return: Character read, or -1 if no data available
 */
int uart_getc_nonblock(void);

/**
 * uart_write - Write multiple bytes
 * @buf: Buffer to write
 * @len: Number of bytes
 * 
 * Return: Number of bytes written
 */
size_t uart_write(const char *buf, size_t len);

/**
 * uart_read - Read multiple bytes
 * @buf: Buffer to read into
 * @len: Maximum bytes to read
 * 
 * Return: Number of bytes read
 */
size_t uart_read(char *buf, size_t len);

#endif /* _DRIVERS_UART_H */
