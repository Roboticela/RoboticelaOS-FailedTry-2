/*
 * RoboJS values (Phase 16.4) — the six types a script can hold, and the conversions between them.
 *
 * ## A value is a tag and a payload, copied by value
 *
 * `JsValue` is a by-value struct, not a handle: passing one costs 32 bytes and allocates nothing,
 * and `JsValueUndefined()` is a value a caller can return from a function without a lifetime
 * question. The three primitives live in the struct (`number` carries both a number and a boolean,
 * `string` carries a heap-owned UTF-8 buffer); the one reference type carries a pointer to a
 * `JsObject`, which the heap owns until the runtime is reset.
 *
 * **A string value's buffer is owned by the heap and never written.** `JsValueString` copies; every
 * other door reads. That is what makes a value safe to share between two environments.
 *
 * ## Conversion is explicit, and the three answers are the ones the language defines
 *
 * `JsValueToBoolean` is the truthiness rule (falsy: undefined, null, false, 0, -0, NaN, "").
 * `JsValueToNumber` is the numeric rule (a string that is not a number is NaN, `null` is 0,
 * `undefined` is NaN). `JsValueToString` writes into a caller's buffer rather than allocating,
 * because its two hot callers — `+`, and a diagnostic — both want text right now.
 *
 * ## What is deliberately not here
 *
 * No BigInt, no Symbol, no typed arrays. No `JsValue` operation calls into the object model beyond
 * `ToString`'s `[object Object]`, which is what keeps this header includable by the lexer's tests.
 */

#ifndef ROBO_JS_VALUE_H
#define ROBO_JS_VALUE_H

#include "RoboJS/RoboJSHeap.h"
#include "RoboJS/RoboJSTypes.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct JsObject JsObject;

/** The six value types. `JsValueTypeObject` covers arrays and functions too. */
typedef enum JsValueType {
    RoboJsTypeUndefined = 0,
    RoboJsTypeNull = 1,
    RoboJsTypeBoolean = 2,
    RoboJsTypeNumber = 3,
    RoboJsTypeString = 4,
    RoboJsTypeObject = 5
} JsValueType;

int JsValueTypeIsKnown(JsValueType type);

/** Human-readable type name as `typeof` spells it; never NULL. */
const char *JsValueTypeName(JsValueType type);

/**
 * One JavaScript value.
 *
 * `flags` is reserved for the object pointer's meaning (see `RoboJSObject.h`) and is 0 for every
 * primitive. `stringLength` is the byte length of `string`, so a caller never calls `strlen` on a
 * value it is about to copy.
 */
typedef struct JsValue {
    uint32_t type;
    uint32_t flags;
    double number;
    const char *string;
    int32_t stringLength;
    JsObject *object;
} JsValue;

/* ------------------------------------------------------------------ */
/* Construction                                                        */
/* ------------------------------------------------------------------ */

JsValue JsValueUndefined(void);
JsValue JsValueNull(void);
JsValue JsValueBool(int truth);
JsValue JsValueNumber(double number);
/** Copy @p utf8 (length < 0 → strlen) into @p heap and answer a string value. A NULL heap or text
 *  answers an empty string rather than a broken value. */
JsValue JsValueString(JsHeap *heap, const char *utf8, int32_t length);
JsValue JsValueObject(JsObject *object);
/** `undefined` when @p object is NULL — the one door that keeps a NULL from becoming a value. */
JsValue JsValueObjectOrUndefined(JsObject *object);

/* ------------------------------------------------------------------ */
/* Inspection                                                          */
/* ------------------------------------------------------------------ */

uint32_t JsValueTypeOf(const JsValue *value);
int JsValueIsUndefined(const JsValue *value);
int JsValueIsNull(const JsValue *value);
int JsValueIsNullOrUndefined(const JsValue *value);
int JsValueIsBoolean(const JsValue *value);
int JsValueIsNumber(const JsValue *value);
int JsValueIsString(const JsValue *value);
int JsValueIsObject(const JsValue *value);
/** A callable: an object whose kind is function. */
int JsValueIsFunction(const JsValue *value);
/** An array object (16.5). */
int JsValueIsArray(const JsValue *value);

/** The boolean payload. A non-boolean answers 0. */
int JsValueAsBool(const JsValue *value);
/** The numeric payload. A non-number answers NaN. */
double JsValueAsNumber(const JsValue *value);
/** The string payload; never NULL for a string value, NULL for every other type. */
const char *JsValueAsString(const JsValue *value);
JsObject *JsValueAsObject(const JsValue *value);

/** 1 when the value is a number with no fractional part inside `int32_t`. */
int JsValueIsInteger(const JsValue *value);
/**
 * The value as an `int32_t`, ECMAScript ToInt32: NaN and the infinities are 0 and the result wraps
 * modulo 2^32. Never fails — this is the door array indices and `String.fromCharCode`-shaped
 * arithmetic use.
 */
int32_t JsValueToInt32(const JsValue *value);

/* ------------------------------------------------------------------ */
/* Conversion                                                          */
/* ------------------------------------------------------------------ */

int JsValueToBoolean(const JsValue *value);
double JsValueToNumber(const JsValue *value);
/** `JsValueToNumber` applied to a string's *value*, exported because `+` needs it. */
double JsStringToNumber(const char *text, int32_t length);
/**
 * Write the value's string form into @p buffer (NUL-terminated). Answers the number of bytes that
 * a complete form needs, so a caller can size a second attempt; the buffer is always NUL-terminated
 * when @p capacity > 0. `undefined`, `null`, `true`, `false`, a number, the string itself, and
 * `[object Object]` for every other object.
 */
int32_t JsValueToString(const JsValue *value, char *buffer, size_t capacity);
/** `JsNumberToString` — `NaN`, `Infinity`, `-0`, an integer without a decimal point, else `%g`. */
int32_t JsNumberToString(double number, char *buffer, size_t capacity);
/** `typeof` (16.4): `"undefined"`, `"object"` (null and arrays), `"boolean"`, `"number"`,
 *  `"string"`, `"function"`. Never NULL. */
const char *JsValueTypeOfString(const JsValue *value);

/* ------------------------------------------------------------------ */
/* Equality                                                            */
/* ------------------------------------------------------------------ */

/** `===`. A number compares by value (NaN is not equal to itself); a string by bytes; an object by
 *  identity. */
int JsValueStrictEquals(const JsValue *a, const JsValue *b);
/** `==`, the minimal part of the loose rule set: same-type is strict; null == undefined; number and
 *  string coerce; boolean coerces to number; an object and a primitive compare through ToString. */
int JsValueLooseEquals(const JsValue *a, const JsValue *b);

/** `+`'s string half: 1 when either operand is a string, so the caller concatenates. */
int JsValuePlusConcatenates(const JsValue *a, const JsValue *b);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_JS_VALUE_H */
