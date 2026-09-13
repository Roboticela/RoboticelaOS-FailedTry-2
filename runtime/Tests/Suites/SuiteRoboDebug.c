/*
 * RoboDebug suite (Goal 1.5).
 *
 * Uses a custom panic handler so the suite never aborts the runner.
 * Default abort is validated via --child-panic / --child-assert from Build.ps1.
 */

#include "RoboBase/RoboDebug.h"
#include "RoboTest.h"

#include <string.h>

static int g_panic_count;
static char g_panic_msg[512];
static char g_logs[8][256];
static int g_log_count;

static void panic_handler(const char *file, int line, const char *message,
                          void *ud)
{
    size_t i;
    (void)file;
    (void)line;
    (void)ud;
    g_panic_count++;
    g_panic_msg[0] = '\0';
    if (!message) {
        return;
    }
    for (i = 0; i + 1 < sizeof(g_panic_msg) && message[i]; i++) {
        g_panic_msg[i] = message[i];
    }
    g_panic_msg[i] = '\0';
}

static void log_writer(robo_log_level_t level, const char *line, void *ud)
{
    size_t i;
    (void)level;
    (void)ud;
    if (g_log_count >= 8 || !line) {
        return;
    }
    for (i = 0; i + 1 < sizeof(g_logs[0]) && line[i]; i++) {
        g_logs[g_log_count][i] = line[i];
    }
    g_logs[g_log_count][i] = '\0';
    g_log_count++;
}

static int log_has(const char *needle)
{
    int i;
    for (i = 0; i < g_log_count; i++) {
        if (strstr(g_logs[i], needle)) {
            return 1;
        }
    }
    return 0;
}

static void install_hooks(void)
{
    g_panic_count = 0;
    g_log_count = 0;
    g_panic_msg[0] = '\0';
    robo_log_set_writer(log_writer, NULL);
    robo_set_panic_handler(panic_handler, NULL);
}

static void clear_hooks(void)
{
    robo_set_panic_handler(NULL, NULL);
    robo_log_set_writer(NULL, NULL);
}

static void test_assert_pass_fail(void)
{
    install_hooks();
    ROBO_ASSERT(1 == 1);
    ROBO_CHECK_EQ_INT(g_panic_count, 0);

    ROBO_ASSERT(1 == 0);
    ROBO_CHECK_EQ_INT(g_panic_count, 1);
    ROBO_CHECK(strstr(g_panic_msg, "assertion failed") != NULL);
    ROBO_CHECK(log_has("ASSERT failed") || log_has("PANIC"));
    clear_hooks();
}

static void test_debug_assert_config(void)
{
    volatile int side = 0;
    install_hooks();

#if ROBO_DEBUG
    ROBO_CHECK_EQ_INT(robo_debug_enabled(), 1);
    ROBO_DEBUG_ASSERT((side = 1, 1));
    ROBO_CHECK_EQ_INT(side, 1);
    ROBO_CHECK_EQ_INT(g_panic_count, 0);
    ROBO_DEBUG_ASSERT(0);
    ROBO_CHECK_EQ_INT(g_panic_count, 1);
#else
    ROBO_CHECK_EQ_INT(robo_debug_enabled(), 0);
    ROBO_DEBUG_ASSERT((side = 1, 0));
    ROBO_CHECK_EQ_INT(side, 0);
    ROBO_CHECK_EQ_INT(g_panic_count, 0);
    ROBO_ASSERT(0);
    ROBO_CHECK_EQ_INT(g_panic_count, 1);
#endif
    clear_hooks();
}

static void test_panic_and_unreachable(void)
{
    install_hooks();
    robo_panic("hello %d", 7);
    ROBO_CHECK_EQ_INT(g_panic_count, 1);
    ROBO_CHECK(strstr(g_panic_msg, "hello 7") != NULL);

    g_panic_count = 0;
    ROBO_UNREACHABLE();
    ROBO_CHECK_EQ_INT(g_panic_count, 1);
    clear_hooks();
}

void robo_test_register_debug(void)
{
    robo_test_register("debug.assert", test_assert_pass_fail);
    robo_test_register("debug.config", test_debug_assert_config);
    robo_test_register("debug.panic_unreachable", test_panic_and_unreachable);
}
