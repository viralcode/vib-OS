/*
 * VibCode x64 - Page Attribute Table (PAT) and Write-Combining Support
 * Enables fast framebuffer access on real hardware
 */

#include "../include/types.h"

/* MSR addresses */
#define MSR_PAT 0x277

/* PAT entry types */
#define PAT_UC          0x00  /* Uncacheable */
#define PAT_WC          0x01  /* Write-Combining (FAST for framebuffer!) */
#define PAT_WT          0x04  /* Write-Through */
#define PAT_WP          0x05  /* Write-Protected */
#define PAT_WB          0x06  /* Write-Back (default, slow for FB) */
#define PAT_UC_MINUS    0x07  /* Uncached (weaker than UC) */

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

/* Check if PAT is supported */
static int pat_supported(void) {
  uint32_t eax, ebx, ecx, edx;
  __asm__ volatile("cpuid" : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx) : "a"(1));
  return (edx & (1 << 16)) != 0;  /* PAT bit in CPUID.1:EDX[16] */
}

/*
 * Initialize PAT with optimal settings for graphics
 * 
 * Default PAT layout:
 *   PAT0: WB, PAT1: WT, PAT2: UC-, PAT3: UC
 *   PAT4: WB, PAT5: WT, PAT6: UC-, PAT7: UC
 *
 * Our layout (put WC in PAT1 for easy use):
 *   PAT0: WB  (normal memory)
 *   PAT1: WC  (framebuffer - FAST!)
 *   PAT2: UC- 
 *   PAT3: UC
 *   PAT4: WB
 *   PAT5: WT
 *   PAT6: UC-
 *   PAT7: UC
 */
void pat_init(void) {
  if (!pat_supported()) {
    return;  /* PAT not supported, use defaults */
  }
  
  /* Build PAT value with WC in entry 1 */
  uint64_t pat_value = 
    ((uint64_t)PAT_WB << 0)  |   /* PAT0: Write-Back */
    ((uint64_t)PAT_WC << 8)  |   /* PAT1: Write-Combining (for framebuffer) */
    ((uint64_t)PAT_UC_MINUS << 16) |  /* PAT2: UC- */
    ((uint64_t)PAT_UC << 24) |   /* PAT3: Uncached */
    ((uint64_t)PAT_WB << 32) |   /* PAT4: Write-Back */
    ((uint64_t)PAT_WT << 40) |   /* PAT5: Write-Through */
    ((uint64_t)PAT_UC_MINUS << 48) |  /* PAT6: UC- */
    ((uint64_t)PAT_UC << 56);    /* PAT7: Uncached */
  
  wrmsr(MSR_PAT, pat_value);
}

/*
 * Set framebuffer memory to Write-Combining
 * This modifies page table entries to use PAT1 (WC)
 * 
 * For PAT1 (WC), we need: PCD=0, PWT=1, PAT=0
 * Page table bits: PAT (bit 7), PCD (bit 4), PWT (bit 3)
 */

/* Page table entry flags for WC memory */
#define PTE_PRESENT  (1ULL << 0)
#define PTE_WRITE    (1ULL << 1)
#define PTE_PWT      (1ULL << 3)   /* Page Write-Through */
#define PTE_PCD      (1ULL << 4)   /* Page Cache Disable */
#define PTE_LARGE    (1ULL << 7)   /* Large page (2MB) */
#define PTE_PAT      (1ULL << 7)   /* PAT bit for 4KB pages */
#define PTE_PAT_LARGE (1ULL << 12) /* PAT bit for large pages */

/* Get CR3 (page table base) */
static inline uint64_t read_cr3(void) {
  uint64_t cr3;
  __asm__ volatile("mov %%cr3, %0" : "=r"(cr3));
  return cr3;
}

/* Flush TLB for an address */
static inline void invlpg(void *addr) {
  __asm__ volatile("invlpg (%0)" : : "r"(addr) : "memory");
}

/* Flush entire TLB */
static inline void flush_tlb(void) {
  uint64_t cr3 = read_cr3();
  __asm__ volatile("mov %0, %%cr3" : : "r"(cr3) : "memory");
}

/*
 * Mark framebuffer region as Write-Combining
 * This walks the page tables and sets appropriate flags
 * 
 * Note: This is a simplified version that assumes identity mapping
 * and 2MB large pages (common for Limine bootloader)
 */
