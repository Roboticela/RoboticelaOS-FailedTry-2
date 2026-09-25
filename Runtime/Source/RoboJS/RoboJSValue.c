/*
 * RoboJS values (see `RoboJSValue.h`).
 *
 * The two converting doors — `JsValueToNumber` and `JsValueToString` — carry the language's rules in
 * one place each. `JsNumberToString` is deliberately *not* `printf("%g")`: an integer prints without
 * a decimal point, `-0` prints as `0`, the two infinities and NaN print as their names, and a
 * fractional value prints with the shortest form that round-trips. The last one is approximated:
 * `%g` with seventeen significant digits, trimmed. A test that wanted the shortest round-tripping
 * form of `0.1` would need a big-number algorithm, and this build states that rather than pretending
 * (`Documentation/RoboJS/Limitations.md`).
 */

#include "RoboJS/RoboJSValue.h"

#include "RoboJS/RoboJSException.h"
#include "RoboJS/RoboJSObject.h"

#include "RoboJSMath.h"
#include <stdio.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/* Type names                                                          */
/* ------------------------------------------------------------------ */

int JsValueTypeIsKnown(JsValueType type)
{
    return (int)type >= 0 && (int)type <= (int)RoboJsTypeObject;
}

const char *JsValueTypeName(JsValueType type)
{
    switch (type) {
    case RoboJsTypeUndefined: return "undefined";
    case RoboJsTypeNull: return "object";
    case RoboJsTypeBoolean: return "boolean";
    case RoboJsTypeNumber: return "number";
    case RoboJsTypeString: return "string";
    case RoboJsTypeObject: return "object";
    default: return "unknown";
    }
}

/* ------------------------------------------------------------------ */
/* Construction                                                        */
/* ------------------------------------------------------------------ */

JsValue JsValueUndefined(void)
{
    JsValue value;

    memset(&value, 0, sizeof(value));
    value.type = RoboJsTypeUndefined;
    return value;
}

JsValue JsValueNull(void)
{
    JsValue value;

    memset(&value, 0, sizeof(value));
    value.type = RoboJsTypeNull;
    return value;
}

JsValue JsValueBool(int truth)
{
    JsValue value;

    memset(&value, 0, sizeof(value));
    value.type = RoboJsTypeBoolean;
    value.number = truth ? 1.0 : 0.0;
    return value;
}

JsValue JsValueNumber(double number)
{
    JsValue value;

    memset(&value, 0, sizeof(value));
    value.type = RoboJsTypeNumber;
    value.number = number;
    return value;
}

JsValue JsValueString(JsHeap *heap, const char *utf8, int32_t length)
{
    JsValue value;

    memset(&value, 0, sizeof(value));
    value.type = RoboJsTypeString;
    value.string = JsHeapString(heap, utf8, length);
    if (value.string == NULL) {
        /* An empty string is a usable value; NULL is not. A heap that cannot give one byte gives a
         * value that compares equal to "" and never crashes a caller. */
        value.string = "";
        value.stringLength = 0;
        return value;
    }
    value.stringLength = (int32_t)strlen(value.string);
    return value;
}

JsValue JsValueObject(JsObject *object)
{
    JsValue value;

    memset(&value, 0, sizeof(value));
    value.type = RoboJsTypeObject;
    value.object = object;
    value.flags = object != NULL ? object->kind : 0u;
    return value;
}

JsValue JsValueObjectOrUndefined(JsObject *object)
{
    if (object == NULL) {
        return JsValueUndefined();
    }
    return JsValueObject(object);
}

/* ------------------------------------------------------------------ */
/* Inspection                                                          */
/* ------------------------------------------------------------------ */

uint32_t JsValueTypeOf(const JsValue *value)
{
    return value != NULL ? value->type : (uint32_t)RoboJsTypeUndefined;
}

int JsValueIsUndefined(const JsValue *value)
{
    return value != NULL && value->type == RoboJsTypeUndefined;
}

int JsValueIsNull(const JsValue *value)
{
    return value != NULL && value->type == RoboJsTypeNull;
}

int JsValueIsNullOrUndefined(const JsValue *value)
{
    return JsValueIsUndefined(value) || JsValueIsNull(value);
}

