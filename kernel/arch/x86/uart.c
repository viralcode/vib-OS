/*
 * x86 UART Driver (16550)
 * COM1 serial port
 */

#include "types.h"
#include "drivers/uart.h"

/* COM1 base port */
#define COM1_BASE   0x3F8

/* UART registers (offsets from base) */
#define UART_DATA   0   /* Data register (R/W) */
#define UART_IER    1   /* Interrupt Enable Register */
#define UART_IIR    2   /* Interrupt Identification Register (R) */
#define UART_FCR    2   /* FIFO Control Register (W) */
#define UART_LCR    3   /* Line Control Register */
#define UART_MCR    4   /* Modem Control Register */
#define UART_LSR    5   /* Line Status Register */
#define UART_MSR    6   /* Modem Status Register */
#define UART_SCR    7   /* Scratch Register */

/* Line Status Register bits */
#define LSR_DATA_READY      0x01
#define LSR_TX_EMPTY        0x20

/* I/O port operations */
static inline void outb(uint16_t port, uint8_t value)
{
    asm volatile("outb %0, %1" :: "a"(value), "Nd"(port));
}

static inline uint8_t inb(uint16_t port)
{
    uint8_t value;
    asm volatile("inb %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

static uint16_t uart_base = COM1_BASE;

void uart_early_init(void)
{
    /* Disable interrupts */
    outb(uart_base + UART_IER, 0x00);
    
    /* Enable DLAB (set baud rate divisor) */
    outb(uart_base + UART_LCR, 0x80);
    
    /* Set divisor to 3 (38400 baud) */
    outb(uart_base + UART_DATA, 0x03);
    outb(uart_base + UART_IER, 0x00);
    
    /* 8 bits, no parity, one stop bit */
    outb(uart_base + UART_LCR, 0x03);
    
    /* Enable FIFO, clear them, with 14-byte threshold */
    outb(uart_base + UART_FCR, 0xC7);
    
    /* IRQs enabled, RTS/DSR set */
    outb(uart_base + UART_MCR, 0x0B);
    
    /* Enable interrupts */
    outb(uart_base + UART_IER, 0x01);
}

void uart_init(void)
{
    uart_early_init();
}

void uart_putc(char c)
{
    /* Wait for transmit buffer to be empty */
    while ((inb(uart_base + UART_LSR) & LSR_TX_EMPTY) == 0)
        ;
    
    /* Send character */
    outb(uart_base + UART_DATA, (uint8_t)c);
}

char uart_getc(void)
{
    /* Wait for data to be available */
    while ((inb(uart_base + UART_LSR) & LSR_DATA_READY) == 0)
        ;
    
    /* Read character */
    return (char)inb(uart_base + UART_DATA);
}

int uart_getc_nonblock(void)
{
    /* Check if data is available */
    if ((inb(uart_base + UART_LSR) & LSR_DATA_READY) == 0) {
        return -1;
    }
    
    /* Read character */
    return (int)inb(uart_base + UART_DATA);
}

void uart_puts(const char *str)
{
    while (*str) {
        if (*str == '\n') {
            uart_putc('\r');
        }
        uart_putc(*str++);
    }
}

size_t uart_write(const char *buf, size_t len)
{
    for (size_t i = 0; i < len; i++) {
        uart_putc(buf[i]);
    }
    return len;
}

size_t uart_read(char *buf, size_t len)
{
    size_t i;
    for (i = 0; i < len; i++) {
        int c = uart_getc_nonblock();
        if (c < 0) {
            break;
        }
        buf[i] = (char)c;
    }
    return i;
}
