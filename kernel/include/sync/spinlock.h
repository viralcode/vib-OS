/*
 * vib-OS Kernel - Spinlock Implementation
 *
 * Provides mutual exclusion primitives for protecting critical sections.
 * IRQ-safe variants disable interrupts to prevent deadlocks.
 */

#ifndef _SYNC_SPINLOCK_H
#define _SYNC_SPINLOCK_H

#include "../types.h"

/* Spinlock structure */
typedef struct spinlock {
  volatile uint32_t lock;
#ifdef DEBUG_SPINLOCK
  const char *name;
  int held_by_cpu;
#endif
} spinlock_t;

/* Static initializer */
#define SPINLOCK_INIT {.lock = 0}
#define DEFINE_SPINLOCK(name) spinlock_t name = SPINLOCK_INIT

/* Spinlock API */
void spin_lock_init(spinlock_t *lock);
void spin_lock(spinlock_t *lock);
void spin_unlock(spinlock_t *lock);
int spin_trylock(spinlock_t *lock);

/* IRQ-safe variants - disable interrupts while holding lock */
uint64_t spin_lock_irqsave(spinlock_t *lock);
void spin_unlock_irqrestore(spinlock_t *lock, uint64_t flags);

/* Architecture-specific interrupt control */
static inline uint64_t arch_irq_save_local(void) {
  uint64_t flags;
#ifdef ARCH_ARM64
  asm volatile("mrs %0, daif" : "=r"(flags));
  asm volatile("msr daifset, #0xf" ::: "memory");
#elif defined(ARCH_X86_64) || defined(ARCH_X86)
  asm volatile("pushfq\n\t"
               "pop %0\n\t"
               "cli"
               : "=r"(flags)
               :
               : "memory");
#else
  flags = 0;
#endif
  return flags;
}

static inline void arch_irq_restore_local(uint64_t flags) {
#ifdef ARCH_ARM64
  asm volatile("msr daif, %0" ::"r"(flags) : "memory");
#elif defined(ARCH_X86_64) || defined(ARCH_X86)
  asm volatile("push %0\n\t"
               "popfq"
               :
               : "r"(flags)
               : "memory", "cc");
#endif
}

#endif /* _SYNC_SPINLOCK_H */
