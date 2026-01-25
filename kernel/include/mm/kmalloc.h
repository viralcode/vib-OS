/*
 * UnixOS Kernel - Kernel Heap Allocator (kmalloc)
 * 
 * Simple SLUB-like allocator for kernel dynamic memory.
 */

#ifndef _MM_KMALLOC_H
#define _MM_KMALLOC_H

#include "types.h"

/* Allocation flags */
#define GFP_KERNEL      0x00    /* Normal kernel allocation */
#define GFP_ATOMIC      0x01    /* Allocation in interrupt context */
#define GFP_ZERO        0x02    /* Zero the memory */

/**
 * kmalloc_init - Initialize the kernel heap
 */
void kmalloc_init(void);

/**
 * kmalloc - Allocate kernel memory
 * @size: Number of bytes to allocate
 * @flags: Allocation flags (GFP_*)
 * 
 * Return: Pointer to allocated memory, or NULL on failure
 */
void *_kmalloc(size_t size, uint32_t flags);

/* Variadic macro to make flags optional (default 0) */
#define _KMALLOC_1(size)        _kmalloc((size), 0)
#define _KMALLOC_2(size, flags) _kmalloc((size), (flags))
#define _KMALLOC_NARG(...) _KMALLOC_NARG_(__VA_ARGS__, 2, 1)
#define _KMALLOC_NARG_(_1, _2, N, ...) N
#define _KMALLOC_DISPATCH(N) _KMALLOC_##N
#define _KMALLOC_DISPATCH_(N) _KMALLOC_DISPATCH(N)
#define kmalloc(...) _KMALLOC_DISPATCH_(_KMALLOC_NARG(__VA_ARGS__))(__VA_ARGS__)

/**
 * kzalloc - Allocate zeroed kernel memory
 * @size: Number of bytes to allocate
 * @flags: Allocation flags
 * 
 * Return: Pointer to zeroed memory, or NULL on failure
 */
void *kzalloc(size_t size, uint32_t flags);

/**
 * kfree - Free kernel memory
 * @ptr: Pointer to free
 */
void kfree(void *ptr);

/**
 * krealloc - Reallocate kernel memory
 * @ptr: Original pointer
 * @new_size: New size
 * @flags: Allocation flags
 * 
 * Return: Pointer to reallocated memory
 */
void *krealloc(void *ptr, size_t new_size, uint32_t flags);

/**
 * kmalloc_get_stats - Get heap statistics
 * @total: Output for total heap size
 * @used: Output for used size
 * @free: Output for free size
 */
void kmalloc_get_stats(size_t *total, size_t *used, size_t *free);

#endif /* _MM_KMALLOC_H */
