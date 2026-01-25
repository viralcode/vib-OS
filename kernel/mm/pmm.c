/*
 * UnixOS Kernel - Physical Memory Manager Implementation
 * 
 * Buddy allocator for physical page allocation.
 */

#include "mm/pmm.h"
#include "printk.h"

/* ===================================================================== */
/* Constants */
/* ===================================================================== */

#define MAX_ORDER           11      /* Maximum order (2^11 = 2048 pages = 8MB) */
#define BUDDY_MAX_PAGES     (1UL << MAX_ORDER)

/* Initial memory layout - will be updated from DTB/UEFI */
#define MEMORY_BASE         0x40000000  /* 1GB - typical for ARM64 */
#define MEMORY_SIZE         (256UL * 1024 * 1024)  /* 256MB - matches QEMU default */

/* ===================================================================== */
/* Static data */
/* ===================================================================== */

/* Free lists for each order */
static struct page *free_lists[MAX_ORDER + 1];

/* Page array - describes all physical pages */
static struct page *page_array;
static size_t total_pages;

/* Memory statistics */
static size_t free_pages_count;
static size_t total_memory;
static phys_addr_t memory_start;
static phys_addr_t memory_end;

/* Bitmap for early page tracking before page_array is set up */
/* Track 64K pages = 256MB - enough for initial boot */
#define EARLY_BITMAP_SIZE   (64 * 1024 / 8)  /* 8KB bitmap */
static uint8_t early_bitmap[EARLY_BITMAP_SIZE];
static bool early_mode = true;

/* ===================================================================== */
/* Helper functions */
/* ===================================================================== */

static inline size_t order_to_pages(unsigned int order)
{
    return 1UL << order;
}

static inline size_t order_to_size(unsigned int order)
{
    return order_to_pages(order) * PAGE_SIZE;
}

static inline unsigned int size_to_order(size_t size)
{
    unsigned int order = 0;
    size_t pages = (size + PAGE_SIZE - 1) / PAGE_SIZE;
    
    while ((1UL << order) < pages && order < MAX_ORDER) {
        order++;
    }
    
    return order;
}

/* ===================================================================== */
/* Early boot allocator (bitmap-based) */
/* ===================================================================== */

static void early_mark_used(phys_addr_t addr)
{
    size_t pfn = PHYS_TO_PFN(addr - memory_start);
    if (pfn < EARLY_BITMAP_SIZE * 8) {
        early_bitmap[pfn / 8] |= (1 << (pfn % 8));
    }
}

static void early_mark_free(phys_addr_t addr)
{
    size_t pfn = PHYS_TO_PFN(addr - memory_start);
    if (pfn < EARLY_BITMAP_SIZE * 8) {
        early_bitmap[pfn / 8] &= ~(1 << (pfn % 8));
    }
}

static bool early_is_free(phys_addr_t addr)
{
    size_t pfn = PHYS_TO_PFN(addr - memory_start);
    if (pfn >= EARLY_BITMAP_SIZE * 8) {
        return false;
    }
    return !(early_bitmap[pfn / 8] & (1 << (pfn % 8)));
}

static phys_addr_t early_alloc_page(void)
{
    for (size_t i = 0; i < EARLY_BITMAP_SIZE; i++) {
        if (early_bitmap[i] != 0xFF) {
            for (int j = 0; j < 8; j++) {
                if (!(early_bitmap[i] & (1 << j))) {
                    early_bitmap[i] |= (1 << j);
                    phys_addr_t addr = memory_start + (i * 8 + j) * PAGE_SIZE;
                    return addr;
                }
            }
        }
    }
    return 0;
}

/* ===================================================================== */
/* Buddy allocator */
/* ===================================================================== */

static inline phys_addr_t buddy_address(phys_addr_t addr, unsigned int order)
{
    return addr ^ (PAGE_SIZE << order);
}

static void buddy_add_to_list(phys_addr_t addr, unsigned int order)
{
    struct page *page = pmm_phys_to_page(addr);
    page->order = order;
    page->flags = PAGE_FLAG_FREE;
    page->next = free_lists[order];
    free_lists[order] = page;
}

static phys_addr_t buddy_remove_from_list(unsigned int order)
{
    if (!free_lists[order]) {
        return 0;
    }
    
    struct page *page = free_lists[order];
    free_lists[order] = page->next;
    page->next = NULL;
    page->flags = PAGE_FLAG_USED;
    
    return pmm_page_to_phys(page);
}

/* ===================================================================== */
/* Public functions */
/* ===================================================================== */

