#include <string.h>
#include <stdint.h>

void *memcpy(void *dst, const void *src, size_t n)
{
    unsigned char *d = (unsigned char *)dst;
    const unsigned char *s = (const unsigned char *)src;
    while (n--)
        *d++ = *s++;
    return dst;
}

void *memmove(void *dst, const void *src, size_t n)
{
    unsigned char *d = (unsigned char *)dst;
    const unsigned char *s = (const unsigned char *)src;
    if (d == s || n == 0)
        return dst;
    if (d < s) {
        while (n--)
            *d++ = *s++;
    } else {
        d += n;
        s += n;
        while (n--)
            *--d = *--s;
    }
    return dst;
}

void *memset(void *dst, int c, size_t n)
{
    unsigned char *d = (unsigned char *)dst;
    unsigned char v = (unsigned char)c;
    while (n--)
        *d++ = v;
    return dst;
}

int memcmp(const void *a, const void *b, size_t n)
{
    const unsigned char *x = (const unsigned char *)a;
    const unsigned char *y = (const unsigned char *)b;
    while (n--) {
        if (*x != *y)
            return (int)*x - (int)*y;
        x++;
        y++;
    }
    return 0;
}

size_t strlen(const char *s)
{
    size_t n = 0;
    if (!s)
        return 0;
    while (s[n])
        n++;
    return n;
}

size_t strnlen(const char *s, size_t n)
{
    size_t i = 0;
    if (!s)
        return 0;
    while (i < n && s[i])
        i++;
    return i;
}

char *strcpy(char *dst, const char *src)
{
    char *o = dst;
    if (!dst || !src)
        return dst;
    while ((*dst++ = *src++) != 0)
        ;
    return o;
}

char *strncpy(char *dst, const char *src, size_t n)
{
    size_t i;
    if (!dst)
        return dst;
    for (i = 0; i < n; i++) {
        char c = src ? src[i] : 0;
        dst[i] = c;
        if (!c) {
            for (i++; i < n; i++)
                dst[i] = 0;
            break;
        }
        if (!src)
            break;
    }
    return dst;
}

int strcmp(const char *a, const char *b)
{
    if (!a)
        a = "";
    if (!b)
        b = "";
    while (*a && *a == *b) {
        a++;
        b++;
    }
    return (unsigned char)*a - (unsigned char)*b;
}

int strncmp(const char *a, const char *b, size_t n)
{
    if (!a)
        a = "";
    if (!b)
        b = "";
    while (n && *a && *a == *b) {
        a++;
        b++;
        n--;
    }
    if (!n)
        return 0;
    return (unsigned char)*a - (unsigned char)*b;
}

char *strchr(const char *s, int c)
{
    char ch = (char)c;
    if (!s)
        return 0;
    for (;;) {
        if (*s == ch)
            return (char *)s;
        if (!*s)
            return 0;
        s++;
    }
}

char *strrchr(const char *s, int c)
{
    const char *last = 0;
    char ch = (char)c;
    if (!s)
        return 0;
    do {
        if (*s == ch)
            last = s;
    } while (*s++);
    return (char *)last;
}

char *strstr(const char *hay, const char *needle)
{
    size_t n;
    if (!hay)
        return 0;
    if (!needle || !needle[0])
        return (char *)hay;
    n = strlen(needle);
    while (*hay) {
        if (strncmp(hay, needle, n) == 0)
            return (char *)hay;
        hay++;
    }
    return 0;
}

char *strcat(char *dst, const char *src)
{
    char *o = dst;
    if (!dst)
        return dst;
    while (*dst)
        dst++;
    if (src) {
        while ((*dst++ = *src++) != 0)
            ;
    }
    return o;
}

char *strncat(char *dst, const char *src, size_t n)
{
    char *o = dst;
    if (!dst)
        return dst;
    while (*dst)
        dst++;
    while (n && src && *src) {
        *dst++ = *src++;
        n--;
    }
    *dst = 0;
    return o;
}
