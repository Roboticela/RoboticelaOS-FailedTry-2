/*
 * RoboJS's built-ins (16.5, 16.6 and the global object of 16.4).
 *
 * What is here is a *controlled* standard library and nothing else: `console`, `Object.keys`,
 * `Array.prototype`'s five methods, `Function.prototype.call`/`apply`, `String`'s four, `Number`,
 * `Math`'s subset, `parseInt`/`parseFloat`, and the five error constructors. Every one of them is a
 * native function this file registers, so a script reaches a host capability only through a door
 * somebody wrote here and can read.
 *
 * ## What is deliberately absent
 *
 * No `JSON` (a parser and a serializer are their own phase), no `Promise`, no `Proxy`/`Reflect`, no
 * `Symbol`, no `BigInt`, no typed arrays, no `Date`, no regular expressions, no `random` — a
 * deterministic runtime has no business inventing a random number, and a test that wanted one would
 * be testing the host rather than the engine. No `eval` and no `Function` constructor: both are
 * "parse this string at runtime", which this build can do but has no security story for.
 * `Documentation/RoboJS/LanguageSubset.md` is the list.
 */

#include "RoboBase/RoboLog.h"
#include "RoboJS/RoboJSException.h"
#include "RoboJS/RoboJSFunction.h"
#include "RoboJS/RoboJSObject.h"
#include "RoboJS/RoboJSRuntime.h"

#include "RoboJSInternal.h"

#include "RoboJSMath.h"
#include <stdio.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/* console                                                             */
/* ------------------------------------------------------------------ */

static void JsConsoleEmit(JsRuntime *runtime, int level, const JsValue *args, int32_t argCount)
{
    char line[512];
    int32_t total = 0;
    int32_t i;

    line[0] = '\0';
    for (i = 0; i < argCount; ++i) {
        char item[256];
        int32_t itemLength = JsValueToString(&args[i], item, sizeof(item));

        if (i > 0 && (size_t)total + 1u < sizeof(line)) {
            line[total] = ' ';
            total += 1;
            line[total] = '\0';
        }
        if ((size_t)total + (size_t)itemLength < sizeof(line)) {
            memcpy(line + total, item, (size_t)itemLength);
            total += itemLength;
            line[total] = '\0';
        }
        (void)itemLength;
    }
    if (runtime->console != NULL) {
        runtime->console(runtime->consoleUserdata, level, line, total);
        return;
    }
    /* No host sink: the runtime's own diagnostics carry it, which is why `console.log` inside a
     * suite shows up in the dump a test prints on failure. */
    JsRuntimeAddDiagnostic(runtime,
                           level == 0 ? RoboJsDiagInfo : (level == 1 ? RoboJsDiagWarn : RoboJsDiagError),
                           RoboJsErrorNone,
                           "console",
                           NULL,
                           "%s",
                           line);
    LogWrite(level == 0 ? RoboLogInfo : (level == 1 ? RoboLogWarn : RoboLogError),
             "RoboJS",
             0,
             "%s",
             line);
}

Result JsConsoleLog(JsRuntime *runtime,
                    JsObject *thisObject,
                    const JsValue *args,
                    int32_t argCount,
                    JsValue *out,
                    void *userdata)
{
    (void)thisObject;
    (void)userdata;
    if (runtime == NULL || out == NULL) {
        return RoboInvalidArgument;
    }
    JsConsoleEmit(runtime, 0, args, argCount);
    *out = JsValueUndefined();
    return RoboOk;
}

Result JsConsoleWarn(JsRuntime *runtime,
                     JsObject *thisObject,
                     const JsValue *args,
                     int32_t argCount,
                     JsValue *out,
                     void *userdata)
{
    (void)thisObject;
    (void)userdata;
    if (runtime == NULL || out == NULL) {
        return RoboInvalidArgument;
    }
    JsConsoleEmit(runtime, 1, args, argCount);
    *out = JsValueUndefined();
    return RoboOk;
}

Result JsConsoleError(JsRuntime *runtime,
                      JsObject *thisObject,
                      const JsValue *args,
                      int32_t argCount,
                      JsValue *out,
                      void *userdata)
{
    (void)thisObject;
    (void)userdata;
    if (runtime == NULL || out == NULL) {
        return RoboInvalidArgument;
    }
    JsConsoleEmit(runtime, 2, args, argCount);
    *out = JsValueUndefined();
    return RoboOk;
}

