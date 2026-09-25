/*
 * RoboJS environments (see `RoboJSEnvironment.h`).
 *
 * Five doors, each of which is one rule of scoping:
 *
 *   `JsEnvDeclare`        writes into **this** link (a parameter, a `let`)
 *   `JsEnvDeclareConst`   the same, with the binding's writability cleared (a `const`)
 *   `JsEnvDeclareOrSet`   writes into this link, defining it if it is not there (a `let` re-executed
 *                         by a loop body, a function declaration re-executed by a block)
 *   `JsEnvSet`            searches this link and every parent, and fails when no link has the name
 *   `JsEnvDeclareVar`     writes into the nearest **function** link (a `var`, which is hoisted)
 *   `JsEnvGet`            searches upward and stops at the first hit, which is shadowing
 *
 * Every one of those writes goes through `JsEnvMirror`, which is the sixth rule and the one that is
 * easy to leave out: a binding written **in the global link** is also a property of the global object
 * (`JsEnv::globalObject`), so `foo` and `this.foo` are one value however the write arrived — a `var`,
 * an assignment, an implicit global, a built-in install. Doing it here rather than at each write site
 * is what makes it true of the sites that do not exist yet.
 *
 * Nothing here frees a link: an environment created by a call that has returned stays alive because
 * a closure points at it, and it is released when the runtime's heap is (`RoboJSHeap.h`).
 */

#include "RoboJS/RoboJSEnvironment.h"

#include "RoboJS/RoboJSObject.h"

#include <string.h>

const char *JsEnvKindName(JsEnvKind kind)
{
    switch (kind) {
    case RoboJsEnvGlobal: return "global";
    case RoboJsEnvFunction: return "function";
    case RoboJsEnvBlock: return "block";
    default: return "unknown";
    }
}

JsEnv *JsEnvCreate(JsHeap *heap, JsEnv *parent, JsEnvKind kind)
{
    JsEnv *env;

    if (heap == NULL) {
        return NULL;
    }
    env = (JsEnv *)JsHeapAlloc(heap, sizeof(JsEnv));
    if (env == NULL) {
        return NULL;
    }
    env->parent = parent;
    env->kind = (uint32_t)kind;
    env->heap = heap;
    env->globalObject = NULL;
    return env;
}

Result JsEnvBindGlobalObject(JsEnv *env, struct JsObject *globalObject)
{
    if (env == NULL) {
        return RoboInvalidArgument;
    }
    env->globalObject = globalObject;
    return RoboOk;
}

/*
 * The second half of a write: when @p link is the global link (the only one carrying a global
 * object), the value is also defined as a property of that object. A function or block write never
 * reaches the object, which is why a `var outer` inside a function cannot overwrite a global `outer`.
 *
 * `JsObjectDefine` rather than `JsObjectSet`: the property may not be there yet on the first write
 * (an implicit global), and define-or-update is the one door that covers both.
 */
static Result JsEnvMirror(const JsEnv *link, const char *name, const JsValue *value)
{
    if (link == NULL || link->globalObject == NULL || link->heap == NULL || name == NULL ||
        value == NULL) {
        return RoboOk;
    }
    return JsObjectDefine(link->heap, link->globalObject, name, value, JsPropAll);
}

/**
 * The global object's own view of a declaration, with the attribute the declaration justifies.
 *
 * A `const` is non-writable on the global object as well as in the environment, because both are
 * observable: a script that reaches the binding through `this.name` must be refused the same write
 * `name = …` is refused. Without this the mirror would be the hole in const-ness.
 */
static Result JsEnvMirrorAttributes(const JsEnv *link,
                                    const char *name,
                                    const JsValue *value,
                                    uint32_t attributes)
{
    if (link == NULL || link->globalObject == NULL || link->heap == NULL || name == NULL ||
        value == NULL) {
        return RoboOk;
    }
    return JsObjectDefineOwn(link->heap, link->globalObject, name, value, attributes);
}

