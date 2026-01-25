/*
 * DOOM for VibeOS - libc declarations
 * Adapted from TCC libc for doomgeneric port
 *
 * Copyright (C) 2024-2025 Kaan Senol
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; version 2.
 */

#ifndef DOOM_LIBC_H
#define DOOM_LIBC_H

/* Include the real kapi definition first - it defines size_t, uint*_t, etc. */
#include "../../lib/vibe.h"

/* Signed integer types (vibe.h only has unsigned) */
typedef signed char int8_t;
typedef signed short int16_t;
typedef signed int int32_t;
typedef signed long int64_t;

/* Additional types not in vibe.h */
typedef long ssize_t;
typedef long ptrdiff_t;
typedef long intptr_t;
typedef unsigned long uintptr_t;
typedef int wchar_t;

#ifndef NULL
#define NULL ((void *)0)
#endif

#ifndef offsetof
#define offsetof(type, member) ((size_t)&((type *)0)->member)
#endif

/* Global kapi pointer set by main */
extern kapi_t *doom_kapi;

/* Initialize libc with kapi pointer */
void doom_libc_init(kapi_t *api);

/* ============ stdio.h ============ */

#define EOF (-1)
#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2
#define BUFSIZ 1024

#define _IOFBF 0
#define _IOLBF 1
#define _IONBF 2

/* Our FILE implementation */
typedef struct {
    void *handle;       /* kapi file handle */
    char *path;         /* file path (for reopening after write) */
    int pos;            /* current position */
    int size;           /* file size */
    int mode;           /* 0=read, 1=write, 2=append */
    int error;          /* error flag */
    int eof;            /* eof flag */
    char *buf;          /* write buffer */
    int buf_size;       /* buffer size */
    int buf_pos;        /* buffer position */
} FILE;

extern FILE *stdin;
extern FILE *stdout;
extern FILE *stderr;

FILE *fopen(const char *path, const char *mode);
FILE *fdopen(int fd, const char *mode);
int fclose(FILE *f);
size_t fread(void *ptr, size_t size, size_t nmemb, FILE *f);
size_t fwrite(const void *ptr, size_t size, size_t nmemb, FILE *f);
int fseek(FILE *f, long offset, int whence);
long ftell(FILE *f);
void rewind(FILE *f);
int feof(FILE *f);
int ferror(FILE *f);
void clearerr(FILE *f);
int fflush(FILE *f);
int fgetc(FILE *f);
int fputc(int c, FILE *f);
char *fgets(char *s, int size, FILE *f);
int fputs(const char *s, FILE *f);
int getc(FILE *f);
int putc(int c, FILE *f);
int ungetc(int c, FILE *f);
int getchar(void);
int putchar(int c);
int puts(const char *s);

int printf(const char *fmt, ...);
int fprintf(FILE *f, const char *fmt, ...);
int sprintf(char *buf, const char *fmt, ...);
int snprintf(char *buf, size_t size, const char *fmt, ...);
int sscanf(const char *str, const char *fmt, ...);
int vprintf(const char *fmt, __builtin_va_list ap);
int vfprintf(FILE *f, const char *fmt, __builtin_va_list ap);
int vsprintf(char *buf, const char *fmt, __builtin_va_list ap);
int vsnprintf(char *buf, size_t size, const char *fmt, __builtin_va_list ap);
int setvbuf(FILE *f, char *buf, int mode, size_t size);
void perror(const char *s);

int remove(const char *path);
int rename(const char *oldpath, const char *newpath);

/* ============ stdlib.h ============ */

void *malloc(size_t size);
void *calloc(size_t nmemb, size_t size);
void *realloc(void *ptr, size_t size);
void free(void *ptr);

void exit(int status);
void abort(void);

int atoi(const char *s);
long atol(const char *s);
long long atoll(const char *s);
double atof(const char *s);

long strtol(const char *s, char **endp, int base);
unsigned long strtoul(const char *s, char **endp, int base);
long long strtoll(const char *s, char **endp, int base);
unsigned long long strtoull(const char *s, char **endp, int base);
double strtod(const char *s, char **endp);
float strtof(const char *s, char **endp);
double strtold(const char *s, char **endp);

