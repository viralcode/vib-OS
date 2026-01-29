/*
 * VibCode x64 - Simple Kernel Memory Allocator
 */

#ifndef _KMALLOC_H
#define _KMALLOC_H

#include "types.h"

/* Allocation flags (simplified) */
#define GFP_KERNEL 0
#define GFP_ATOMIC 1

/* Initialize the heap */
void kmalloc_init(void);

/* Allocate memory */
void *kmalloc(size_t size);
void *kzalloc(size_t size); /* Zero-initialized */
void *krealloc(void *ptr, size_t new_size);

/* Free memory */
void kfree(void *ptr);

/* Get heap statistics */
size_t kmalloc_get_used(void);
size_t kmalloc_get_free(void);

#endif /* _KMALLOC_H */
