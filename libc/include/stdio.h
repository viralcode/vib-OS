/*
 * UnixOS - Minimal C Library (libc)
 * Standard I/O Header
 */

#ifndef _LIBC_STDIO_H
#define _LIBC_STDIO_H

#include "stddef.h"
#include "stdarg.h"

/* Standard file pointers */
typedef struct _FILE FILE;

extern FILE *stdin;
extern FILE *stdout;
extern FILE *stderr;

/* Buffer modes */
#define _IOFBF  0   /* Full buffering */
#define _IOLBF  1   /* Line buffering */
#define _IONBF  2   /* No buffering */

/* Default buffer size */
#define BUFSIZ  1024

/* End of file */
#define EOF     (-1)

/* ===================================================================== */
/* File operations */
/* ===================================================================== */

FILE *fopen(const char *pathname, const char *mode);
int fclose(FILE *stream);
size_t fread(void *ptr, size_t size, size_t nmemb, FILE *stream);
size_t fwrite(const void *ptr, size_t size, size_t nmemb, FILE *stream);
int fflush(FILE *stream);
int fseek(FILE *stream, long offset, int whence);
long ftell(FILE *stream);
void rewind(FILE *stream);
int feof(FILE *stream);
int ferror(FILE *stream);
void clearerr(FILE *stream);
int fileno(FILE *stream);

/* ===================================================================== */
/* Formatted I/O */
/* ===================================================================== */

int printf(const char *format, ...) __attribute__((format(printf, 1, 2)));
int fprintf(FILE *stream, const char *format, ...) __attribute__((format(printf, 2, 3)));
int sprintf(char *str, const char *format, ...) __attribute__((format(printf, 2, 3)));
int snprintf(char *str, size_t size, const char *format, ...) __attribute__((format(printf, 3, 4)));

int vprintf(const char *format, va_list ap);
int vfprintf(FILE *stream, const char *format, va_list ap);
int vsprintf(char *str, const char *format, va_list ap);
int vsnprintf(char *str, size_t size, const char *format, va_list ap);

int scanf(const char *format, ...);
int fscanf(FILE *stream, const char *format, ...);
int sscanf(const char *str, const char *format, ...);

/* ===================================================================== */
/* Character I/O */
/* ===================================================================== */

int fgetc(FILE *stream);
char *fgets(char *s, int size, FILE *stream);
int fputc(int c, FILE *stream);
int fputs(const char *s, FILE *stream);

int getc(FILE *stream);
int getchar(void);
int putc(int c, FILE *stream);
int putchar(int c);
int puts(const char *s);

int ungetc(int c, FILE *stream);

/* ===================================================================== */
/* Error handling */
/* ===================================================================== */

void perror(const char *s);

#endif /* _LIBC_STDIO_H */
