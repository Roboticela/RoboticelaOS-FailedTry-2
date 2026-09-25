/*
 * The RoboJS interpreter (see `RoboJSInterpreter.h`).
 *
 * ## Two functions and a completion
 *
 * `JsEvalStmt` writes a `JsCompletion`; every statement arm either finishes normally or raises one of
 * the four control-flow completions. `JsEvalExpr` writes a `JsValue` and reports through the
 * `Result`: `RoboOk` with a value, or an exception already set on the runtime. The asymmetry is
 * deliberate — an expression cannot `return` — and it is why `finally` needs no bookkeeping.
 *
 * ## Hoisting is one pass, and it happens before a block runs
 *
 * `JsHoist` walks a statement list once and declares what the list will define: a
 * `function` declaration becomes a *function object* in the nearest function scope, and a `var`
 * becomes `undefined` there, while a `let`/`const` is declared in the block that owns it. Both
 * effects are observable from the first statement of the list, which is what "hoisted" means, and
 * the declaration statements themselves then only assign.
 *
 * ## `this` is an argument, not state
 *
 * Every evaluator function takes the receiver. A method call passes the member's object, a plain
 * call passes `NULL` (which the evaluator substitutes with the global object, this build being
 * sloppy-mode), and an **arrow function captures it**: `JsFunctionCreateUser` is given
 * `userdata = the defining receiver`, because `this` is lexical for an arrow and a per-call
 * substitution would be exactly the wrong answer.
 *
 * ## Budgets
 *
 * Every node evaluation spends a step (`JsRuntimeSpendStep`) and every call enters a frame
 * (`JsRuntimeEnterCall`). Both produce a limit error with a located span, so a runaway script is a
 * diagnostic rather than a hung process.
 */

#include "RoboJS/RoboJSInterpreter.h"

/* Declared before its definition because a number-to-text conversion earlier in the file needs the
 * same "is this a whole number I can print without a decimal point" rule the helper states once. */
static int JsNumberIsInteger(double value);

#include "RoboJS/RoboJSException.h"
#include "RoboJS/RoboJSFunction.h"
#include "RoboJS/RoboJSObject.h"
#include "RoboJS/RoboJSRuntime.h"

#include "RoboJSInternal.h"

#include "RoboJSMath.h"
#include <stdio.h>
#include <string.h>

#define JsMaxLiteralString 512

/* ------------------------------------------------------------------ */
/* Completions                                                         */
/* ------------------------------------------------------------------ */

JsCompletion JsCompletionNormal(const JsValue *value)
{
    JsCompletion completion;

    completion.kind = RoboJsCompleteNormal;
    completion.value = value != NULL ? *value : JsValueUndefined();
    return completion;
}

JsCompletion JsCompletionOf(JsCompletionKind kind, const JsValue *value)
{
    JsCompletion completion;

    completion.kind = (uint32_t)kind;
    completion.value = value != NULL ? *value : JsValueUndefined();
    return completion;
}

int JsCompletionIsNormal(const JsCompletion *completion)
{
    return completion != NULL && completion->kind == (uint32_t)RoboJsCompleteNormal;
}

/* ------------------------------------------------------------------ */
/* Forward declarations                                                */
/* ------------------------------------------------------------------ */

static Result JsEvalExpr(JsRuntime *rt, JsNode *node, JsEnv *env, JsObject *thisObj, JsValue *out);
static Result JsEvalStmt(JsRuntime *rt,
                         JsNode *node,
                         JsEnv *env,
                         JsObject *thisObj,
                         JsCompletion *out);
static Result JsEvalList(JsRuntime *rt,
                         JsNode **list,
                         int32_t count,
                         JsEnv *env,
                         JsObject *thisObj,
                         JsCompletion *out);
static Result JsHoist(JsRuntime *rt, JsNode **list, int32_t count, JsEnv *env);

/* ------------------------------------------------------------------ */
/* Small helpers                                                       */
/* ------------------------------------------------------------------ */

static JsValue JsNumberValue(double value)
{
    return JsValueNumber(value);
}

static Result JsForcedFailure(JsRuntime *rt, Result status)
{
    if (status == RoboOk) {
        return RoboOk;
    }
    if (!JsRuntimeHasException(rt)) {
        JsRuntimeThrow(rt, RoboJsErrorInternal, NULL, "the runtime failed with %s", ErrorString(status));
    }
    return status;
}

/** A property key from a value: a string as it is, a number canonically, else `ToString`. */
int32_t JsInterpPropertyKey(const JsValue *value, char *buffer, size_t capacity)
{
    if (buffer != NULL && capacity > 0) {
        buffer[0] = '\0';
    }
    if (value == NULL) {
        return 0;
    }
    if (JsValueIsNumber(value)) {
        double number = value->number;

        if (JsNumberIsInteger(number)) {
            char scratch[32];
            int written = snprintf(scratch, sizeof(scratch), "%.0f", number);

            if (buffer != NULL && capacity > 0) {
                size_t copy = (size_t)(written > 0 ? written : 0);

                if (copy >= capacity) {
                    copy = capacity - 1u;
                }
                memcpy(buffer, scratch, copy);
                buffer[copy] = '\0';
            }
            return written > 0 ? written : 0;
        }
    }
    return JsValueToString(value, buffer, capacity);
}

/** Read a property of a value with the language's own special cases. */
static Result JsGetProperty(JsRuntime *rt, const JsValue *target, const JsValue *key, JsValue *out)
{
    char name[JsMaxLiteralString];
    int32_t nameLength;

    *out = JsValueUndefined();
    if (target == NULL) {
        return RoboInvalidArgument;
    }
    nameLength = JsInterpPropertyKey(key, name, sizeof(name));
    if (nameLength >= JsMaxLiteralString) {
        JsRuntimeThrow(rt, RoboJsErrorRange, NULL, "a property name is longer than this build allows");
        return RoboError;
    }
    if (JsValueIsString(target)) {
        if (strcmp(name, "length") == 0) {
            *out = JsValueNumber((double)target->stringLength);
            return RoboOk;
        }
        {
            int32_t index;

            if (JsArrayIndexOfKey(name, &index) && index < target->stringLength) {
                *out = JsValueString(rt->heap, target->string + index, 1);
                return RoboOk;
            }
        }
        /* A string's methods come from `String.prototype`, so the lookup continues there. */
        if (JsObjectGet(rt->stringPrototype, name, out) == RoboOk) {
            return RoboOk;
        }
        return RoboOk;
    }
    if (JsValueIsArray(target)) {
        if (strcmp(name, "length") == 0) {
            *out = JsValueNumber((double)target->object->length);
            return RoboOk;
        }
        {
            int32_t index;

            if (JsArrayIndexOfKey(name, &index)) {
                if (index < target->object->length) {
                    *out = target->object->elements[index];
                    return RoboOk;
                }
                if (JsObjectGet(rt->arrayPrototype, name, out) == RoboOk) {
                    return RoboOk;
                }
                return RoboOk;
            }
        }
        JsObjectGet(target->object, name, out);
        return RoboOk;
    }
    if (JsValueIsObject(target)) {
        JsObjectGet(target->object, name, out);
        return RoboOk;
    }
    /* A number, boolean, null or undefined has no properties in this build: `undefined.length` is
     * `undefined` rather than a TypeError, which is a stated simplification. */
    return RoboOk;
}

