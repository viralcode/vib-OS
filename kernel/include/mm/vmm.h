/*
 * UnixOS Kernel - Virtual Memory Manager Header
 */

#ifndef _MM_VMM_H
#define _MM_VMM_H

#include "types.h"

/* ===================================================================== */
/* ARM64 Page Table Definitions */
/* ===================================================================== */

/* Page table levels (4KB granule, 48-bit VA) */
#define VMM_LEVELS          4
#define VMM_LEVEL0_SHIFT    39
#define VMM_LEVEL1_SHIFT    30
#define VMM_LEVEL2_SHIFT    21
#define VMM_LEVEL3_SHIFT    12

/* Entries per table */
#define VMM_ENTRIES         512

/* Page table entry flags */
#define PTE_VALID           (1UL << 0)
#define PTE_TABLE           (1UL << 1)   /* Not block/page */
#define PTE_BLOCK           (0UL << 1)   /* Block descriptor */
#define PTE_PAGE            (1UL << 1)   /* Page descriptor (L3 only) */

/* Access permissions */
#define PTE_USER            (1UL << 6)   /* User accessible */
#define PTE_RDONLY          (1UL << 7)   /* Read-only */
#define PTE_ACCESSED        (1UL << 10)  /* Access flag */
#define PTE_NOT_GLOBAL      (1UL << 11)  /* Not global */

/* Memory attributes */
#define PTE_ATTR_IDX(n)     ((n) << 2)   /* MAIR index */
#define PTE_ATTR_NORMAL     PTE_ATTR_IDX(0)
#define PTE_ATTR_DEVICE     PTE_ATTR_IDX(1)
#define PTE_ATTR_NC         PTE_ATTR_IDX(2)  /* Non-cacheable */

/* Shareability */
#define PTE_SH_NONE         (0UL << 8)
#define PTE_SH_OUTER        (2UL << 8)
#define PTE_SH_INNER        (3UL << 8)

/* Execute never */
#define PTE_UXN             (1UL << 54)  /* User execute never */
#define PTE_PXN             (1UL << 53)  /* Privileged execute never */

/* Common flag combinations */
#define PTE_KERNEL_RO       (PTE_VALID | PTE_TABLE | PTE_ATTR_NORMAL | PTE_SH_INNER | PTE_ACCESSED | PTE_RDONLY | PTE_UXN)
#define PTE_KERNEL_RW       (PTE_VALID | PTE_TABLE | PTE_ATTR_NORMAL | PTE_SH_INNER | PTE_ACCESSED | PTE_UXN)
#define PTE_KERNEL_RX       (PTE_VALID | PTE_TABLE | PTE_ATTR_NORMAL | PTE_SH_INNER | PTE_ACCESSED | PTE_RDONLY | PTE_PXN)
#define PTE_KERNEL_RWX      (PTE_VALID | PTE_TABLE | PTE_ATTR_NORMAL | PTE_SH_INNER | PTE_ACCESSED)

#define PTE_USER_RO         (PTE_VALID | PTE_TABLE | PTE_USER | PTE_ATTR_NORMAL | PTE_SH_INNER | PTE_ACCESSED | PTE_RDONLY | PTE_PXN)
#define PTE_USER_RW         (PTE_VALID | PTE_TABLE | PTE_USER | PTE_ATTR_NORMAL | PTE_SH_INNER | PTE_ACCESSED | PTE_PXN)
#define PTE_USER_RX         (PTE_VALID | PTE_TABLE | PTE_USER | PTE_ATTR_NORMAL | PTE_SH_INNER | PTE_ACCESSED | PTE_RDONLY)

#define PTE_DEVICE          (PTE_VALID | PTE_TABLE | PTE_ATTR_DEVICE | PTE_SH_NONE | PTE_ACCESSED | PTE_UXN | PTE_PXN)

/* Address mask */
#define PTE_ADDR_MASK       0x0000FFFFFFFFF000UL

/* ===================================================================== */
/* Virtual memory protection flags */
/* ===================================================================== */

#define VM_NONE             0
#define VM_READ             (1 << 0)
#define VM_WRITE            (1 << 1)
#define VM_EXEC             (1 << 2)
#define VM_USER             (1 << 3)
#define VM_SHARED           (1 << 4)
#define VM_DEVICE           (1 << 5)

/* ===================================================================== */
/* Memory layout */
/* ===================================================================== */

/* Kernel virtual address base (high half) */
#define KERNEL_VMA_BASE     0xFFFF000000000000UL

/* Direct physical mapping offset */
#define PHYS_OFFSET         0xFFFF800000000000UL

