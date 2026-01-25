/*
 * UnixOS - Minimal C Library Implementation
 * Standard I/O Functions
 */

#include "../include/stdio.h"
#include "../include/stdlib.h"
#include "../include/string.h"
#include "../include/unistd.h"
#include "../include/fcntl.h"
#include "../include/errno.h"

/* ===================================================================== */
/* FILE structure */
/* ===================================================================== */

struct _FILE {
    int fd;
    int flags;
    int error;
    int eof;
    char *buf;
    size_t buf_size;
    size_t buf_pos;
    int buf_mode;
    int ungetc_char;
    int has_ungetc;
    int in_use;
};

#define FILE_READ   1
#define FILE_WRITE  2
#define FILE_APPEND 4

/* File pool for fopen */
#define FILE_POOL_SIZE 16
static FILE _file_pool[FILE_POOL_SIZE];

/* Standard streams */
static FILE _stdin  = { .fd = 0, .flags = FILE_READ,  .buf_mode = _IOLBF, .in_use = 1 };
static FILE _stdout = { .fd = 1, .flags = FILE_WRITE, .buf_mode = _IOLBF, .in_use = 1 };
static FILE _stderr = { .fd = 2, .flags = FILE_WRITE, .buf_mode = _IONBF, .in_use = 1 };

FILE *stdin  = &_stdin;
FILE *stdout = &_stdout;
FILE *stderr = &_stderr;

/* ===================================================================== */
/* File open/close */
/* ===================================================================== */

static FILE *_alloc_file(void)
{
    for (int i = 0; i < FILE_POOL_SIZE; i++) {
        if (!_file_pool[i].in_use) {
            memset(&_file_pool[i], 0, sizeof(FILE));
            _file_pool[i].in_use = 1;
            return &_file_pool[i];
        }
    }
    return NULL;
}

FILE *fopen(const char *pathname, const char *mode)
{
    if (!pathname || !mode) return NULL;
    
    int flags = 0;
    int oflags = 0;
    
    switch (mode[0]) {
        case 'r':
            flags = FILE_READ;
            oflags = O_RDONLY;
            if (mode[1] == '+') {
                flags |= FILE_WRITE;
                oflags = O_RDWR;
            }
            break;
        case 'w':
            flags = FILE_WRITE;
            oflags = O_WRONLY | O_CREAT | O_TRUNC;
            if (mode[1] == '+') {
                flags |= FILE_READ;
                oflags = O_RDWR | O_CREAT | O_TRUNC;
            }
            break;
        case 'a':
            flags = FILE_WRITE | FILE_APPEND;
            oflags = O_WRONLY | O_CREAT | O_APPEND;
            if (mode[1] == '+') {
                flags |= FILE_READ;
                oflags = O_RDWR | O_CREAT | O_APPEND;
            }
            break;
        default:
            return NULL;
    }
    
    int fd = open(pathname, oflags);
    if (fd < 0) return NULL;
    
    FILE *fp = _alloc_file();
    if (!fp) {
        close(fd);
        return NULL;
    }
    
    fp->fd = fd;
    fp->flags = flags;
    fp->buf_mode = _IOFBF;
    
    return fp;
}

int fclose(FILE *stream)
{
    if (!stream) return EOF;
    if (stream == stdin || stream == stdout || stream == stderr) {
        return 0;  /* Don't actually close standard streams */
    }
    
    fflush(stream);
    int ret = close(stream->fd);
    stream->in_use = 0;
    
    return (ret < 0) ? EOF : 0;
}

FILE *freopen(const char *pathname, const char *mode, FILE *stream)
{
    if (!stream) return NULL;
    
    fflush(stream);
    close(stream->fd);
    
    if (!pathname) {
        /* Just change mode, not implemented */
        return NULL;
    }
    
    int flags = 0;
    int oflags = 0;
    
    switch (mode[0]) {
        case 'r':
            flags = FILE_READ;
            oflags = O_RDONLY;
            break;
        case 'w':
            flags = FILE_WRITE;
            oflags = O_WRONLY | O_CREAT | O_TRUNC;
            break;
        case 'a':
            flags = FILE_WRITE | FILE_APPEND;
            oflags = O_WRONLY | O_CREAT | O_APPEND;
            break;
        default:
            return NULL;
    }
    
    int fd = open(pathname, oflags);
    if (fd < 0) return NULL;
    
    stream->fd = fd;
    stream->flags = flags;
    stream->error = 0;
    stream->eof = 0;
    stream->has_ungetc = 0;
    
    return stream;
}

/* ===================================================================== */
/* Basic I/O */
/* ===================================================================== */

