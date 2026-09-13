/*
 * RoboDebug — assertions and panic (Goal 1.4).
 *
 * Build flags:
 *   -DROBO_DEBUG=1  → debug build (ROBO_DEBUG_ASSERT enabled)
 *   -DROBO_DEBUG=0  → release build (ROBO_DEBUG_ASSERT compiled out)
 * Default if undefined: 0 (release).
 *
 * See Docs/RoboDebug.md.
 */

#ifndef ROBO_DEBUG_H
#define ROBO_DEBUG_H

#include "RoboBase/RoboError.h"
#include "RoboBase/RoboLog.h"

#ifdef __cplusplus
extern "C" {
#endif

#ifndef ROBO_DEBUG
#define ROBO_DEBUG 0
#endif

/** @return 1 if compiled with ROBO_DEBUG=1, else 0. */
robo_i32 robo_debug_enabled(void);

/**
 * Panic handler. Must not return under the default implementation.
 * Test code may install a handler that returns (or longjmps) to avoid abort.
 */
typedef void (*robo_panic_handler_fn)(const char *file,
                                      int line,
                                      const char *message,
                                      void *userdata);

/** Install handler; NULL restores default (log + abort). */
void robo_set_panic_handler(robo_panic_handler_fn handler, void *userdata);

/**
 * Log a fatal message and invoke the panic handler.
 * Default handler does not return.
 */
void robo_panic_at(const char *file, int line, const char *fmt, ...);

#define robo_panic(...) robo_panic_at(__FILE__, __LINE__, __VA_ARGS__)

/** Attempt a debugger break where supported; then returns. */
void robo_debug_break(void);

/** Internal: used by assertion macros. */
void robo_assert_fail(const char *file, int line, const char *expr);

/**
 * Always-on assertion. On failure: ERROR log + panic.
 * Condition is always evaluated.
 */
#define ROBO_ASSERT(cond)                                                      \
    do {                                                                       \
        if (!(cond)) {                                                         \
            robo_assert_fail(__FILE__, __LINE__, #cond);                       \
        }                                                                      \
    } while (0)

/**
 * Debug-only assertion. Enabled iff ROBO_DEBUG=1.
 * In release builds the condition is not evaluated (no side effects).
 */
#if ROBO_DEBUG
#define ROBO_DEBUG_ASSERT(cond) ROBO_ASSERT(cond)
#else
#define ROBO_DEBUG_ASSERT(cond) ((void)0)
#endif

/** Mark code path as unreachable; always panics. */
#define ROBO_UNREACHABLE()                                                     \
    do {                                                                       \
        robo_panic_at(__FILE__, __LINE__, "unreachable code");                 \
    } while (0)

#ifdef __cplusplus
}
#endif

#endif /* ROBO_DEBUG_H */