/* ------------------------------------------------------------------ */
/* Object                                                              */
/* ------------------------------------------------------------------ */

/** `Object.keys(o)` — an array of the object's enumerable own keys, in insertion order. */
static Result JsObjectKeysNative(JsRuntime *runtime,
                                 JsObject *thisObject,
                                 const JsValue *args,
                                 int32_t argCount,
                                 JsValue *out,
                                 void *userdata)
{
    const char *keys[128];
    int32_t count;
    JsObject *result;

    (void)thisObject;
    (void)userdata;
    if (runtime == NULL || out == NULL) {
        return RoboInvalidArgument;
    }
    if (argCount < 1 || !JsValueIsObject(&args[0])) {
        JsRuntimeThrow(runtime, RoboJsErrorType, NULL, "Object.keys needs an object");
        return RoboError;
    }
    result = JsArrayCreate(runtime->heap, runtime->arrayPrototype, 0);
    if (result == NULL) {
        JsRuntimeThrow(runtime, RoboJsErrorInternal, NULL, "out of memory");
        return RoboOutOfMemory;
    }
    count = JsObjectOwnKeys(args[0].object, keys, 128);
    {
        int32_t i;

        for (i = 0; i < count; ++i) {
            JsValue item = JsValueString(runtime->heap, keys[i], -1);

            if (JsArrayPush(runtime->heap, result, &item) < 0) {
                JsRuntimeThrow(runtime, RoboJsErrorInternal, NULL, "out of memory");
                return RoboOutOfMemory;
            }
        }
    }
    *out = JsValueObject(result);
    return RoboOk;
}

/** `Object(...)` — a new plain object, which is also what the constructor answers. */
static Result JsObjectConstructorNative(JsRuntime *runtime,
                                        JsObject *thisObject,
                                        const JsValue *args,
                                        int32_t argCount,
                                        JsValue *out,
                                        void *userdata)
{
    JsObject *created;

    (void)thisObject;
    (void)args;
    (void)argCount;
    (void)userdata;
    if (runtime == NULL || out == NULL) {
        return RoboInvalidArgument;
    }
    created = JsObjectCreate(runtime->heap, runtime->objectPrototype);
    if (created == NULL) {
        JsRuntimeThrow(runtime, RoboJsErrorInternal, NULL, "out of memory");
        return RoboOutOfMemory;
    }
    *out = JsValueObject(created);
    return RoboOk;
}

/** `Object.create(proto)` — a fresh object whose chain starts at @p proto. */
static Result JsObjectCreateNative(JsRuntime *runtime,
                                   JsObject *thisObject,
                                   const JsValue *args,
                                   int32_t argCount,
                                   JsValue *out,
                                   void *userdata)
{
    JsObject *created;

    (void)thisObject;
    (void)userdata;
    if (runtime == NULL || out == NULL) {
        return RoboInvalidArgument;
    }
    if (argCount < 1 || (!JsValueIsObject(&args[0]) && !JsValueIsNull(&args[0]))) {
        JsRuntimeThrow(runtime, RoboJsErrorType, NULL, "Object.create needs an object or null");
        return RoboError;
    }
    created = JsObjectCreate(runtime->heap, JsValueIsObject(&args[0]) ? args[0].object : NULL);
    if (created == NULL) {
        JsRuntimeThrow(runtime, RoboJsErrorInternal, NULL, "out of memory");
        return RoboOutOfMemory;
    }
    *out = JsValueObject(created);
    return RoboOk;
}

Result JsRuntimeInstallObjectBuiltins(JsRuntime *runtime)
{
    JsObject *objectConstructor;
    JsObject *objectPrototype;
    JsValue wrapped;

    if (runtime == NULL) {
        return RoboInvalidArgument;
    }
    objectPrototype = runtime->objectPrototype;
    objectConstructor = JsFunctionCreateNative(runtime->heap,
                                               "Object",
                                               1,
                                               JsObjectConstructorNative,
                                               NULL,
                                               runtime->functionPrototype);
    if (objectConstructor == NULL) {
        return RoboOutOfMemory;
    }
    /* `Object(...)` and `new Object()` both answer a plain object whose chain is
     * `Object.prototype`; there is no primitive-wrapper behaviour in this build. */
    wrapped = JsValueObject(objectPrototype);
    if (JsObjectSet(runtime->heap, objectConstructor, "prototype", &wrapped) != RoboOk) {
        return RoboOutOfMemory;
    }
    if (JsObjectDefineNative(runtime, objectConstructor, "keys", 1, JsObjectKeysNative, NULL) !=
        RoboOk) {
        return RoboOutOfMemory;
    }
    if (JsObjectDefineNative(runtime, objectConstructor, "create", 1, JsObjectCreateNative, NULL) !=
        RoboOk) {
        return RoboOutOfMemory;
    }
    wrapped = JsValueObject(objectConstructor);
    return JsRuntimeDefineGlobal(runtime, "Object", &wrapped);
}