int JsValueIsBoolean(const JsValue *value)
{
    return value != NULL && value->type == RoboJsTypeBoolean;
}

int JsValueIsNumber(const JsValue *value)
{
    return value != NULL && value->type == RoboJsTypeNumber;
}

int JsValueIsString(const JsValue *value)
{
    return value != NULL && value->type == RoboJsTypeString;
}

int JsValueIsObject(const JsValue *value)
{
    return value != NULL && value->type == RoboJsTypeObject;
}

int JsValueIsFunction(const JsValue *value)
{
    return JsValueIsObject(value) && value->object != NULL &&
           value->object->kind == (uint32_t)RoboJsObjectFunction;
}

int JsValueIsArray(const JsValue *value)
{
    return JsValueIsObject(value) && value->object != NULL &&
           value->object->kind == (uint32_t)RoboJsObjectArray;
}

int JsValueAsBool(const JsValue *value)
{
    if (!JsValueIsBoolean(value)) {
        return 0;
    }
    return value->number != 0.0 ? 1 : 0;
}

double JsValueAsNumber(const JsValue *value)
{
    if (!JsValueIsNumber(value)) {
        return JsMathNan();
    }
    return value->number;
}

const char *JsValueAsString(const JsValue *value)
{
    if (!JsValueIsString(value)) {
        return NULL;
    }
    return value->string;
}

JsObject *JsValueAsObject(const JsValue *value)
{
    if (!JsValueIsObject(value)) {
        return NULL;
    }
    return value->object;
}

int JsValueIsInteger(const JsValue *value)
{
    double number;

    if (!JsValueIsNumber(value)) {
        return 0;
    }
    number = value->number;
    if (number != JsMathFloor(number)) {
        return 0;
    }
    if (number < -2147483648.0 || number > 2147483647.0) {
        return 0;
    }
    return 1;
}

int32_t JsValueToInt32(const JsValue *value)
{
    double number;
    double truncated;
    double modulo;

    number = JsValueToNumber(value);
    if (!(number == number) || number == 0.0) {
        /* NaN and ±0 both answer 0, which is what ToInt32 says. `number == number` is the NaN test:
         * a NaN compares unequal to itself, and no library call is needed. */
        return 0;
    }
    if (!JsMathIsFinite(number)) {
        /* The infinities answer 0 too, and they must be caught here: `fmod` gives NaN for them and
         * the cast at the bottom would then be undefined rather than zero. */
        return 0;
    }
    truncated = number < 0 ? JsMathCeil(number) : JsMathFloor(number);
    modulo = JsMathFmod(truncated, 4294967296.0);
    if (modulo < 0) {
        modulo += 4294967296.0;
    }
    if (modulo >= 2147483648.0) {
        modulo -= 4294967296.0;
    }
    return (int32_t)modulo;
}

/* ------------------------------------------------------------------ */
/* Conversion                                                          */
/* ------------------------------------------------------------------ */

int JsValueToBoolean(const JsValue *value)
{
    if (value == NULL) {
        return 0;
    }
    switch (value->type) {
    case RoboJsTypeUndefined:
    case RoboJsTypeNull:
        return 0;
    case RoboJsTypeBoolean:
        return value->number != 0.0 ? 1 : 0;
    case RoboJsTypeNumber:
        /* 0, -0 and NaN are false; nothing else is. */
        return (value->number != 0.0 && value->number == value->number) ? 1 : 0;
    case RoboJsTypeString:
        return value->stringLength > 0 ? 1 : 0;
    case RoboJsTypeObject:
        return 1;
    default:
        return 0;
    }
}

