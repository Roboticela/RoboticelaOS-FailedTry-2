/*
 * RoboHTML forms foundation (Phase 8.14).
 *
 * Four layers, each with one job:
 *
 *   vocabulary   method, enctype, input type, button type — closed sets the
 *                rest of the module reasons with, so no caller compares
 *                strings and an unrecognised value has one defined answer.
 *   attributes   read the form-relevant attributes off an element and parse
 *                the numeric ones. No validity checking, no comparison.
 *   relations    resolve a label to its control and a control to its form
 *                owner by walking the live tree, on every call, so nothing
 *                can go stale.
 *   state        HtmlControlState — the explicit overrides that make
 *                "current value" different from "declared value".
 *
 * Nothing here allocates per read, and nothing here caches a relationship.
 * The only heap this module owns is the value copy of an explicit override,
 * and that lives in the document slot's control table, freed by
 * HtmlDocumentControlsFree on both teardown paths.
 */

#include "RoboHTML/RoboHTMLForm.h"

#include "RoboBase/RoboLog.h"
#include "RoboHTML/RoboHTMLElement.h"
#include "RoboHTML/RoboHTMLTag.h"
#include "RoboHTML/RoboHTMLTraversal.h"
#include "RoboMem/RoboMem.h"
#include "RoboHTMLInternal.h"

#include <string.h>

/* ------------------------------------------------------------------ */
/* Small helpers                                                       */
/* ------------------------------------------------------------------ */

/**
 * ASCII whitespace as HTML defines it in an attribute value.
 * Deliberately not isspace(): the C locale's extras are not HTML whitespace,
 * and a classification rule must not depend on the host locale.
 */
static int AsciiIsSpace(char c)
{
    return c == ' ' || c == '\t' || c == '\n' || c == '\f' || c == '\r';
}

/**
 * Non-zero when the @p length bytes at @p value name @p name, ignoring
 * surrounding ASCII whitespace and ASCII case.
 *
 * Attribute values are stored exactly as the source wrote them — the model
 * lowercases attribute *names*, never values — so every classification here
 * has to tolerate case and padding rather than assume either.
 */
static int SpanNames(const char *value, size_t length, const char *name)
{
    size_t begin = 0u;
    size_t end = length;

    if (!value || !name) {
        return 0;
    }
    while (begin < end && AsciiIsSpace(value[begin])) {
        begin++;
    }
    while (end > begin && AsciiIsSpace(value[end - 1u])) {
        end--;
    }
    return HtmlNameEqualSpan(value + begin, end - begin, name);
}

/**
 * Parse a strict decimal number: sign, digits, and an optional fractional
 * part. @p out is written only when the whole value is accepted.
 *
 * The grammar is deliberately narrower than C's strtod and wider than an
 * integer, and each restriction is for a reason:
 *
 *   - No locale. A decimal point is '.' and nothing else, so the same bytes
 *     parse the same way on every host and in the guest. strtod would accept
 *     a locale's own separator, which would make a document's meaning depend
 *     on where it is read.
 *   - No exponent and no hex. `min="1e3"` is refused rather than silently
 *     read as 1000, because a value this model cannot read exactly is better
 *     reported as unreadable than approximated.
 *   - Fifteen significant digits at most. That bounds the magnitude below
 *     10^15, so the accumulation below cannot overflow to infinity, and it is
 *     one digit short of what a double can hold exactly — so a number that is
 *     accepted converts exactly rather than being silently rounded.
 *
 * No libm, no errno, no allocation: the exponent-free form is a loop.
 */
static int ParseDecimal(const char *text, size_t length, double *out)
{
    size_t begin = 0u;
    size_t end = length;
    size_t i;
    size_t digits = 0u;
    size_t fractionDigits = 0u;
    int negative = 0;
    int afterPoint = 0;
    int sawDigit = 0; /* any digit at all — a lone sign is not a number */
    int started = 0;   /* seen a non-zero digit yet (leading zeros do not count) */
    double whole = 0.0;
    double fraction = 0.0;
    double divisor = 1.0;

    if (!text || !out) {
        return 0;
    }
    while (begin < end && AsciiIsSpace(text[begin])) {
        begin++;
    }
    while (end > begin && AsciiIsSpace(text[end - 1u])) {
        end--;
    }
    if (begin >= end) {
        return 0;
    }

    i = begin;
    if (text[i] == '+' || text[i] == '-') {
        negative = (text[i] == '-');
        i++;
    }
    for (; i < end; i++) {
        char c = text[i];

        if (c == '.') {
            if (afterPoint) {
                return 0; /* a second point: not a number */
            }
            afterPoint = 1;
            continue;
        }
        if (c < '0' || c > '9') {
            return 0; /* anything else — including 'e', 'x', a space inside */
        }
        sawDigit = 1;
        if (c != '0') {
            started = 1;
        }
        if (started) {
            digits++;
            if (digits > 15u) {
                return 0;
            }
        }
        if (afterPoint) {
            fraction = fraction * 10.0 + (double)(c - '0');
            divisor *= 10.0;
            fractionDigits++;
        } else {
            whole = whole * 10.0 + (double)(c - '0');
        }
    }
    /*
     * Two rejections, and the first one is why `sawDigit` exists rather than
     * reusing `started`: zero has no non-zero digit, so a "did a non-zero digit
     * appear?" test would refuse `min="0"`. "Any digit appeared" is the rule
     * that rejects a lone sign while accepting `0`, `00` and `0.5`.
     */
    if (!sawDigit) {
        return 0; /* a lone sign, or an empty value */
    }
    if (afterPoint && fractionDigits == 0u) {
        return 0; /* "1." / "." — a point must be followed by a digit */
    }

    whole += fraction / divisor;
    *out = negative ? -whole : whole;
    return 1;
}

/**
 * Parse a non-negative integer of at most @p cap: digits only, no sign, no
 * fraction, no padding beyond whitespace. Used for `rows` and `cols`, where
 * the HTML rule is exactly this.
 */
static int ParseUnsigned(const char *text, size_t length, unsigned long cap,
                          unsigned long *out)
{
    size_t begin = 0u;
    size_t end = length;
    size_t i;
    unsigned long value = 0u;
    int started = 0;

    if (!text || !out) {
        return 0;
    }
    while (begin < end && AsciiIsSpace(text[begin])) {
        begin++;
    }
    while (end > begin && AsciiIsSpace(text[end - 1u])) {
        end--;
    }
    if (begin >= end) {
        return 0;
    }
    for (i = begin; i < end; i++) {
        char c = text[i];

        if (c < '0' || c > '9') {
            return 0;
        }
        if (c != '0' || started) {
            started = 1;
            value = value * 10u + (unsigned long)(c - '0');
            if (value > cap) {
                return 0;
            }
        }
    }
    *out = value;
    return 1;
}

/**
 * Copy @p length bytes of @p text into @p out, NUL-terminating and truncating
 * to @p outCap - 1. @p outLength receives the untruncated length, so a
 * caller can tell truncation from a short value — the same contract
 * HtmlNodeSubtreeText uses, so the two are interchangeable to a caller.
 */
