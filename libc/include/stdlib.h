/*
 * UnixOS - Minimal C Library
 * Standard Library Header
 */

#ifndef _LIBC_STDLIB_H
#define _LIBC_STDLIB_H

#include "stddef.h"

/* Exit status */
#define EXIT_SUCCESS    0
#define EXIT_FAILURE    1

/* Random number limits */
#define RAND_MAX        2147483647

/* ===================================================================== */
/* Memory allocation */
/* ===================================================================== */

void *malloc(size_t size);
void *calloc(size_t nmemb, size_t size);
void *realloc(void *ptr, size_t size);
void free(void *ptr);

/* ===================================================================== */
/* Process control */
/* ===================================================================== */

void exit(int status) __attribute__((noreturn));
void _Exit(int status) __attribute__((noreturn));
void abort(void) __attribute__((noreturn));
int atexit(void (*function)(void));

/* ===================================================================== */
/* Environment */
/* ===================================================================== */

char *getenv(const char *name);
int setenv(const char *name, const char *value, int overwrite);
int unsetenv(const char *name);
int putenv(char *string);

/* ===================================================================== */
/* String conversion */
/* ===================================================================== */

int atoi(const char *nptr);
long atol(const char *nptr);
long long atoll(const char *nptr);
double atof(const char *nptr);

long strtol(const char *nptr, char **endptr, int base);
unsigned long strtoul(const char *nptr, char **endptr, int base);
long long strtoll(const char *nptr, char **endptr, int base);
unsigned long long strtoull(const char *nptr, char **endptr, int base);
double strtod(const char *nptr, char **endptr);

/* ===================================================================== */
/* Random numbers */
/* ===================================================================== */

int rand(void);
void srand(unsigned int seed);

/* ===================================================================== */
/* Sorting and searching */
/* ===================================================================== */

void qsort(void *base, size_t nmemb, size_t size,
           int (*compar)(const void *, const void *));
void *bsearch(const void *key, const void *base, size_t nmemb, size_t size,
              int (*compar)(const void *, const void *));

/* ===================================================================== */
/* Integer arithmetic */
/* ===================================================================== */

int abs(int j);
long labs(long j);
long long llabs(long long j);

typedef struct {
    int quot;
    int rem;
} div_t;

typedef struct {
    long quot;
    long rem;
} ldiv_t;

div_t div(int numer, int denom);
ldiv_t ldiv(long numer, long denom);

#endif /* _LIBC_STDLIB_H */
