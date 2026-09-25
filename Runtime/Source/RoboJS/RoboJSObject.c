/*
 * RoboJS objects (see `RoboJSObject.h`).
 *
 * Three decisions are visible here and nowhere else.
 *
 *   **A property array grows by doubling and is reallocated through the heap's abandon-in-place
 *   `JsHeapRealloc`**, so a pointer into it is invalid after an insert. Nothing outside this file
 *   holds one: `JsObjectFindOwn` hands out a `JsProperty *` and is documented as valid until the
 *   next write, and every other door copies the value out.
 *
 *   **`JsObjectSet` is define-or-overwrite, and the difference is the attribute check.** Overwriting
 *   a non-writable own property is `RoboPermissionDenied`; a property reached through the prototype
 *   chain is *not* the object's to overwrite, so the write creates an own property instead — the rule
 *   that makes `o.toString = 1` work while `Object.prototype.toString` stays intact.
 *
 *   **An array's `length` is derived.** It is `array->length`, reported by the interpreter's member
 *   access as a number, and there is no stored `"length"` property that could disagree with it.
 */

#include "RoboJS/RoboJSObject.h"

#include <string.h>

int JsObjectKindIsKnown(JsObjectKind kind)
{
    return (int)kind >= 0 && (int)kind <= (int)RoboJsObjectHost;
}

const char *JsObjectKindName(JsObjectKind kind)
{
    switch (kind) {
    case RoboJsObjectPlain: return "plain";
    case RoboJsObjectArray: return "array";
    case RoboJsObjectFunction: return "function";
    case RoboJsObjectError: return "error";
    case RoboJsObjectHost: return "host";
    default: return "unknown";
    }
}

/* ------------------------------------------------------------------ */
/* Creation                                                            */
/* ------------------------------------------------------------------ */

static JsObject *JsObjectAllocate(JsHeap *heap, JsObject *prototype, uint32_t kind)
{
    JsObject *object;

    if (heap == NULL) {
        return NULL;
    }
    object = (JsObject *)JsHeapAlloc(heap, sizeof(JsObject));
    if (object == NULL) {
        return NULL;
    }
    object->kind = kind;
    object->prototype = prototype;
    return object;
}

JsObject *JsObjectCreate(JsHeap *heap, JsObject *prototype)
{
    return JsObjectAllocate(heap, prototype, (uint32_t)RoboJsObjectPlain);
}

static int JsArrayReserve(JsHeap *heap, JsObject *array, int32_t needed)
{
    int32_t capacity;
    JsValue *fresh;

    if (array->capacity >= needed) {
        return 1;
    }
    capacity = array->capacity > 0 ? array->capacity : 4;
    while (capacity < needed) {
        capacity *= 2;
        if (capacity <= 0) {
            capacity = needed;
            break;
        }
    }
    if (capacity > JsHeapMaxProperties(heap)) {
        capacity = needed;
    }
    fresh = (JsValue *)JsHeapRealloc(heap,
                                     array->elements,
                                     (size_t)array->capacity * sizeof(JsValue),
                                     (size_t)capacity * sizeof(JsValue));
    if (fresh == NULL) {
        return 0;
    }
    array->elements = fresh;
    array->capacity = capacity;
    return 1;
}

JsObject *JsArrayCreate(JsHeap *heap, JsObject *prototype, int32_t length)
{
    JsObject *array;

    array = JsObjectAllocate(heap, prototype, (uint32_t)RoboJsObjectArray);
    if (array == NULL) {
        return NULL;
    }
    if (length > 0) {
        int32_t i;

        if (!JsArrayReserve(heap, array, length)) {
            return NULL;
        }
        for (i = 0; i < length; ++i) {
            array->elements[i] = JsValueUndefined();
        }
        array->length = length;
    }
    return array;
}

JsObject *JsObjectCreateHost(JsHeap *heap,
                             JsObject *prototype,
                             uint32_t hostKind,
                             uint64_t hostId,
                             void *hostData)
{
    JsObject *object;

    object = JsObjectAllocate(heap, prototype, (uint32_t)RoboJsObjectHost);
    if (object == NULL) {
        return NULL;
    }
    object->hostKind = hostKind;
    object->hostId = hostId;
    object->hostData = hostData;
    return object;
}