int pmm_init(void)
{
    printk("PMM: Starting init\n");
    
    /* For now, use hardcoded memory range */
    /* TODO: Parse device tree or UEFI memory map */
    
    memory_start = MEMORY_BASE;
    memory_end = MEMORY_BASE + MEMORY_SIZE;
    total_memory = MEMORY_SIZE;
    total_pages = total_memory / PAGE_SIZE;
    
    printk("PMM: Memory configured\n");
    
    /* Initialize free lists */
    for (int i = 0; i <= MAX_ORDER; i++) {
        free_lists[i] = NULL;
    }
    
    printk("PMM: Free lists cleared\n");
    
    /* Skip bitmap clearing - BSS should already be zero */
    printk("PMM: Skipping bitmap clear (BSS pre-zeroed)\n");
    
    /* Reserve kernel memory */
    extern char __kernel_start[];
    extern char __kernel_end[];
    
    phys_addr_t kernel_start = (phys_addr_t)__kernel_start;
    phys_addr_t kernel_end = (phys_addr_t)__kernel_end;
    
    printk("PMM: Got kernel addresses\n");
    
    /* Mark kernel pages as used */
    for (phys_addr_t addr = PAGE_ALIGN_DOWN(kernel_start);
         addr < PAGE_ALIGN(kernel_end);
         addr += PAGE_SIZE) {
        early_mark_used(addr);
    }
    
    printk("PMM: Kernel pages marked\n");
    
    /* Count free pages */
    free_pages_count = 0;
    for (size_t i = 0; i < total_pages && i < EARLY_BITMAP_SIZE * 8; i++) {
        if (!(early_bitmap[i / 8] & (1 << (i % 8)))) {
            free_pages_count++;
        }
    }
    
    printk("PMM: Init complete\n");
    
    /* TODO: Initialize buddy allocator with free pages */
    /* For now, we stay in early mode using bitmap */
    
    return 0;
}

phys_addr_t pmm_alloc_page(void)
{
    return pmm_alloc_pages(0);
}

phys_addr_t pmm_alloc_pages(unsigned int order)
{
    if (order > MAX_ORDER) {
        return 0;
    }
    
    if (early_mode) {
        /* Allocate contiguous pages in early mode */
        size_t count = order_to_pages(order);
        phys_addr_t start = 0;
        size_t found = 0;
        
        for (phys_addr_t addr = memory_start;
             addr < memory_end;
             addr += PAGE_SIZE) {
            if (early_is_free(addr)) {
                if (found == 0) {
                    start = addr;
                }
                found++;
                if (found == count) {
                    /* Mark all as used */
                    for (size_t i = 0; i < count; i++) {
                        early_mark_used(start + i * PAGE_SIZE);
                    }
                    free_pages_count -= count;
                    return start;
                }
            } else {
                found = 0;
            }
        }
        return 0;
    }
    
    /* Buddy allocator */
    for (unsigned int o = order; o <= MAX_ORDER; o++) {
        phys_addr_t addr = buddy_remove_from_list(o);
        if (addr) {
            /* Split larger blocks if needed */
            while (o > order) {
                o--;
                phys_addr_t buddy = buddy_address(addr, o);
                buddy_add_to_list(buddy, o);
            }
            free_pages_count -= order_to_pages(order);
            return addr;
        }
    }
    
    return 0;
}

void pmm_free_page(phys_addr_t addr)
{
    pmm_free_pages(addr, 0);
}

void pmm_free_pages(phys_addr_t addr, unsigned int order)
{
    if (!addr || order > MAX_ORDER) {
        return;
    }
    
    if (early_mode) {
        size_t count = order_to_pages(order);
        for (size_t i = 0; i < count; i++) {
            early_mark_free(addr + i * PAGE_SIZE);
        }
        free_pages_count += count;
        return;
    }
    
    /* Buddy allocator - coalesce with buddy if possible */
    while (order < MAX_ORDER) {
        phys_addr_t buddy = buddy_address(addr, order);
        struct page *buddy_page = pmm_phys_to_page(buddy);
        
        /* Check if buddy is free and same order */
        if (buddy_page && buddy_page->flags == PAGE_FLAG_FREE &&
            buddy_page->order == order) {
            /* Remove buddy from free list */
            /* ... */
            
            /* Merge with buddy */
            if (buddy < addr) {
                addr = buddy;
            }
            order++;
        } else {
            break;
        }
    }
    
    buddy_add_to_list(addr, order);
    free_pages_count += order_to_pages(order);
}

size_t pmm_get_free_memory(void)
{
    return free_pages_count * PAGE_SIZE;
}

size_t pmm_get_total_memory(void)
{
    return total_memory;
}

phys_addr_t pmm_page_to_phys(struct page *page)
{
    if (!page_array || !page) {
        return 0;
    }
    size_t index = page - page_array;
    return memory_start + index * PAGE_SIZE;
}

struct page *pmm_phys_to_page(phys_addr_t addr)
{
    if (!page_array || addr < memory_start || addr >= memory_end) {
        return NULL;
    }
    size_t index = PHYS_TO_PFN(addr - memory_start);
    return &page_array[index];
}