/** Assign a property of a value; the same special cases as the read door, on the write side. */
static Result JsSetProperty(JsRuntime *rt, const JsValue *target, const JsValue *key, const JsValue *value)
{
    char name[JsMaxLiteralString];
    int32_t nameLength;

    if (target == NULL || value == NULL) {
        return RoboInvalidArgument;
    }
    nameLength = JsInterpPropertyKey(key, name, sizeof(name));
    if (nameLength >= JsMaxLiteralString) {
        JsRuntimeThrow(rt, RoboJsErrorRange, NULL, "a property name is longer than this build allows");
        return RoboError;
    }
    if (JsValueIsArray(target)) {
        if (strcmp(name, "length") == 0) {
            double length = JsValueToNumber(value);

            if (length < 0 || length != JsMathFloor(length) || length > 2147483647.0) {
                JsRuntimeThrow(rt, RoboJsErrorRange, NULL, "an array length must be a non-negative integer");
                return RoboError;
            }
            if (JsArraySetLength(rt->heap, target->object, (int32_t)length) != RoboOk) {
                JsRuntimeThrow(rt, RoboJsErrorInternal, NULL, "out of memory growing the array");
                return RoboOutOfMemory;
            }
            return RoboOk;
        }
        {
            int32_t index;

            if (JsArrayIndexOfKey(name, &index)) {
                if (JsArraySet(rt->heap, target->object, index, value) != RoboOk) {
                    JsRuntimeThrow(rt, RoboJsErrorRange, NULL, "the array is at its size limit");
                    return RoboError;
                }
                return RoboOk;
            }
        }
        JsObjectSet(rt->heap, target->object, name, value);
        return RoboOk;
    }
    if (JsValueIsObject(target)) {
        Result status = JsObjectSet(rt->heap, target->object, name, value);

        if (status == RoboPermissionDenied) {
            return RoboOk; /* sloppy mode: a write to a read-only property is ignored, not an error */
        }
        if (status != RoboOk) {
            /* A host that refused the write may already have raised its own, specific reason — the DOM
             * binding names the refusal ("this document accepts no new nodes") where this generic line
             * could only say "a property write failed". A reason a layer below can state is the better
             * diagnostic, so it is kept. */
            if (!JsRuntimeHasException(rt)) {
                JsRuntimeThrow(rt, RoboJsErrorInternal, NULL, "a property write failed (%s)",
                               ErrorString(status));
            }
            return status;
        }
        return RoboOk;
    }
    /* Writing a property of a primitive is a silent no-op here. */
    return RoboOk;
}

/** Numeric coercion for the binary operators, with NaN propagation the arithmetic gets for free. */
static double JsToNumberOr(const JsValue *value, double fallback)
{
    if (value == NULL) {
        return fallback;
    }
    return JsValueToNumber(value);
}

static int JsNumberIsInteger(double value)
{
    return value == JsMathFloor(value) && JsMathAbs(value) < 1.0e15;
}

/* ------------------------------------------------------------------ */
/* Hoisting                                                            */
/* ------------------------------------------------------------------ */

static Result JsHoist(JsRuntime *rt, JsNode **list, int32_t count, JsEnv *env)
{
    int32_t i;

    for (i = 0; i < count; ++i) {
        JsNode *statement = list[i];

        if (statement == NULL) {
            continue;
        }
        if ((JsNodeKind)statement->kind == RoboJsNodeFunctionDeclaration) {
            JsObject *function;
            JsValue wrapped;

            function = JsFunctionCreateUser(rt->heap,
                                            statement->text,
                                            statement->params,
                                            statement->paramCount,
                                            statement->first,
                                            env,
                                            0,
                                            rt->functionPrototype);
            if (function == NULL) {
                JsRuntimeThrow(rt, RoboJsErrorInternal, NULL, "out of memory hoisting a function");
                return RoboOutOfMemory;
            }
            wrapped = JsValueObject(function);
            if (JsEnvDeclareVar(rt->heap, env, statement->text, &wrapped) != RoboOk) {
                return JsForcedFailure(rt, RoboOutOfMemory);
            }
            continue;
        }
        if ((JsNodeKind)statement->kind == RoboJsNodeVariableDeclaration) {
            int32_t d;

            for (d = 0; d < statement->count; ++d) {
                JsNode *declarator = statement->list[d];
                JsNode *target;
                JsValue undefined = JsValueUndefined();

                if (declarator == NULL || (JsNodeKind)declarator->kind != RoboJsNodeVariableDeclarator) {
                    continue;
                }
                target = declarator->first;
                if (target == NULL || target->text == NULL) {
                    continue;
                }
                /* **A `const` is hoisted *unwritable*.** The hoisting pass is where the
                 * binding comes into existence, so it is the only place the declaration's kind can be
                 * recorded: by the time the initializer runs, the write it performs is a write to an
                 * existing binding, and the mutability a later `x = …` is judged against has already
                 * been fixed. */
                if ((JsTokenKind)statement->op == RoboJsTokenConst) {
                    Result declared = JsEnvDeclareConst(rt->heap, env, target->text, &undefined);

                    if (declared != RoboOk && declared != RoboAlreadyExists) {
                        return JsForcedFailure(rt, RoboOutOfMemory);
                    }
                    continue;
                }
                if ((JsTokenKind)statement->op == RoboJsTokenVar) {
                    JsEnv *functionScope = JsEnvFunctionScope(env);
                    Result declared = JsEnvDeclare(rt->heap, functionScope, target->text, &undefined);

                    /* Declared **in this link**, so a `var` whose name an enclosing link also has
                     * still creates the shadow it is supposed to. `JsEnvHas` would have searched the
                     * chain and skipped the hoist; `RoboAlreadyExists` is a re-executed declaration
                     * rather than a failure. */
                    if (declared != RoboOk && declared != RoboAlreadyExists) {
                        return JsForcedFailure(rt, RoboOutOfMemory);
                    }
                } else if (!JsEnvHas(env, target->text)) {
                    if (JsEnvDeclare(rt->heap, env, target->text, &undefined) != RoboOk) {
                        return JsForcedFailure(rt, RoboOutOfMemory);
                    }
                }
            }
            continue;
        }
    }
    return RoboOk;
}

/* ------------------------------------------------------------------ */
/* Calls                                                               */
/* ------------------------------------------------------------------ */

static Result JsCallUserFunction(JsRuntime *rt,
                                 JsObject *function,
                                 const JsValue *thisValue,
                                 const JsValue *args,
                                 int32_t argCount,
                                 JsValue *out)
{
    JsEnv *callEnv;
    JsObject *receiver = NULL;
    JsCompletion completion;
    Result status;
    int32_t i;

    if (function->fn.closure == NULL) {
        JsRuntimeThrow(rt, RoboJsErrorInternal, NULL, "a function has no defining environment");
        return RoboError;
    }
    /* An arrow function's `this` was captured at creation; every other function answers with the
     * receiver it was called with. */
    if ((function->fn.flags & JsFunctionFlagArrow) != 0u) {
        receiver = JsValueIsObject(thisValue) ? thisValue->object
                                              : (JsObject *)function->fn.userdata;
    } else {
        receiver = JsValueIsObject(thisValue) ? thisValue->object : NULL;
    }
    callEnv = JsEnvCreate(rt->heap, function->fn.closure, RoboJsEnvFunction);
    if (callEnv == NULL) {
        JsRuntimeThrow(rt, RoboJsErrorInternal, NULL, "out of memory entering a call");
        return RoboOutOfMemory;
    }
    for (i = 0; i < function->fn.paramCount; ++i) {
        JsValue value = i < argCount && args != NULL ? args[i] : JsValueUndefined();

        if (JsEnvDeclare(rt->heap, callEnv, function->fn.params[i], &value) != RoboOk) {
            return JsForcedFailure(rt, RoboOutOfMemory);
        }
    }
    status = JsRuntimeEnterCall(rt, function->fn.body != NULL ? &function->fn.body->span : NULL);
    if (status != RoboOk) {
        return status;
    }
    if (function->fn.body != NULL &&
        (JsNodeKind)function->fn.body->kind == RoboJsNodeBlockStatement) {
        status = JsHoist(rt, function->fn.body->list, function->fn.body->count, callEnv);
    }
    if (status == RoboOk) {
        completion = JsCompletionNormal(NULL);
        status = JsEvalStmt(rt, function->fn.body, callEnv, receiver, &completion);
    }
    JsRuntimeLeaveCall(rt);
    if (status != RoboOk) {
        return status;
    }
    if (completion.kind == (uint32_t)RoboJsCompleteThrow) {
        JsRuntimeSetException(rt, &completion.value);
        *out = JsValueUndefined();
        return RoboError;
    }
    if (completion.kind == (uint32_t)RoboJsCompleteReturn) {
        *out = completion.value;
        return RoboOk;
    }
    if (completion.kind == (uint32_t)RoboJsCompleteBreak ||
        completion.kind == (uint32_t)RoboJsCompleteContinue) {
        JsRuntimeThrow(rt, RoboJsErrorSyntax, NULL, "%s outside a loop",
                       JsCompletionKindName((JsCompletionKind)completion.kind));
        return RoboError;
    }
    *out = JsValueUndefined();
    return RoboOk;
}

