/*
 * Minimal MMIO mapping helpers
 */

#ifndef _MMIO_H
#define _MMIO_H

#include "types.h"

/* Map a physical MMIO range into virtual space.
 * Returns virtual base address or 0 on failure.
 */
uint64_t mmio_map_range(uint64_t phys_addr, size_t size);

#endif /* _MMIO_H */
