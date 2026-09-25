/*
 * RoboJS objects (Phase 16.5) — property storage, lookup through a prototype, and arrays.
 *
 * One struct carries a plain object, an array, a function, an error and a DOM wrapper. The
 * alternative — a tagged union of five structs — buys a few bytes per object and costs every call
 * site a switch; the fields that do not apply to a kind are zero, and `kind` is the one thing a
 * caller must read before touching a specialised field.
 *
 * ## Prototypes are a chain, and the chain ends at `null`
 *
 * `JsObjectGet` walks `prototype` until it finds the key or runs out, which is why an array sees
 * `push` and a plain object sees nothing. The interpreter finds a *method call* by looking up the
 * property and then calling it with the receiver as `this`; there is no separate method table.
 *
 * ## Arrays carry their elements contiguously and their named properties beside them
 *
 * `elements[0..length)` is the indexed half and `properties` is the named half, so `a[3]` is a
 * bounds check rather than a key lookup, and `a.foo = 1` works without pretending `foo` is an
 * index. `length` is derived from the element array (`JsArraySetLength` truncates or grows it) and
 * is reported as a number by the interpreter's member access — it is deliberately **not** a stored
 * property, so it can never disagree with the array's actual size.
 *
 * ## Property attributes are stored and enforced
 *
 * `JsPropWritable`, `JsPropEnumerable` and `JsPropConfigurable` are what they say; a non-writable
 * property refuses `JsObjectSet` with `RoboPermissionDenied` (which the interpreter turns into a
 * `TypeError` in strict-ish terms, or silently ignores for a plain assignment — the caller decides,
 * this layer reports). `JsObjectOwnKeys` answers enumerable names, which is what a `for...in` reads.
 *
 * ## The DOM half
 *
 * `host` and `hostKind` are how a DOM wrapper (16.10) stays an ordinary object: the wrapper's
 * `host` is the host's node id, and the binding's native functions reach the host through it. No DOM
 * header is included here, which is what keeps this header useable by a value test.
 */

#ifndef ROBO_JS_OBJECT_H
#define ROBO_JS_OBJECT_H

#include "RoboJS/RoboJSFunction.h"
#include "RoboJS/RoboJSHeap.h"
#include "RoboJS/RoboJSTypes.h"
#include "RoboJS/RoboJSValue.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Object kinds. */
typedef enum JsObjectKind {
    RoboJsObjectPlain = 0,
    RoboJsObjectArray = 1,
    RoboJsObjectFunction = 2,
    RoboJsObjectError = 3,
    /** A wrapper around a host object (a DOM node in 16.10, a timer handle elsewhere). */
    RoboJsObjectHost = 4
} JsObjectKind;

int JsObjectKindIsKnown(JsObjectKind kind);
const char *JsObjectKindName(JsObjectKind kind);

/**
 * A host object's computed properties (16.10).
 *
 * A DOM wrapper's `id`, `textContent` and `children` are *computed from the host*, not stored, and
 * this build's object model has no accessor properties. So a host object carries two function
 * pointers instead: when an ordinary lookup finds nothing on the object or its prototype chain, the
 * object's `hostOps->get` is asked, and a write that finds nothing is offered to `hostOps->set`.
 *
 * A door that has no answer for a key returns `RoboNotFound`, which is what keeps this from
 * swallowing a genuine miss: `el.nosuchthing` is still `undefined`.
 */
typedef struct JsHostOps {
    Result (*get)(JsObject *object, const char *key, JsValue *out);
    Result (*set)(JsObject *object, const char *key, const JsValue *value);
} JsHostOps;

/** Property attributes (the subset this build enforces). */
#define JsPropWritable 1u
#define JsPropEnumerable 2u
#define JsPropConfigurable 4u
#define JsPropAll (JsPropWritable | JsPropEnumerable | JsPropConfigurable)

typedef struct JsProperty {
    const char *key; /* heap-owned, NUL-terminated */
    JsValue value;
    uint32_t attributes;
} JsProperty;

/**
 * An object. Every field is zero for a fresh plain object except `kind` and `prototype`. The
 * private fields (`properties`, `elements`) are owned by the heap and must not be written by a
 * caller — `JsObjectSet` and the array doors are the only writers, so the count and the capacity
 * can never disagree with the storage.
 */
typedef struct JsObject {
    uint32_t kind; /* JsObjectKind */
    uint32_t flags;
    JsObject *prototype;

    JsProperty *properties;
    int32_t propertyCount;
    int32_t propertyCapacity;

    JsValue *elements; /* arrays only */
    int32_t length;
    int32_t capacity;

    JsFunction fn; /* functions only */

    /** Host objects only: the binding's own id, the node/handle it wraps, its binding record and
     *  the computed-property doors. */
    uint32_t hostKind;
    uint64_t hostId;
    void *hostData;
    const JsHostOps *hostOps;

    /** Error objects only: the message, kept beside the property so a diagnostic can read it
     *  without a property lookup. */
    const char *errorMessage;
    uint32_t errorKind;
} JsObject;

/* ------------------------------------------------------------------ */
/* Creation                                                            */
/* ------------------------------------------------------------------ */

/** A plain object whose `[[Prototype]]` is @p prototype (may be NULL). */
JsObject *JsObjectCreate(JsHeap *heap, JsObject *prototype);

/**
 * An array of @p length elements (0 when @p length is negative), each `undefined`. @p prototype is
 * the array prototype.
 */
