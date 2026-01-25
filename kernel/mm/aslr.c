/*
 * vib-OS Kernel - ASLR Implementation
 *
 * Uses hardware timer counter as entropy source for address randomization.
 * Implements a simple LCG PRNG seeded from timer values.
 */

#include "../include/mm/aslr.h"
#include "../include/printk.h"

/* ===================================================================== */
/* PRNG State */
/* ===================================================================== */

/* LCG constants (Knuth's recommended values) */
#define LCG_MULTIPLIER 6364136223846793005ULL
#define LCG_INCREMENT 1442695040888963407ULL

static uint64_t aslr_seed = 0;
static int aslr_initialized = 0;

/* ===================================================================== */
/* Entropy Collection */
/* ===================================================================== */

static uint64_t get_hardware_entropy(void) {
  uint64_t entropy = 0;

#ifdef ARCH_ARM64
  /* Use ARM64 counter timer (CNTPCT_EL0) for randomness */
  asm volatile("mrs %0, cntpct_el0" : "=r"(entropy));

  /* Mix in some CNTVCT as well */
  uint64_t virt_counter;
  asm volatile("mrs %0, cntvct_el0" : "=r"(virt_counter));
  entropy ^= virt_counter;
#elif defined(ARCH_X86_64) || defined(ARCH_X86)
  /* Use RDTSC for x86 */
  uint32_t lo, hi;
  asm volatile("rdtsc" : "=a"(lo), "=d"(hi));
  entropy = ((uint64_t)hi << 32) | lo;
#else
  /* Fallback: use a constant (BAD - no real randomness) */
  entropy = 0x1234567890ABCDEFULL;
#endif

  return entropy;
}

/* ===================================================================== */
/* PRNG Implementation */
/* ===================================================================== */

static uint64_t next_random(void) {
  aslr_seed = aslr_seed * LCG_MULTIPLIER + LCG_INCREMENT;
  return aslr_seed;
}

/* ===================================================================== */
/* Public API */
/* ===================================================================== */

void aslr_init(void) {
  /* Collect multiple entropy samples and mix them */
  uint64_t entropy1 = get_hardware_entropy();

  /* Delay to get different timer value */
  for (volatile int i = 0; i < 1000; i++) {
  }

  uint64_t entropy2 = get_hardware_entropy();

  /* Mix entropy sources */
  aslr_seed = entropy1 ^ (entropy2 << 13) ^ (entropy2 >> 7);

  /* Warm up the PRNG (discard first few outputs) */
  for (int i = 0; i < 8; i++) {
    next_random();
  }

  aslr_initialized = 1;
  printk(KERN_INFO "ASLR: Initialized with seed 0x%llx\n",
         (unsigned long long)(aslr_seed & 0xFFFFFFFF)); /* Only show low bits */
}

uint64_t aslr_random(void) {
  if (!aslr_initialized) {
    aslr_init();
  }
  return next_random();
}

uint64_t aslr_stack_offset(void) {
  /* Generate random offset, mask to ASLR_STACK_BITS, align to page */
  uint64_t random = aslr_random();
  uint64_t offset = (random & ((1ULL << ASLR_STACK_BITS) - 1))
                    << 12; /* Page-aligned */
  return offset;
}

uint64_t aslr_heap_offset(void) {
  uint64_t random = aslr_random();
  uint64_t offset = (random & ((1ULL << ASLR_HEAP_BITS) - 1))
                    << 12; /* Page-aligned */
  return offset;
}

uint64_t aslr_exec_offset(void) {
  uint64_t random = aslr_random();
  /* 64KB aligned for ELF segment alignment */
  uint64_t offset = (random & ((1ULL << ASLR_EXEC_BITS) - 1)) << 16;
  return offset;
}

uint64_t aslr_mmap_offset(void) {
  uint64_t random = aslr_random();
  uint64_t offset = (random & ((1ULL << ASLR_MMAP_BITS) - 1))
                    << 12; /* Page-aligned */
  return offset;
}
