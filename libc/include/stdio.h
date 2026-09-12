#ifndef ROBO_LIBC_STDIO_H
#define ROBO_LIBC_STDIO_H

#include <stddef.h>
#include <stdarg.h>

#define EOF (-1)
#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2

typedef struct __robo_file FILE;

extern FILE *stdin;
extern FILE *stdout;
extern FILE *stderr;

int printf(const char *fmt, ...);
int fprintf(FILE *fp, const char *fmt, ...);
int sprintf(char *dst, const char *fmt, ...);
int snprintf(char *dst, size_t n, const char *fmt, ...);

int vprintf(const char *fmt, va_list ap);
int vfprintf(FILE *fp, const char *fmt, va_list ap);
int vsnprintf(char *dst, size_t n, const char *fmt, va_list ap);

int puts(const char *s);
int fputs(const char *s, FILE *fp);
int putchar(int c);
int fputc(int c, FILE *fp);

FILE *fopen(const char *path, const char *mode);
int fclose(FILE *fp);
size_t fread(void *ptr, size_t size, size_t nmemb, FILE *fp);
size_t fwrite(const void *ptr, size_t size, size_t nmemb, FILE *fp);
int fflush(FILE *fp);

#endif
