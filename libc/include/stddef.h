/*
 * UnixOS - Minimal C Library
 * Standard Definitions
 */

#ifndef _LIBC_STDDEF_H
#define _LIBC_STDDEF_H

/* Size types */
typedef unsigned long size_t;
typedef long ssize_t;
typedef long ptrdiff_t;

/* Wide character type */
typedef int wchar_t;

/* NULL pointer */
#ifndef NULL
#ifdef __cplusplus
#define NULL 0
#else
#define NULL ((void *)0)
#endif
#endif

/* Offset of member in struct */
#define offsetof(type, member) __builtin_offsetof(type, member)

/* Max alignment type */
typedef struct {
    long long __ll;
    long double __ld;
} max_align_t;

#endif /* _LIBC_STDDEF_H */