/* Kernel heap area */
#define KERNEL_HEAP_START   0xFFFF000040000000UL
#define KERNEL_HEAP_SIZE    (1UL * 1024 * 1024 * 1024)  /* 1GB */

/* User virtual address space */
#define USER_VMA_START      0x0000000000000000UL
#define USER_VMA_END        0x0000800000000000UL

/* ===================================================================== */
/* Address space structure */
/* ===================================================================== */

struct vm_area {
    virt_addr_t start;
    virt_addr_t end;
    uint32_t flags;
    struct vm_area *next;
};

struct mm_struct {
    uint64_t *pgd;              /* Page table root */
    struct vm_area *vma_list;   /* VM areas */
    size_t total_vm;            /* Total mapped size */
    atomic_t users;             /* Reference count */
    
    /* Code segment */
    uint64_t start_code;        /* Start of text segment */
    uint64_t end_code;          /* End of text segment */
    
    /* Data segment */
    uint64_t start_data;        /* Start of data segment */
    uint64_t end_data;          /* End of data segment */
    
    /* Heap (brk) */
    uint64_t start_brk;         /* Start of heap */
    uint64_t brk;               /* Current program break */
    
    /* Stack */
    uint64_t start_stack;       /* Start of user stack */
    
    /* Arguments and environment */
    uint64_t arg_start;         /* Start of arguments */
    uint64_t arg_end;           /* End of arguments */
    uint64_t env_start;         /* Start of environment */
    uint64_t env_end;           /* End of environment */
};

/* ===================================================================== */
/* Function declarations */
/* ===================================================================== */

/**
 * vmm_init - Initialize virtual memory manager
 * 
 * Sets up kernel page tables and enables paging.
 * 
 * Return: 0 on success, negative on error
 */
int vmm_init(void);

/**
 * vmm_map_page - Map a single page
 * @vaddr: Virtual address
 * @paddr: Physical address
 * @flags: Protection flags (VM_*)
 * 
 * Return: 0 on success, negative on error
 */
int vmm_map_page(virt_addr_t vaddr, phys_addr_t paddr, uint32_t flags);

/**
 * vmm_unmap_page - Unmap a single page
 * @vaddr: Virtual address to unmap
 * 
 * Return: 0 on success, negative on error
 */
int vmm_unmap_page(virt_addr_t vaddr);

/**
 * vmm_map_range - Map a range of pages
 * @vaddr: Starting virtual address
 * @paddr: Starting physical address
 * @size: Size in bytes (will be page-aligned)
 * @flags: Protection flags
 * 
 * Return: 0 on success, negative on error
 */
int vmm_map_range(virt_addr_t vaddr, phys_addr_t paddr, size_t size, uint32_t flags);

/**
 * vmm_unmap_range - Unmap a range of pages
 * @vaddr: Starting virtual address
 * @size: Size in bytes
 * 
 * Return: 0 on success, negative on error
 */
int vmm_unmap_range(virt_addr_t vaddr, size_t size);

/**
 * vmm_virt_to_phys - Translate virtual to physical address
 * @vaddr: Virtual address
 * 
 * Return: Physical address, or 0 if not mapped
 */
phys_addr_t vmm_virt_to_phys(virt_addr_t vaddr);

/**
 * vmm_create_address_space - Create new address space for process
 * 
 * Return: Pointer to mm_struct, or NULL on failure
 */
struct mm_struct *vmm_create_address_space(void);

/**
 * vmm_destroy_address_space - Free an address space
 * @mm: Address space to destroy
 */
void vmm_destroy_address_space(struct mm_struct *mm);

/**
 * vmm_switch_address_space - Switch to a different address space
 * @mm: Address space to switch to
 */
void vmm_switch_address_space(struct mm_struct *mm);

/**
 * vmm_flush_tlb - Flush TLB entries
 */
void vmm_flush_tlb(void);

/**
 * vmm_flush_tlb_page - Flush TLB for specific page
 * @vaddr: Virtual address of page
 */
void vmm_flush_tlb_page(virt_addr_t vaddr);

/* ===================================================================== */
/* Inline helpers */
/* ===================================================================== */

/* Convert physical to kernel virtual (direct mapping) */
static inline void *phys_to_virt(phys_addr_t paddr)
{
    return (void *)(PHYS_OFFSET + paddr);
}

/* Convert kernel virtual to physical (direct mapping) */
static inline phys_addr_t virt_to_phys(void *vaddr)
{
    return (phys_addr_t)vaddr - PHYS_OFFSET;
}

#endif /* _MM_VMM_H */
