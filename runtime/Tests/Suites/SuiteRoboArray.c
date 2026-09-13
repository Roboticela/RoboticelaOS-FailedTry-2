/*
 * RoboArray unit suite (Goal 3.1).
 */

#include "RoboContainers/RoboArray.h"
#include "RoboMem/RoboMemDebug.h"
#include "RoboTest.h"

#include <stdint.h>
#include <string.h>

static void test_array_init_destroy(void)
{
    robo_array_t a;

    ROBO_CHECK_EQ_INT(robo_array_init(NULL, sizeof(int)), ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_array_init(&a, 0), ROBO_INVALID_ARGUMENT);

    ROBO_CHECK_EQ_INT(robo_array_init(&a, sizeof(int)), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_array_size(&a), 0);
    ROBO_CHECK_EQ_INT(robo_array_capacity(&a), 0);
    ROBO_CHECK_EQ_INT(robo_array_element_size(&a), (int)sizeof(int));
    ROBO_CHECK(robo_array_empty(&a));
    ROBO_CHECK(robo_array_data(&a) == NULL);

    robo_array_destroy(&a);
    robo_array_destroy(NULL);
    ROBO_CHECK_EQ_INT(robo_array_size(&a), 0);
    ROBO_CHECK_EQ_INT(robo_array_element_size(&a), 0);
}

static void test_array_empty(void)
{
    robo_array_t a;
    int v = 1;

    ROBO_CHECK(robo_array_empty(NULL));
    ROBO_CHECK_EQ_INT(robo_array_init(&a, sizeof(int)), ROBO_OK);
    ROBO_CHECK(robo_array_empty(&a));
    ROBO_CHECK_EQ_INT(robo_array_push(&a, &v), ROBO_OK);
    ROBO_CHECK(!robo_array_empty(&a));
    robo_array_clear(&a);
    ROBO_CHECK(robo_array_empty(&a));
    robo_array_destroy(&a);
}

static void test_array_push_one(void)
{
    robo_array_t a;
    int v = 42;
    int *p;

    ROBO_CHECK_EQ_INT(robo_array_init(&a, sizeof(int)), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_array_push(&a, &v), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_array_size(&a), 1);
    ROBO_CHECK(robo_array_capacity(&a) >= 1);
    p = (int *)robo_array_get(&a, 0);
    ROBO_CHECK(p != NULL);
    ROBO_CHECK_EQ_INT(*p, 42);
    robo_array_destroy(&a);
}

static void test_array_push_many_growth(void)
{
    robo_array_t a;
    size_t i;
    int v;
    size_t prev_cap;

    ROBO_CHECK_EQ_INT(robo_array_init(&a, sizeof(int)), ROBO_OK);
    prev_cap = 0;
    for (i = 0; i < 100; i++) {
        v = (int)i;
        ROBO_CHECK_EQ_INT(robo_array_push(&a, &v), ROBO_OK);
        ROBO_CHECK_EQ_INT(robo_array_size(&a), (int)(i + 1));
        ROBO_CHECK(robo_array_capacity(&a) >= robo_array_size(&a));
        if (robo_array_capacity(&a) > prev_cap && prev_cap > 0) {
            /* Growth happened at least once across the loop. */
        }
        prev_cap = robo_array_capacity(&a);
    }
    ROBO_CHECK(robo_array_capacity(&a) >= 100);
    for (i = 0; i < 100; i++) {
        ROBO_CHECK_EQ_INT(*(int *)robo_array_get(&a, i), (int)i);
    }
    robo_array_destroy(&a);
}

static void test_array_reserve(void)
{
    robo_array_t a;
    int v = 7;

    ROBO_CHECK_EQ_INT(robo_array_init(&a, sizeof(int)), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_array_reserve(&a, 64), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_array_capacity(&a), 64);
    ROBO_CHECK_EQ_INT(robo_array_size(&a), 0);
    ROBO_CHECK(robo_array_data(&a) != NULL);

    ROBO_CHECK_EQ_INT(robo_array_push(&a, &v), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_array_capacity(&a), 64); /* no growth needed */

    ROBO_CHECK_EQ_INT(robo_array_reserve(&a, 32), ROBO_OK); /* no shrink */
    ROBO_CHECK_EQ_INT(robo_array_capacity(&a), 64);

    ROBO_CHECK_EQ_INT(robo_array_reserve(NULL, 8), ROBO_INVALID_ARGUMENT);
    robo_array_destroy(&a);
}

static void test_array_get_set(void)
{
    robo_array_t a;
    int v = 1;
    int w = 99;

    ROBO_CHECK_EQ_INT(robo_array_init(&a, sizeof(int)), ROBO_OK);
    ROBO_CHECK(robo_array_get(&a, 0) == NULL);
    ROBO_CHECK_EQ_INT(robo_array_set(&a, 0, &w), ROBO_INVALID_ARGUMENT);

    ROBO_CHECK_EQ_INT(robo_array_push(&a, &v), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_array_push(&a, &v), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_array_set(&a, 1, &w), ROBO_OK);
    ROBO_CHECK_EQ_INT(*(int *)robo_array_get(&a, 1), 99);
    ROBO_CHECK_EQ_INT(robo_array_set(&a, 5, &w), ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_array_set(&a, 0, NULL), ROBO_INVALID_ARGUMENT);
    ROBO_CHECK(robo_array_get(NULL, 0) == NULL);
    robo_array_destroy(&a);
}