static Result CopySpan(char *out, size_t outCap, const char *text,
                               size_t length, size_t *outLength)
{
    size_t copy;

    if (!out || outCap == 0u) {
        return RoboInvalidArgument;
    }
    copy = length;
    if (copy > outCap - 1u) {
        copy = outCap - 1u;
    }
    if (copy > 0u && text) {
        memcpy(out, text, copy);
    }
    out[copy] = '\0';
    if (outLength) {
        *outLength = length;
    }
    return RoboOk;
}

/* ------------------------------------------------------------------ */
/* <form> vocabulary                                                   */
/* ------------------------------------------------------------------ */

int HtmlFormMethodIsKnown(HtmlFormMethod method)
{
    return method >= RoboHtmlFormMethodNone &&
           method <= RoboHtmlFormMethodUnknown;
}

const char *HtmlFormMethodName(HtmlFormMethod method)
{
    switch (method) {
    case RoboHtmlFormMethodNone:
        return "none";
    case RoboHtmlFormMethodGet:
        return "get";
    case RoboHtmlFormMethodPost:
        return "post";
    case RoboHtmlFormMethodDialog:
        return "dialog";
    case RoboHtmlFormMethodUnknown:
        return "unknown";
    default:
        return "unknown";
    }
}

HtmlFormMethod HtmlFormMethodFromName(const char *value,
                                                        size_t length)
{
    if (!HtmlSpanIsUsable(value, length)) {
        return RoboHtmlFormMethodNone;
    }
    if (SpanNames(value, length, "get")) {
        return RoboHtmlFormMethodGet;
    }
    if (SpanNames(value, length, "post")) {
        return RoboHtmlFormMethodPost;
    }
    if (SpanNames(value, length, "dialog")) {
        return RoboHtmlFormMethodDialog;
    }
    return RoboHtmlFormMethodUnknown;
}

int HtmlFormEnctypeIsKnown(HtmlFormEnctype enctype)
{
    return enctype >= RoboHtmlFormEnctypeNone &&
           enctype <= RoboHtmlFormEnctypeUnknown;
}

const char *HtmlFormEnctypeName(HtmlFormEnctype enctype)
{
    switch (enctype) {
    case RoboHtmlFormEnctypeNone:
        return "none";
    case RoboHtmlFormEnctypeUrlencoded:
        return "application/x-www-form-urlencoded";
    case RoboHtmlFormEnctypeMultipart:
        return "multipart/form-data";
    case RoboHtmlFormEnctypeTextPlain:
        return "text/plain";
    case RoboHtmlFormEnctypeUnknown:
        return "unknown";
    default:
        return "unknown";
    }
}

HtmlFormEnctype HtmlFormEnctypeFromName(const char *value,
                                                          size_t length)
{
    if (!HtmlSpanIsUsable(value, length)) {
        return RoboHtmlFormEnctypeNone;
    }
    if (SpanNames(value, length, "application/x-www-form-urlencoded")) {
        return RoboHtmlFormEnctypeUrlencoded;
    }
    if (SpanNames(value, length, "multipart/form-data")) {
        return RoboHtmlFormEnctypeMultipart;
    }
    if (SpanNames(value, length, "text/plain")) {
        return RoboHtmlFormEnctypeTextPlain;
    }
    return RoboHtmlFormEnctypeUnknown;
}

/* ------------------------------------------------------------------ */
/* <input> / <button> vocabulary                                       */
/* ------------------------------------------------------------------ */

int HtmlInputTypeIsKnown(HtmlInputType type)
{
    return type >= RoboHtmlInputTypeText &&
           type <= RoboHtmlInputTypeUnknown;
}

const char *HtmlInputTypeName(HtmlInputType type)
{
    switch (type) {
    case RoboHtmlInputTypeText:
        return "text";
    case RoboHtmlInputTypePassword:
        return "password";
    case RoboHtmlInputTypeCheckbox:
        return "checkbox";
    case RoboHtmlInputTypeRadio:
        return "radio";
    case RoboHtmlInputTypeNumber:
        return "number";
    case RoboHtmlInputTypeEmail:
        return "email";
    case RoboHtmlInputTypeSubmit:
        return "submit";
    case RoboHtmlInputTypeButton:
        return "button";
    case RoboHtmlInputTypeReset:
        return "reset";
    case RoboHtmlInputTypeSearch:
        return "search";
    case RoboHtmlInputTypeHidden:
        return "hidden";
    case RoboHtmlInputTypeUnknown:
        return "unknown";
    default:
        return "unknown";
    }
}

HtmlInputType HtmlInputTypeEffective(
    HtmlInputType declared)
{
    /*
     * This one line is the whole "degrade safely" requirement: an unrecognised
     * type behaves as text, so the element keeps every other fact it has and a
     * document is never damaged by a type this runtime has not met.
     */
    return (declared == RoboHtmlInputTypeUnknown ||
            !HtmlInputTypeIsKnown(declared))
               ? RoboHtmlInputTypeText
               : declared;
}

HtmlInputType HtmlInputTypeFromName(const char *value,
                                                      size_t length)
{
    if (!HtmlSpanIsUsable(value, length)) {
        return RoboHtmlInputTypeText;
    }
    if (SpanNames(value, length, "text")) {
        return RoboHtmlInputTypeText;
    }
    if (SpanNames(value, length, "password")) {
        return RoboHtmlInputTypePassword;
    }
    if (SpanNames(value, length, "checkbox")) {
        return RoboHtmlInputTypeCheckbox;
    }
    if (SpanNames(value, length, "radio")) {
        return RoboHtmlInputTypeRadio;
    }
    if (SpanNames(value, length, "number")) {
        return RoboHtmlInputTypeNumber;
    }
    if (SpanNames(value, length, "email")) {
        return RoboHtmlInputTypeEmail;
    }
    if (SpanNames(value, length, "submit")) {
        return RoboHtmlInputTypeSubmit;
    }
    if (SpanNames(value, length, "button")) {
        return RoboHtmlInputTypeButton;
    }
    if (SpanNames(value, length, "reset")) {
        return RoboHtmlInputTypeReset;
    }
    if (SpanNames(value, length, "search")) {
        return RoboHtmlInputTypeSearch;
    }
    if (SpanNames(value, length, "hidden")) {
        return RoboHtmlInputTypeHidden;
    }
    return RoboHtmlInputTypeUnknown;
}

int HtmlInputTypeIsCheckable(HtmlInputType type)
{
    return type == RoboHtmlInputTypeCheckbox ||
           type == RoboHtmlInputTypeRadio;
}

int HtmlButtonTypeIsKnown(HtmlButtonType type)
{
    return type >= RoboHtmlButtonTypeSubmit &&
           type <= RoboHtmlButtonTypeUnknown;
}

const char *HtmlButtonTypeName(HtmlButtonType type)
{
    switch (type) {
    case RoboHtmlButtonTypeSubmit:
        return "submit";
    case RoboHtmlButtonTypeButton:
        return "button";
    case RoboHtmlButtonTypeReset:
        return "reset";
    case RoboHtmlButtonTypeUnknown:
        return "unknown";
    default:
        return "unknown";
    }
}

