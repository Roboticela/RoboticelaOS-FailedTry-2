/*
 * RoboMem debug allocator suite (Goal 2.4).
 */

#include "RoboMem/RoboMem.h"
#include "RoboMem/RoboMemDebug.h"
#include "RoboTest.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct fault_capture {
    int count;
    char kind[64];
    char message[256];
} fault_capture_t;

static void fault_capture_handler(const char *kind, const char *message, void *userdata)
{
    fault_capture_t *c = (fault_capture_t *)userdata;
    size_t n;
    c->count++;
    if (kind) {
        n = strlen(kind);
        if (n >= sizeof(c->kind)) {
            n = sizeof(c->kind) - 1;
        }
        memcpy(c->kind, kind, n);
        c->kind[n] = '\0';
    }
    if (message) {
        n = strlen(message);
        if (n >= sizeof(c->message)) {
            n = sizeof(c->message) - 1;
        }
        memcpy(c->message, message, n);
        c->message[n] = '\0';
    }
}

static void debug_test_begin(fault_capture_t *cap)
{
    memset(cap, 0, sizeof(*cap));
    robo_mem_debug_enable();
    robo_mem_debug_reset_stats();
    robo_mem_debug_set_fault_handler(fault_capture_handler, cap);
}

static void debug_test_end(void)
{
    robo_mem_debug_set_fault_handler(NULL, NULL);
    robo_mem_debug_enable();
}

static void test_memdbg_normal_alloc_free(void)
{
    fault_capture_t cap;
    void *p;

    debug_test_begin(&cap);
#if ROBO_MEM_DEBUG
    ROBO_CHECK(robo_mem_debug_is_enabled());
    p = robo_malloc(48);
    ROBO_CHECK(p != NULL);
    ROBO_CHECK_EQ_INT(robo_mem_debug_live_count(), 1);
    ROBO_CHECK_EQ_INT(robo_mem_debug_check(p), 0);
    robo_free(p);
    ROBO_CHECK_EQ_INT(robo_mem_debug_live_count(), 0);
    ROBO_CHECK_EQ_INT(cap.count, 0);
#else
    ROBO_CHECK(!robo_mem_debug_is_enabled());
    p = robo_malloc(48);
    ROBO_CHECK(p != NULL);
    robo_free(p);
#endif
    debug_test_end();
}

static void test_memdbg_leak_report(void)
{
    fault_capture_t cap;
    void *a;
    void *b;
    void *c;

    debug_test_begin(&cap);
    a = robo_malloc(16);
    b = robo_malloc(32);
    c = robo_malloc(64);
    ROBO_CHECK(a && b && c);
#if ROBO_MEM_DEBUG
    ROBO_CHECK_EQ_INT(robo_mem_debug_live_count(), 3);
#endif
    /* Intentionally leave a and b live for report; free c. */
    robo_free(c);
    robo_mem_debug_report();
#if ROBO_MEM_DEBUG
    ROBO_CHECK_EQ_INT(robo_mem_debug_live_count(), 2);
#endif
    robo_free(a);
    robo_free(b);
    debug_test_end();
}

static void test_memdbg_double_free(void)
{
    fault_capture_t cap;
    void *p;

    debug_test_begin(&cap);
    p = robo_malloc(24);
    ROBO_CHECK(p != NULL);
    robo_free(p);
#if ROBO_MEM_DEBUG
    robo_free(p);
    ROBO_CHECK_EQ_INT(cap.count, 1);
    ROBO_CHECK_STR_EQ(cap.kind, "double-free");
#else
    (void)p;
    ROBO_CHECK(1);
#endif
    debug_test_end();
}

static void test_memdbg_invalid_free(void)
{
    fault_capture_t cap;
    int stack_local = 0;
    void *foreign;

    debug_test_begin(&cap);
#if ROBO_MEM_DEBUG
    robo_free(&stack_local);
    ROBO_CHECK(cap.count >= 1);
    ROBO_CHECK_STR_EQ(cap.kind, "invalid-free");

    memset(&cap, 0, sizeof(cap));
    foreign = malloc(8);
    ROBO_CHECK(foreign != NULL);
    robo_free(foreign); /* not a debug allocation */
    ROBO_CHECK_EQ_INT(cap.count, 1);
    ROBO_CHECK_STR_EQ(cap.kind, "invalid-free");
    free(foreign);
#else
    (void)stack_local;
    (void)foreign;
    ROBO_CHECK(1);
#endif
    debug_test_end();
}

static void test_memdbg_underflow(void)
{
    fault_capture_t cap;
    void *p;

    debug_test_begin(&cap);
    p = robo_malloc(32);
    ROBO_CHECK(p != NULL);
#if ROBO_MEM_DEBUG
    robo_mem_debug_corrupt_guard_for_test(p, 0);
    ROBO_CHECK_EQ_INT(robo_mem_debug_check(p), 1);
    robo_free(p);
    ROBO_CHECK_EQ_INT(cap.count, 1);
    ROBO_CHECK_STR_EQ(cap.kind, "guard-corruption");
#else
    robo_free(p);
#endif
    debug_test_end();
}

