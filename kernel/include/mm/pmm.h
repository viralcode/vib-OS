/*
 * UnixOS Kernel - Physical Memory Manager Header
 */

#ifndef _MM_PMM_H
#define _MM_PMM_H

#include "types.h"

/* ===================================================================== */
/* Page size definitions */
/* ===================================================================== */

#define PAGE_SHIFT          12
#define PAGE_SIZE           (1UL << PAGE_SHIFT)     /* 4KB */
#define PAGE_MASK           (~(PAGE_SIZE - 1))

#define LARGE_PAGE_SHIFT    21
#define LARGE_PAGE_SIZE     (1UL << LARGE_PAGE_SHIFT)   /* 2MB */

/* ===================================================================== */
/* Address conversion macros */
/* ===================================================================== */

#define PHYS_TO_PFN(addr)   ((addr) >> PAGE_SHIFT)
#define PFN_TO_PHYS(pfn)    ((pfn) << PAGE_SHIFT)

#define PAGE_ALIGN(addr)    ALIGN(addr, PAGE_SIZE)
#define PAGE_ALIGN_DOWN(addr) ALIGN_DOWN(addr, PAGE_SIZE)

/* ===================================================================== */
/* Memory zones */
/* ===================================================================== */

typedef enum {
    ZONE_DMA,       /* 0-16MB, for legacy DMA */
    ZONE_NORMAL,    /* 16MB-4GB, normal memory */
    ZONE_HIGH,      /* Above 4GB */
    ZONE_COUNT
} zone_type_t;

/* ===================================================================== */
/* Page flags */
/* ===================================================================== */

#define PAGE_FLAG_FREE          (0)
#define PAGE_FLAG_USED          (1 << 0)
#define PAGE_FLAG_KERNEL        (1 << 1)
#define PAGE_FLAG_LOCKED        (1 << 2)
#define PAGE_FLAG_RESERVED      (1 << 3)
#define PAGE_FLAG_SLAB          (1 << 4)

/* ===================================================================== */
/* Page structure */
/* ===================================================================== */

struct page {
    uint32_t flags;
    uint32_t order;         /* For buddy allocator */
    struct page *next;      /* Free list link */
    void *slab;             /* For slab allocator */
    atomic_t refcount;
};

/* ===================================================================== */
/* Function declarations */
/* ===================================================================== */

/**
 * pmm_init - Initialize physical memory manager
 * 
 * Discovers available memory from device tree or UEFI memory map.
 * Sets up the buddy allocator.
 * 
 * Return: 0 on success, negative on error
 */
int pmm_init(void);

/**
 * pmm_alloc_page - Allocate a single physical page
 * 
 * Return: Physical address of allocated page, or 0 on failure
 */
phys_addr_t pmm_alloc_page(void);

/**
 * pmm_alloc_pages - Allocate contiguous pages
 * @order: Power of 2 number of pages (0=1, 1=2, 2=4, etc.)
 * 
 * Return: Physical address of first page, or 0 on failure
 */
phys_addr_t pmm_alloc_pages(unsigned int order);

/**
 * pmm_free_page - Free a single physical page
 * @addr: Physical address of page to free
 */
void pmm_free_page(phys_addr_t addr);

/**
 * pmm_free_pages - Free contiguous pages
 * @addr: Physical address of first page
 * @order: Power of 2 number of pages
 */
void pmm_free_pages(phys_addr_t addr, unsigned int order);

/**
 * pmm_get_free_memory - Get total free physical memory
 * 
 * Return: Bytes of free memory
 */
size_t pmm_get_free_memory(void);

/**
 * pmm_get_total_memory - Get total physical memory
 * 
 * Return: Total bytes of physical memory
 */
size_t pmm_get_total_memory(void);

/**
 * pmm_page_to_phys - Convert page struct to physical address
 */
phys_addr_t pmm_page_to_phys(struct page *page);

/**
 * pmm_phys_to_page - Convert physical address to page struct
 */
struct page *pmm_phys_to_page(phys_addr_t addr);

#endif /* _MM_PMM_H */
