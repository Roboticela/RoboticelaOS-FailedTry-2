/*
 * RoboDebug — Goal 1.4 implementation.
 */

#include "RoboBase/RoboDebug.h"

#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>

static robo_panic_handler_fn g_robo_panic_handler = NULL;
static void *g_robo_panic_userdata = NULL;

robo_i32 robo_debug_enabled(void)
{
#if ROBO_DEBUG
    return 1;
#else
    return 0;
#endif
}

void robo_set_panic_handler(robo_panic_handler_fn handler, void *userdata)
{
    g_robo_panic_handler = handler;
    g_robo_panic_userdata = userdata;
}

static void robo_default_panic(const char *file, int line, const char *message)
{
    robo_log_write(ROBO_LOG_ERROR, file, line, "PANIC: %s",
                   message ? message : "(null)");
    fflush(stderr);
    abort();
}

void robo_panic_at(const char *file, int line, const char *fmt, ...)
{
    char body[1024];
    va_list ap;
    int n;
    robo_panic_handler_fn handler;

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

    handler = g_robo_panic_handler;
    if (handler) {
        handler(file ? file : "?", line, body, g_robo_panic_userdata);
        return;
    }
    robo_default_panic(file ? file : "?", line, body);
}

void robo_assert_fail(const char *file, int line, const char *expr)
{
    robo_log_write(ROBO_LOG_ERROR, file, line, "ASSERT failed: %s (%s)",
                   expr ? expr : "?",
                   robo_error_string(ROBO_ERROR));
    robo_panic_at(file, line, "assertion failed: %s", expr ? expr : "?");
}

void robo_debug_break(void)
{
    /*
     * Prefer a no-op + log for automated environments. Hardware traps
     * (__builtin_debugtrap / __debugbreak) stop under a debugger and are
     * awkward in CI; call sites that need a hard trap can be added later
     * behind an explicit ROBO_DEBUG_TRAP define if required.
     */
#if defined(ROBO_DEBUG_TRAP) && ROBO_DEBUG_TRAP
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
    robo_log_warn("robo_debug_break: no trap (define ROBO_DEBUG_TRAP=1 to enable)");
}
