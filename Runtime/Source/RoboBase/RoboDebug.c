/*
 * RoboDebug — Goal 1.4 implementation.
 */

#include "RoboBase/RoboDebug.h"

#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>

static PanicHandlerFn gRoboPanicHandler = NULL;
static void *gRoboPanicUserdata = NULL;

I32 RoboDebugEnabled(void)
{
#if RoboDebug
    return 1;
#else
    return 0;
#endif
}

void SetPanicHandler(PanicHandlerFn handler, void *userdata)
{
    gRoboPanicHandler = handler;
    gRoboPanicUserdata = userdata;
}

static void DefaultPanic(const char *file, int line, const char *message)
{
    LogWrite(RoboLogError, file, line, "PANIC: %s",
                   message ? message : "(null)");
    fflush(stderr);
    abort();
}

void PanicAt(const char *file, int line, const char *fmt, ...)
{
    char body[1024];
    va_list ap;
    int n;
    PanicHandlerFn handler;

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

    handler = gRoboPanicHandler;
    if (handler) {
        handler(file ? file : "?", line, body, gRoboPanicUserdata);
        return;
    }
    DefaultPanic(file ? file : "?", line, body);
}

void AssertFail(const char *file, int line, const char *expr)
{
    LogWrite(RoboLogError, file, line, "ASSERT failed: %s (%s)",
                   expr ? expr : "?",
                   ErrorString(RoboError));
    PanicAt(file, line, "assertion failed: %s", expr ? expr : "?");
}

void RoboDebugBreak(void)
{
    /*
     * Prefer a no-op + log for automated environments. Hardware traps
     * (__builtin_debugtrap / __debugbreak) stop under a debugger and are
     * awkward in CI; call sites that need a hard trap can be added later
     * behind an explicit RoboDebugTrap define if required.
     */
#if defined(RoboDebugTrap) && RoboDebugTrap
#if defined(__has_builtin)
#if __has_builtin(__builtin_debugtrap)
    __builtin_debugtrap();
    return;
#endif
#endif
#if defined(_MSC_VER)
    __debugbreak();
    return;
#endif
#endif
    LogWarn("robo_debug_break: no trap (define ROBO_DEBUG_TRAP=1 to enable)");
}
