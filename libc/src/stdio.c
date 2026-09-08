#include "../include/stdio.h"
#include "../include/unistd.h"
#include "../include/string.h"
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>

static FILE _stdin_file = { STDIN_FILENO };
static FILE _stdout_file = { STDOUT_FILENO };
static FILE _stderr_file = { STDERR_FILENO };

FILE *stdin = &_stdin_file;
FILE *stdout = &_stdout_file;
FILE *stderr = &_stderr_file;

int putchar(int c) {
    unsigned char ch = (unsigned char)c;
    if (write(STDOUT_FILENO, &ch, 1) == 1) {
        return ch;
    }
    return EOF;
}

int getchar(void) {
    unsigned char ch;
    if (read(STDIN_FILENO, &ch, 1) == 1) {
        return ch;
    }
    return EOF;
}

int puts(const char *s) {
    if (!s) return EOF;
    size_t len = strlen(s);
    if (write(STDOUT_FILENO, s, len) < 0) return EOF;
    if (write(STDOUT_FILENO, "\n", 1) < 0) return EOF;
    return (int)len + 1;
}

static void format_num(char **out, size_t *rem, unsigned long val, int base, int uppercase, int width, char pad, int negative) {
    char buf[32];
    int i = 0;
    const char *digits = uppercase ? "0123456789ABCDEF" : "0123456789abcdef";

    if (val == 0) {
        buf[i++] = '0';
    } else {
        while (val > 0) {
            buf[i++] = digits[val % base];
            val /= base;
        }
    }

    if (negative) {
        buf[i++] = '-';
    }

    int pad_count = (width > i) ? (width - i) : 0;
    if (pad == '0' && negative) {
        if (*rem > 1) {
            *(*out)++ = '-';
            (*rem)--;
        }
        i--; // skip '-'
    }

    while (pad_count-- > 0) {
        if (*rem > 1) {
            *(*out)++ = pad;
            (*rem)--;
        }
    }

    while (i-- > 0) {
        if (*rem > 1) {
            *(*out)++ = buf[i];
            (*rem)--;
        }
    }
}

int vsnprintf(char *str, size_t size, const char *format, va_list ap) {
    if (!str || size == 0) return 0;

    char *out = str;
    size_t rem = size;

    while (*format && rem > 1) {
        if (*format != '%') {
            *out++ = *format++;
            rem--;
            continue;
        }

        format++; // skip '%'
        if (*format == '\0') break;

        // check for %%
        if (*format == '%') {
            *out++ = '%';
            rem--;
            format++;
            continue;
        }

        // padding
        char pad = ' ';
        if (*format == '0') {
            pad = '0';
            format++;
        }

        // width
        int width = 0;
        while (*format >= '0' && *format <= '9') {
            width = width * 10 + (*format - '0');
            format++;
        }

        // length
        int is_long = 0;
        if (*format == 'l') {
            is_long = 1;
            format++;
            if (*format == 'l') {
                is_long = 2;
                format++;
            }
        }

        switch (*format) {
            case 'c': {
                char c = (char)va_arg(ap, int);
                if (rem > 1) {
                    *out++ = c;
                    rem--;
                }
                break;
            }
            case 's': {
                const char *s = va_arg(ap, const char*);
                if (!s) s = "(null)";
                size_t slen = strlen(s);
                int pad_count = (width > (int)slen) ? (width - (int)slen) : 0;
                while (pad_count-- > 0 && rem > 1) {
                    *out++ = ' ';
                    rem--;
                }
                while (*s && rem > 1) {
                    *out++ = *s++;
                    rem--;
                }
                break;
            }
            case 'd':
            case 'i': {
                long val = is_long ? va_arg(ap, long) : va_arg(ap, int);
                int negative = 0;
                unsigned long uval;
                if (val < 0) {
                    negative = 1;
                    uval = (unsigned long)-val;
                } else {
                    uval = (unsigned long)val;
                }
                format_num(&out, &rem, uval, 10, 0, width, pad, negative);
                break;
            }
            case 'u': {
                unsigned long val = is_long ? va_arg(ap, unsigned long) : va_arg(ap, unsigned int);
                format_num(&out, &rem, val, 10, 0, width, pad, 0);
                break;
            }
            case 'x': {
                unsigned long val = is_long ? va_arg(ap, unsigned long) : va_arg(ap, unsigned int);
                format_num(&out, &rem, val, 16, 0, width, pad, 0);
                break;
            }
            case 'X': {
                unsigned long val = is_long ? va_arg(ap, unsigned long) : va_arg(ap, unsigned int);
                format_num(&out, &rem, val, 16, 1, width, pad, 0);
                break;
            }
            case 'o': {
                unsigned long val = is_long ? va_arg(ap, unsigned long) : va_arg(ap, unsigned int);
                format_num(&out, &rem, val, 8, 0, width, pad, 0);
                break;
            }
            case 'p': {
                void *ptr = va_arg(ap, void*);
                if (rem > 3) {
                    *out++ = '0';
                    *out++ = 'x';
                    rem -= 2;
                }
                format_num(&out, &rem, (uintptr_t)ptr, 16, 0, sizeof(uintptr_t) * 2, '0', 0);
                break;
            }
            default:
                if (rem > 1) {
                    *out++ = *format;
                    rem--;
                }
                break;
        }
        format++;
    }

    *out = '\0';
    return (int)(out - str);
}

int snprintf(char *str, size_t size, const char *format, ...) {
    va_list ap;
    va_start(ap, format);
    int ret = vsnprintf(str, size, format, ap);
    va_end(ap);
    return ret;
}

int vsprintf(char *str, const char *format, va_list ap) {
    return vsnprintf(str, 1048576, format, ap);
}

int sprintf(char *str, const char *format, ...) {
    va_list ap;
    va_start(ap, format);
    int ret = vsprintf(str, format, ap);
    va_end(ap);
    return ret;
}

int vdprintf(int fd, const char *format, va_list ap) {
    char buf[1024];
    int len = vsnprintf(buf, sizeof(buf), format, ap);
    if (len > 0) {
        return write(fd, buf, len);
    }
    return 0;
}

int dprintf(int fd, const char *format, ...) {
    va_list ap;
    va_start(ap, format);
    int ret = vdprintf(fd, format, ap);
    va_end(ap);
    return ret;
}

int printf(const char *format, ...) {
    va_list ap;
    va_start(ap, format);
    int ret = vdprintf(STDOUT_FILENO, format, ap);
    va_end(ap);
    return ret;
}