HtmlButtonType HtmlButtonTypeEffective(
    HtmlButtonType declared)
{
    return (declared == RoboHtmlButtonTypeUnknown ||
            !HtmlButtonTypeIsKnown(declared))
               ? RoboHtmlButtonTypeSubmit
               : declared;
}

HtmlButtonType HtmlButtonTypeFromName(const char *value,
                                                        size_t length)
{
    if (!HtmlSpanIsUsable(value, length)) {
        return RoboHtmlButtonTypeSubmit;
    }
    if (SpanNames(value, length, "submit")) {
        return RoboHtmlButtonTypeSubmit;
    }
    if (SpanNames(value, length, "button")) {
        return RoboHtmlButtonTypeButton;
    }
    if (SpanNames(value, length, "reset")) {
        return RoboHtmlButtonTypeReset;
    }
    return RoboHtmlButtonTypeUnknown;
}

int HtmlFormOwnerSourceIsKnown(HtmlFormOwnerSource source)
{
    return source >= RoboHtmlFormOwnerSourceNone &&
           source <= RoboHtmlFormOwnerSourceAncestor;
}

const char *HtmlFormOwnerSourceName(HtmlFormOwnerSource source)
{
    switch (source) {
    case RoboHtmlFormOwnerSourceNone:
        return "none";
    case RoboHtmlFormOwnerSourceAttribute:
        return "attribute";
    case RoboHtmlFormOwnerSourceAncestor:
        return "ancestor";
    default:
        return "unknown";
    }
}

/* ------------------------------------------------------------------ */
/* Element-side gates                                                  */
/* ------------------------------------------------------------------ */

/**
 * Shared "is this a live element, and is it one of @p tag" gate.
 *
 * Two questions in a fixed order, because their answers differ: "is this still
 * an element?" (a stale handle or a destroyed document is NOT_FOUND) and "is
 * it the element I asked about?" (a live `<p>` is NOT_SUPPORTED). Collapsing
 * them would report a dead node as "not a form", which is a misleading thing
 * to say about a node that no longer exists.
 */
static Result TagGate(const HtmlNodeHandle *element,
                              HtmlTag tag, const char *what,
                              HtmlDiag *diag)
{
    if (!element) {
        return RoboHtmlDiag(diag, RoboHtmlDomainForms,
                              RoboInvalidArgument, "NULL element");
    }
    if (!HtmlNodeMatchesFilter(element, RoboHtmlNodeFilterElements)) {
        return RoboHtmlDiag(diag, RoboHtmlDomainForms, RoboNotFound,
                              "element is not a live element");
    }
    if (!HtmlElementIsTag(element, tag)) {
        return RoboHtmlDiag(diag, RoboHtmlDomainForms, RoboNotSupported,
                              what);
    }
    return RoboOk;
}

/**
 * Elements this phase models as *holding a value*.
 *
 * Narrower than HtmlTagIsFormAssociated on purpose: a `<fieldset>`
 * has a form owner but no value, so it answers the ownership question and
 * refuses the value question. A `<select>` has a value; the option it reports
 * as selected comes from the option elements, not from an override.
 */
static int TagHoldsValue(HtmlTag tag)
{
    return tag == RoboHtmlTagInput || tag == RoboHtmlTagTextarea ||
           tag == RoboHtmlTagSelect || tag == RoboHtmlTagButton ||
           tag == RoboHtmlTagOutput;
}

/* ------------------------------------------------------------------ */
/* <form> attributes                                                   */
/* ------------------------------------------------------------------ */

Result HtmlElementFormAttributes(
    const HtmlNodeHandle *element, HtmlFormAttributes *out,
    HtmlDiag *diag)
{
    Result res;
    size_t length = 0u;
    const char *value;

    if (!out) {
        return RoboHtmlDiag(diag, RoboHtmlDomainForms,
                              RoboInvalidArgument, "NULL out");
    }
    memset(out, 0, sizeof(*out));
    res = TagGate(element, RoboHtmlTagForm, "element is not a form", diag);
    if (res != RoboOk) {
        return res;
    }

#define TAKE_ATTR(field, name)                                                \
    do {                                                                      \
        value = HtmlElementAttributeValue(element, name, &length);    \
        out->field##Present = (value != NULL);                               \
        out->field = value;                                                   \
        out->field##Length = value ? length : 0u;                            \
    } while (0)

    TAKE_ATTR(action, "action");
    TAKE_ATTR(method, "method");
    TAKE_ATTR(enctype, "enctype");
    TAKE_ATTR(name, "name");
    TAKE_ATTR(target, "target");
#undef TAKE_ATTR

    /* "usable" is the same rule everywhere it is asked: present, and not
     * blank. `<form action="  ">` names nothing, exactly as `<a href="  ">`
     * does — one rule, two readers, one answer. */
    out->actionUsable = HtmlSpanIsUsable(out->action,
                                                  out->actionLength);
    out->methodKind = HtmlFormMethodFromName(out->method,
                                                       out->methodLength);
    out->enctypeKind = HtmlFormEnctypeFromName(out->enctype,
                                                         out->enctypeLength);
    return RoboOk;
}

/* ------------------------------------------------------------------ */
/* <input> attributes                                                  */
/* ------------------------------------------------------------------ */

Result HtmlElementInputAttributes(
    const HtmlNodeHandle *element, HtmlInputAttributes *out,
    HtmlDiag *diag)
{
    Result res;
    size_t length = 0u;
    const char *value;

    if (!out) {
        return RoboHtmlDiag(diag, RoboHtmlDomainForms,
                              RoboInvalidArgument, "NULL out");
    }
    memset(out, 0, sizeof(*out));
    res = TagGate(element, RoboHtmlTagInput, "element is not an input",
                   diag);
    if (res != RoboOk) {
        return res;
    }

#define TAKE_ATTR(field, name)                                                \
    do {                                                                      \
        value = HtmlElementAttributeValue(element, name, &length);    \
        out->field##Present = (value != NULL);                               \
        out->field = value;                                                   \
        out->field##Length = value ? length : 0u;                            \
    } while (0)

    TAKE_ATTR(type, "type");
    TAKE_ATTR(name, "name");
    TAKE_ATTR(value, "value");
    TAKE_ATTR(placeholder, "placeholder");
    TAKE_ATTR(min, "min");
    TAKE_ATTR(max, "max");
    TAKE_ATTR(step, "step");
#undef TAKE_ATTR

    out->typeDeclared = HtmlInputTypeFromName(out->type,
                                                        out->typeLength);
    out->typeEffective = HtmlInputTypeEffective(out->typeDeclared);
    /* An absent or empty `type` is the declared default (text), not a
     * degradation: the author said nothing, and nothing means text. */
    out->typeRecognised = (out->typeDeclared != RoboHtmlInputTypeUnknown);

    /* Boolean attributes are presence tests, so `<input checked>` and
     * `<input checked="checked">` agree — the HTML rule. */
    out->checked = HtmlElementHasAttribute(element, "checked");
    out->disabled = HtmlElementHasAttribute(element, "disabled");
    out->readonly = HtmlElementHasAttribute(element, "readonly");
    out->required = HtmlElementHasAttribute(element, "required");

    /* Parsed, not compared: `<input min="10" max="1">` is reported
     * faithfully rather than flagged, because validation is not this phase. */
    out->minValid = ParseDecimal(out->min, out->minLength, &out->minValue);
    out->maxValid = ParseDecimal(out->max, out->maxLength, &out->maxValue);
    out->stepValid = ParseDecimal(out->step, out->stepLength,
                                    &out->stepValue);
    return RoboOk;
}