double JsStringToNumber(const char *text, int32_t length)
{
    size_t i;
    size_t end;
    int negative = 0;
    double value = 0.0;
    double fraction = 0.0;
    double divisor = 1.0;
    int digits = 0;
    int seenDot = 0;
    int seenDigit = 0;

    if (text == NULL) {
        return JsMathNan();
    }
    end = length >= 0 ? (size_t)length : strlen(text);
    /* Both ends: `Number(" 7 ")` is 7, and trimming only the front would answer NaN for a string a
     * caller has every reason to expect parses. */
    while (end > 0 && (text[end - 1] == ' ' || text[end - 1] == '\t' || text[end - 1] == '\n' ||
                       text[end - 1] == '\r')) {
        end -= 1;
    }
    i = 0;
    while (i < end && (text[i] == ' ' || text[i] == '\t' || text[i] == '\n' || text[i] == '\r')) {
        i += 1;
    }
    if (i >= end) {
        /* The empty string is 0 in JavaScript, and a string of spaces is empty after trimming. */
        return 0.0;
    }
    if (i < end && (text[i] == '+' || text[i] == '-')) {
        negative = text[i] == '-';
        i += 1;
    }
    if (i < end && text[i] == 'I') {
        if (end - i >= 8 && strncmp(text + i, "Infinity", 8) == 0 && i + 8 == end) {
            return negative ? -JsMathInfinity() : JsMathInfinity();
        }
        return JsMathNan();
    }
    while (i < end) {
        char c = text[i];

        if (c >= '0' && c <= '9') {
            if (seenDot) {
                divisor *= 10.0;
                fraction += (double)(c - '0') / divisor;
            } else {
                value = value * 10.0 + (double)(c - '0');
            }
            seenDigit = 1;
            digits += 1;
        } else if (c == '.' && !seenDot) {
            seenDot = 1;
        } else {
            /* Trailing text after digits: a suffix, an exponent, or a typo. This build refuses the
             * whole string rather than guessing a prefix, which is the answer that cannot be
             * surprised by `"12px"`. */
            return JsMathNan();
        }
        i += 1;
    }
    if (!seenDigit) {
        return JsMathNan();
    }
    (void)digits;
    value += fraction;
    return negative ? -value : value;
}

double JsValueToNumber(const JsValue *value)
{
    if (value == NULL) {
        return JsMathNan();
    }
    switch (value->type) {
    case RoboJsTypeUndefined:
        return JsMathNan();
    case RoboJsTypeNull:
        return 0.0;
    case RoboJsTypeBoolean:
        return value->number != 0.0 ? 1.0 : 0.0;
    case RoboJsTypeNumber:
        return value->number;
    case RoboJsTypeString:
        return JsStringToNumber(value->string, value->stringLength);
    case RoboJsTypeObject:
        /* ToPrimitive without a `valueOf` hook: this build's objects stringify, and the string then
         * converts. That is the rule for a plain object (`Number({})` is NaN) and the reason an
         * array of numbers is not accidentally numeric. */
        if (value->object != NULL && value->object->kind == (uint32_t)RoboJsObjectArray) {
            char buffer[64];

            if (value->object->length == 0) {
                return 0.0;
            }
            if (value->object->length == 1) {
                JsValueToString(&value->object->elements[0], buffer, sizeof(buffer));
                return JsStringToNumber(buffer, -1);
            }
        }
        return JsMathNan();
    default:
        return JsMathNan();
    }
}

int32_t JsNumberToString(double number, char *buffer, size_t capacity)
{
    char scratch[40];
    int written;
    size_t length;
    size_t i;

    if (number != number) {
        written = snprintf(scratch, sizeof(scratch), "NaN");
    } else if (number == JsMathInfinity()) {
        written = snprintf(scratch, sizeof(scratch), "Infinity");
    } else if (number == -JsMathInfinity()) {
        written = snprintf(scratch, sizeof(scratch), "-Infinity");
    } else if (number == 0.0) {
        /* Covers `-0` too: JavaScript's `String(-0)` is `"0"`, which is the one place the language
         * deliberately loses the sign. */
        written = snprintf(scratch, sizeof(scratch), "0");
    } else if (number == JsMathFloor(number) && JsMathAbs(number) < 1.0e15) {
        written = snprintf(scratch, sizeof(scratch), "%.0f", number);
    } else {
        written = snprintf(scratch, sizeof(scratch), "%.17g", number);
        /* Trim the trailing zeros of the round-trip form so `0.5` prints as `0.5` rather than
         * `0.50000000000000000`. */
        if (written > 0) {
            size_t len = (size_t)written;
            size_t dot = 0;
            int hasDot = 0;
            int hasExp = 0;

            for (i = 0; i < len; ++i) {
                if (scratch[i] == '.') {
                    hasDot = 1;
                    dot = i;
                } else if (scratch[i] == 'e' || scratch[i] == 'E') {
                    hasExp = 1;
                    break;
                }
            }
            if (hasDot && !hasExp) {
                while (len > dot + 1 && scratch[len - 1] == '0') {
                    len -= 1;
                }
                if (len == dot + 1) {
                    len = dot;
                }
                scratch[len] = '\0';
                written = (int)len;
            }
        }
    }
    length = written > 0 ? (size_t)written : 0u;
    if (buffer != NULL && capacity > 0) {
        size_t copy = length < capacity - 1u ? length : capacity - 1u;

        if (copy > strlen(scratch)) {
            copy = strlen(scratch);
        }
        memcpy(buffer, scratch, copy);
        buffer[copy] = '\0';
    }
    return (int32_t)length;
}

