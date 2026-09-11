#include "arch/x86_64/timer/tsc.h"

static void cpuid(uint32_t leaf, uint32_t sub,
                  uint32_t *a, uint32_t *b, uint32_t *c, uint32_t *d)
{
    uint32_t aa, bb, cc, dd;
    __asm__ volatile ("cpuid"
                      : "=a"(aa), "=b"(bb), "=c"(cc), "=d"(dd)
                      : "a"(leaf), "c"(sub));
    if (a) *a = aa;
    if (b) *b = bb;
    if (c) *c = cc;
    if (d) *d = dd;
}

static uint64_t rdtsc_raw(void)
{
    uint32_t lo, hi;
    __asm__ volatile ("rdtsc" : "=a"(lo), "=d"(hi));
    return ((uint64_t)hi << 32) | lo;
}

static uint64_t rdtscp_raw(void)
{
    uint32_t lo, hi;
    __asm__ volatile ("rdtscp" : "=a"(lo), "=d"(hi) : : "rcx");
    return ((uint64_t)hi << 32) | lo;
}

static int g_use_rdtscp;

void robo_tsc_probe(struct robo_tsc_info *out)
{
    uint32_t a, b, c, d, max_leaf, max_ext;
    if (!out)
        return;
    out->present = 0;
    out->rdtscp = 0;
    out->invariant = 0;
    out->hz = 0;
    out->cpuid_15 = 0;
    g_use_rdtscp = 0;

    cpuid(0, 0, &max_leaf, 0, 0, 0);
    cpuid(1, 0, &a, &b, &c, &d);
    if ((d & (1u << 4)) == 0)
        return;
    out->present = 1;

    cpuid(0x80000000u, 0, &max_ext, 0, 0, 0);
    if (max_ext >= 0x80000001u) {
        cpuid(0x80000001u, 0, &a, &b, &c, &d);
        if (d & (1u << 27))
            out->rdtscp = 1;
    }
    if (max_ext >= 0x80000007u) {
        cpuid(0x80000007u, 0, &a, &b, &c, &d);
        if (d & (1u << 8))
            out->invariant = 1;
    }

    if (max_leaf >= 0x15u) {
        uint32_t denom, numer, crystal;
        cpuid(0x15u, 0, &denom, &numer, &crystal, 0);
        if (denom && numer && crystal) {
            out->hz = (uint64_t)crystal * (uint64_t)numer / (uint64_t)denom;
            out->cpuid_15 = 1;
        }
    }

    g_use_rdtscp = out->rdtscp;
}

uint64_t robo_x86_tsc_read(void)
{
    if (g_use_rdtscp)
        return rdtscp_raw();
    __asm__ volatile ("lfence" ::: "memory");
    return rdtsc_raw();
}

uint64_t robo_x86_tsc_frequency(const struct robo_tsc_info *info)
{
    if (!info)
        return 0;
    return info->hz;
}
