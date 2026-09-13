/*
 * RoboArena suite (Goal 2.2).
 */

#include "RoboMem/RoboArena.h"
#include "RoboMem/RoboMem.h"
#include "RoboMem/RoboMemDebug.h"
#include "RoboTest.h"

#include <stdint.h>
#include <string.h>

static void test_arena_create_destroy(void)
{
    robo_arena_t *a;

    ROBO_CHECK(robo_arena_create(0) == NULL);

    a = robo_arena_create(256);
    ROBO_CHECK(a != NULL);
    ROBO_CHECK(robo_arena_bytes_remaining(a) == 256);
    robo_arena_destroy(a);
    robo_arena_destroy(NULL);
}

static void test_arena_basic_and_multiple(void)
{
    robo_arena_t *a = robo_arena_create(128);
    char *p1;
    char *p2;
    char *p3;

    ROBO_CHECK(a != NULL);
    p1 = (char *)robo_arena_alloc(a, 16);
    p2 = (char *)robo_arena_alloc(a, 16);
    p3 = (char *)robo_arena_alloc(a, 16);
    ROBO_CHECK(p1 && p2 && p3);
    ROBO_CHECK(p1 != p2 && p2 != p3 && p1 != p3);

    /* Non-overlapping: write distinct markers */
    robo_mem_set(p1, 0x11, 16);
    robo_mem_set(p2, 0x22, 16);
    robo_mem_set(p3, 0x33, 16);
    ROBO_CHECK_EQ_INT((unsigned char)p1[0], 0x11);
    ROBO_CHECK_EQ_INT((unsigned char)p2[0], 0x22);
    ROBO_CHECK_EQ_INT((unsigned char)p3[0], 0x33);
    ROBO_CHECK_EQ_INT((unsigned char)p1[15], 0x11);
    ROBO_CHECK_EQ_INT((unsigned char)p2[15], 0x22);

    /* Ordered bump: later blocks at higher addresses */
    ROBO_CHECK((uintptr_t)p2 >= (uintptr_t)p1 + 16);
    ROBO_CHECK((uintptr_t)p3 >= (uintptr_t)p2 + 16);

    robo_arena_destroy(a);
}

static void test_arena_alignment(void)
{
    robo_arena_t *a = robo_arena_create(256);
    void *p;
    uintptr_t addr;

    ROBO_CHECK(a != NULL);

    /* Force odd offset then align */
    ROBO_CHECK(robo_arena_alloc_aligned(a, 1, 1) != NULL);

    p = robo_arena_alloc_aligned(a, 8, 16);
    ROBO_CHECK(p != NULL);
    addr = (uintptr_t)p;
    ROBO_CHECK((addr % 16) == 0);

    p = robo_arena_alloc_aligned(a, 4, 1);
    ROBO_CHECK(p != NULL);

    p = robo_arena_alloc_aligned(a, 8, 8);
    ROBO_CHECK(p != NULL);
    ROBO_CHECK(((uintptr_t)p % 8) == 0);

    /* Invalid alignment */
    ROBO_CHECK(robo_arena_alloc_aligned(a, 8, 0) == NULL);
    ROBO_CHECK(robo_arena_alloc_aligned(a, 8, 3) == NULL);
    ROBO_CHECK(robo_arena_alloc_aligned(a, 8, 6) == NULL);

    robo_arena_destroy(a);
}

static void test_arena_exhaustion_and_zero(void)
{
    robo_arena_t *a = robo_arena_create(32);
    void *p;

    ROBO_CHECK(a != NULL);
    ROBO_CHECK(robo_arena_alloc(NULL, 8) == NULL);
    ROBO_CHECK(robo_arena_alloc(a, 0) == NULL);
    ROBO_CHECK(robo_arena_alloc_aligned(a, 0, 8) == NULL);

    p = robo_arena_alloc(a, 24);
    ROBO_CHECK(p != NULL);
    /* Not enough room for another default-aligned 24-byte block */
    ROBO_CHECK(robo_arena_alloc(a, 24) == NULL);
    ROBO_CHECK(robo_arena_alloc(a, 1000) == NULL);

    robo_arena_destroy(a);
}

static void test_arena_reset_reuse(void)
{
    robo_arena_t *a = robo_arena_create(64);
    char *p1;
    char *p2;

    ROBO_CHECK(a != NULL);
    p1 = (char *)robo_arena_alloc(a, 32);
    ROBO_CHECK(p1 != NULL);
    robo_mem_set(p1, 0xAA, 32);

    robo_arena_reset(a);
    robo_arena_reset(a); /* multiple resets */
    robo_arena_reset(NULL);

    ROBO_CHECK(robo_arena_bytes_remaining(a) == 64);
    p2 = (char *)robo_arena_alloc(a, 32);
    ROBO_CHECK(p2 != NULL);
    /* After reset, bump restarts; same region may be reused */
    ROBO_CHECK(p2 == p1);
    robo_mem_set(p2, 0xBB, 32);
    ROBO_CHECK_EQ_INT((unsigned char)p2[0], 0xBB);

    robo_arena_destroy(a);
}

static void test_arena_oom_create(void)
{
    robo_arena_t *a;

    robo_mem_debug_fail_next(1);
    a = robo_arena_create(128);
    ROBO_CHECK(a == NULL);
    robo_mem_debug_fail_next(0);
}

void robo_test_register_arena(void)
{
    robo_test_register("arena.create_destroy", test_arena_create_destroy);
    robo_test_register("arena.basic_multiple", test_arena_basic_and_multiple);
    robo_test_register("arena.alignment", test_arena_alignment);
    robo_test_register("arena.exhaustion_zero", test_arena_exhaustion_and_zero);
    robo_test_register("arena.reset_reuse", test_arena_reset_reuse);
    robo_test_register("arena.create_oom", test_arena_oom_create);
}
