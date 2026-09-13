/*
 * RoboLog — Goal 1.2 implementation.
 *
 * Default sink: stderr via Roboticela/host libc.
 * No Chromium code. No heap allocation.
 */

#include "RoboBase/RoboLog.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#define ROBO_LOG_LINE_MAX 1024

static robo_log_level_t g_robo_log_min_level = ROBO_LOG_DEBUG;
static robo_log_writer_fn g_robo_log_writer = NULL;
static void *g_robo_log_writer_userdata = NULL;

static const char *robo_log_level_name(robo_log_level_t level)
{
    switch (level) {
    case ROBO_LOG_DEBUG:
        return "DEBUG";
    case ROBO_LOG_INFO:
        return "INFO";
    case ROBO_LOG_WARN:
        return "WARN";
    case ROBO_LOG_ERROR:
        return "ERROR";
    default:
        return "LOG";
    }
}

static const char *robo_log_basename(const char *file)
{
    const char *base;
    const char *p;

    if (!file || !file[0]) {
        return "?";
    }
    base = file;
    for (p = file; *p; p++) {
        if (*p == '/' || *p == '\\') {
            base = p + 1;
        }
    }
    return base;
}

static void robo_log_default_writer(robo_log_level_t level,
                                    const char *line,
                                    void *userdata)
{
    (void)level;
    (void)userdata;
    if (!line) {
        return;
    }
    /* stderr is the process console stream for this milestone. */
    fputs(line, stderr);
    fputc('\n', stderr);
    fflush(stderr);
}

void robo_log_set_min_level(robo_log_level_t level)
{
    if (level < ROBO_LOG_DEBUG) {
        level = ROBO_LOG_DEBUG;
    }
    if (level > ROBO_LOG_ERROR) {
        level = ROBO_LOG_ERROR;
    }
    g_robo_log_min_level = level;
}

robo_log_level_t robo_log_get_min_level(void)
{
    return g_robo_log_min_level;
}

void robo_log_set_writer(robo_log_writer_fn writer, void *userdata)
{
    g_robo_log_writer = writer;
    g_robo_log_writer_userdata = userdata;
}

void robo_log_write(robo_log_level_t level,
                    const char *file,
                    int line,
                    const char *fmt,
                    ...)
{
    char body[ROBO_LOG_LINE_MAX];
    char linebuf[ROBO_LOG_LINE_MAX];
    va_list ap;
    int n;
    robo_log_writer_fn writer;

    if (level < g_robo_log_min_level) {
        return;
    }

    if (!fmt) {
        fmt = "";
    }

    va_start(ap, fmt);
    n = vsnprintf(body, sizeof(body), fmt, ap);
    va_end(ap);
    if (n < 0) {
        body[0] = '\0';
    } else if ((size_t)n >= sizeof(body)) {
        body[sizeof(body) - 1] = '\0';
    }

    n = snprintf(linebuf,
                 sizeof(linebuf),
                 "[%s] %s:%d: %s",
                 robo_log_level_name(level),
                 robo_log_basename(file),
                 line,
                 body);
    if (n < 0) {
        return;
    }
    if ((size_t)n >= sizeof(linebuf)) {
        linebuf[sizeof(linebuf) - 1] = '\0';
    }

    writer = g_robo_log_writer ? g_robo_log_writer : robo_log_default_writer;
    writer(level, linebuf, g_robo_log_writer_userdata);
}
