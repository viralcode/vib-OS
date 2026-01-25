/*
 * vib-OS Kernel - Spinlock Implementation
 *
 * Architecture-specific spinlock implementations using atomic operations.
 * Provides mutual exclusion for multiprocessor and interrupt safety.
 */

#include "../include/sync/spinlock.h"

/* Initialize a spinlock */
void spin_lock_init(spinlock_t *lock) {
  lock->lock = 0;
#ifdef DEBUG_SPINLOCK
  lock->name = NULL;
  lock->held_by_cpu = -1;
#endif
}

#ifdef ARCH_ARM64
/*
 * ARM64 spinlock using Load-Exclusive/Store-Exclusive
 * LDAXR provides acquire semantics, STXR provides release semantics
 */
void spin_lock(spinlock_t *lock) {
  uint32_t tmp;
  asm volatile("   sevl\n"                   /* Set event locally */
               "1: wfe\n"                    /* Wait for event (power saving) */
               "2: ldaxr   %w0, [%1]\n"      /* Load exclusive with acquire */
               "   cbnz    %w0, 1b\n"        /* If locked, wait for event */
               "   stxr    %w0, %w2, [%1]\n" /* Try to acquire */
               "   cbnz    %w0, 2b\n"        /* If failed, retry load */
               : "=&r"(tmp)
               : "r"(&lock->lock), "r"(1)
               : "memory");
}

void spin_unlock(spinlock_t *lock) {
  asm volatile("stlr wzr, [%0]" /* Store release (0) - release lock */
               :
               : "r"(&lock->lock)
               : "memory");
}

int spin_trylock(spinlock_t *lock) {
  uint32_t tmp, result;
  asm volatile("   ldaxr   %w0, [%2]\n"      /* Load exclusive with acquire */
               "   cbnz    %w0, 1f\n"        /* If already locked, fail */
               "   stxr    %w0, %w3, [%2]\n" /* Try to acquire */
               "   cbz     %w0, 2f\n"        /* If succeeded, return 1 */
               "1: mov     %w1, #0\n"        /* Failure path */
               "   b       3f\n"
               "2: mov     %w1, #1\n" /* Success path */
               "3:\n"
               : "=&r"(tmp), "=r"(result)
               : "r"(&lock->lock), "r"(1)
               : "memory");
  return result;
}

#elif defined(ARCH_X86_64)
/*
 * x86_64 spinlock using XCHG (implicit lock prefix)
 */
void spin_lock(spinlock_t *lock) {
  while (1) {
    /* Spin on read first to avoid cache line bouncing */
    while (lock->lock) {
      asm volatile("pause" ::: "memory");
    }

    /* Try to acquire */
    uint32_t expected = 0;
    uint32_t desired = 1;
    uint32_t old;
    asm volatile("lock cmpxchgl %2, %1"
                 : "=a"(old), "+m"(lock->lock)
                 : "r"(desired), "0"(expected)
                 : "memory", "cc");
    if (old == 0) {
      return; /* Acquired */
    }
  }
}

void spin_unlock(spinlock_t *lock) {
  asm volatile("movl $0, %0" : "=m"(lock->lock) : : "memory");
}

int spin_trylock(spinlock_t *lock) {
  uint32_t old;
  asm volatile("lock xchgl %0, %1"
               : "=r"(old), "+m"(lock->lock)
               : "0"(1)
               : "memory");
  return old == 0;
}

#elif defined(ARCH_X86)
/*
 * x86 32-bit spinlock
 */
void spin_lock(spinlock_t *lock) {
  while (1) {
    while (lock->lock) {
      asm volatile("pause" ::: "memory");
    }

    uint32_t old;
    asm volatile("lock xchgl %0, %1"
                 : "=r"(old), "+m"(lock->lock)
                 : "0"(1)
                 : "memory");
    if (old == 0) {
      return;
    }
  }
}

void spin_unlock(spinlock_t *lock) {
  asm volatile("movl $0, %0" : "=m"(lock->lock) : : "memory");
}

int spin_trylock(spinlock_t *lock) {
  uint32_t old;
  asm volatile("lock xchgl %0, %1"
               : "=r"(old), "+m"(lock->lock)
               : "0"(1)
               : "memory");
  return old == 0;
}

#else
/* Fallback for unsupported architectures - not safe for SMP! */
void spin_lock(spinlock_t *lock) {
  while (__sync_lock_test_and_set(&lock->lock, 1)) {
    /* spin */
  }
}

void spin_unlock(spinlock_t *lock) { __sync_lock_release(&lock->lock); }

int spin_trylock(spinlock_t *lock) {
  return __sync_lock_test_and_set(&lock->lock, 1) == 0;
}
#endif

/*
 * IRQ-safe spinlock variants
 * These disable interrupts before acquiring the lock to prevent deadlock
 * when an interrupt handler tries to acquire a lock held by the interrupted
 * code.
 */
uint64_t spin_lock_irqsave(spinlock_t *lock) {
  uint64_t flags = arch_irq_save_local();
  spin_lock(lock);
  return flags;
}

void spin_unlock_irqrestore(spinlock_t *lock, uint64_t flags) {
  spin_unlock(lock);
  arch_irq_restore_local(flags);
}
