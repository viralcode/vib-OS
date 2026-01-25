/*
 * Vib-OS Kernel - x86_64 PIT (Programmable Interval Timer) Driver
 * 
 * 8253/8254 PIT for system timer
 */

#include "arch/arch.h"
#include "printk.h"
#include "types.h"

/* ===================================================================== */
/* PIT Ports */
/* ===================================================================== */

#define PIT_CHANNEL0    0x40
#define PIT_CHANNEL1    0x41
#define PIT_CHANNEL2    0x42
#define PIT_COMMAND     0x43

/* Command register bits */
#define PIT_CMD_BINARY      0x00    /* Use binary counter */
#define PIT_CMD_BCD         0x01    /* Use BCD counter */
#define PIT_CMD_MODE0       0x00    /* Interrupt on terminal count */
#define PIT_CMD_MODE1       0x02    /* Hardware re-triggerable one-shot */
#define PIT_CMD_MODE2       0x04    /* Rate generator */
#define PIT_CMD_MODE3       0x06    /* Square wave generator */
#define PIT_CMD_MODE4       0x08    /* Software triggered strobe */
#define PIT_CMD_MODE5       0x0A    /* Hardware triggered strobe */
#define PIT_CMD_LATCH       0x00    /* Latch count value */
#define PIT_CMD_LSB         0x10    /* Read/write LSB only */
#define PIT_CMD_MSB         0x20    /* Read/write MSB only */
#define PIT_CMD_BOTH        0x30    /* Read/write LSB then MSB */
#define PIT_CMD_CHANNEL0    0x00
#define PIT_CMD_CHANNEL1    0x40
#define PIT_CMD_CHANNEL2    0x80
#define PIT_CMD_READBACK    0xC0

/* PIT frequency */
#define PIT_FREQUENCY   1193182     /* Hz */

/* ===================================================================== */
/* Initialization */
/* ===================================================================== */

void pit_init(void)
{
    /* Set up channel 0 for 1000 Hz (1ms) timer */
    uint32_t divisor = PIT_FREQUENCY / 1000;
    
    printk(KERN_INFO "PIT: Initializing at 1000 Hz (divisor=%u)\n", divisor);
    
    /* Send command byte */
    outb(PIT_COMMAND, PIT_CMD_CHANNEL0 | PIT_CMD_BOTH | PIT_CMD_MODE2 | PIT_CMD_BINARY);
    
    /* Send divisor */
    outb(PIT_CHANNEL0, divisor & 0xFF);
    outb(PIT_CHANNEL0, (divisor >> 8) & 0xFF);
    
    printk(KERN_INFO "PIT: Initialized\n");
}

/* ===================================================================== */
/* Timer Functions */
/* ===================================================================== */

static volatile uint64_t pit_ticks = 0;

void pit_handler(void)
{
    pit_ticks++;
    extern void arch_timer_tick(void);
    arch_timer_tick();
}

uint64_t pit_get_ticks(void)
{
    return pit_ticks;
}

void pit_sleep(uint32_t ms)
{
    uint64_t target = pit_ticks + ms;
    while (pit_ticks < target) {
        arch_idle();
    }
}
