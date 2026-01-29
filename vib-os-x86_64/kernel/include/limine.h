/*
 * Limine Boot Protocol Header
 * Based on the official Limine protocol specification
 * https://github.com/limine-bootloader/limine/blob/trunk/PROTOCOL.md
 */

#ifndef _LIMINE_H
#define _LIMINE_H

#include "types.h"

/* Limine magic numbers */
#define LIMINE_COMMON_MAGIC {0xc7b1dd30df4c8b88, 0x0a82e883a194f07b}

/* Base revision - we use revision 2 */
#define LIMINE_BASE_REVISION(N)                                                \
  uint64_t limine_base_revision[3] = {0xf9562b2d5c95a6c8, 0x6a7b384944536bdc,  \
                                      (N)};

/* Request markers */
#define LIMINE_REQUESTS_START_MARKER                                           \
  uint64_t limine_requests_start_marker[4] = {                                 \
      0xf6b8f4b39de7d1ae, 0xfab91a6940fcb9cf, 0x785c6ed015d3e316,              \
      0x181e920a7852b9d9};

#define LIMINE_REQUESTS_END_MARKER                                             \
  uint64_t limine_requests_end_marker[2] = {0xadc0e0531bb10d03,                \
                                            0x9572709f31764c62};

/* Request IDs */
#define LIMINE_FRAMEBUFFER_REQUEST                                             \
  {LIMINE_COMMON_MAGIC, 0x9d5827dcd881dd75, 0xa3148604f6fab11b}
#define LIMINE_MEMMAP_REQUEST                                                  \
  {LIMINE_COMMON_MAGIC, 0x67cf3d9d378a806f, 0xe304acdfc50c3c62}
#define LIMINE_HHDM_REQUEST                                                    \
  {LIMINE_COMMON_MAGIC, 0x48dcf1cb8ad2b852, 0x63984e959a98244b}
#define LIMINE_KERNEL_ADDRESS_REQUEST                                          \
  {LIMINE_COMMON_MAGIC, 0x71ba76863cc55f63, 0xb2644a48c516a487}
#define LIMINE_RSDP_REQUEST                                                    \
  {LIMINE_COMMON_MAGIC, 0xc5e77b6b397e7b43, 0x27637845accdcf3c}

/* Memory map entry types */
#define LIMINE_MEMMAP_USABLE 0
#define LIMINE_MEMMAP_RESERVED 1
#define LIMINE_MEMMAP_ACPI_RECLAIMABLE 2
#define LIMINE_MEMMAP_ACPI_NVS 3
#define LIMINE_MEMMAP_BAD_MEMORY 4
#define LIMINE_MEMMAP_BOOTLOADER_RECLAIMABLE 5
#define LIMINE_MEMMAP_KERNEL_AND_MODULES 6
#define LIMINE_MEMMAP_FRAMEBUFFER 7

/* Framebuffer memory model */
#define LIMINE_FRAMEBUFFER_RGB 1

/* ========== Structures ========== */

/* Framebuffer */
struct limine_framebuffer {
  void *address;
  uint64_t width;
  uint64_t height;
  uint64_t pitch;
  uint16_t bpp;
  uint8_t memory_model;
  uint8_t red_mask_size;
  uint8_t red_mask_shift;
  uint8_t green_mask_size;
  uint8_t green_mask_shift;
  uint8_t blue_mask_size;
  uint8_t blue_mask_shift;
  uint8_t unused[7];
  uint64_t edid_size;
  void *edid;
  /* Video modes (revision 1+) */
  uint64_t mode_count;
  struct limine_video_mode **modes;
};

struct limine_video_mode {
  uint64_t pitch;
  uint64_t width;
  uint64_t height;
  uint16_t bpp;
  uint8_t memory_model;
  uint8_t red_mask_size;
  uint8_t red_mask_shift;
  uint8_t green_mask_size;
  uint8_t green_mask_shift;
  uint8_t blue_mask_size;
  uint8_t blue_mask_shift;
};

struct limine_framebuffer_response {
  uint64_t revision;
  uint64_t framebuffer_count;
  struct limine_framebuffer **framebuffers;
};

struct limine_framebuffer_request {
  uint64_t id[4];
  uint64_t revision;
  struct limine_framebuffer_response *response;
};

/* Memory Map */
struct limine_memmap_entry {
  uint64_t base;
  uint64_t length;
  uint64_t type;
};

struct limine_memmap_response {
  uint64_t revision;
  uint64_t entry_count;
  struct limine_memmap_entry **entries;
};

struct limine_memmap_request {
  uint64_t id[4];
  uint64_t revision;
  struct limine_memmap_response *response;
};

/* Higher Half Direct Map */
struct limine_hhdm_response {
  uint64_t revision;
  uint64_t offset;
};

struct limine_hhdm_request {
  uint64_t id[4];
  uint64_t revision;
  struct limine_hhdm_response *response;
};

/* Kernel Address */
struct limine_kernel_address_response {
  uint64_t revision;
  uint64_t physical_base;
  uint64_t virtual_base;
};

struct limine_kernel_address_request {
  uint64_t id[4];
  uint64_t revision;
  struct limine_kernel_address_response *response;
};

/* RSDP (ACPI root pointer) */
struct limine_rsdp_response {
  uint64_t revision;
  void *address;
};

struct limine_rsdp_request {
  uint64_t id[4];
  uint64_t revision;
  struct limine_rsdp_response *response;
};

#endif /* _LIMINE_H */
