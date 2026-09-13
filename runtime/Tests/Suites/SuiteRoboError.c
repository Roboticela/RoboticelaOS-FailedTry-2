/*
 * RoboError suite (Goal 1.5).
 */

#include "RoboBase/RoboError.h"
#include "RoboTest.h"

#include <string.h>

static void test_error_codes_and_strings(void)
{
    const robo_result_t codes[] = {
        ROBO_OK,
        ROBO_ERROR,
        ROBO_INVALID_ARGUMENT,
        ROBO_OUT_OF_MEMORY,
        ROBO_NOT_FOUND,
        ROBO_ALREADY_EXISTS,
        ROBO_PERMISSION_DENIED,
        ROBO_NOT_SUPPORTED,
        ROBO_TIMEOUT,
        ROBO_IO_ERROR,
        ROBO_PARSE_ERROR,
        ROBO_INIT_ERROR
    };
    const int n = (int)(sizeof(codes) / sizeof(codes[0]));
    int i;
    robo_result_t unknown = (robo_result_t)9999;

    ROBO_CHECK(robo_result_is_ok(ROBO_OK));
    ROBO_CHECK(!robo_result_is_error(ROBO_OK));
    ROBO_CHECK_STR_EQ(robo_error_string(ROBO_OK), "Success");
    ROBO_CHECK_EQ_INT(n, (int)(ROBO_RESULT_LAST - ROBO_RESULT_FIRST + 1));

    for (i = 0; i < n; i++) {
        const char *s = robo_error_string(codes[i]);
        ROBO_CHECK(robo_result_is_known(codes[i]));
        ROBO_CHECK(s != NULL && s[0] != '\0');
        if (codes[i] != ROBO_OK) {
            ROBO_CHECK(robo_result_is_error(codes[i]));
            ROBO_CHECK(strcmp(s, "Success") != 0);
        }
    }

    ROBO_CHECK(!robo_result_is_known(unknown));
    ROBO_CHECK(robo_result_is_error(unknown));
    ROBO_CHECK_STR_EQ(robo_error_string(unknown), "Unknown error");

    ROBO_CHECK_EQ_INT(ROBO_ERR_ALREADY_INITIALIZED, ROBO_ALREADY_EXISTS);
    ROBO_CHECK_EQ_INT(ROBO_ERR_NOT_INITIALIZED, ROBO_INIT_ERROR);
    ROBO_CHECK_EQ_INT(ROBO_ERR_INVALID_ARGUMENT, ROBO_INVALID_ARGUMENT);
}

static void test_error_info(void)
{
    robo_error_info_t info;
    robo_result_t st;

    ROBO_CHECK_EQ_INT(
        robo_error_info_set(NULL, ROBO_NOT_FOUND, "t", "m", "f", 1, ROBO_OK),
        ROBO_INVALID_ARGUMENT);
    robo_error_info_reset(NULL);

    robo_error_info_reset(&info);
    ROBO_CHECK_EQ_INT(info.code, ROBO_OK);

    st = robo_error_info_set(&info, ROBO_PARSE_ERROR, "RoboHTML", "bad token",
                             "Parser.c", 42, ROBO_IO_ERROR);
    ROBO_CHECK_EQ_INT(st, ROBO_PARSE_ERROR);
    ROBO_CHECK_EQ_INT(info.code, ROBO_PARSE_ERROR);
    ROBO_CHECK_STR_EQ(info.subsystem, "RoboHTML");
    ROBO_CHECK_STR_EQ(info.message, "bad token");
    ROBO_CHECK_STR_EQ(info.file, "Parser.c");
    ROBO_CHECK_EQ_INT(info.line, 42);
    ROBO_CHECK_EQ_INT(info.underlying, ROBO_IO_ERROR);

    st = robo_error_info_set(&info, ROBO_OK, NULL, NULL, NULL, 0, ROBO_ERROR);
    ROBO_CHECK_EQ_INT(st, ROBO_OK);
    ROBO_CHECK_EQ_INT(info.code, ROBO_OK);
}

void robo_test_register_error(void)
{
    robo_test_register("error.codes_strings", test_error_codes_and_strings);
    robo_test_register("error.info", test_error_info);
}
