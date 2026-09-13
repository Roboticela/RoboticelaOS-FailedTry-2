/*
 * RoboMem unit suite (Goal 2.1).
 */

#include "RoboMem/RoboMem.h"
#include "RoboMem/RoboMemDebug.h"
#include "RoboTest.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

static void test_mem_basic_alloc_free(void)
{
    void *p = robo_malloc(64);
    ROBO_CHECK(p != NULL);
    robo_mem_set(p, 0xAB, 64);
    ROBO_CHECK_EQ_INT(((unsigned char *)p)[0], 0xAB);
    ROBO_CHECK_EQ_INT(((unsigned char *)p)[63], 0xAB);
    robo_free(p);
    robo_free(NULL); /* no-op */
}

static void test_mem_calloc_zeroed(void)
{
    size_t i;
    unsigned char *p = (unsigned char *)robo_calloc(32, 1);
    ROBO_CHECK(p != NULL);
    for (i = 0; i < 32; i++) {
        ROBO_CHECK_EQ_INT(p[i], 0);
    }
    robo_free(p);
}

static void test_mem_realloc_grow_shrink(void)
{
    char *p = (char *)robo_malloc(8);
    ROBO_CHECK(p != NULL);
    robo_mem_copy(p, "abcdefg", 8); /* includes NUL */

    p = (char *)robo_realloc(p, 16);
    ROBO_CHECK(p != NULL);
    ROBO_CHECK_STR_EQ(p, "abcdefg");
    p[8] = 'X';
    p[9] = '\0';

    p = (char *)robo_realloc(p, 4);
    ROBO_CHECK(p != NULL);
    ROBO_CHECK_EQ_INT(p[0], 'a');
    ROBO_CHECK_EQ_INT(p[1], 'b');
    ROBO_CHECK_EQ_INT(p[2], 'c');
    ROBO_CHECK_EQ_INT(p[3], 'd');
    robo_free(p);
}

static void test_mem_realloc_null_and_zero(void)
{
    void *p = robo_realloc(NULL, 16);
    ROBO_CHECK(p != NULL);
    p = robo_realloc(p, 0);
    ROBO_CHECK(p == NULL);
}

static void test_mem_utilities(void)
{
    unsigned char a[16];
    unsigned char b[16];
    unsigned char c[16];

    ROBO_CHECK(robo_mem_set(a, 0x11, sizeof(a)) == a);
    ROBO_CHECK_EQ_INT(a[0], 0x11);
    ROBO_CHECK_EQ_INT(a[15], 0x11);

    ROBO_CHECK(robo_mem_copy(b, a, sizeof(a)) == b);
    ROBO_CHECK_EQ_INT(robo_mem_compare(a, b, sizeof(a)), 0);

    robo_mem_set(c, 0, sizeof(c));
    robo_mem_copy(c, a, 8);
    robo_mem_move(c + 4, c, 8); /* overlapping */
    ROBO_CHECK_EQ_INT(c[4], 0x11);

    ROBO_CHECK(robo_mem_copy(NULL, a, 1) == NULL);
    ROBO_CHECK(robo_mem_move(a, NULL, 1) == NULL);
    ROBO_CHECK(robo_mem_set(NULL, 0, 1) == NULL);
    ROBO_CHECK(robo_mem_copy(a, a, 0) == a);
    ROBO_CHECK_EQ_INT(robo_mem_compare(NULL, NULL, 0), 0);
    ROBO_CHECK(robo_mem_compare(NULL, a, 1) != 0);
}

static void test_mem_multiple(void)
{
    void *blocks[16];
    int i;
    for (i = 0; i < 16; i++) {
        blocks[i] = robo_malloc((size_t)(8 + i));
        ROBO_CHECK(blocks[i] != NULL);
    }
    for (i = 0; i < 16; i++) {
        robo_free(blocks[i]);
    }
}

static void test_mem_zero_size(void)
{
    ROBO_CHECK(robo_malloc(0) == NULL);
    ROBO_CHECK(robo_calloc(0, 8) == NULL);
    ROBO_CHECK(robo_calloc(8, 0) == NULL);
}

static void test_mem_overflow_and_oom(void)
{
    void *p;

    /* Overflow in count*size */
    ROBO_CHECK(robo_calloc(SIZE_MAX, 4) == NULL);
    ROBO_CHECK(robo_calloc(4, SIZE_MAX) == NULL);

    robo_mem_debug_fail_next(2);
    p = robo_malloc(32);
    ROBO_CHECK(p == NULL);
    p = robo_calloc(4, 4);
    ROBO_CHECK(p == NULL);
    /* counter exhausted; normal alloc works again */
    p = robo_malloc(32);
    ROBO_CHECK(p != NULL);
    robo_free(p);

    /* realloc failure leaves original intact */
    p = robo_malloc(8);
    ROBO_CHECK(p != NULL);
    robo_mem_set(p, 0x5A, 8);
    robo_mem_debug_fail_next(1);
    {
        void *q = robo_realloc(p, 64);
        ROBO_CHECK(q == NULL);
        ROBO_CHECK_EQ_INT(((unsigned char *)p)[0], 0x5A);
        robo_free(p);
    }
    robo_mem_debug_fail_next(0);
}

static void test_mem_alignment(void)
{
    void *p = robo_malloc(1);
    uintptr_t addr;
    ROBO_CHECK(p != NULL);
    addr = (uintptr_t)p;
    /* At least pointer alignment; debug path also targets max_align_t. */
    ROBO_CHECK((addr % sizeof(void *)) == 0);
#if defined(__STDC_VERSION__) && __STDC_VERSION__ >= 201112L
    ROBO_CHECK((addr % (size_t)_Alignof(max_align_t)) == 0);
#endif
    robo_free(p);
}

void robo_test_register_mem(void)
{
    robo_test_register("mem.basic_alloc_free", test_mem_basic_alloc_free);
    robo_test_register("mem.calloc_zeroed", test_mem_calloc_zeroed);
    robo_test_register("mem.realloc_grow_shrink", test_mem_realloc_grow_shrink);
    robo_test_register("mem.realloc_null_zero", test_mem_realloc_null_and_zero);
    robo_test_register("mem.utilities", test_mem_utilities);
    robo_test_register("mem.multiple", test_mem_multiple);
    robo_test_register("mem.zero_size", test_mem_zero_size);
    robo_test_register("mem.overflow_oom", test_mem_overflow_and_oom);
    robo_test_register("mem.alignment", test_mem_alignment);
}