char *getenv(const char *name);
int abs(int x);
long labs(long x);

void qsort(void *base, size_t nmemb, size_t size, int (*compar)(const void *, const void *));
void *bsearch(const void *key, const void *base, size_t nmemb, size_t size,
              int (*compar)(const void *, const void *));

int rand(void);
void srand(unsigned int seed);
#define RAND_MAX 32767

int system(const char *command);

/* ============ string.h ============ */

void *memcpy(void *dest, const void *src, size_t n);
void *memmove(void *dest, const void *src, size_t n);
void *memset(void *s, int c, size_t n);
int memcmp(const void *s1, const void *s2, size_t n);
void *memchr(const void *s, int c, size_t n);

size_t strlen(const char *s);
char *strcpy(char *dest, const char *src);
char *strncpy(char *dest, const char *src, size_t n);
char *strcat(char *dest, const char *src);
char *strncat(char *dest, const char *src, size_t n);
int strcmp(const char *s1, const char *s2);
int strncmp(const char *s1, const char *s2, size_t n);
int strcasecmp(const char *s1, const char *s2);
int strncasecmp(const char *s1, const char *s2, size_t n);
char *strchr(const char *s, int c);
char *strrchr(const char *s, int c);
char *strstr(const char *haystack, const char *needle);
char *strdup(const char *s);
char *strndup(const char *s, size_t n);
size_t strspn(const char *s, const char *accept);
size_t strcspn(const char *s, const char *reject);
char *strpbrk(const char *s, const char *accept);
char *strtok(char *str, const char *delim);
char *strerror(int errnum);

/* ============ ctype.h ============ */

int isalpha(int c);
int isdigit(int c);
int isalnum(int c);
int isspace(int c);
int isupper(int c);
int islower(int c);
int isxdigit(int c);
int isprint(int c);
int ispunct(int c);
int iscntrl(int c);
int isgraph(int c);
int toupper(int c);
int tolower(int c);

/* ============ errno.h ============ */

extern int errno;

#define ENOENT  2
#define EIO     5
#define ENOMEM 12
#define EACCES 13
#define EEXIST 17
#define ENOTDIR 20
#define EISDIR 21
#define EINVAL 22
#define ENOSPC 28
#define ENAMETOOLONG 36
#define ENOTEMPTY 39
#define ERANGE 34

/* ============ fcntl.h / unistd.h ============ */

#define O_RDONLY   0x0000
#define O_WRONLY   0x0001
#define O_RDWR     0x0002
#define O_CREAT    0x0100
#define O_TRUNC    0x0200
#define O_APPEND   0x0400
#define O_BINARY   0x0000  /* No-op on VibeOS */

int open(const char *path, int flags, ...);
int close(int fd);
ssize_t read(int fd, void *buf, size_t count);
ssize_t write(int fd, const void *buf, size_t count);
long lseek(int fd, long offset, int whence);
int unlink(const char *path);
int access(const char *path, int mode);
char *getcwd(char *buf, size_t size);
int chdir(const char *path);
int isatty(int fd);
int mkdir(const char *path, int mode);

#define R_OK 4
#define W_OK 2
#define X_OK 1
#define F_OK 0

char *realpath(const char *path, char *resolved_path);

#define STDIN_FILENO  0
#define STDOUT_FILENO 1
#define STDERR_FILENO 2

/* ============ time.h / sys/time.h ============ */

typedef long time_t;
typedef long suseconds_t;
typedef long clock_t;

#define CLOCKS_PER_SEC 1000

struct timeval {
    time_t tv_sec;
    suseconds_t tv_usec;
};

struct timezone {
    int tz_minuteswest;
    int tz_dsttime;
};

struct tm {
    int tm_sec;
    int tm_min;
    int tm_hour;
    int tm_mday;
    int tm_mon;
    int tm_year;
    int tm_wday;
    int tm_yday;
    int tm_isdst;
};

