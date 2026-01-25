/*
 * UnixOS - Minimal C Library Implementation
 * Standard Library Functions
 */

#include "../include/stdlib.h"
#include "../include/string.h"
#include "../include/unistd.h"

/* ===================================================================== */
/* Memory allocation (simple bump allocator for now) */
/* ===================================================================== */

/* Simple heap using static buffer for now */
#define HEAP_SIZE (16 * 1024 * 1024)  /* 16MB */

static char heap[HEAP_SIZE];
static size_t heap_used = 0;

/* Simple block header */
struct malloc_header {
    size_t size;
    int used;
};

#define ALIGN(x) (((x) + 15) & ~15)

void *malloc(size_t size)
{
    if (size == 0) return NULL;
    
    size_t total = ALIGN(sizeof(struct malloc_header) + size);
    
    if (heap_used + total > HEAP_SIZE) {
        return NULL;
    }
    
    struct malloc_header *hdr = (struct malloc_header *)(heap + heap_used);
    hdr->size = size;
    hdr->used = 1;
    
    heap_used += total;
    
    return (void *)(hdr + 1);
}

void *calloc(size_t nmemb, size_t size)
{
    size_t total = nmemb * size;
    void *ptr = malloc(total);
    if (ptr) {
        memset(ptr, 0, total);
    }
    return ptr;
}

void *realloc(void *ptr, size_t size)
{
    if (!ptr) return malloc(size);
    if (size == 0) {
        free(ptr);
        return NULL;
    }
    
    struct malloc_header *hdr = (struct malloc_header *)ptr - 1;
    
    if (size <= hdr->size) {
        return ptr;
    }
    
    void *new_ptr = malloc(size);
    if (new_ptr) {
        memcpy(new_ptr, ptr, hdr->size);
        free(ptr);
    }
    
    return new_ptr;
}

void free(void *ptr)
{
    if (!ptr) return;
    
    struct malloc_header *hdr = (struct malloc_header *)ptr - 1;
    hdr->used = 0;
    
    /* Simple allocator doesn't actually free memory */
}

/* ===================================================================== */
/* Process control */
/* ===================================================================== */

static void (*atexit_funcs[32])(void);
static int atexit_count = 0;

int atexit(void (*function)(void))
{
    if (atexit_count >= 32) return -1;
    atexit_funcs[atexit_count++] = function;
    return 0;
}

void exit(int status)
{
    /* Call atexit handlers in reverse order */
    while (atexit_count > 0) {
        atexit_funcs[--atexit_count]();
    }
    
    _exit(status);
}

void _Exit(int status)
{
    _exit(status);
}

void abort(void)
{
    _exit(134);  /* SIGABRT */
}

/* ===================================================================== */
/* Environment (stub) */
/* ===================================================================== */

static char *environ_empty[] = { NULL };
char **environ = environ_empty;

char *getenv(const char *name)
{
    (void)name;
    return NULL;
}

int setenv(const char *name, const char *value, int overwrite)
{
    (void)name;
    (void)value;
    (void)overwrite;
    return -1;
}

int unsetenv(const char *name)
{
    (void)name;
    return -1;
}

int putenv(char *string)
{
    (void)string;
    return -1;
}

/* ===================================================================== */
/* String conversion */
/* ===================================================================== */

int atoi(const char *nptr)
{
    return (int)atol(nptr);
}

long atol(const char *nptr)
{
    long result = 0;
    int sign = 1;
    
    /* Skip whitespace */
    while (*nptr == ' ' || *nptr == '\t' || *nptr == '\n') {
        nptr++;
    }
    
    /* Handle sign */
    if (*nptr == '-') {
        sign = -1;
        nptr++;
    } else if (*nptr == '+') {
        nptr++;
    }
    
    /* Convert digits */
    while (*nptr >= '0' && *nptr <= '9') {
        result = result * 10 + (*nptr - '0');
        nptr++;
    }
    
    return result * sign;
}

long long atoll(const char *nptr)
{
    return (long long)atol(nptr);
}

double atof(const char *nptr)
{
    /* Simplified implementation */
    return (double)atol(nptr);
}

long strtol(const char *nptr, char **endptr, int base)
{
    long result = 0;
    int sign = 1;
    const char *p = nptr;
    
    /* Skip whitespace */
    while (*p == ' ' || *p == '\t') p++;
    
    /* Handle sign */
    if (*p == '-') {
        sign = -1;
        p++;
    } else if (*p == '+') {
        p++;
    }
    
    /* Handle base prefix */
    if (base == 0) {
        if (*p == '0') {
            if (p[1] == 'x' || p[1] == 'X') {
                base = 16;
                p += 2;
            } else {
                base = 8;
                p++;
            }
        } else {
            base = 10;
        }
    } else if (base == 16 && *p == '0' && (p[1] == 'x' || p[1] == 'X')) {
        p += 2;
    }
    
    /* Convert digits */
    while (*p) {
        int digit;
        if (*p >= '0' && *p <= '9') {
            digit = *p - '0';
        } else if (*p >= 'a' && *p <= 'z') {
            digit = *p - 'a' + 10;
        } else if (*p >= 'A' && *p <= 'Z') {
            digit = *p - 'A' + 10;
        } else {
            break;
        }
        
        if (digit >= base) break;
        
        result = result * base + digit;
        p++;
    }
    
    if (endptr) *endptr = (char *)p;
    
    return result * sign;
}

unsigned long strtoul(const char *nptr, char **endptr, int base)
{
    return (unsigned long)strtol(nptr, endptr, base);
}

/* ===================================================================== */
/* Random numbers */
/* ===================================================================== */

static unsigned int rand_seed = 1;

int rand(void)
{
    rand_seed = rand_seed * 1103515245 + 12345;
    return (rand_seed >> 16) & RAND_MAX;
}

void srand(unsigned int seed)
{
    rand_seed = seed;
}

/* ===================================================================== */
/* Integer arithmetic */
/* ===================================================================== */

int abs(int j)
{
    return j < 0 ? -j : j;
}

long labs(long j)
{
    return j < 0 ? -j : j;
}

long long llabs(long long j)
{
    return j < 0 ? -j : j;
}

div_t div(int numer, int denom)
{
    div_t result;
    result.quot = numer / denom;
    result.rem = numer % denom;
    return result;
}

ldiv_t ldiv(long numer, long denom)
{
    ldiv_t result;
    result.quot = numer / denom;
    result.rem = numer % denom;
    return result;
}