int32_t JsValueToString(const JsValue *value, char *buffer, size_t capacity)
{
    const char *text = NULL;
    size_t length;

    if (buffer != NULL && capacity > 0) {
        buffer[0] = '\0';
    }
    if (value == NULL) {
        return 0;
    }
    switch (value->type) {
    case RoboJsTypeUndefined:
        text = "undefined";
        break;
    case RoboJsTypeNull:
        text = "null";
        break;
    case RoboJsTypeBoolean:
        text = value->number != 0.0 ? "true" : "false";
        break;
    case RoboJsTypeNumber:
        return JsNumberToString(value->number, buffer, capacity);
    case RoboJsTypeString:
        text = value->string != NULL ? value->string : "";
        break;
    case RoboJsTypeObject:
        if (value->object != NULL) {
            if (value->object->kind == (uint32_t)RoboJsObjectArray) {
                /* `[1,2]` and `[,]`-shaped joining, which is what makes `"a" + [1,2]` testable. */
                int32_t total = 0;
                int32_t i;
                int first = 1;

                if (value->object->length == 0) {
                    text = "";
                    break;
                }
                for (i = 0; i < value->object->length; ++i) {
                    char item[64];
                    int32_t itemLength;
                    size_t offset = (size_t)total;

                    if (!first) {
                        if (buffer != NULL && offset + 1u < capacity) {
                            buffer[offset] = ',';
                            buffer[offset + 1u] = '\0';
                        }
                        total += 1;
                    }
                    itemLength = JsValueToString(&value->object->elements[i], item, sizeof(item));
                    if (buffer != NULL && (size_t)total < capacity) {
                        size_t room = capacity - (size_t)total - 1u;
                        size_t copy = (size_t)itemLength < room ? (size_t)itemLength : room;

                        memcpy(buffer + total, item, copy);
                        buffer[total + (int32_t)copy] = '\0';
                    }
                    total += itemLength;
                    first = 0;
                }
                return total;
            }
            if (value->object->kind == (uint32_t)RoboJsObjectError) {
                /* An error stringifies to `"Name: message"`, which is what a diagnostic wants. */
                char scratch[320];
                int written = snprintf(scratch,
                                       sizeof(scratch),
                                       "%s%s%s",
                                       JsErrorKindName((JsErrorKind)value->object->errorKind),
                                       value->object->errorMessage != NULL ? ": " : "",
                                       value->object->errorMessage != NULL
                                           ? value->object->errorMessage
                                           : "");
                text = scratch;
                if (written < 0) {
                    text = "Error";
                }
                /* `scratch` dies with this scope, so copy it into the caller's buffer here. */
                length = strlen(text);
                if (buffer != NULL && capacity > 0) {
                    size_t copy = length < capacity - 1u ? length : capacity - 1u;

                    memcpy(buffer, text, copy);
                    buffer[copy] = '\0';
                }
                return (int32_t)length;
            }
            if (value->object->kind == (uint32_t)RoboJsObjectFunction) {
                char scratch[64];
                int written = snprintf(scratch,
                                       sizeof(scratch),
                                       "function %s() { [native or script code] }",
                                       value->object->fn.name != NULL ? value->object->fn.name : "");
                text = written > 0 ? scratch : "function () {}";
                length = strlen(text);
                if (buffer != NULL && capacity > 0) {
                    size_t copy = length < capacity - 1u ? length : capacity - 1u;

                    memcpy(buffer, text, copy);
                    buffer[copy] = '\0';
                }
                return (int32_t)length;
            }
        }
        text = "[object Object]";
        break;
    default:
        text = "undefined";
        break;
    }
    length = strlen(text);
    if (buffer != NULL && capacity > 0) {
        size_t copy = length < capacity - 1u ? length : capacity - 1u;

        memcpy(buffer, text, copy);
        buffer[copy] = '\0';
    }
    return (int32_t)length;
}

