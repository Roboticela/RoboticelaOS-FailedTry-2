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
 *   state        robo_html_control_state_t — the explicit overrides that make
 *                "current value" different from "declared value".
 *
 * Nothing here allocates per read, and nothing here caches a relationship.
 * The only heap this module owns is the value copy of an explicit override,
 * and that lives in the document slot's control table, freed by
 * robo_html_document_controls_free on both teardown paths.
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
static int ascii_is_space(char c)
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
static int span_names(const char *value, size_t length, const char *name)
{
    size_t begin = 0u;
    size_t end = length;

    if (!value || !name) {
        return 0;
    }
    while (begin < end && ascii_is_space(value[begin])) {
        begin++;
    }
    while (end > begin && ascii_is_space(value[end - 1u])) {
        end--;
    }
    return robo_html_name_equal_span(value + begin, end - begin, name);
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
static int parse_decimal(const char *text, size_t length, double *out)
{
    size_t begin = 0u;
    size_t end = length;
    size_t i;
    size_t digits = 0u;
    size_t fraction_digits = 0u;
    int negative = 0;
    int after_point = 0;
    int saw_digit = 0; /* any digit at all — a lone sign is not a number */
    int started = 0;   /* seen a non-zero digit yet (leading zeros do not count) */
    double whole = 0.0;
    double fraction = 0.0;
    double divisor = 1.0;

    if (!text || !out) {
        return 0;
    }
    while (begin < end && ascii_is_space(text[begin])) {
        begin++;
    }
    while (end > begin && ascii_is_space(text[end - 1u])) {
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
            if (after_point) {
                return 0; /* a second point: not a number */
            }
            after_point = 1;
            continue;
        }
        if (c < '0' || c > '9') {
            return 0; /* anything else — including 'e', 'x', a space inside */
        }
        saw_digit = 1;
        if (c != '0') {
            started = 1;
        }
        if (started) {
            digits++;
            if (digits > 15u) {
                return 0;
            }
        }
        if (after_point) {
            fraction = fraction * 10.0 + (double)(c - '0');
            divisor *= 10.0;
            fraction_digits++;
        } else {
            whole = whole * 10.0 + (double)(c - '0');
        }
    }
    /*
     * Two rejections, and the first one is why `saw_digit` exists rather than
     * reusing `started`: zero has no non-zero digit, so a "did a non-zero digit
     * appear?" test would refuse `min="0"`. "Any digit appeared" is the rule
     * that rejects a lone sign while accepting `0`, `00` and `0.5`.
     */
    if (!saw_digit) {
        return 0; /* a lone sign, or an empty value */
    }
    if (after_point && fraction_digits == 0u) {
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
static int parse_unsigned(const char *text, size_t length, unsigned long cap,
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
    while (begin < end && ascii_is_space(text[begin])) {
        begin++;
    }
    while (end > begin && ascii_is_space(text[end - 1u])) {
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
 * to @p out_cap - 1. @p out_length receives the untruncated length, so a
 * caller can tell truncation from a short value — the same contract
 * robo_html_node_subtree_text uses, so the two are interchangeable to a caller.
 */
static robo_result_t copy_span(char *out, size_t out_cap, const char *text,
                               size_t length, size_t *out_length)
{
    size_t copy;

    if (!out || out_cap == 0u) {
        return ROBO_INVALID_ARGUMENT;
    }
    copy = length;
    if (copy > out_cap - 1u) {
        copy = out_cap - 1u;
    }
    if (copy > 0u && text) {
        memcpy(out, text, copy);
    }
    out[copy] = '\0';
    if (out_length) {
        *out_length = length;
    }
    return ROBO_OK;
}

/* ------------------------------------------------------------------ */
/* <form> vocabulary                                                   */
/* ------------------------------------------------------------------ */

int robo_html_form_method_is_known(robo_html_form_method_t method)
{
    return method >= ROBO_HTML_FORM_METHOD_NONE &&
           method <= ROBO_HTML_FORM_METHOD_UNKNOWN;
}

const char *robo_html_form_method_name(robo_html_form_method_t method)
{
    switch (method) {
    case ROBO_HTML_FORM_METHOD_NONE:
        return "none";
    case ROBO_HTML_FORM_METHOD_GET:
        return "get";
    case ROBO_HTML_FORM_METHOD_POST:
        return "post";
    case ROBO_HTML_FORM_METHOD_DIALOG:
        return "dialog";
    case ROBO_HTML_FORM_METHOD_UNKNOWN:
        return "unknown";
    default:
        return "unknown";
    }
}

robo_html_form_method_t robo_html_form_method_from_name(const char *value,
                                                        size_t length)
{
    if (!robo_html_span_is_usable(value, length)) {
        return ROBO_HTML_FORM_METHOD_NONE;
    }
    if (span_names(value, length, "get")) {
        return ROBO_HTML_FORM_METHOD_GET;
    }
    if (span_names(value, length, "post")) {
        return ROBO_HTML_FORM_METHOD_POST;
    }
    if (span_names(value, length, "dialog")) {
        return ROBO_HTML_FORM_METHOD_DIALOG;
    }
    return ROBO_HTML_FORM_METHOD_UNKNOWN;
}

int robo_html_form_enctype_is_known(robo_html_form_enctype_t enctype)
{
    return enctype >= ROBO_HTML_FORM_ENCTYPE_NONE &&
           enctype <= ROBO_HTML_FORM_ENCTYPE_UNKNOWN;
}

const char *robo_html_form_enctype_name(robo_html_form_enctype_t enctype)
{
    switch (enctype) {
    case ROBO_HTML_FORM_ENCTYPE_NONE:
        return "none";
    case ROBO_HTML_FORM_ENCTYPE_URLENCODED:
        return "application/x-www-form-urlencoded";
    case ROBO_HTML_FORM_ENCTYPE_MULTIPART:
        return "multipart/form-data";
    case ROBO_HTML_FORM_ENCTYPE_TEXT_PLAIN:
        return "text/plain";
    case ROBO_HTML_FORM_ENCTYPE_UNKNOWN:
        return "unknown";
    default:
        return "unknown";
    }
}

robo_html_form_enctype_t robo_html_form_enctype_from_name(const char *value,
                                                          size_t length)
{
    if (!robo_html_span_is_usable(value, length)) {
        return ROBO_HTML_FORM_ENCTYPE_NONE;
    }
    if (span_names(value, length, "application/x-www-form-urlencoded")) {
        return ROBO_HTML_FORM_ENCTYPE_URLENCODED;
    }
    if (span_names(value, length, "multipart/form-data")) {
        return ROBO_HTML_FORM_ENCTYPE_MULTIPART;
    }
    if (span_names(value, length, "text/plain")) {
        return ROBO_HTML_FORM_ENCTYPE_TEXT_PLAIN;
    }
    return ROBO_HTML_FORM_ENCTYPE_UNKNOWN;
}

/* ------------------------------------------------------------------ */
/* <input> / <button> vocabulary                                       */
/* ------------------------------------------------------------------ */

int robo_html_input_type_is_known(robo_html_input_type_t type)
{
    return type >= ROBO_HTML_INPUT_TYPE_TEXT &&
           type <= ROBO_HTML_INPUT_TYPE_UNKNOWN;
}

const char *robo_html_input_type_name(robo_html_input_type_t type)
{
    switch (type) {
    case ROBO_HTML_INPUT_TYPE_TEXT:
        return "text";
    case ROBO_HTML_INPUT_TYPE_PASSWORD:
        return "password";
    case ROBO_HTML_INPUT_TYPE_CHECKBOX:
        return "checkbox";
    case ROBO_HTML_INPUT_TYPE_RADIO:
        return "radio";
    case ROBO_HTML_INPUT_TYPE_NUMBER:
        return "number";
    case ROBO_HTML_INPUT_TYPE_EMAIL:
        return "email";
    case ROBO_HTML_INPUT_TYPE_SUBMIT:
        return "submit";
    case ROBO_HTML_INPUT_TYPE_BUTTON:
        return "button";
    case ROBO_HTML_INPUT_TYPE_RESET:
        return "reset";
    case ROBO_HTML_INPUT_TYPE_SEARCH:
        return "search";
    case ROBO_HTML_INPUT_TYPE_HIDDEN:
        return "hidden";
    case ROBO_HTML_INPUT_TYPE_UNKNOWN:
        return "unknown";
    default:
        return "unknown";
    }
}

robo_html_input_type_t robo_html_input_type_effective(
    robo_html_input_type_t declared)
{
    /*
     * This one line is the whole "degrade safely" requirement: an unrecognised
     * type behaves as text, so the element keeps every other fact it has and a
     * document is never damaged by a type this runtime has not met.
     */
    return (declared == ROBO_HTML_INPUT_TYPE_UNKNOWN ||
            !robo_html_input_type_is_known(declared))
               ? ROBO_HTML_INPUT_TYPE_TEXT
               : declared;
}

robo_html_input_type_t robo_html_input_type_from_name(const char *value,
                                                      size_t length)
{
    if (!robo_html_span_is_usable(value, length)) {
        return ROBO_HTML_INPUT_TYPE_TEXT;
    }
    if (span_names(value, length, "text")) {
        return ROBO_HTML_INPUT_TYPE_TEXT;
    }
    if (span_names(value, length, "password")) {
        return ROBO_HTML_INPUT_TYPE_PASSWORD;
    }
    if (span_names(value, length, "checkbox")) {
        return ROBO_HTML_INPUT_TYPE_CHECKBOX;
    }
    if (span_names(value, length, "radio")) {
        return ROBO_HTML_INPUT_TYPE_RADIO;
    }
    if (span_names(value, length, "number")) {
        return ROBO_HTML_INPUT_TYPE_NUMBER;
    }
    if (span_names(value, length, "email")) {
        return ROBO_HTML_INPUT_TYPE_EMAIL;
    }
    if (span_names(value, length, "submit")) {
        return ROBO_HTML_INPUT_TYPE_SUBMIT;
    }
    if (span_names(value, length, "button")) {
        return ROBO_HTML_INPUT_TYPE_BUTTON;
    }
    if (span_names(value, length, "reset")) {
        return ROBO_HTML_INPUT_TYPE_RESET;
    }
    if (span_names(value, length, "search")) {
        return ROBO_HTML_INPUT_TYPE_SEARCH;
    }
    if (span_names(value, length, "hidden")) {
        return ROBO_HTML_INPUT_TYPE_HIDDEN;
    }
    return ROBO_HTML_INPUT_TYPE_UNKNOWN;
}

int robo_html_input_type_is_checkable(robo_html_input_type_t type)
{
    return type == ROBO_HTML_INPUT_TYPE_CHECKBOX ||
           type == ROBO_HTML_INPUT_TYPE_RADIO;
}

int robo_html_button_type_is_known(robo_html_button_type_t type)
{
    return type >= ROBO_HTML_BUTTON_TYPE_SUBMIT &&
           type <= ROBO_HTML_BUTTON_TYPE_UNKNOWN;
}

const char *robo_html_button_type_name(robo_html_button_type_t type)
{
    switch (type) {
    case ROBO_HTML_BUTTON_TYPE_SUBMIT:
        return "submit";
    case ROBO_HTML_BUTTON_TYPE_BUTTON:
        return "button";
    case ROBO_HTML_BUTTON_TYPE_RESET:
        return "reset";
    case ROBO_HTML_BUTTON_TYPE_UNKNOWN:
        return "unknown";
    default:
        return "unknown";
    }
}

robo_html_button_type_t robo_html_button_type_effective(
    robo_html_button_type_t declared)
{
    return (declared == ROBO_HTML_BUTTON_TYPE_UNKNOWN ||
            !robo_html_button_type_is_known(declared))
               ? ROBO_HTML_BUTTON_TYPE_SUBMIT
               : declared;
}

robo_html_button_type_t robo_html_button_type_from_name(const char *value,
                                                        size_t length)
{
    if (!robo_html_span_is_usable(value, length)) {
        return ROBO_HTML_BUTTON_TYPE_SUBMIT;
    }
    if (span_names(value, length, "submit")) {
        return ROBO_HTML_BUTTON_TYPE_SUBMIT;
    }
    if (span_names(value, length, "button")) {
        return ROBO_HTML_BUTTON_TYPE_BUTTON;
    }
    if (span_names(value, length, "reset")) {
        return ROBO_HTML_BUTTON_TYPE_RESET;
    }
    return ROBO_HTML_BUTTON_TYPE_UNKNOWN;
}

int robo_html_form_owner_source_is_known(robo_html_form_owner_source_t source)
{
    return source >= ROBO_HTML_FORM_OWNER_SOURCE_NONE &&
           source <= ROBO_HTML_FORM_OWNER_SOURCE_ANCESTOR;
}

const char *robo_html_form_owner_source_name(robo_html_form_owner_source_t source)
{
    switch (source) {
    case ROBO_HTML_FORM_OWNER_SOURCE_NONE:
        return "none";
    case ROBO_HTML_FORM_OWNER_SOURCE_ATTRIBUTE:
        return "attribute";
    case ROBO_HTML_FORM_OWNER_SOURCE_ANCESTOR:
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
static robo_result_t tag_gate(const robo_html_node_handle_t *element,
                              robo_html_tag_t tag, const char *what,
                              robo_html_diag_t *diag)
{
    if (!element) {
        return ROBO_HTML_DIAG(diag, ROBO_HTML_DOMAIN_FORMS,
                              ROBO_INVALID_ARGUMENT, "NULL element");
    }
    if (!robo_html_node_matches_filter(element, ROBO_HTML_NODE_FILTER_ELEMENTS)) {
        return ROBO_HTML_DIAG(diag, ROBO_HTML_DOMAIN_FORMS, ROBO_NOT_FOUND,
                              "element is not a live element");
    }
    if (!robo_html_element_is_tag(element, tag)) {
        return ROBO_HTML_DIAG(diag, ROBO_HTML_DOMAIN_FORMS, ROBO_NOT_SUPPORTED,
                              what);
    }
    return ROBO_OK;
}

/**
 * Elements this phase models as *holding a value*.
 *
 * Narrower than robo_html_tag_is_form_associated on purpose: a `<fieldset>`
 * has a form owner but no value, so it answers the ownership question and
 * refuses the value question. A `<select>` has a value; the option it reports
 * as selected comes from the option elements, not from an override.
 */
static int tag_holds_value(robo_html_tag_t tag)
{
    return tag == ROBO_HTML_TAG_INPUT || tag == ROBO_HTML_TAG_TEXTAREA ||
           tag == ROBO_HTML_TAG_SELECT || tag == ROBO_HTML_TAG_BUTTON ||
           tag == ROBO_HTML_TAG_OUTPUT;
}

/* ------------------------------------------------------------------ */
/* <form> attributes                                                   */
/* ------------------------------------------------------------------ */

robo_result_t robo_html_element_form_attributes(
    const robo_html_node_handle_t *element, robo_html_form_attributes_t *out,
    robo_html_diag_t *diag)
{
    robo_result_t res;
    size_t length = 0u;
    const char *value;

    if (!out) {
        return ROBO_HTML_DIAG(diag, ROBO_HTML_DOMAIN_FORMS,
                              ROBO_INVALID_ARGUMENT, "NULL out");
    }
    memset(out, 0, sizeof(*out));
    res = tag_gate(element, ROBO_HTML_TAG_FORM, "element is not a form", diag);
    if (res != ROBO_OK) {
        return res;
    }

#define TAKE_ATTR(field, name)                                                \
    do {                                                                      \
        value = robo_html_element_attribute_value(element, name, &length);    \
        out->field##_present = (value != NULL);                               \
        out->field = value;                                                   \
        out->field##_length = value ? length : 0u;                            \
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
    out->action_usable = robo_html_span_is_usable(out->action,
                                                  out->action_length);
    out->method_kind = robo_html_form_method_from_name(out->method,
                                                       out->method_length);
    out->enctype_kind = robo_html_form_enctype_from_name(out->enctype,
                                                         out->enctype_length);
    return ROBO_OK;
}

/* ------------------------------------------------------------------ */
/* <input> attributes                                                  */
/* ------------------------------------------------------------------ */

robo_result_t robo_html_element_input_attributes(
    const robo_html_node_handle_t *element, robo_html_input_attributes_t *out,
    robo_html_diag_t *diag)
{
    robo_result_t res;
    size_t length = 0u;
    const char *value;

    if (!out) {
        return ROBO_HTML_DIAG(diag, ROBO_HTML_DOMAIN_FORMS,
                              ROBO_INVALID_ARGUMENT, "NULL out");
    }
    memset(out, 0, sizeof(*out));
    res = tag_gate(element, ROBO_HTML_TAG_INPUT, "element is not an input",
                   diag);
    if (res != ROBO_OK) {
        return res;
    }

#define TAKE_ATTR(field, name)                                                \
    do {                                                                      \
        value = robo_html_element_attribute_value(element, name, &length);    \
        out->field##_present = (value != NULL);                               \
        out->field = value;                                                   \
        out->field##_length = value ? length : 0u;                            \
    } while (0)

    TAKE_ATTR(type, "type");
    TAKE_ATTR(name, "name");
    TAKE_ATTR(value, "value");
    TAKE_ATTR(placeholder, "placeholder");
    TAKE_ATTR(min, "min");
    TAKE_ATTR(max, "max");
    TAKE_ATTR(step, "step");
#undef TAKE_ATTR

    out->type_declared = robo_html_input_type_from_name(out->type,
                                                        out->type_length);
    out->type_effective = robo_html_input_type_effective(out->type_declared);
    /* An absent or empty `type` is the declared default (text), not a
     * degradation: the author said nothing, and nothing means text. */
    out->type_recognised = (out->type_declared != ROBO_HTML_INPUT_TYPE_UNKNOWN);

    /* Boolean attributes are presence tests, so `<input checked>` and
     * `<input checked="checked">` agree — the HTML rule. */
    out->checked = robo_html_element_has_attribute(element, "checked");
    out->disabled = robo_html_element_has_attribute(element, "disabled");
    out->readonly = robo_html_element_has_attribute(element, "readonly");
    out->required = robo_html_element_has_attribute(element, "required");

    /* Parsed, not compared: `<input min="10" max="1">` is reported
     * faithfully rather than flagged, because validation is not this phase. */
    out->min_valid = parse_decimal(out->min, out->min_length, &out->min_value);
    out->max_valid = parse_decimal(out->max, out->max_length, &out->max_value);
    out->step_valid = parse_decimal(out->step, out->step_length,
                                    &out->step_value);
    return ROBO_OK;
}

/* ------------------------------------------------------------------ */
/* <button> attributes                                                 */
/* ------------------------------------------------------------------ */

robo_result_t robo_html_element_button_attributes(
    const robo_html_node_handle_t *element, robo_html_button_attributes_t *out,
    robo_html_diag_t *diag)
{
    robo_result_t res;
    size_t length = 0u;
    const char *value;

    if (!out) {
        return ROBO_HTML_DIAG(diag, ROBO_HTML_DOMAIN_FORMS,
                              ROBO_INVALID_ARGUMENT, "NULL out");
    }
    memset(out, 0, sizeof(*out));
    res = tag_gate(element, ROBO_HTML_TAG_BUTTON, "element is not a button",
                   diag);
    if (res != ROBO_OK) {
        return res;
    }

#define TAKE_ATTR(field, name)                                                \
    do {                                                                      \
        value = robo_html_element_attribute_value(element, name, &length);    \
        out->field##_present = (value != NULL);                               \
        out->field = value;                                                   \
        out->field##_length = value ? length : 0u;                            \
    } while (0)

    TAKE_ATTR(type, "type");
    TAKE_ATTR(name, "name");
    TAKE_ATTR(value, "value");
#undef TAKE_ATTR

    out->type_declared = robo_html_button_type_from_name(out->type,
                                                         out->type_length);
    out->type_effective = robo_html_button_type_effective(out->type_declared);
    out->type_recognised =
        (out->type_declared != ROBO_HTML_BUTTON_TYPE_UNKNOWN);
    out->disabled = robo_html_element_has_attribute(element, "disabled");
    return ROBO_OK;
}

/* ------------------------------------------------------------------ */
/* <textarea> attributes                                               */
/* ------------------------------------------------------------------ */

robo_result_t robo_html_element_textarea_attributes(
    const robo_html_node_handle_t *element,
    robo_html_textarea_attributes_t *out, robo_html_diag_t *diag)
{
    robo_result_t res;
    size_t length = 0u;
    const char *value;

    if (!out) {
        return ROBO_HTML_DIAG(diag, ROBO_HTML_DOMAIN_FORMS,
                              ROBO_INVALID_ARGUMENT, "NULL out");
    }
    memset(out, 0, sizeof(*out));
    res = tag_gate(element, ROBO_HTML_TAG_TEXTAREA,
                   "element is not a textarea", diag);
    if (res != ROBO_OK) {
        return res;
    }

#define TAKE_ATTR(field, name)                                                \
    do {                                                                      \
        value = robo_html_element_attribute_value(element, name, &length);    \
        out->field##_present = (value != NULL);                               \
        out->field = value;                                                   \
        out->field##_length = value ? length : 0u;                            \
    } while (0)

    TAKE_ATTR(name, "name");
    TAKE_ATTR(rows, "rows");
    TAKE_ATTR(cols, "cols");
    TAKE_ATTR(placeholder, "placeholder");
#undef TAKE_ATTR

    out->rows_valid = parse_unsigned(out->rows, out->rows_length,
                                     ROBO_HTML_FORM_MAX_DECLARED_LINES,
                                     &out->rows_value);
    out->cols_valid = parse_unsigned(out->cols, out->cols_length,
                                     ROBO_HTML_FORM_MAX_DECLARED_LINES,
                                     &out->cols_value);
    out->disabled = robo_html_element_has_attribute(element, "disabled");
    out->readonly = robo_html_element_has_attribute(element, "readonly");
    out->required = robo_html_element_has_attribute(element, "required");
    return ROBO_OK;
}

/* ------------------------------------------------------------------ */
/* <select> / <option> attributes                                      */
/* ------------------------------------------------------------------ */

robo_result_t robo_html_element_select_attributes(
    const robo_html_node_handle_t *element, robo_html_select_attributes_t *out,
    robo_html_diag_t *diag)
{
    robo_result_t res;
    size_t length = 0u;
    const char *value;

    if (!out) {
        return ROBO_HTML_DIAG(diag, ROBO_HTML_DOMAIN_FORMS,
                              ROBO_INVALID_ARGUMENT, "NULL out");
    }
    memset(out, 0, sizeof(*out));
    res = tag_gate(element, ROBO_HTML_TAG_SELECT, "element is not a select",
                   diag);
    if (res != ROBO_OK) {
        return res;
    }
    value = robo_html_element_attribute_value(element, "name", &length);
    out->name_present = (value != NULL);
    out->name = value;
    out->name_length = value ? length : 0u;
    out->multiple = robo_html_element_has_attribute(element, "multiple");
    out->disabled = robo_html_element_has_attribute(element, "disabled");
    out->required = robo_html_element_has_attribute(element, "required");
    return ROBO_OK;
}

robo_result_t robo_html_element_option_attributes(
    const robo_html_node_handle_t *element, robo_html_option_attributes_t *out,
    robo_html_diag_t *diag)
{
    robo_result_t res;
    size_t length = 0u;
    const char *value;

    if (!out) {
        return ROBO_HTML_DIAG(diag, ROBO_HTML_DOMAIN_FORMS,
                              ROBO_INVALID_ARGUMENT, "NULL out");
    }
    memset(out, 0, sizeof(*out));
    res = tag_gate(element, ROBO_HTML_TAG_OPTION, "element is not an option",
                   diag);
    if (res != ROBO_OK) {
        return res;
    }
    value = robo_html_element_attribute_value(element, "value", &length);
    out->value_present = (value != NULL);
    out->value = value;
    out->value_length = value ? length : 0u;

    value = robo_html_element_attribute_value(element, "label", &length);
    out->label_present = (value != NULL);
    out->label = value;
    out->label_length = value ? length : 0u;

    out->selected = robo_html_element_has_attribute(element, "selected");
    out->disabled = robo_html_element_has_attribute(element, "disabled");
    return ROBO_OK;
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

typedef struct id_lookup_context {
    const char *id;
    size_t id_length;
} id_lookup_context_t;

static int id_lookup_match(const robo_html_node_view_t *view, void *user)
{
    const id_lookup_context_t *context = (const id_lookup_context_t *)user;
    size_t length = 0u;
    const char *value;

    if (!view || view->kind != ROBO_HTML_NODE_KIND_ELEMENT || !context) {
        return 0;
    }
    value = robo_html_element_attribute_value(&view->handle, "id", &length);
    return value && length == context->id_length &&
           (length == 0u || memcmp(value, context->id, length) == 0);
}

/** First element in @p scope's subtree (or @p scope itself) whose id is @p id. */
static robo_result_t find_element_by_id(const robo_html_node_handle_t *scope,
                                        const char *id, size_t id_length,
                                        robo_html_node_handle_t *out)
{
    id_lookup_context_t context;

    robo_html_node_handle_clear(out);
    if (!scope || !id || id_length == 0u) {
        return ROBO_NOT_FOUND;
    }
    context.id = id;
    context.id_length = id_length;
    return robo_html_node_find_first(scope, ROBO_HTML_NODE_FILTER_ELEMENTS,
                                     id_lookup_match, &context, out, NULL);
}

/**
 * Nearest ancestor of @p element that is a `<form>`, or ROBO_NOT_FOUND.
 *
 * The loop is depth-bounded as well as acyclic-bounded: a valid tree cannot
 * loop, and the guard costs one comparison per level so that a corrupted one
 * cannot hang the runtime.
 */
static robo_result_t nearest_ancestor_form(
    const robo_html_node_handle_t *element, robo_html_node_handle_t *out)
{
    robo_html_node_handle_t current = *element;
    size_t guard = 0u;

    robo_html_node_handle_clear(out);
    while (guard++ < ROBO_HTML_MAX_NODES_PER_DOCUMENT) {
        robo_html_node_handle_t parent;

        if (robo_html_element_parent(&current, &parent, NULL) != ROBO_OK) {
            return ROBO_NOT_FOUND;
        }
        if (robo_html_element_is_tag(&parent, ROBO_HTML_TAG_FORM)) {
            *out = parent;
            return ROBO_OK;
        }
        current = parent;
    }
    return ROBO_NOT_FOUND;
}

/* ------------------------------------------------------------------ */
/* <label> — the labelled control                                      */
/* ------------------------------------------------------------------ */

static int labelable_predicate(const robo_html_node_view_t *view, void *user)
{
    (void)user;
    if (!view || view->kind != ROBO_HTML_NODE_KIND_ELEMENT) {
        return 0;
    }
    return robo_html_tag_is_labelable(robo_html_element_tag(&view->handle));
}

robo_result_t robo_html_element_label_target(
    const robo_html_node_handle_t *label, robo_html_node_handle_t *out,
    int *out_from_attribute, robo_html_diag_t *diag)
{
    robo_result_t res;
    const char *for_id;
    size_t for_length = 0u;
    robo_html_node_handle_t subject;

    if (!out || !label) {
        return ROBO_HTML_DIAG(diag, ROBO_HTML_DOMAIN_FORMS,
                              ROBO_INVALID_ARGUMENT, "NULL label or out");
    }
    /*
     * Work on a copy, so a caller that passes the same handle as @p label and
     * @p out is well defined rather than silently reading a handle this
     * function just cleared. One copy is cheaper than a rule every caller has
     * to know.
     */
    subject = *label;
    label = &subject;
    robo_html_node_handle_clear(out);
    if (out_from_attribute) {
        *out_from_attribute = 0;
    }
    res = tag_gate(label, ROBO_HTML_TAG_LABEL, "element is not a label", diag);
    if (res != ROBO_OK) {
        return res;
    }

    for_id = robo_html_element_attribute_value(label, "for", &for_length);
    if (robo_html_span_is_usable(for_id, for_length)) {
        robo_html_node_handle_t root =
            robo_html_document_root(robo_html_node_handle_document(label));

        if (find_element_by_id(&root, for_id, for_length, out) == ROBO_OK &&
            robo_html_tag_is_labelable(robo_html_element_tag(out))) {
            if (out_from_attribute) {
                *out_from_attribute = 1;
            }
            return ROBO_OK;
        }
        /*
         * The author named something, and it does not resolve to a control.
         * Falling through to the descendant search here would silently ignore
         * an id that is wrong, or points at a stranger's element, and report
         * the wrapped control instead — which is exactly the kind of guess
         * that makes a broken document look fine.
         */
        robo_html_node_handle_clear(out);
        return ROBO_OK;
    }

    res = robo_html_node_find_descendant(label, ROBO_HTML_NODE_FILTER_ELEMENTS,
                                         labelable_predicate, NULL, out, NULL);
    if (res == ROBO_NOT_FOUND) {
        robo_html_node_handle_clear(out);
        return ROBO_OK;
    }
    return res;
}

/* ------------------------------------------------------------------ */
/* Control state table                                                 */
/* ------------------------------------------------------------------ */

/** Index of @p node's entry, or CONTROL_INDEX_NONE. */
#define CONTROL_INDEX_NONE ((size_t)-1)

static size_t control_find_index(robo_html_document_slot_t *slot,
                                 const robo_html_node_handle_t *node)
{
    size_t i;

    for (i = 0u; i < slot->control_count; i++) {
        if (robo_html_node_handle_equal(&slot->controls[i].node, node)) {
            return i;
        }
    }
    return CONTROL_INDEX_NONE;
}

static robo_html_control_state_t *control_find(
    robo_html_document_slot_t *slot, const robo_html_node_handle_t *node)
{
    size_t index = control_find_index(slot, node);

    return (index == CONTROL_INDEX_NONE) ? NULL : &slot->controls[index];
}

static void control_entry_release(robo_html_control_state_t *entry)
{
    if (entry->value) {
        robo_free(entry->value);
        entry->value = NULL;
    }
    entry->value_length = 0u;
    entry->checked_set = 0;
    entry->checked = 0;
}

/** Release and remove entry @p index, compacting the table. */
static void control_remove(robo_html_document_slot_t *slot, size_t index)
{
    control_entry_release(&slot->controls[index]);
    if (index + 1u < slot->control_count) {
        memmove(&slot->controls[index], &slot->controls[index + 1u],
                (slot->control_count - index - 1u) * sizeof(slot->controls[0]));
    }
    slot->control_count--;
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
static void control_tidy(robo_html_document_slot_t *slot, size_t index)
{
    if (index != CONTROL_INDEX_NONE && !slot->controls[index].value &&
        !slot->controls[index].checked_set) {
        control_remove(slot, index);
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
static robo_html_control_state_t *control_ensure(
    robo_html_document_slot_t *slot, const robo_html_node_handle_t *node,
    robo_html_diag_t *diag)
{
    robo_html_control_state_t *existing = control_find(slot, node);

    if (existing) {
        return existing;
    }
    if (slot->control_count == slot->control_capacity) {
        size_t next = slot->control_capacity ? slot->control_capacity * 2u : 4u;
        robo_html_control_state_t *grown = (robo_html_control_state_t *)
            robo_realloc(slot->controls, next * sizeof(slot->controls[0]));

        if (!grown) {
            ROBO_HTML_DIAG(diag, ROBO_HTML_DOMAIN_FORMS, ROBO_OUT_OF_MEMORY,
                           "control state table growth");
            return NULL;
        }
        slot->controls = grown;
        slot->control_capacity = next;
    }
    memset(&slot->controls[slot->control_count], 0,
           sizeof(slot->controls[0]));
    slot->controls[slot->control_count].node = *node;
    slot->control_count++;
    return &slot->controls[slot->control_count - 1u];
}

void robo_html_document_controls_free(robo_html_document_slot_t *slot)
{
    size_t i;

    if (!slot) {
        return;
    }
    for (i = 0u; i < slot->control_count; i++) {
        control_entry_release(&slot->controls[i]);
    }
    if (slot->controls) {
        robo_free(slot->controls);
    }
    slot->controls = NULL;
    slot->control_count = 0u;
    slot->control_capacity = 0u;
}

/** Gate shared by the value entry points. */
static robo_result_t value_gate(const robo_html_node_handle_t *element,
                                robo_html_diag_t *diag)
{
    if (!element) {
        return ROBO_HTML_DIAG(diag, ROBO_HTML_DOMAIN_FORMS,
                              ROBO_INVALID_ARGUMENT, "NULL element");
    }
    if (!robo_html_node_matches_filter(element, ROBO_HTML_NODE_FILTER_ELEMENTS)) {
        return ROBO_HTML_DIAG(diag, ROBO_HTML_DOMAIN_FORMS, ROBO_NOT_FOUND,
                              "element is not a live element");
    }
    if (!tag_holds_value(robo_html_element_tag(element))) {
        return ROBO_HTML_DIAG(diag, ROBO_HTML_DOMAIN_FORMS, ROBO_NOT_SUPPORTED,
                              "element is not a control that holds a value");
    }
    return ROBO_OK;
}

/**
 * Gate shared by the checkedness entry points.
 *
 * Checkedness is modelled for `<input>` only, and only when the input's
 * *effective* type is checkable. So `<input type="text" checked>` has an
 * attribute that is preserved (see robo_html_element_input_attributes) and no
 * checkedness to report — two honest facts instead of one misleading one.
 *
 * The type is read from the element rather than passed in, so a type change is
 * observed on the next call with no cache and no mutation hook.
 */
static robo_result_t checked_gate(const robo_html_node_handle_t *element,
                                  robo_html_diag_t *diag)
{
    robo_html_input_attributes_t attributes;
    robo_result_t res;

    if (!element) {
        return ROBO_HTML_DIAG(diag, ROBO_HTML_DOMAIN_FORMS,
                              ROBO_INVALID_ARGUMENT, "NULL element");
    }
    if (!robo_html_node_matches_filter(element, ROBO_HTML_NODE_FILTER_ELEMENTS)) {
        return ROBO_HTML_DIAG(diag, ROBO_HTML_DOMAIN_FORMS, ROBO_NOT_FOUND,
                              "element is not a live element");
    }
    if (!robo_html_element_is_tag(element, ROBO_HTML_TAG_INPUT)) {
        return ROBO_HTML_DIAG(diag, ROBO_HTML_DOMAIN_FORMS, ROBO_NOT_SUPPORTED,
                              "element has no checkedness");
    }
    res = robo_html_element_input_attributes(element, &attributes, NULL);
    if (res != ROBO_OK) {
        return res;
    }
    if (!robo_html_input_type_is_checkable(attributes.type_effective)) {
        return ROBO_HTML_DIAG(diag, ROBO_HTML_DOMAIN_FORMS, ROBO_NOT_SUPPORTED,
                              "input type has no checkedness");
    }
    return ROBO_OK;
}

robo_result_t robo_html_element_control_value(
    const robo_html_node_handle_t *element, char *out, size_t out_cap,
    size_t *out_length, int *out_is_declared, robo_html_diag_t *diag)
{
    robo_html_document_slot_t *slot;
    robo_html_control_state_t *entry;
    robo_result_t res;
    const char *value;
    size_t length = 0u;

    if (!out || out_cap == 0u) {
        return ROBO_HTML_DIAG(diag, ROBO_HTML_DOMAIN_FORMS,
                              ROBO_INVALID_ARGUMENT, "NULL or zero-capacity out");
    }
    res = value_gate(element, diag);
    if (res != ROBO_OK) {
        return res;
    }
    slot = robo_html_document_slot_find(element->document_id);
    if (slot) {
        entry = control_find(slot, element);
        if (entry && entry->value) {
            if (out_is_declared) {
                *out_is_declared = 0;
            }
            return copy_span(out, out_cap, entry->value, entry->value_length,
                             out_length);
        }
    }
    if (out_is_declared) {
        *out_is_declared = 1;
    }
    /*
     * A textarea has no `value` attribute: its children *are* its default
     * value, which is why the declared read for it is the subtree text (the
     * same call the rest of the runtime uses to read a subtree) and not an
     * attribute lookup that could only ever answer "absent".
     */
    if (robo_html_element_is_tag(element, ROBO_HTML_TAG_TEXTAREA)) {
        return robo_html_node_subtree_text(element, out, out_cap, out_length,
                                           diag);
    }
    value = robo_html_element_attribute_value(element, "value", &length);
    return copy_span(out, out_cap, value, value ? length : 0u, out_length);
}

/**
 * Tag name for a debug line, never NULL.
 *
 * A log line about a control names the control and how much it holds, and
 * deliberately **not** what it holds: a password field's bytes are user data,
 * and a serial log is the last place they belong. Length is the honest, safe
 * fact.
 */
static const char *control_log_name(const robo_html_node_handle_t *element)
{
    const char *name = robo_html_element_name(element, NULL);

    return (name && name[0]) ? name : "?";
}

robo_result_t robo_html_element_set_control_value(
    const robo_html_node_handle_t *element, const char *value, size_t length,
    robo_html_diag_t *diag)
{
    robo_html_document_slot_t *slot;
    robo_html_control_state_t *entry;
    char *copy;
    robo_result_t res;

    if (value == NULL && length != 0u) {
        return ROBO_HTML_DIAG(diag, ROBO_HTML_DOMAIN_FORMS,
                              ROBO_INVALID_ARGUMENT,
                              "NULL value with non-zero length");
    }
    res = value_gate(element, diag);
    if (res != ROBO_OK) {
        return res;
    }
    slot = robo_html_document_slot_find(element->document_id);
    if (!slot) {
        return ROBO_HTML_DIAG(diag, ROBO_HTML_DOMAIN_FORMS, ROBO_NOT_FOUND,
                              "document does not resolve");
    }
    /*
     * Copy even a zero-length value: an empty *override* is a real state that
     * differs from "no override" (the field was cleared, and a reset would
     * bring the declared value back), so it cannot be represented by a NULL
     * pointer. NUL-terminating as well as counting costs one byte and lets the
     * bytes be handed to a C caller without another copy.
     */
    copy = (char *)robo_calloc(1u, length + 1u);
    if (!copy) {
        return ROBO_HTML_DIAG(diag, ROBO_HTML_DOMAIN_FORMS, ROBO_OUT_OF_MEMORY,
                              "control value allocation failed");
    }
    if (length > 0u) {
        memcpy(copy, value, length);
    }
    copy[length] = '\0';

    entry = control_ensure(slot, element, diag);
    if (!entry) {
        robo_free(copy);
        return ROBO_OUT_OF_MEMORY;
    }
    if (entry->value) {
        robo_free(entry->value);
    }
    entry->value = copy;
    entry->value_length = length;
    robo_log_debug("robo_html: control value set doc=%llu tag=%s len=%llu",
                   (unsigned long long)element->document_id,
                   control_log_name(element), (unsigned long long)length);
    return ROBO_OK;
}

robo_result_t robo_html_element_clear_control_value(
    const robo_html_node_handle_t *element, robo_html_diag_t *diag)
{
    robo_html_document_slot_t *slot;
    robo_html_control_state_t *entry;
    robo_result_t res = value_gate(element, diag);
    int had_override = 0;

    if (res != ROBO_OK) {
        return res;
    }
    slot = robo_html_document_slot_find(element->document_id);
    if (!slot) {
        return ROBO_HTML_DIAG(diag, ROBO_HTML_DOMAIN_FORMS, ROBO_NOT_FOUND,
                              "document does not resolve");
    }
    entry = control_find(slot, element);
    had_override = (entry && entry->value) ? 1 : 0;
    if (had_override) {
        robo_free(entry->value);
        entry->value = NULL;
        entry->value_length = 0u;
    }
    control_tidy(slot, control_find_index(slot, element));
    robo_log_debug(
        "robo_html: control value cleared doc=%llu tag=%s had_override=%d",
        (unsigned long long)element->document_id, control_log_name(element),
        had_override);
    /* Absent override is a successful no-op: a form reset must not fail
     * because one field was never edited. */
    return ROBO_OK;
}

robo_result_t robo_html_element_control_checked(
    const robo_html_node_handle_t *element, int *out_checked,
    int *out_is_declared, robo_html_diag_t *diag)
{
    robo_html_document_slot_t *slot;
    robo_html_control_state_t *entry;
    robo_result_t res;

    if (!out_checked) {
        return ROBO_HTML_DIAG(diag, ROBO_HTML_DOMAIN_FORMS,
                              ROBO_INVALID_ARGUMENT, "NULL out");
    }
    res = checked_gate(element, diag);
    if (res != ROBO_OK) {
        return res;
    }
    slot = robo_html_document_slot_find(element->document_id);
    if (slot) {
        entry = control_find(slot, element);
        if (entry && entry->checked_set) {
            if (out_is_declared) {
                *out_is_declared = 0;
            }
            *out_checked = entry->checked;
            return ROBO_OK;
        }
    }
    if (out_is_declared) {
        *out_is_declared = 1;
    }
    *out_checked = robo_html_element_has_attribute(element, "checked");
    return ROBO_OK;
}

robo_result_t robo_html_element_set_control_checked(
    const robo_html_node_handle_t *element, int checked, robo_html_diag_t *diag)
{
    robo_html_document_slot_t *slot;
    robo_html_control_state_t *entry;
    robo_result_t res = checked_gate(element, diag);

    if (res != ROBO_OK) {
        return res;
    }
    slot = robo_html_document_slot_find(element->document_id);
    if (!slot) {
        return ROBO_HTML_DIAG(diag, ROBO_HTML_DOMAIN_FORMS, ROBO_NOT_FOUND,
                              "document does not resolve");
    }
    entry = control_ensure(slot, element, diag);
    if (!entry) {
        return ROBO_OUT_OF_MEMORY;
    }
    entry->checked_set = 1;
    entry->checked = checked ? 1 : 0;
    robo_log_debug("robo_html: control checked set doc=%llu tag=%s checked=%d",
                   (unsigned long long)element->document_id,
                   control_log_name(element), entry->checked);
    return ROBO_OK;
}

robo_result_t robo_html_element_clear_control_checked(
    const robo_html_node_handle_t *element, robo_html_diag_t *diag)
{
    robo_html_document_slot_t *slot;
    robo_html_control_state_t *entry;
    robo_result_t res = checked_gate(element, diag);
    int had_override = 0;

    if (res != ROBO_OK) {
        return res;
    }
    slot = robo_html_document_slot_find(element->document_id);
    if (!slot) {
        return ROBO_HTML_DIAG(diag, ROBO_HTML_DOMAIN_FORMS, ROBO_NOT_FOUND,
                              "document does not resolve");
    }
    entry = control_find(slot, element);
    had_override = (entry && entry->checked_set) ? 1 : 0;
    if (entry) {
        entry->checked_set = 0;
        entry->checked = 0;
    }
    control_tidy(slot, control_find_index(slot, element));
    robo_log_debug(
        "robo_html: control checked cleared doc=%llu tag=%s had_override=%d",
        (unsigned long long)element->document_id, control_log_name(element),
        had_override);
    return ROBO_OK;
}

/* ------------------------------------------------------------------ */
/* <select> / <option> helpers                                         */
/* ------------------------------------------------------------------ */

static int option_predicate(const robo_html_node_view_t *view, void *user)
{
    (void)user;
    return view && view->kind == ROBO_HTML_NODE_KIND_ELEMENT &&
           robo_html_element_is_tag(&view->handle, ROBO_HTML_TAG_OPTION);
}

static int option_selected_predicate(const robo_html_node_view_t *view,
                                     void *user)
{
    (void)user;
    return option_predicate(view, NULL) &&
           robo_html_element_has_attribute(&view->handle, "selected");
}

static int option_enabled_predicate(const robo_html_node_view_t *view,
                                    void *user)
{
    (void)user;
    return option_predicate(view, NULL) &&
           !robo_html_element_has_attribute(&view->handle, "disabled");
}

typedef struct option_count_context {
    size_t count;
} option_count_context_t;

static robo_result_t option_count_visit(const robo_html_node_view_t *view,
                                        size_t depth, void *user)
{
    option_count_context_t *context = (option_count_context_t *)user;

    (void)depth;
    if (option_predicate(view, NULL)) {
        context->count++;
    }
    return ROBO_OK;
}

typedef struct option_index_context {
    const robo_html_node_handle_t *target;
    size_t seen;
    int found;
} option_index_context_t;

static robo_result_t option_index_visit(const robo_html_node_view_t *view,
                                        size_t depth, void *user)
{
    option_index_context_t *context = (option_index_context_t *)user;

    (void)depth;
    if (!option_predicate(view, NULL)) {
        return ROBO_OK;
    }
    if (robo_html_node_handle_equal(&view->handle, context->target)) {
        context->found = 1;
        return ROBO_NOT_FOUND; /* stop the walk; the caller reads the index */
    }
    context->seen++;
    return ROBO_OK;
}

robo_result_t robo_html_element_select_option_count(
    const robo_html_node_handle_t *select, size_t *out_count,
    robo_html_diag_t *diag)
{
    robo_result_t res;
    option_count_context_t context;

    if (!select || !out_count) {
        return ROBO_HTML_DIAG(diag, ROBO_HTML_DOMAIN_FORMS,
                              ROBO_INVALID_ARGUMENT, "NULL select or out");
    }
    *out_count = 0u;
    res = tag_gate(select, ROBO_HTML_TAG_SELECT, "element is not a select",
                   diag);
    if (res != ROBO_OK) {
        return res;
    }
    context.count = 0u;
    res = robo_html_node_walk_descendants(select, ROBO_HTML_NODE_FILTER_ELEMENTS,
                                          option_count_visit, &context, diag);
    if (res != ROBO_OK) {
        return res;
    }
    *out_count = context.count;
    return ROBO_OK;
}

robo_result_t robo_html_element_option_index(
    const robo_html_node_handle_t *option, size_t *out_index,
    robo_html_diag_t *diag)
{
    robo_html_node_handle_t select;
    option_index_context_t context;
    robo_result_t res;

    if (!option || !out_index) {
        return ROBO_HTML_DIAG(diag, ROBO_HTML_DOMAIN_FORMS,
                              ROBO_INVALID_ARGUMENT, "NULL option or out");
    }
    *out_index = 0u;
    res = tag_gate(option, ROBO_HTML_TAG_OPTION, "element is not an option",
                   diag);
    if (res != ROBO_OK) {
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
        robo_html_node_handle_t current = *option;
        size_t guard = 0u;
        int found = 0;

        robo_html_node_handle_clear(&select);
        while (guard++ < ROBO_HTML_MAX_NODES_PER_DOCUMENT) {
            robo_html_node_handle_t parent;

            if (robo_html_element_parent(&current, &parent, NULL) != ROBO_OK) {
                break;
            }
            if (robo_html_element_is_tag(&parent, ROBO_HTML_TAG_SELECT)) {
                select = parent;
                found = 1;
                break;
            }
            current = parent;
        }
        if (!found) {
            return ROBO_HTML_DIAG(diag, ROBO_HTML_DOMAIN_FORMS,
                                  ROBO_NOT_FOUND,
                                  "option is not inside a select");
        }
    }

    context.target = option;
    context.seen = 0u;
    context.found = 0;
    (void)robo_html_node_walk_descendants(&select,
                                          ROBO_HTML_NODE_FILTER_ELEMENTS,
                                          option_index_visit, &context, NULL);
    if (!context.found) {
        return ROBO_HTML_DIAG(diag, ROBO_HTML_DOMAIN_FORMS, ROBO_NOT_FOUND,
                              "option is not a descendant of its select");
    }
    *out_index = context.seen;
    return ROBO_OK;
}

robo_result_t robo_html_element_option_value(
    const robo_html_node_handle_t *option, char *out, size_t out_cap,
    size_t *out_length, int *out_is_explicit, robo_html_diag_t *diag)
{
    const char *value;
    size_t length = 0u;
    robo_result_t res;

    if (!out || out_cap == 0u) {
        return ROBO_HTML_DIAG(diag, ROBO_HTML_DOMAIN_FORMS,
                              ROBO_INVALID_ARGUMENT, "NULL or zero-capacity out");
    }
    res = tag_gate(option, ROBO_HTML_TAG_OPTION, "element is not an option",
                   diag);
    if (res != ROBO_OK) {
        return res;
    }
    value = robo_html_element_attribute_value(option, "value", &length);
    if (value) {
        if (out_is_explicit) {
            *out_is_explicit = 1;
        }
        return copy_span(out, out_cap, value, length, out_length);
    }
    /*
     * The fallback is the real rule: an `<option>Portugal</option>` with no
     * `value` submits "Portugal". The text is taken verbatim rather than
     * whitespace-folded, because folding is a decision for the code that
     * renders or submits a value, not for the code that reads one.
     */
    if (out_is_explicit) {
        *out_is_explicit = 0;
    }
    return robo_html_node_subtree_text(option, out, out_cap, out_length, diag);
}

robo_result_t robo_html_element_select_selected_option(
    const robo_html_node_handle_t *select, robo_html_node_handle_t *out,
    robo_html_diag_t *diag)
{
    robo_result_t res;
    robo_html_node_handle_t subject;

    if (!out || !select) {
        return ROBO_HTML_DIAG(diag, ROBO_HTML_DOMAIN_FORMS,
                              ROBO_INVALID_ARGUMENT, "NULL select or out");
    }
    /* Same aliasing rule as the other resolvers: the caller may pass the
     * select as the output slot. */
    subject = *select;
    select = &subject;
    robo_html_node_handle_clear(out);
    res = tag_gate(select, ROBO_HTML_TAG_SELECT, "element is not a select",
                   diag);
    if (res != ROBO_OK) {
        return res;
    }
    /* First `selected` wins; otherwise the first option that is not disabled,
     * because a select with no `selected` shows a selectable option and a
     * disabled first option is a state the user could never reach. */
    res = robo_html_node_find_descendant(select, ROBO_HTML_NODE_FILTER_ELEMENTS,
                                         option_selected_predicate, NULL, out,
                                         NULL);
    if (res == ROBO_OK) {
        return ROBO_OK;
    }
    if (res != ROBO_NOT_FOUND) {
        return res;
    }
    res = robo_html_node_find_descendant(select, ROBO_HTML_NODE_FILTER_ELEMENTS,
                                         option_enabled_predicate, NULL, out,
                                         NULL);
    if (res == ROBO_NOT_FOUND) {
        robo_html_node_handle_clear(out);
        return ROBO_OK; /* an empty select selects nothing; that is not an error */
    }
    return res;
}

/* ------------------------------------------------------------------ */
/* Form ownership                                                      */
/* ------------------------------------------------------------------ */

robo_result_t robo_html_element_form_owner(
    const robo_html_node_handle_t *control, robo_html_node_handle_t *out,
    robo_html_form_owner_source_t *out_source, robo_html_diag_t *diag)
{
    const char *form_id;
    size_t form_length = 0u;
    robo_html_node_handle_t subject;

    if (!out) {
        return ROBO_HTML_DIAG(diag, ROBO_HTML_DOMAIN_FORMS,
                              ROBO_INVALID_ARGUMENT, "NULL out");
    }
    if (!control) {
        return ROBO_HTML_DIAG(diag, ROBO_HTML_DOMAIN_FORMS,
                              ROBO_INVALID_ARGUMENT, "NULL control");
    }
    /*
     * Work on a copy, so a caller that passes the same handle as @p control
     * and @p out is well defined rather than silently reading a handle this
     * function just cleared. One copy is cheaper than a rule every caller has
     * to know.
     */
    subject = *control;
    control = &subject;
    robo_html_node_handle_clear(out);
    if (out_source) {
        *out_source = ROBO_HTML_FORM_OWNER_SOURCE_NONE;
    }
    if (!robo_html_node_matches_filter(control, ROBO_HTML_NODE_FILTER_ELEMENTS)) {
        return ROBO_HTML_DIAG(diag, ROBO_HTML_DOMAIN_FORMS, ROBO_NOT_FOUND,
                              "control is not a live element");
    }
    if (!robo_html_tag_is_form_associated(robo_html_element_tag(control))) {
        return ROBO_HTML_DIAG(diag, ROBO_HTML_DOMAIN_FORMS, ROBO_NOT_SUPPORTED,
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
    form_id = robo_html_element_attribute_value(control, "form", &form_length);
    if (robo_html_span_is_usable(form_id, form_length)) {
        robo_html_node_handle_t root =
            robo_html_document_root(robo_html_node_handle_document(control));
        robo_html_node_handle_t candidate;

        if (find_element_by_id(&root, form_id, form_length, &candidate) ==
                ROBO_OK &&
            robo_html_element_is_tag(&candidate, ROBO_HTML_TAG_FORM)) {
            *out = candidate;
            if (out_source) {
                *out_source = ROBO_HTML_FORM_OWNER_SOURCE_ATTRIBUTE;
            }
            return ROBO_OK;
        }
    }

    /* Rule 2: the nearest ancestor <form>. */
    if (nearest_ancestor_form(control, out) == ROBO_OK) {
        if (out_source) {
            *out_source = ROBO_HTML_FORM_OWNER_SOURCE_ANCESTOR;
        }
        return ROBO_OK;
    }

    /* Rule 3: no owner. A valid state, not an error. */
    robo_html_node_handle_clear(out);
    return ROBO_OK;
}
