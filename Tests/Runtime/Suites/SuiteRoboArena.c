/*
 * RoboArena suite (Goal 2.2).
 */

#include "RoboMem/RoboArena.h"
#include "RoboMem/RoboMem.h"
#include "RoboMem/RoboMemDebug.h"
#include "RoboTest.h"

#include <stdint.h>
#include <string.h>

static void TestArenaCreateDestroy(void)
{
    Arena *a;

    RoboCheck(ArenaCreate(0) == NULL);

    a = ArenaCreate(256);
    RoboCheck(a != NULL);
    RoboCheck(ArenaBytesRemaining(a) == 256);
    ArenaDestroy(a);
    ArenaDestroy(NULL);
}

static void TestArenaBasicAndMultiple(void)
{
    Arena *a = ArenaCreate(128);
    char *p1;
    char *p2;
    char *p3;

    RoboCheck(a != NULL);
    p1 = (char *)ArenaAlloc(a, 16);
    p2 = (char *)ArenaAlloc(a, 16);
    p3 = (char *)ArenaAlloc(a, 16);
    RoboCheck(p1 && p2 && p3);
    RoboCheck(p1 != p2 && p2 != p3 && p1 != p3);

    /* Non-overlapping: write distinct markers */
    MemSet(p1, 0x11, 16);
    MemSet(p2, 0x22, 16);
    MemSet(p3, 0x33, 16);
    RoboCheckEqInt((unsigned char)p1[0], 0x11);
    RoboCheckEqInt((unsigned char)p2[0], 0x22);
    RoboCheckEqInt((unsigned char)p3[0], 0x33);
    RoboCheckEqInt((unsigned char)p1[15], 0x11);
    RoboCheckEqInt((unsigned char)p2[15], 0x22);

    /* Ordered bump: later blocks at higher addresses */
    RoboCheck((uintptr_t)p2 >= (uintptr_t)p1 + 16);
    RoboCheck((uintptr_t)p3 >= (uintptr_t)p2 + 16);

    ArenaDestroy(a);
}

static void TestArenaAlignment(void)
{
    Arena *a = ArenaCreate(256);
    void *p;
    uintptr_t addr;

    RoboCheck(a != NULL);

    /* Force odd offset then align */
    RoboCheck(ArenaAllocAligned(a, 1, 1) != NULL);

    p = ArenaAllocAligned(a, 8, 16);
    RoboCheck(p != NULL);
    addr = (uintptr_t)p;
    RoboCheck((addr % 16) == 0);

    p = ArenaAllocAligned(a, 4, 1);
    RoboCheck(p != NULL);

    p = ArenaAllocAligned(a, 8, 8);
    RoboCheck(p != NULL);
    RoboCheck(((uintptr_t)p % 8) == 0);

    /* Invalid alignment */
    RoboCheck(ArenaAllocAligned(a, 8, 0) == NULL);
    RoboCheck(ArenaAllocAligned(a, 8, 3) == NULL);
    RoboCheck(ArenaAllocAligned(a, 8, 6) == NULL);

    ArenaDestroy(a);
}

static void TestArenaExhaustionAndZero(void)
{
    Arena *a = ArenaCreate(32);
    void *p;

    RoboCheck(a != NULL);
    RoboCheck(ArenaAlloc(NULL, 8) == NULL);
    RoboCheck(ArenaAlloc(a, 0) == NULL);
    RoboCheck(ArenaAllocAligned(a, 0, 8) == NULL);

    p = ArenaAlloc(a, 24);
    RoboCheck(p != NULL);
    /* Not enough room for another default-aligned 24-byte block */
    RoboCheck(ArenaAlloc(a, 24) == NULL);
    RoboCheck(ArenaAlloc(a, 1000) == NULL);

    ArenaDestroy(a);
}

static void TestArenaResetReuse(void)
{
    Arena *a = ArenaCreate(64);
    char *p1;
    char *p2;

    RoboCheck(a != NULL);
    p1 = (char *)ArenaAlloc(a, 32);
    RoboCheck(p1 != NULL);
    MemSet(p1, 0xAA, 32);

    ArenaReset(a);
    ArenaReset(a); /* multiple resets */
    ArenaReset(NULL);

    RoboCheck(ArenaBytesRemaining(a) == 64);
    p2 = (char *)ArenaAlloc(a, 32);
    RoboCheck(p2 != NULL);
    /* After reset, bump restarts; same region may be reused */
    RoboCheck(p2 == p1);
    MemSet(p2, 0xBB, 32);
    RoboCheckEqInt((unsigned char)p2[0], 0xBB);

    ArenaDestroy(a);
}

static void TestArenaOomCreate(void)
{
    Arena *a;

    MemDebugFailNext(1);
    a = ArenaCreate(128);
    RoboCheck(a == NULL);
    MemDebugFailNext(0);
}

void TestRegisterArena(void)
{
    TestRegister("arena.create_destroy", TestArenaCreateDestroy);
    TestRegister("arena.basic_multiple", TestArenaBasicAndMultiple);
    TestRegister("arena.alignment", TestArenaAlignment);
    TestRegister("arena.exhaustion_zero", TestArenaExhaustionAndZero);
    TestRegister("arena.reset_reuse", TestArenaResetReuse);
    TestRegister("arena.create_oom", TestArenaOomCreate);
}