int fgetc(FILE *stream)
{
    if (!stream) return EOF;
    
    if (stream->has_ungetc) {
        stream->has_ungetc = 0;
        return stream->ungetc_char;
    }
    
    unsigned char c;
    ssize_t n = read(stream->fd, &c, 1);
    
    if (n <= 0) {
        if (n == 0) stream->eof = 1;
        else stream->error = 1;
        return EOF;
    }
    
    return c;
}

char *fgets(char *s, int size, FILE *stream)
{
    if (!s || size <= 0 || !stream) return NULL;
    
    int i = 0;
    int c;
    
    while (i < size - 1) {
        c = fgetc(stream);
        if (c == EOF) {
            if (i == 0) return NULL;
            break;
        }
        s[i++] = c;
        if (c == '\n') break;
    }
    
    s[i] = '\0';
    return s;
}

int fputc(int c, FILE *stream)
{
    if (!stream) return EOF;
    
    unsigned char ch = c;
    ssize_t n = write(stream->fd, &ch, 1);
    
    if (n != 1) {
        stream->error = 1;
        return EOF;
    }
    
    return (unsigned char)c;
}

int fputs(const char *s, FILE *stream)
{
    if (!s || !stream) return EOF;
    
    size_t len = strlen(s);
    ssize_t n = write(stream->fd, s, len);
    
    if (n != (ssize_t)len) {
        stream->error = 1;
        return EOF;
    }
    
    return 0;
}

int getc(FILE *stream) { return fgetc(stream); }
int getchar(void) { return fgetc(stdin); }
int putc(int c, FILE *stream) { return fputc(c, stream); }
int putchar(int c) { return fputc(c, stdout); }

int puts(const char *s)
{
    if (fputs(s, stdout) == EOF) return EOF;
    if (fputc('\n', stdout) == EOF) return EOF;
    return 0;
}

int ungetc(int c, FILE *stream)
{
    if (!stream || c == EOF) return EOF;
    stream->ungetc_char = c;
    stream->has_ungetc = 1;
    stream->eof = 0;
    return c;
}

/* ===================================================================== */
/* Read/Write */
/* ===================================================================== */

size_t fread(void *ptr, size_t size, size_t nmemb, FILE *stream)
{
    if (!ptr || !stream || size == 0 || nmemb == 0) return 0;
    
    size_t total = size * nmemb;
    ssize_t n = read(stream->fd, ptr, total);
    
    if (n <= 0) {
        if (n == 0) stream->eof = 1;
        else stream->error = 1;
        return 0;
    }
    
    return n / size;
}

size_t fwrite(const void *ptr, size_t size, size_t nmemb, FILE *stream)
{
    if (!ptr || !stream || size == 0 || nmemb == 0) return 0;
    
    size_t total = size * nmemb;
    ssize_t n = write(stream->fd, ptr, total);
    
    if (n < 0) {
        stream->error = 1;
        return 0;
    }
    
    return n / size;
}

int fflush(FILE *stream)
{
    (void)stream;
    /* No buffering implemented yet */
    return 0;
}

/* ===================================================================== */
/* Seek/Tell */
/* ===================================================================== */

int fseek(FILE *stream, long offset, int whence)
{
    if (!stream) return -1;
    
    off_t result = lseek(stream->fd, offset, whence);
    if (result < 0) return -1;
    
    stream->eof = 0;
    stream->has_ungetc = 0;
    return 0;
}

long ftell(FILE *stream)
{
    if (!stream) return -1;
    return lseek(stream->fd, 0, SEEK_CUR);
}

void rewind(FILE *stream)
{
    if (stream) {
        fseek(stream, 0, SEEK_SET);
        stream->error = 0;
    }
}

/* ===================================================================== */
/* Status */
/* ===================================================================== */

int feof(FILE *stream)
{
    return stream ? stream->eof : 0;
}

int ferror(FILE *stream)
{
    return stream ? stream->error : 0;
}

void clearerr(FILE *stream)
{
    if (stream) {
        stream->error = 0;
        stream->eof = 0;
    }
}

int fileno(FILE *stream)
{
    return stream ? stream->fd : -1;
}

/* ===================================================================== */
/* Formatted output */
/* ===================================================================== */

