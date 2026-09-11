#include "time/convert.h"

#include <stdio.h>
#include <stdint.h>

static int g_pass;
static int g_fails;

#define CHECK(cond, msg) do { \
    if (cond) { g_pass++; } \
    else { g_fails++; fprintf(stderr, "FAIL %s:%d %s\n", __FILE__, __LINE__, msg); } \
} while (0)

int main(void)
{
    uint64_t hz = 2394000000ull;
    uint64_t ns, ticks;

    CHECK(robo_ticks_to_ns(0, hz) == 0, "ticks 0");
    CHECK(robo_ticks_to_ns(1, ROBO_NS_PER_SEC) == 1, "1 tick at 1GHz");
    CHECK(robo_ticks_to_ns(100, 0) == 0, "hz 0");
    CHECK(robo_ns_to_ticks(0, hz) == 0, "ns 0");
    CHECK(robo_ns_to_ticks(ROBO_NS_PER_SEC, ROBO_NS_PER_SEC) == ROBO_NS_PER_SEC, "1s identity");

    ns = robo_ticks_to_ns(hz, hz);
    CHECK(ns == ROBO_NS_PER_SEC, "1 second of ticks");

    ns = robo_ticks_to_ns(hz * 60ull, hz);
    CHECK(ns == 60ull * ROBO_NS_PER_SEC, "1 minute");

    ns = robo_ticks_to_ns(hz * 3600ull, hz);
    CHECK(ns == 3600ull * ROBO_NS_PER_SEC, "1 hour");

    ns = robo_ticks_to_ns(hz * 86400ull, hz);
    CHECK(ns == 86400ull * ROBO_NS_PER_SEC, "1 day");

    ticks = robo_ns_to_ticks(ROBO_NS_PER_SEC, hz);
    CHECK(ticks == hz, "ns back to ticks");

    CHECK(robo_ticks_to_ns(UINT64_MAX, 1) == UINT64_MAX, "saturate ticks->ns");
    CHECK(robo_ns_to_ticks(UINT64_MAX, UINT64_MAX) == UINT64_MAX, "saturate ns->ticks");
    CHECK(robo_time_deadline(100, 50) == 150, "deadline add");
    CHECK(robo_time_deadline(UINT64_MAX - 5, 10) == UINT64_MAX, "deadline saturate");
    CHECK(robo_time_deadline(UINT64_MAX, 0) == UINT64_MAX, "deadline max+0");
    CHECK(robo_ticks_to_ns(1, hz) <= 1, "sub-ns truncates");

    {
        uint64_t a = 1000, b = 2000, i;
        for (i = 0; i < 10000000ull; i++) {
            uint64_t n = robo_time_deadline(a, 1);
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

    printf("Timer host tests  %d passed  %d failed\n", g_pass, g_fails);
    return g_fails ? 1 : 0;
}
