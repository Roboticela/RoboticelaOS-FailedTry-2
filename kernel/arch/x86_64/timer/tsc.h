#ifndef ROBO_ARCH_TSC_H
#define ROBO_ARCH_TSC_H

#include <stdint.h>

struct robo_tsc_info {
    int present;
    int rdtscp;
    int invariant;
    uint64_t hz;      /* 0 if unknown */
    int cpuid_15;     /* frequency from CPUID leaf 0x15 */
};

void robo_tsc_probe(struct robo_tsc_info *out);
uint64_t robo_x86_tsc_read(void);
uint64_t robo_x86_tsc_frequency(const struct robo_tsc_info *info);

#endif