const char *JsValueTypeOfString(const JsValue *value)
{
    if (value == NULL) {
        return "undefined";
    }
    if (value->type == RoboJsTypeObject) {
        return JsValueIsFunction(value) ? "function" : "object";
    }
    return JsValueTypeName((JsValueType)value->type);
}

/* ------------------------------------------------------------------ */
/* Equality                                                            */
/* ------------------------------------------------------------------ */

int JsValueStrictEquals(const JsValue *a, const JsValue *b)
{
    if (a == NULL || b == NULL) {
        return a == b;
    }
    if (a->type != b->type) {
        return 0;
    }
    switch (a->type) {
    case RoboJsTypeUndefined:
    case RoboJsTypeNull:
        return 1;
    case RoboJsTypeBoolean:
    case RoboJsTypeNumber:
        /* NaN !== NaN is the one comparison in the language that is not reflexive. */
        return a->number == b->number;
    case RoboJsTypeString:
        if (a->stringLength != b->stringLength) {
            return 0;
        }
        if (a->string == NULL || b->string == NULL) {
            return a->string == b->string;
        }
        return memcmp(a->string, b->string, (size_t)a->stringLength) == 0;
    case RoboJsTypeObject:
        return a->object == b->object;
    default:
        return 0;
    }
}

int JsValueLooseEquals(const JsValue *a, const JsValue *b)
{
    if (a == NULL || b == NULL) {
        return a == b;
    }
    if (a->type == b->type) {
        return JsValueStrictEquals(a, b);
    }
    if (JsValueIsNullOrUndefined(a) && JsValueIsNullOrUndefined(b)) {
        return 1;
    }
    if (JsValueIsNullOrUndefined(a) || JsValueIsNullOrUndefined(b)) {
        return 0;
    }
    if (JsValueIsBoolean(a)) {
        JsValue number = JsValueNumber(JsValueToNumber(a));

        return JsValueLooseEquals(&number, b);
    }
    if (JsValueIsBoolean(b)) {
        JsValue number = JsValueNumber(JsValueToNumber(b));

        return JsValueLooseEquals(a, &number);
    }
    if ((JsValueIsNumber(a) && JsValueIsString(b)) ||
        (JsValueIsString(a) && JsValueIsNumber(b))) {
        return JsValueToNumber(a) == JsValueToNumber(b);
    }
    if (JsValueIsObject(a) && !JsValueIsObject(b)) {
        /* An object against a primitive compares through the primitive's own conversion: a string
         * stays a string, a number coerces. No `valueOf` hook exists in this build, so an object
         * against a number is NaN and therefore false, which is the honest answer. */
        if (JsValueIsString(b)) {
            char buffer[256];
            int32_t length;

            if (JsValueIsArray(a)) {
                return 0;
            }
            length = JsValueToString(a, buffer, sizeof(buffer));
            if (length >= (int32_t)sizeof(buffer)) {
                return 0;
            }
            return strcmp(buffer, b->string) == 0;
        }
        return JsValueToNumber(a) == JsValueToNumber(b);
    }
    if (JsValueIsObject(b) && !JsValueIsObject(a)) {
        return JsValueLooseEquals(b, a);
    }
    return 0;
}

int JsValuePlusConcatenates(const JsValue *a, const JsValue *b)
{
    if (a == NULL || b == NULL) {
        return 0;
    }
    if (JsValueIsString(a) || JsValueIsString(b)) {
        return 1;
    }
    /* An object operand concatenates too, because `+` calls ToPrimitive on it and this build's
     * ToPrimitive answers a string. */
    if (JsValueIsObject(a) || JsValueIsObject(b)) {
        return 1;
    }
    return 0;
}