/* ------------------------------------------------------------------ */
/* <button> attributes                                                 */
/* ------------------------------------------------------------------ */

Result HtmlElementButtonAttributes(
    const HtmlNodeHandle *element, HtmlButtonAttributes *out,
    HtmlDiag *diag)
{
    Result res;
    size_t length = 0u;
    const char *value;

    if (!out) {
        return RoboHtmlDiag(diag, RoboHtmlDomainForms,
                              RoboInvalidArgument, "NULL out");
    }
    memset(out, 0, sizeof(*out));
    res = TagGate(element, RoboHtmlTagButton, "element is not a button",
                   diag);
    if (res != RoboOk) {
        return res;
    }

#define TAKE_ATTR(field, name)                                                \
    do {                                                                      \
        value = HtmlElementAttributeValue(element, name, &length);    \
        out->field##Present = (value != NULL);                               \
        out->field = value;                                                   \
        out->field##Length = value ? length : 0u;                            \
    } while (0)

    TAKE_ATTR(type, "type");
    TAKE_ATTR(name, "name");
    TAKE_ATTR(value, "value");
#undef TAKE_ATTR

    out->typeDeclared = HtmlButtonTypeFromName(out->type,
                                                         out->typeLength);
    out->typeEffective = HtmlButtonTypeEffective(out->typeDeclared);
    out->typeRecognised =
        (out->typeDeclared != RoboHtmlButtonTypeUnknown);
    out->disabled = HtmlElementHasAttribute(element, "disabled");
    return RoboOk;
}

/* ------------------------------------------------------------------ */
/* <textarea> attributes                                               */
/* ------------------------------------------------------------------ */

Result HtmlElementTextareaAttributes(
    const HtmlNodeHandle *element,
    HtmlTextareaAttributes *out, HtmlDiag *diag)
{
    Result res;
    size_t length = 0u;
    const char *value;

    if (!out) {
        return RoboHtmlDiag(diag, RoboHtmlDomainForms,
                              RoboInvalidArgument, "NULL out");
    }
    memset(out, 0, sizeof(*out));
    res = TagGate(element, RoboHtmlTagTextarea,
                   "element is not a textarea", diag);
    if (res != RoboOk) {
        return res;
    }

#define TAKE_ATTR(field, name)                                                \
    do {                                                                      \
        value = HtmlElementAttributeValue(element, name, &length);    \
        out->field##Present = (value != NULL);                               \
        out->field = value;                                                   \
        out->field##Length = value ? length : 0u;                            \
    } while (0)

    TAKE_ATTR(name, "name");
    TAKE_ATTR(rows, "rows");
    TAKE_ATTR(cols, "cols");
    TAKE_ATTR(placeholder, "placeholder");
#undef TAKE_ATTR

    out->rowsValid = ParseUnsigned(out->rows, out->rowsLength,
                                     RoboHtmlFormMaxDeclaredLines,
                                     &out->rowsValue);
    out->colsValid = ParseUnsigned(out->cols, out->colsLength,
                                     RoboHtmlFormMaxDeclaredLines,
                                     &out->colsValue);
    out->disabled = HtmlElementHasAttribute(element, "disabled");
    out->readonly = HtmlElementHasAttribute(element, "readonly");
    out->required = HtmlElementHasAttribute(element, "required");
    return RoboOk;
}

/* ------------------------------------------------------------------ */
/* <select> / <option> attributes                                      */
/* ------------------------------------------------------------------ */

Result HtmlElementSelectAttributes(
    const HtmlNodeHandle *element, HtmlSelectAttributes *out,
    HtmlDiag *diag)
{
    Result res;
    size_t length = 0u;
    const char *value;

    if (!out) {
        return RoboHtmlDiag(diag, RoboHtmlDomainForms,
                              RoboInvalidArgument, "NULL out");
    }
    memset(out, 0, sizeof(*out));
    res = TagGate(element, RoboHtmlTagSelect, "element is not a select",
                   diag);
    if (res != RoboOk) {
        return res;
    }
    value = HtmlElementAttributeValue(element, "name", &length);
    out->namePresent = (value != NULL);
    out->name = value;
    out->nameLength = value ? length : 0u;
    out->multiple = HtmlElementHasAttribute(element, "multiple");
    out->disabled = HtmlElementHasAttribute(element, "disabled");
    out->required = HtmlElementHasAttribute(element, "required");
    return RoboOk;
}

Result HtmlElementOptionAttributes(
    const HtmlNodeHandle *element, HtmlOptionAttributes *out,
    HtmlDiag *diag)
{
    Result res;
    size_t length = 0u;
    const char *value;

    if (!out) {
        return RoboHtmlDiag(diag, RoboHtmlDomainForms,
                              RoboInvalidArgument, "NULL out");
    }
    memset(out, 0, sizeof(*out));
    res = TagGate(element, RoboHtmlTagOption, "element is not an option",
                   diag);
    if (res != RoboOk) {
        return res;
    }
    value = HtmlElementAttributeValue(element, "value", &length);
    out->valuePresent = (value != NULL);
    out->value = value;
    out->valueLength = value ? length : 0u;

    value = HtmlElementAttributeValue(element, "label", &length);
    out->labelPresent = (value != NULL);
    out->label = value;
    out->labelLength = value ? length : 0u;

    out->selected = HtmlElementHasAttribute(element, "selected");
    out->disabled = HtmlElementHasAttribute(element, "disabled");
    return RoboOk;
}

/* ------------------------------------------------------------------ */
/* Id lookup                                                           */
/* ------------------------------------------------------------------ */

/*
 * "The first element in document order whose id is X".
 *
 * There is no id index in this runtime — 8.8 built one traversal order and
 * this reuses it rather than growing a second structure that would need to be
 * kept in step with every mutation. That choice is what makes 8.14.15's "no
 * stale association" requirement free: there is nothing to keep in step. An
 * index would be faster and would need invalidation hooks on insert, remove,
 * move and attribute change; the walk needs none and is bounded by the
 * document's node count, which is already the ceiling on everything else.
 *
 * Comparison is exact, because an id is a case-sensitive name, not a keyword.
 */

typedef struct idLookupContext {
    const char *id;
    size_t idLength;
} IdLookupContext;

static int IdLookupMatch(const HtmlNodeView *view, void *user)
{
    const IdLookupContext *context = (const IdLookupContext *)user;
    size_t length = 0u;
    const char *value;

    if (!view || view->kind != RoboHtmlNodeKindElement || !context) {
        return 0;
    }
    value = HtmlElementAttributeValue(&view->handle, "id", &length);
    return value && length == context->idLength &&
           (length == 0u || memcmp(value, context->id, length) == 0);
}

