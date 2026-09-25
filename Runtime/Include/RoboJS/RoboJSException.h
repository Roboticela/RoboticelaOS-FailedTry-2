/*
 * RoboJS exceptions (Phase 16.8) — `throw`, the five error kinds, and one pending-exception slot.
 *
 * ## A thrown value is a value, and the runtime holds exactly one
 *
 * JavaScript allows `throw 42`. The runtime therefore has a pending slot holding a `JsValue`, not an
 * error struct, and `JsRuntimeSetException` accepts anything. `JsErrorCreate` builds the *usual*
 * value — an error object with `name`, `message`, `line` and `column` — which is what `throw new
 * TypeError("x")` and every failure inside the engine use, and which a `catch` reads with ordinary
 * property access.
 *
 * ## The interpreter propagates, the host reports
 *
 * Inside the interpreter a `throw` is a `JsCompletionThrow` carrying the value, so `try`/`catch`/
 * `finally` is a switch on completions rather than a `longjmp`, and a frame's environment unwinds
 * normally. A native binding has no completion to return, so it sets the pending slot and answers a
 * non-OK `Result`; `JsFunctionCall` turns that back into the pending slot the interpreter reads.
 * The two meet in `JsRuntimeTakeException`, which is what a top-level evaluation calls to report an
 * uncaught error as a diagnostic.
 *
 * ## Nothing here crashes the process
 *
 * Every door in this header reports. `RoboJsErrorLimit` exists for the resource limits (16.1) — a
 * script that loops forever produces one of these, and the operating system never notices.
 */

#ifndef ROBO_JS_EXCEPTION_H
#define ROBO_JS_EXCEPTION_H

#include "RoboJS/RoboJSHeap.h"
#include "RoboJS/RoboJSTypes.h"
#include "RoboJS/RoboJSValue.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Create an error object: `name` (from @p kind), `message`, `line`, `column`, and an internal
 * `[[Prototype]]` of the runtime's error prototype so a `catch` can read them and an identity
 * comparison against another error still distinguishes them. A NULL @p message becomes `""`.
 */
JsObject *JsErrorCreate(JsRuntime *runtime, JsErrorKind kind, const char *message);

/** `JsErrorCreate` plus a source position, for an error the interpreter knows the place of. */
JsObject *JsErrorCreateAt(JsRuntime *runtime,
                          JsErrorKind kind,
                          const char *message,
                          const JsSpan *span);

/**
 * Set the runtime's pending exception to @p value (any value). Overwrites a pending one, which is
 * what "the outermost error wins" means in practice: a failure inside a `catch` replaces the
 * original.
 */
void JsRuntimeSetException(JsRuntime *runtime, const JsValue *value);

/** Set the pending exception to a fresh error object of @p kind with a formatted message. */
void JsRuntimeThrow(JsRuntime *runtime,
                    JsErrorKind kind,
                    const JsSpan *span,
                    const char *format,
                    ...);

/** 1 when an exception is pending. */
int JsRuntimeHasException(JsRuntime *runtime);

/** Read the pending exception without clearing it. @p out is left `undefined` when there is none. */
Result JsRuntimePeekException(JsRuntime *runtime, JsValue *out);

/** Read and clear the pending exception. Answers `RoboNotFound` when there was none. */
Result JsRuntimeTakeException(JsRuntime *runtime, JsValue *out);

/** Clear the pending exception (no-op when there is none). */
void JsRuntimeClearException(JsRuntime *runtime);

/**
 * The message of a thrown value, written into @p buffer: an error object's `message` property, a
 * string's own text, else the value's string form. Answers the bytes a complete message needs.
 */
int32_t JsExceptionMessage(const JsValue *value, char *buffer, size_t capacity);

/** The kind of a thrown value: an error object's `errorKind`, else `RoboJsErrorGeneral`. */
JsErrorKind JsExceptionKindOf(const JsValue *value);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_JS_EXCEPTION_H */
