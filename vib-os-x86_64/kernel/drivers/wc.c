/*
 * VibCode x64 - Write-Combining Framebuffer Support
 * 
 * Implements proper MTRR configuration for fast framebuffer access.
 * Based on Intel/AMD documentation and Linux kernel implementation.
 */

#include "../include/types.h"

/* MSR addresses */
#define MSR_MTRRCAP           0x000000FE
#define MSR_MTRR_DEF_TYPE     0x000002FF
#define MSR_MTRR_PHYS_BASE(n) (0x00000200 + 2*(n))
#define MSR_MTRR_PHYS_MASK(n) (0x00000201 + 2*(n))

/* Memory types */
#define MTRR_TYPE_UC    0  /* Uncacheable */
#define MTRR_TYPE_WC    1  /* Write-Combining */
#define MTRR_TYPE_WT    4  /* Write-Through */
#define MTRR_TYPE_WP    5  /* Write-Protected */
#define MTRR_TYPE_WB    6  /* Write-Back */

/* MTRR capability bits */
#define MTRR_CAP_VCNT_MASK    0xFF    /* Variable range count */
#define MTRR_CAP_WC           (1<<10) /* Write-combining supported */

/* MTRR default type bits */
#define MTRR_DEF_TYPE_E       (1<<11) /* MTRRs enabled */
#define MTRR_DEF_TYPE_FE      (1<<10) /* Fixed-range MTRRs enabled */

/* MTRR physmask valid bit */
#define MTRR_PHYSMASK_VALID   (1ULL<<11)

/* Debug output via serial */
#define COM1 0x3F8

static inline void outb(uint16_t port, uint8_t val) {
    __asm__ volatile("outb %0, %1" : : "a"(val), "Nd"(port));
}