/** First element in @p scope's subtree (or @p scope itself) whose id is @p id. */
static Result FindElementById(const HtmlNodeHandle *scope,
                                        const char *id, size_t idLength,
                                        HtmlNodeHandle *out)
{
    IdLookupContext context;

    HtmlNodeHandleClear(out);
    if (!scope || !id || idLength == 0u) {
        return RoboNotFound;
    }
    context.id = id;
    context.idLength = idLength;
    return HtmlNodeFindFirst(scope, RoboHtmlNodeFilterElements,
                                     IdLookupMatch, &context, out, NULL);
}

/**
 * Nearest ancestor of @p element that is a `<form>`, or RoboNotFound.
 *
 * The loop is depth-bounded as well as acyclic-bounded: a valid tree cannot
 * loop, and the guard costs one comparison per level so that a corrupted one
 * cannot hang the runtime.
 */
static Result NearestAncestorForm(
    const HtmlNodeHandle *element, HtmlNodeHandle *out)
{
    HtmlNodeHandle current = *element;
    size_t guard = 0u;

    HtmlNodeHandleClear(out);
    while (guard++ < RoboHtmlMaxNodesPerDocument) {
        HtmlNodeHandle parent;

        if (HtmlElementParent(&current, &parent, NULL) != RoboOk) {
            return RoboNotFound;
        }
        if (HtmlElementIsTag(&parent, RoboHtmlTagForm)) {
            *out = parent;
            return RoboOk;
        }
        current = parent;
    }
    return RoboNotFound;
}

/* ------------------------------------------------------------------ */
/* <label> — the labelled control                                      */
/* ------------------------------------------------------------------ */

static int LabelablePredicate(const HtmlNodeView *view, void *user)
{
    (void)user;
    if (!view || view->kind != RoboHtmlNodeKindElement) {
        return 0;
    }
    return HtmlTagIsLabelable(HtmlElementTag(&view->handle));
}

Result HtmlElementLabelTarget(
    const HtmlNodeHandle *label, HtmlNodeHandle *out,
    int *outFromAttribute, HtmlDiag *diag)
{
    Result res;
    const char *forId;
    size_t forLength = 0u;
    HtmlNodeHandle subject;

    if (!out || !label) {
        return RoboHtmlDiag(diag, RoboHtmlDomainForms,
                              RoboInvalidArgument, "NULL label or out");
    }
    /*
     * Work on a copy, so a caller that passes the same handle as @p label and
     * @p out is well defined rather than silently reading a handle this
     * function just cleared. One copy is cheaper than a rule every caller has
     * to know.
     */
    subject = *label;
    label = &subject;
    HtmlNodeHandleClear(out);
    if (outFromAttribute) {
        *outFromAttribute = 0;
    }
    res = TagGate(label, RoboHtmlTagLabel, "element is not a label", diag);
    if (res != RoboOk) {
        return res;
    }

    forId = HtmlElementAttributeValue(label, "for", &forLength);
    if (HtmlSpanIsUsable(forId, forLength)) {
        HtmlNodeHandle root =
            HtmlDocumentRoot(HtmlNodeHandleDocument(label));

        if (FindElementById(&root, forId, forLength, out) == RoboOk &&
            HtmlTagIsLabelable(HtmlElementTag(out))) {
            if (outFromAttribute) {
                *outFromAttribute = 1;
            }
            return RoboOk;
        }
        /*
         * The author named something, and it does not resolve to a control.
         * Falling through to the descendant search here would silently ignore
         * an id that is wrong, or points at a stranger's element, and report
         * the wrapped control instead — which is exactly the kind of guess
         * that makes a broken document look fine.
         */
        HtmlNodeHandleClear(out);
        return RoboOk;
    }

    res = HtmlNodeFindDescendant(label, RoboHtmlNodeFilterElements,
                                         LabelablePredicate, NULL, out, NULL);
    if (res == RoboNotFound) {
        HtmlNodeHandleClear(out);
        return RoboOk;
    }
    return res;
}

/* ------------------------------------------------------------------ */
/* Control state table                                                 */
/* ------------------------------------------------------------------ */

/** Index of @p node's entry, or CONTROL_INDEX_NONE. */
#define CONTROL_INDEX_NONE ((size_t)-1)

static size_t ControlFindIndex(HtmlDocumentSlot *slot,
                                 const HtmlNodeHandle *node)
{
    size_t i;

    for (i = 0u; i < slot->controlCount; i++) {
        if (HtmlNodeHandleEqual(&slot->controls[i].node, node)) {
            return i;
        }
    }
    return CONTROL_INDEX_NONE;
}

static HtmlControlState *ControlFind(
    HtmlDocumentSlot *slot, const HtmlNodeHandle *node)
{
    size_t index = ControlFindIndex(slot, node);

    return (index == CONTROL_INDEX_NONE) ? NULL : &slot->controls[index];
}

static void ControlEntryRelease(HtmlControlState *entry)
{
    if (entry->value) {
        RoboFree(entry->value);
        entry->value = NULL;
    }
    entry->valueLength = 0u;
    entry->checkedSet = 0;
    entry->checked = 0;
}

/** Release and remove entry @p index, compacting the table. */
static void ControlRemove(HtmlDocumentSlot *slot, size_t index)
{
    ControlEntryRelease(&slot->controls[index]);
    if (index + 1u < slot->controlCount) {
        memmove(&slot->controls[index], &slot->controls[index + 1u],
                (slot->controlCount - index - 1u) * sizeof(slot->controls[0]));
    }
    slot->controlCount--;
}

/**
 * Drop @p index's entry when it holds no override at all.
 *
 * This is what makes "an entry exists only while a control is not tracking its
 * attribute" true rather than merely a comment: a document whose controls are
 * all edited and then reset ends with an empty table, not with a table full of
 * entries that describe nothing. It costs one branch on a path that is already
 * a write, and it keeps the table's size meaningful to anyone looking at it.
 */
static void ControlTidy(HtmlDocumentSlot *slot, size_t index)
{
    if (index != CONTROL_INDEX_NONE && !slot->controls[index].value &&
        !slot->controls[index].checkedSet) {
        ControlRemove(slot, index);
    }
}

/**
 * Entry for @p node, created empty when there is none.
 *
 * An entry exists only while a control is *not* tracking its attribute, so
 * creating one for a control that is subsequently only cleared leaves an empty
 * entry behind. That is harmless — it answers every read exactly as "no entry"
 * would, because both overrides are nullable — and it keeps the write path
 * from having to repeat the lookup it already did. The table is per-document
 * and released wholesale at teardown.
 */
static HtmlControlState *ControlEnsure(
    HtmlDocumentSlot *slot, const HtmlNodeHandle *node,
    HtmlDiag *diag)
{
    HtmlControlState *existing = ControlFind(slot, node);

    if (existing) {
        return existing;
    }
    if (slot->controlCount == slot->controlCapacity) {
        size_t next = slot->controlCapacity ? slot->controlCapacity * 2u : 4u;
        HtmlControlState *grown = (HtmlControlState *)
            Realloc(slot->controls, next * sizeof(slot->controls[0]));

        if (!grown) {
            RoboHtmlDiag(diag, RoboHtmlDomainForms, RoboOutOfMemory,
                           "control state table growth");
            return NULL;
        }
        slot->controls = grown;
        slot->controlCapacity = next;
    }
    memset(&slot->controls[slot->controlCount], 0,
           sizeof(slot->controls[0]));
    slot->controls[slot->controlCount].node = *node;
    slot->controlCount++;
    return &slot->controls[slot->controlCount - 1u];
}

