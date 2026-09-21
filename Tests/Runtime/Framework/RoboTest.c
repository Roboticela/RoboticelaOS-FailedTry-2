/*
 * RoboTest framework implementation (Goal 1.5).
 */

#include "RoboTest.h"

#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

/*
 * Static table size for the registry. Raised from 1536 in Phase 9.2, when the
 * RoboCSS tokenizer suites took the registry past it, and from 2048 in Phase
 * 10.1, when the RoboLayout suites took it past that; `TestRegister`
 * refuses past the end rather than overflowing, so a full table loses tests
 * silently apart from a line on stderr. Keep this comfortably ahead of the
 * count the suites actually register -- and note that the refusal is *silent* to
 * a run that does not read stderr, which is why BUILD reports registrations that
 * fail rather than only a run that fails.
 */
#define RoboTestMax 4096
#define RoboTestNameMax 96

typedef struct TestCase {
    char name[RoboTestNameMax];
    TestFn fn;
} TestCase;

static TestCase gTests[RoboTestMax];
static int gTestCount;
static int gCurrentFailures;
static int gVerbose;

static void CopyName(char *dst, const char *src, size_t n)
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

void TestRegister(const char *name, TestFn fn)
{
    if (!name || !fn || gTestCount >= RoboTestMax) {
        fprintf(stderr, "robo_test_register failed\n");
        return;
    }
    CopyName(gTests[gTestCount].name, name, RoboTestNameMax);
    gTests[gTestCount].fn = fn;
    gTestCount++;
}

int TestFailureCount(void)
{
    return gCurrentFailures;
}

void TestFail(const char *file, int line, const char *fmt, ...)
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
    gCurrentFailures++;
}

void TestCheck(int cond, const char *expr, const char *file, int line)
{
    if (!cond) {
        TestFail(file, line, "check failed: %s", expr ? expr : "?");
    }
}

static int NameMatches(const char *name, const char *filter)
{
    if (!filter || !filter[0]) {
        return 1;
    }
    return strstr(name, filter) != NULL;
}

int TestMain(int argc, char **argv)
{
    const char *filter = NULL;
    int listOnly = 0;
    int i;
    int ran = 0;
    int passed = 0;
    int failed = 0;

    gVerbose = 0;
    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--verbose") == 0 || strcmp(argv[i], "-v") == 0) {
            gVerbose = 1;
        } else if (strcmp(argv[i], "--list") == 0) {
            listOnly = 1;
        } else if (strncmp(argv[i], "--filter=", 9) == 0) {
            filter = argv[i] + 9;
        } else if (strcmp(argv[i], "--filter") == 0 && i + 1 < argc) {
            filter = argv[++i];
        }
    }

    printf("Roboticela Runtime Tests\n\n");

    if (listOnly) {
        for (i = 0; i < gTestCount; i++) {
            if (NameMatches(gTests[i].name, filter)) {
                printf("%s\n", gTests[i].name);
            }
        }
        return 0;
    }

    for (i = 0; i < gTestCount; i++) {
        if (!NameMatches(gTests[i].name, filter)) {
            continue;
        }
        ran++;
        gCurrentFailures = 0;
        if (gVerbose) {
            printf("RUN  %s\n", gTests[i].name);
        }
        gTests[i].fn();
        if (gCurrentFailures == 0) {
            printf("[PASS] %s\n", gTests[i].name);
            passed++;
        } else {
            printf("[FAIL] %s (%d checks)\n", gTests[i].name,
                   gCurrentFailures);
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