static inline uint8_t inb(uint16_t port) {
    uint8_t ret;
    __asm__ volatile("inb %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

static void serial_putc(char c) {
    while ((inb(COM1 + 5) & 0x20) == 0);
    outb(COM1, c);
}

static void serial_puts(const char *s) {
    while (*s) {
        if (*s == '\n') serial_putc('\r');
        serial_putc(*s++);
    }
}

static void serial_puthex(uint64_t val) {
    const char *hex = "0123456789ABCDEF";
    serial_puts("0x");
    for (int i = 60; i >= 0; i -= 4) {
        serial_putc(hex[(val >> i) & 0xF]);
    }
}

/* Read MSR */
static inline uint64_t rdmsr(uint32_t msr) {
    uint32_t lo, hi;
    __asm__ volatile("rdmsr" : "=a"(lo), "=d"(hi) : "c"(msr));
    return ((uint64_t)hi << 32) | lo;
}

/* Write MSR */
static inline void wrmsr(uint32_t msr, uint64_t value) {
    uint32_t lo = (uint32_t)value;
    uint32_t hi = (uint32_t)(value >> 32);
    __asm__ volatile("wrmsr" : : "c"(msr), "a"(lo), "d"(hi));
}

/* Read CR0 */
static inline uint64_t read_cr0(void) {
    uint64_t val;
    __asm__ volatile("mov %%cr0, %0" : "=r"(val));
    return val;
}

/* Write CR0 */
static inline void write_cr0(uint64_t val) {
    __asm__ volatile("mov %0, %%cr0" : : "r"(val));
}

/* Read CR4 */
static inline uint64_t read_cr4(void) {
    uint64_t val;
    __asm__ volatile("mov %%cr4, %0" : "=r"(val));
    return val;
}

/* Write CR4 */
static inline void write_cr4(uint64_t val) {
    __asm__ volatile("mov %0, %%cr4" : : "r"(val));
}

/* Check CPUID for MTRR support */
static int mtrr_supported(void) {
    uint32_t eax, ebx, ecx, edx;
    __asm__ volatile("cpuid" : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx) : "a"(1));
    return (edx & (1 << 12)) != 0;  /* MTRR bit */
}

/* Get number of variable MTRRs */
static int mtrr_get_var_count(void) {
    uint64_t cap = rdmsr(MSR_MTRRCAP);
    return cap & MTRR_CAP_VCNT_MASK;
}

/* Check if WC is supported */
static int mtrr_wc_supported(void) {
    uint64_t cap = rdmsr(MSR_MTRRCAP);
    return (cap & MTRR_CAP_WC) != 0;
}

/* Find a free MTRR slot */
static int mtrr_find_free_slot(void) {
    int count = mtrr_get_var_count();
    for (int i = 0; i < count; i++) {
        uint64_t mask = rdmsr(MSR_MTRR_PHYS_MASK(i));
        if (!(mask & MTRR_PHYSMASK_VALID)) {
            return i;  /* Found free slot */
        }
    }
    return -1;  /* No free slots */
}

/* Round up to power of 2 */
static uint64_t round_up_pow2(uint64_t size) {
    if (size == 0) return 0;
    size--;
    size |= size >> 1;
    size |= size >> 2;
    size |= size >> 4;
    size |= size >> 8;
    size |= size >> 16;
    size |= size >> 32;
    return size + 1;
}

/*
 * Set framebuffer region to Write-Combining
 * 
 * This follows the Intel procedure for modifying MTRRs:
 * 1. Disable interrupts
 * 2. Enter no-fill cache mode (set CD flag in CR0)
 * 3. Flush all caches (WBINVD)
 * 4. If using PAT, flush TLBs
 * 5. Disable MTRRs
 * 6. Update the MTRRs
 * 7. Re-enable MTRRs
 * 8. Flush caches and TLBs again
 * 9. Enter normal cache mode
 * 10. Re-enable interrupts
 */
int wc_set_framebuffer(uint64_t phys_addr, uint64_t size) {
    serial_puts("\n[WC] Setting up Write-Combining for framebuffer\n");
    serial_puts("[WC] Address: ");
    serial_puthex(phys_addr);
    serial_puts("\n[WC] Size: ");
    serial_puthex(size);
    serial_puts("\n");
    
    /* Check MTRR support */
    if (!mtrr_supported()) {
        serial_puts("[WC] ERROR: MTRRs not supported\n");
        return -1;
    }
    
    /* Check WC support */
    if (!mtrr_wc_supported()) {
        serial_puts("[WC] ERROR: Write-Combining not supported\n");
        return -1;
    }
    
    serial_puts("[WC] MTRR count: ");
    int var_count = mtrr_get_var_count();
    serial_puthex(var_count);
    serial_puts("\n");
    
    /* Find free slot */
    int slot = mtrr_find_free_slot();
    if (slot < 0) {
        serial_puts("[WC] ERROR: No free MTRR slots\n");
        return -1;
    }
    serial_puts("[WC] Using MTRR slot: ");
    serial_puthex(slot);
    serial_puts("\n");
    
    /* Size must be power of 2 and >= 4KB */
    uint64_t aligned_size = round_up_pow2(size);
    if (aligned_size < 0x1000) aligned_size = 0x1000;
    
    /* Base address must be aligned to size */
    uint64_t aligned_base = phys_addr & ~(aligned_size - 1);
    
    serial_puts("[WC] Aligned base: ");
    serial_puthex(aligned_base);
    serial_puts("\n[WC] Aligned size: ");
    serial_puthex(aligned_size);
    serial_puts("\n");
    
    /* Calculate mask (for 48-bit physical addresses) */
    uint64_t phys_mask = ((1ULL << 48) - 1) & ~(aligned_size - 1);
    
    /* Step 1: Disable interrupts */
    __asm__ volatile("cli");
    
    /* Step 2: Enter no-fill cache mode */
    uint64_t cr0 = read_cr0();
    write_cr0(cr0 | (1ULL << 30));  /* Set CD (Cache Disable) */
    
    /* Step 3: Flush all caches */
    __asm__ volatile("wbinvd" ::: "memory");
    
    /* Step 4: Flush TLBs */
    uint64_t cr4 = read_cr4();
    write_cr4(cr4 & ~(1ULL << 7));  /* Clear PGE */
    uint64_t cr3;
    __asm__ volatile("mov %%cr3, %0" : "=r"(cr3));
    __asm__ volatile("mov %0, %%cr3" : : "r"(cr3));  /* Flush TLB */
    write_cr4(cr4);
    
    /* Step 5-6: Disable MTRRs and update */
    uint64_t def_type = rdmsr(MSR_MTRR_DEF_TYPE);
    wrmsr(MSR_MTRR_DEF_TYPE, def_type & ~MTRR_DEF_TYPE_E);  /* Disable */
    
    /* Set the MTRR */
    wrmsr(MSR_MTRR_PHYS_BASE(slot), aligned_base | MTRR_TYPE_WC);
    wrmsr(MSR_MTRR_PHYS_MASK(slot), phys_mask | MTRR_PHYSMASK_VALID);
    
    /* Step 7: Re-enable MTRRs */
    wrmsr(MSR_MTRR_DEF_TYPE, def_type | MTRR_DEF_TYPE_E);
    
    /* Step 8: Flush again */
    __asm__ volatile("wbinvd" ::: "memory");
    __asm__ volatile("mov %0, %%cr3" : : "r"(cr3));
    
    /* Step 9: Exit no-fill mode */
    write_cr0(cr0);
    
    /* Step 10: Re-enable interrupts */
    __asm__ volatile("sti");
    
    serial_puts("[WC] Write-Combining enabled successfully!\n");
    return 0;
}

/*
 * Debug: Print current MTRR configuration
 */
void wc_dump_mtrrs(void) {
    serial_puts("\n[WC] Current MTRR configuration:\n");
    
    uint64_t def_type = rdmsr(MSR_MTRR_DEF_TYPE);
    serial_puts("[WC] Default type MSR: ");
    serial_puthex(def_type);
    serial_puts("\n");
    
    int count = mtrr_get_var_count();
    for (int i = 0; i < count; i++) {
        uint64_t base = rdmsr(MSR_MTRR_PHYS_BASE(i));
        uint64_t mask = rdmsr(MSR_MTRR_PHYS_MASK(i));
        
        if (mask & MTRR_PHYSMASK_VALID) {
            serial_puts("[WC] MTRR");
            serial_putc('0' + i);
            serial_puts(": base=");
            serial_puthex(base & ~0xFFFULL);
            serial_puts(" type=");
            serial_puthex(base & 0xFF);
            serial_puts(" mask=");
            serial_puthex(mask & ~0xFFFULL);
            serial_puts("\n");
        }
    }
}
