/*
 * RoboJS exceptions (see `RoboJSException.h`).
 *
 * An error is an ordinary object with four properties, and that is the whole design: a `catch` reads
 * `error.message` through the same member access that reads any property, and a diagnostic reads the
 * two mirrored fields (`errorKind`, `errorMessage`) without a property lookup. The duplication is
 * deliberate — one copy is the script's view, the other the host's — and both are written once, in
 * `JsErrorCreateAt`, so they cannot drift.
 */

#include "RoboJS/RoboJSException.h"

#include "RoboJS/RoboJSObject.h"

#include "RoboJSInternal.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

JsObject *JsErrorCreateAt(JsRuntime *runtime,
                          JsErrorKind kind,
                          const char *message,
                          const JsSpan *span)
{
    JsObject *object;
    JsHeap *heap;
    JsValue text;
    const char *name;

    if (runtime == NULL) {
        return NULL;
    }
    heap = JsRuntimeHeap(runtime);
    object = JsObjectCreate(heap, JsRuntimeErrorPrototype(runtime));
    if (object == NULL) {
        return NULL;
    }
    object->kind = (uint32_t)RoboJsObjectError;
    object->errorKind = (uint32_t)kind;
    object->errorMessage = JsHeapString(heap, message != NULL ? message : "", -1);
    if (object->errorMessage == NULL) {
        object->errorMessage = "";
    }
    name = JsErrorKindName(kind);
    if (JsObjectSetString(heap, object, "name", name) != RoboOk) {
        return NULL;
    }
    text = JsValueString(heap, object->errorMessage, -1);
    if (JsObjectSet(heap, object, "message", &text) != RoboOk) {
        return NULL;
    }
    if (span == NULL && runtime->executing && runtime->hasCurrentSpan) {
        /* No span of its own: a `new Error("x")`, or a native door that complained. The interpreter
         * records where the script was (`RoboJSInternal.h`), which is the position a diagnostic — and
         * `catch (e) { e.line }` — is asking for. */
        span = &runtime->currentSpan;
    }
    if (span != NULL) {
        if (JsObjectSetNumber(heap, object, "line", (double)span->line) != RoboOk) {
            return NULL;
        }
        if (JsObjectSetNumber(heap, object, "column", (double)span->column) != RoboOk) {
            return NULL;
        }
        if (JsObjectSetNumber(heap, object, "offset", (double)span->offset) != RoboOk) {
            return NULL;
        }
    }
    return object;
}

JsObject *JsErrorCreate(JsRuntime *runtime, JsErrorKind kind, const char *message)
{
    return JsErrorCreateAt(runtime, kind, message, NULL);
}

JsValue *JsRuntimeExceptionSlot(JsRuntime *runtime)
{
    if (runtime == NULL) {
        return NULL;
    }
    return &runtime->exception;
}

void JsRuntimeSetException(JsRuntime *runtime, const JsValue *value)
{
    if (runtime == NULL) {
        return;
    }
    runtime->hasException = 1;
    runtime->exception = value != NULL ? *value : JsValueUndefined();
}

void JsRuntimeThrow(JsRuntime *runtime,
                    JsErrorKind kind,
                    const JsSpan *span,
                    const char *format,
                    ...)
{
    char message[256];
    JsObject *error;
    JsValue value;

    if (runtime == NULL) {
        return;
    }
    message[0] = '\0';
    if (format != NULL) {
        va_list args;

        va_start(args, format);
        vsnprintf(message, sizeof(message), format, args);
        va_end(args);
    }
    error = JsErrorCreateAt(runtime, kind, message, span);
    if (error == NULL) {
        /* The error object could not be built, so the pending value is the message itself. That
         * keeps "there is an exception" true even at the memory limit, which is the case a caller is
         * most likely to be debugging. */
        value = JsValueString(JsRuntimeHeap(runtime), message, -1);
        JsRuntimeSetException(runtime, &value);
        return;
    }
    value = JsValueObject(error);
    JsRuntimeSetException(runtime, &value);
}

int JsRuntimeHasException(JsRuntime *runtime)
{
    return runtime != NULL && runtime->hasException;
}

Result JsRuntimePeekException(JsRuntime *runtime, JsValue *out)
{
    if (runtime == NULL || out == NULL) {
        return RoboInvalidArgument;
    }
    if (!runtime->hasException) {
        *out = JsValueUndefined();
        return RoboNotFound;
    }
    *out = runtime->exception;
    return RoboOk;
}

Result JsRuntimeTakeException(JsRuntime *runtime, JsValue *out)
{
    if (runtime == NULL || out == NULL) {
        return RoboInvalidArgument;
    }
    if (!runtime->hasException) {
        *out = JsValueUndefined();
        return RoboNotFound;
    }
    *out = runtime->exception;
    runtime->hasException = 0;
    runtime->exception = JsValueUndefined();
    return RoboOk;
}

void JsRuntimeClearException(JsRuntime *runtime)
{
    if (runtime == NULL) {
        return;
    }
    runtime->hasException = 0;
    runtime->exception = JsValueUndefined();
}

int32_t JsExceptionMessage(const JsValue *value, char *buffer, size_t capacity)
{
    if (value == NULL) {
        return 0;
    }
    if (JsValueIsObject(value) && value->object->kind == (uint32_t)RoboJsObjectError) {
        const char *message = value->object->errorMessage != NULL ? value->object->errorMessage : "";
        size_t length = strlen(message);

        if (buffer != NULL && capacity > 0) {
            size_t copy = length < capacity - 1u ? length : capacity - 1u;

            memcpy(buffer, message, copy);
            buffer[copy] = '\0';
        }
        return (int32_t)length;
    }
    return JsValueToString(value, buffer, capacity);
}

JsErrorKind JsExceptionKindOf(const JsValue *value)
{
    if (value != NULL && JsValueIsObject(value) && value->object->kind == (uint32_t)RoboJsObjectError) {
        return (JsErrorKind)value->object->errorKind;
    }
    return RoboJsErrorGeneral;
}
