/*
 * RoboTest — lightweight unit-test framework (Goal 1.5).
 *
 * Lives under Tests/ only. Production runtime must never include this header.
 */

#ifndef ROBO_TEST_H
#define ROBO_TEST_H

#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*robo_test_fn)(void);

/** Register a named test case (call before robo_test_main). */
void robo_test_register(const char *name, robo_test_fn fn);

/**
 * Parse argv and run registered tests.
 * Options: --filter=<substr>  --verbose  -v  --list
 * Child helpers: --child-panic  --child-assert (handled by caller if needed)
 * @return 0 if all selected tests passed, 1 otherwise.
 */
int robo_test_main(int argc, char **argv);

/** Current failure count for the active test (0 = ok so far). */
int robo_test_failure_count(void);

void robo_test_check(int cond, const char *expr, const char *file, int line);
void robo_test_fail(const char *file, int line, const char *fmt, ...);

#define ROBO_CHECK(cond)                                                       \
    robo_test_check(!!(cond), #cond, __FILE__, __LINE__)

#define ROBO_CHECK_MSG(cond, msg)                                              \
    do {                                                                       \
        if (!(cond)) {                                                         \
            robo_test_fail(__FILE__, __LINE__, "%s", (msg));                    \
        }                                                                      \
    } while (0)

#define ROBO_CHECK_EQ_INT(a, b)                                                \
    do {                                                                       \
        long long _ra = (long long)(a);                                        \
        long long _rb = (long long)(b);                                        \
        if (_ra != _rb) {                                                      \
            robo_test_fail(__FILE__, __LINE__,                                 \
                           "%s == %s  (%lld != %lld)", #a, #b, _ra, _rb);      \
        }                                                                      \
    } while (0)

#define ROBO_CHECK_STR_EQ(a, b)                                                \
    do {                                                                       \
        const char *_sa = (a);                                                 \
        const char *_sb = (b);                                                 \
        if (!_sa || !_sb || strcmp(_sa, _sb) != 0) {                           \
            robo_test_fail(__FILE__, __LINE__,                                 \
                           "%s == %s  (\"%s\" != \"%s\")", #a, #b,             \
                           _sa ? _sa : "(null)", _sb ? _sb : "(null)");        \
        }                                                                      \
    } while (0)

#ifdef __cplusplus
}
#endif

#endif /* ROBO_TEST_H */
