/*
 * x86 PIT (Programmable Interval Timer) Driver
 * 8253/8254 PIT for x86 32-bit
 */

#include "types.h"

/* PIT ports */
#define PIT_CHANNEL0    0x40
#define PIT_CHANNEL1    0x41
#define PIT_CHANNEL2    0x42
#define PIT_COMMAND     0x43

/* PIT frequency */
#define PIT_FREQUENCY   1193182  /* Hz */
#define TIMER_HZ        100      /* 100 Hz = 10ms tick */

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

void pit_init(void)
{
    /* Calculate divisor for desired frequency */
    uint32_t divisor = PIT_FREQUENCY / TIMER_HZ;
    
    /* Send command byte */
    /* Channel 0, lobyte/hibyte, rate generator, binary mode */
    outb(PIT_COMMAND, 0x36);
    
    /* Send divisor */
    outb(PIT_CHANNEL0, (uint8_t)(divisor & 0xFF));
    outb(PIT_CHANNEL0, (uint8_t)((divisor >> 8) & 0xFF));
    
    /* Enable IRQ 0 (timer interrupt) */
    extern void pic_clear_mask(uint8_t irq);
    pic_clear_mask(0);
}

void pit_handler(void)
{
    /* Call architecture timer tick */
    extern void arch_timer_tick(void);
    arch_timer_tick();
    
    /* Send EOI to PIC */
    extern void pic_send_eoi(uint8_t irq);
    pic_send_eoi(0);
}
