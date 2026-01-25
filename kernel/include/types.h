/*
 * UnixOS Kernel - Basic Type Definitions
 */

#ifndef _KERNEL_TYPES_H
#define _KERNEL_TYPES_H

/* ===================================================================== */
/* Fixed-width integer types */
/* ===================================================================== */

typedef signed char         int8_t;
typedef short               int16_t;
typedef int                 int32_t;
typedef long long           int64_t;

typedef unsigned char       uint8_t;
typedef unsigned short      uint16_t;
typedef unsigned int        uint32_t;
typedef unsigned long long  uint64_t;

/* ===================================================================== */
/* Size types */
/* ===================================================================== */

typedef unsigned long       size_t;
typedef long                ssize_t;
typedef long                ptrdiff_t;

/* Pointer-sized integer */
typedef unsigned long       uintptr_t;
typedef long                intptr_t;

/* ===================================================================== */
/* Boolean type */
/* ===================================================================== */

typedef _Bool               bool;
#define true                1
#define false               0

/* ===================================================================== */
/* NULL pointer */
/* ===================================================================== */

#define NULL                ((void *)0)

/* ===================================================================== */
/* Process IDs */
/* ===================================================================== */

typedef int32_t             pid_t;
typedef uint32_t            uid_t;
typedef uint32_t            gid_t;

/* ===================================================================== */
/* File system types */
/* ===================================================================== */

typedef int32_t             off_t;
typedef int64_t             loff_t;
typedef uint32_t            mode_t;
typedef uint32_t            dev_t;
typedef uint64_t            ino_t;
typedef uint32_t            nlink_t;
typedef int64_t             blkcnt_t;
typedef int64_t             blksize_t;

/* ===================================================================== */
/* Time types */
/* ===================================================================== */

typedef int64_t             time_t;
typedef int64_t             suseconds_t;

struct timespec {
    time_t tv_sec;
    long   tv_nsec;
};

struct timeval {
    time_t      tv_sec;
    suseconds_t tv_usec;
};

/* ===================================================================== */
/* Memory types */
/* ===================================================================== */

typedef uint64_t            phys_addr_t;    /* Physical address */
typedef uint64_t            virt_addr_t;    /* Virtual address */
typedef uint64_t            dma_addr_t;     /* DMA address */

/* Page Frame Number */
typedef unsigned long       pfn_t;

/* ===================================================================== */
/* Atomic types */
/* ===================================================================== */

typedef struct {
    volatile int counter;
} atomic_t;

typedef struct {
    volatile long counter;
} atomic64_t;

/* Atomic operations */
static inline void atomic_set(atomic_t *v, int i)
{
    v->counter = i;
}

static inline int atomic_read(atomic_t *v)
{
    return v->counter;
}

static inline void atomic_inc(atomic_t *v)
{
    __sync_add_and_fetch(&v->counter, 1);
}

static inline void atomic_dec(atomic_t *v)
{
    __sync_sub_and_fetch(&v->counter, 1);
}

static inline int atomic_dec_and_test(atomic_t *v)
{
    return __sync_sub_and_fetch(&v->counter, 1) == 0;
}

/* ===================================================================== */
/* Limits */
/* ===================================================================== */

#define INT8_MIN            (-128)
#define INT16_MIN           (-32768)
#define INT32_MIN           (-2147483647 - 1)
#define INT64_MIN           (-9223372036854775807LL - 1)

#define INT8_MAX            127
#define INT16_MAX           32767
#define INT32_MAX           2147483647
#define INT64_MAX           9223372036854775807LL

#define UINT8_MAX           255
#define UINT16_MAX          65535
#define UINT32_MAX          4294967295U
#define UINT64_MAX          18446744073709551615ULL

#define SIZE_MAX            UINT64_MAX
#define SSIZE_MAX           INT64_MAX

/* ===================================================================== */
/* Helper macros */
/* ===================================================================== */

#define ARRAY_SIZE(arr)     (sizeof(arr) / sizeof((arr)[0]))

#define ALIGN(x, a)         (((x) + (a) - 1) & ~((a) - 1))
#define ALIGN_DOWN(x, a)    ((x) & ~((a) - 1))
#define IS_ALIGNED(x, a)    (((x) & ((a) - 1)) == 0)

#define MIN(a, b)           ((a) < (b) ? (a) : (b))
#define MAX(a, b)           ((a) > (b) ? (a) : (b))
#define CLAMP(val, lo, hi)  MIN(MAX(val, lo), hi)

#define BIT(n)              (1UL << (n))
#define BITS_PER_LONG       64

#define offsetof(type, member) ((size_t)&((type *)0)->member)
#define container_of(ptr, type, member) \
    ((type *)((char *)(ptr) - offsetof(type, member)))

/* Compiler hints */
#define likely(x)           __builtin_expect(!!(x), 1)
#define unlikely(x)         __builtin_expect(!!(x), 0)

#define __packed            __attribute__((packed))
#define __aligned(x)        __attribute__((aligned(x)))
#define __unused            __attribute__((unused))
#define __noreturn          __attribute__((noreturn))
#define __weak              __attribute__((weak))

/* Memory barriers */
#define barrier()           asm volatile("" ::: "memory")
#define mb()                asm volatile("dmb sy" ::: "memory")
#define rmb()               asm volatile("dmb ld" ::: "memory")
#define wmb()               asm volatile("dmb st" ::: "memory")

#endif /* _KERNEL_TYPES_H */
