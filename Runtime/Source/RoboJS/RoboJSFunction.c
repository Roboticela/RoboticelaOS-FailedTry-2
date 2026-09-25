/*
 * RoboJS functions (see `RoboJSFunction.h`).
 *
 * The file is small on purpose: everything interesting about calling a function is the interpreter's
 * (`JsInterpCallValue` — the one call path, declared in `RoboJSInternal.h`), and everything about
 * *being* one is here. `JsFunctionCall` and `JsFunctionConstruct` are thin: the first checks
 * callability and delegates, the second creates the receiver, sets its prototype from the function's
 * own `prototype` property and applies the standard "an object result wins" rule.
 */

#include "RoboJS/RoboJSFunction.h"

#include "RoboJS/RoboJSException.h"
#include "RoboJS/RoboJSObject.h"

#include "RoboJSInternal.h"

#include <string.h>

/** The `length` and `name` properties every function carries, defined once. */
static Result JsFunctionDecorate(JsHeap *heap, JsObject *object, int32_t arity)
{
    Result status;

    status = JsObjectSetNumber(heap, object, "length", (double)arity);
    if (status != RoboOk) {
        return status;
    }
    return RoboOk;
}

JsObject *JsFunctionCreateUser(JsHeap *heap,
                               const char *name,
                               const char **params,
                               int32_t paramCount,
                               JsNode *body,
                               JsEnv *closure,
                               int isArrow,
                               JsObject *prototype)
{
    JsObject *object;
    const char *ownedName;
    const char **ownedParams = NULL;
    int32_t i;

    if (heap == NULL) {
        return NULL;
    }
    object = JsObjectCreate(heap, prototype);
    if (object == NULL) {
        return NULL;
    }
    object->kind = (uint32_t)RoboJsObjectFunction;
    ownedName = JsHeapString(heap, name != NULL ? name : "", -1);
    if (ownedName == NULL) {
        return NULL;
    }
    if (paramCount > 0) {
        ownedParams = (const char **)JsHeapAllocArray(heap, (size_t)paramCount, sizeof(char *));
        if (ownedParams == NULL) {
            return NULL;
        }
        for (i = 0; i < paramCount; ++i) {
            const char *owned = JsHeapString(heap,
                                             params != NULL && params[i] != NULL ? params[i] : "",
                                             -1);

            if (owned == NULL) {
                return NULL;
            }
            ownedParams[i] = owned;
        }
    }
    object->fn.kind = (uint32_t)RoboJsFunctionUser;
    object->fn.arity = (uint32_t)paramCount;
    object->fn.name = ownedName;
    object->fn.params = ownedParams;
    object->fn.paramCount = paramCount;
    object->fn.body = body;
    object->fn.closure = closure;
    object->fn.flags = isArrow ? JsFunctionFlagArrow : 0u;

    if (JsFunctionDecorate(heap, object, paramCount) != RoboOk) {
        return NULL;
    }
    if (!isArrow) {
        /* Every non-arrow function gets a fresh `prototype` object, which is what makes `new` work.
         * An arrow function deliberately has none: it cannot be constructed, and a `prototype`
         * property that suggested otherwise would be a lie. */
        JsObject *instancePrototype = JsObjectCreate(heap, NULL);
        JsValue wrapped;

        if (instancePrototype == NULL) {
            return NULL;
        }
        wrapped = JsValueObject(instancePrototype);
        if (JsObjectSet(heap, object, "prototype", &wrapped) != RoboOk) {
            return NULL;
        }
    }
    return object;
}

JsObject *JsFunctionCreateNative(JsHeap *heap,
                                 const char *name,
                                 int32_t arity,
                                 JsNativeFn native,
                                 void *userdata,
                                 JsObject *prototype)
{
    JsObject *object;
    const char *ownedName;

    if (heap == NULL) {
        return NULL;
    }
    object = JsObjectCreate(heap, prototype);
    if (object == NULL) {
        return NULL;
    }
    object->kind = (uint32_t)RoboJsObjectFunction;
    ownedName = JsHeapString(heap, name != NULL ? name : "", -1);
    if (ownedName == NULL) {
        return NULL;
    }
    object->fn.kind = (uint32_t)RoboJsFunctionNative;
    object->fn.arity = (uint32_t)(arity > 0 ? arity : 0);
    object->fn.name = ownedName;
    object->fn.native = native;
    object->fn.userdata = userdata;
    if (JsFunctionDecorate(heap, object, arity > 0 ? arity : 0) != RoboOk) {
        return NULL;
    }
    return object;
}

JsFunction *JsFunctionOf(const JsValue *value)
{
    if (!JsValueIsFunction(value)) {
        return NULL;
    }
    return &value->object->fn;
}

JsFunction *JsFunctionOfObject(JsObject *object)
{
    if (object == NULL || object->kind != (uint32_t)RoboJsObjectFunction) {
        return NULL;
    }
    return &object->fn;
}

Result JsFunctionCall(JsRuntime *runtime,
                      const JsValue *callable,
                      const JsValue *thisValue,
                      const JsValue *args,
                      int32_t argCount,
                      JsValue *out)
{
    JsValue undefinedThis;
    JsObject *thisObject;

    if (runtime == NULL || callable == NULL || out == NULL) {
        return RoboInvalidArgument;
    }
    if (!JsValueIsFunction(callable)) {
        return RoboInvalidArgument;
    }
    if (argCount < 0 || (argCount > 0 && args == NULL)) {
        return RoboInvalidArgument;
    }
    *out = JsValueUndefined();
    undefinedThis = JsValueUndefined();
    if (thisValue == NULL) {
        thisValue = &undefinedThis;
    }
    thisObject = JsValueIsObject(thisValue) ? thisValue->object : NULL;
    return JsInterpCallValue(runtime, callable, thisObject, args, argCount, out);
}

Result JsFunctionConstruct(JsRuntime *runtime,
                           const JsValue *callable,
                           const JsValue *args,
                           int32_t argCount,
                           JsValue *out)
{
    JsObject *instancePrototype;
    JsValue prototypeValue;
    JsObject *receiver;
    JsValue wrappedReceiver;
    JsValue result;
    Result status;

    if (runtime == NULL || callable == NULL || out == NULL) {
        return RoboInvalidArgument;
    }
    if (!JsValueIsFunction(callable)) {
        JsRuntimeThrow(runtime, RoboJsErrorType, NULL, "not a constructor");
        return RoboError;
    }
    instancePrototype = JsRuntimeObjectPrototype(runtime);
    if (JsObjectGet(callable->object, "prototype", &prototypeValue) == RoboOk &&
        JsValueIsObject(&prototypeValue)) {
        instancePrototype = prototypeValue.object;
    }
    receiver = JsObjectCreate(JsRuntimeHeap(runtime), instancePrototype);
    if (receiver == NULL) {
        JsRuntimeThrow(runtime, RoboJsErrorInternal, NULL, "out of memory creating an object");
        return RoboOutOfMemory;
    }
    wrappedReceiver = JsValueObject(receiver);
    result = JsValueUndefined();
    status = JsFunctionCall(runtime, callable, &wrappedReceiver, args, argCount, &result);
    if (status != RoboOk) {
        return status;
    }
    if (JsValueIsObject(&result)) {
        *out = result;
        return RoboOk;
    }
    *out = wrappedReceiver;
    return RoboOk;
}

int32_t JsRuntimeCallDepth(JsRuntime *runtime)
{
    return runtime != NULL ? runtime->callDepth : 0;
}
