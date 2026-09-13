/*
 * Unified RoboMem subsystem suite (Goal 2.5).
 *
 * Integration / coherence checks across heap, arena, pool, and debug.
 * Component-level cases remain in SuiteRoboMem / Arena / Pool / MemDebug.
 */

#include "RoboMem/RoboMemAll.h"
#include "RoboTest.h"

#include <stdint.h>
#include <string.h>

typedef struct fault_capture {
    int count;
    char kind[64];
} fault_capture_t;

static void fault_capture_handler(const char *kind, const char *message, void *userdata)
{
    fault_capture_t *c = (fault_capture_t *)userdata;
    size_t n;
    (void)message;
    c->count++;
    if (kind) {
        n = strlen(kind);
        if (n >= sizeof(c->kind)) {
            n = sizeof(c->kind) - 1;
        }
        memcpy(c->kind, kind, n);
        c->kind[n] = '\0';
    }
}

static void test_memsys_heap_arena_pool(void)
{
    void *heap;
    robo_arena_t *arena;
    robo_pool_t *pool;
    void *a1;
    void *a2;
    void *p1;
    void *p2;

    heap = robo_malloc(64);
    ROBO_CHECK(heap != NULL);
    robo_mem_set(heap, 0x7E, 64);

    arena = robo_arena_create(128);
    ROBO_CHECK(arena != NULL);
    a1 = robo_arena_alloc(arena, 32);
    a2 = robo_arena_alloc_aligned(arena, 16, 16);
    ROBO_CHECK(a1 && a2);
    ROBO_CHECK(((uintptr_t)a2 % 16) == 0);
    robo_mem_set(a1, 0x11, 32);
    robo_mem_set(a2, 0x22, 16);

    pool = robo_pool_create(24, 4);
    ROBO_CHECK(pool != NULL);
    p1 = robo_pool_alloc(pool);
    p2 = robo_pool_alloc(pool);
    ROBO_CHECK(p1 && p2);
    ROBO_CHECK_EQ_INT(robo_pool_available(pool), 2);
    ROBO_CHECK_EQ_INT(robo_pool_free(pool, p1), ROBO_OK);
    ROBO_CHECK(robo_pool_alloc(pool) == p1); /* reuse */

    /* Distinct regions */
    ROBO_CHECK(heap != a1 && heap != p1 && a1 != p1);

    robo_pool_destroy(pool);
    robo_arena_destroy(arena);
    robo_free(heap);
}

static void test_memsys_arena_exhaust_reset(void)
{
    robo_arena_t *a = robo_arena_create(40);
    void *p;

    ROBO_CHECK(a != NULL);
    p = robo_arena_alloc(a, 32);
    ROBO_CHECK(p != NULL);
    ROBO_CHECK(robo_arena_alloc(a, 32) == NULL); /* exhaustion */
    robo_arena_reset(a);
    ROBO_CHECK(robo_arena_bytes_remaining(a) == 40);
    p = robo_arena_alloc(a, 32);
    ROBO_CHECK(p != NULL);
    robo_arena_destroy(a);
}

static void test_memsys_pool_exhaust_invalid(void)
{
    robo_pool_t *p = robo_pool_create(8, 2);
    void *a;
    void *b;
    void *c;

    ROBO_CHECK(p != NULL);
    a = robo_pool_alloc(p);
    b = robo_pool_alloc(p);
    c = robo_pool_alloc(p);
    ROBO_CHECK(a && b);
    ROBO_CHECK(c == NULL); /* exhaustion */
    ROBO_CHECK_EQ_INT(robo_pool_free(p, a), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_pool_free(p, a), ROBO_ALREADY_EXISTS);
    ROBO_CHECK_EQ_INT(robo_pool_free(p, (char *)b + 1), ROBO_INVALID_ARGUMENT);
    robo_pool_destroy(p);
}

static void test_memsys_debug_stats_guards(void)
{
    fault_capture_t cap;
    robo_mem_stats_t s;
    void *p;

    memset(&cap, 0, sizeof(cap));
    robo_mem_debug_enable();
    robo_mem_debug_reset_stats();
    robo_mem_debug_set_fault_handler(fault_capture_handler, &cap);

    p = robo_malloc(48);
    ROBO_CHECK(p != NULL);
    ROBO_CHECK(((uintptr_t)p % sizeof(void *)) == 0);

#if ROBO_MEM_DEBUG
    ROBO_CHECK_EQ_INT(robo_mem_debug_check(p), 0);
    robo_mem_debug_get_stats(&s);
    ROBO_CHECK_EQ_INT(s.live_count, 1);
    ROBO_CHECK_EQ_INT(s.live_bytes, 48);
    robo_mem_debug_report();

    robo_mem_debug_corrupt_guard_for_test(p, 1);
    ROBO_CHECK_EQ_INT(robo_mem_debug_check(p), 1);
    robo_free(p);
    ROBO_CHECK_EQ_INT(cap.count, 1);
    ROBO_CHECK_STR_EQ(cap.kind, "guard-corruption");
#else
    robo_mem_debug_get_stats(&s);
    ROBO_CHECK_EQ_INT(s.live_count, 0);
    robo_mem_debug_report();
    robo_free(p);
#endif

    robo_mem_debug_set_fault_handler(NULL, NULL);
}

static void test_memsys_realloc_utilities(void)
{
    char *p = (char *)robo_malloc(8);
    unsigned char buf[8];

    ROBO_CHECK(p != NULL);
    robo_mem_copy(p, "hello", 6);
    p = (char *)robo_realloc(p, 32);
    ROBO_CHECK(p != NULL);
    ROBO_CHECK_STR_EQ(p, "hello");
    robo_free(p);

    ROBO_CHECK(robo_mem_set(buf, 0x5A, sizeof(buf)) == buf);
    ROBO_CHECK_EQ_INT(buf[0], 0x5A);
    ROBO_CHECK_EQ_INT(robo_mem_compare(buf, buf, sizeof(buf)), 0);
}

void robo_test_register_mem_system(void)
{
    robo_test_register("memsys.heap_arena_pool", test_memsys_heap_arena_pool);
    robo_test_register("memsys.arena_exhaust_reset", test_memsys_arena_exhaust_reset);
    robo_test_register("memsys.pool_exhaust_invalid", test_memsys_pool_exhaust_invalid);
    robo_test_register("memsys.debug_stats_guards", test_memsys_debug_stats_guards);
    robo_test_register("memsys.realloc_utilities", test_memsys_realloc_utilities);
}
