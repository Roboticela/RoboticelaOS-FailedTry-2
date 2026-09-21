#ifndef ROBO_ARCH_TSC_H
#define ROBO_ARCH_TSC_H

#include <stdint.h>

struct TscInfo {
    int present;
    int rdtscp;
    int invariant;
    uint64_t hz;      /* 0 if unknown */
    int cpuid15;     /* frequency from CPUID leaf 0x15 */
};

void TscProbe(struct TscInfo *out);
uint64_t X86TscRead(void);
uint64_t X86TscFrequency(const struct TscInfo *info);

#endif
