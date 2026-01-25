/*
 * UnixOS Kernel - Virtual Memory Manager Implementation
 */

#include "mm/vmm.h"
#include "mm/pmm.h"
#include "printk.h"

/* ===================================================================== */
/* Static data */
/* ===================================================================== */

/* Kernel page table (identity mapped initially) */
static uint64_t kernel_pgd[VMM_ENTRIES] __aligned(PAGE_SIZE) = {0};

/* Get kernel page table pointer */
uint64_t *get_kernel_pgd(void)
{
    return kernel_pgd;
}

/* Pre-allocated page tables for early boot */
#define EARLY_TABLES_COUNT  4
static uint64_t early_tables[EARLY_TABLES_COUNT][VMM_ENTRIES] __aligned(PAGE_SIZE);
static size_t early_table_index = 0;

/* ===================================================================== */
/* Helper functions */
/* ===================================================================== */

static inline int pte_index(virt_addr_t vaddr, int level)
{
    int shift;
    switch (level) {
        case 0: shift = VMM_LEVEL0_SHIFT; break;
        case 1: shift = VMM_LEVEL1_SHIFT; break;
        case 2: shift = VMM_LEVEL2_SHIFT; break;
        case 3: shift = VMM_LEVEL3_SHIFT; break;
        default: return -1;
    }
    return (vaddr >> shift) & (VMM_ENTRIES - 1);
}

static inline bool pte_is_valid(uint64_t pte)
{
    return (pte & PTE_VALID) != 0;
}

static inline bool pte_is_table(uint64_t pte)
{
    return (pte & (PTE_VALID | PTE_TABLE)) == (PTE_VALID | PTE_TABLE);
}

static inline phys_addr_t pte_to_phys(uint64_t pte)
{
    return pte & PTE_ADDR_MASK;
}

static inline uint64_t phys_to_pte(phys_addr_t paddr, uint64_t flags)
{
    return (paddr & PTE_ADDR_MASK) | flags;
}

static uint64_t vm_flags_to_pte(uint32_t flags)
{
    uint64_t pte_flags = PTE_VALID | PTE_TABLE | PTE_ACCESSED;
    
    if (flags & VM_DEVICE) {
        pte_flags |= PTE_ATTR_DEVICE | PTE_SH_NONE;
    } else {
        pte_flags |= PTE_ATTR_NORMAL | PTE_SH_INNER;
    }
    
    if (flags & VM_USER) {
        pte_flags |= PTE_USER;
    }
    
    if (!(flags & VM_WRITE)) {
        pte_flags |= PTE_RDONLY;
    }
    
    if (!(flags & VM_EXEC)) {
        if (flags & VM_USER) {
            pte_flags |= PTE_UXN;
        } else {
            pte_flags |= PTE_PXN;
        }
    }
    
    return pte_flags;
}

static uint64_t *alloc_page_table(void)
{
    /* Use early tables if available */
    if (early_table_index < EARLY_TABLES_COUNT) {
        uint64_t *table = early_tables[early_table_index++];
        for (int i = 0; i < VMM_ENTRIES; i++) {
            table[i] = 0;
        }
        return table;
    }
    
    /* Allocate from physical memory */
    phys_addr_t paddr = pmm_alloc_page();
    if (!paddr) {
        return NULL;
    }
    
    uint64_t *table = (uint64_t *)paddr;  /* Identity mapped for now */
    for (int i = 0; i < VMM_ENTRIES; i++) {
        table[i] = 0;
    }
    
    return table;
}

/* ===================================================================== */
/* Page table walking */
/* ===================================================================== */

static uint64_t *walk_page_table(uint64_t *pgd, virt_addr_t vaddr, bool allocate)
{
    uint64_t *table = pgd;
    
    for (int level = 0; level < 3; level++) {
        int idx = pte_index(vaddr, level);
        uint64_t pte = table[idx];
        
        if (!pte_is_valid(pte)) {
            if (!allocate) {
                return NULL;
            }
            
            /* Allocate new table */
            uint64_t *new_table = alloc_page_table();
            if (!new_table) {
                return NULL;
            }
            
            /* Install table entry */
            table[idx] = phys_to_pte((phys_addr_t)new_table, PTE_VALID | PTE_TABLE);
            table = new_table;
        } else if (pte_is_table(pte)) {
            table = (uint64_t *)pte_to_phys(pte);
        } else {
            /* Block mapping - can't continue */
            return NULL;
        }
    }
    
    return table;
}