static Result JsEnvDeclareFlags(JsHeap *heap,
                                JsEnv *env,
                                const char *name,
                                const JsValue *value,
                                uint32_t extraFlags);

static JsBinding *JsEnvFindHere(JsEnv *env, const char *name)
{
    int32_t i;

    if (env == NULL || name == NULL) {
        return NULL;
    }
    for (i = 0; i < env->bindingCount; ++i) {
        if (strcmp(env->bindings[i].name, name) == 0) {
            return &env->bindings[i];
        }
    }
    return NULL;
}

Result JsEnvDeclare(JsHeap *heap,
                    JsEnv *env,
                    const char *name,
                    const JsValue *value)
{
    return JsEnvDeclareFlags(heap, env, name, value, JsBindingMutable);
}

Result JsEnvDeclareConst(JsHeap *heap,
                         JsEnv *env,
                         const char *name,
                         const JsValue *value)
{
    /* No `JsBindingMutable`: `JsEnvSet` refuses the write, and the mirror is non-writable so the
     * global object's property refuses it too. */
    return JsEnvDeclareFlags(heap, env, name, value, 0u);
}

static Result JsEnvDeclareFlags(JsHeap *heap,
                                JsEnv *env,
                                const char *name,
                                const JsValue *value,
                                uint32_t extraFlags)
{
    JsBinding *fresh;
    int32_t capacity;
    const char *ownedName;
    int isConst = (extraFlags & JsBindingMutable) == 0u;

    if (heap == NULL || env == NULL || name == NULL || value == NULL || name[0] == '\0') {
        return RoboInvalidArgument;
    }
    if (JsEnvFindHere(env, name) != NULL) {
        return RoboAlreadyExists;
    }
    if (env->bindingCount >= JsHeapMaxProperties(heap)) {
        return RoboOutOfMemory;
    }
    if (env->bindingCount == env->bindingCapacity) {
        capacity = env->bindingCapacity > 0 ? env->bindingCapacity * 2 : 8;
        if (capacity > JsHeapMaxProperties(heap)) {
            capacity = JsHeapMaxProperties(heap);
        }
        if (capacity <= env->bindingCount) {
            return RoboOutOfMemory;
        }
        fresh = (JsBinding *)JsHeapRealloc(heap,
                                          env->bindings,
                                          (size_t)env->bindingCapacity * sizeof(JsBinding),
                                          (size_t)capacity * sizeof(JsBinding));
        if (fresh == NULL) {
            return RoboOutOfMemory;
        }
        env->bindings = fresh;
        env->bindingCapacity = capacity;
    }
    ownedName = JsHeapString(heap, name, -1);
    if (ownedName == NULL) {
        return RoboOutOfMemory;
    }
    env->bindings[env->bindingCount].name = ownedName;
    env->bindings[env->bindingCount].value = *value;
    env->bindings[env->bindingCount].flags = extraFlags |
                                             (env->globalObject != NULL ? JsBindingGlobal : 0u);
    env->bindingCount += 1;
    return JsEnvMirrorAttributes(env,
                                 name,
                                 value,
                                 isConst ? (JsPropAll & ~JsPropWritable) : JsPropAll);
}

Result JsEnvDeclareOrSet(JsHeap *heap,
                         JsEnv *env,
                         const char *name,
                         const JsValue *value)
{
    JsBinding *existing;

    if (env == NULL || name == NULL || value == NULL) {
        return RoboInvalidArgument;
    }
    existing = JsEnvFindHere(env, name);
    if (existing != NULL) {
        /* The mirror carries the binding's **own** attribute rather than `JsPropAll`: for a `const`,
         * this write is the initialization, so the value changes while the lock stays. Using the
         * default attributes here would widen the property back to writable and lose `const`-ness at
         * the global object, which is the one place a script can reach it without the environment. */
        uint32_t attributes = (existing->flags & JsBindingMutable) != 0u
                                  ? JsPropAll
                                  : (JsPropAll & ~JsPropWritable);

        existing->value = *value;
        return JsEnvMirrorAttributes(env, name, value, attributes);
    }
    return JsEnvDeclare(heap, env, name, value);
}

