/*
 * RoboArray unit suite (Goal 3.1).
 */

#include "RoboContainers/RoboArray.h"
#include "RoboMem/RoboMemDebug.h"
#include "RoboTest.h"

#include <stdint.h>
#include <string.h>

static void TestArrayInitDestroy(void)
{
    Array a;

    RoboCheckEqInt(ArrayInit(NULL, sizeof(int)), RoboInvalidArgument);
    RoboCheckEqInt(ArrayInit(&a, 0), RoboInvalidArgument);

    RoboCheckEqInt(ArrayInit(&a, sizeof(int)), RoboOk);
    RoboCheckEqInt(ArraySize(&a), 0);
    RoboCheckEqInt(ArrayCapacity(&a), 0);
    RoboCheckEqInt(ArrayElementSize(&a), (int)sizeof(int));
    RoboCheck(ArrayEmpty(&a));
    RoboCheck(ArrayData(&a) == NULL);

    ArrayDestroy(&a);
    ArrayDestroy(NULL);
    RoboCheckEqInt(ArraySize(&a), 0);
    RoboCheckEqInt(ArrayElementSize(&a), 0);
}

static void TestArrayEmpty(void)
{
    Array a;
    int v = 1;

    RoboCheck(ArrayEmpty(NULL));
    RoboCheckEqInt(ArrayInit(&a, sizeof(int)), RoboOk);
    RoboCheck(ArrayEmpty(&a));
    RoboCheckEqInt(ArrayPush(&a, &v), RoboOk);
    RoboCheck(!ArrayEmpty(&a));
    ArrayClear(&a);
    RoboCheck(ArrayEmpty(&a));
    ArrayDestroy(&a);
}

static void TestArrayPushOne(void)
{
    Array a;
    int v = 42;
    int *p;

    RoboCheckEqInt(ArrayInit(&a, sizeof(int)), RoboOk);
    RoboCheckEqInt(ArrayPush(&a, &v), RoboOk);
    RoboCheckEqInt(ArraySize(&a), 1);
    RoboCheck(ArrayCapacity(&a) >= 1);
    p = (int *)ArrayGet(&a, 0);
    RoboCheck(p != NULL);
    RoboCheckEqInt(*p, 42);
    ArrayDestroy(&a);
}

static void TestArrayPushManyGrowth(void)
{
    Array a;
    size_t i;
    int v;
    size_t prevCap;

    RoboCheckEqInt(ArrayInit(&a, sizeof(int)), RoboOk);
    prevCap = 0;
    for (i = 0; i < 100; i++) {
        v = (int)i;
        RoboCheckEqInt(ArrayPush(&a, &v), RoboOk);
        RoboCheckEqInt(ArraySize(&a), (int)(i + 1));
        RoboCheck(ArrayCapacity(&a) >= ArraySize(&a));
        if (ArrayCapacity(&a) > prevCap && prevCap > 0) {
            /* Growth happened at least once across the loop. */
        }
        prevCap = ArrayCapacity(&a);
    }
    RoboCheck(ArrayCapacity(&a) >= 100);
    for (i = 0; i < 100; i++) {
        RoboCheckEqInt(*(int *)ArrayGet(&a, i), (int)i);
    }
    ArrayDestroy(&a);
}

static void TestArrayReserve(void)
{
    Array a;
    int v = 7;

    RoboCheckEqInt(ArrayInit(&a, sizeof(int)), RoboOk);
    RoboCheckEqInt(ArrayReserve(&a, 64), RoboOk);
    RoboCheckEqInt(ArrayCapacity(&a), 64);
    RoboCheckEqInt(ArraySize(&a), 0);
    RoboCheck(ArrayData(&a) != NULL);

    RoboCheckEqInt(ArrayPush(&a, &v), RoboOk);
    RoboCheckEqInt(ArrayCapacity(&a), 64); /* no growth needed */

    RoboCheckEqInt(ArrayReserve(&a, 32), RoboOk); /* no shrink */
    RoboCheckEqInt(ArrayCapacity(&a), 64);

    RoboCheckEqInt(ArrayReserve(NULL, 8), RoboInvalidArgument);
    ArrayDestroy(&a);
}

static void TestArrayGetSet(void)
{
    Array a;
    int v = 1;
    int w = 99;

    RoboCheckEqInt(ArrayInit(&a, sizeof(int)), RoboOk);
    RoboCheck(ArrayGet(&a, 0) == NULL);
    RoboCheckEqInt(ArraySet(&a, 0, &w), RoboInvalidArgument);

    RoboCheckEqInt(ArrayPush(&a, &v), RoboOk);
    RoboCheckEqInt(ArrayPush(&a, &v), RoboOk);
    RoboCheckEqInt(ArraySet(&a, 1, &w), RoboOk);
    RoboCheckEqInt(*(int *)ArrayGet(&a, 1), 99);
    RoboCheckEqInt(ArraySet(&a, 5, &w), RoboInvalidArgument);
    RoboCheckEqInt(ArraySet(&a, 0, NULL), RoboInvalidArgument);
    RoboCheck(ArrayGet(NULL, 0) == NULL);
    ArrayDestroy(&a);
}

