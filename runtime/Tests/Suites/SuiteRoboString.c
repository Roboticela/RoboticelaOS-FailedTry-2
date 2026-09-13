/*
 * RoboString unit suite (Goal 3.2).
 */

#include "RoboContainers/RoboString.h"
#include "RoboMem/RoboMemDebug.h"
#include "RoboTest.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

static void test_string_empty(void)
{
    robo_string_t s;

    ROBO_CHECK_EQ_INT(robo_string_init(NULL), ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_string_init(&s), ROBO_OK);
    ROBO_CHECK(robo_string_empty(&s));
    ROBO_CHECK_EQ_INT(robo_string_size(&s), 0);
    ROBO_CHECK(robo_string_data(&s) != NULL);
    ROBO_CHECK_STR_EQ(robo_string_data(&s), "");
    ROBO_CHECK(robo_string_empty(NULL));
    robo_string_destroy(&s);
    robo_string_destroy(NULL);
}

static void test_string_ascii(void)
{
    robo_string_t s;

    ROBO_CHECK_EQ_INT(robo_string_from_cstr(&s, "Hello"), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_string_size(&s), 5);
    ROBO_CHECK_STR_EQ(robo_string_data(&s), "Hello");
    ROBO_CHECK(robo_string_capacity(&s) >= 5);
    robo_string_destroy(&s);
}

static void test_string_utf8_multibyte(void)
{
    robo_string_t s;
    /* "café" in UTF-8: c a f c3 a9 — 5 bytes, 4 characters */
    const char *cafe = "caf\xC3\xA9";

    ROBO_CHECK_EQ_INT(robo_string_from_cstr(&s, cafe), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_string_size(&s), 5);
    ROBO_CHECK_EQ_INT((unsigned char)robo_string_data(&s)[3], 0xC3);
    ROBO_CHECK_EQ_INT((unsigned char)robo_string_data(&s)[4], 0xA9);
    /* Documented: size is bytes, not Unicode scalar/grapheme count. */
    ROBO_CHECK(robo_string_size(&s) != 4);
    robo_string_destroy(&s);
}

static void test_string_append_and_char(void)
{
    robo_string_t s;

    ROBO_CHECK_EQ_INT(robo_string_init(&s), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_string_append_cstr(&s, "ab"), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_string_append_char(&s, 'c'), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_string_append(&s, "de", 2), ROBO_OK);
    ROBO_CHECK_STR_EQ(robo_string_data(&s), "abcde");
    ROBO_CHECK_EQ_INT(robo_string_size(&s), 5);
    robo_string_destroy(&s);
}

static void test_string_set_clear_reserve(void)
{
    robo_string_t s;
    size_t cap;

    ROBO_CHECK_EQ_INT(robo_string_init(&s), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_string_reserve(&s, 64), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_string_capacity(&s), 64);
    ROBO_CHECK_EQ_INT(robo_string_set_cstr(&s, "xyz"), ROBO_OK);
    ROBO_CHECK_STR_EQ(robo_string_data(&s), "xyz");
    ROBO_CHECK_EQ_INT(robo_string_capacity(&s), 64);

    cap = robo_string_capacity(&s);
    robo_string_clear(&s);
    robo_string_clear(NULL);
    ROBO_CHECK(robo_string_empty(&s));
    ROBO_CHECK_STR_EQ(robo_string_data(&s), "");
    ROBO_CHECK_EQ_INT(robo_string_capacity(&s), (int)cap);

    ROBO_CHECK_EQ_INT(robo_string_set(&s, "hi", 2), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_string_set_cstr(&s, NULL), ROBO_OK); /* → empty */
    ROBO_CHECK(robo_string_empty(&s));
    robo_string_destroy(&s);
}

static void test_string_compare(void)
{
    robo_string_t a;
    robo_string_t b;
    robo_string_t c;

    ROBO_CHECK_EQ_INT(robo_string_from_cstr(&a, "abc"), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_string_from_cstr(&b, "abc"), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_string_from_cstr(&c, "abd"), ROBO_OK);

    ROBO_CHECK_EQ_INT(robo_string_compare(&a, &b), 0);
    ROBO_CHECK(robo_string_compare(&a, &c) < 0);
    ROBO_CHECK(robo_string_compare(&c, &a) > 0);
    ROBO_CHECK_EQ_INT(robo_string_compare(NULL, NULL), 0);
    ROBO_CHECK(robo_string_compare(NULL, &a) < 0);

    /* Embedded NUL: size-aware compare */
    ROBO_CHECK_EQ_INT(robo_string_set(&a, "a\0b", 3), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_string_set(&b, "a\0c", 3), ROBO_OK);
    ROBO_CHECK(robo_string_compare(&a, &b) < 0);
    ROBO_CHECK_EQ_INT(robo_string_size(&a), 3);

    robo_string_destroy(&a);
    robo_string_destroy(&b);
    robo_string_destroy(&c);
}

