/*
 * VibCode x64 - Write-Combining Support Header
 */

#ifndef _WC_H
#define _WC_H

#include "types.h"

/* Set framebuffer region to Write-Combining for fast access */
int wc_set_framebuffer(uint64_t phys_addr, uint64_t size);

/* Debug: dump current MTRR configuration */
void wc_dump_mtrrs(void);

#endif /* _WC_H */