/* ------------------------------------------------------------------ */
/* Properties                                                          */
/* ------------------------------------------------------------------ */

static int JsKeyEquals(const char *a, const char *b)
{
    if (a == NULL || b == NULL) {
        return a == b;
    }
    return strcmp(a, b) == 0;
}

JsProperty *JsObjectFindOwn(JsObject *object, const char *key)
{
    int32_t i;

    if (object == NULL || key == NULL) {
        return NULL;
    }
    for (i = 0; i < object->propertyCount; ++i) {
        if (JsKeyEquals(object->properties[i].key, key)) {
            return &object->properties[i];
        }
    }
    return NULL;
}

Result JsObjectGetOwn(JsObject *object, const char *key, JsValue *out)
{
    JsProperty *property;

    if (out == NULL) {
        return RoboInvalidArgument;
    }
    *out = JsValueUndefined();
    if (object == NULL || key == NULL) {
        return RoboInvalidArgument;
    }
    property = JsObjectFindOwn(object, key);
    if (property == NULL) {
        return RoboNotFound;
    }
    *out = property->value;
    return RoboOk;
}

Result JsObjectGetN(JsObject *object, const char *key, int32_t keyLength, JsValue *out)
{
    char buffer[256];
    int32_t copied;

    if (object == NULL || key == NULL || out == NULL) {
        return RoboInvalidArgument;
    }
    if (keyLength >= 0 && keyLength < (int32_t)sizeof(buffer)) {
        JsObject *cursor = object;

        memcpy(buffer, key, (size_t)keyLength);
        buffer[keyLength] = '\0';
        copied = keyLength;
        (void)copied;
        return JsObjectGet(cursor, buffer, out);
    }
    /* A key longer than the local buffer cannot be an own property of anything this build created —
     * every stored key comes through `JsObjectDefine`, whose buffer is this size or smaller — so the
     * answer is "not found" rather than a truncated lookup that might match a shorter name. */
    *out = JsValueUndefined();
    return RoboNotFound;
}

Result JsObjectGet(JsObject *object, const char *key, JsValue *out)
{
    JsObject *cursor;

    if (object == NULL || key == NULL || out == NULL) {
        return RoboInvalidArgument;
    }
    *out = JsValueUndefined();
    cursor = object;
    while (cursor != NULL) {
        JsProperty *property = JsObjectFindOwn(cursor, key);

        if (property != NULL) {
            *out = property->value;
            return RoboOk;
        }
        cursor = cursor->prototype;
    }
    /* A host object computes the properties only it can answer — see `JsHostOps`. The fallback is
     * last, so a wrapper that *does* carry an own property of that name answers from storage. */
    if (object->hostOps != NULL && object->hostOps->get != NULL) {
        return object->hostOps->get(object, key, out);
    }
    return RoboNotFound;
}

static Result JsObjectAppendProperty(JsHeap *heap,
                                     JsObject *object,
                                     const char *key,
                                     const JsValue *value,
                                     uint32_t attributes)
{
    JsProperty *fresh;
    int32_t capacity;
    const char *ownedKey;

    if (object->propertyCount >= JsHeapMaxProperties(heap)) {
        return RoboOutOfMemory;
    }
    if (object->propertyCount == object->propertyCapacity) {
        capacity = object->propertyCapacity > 0 ? object->propertyCapacity * 2 : 4;
        if (capacity > JsHeapMaxProperties(heap)) {
            capacity = JsHeapMaxProperties(heap);
        }
        if (capacity <= object->propertyCount) {
            return RoboOutOfMemory;
        }
        fresh = (JsProperty *)JsHeapRealloc(heap,
                                            object->properties,
                                            (size_t)object->propertyCapacity * sizeof(JsProperty),
                                            (size_t)capacity * sizeof(JsProperty));
        if (fresh == NULL) {
            return RoboOutOfMemory;
        }
        object->properties = fresh;
        object->propertyCapacity = capacity;
    }
    ownedKey = JsHeapString(heap, key, -1);
    if (ownedKey == NULL) {
        return RoboOutOfMemory;
    }
    object->properties[object->propertyCount].key = ownedKey;
    object->properties[object->propertyCount].value = *value;
    object->properties[object->propertyCount].attributes = attributes;
    object->propertyCount += 1;
    return RoboOk;
}

