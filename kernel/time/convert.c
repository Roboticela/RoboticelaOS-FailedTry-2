#include "time/convert.h"

#include <stdint.h>

uint64_t robo_ticks_to_ns(uint64_t ticks, uint64_t hz)
{
    uint64_t q, r, nsq, nsr;
    if (hz == 0)
        return 0;
    q = ticks / hz;
    r = ticks % hz;
    if (q > UINT64_MAX / ROBO_NS_PER_SEC)
        return UINT64_MAX;
    nsq = q * ROBO_NS_PER_SEC;
    if (r > UINT64_MAX / ROBO_NS_PER_SEC)
        return UINT64_MAX;
    nsr = (r * ROBO_NS_PER_SEC) / hz;
    if (nsq > UINT64_MAX - nsr)
        return UINT64_MAX;
    return nsq + nsr;
}

uint64_t robo_ns_to_ticks(uint64_t ns, uint64_t hz)
{
    uint64_t q, r, tq, tr;
    if (hz == 0)
        return 0;
    q = ns / ROBO_NS_PER_SEC;
    r = ns % ROBO_NS_PER_SEC;
    if (hz != 0 && q > UINT64_MAX / hz)
        return UINT64_MAX;
    tq = q * hz;
    if (r && hz > UINT64_MAX / r)
        return UINT64_MAX;
    tr = (r * hz) / ROBO_NS_PER_SEC;
    if (tq > UINT64_MAX - tr)
        return UINT64_MAX;
    return tq + tr;
}

uint64_t robo_time_deadline(uint64_t now_ns, uint64_t duration_ns)
{
    if (duration_ns > UINT64_MAX - now_ns)
        return UINT64_MAX;
    return now_ns + duration_ns;
}
