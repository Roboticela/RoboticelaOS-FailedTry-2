#include "time/convert.h"

#include <stdio.h>
#include <stdint.h>

static int gPass;
static int gFails;

#define CHECK(cond, msg) do { \
    if (cond) { gPass++; } \
    else { gFails++; fprintf(stderr, "FAIL %s:%d %s\n", __FILE__, __LINE__, msg); } \
} while (0)

int main(void)
{
    uint64_t hz = 2394000000ull;
    uint64_t ns, ticks;

    CHECK(TicksToNs(0, hz) == 0, "ticks 0");
    CHECK(TicksToNs(1, RoboNsPerSec) == 1, "1 tick at 1GHz");
    CHECK(TicksToNs(100, 0) == 0, "hz 0");
    CHECK(NsToTicks(0, hz) == 0, "ns 0");
    CHECK(NsToTicks(RoboNsPerSec, RoboNsPerSec) == RoboNsPerSec, "1s identity");

    ns = TicksToNs(hz, hz);
    CHECK(ns == RoboNsPerSec, "1 second of ticks");

    ns = TicksToNs(hz * 60ull, hz);
    CHECK(ns == 60ull * RoboNsPerSec, "1 minute");

    ns = TicksToNs(hz * 3600ull, hz);
    CHECK(ns == 3600ull * RoboNsPerSec, "1 hour");

    ns = TicksToNs(hz * 86400ull, hz);
    CHECK(ns == 86400ull * RoboNsPerSec, "1 day");

    ticks = NsToTicks(RoboNsPerSec, hz);
    CHECK(ticks == hz, "ns back to ticks");

    CHECK(TicksToNs(UINT64_MAX, 1) == UINT64_MAX, "saturate ticks->ns");
    CHECK(NsToTicks(UINT64_MAX, UINT64_MAX) == UINT64_MAX, "saturate ns->ticks");
    CHECK(TimeDeadline(100, 50) == 150, "deadline add");
    CHECK(TimeDeadline(UINT64_MAX - 5, 10) == UINT64_MAX, "deadline saturate");
    CHECK(TimeDeadline(UINT64_MAX, 0) == UINT64_MAX, "deadline max+0");
    CHECK(TicksToNs(1, hz) <= 1, "sub-ns truncates");

    {
        uint64_t a = 1000, b = 2000, i;
        for (i = 0; i < 10000000ull; i++) {
            uint64_t n = TimeDeadline(a, 1);
            if (n < a) {
                CHECK(0, "deadline monotonic");
                break;
            }
            a = n;
            if (a == UINT64_MAX)
                break;
            (void)b;
        }
        CHECK(1, "10000000 deadline steps");
    }

    printf("Timer host tests  %d passed  %d failed\n", gPass, gFails);
    return gFails ? 1 : 0;
}
