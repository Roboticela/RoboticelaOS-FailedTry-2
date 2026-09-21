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

#define RoboLogLineMax 1024

static LogLevel gRoboLogMinLevel = RoboLogDebug;
static LogWriterFn gRoboLogWriter = NULL;
static void *gRoboLogWriterUserdata = NULL;

static const char *LogLevelName(LogLevel level)
{
    switch (level) {
    case RoboLogDebug:
        return "DEBUG";
    case RoboLogInfo:
        return "INFO";
    case RoboLogWarn:
        return "WARN";
    case RoboLogError:
        return "ERROR";
    default:
        return "LOG";
    }
}

static const char *LogBasename(const char *file)
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

static void LogDefaultWriter(LogLevel level,
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

void LogSetMinLevel(LogLevel level)
{
    if (level < RoboLogDebug) {
        level = RoboLogDebug;
    }
    if (level > RoboLogError) {
        level = RoboLogError;
    }
    gRoboLogMinLevel = level;
}

LogLevel LogGetMinLevel(void)
{
    return gRoboLogMinLevel;
}

void LogSetWriter(LogWriterFn writer, void *userdata)
{
    gRoboLogWriter = writer;
    gRoboLogWriterUserdata = userdata;
}

void LogWrite(LogLevel level,
                    const char *file,
                    int line,
                    const char *fmt,
                    ...)
{
    char body[RoboLogLineMax];
    char linebuf[RoboLogLineMax];
    va_list ap;
    int n;
    LogWriterFn writer;

    if (level < gRoboLogMinLevel) {
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
                 LogLevelName(level),
                 LogBasename(file),
                 line,
                 body);
    if (n < 0) {
        return;
    }
    if ((size_t)n >= sizeof(linebuf)) {
        linebuf[sizeof(linebuf) - 1] = '\0';
    }

    writer = gRoboLogWriter ? gRoboLogWriter : LogDefaultWriter;
    writer(level, linebuf, gRoboLogWriterUserdata);
}
