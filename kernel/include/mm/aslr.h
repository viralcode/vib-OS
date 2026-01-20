/*
 * vib-OS Kernel - Address Space Layout Randomization (ASLR)
 *
 * Provides base address randomization for user processes to make
 * buffer overflow exploitation more difficult.
 */

#ifndef _MM_ASLR_H
#define _MM_ASLR_H

#include "../types.h"

/* ASLR entropy ranges */
#define ASLR_STACK_BITS 12 /* 4KB * 4096 = 16MB randomization */
#define ASLR_HEAP_BITS 12  /* 16MB heap randomization */
#define ASLR_MMAP_BITS 16  /* 64K * 64KB = 4GB randomization for mmap */
#define ASLR_EXEC_BITS 16  /* Executable base randomization */

/* Initialize ASLR with entropy from hardware timer */
void aslr_init(void);

/* Get random offset for user stack (page-aligned) */
uint64_t aslr_stack_offset(void);

/* Get random offset for user heap (page-aligned) */
uint64_t aslr_heap_offset(void);

/* Get random offset for executable load address (64KB-aligned) */
uint64_t aslr_exec_offset(void);

/* Get random offset for mmap region (page-aligned) */
uint64_t aslr_mmap_offset(void);

/* Get raw random bits */
uint64_t aslr_random(void);

#endif /* _MM_ASLR_H */
