/*
 * RoboPool suite (Goal 2.3).
 */

#include "RoboMem/RoboPool.h"
#include "RoboMem/RoboMem.h"
#include "RoboMem/RoboMemDebug.h"
#include "RoboTest.h"

#include <stdint.h>
#include <string.h>

static void TestPoolCreateDestroy(void)
{
    Pool *p;

    RoboCheck(PoolCreate(0, 8) == NULL);
    RoboCheck(PoolCreate(16, 0) == NULL);

    p = PoolCreate(16, 4);
    RoboCheck(p != NULL);
    RoboCheckEqInt(PoolCapacity(p), 4);
    RoboCheckEqInt(PoolObjectSize(p), 16);
    RoboCheckEqInt(PoolAvailable(p), 4);
    PoolDestroy(p);
    PoolDestroy(NULL);
}

static void TestPoolAllocFreeReuse(void)
{
    Pool *p = PoolCreate(8, 3);
    void *a;
    void *b;
    void *c;
    void *d;

    RoboCheck(p != NULL);
    RoboCheck(PoolAlloc(NULL) == NULL);

    a = PoolAlloc(p);
    b = PoolAlloc(p);
    c = PoolAlloc(p);
    RoboCheck(a && b && c);
    RoboCheck(a != b && b != c);
    RoboCheckEqInt(PoolAvailable(p), 0);
    d = PoolAlloc(p);
    RoboCheck(d == NULL); /* exhausted */

    RoboCheckEqInt(PoolFree(p, b), RoboOk);
    RoboCheckEqInt(PoolAvailable(p), 1);
    d = PoolAlloc(p);
    RoboCheck(d == b); /* LIFO reuse */

    RoboCheckEqInt(PoolFree(p, a), RoboOk);
    RoboCheckEqInt(PoolFree(p, c), RoboOk);
    RoboCheckEqInt(PoolFree(p, d), RoboOk);
    RoboCheckEqInt(PoolAvailable(p), 3);

    PoolDestroy(p);
}

static void TestPoolAlignmentAndSmall(void)
{
    Pool *small = PoolCreate(1, 4); /* smaller than pointer */
    Pool *pow2 = PoolCreate(16, 2);
    void *p;
    uintptr_t addr;

    RoboCheck(small != NULL);
    p = PoolAlloc(small);
    RoboCheck(p != NULL);
    addr = (uintptr_t)p;
    RoboCheck((addr % sizeof(void *)) == 0);
    PoolDestroy(small);

    RoboCheck(pow2 != NULL);
    p = PoolAlloc(pow2);
    RoboCheck(p != NULL);
    addr = (uintptr_t)p;
    RoboCheck((addr % 16) == 0);
    PoolDestroy(pow2);
}

static void TestPoolInvalidAndDoubleFree(void)
{
    Pool *p = PoolCreate(8, 2);
    Pool *q = PoolCreate(8, 2);
    void *a;
    void *foreign;
    char stackObj[8];

    RoboCheck(p && q);
    a = PoolAlloc(p);
    foreign = PoolAlloc(q);
    RoboCheck(a && foreign);

    RoboCheckEqInt(PoolFree(NULL, a), RoboInvalidArgument);
    RoboCheckEqInt(PoolFree(p, NULL), RoboInvalidArgument);
    RoboCheckEqInt(PoolFree(p, foreign), RoboInvalidArgument);
    RoboCheckEqInt(PoolFree(p, stackObj), RoboInvalidArgument);
    RoboCheckEqInt(PoolFree(p, (char *)a + 1), RoboInvalidArgument);

    RoboCheckEqInt(PoolFree(p, a), RoboOk);
    RoboCheckEqInt(PoolFree(p, a), RoboAlreadyExists);

    PoolDestroy(p);
    PoolDestroy(q);
}

static void TestPoolReset(void)
{
    Pool *p = PoolCreate(8, 4);
    void *a;
    void *b;

    RoboCheck(p != NULL);
    a = PoolAlloc(p);
    b = PoolAlloc(p);
    RoboCheck(a && b);
    RoboCheckEqInt(PoolAvailable(p), 2);

    PoolReset(p);
    PoolReset(NULL);
    RoboCheckEqInt(PoolAvailable(p), 4);

    a = PoolAlloc(p);
    RoboCheck(a != NULL);
    PoolDestroy(p);
}

static void TestPoolNonoverlap(void)
{
    Pool *p = PoolCreate(8, 3);
    unsigned char *a;
    unsigned char *b;
    unsigned char *c;

    RoboCheck(p != NULL);
    a = (unsigned char *)PoolAlloc(p);
    b = (unsigned char *)PoolAlloc(p);
    c = (unsigned char *)PoolAlloc(p);
    RoboCheck(a && b && c);
    MemSet(a, 0x11, 8);
    MemSet(b, 0x22, 8);
    MemSet(c, 0x33, 8);
    RoboCheckEqInt(a[0], 0x11);
    RoboCheckEqInt(b[0], 0x22);
    RoboCheckEqInt(c[0], 0x33);
    RoboCheckEqInt(a[7], 0x11);
    RoboCheckEqInt(b[7], 0x22);
    PoolDestroy(p);
}

static void TestPoolOomCreate(void)
{
    Pool *p;

    MemDebugFailNext(1);
    p = PoolCreate(16, 8);
    RoboCheck(p == NULL);
    MemDebugFailNext(0);
}

void TestRegisterPool(void)
{
    TestRegister("pool.create_destroy", TestPoolCreateDestroy);
    TestRegister("pool.alloc_free_reuse", TestPoolAllocFreeReuse);
    TestRegister("pool.alignment_small", TestPoolAlignmentAndSmall);
    TestRegister("pool.invalid_double_free", TestPoolInvalidAndDoubleFree);
    TestRegister("pool.reset", TestPoolReset);
    TestRegister("pool.nonoverlap", TestPoolNonoverlap);
    TestRegister("pool.create_oom", TestPoolOomCreate);
}