/* ------------------------------------------------------------------ */
/* Array                                                               */
/* ------------------------------------------------------------------ */

static Result JsArrayPushNative(JsRuntime *runtime,
                                JsObject *thisObject,
                                const JsValue *args,
                                int32_t argCount,
                                JsValue *out,
                                void *userdata)
{
    int32_t i;
    int32_t length = 0;

    (void)userdata;
    if (runtime == NULL || out == NULL) {
        return RoboInvalidArgument;
    }
    if (thisObject == NULL || thisObject->kind != (uint32_t)RoboJsObjectArray) {
        JsRuntimeThrow(runtime, RoboJsErrorType, NULL, "push needs an array");
        return RoboError;
    }
    for (i = 0; i < argCount; ++i) {
        length = JsArrayPush(runtime->heap, thisObject, &args[i]);
        if (length < 0) {
            JsRuntimeThrow(runtime, RoboJsErrorRange, NULL, "the array is at its size limit");
            return RoboError;
        }
    }
    *out = JsValueNumber((double)length);
    return RoboOk;
}

static Result JsArrayPopNative(JsRuntime *runtime,
                               JsObject *thisObject,
                               const JsValue *args,
                               int32_t argCount,
                               JsValue *out,
                               void *userdata)
{
    (void)args;
    (void)argCount;
    (void)userdata;
    if (runtime == NULL || out == NULL) {
        return RoboInvalidArgument;
    }
    if (thisObject == NULL || thisObject->kind != (uint32_t)RoboJsObjectArray) {
        JsRuntimeThrow(runtime, RoboJsErrorType, NULL, "pop needs an array");
        return RoboError;
    }
    return JsArrayPop(thisObject, out);
}

static Result JsArrayJoinNative(JsRuntime *runtime,
                                JsObject *thisObject,
                                const JsValue *args,
                                int32_t argCount,
                                JsValue *out,
                                void *userdata)
{
    char separator[8];
    char buffer[1024];
    int32_t total = 0;
    int32_t i;
    int32_t limit;

    (void)userdata;
    if (runtime == NULL || out == NULL) {
        return RoboInvalidArgument;
    }
    if (thisObject == NULL || thisObject->kind != (uint32_t)RoboJsObjectArray) {
        JsRuntimeThrow(runtime, RoboJsErrorType, NULL, "join needs an array");
        return RoboError;
    }
    if (argCount >= 1 && JsValueIsString(&args[0])) {
        int32_t length = args[0].stringLength;

        if (length > 7) {
            length = 7;
        }
        memcpy(separator, args[0].string, (size_t)length);
        separator[length] = '\0';
    } else {
        separator[0] = ',';
        separator[1] = '\0';
    }
    buffer[0] = '\0';
    limit = thisObject->length;
    if (limit > 64) {
        limit = 64;
    }
    for (i = 0; i < limit; ++i) {
        char item[64];
        int32_t itemLength = JsValueToString(&thisObject->elements[i], item, sizeof(item));

        if (i > 0) {
            size_t separatorLength = strlen(separator);

            if ((size_t)total + separatorLength < sizeof(buffer)) {
                memcpy(buffer + total, separator, separatorLength);
                total += (int32_t)separatorLength;
                buffer[total] = '\0';
            }
        }
        if ((size_t)total + (size_t)itemLength < sizeof(buffer) &&
            thisObject->elements[i].type != RoboJsTypeUndefined &&
            thisObject->elements[i].type != RoboJsTypeNull) {
            memcpy(buffer + total, item, (size_t)itemLength);
            total += itemLength;
            buffer[total] = '\0';
        }
    }
    if (thisObject->length > limit) {
        /* A join whose answer does not fit the runtime's line budget is refused rather than
         * truncated silently. */
        JsRuntimeThrow(runtime,
                       RoboJsErrorRange,
                       NULL,
                       "join is limited to %d elements in this build",
                       limit);
        return RoboError;
    }
    *out = JsValueString(runtime->heap, buffer, total);
    return RoboOk;
}

