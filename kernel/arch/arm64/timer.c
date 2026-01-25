/*
 * UnixOS Kernel - Timer Implementation
 * 
 * ARM Generic Timer using virtual timer for OS timing.
 */

#include "arch/arm64/timer.h"
#include "arch/arm64/gic.h"
#include "sched/sched.h"
#include "printk.h"

/* ===================================================================== */
/* Static data */
/* ===================================================================== */

static uint64_t timer_frequency;
static uint64_t ticks_per_ms;
static uint64_t ticks_per_us;

/* Scheduler tick rate (100Hz = 10ms period) */
#define HZ                  100
#define TICK_PERIOD_MS      (1000 / HZ)

static uint64_t jiffies = 0;  /* Tick counter */

/* ===================================================================== */
/* System register helpers */
/* ===================================================================== */

static inline uint64_t read_cntfrq(void)
{
    uint64_t val;
    asm volatile("mrs %0, cntfrq_el0" : "=r" (val));
    return val;
}

static inline uint64_t __attribute__((unused)) read_cntpct(void)
{
    uint64_t val;
    asm volatile("mrs %0, cntpct_el0" : "=r" (val));
    return val;
}

static inline uint64_t read_cntvct(void)
{
    uint64_t val;
    asm volatile("mrs %0, cntvct_el0" : "=r" (val));
    return val;
}

static inline void write_cntv_tval(uint64_t val)
{
    asm volatile("msr cntv_tval_el0, %0" : : "r" (val));
}

static inline void write_cntv_ctl(uint64_t val)
{
    asm volatile("msr cntv_ctl_el0, %0" : : "r" (val));
}

static inline uint64_t __attribute__((unused)) read_cntv_ctl(void)
{
    uint64_t val;
    asm volatile("mrs %0, cntv_ctl_el0" : "=r" (val));
    return val;
}

/* ===================================================================== */
/* Timer interrupt handler */
/* ===================================================================== */

static void timer_irq_handler(uint32_t irq, void *data)
{
    (void)irq;
    (void)data;
    
    /* Increment jiffies */
    jiffies++;
    
    /* Set up next timer interrupt */
    write_cntv_tval(timer_frequency / HZ);
    
    /* Invoke scheduler for preemptive multitasking */
    extern void process_schedule_from_irq(void);
    process_schedule_from_irq();
}

/* ===================================================================== */
/* Public functions */
/* ===================================================================== */

void timer_init(void)
{
    printk(KERN_INFO "TIMER: Initializing ARM generic timer\n");
    
    /* Read timer frequency */
    timer_frequency = read_cntfrq();
    
    printk("TIMER: Read CNTFRQ done\n");
    
    if (timer_frequency == 0) {
        /* Default to 24MHz if not set (common for QEMU) */
        timer_frequency = 24000000;
        printk(KERN_WARNING "TIMER: CNTFRQ not set, using default 24MHz\n");
    }
    
    ticks_per_ms = timer_frequency / 1000;
    ticks_per_us = timer_frequency / 1000000;
    
    printk("TIMER: Calculated ticks\n");
    
    /* Register timer interrupt handler */
    gic_register_handler(TIMER_IRQ_VIRT, timer_irq_handler, NULL);
    
    printk("TIMER: Handler registered\n");
    
    /* Note: We don't enable the timer interrupt here - 
     * it will be enabled later when interrupts are turned on */
    gic_set_priority(TIMER_IRQ_VIRT, 0x80);
    /* Don't enable IRQ yet - will be done after full init */
    /* gic_enable_irq(TIMER_IRQ_VIRT); */
    
    printk("TIMER: Priority set (IRQ not enabled yet)\n");
    
    /* Set up first timer tick value but don't enable timer */
    write_cntv_tval(timer_frequency / HZ);
    
    printk("TIMER: TVAL set\n");
    
    /* Enable timer and IRQ now */
    write_cntv_ctl(TIMER_CTL_ENABLE);
    gic_enable_irq(TIMER_IRQ_VIRT);
    
    printk(KERN_INFO "TIMER: Initialized and IRQ enabled\n");
}

uint64_t timer_get_frequency(void)
{
    return timer_frequency;
}

uint64_t timer_get_count(void)
{
    return read_cntvct();
}

void timer_set_next(uint64_t ticks)
{
    write_cntv_tval(ticks);
}

uint64_t timer_get_ms(void)
{
    return read_cntvct() / ticks_per_ms;
}

uint64_t timer_get_us(void)
{
    if (ticks_per_us == 0) {
        return 0;
    }
    return read_cntvct() / ticks_per_us;
}

void timer_delay_ms(uint32_t ms)
{
    uint64_t start = timer_get_count();
    uint64_t wait = (uint64_t)ms * ticks_per_ms;
    
    while ((timer_get_count() - start) < wait) {
        /* Busy wait */
        asm volatile("yield");
    }
}

void timer_delay_us(uint32_t us)
{
    uint64_t start = timer_get_count();
    uint64_t wait = (uint64_t)us * ticks_per_us;
    
    while ((timer_get_count() - start) < wait) {
        /* Busy wait */
        asm volatile("yield");
    }
}
