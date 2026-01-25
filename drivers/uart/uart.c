/*
 * UnixOS Kernel - UART Driver Implementation
 * 
 * Supports:
 * - Apple Silicon UART (Samsung S5L UART compatible)
 * - PL011 UART (for QEMU virt machine testing)
 */

#include "drivers/uart.h"
#include "types.h"

/* ===================================================================== */
/* PL011 UART registers (for QEMU virt machine) */
/* ===================================================================== */

/* QEMU virt machine UART base address */
#define PL011_BASE          0x09000000

/* PL011 register offsets */
#define PL011_DR            0x000   /* Data Register */
#define PL011_FR            0x018   /* Flag Register */
#define PL011_IBRD          0x024   /* Integer Baud Rate */
#define PL011_FBRD          0x028   /* Fractional Baud Rate */
#define PL011_LCR_H         0x02C   /* Line Control */
#define PL011_CR            0x030   /* Control Register */
#define PL011_IMSC          0x038   /* Interrupt Mask */
#define PL011_ICR           0x044   /* Interrupt Clear */

/* Flag register bits */
#define PL011_FR_TXFF       (1 << 5)    /* TX FIFO full */
#define PL011_FR_RXFE       (1 << 4)    /* RX FIFO empty */
#define PL011_FR_BUSY       (1 << 3)    /* UART busy */

/* Control register bits */
#define PL011_CR_RXE        (1 << 9)    /* RX enable */
#define PL011_CR_TXE        (1 << 8)    /* TX enable */
#define PL011_CR_UARTEN     (1 << 0)    /* UART enable */

/* Line control bits */
#define PL011_LCR_WLEN8     (3 << 5)    /* 8 bit word */
#define PL011_LCR_FEN       (1 << 4)    /* FIFO enable */

/* ===================================================================== */
/* Apple Silicon UART registers */
/* ===================================================================== */

/* Apple UART base (actual address obtained from device tree) */
#define APPLE_UART_BASE     0x235200000  /* M2 serial port */

/* Apple UART register offsets (Samsung S5L compatible) */
#define APPLE_ULCON         0x000   /* Line Control */
#define APPLE_UCON          0x004   /* Control */
#define APPLE_UFCON         0x008   /* FIFO Control */
#define APPLE_UTRSTAT       0x010   /* TX/RX Status */
#define APPLE_UTXH          0x020   /* TX Buffer */
#define APPLE_URXH          0x024   /* RX Buffer */

/* TX/RX Status bits */
#define APPLE_UTRSTAT_TXBE  (1 << 1)    /* TX buffer empty */
#define APPLE_UTRSTAT_RXDR  (1 << 0)    /* RX data ready */

/* ===================================================================== */
/* Current UART configuration */
/* ===================================================================== */

/* We use PL011 for QEMU, Apple UART for real hardware */
/* This is selected at compile time or runtime based on DTB */

#define USE_PL011   1   /* Default to PL011 for QEMU testing */

#if USE_PL011
#define UART_BASE   PL011_BASE
#else
#define UART_BASE   APPLE_UART_BASE
#endif

/* Memory-mapped I/O helpers */
static inline void mmio_write32(uint64_t addr, uint32_t val)
{
    *(volatile uint32_t *)addr = val;
}

static inline uint32_t mmio_read32(uint64_t addr)
{
    return *(volatile uint32_t *)addr;
}

/* ===================================================================== */
/* PL011 Implementation */
/* ===================================================================== */

#if USE_PL011

void uart_early_init(void)
{
    /* Disable UART */
    mmio_write32(PL011_BASE + PL011_CR, 0);
    
    /* Clear pending interrupts */
    mmio_write32(PL011_BASE + PL011_ICR, 0x7FF);
    
    /* Set baud rate (115200 @ 24MHz clock) */
    /* Divisor = 24000000 / (16 * 115200) = 13.0208 */
    mmio_write32(PL011_BASE + PL011_IBRD, 13);
    mmio_write32(PL011_BASE + PL011_FBRD, 1);  /* 0.0208 * 64 = 1.33 */
    
    /* 8 bits, no parity, 1 stop bit, FIFO enabled */
    mmio_write32(PL011_BASE + PL011_LCR_H, PL011_LCR_WLEN8 | PL011_LCR_FEN);
    
    /* Mask all interrupts */
    mmio_write32(PL011_BASE + PL011_IMSC, 0);
    
    /* Enable UART, TX, RX */
    mmio_write32(PL011_BASE + PL011_CR, PL011_CR_UARTEN | PL011_CR_TXE | PL011_CR_RXE);
}

void uart_init(void)
{
    /* Full init is same as early init for PL011 */
    uart_early_init();
}

void uart_putc(char c)
{
    /* Wait for TX FIFO to have space */
    while (mmio_read32(PL011_BASE + PL011_FR) & PL011_FR_TXFF)
        ;
    
    /* Write character */
    mmio_write32(PL011_BASE + PL011_DR, c);
    
    /* Handle newlines */
    if (c == '\n') {
        uart_putc('\r');
    }
}

char uart_getc(void)
{
    /* Wait for RX FIFO to have data */
    while (mmio_read32(PL011_BASE + PL011_FR) & PL011_FR_RXFE)
        ;
    
    return mmio_read32(PL011_BASE + PL011_DR) & 0xFF;
}

int uart_getc_nonblock(void)
{
    /* Check if data available */
    if (mmio_read32(PL011_BASE + PL011_FR) & PL011_FR_RXFE)
        return -1;
    
    return mmio_read32(PL011_BASE + PL011_DR) & 0xFF;
}

#else /* Apple Silicon UART */

void uart_early_init(void)
{
    /* Apple UART is usually initialized by iBoot/m1n1 */
    /* Just ensure TX is enabled */
    uint32_t ucon = mmio_read32(APPLE_UART_BASE + APPLE_UCON);
    ucon |= (1 << 2);  /* TX enable */
    mmio_write32(APPLE_UART_BASE + APPLE_UCON, ucon);
}

void uart_init(void)
{
    uart_early_init();
}

void uart_putc(char c)
{
    /* Wait for TX buffer empty */
    while (!(mmio_read32(APPLE_UART_BASE + APPLE_UTRSTAT) & APPLE_UTRSTAT_TXBE))
        ;
    
    /* Write character */
    mmio_write32(APPLE_UART_BASE + APPLE_UTXH, c);
    
    if (c == '\n') {
        uart_putc('\r');
    }
}

char uart_getc(void)
{
    /* Wait for RX data ready */
    while (!(mmio_read32(APPLE_UART_BASE + APPLE_UTRSTAT) & APPLE_UTRSTAT_RXDR))
        ;
    
    return mmio_read32(APPLE_UART_BASE + APPLE_URXH) & 0xFF;
}

int uart_getc_nonblock(void)
{
    if (!(mmio_read32(APPLE_UART_BASE + APPLE_UTRSTAT) & APPLE_UTRSTAT_RXDR))
        return -1;
    
    return mmio_read32(APPLE_UART_BASE + APPLE_URXH) & 0xFF;
}

#endif /* USE_PL011 */

/* ===================================================================== */
/* Common functions */
/* ===================================================================== */

void uart_puts(const char *s)
{
    while (*s) {
        uart_putc(*s++);
    }
}

size_t uart_write(const char *buf, size_t len)
{
    size_t i;
    for (i = 0; i < len; i++) {
        uart_putc(buf[i]);
    }
    return len;
}

size_t uart_read(char *buf, size_t len)
{
    size_t i;
    for (i = 0; i < len; i++) {
        buf[i] = uart_getc();
    }
    return len;
}