static void test_string_large_growth(void)
{
    robo_string_t s;
    size_t i;
    size_t prev_cap;

    ROBO_CHECK_EQ_INT(robo_string_init(&s), ROBO_OK);
    prev_cap = robo_string_capacity(&s);
    for (i = 0; i < 200; i++) {
        ROBO_CHECK_EQ_INT(robo_string_append_char(&s, (unsigned char)('a' + (i % 26))),
                          ROBO_OK);
    }
    ROBO_CHECK_EQ_INT(robo_string_size(&s), 200);
    ROBO_CHECK(robo_string_capacity(&s) >= 200);
    ROBO_CHECK(robo_string_capacity(&s) > prev_cap);
    ROBO_CHECK_EQ_INT(robo_string_data(&s)[200], 0);
    robo_string_destroy(&s);
}

static void test_string_oom_and_null(void)
{
    robo_string_t s;

    robo_mem_debug_fail_next(1);
    ROBO_CHECK_EQ_INT(robo_string_init(&s), ROBO_OUT_OF_MEMORY);
    robo_mem_debug_fail_next(0);

    ROBO_CHECK_EQ_INT(robo_string_init(&s), ROBO_OK);
    robo_mem_debug_fail_next(1);
    ROBO_CHECK_EQ_INT(robo_string_append_cstr(&s, "grow-me-please"), ROBO_OUT_OF_MEMORY);
    ROBO_CHECK(robo_string_empty(&s));

    robo_mem_debug_fail_next(1);
    ROBO_CHECK_EQ_INT(robo_string_reserve(&s, 128), ROBO_OUT_OF_MEMORY);
    robo_mem_debug_fail_next(0);

    ROBO_CHECK_EQ_INT(robo_string_set(&s, NULL, 1), ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_string_append(&s, NULL, 1), ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_string_append_cstr(&s, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_string_reserve(NULL, 8), ROBO_INVALID_ARGUMENT);
    ROBO_CHECK(robo_string_data(NULL) == NULL);
    robo_string_destroy(&s);

    ROBO_CHECK_EQ_INT(robo_string_from_cstr(&s, NULL), ROBO_OK);
    ROBO_CHECK(robo_string_empty(&s));
    ROBO_CHECK_STR_EQ(robo_string_data(&s), "");
    robo_string_destroy(&s);
}

static void test_string_invalid_utf8_opaque(void)
{
    robo_string_t s;
    /* Lone continuation byte — invalid UTF-8, stored as opaque bytes. */
    const unsigned char bad[] = {0x80, 0x41};

    ROBO_CHECK_EQ_INT(robo_string_init(&s), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_string_set(&s, bad, 2), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_string_size(&s), 2);
    ROBO_CHECK_EQ_INT((unsigned char)robo_string_data(&s)[0], 0x80);
    ROBO_CHECK_EQ_INT((unsigned char)robo_string_data(&s)[1], 0x41);
    ROBO_CHECK_EQ_INT(robo_string_data(&s)[2], 0);
    robo_string_destroy(&s);
}

static void test_string_overflow(void)
{
    robo_string_t s;

    ROBO_CHECK_EQ_INT(robo_string_init(&s), ROBO_OK);
    /* Force overflow path: reserve SIZE_MAX should fail (need SIZE_MAX+1 alloc). */
    ROBO_CHECK_EQ_INT(robo_string_reserve(&s, SIZE_MAX), ROBO_OUT_OF_MEMORY);
    robo_string_destroy(&s);
}

void robo_test_register_string(void)
{
    robo_test_register("string.empty", test_string_empty);
    robo_test_register("string.ascii", test_string_ascii);
    robo_test_register("string.utf8_multibyte", test_string_utf8_multibyte);
    robo_test_register("string.append_char", test_string_append_and_char);
    robo_test_register("string.set_clear_reserve", test_string_set_clear_reserve);
    robo_test_register("string.compare", test_string_compare);
    robo_test_register("string.large_growth", test_string_large_growth);
    robo_test_register("string.oom_null", test_string_oom_and_null);
    robo_test_register("string.invalid_utf8", test_string_invalid_utf8_opaque);
    robo_test_register("string.overflow", test_string_overflow);
}
