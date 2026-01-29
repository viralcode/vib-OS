/*
 * Minimal MMIO mapping (4KB pages) for device registers
 */

#include "../include/mmio.h"
#include "../include/string.h"

#define PAGE_SIZE 0x1000ULL
#define PAGE_MASK (~(PAGE_SIZE - 1))

#define PTE_PRESENT (1ULL << 0)
#define PTE_WRITE   (1ULL << 1)
#define PTE_PWT     (1ULL << 3)
#define PTE_PCD     (1ULL << 4)
#define PTE_LARGE   (1ULL << 7)

/* Simple page pool for new page tables */
static uint8_t mmio_page_pool[16 * PAGE_SIZE] __attribute__((aligned(4096)));
static size_t mmio_page_pool_used = 0;

static inline uint64_t read_cr3(void) {
  uint64_t cr3;
  __asm__ volatile("mov %%cr3, %0" : "=r"(cr3));
  return cr3;
}

static inline void flush_tlb(void) {
  uint64_t cr3 = read_cr3();
  __asm__ volatile("mov %0, %%cr3" : : "r"(cr3) : "memory");
}

static void *mmio_alloc_page(void) {
  if (mmio_page_pool_used + PAGE_SIZE > sizeof(mmio_page_pool)) {
    return NULL;
  }
  void *page = &mmio_page_pool[mmio_page_pool_used];
  mmio_page_pool_used += PAGE_SIZE;
  memset(page, 0, PAGE_SIZE);
  return page;
}

static uint64_t virt_to_phys(uint64_t virt) {
  if (kernel_phys_base && kernel_virt_base && virt >= kernel_virt_base) {
    return kernel_phys_base + (virt - kernel_virt_base);
  }
  if (virt >= hhdm_offset) {
    return virt - hhdm_offset;
  }
  return 0;
}

static uint64_t *table_from_entry(uint64_t entry) {
  return (uint64_t *)(uintptr_t)(hhdm_offset + (entry & PAGE_MASK));
}

static int map_page(uint64_t virt, uint64_t phys, uint64_t flags) {
  uint64_t cr3 = read_cr3() & PAGE_MASK;
  uint64_t *pml4 = (uint64_t *)(uintptr_t)(hhdm_offset + cr3);

  uint64_t pml4_idx = (virt >> 39) & 0x1FF;
  uint64_t pdpt_idx = (virt >> 30) & 0x1FF;
  uint64_t pd_idx = (virt >> 21) & 0x1FF;
  uint64_t pt_idx = (virt >> 12) & 0x1FF;

  if (!(pml4[pml4_idx] & PTE_PRESENT)) {
    void *new_pdpt = mmio_alloc_page();
    if (!new_pdpt) return -1;
    uint64_t new_pdpt_phys = virt_to_phys((uint64_t)new_pdpt);
    if (!new_pdpt_phys) return -1;
    pml4[pml4_idx] = new_pdpt_phys | PTE_PRESENT | PTE_WRITE;
  }

  uint64_t *pdpt = table_from_entry(pml4[pml4_idx]);
  if (pdpt[pdpt_idx] & PTE_LARGE) {
    return 0; /* Already mapped with large page */
  }
  if (!(pdpt[pdpt_idx] & PTE_PRESENT)) {
    void *new_pd = mmio_alloc_page();
    if (!new_pd) return -1;
    uint64_t new_pd_phys = virt_to_phys((uint64_t)new_pd);
    if (!new_pd_phys) return -1;
    pdpt[pdpt_idx] = new_pd_phys | PTE_PRESENT | PTE_WRITE;
  }

  uint64_t *pd = table_from_entry(pdpt[pdpt_idx]);
  if (pd[pd_idx] & PTE_LARGE) {
    return 0; /* Already mapped with large page */
  }
  if (!(pd[pd_idx] & PTE_PRESENT)) {
    void *new_pt = mmio_alloc_page();
    if (!new_pt) return -1;
    uint64_t new_pt_phys = virt_to_phys((uint64_t)new_pt);
    if (!new_pt_phys) return -1;
    pd[pd_idx] = new_pt_phys | PTE_PRESENT | PTE_WRITE;
  }

  uint64_t *pt = table_from_entry(pd[pd_idx]);
  pt[pt_idx] = (phys & PAGE_MASK) | flags | PTE_PRESENT | PTE_WRITE;
  return 0;
}

uint64_t mmio_map_range(uint64_t phys_addr, size_t size) {
  if (!phys_addr || size == 0) {
    return 0;
  }
  if (!kernel_phys_base || !kernel_virt_base) {
    return 0;
  }

  uint64_t start = phys_addr & PAGE_MASK;
  uint64_t end = (phys_addr + size + PAGE_SIZE - 1) & PAGE_MASK;
  uint64_t virt_base = hhdm_offset + phys_addr;
  uint64_t virt = hhdm_offset + start;

  for (uint64_t phys = start; phys < end; phys += PAGE_SIZE, virt += PAGE_SIZE) {
    if (map_page(virt, phys, PTE_PCD | PTE_PWT) != 0) {
      return 0;
    }
  }

  flush_tlb();
  return virt_base;
}
