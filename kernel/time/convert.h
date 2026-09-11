#ifndef ROBO_TIME_CONVERT_H
#define ROBO_TIME_CONVERT_H

#include <stdint.h>

#define ROBO_NS_PER_SEC  1000000000ull
#define ROBO_NS_PER_MS   1000000ull
#define ROBO_NS_PER_US   1000ull

/*
 * Overflow-safe integer conversions. Results saturate at UINT64_MAX.
 * hz == 0 yields 0.
 */
uint64_t robo_ticks_to_ns(uint64_t ticks, uint64_t hz);
uint64_t robo_ns_to_ticks(uint64_t ns, uint64_t hz);

/* Saturating now + duration. */
uint64_t robo_time_deadline(uint64_t now_ns, uint64_t duration_ns);

#endif
