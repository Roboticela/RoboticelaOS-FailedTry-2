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

typedef void (*TestFn)(void);

/** Register a named test case (call before TestMain). */
void TestRegister(const char *name, TestFn fn);

/**
 * Parse argv and run registered tests.
 * Options: --filter=<substr>  --verbose  -v  --list
 * Child helpers: --child-panic  --child-assert (handled by caller if needed)
 * @return 0 if all selected tests passed, 1 otherwise.
 */
int TestMain(int argc, char **argv);

/** Current failure count for the active test (0 = ok so far). */
int TestFailureCount(void);

void TestCheck(int cond, const char *expr, const char *file, int line);
void TestFail(const char *file, int line, const char *fmt, ...);

#define RoboCheck(cond)                                                       \
    TestCheck(!!(cond), #cond, __FILE__, __LINE__)

#define RoboCheckMsg(cond, msg)                                              \
    do {                                                                       \
        if (!(cond)) {                                                         \
            TestFail(__FILE__, __LINE__, "%s", (msg));                    \
        }                                                                      \
    } while (0)

#define RoboCheckEqInt(a, b)                                                \
    do {                                                                       \
        long long ra = (long long)(a);                                        \
        long long rb = (long long)(b);                                        \
        if (ra != rb) {                                                      \
            TestFail(__FILE__, __LINE__,                                 \
                           "%s == %s  (%lld != %lld)", #a, #b, ra, rb);      \
        }                                                                      \
    } while (0)

#define RoboCheckStrEq(a, b)                                                \
    do {                                                                       \
        const char *sa = (a);                                                 \
        const char *sb = (b);                                                 \
        if (!sa || !sb || strcmp(sa, sb) != 0) {                           \
            TestFail(__FILE__, __LINE__,                                 \
                           "%s == %s  (\"%s\" != \"%s\")", #a, #b,             \
                           sa ? sa : "(null)", sb ? sb : "(null)");        \
        }                                                                      \
    } while (0)

#ifdef __cplusplus
}
#endif

#endif /* ROBO_TEST_H */