int vsnprintf(char *str, size_t size, const char *format, va_list ap)
{
    char *p = str;
    char *end = size > 0 ? str + size - 1 : str;
    char numbuf[32];
    
    while (*format && p < end) {
        if (*format != '%') {
            *p++ = *format++;
            continue;
        }
        
        format++;  /* Skip '%' */
        
        /* Flags */
        int zero_pad = 0;
        int width = 0;
        int is_long = 0;
        
        while (*format == '0') {
            zero_pad = 1;
            format++;
        }
        
        while (*format >= '0' && *format <= '9') {
            width = width * 10 + (*format - '0');
            format++;
        }
        
        if (*format == 'l') {
            is_long = 1;
            format++;
            if (*format == 'l') {
                format++;
            }
        } else if (*format == 'z') {
            is_long = 1;
            format++;
        }
        
        switch (*format) {
            case 'd':
            case 'i': {
                long val = is_long ? va_arg(ap, long) : va_arg(ap, int);
                int neg = val < 0;
                if (neg) val = -val;
                
                int i = 0;
                do {
                    numbuf[i++] = '0' + (val % 10);
                    val /= 10;
                } while (val > 0);
                
                if (neg) numbuf[i++] = '-';
                
                while (i < width && p < end && zero_pad) {
                    *p++ = neg ? (--i == 0 ? '-' : '0') : '0';
                }
                
                while (i > 0 && p < end) {
                    *p++ = numbuf[--i];
                }
                break;
            }
            
            case 'u': {
                unsigned long val = is_long ? va_arg(ap, unsigned long) : va_arg(ap, unsigned int);
                int i = 0;
                do {
                    numbuf[i++] = '0' + (val % 10);
                    val /= 10;
                } while (val > 0);
                
                while (i < width && p < end) {
                    *p++ = zero_pad ? '0' : ' ';
                }
                while (i > 0 && p < end) {
                    *p++ = numbuf[--i];
                }
                break;
            }
            
            case 'x':
            case 'X': {
                unsigned long val = is_long ? va_arg(ap, unsigned long) : va_arg(ap, unsigned int);
                const char *digits = (*format == 'X') ? "0123456789ABCDEF" : "0123456789abcdef";
                int i = 0;
                do {
                    numbuf[i++] = digits[val & 0xf];
                    val >>= 4;
                } while (val > 0);
                
                while (i < width && p < end) {
                    *p++ = zero_pad ? '0' : ' ';
                }
                while (i > 0 && p < end) {
                    *p++ = numbuf[--i];
                }
                break;
            }
            
            case 'p': {
                unsigned long val = (unsigned long)va_arg(ap, void *);
                if (p < end) *p++ = '0';
                if (p < end) *p++ = 'x';
                
                int i = 0;
                for (int j = 0; j < 16; j++) {
                    numbuf[i++] = "0123456789abcdef"[val & 0xf];
                    val >>= 4;
                }
                while (i > 0 && p < end) {
                    *p++ = numbuf[--i];
                }
                break;
            }
            
            case 's': {
                const char *s = va_arg(ap, const char *);
                if (!s) s = "(null)";
                while (*s && p < end) {
                    *p++ = *s++;
                }
                break;
            }
            
            case 'c':
                if (p < end) *p++ = (char)va_arg(ap, int);
                break;
                
            case '%':
                if (p < end) *p++ = '%';
                break;
                
            default:
                if (p < end) *p++ = '%';
                if (p < end) *p++ = *format;
                break;
        }
        
        format++;
    }
    
    if (size > 0) *p = '\0';
    return p - str;
}

int vsprintf(char *str, const char *format, va_list ap)
{
    return vsnprintf(str, (size_t)-1, format, ap);
}

int snprintf(char *str, size_t size, const char *format, ...)
{
    va_list ap;
    va_start(ap, format);
    int ret = vsnprintf(str, size, format, ap);
    va_end(ap);
    return ret;
}

int sprintf(char *str, const char *format, ...)
{
    va_list ap;
    va_start(ap, format);
    int ret = vsprintf(str, format, ap);
    va_end(ap);
    return ret;
}

int vfprintf(FILE *stream, const char *format, va_list ap)
{
    char buf[1024];
    int len = vsnprintf(buf, sizeof(buf), format, ap);
    if (len > 0) {
        write(stream->fd, buf, len);
    }
    return len;
}

int fprintf(FILE *stream, const char *format, ...)
{
    va_list ap;
    va_start(ap, format);
    int ret = vfprintf(stream, format, ap);
    va_end(ap);
    return ret;
}

int vprintf(const char *format, va_list ap)
{
    return vfprintf(stdout, format, ap);
}

int printf(const char *format, ...)
{
    va_list ap;
    va_start(ap, format);
    int ret = vprintf(format, ap);
    va_end(ap);
    return ret;
}

/* ===================================================================== */
/* Error handling */
/* ===================================================================== */

void perror(const char *s)
{
    if (s && *s) {
        fputs(s, stderr);
        fputs(": ", stderr);
    }
    fputs("Error\n", stderr);
}