static Result JsArrayIndexOfNative(JsRuntime *runtime,
                                   JsObject *thisObject,
                                   const JsValue *args,
                                   int32_t argCount,
                                   JsValue *out,
                                   void *userdata)
{
    int32_t i;

    (void)userdata;
    if (runtime == NULL || out == NULL) {
        return RoboInvalidArgument;
    }
    if (thisObject == NULL || thisObject->kind != (uint32_t)RoboJsObjectArray) {
        JsRuntimeThrow(runtime, RoboJsErrorType, NULL, "indexOf needs an array");
        return RoboError;
    }
    for (i = 0; i < thisObject->length; ++i) {
        if (argCount >= 1 && JsValueStrictEquals(&thisObject->elements[i], &args[0])) {
            *out = JsValueNumber((double)i);
            return RoboOk;
        }
    }
    *out = JsValueNumber(-1.0);
    return RoboOk;
}

static Result JsArraySliceNative(JsRuntime *runtime,
                                 JsObject *thisObject,
                                 const JsValue *args,
                                 int32_t argCount,
                                 JsValue *out,
                                 void *userdata)
{
    int32_t start = 0;
    int32_t end;
    int32_t i;
    JsObject *result;

    (void)userdata;
    if (runtime == NULL || out == NULL) {
        return RoboInvalidArgument;
    }
    if (thisObject == NULL || thisObject->kind != (uint32_t)RoboJsObjectArray) {
        JsRuntimeThrow(runtime, RoboJsErrorType, NULL, "slice needs an array");
        return RoboError;
    }
    end = thisObject->length;
    if (argCount >= 1 && JsValueIsNumber(&args[0])) {
        start = JsValueToInt32(&args[0]);
        if (start < 0) {
            start = 0;
        }
        if (start > end) {
            start = end;
        }
    }
    if (argCount >= 2 && JsValueIsNumber(&args[1])) {
        end = JsValueToInt32(&args[1]);
        if (end < start) {
            end = start;
        }
        if (end > thisObject->length) {
            end = thisObject->length;
        }
    }
    result = JsArrayCreate(runtime->heap, runtime->arrayPrototype, 0);
    if (result == NULL) {
        JsRuntimeThrow(runtime, RoboJsErrorInternal, NULL, "out of memory");
        return RoboOutOfMemory;
    }
    for (i = start; i < end; ++i) {
        if (JsArrayPush(runtime->heap, result, &thisObject->elements[i]) < 0) {
            JsRuntimeThrow(runtime, RoboJsErrorRange, NULL, "the array is at its size limit");
            return RoboError;
        }
    }
    *out = JsValueObject(result);
    return RoboOk;
}

/* ------------------------------------------------------------------ */
/* Function.prototype                                                  */
/* ------------------------------------------------------------------ */

static Result JsFunctionCallNative(JsRuntime *runtime,
                                   JsObject *thisObject,
                                   const JsValue *args,
                                   int32_t argCount,
                                   JsValue *out,
                                   void *userdata)
{
    JsValue callable;

    (void)userdata;
    if (runtime == NULL || out == NULL) {
        return RoboInvalidArgument;
    }
    if (thisObject == NULL || thisObject->kind != (uint32_t)RoboJsObjectFunction) {
        JsRuntimeThrow(runtime, RoboJsErrorType, NULL, "call needs a function");
        return RoboError;
    }
    callable = JsValueObject(thisObject);
    if (argCount <= 0) {
        return JsFunctionCall(runtime, &callable, NULL, NULL, 0, out);
    }
    return JsFunctionCall(runtime, &callable, &args[0], args + 1, argCount - 1, out);
}