static void test_memdbg_overflow(void)
{
    fault_capture_t cap;
    void *p;

    debug_test_begin(&cap);
    p = robo_malloc(32);
    ROBO_CHECK(p != NULL);
#if ROBO_MEM_DEBUG
    robo_mem_debug_corrupt_guard_for_test(p, 1);
    ROBO_CHECK_EQ_INT(robo_mem_debug_check(p), 1);
    robo_free(p);
    ROBO_CHECK_EQ_INT(cap.count, 1);
    ROBO_CHECK_STR_EQ(cap.kind, "guard-corruption");
#else
    robo_free(p);
#endif
    debug_test_end();
}

static void test_memdbg_stats(void)
{
    fault_capture_t cap;
    robo_mem_stats_t s;
    void *a;
    void *b;

    debug_test_begin(&cap);
    a = robo_malloc(10);
    b = robo_malloc(20);
    ROBO_CHECK(a && b);
    robo_mem_debug_get_stats(&s);
#if ROBO_MEM_DEBUG
    ROBO_CHECK_EQ_INT(s.live_count, 2);
    ROBO_CHECK_EQ_INT(s.live_bytes, 30);
    ROBO_CHECK_EQ_INT(s.total_allocs, 2);
    ROBO_CHECK_EQ_INT(s.total_frees, 0);
    robo_free(a);
    robo_mem_debug_get_stats(&s);
    ROBO_CHECK_EQ_INT(s.live_count, 1);
    ROBO_CHECK_EQ_INT(s.live_bytes, 20);
    ROBO_CHECK_EQ_INT(s.total_frees, 1);
    robo_free(b);
#else
    robo_free(a);
    robo_free(b);
    ROBO_CHECK_EQ_INT(s.live_count, 0);
#endif
    debug_test_end();
}

static void test_memdbg_peak_stats(void)
{
    fault_capture_t cap;
    robo_mem_stats_t s;
    void *a;
    void *b;
    void *c;

    debug_test_begin(&cap);
    a = robo_malloc(100);
    b = robo_malloc(200);
    c = robo_malloc(50);
    ROBO_CHECK(a && b && c);
    robo_free(c);
    robo_mem_debug_get_stats(&s);
#if ROBO_MEM_DEBUG
    ROBO_CHECK_EQ_INT(s.peak_live_count, 3);
    ROBO_CHECK_EQ_INT(s.peak_live_bytes, 350);
    ROBO_CHECK_EQ_INT(s.live_count, 2);
    ROBO_CHECK_EQ_INT(s.live_bytes, 300);
#endif
    robo_free(a);
    robo_free(b);
    debug_test_end();
}

static void test_memdbg_enable_disable(void)
{
    fault_capture_t cap;
    void *tracked;
    void *rawish;
    robo_mem_stats_t s;

    debug_test_begin(&cap);
#if ROBO_MEM_DEBUG
    tracked = robo_malloc(40);
    ROBO_CHECK(tracked != NULL);
    ROBO_CHECK_EQ_INT(robo_mem_debug_live_count(), 1);

    robo_mem_debug_disable();
    ROBO_CHECK(!robo_mem_debug_is_enabled());
    rawish = robo_malloc(40);
    ROBO_CHECK(rawish != NULL);
    robo_mem_debug_get_stats(&s);
    /* Disabled path does not bump live tracking for new allocs. */
    ROBO_CHECK_EQ_INT(s.live_count, 1);

    robo_free(rawish); /* untracked while disabled → system free */
    robo_mem_debug_enable();
    robo_free(tracked);
    ROBO_CHECK_EQ_INT(robo_mem_debug_live_count(), 0);
    ROBO_CHECK_EQ_INT(cap.count, 0);
#else
    (void)tracked;
    (void)rawish;
    (void)s;
    robo_mem_debug_disable();
    ROBO_CHECK(!robo_mem_debug_is_enabled());
    robo_mem_debug_enable();
#endif
    debug_test_end();
}

void robo_test_register_mem_debug(void)
{
    robo_test_register("memdbg.normal_alloc_free", test_memdbg_normal_alloc_free);
    robo_test_register("memdbg.leak_report", test_memdbg_leak_report);
    robo_test_register("memdbg.double_free", test_memdbg_double_free);
    robo_test_register("memdbg.invalid_free", test_memdbg_invalid_free);
    robo_test_register("memdbg.underflow", test_memdbg_underflow);
    robo_test_register("memdbg.overflow", test_memdbg_overflow);
    robo_test_register("memdbg.stats", test_memdbg_stats);
    robo_test_register("memdbg.peak_stats", test_memdbg_peak_stats);
    robo_test_register("memdbg.enable_disable", test_memdbg_enable_disable);
}