/* ===================================================================== */
/* Formatted input (simplified) */
/* ===================================================================== */

static int _isspace(int c)
{
    return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v';
}

static int _isdigit(int c)
{
    return c >= '0' && c <= '9';
}

int vsscanf(const char *str, const char *format, va_list ap)
{
    const char *s = str;
    int count = 0;
    
    while (*format && *s) {
        if (_isspace(*format)) {
            format++;
            while (_isspace(*s)) s++;
            continue;
        }
        
        if (*format != '%') {
            if (*format == *s) {
                format++;
                s++;
                continue;
            }
            break;
        }
        
        format++;  /* Skip '%' */
        
        /* Handle width specifier (ignored for now) */
        int width = 0;
        while (_isdigit(*format)) {
            width = width * 10 + (*format - '0');
            format++;
        }
        (void)width;
        
        /* Handle length modifier */
        int is_long = 0;
        if (*format == 'l') {
            is_long = 1;
            format++;
            if (*format == 'l') format++;
        } else if (*format == 'h') {
            format++;
            if (*format == 'h') format++;
        }
        
        switch (*format) {
            case 'd':
            case 'i': {
                while (_isspace(*s)) s++;
                int sign = 1;
                if (*s == '-') {
                    sign = -1;
                    s++;
                } else if (*s == '+') {
                    s++;
                }
                
                if (!_isdigit(*s)) goto done;
                
                long val = 0;
                while (_isdigit(*s)) {
                    val = val * 10 + (*s - '0');
                    s++;
                }
                val *= sign;
                
                if (is_long) {
                    *va_arg(ap, long *) = val;
                } else {
                    *va_arg(ap, int *) = (int)val;
                }
                count++;
                break;
            }
            
            case 'u': {
                while (_isspace(*s)) s++;
                if (!_isdigit(*s)) goto done;
                
                unsigned long val = 0;
                while (_isdigit(*s)) {
                    val = val * 10 + (*s - '0');
                    s++;
                }
                
                if (is_long) {
                    *va_arg(ap, unsigned long *) = val;
                } else {
                    *va_arg(ap, unsigned int *) = (unsigned int)val;
                }
                count++;
                break;
            }
            
            case 'x':
            case 'X': {
                while (_isspace(*s)) s++;
                if (*s == '0' && (s[1] == 'x' || s[1] == 'X')) {
                    s += 2;
                }
                
                unsigned long val = 0;
                while (1) {
                    int digit;
                    if (_isdigit(*s)) {
                        digit = *s - '0';
                    } else if (*s >= 'a' && *s <= 'f') {
                        digit = *s - 'a' + 10;
                    } else if (*s >= 'A' && *s <= 'F') {
                        digit = *s - 'A' + 10;
                    } else {
                        break;
                    }
                    val = val * 16 + digit;
                    s++;
                }
                
                if (is_long) {
                    *va_arg(ap, unsigned long *) = val;
                } else {
                    *va_arg(ap, unsigned int *) = (unsigned int)val;
                }
                count++;
                break;
            }
            
            case 's': {
                while (_isspace(*s)) s++;
                char *dest = va_arg(ap, char *);
                while (*s && !_isspace(*s)) {
                    *dest++ = *s++;
                }
                *dest = '\0';
                count++;
                break;
            }
            
            case 'c': {
                char *dest = va_arg(ap, char *);
                *dest = *s++;
                count++;
                break;
            }
            
            case '%':
                if (*s == '%') s++;
                else goto done;
                break;
                
            case 'n': {
                *va_arg(ap, int *) = (int)(s - str);
                break;
            }
            
            default:
                goto done;
        }
        
        format++;
    }
    
done:
    return count;
}

int sscanf(const char *str, const char *format, ...)
{
    va_list ap;
    va_start(ap, format);
    int ret = vsscanf(str, format, ap);
    va_end(ap);
    return ret;
}

int vfscanf(FILE *stream, const char *format, va_list ap)
{
    char buf[1024];
    int i = 0;
    int c;
    
    /* Read a line for simplicity */
    while (i < 1023 && (c = fgetc(stream)) != EOF && c != '\n') {
        buf[i++] = c;
    }
    buf[i] = '\0';
    
    return vsscanf(buf, format, ap);
}

int fscanf(FILE *stream, const char *format, ...)
{
    va_list ap;
    va_start(ap, format);
    int ret = vfscanf(stream, format, ap);
    va_end(ap);
    return ret;
}

int scanf(const char *format, ...)
{
    va_list ap;
    va_start(ap, format);
    int ret = vfscanf(stdin, format, ap);
    va_end(ap);
    return ret;
}
