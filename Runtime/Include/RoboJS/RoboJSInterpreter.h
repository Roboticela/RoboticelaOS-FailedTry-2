/*
 * RoboJS interpreter (Phase 16.9) — the tree-walking evaluator.
 *
 * ## Control flow is a completion, not a jump
 *
 * `return`, `break`, `continue` and a throw are all `JsCompletion` values travelling up the C stack
 * as ordinary returns. That choice is what makes `finally` correct without bookkeeping: the `try`
 * arm sees a `Return` completion, runs the `finally` block, and only then passes the completion on.
 * A `longjmp`-based interpreter has to reconstruct that with a flag per frame, and a `goto`-based one
 * cannot express a `throw` crossing a native binding at all.
 *
 * ## The evaluator's shape
 *
 * `JsInterpRunProgram` walks a statement list; statements dispatch to `EvalStatement`, expressions to
 * `EvalExpression`, and the two operator families to one each (`EvalBinary`, `EvalUnary`) switching
 * on the operator kind. Nothing in the evaluator includes a DOM header: `document.getElementById` is
 * an ordinary native function this evaluator called, which is the seam that keeps "the interpreter
 * knows about HTML" from ever becoming true.
 *
 * ## Two budgets, both stated
 *
 * Every evaluation gets `maxSteps` (a step is one evaluated node) and every call gets `maxCallDepth`.
 * Exceeding either produces a `RoboJsErrorLimit` with a located diagnostic rather than a hang or a
 * host-stack overflow, and the runtime is left in a state a caller can keep using — the pending
 * exception is the only thing a limit leaves behind.
 */

#ifndef ROBO_JS_INTERPRETER_H
#define ROBO_JS_INTERPRETER_H

#include "RoboJS/RoboJSAst.h"
#include "RoboJS/RoboJSEnvironment.h"
#include "RoboJS/RoboJSTypes.h"
#include "RoboJS/RoboJSValue.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** How one node finished. */
typedef struct JsCompletion {
    uint32_t kind; /* JsCompletionKind */
    JsValue value;
} JsCompletion;

/** A completion that finished normally with @p value. */
JsCompletion JsCompletionNormal(const JsValue *value);
/** A completion of @p kind carrying @p value. */
JsCompletion JsCompletionOf(JsCompletionKind kind, const JsValue *value);
/** 1 when the completion is `Normal` — the tests' one question. */
int JsCompletionIsNormal(const JsCompletion *completion);

/** The result of one whole program. */
typedef struct JsScriptResult {
    /** `RoboOk`, or the failure the runtime also recorded as a diagnostic. */
    Result status;
    /** The program's completion value (`undefined` for a program that ended in a statement). */
    JsValue value;
    /** Steps consumed. */
    int32_t steps;
    /** Deepest call reached. */
    int32_t depth;
    /** Set when @p status is not RoboOk, with the span of the failure. */
    JsError error;
    /** Statements executed (the program's list length, plus a nested function's bodies as they ran). */
    int32_t statements;
} JsScriptResult;

/**
 * Run a parsed program in @p env (the global environment, in every shipped path).
 *
 * @return RoboOk when the program ran to completion — including a program whose last statement was a
 *         `throw` that an enclosing `try` caught. `RoboError` when a value was thrown out of the
 *         program (the runtime's pending exception holds it), `RoboJsErrorLimit`-shaped limits
 *         report `RoboError` with `JsScriptResult.error.kind` set, and `RoboInvalidArgument` for a
 *         NULL argument or a tree whose root is not a Program.
 */
Result JsInterpRunProgram(JsRuntime *runtime,
                          JsNode *program,
                          JsEnv *env,
                          JsScriptResult *out);

/**
 * Evaluate a single expression node in @p env — the door a test uses to keep an expectation small,
 * and the door an integration layer uses to resume a script it stepped by hand.
 */
Result JsInterpRunExpression(JsRuntime *runtime,
                             JsNode *expression,
                             JsEnv *env,
                             JsValue *out);

/**
 * The property key an expression evaluates to, for `a[k]`: a string stays as it is, a number becomes
 * its canonical numeric text, everything else goes through `JsValueToString`. Answers the bytes a
 * complete key needs; the buffer is NUL-terminated when @p capacity > 0.
 */
int32_t JsInterpPropertyKey(const JsValue *value, char *buffer, size_t capacity);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_JS_INTERPRETER_H */