static void test_array_pop_clear(void)
{
    robo_array_t a;
    int v;
    int out;
    size_t cap;

    ROBO_CHECK_EQ_INT(robo_array_init(&a, sizeof(int)), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_array_pop(&a, &out), ROBO_NOT_FOUND);

    v = 10;
    ROBO_CHECK_EQ_INT(robo_array_push(&a, &v), ROBO_OK);
    v = 20;
    ROBO_CHECK_EQ_INT(robo_array_push(&a, &v), ROBO_OK);
    cap = robo_array_capacity(&a);

    ROBO_CHECK_EQ_INT(robo_array_pop(&a, &out), ROBO_OK);
    ROBO_CHECK_EQ_INT(out, 20);
    ROBO_CHECK_EQ_INT(robo_array_size(&a), 1);
    ROBO_CHECK_EQ_INT(robo_array_pop(&a, NULL), ROBO_OK);
    ROBO_CHECK(robo_array_empty(&a));

    v = 30;
    ROBO_CHECK_EQ_INT(robo_array_push(&a, &v), ROBO_OK);
    robo_array_clear(&a);
    robo_array_clear(NULL);
    ROBO_CHECK(robo_array_empty(&a));
    ROBO_CHECK_EQ_INT(robo_array_capacity(&a), (int)cap); /* capacity retained */
    robo_array_destroy(&a);
}

static void test_array_bounds_errors(void)
{
    robo_array_t a;
    int v = 1;

    ROBO_CHECK_EQ_INT(robo_array_push(NULL, &v), ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_array_init(&a, sizeof(int)), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_array_push(&a, NULL), ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_array_pop(NULL, NULL), ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_array_reserve(&a, 0), ROBO_OK);
    robo_array_destroy(&a);
}

static void test_array_oom(void)
{
    robo_array_t a;
    int v = 1;

    ROBO_CHECK_EQ_INT(robo_array_init(&a, sizeof(int)), ROBO_OK);
    robo_mem_debug_fail_next(1);
    ROBO_CHECK_EQ_INT(robo_array_push(&a, &v), ROBO_OUT_OF_MEMORY);
    ROBO_CHECK_EQ_INT(robo_array_size(&a), 0);

    robo_mem_debug_fail_next(1);
    ROBO_CHECK_EQ_INT(robo_array_reserve(&a, 16), ROBO_OUT_OF_MEMORY);
    ROBO_CHECK_EQ_INT(robo_array_capacity(&a), 0);

    robo_mem_debug_fail_next(0);
    ROBO_CHECK_EQ_INT(robo_array_push(&a, &v), ROBO_OK);
    robo_array_destroy(&a);
}

static void test_array_overflow_protection(void)
{
    robo_array_t a;
    /* element_size that makes large capacities overflow size_t */
    size_t huge = (SIZE_MAX / 2u) + 1u;

    ROBO_CHECK_EQ_INT(robo_array_init(&a, huge), ROBO_OK);
    /* reserving 3 elements would overflow 3 * huge */
    ROBO_CHECK_EQ_INT(robo_array_reserve(&a, 3), ROBO_OUT_OF_MEMORY);
    ROBO_CHECK_EQ_INT(robo_array_capacity(&a), 0);
    robo_array_destroy(&a);
}

static void test_array_pointers_and_bytes(void)
{
    robo_array_t ptrs;
    robo_array_t bytes;
    const char *s = "dom";
    const char *got;
    unsigned char b = 0xAB;

    ROBO_CHECK_EQ_INT(robo_array_init(&ptrs, sizeof(const char *)), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_array_push(&ptrs, &s), ROBO_OK);
    got = *(const char *const *)robo_array_get_const(&ptrs, 0);
    ROBO_CHECK_STR_EQ(got, "dom");
    robo_array_destroy(&ptrs);

    ROBO_CHECK_EQ_INT(robo_array_init(&bytes, 1), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_array_push(&bytes, &b), ROBO_OK);
    ROBO_CHECK_EQ_INT(*(unsigned char *)robo_array_data(&bytes), 0xAB);
    robo_array_destroy(&bytes);
}

void robo_test_register_array(void)
{
    robo_test_register("array.init_destroy", test_array_init_destroy);
    robo_test_register("array.empty", test_array_empty);
    robo_test_register("array.push_one", test_array_push_one);
    robo_test_register("array.push_many_growth", test_array_push_many_growth);
    robo_test_register("array.reserve", test_array_reserve);
    robo_test_register("array.get_set", test_array_get_set);
    robo_test_register("array.pop_clear", test_array_pop_clear);
    robo_test_register("array.bounds_errors", test_array_bounds_errors);
    robo_test_register("array.oom", test_array_oom);
    robo_test_register("array.overflow", test_array_overflow_protection);
    robo_test_register("array.pointers_bytes", test_array_pointers_and_bytes);
}