void HtmlDocumentControlsFree(HtmlDocumentSlot *slot)
{
    size_t i;

    if (!slot) {
        return;
    }
    for (i = 0u; i < slot->controlCount; i++) {
        ControlEntryRelease(&slot->controls[i]);
    }
    if (slot->controls) {
        RoboFree(slot->controls);
    }
    slot->controls = NULL;
    slot->controlCount = 0u;
    slot->controlCapacity = 0u;
}

/** Gate shared by the value entry points. */
static Result ValueGate(const HtmlNodeHandle *element,
                                HtmlDiag *diag)
{
    if (!element) {
        return RoboHtmlDiag(diag, RoboHtmlDomainForms,
                              RoboInvalidArgument, "NULL element");
    }
    if (!HtmlNodeMatchesFilter(element, RoboHtmlNodeFilterElements)) {
        return RoboHtmlDiag(diag, RoboHtmlDomainForms, RoboNotFound,
                              "element is not a live element");
    }
    if (!TagHoldsValue(HtmlElementTag(element))) {
        return RoboHtmlDiag(diag, RoboHtmlDomainForms, RoboNotSupported,
                              "element is not a control that holds a value");
    }
    return RoboOk;
}

/**
 * Gate shared by the checkedness entry points.
 *
 * Checkedness is modelled for `<input>` only, and only when the input's
 * *effective* type is checkable. So `<input type="text" checked>` has an
 * attribute that is preserved (see HtmlElementInputAttributes) and no
 * checkedness to report — two honest facts instead of one misleading one.
 *
 * The type is read from the element rather than passed in, so a type change is
 * observed on the next call with no cache and no mutation hook.
 */
static Result CheckedGate(const HtmlNodeHandle *element,
                                  HtmlDiag *diag)
{
    HtmlInputAttributes attributes;
    Result res;

    if (!element) {
        return RoboHtmlDiag(diag, RoboHtmlDomainForms,
                              RoboInvalidArgument, "NULL element");
    }
    if (!HtmlNodeMatchesFilter(element, RoboHtmlNodeFilterElements)) {
        return RoboHtmlDiag(diag, RoboHtmlDomainForms, RoboNotFound,
                              "element is not a live element");
    }
    if (!HtmlElementIsTag(element, RoboHtmlTagInput)) {
        return RoboHtmlDiag(diag, RoboHtmlDomainForms, RoboNotSupported,
                              "element has no checkedness");
    }
    res = HtmlElementInputAttributes(element, &attributes, NULL);
    if (res != RoboOk) {
        return res;
    }
    if (!HtmlInputTypeIsCheckable(attributes.typeEffective)) {
        return RoboHtmlDiag(diag, RoboHtmlDomainForms, RoboNotSupported,
                              "input type has no checkedness");
    }
    return RoboOk;
}

Result HtmlElementControlValue(
    const HtmlNodeHandle *element, char *out, size_t outCap,
    size_t *outLength, int *outIsDeclared, HtmlDiag *diag)
{
    HtmlDocumentSlot *slot;
    HtmlControlState *entry;
    Result res;
    const char *value;
    size_t length = 0u;

    if (!out || outCap == 0u) {
        return RoboHtmlDiag(diag, RoboHtmlDomainForms,
                              RoboInvalidArgument, "NULL or zero-capacity out");
    }
    res = ValueGate(element, diag);
    if (res != RoboOk) {
        return res;
    }
    slot = HtmlDocumentSlotFind(element->documentId);
    if (slot) {
        entry = ControlFind(slot, element);
        if (entry && entry->value) {
            if (outIsDeclared) {
                *outIsDeclared = 0;
            }
            return CopySpan(out, outCap, entry->value, entry->valueLength,
                             outLength);
        }
    }
    if (outIsDeclared) {
        *outIsDeclared = 1;
    }
    /*
     * A textarea has no `value` attribute: its children *are* its default
     * value, which is why the declared read for it is the subtree text (the
     * same call the rest of the runtime uses to read a subtree) and not an
     * attribute lookup that could only ever answer "absent".
     */
    if (HtmlElementIsTag(element, RoboHtmlTagTextarea)) {
        return HtmlNodeSubtreeText(element, out, outCap, outLength,
                                           diag);
    }
    value = HtmlElementAttributeValue(element, "value", &length);
    return CopySpan(out, outCap, value, value ? length : 0u, outLength);
}

/**
 * Tag name for a debug line, never NULL.
 *
 * A log line about a control names the control and how much it holds, and
 * deliberately **not** what it holds: a password field's bytes are user data,
 * and a serial log is the last place they belong. Length is the honest, safe
 * fact.
 */
static const char *ControlLogName(const HtmlNodeHandle *element)
{
    const char *name = HtmlElementName(element, NULL);

    return (name && name[0]) ? name : "?";
}

Result HtmlElementSetControlValue(
    const HtmlNodeHandle *element, const char *value, size_t length,
    HtmlDiag *diag)
{
    HtmlDocumentSlot *slot;
    HtmlControlState *entry;
    char *copy;
    Result res;

    if (value == NULL && length != 0u) {
        return RoboHtmlDiag(diag, RoboHtmlDomainForms,
                              RoboInvalidArgument,
                              "NULL value with non-zero length");
    }
    res = ValueGate(element, diag);
    if (res != RoboOk) {
        return res;
    }
    slot = HtmlDocumentSlotFind(element->documentId);
    if (!slot) {
        return RoboHtmlDiag(diag, RoboHtmlDomainForms, RoboNotFound,
                              "document does not resolve");
    }
    /*
     * Copy even a zero-length value: an empty *override* is a real state that
     * differs from "no override" (the field was cleared, and a reset would
     * bring the declared value back), so it cannot be represented by a NULL
     * pointer. NUL-terminating as well as counting costs one byte and lets the
     * bytes be handed to a C caller without another copy.
     */
    copy = (char *)RoboCalloc(1u, length + 1u);
    if (!copy) {
        return RoboHtmlDiag(diag, RoboHtmlDomainForms, RoboOutOfMemory,
                              "control value allocation failed");
    }
    if (length > 0u) {
        memcpy(copy, value, length);
    }
    copy[length] = '\0';

    entry = ControlEnsure(slot, element, diag);
    if (!entry) {
        RoboFree(copy);
        return RoboOutOfMemory;
    }
    if (entry->value) {
        RoboFree(entry->value);
    }
    entry->value = copy;
    entry->valueLength = length;
    LogDebug("robo_html: control value set doc=%llu tag=%s len=%llu",
                   (unsigned long long)element->documentId,
                   ControlLogName(element), (unsigned long long)length);
    return RoboOk;
}

