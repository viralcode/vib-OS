/*
 * Vib-OS - Kernel String/Memory Functions
 */

#include "types.h"

void *memcpy(void *dest, const void *src, size_t n) {
  uint8_t *d = (uint8_t *)dest;
  const uint8_t *s = (const uint8_t *)src;

  while (n--) {
    *d++ = *s++;
  }

  return dest;
}

void *memset(void *s, int c, size_t n) {
  uint8_t *p = (uint8_t *)s;

  while (n--) {
    *p++ = (uint8_t)c;
  }

  return s;
}

void *memmove(void *dest, const void *src, size_t n) {
  uint8_t *d = (uint8_t *)dest;
  const uint8_t *s = (const uint8_t *)src;

  if (d < s) {
    while (n--) {
      *d++ = *s++;
    }
  } else {
    d += n;
    s += n;
    while (n--) {
      *--d = *--s;
    }
  }

  return dest;
}

int memcmp(const void *s1, const void *s2, size_t n) {
  const uint8_t *p1 = (const uint8_t *)s1;
  const uint8_t *p2 = (const uint8_t *)s2;

  while (n--) {
    if (*p1 != *p2) {
      return *p1 - *p2;
    }
    p1++;
    p2++;
  }

  return 0;
}

size_t strlen(const char *s) {
  size_t len = 0;
  while (s[len])
    len++;
  return len;
}

char *strncpy(char *dest, const char *src, size_t n) {
  size_t i;
  for (i = 0; i < n && src[i]; i++) {
    dest[i] = src[i];
  }
  for (; i < n; i++) {
    dest[i] = '\0';
  }
  return dest;
}

/* DEPRECATED: Use strncpy instead. This wrapper limits copies to prevent
 * overflow. */
char *strcpy(char *dest, const char *src) {
  /* Safety limit - prevent unbounded copies */
  size_t src_len = strlen(src);
  if (src_len > 4095)
    src_len = 4095;
  strncpy(dest, src, src_len + 1);
  return dest;
}

int strcmp(const char *s1, const char *s2) {
  while (*s1 && *s1 == *s2) {
    s1++;
    s2++;
  }
  return *(unsigned char *)s1 - *(unsigned char *)s2;
}

int strncmp(const char *s1, const char *s2, size_t n) {
  if (n == 0)
    return 0;

  while (n-- && *s1 && *s1 == *s2) {
    s1++;
    s2++;
  }

  if (n == (size_t)-1)
    return 0;
  return *(unsigned char *)s1 - *(unsigned char *)s2;
}