Result JsInterpCallValue(JsRuntime *rt,
                         const JsValue *callable,
                         JsObject *thisObject,
                         const JsValue *args,
                         int32_t argCount,
                         JsValue *out)
{
    JsObject *function;
    JsValue receiver;
    Result status;

    (void)out;
    if (rt == NULL || callable == NULL) {
        return RoboInvalidArgument;
    }
    if (!JsValueIsFunction(callable)) {
        JsRuntimeThrow(rt, RoboJsErrorType, NULL, "%s is not a function", JsValueTypeOfString(callable));
        return RoboError;
    }
    function = callable->object;
    receiver = thisObject != NULL ? JsValueObject(thisObject) : JsValueUndefined();
    if (function->fn.kind == (uint32_t)RoboJsFunctionNative) {
        JsValue local;

        if (function->fn.native == NULL) {
            JsRuntimeThrow(rt, RoboJsErrorInternal, NULL, "a native function has no binding");
            return RoboError;
        }
        status = JsRuntimeEnterCall(rt, NULL);
        if (status != RoboOk) {
            return status;
        }
        local = JsValueUndefined();
        status = function->fn.native(rt, thisObject, args, argCount, &local, function->fn.userdata);
        JsRuntimeLeaveCall(rt);
        if (status != RoboOk) {
            return JsForcedFailure(rt, status);
        }
        if (out != NULL) {
            *out = local;
        }
        return RoboOk;
    }
    return JsCallUserFunction(rt, function, &receiver, args, argCount, out);
}

/** A call from an expression: the callee, its receiver and its arguments. */
typedef struct JsCallSite {
    JsValue callee;
    JsObject *receiver;
} JsCallSite;

/** Evaluate a callee expression and the receiver it should be called with. */
static Result JsEvalCallee(JsRuntime *rt, JsNode *calleeNode, JsEnv *env, JsObject *thisObj,
                           JsCallSite *site)
{
    site->receiver = NULL;
    site->callee = JsValueUndefined();
    if (calleeNode != NULL && (JsNodeKind)calleeNode->kind == RoboJsNodeMemberExpression) {
        JsValue target;
        JsValue key;
        Result status;

        status = JsEvalExpr(rt, calleeNode->first, env, thisObj, &target);
        if (status != RoboOk) {
            return status;
        }
        if (calleeNode->intValue) {
            status = JsEvalExpr(rt, calleeNode->second, env, thisObj, &key);
        } else {
            key = JsValueString(rt->heap, calleeNode->second->text, calleeNode->second->textLength);
            status = RoboOk;
        }
        if (status != RoboOk) {
            return status;
        }
        site->receiver = JsValueIsObject(&target) ? target.object : NULL;
        return JsGetProperty(rt, &target, &key, &site->callee);
    }
    return JsEvalExpr(rt, calleeNode, env, thisObj, &site->callee);
}

/* ------------------------------------------------------------------ */
/* Expressions                                                         */
/* ------------------------------------------------------------------ */

static Result JsEvalArguments(JsRuntime *rt,
                              JsNode *call,
                              JsEnv *env,
                              JsObject *thisObj,
                              JsValue *args,
                              int32_t capacity,
                              int32_t *count)
{
    int32_t i;

    *count = call->count;
    if (*count > capacity) {
        *count = capacity;
    }
    for (i = 0; i < *count; ++i) {
        Result status = JsEvalExpr(rt, call->list[i], env, thisObj, &args[i]);

        if (status != RoboOk) {
            return status;
        }
    }
    return RoboOk;
}

static Result JsEvalArithmetic(JsRuntime *rt, JsTokenKind op, const JsValue *a, const JsValue *b,
                               JsValue *out)
{
    double left;
    double right;

    if (op == RoboJsTokenPlus && JsValuePlusConcatenates(a, b)) {
        char leftText[512];
        char rightText[512];
        char joined[1024];
        int32_t leftLength;
        int32_t rightLength;

        leftLength = JsValueToString(a, leftText, sizeof(leftText));
        rightLength = JsValueToString(b, rightText, sizeof(rightText));
        if (leftLength >= (int32_t)sizeof(leftText) ||
            rightLength >= (int32_t)sizeof(rightText) ||
            leftLength + rightLength >= (int32_t)sizeof(joined)) {
            JsRuntimeThrow(rt, RoboJsErrorRange, NULL, "the concatenation is longer than this build allows");
            return RoboError;
        }
        memcpy(joined, leftText, (size_t)leftLength);
        memcpy(joined + leftLength, rightText, (size_t)rightLength);
        joined[leftLength + rightLength] = '\0';
        *out = JsValueString(rt->heap, joined, leftLength + rightLength);
        return RoboOk;
    }
    left = JsToNumberOr(a, 0.0 / 0.0);
    right = JsToNumberOr(b, 0.0 / 0.0);
    switch (op) {
    case RoboJsTokenMinus:
        *out = JsNumberValue(left - right);
        return RoboOk;
    case RoboJsTokenStar:
        *out = JsNumberValue(left * right);
        return RoboOk;
    case RoboJsTokenSlash:
        *out = JsNumberValue(left / right);
        return RoboOk;
    case RoboJsTokenPercent:
        *out = JsNumberValue(JsMathFmod(left, right));
        return RoboOk;
    default:
        *out = JsNumberValue(left + right);
        return RoboOk;
    }
}

static Result JsEvalComparison(JsRuntime *rt, JsTokenKind op, const JsValue *a, const JsValue *b,
                               JsValue *out)
{
    (void)rt;
    switch (op) {
    case RoboJsTokenEqEq:
        *out = JsValueBool(JsValueLooseEquals(a, b));
        return RoboOk;
    case RoboJsTokenNotEq:
        *out = JsValueBool(!JsValueLooseEquals(a, b));
        return RoboOk;
    case RoboJsTokenEqEqEq:
        *out = JsValueBool(JsValueStrictEquals(a, b));
        return RoboOk;
    case RoboJsTokenNotEqEq:
        *out = JsValueBool(!JsValueStrictEquals(a, b));
        return RoboOk;
    default:
        break;
    }
    /* Relational: two strings compare by bytes, everything else numerically. `NaN` makes every
     * relational comparison false, which the arithmetic gives for free. */
    if (JsValueIsString(a) && JsValueIsString(b)) {
        int order = strcmp(a->string != NULL ? a->string : "",
                           b->string != NULL ? b->string : "");

        switch (op) {
        case RoboJsTokenLt: *out = JsValueBool(order < 0); break;
        case RoboJsTokenGt: *out = JsValueBool(order > 0); break;
        case RoboJsTokenLtEq: *out = JsValueBool(order <= 0); break;
        default: *out = JsValueBool(order >= 0); break;
        }
        return RoboOk;
    }
    {
        double left = JsToNumberOr(a, 0.0 / 0.0);
        double right = JsToNumberOr(b, 0.0 / 0.0);

        switch (op) {
        case RoboJsTokenLt: *out = JsValueBool(left < right); break;
        case RoboJsTokenGt: *out = JsValueBool(left > right); break;
        case RoboJsTokenLtEq: *out = JsValueBool(left <= right); break;
        default: *out = JsValueBool(left >= right); break;
        }
    }
    return RoboOk;
}

