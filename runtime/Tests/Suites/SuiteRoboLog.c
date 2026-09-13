/*
 * RoboLog suite (Goal 1.5).
 */

#include "RoboBase/RoboLog.h"
#include "RoboTest.h"

#include <string.h>

#define CAP 8
#define CAP_LEN 256

static char g_lines[CAP][CAP_LEN];
static robo_log_level_t g_levels[CAP];
static int g_count;

static void capture_reset(void)
{
    g_count = 0;
}

static void capture_writer(robo_log_level_t level, const char *line, void *ud)
{
    size_t i;
    (void)ud;
    if (g_count >= CAP || !line) {
        return;
    }
    g_levels[g_count] = level;
    for (i = 0; i + 1 < CAP_LEN && line[i]; i++) {
        g_lines[g_count][i] = line[i];
    }
    g_lines[g_count][i] = '\0';
    g_count++;
}

static int has_prefix(const char *prefix)
{
    size_t n = strlen(prefix);
    int i;
    for (i = 0; i < g_count; i++) {
        if (strncmp(g_lines[i], prefix, n) == 0) {
            return 1;
        }
    }
    return 0;
}

static int contains(const char *needle)
{
    int i;
    for (i = 0; i < g_count; i++) {
        if (strstr(g_lines[i], needle)) {
            return 1;
        }
    }
    return 0;
}

static void test_logging_levels(void)
{
    robo_log_set_writer(capture_writer, NULL);
    robo_log_set_min_level(ROBO_LOG_DEBUG);

    capture_reset();
    robo_log_info("hello info");
    ROBO_CHECK_EQ_INT(g_count, 1);
    ROBO_CHECK_EQ_INT(g_levels[0], ROBO_LOG_INFO);
    ROBO_CHECK(has_prefix("[INFO]"));

    capture_reset();
    robo_log_warn("hello warn");
    ROBO_CHECK(has_prefix("[WARN]"));

    capture_reset();
    robo_log_error("hello error");
    ROBO_CHECK(has_prefix("[ERROR]"));

    capture_reset();
    robo_log_debug("hello debug");
    ROBO_CHECK(has_prefix("[DEBUG]"));

    capture_reset();
    robo_log_info("value=%d name=%s", 42, "robo");
    ROBO_CHECK(contains("value=42"));
    ROBO_CHECK(contains("name=robo"));

    capture_reset();
    robo_log_info(NULL);
    robo_log_write(ROBO_LOG_INFO, NULL, 0, NULL);
    ROBO_CHECK_EQ_INT(g_count, 2);

    capture_reset();
    robo_log_set_min_level(ROBO_LOG_WARN);
    robo_log_debug("hidden");
    robo_log_info("hidden");
    robo_log_warn("shown");
    ROBO_CHECK_EQ_INT(g_count, 1);
    ROBO_CHECK(has_prefix("[WARN]"));
    robo_log_set_min_level(ROBO_LOG_DEBUG);

    capture_reset();
    robo_log_error("with location");
    ROBO_CHECK(contains("SuiteRoboLog.c:"));
    ROBO_CHECK(contains("with location"));

    robo_log_set_writer(NULL, NULL);
}

void robo_test_register_log(void)
{
    robo_test_register("log.levels_format_filter", test_logging_levels);
}
