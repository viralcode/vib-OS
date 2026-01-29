/*
 * Minimal ACPI table parsing (RSDP -> RSDT/XSDT -> MADT)
 */

#include "../include/acpi.h"

static const acpi_madt_t *madt = NULL;

static int checksum_ok(const uint8_t *ptr, uint32_t len) {
  uint8_t sum = 0;
  for (uint32_t i = 0; i < len; i++) {
    sum = (uint8_t)(sum + ptr[i]);
  }
  return sum == 0;
}

static const acpi_sdt_header_t *find_sdt(const acpi_rsdp_t *rsdp, const char *sig) {
  if (!rsdp) return NULL;

  if (rsdp->revision >= 2 && rsdp->xsdt_address) {
    const acpi_sdt_header_t *xsdt = (const acpi_sdt_header_t *)(uintptr_t)rsdp->xsdt_address;
    if (!checksum_ok((const uint8_t *)xsdt, xsdt->length)) return NULL;
    uint32_t count = (xsdt->length - sizeof(acpi_sdt_header_t)) / 8;
    const uint64_t *entries = (const uint64_t *)((const uint8_t *)xsdt + sizeof(acpi_sdt_header_t));
    for (uint32_t i = 0; i < count; i++) {
      const acpi_sdt_header_t *hdr = (const acpi_sdt_header_t *)(uintptr_t)entries[i];
      if (!hdr) continue;
      if (hdr->signature[0] == sig[0] && hdr->signature[1] == sig[1] &&
          hdr->signature[2] == sig[2] && hdr->signature[3] == sig[3]) {
        if (checksum_ok((const uint8_t *)hdr, hdr->length)) {
          return hdr;
        }
      }
    }
  } else if (rsdp->rsdt_address) {
    const acpi_sdt_header_t *rsdt = (const acpi_sdt_header_t *)(uintptr_t)rsdp->rsdt_address;
    if (!checksum_ok((const uint8_t *)rsdt, rsdt->length)) return NULL;
    uint32_t count = (rsdt->length - sizeof(acpi_sdt_header_t)) / 4;
    const uint32_t *entries = (const uint32_t *)((const uint8_t *)rsdt + sizeof(acpi_sdt_header_t));
    for (uint32_t i = 0; i < count; i++) {
      const acpi_sdt_header_t *hdr = (const acpi_sdt_header_t *)(uintptr_t)entries[i];
      if (!hdr) continue;
      if (hdr->signature[0] == sig[0] && hdr->signature[1] == sig[1] &&
          hdr->signature[2] == sig[2] && hdr->signature[3] == sig[3]) {
        if (checksum_ok((const uint8_t *)hdr, hdr->length)) {
          return hdr;
        }
      }
    }
  }
  return NULL;
}

void acpi_init(void *rsdp_ptr) {
  madt = NULL;
  if (!rsdp_ptr) return;

  /* For now, skip full ACPI parsing since RSDT/XSDT addresses are physical
   * and we don't have HHDM offset available here. This avoids page faults.
   * USB HID keyboard doesn't require ACPI MADT. */
  (void)rsdp_ptr;
  return;
}

const acpi_madt_t *acpi_get_madt(void) {
  return madt;
}
