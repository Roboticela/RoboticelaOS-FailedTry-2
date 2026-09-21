#include "roboboot.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

const Platform *Plat = NULL;

void *Alloc(size_t n)
{
    return malloc(n ? n : 1);
}

void *RoboRbCalloc(size_t n)
{
    void *p = calloc(1, n ? n : 1);
    return p;
}

void RoboRbFree(void *p)
{
    free(p);
}

void Zero(void *p, size_t n)
{
    memset(p, 0, n);
}

void Copy(void *dst, const void *src, size_t n)
{
    if (n) memcpy(dst, src, n);
}

int Strlen(const char *s)
{
    return s ? (int)strlen(s) : 0;
}

int Strcmp(const char *a, const char *b)
{
    if (!a) a = "";
    if (!b) b = "";
    return strcmp(a, b);
}

int Strncmp(const char *a, const char *b, size_t n)
{
    if (!a) a = "";
    if (!b) b = "";
    return strncmp(a, b, n);
}

void Strcpy(char *dst, size_t cap, const char *src)
{
    if (!dst || cap == 0) return;
    if (!src) src = "";
    size_t n = strlen(src);
    if (n >= cap) n = cap - 1;
    memcpy(dst, src, n);
    dst[n] = 0;
}

int StrEq(const char *a, const char *b)
{
    return Strcmp(a, b) == 0;
}

int Endswith(const char *s, const char *suf)
{
    int ls = Strlen(s);
    int lf = Strlen(suf);
    if (lf > ls) return 0;
    return Strcmp(s + ls - lf, suf) == 0;
}

void Join(char *dst, size_t cap, const char *a, const char *b)
{
    Strcpy(dst, cap, a ? a : "");
    if (!b || !b[0]) return;
    size_t n = strlen(dst);
    int needSlash = n > 0 && dst[n - 1] != '/' && dst[n - 1] != '\\' && b[0] != '/' && b[0] != '\\';
    if (needSlash && n + 1 < cap) {
        dst[n] = '/';
        dst[n + 1] = 0;
    }
    size_t used = strlen(dst);
    if (used >= cap) return;
    Strcpy(dst + used, cap - used, b);
}

void Logf(const char *fmt, ...)
{
    char buf[512];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    if (Plat && Plat->log) Plat->log(buf);
    else fprintf(stderr, "%s\n", buf);
}

uint32_t Rgba(int r, int g, int b, int a)
{
    if (r < 0) r = 0; if (r > 255) r = 255;
    if (g < 0) g = 0; if (g > 255) g = 255;
    if (b < 0) b = 0; if (b > 255) b = 255;
    if (a < 0) a = 0; if (a > 255) a = 255;
    return ((uint32_t)a << 24) | ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b;
}

uint32_t Token(uint32_t rgb, int alpha)
{
    return Rgba((int)((rgb >> 16) & 255u), (int)((rgb >> 8) & 255u), (int)(rgb & 255u), alpha);
}

uint32_t Tokenf(uint32_t rgb, float fade)
{
    return Token(rgb, (int)(Clampf(fade, 0.f, 1.f) * 255.f + 0.5f));
}

float Clampf(float v, float lo, float hi)
{
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

float Lerpf(float a, float b, float t)
{
    return a + (b - a) * t;
}

float Smoothstep(float t)
{
    t = Clampf(t, 0.f, 1.f);
    return t * t * (3.f - 2.f * t);
}

float EaseApple(float t)
{
    float u;
    int i;
    t = Clampf(t, 0.f, 1.f);
    u = t;
    for (i = 0; i < 8; i++) {
        float omt = 1.f - u;
        float x = 3.f * omt * omt * u * 0.28f + 3.f * omt * u * u * 0.32f + u * u * u;
        float dx = 3.f * omt * omt * 0.28f + 6.f * omt * u * (0.32f - 0.28f) + 3.f * u * u * (1.f - 0.32f);
        if (fabsf(dx) < 1e-6f) break;
        u = Clampf(u - (x - t) / dx, 0.f, 1.f);
    }
    {
        float omt = 1.f - u;
        return 3.f * omt * omt * u * 0.11f + 3.f * omt * u * u * 1.f + u * u * u;
    }
}

uint32_t Blend(uint32_t dst, uint32_t src)
{
    unsigned sa = (src >> 24) & 255u;
    if (sa == 0) return dst;
    if (sa == 255) return src;
    unsigned da = (dst >> 24) & 255u;
    unsigned inv = 255u - sa;
    unsigned r = (((src >> 16) & 255u) * sa + ((dst >> 16) & 255u) * inv + 127u) / 255u;
    unsigned g = (((src >> 8) & 255u) * sa + ((dst >> 8) & 255u) * inv + 127u) / 255u;
    unsigned b = ((src & 255u) * sa + (dst & 255u) * inv + 127u) / 255u;
    unsigned a = sa + (da * inv + 127u) / 255u;
    if (a > 255) a = 255;
    return (a << 24) | (r << 16) | (g << 8) | b;
}
