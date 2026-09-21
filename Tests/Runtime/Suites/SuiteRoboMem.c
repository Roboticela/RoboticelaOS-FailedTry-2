/*
 * RoboMem unit suite (Goal 2.1).
 */

#include "RoboMem/RoboMem.h"
#include "RoboMem/RoboMemDebug.h"
#include "RoboTest.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

static void TestMemBasicAllocFree(void)
{
    void *p = Malloc(64);
    RoboCheck(p != NULL);
    MemSet(p, 0xAB, 64);
    RoboCheckEqInt(((unsigned char *)p)[0], 0xAB);
    RoboCheckEqInt(((unsigned char *)p)[63], 0xAB);
    RoboFree(p);
    RoboFree(NULL); /* no-op */
}

static void TestMemCallocZeroed(void)
{
    size_t i;
    unsigned char *p = (unsigned char *)RoboCalloc(32, 1);
    RoboCheck(p != NULL);
    for (i = 0; i < 32; i++) {
        RoboCheckEqInt(p[i], 0);
    }
    RoboFree(p);
}

static void TestMemReallocGrowShrink(void)
{
    char *p = (char *)Malloc(8);
    RoboCheck(p != NULL);
    RoboMemCopy(p, "abcdefg", 8); /* includes NUL */

    p = (char *)Realloc(p, 16);
    RoboCheck(p != NULL);
    RoboCheckStrEq(p, "abcdefg");
    p[8] = 'X';
    p[9] = '\0';

    p = (char *)Realloc(p, 4);
    RoboCheck(p != NULL);
    RoboCheckEqInt(p[0], 'a');
    RoboCheckEqInt(p[1], 'b');
    RoboCheckEqInt(p[2], 'c');
    RoboCheckEqInt(p[3], 'd');
    RoboFree(p);
}

static void TestMemReallocNullAndZero(void)
{
    void *p = Realloc(NULL, 16);
    RoboCheck(p != NULL);
    p = Realloc(p, 0);
    RoboCheck(p == NULL);
}

static void TestMemUtilities(void)
{
    unsigned char a[16];
    unsigned char b[16];
    unsigned char c[16];

    RoboCheck(MemSet(a, 0x11, sizeof(a)) == a);
    RoboCheckEqInt(a[0], 0x11);
    RoboCheckEqInt(a[15], 0x11);

    RoboCheck(RoboMemCopy(b, a, sizeof(a)) == b);
    RoboCheckEqInt(MemCompare(a, b, sizeof(a)), 0);

    MemSet(c, 0, sizeof(c));
    RoboMemCopy(c, a, 8);
    MemMove(c + 4, c, 8); /* overlapping */
    RoboCheckEqInt(c[4], 0x11);

    RoboCheck(RoboMemCopy(NULL, a, 1) == NULL);
    RoboCheck(MemMove(a, NULL, 1) == NULL);
    RoboCheck(MemSet(NULL, 0, 1) == NULL);
    RoboCheck(RoboMemCopy(a, a, 0) == a);
    RoboCheckEqInt(MemCompare(NULL, NULL, 0), 0);
    RoboCheck(MemCompare(NULL, a, 1) != 0);
}

static void TestMemMultiple(void)
{
    void *blocks[16];
    int i;
    for (i = 0; i < 16; i++) {
        blocks[i] = Malloc((size_t)(8 + i));
        RoboCheck(blocks[i] != NULL);
    }
    for (i = 0; i < 16; i++) {
        RoboFree(blocks[i]);
    }
}

static void TestMemZeroSize(void)
{
    RoboCheck(Malloc(0) == NULL);
    RoboCheck(RoboCalloc(0, 8) == NULL);
    RoboCheck(RoboCalloc(8, 0) == NULL);
}

static void TestMemOverflowAndOom(void)
{
    void *p;

    /* Overflow in count*size */
    RoboCheck(RoboCalloc(SIZE_MAX, 4) == NULL);
    RoboCheck(RoboCalloc(4, SIZE_MAX) == NULL);

    MemDebugFailNext(2);
    p = Malloc(32);
    RoboCheck(p == NULL);
    p = RoboCalloc(4, 4);
    RoboCheck(p == NULL);
    /* counter exhausted; normal alloc works again */
    p = Malloc(32);
    RoboCheck(p != NULL);
    RoboFree(p);

    /* realloc failure leaves original intact */
    p = Malloc(8);
    RoboCheck(p != NULL);
    MemSet(p, 0x5A, 8);
    MemDebugFailNext(1);
    {
        void *q = Realloc(p, 64);
        RoboCheck(q == NULL);
        RoboCheckEqInt(((unsigned char *)p)[0], 0x5A);
        RoboFree(p);
    }
    MemDebugFailNext(0);
}

static void TestMemAlignment(void)
{
    void *p = Malloc(1);
    uintptr_t addr;
    RoboCheck(p != NULL);
    addr = (uintptr_t)p;
    /* At least pointer alignment; debug path also targets max_align_t. */
    RoboCheck((addr % sizeof(void *)) == 0);
#if defined(__STDC_VERSION__) && __STDC_VERSION__ >= 201112L
    RoboCheck((addr % (size_t)_Alignof(max_align_t)) == 0);
#endif
    RoboFree(p);
}

void TestRegisterMem(void)
{
    TestRegister("mem.basic_alloc_free", TestMemBasicAllocFree);
    TestRegister("mem.calloc_zeroed", TestMemCallocZeroed);
    TestRegister("mem.realloc_grow_shrink", TestMemReallocGrowShrink);
    TestRegister("mem.realloc_null_zero", TestMemReallocNullAndZero);
    TestRegister("mem.utilities", TestMemUtilities);
    TestRegister("mem.multiple", TestMemMultiple);
    TestRegister("mem.zero_size", TestMemZeroSize);
    TestRegister("mem.overflow_oom", TestMemOverflowAndOom);
    TestRegister("mem.alignment", TestMemAlignment);
}