static Result JsEvalIdentifier(JsRuntime *rt, JsNode *node, JsEnv *env, JsValue *out)
{
    Result status;

    if (node->text == NULL) {
        *out = JsValueUndefined();
        return RoboOk;
    }
    status = JsEnvGet(env, node->text, out);
    if (status == RoboOk) {
        return RoboOk;
    }
    /* A global that lives on the global object but not in the environment — `console`, `document`,
     * `Math` — is not a `ReferenceError`. */
    if (JsObjectGetOwn(rt->globalObject, node->text, out) == RoboOk) {
        return RoboOk;
    }
    JsRuntimeThrow(rt, RoboJsErrorReference, &node->span, "%s is not defined", node->text);
    return RoboError;
}

static Result JsEvalUnary(JsRuntime *rt, JsNode *node, JsEnv *env, JsObject *thisObj, JsValue *out)
{
    JsTokenKind op = node->op;

    if (op == RoboJsTokenTypeof) {
        /* `typeof undeclaredName` is the one place a missing name is not an error, so it is answered
         * before the operand is evaluated. */
        if (node->first != NULL && (JsNodeKind)node->first->kind == RoboJsNodeIdentifier) {
            JsValue probe;

            if (JsEnvGet(env, node->first->text, &probe) != RoboOk &&
                JsObjectGetOwn(rt->globalObject, node->first->text, &probe) != RoboOk) {
                *out = JsValueString(rt->heap, "undefined", -1);
                return RoboOk;
            }
            *out = JsValueString(rt->heap, JsValueTypeOfString(&probe), -1);
            return RoboOk;
        }
        {
            JsValue operand;

            if (JsEvalExpr(rt, node->first, env, thisObj, &operand) != RoboOk) {
                return RoboError;
            }
            *out = JsValueString(rt->heap, JsValueTypeOfString(&operand), -1);
            return RoboOk;
        }
    }
    if (op == RoboJsTokenDelete) {
        if (node->first != NULL && (JsNodeKind)node->first->kind == RoboJsNodeIdentifier) {
            /* Deleting a variable is not allowed; deleting an unresolved name answers true. */
            *out = JsValueBool(0);
            return RoboOk;
        }
        if (node->first != NULL && (JsNodeKind)node->first->kind == RoboJsNodeMemberExpression) {
            JsValue target;
            JsValue key;
            char name[JsMaxLiteralString];

            if (JsEvalExpr(rt, node->first->first, env, thisObj, &target) != RoboOk) {
                return RoboError;
            }
            if (node->first->intValue) {
                if (JsEvalExpr(rt, node->first->second, env, thisObj, &key) != RoboOk) {
                    return RoboError;
                }
            } else {
                key = JsValueString(rt->heap, node->first->second->text,
                                    node->first->second->textLength);
            }
            JsInterpPropertyKey(&key, name, sizeof(name));
            if (JsValueIsObject(&target) && target.object->kind != (uint32_t)RoboJsObjectArray) {
                if (target.object == rt->globalObject) {
                    /* A global name has two readers (`JsRuntimeDefineGlobal`), so `delete` must drop
                     * both: dropping the property alone would leave `typeof name` answering. */
                    *out = JsValueBool(JsRuntimeDeleteGlobal(rt, name) == RoboOk);
                    return RoboOk;
                }
                *out = JsValueBool(JsObjectDelete(target.object, name) == RoboOk);
                return RoboOk;
            }
            *out = JsValueBool(0);
            return RoboOk;
        }
        *out = JsValueBool(1);
        return RoboOk;
    }
    {
        JsValue operand;
        Result status = JsEvalExpr(rt, node->first, env, thisObj, &operand);

        if (status != RoboOk) {
            return status;
        }
        switch (op) {
        case RoboJsTokenNot:
            *out = JsValueBool(!JsValueToBoolean(&operand));
            return RoboOk;
        case RoboJsTokenMinus:
            *out = JsNumberValue(-JsValueToNumber(&operand));
            return RoboOk;
        case RoboJsTokenPlus:
            *out = JsNumberValue(JsValueToNumber(&operand));
            return RoboOk;
        case RoboJsTokenVoid:
            *out = JsValueUndefined();
            return RoboOk;
        default:
            *out = operand;
            return RoboOk;
        }
    }
}

/** Read/write an lvalue (an identifier or a member) for the update and assignment operators. */
static Result JsReadLValue(JsRuntime *rt, JsNode *node, JsEnv *env, JsObject *thisObj, JsValue *out)
{
    return JsEvalExpr(rt, node, env, thisObj, out);
}

static Result JsWriteLValue(JsRuntime *rt, JsNode *node, JsEnv *env, JsObject *thisObj,
                            const JsValue *value)
{
    if ((JsNodeKind)node->kind == RoboJsNodeIdentifier) {
        Result status = JsEnvSet(env, node->text, value);

        if (status == RoboOk) {
            return RoboOk;
        }
        /* **A `const` is not a missing name.** `JsEnvSet` answers `RoboPermissionDenied` when the name
         * resolves to a binding whose writability was cleared, and falling through to the implicit-global
         * rule below would *create* the global the constant was supposed to shadow — turning the one
         * write `const` exists to refuse into a successful one. This is the arm that makes
         * `const x = 1; x = 2;` a `TypeError` rather than a silent global. */
        if (status == RoboPermissionDenied) {
            JsRuntimeThrow(rt, RoboJsErrorType, NULL,
                           "assignment to constant variable '%s'", node->text);
            return RoboError;
        }
        /* An assignment to a name nothing declares creates a global — sloppy mode's rule, and the
         * one that makes `foo = 1` at the top level of a script work. */
        return JsRuntimeDefineGlobal(rt, node->text, value);
    }
    if ((JsNodeKind)node->kind == RoboJsNodeMemberExpression) {
        JsValue target;
        JsValue key;
        Result status;

        status = JsEvalExpr(rt, node->first, env, thisObj, &target);
        if (status != RoboOk) {
            return status;
        }
        if (node->intValue) {
            status = JsEvalExpr(rt, node->second, env, thisObj, &key);
            if (status != RoboOk) {
                return status;
            }
        } else {
            key = JsValueString(rt->heap, node->second->text, node->second->textLength);
        }
        return JsSetProperty(rt, &target, &key, value);
    }
    JsRuntimeThrow(rt, RoboJsErrorReference, &node->span, "the left side of an assignment must be a variable or a property");
    return RoboError;
}

static Result JsEvalUpdate(JsRuntime *rt, JsNode *node, JsEnv *env, JsObject *thisObj, JsValue *out)
{
    JsValue current;
    double updated;
    Result status = JsReadLValue(rt, node->first, env, thisObj, &current);

    if (status != RoboOk) {
        return status;
    }
    updated = JsValueToNumber(&current);
    updated += node->op == RoboJsTokenPlusPlus ? 1.0 : -1.0;
    {
        JsValue written = JsNumberValue(updated);

        status = JsWriteLValue(rt, node->first, env, thisObj, &written);
        if (status != RoboOk) {
            return status;
        }
        /* Postfix answers the value *before* the write, prefix the value after it. */
        *out = node->boolean ? current : written;
        if (node->boolean && !JsValueIsNumber(&current)) {
            *out = JsNumberValue(JsValueToNumber(&current));
        }
    }
    return RoboOk;
}

static Result JsEvalMember(JsRuntime *rt, JsNode *node, JsEnv *env, JsObject *thisObj, JsValue *out)
{
    JsValue target;
    JsValue key;
    Result status;

    status = JsEvalExpr(rt, node->first, env, thisObj, &target);
    if (status != RoboOk) {
        return status;
    }
    if (node->intValue) {
        status = JsEvalExpr(rt, node->second, env, thisObj, &key);
        if (status != RoboOk) {
            return status;
        }
    } else {
        key = JsValueString(rt->heap, node->second->text, node->second->textLength);
    }
    /* `a.b` on a null or undefined target is the classic TypeError, and saying so is more useful
     * than answering `undefined`. */
    if (JsValueIsNullOrUndefined(&target)) {
        JsRuntimeThrow(rt,
                       RoboJsErrorType,
                       &node->span,
                       "cannot read a property of %s",
                       JsValueTypeOfString(&target));
        return RoboError;
    }
    return JsGetProperty(rt, &target, &key, out);
}