static void TestArrayPopClear(void)
{
    Array a;
    int v;
    int out;
    size_t cap;

    RoboCheckEqInt(ArrayInit(&a, sizeof(int)), RoboOk);
    RoboCheckEqInt(ArrayPop(&a, &out), RoboNotFound);

    v = 10;
    RoboCheckEqInt(ArrayPush(&a, &v), RoboOk);
    v = 20;
    RoboCheckEqInt(ArrayPush(&a, &v), RoboOk);
    cap = ArrayCapacity(&a);

    RoboCheckEqInt(ArrayPop(&a, &out), RoboOk);
    RoboCheckEqInt(out, 20);
    RoboCheckEqInt(ArraySize(&a), 1);
    RoboCheckEqInt(ArrayPop(&a, NULL), RoboOk);
    RoboCheck(ArrayEmpty(&a));

    v = 30;
    RoboCheckEqInt(ArrayPush(&a, &v), RoboOk);
    ArrayClear(&a);
    ArrayClear(NULL);
    RoboCheck(ArrayEmpty(&a));
    RoboCheckEqInt(ArrayCapacity(&a), (int)cap); /* capacity retained */
    ArrayDestroy(&a);
}

static void TestArrayBoundsErrors(void)
{
    Array a;
    int v = 1;

    RoboCheckEqInt(ArrayPush(NULL, &v), RoboInvalidArgument);
    RoboCheckEqInt(ArrayInit(&a, sizeof(int)), RoboOk);
    RoboCheckEqInt(ArrayPush(&a, NULL), RoboInvalidArgument);
    RoboCheckEqInt(ArrayPop(NULL, NULL), RoboInvalidArgument);
    RoboCheckEqInt(ArrayReserve(&a, 0), RoboOk);
    ArrayDestroy(&a);
}

static void TestArrayOom(void)
{
    Array a;
    int v = 1;

    RoboCheckEqInt(ArrayInit(&a, sizeof(int)), RoboOk);
    MemDebugFailNext(1);
    RoboCheckEqInt(ArrayPush(&a, &v), RoboOutOfMemory);
    RoboCheckEqInt(ArraySize(&a), 0);

    MemDebugFailNext(1);
    RoboCheckEqInt(ArrayReserve(&a, 16), RoboOutOfMemory);
    RoboCheckEqInt(ArrayCapacity(&a), 0);

    MemDebugFailNext(0);
    RoboCheckEqInt(ArrayPush(&a, &v), RoboOk);
    ArrayDestroy(&a);
}

static void TestArrayOverflowProtection(void)
{
    Array a;
    /* elementSize that makes large capacities overflow size_t */
    size_t huge = (SIZE_MAX / 2u) + 1u;

    RoboCheckEqInt(ArrayInit(&a, huge), RoboOk);
    /* reserving 3 elements would overflow 3 * huge */
    RoboCheckEqInt(ArrayReserve(&a, 3), RoboOutOfMemory);
    RoboCheckEqInt(ArrayCapacity(&a), 0);
    ArrayDestroy(&a);
}

static void TestArrayPointersAndBytes(void)
{
    Array ptrs;
    Array bytes;
    const char *s = "dom";
    const char *got;
    unsigned char b = 0xAB;

    RoboCheckEqInt(ArrayInit(&ptrs, sizeof(const char *)), RoboOk);
    RoboCheckEqInt(ArrayPush(&ptrs, &s), RoboOk);
    got = *(const char *const *)ArrayGetConst(&ptrs, 0);
    RoboCheckStrEq(got, "dom");
    ArrayDestroy(&ptrs);

    RoboCheckEqInt(ArrayInit(&bytes, 1), RoboOk);
    RoboCheckEqInt(ArrayPush(&bytes, &b), RoboOk);
    RoboCheckEqInt(*(unsigned char *)ArrayData(&bytes), 0xAB);
    ArrayDestroy(&bytes);
}

void TestRegisterArray(void)
{
    TestRegister("array.init_destroy", TestArrayInitDestroy);
    TestRegister("array.empty", TestArrayEmpty);
    TestRegister("array.push_one", TestArrayPushOne);
    TestRegister("array.push_many_growth", TestArrayPushManyGrowth);
    TestRegister("array.reserve", TestArrayReserve);
    TestRegister("array.get_set", TestArrayGetSet);
    TestRegister("array.pop_clear", TestArrayPopClear);
    TestRegister("array.bounds_errors", TestArrayBoundsErrors);
    TestRegister("array.oom", TestArrayOom);
    TestRegister("array.overflow", TestArrayOverflowProtection);
    TestRegister("array.pointers_bytes", TestArrayPointersAndBytes);
}
