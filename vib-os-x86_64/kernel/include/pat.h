/*
 * VibCode x64 - PAT (Page Attribute Table) Header
 */

#ifndef _PAT_H
#define _PAT_H

#include "types.h"

/* Initialize PAT with WC support */
void pat_init(void);

/* Set framebuffer region to Write-Combining */
void pat_set_framebuffer_wc(uint64_t fb_phys, size_t size);

/* Alternative: Use MTRR for WC */
void mtrr_set_wc(uint64_t base, size_t size);

#endif /* _PAT_H */