/* ===================================================================== */
/* Public functions */
/* ===================================================================== */

int vmm_init(void)
{
    printk(KERN_INFO "VMM: Initializing virtual memory manager\n");
    
    /* Kernel PGD is a static array, already initialized */
    printk("VMM: Kernel PGD ready\n");
    
    /* Set up memory attributes - architecture specific */
#ifdef ARCH_ARM64
    /* Set up MAIR (Memory Attribute Indirection Register) */
    uint64_t mair = 
        (0xFFUL << 0) |     /* Index 0: Normal, Write-back */
        (0x00UL << 8) |     /* Index 1: Device nGnRnE */
        (0x44UL << 16);     /* Index 2: Normal, Non-cacheable */
    
    asm volatile("msr mair_el1, %0" : : "r" (mair));
    
    /* Set up TCR (Translation Control Register) for 4KB granule, 48-bit VA */
    uint64_t tcr = 
        (16UL << 0) |       /* T0SZ: 48-bit VA for TTBR0 */
        (16UL << 16) |      /* T1SZ: 48-bit VA for TTBR1 */
        (0UL << 14) |       /* TG0: 4KB granule for TTBR0 */
        (2UL << 30) |       /* TG1: 4KB granule for TTBR1 */
        (1UL << 8) |        /* IRGN0: Inner Write-back */
        (1UL << 10) |       /* ORGN0: Outer Write-back */
        (3UL << 12) |       /* SH0: Inner Shareable */
        (1UL << 24) |       /* IRGN1: Inner Write-back */
        (1UL << 26) |       /* ORGN1: Outer Write-back */
        (3UL << 28) |       /* SH1: Inner Shareable */
        (5UL << 32);        /* IPS: 48-bit Output Address */
    
    asm volatile("msr tcr_el1, %0" : : "r" (tcr));
#elif defined(ARCH_X86_64)
    /* x86_64: Set up PAT (Page Attribute Table) if needed */
    /* For now, use default PAT settings from bootloader */
#elif defined(ARCH_X86)
    /* x86 32-bit: Use default memory attributes */
#endif
    
    printk("VMM: Memory attributes configured\n");
    
    /* Create identity mapping for first 1GB (covers kernel and devices) */
    /* Using 1GB block mappings at level 1 for efficiency */
    
    /* Map 0x00000000-0x3FFFFFFF (first 1GB - RAM) as normal memory */
    int idx0 = pte_index(0x00000000UL, 0);
    uint64_t *l1_table = alloc_page_table();
    if (!l1_table) {
        printk(KERN_ERR "VMM: Failed to allocate L1 table\n");
        return -1;
    }
    kernel_pgd[idx0] = phys_to_pte((phys_addr_t)l1_table, PTE_VALID | PTE_TABLE);
    
    /* Map 0x00000000-0x3FFFFFFF (first 1GB - MMIO) as DEVICE memory */
    l1_table[0] = (0x00000000UL & PTE_ADDR_MASK) | 
                  PTE_VALID | PTE_BLOCK | PTE_ATTR_DEVICE | PTE_SH_NONE | PTE_ACCESSED;
    
    /* Map 0x40000000-0x7FFFFFFF as normal memory (kernel load area) */
    l1_table[1] = (0x40000000UL & PTE_ADDR_MASK) | 
                  PTE_VALID | PTE_BLOCK | PTE_ATTR_NORMAL | PTE_SH_INNER | PTE_ACCESSED;
    
    /* Map High PCI ECAM region (0x40_0000_0000) for 1GB (covers 0x40_1000_0000) */
    /* L1 index 256 (256GB) maps 0x40_0000_0000 - 0x40_3FFF_FFFF */
    /* Map as DEVICE memory (nGnRnE) */
    l1_table[256] = (0x4000000000ULL & PTE_ADDR_MASK) | 
                    PTE_VALID | PTE_BLOCK | PTE_ATTR_DEVICE | PTE_SH_NONE | PTE_ACCESSED;
    
    printk("VMM: RAM identity mapped (0-2GB) + High PCI ECAM (256GB base)\n");
    
    /* Map device region 0x08000000-0x10000000 for GIC, UART etc */
    /* This is at L1 index 0, but we need L2 tables for finer control */
    /* For simplicity, use block mappings - device memory is in first 1GB */
    
    /* Load page tables - architecture specific */
#ifdef ARCH_ARM64
    /* Load TTBR0 (identity mapping for kernel boot) */
    asm volatile("msr ttbr0_el1, %0" : : "r" ((uint64_t)kernel_pgd));
    
    /* Load TTBR1 (will be used for high-half kernel later) */
    asm volatile("msr ttbr1_el1, %0" : : "r" ((uint64_t)kernel_pgd));
#elif defined(ARCH_X86_64)
    /* Load CR3 with page table address */
    asm volatile("mov %0, %%cr3" :: "r"((uint64_t)kernel_pgd) : "memory");
#elif defined(ARCH_X86)
    /* Load CR3 with page directory address */
    asm volatile("mov %0, %%cr3" :: "r"((uint32_t)kernel_pgd) : "memory");
#endif
    
    /* Ensure all writes complete before enabling MMU */
#ifdef ARCH_ARM64
    asm volatile("dsb sy");
    asm volatile("isb");
#elif defined(ARCH_X86_64) || defined(ARCH_X86)
    asm volatile("" ::: "memory");  /* Compiler barrier */
#endif
    
    printk("VMM: TTBRs configured, about to enable MMU...\n");
    
    /* Enable MMU - architecture specific */
#ifdef ARCH_ARM64
    uint64_t sctlr;
    asm volatile("mrs %0, sctlr_el1" : "=r" (sctlr));
    sctlr |= (1 << 0);   /* M: Enable MMU */
    sctlr |= (1 << 2);   /* C: Enable data cache */
    sctlr |= (1 << 12);  /* I: Enable instruction cache */
    asm volatile("msr sctlr_el1, %0" : : "r" (sctlr));
#elif defined(ARCH_X86_64) || defined(ARCH_X86)
    /* x86: MMU already enabled by bootloader, just reload CR3 */
    /* CR3 was already loaded above */
#endif
    
#ifdef ARCH_ARM64
    asm volatile("isb");
#elif defined(ARCH_X86_64) || defined(ARCH_X86)
    /* No ISB equivalent needed on x86 */
#endif
    
    printk(KERN_INFO "VMM: MMU enabled! Page tables active.\n");
    
    return 0;
}