static Result JsFunctionApplyNative(JsRuntime *runtime,
                                    JsObject *thisObject,
                                    const JsValue *args,
                                    int32_t argCount,
                                    JsValue *out,
                                    void *userdata)
{
    JsValue callable;
    const JsValue *thisValue = NULL;
    const JsValue *arguments = NULL;
    int32_t argumentCount = 0;

    (void)userdata;
    if (runtime == NULL || out == NULL) {
        return RoboInvalidArgument;
    }
    if (thisObject == NULL || thisObject->kind != (uint32_t)RoboJsObjectFunction) {
        JsRuntimeThrow(runtime, RoboJsErrorType, NULL, "apply needs a function");
        return RoboError;
    }
    callable = JsValueObject(thisObject);
    if (argCount >= 1) {
        thisValue = &args[0];
    }
    if (argCount >= 2 && JsValueIsArray(&args[1])) {
        arguments = args[1].object->elements;
        argumentCount = args[1].object->length;
    }
    return JsFunctionCall(runtime, &callable, thisValue, arguments, argumentCount, out);
}

/* ------------------------------------------------------------------ */
/* String                                                              */
/* ------------------------------------------------------------------ */

static Result JsStringUpperNative(JsRuntime *runtime,
                                  JsObject *thisObject,
                                  const JsValue *args,
                                  int32_t argCount,
                                  JsValue *out,
                                  void *userdata)
{
    char buffer[512];
    const char *source;
    int32_t length;
    int32_t i;

    (void)thisObject;
    (void)userdata;
    if (runtime == NULL || out == NULL) {
        return RoboInvalidArgument;
    }
    if (argCount < 1 || !JsValueIsString(&args[0])) {
        JsRuntimeThrow(runtime, RoboJsErrorType, NULL, "this build's toUpperCase reads a string");
        return RoboError;
    }
    source = args[0].string;
    length = args[0].stringLength;
    if (length >= (int32_t)sizeof(buffer)) {
        length = (int32_t)sizeof(buffer) - 1;
    }
    for (i = 0; i < length; ++i) {
        char c = source[i];

        buffer[i] = (c >= 'a' && c <= 'z') ? (char)(c - 32) : c;
    }
    buffer[length] = '\0';
    *out = JsValueString(runtime->heap, buffer, length);
    return RoboOk;
}

static Result JsStringLowerNative(JsRuntime *runtime,
                                  JsObject *thisObject,
                                  const JsValue *args,
                                  int32_t argCount,
                                  JsValue *out,
                                  void *userdata)
{
    char buffer[512];
    const char *source;
    int32_t length;
    int32_t i;

    (void)thisObject;
    (void)userdata;
    if (runtime == NULL || out == NULL) {
        return RoboInvalidArgument;
    }
    if (argCount < 1 || !JsValueIsString(&args[0])) {
        JsRuntimeThrow(runtime, RoboJsErrorType, NULL, "this build's toLowerCase reads a string");
        return RoboError;
    }
    source = args[0].string;
    length = args[0].stringLength;
    if (length >= (int32_t)sizeof(buffer)) {
        length = (int32_t)sizeof(buffer) - 1;
    }
    for (i = 0; i < length; ++i) {
        char c = source[i];

        buffer[i] = (c >= 'A' && c <= 'Z') ? (char)(c + 32) : c;
    }
    buffer[length] = '\0';
    *out = JsValueString(runtime->heap, buffer, length);
    return RoboOk;
}

/* ------------------------------------------------------------------ */
/* Numeric helpers, Math and the conversions                           */
/* ------------------------------------------------------------------ */

static Result JsMathUnaryNative(JsRuntime *runtime,
                                JsObject *thisObject,
                                const JsValue *args,
                                int32_t argCount,
                                JsValue *out,
                                void *userdata)
{
    double value = argCount >= 1 ? JsValueToNumber(&args[0]) : (0.0 / 0.0);
    int which = (int)(intptr_t)userdata;

    (void)thisObject;
    if (runtime == NULL || out == NULL) {
        return RoboInvalidArgument;
    }
    switch (which) {
    case 0: *out = JsValueNumber(JsMathFloor(value)); break;
    case 1: *out = JsValueNumber(JsMathCeil(value)); break;
    case 2: *out = JsValueNumber(JsMathFloor(value + 0.5)); break;
    case 3: *out = JsValueNumber(JsMathAbs(value)); break;
    case 4: *out = JsValueNumber(JsMathSqrt(value)); break;
    default: *out = JsValueNumber(value); break;
    }
    return RoboOk;
}

