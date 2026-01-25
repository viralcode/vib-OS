/*
 * Vib-OS ELF64 Loader Definitions
 * Ported from VibeOS
 */

#ifndef ELF_H
#define ELF_H

#include "types.h"

/* ELF Magic */
#define ELF_MAGIC 0x464C457F  /* "\x7FELF" as little-endian uint32 */

/* ELF64 Header */
typedef struct {
    uint8_t  e_ident[16];    /* ELF identification */
    uint16_t e_type;         /* Object file type */
    uint16_t e_machine;      /* Machine type */
    uint32_t e_version;      /* Object file version */
    uint64_t e_entry;        /* Entry point address */
    uint64_t e_phoff;        /* Program header offset */
    uint64_t e_shoff;        /* Section header offset */
    uint32_t e_flags;        /* Processor-specific flags */
    uint16_t e_ehsize;       /* ELF header size */
    uint16_t e_phentsize;    /* Program header entry size */
    uint16_t e_phnum;        /* Number of program headers */
    uint16_t e_shentsize;    /* Section header entry size */
    uint16_t e_shnum;        /* Number of section headers */
    uint16_t e_shstrndx;     /* Section name string table index */
} Elf64_Ehdr;

/* Program Header */
typedef struct {
    uint32_t p_type;         /* Segment type */
    uint32_t p_flags;        /* Segment flags */
    uint64_t p_offset;       /* Segment offset in file */
    uint64_t p_vaddr;        /* Virtual address */
    uint64_t p_paddr;        /* Physical address */
    uint64_t p_filesz;       /* Size in file */
    uint64_t p_memsz;        /* Size in memory */
    uint64_t p_align;        /* Alignment */
} Elf64_Phdr;

/* Program header types */
#define PT_NULL    0
#define PT_LOAD    1
#define PT_DYNAMIC 2
#define PT_INTERP  3

/* Dynamic section entry */
typedef struct {
    int64_t  d_tag;
    uint64_t d_val;
} Elf64_Dyn;

/* Dynamic tags */
#define DT_NULL    0
#define DT_RELA    7   /* Address of Rela relocs */
#define DT_RELASZ  8   /* Total size of Rela relocs */
#define DT_RELAENT 9   /* Size of one Rela entry */

/* Relocation entry with addend */
typedef struct {
    uint64_t r_offset;
    uint64_t r_info;
    int64_t  r_addend;
} Elf64_Rela;

/* Relocation types for AArch64 */
#define R_AARCH64_RELATIVE 0x403

/* ELF identification indices */
#define EI_MAG0    0
#define EI_MAG1    1
#define EI_MAG2    2
#define EI_MAG3    3
#define EI_CLASS   4
#define EI_DATA    5

/* ELF class */
#define ELFCLASS64 2

/* ELF data encoding */
#define ELFDATA2LSB 1  /* Little endian */

/* Machine types */
#define EM_AARCH64 183

/* ELF types */
#define ET_EXEC 2
#define ET_DYN  3  /* Shared object / PIE */

/* Info about a loaded program */
typedef struct {
    uint64_t entry;       /* Entry point (absolute address) */
    uint64_t load_base;   /* Where it was loaded */
    uint64_t load_size;   /* Total size in memory */
} elf_load_info_t;

/* Validate ELF header, returns 0 if valid */
int elf_validate(const void *data, size_t size);

/* Get entry point from ELF */
uint64_t elf_entry(const void *data);

/* Load ELF at a specific base address (for PIE binaries) */
int elf_load_at(const void *data, size_t size, uint64_t base, elf_load_info_t *info);

/* Calculate total memory size needed for ELF */
uint64_t elf_calc_size(const void *data, size_t size);

#endif /* ELF_H */
