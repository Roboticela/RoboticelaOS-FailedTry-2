#ifndef ROBO_LIBC_STDDEF_H
#define ROBO_LIBC_STDDEF_H

#ifdef __SIZE_TYPE__
typedef __SIZE_TYPE__ size_t;
#else
typedef unsigned long long size_t;
#endif

#ifdef __PTRDIFF_TYPE__
typedef __PTRDIFF_TYPE__ ptrdiff_t;
#else
typedef long long ptrdiff_t;
#endif

#ifdef __WCHAR_TYPE__
typedef __WCHAR_TYPE__ wchar_t;
#else
typedef int wchar_t;
#endif

typedef struct {
    long long __ll;
    long double __ld;
} max_align_t;

#define NULL ((void *)0)

#ifdef __builtin_offsetof
#define offsetof(type, member) __builtin_offsetof(type, member)
#else
#define offsetof(type, member) ((size_t)&(((type *)0)->member))
#endif

#endif