static Result JsMathBinaryNative(JsRuntime *runtime,
                                 JsObject *thisObject,
                                 const JsValue *args,
                                 int32_t argCount,
                                 JsValue *out,
                                 void *userdata)
{
    double a = argCount >= 1 ? JsValueToNumber(&args[0]) : (0.0 / 0.0);
    double b = argCount >= 2 ? JsValueToNumber(&args[1]) : (0.0 / 0.0);
    int which = (int)(intptr_t)userdata;

    (void)thisObject;
    if (runtime == NULL || out == NULL) {
        return RoboInvalidArgument;
    }
    switch (which) {
    case 0: *out = JsValueNumber(JsMathPow(a, b)); break;
    case 1: {
        int32_t i;
        double result = argCount >= 1 ? JsValueToNumber(&args[0]) : (0.0 / 0.0);

        for (i = 1; i < argCount; ++i) {
            double candidate = JsValueToNumber(&args[i]);

            if (candidate < result || result != result) {
                result = candidate;
            }
        }
        *out = JsValueNumber(result);
        break;
    }
    case 2: {
        int32_t i;
        double result = argCount >= 1 ? JsValueToNumber(&args[0]) : (0.0 / 0.0);

        for (i = 1; i < argCount; ++i) {
            double candidate = JsValueToNumber(&args[i]);

            if (candidate > result || result != result) {
                result = candidate;
            }
        }
        *out = JsValueNumber(result);
        break;
    }
    default: *out = JsValueNumber(a + b); break;
    }
    return RoboOk;
}

static Result JsStringConstructorNative(JsRuntime *runtime,
                                        JsObject *thisObject,
                                        const JsValue *args,
                                        int32_t argCount,
                                        JsValue *out,
                                        void *userdata)
{
    char buffer[256];

    (void)thisObject;
    (void)userdata;
    if (runtime == NULL || out == NULL) {
        return RoboInvalidArgument;
    }
    if (argCount < 1) {
        *out = JsValueString(runtime->heap, "", 0);
        return RoboOk;
    }
    JsValueToString(&args[0], buffer, sizeof(buffer));
    *out = JsValueString(runtime->heap, buffer, -1);
    return RoboOk;
}

static Result JsNumberConstructorNative(JsRuntime *runtime,
                                        JsObject *thisObject,
                                        const JsValue *args,
                                        int32_t argCount,
                                        JsValue *out,
                                        void *userdata)
{
    (void)thisObject;
    (void)userdata;
    if (runtime == NULL || out == NULL) {
        return RoboInvalidArgument;
    }
    if (argCount < 1) {
        *out = JsValueNumber(0.0);
        return RoboOk;
    }
    *out = JsValueNumber(JsValueToNumber(&args[0]));
    return RoboOk;
}

static Result JsBooleanConstructorNative(JsRuntime *runtime,
                                         JsObject *thisObject,
                                         const JsValue *args,
                                         int32_t argCount,
                                         JsValue *out,
                                         void *userdata)
{
    (void)thisObject;
    (void)userdata;
    if (runtime == NULL || out == NULL) {
        return RoboInvalidArgument;
    }
    *out = JsValueBool(argCount >= 1 && JsValueToBoolean(&args[0]));
    return RoboOk;
}

static Result JsFromCharCodeNative(JsRuntime *runtime,
                                   JsObject *thisObject,
                                   const JsValue *args,
                                   int32_t argCount,
                                   JsValue *out,
                                   void *userdata)
{
    char buffer[16];
    int32_t written = 0;
    int32_t i;

    (void)thisObject;
    (void)userdata;
    if (runtime == NULL || out == NULL) {
        return RoboInvalidArgument;
    }
    for (i = 0; i < argCount && written < 12; ++i) {
        int32_t code = JsValueToInt32(&args[i]);

        if (code < 0 || code > 0x7F) {
            JsRuntimeThrow(runtime,
                           RoboJsErrorRange,
                           NULL,
                           "this build's fromCharCode is ASCII only (code %d)",
                           code);
            return RoboError;
        }
        buffer[written++] = (char)code;
    }
    buffer[written] = '\0';
    *out = JsValueString(runtime->heap, buffer, written);
    return RoboOk;
}