void pat_set_framebuffer_wc(uint64_t fb_phys, size_t size) {
  if (!pat_supported()) {
    return;
  }
  
  /* Get PML4 (level 4 page table) */
  uint64_t cr3 = read_cr3() & ~0xFFFULL;
  uint64_t *pml4 = (uint64_t *)cr3;
  
  /* Calculate address range */
  uint64_t start = fb_phys & ~0x1FFFFFULL;  /* Align to 2MB */
  uint64_t end = (fb_phys + size + 0x1FFFFF) & ~0x1FFFFFULL;
  
  for (uint64_t addr = start; addr < end; addr += 0x200000) {
    /* Get page table indices */
    int pml4_idx = (addr >> 39) & 0x1FF;
    int pdpt_idx = (addr >> 30) & 0x1FF;
    int pd_idx = (addr >> 21) & 0x1FF;
    
    /* Walk page tables */
    if (!(pml4[pml4_idx] & PTE_PRESENT)) continue;
    
    uint64_t *pdpt = (uint64_t *)(pml4[pml4_idx] & ~0xFFFULL);
    if (!(pdpt[pdpt_idx] & PTE_PRESENT)) continue;
    
    /* Check if it's a 1GB page */
    if (pdpt[pdpt_idx] & PTE_LARGE) {
      /* 1GB page - set WC flags: PWT=1, PCD=0, PAT=0 */
      pdpt[pdpt_idx] = (pdpt[pdpt_idx] & ~(PTE_PCD | PTE_PAT_LARGE)) | PTE_PWT;
      invlpg((void *)addr);
      continue;
    }
    
    uint64_t *pd = (uint64_t *)(pdpt[pdpt_idx] & ~0xFFFULL);
    if (!(pd[pd_idx] & PTE_PRESENT)) continue;
    
    /* Check if it's a 2MB page */
    if (pd[pd_idx] & PTE_LARGE) {
      /* 2MB page - set WC flags: PWT=1, PCD=0, PAT=0 */
      pd[pd_idx] = (pd[pd_idx] & ~(PTE_PCD | PTE_PAT_LARGE)) | PTE_PWT;
      invlpg((void *)addr);
    }
    /* For 4KB pages, we'd need to walk PT level too */
  }
  
  /* Flush TLB to apply changes */
  flush_tlb();
}

/*
 * Alternative: Use MTRR if PAT doesn't work
 * MTRRs (Memory Type Range Registers) can also set WC
 */
#define MSR_MTRR_CAP          0xFE
#define MSR_MTRR_DEF_TYPE     0x2FF
#define MSR_MTRR_PHYS_BASE(n) (0x200 + 2*(n))
#define MSR_MTRR_PHYS_MASK(n) (0x201 + 2*(n))

#define MTRR_TYPE_WC          0x01

static int mtrr_get_free_slot(void) {
  uint64_t cap = rdmsr(MSR_MTRR_CAP);
  int num_var = cap & 0xFF;
  
  for (int i = 0; i < num_var; i++) {
    uint64_t mask = rdmsr(MSR_MTRR_PHYS_MASK(i));
    if (!(mask & (1ULL << 11))) {  /* Valid bit not set = free */
      return i;
    }
  }
  return -1;  /* No free slot */
}

void mtrr_set_wc(uint64_t base, size_t size) {
  int slot = mtrr_get_free_slot();
  if (slot < 0) return;
  
  /* Disable interrupts */
  __asm__ volatile("cli");
  
  /* Disable caches */
  uint64_t cr0;
  __asm__ volatile("mov %%cr0, %0" : "=r"(cr0));
  __asm__ volatile("mov %0, %%cr0" : : "r"(cr0 | (1ULL << 30)));  /* CD=1 */
  
  /* Flush caches */
  __asm__ volatile("wbinvd");
  
  /* Calculate mask (must be power of 2 aligned) */
  uint64_t mask = ~(size - 1) & 0xFFFFFFFFF000ULL;
  
  /* Set MTRR */
  wrmsr(MSR_MTRR_PHYS_BASE(slot), (base & 0xFFFFFFFFF000ULL) | MTRR_TYPE_WC);
  wrmsr(MSR_MTRR_PHYS_MASK(slot), mask | (1ULL << 11));  /* Valid=1 */
  
  /* Flush TLB */
  flush_tlb();
  
  /* Re-enable caches */
  __asm__ volatile("mov %0, %%cr0" : : "r"(cr0));
  
  /* Re-enable interrupts */
  __asm__ volatile("sti");
}