JsObject *JsArrayCreate(JsHeap *heap, JsObject *prototype, int32_t length);

/**
 * A host wrapper object: kind `Host`, with @p hostKind/@p hostId/@p hostData stored. `prototype` is
 * the wrapper prototype the binding passes.
 */
JsObject *JsObjectCreateHost(JsHeap *heap,
                             JsObject *prototype,
                             uint32_t hostKind,
                             uint64_t hostId,
                             void *hostData);

/* ------------------------------------------------------------------ */
/* Properties                                                          */
/* ------------------------------------------------------------------ */

/**
 * Read @p key, walking the prototype chain.
 * @return RoboOk with @p out set; RoboNotFound when neither the object nor its chain has the key
 *         (@p out is left `undefined`).
 */
Result JsObjectGet(JsObject *object, const char *key, JsValue *out);

/** Read a key whose length is known (a property name with embedded NULs is not a case here, but a
 *  name taken from a token is). */
Result JsObjectGetN(JsObject *object, const char *key, int32_t keyLength, JsValue *out);

/**
 * Define or overwrite @p key **on this object**, with the given attributes for a new property.
 * Overwriting a writable property keeps its attributes and updates the value.
 * @return RoboOk; RoboPermissionDenied when the existing property is not writable; RoboOutOfMemory
 *         when the object is at `maxProperties`.
 */
Result JsObjectSet(JsHeap *heap,
                   JsObject *object,
                   const char *key,
                   const JsValue *value);

/** `JsObjectSet` with explicit attributes for a property that does not exist yet. */
Result JsObjectDefine(JsHeap *heap,
                      JsObject *object,
                      const char *key,
                      const JsValue *value,
                      uint32_t attributes);

/** Set @p key to @p value with default attributes (`JsPropAll`). */
Result JsObjectSetString(JsHeap *heap, JsObject *object, const char *key, const char *value);

/**
 * Define @p key with @p value **and** @p attributes, replacing both whatever the property's current
 * attributes are — the specification's `DefineOwnProperty`, and the only door that can write a
 * non-writable property.
 *
 * `JsObjectDefine` refuses an existing non-writable property with `RoboPermissionDenied`, and that is
 * the correct answer for a *write*. It is the wrong answer for a binding's own initialization: a
 * `const` is hoisted non-writable and its initializer is the write that gives it a value, so the
 * mirror that carries the binding onto the global object has to be able to set the value **and**
 * keep the attribute. Reaching for `JsObjectDefine` there would make
 * `const answer = 42` fail with a permission error, and reaching for `JsObjectSet` would silently
 * widen the property back to writable and lose `const`-ness.
 *
 * @return RoboOk; RoboInvalidArgument; RoboOutOfMemory at the object's property cap.
 */
Result JsObjectDefineOwn(JsHeap *heap,
                         JsObject *object,
                         const char *key,
                         const JsValue *value,
                         uint32_t attributes);
Result JsObjectSetNumber(JsHeap *heap, JsObject *object, const char *key, double value);
Result JsObjectSetBool(JsHeap *heap, JsObject *object, const char *key, int value);

/** Look up @p key's own property record (no prototype walk). NULL when absent. */
JsProperty *JsObjectFindOwn(JsObject *object, const char *key);
/** Read @p key from **this object only**. RoboNotFound when absent. */
Result JsObjectGetOwn(JsObject *object, const char *key, JsValue *out);
/** 1 when @p key exists on this object or its chain. */
int JsObjectHas(JsObject *object, const char *key);
/** 1 when @p key is an own property. */
int JsObjectHasOwn(JsObject *object, const char *key);
/** Remove an own property. RoboNotFound when absent, RoboPermissionDenied when not configurable. */
Result JsObjectDelete(JsObject *object, const char *key);

/** Number of own properties (the named half; an array's elements are not counted). */
int32_t JsObjectOwnPropertyCount(const JsObject *object);
/**
 * Write the names of the enumerable own properties into @p keys (bounded by @p maxKeys).
 * @return the number written; 0 when there are none.
 */
int32_t JsObjectOwnKeys(const JsObject *object, const char **keys, int32_t maxKeys);

/* ------------------------------------------------------------------ */
/* Arrays                                                              */
/* ------------------------------------------------------------------ */

/** The numeric half: index access with no key formatting. */
Result JsArrayGet(const JsObject *array, int32_t index, JsValue *out);
Result JsArraySet(JsHeap *heap, JsObject *array, int32_t index, const JsValue *value);
/**
 * `push`: append one value. @return the new length, or a negative `Result` on failure — which the
 * array binding turns into a `TypeError`.
 */
int32_t JsArrayPush(JsHeap *heap, JsObject *array, const JsValue *value);
/** `pop`: remove and answer the last element; `undefined` when empty. */
Result JsArrayPop(JsObject *array, JsValue *out);
/**
 * Set `length` explicitly: truncating drops the tail, growing fills with `undefined`. A negative or
 * non-integer length is `RoboInvalidArgument` (the caller reports a `RangeError`).
 */
Result JsArraySetLength(JsHeap *heap, JsObject *array, int32_t length);

/** 1 when @p key is a canonical array index (`"0"`, `"12"`, not `"01"`, not `"-1"`). */
int32_t JsArrayIndexOfKey(const char *key, int32_t *outIndex);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_JS_OBJECT_H */