Result HtmlElementClearControlValue(
    const HtmlNodeHandle *element, HtmlDiag *diag)
{
    HtmlDocumentSlot *slot;
    HtmlControlState *entry;
    Result res = ValueGate(element, diag);
    int hadOverride = 0;

    if (res != RoboOk) {
        return res;
    }
    slot = HtmlDocumentSlotFind(element->documentId);
    if (!slot) {
        return RoboHtmlDiag(diag, RoboHtmlDomainForms, RoboNotFound,
                              "document does not resolve");
    }
    entry = ControlFind(slot, element);
    hadOverride = (entry && entry->value) ? 1 : 0;
    if (hadOverride) {
        RoboFree(entry->value);
        entry->value = NULL;
        entry->valueLength = 0u;
    }
    ControlTidy(slot, ControlFindIndex(slot, element));
    LogDebug(
        "robo_html: control value cleared doc=%llu tag=%s had_override=%d",
        (unsigned long long)element->documentId, ControlLogName(element),
        hadOverride);
    /* Absent override is a successful no-op: a form reset must not fail
     * because one field was never edited. */
    return RoboOk;
}

Result HtmlElementControlChecked(
    const HtmlNodeHandle *element, int *outChecked,
    int *outIsDeclared, HtmlDiag *diag)
{
    HtmlDocumentSlot *slot;
    HtmlControlState *entry;
    Result res;

    if (!outChecked) {
        return RoboHtmlDiag(diag, RoboHtmlDomainForms,
                              RoboInvalidArgument, "NULL out");
    }
    res = CheckedGate(element, diag);
    if (res != RoboOk) {
        return res;
    }
    slot = HtmlDocumentSlotFind(element->documentId);
    if (slot) {
        entry = ControlFind(slot, element);
        if (entry && entry->checkedSet) {
            if (outIsDeclared) {
                *outIsDeclared = 0;
            }
            *outChecked = entry->checked;
            return RoboOk;
        }
    }
    if (outIsDeclared) {
        *outIsDeclared = 1;
    }
    *outChecked = HtmlElementHasAttribute(element, "checked");
    return RoboOk;
}

Result HtmlElementSetControlChecked(
    const HtmlNodeHandle *element, int checked, HtmlDiag *diag)
{
    HtmlDocumentSlot *slot;
    HtmlControlState *entry;
    Result res = CheckedGate(element, diag);

    if (res != RoboOk) {
        return res;
    }
    slot = HtmlDocumentSlotFind(element->documentId);
    if (!slot) {
        return RoboHtmlDiag(diag, RoboHtmlDomainForms, RoboNotFound,
                              "document does not resolve");
    }
    entry = ControlEnsure(slot, element, diag);
    if (!entry) {
        return RoboOutOfMemory;
    }
    entry->checkedSet = 1;
    entry->checked = checked ? 1 : 0;
    LogDebug("robo_html: control checked set doc=%llu tag=%s checked=%d",
                   (unsigned long long)element->documentId,
                   ControlLogName(element), entry->checked);
    return RoboOk;
}

Result HtmlElementClearControlChecked(
    const HtmlNodeHandle *element, HtmlDiag *diag)
{
    HtmlDocumentSlot *slot;
    HtmlControlState *entry;
    Result res = CheckedGate(element, diag);
    int hadOverride = 0;

    if (res != RoboOk) {
        return res;
    }
    slot = HtmlDocumentSlotFind(element->documentId);
    if (!slot) {
        return RoboHtmlDiag(diag, RoboHtmlDomainForms, RoboNotFound,
                              "document does not resolve");
    }
    entry = ControlFind(slot, element);
    hadOverride = (entry && entry->checkedSet) ? 1 : 0;
    if (entry) {
        entry->checkedSet = 0;
        entry->checked = 0;
    }
    ControlTidy(slot, ControlFindIndex(slot, element));
    LogDebug(
        "robo_html: control checked cleared doc=%llu tag=%s had_override=%d",
        (unsigned long long)element->documentId, ControlLogName(element),
        hadOverride);
    return RoboOk;
}

/* ------------------------------------------------------------------ */
/* <select> / <option> helpers                                         */
/* ------------------------------------------------------------------ */

static int OptionPredicate(const HtmlNodeView *view, void *user)
{
    (void)user;
    return view && view->kind == RoboHtmlNodeKindElement &&
           HtmlElementIsTag(&view->handle, RoboHtmlTagOption);
}

static int OptionSelectedPredicate(const HtmlNodeView *view,
                                     void *user)
{
    (void)user;
    return OptionPredicate(view, NULL) &&
           HtmlElementHasAttribute(&view->handle, "selected");
}

static int OptionEnabledPredicate(const HtmlNodeView *view,
                                    void *user)
{
    (void)user;
    return OptionPredicate(view, NULL) &&
           !HtmlElementHasAttribute(&view->handle, "disabled");
}

typedef struct optionCountContext {
    size_t count;
} OptionCountContext;

static Result OptionCountVisit(const HtmlNodeView *view,
                                        size_t depth, void *user)
{
    OptionCountContext *context = (OptionCountContext *)user;

    (void)depth;
    if (OptionPredicate(view, NULL)) {
        context->count++;
    }
    return RoboOk;
}

typedef struct optionIndexContext {
    const HtmlNodeHandle *target;
    size_t seen;
    int found;
} OptionIndexContext;

static Result OptionIndexVisit(const HtmlNodeView *view,
                                        size_t depth, void *user)
{
    OptionIndexContext *context = (OptionIndexContext *)user;

    (void)depth;
    if (!OptionPredicate(view, NULL)) {
        return RoboOk;
    }
    if (HtmlNodeHandleEqual(&view->handle, context->target)) {
        context->found = 1;
        return RoboNotFound; /* stop the walk; the caller reads the index */
    }
    context->seen++;
    return RoboOk;
}

Result HtmlElementSelectOptionCount(
    const HtmlNodeHandle *select, size_t *outCount,
    HtmlDiag *diag)
{
    Result res;
    OptionCountContext context;

    if (!select || !outCount) {
        return RoboHtmlDiag(diag, RoboHtmlDomainForms,
                              RoboInvalidArgument, "NULL select or out");
    }
    *outCount = 0u;
    res = TagGate(select, RoboHtmlTagSelect, "element is not a select",
                   diag);
    if (res != RoboOk) {
        return res;
    }
    context.count = 0u;
    res = HtmlNodeWalkDescendants(select, RoboHtmlNodeFilterElements,
                                          OptionCountVisit, &context, diag);
    if (res != RoboOk) {
        return res;
    }
    *outCount = context.count;
    return RoboOk;
}