static Result JsParseIntNative(JsRuntime *runtime,
                               JsObject *thisObject,
                               const JsValue *args,
                               int32_t argCount,
                               JsValue *out,
                               void *userdata)
{
    const char *text;
    int32_t i = 0;
    int negative = 0;
    double value = 0.0;
    int seen = 0;

    (void)thisObject;
    (void)userdata;
    if (runtime == NULL || out == NULL) {
        return RoboInvalidArgument;
    }
    if (argCount < 1) {
        *out = JsValueNumber(0.0 / 0.0);
        return RoboOk;
    }
    {
        char buffer[128];
        int32_t length = JsValueToString(&args[0], buffer, sizeof(buffer));

        if (length >= (int32_t)sizeof(buffer)) {
            *out = JsValueNumber(0.0 / 0.0);
            return RoboOk;
        }
        text = buffer;
    }
    while (text[i] == ' ' || text[i] == '\t' || text[i] == '\n') {
        i += 1;
    }
    if (text[i] == '+' || text[i] == '-') {
        negative = text[i] == '-';
        i += 1;
    }
    while (text[i] >= '0' && text[i] <= '9') {
        value = value * 10.0 + (double)(text[i] - '0');
        seen = 1;
        i += 1;
    }
    if (!seen) {
        *out = JsValueNumber(0.0 / 0.0);
        return RoboOk;
    }
    *out = JsValueNumber(negative ? -value : value);
    return RoboOk;
}

static Result JsErrorConstructorNative(JsRuntime *runtime,
                                       JsObject *thisObject,
                                       const JsValue *args,
                                       int32_t argCount,
                                       JsValue *out,
                                       void *userdata)
{
    char message[200];
    JsErrorKind kind = (JsErrorKind)(intptr_t)userdata;
    JsObject *error;

    (void)thisObject;
    if (runtime == NULL || out == NULL) {
        return RoboInvalidArgument;
    }
    message[0] = '\0';
    if (argCount >= 1) {
        JsValueToString(&args[0], message, sizeof(message));
    }
    error = JsErrorCreate(runtime, kind, message);
    if (error == NULL) {
        return RoboOutOfMemory;
    }
    *out = JsValueObject(error);
    return RoboOk;
}

/* ------------------------------------------------------------------ */
/* Installation                                                        */
/* ------------------------------------------------------------------ */

