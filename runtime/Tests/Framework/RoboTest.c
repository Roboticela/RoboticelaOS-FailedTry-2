/*
 * RoboTest framework implementation (Goal 1.5).
 */

#include "RoboTest.h"

#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

#define ROBO_TEST_MAX 640
#define ROBO_TEST_NAME_MAX 96

typedef struct robo_test_case {
    char name[ROBO_TEST_NAME_MAX];
    robo_test_fn fn;
} robo_test_case_t;

static robo_test_case_t g_tests[ROBO_TEST_MAX];
static int g_test_count;
static int g_current_failures;
static int g_verbose;

static void copy_name(char *dst, const char *src, size_t n)
{
    size_t i;
    if (!dst || n == 0) {
        return;
    }
    if (!src) {
        dst[0] = '\0';
        return;
    }
    for (i = 0; i + 1 < n && src[i]; i++) {
        dst[i] = src[i];
    }
    dst[i] = '\0';
}

void robo_test_register(const char *name, robo_test_fn fn)
{
    if (!name || !fn || g_test_count >= ROBO_TEST_MAX) {
        fprintf(stderr, "robo_test_register failed\n");
        return;
    }
    copy_name(g_tests[g_test_count].name, name, ROBO_TEST_NAME_MAX);
    g_tests[g_test_count].fn = fn;
    g_test_count++;
}

int robo_test_failure_count(void)
{
    return g_current_failures;
}

void robo_test_fail(const char *file, int line, const char *fmt, ...)
{
    va_list ap;
    const char *base = file ? file : "?";
    const char *p;

    for (p = file; file && *p; p++) {
        if (*p == '/' || *p == '\\') {
            base = p + 1;
        }
    }

    fprintf(stderr, "    FAIL %s:%d: ", base, line);
    va_start(ap, fmt);
    vfprintf(stderr, fmt ? fmt : "", ap);
    va_end(ap);
    fputc('\n', stderr);
    g_current_failures++;
}

void robo_test_check(int cond, const char *expr, const char *file, int line)
{
    if (!cond) {
        robo_test_fail(file, line, "check failed: %s", expr ? expr : "?");
    }
}

static int name_matches(const char *name, const char *filter)
{
    if (!filter || !filter[0]) {
        return 1;
    }
    return strstr(name, filter) != NULL;
}

int robo_test_main(int argc, char **argv)
{
    const char *filter = NULL;
    int list_only = 0;
    int i;
    int ran = 0;
    int passed = 0;
    int failed = 0;

    g_verbose = 0;
    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--verbose") == 0 || strcmp(argv[i], "-v") == 0) {
            g_verbose = 1;
        } else if (strcmp(argv[i], "--list") == 0) {
            list_only = 1;
        } else if (strncmp(argv[i], "--filter=", 9) == 0) {
            filter = argv[i] + 9;
        } else if (strcmp(argv[i], "--filter") == 0 && i + 1 < argc) {
            filter = argv[++i];
        }
    }

    printf("Roboticela Runtime Tests\n\n");

    if (list_only) {
        for (i = 0; i < g_test_count; i++) {
            if (name_matches(g_tests[i].name, filter)) {
                printf("%s\n", g_tests[i].name);
            }
        }
        return 0;
    }

    for (i = 0; i < g_test_count; i++) {
        if (!name_matches(g_tests[i].name, filter)) {
            continue;
        }
        ran++;
        g_current_failures = 0;
        if (g_verbose) {
            printf("RUN  %s\n", g_tests[i].name);
        }
        g_tests[i].fn();
        if (g_current_failures == 0) {
            printf("[PASS] %s\n", g_tests[i].name);
            passed++;
        } else {
            printf("[FAIL] %s (%d checks)\n", g_tests[i].name,
                   g_current_failures);
            failed++;
        }
    }

    printf("\nTests: %d\nPassed: %d\nFailed: %d\n\n", ran, passed, failed);
    if (failed == 0 && ran > 0) {
        printf("ALL TESTS PASSED\n");
        return 0;
    }
    if (ran == 0) {
        printf("NO TESTS RAN\n");
        return 1;
    }
    printf("SOME TESTS FAILED\n");
    return 1;
}
