/*
 * Vib-OS Kernel - x86_64 Serial Port (16550 UART) Driver
 * 
 * Supports standard PC serial ports (COM1-COM4)
 */

#include "arch/arch.h"
#include "drivers/uart.h"
#include "types.h"

/* ===================================================================== */
/* 16550 UART Registers */
/* ===================================================================== */

#define COM1_PORT   0x3F8
#define COM2_PORT   0x2F8
#define COM3_PORT   0x3E8
#define COM4_PORT   0x2E8

/* Register offsets */
#define UART_DATA       0   /* Data register (R/W) */
#define UART_IER        1   /* Interrupt Enable Register */
#define UART_IIR        2   /* Interrupt ID Register (R) */
#define UART_FCR        2   /* FIFO Control Register (W) */
#define UART_LCR        3   /* Line Control Register */
#define UART_MCR        4   /* Modem Control Register */
#define UART_LSR        5   /* Line Status Register */
#define UART_MSR        6   /* Modem Status Register */
#define UART_SCRATCH    7   /* Scratch Register */

/* When DLAB=1 (LCR bit 7) */
#define UART_DLL        0   /* Divisor Latch Low */
#define UART_DLH        1   /* Divisor Latch High */

/* Line Status Register bits */
#define LSR_DATA_READY      (1 << 0)
#define LSR_OVERRUN_ERROR   (1 << 1)
#define LSR_PARITY_ERROR    (1 << 2)
#define LSR_FRAMING_ERROR   (1 << 3)
#define LSR_BREAK_INDICATOR (1 << 4)
#define LSR_THR_EMPTY       (1 << 5)    /* Transmitter Holding Register empty */
#define LSR_TRANSMITTER_EMPTY (1 << 6)  /* Transmitter empty */
#define LSR_FIFO_ERROR      (1 << 7)

/* Line Control Register bits */
#define LCR_WORD_LEN_5  0x00
#define LCR_WORD_LEN_6  0x01
#define LCR_WORD_LEN_7  0x02
#define LCR_WORD_LEN_8  0x03
#define LCR_STOP_BITS_1 0x00
#define LCR_STOP_BITS_2 0x04
#define LCR_PARITY_NONE 0x00
#define LCR_PARITY_ODD  0x08
#define LCR_PARITY_EVEN 0x18
#define LCR_DLAB        0x80    /* Divisor Latch Access Bit */

/* FIFO Control Register bits */
#define FCR_ENABLE_FIFO     0x01
#define FCR_CLEAR_RX_FIFO   0x02
#define FCR_CLEAR_TX_FIFO   0x04
#define FCR_TRIGGER_1       0x00
#define FCR_TRIGGER_4       0x40
#define FCR_TRIGGER_8       0x80
#define FCR_TRIGGER_14      0xC0

/* ===================================================================== */
/* Current UART port */
/* ===================================================================== */

static uint16_t uart_port = COM1_PORT;

/* ===================================================================== */
/* Helper Functions */
/* ===================================================================== */

static inline void uart_write_reg(uint8_t reg, uint8_t value)
{
    outb(uart_port + reg, value);
}

static inline uint8_t uart_read_reg(uint8_t reg)
{
    return inb(uart_port + reg);
}

static int uart_is_transmit_empty(void)
{
    return uart_read_reg(UART_LSR) & LSR_THR_EMPTY;
}

static int uart_is_data_ready(void)
{
    return uart_read_reg(UART_LSR) & LSR_DATA_READY;
}

/* ===================================================================== */
/* Initialization */
/* ===================================================================== */

void uart_early_init(void)
{
    /* Disable interrupts */
    uart_write_reg(UART_IER, 0x00);
    
    /* Enable DLAB to set baud rate */
    uart_write_reg(UART_LCR, LCR_DLAB);
    
    /* Set baud rate to 115200 (divisor = 1) */
    uart_write_reg(UART_DLL, 0x01);
    uart_write_reg(UART_DLH, 0x00);
    
    /* 8 bits, no parity, 1 stop bit */
    uart_write_reg(UART_LCR, LCR_WORD_LEN_8 | LCR_STOP_BITS_1 | LCR_PARITY_NONE);
    
    /* Enable FIFO, clear them, 14-byte threshold */
    uart_write_reg(UART_FCR, FCR_ENABLE_FIFO | FCR_CLEAR_RX_FIFO | 
                              FCR_CLEAR_TX_FIFO | FCR_TRIGGER_14);
    
    /* Enable IRQs, RTS/DSR set */
    uart_write_reg(UART_MCR, 0x0B);
}

void uart_init(void)
{
    uart_early_init();
}

/* ===================================================================== */
/* I/O Functions */
/* ===================================================================== */

void uart_putc(char c)
{
    /* Wait for transmit buffer to be empty */
    while (!uart_is_transmit_empty())
        ;
    
    uart_write_reg(UART_DATA, c);
    
    /* Handle newlines */
    if (c == '\n') {
        while (!uart_is_transmit_empty())
            ;
        uart_write_reg(UART_DATA, '\r');
    }
}

char uart_getc(void)
{
    /* Wait for data to be available */
    while (!uart_is_data_ready())
        ;
    
    return uart_read_reg(UART_DATA);
}

int uart_getc_nonblock(void)
{
    if (!uart_is_data_ready()) {
        return -1;
    }
    
    return uart_read_reg(UART_DATA);
}

void uart_puts(const char *s)
{
    while (*s) {
        uart_putc(*s++);
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
    for (size_t i = 0; i < len; i++) {
        buf[i] = uart_getc();
    }
    return len;
}