Result HtmlElementOptionIndex(
    const HtmlNodeHandle *option, size_t *outIndex,
    HtmlDiag *diag)
{
    HtmlNodeHandle select;
    OptionIndexContext context;
    Result res;

    if (!option || !outIndex) {
        return RoboHtmlDiag(diag, RoboHtmlDomainForms,
                              RoboInvalidArgument, "NULL option or out");
    }
    *outIndex = 0u;
    res = TagGate(option, RoboHtmlTagOption, "element is not an option",
                   diag);
    if (res != RoboOk) {
        return res;
    }
    /*
     * An option outside any select has no index. Reporting 0 would be a lie
     * that looks like a position, so it is NOT_FOUND — the answer anything
     * without a position should give. Note this walks to the nearest *select*,
     * not the nearest form: an option's position is a property of its own
     * select, and a select that is not inside a form is entirely ordinary.
     */
    {
        HtmlNodeHandle current = *option;
        size_t guard = 0u;
        int found = 0;

        HtmlNodeHandleClear(&select);
        while (guard++ < RoboHtmlMaxNodesPerDocument) {
            HtmlNodeHandle parent;

            if (HtmlElementParent(&current, &parent, NULL) != RoboOk) {
                break;
            }
            if (HtmlElementIsTag(&parent, RoboHtmlTagSelect)) {
                select = parent;
                found = 1;
                break;
            }
            current = parent;
        }
        if (!found) {
            return RoboHtmlDiag(diag, RoboHtmlDomainForms,
                                  RoboNotFound,
                                  "option is not inside a select");
        }
    }

    context.target = option;
    context.seen = 0u;
    context.found = 0;
    (void)HtmlNodeWalkDescendants(&select,
                                          RoboHtmlNodeFilterElements,
                                          OptionIndexVisit, &context, NULL);
    if (!context.found) {
        return RoboHtmlDiag(diag, RoboHtmlDomainForms, RoboNotFound,
                              "option is not a descendant of its select");
    }
    *outIndex = context.seen;
    return RoboOk;
}

Result HtmlElementOptionValue(
    const HtmlNodeHandle *option, char *out, size_t outCap,
    size_t *outLength, int *outIsExplicit, HtmlDiag *diag)
{
    const char *value;
    size_t length = 0u;
    Result res;

    if (!out || outCap == 0u) {
        return RoboHtmlDiag(diag, RoboHtmlDomainForms,
                              RoboInvalidArgument, "NULL or zero-capacity out");
    }
    res = TagGate(option, RoboHtmlTagOption, "element is not an option",
                   diag);
    if (res != RoboOk) {
        return res;
    }
    value = HtmlElementAttributeValue(option, "value", &length);
    if (value) {
        if (outIsExplicit) {
            *outIsExplicit = 1;
        }
        return CopySpan(out, outCap, value, length, outLength);
    }
    /*
     * The fallback is the real rule: an `<option>Portugal</option>` with no
     * `value` submits "Portugal". The text is taken verbatim rather than
     * whitespace-folded, because folding is a decision for the code that
     * renders or submits a value, not for the code that reads one.
     */
    if (outIsExplicit) {
        *outIsExplicit = 0;
    }
    return HtmlNodeSubtreeText(option, out, outCap, outLength, diag);
}

Result HtmlElementSelectSelectedOption(
    const HtmlNodeHandle *select, HtmlNodeHandle *out,
    HtmlDiag *diag)
{
    Result res;
    HtmlNodeHandle subject;

    if (!out || !select) {
        return RoboHtmlDiag(diag, RoboHtmlDomainForms,
                              RoboInvalidArgument, "NULL select or out");
    }
    /* Same aliasing rule as the other resolvers: the caller may pass the
     * select as the output slot. */
    subject = *select;
    select = &subject;
    HtmlNodeHandleClear(out);
    res = TagGate(select, RoboHtmlTagSelect, "element is not a select",
                   diag);
    if (res != RoboOk) {
        return res;
    }
    /* First `selected` wins; otherwise the first option that is not disabled,
     * because a select with no `selected` shows a selectable option and a
     * disabled first option is a state the user could never reach. */
    res = HtmlNodeFindDescendant(select, RoboHtmlNodeFilterElements,
                                         OptionSelectedPredicate, NULL, out,
                                         NULL);
    if (res == RoboOk) {
        return RoboOk;
    }
    if (res != RoboNotFound) {
        return res;
    }
    res = HtmlNodeFindDescendant(select, RoboHtmlNodeFilterElements,
                                         OptionEnabledPredicate, NULL, out,
                                         NULL);
    if (res == RoboNotFound) {
        HtmlNodeHandleClear(out);
        return RoboOk; /* an empty select selects nothing; that is not an error */
    }
    return res;
}

/* ------------------------------------------------------------------ */
/* Form ownership                                                      */
/* ------------------------------------------------------------------ */

Result HtmlElementFormOwner(
    const HtmlNodeHandle *control, HtmlNodeHandle *out,
    HtmlFormOwnerSource *outSource, HtmlDiag *diag)
{
    const char *formId;
    size_t formLength = 0u;
    HtmlNodeHandle subject;

    if (!out) {
        return RoboHtmlDiag(diag, RoboHtmlDomainForms,
                              RoboInvalidArgument, "NULL out");
    }
    if (!control) {
        return RoboHtmlDiag(diag, RoboHtmlDomainForms,
                              RoboInvalidArgument, "NULL control");
    }
    /*
     * Work on a copy, so a caller that passes the same handle as @p control
     * and @p out is well defined rather than silently reading a handle this
     * function just cleared. One copy is cheaper than a rule every caller has
     * to know.
     */
    subject = *control;
    control = &subject;
    HtmlNodeHandleClear(out);
    if (outSource) {
        *outSource = RoboHtmlFormOwnerSourceNone;
    }
    if (!HtmlNodeMatchesFilter(control, RoboHtmlNodeFilterElements)) {
        return RoboHtmlDiag(diag, RoboHtmlDomainForms, RoboNotFound,
                              "control is not a live element");
    }
    if (!HtmlTagIsFormAssociated(HtmlElementTag(control))) {
        return RoboHtmlDiag(diag, RoboHtmlDomainForms, RoboNotSupported,
                              "element cannot have a form owner");
    }

    /*
     * Rule 1: the `form` attribute, when it resolves to a <form>.
     *
     * A `form` that names an element which is not a form, or names nothing at
     * all, falls through to rule 2 rather than refusing. The source field
     * records that the attribute did not answer, so a caller that needs the
     * distinction has it; a caller that only wants "which form is this in?"
     * gets the useful answer instead of a detached control.
     */
    formId = HtmlElementAttributeValue(control, "form", &formLength);
    if (HtmlSpanIsUsable(formId, formLength)) {
        HtmlNodeHandle root =
            HtmlDocumentRoot(HtmlNodeHandleDocument(control));
        HtmlNodeHandle candidate;

        if (FindElementById(&root, formId, formLength, &candidate) ==
                RoboOk &&
            HtmlElementIsTag(&candidate, RoboHtmlTagForm)) {
            *out = candidate;
            if (outSource) {
                *outSource = RoboHtmlFormOwnerSourceAttribute;
            }
            return RoboOk;
        }
    }

    /* Rule 2: the nearest ancestor <form>. */
    if (NearestAncestorForm(control, out) == RoboOk) {
        if (outSource) {
            *outSource = RoboHtmlFormOwnerSourceAncestor;
        }
        return RoboOk;
    }

    /* Rule 3: no owner. A valid state, not an error. */
    HtmlNodeHandleClear(out);
    return RoboOk;
}
