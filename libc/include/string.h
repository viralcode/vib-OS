/*
 * UnixOS - Minimal C Library
 * String Header
 */

#ifndef _LIBC_STRING_H
#define _LIBC_STRING_H

#include "stddef.h"

/* ===================================================================== */
/* String manipulation */
/* ===================================================================== */

char *strcpy(char *dest, const char *src);
char *strncpy(char *dest, const char *src, size_t n);
char *strcat(char *dest, const char *src);
char *strncat(char *dest, const char *src, size_t n);
char *strdup(const char *s);
char *strndup(const char *s, size_t n);

/* ===================================================================== */
/* String examination */
/* ===================================================================== */

size_t strlen(const char *s);
size_t strnlen(const char *s, size_t maxlen);
int strcmp(const char *s1, const char *s2);
int strncmp(const char *s1, const char *s2, size_t n);
int strcasecmp(const char *s1, const char *s2);
int strncasecmp(const char *s1, const char *s2, size_t n);
char *strchr(const char *s, int c);
char *strrchr(const char *s, int c);
char *strstr(const char *haystack, const char *needle);
char *strpbrk(const char *s, const char *accept);
size_t strspn(const char *s, const char *accept);
size_t strcspn(const char *s, const char *reject);
char *strtok(char *str, const char *delim);
char *strtok_r(char *str, const char *delim, char **saveptr);

/* ===================================================================== */
/* Memory manipulation */
/* ===================================================================== */

void *memcpy(void *dest, const void *src, size_t n);
void *memmove(void *dest, const void *src, size_t n);
void *memset(void *s, int c, size_t n);
int memcmp(const void *s1, const void *s2, size_t n);
void *memchr(const void *s, int c, size_t n);
void bzero(void *s, size_t n);

/* ===================================================================== */
/* Error strings */
/* ===================================================================== */

char *strerror(int errnum);
int strerror_r(int errnum, char *buf, size_t buflen);

#endif /* _LIBC_STRING_H */