time_t time(time_t *t);
clock_t clock(void);
int gettimeofday(struct timeval *tv, struct timezone *tz);
struct tm *localtime(const time_t *timep);
struct tm *gmtime(const time_t *timep);
size_t strftime(char *s, size_t max, const char *format, const struct tm *tm);

/* ============ setjmp.h ============ */

typedef unsigned long jmp_buf[22];

int setjmp(jmp_buf env);
void longjmp(jmp_buf env, int val);

/* ============ stdarg.h ============ */

typedef __builtin_va_list va_list;
#define va_start(ap, last) __builtin_va_start(ap, last)
#define va_arg(ap, type) __builtin_va_arg(ap, type)
#define va_end(ap) __builtin_va_end(ap)
#define va_copy(dest, src) __builtin_va_copy(dest, src)

/* ============ stdbool.h ============ */
/* Note: DOOM defines its own boolean enum, so we skip bool/true/false macros */
/* Using gnu89 to avoid C99 _Bool keyword */

/* ============ assert.h ============ */

#ifdef NDEBUG
#define assert(expr) ((void)0)
#else
#define assert(expr) ((expr) ? (void)0 : __assert_fail(#expr, __FILE__, __LINE__))
void __assert_fail(const char *expr, const char *file, int line);
#endif

/* ============ limits.h ============ */

#define CHAR_BIT 8
#define SCHAR_MIN (-128)
#define SCHAR_MAX 127
#define UCHAR_MAX 255
#define CHAR_MIN 0
#define CHAR_MAX 255
#define SHRT_MIN (-32768)
#define SHRT_MAX 32767
#define USHRT_MAX 65535
#define INT_MIN (-2147483647-1)
#define INT_MAX 2147483647
#define UINT_MAX 4294967295U
#define LONG_MIN (-9223372036854775807L-1)
#define LONG_MAX 9223372036854775807L
#define ULONG_MAX 18446744073709551615UL
#define LLONG_MIN LONG_MIN
#define LLONG_MAX LONG_MAX
#define ULLONG_MAX ULONG_MAX
#define PATH_MAX 1024
#define NAME_MAX 255

/* ============ signal.h (stubs) ============ */

typedef int sig_atomic_t;
typedef void (*sighandler_t)(int);
#define SIG_DFL ((sighandler_t)0)
#define SIG_IGN ((sighandler_t)1)
#define SIG_ERR ((sighandler_t)-1)
#define SIGINT 2
#define SIGTERM 15

sighandler_t signal(int signum, sighandler_t handler);

/* ============ math.h ============ */

#define HUGE_VAL __builtin_huge_val()
#define INFINITY __builtin_inf()
#define NAN __builtin_nan("")
#define M_PI 3.14159265358979323846

double fabs(double x);
double floor(double x);
double ceil(double x);
double sqrt(double x);
double pow(double x, double y);
double log(double x);
double log10(double x);
double exp(double x);
double sin(double x);
double cos(double x);
double tan(double x);
double asin(double x);
double acos(double x);
double atan(double x);
double atan2(double y, double x);
double fmod(double x, double y);
double ldexp(double x, int exp);
double frexp(double x, int *exp);
double modf(double x, double *iptr);

int isnan(double x);
int isinf(double x);
int isfinite(double x);

/* Long double versions - just use double to avoid libgcc */
double fabsl(double x);
double floorl(double x);
double ceill(double x);
double sqrtl(double x);
double ldexpl(double x, int exp);

/* Float versions */
float fabsf(float x);
float floorf(float x);
float ceilf(float x);
float sqrtf(float x);

/* ============ sys/stat.h ============ */

struct stat {
    int st_mode;
    size_t st_size;
    time_t st_mtime;
};

#define S_IFMT   0170000
#define S_IFDIR  0040000
#define S_IFREG  0100000
#define S_ISDIR(m) (((m) & S_IFMT) == S_IFDIR)
#define S_ISREG(m) (((m) & S_IFMT) == S_IFREG)

int stat(const char *path, struct stat *buf);
int fstat(int fd, struct stat *buf);

#endif /* DOOM_LIBC_H */
