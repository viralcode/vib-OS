/* Minimal string.h stub for freestanding userspace */
#ifndef _STRING_H
#define _STRING_H

#include "vibe.h"

/* memcpy and memset are defined in vibe.h */

/* memcmp - compare memory regions */
static inline int memcmp(const void *s1, const void *s2, unsigned long n) {
    const unsigned char *p1 = s1, *p2 = s2;
    while (n--) {
        if (*p1 != *p2) return *p1 - *p2;
        p1++; p2++;
    }
    return 0;
}

/* memmove - copy memory with overlap handling */
static inline void *memmove(void *dst, const void *src, unsigned long n) {
    unsigned char *d = dst;
    const unsigned char *s = src;
    if (d < s) {
        while (n--) *d++ = *s++;
    } else {
        d += n;
        s += n;
        while (n--) *--d = *--s;
    }
    return dst;
}

#endif
