/*
 * RoboDebug — assertions and panic (Goal 1.4).
 *
 * Build flags:
 *   -DRoboDebug=1  → debug build (RoboDebugAssert enabled)
 *   -DRoboDebug=0  → release build (RoboDebugAssert compiled out)
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

#ifndef RoboDebug
#define RoboDebug 0
#endif

/** @return 1 if compiled with RoboDebug=1, else 0. */
I32 RoboDebugEnabled(void);

/**
 * Panic handler. Must not return under the default implementation.
 * Test code may install a handler that returns (or longjmps) to avoid abort.
 */
typedef void (*PanicHandlerFn)(const char *file,
                                      int line,
                                      const char *message,
                                      void *userdata);

/** Install handler; NULL restores default (log + abort). */
void SetPanicHandler(PanicHandlerFn handler, void *userdata);

/**
 * Log a fatal message and invoke the panic handler.
 * Default handler does not return.
 */
void PanicAt(const char *file, int line, const char *fmt, ...);

#define Panic(...) PanicAt(__FILE__, __LINE__, __VA_ARGS__)

/** Attempt a debugger break where supported; then returns. */
void RoboDebugBreak(void);

/** Internal: used by assertion macros. */
void AssertFail(const char *file, int line, const char *expr);

/**
 * Always-on assertion. On failure: ERROR log + panic.
 * Condition is always evaluated.
 */
#define RoboAssert(cond)                                                      \
    do {                                                                       \
        if (!(cond)) {                                                         \
            AssertFail(__FILE__, __LINE__, #cond);                       \
        }                                                                      \
    } while (0)

/**
 * Debug-only assertion. Enabled iff RoboDebug=1.
 * In release builds the condition is not evaluated (no side effects).
 */
#if RoboDebug
#define RoboDebugAssert(cond) RoboAssert(cond)
#else
#define RoboDebugAssert(cond) ((void)0)
#endif

/** Mark code path as unreachable; always panics. */
#define RoboUnreachable()                                                     \
    do {                                                                       \
        PanicAt(__FILE__, __LINE__, "unreachable code");                 \
    } while (0)

#ifdef __cplusplus
}
#endif

#endif /* ROBO_DEBUG_H */
