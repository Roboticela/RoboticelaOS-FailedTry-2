/*
 * RoboPool suite (Goal 2.3).
 */

#include "RoboMem/RoboPool.h"
#include "RoboMem/RoboMem.h"
#include "RoboMem/RoboMemDebug.h"
#include "RoboTest.h"

#include <stdint.h>
#include <string.h>

static void test_pool_create_destroy(void)
{
    robo_pool_t *p;

    ROBO_CHECK(robo_pool_create(0, 8) == NULL);
    ROBO_CHECK(robo_pool_create(16, 0) == NULL);

    p = robo_pool_create(16, 4);
    ROBO_CHECK(p != NULL);
    ROBO_CHECK_EQ_INT(robo_pool_capacity(p), 4);
    ROBO_CHECK_EQ_INT(robo_pool_object_size(p), 16);
    ROBO_CHECK_EQ_INT(robo_pool_available(p), 4);
    robo_pool_destroy(p);
    robo_pool_destroy(NULL);
}

static void test_pool_alloc_free_reuse(void)
{
    robo_pool_t *p = robo_pool_create(8, 3);
    void *a;
    void *b;
    void *c;
    void *d;

    ROBO_CHECK(p != NULL);
    ROBO_CHECK(robo_pool_alloc(NULL) == NULL);

    a = robo_pool_alloc(p);
    b = robo_pool_alloc(p);
    c = robo_pool_alloc(p);
    ROBO_CHECK(a && b && c);
    ROBO_CHECK(a != b && b != c);
    ROBO_CHECK_EQ_INT(robo_pool_available(p), 0);
    d = robo_pool_alloc(p);
    ROBO_CHECK(d == NULL); /* exhausted */

    ROBO_CHECK_EQ_INT(robo_pool_free(p, b), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_pool_available(p), 1);
    d = robo_pool_alloc(p);
    ROBO_CHECK(d == b); /* LIFO reuse */

    ROBO_CHECK_EQ_INT(robo_pool_free(p, a), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_pool_free(p, c), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_pool_free(p, d), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_pool_available(p), 3);

    robo_pool_destroy(p);
}

static void test_pool_alignment_and_small(void)
{
    robo_pool_t *small = robo_pool_create(1, 4); /* smaller than pointer */
    robo_pool_t *pow2 = robo_pool_create(16, 2);
    void *p;
    uintptr_t addr;

    ROBO_CHECK(small != NULL);
    p = robo_pool_alloc(small);
    ROBO_CHECK(p != NULL);
    addr = (uintptr_t)p;
    ROBO_CHECK((addr % sizeof(void *)) == 0);
    robo_pool_destroy(small);

    ROBO_CHECK(pow2 != NULL);
    p = robo_pool_alloc(pow2);
    ROBO_CHECK(p != NULL);
    addr = (uintptr_t)p;
    ROBO_CHECK((addr % 16) == 0);
    robo_pool_destroy(pow2);
}

static void test_pool_invalid_and_double_free(void)
{
    robo_pool_t *p = robo_pool_create(8, 2);
    robo_pool_t *q = robo_pool_create(8, 2);
    void *a;
    void *foreign;
    char stack_obj[8];

    ROBO_CHECK(p && q);
    a = robo_pool_alloc(p);
    foreign = robo_pool_alloc(q);
    ROBO_CHECK(a && foreign);

    ROBO_CHECK_EQ_INT(robo_pool_free(NULL, a), ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_pool_free(p, NULL), ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_pool_free(p, foreign), ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_pool_free(p, stack_obj), ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_pool_free(p, (char *)a + 1), ROBO_INVALID_ARGUMENT);

    ROBO_CHECK_EQ_INT(robo_pool_free(p, a), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_pool_free(p, a), ROBO_ALREADY_EXISTS);

    robo_pool_destroy(p);
    robo_pool_destroy(q);
}

static void test_pool_reset(void)
{
    robo_pool_t *p = robo_pool_create(8, 4);
    void *a;
    void *b;

    ROBO_CHECK(p != NULL);
    a = robo_pool_alloc(p);
    b = robo_pool_alloc(p);
    ROBO_CHECK(a && b);
    ROBO_CHECK_EQ_INT(robo_pool_available(p), 2);

    robo_pool_reset(p);
    robo_pool_reset(NULL);
    ROBO_CHECK_EQ_INT(robo_pool_available(p), 4);

    a = robo_pool_alloc(p);
    ROBO_CHECK(a != NULL);
    robo_pool_destroy(p);
}

static void test_pool_nonoverlap(void)
{
    robo_pool_t *p = robo_pool_create(8, 3);
    unsigned char *a;
    unsigned char *b;
    unsigned char *c;

    ROBO_CHECK(p != NULL);
    a = (unsigned char *)robo_pool_alloc(p);
    b = (unsigned char *)robo_pool_alloc(p);
    c = (unsigned char *)robo_pool_alloc(p);
    ROBO_CHECK(a && b && c);
    robo_mem_set(a, 0x11, 8);
    robo_mem_set(b, 0x22, 8);
    robo_mem_set(c, 0x33, 8);
    ROBO_CHECK_EQ_INT(a[0], 0x11);
    ROBO_CHECK_EQ_INT(b[0], 0x22);
    ROBO_CHECK_EQ_INT(c[0], 0x33);
    ROBO_CHECK_EQ_INT(a[7], 0x11);
    ROBO_CHECK_EQ_INT(b[7], 0x22);
    robo_pool_destroy(p);
}

static void test_pool_oom_create(void)
{
    robo_pool_t *p;

    robo_mem_debug_fail_next(1);
    p = robo_pool_create(16, 8);
    ROBO_CHECK(p == NULL);
    robo_mem_debug_fail_next(0);
}

void robo_test_register_pool(void)
{
    robo_test_register("pool.create_destroy", test_pool_create_destroy);
    robo_test_register("pool.alloc_free_reuse", test_pool_alloc_free_reuse);
    robo_test_register("pool.alignment_small", test_pool_alignment_and_small);
    robo_test_register("pool.invalid_double_free", test_pool_invalid_and_double_free);
    robo_test_register("pool.reset", test_pool_reset);
    robo_test_register("pool.nonoverlap", test_pool_nonoverlap);
    robo_test_register("pool.create_oom", test_pool_oom_create);
}