int vmm_map_page(virt_addr_t vaddr, phys_addr_t paddr, uint32_t flags)
{
    /* Walk to level 3 table, allocating as needed */
    uint64_t *pte_table = walk_page_table(kernel_pgd, vaddr, true);
    if (!pte_table) {
        return -1;
    }
    
    /* Get level 3 index */
    int idx = pte_index(vaddr, 3);
    
    /* Check if already mapped */
    if (pte_is_valid(pte_table[idx])) {
        return -1;  /* Already mapped */
    }
    
    /* Install the page mapping */
    uint64_t pte_flags = vm_flags_to_pte(flags);
    pte_table[idx] = phys_to_pte(paddr, pte_flags);
    
    /* Flush TLB for this page */
    vmm_flush_tlb_page(vaddr);
    
    return 0;
}

int vmm_unmap_page(virt_addr_t vaddr)
{
    uint64_t *pte_table = walk_page_table(kernel_pgd, vaddr, false);
    if (!pte_table) {
        return -1;
    }
    
    int idx = pte_index(vaddr, 3);
    
    if (!pte_is_valid(pte_table[idx])) {
        return -1;  /* Not mapped */
    }
    
    /* Clear the entry */
    pte_table[idx] = 0;
    
    /* Flush TLB */
    vmm_flush_tlb_page(vaddr);
    
    return 0;
}

int vmm_map_range(virt_addr_t vaddr, phys_addr_t paddr, size_t size, uint32_t flags)
{
    vaddr = PAGE_ALIGN_DOWN(vaddr);
    paddr = PAGE_ALIGN_DOWN(paddr);
    size = PAGE_ALIGN(size);
    
    for (size_t offset = 0; offset < size; offset += PAGE_SIZE) {
        int ret = vmm_map_page(vaddr + offset, paddr + offset, flags);
        if (ret < 0) {
            /* Rollback on failure */
            vmm_unmap_range(vaddr, offset);
            return ret;
        }
    }
    
    return 0;
}

int vmm_unmap_range(virt_addr_t vaddr, size_t size)
{
    vaddr = PAGE_ALIGN_DOWN(vaddr);
    size = PAGE_ALIGN(size);
    
    for (size_t offset = 0; offset < size; offset += PAGE_SIZE) {
        vmm_unmap_page(vaddr + offset);
    }
    
    return 0;
}