Result JsObjectDefine(JsHeap *heap,
                      JsObject *object,
                      const char *key,
                      const JsValue *value,
                      uint32_t attributes)
{
    JsProperty *existing;

    if (heap == NULL || object == NULL || key == NULL || value == NULL) {
        return RoboInvalidArgument;
    }
    if (key[0] == '\0') {
        return RoboInvalidArgument;
    }
    existing = JsObjectFindOwn(object, key);
    if (existing != NULL) {
        if ((existing->attributes & JsPropWritable) == 0u) {
            return RoboPermissionDenied;
        }
        existing->value = *value;
        return RoboOk;
    }
    return JsObjectAppendProperty(heap, object, key, value, attributes);
}

Result JsObjectDefineOwn(JsHeap *heap,
                         JsObject *object,
                         const char *key,
                         const JsValue *value,
                         uint32_t attributes)
{
    JsProperty *existing;

    if (heap == NULL || object == NULL || key == NULL || value == NULL) {
        return RoboInvalidArgument;
    }
    if (key[0] == '\0') {
        return RoboInvalidArgument;
    }
    existing = JsObjectFindOwn(object, key);
    if (existing != NULL) {
        /* Both, unconditionally: this is the door that can write a non-writable property, which is
         * exactly what a `const` binding's own initialization needs. */
        existing->value = *value;
        existing->attributes = attributes;
        return RoboOk;
    }
    return JsObjectAppendProperty(heap, object, key, value, attributes);
}

Result JsObjectSet(JsHeap *heap, JsObject *object, const char *key, const JsValue *value)
{
    Result status;

    if (object != NULL && object->hostOps != NULL && object->hostOps->set != NULL &&
        JsObjectFindOwn(object, key) == NULL) {
        Result handled = object->hostOps->set(object, key, value);

        if (handled == RoboOk) {
            return RoboOk;
        }
        if (handled != RoboNotFound) {
            /* The host recognised the key and refused the write — a read-only property, or a
             * mutation against a destroyed node. Reporting that as a *stored* property would hide
             * the refusal, so it is returned as it is. */
            return handled;
        }
    }
    status = JsObjectDefine(heap, object, key, value, JsPropAll);
    return status;
}

Result JsObjectSetString(JsHeap *heap, JsObject *object, const char *key, const char *value)
{
    JsValue wrapped = JsValueString(heap, value, -1);

    return JsObjectSet(heap, object, key, &wrapped);
}

Result JsObjectSetNumber(JsHeap *heap, JsObject *object, const char *key, double value)
{
    JsValue wrapped = JsValueNumber(value);

    return JsObjectSet(heap, object, key, &wrapped);
}

Result JsObjectSetBool(JsHeap *heap, JsObject *object, const char *key, int value)
{
    JsValue wrapped = JsValueBool(value);

    return JsObjectSet(heap, object, key, &wrapped);
}

int JsObjectHas(JsObject *object, const char *key)
{
    JsObject *cursor = object;

    if (object == NULL || key == NULL) {
        return 0;
    }
    while (cursor != NULL) {
        if (JsObjectFindOwn(cursor, key) != NULL) {
            return 1;
        }
        cursor = cursor->prototype;
    }
    if (object->hostOps != NULL && object->hostOps->get != NULL) {
        JsValue probe;

        return object->hostOps->get(object, key, &probe) == RoboOk;
    }
    return 0;
}

int JsObjectHasOwn(JsObject *object, const char *key)
{
    return JsObjectFindOwn(object, key) != NULL;
}

Result JsObjectDelete(JsObject *object, const char *key)
{
    int32_t i;

    if (object == NULL || key == NULL) {
        return RoboInvalidArgument;
    }
    for (i = 0; i < object->propertyCount; ++i) {
        if (JsKeyEquals(object->properties[i].key, key)) {
            int32_t move;

            if ((object->properties[i].attributes & JsPropConfigurable) == 0u) {
                return RoboPermissionDenied;
            }
            for (move = i; move + 1 < object->propertyCount; ++move) {
                object->properties[move] = object->properties[move + 1];
            }
            object->propertyCount -= 1;
            return RoboOk;
        }
    }
    return RoboNotFound;
}