static Result JsEvalBinary(JsRuntime *rt, JsNode *node, JsEnv *env, JsObject *thisObj, JsValue *out)
{
    JsValue left;
    JsValue right;
    Result status;

    status = JsEvalExpr(rt, node->first, env, thisObj, &left);
    if (status != RoboOk) {
        return status;
    }
    if (node->op == RoboJsTokenIn) {
        JsValue target;

        status = JsEvalExpr(rt, node->second, env, thisObj, &target);
        if (status != RoboOk) {
            return status;
        }
        if (!JsValueIsObject(&target)) {
            JsRuntimeThrow(rt, RoboJsErrorType, &node->span, "in needs an object on the right");
            return RoboError;
        }
        {
            char name[JsMaxLiteralString];

            JsInterpPropertyKey(&left, name, sizeof(name));
            *out = JsValueBool(JsObjectHas(target.object, name));
        }
        return RoboOk;
    }
    if (node->op == RoboJsTokenInstanceof) {
        JsValue target;

        status = JsEvalExpr(rt, node->second, env, thisObj, &target);
        if (status != RoboOk) {
            return status;
        }
        if (!JsValueIsFunction(&target)) {
            JsRuntimeThrow(rt, RoboJsErrorType, &node->span, "instanceof needs a function on the right");
            return RoboError;
        }
        {
            JsValue prototypeValue;
            JsObject *cursor = JsValueIsObject(&left) ? left.object->prototype : NULL;
            JsObject *expected = NULL;

            if (JsObjectGet(target.object, "prototype", &prototypeValue) == RoboOk &&
                JsValueIsObject(&prototypeValue)) {
                expected = prototypeValue.object;
            }
            while (cursor != NULL) {
                if (cursor == expected) {
                    *out = JsValueBool(1);
                    return RoboOk;
                }
                cursor = cursor->prototype;
            }
        }
        *out = JsValueBool(0);
        return RoboOk;
    }
    status = JsEvalExpr(rt, node->second, env, thisObj, &right);
    if (status != RoboOk) {
        return status;
    }
    switch (node->op) {
    case RoboJsTokenEqEq:
    case RoboJsTokenNotEq:
    case RoboJsTokenEqEqEq:
    case RoboJsTokenNotEqEq:
    case RoboJsTokenLt:
    case RoboJsTokenGt:
    case RoboJsTokenLtEq:
    case RoboJsTokenGtEq:
        return JsEvalComparison(rt, node->op, &left, &right, out);
    default:
        return JsEvalArithmetic(rt, node->op, &left, &right, out);
    }
}

static Result JsEvalLogical(JsRuntime *rt, JsNode *node, JsEnv *env, JsObject *thisObj, JsValue *out)
{
    Result status = JsEvalExpr(rt, node->first, env, thisObj, out);

    if (status != RoboOk) {
        return status;
    }
    if (node->op == RoboJsTokenAndAnd) {
        if (!JsValueToBoolean(out)) {
            return RoboOk;
        }
        return JsEvalExpr(rt, node->second, env, thisObj, out);
    }
    if (JsValueToBoolean(out)) {
        return RoboOk;
    }
    return JsEvalExpr(rt, node->second, env, thisObj, out);
}

static Result JsEvalAssignment(JsRuntime *rt, JsNode *node, JsEnv *env, JsObject *thisObj, JsValue *out)
{
    JsValue right;
    Result status;

    if (node->op == RoboJsTokenAssign) {
        status = JsEvalExpr(rt, node->second, env, thisObj, &right);
        if (status != RoboOk) {
            return status;
        }
        status = JsWriteLValue(rt, node->first, env, thisObj, &right);
        if (status != RoboOk) {
            return status;
        }
        *out = right;
        return RoboOk;
    }
    {
        JsValue left;
        JsTokenKind op;

        switch (node->op) {
        case RoboJsTokenPlusAssign: op = RoboJsTokenPlus; break;
        case RoboJsTokenMinusAssign: op = RoboJsTokenMinus; break;
        case RoboJsTokenStarAssign: op = RoboJsTokenStar; break;
        case RoboJsTokenSlashAssign: op = RoboJsTokenSlash; break;
        default: op = RoboJsTokenPercent; break;
        }
        status = JsReadLValue(rt, node->first, env, thisObj, &left);
        if (status != RoboOk) {
            return status;
        }
        status = JsEvalExpr(rt, node->second, env, thisObj, &right);
        if (status != RoboOk) {
            return status;
        }
        status = JsEvalArithmetic(rt, op, &left, &right, out);
        if (status != RoboOk) {
            return status;
        }
        return JsWriteLValue(rt, node->first, env, thisObj, out);
    }
}

static Result JsEvalObjectLiteral(JsRuntime *rt, JsNode *node, JsEnv *env, JsObject *thisObj,
                                  JsValue *out)
{
    JsObject *object = JsObjectCreate(rt->heap, rt->objectPrototype);
    int32_t i;

    if (object == NULL) {
        JsRuntimeThrow(rt, RoboJsErrorInternal, NULL, "out of memory creating an object");
        return RoboOutOfMemory;
    }
    for (i = 0; i < node->count; ++i) {
        JsNode *property = node->list[i];
        JsValue value;

        if (property == NULL || (JsNodeKind)property->kind != RoboJsNodeProperty) {
            continue;
        }
        if (JsEvalExpr(rt, property->second, env, thisObj, &value) != RoboOk) {
            return RoboError;
        }
        if (property->first == NULL || property->first->text == NULL) {
            continue;
        }
        if (JsObjectSet(rt->heap, object, property->first->text, &value) != RoboOk) {
            JsRuntimeThrow(rt, RoboJsErrorInternal, NULL, "out of memory setting a property");
            return RoboOutOfMemory;
        }
    }
    *out = JsValueObject(object);
    return RoboOk;
}

static Result JsEvalArrayLiteral(JsRuntime *rt, JsNode *node, JsEnv *env, JsObject *thisObj,
                                 JsValue *out)
{
    JsObject *array = JsArrayCreate(rt->heap, rt->arrayPrototype, 0);
    int32_t i;

    if (array == NULL) {
        JsRuntimeThrow(rt, RoboJsErrorInternal, NULL, "out of memory creating an array");
        return RoboOutOfMemory;
    }
    for (i = 0; i < node->count; ++i) {
        JsValue item;

        if (JsEvalExpr(rt, node->list[i], env, thisObj, &item) != RoboOk) {
            return RoboError;
        }
        if (JsArrayPush(rt->heap, array, &item) < 0) {
            JsRuntimeThrow(rt, RoboJsErrorRange, NULL, "the array is at its size limit");
            return RoboError;
        }
    }
    *out = JsValueObject(array);
    return RoboOk;
}

static Result JsEvalFunctionNode(JsRuntime *rt, JsNode *node, JsEnv *env, JsObject *thisObj,
                                 JsValue *out)
{
    int isArrow = (JsNodeKind)node->kind == RoboJsNodeArrowFunction;
    JsObject *function = JsFunctionCreateUser(rt->heap,
                                              node->text,
                                              node->params,
                                              node->paramCount,
                                              node->first,
                                              env,
                                              isArrow,
                                              rt->functionPrototype);

    if (function == NULL) {
        JsRuntimeThrow(rt, RoboJsErrorInternal, NULL, "out of memory creating a function");
        return RoboOutOfMemory;
    }
    if (isArrow) {
        /* `this` is lexical for an arrow: it is captured here, from the environment that is defining
         * it, rather than taken from the call site. */
        function->fn.userdata = thisObj;
    }
    *out = JsValueObject(function);
    return RoboOk;
}