phys_addr_t vmm_virt_to_phys(virt_addr_t vaddr)
{
    uint64_t *pte_table = walk_page_table(kernel_pgd, vaddr, false);
    if (!pte_table) {
        return 0;
    }
    
    int idx = pte_index(vaddr, 3);
    uint64_t pte = pte_table[idx];
    
    if (!pte_is_valid(pte)) {
        return 0;
    }
    
    return pte_to_phys(pte) | (vaddr & (PAGE_SIZE - 1));
}

struct mm_struct *vmm_create_address_space(void)
{
    /* Allocate mm_struct */
    /* TODO: Use kmalloc when available */
    static struct mm_struct mm_pool[64];
    static int mm_index = 0;
    
    if (mm_index >= 64) {
        return NULL;
    }
    
    struct mm_struct *mm = &mm_pool[mm_index++];
    
    /* Allocate page table */
    mm->pgd = alloc_page_table();
    if (!mm->pgd) {
        return NULL;
    }
    
    mm->vma_list = NULL;
    mm->total_vm = 0;
    mm->users.counter = 1;
    
    /* Copy kernel mappings (upper half) */
    for (int i = VMM_ENTRIES / 2; i < VMM_ENTRIES; i++) {
        mm->pgd[i] = kernel_pgd[i];
    }
    
    return mm;
}

void vmm_destroy_address_space(struct mm_struct *mm)
{
    if (!mm) {
        return;
    }
    
    /* Free all VMAs */
    struct vm_area *vma = mm->vma_list;
    while (vma) {
        struct vm_area *next = vma->next;
        /* Note: Should use kfree but avoiding for now */
        vma = next;
    }
    
    /* TODO: Free all user page tables recursively */
    /* For now, just clear the lower half */
    if (mm->pgd) {
        for (int i = 0; i < VMM_ENTRIES / 2; i++) {
            mm->pgd[i] = 0;
        }
    }
    
    mm->pgd = NULL;
    mm->vma_list = NULL;
}

/* ===================================================================== */
/* User Address Space Management */
/* ===================================================================== */

/* Map a page in user address space */
int vmm_map_user_page(struct mm_struct *mm, virt_addr_t vaddr, phys_addr_t paddr, uint32_t flags)
{
    if (!mm || !mm->pgd) return -1;
    
    /* Ensure this is a user address */
    if (vaddr >= USER_VMA_END) return -1;
    
    /* Save and switch page tables temporarily if needed */
    uint64_t *saved_pgd = get_kernel_pgd();
    
    /* Use mm's page tables */
    /* For simplicity, we manipulate the tables directly */
    
    /* Build page table indices */
    int l0_idx = (vaddr >> VMM_LEVEL0_SHIFT) & (VMM_ENTRIES - 1);
    int l1_idx = (vaddr >> VMM_LEVEL1_SHIFT) & (VMM_ENTRIES - 1);
    int l2_idx = (vaddr >> VMM_LEVEL2_SHIFT) & (VMM_ENTRIES - 1);
    int l3_idx = (vaddr >> VMM_LEVEL3_SHIFT) & (VMM_ENTRIES - 1);
    
    uint64_t *l0 = mm->pgd;
    
    /* Walk/create L1 */
    if (!(l0[l0_idx] & PTE_VALID)) {
        uint64_t *l1 = alloc_page_table();
        if (!l1) return -1;
        l0[l0_idx] = ((uint64_t)l1 & PTE_ADDR_MASK) | PTE_VALID | PTE_TABLE;
    }
    uint64_t *l1 = (uint64_t *)(l0[l0_idx] & PTE_ADDR_MASK);
    
    /* Walk/create L2 */
    if (!(l1[l1_idx] & PTE_VALID)) {
        uint64_t *l2 = alloc_page_table();
        if (!l2) return -1;
        l1[l1_idx] = ((uint64_t)l2 & PTE_ADDR_MASK) | PTE_VALID | PTE_TABLE;
    }
    uint64_t *l2 = (uint64_t *)(l1[l1_idx] & PTE_ADDR_MASK);
    
    /* Walk/create L3 */
    if (!(l2[l2_idx] & PTE_VALID)) {
        uint64_t *l3 = alloc_page_table();
        if (!l3) return -1;
        l2[l2_idx] = ((uint64_t)l3 & PTE_ADDR_MASK) | PTE_VALID | PTE_TABLE;
    }
    uint64_t *l3 = (uint64_t *)(l2[l2_idx] & PTE_ADDR_MASK);
    
    /* Convert VM flags to page table flags */
    uint64_t pte_flags = PTE_VALID | PTE_PAGE | PTE_USER | PTE_ATTR_NORMAL | 
                         PTE_SH_INNER | PTE_ACCESSED;
    
    if (!(flags & VM_WRITE)) pte_flags |= PTE_RDONLY;
    if (!(flags & VM_EXEC)) pte_flags |= PTE_UXN;
    pte_flags |= PTE_PXN;  /* Always disable privileged execute */
    
    /* Set the page */
    l3[l3_idx] = (paddr & PTE_ADDR_MASK) | pte_flags;
    
    (void)saved_pgd;  /* Not used currently */
    return 0;
}

