#include "time/convert.h"

#include <stdint.h>

uint64_t TicksToNs(uint64_t ticks, uint64_t hz)
{
    uint64_t q, r, nsq, nsr;
    if (hz == 0)
        return 0;
    q = ticks / hz;
    r = ticks % hz;
    if (q > UINT64_MAX / RoboNsPerSec)
        return UINT64_MAX;
    nsq = q * RoboNsPerSec;
    if (r > UINT64_MAX / RoboNsPerSec)
        return UINT64_MAX;
    nsr = (r * RoboNsPerSec) / hz;
    if (nsq > UINT64_MAX - nsr)
        return UINT64_MAX;
    return nsq + nsr;
}

uint64_t NsToTicks(uint64_t ns, uint64_t hz)
{
    uint64_t q, r, tq, tr;
    if (hz == 0)
        return 0;
    q = ns / RoboNsPerSec;
    r = ns % RoboNsPerSec;
    if (hz != 0 && q > UINT64_MAX / hz)
        return UINT64_MAX;
    tq = q * hz;
    if (r && hz > UINT64_MAX / r)
        return UINT64_MAX;
    tr = (r * hz) / RoboNsPerSec;
    if (tq > UINT64_MAX - tr)
        return UINT64_MAX;
    return tq + tr;
}

uint64_t TimeDeadline(uint64_t nowNs, uint64_t durationNs)
{
    if (durationNs > UINT64_MAX - nowNs)
        return UINT64_MAX;
    return nowNs + durationNs;
}