int32_t JsObjectOwnPropertyCount(const JsObject *object)
{
    return object != NULL ? object->propertyCount : 0;
}

int32_t JsObjectOwnKeys(const JsObject *object, const char **keys, int32_t maxKeys)
{
    int32_t i;
    int32_t written = 0;

    if (object == NULL || keys == NULL || maxKeys <= 0) {
        return 0;
    }
    for (i = 0; i < object->propertyCount && written < maxKeys; ++i) {
        if ((object->properties[i].attributes & JsPropEnumerable) != 0u) {
            keys[written] = object->properties[i].key;
            written += 1;
        }
    }
    return written;
}

/* ------------------------------------------------------------------ */
/* Arrays                                                              */
/* ------------------------------------------------------------------ */

Result JsArrayGet(const JsObject *array, int32_t index, JsValue *out)
{
    if (array == NULL || out == NULL) {
        return RoboInvalidArgument;
    }
    if (index < 0 || index >= array->length) {
        *out = JsValueUndefined();
        return RoboNotFound;
    }
    *out = array->elements[index];
    return RoboOk;
}

Result JsArraySet(JsHeap *heap, JsObject *array, int32_t index, const JsValue *value)
{
    if (heap == NULL || array == NULL || value == NULL) {
        return RoboInvalidArgument;
    }
    if (index < 0) {
        return RoboInvalidArgument;
    }
    if (index >= JsHeapMaxProperties(heap)) {
        return RoboOutOfMemory;
    }
    /* Writing past the end grows the array and leaves the gap `undefined`, which is what
     * `a[5] = 1` on a fresh array does. */
    if (index >= array->length) {
        int32_t i;

        if (!JsArrayReserve(heap, array, index + 1)) {
            return RoboOutOfMemory;
        }
        for (i = array->length; i < index; ++i) {
            array->elements[i] = JsValueUndefined();
        }
        array->length = index + 1;
    }
    array->elements[index] = *value;
    return RoboOk;
}

int32_t JsArrayPush(JsHeap *heap, JsObject *array, const JsValue *value)
{
    Result status;

    if (array == NULL || value == NULL) {
        return (int32_t)RoboInvalidArgument;
    }
    status = JsArraySet(heap, array, array->length, value);
    if (status != RoboOk) {
        return -(int32_t)status;
    }
    return array->length;
}

Result JsArrayPop(JsObject *array, JsValue *out)
{
    if (array == NULL || out == NULL) {
        return RoboInvalidArgument;
    }
    if (array->length <= 0) {
        *out = JsValueUndefined();
        return RoboOk;
    }
    array->length -= 1;
    *out = array->elements[array->length];
    return RoboOk;
}

Result JsArraySetLength(JsHeap *heap, JsObject *array, int32_t length)
{
    int32_t i;

    if (heap == NULL || array == NULL || length < 0) {
        return RoboInvalidArgument;
    }
    if (length > array->length) {
        if (!JsArrayReserve(heap, array, length)) {
            return RoboOutOfMemory;
        }
        for (i = array->length; i < length; ++i) {
            array->elements[i] = JsValueUndefined();
        }
    }
    array->length = length;
    return RoboOk;
}

int32_t JsArrayIndexOfKey(const char *key, int32_t *outIndex)
{
    int32_t i;
    int64_t value = 0;

    if (key == NULL || key[0] == '\0') {
        return 0;
    }
    if (key[0] == '0' && key[1] != '\0') {
        /* `"01"` is a name, not an index — the canonical-form rule, and the reason `a[01]` and
         * `a[1]` are different keys. */
        return 0;
    }
    for (i = 0; key[i] != '\0'; ++i) {
        if (key[i] < '0' || key[i] > '9') {
            return 0;
        }
        value = value * 10 + (int64_t)(key[i] - '0');
        if (value > 2147483647) {
            return 0;
        }
    }
    if (outIndex != NULL) {
        *outIndex = (int32_t)value;
    }
    return 1;
}