/* Add a VM area to the address space */
int vmm_add_vma(struct mm_struct *mm, virt_addr_t start, virt_addr_t end, uint32_t flags)
{
    if (!mm) return -1;
    
    /* Allocate VMA from static pool (should use kmalloc) */
    static struct vm_area vma_pool[256];
    static int vma_index = 0;
    
    if (vma_index >= 256) return -1;
    
    struct vm_area *vma = &vma_pool[vma_index++];
    vma->start = start;
    vma->end = end;
    vma->flags = flags;
    vma->next = mm->vma_list;
    mm->vma_list = vma;
    
    mm->total_vm += (end - start);
    
    return 0;
}

/* Find a VM area containing an address */
struct vm_area *vmm_find_vma(struct mm_struct *mm, virt_addr_t addr)
{
    if (!mm) return NULL;
    
    struct vm_area *vma = mm->vma_list;
    while (vma) {
        if (addr >= vma->start && addr < vma->end) {
            return vma;
        }
        vma = vma->next;
    }
    return NULL;
}

/* Map user address range with physical pages */
int vmm_map_user_range(struct mm_struct *mm, virt_addr_t vaddr, size_t size, uint32_t flags)
{
    if (!mm) return -1;
    
    virt_addr_t end = (vaddr + size + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
    vaddr = vaddr & ~(PAGE_SIZE - 1);
    
    /* Add VMA */
    vmm_add_vma(mm, vaddr, end, flags);
    
    /* Allocate and map pages */
    for (virt_addr_t addr = vaddr; addr < end; addr += PAGE_SIZE) {
        phys_addr_t paddr = pmm_alloc_page();
        if (!paddr) {
            printk(KERN_ERR "vmm_map_user_range: out of memory\n");
            return -1;
        }
        
        int ret = vmm_map_user_page(mm, addr, paddr, flags);
        if (ret != 0) {
            pmm_free_page(paddr);
            return ret;
        }
    }
    
    return 0;
}

void vmm_switch_address_space(struct mm_struct *mm)
{
    if (!mm || !mm->pgd) {
        return;
    }
    
    /* Load TTBR0 (user page tables) */
#ifdef ARCH_ARM64
    asm volatile("msr ttbr0_el1, %0" : : "r" ((uint64_t)mm->pgd));
    asm volatile("isb");
#elif defined(ARCH_X86_64)
    asm volatile("mov %0, %%cr3" :: "r"((uint64_t)mm->pgd) : "memory");
#elif defined(ARCH_X86)
    asm volatile("mov %0, %%cr3" :: "r"((uint32_t)mm->pgd) : "memory");
#endif
    vmm_flush_tlb();
}

void vmm_flush_tlb(void)
{
#ifdef ARCH_ARM64
    asm volatile(
        "dsb ishst\n"
        "tlbi vmalle1is\n"
        "dsb ish\n"
        "isb"
    );
#elif defined(ARCH_X86_64)
    /* Reload CR3 to flush TLB */
    uint64_t cr3;
    asm volatile("mov %%cr3, %0" : "=r"(cr3));
    asm volatile("mov %0, %%cr3" :: "r"(cr3) : "memory");
#elif defined(ARCH_X86)
    /* Reload CR3 to flush TLB */
    uint32_t cr3;
    asm volatile("mov %%cr3, %0" : "=r"(cr3));
    asm volatile("mov %0, %%cr3" :: "r"(cr3) : "memory");
#endif
}

void vmm_flush_tlb_page(virt_addr_t vaddr)
{
#ifdef ARCH_ARM64
    asm volatile(
        "dsb ishst\n"
        "tlbi vale1is, %0\n"
        "dsb ish\n"
        "isb"
        : : "r" (vaddr >> 12)
    );
#elif defined(ARCH_X86_64) || defined(ARCH_X86)
    /* Invalidate single page */
    asm volatile("invlpg (%0)" :: "r"(vaddr) : "memory");
#endif
}