static Result JsEvalExpr(JsRuntime *rt, JsNode *node, JsEnv *env, JsObject *thisObj, JsValue *out)
{
    Result status;

    if (out == NULL) {
        return RoboInvalidArgument;
    }
    *out = JsValueUndefined();
    if (node == NULL || env == NULL) {
        return RoboInvalidArgument;
    }
    /* Where the script is, for an error the engine raises with no span of its own: an innermost node
     * wins, so a native door complaining inside a call is located at the call (`RoboJSException.c`). */
    rt->currentSpan = node->span;
    rt->hasCurrentSpan = 1;
    status = JsRuntimeSpendStep(rt, &node->span);
    if (status != RoboOk) {
        return status;
    }
    switch ((JsNodeKind)node->kind) {
    case RoboJsNodeNumberLiteral:
        *out = JsValueNumber(node->number);
        return RoboOk;
    case RoboJsNodeStringLiteral:
        *out = JsValueString(rt->heap, node->text, node->textLength);
        return RoboOk;
    case RoboJsNodeBooleanLiteral:
        *out = JsValueBool(node->boolean);
        return RoboOk;
    case RoboJsNodeNullLiteral:
        *out = JsValueNull();
        return RoboOk;
    case RoboJsNodeUndefinedLiteral:
        *out = JsValueUndefined();
        return RoboOk;
    case RoboJsNodeIdentifier:
        return JsEvalIdentifier(rt, node, env, out);
    case RoboJsNodeThisExpression:
        *out = thisObj != NULL ? JsValueObject(thisObj) : JsValueObject(rt->globalObject);
        return RoboOk;
    case RoboJsNodeArrayLiteral:
        return JsEvalArrayLiteral(rt, node, env, thisObj, out);
    case RoboJsNodeObjectLiteral:
        return JsEvalObjectLiteral(rt, node, env, thisObj, out);
    case RoboJsNodeFunctionExpression:
    case RoboJsNodeArrowFunction:
        return JsEvalFunctionNode(rt, node, env, thisObj, out);
    case RoboJsNodeUnaryExpression:
        return JsEvalUnary(rt, node, env, thisObj, out);
    case RoboJsNodeUpdateExpression:
        return JsEvalUpdate(rt, node, env, thisObj, out);
    case RoboJsNodeBinaryExpression:
        return JsEvalBinary(rt, node, env, thisObj, out);
    case RoboJsNodeLogicalExpression:
        return JsEvalLogical(rt, node, env, thisObj, out);
    case RoboJsNodeAssignmentExpression:
        return JsEvalAssignment(rt, node, env, thisObj, out);
    case RoboJsNodeConditionalExpression: {
        JsValue test;

        status = JsEvalExpr(rt, node->first, env, thisObj, &test);
        if (status != RoboOk) {
            return status;
        }
        if (JsValueToBoolean(&test)) {
            return JsEvalExpr(rt, node->second, env, thisObj, out);
        }
        return JsEvalExpr(rt, node->third, env, thisObj, out);
    }
    case RoboJsNodeMemberExpression:
        return JsEvalMember(rt, node, env, thisObj, out);
    case RoboJsNodeCallExpression: {
        JsCallSite site;
        JsValue args[16];
        int32_t argCount = 0;

        status = JsEvalCallee(rt, node->first, env, thisObj, &site);
        if (status != RoboOk) {
            return status;
        }
        status = JsEvalArguments(rt, node, env, thisObj, args, 16, &argCount);
        if (status != RoboOk) {
            return status;
        }
        if (node->count > 16) {
            JsRuntimeThrow(rt, RoboJsErrorRange, &node->span,
                           "a call may not take more than 16 arguments in this build");
            return RoboError;
        }
        return JsInterpCallValue(rt, &site.callee, site.receiver, args, argCount, out);
    }
    case RoboJsNodeNewExpression: {
        JsCallSite site;
        JsValue args[16];
        int32_t argCount = 0;

        status = JsEvalCallee(rt, node->first, env, thisObj, &site);
        if (status != RoboOk) {
            return status;
        }
        status = JsEvalArguments(rt, node, env, thisObj, args, 16, &argCount);
        if (status != RoboOk) {
            return status;
        }
        if (node->count > 16) {
            JsRuntimeThrow(rt, RoboJsErrorRange, &node->span,
                           "a constructor may not take more than 16 arguments in this build");
            return RoboError;
        }
        return JsFunctionConstruct(rt, &site.callee, args, argCount, out);
    }
    case RoboJsNodeSequenceExpression: {
        int32_t i;

        for (i = 0; i < node->count; ++i) {
            status = JsEvalExpr(rt, node->list[i], env, thisObj, out);
            if (status != RoboOk) {
                return status;
            }
        }
        return RoboOk;
    }
    case RoboJsNodeFunctionDeclaration:
        /* Already hoisted: the statement's own execution has nothing left to do. */
        *out = JsValueUndefined();
        return RoboOk;
    default:
        JsRuntimeThrow(rt,
                       RoboJsErrorInternal,
                       &node->span,
                       "%s is not an expression",
                       JsNodeKindName((JsNodeKind)node->kind));
        return RoboError;
    }
}

/* ------------------------------------------------------------------ */
/* Statements                                                          */
/* ------------------------------------------------------------------ */

static Result JsEvalList(JsRuntime *rt,
                         JsNode **list,
                         int32_t count,
                         JsEnv *env,
                         JsObject *thisObj,
                         JsCompletion *out)
{
    int32_t i;
    JsValue last = JsValueUndefined();

    for (i = 0; i < count; ++i) {
        JsCompletion step = JsCompletionNormal(NULL);
        Result status = JsEvalStmt(rt, list[i], env, thisObj, &step);

        if (status != RoboOk) {
            return status;
        }
        if (step.kind != (uint32_t)RoboJsCompleteNormal) {
            *out = step;
            return RoboOk;
        }
        last = step.value;
    }
    *out = JsCompletionNormal(&last);
    return RoboOk;
}

static Result JsEvalBlock(JsRuntime *rt, JsNode *node, JsEnv *env, JsObject *thisObj,
                          JsCompletion *out)
{
    JsEnv *blockEnv;
    Result status;

    blockEnv = JsEnvCreate(rt->heap, env, RoboJsEnvBlock);
    if (blockEnv == NULL) {
        JsRuntimeThrow(rt, RoboJsErrorInternal, &node->span, "out of memory entering a block");
        return RoboOutOfMemory;
    }
    status = JsHoist(rt, node->list, node->count, blockEnv);
    if (status != RoboOk) {
        return status;
    }
    return JsEvalList(rt, node->list, node->count, blockEnv, thisObj, out);
}

static Result JsEvalVariableDeclaration(JsRuntime *rt, JsNode *node, JsEnv *env, JsObject *thisObj,
                                        JsCompletion *out)
{
    int32_t i;

    for (i = 0; i < node->count; ++i) {
        JsNode *declarator = node->list[i];
        JsNode *target;
        JsValue value = JsValueUndefined();
        Result status;

        if (declarator == NULL || (JsNodeKind)declarator->kind != RoboJsNodeVariableDeclarator) {
            continue;
        }
        target = declarator->first;
        if (target == NULL || target->text == NULL) {
            continue;
        }
        if (declarator->second != NULL) {
            status = JsEvalExpr(rt, declarator->second, env, thisObj, &value);
            if (status != RoboOk) {
                return status;
            }
        }
        if ((JsTokenKind)node->op == RoboJsTokenVar) {
            /* The *function* link, and by name **in that link**: `JsEnvSet` searches the chain, so it
             * would write an enclosing function's (or the global) binding when this link's own `var`
             * is the one the script means. `var outer` inside a function must not overwrite a global
             * `outer`, and that is a shadowing rule, not an assignment. */
            if (JsEnvDeclareOrSet(rt->heap, JsEnvFunctionScope(env), target->text, &value) != RoboOk) {
                return JsForcedFailure(rt, RoboOutOfMemory);
            }
        } else if (JsEnvDeclareOrSet(rt->heap, env, target->text, &value) != RoboOk) {
            return JsForcedFailure(rt, RoboOutOfMemory);
        }
    }
    *out = JsCompletionNormal(NULL);
    return RoboOk;
}