Result JsEnvSet(JsEnv *env, const char *name, const JsValue *value)
{
    JsEnv *cursor;

    if (env == NULL || name == NULL || value == NULL) {
        return RoboInvalidArgument;
    }
    cursor = env;
    while (cursor != NULL) {
        JsBinding *binding = JsEnvFindHere(cursor, name);

        if (binding != NULL) {
            if ((binding->flags & JsBindingMutable) == 0u) {
                return RoboPermissionDenied;
            }
            binding->value = *value;
            /* The link that owns the binding decides: only the global one mirrors, so a local
             * binding that shadows a global writes the local and leaves the global alone. */
            return JsEnvMirror(cursor, name, value);
        }
        cursor = cursor->parent;
    }
    return RoboNotFound;
}

Result JsEnvGet(const JsEnv *env, const char *name, JsValue *out)
{
    const JsEnv *cursor;

    if (out == NULL) {
        return RoboInvalidArgument;
    }
    *out = JsValueUndefined();
    if (env == NULL || name == NULL) {
        return RoboInvalidArgument;
    }
    cursor = env;
    while (cursor != NULL) {
        int32_t i;

        for (i = 0; i < cursor->bindingCount; ++i) {
            if (strcmp(cursor->bindings[i].name, name) == 0) {
                *out = cursor->bindings[i].value;
                return RoboOk;
            }
        }
        cursor = cursor->parent;
    }
    return RoboNotFound;
}

int JsEnvHas(const JsEnv *env, const char *name)
{
    const JsEnv *cursor = env;

    if (name == NULL) {
        return 0;
    }
    while (cursor != NULL) {
        int32_t i;

        for (i = 0; i < cursor->bindingCount; ++i) {
            if (strcmp(cursor->bindings[i].name, name) == 0) {
                return 1;
            }
        }
        cursor = cursor->parent;
    }
    return 0;
}

JsEnv *JsEnvFunctionScope(JsEnv *env)
{
    JsEnv *cursor = env;

    while (cursor != NULL) {
        if (cursor->kind == (uint32_t)RoboJsEnvFunction || cursor->kind == (uint32_t)RoboJsEnvGlobal) {
            return cursor;
        }
        cursor = cursor->parent;
    }
    return env;
}

Result JsEnvDeclareVar(JsHeap *heap,
                       JsEnv *env,
                       const char *name,
                       const JsValue *value)
{
    JsEnv *scope;

    if (env == NULL || name == NULL || value == NULL) {
        return RoboInvalidArgument;
    }
    scope = JsEnvFunctionScope(env);
    if (scope == NULL) {
        return RoboInvalidArgument;
    }
    /* A `var` already visible in an enclosing link is that binding — re-declaring it in the function
     * link would shadow it and break the closure that captured the outer one. */
    if (JsEnvHas(env, name)) {
        return JsEnvDeclareOrSet(heap, scope, name, value);
    }
    return JsEnvDeclare(heap, scope, name, value);
}

Result JsEnvRemove(JsEnv *env, const char *name)
{
    int32_t i;

    if (env == NULL || name == NULL) {
        return RoboInvalidArgument;
    }
    for (i = 0; i < env->bindingCount; ++i) {
        if (strcmp(env->bindings[i].name, name) != 0) {
            continue;
        }
        /* Shift the tail down rather than leaving a hole: a removal must not reorder the links a
         * later lookup walks past, and the binding table is small by construction. */
        for (; i + 1 < env->bindingCount; ++i) {
            env->bindings[i] = env->bindings[i + 1];
        }
        env->bindingCount -= 1;
        return RoboOk;
    }
    return RoboNotFound;
}

int32_t JsEnvBindingCount(const JsEnv *env)
{
    return env != NULL ? env->bindingCount : 0;
}
