/*
 * RoboJS functions (Phase 16.6) — one callable shape for two very different things.
 *
 * A JavaScript function and a native binding differ in exactly one field: `kind`. Everything a
 * caller does with a function — call it, read its `name`, read its `length`, pass it to
 * `addEventListener` — is the same either way, which is what lets `setTimeout` accept a user
 * function and `Function.prototype.call` accept a native one without either knowing what the other
 * is.
 *
 * ## A user function's environment is the closure
 *
 * `closure` is the environment the function was *defined* in, not the one it is called from. That
 * one field is the whole of 16.7: a returned function keeps its defining environment alive because
 * nothing frees an environment until the runtime is reset (`RoboJSHeap.h`), so an environment
 * outliving the call that created it is the normal case rather than a special case. `params` and
 * `body` are AST references, not copies.
 *
 * ## A native function never sees its arguments as a value record
 *
 * `JsNativeFn` takes `(this, args, argCount)`, and `JsValue` is by-value, so a native binding reads
 * its parameters without an allocation. A binding that fails answers a non-OK `Result` **after**
 * setting the runtime's pending exception (`JsRuntimeSetException`), never by reporting a broken
 * value: the interpreter distinguishes "the call failed" from "the call returned undefined", and a
 * binding that could not tell them apart would swallow an error.
 *
 * ## `this` and `new`
 *
 * `JsFunctionCall` passes `this` through untouched — a plain call passes `undefined`, a method call
 * passes the receiver, and `new` passes a freshly created object whose prototype is the function's
 * `prototype` property. `JsFunctionConstruct` is the second door and returns the constructed object;
 * a user function that returns an object overrides it, which is the standard rule.
 */

#ifndef ROBO_JS_FUNCTION_H
#define ROBO_JS_FUNCTION_H

#include "RoboJS/RoboJSTypes.h"
#include "RoboJS/RoboJSValue.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Function payload kinds. */
typedef enum JsFunctionKind {
    RoboJsFunctionUser = 1,
    RoboJsFunctionNative = 2
} JsFunctionKind;

/**
 * A native binding. `thisObject` is NULL for a plain call (`undefined`), `args` may be NULL when
 * `argCount` is 0, and `userdata` is whatever the registering door was given.
 *
 * @return RoboOk with @p out filled, or a non-OK `Result` **after** setting the runtime's pending
 *         exception. Never leaves @p out unspecified on RoboOk.
 */
typedef Result (*JsNativeFn)(JsRuntime *runtime,
                             JsObject *thisObject,
                             const JsValue *args,
                             int32_t argCount,
                             JsValue *out,
                             void *userdata);

/** The callable payload embedded in a function object. */
typedef struct JsFunction {
    uint32_t kind;      /* JsFunctionKind */
    uint32_t arity;     /* the declared parameter count, as `length` reports it */
    uint32_t flags;     /* JsFunctionFlag* */
    const char *name;   /* heap-owned, `""` when anonymous */
    const char **params;
    int32_t paramCount;
    JsNode *body;       /* user functions: the block statement */
    JsEnv *closure;     /* user functions: the defining environment */
    JsNativeFn native;
    void *userdata;
} JsFunction;

/** An arrow function: `this` is the closure's, not the call's. */
#define JsFunctionFlagArrow 1u
/** A declaration hoisted before the enclosing block ran. */
#define JsFunctionFlagDeclared 2u

/**
 * Create a user function object. @p params is copied into the heap (a NULL @p params is allowed
 * with @p paramCount 0); @p body may be NULL for an empty function.
 *
 * @p prototype is the object the new function's internal `[[Prototype]]` is set to
 * (`JsRuntimeFunctionPrototype`); a NULL @p prototype means "no prototype", which only a test wants.
 * The function also gets a fresh `prototype` **property** (an empty object) unless it is an arrow
 * function, which is what makes `new` work.
 */
JsObject *JsFunctionCreateUser(JsHeap *heap,
                               const char *name,
                               const char **params,
                               int32_t paramCount,
                               JsNode *body,
                               JsEnv *closure,
                               int isArrow,
                               JsObject *prototype);

/**
 * Create a native function object. @p arity is the declared `length`.
 */
JsObject *JsFunctionCreateNative(JsHeap *heap,
                                 const char *name,
                                 int32_t arity,
                                 JsNativeFn native,
                                 void *userdata,
                                 JsObject *prototype);

/** The payload of a function value, or NULL when the value is not callable. */
JsFunction *JsFunctionOf(const JsValue *value);
/** The payload of a function object, or NULL. */
JsFunction *JsFunctionOfObject(JsObject *object);

/**
 * Call @p callable. @p thisValue may be NULL (meaning `undefined`); @p args may be NULL with
 * @p argCount 0.
 *
 * @return RoboInvalidArgument when @p callable is not callable or @p out is NULL; a non-OK result
 *         from the callee (with the runtime's exception set) when the call threw; RoboOk otherwise.
 *         A `throw` inside the function is *not* an error here — it is the runtime's pending
 *         exception and the caller's job to propagate.
 */
Result JsFunctionCall(JsRuntime *runtime,
                      const JsValue *callable,
                      const JsValue *thisValue,
                      const JsValue *args,
                      int32_t argCount,
                      JsValue *out);

/**
 * `new callable(...)`. Creates the object, sets its prototype from the function's own `prototype`
 * property, calls the function with that object as `this`, and answers the function's object result
 * when there was one, else the created object.
 */
Result JsFunctionConstruct(JsRuntime *runtime,
                           const JsValue *callable,
                           const JsValue *args,
                           int32_t argCount,
                           JsValue *out);

/** Current call depth of the runtime's active frame stack (used by the limit in 16.1). */
int32_t JsRuntimeCallDepth(JsRuntime *runtime);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_JS_FUNCTION_H */