Result JsRuntimeInstallValueBuiltins(JsRuntime *runtime)
{
    JsObject *arrayPrototype;
    JsObject *functionPrototype;
    JsObject *stringPrototype;
    JsObject *mathObject;
    JsObject *stringConstructor;
    JsObject *numberConstructor;
    JsObject *booleanConstructor;
    JsValue wrapped;

    if (runtime == NULL) {
        return RoboInvalidArgument;
    }
    arrayPrototype = runtime->arrayPrototype;
    functionPrototype = runtime->functionPrototype;
    stringPrototype = runtime->stringPrototype;

    if (JsObjectDefineNative(runtime, arrayPrototype, "push", 1, JsArrayPushNative, NULL) != RoboOk ||
        JsObjectDefineNative(runtime, arrayPrototype, "pop", 0, JsArrayPopNative, NULL) != RoboOk ||
        JsObjectDefineNative(runtime, arrayPrototype, "join", 1, JsArrayJoinNative, NULL) != RoboOk ||
        JsObjectDefineNative(runtime, arrayPrototype, "indexOf", 1, JsArrayIndexOfNative, NULL) !=
            RoboOk ||
        JsObjectDefineNative(runtime, arrayPrototype, "slice", 2, JsArraySliceNative, NULL) !=
            RoboOk) {
        return RoboOutOfMemory;
    }
    if (JsObjectDefineNative(runtime, functionPrototype, "call", 1, JsFunctionCallNative, NULL) !=
            RoboOk ||
        JsObjectDefineNative(runtime, functionPrototype, "apply", 2, JsFunctionApplyNative, NULL) !=
            RoboOk) {
        return RoboOutOfMemory;
    }
    if (JsObjectDefineNative(runtime,
                             stringPrototype,
                             "toUpperCase",
                             0,
                             JsStringUpperNative,
                             NULL) != RoboOk ||
        JsObjectDefineNative(runtime,
                             stringPrototype,
                             "toLowerCase",
                             0,
                             JsStringLowerNative,
                             NULL) != RoboOk) {
        return RoboOutOfMemory;
    }

    stringConstructor = JsFunctionCreateNative(runtime->heap,
                                               "String",
                                               1,
                                               JsStringConstructorNative,
                                               NULL,
                                               functionPrototype);
    if (stringConstructor == NULL) {
        return RoboOutOfMemory;
    }
    wrapped = JsValueObject(stringPrototype);
    if (JsObjectSet(runtime->heap, stringConstructor, "prototype", &wrapped) != RoboOk ||
        JsObjectDefineNative(runtime, stringConstructor, "fromCharCode", 1, JsFromCharCodeNative,
                             NULL) != RoboOk) {
        return RoboOutOfMemory;
    }
    wrapped = JsValueObject(stringConstructor);
    if (JsRuntimeDefineGlobal(runtime, "String", &wrapped) != RoboOk) {
        return RoboOutOfMemory;
    }

    numberConstructor = JsFunctionCreateNative(runtime->heap,
                                               "Number",
                                               1,
                                               JsNumberConstructorNative,
                                               NULL,
                                               functionPrototype);
    if (numberConstructor == NULL) {
        return RoboOutOfMemory;
    }
    wrapped = JsValueObject(numberConstructor);
    if (JsRuntimeDefineGlobal(runtime, "Number", &wrapped) != RoboOk) {
        return RoboOutOfMemory;
    }

    booleanConstructor = JsFunctionCreateNative(runtime->heap,
                                                "Boolean",
                                                1,
                                                JsBooleanConstructorNative,
                                                NULL,
                                                functionPrototype);
    if (booleanConstructor == NULL) {
        return RoboOutOfMemory;
    }
    wrapped = JsValueObject(booleanConstructor);
    if (JsRuntimeDefineGlobal(runtime, "Boolean", &wrapped) != RoboOk) {
        return RoboOutOfMemory;
    }

    mathObject = JsObjectCreate(runtime->heap, runtime->objectPrototype);
    if (mathObject == NULL) {
        return RoboOutOfMemory;
    }
    if (JsObjectDefineNative(runtime, mathObject, "floor", 1, JsMathUnaryNative,
                             (void *)(intptr_t)0) != RoboOk ||
        JsObjectDefineNative(runtime, mathObject, "ceil", 1, JsMathUnaryNative,
                             (void *)(intptr_t)1) != RoboOk ||
        JsObjectDefineNative(runtime, mathObject, "round", 1, JsMathUnaryNative,
                             (void *)(intptr_t)2) != RoboOk ||
        JsObjectDefineNative(runtime, mathObject, "abs", 1, JsMathUnaryNative,
                             (void *)(intptr_t)3) != RoboOk ||
        JsObjectDefineNative(runtime, mathObject, "sqrt", 1, JsMathUnaryNative,
                             (void *)(intptr_t)4) != RoboOk ||
        JsObjectDefineNative(runtime, mathObject, "pow", 2, JsMathBinaryNative,
                             (void *)(intptr_t)0) != RoboOk ||
        JsObjectDefineNative(runtime, mathObject, "min", 2, JsMathBinaryNative,
                             (void *)(intptr_t)1) != RoboOk ||
        JsObjectDefineNative(runtime, mathObject, "max", 2, JsMathBinaryNative,
                             (void *)(intptr_t)2) != RoboOk ||
        JsObjectSetNumber(runtime->heap, mathObject, "PI", 3.14159265358979323846) != RoboOk) {
        return RoboOutOfMemory;
    }
    wrapped = JsValueObject(mathObject);
    if (JsRuntimeDefineGlobal(runtime, "Math", &wrapped) != RoboOk) {
        return RoboOutOfMemory;
    }

    if (JsRuntimeDefineGlobalNative(runtime, "parseInt", 1, JsParseIntNative, NULL) != RoboOk) {
        return RoboOutOfMemory;
    }
    if (JsRuntimeDefineGlobalNative(runtime, "Error", 1, JsErrorConstructorNative,
                                    (void *)(intptr_t)RoboJsErrorGeneral) != RoboOk ||
        JsRuntimeDefineGlobalNative(runtime, "TypeError", 1, JsErrorConstructorNative,
                                    (void *)(intptr_t)RoboJsErrorType) != RoboOk ||
        JsRuntimeDefineGlobalNative(runtime, "ReferenceError", 1, JsErrorConstructorNative,
                                    (void *)(intptr_t)RoboJsErrorReference) != RoboOk ||
        JsRuntimeDefineGlobalNative(runtime, "RangeError", 1, JsErrorConstructorNative,
                                    (void *)(intptr_t)RoboJsErrorRange) != RoboOk ||
        JsRuntimeDefineGlobalNative(runtime, "SyntaxError", 1, JsErrorConstructorNative,
                                    (void *)(intptr_t)RoboJsErrorSyntax) != RoboOk) {
        return RoboOutOfMemory;
    }
    return RoboOk;
}