/*
 * Is the failure in @p rt a JavaScript throw the script may catch?
 *
 * A `throw` statement travels as a completion, but a throw raised **inside a call** cannot: it leaves
 * `JsCallUserFunction` through a door whose only channel is a status, and that door leaves the value
 * in the runtime's pending-exception slot (`RoboJSException.h`). This turns the second form back into
 * the completion the rest of the machinery already understands, which is what makes
 *
 *     try { f(); } catch (e) { ... }        // f throws
 *
 * work for the same reason `try { throw v; } catch (e) { ... }` does.
 *
 * The test is the **kind**, not the presence of an exception: `RoboJsErrorInternal` is the engine's
 * own failure and `RoboJsErrorLimit` is a budget the OS set (`RoboJSTypes.h` calls the first "never a
 * script's"), and a script catching either would be swallowing the runtime's own limits.
 *
 * @return 1 when it filled @p completion with the thrown value, 0 when this is not a catchable throw.
 */
static int JsTryCatchable(JsRuntime *rt, JsCompletion *completion)
{
    JsValue pending;
    JsErrorKind kind;

    if (completion == NULL || JsRuntimePeekException(rt, &pending) != RoboOk) {
        return 0;
    }
    kind = JsExceptionKindOf(&pending);
    if (kind == RoboJsErrorInternal || kind == RoboJsErrorLimit) {
        return 0;
    }
    *completion = JsCompletionOf(RoboJsCompleteThrow, &pending);
    return 1;
}

static Result JsEvalTry(JsRuntime *rt, JsNode *node, JsEnv *env, JsObject *thisObj,
                        JsCompletion *out)
{
    JsCompletion completion = JsCompletionNormal(NULL);
    Result status = JsEvalStmt(rt, node->first, env, thisObj, &completion);

    if (status != RoboOk && (uint32_t)completion.kind != (uint32_t)RoboJsCompleteThrow &&
        JsTryCatchable(rt, &completion)) {
        status = RoboOk;
    }
    if (status != RoboOk && completion.kind != (uint32_t)RoboJsCompleteThrow) {
        /* An engine failure (a limit) is not a JavaScript exception: `catch` must not swallow it, so
         * it propagates and the `finally` block still runs below. */
        if (node->third != NULL) {
            JsCompletion ignored = JsCompletionNormal(NULL);

            JsEvalStmt(rt, node->third, env, thisObj, &ignored);
        }
        return status;
    }
    if (completion.kind == (uint32_t)RoboJsCompleteThrow && node->second != NULL) {
        JsNode *clause = node->second;
        JsEnv *catchEnv = JsEnvCreate(rt->heap, env, RoboJsEnvBlock);

        if (catchEnv == NULL) {
            JsRuntimeThrow(rt, RoboJsErrorInternal, &node->span, "out of memory entering a catch");
            return RoboOutOfMemory;
        }
        if (clause->first != NULL && clause->first->text != NULL) {
            if (JsEnvDeclare(rt->heap, catchEnv, clause->first->text, &completion.value) != RoboOk &&
                JsEnvDeclareOrSet(rt->heap, catchEnv, clause->first->text, &completion.value) !=
                    RoboOk) {
                return JsForcedFailure(rt, RoboOutOfMemory);
            }
        }
        /* The thrown value is being handled: nothing is pending any more, so a `throw` inside the
         * catch replaces the original rather than being read as an uncaught error. */
        JsRuntimeClearException(rt);
        if (clause->second != NULL) {
            JsEnv *bodyEnv = JsEnvCreate(rt->heap, catchEnv, RoboJsEnvBlock);

            if (bodyEnv == NULL) {
                return JsForcedFailure(rt, RoboOutOfMemory);
            }
            if (JsHoist(rt, clause->second->list, clause->second->count, bodyEnv) != RoboOk) {
                return RoboError;
            }
            status = JsEvalList(rt,
                                clause->second->list,
                                clause->second->count,
                                bodyEnv,
                                thisObj,
                                &completion);
        } else {
            completion = JsCompletionNormal(NULL);
        }
        if (status != RoboOk && completion.kind != (uint32_t)RoboJsCompleteThrow) {
            if (node->third != NULL) {
                JsCompletion ignored = JsCompletionNormal(NULL);

                JsEvalStmt(rt, node->third, env, thisObj, &ignored);
            }
            return status;
        }
    }
    if (node->third != NULL) {
        JsCompletion finalCompletion = JsCompletionNormal(NULL);
        Result finalStatus = JsEvalStmt(rt, node->third, env, thisObj, &finalCompletion);

        if (finalStatus != RoboOk && finalCompletion.kind != (uint32_t)RoboJsCompleteThrow) {
            return finalStatus;
        }
        /* A non-normal completion from `finally` replaces the one that was travelling. */
        if (finalCompletion.kind != (uint32_t)RoboJsCompleteNormal) {
            *out = finalCompletion;
            return RoboOk;
        }
    }
    if (completion.kind == (uint32_t)RoboJsCompleteThrow) {
        JsRuntimeSetException(rt, &completion.value);
        *out = completion;
        return RoboOk;
    }
    *out = completion;
    return RoboOk;
}

