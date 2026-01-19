/*
 * Vib-OS ELF64 Loader
 * Ported from VibeOS
 */

#include "loader/elf.h"
#include "printk.h"

/* Simple memcpy/memset for loader (avoid dependency on libc) */
static void *elf_memcpy(void *dest, const void *src, size_t n) {
  uint8_t *d = dest;
  const uint8_t *s = src;
  while (n--)
    *d++ = *s++;
  return dest;
}

static void *elf_memset(void *s, int c, size_t n) {
  uint8_t *p = s;
  while (n--)
    *p++ = (uint8_t)c;
  return s;
}

int elf_validate(const void *data, size_t size) {
  if (size < sizeof(Elf64_Ehdr)) {
    return -1;
  }

  const Elf64_Ehdr *ehdr = (const Elf64_Ehdr *)data;

  /* Check magic */
  if (ehdr->e_ident[EI_MAG0] != 0x7F || ehdr->e_ident[EI_MAG1] != 'E' ||
      ehdr->e_ident[EI_MAG2] != 'L' || ehdr->e_ident[EI_MAG3] != 'F') {
    return -2;
  }

  /* Check class (64-bit) */
  if (ehdr->e_ident[EI_CLASS] != ELFCLASS64) {
    return -3;
  }

  /* Check endianness (little endian) */
  if (ehdr->e_ident[EI_DATA] != ELFDATA2LSB) {
    return -4;
  }

  /* Check machine type (AArch64) */
  if (ehdr->e_machine != EM_AARCH64) {
    return -5;
  }

  /* Check type (executable or PIE) */
  if (ehdr->e_type != ET_EXEC && ehdr->e_type != ET_DYN) {
    return -6;
  }

  return 0;
}

uint64_t elf_entry(const void *data) {
  const Elf64_Ehdr *ehdr = (const Elf64_Ehdr *)data;
  return ehdr->e_entry;
}

/* Calculate total memory size needed for all LOAD segments */
uint64_t elf_calc_size(const void *data, size_t size) {
  int valid = elf_validate(data, size);
  if (valid != 0)
    return 0;

  const Elf64_Ehdr *ehdr = (const Elf64_Ehdr *)data;
  const uint8_t *base = (const uint8_t *)data;

  uint64_t min_addr = (uint64_t)-1;
  uint64_t max_addr = 0;

  for (int i = 0; i < ehdr->e_phnum; i++) {
    const Elf64_Phdr *phdr =
        (const Elf64_Phdr *)(base + ehdr->e_phoff + i * ehdr->e_phentsize);
    if (phdr->p_type != PT_LOAD)
      continue;

    if (phdr->p_vaddr < min_addr) {
      min_addr = phdr->p_vaddr;
    }
    uint64_t end = phdr->p_vaddr + phdr->p_memsz;
    if (end > max_addr) {
      max_addr = end;
    }
  }

  if (max_addr <= min_addr)
    return 0;
  return max_addr - min_addr;
}

/* Process dynamic relocations for PIE binaries */
static void elf_process_relocations(uint64_t load_base,
                                    const Elf64_Dyn *dynamic) {
  uint64_t rela_addr = 0;
  uint64_t rela_size = 0;
  uint64_t rela_ent = sizeof(Elf64_Rela);

  /* Parse dynamic section to find RELA info */
  for (const Elf64_Dyn *dyn = dynamic; dyn->d_tag != DT_NULL; dyn++) {
    switch (dyn->d_tag) {
    case DT_RELA:
      rela_addr = dyn->d_val;
      break;
    case DT_RELASZ:
      rela_size = dyn->d_val;
      break;
    case DT_RELAENT:
      rela_ent = dyn->d_val;
      break;
    }
  }

  if (rela_addr == 0 || rela_size == 0) {
    return;
  }

  const Elf64_Rela *rela = (const Elf64_Rela *)(load_base + rela_addr);
  int num_relas = rela_size / rela_ent;

  for (int i = 0; i < num_relas; i++) {
    uint64_t offset = rela[i].r_offset;
    uint64_t type = rela[i].r_info & 0xFFFFFFFF;
    int64_t addend = rela[i].r_addend;

    if (type == R_AARCH64_RELATIVE) {
      uint64_t *target = (uint64_t *)(load_base + offset);
      *target = load_base + addend;
    } else {
      printk(KERN_WARNING "[ELF] Unknown relocation type 0x%llx\n",
             (unsigned long long)type);
    }
  }
}

