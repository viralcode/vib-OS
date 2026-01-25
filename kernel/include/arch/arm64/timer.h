/*
 * UnixOS Kernel - Timer Header
 */

#ifndef _ARCH_ARM64_TIMER_H
#define _ARCH_ARM64_TIMER_H

#include "types.h"

/* ===================================================================== */
/* ARM Generic Timer */
/* ===================================================================== */

/* Timer IRQ numbers (PPI) */
#define TIMER_IRQ_SEC_PHYS      29  /* Secure physical timer */
#define TIMER_IRQ_PHYS          30  /* Non-secure physical timer */
#define TIMER_IRQ_VIRT          27  /* Virtual timer */
#define TIMER_IRQ_HYP_PHYS      26  /* Hypervisor physical timer */

/* Timer control register bits */
#define TIMER_CTL_ENABLE        (1 << 0)
#define TIMER_CTL_IMASK         (1 << 1)
#define TIMER_CTL_ISTATUS       (1 << 2)

/* ===================================================================== */
/* Function declarations */
/* ===================================================================== */

/**
 * timer_init - Initialize the system timer
 */
void timer_init(void);

/**
 * timer_get_frequency - Get timer frequency in Hz
 * 
 * Return: Timer frequency
 */
uint64_t timer_get_frequency(void);

/**
 * timer_get_count - Get current timer count
 * 
 * Return: Current count value
 */
uint64_t timer_get_count(void);

/**
 * timer_set_next - Set next timer interrupt
 * @ticks: Number of ticks until interrupt
 */
void timer_set_next(uint64_t ticks);

/**
 * timer_get_ms - Get milliseconds since boot
 * 
 * Return: Milliseconds since boot
 */
uint64_t timer_get_ms(void);

/**
 * timer_get_us - Get microseconds since boot
 * 
 * Return: Microseconds since boot
 */
uint64_t timer_get_us(void);

/**
 * timer_delay_ms - Busy-wait for milliseconds
 * @ms: Milliseconds to wait
 */
void timer_delay_ms(uint32_t ms);

/**
 * timer_delay_us - Busy-wait for microseconds
 * @us: Microseconds to wait
 */
void timer_delay_us(uint32_t us);

#endif /* _ARCH_ARM64_TIMER_H */
