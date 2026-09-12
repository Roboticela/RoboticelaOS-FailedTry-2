#ifndef ROBO_LIBC_STDLIB_H
#define ROBO_LIBC_STDLIB_H

#include <stddef.h>

#define EXIT_SUCCESS 0
#define EXIT_FAILURE 1

void *malloc(size_t size);
void *calloc(size_t count, size_t size);
void *realloc(void *ptr, size_t size);
void free(void *ptr);

void abort(void);
void exit(int status);
void _Exit(int status);

int atoi(const char *s);
long strtol(const char *s, char **end, int base);
unsigned long strtoul(const char *s, char **end, int base);

void qsort(void *base, size_t count, size_t size,
           int (*cmp)(const void *, const void *));
void *bsearch(const void *key, const void *base, size_t count, size_t size,
              int (*cmp)(const void *, const void *));

int abs(int n);
long labs(long n);

char *getenv(const char *name);
int setenv(const char *name, const char *value, int overwrite);
int unsetenv(const char *name);

#endif