/* Load ELF at a specific base address */
int elf_load_at(const void *data, size_t size, uint64_t load_base,
                elf_load_info_t *info) {
  int valid = elf_validate(data, size);
  if (valid != 0) {
    printk(KERN_ERR "[ELF] Invalid ELF: error %d\n", valid);
    return -1;
  }

  const Elf64_Ehdr *ehdr = (const Elf64_Ehdr *)data;
  const uint8_t *base = (const uint8_t *)data;
  int is_pie = (ehdr->e_type == ET_DYN);

  printk(KERN_INFO "[ELF] Loading %s at 0x%llx (%d program headers)\n",
         is_pie ? "PIE" : "EXEC", (unsigned long long)load_base, ehdr->e_phnum);

  uint64_t total_size = 0;
  const Elf64_Dyn *dynamic = NULL;

  /* Process program headers */
  for (int i = 0; i < ehdr->e_phnum; i++) {
    /* Validate program header offset is within file */
    if (ehdr->e_phoff + (i + 1) * ehdr->e_phentsize > size) {
      printk(KERN_ERR "[ELF] Program header %d beyond file bounds\n", i);
      return -1;
    }

    const Elf64_Phdr *phdr =
        (const Elf64_Phdr *)(base + ehdr->e_phoff + i * ehdr->e_phentsize);

    /* Remember DYNAMIC segment for relocations */
    if (phdr->p_type == PT_DYNAMIC) {
      dynamic = (const Elf64_Dyn *)(load_base + phdr->p_vaddr);
      continue;
    }

    if (phdr->p_type != PT_LOAD)
      continue;

    /* Validate segment file data is within bounds */
    if (phdr->p_offset > size || phdr->p_filesz > size - phdr->p_offset) {
      printk(KERN_ERR "[ELF] Segment %d: offset/size exceeds file bounds\n", i);
      return -1;
    }

    /* For PIE, add load_base to vaddr */
    uint64_t dest_addr = is_pie ? (load_base + phdr->p_vaddr) : phdr->p_vaddr;

    void *dest = (void *)dest_addr;
    const void *src = base + phdr->p_offset;

    /* Copy file contents */
    if (phdr->p_filesz > 0) {
      elf_memcpy(dest, src, phdr->p_filesz);
    }

    /* Zero BSS */
    if (phdr->p_memsz > phdr->p_filesz) {
      uint64_t bss_size = phdr->p_memsz - phdr->p_filesz;
      elf_memset((uint8_t *)dest + phdr->p_filesz, 0, bss_size);
    }

    uint64_t seg_end = phdr->p_vaddr + phdr->p_memsz;
    if (seg_end > total_size)
      total_size = seg_end;
  }

  /* Process relocations for PIE binaries */
  if (is_pie && dynamic) {
    elf_process_relocations(load_base, dynamic);
  }

  /* Calculate entry point */
  uint64_t entry = is_pie ? (load_base + ehdr->e_entry) : ehdr->e_entry;

  printk(KERN_INFO "[ELF] Entry point: 0x%llx\n", (unsigned long long)entry);

  /* Debug: dump first instructions at entry */
  uint32_t *code = (uint32_t *)entry;
  printk(KERN_INFO "[ELF] Code at entry: %08x %08x %08x %08x\n", code[0],
         code[1], code[2], code[3]);

  /* Fill info struct */
  if (info) {
    info->entry = entry;
    info->load_base = load_base;
    info->load_size = total_size;
  }

  return 0;
}
