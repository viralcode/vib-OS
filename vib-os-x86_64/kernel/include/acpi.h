/*
 * Minimal ACPI table parsing (RSDP -> RSDT/XSDT -> MADT)
 */

#ifndef _ACPI_H
#define _ACPI_H

#include "types.h"

typedef struct {
  char signature[8];
  uint8_t checksum;
  char oem_id[6];
  uint8_t revision;
  uint32_t rsdt_address;
  uint32_t length;
  uint64_t xsdt_address;
  uint8_t ext_checksum;
  uint8_t reserved[3];
} __attribute__((packed)) acpi_rsdp_t;

typedef struct {
  char signature[4];
  uint32_t length;
  uint8_t revision;
  uint8_t checksum;
  char oem_id[6];
  char oem_table_id[8];
  uint32_t oem_revision;
  uint32_t creator_id;
  uint32_t creator_revision;
} __attribute__((packed)) acpi_sdt_header_t;

typedef struct {
  acpi_sdt_header_t header;
  uint32_t lapic_addr;
  uint32_t flags;
  /* Followed by MADT entries */
} __attribute__((packed)) acpi_madt_t;

void acpi_init(void *rsdp_ptr);
const acpi_madt_t *acpi_get_madt(void);

#endif /* _ACPI_H */
