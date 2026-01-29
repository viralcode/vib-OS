/*
 * UEFI Demo OS - String Functions
 */

#ifndef _STRING_H
#define _STRING_H

#include "types.h"

/* Memory functions */
void *memset(void *s, int c, size_t n);
void *memcpy(void *dest, const void *src, size_t n);
void *memmove(void *dest, const void *src, size_t n);
int memcmp(const void *s1, const void *s2, size_t n);

/* String functions */
size_t strlen(const char *s);
int strcmp(const char *s1, const char *s2);
int strncmp(const char *s1, const char *s2, size_t n);
char *strcpy(char *dest, const char *src);
char *strncpy(char *dest, const char *src, size_t n);
char *strcat(char *dest, const char *src);

/* Number to string conversion */
void itoa(int value, char *str, int base);
void utoa(uint64_t value, char *str, int base);

/* Formatted output */
int snprintf(char *str, size_t size, const char *format, ...);

/* String search */
char *strstr(const char *haystack, const char *needle);

#endif /* _STRING_H */