static Result JsEvalStatement(JsRuntime *rt,
                              JsNode *node,
                              JsEnv *env,
                              JsObject *thisObj,
                              JsCompletion *out)
{
    Result status;

    if (out == NULL) {
        return RoboInvalidArgument;
    }
    *out = JsCompletionNormal(NULL);
    if (node != NULL) {
        rt->currentSpan = node->span;
        rt->hasCurrentSpan = 1;
    }
    if (node == NULL) {
        return RoboInvalidArgument;
    }
    status = JsRuntimeSpendStep(rt, &node->span);
    if (status != RoboOk) {
        return status;
    }
    switch ((JsNodeKind)node->kind) {
    case RoboJsNodeProgram:
        return JsEvalList(rt, node->list, node->count, env, thisObj, out);
    case RoboJsNodeBlockStatement:
        return JsEvalBlock(rt, node, env, thisObj, out);
    case RoboJsNodeEmptyStatement:
        return RoboOk;
    case RoboJsNodeExpressionStatement: {
        JsValue value;

        status = JsEvalExpr(rt, node->first, env, thisObj, &value);
        if (status != RoboOk) {
            return status;
        }
        *out = JsCompletionNormal(&value);
        return RoboOk;
    }
    case RoboJsNodeVariableDeclaration:
        return JsEvalVariableDeclaration(rt, node, env, thisObj, out);
    case RoboJsNodeFunctionDeclaration:
        return RoboOk;
    case RoboJsNodeIfStatement: {
        JsValue test;

        status = JsEvalExpr(rt, node->first, env, thisObj, &test);
        if (status != RoboOk) {
            return status;
        }
        if (JsValueToBoolean(&test)) {
            return JsEvalStmt(rt, node->second, env, thisObj, out);
        }
        if (node->third != NULL) {
            return JsEvalStmt(rt, node->third, env, thisObj, out);
        }
        return RoboOk;
    }
    case RoboJsNodeWhileStatement:
    case RoboJsNodeDoWhileStatement: {
        int isDo = (JsNodeKind)node->kind == RoboJsNodeDoWhileStatement;
        JsNode *testNode = node->first;
        JsNode *bodyNode = node->second;
        int first = 1;

        for (;;) {
            JsValue test;

            if (!isDo || !first) {
                status = JsEvalExpr(rt, testNode, env, thisObj, &test);
                if (status != RoboOk) {
                    return status;
                }
                if (!JsValueToBoolean(&test)) {
                    break;
                }
            }
            first = 0;
            {
                JsCompletion body = JsCompletionNormal(NULL);

                status = JsEvalStmt(rt, bodyNode, env, thisObj, &body);
                if (status != RoboOk) {
                    return status;
                }
                if (body.kind == (uint32_t)RoboJsCompleteBreak) {
                    break;
                }
                if (body.kind == (uint32_t)RoboJsCompleteReturn ||
                    body.kind == (uint32_t)RoboJsCompleteThrow) {
                    *out = body;
                    return RoboOk;
                }
            }
        }
        *out = JsCompletionNormal(NULL);
        return RoboOk;
    }
    case RoboJsNodeForStatement: {
        JsEnv *loopEnv = JsEnvCreate(rt->heap, env, RoboJsEnvBlock);

        if (loopEnv == NULL) {
            return JsForcedFailure(rt, RoboOutOfMemory);
        }
        if (node->first != NULL) {
            if ((JsNodeKind)node->first->kind == RoboJsNodeVariableDeclaration) {
                /* A `let` loop variable is hoisted into the loop's own block, so each iteration
                 * reads the binding the loop owns rather than the enclosing one. */
                status = JsHoist(rt, &node->first, 1, loopEnv);
                if (status != RoboOk) {
                    return status;
                }
            }
            {
                JsCompletion init = JsCompletionNormal(NULL);

                if ((JsNodeKind)node->first->kind == RoboJsNodeVariableDeclaration ||
                    (JsNodeKind)node->first->kind == RoboJsNodeExpressionStatement) {
                    status = JsEvalStmt(rt, node->first, loopEnv, thisObj, &init);
                } else {
                    JsValue ignored;

                    status = JsEvalExpr(rt, node->first, loopEnv, thisObj, &ignored);
                }
                if (status != RoboOk) {
                    return status;
                }
            }
        }
        for (;;) {
            if (node->second != NULL) {
                JsValue test;

                status = JsEvalExpr(rt, node->second, loopEnv, thisObj, &test);
                if (status != RoboOk) {
                    return status;
                }
                if (!JsValueToBoolean(&test)) {
                    break;
                }
            }
            if (node->count > 0) {
                JsCompletion body = JsCompletionNormal(NULL);

                status = JsEvalStmt(rt, node->list[0], loopEnv, thisObj, &body);
                if (status != RoboOk) {
                    return status;
                }
                if (body.kind == (uint32_t)RoboJsCompleteBreak) {
                    break;
                }
                if (body.kind == (uint32_t)RoboJsCompleteReturn ||
                    body.kind == (uint32_t)RoboJsCompleteThrow) {
                    *out = body;
                    return RoboOk;
                }
            }
            if (node->third != NULL) {
                JsValue ignored;

                status = JsEvalExpr(rt, node->third, loopEnv, thisObj, &ignored);
                if (status != RoboOk) {
                    return status;
                }
            }
        }
        *out = JsCompletionNormal(NULL);
        return RoboOk;
    }
    case RoboJsNodeBreakStatement:
        *out = JsCompletionOf(RoboJsCompleteBreak, NULL);
        return RoboOk;
    case RoboJsNodeContinueStatement:
        *out = JsCompletionOf(RoboJsCompleteContinue, NULL);
        return RoboOk;
    case RoboJsNodeReturnStatement: {
        JsValue value = JsValueUndefined();

        if (node->first != NULL) {
            status = JsEvalExpr(rt, node->first, env, thisObj, &value);
            if (status != RoboOk) {
                return status;
            }
        }
        *out = JsCompletionOf(RoboJsCompleteReturn, &value);
        return RoboOk;
    }
    case RoboJsNodeThrowStatement: {
        JsValue value;

        status = JsEvalExpr(rt, node->first, env, thisObj, &value);
        if (status != RoboOk) {
            return status;
        }
        *out = JsCompletionOf(RoboJsCompleteThrow, &value);
        return RoboOk;
    }
    case RoboJsNodeTryStatement:
        return JsEvalTry(rt, node, env, thisObj, out);
    case RoboJsNodeSwitchStatement: {
        JsValue discriminant;
        int32_t matched = -1;
        int32_t defaultIndex = -1;
        int32_t i;

        status = JsEvalExpr(rt, node->first, env, thisObj, &discriminant);
        if (status != RoboOk) {
            return status;
        }
        for (i = 0; i < node->count; ++i) {
            JsNode *clause = node->list[i];

            if (clause == NULL) {
                continue;
            }
            if (clause->first == NULL) {
                defaultIndex = i;
                continue;
            }
            {
                JsValue test;

                status = JsEvalExpr(rt, clause->first, env, thisObj, &test);
                if (status != RoboOk) {
                    return status;
                }
                if (JsValueStrictEquals(&discriminant, &test)) {
                    matched = i;
                    break;
                }
            }
        }
        if (matched < 0) {
            matched = defaultIndex;
        }
        if (matched < 0) {
            return RoboOk;
        }
        for (i = matched; i < node->count; ++i) {
            JsNode *clause = node->list[i];
            JsCompletion body = JsCompletionNormal(NULL);

            if (clause == NULL) {
                continue;
            }
            status = JsEvalList(rt, clause->list, clause->count, env, thisObj, &body);
            if (status != RoboOk) {
                return status;
            }
            if (body.kind == (uint32_t)RoboJsCompleteBreak) {
                return RoboOk;
            }
            if (body.kind != (uint32_t)RoboJsCompleteNormal) {
                *out = body;
                return RoboOk;
            }
        }
        return RoboOk;
    }
    default:
        JsRuntimeThrow(rt,
                       RoboJsErrorInternal,
                       &node->span,
                       "%s is not a statement",
                       JsNodeKindName((JsNodeKind)node->kind));
        return RoboError;
    }
}

static Result JsEvalStmt(JsRuntime *rt,
                         JsNode *node,
                         JsEnv *env,
                         JsObject *thisObj,
                         JsCompletion *out)
{
    return JsEvalStatement(rt, node, env, thisObj, out);
}

/* ------------------------------------------------------------------ */
/* Entry points                                                        */
/* ------------------------------------------------------------------ */

Result JsInterpRunProgram(JsRuntime *rt, JsNode *program, JsEnv *env, JsScriptResult *out)
{
    JsCompletion completion;
    Result status;

    if (rt == NULL || program == NULL || env == NULL || out == NULL) {
        return RoboInvalidArgument;
    }
    if ((JsNodeKind)program->kind != RoboJsNodeProgram) {
        return RoboInvalidArgument;
    }
    memset(out, 0, sizeof(*out));
    JsErrorInit(&out->error);
    status = JsHoist(rt, program->list, program->count, env);
    if (status != RoboOk) {
        out->status = status;
        JsErrorSet(&out->error, RoboJsErrorInternal, NULL, "hoisting failed");
        return status;
    }
    completion = JsCompletionNormal(NULL);
    status = JsEvalList(rt, program->list, program->count, env, rt->globalObject, &completion);
    out->steps = rt->steps;
    out->depth = rt->maxDepthReached;
    out->statements = program->count;
    if (status != RoboOk && (uint32_t)completion.kind != (uint32_t)RoboJsCompleteThrow &&
        JsTryCatchable(rt, &completion)) {
        /* An uncaught throw from inside a call: report it as the JavaScript error it is (kind and
         * value below) rather than as the limit the failure status alone would suggest. */
        status = RoboOk;
    }
    if (status != RoboOk && completion.kind != (uint32_t)RoboJsCompleteThrow) {
        out->status = status;
        out->error.kind = RoboJsErrorLimit;
        {
            JsValue pending;

            if (JsRuntimePeekException(rt, &pending) == RoboOk) {
                JsExceptionMessage(&pending, out->error.message, sizeof(out->error.message));
            }
        }
        return status;
    }
    if (completion.kind == (uint32_t)RoboJsCompleteThrow) {
        JsRuntimeSetException(rt, &completion.value);
        out->status = RoboError;
        out->value = completion.value;
        return RoboError;
    }
    out->status = RoboOk;
    out->value = completion.value;
    return RoboOk;
}

Result JsInterpRunExpression(JsRuntime *rt, JsNode *expression, JsEnv *env, JsValue *out)
{
    Result status;

    if (rt == NULL || expression == NULL || env == NULL || out == NULL) {
        return RoboInvalidArgument;
    }
    *out = JsValueUndefined();
    rt->steps = 0;
    status = JsEvalExpr(rt, expression, env, rt->globalObject, out);
    if (status == RoboOk) {
        rt->evaluationCount += 1;
    }
    return status;
}
