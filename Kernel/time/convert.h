#ifndef ROBO_TIME_CONVERT_H
#define ROBO_TIME_CONVERT_H

#include <stdint.h>

#define RoboNsPerSec  1000000000ull
#define RoboNsPerMs   1000000ull
#define RoboNsPerUs   1000ull

/*
 * Overflow-safe integer conversions. Results saturate at UINT64_MAX.
 * hz == 0 yields 0.
 */
uint64_t TicksToNs(uint64_t ticks, uint64_t hz);
uint64_t NsToTicks(uint64_t ns, uint64_t hz);

/* Saturating now + duration. */
uint64_t TimeDeadline(uint64_t nowNs, uint64_t durationNs);

#endif
