/*
 * RoboJS environments (Phase 16.7) — the lexical scope chain, and the reason closures work.
 *
 * An environment is a parent pointer and a table of names. Lookup walks the chain, declaration
 * writes into one chosen link, and nothing about either operation mentions the call stack — which is
 * the whole point: `JsEnv` is allocated on the heap and owned by the runtime, so an environment
 * created by a call that has *returned* is still alive and still holds its variables. A closure is
 * therefore not a special object: it is a function whose `closure` field points at an environment
 * that outlives its creating call.
 *
 * ## Three kinds of environment, and what each one is for
 *
 * `Global` is the runtime's outermost link: the globals object's bindings and the built-ins. A write
 * in that link is also a write of the global object's same-named property, so `foo` and `this.foo`
 * cannot disagree — see `JsEnv::globalObject`.
 * `Function` is created per call and receives the parameters plus every `var` declared anywhere in
 * the function body (hoisting). `Block` is created per `{}` and receives the `let`/`const` names
 * declared in it. `JsEnvFunctionScope` walks up from wherever the interpreter is to the nearest
 * function-or-global link, which is exactly the rule `var` uses and the reason a `var` inside an
 * `if` is visible after it.
 *
 * ## What this build does not do
 *
 * No temporal dead zone: a `let` name is readable as `undefined` from the top of its block rather
 * than throwing, and there is no separate `let`-vs-`var` redeclaration error. That is a documented
 * limitation (`Documentation/RoboJS/Limitations.md`) rather than an accident, and it is why
 * `RoboJsEnvKind` is stored per link: a later phase that wants the TDZ has the field it needs.
 */

#ifndef ROBO_JS_ENVIRONMENT_H
#define ROBO_JS_ENVIRONMENT_H

#include "RoboJS/RoboJSHeap.h"
#include "RoboJS/RoboJSTypes.h"
#include "RoboJS/RoboJSValue.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum JsEnvKind {
    RoboJsEnvGlobal = 0,
    RoboJsEnvFunction = 1,
    RoboJsEnvBlock = 2
} JsEnvKind;

const char *JsEnvKindName(JsEnvKind kind);

typedef struct JsBinding {
    const char *name; /* heap-owned */
    JsValue value;
    uint32_t flags;
} JsBinding;

/** A mutable binding (`var`, `let`, a parameter). */
#define JsBindingMutable 1u
/** A global object binding: the name mirrors a property of the global object. */
#define JsBindingGlobal 2u

typedef struct JsEnv {
    struct JsEnv *parent;
    uint32_t kind; /* JsEnvKind */
    uint32_t flags;
    JsBinding *bindings;
    int32_t bindingCount;
    int32_t bindingCapacity;
    /** The heap the bindings live in, kept so a write can mirror without being handed one. */
    JsHeap *heap;
    /**
     * The global object, set **only on the global link** (`JsEnvBindGlobalObject`) and NULL on every
     * other. A write in that link is also a write of the same-named property there, which is what
     * makes a bare `foo` and `this.foo` one value rather than two that can disagree — the rule
     * `RoboJSRuntime.h` states for `JsRuntimeDefineGlobal`, enforced here for **every** write
     * instead of only for the host's.
     */
    struct JsObject *globalObject;
} JsEnv;

/** Create a link whose parent is @p parent (NULL for the global link). */
JsEnv *JsEnvCreate(JsHeap *heap, JsEnv *parent, JsEnvKind kind);

/**
 * Attach the global object to the global link, which turns on the mirroring `globalObject`
 * documents. Called once by `JsRuntimeCreate`; a chain with no bound object keeps the plain
 * environment behaviour, which is what a bare `JsEnv` test wants.
 *
 * @return RoboOk; RoboInvalidArgument (NULL @p env).
 */
Result JsEnvBindGlobalObject(JsEnv *env, struct JsObject *globalObject);

/**
 * Declare @p name **in this link** with @p value.
 * @return RoboOk; RoboAlreadyExists when this link already has the name (the caller decides whether
 *         that is a redeclaration error or a definition to ignore); RoboOutOfMemory at the cap.
 */
Result JsEnvDeclare(JsHeap *heap,
                    JsEnv *env,
                    const char *name,
                    const JsValue *value);

/**
 * Declare @p name in this link as a **constant**: the binding exists, reads work, and `JsEnvSet`
 * refuses it with `RoboPermissionDenied`.
 *
 * `const` is the one declaration keyword whose difference from `let` is *observable after the
 * declaration*, so it has to be recorded on the binding rather than checked at the declaration site.
 * The refusal is the same code a read-only property gives, which is why the interpreter can turn both
 * into a `TypeError` through one arm.
 *
 * @return RoboOk; RoboAlreadyExists when this link already has the name; RoboOutOfMemory at the cap.
 */
Result JsEnvDeclareConst(JsHeap *heap,
                         JsEnv *env,
                         const char *name,
                         const JsValue *value);

/** Declare @p name in this link if absent, else overwrite — the "define or assign" door that a
 *  `let` re-executed by a loop body, and a function declaration re-executed by a block, both need. */
Result JsEnvDeclareOrSet(JsHeap *heap,
                         JsEnv *env,
                         const char *name,
                         const JsValue *value);

/**
 * Assign to an existing @p name, searching this link and every parent.
 * @return RoboOk; RoboNotFound when nothing declares it (the interpreter turns that into a
 *         `ReferenceError`, or into an implicit global when the runtime allows it).
 */
Result JsEnvSet(JsEnv *env, const char *name, const JsValue *value);

/** Read @p name, searching upward. RoboNotFound when no link declares it. */
Result JsEnvGet(const JsEnv *env, const char *name, JsValue *out);

/** 1 when @p name resolves in this chain. */
int JsEnvHas(const JsEnv *env, const char *name);

/** `var`'s home: the nearest function link, or the global link. Never NULL for a rooted chain. */
JsEnv *JsEnvFunctionScope(JsEnv *env);

/**
 * Declare @p name in `JsEnvFunctionScope(env)` if no link in between already has it. This is
 * `var` hoisting and function-declaration hoisting expressed once, so the interpreter's two
 * declaration arms cannot disagree about where a name lands.
 */
Result JsEnvDeclareVar(JsHeap *heap,
                       JsEnv *env,
                       const char *name,
                       const JsValue *value);

/**
 * Remove @p name from **this link** only — the inverse of `JsEnvDeclare`, and the reason a link needs
 * one: a name that is no longer a binding of the global link must stop resolving there, which deleting
 * the global object's property alone does not do.
 *
 * @return RoboOk; RoboNotFound when this link does not declare @p name (a name that lives in an
 *         enclosing link is that link's to remove); RoboInvalidArgument for a NULL argument.
 */
Result JsEnvRemove(JsEnv *env, const char *name);

/** Number of bindings held by one link (not the chain). */
int32_t JsEnvBindingCount(const JsEnv *env);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_JS_ENVIRONMENT_H */
