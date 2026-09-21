/*
 * Unified RoboMem subsystem suite (Goal 2.5).
 *
 * Integration / coherence checks across heap, arena, pool, and debug.
 * Component-level cases remain in SuiteRoboMem / Arena / Pool / MemDebug.
 */

#include "RoboMem/RoboMemAll.h"
#include "RoboTest.h"

#include <stdint.h>
#include <string.h>

typedef struct faultCapture {
    int count;
    char kind[64];
} FaultCapture;

static void FaultCaptureHandler(const char *kind, const char *message, void *userdata)
{
    FaultCapture *c = (FaultCapture *)userdata;
    size_t n;
    (void)message;
    c->count++;
    if (kind) {
        n = strlen(kind);
        if (n >= sizeof(c->kind)) {
            n = sizeof(c->kind) - 1;
        }
        memcpy(c->kind, kind, n);
        c->kind[n] = '\0';
    }
}

static void TestMemsysHeapArenaPool(void)
{
    void *heap;
    Arena *arena;
    Pool *pool;
    void *a1;
    void *a2;
    void *p1;
    void *p2;

    heap = Malloc(64);
    RoboCheck(heap != NULL);
    MemSet(heap, 0x7E, 64);

    arena = ArenaCreate(128);
    RoboCheck(arena != NULL);
    a1 = ArenaAlloc(arena, 32);
    a2 = ArenaAllocAligned(arena, 16, 16);
    RoboCheck(a1 && a2);
    RoboCheck(((uintptr_t)a2 % 16) == 0);
    MemSet(a1, 0x11, 32);
    MemSet(a2, 0x22, 16);

    pool = PoolCreate(24, 4);
    RoboCheck(pool != NULL);
    p1 = PoolAlloc(pool);
    p2 = PoolAlloc(pool);
    RoboCheck(p1 && p2);
    RoboCheckEqInt(PoolAvailable(pool), 2);
    RoboCheckEqInt(PoolFree(pool, p1), RoboOk);
    RoboCheck(PoolAlloc(pool) == p1); /* reuse */

    /* Distinct regions */
    RoboCheck(heap != a1 && heap != p1 && a1 != p1);

    PoolDestroy(pool);
    ArenaDestroy(arena);
    RoboFree(heap);
}

static void TestMemsysArenaExhaustReset(void)
{
    Arena *a = ArenaCreate(40);
    void *p;

    RoboCheck(a != NULL);
    p = ArenaAlloc(a, 32);
    RoboCheck(p != NULL);
    RoboCheck(ArenaAlloc(a, 32) == NULL); /* exhaustion */
    ArenaReset(a);
    RoboCheck(ArenaBytesRemaining(a) == 40);
    p = ArenaAlloc(a, 32);
    RoboCheck(p != NULL);
    ArenaDestroy(a);
}

static void TestMemsysPoolExhaustInvalid(void)
{
    Pool *p = PoolCreate(8, 2);
    void *a;
    void *b;
    void *c;

    RoboCheck(p != NULL);
    a = PoolAlloc(p);
    b = PoolAlloc(p);
    c = PoolAlloc(p);
    RoboCheck(a && b);
    RoboCheck(c == NULL); /* exhaustion */
    RoboCheckEqInt(PoolFree(p, a), RoboOk);
    RoboCheckEqInt(PoolFree(p, a), RoboAlreadyExists);
    RoboCheckEqInt(PoolFree(p, (char *)b + 1), RoboInvalidArgument);
    PoolDestroy(p);
}

static void TestMemsysDebugStatsGuards(void)
{
    FaultCapture cap;
    MemStats s;
    void *p;

    memset(&cap, 0, sizeof(cap));
    MemDebugEnable();
    MemDebugResetStats();
    MemDebugSetFaultHandler(FaultCaptureHandler, &cap);

    p = Malloc(48);
    RoboCheck(p != NULL);
    RoboCheck(((uintptr_t)p % sizeof(void *)) == 0);

#if RoboMemDebug
    RoboCheckEqInt(MemDebugCheck(p), 0);
    MemDebugGetStats(&s);
    RoboCheckEqInt(s.liveCount, 1);
    RoboCheckEqInt(s.liveBytes, 48);
    MemDebugReport();

    MemDebugCorruptGuardForTest(p, 1);
    RoboCheckEqInt(MemDebugCheck(p), 1);
    RoboFree(p);
    RoboCheckEqInt(cap.count, 1);
    RoboCheckStrEq(cap.kind, "guard-corruption");
#else
    MemDebugGetStats(&s);
    RoboCheckEqInt(s.liveCount, 0);
    MemDebugReport();
    RoboFree(p);
#endif

    MemDebugSetFaultHandler(NULL, NULL);
}

static void TestMemsysReallocUtilities(void)
{
    char *p = (char *)Malloc(8);
    unsigned char buf[8];

    RoboCheck(p != NULL);
    RoboMemCopy(p, "hello", 6);
    p = (char *)Realloc(p, 32);
    RoboCheck(p != NULL);
    RoboCheckStrEq(p, "hello");
    RoboFree(p);

    RoboCheck(MemSet(buf, 0x5A, sizeof(buf)) == buf);
    RoboCheckEqInt(buf[0], 0x5A);
    RoboCheckEqInt(MemCompare(buf, buf, sizeof(buf)), 0);
}

void TestRegisterMemSystem(void)
{
    TestRegister("memsys.heap_arena_pool", TestMemsysHeapArenaPool);
    TestRegister("memsys.arena_exhaust_reset", TestMemsysArenaExhaustReset);
    TestRegister("memsys.pool_exhaust_invalid", TestMemsysPoolExhaustInvalid);
    TestRegister("memsys.debug_stats_guards", TestMemsysDebugStatsGuards);
    TestRegister("memsys.realloc_utilities", TestMemsysReallocUtilities);
}
