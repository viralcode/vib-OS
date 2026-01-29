/*
 * UEFI Demo OS - String and Memory Functions
 */

#include "../include/string.h"

/* ========== Memory Functions ========== */

void *memset(void *s, int c, size_t n) {
  uint8_t *p = (uint8_t *)s;
  while (n--) {
    *p++ = (uint8_t)c;
  }
  return s;
}

void *memcpy(void *dest, const void *src, size_t n) {
  uint8_t *d = (uint8_t *)dest;
  const uint8_t *s = (const uint8_t *)src;
  while (n--) {
    *d++ = *s++;
  }
  return dest;
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

/* ========== String Functions ========== */

size_t strlen(const char *s) {
  size_t len = 0;
  while (*s++) {
    len++;
  }
  return len;
}

int strcmp(const char *s1, const char *s2) {
  while (*s1 && (*s1 == *s2)) {
    s1++;
    s2++;
  }
  return *(unsigned char *)s1 - *(unsigned char *)s2;
}

int strncmp(const char *s1, const char *s2, size_t n) {
  while (n && *s1 && (*s1 == *s2)) {
    s1++;
    s2++;
    n--;
  }
  if (n == 0) {
    return 0;
  }
  return *(unsigned char *)s1 - *(unsigned char *)s2;
}

char *strcpy(char *dest, const char *src) {
  char *d = dest;
  while ((*d++ = *src++))
    ;
  return dest;
}

char *strncpy(char *dest, const char *src, size_t n) {
  char *d = dest;
  while (n && (*d++ = *src++)) {
    n--;
  }
  while (n--) {
    *d++ = '\0';
  }
  return dest;
}

char *strcat(char *dest, const char *src) {
  char *d = dest;
  while (*d)
    d++;
  while ((*d++ = *src++))
    ;
  return dest;
}

/* ========== Number Conversion ========== */

void itoa(int value, char *str, int base) {
  char *p = str;
  char *p1, *p2;
  int sign = 0;
  unsigned int uvalue;

  /* Handle negative numbers for base 10 */
  if (value < 0 && base == 10) {
    sign = 1;
    uvalue = (unsigned int)(-value);
  } else {
    uvalue = (unsigned int)value;
  }

  /* Convert to string (reversed) */
  do {
    int digit = uvalue % base;
    *p++ = (digit < 10) ? ('0' + digit) : ('a' + digit - 10);
    uvalue /= base;
  } while (uvalue);

  if (sign) {
    *p++ = '-';
  }

  *p = '\0';

  /* Reverse the string */
  p1 = str;
  p2 = p - 1;
  while (p1 < p2) {
    char tmp = *p1;
    *p1++ = *p2;
    *p2-- = tmp;
  }
}

void utoa(uint64_t value, char *str, int base) {
  char *p = str;
  char *p1, *p2;

  /* Convert to string (reversed) */
  do {
    int digit = value % base;
    *p++ = (digit < 10) ? ('0' + digit) : ('a' + digit - 10);
    value /= base;
  } while (value);

  *p = '\0';

  /* Reverse the string */
  p1 = str;
  p2 = p - 1;
  while (p1 < p2) {
    char tmp = *p1;
    *p1++ = *p2;
    *p2-- = tmp;
  }
}

/* ========== String Search ========== */

char *strstr(const char *haystack, const char *needle) {
  if (!*needle) return (char *)haystack;
  
  for (; *haystack; haystack++) {
    const char *h = haystack;
    const char *n = needle;
    
    while (*h && *n && *h == *n) {
      h++;
      n++;
    }
    
    if (!*n) return (char *)haystack;
  }
  
  return NULL;
}

/* ========== Formatted Output ========== */

/* Simple snprintf implementation */
int snprintf(char *str, size_t size, const char *format, ...) {
  if (size == 0) return 0;
  
  __builtin_va_list args;
  __builtin_va_start(args, format);
  
  char *out = str;
  char *end = str + size - 1;
  
  while (*format && out < end) {
    if (*format == '%') {
      format++;
      
      /* Handle flags and width (simplified) */
      int width = 0;
      int pad_zero = 0;
      
      if (*format == '0') {
        pad_zero = 1;
        format++;
      }
      
      while (*format >= '0' && *format <= '9') {
        width = width * 10 + (*format - '0');
        format++;
      }
      
      /* Handle length modifiers */
      int is_long = 0;
      if (*format == 'l') {
        is_long = 1;
        format++;
        if (*format == 'l') { /* %lld */
          format++;
        }
      }
      
      char buf[32];
      const char *s = NULL;
      int len = 0;
      
      switch (*format) {
        case 'd':
        case 'i': {
          long val = is_long ? __builtin_va_arg(args, long) : __builtin_va_arg(args, int);
          itoa((int)val, buf, 10);
          s = buf;
          break;
        }
        case 'u': {
          unsigned long val = is_long ? __builtin_va_arg(args, unsigned long) : __builtin_va_arg(args, unsigned int);
          utoa(val, buf, 10);
          s = buf;
          break;
        }
        case 'x':
        case 'X': {
          unsigned long val = is_long ? __builtin_va_arg(args, unsigned long) : __builtin_va_arg(args, unsigned int);
          utoa(val, buf, 16);
          s = buf;
          break;
        }
        case 's':
          s = __builtin_va_arg(args, const char *);
          if (!s) s = "(null)";
          break;
        case 'c':
          buf[0] = (char)__builtin_va_arg(args, int);
          buf[1] = '\0';
          s = buf;
          break;
        case '%':
          buf[0] = '%';
          buf[1] = '\0';
          s = buf;
          break;
        default:
          buf[0] = '%';
          buf[1] = *format;
          buf[2] = '\0';
          s = buf;
          break;
      }
      
      if (s) {
        len = strlen(s);
        
        /* Padding */
        while (width > len && out < end) {
          *out++ = pad_zero ? '0' : ' ';
          width--;
        }
        
        /* Copy string */
        while (*s && out < end) {
          *out++ = *s++;
        }
      }
      
      format++;
    } else {
      *out++ = *format++;
    }
  }
  
  *out = '\0';
  
  __builtin_va_end(args);
  
  return (int)(out - str);
}
