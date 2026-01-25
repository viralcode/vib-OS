/*
 * UnixOS - Minimal C Library Implementation
 * String Functions
 */

#include "../include/string.h"

/* ===================================================================== */
/* Memory functions */
/* ===================================================================== */

void *memcpy(void *dest, const void *src, size_t n)
{
    unsigned char *d = dest;
    const unsigned char *s = src;
    
    while (n--) {
        *d++ = *s++;
    }
    
    return dest;
}

void *memmove(void *dest, const void *src, size_t n)
{
    unsigned char *d = dest;
    const unsigned char *s = src;
    
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

void *memset(void *s, int c, size_t n)
{
    unsigned char *p = s;
    
    while (n--) {
        *p++ = (unsigned char)c;
    }
    
    return s;
}

int memcmp(const void *s1, const void *s2, size_t n)
{
    const unsigned char *p1 = s1;
    const unsigned char *p2 = s2;
    
    while (n--) {
        if (*p1 != *p2) {
            return *p1 - *p2;
        }
        p1++;
        p2++;
    }
    
    return 0;
}

void *memchr(const void *s, int c, size_t n)
{
    const unsigned char *p = s;
    
    while (n--) {
        if (*p == (unsigned char)c) {
            return (void *)p;
        }
        p++;
    }
    
    return NULL;
}

void bzero(void *s, size_t n)
{
    memset(s, 0, n);
}

/* ===================================================================== */
/* String functions */
/* ===================================================================== */

size_t strlen(const char *s)
{
    const char *p = s;
    while (*p) p++;
    return p - s;
}

size_t strnlen(const char *s, size_t maxlen)
{
    size_t len = 0;
    while (len < maxlen && s[len]) {
        len++;
    }
    return len;
}

char *strcpy(char *dest, const char *src)
{
    char *d = dest;
    while ((*d++ = *src++));
    return dest;
}

char *strncpy(char *dest, const char *src, size_t n)
{
    char *d = dest;
    
    while (n && (*d++ = *src++)) {
        n--;
    }
    
    while (n--) {
        *d++ = '\0';
    }
    
    return dest;
}

char *strcat(char *dest, const char *src)
{
    char *d = dest;
    while (*d) d++;
    while ((*d++ = *src++));
    return dest;
}

char *strncat(char *dest, const char *src, size_t n)
{
    char *d = dest;
    while (*d) d++;
    
    while (n-- && (*d++ = *src++)) {
        if (n == 0) {
            *d = '\0';
        }
    }
    
    return dest;
}

int strcmp(const char *s1, const char *s2)
{
    while (*s1 && *s1 == *s2) {
        s1++;
        s2++;
    }
    return *(unsigned char *)s1 - *(unsigned char *)s2;
}

int strncmp(const char *s1, const char *s2, size_t n)
{
    if (n == 0) return 0;
    
    while (--n && *s1 && *s1 == *s2) {
        s1++;
        s2++;
    }
    return *(unsigned char *)s1 - *(unsigned char *)s2;
}

static int tolower(int c)
{
    if (c >= 'A' && c <= 'Z') {
        return c + ('a' - 'A');
    }
    return c;
}

int strcasecmp(const char *s1, const char *s2)
{
    while (*s1 && tolower(*s1) == tolower(*s2)) {
        s1++;
        s2++;
    }
    return tolower(*(unsigned char *)s1) - tolower(*(unsigned char *)s2);
}

int strncasecmp(const char *s1, const char *s2, size_t n)
{
    if (n == 0) return 0;
    
    while (--n && *s1 && tolower(*s1) == tolower(*s2)) {
        s1++;
        s2++;
    }
    return tolower(*(unsigned char *)s1) - tolower(*(unsigned char *)s2);
}

char *strchr(const char *s, int c)
{
    while (*s) {
        if (*s == (char)c) {
            return (char *)s;
        }
        s++;
    }
    return c == 0 ? (char *)s : NULL;
}

char *strrchr(const char *s, int c)
{
    const char *last = NULL;
    
    while (*s) {
        if (*s == (char)c) {
            last = s;
        }
        s++;
    }
    
    if (c == 0) return (char *)s;
    return (char *)last;
}

char *strstr(const char *haystack, const char *needle)
{
    size_t needle_len = strlen(needle);
    
    if (needle_len == 0) {
        return (char *)haystack;
    }
    
    while (*haystack) {
        if (strncmp(haystack, needle, needle_len) == 0) {
            return (char *)haystack;
        }
        haystack++;
    }
    
    return NULL;
}

/* Error strings */
static const char *error_strings[] = {
    "Success",
    "Operation not permitted",
    "No such file or directory",
    "No such process",
    "Interrupted system call",
    "I/O error",
    "No such device or address",
    "Argument list too long",
    "Exec format error",
    "Bad file number",
    "No child processes",
    "Try again",
    "Out of memory",
    "Permission denied",
};

char *strerror(int errnum)
{
    static char buf[64];
    
    if (errnum >= 0 && errnum < (int)(sizeof(error_strings) / sizeof(error_strings[0]))) {
        return (char *)error_strings[errnum];
    }
    
    /* Format unknown error */
    char *p = buf;
    const char *prefix = "Unknown error ";
    while (*prefix) *p++ = *prefix++;
    
    /* Convert number */
    if (errnum < 0) {
        *p++ = '-';
        errnum = -errnum;
    }
    
    char num[16];
    int i = 0;
    do {
        num[i++] = '0' + (errnum % 10);
        errnum /= 10;
    } while (errnum > 0);
    
    while (i > 0) {
        *p++ = num[--i];
    }
    *p = '\0';
    
    return buf;
}
