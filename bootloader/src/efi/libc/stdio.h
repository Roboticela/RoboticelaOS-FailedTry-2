#pragma once
#include <stddef.h>
#include <stdarg.h>

typedef struct rb_efi_file FILE;
extern FILE *stderr;
extern FILE *stdout;

int snprintf(char *s, size_t n, const char *fmt, ...);
int vsnprintf(char *s, size_t n, const char *fmt, va_list ap);
int fprintf(FILE *f, const char *fmt, ...);
