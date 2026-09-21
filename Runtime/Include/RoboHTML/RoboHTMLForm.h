/*
 * RoboHTML forms foundation (Phase 8.14).
 *
 * 8.11 gave `form`, `label`, `input`, `button`, `textarea`, `select` and
 * `option` tag identity, so the parser was already building them before this
 * phase began — `input` was already void and `<textarea>` was already an
 * RCDATA text element. What was missing is everything after that: a control is
 * only interesting because of the *state* its attributes declare, and this
 * header is that state's model.
 *
 *   <form action="/Save" method="post" enctype="multipart/form-data">
 *     <label for="Email">Email</label>
 *     <input id="Email" type="email" name="email" value="a@b" required>
 *     <input type="checkbox" name="news" checked>
 *     <textarea name="note" rows="4">Hello</textarea>
 *     <select name="country">
 *       <option value="pt">Portugal</option>
 *       <option selected>United Kingdom</option>
 *     </select>
 *     <button type="submit">Save</button>
 *   </form>
 *
 *        │
 *        ├── attributes     typed views, read verbatim from the 8.6 model
 *        │      form       action │ method │ enctype │ name │ target
 *        │      label      for
 *        │      input      type │ name │ value │ placeholder │ checked │
 *        │                 disabled │ readonly │ required │ min │ max │ step
 *        │      button     type │ name │ value │ disabled
 *        │      textarea   name │ rows │ cols │ placeholder │ disabled │
 *        │                 readonly │ required
 *        │      select     name │ multiple │ disabled │ required
 *        │      option     value │ selected │ disabled │ label
 *        │
 *        ├── relationships  resolved through the DOM, never cached
 *        │      label → control     `for` by id, else first labelable descendant
 *        │      control → form      `form` by id when it names a <form>,
 *        │                          else the nearest ancestor <form>
 *        │
 *        └── state          declared (attribute) vs current (explicit)
 *               value       generic over input and textarea
 *               checked     input only, and only when checkable
 *
 * Declared and current are two different facts, and the split is the point
 *   HTML does not have one value per control, it has two: the *default* value
 *   the author wrote, and the *current* value the control is actually holding.
 *   `<input value="a">` then `.value = "b"` leaves the attribute saying `a`
 *   and the control saying `b`, and a form reset returns it to `a`. A model
 *   with a single field cannot express that, so this phase keeps the override
 *   in a per-document table and reports which one answered:
 *
 *     HtmlElementControlValue   → the bytes, plus whether they are
 *                                         still the declared ones
 *     HtmlElementSetControlValue → pin a current value
 *     HtmlElementClearControlValue → drop the override, so the value
 *                                         tracks the attribute again
 *
 *   The same shape applies to checkedness, and the two overrides are
 *   independent — setting a value never disturbs checkedness and vice versa,
 *   because in HTML they are separate dirty flags.
 *
 *   The consequence worth stating: for a control that has *not* been
 *   overridden, changing the `value` attribute changes the current value
 *   (it is still tracking it), while for one that has, it does not. That is
 *   not an inconsistency to be tidied away; it is the entire content of the
 *   dirty-value flag, and the suite pins both halves.
 *
 * Nothing is cached, so nothing can go stale
 *   8.14.15 asks that a mutation never leave a stale association, and the
 *   answer is that there is nothing to keep in step. A form owner, a label's
 *   target and a select's selected option are all *derived* on demand from the
 *   live tree: move a control out of a form and its owner changes because the
 *   next lookup walks the new ancestry; remove a form and a control's owner
 *   becomes its next ancestor form, not a dangling reference. The only stored
 *   form state is the two explicit overrides above, and those belong to one
 *   node by identity rather than to a relationship.
 *
 * What this phase is NOT: submission, HTTP, encodings, validation, constraint
 * checking, a file input, a date/time/range/color control, user interaction,
 * `input`/`change`/`submit` events, JavaScript bindings, CSS, layout, or an
 * accessibility tree. A value only ever changes because an API call changed it.
 *
 * See html/docs/Forms.md.
 */

#ifndef ROBO_HTML_FORM_H
#define ROBO_HTML_FORM_H

#include "RoboBase/RoboError.h"
#include "RoboHTML/RoboHTMLDiagnostics.h"
#include "RoboHTML/RoboHTMLNode.h"
#include "RoboHTML/RoboHTMLTypes.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------ */
/* <form> — the element and its attributes                             */
/* ------------------------------------------------------------------ */

/**
 * How a `<form method>` was declared.
 *
 * @c NONE is "no method attribute", which is a different fact from
 * @c UNKNOWN ("a method attribute naming something this runtime does not
 * recognise"). Both would submit as GET in a browser, but that defaulting is a
 * *submission* rule and submission is not this phase, so the model reports
 * what the document said and lets a later phase apply the default.
 */
typedef enum HtmlFormMethod {
    RoboHtmlFormMethodNone = 0,
    RoboHtmlFormMethodGet = 1,
    RoboHtmlFormMethodPost = 2,
    RoboHtmlFormMethodDialog = 3,
    RoboHtmlFormMethodUnknown = 4
} HtmlFormMethod;

/** Non-zero if @p method is a defined enumerator. */
int HtmlFormMethodIsKnown(HtmlFormMethod method);

/** Stable lowercase name of @p method; never NULL, never allocated. */
const char *HtmlFormMethodName(HtmlFormMethod method);

/**
 * Classify a `method` value. Case-insensitive (the attribute is not
 * lowercased on the way in); leading and trailing ASCII whitespace is
 * ignored, because the HTML rule ignores it. An empty or absent value is
 * @c NONE, never @c UNKNOWN.
 */
HtmlFormMethod HtmlFormMethodFromName(const char *value,
                                                        size_t length);

/**
 * How a `<form enctype>` was declared. Same NONE-versus-UNKNOWN split as
 * HtmlFormMethod, and for the same reason.
 */
typedef enum HtmlFormEnctype {
    RoboHtmlFormEnctypeNone = 0,
    RoboHtmlFormEnctypeUrlencoded = 1,
    RoboHtmlFormEnctypeMultipart = 2,
    RoboHtmlFormEnctypeTextPlain = 3,
    RoboHtmlFormEnctypeUnknown = 4
} HtmlFormEnctype;

/** Non-zero if @p enctype is a defined enumerator. */
int HtmlFormEnctypeIsKnown(HtmlFormEnctype enctype);

/** Stable lowercase name of @p enctype; never NULL, never allocated. */
const char *HtmlFormEnctypeName(HtmlFormEnctype enctype);

/**
 * Classify an `enctype` value, by the same rule as
 * HtmlFormMethodFromName: case-insensitive, whitespace-tolerant, and
 * absent or empty is @c NONE.
 */
HtmlFormEnctype HtmlFormEnctypeFromName(const char *value,
                                                          size_t length);

/**
 * The form-relevant attributes of one element, read verbatim.
 *
 * Every string is borrowed from the element and stays valid only while the
 * element's attributes are unchanged — the same rule as
 * HtmlElementAttributeValue, which this is a typed view over.
 *
 * `present` and `usable` are separate facts: `present` means the attribute
 * record exists at all, `usable` means it names something once surrounding
 * ASCII whitespace is ignored. So `<form action>` is present and unusable,
 * `<form action="">` is present and unusable, and `<form>` is neither. That is
 * the distinction a caller needs, and it is why `actionUsable` is reported
 * rather than inferred from a non-zero length.
 *
 * `method` and `enctype` are classified rather than merely borrowed, so a
 * caller never compares strings: the raw value is still exposed for anything
 * the classification discards.
 */
typedef struct HtmlFormAttributes {
    int actionPresent;
    /** Borrowed `action` bytes; NULL when absent. Never resolved, never fetched. */
    const char *action;
    size_t actionLength;
    /** Non-zero when `action` is present and names something (non-blank). */
    int actionUsable;

    int methodPresent;
    /** Borrowed `method` bytes; NULL when absent. */
    const char *method;
    size_t methodLength;
    /** Classified `method`; NONE when absent. */
    HtmlFormMethod methodKind;

    int enctypePresent;
    /** Borrowed `enctype` bytes; NULL when absent. */
    const char *enctype;
    size_t enctypeLength;
    /** Classified `enctype`; NONE when absent. */
    HtmlFormEnctype enctypeKind;

    int namePresent;
    const char *name;
    size_t nameLength;

    int targetPresent;
    const char *target;
    size_t targetLength;
} HtmlFormAttributes;

/**
 * Read the attributes of @p element as a `<form>`.
 *
 * @return RoboOk; RoboInvalidArgument for NULL @p element / @p out;
 *         RoboNotFound when @p element is not a live element;
 *         RoboNotSupported when the element is not a `<form>`. That last
 *         case is a refusal rather than an empty view on purpose: reporting
 *         "no action" for a `<div>` would let a caller mistake "not a form"
 *         for "a form with nothing to do".
 */
Result HtmlElementFormAttributes(
    const HtmlNodeHandle *element,
    HtmlFormAttributes *out,
    HtmlDiag *diag);

/* ------------------------------------------------------------------ */
/* <label> — the labelled control                                      */
/* ------------------------------------------------------------------ */

/**
 * Resolve @p label to the control it labels.
 *
 * The rule, in order, and it is deliberately this small:
 *
 *   1. a usable `for` attribute naming an id → the first element in document
 *      order with that id, *when that element is labelable*
 *      (`HtmlTagIsLabelable`);
 *   2. otherwise the first labelable descendant of the label, in document
 *      order;
 *   3. otherwise there is none, which is a valid state: a `<label>` wrapping
 *      only text labels nothing.
 *
 * This is a stated simplification of the HTML algorithm, not a claim to
 * implement it: the spec's "labelable element" test also excludes
 * `<input type=hidden>`, and the "first element in tree order with that id"
 * search here is the document walk rather than a maintained id index. Both
 * differences are visible in the result rather than hidden behind it.
 *
 * Nothing is cached, so a mutation cannot leave the association stale — move
 * the label, retarget the `for`, or remove the referenced control, and the
 * next call describes the tree as it is now.
 *
 * `for` is not a fallback chain in the other direction: when a usable `for`
 * names an element that is *not* labelable, steps 2 and 3 are not consulted,
 * because the author named something and the honest answer is that the name
 * does not resolve to a control. Use @p outFromAttribute to tell which step
 * answered.
 *
 * @return RoboOk and a resolved @p out; RoboOk with @p out cleared and
 *         @p outFromAttribute 0 when nothing is labelled;
 *         RoboInvalidArgument for NULL @p label / @p out; RoboNotFound
 *         when @p label is not a live element; RoboNotSupported when it is
 *         not a `<label>`.
 */
Result HtmlElementLabelTarget(
    const HtmlNodeHandle *label,
    HtmlNodeHandle *out,
    int *outFromAttribute,
    HtmlDiag *diag);

/* ------------------------------------------------------------------ */
/* <input> — the type vocabulary                                       */
/* ------------------------------------------------------------------ */

/**
 * An input's type.
 *
 * The set is deliberately the one this phase models. Everything else —
 * `range`, `color`, `date`, `time`, `file`, and any future type — classifies
 * as @c UNKNOWN and *degrades* to @c TEXT through
 * HtmlInputTypeEffective, which is exactly what the HTML rule says an
 * unknown type does and exactly what "must degrade safely rather than
 * corrupting the DOM" means here: the element keeps its attributes, its
 * name and its place in the tree, and behaves as the default control.
 *
 * @c TEXT is 0 on purpose. A zeroed view is therefore a text input, which is
 * the HTML default for a missing `type`, so the zero value of this enum and
 * the default of the language agree rather than needing a special case.
 */
typedef enum HtmlInputType {
    /** `text`, and the default when `type` is absent. */
    RoboHtmlInputTypeText = 0,
    RoboHtmlInputTypePassword = 1,
    RoboHtmlInputTypeCheckbox = 2,
    RoboHtmlInputTypeRadio = 3,
    RoboHtmlInputTypeNumber = 4,
    RoboHtmlInputTypeEmail = 5,
    RoboHtmlInputTypeSubmit = 6,
    RoboHtmlInputTypeButton = 7,
    RoboHtmlInputTypeReset = 8,
    RoboHtmlInputTypeSearch = 9,
    RoboHtmlInputTypeHidden = 10,
    /** A `type` naming a type this phase does not model. */
    RoboHtmlInputTypeUnknown = 11
} HtmlInputType;

/** Non-zero if @p type is a defined enumerator (including UNKNOWN). */
int HtmlInputTypeIsKnown(HtmlInputType type);

/** Stable lowercase name of @p type; never NULL, never allocated. */
const char *HtmlInputTypeName(HtmlInputType type);

/**
 * The type a control actually behaves as: @p declared, with @c UNKNOWN
 * mapped to @c TEXT. Never returns @c UNKNOWN.
 */
HtmlInputType HtmlInputTypeEffective(
    HtmlInputType declared);

/**
 * Classify an `input type` value. Case-insensitive, whitespace-tolerant, and
 * an empty or absent value is @c TEXT — the declared default, not @c UNKNOWN,
 * because "the author did not say" and "the author said something we do not
 * know" are different.
 */
HtmlInputType HtmlInputTypeFromName(const char *value,
                                                      size_t length);

/** Non-zero for the types whose checkedness this phase models: checkbox, radio. */
int HtmlInputTypeIsCheckable(HtmlInputType type);

/* ------------------------------------------------------------------ */
/* <input> — attributes and declared state                             */
/* ------------------------------------------------------------------ */

/**
 * The attributes of one `<input>`, read verbatim, plus the numeric ones
 * parsed.
 *
 * `value` here is the *declared* value — the attribute, exactly as written,
 * which is what a form reset would restore. The *current* value is
 * HtmlElementControlValue, and the two differ only for a control that
 * has been overridden.
 *
 * The numeric constraints carry the same present/valid split the image
 * module's dimensions do, and it is what lets a caller tell `<input min>`
 * from `<input min="abc">` from `<input>`:
 *
 *   - `present` means the attribute record exists;
 *   - `_valid` means "and it parsed as a number this model accepts";
 *   - `_value` is meaningful only when `_valid`.
 *
 * An invalid number is reported rather than dropped, because a document that
 * says `min="ten"` has said something, and silently discarding it would hide a
 * real authoring error from every later phase including the one that finally
 * implements validation.
 *
 * The boolean attributes are presence tests, so `<input checked>` and
 * `<input checked="checked">` and `<input checked="">` all report 1 — which is
 * the HTML rule, and the reason these are `int` fields and not pointers.
 */
typedef struct HtmlInputAttributes {
    int typePresent;
    /** Borrowed `type` bytes; NULL when absent. */
    const char *type;
    size_t typeLength;
    /** Classified `type`; TEXT when absent, UNKNOWN when unrecognised. */
    HtmlInputType typeDeclared;
    /** What the control behaves as; never UNKNOWN (see HtmlInputTypeEffective). */
    HtmlInputType typeEffective;
    /** Non-zero when `type` was absent or recognised — i.e. not degraded. */
    int typeRecognised;

    int namePresent;
    const char *name;
    size_t nameLength;

    int valuePresent;
    /** Borrowed *declared* `value` bytes; NULL when absent. */
    const char *value;
    size_t valueLength;

    int placeholderPresent;
    const char *placeholder;
    size_t placeholderLength;

    /** `checked` present — the *default* checkedness, not the current one. */
    int checked;
    int disabled;
    int readonly;
    int required;

    int minPresent;
    const char *min;
    size_t minLength;
    int minValid;
    double minValue;

    int maxPresent;
    const char *max;
    size_t maxLength;
    int maxValid;
    double maxValue;

    int stepPresent;
    const char *step;
    size_t stepLength;
    int stepValid;
    double stepValue;
} HtmlInputAttributes;

/**
 * Read the attributes of @p element as an `<input>`.
 *
 * The `type` is classified; everything else is borrowed or a presence test.
 * No validation is performed and none is implied: `min`/`max`/`step` are
 * parsed as *numbers*, not *compared*, so `<input min="10" max="1">` is
 * reported faithfully rather than flagged.
 *
 * @return RoboOk; RoboInvalidArgument for NULL @p element / @p out;
 *         RoboNotFound when @p element is not a live element;
 *         RoboNotSupported when it is not an `<input>`.
 */
Result HtmlElementInputAttributes(
    const HtmlNodeHandle *element,
    HtmlInputAttributes *out,
    HtmlDiag *diag);

/* ------------------------------------------------------------------ */
/* Control state — declared vs current                                 */
/* ------------------------------------------------------------------ */

/**
 * Read @p element's current value into @p out.
 *
 * The value is the control's *current* one, which is the explicit override if
 * there is one and the declared value otherwise:
 *
 *   - `<input>`  — the declared value is the `value` attribute;
 *   - `<textarea>` — the declared value is the element's own text content
 *     (a textarea has no `value` attribute; its children are its default);
 *   - `<select>`, `<button>`, `<output>` — the declared value is the `value`
 *     attribute, as for an input.
 *
 * @p out is filled the way HtmlNodeSubtreeText fills a buffer:
 * NUL-terminated, truncated to @p outCap - 1 bytes, with @p outLength
 * receiving the *untruncated* byte length so a caller can detect truncation
 * (`*outLength >= outCap` means content was dropped). Nothing is allocated,
 * so a caller's buffer is the whole memory cost.
 *
 * @p outIsDeclared is optional and receives non-zero when the bytes returned
 * are still the declared ones — i.e. no override is in effect. A caller that
 * wants to know "has this control been touched?" asks that question rather
 * than comparing strings, which would confuse "overridden to the same text"
 * with "never overridden".
 *
 * @return RoboOk; RoboInvalidArgument for NULL @p element / NULL or
 *         zero-capacity @p out; RoboNotFound when @p element is not a live
 *         element; RoboNotSupported when it is not a control that holds a
 *         value (see the note on HtmlElementSetControlValue).
 */
Result HtmlElementControlValue(
    const HtmlNodeHandle *element,
    char *out,
    size_t outCap,
    size_t *outLength,
    int *outIsDeclared,
    HtmlDiag *diag);

/**
 * Pin @p element's current value to @p length bytes of @p value.
 *
 * The bytes are copied, so @p value may be a transient buffer, and @p length
 * is authoritative — a NUL inside it is stored like any other byte, which is
 * what keeps this from silently truncating a value the document really has.
 * A NULL @p value is accepted only with @p length 0 (set to the empty string).
 *
 * Setting a value never touches checkedness. It also never touches the
 * element's *attributes*: the `value` attribute keeps saying what the author
 * wrote, so the declared value remains readable and the control can be
 * returned to it with HtmlElementClearControlValue.
 *
 * @return RoboOk; RoboInvalidArgument for NULL @p element or a NULL
 *         @p value with non-zero @p length; RoboNotFound when @p element is
 *         not a live element; RoboNotSupported when it is not an `<input>`,
 *         `<textarea>`, `<select>`, `<button>` or `<output>` — the elements
 *         this phase models as holding a value; RoboOutOfMemory.
 */
Result HtmlElementSetControlValue(
    const HtmlNodeHandle *element,
    const char *value,
    size_t length,
    HtmlDiag *diag);

/**
 * Drop any explicit value override on @p element, so its current value tracks
 * its declared value again.
 *
 * Removing an override that is not there is a successful no-op: this is a
 * "make it not overridden" operation, not a presence test, and a form reset
 * must not fail because one field was never edited. It does not clear
 * checkedness — the two are independent.
 *
 * @return RoboOk; RoboInvalidArgument for NULL @p element; RoboNotFound
 *         when @p element is not a live element; RoboNotSupported when it is
 *         not a control that holds a value.
 */
Result HtmlElementClearControlValue(
    const HtmlNodeHandle *element,
    HtmlDiag *diag);

/**
 * Read @p element's current checkedness.
 *
 * Checkedness is modelled for `<input>` only, and only when the input's
 * *effective* type is checkable (`HtmlInputTypeIsCheckable` — checkbox
 * or radio). Every other element reports RoboNotSupported rather than a
 * default, so `<input type="text" checked>` has an attribute that is preserved
 * and no checkedness to report, which is the honest pair of facts rather than
 * one misleading one.
 *
 * @p outIsDeclared is optional and receives non-zero when the answer is still
 * the `checked` attribute's (see HtmlElementControlValue).
 *
 * @return RoboOk; RoboInvalidArgument for NULL @p element / @p out;
 *         RoboNotFound when @p element is not a live element;
 *         RoboNotSupported for any element or input type without checkedness.
 */
Result HtmlElementControlChecked(
    const HtmlNodeHandle *element,
    int *outChecked,
    int *outIsDeclared,
    HtmlDiag *diag);

/**
 * Pin @p element's checkedness to @p checked (non-zero = checked).
 *
 * Same rules as setting a value: the `checked` attribute is untouched, the
 * override is independent of the value override, and only an `<input>` whose
 * effective type is checkable accepts it.
 *
 * @return RoboOk; RoboInvalidArgument for NULL @p element; RoboNotFound
 *         when @p element is not a live element; RoboNotSupported for any
 *         element or input type without checkedness; RoboOutOfMemory.
 */
Result HtmlElementSetControlChecked(
    const HtmlNodeHandle *element,
    int checked,
    HtmlDiag *diag);

/**
 * Drop any explicit checkedness override on @p element, so it tracks its
 * `checked` attribute again. A successful no-op when there is no override, and
 * it does not clear a value override.
 *
 * @return RoboOk; RoboInvalidArgument for NULL @p element; RoboNotFound
 *         when @p element is not a live element; RoboNotSupported for any
 *         element or input type without checkedness.
 */
Result HtmlElementClearControlChecked(
    const HtmlNodeHandle *element,
    HtmlDiag *diag);

/* ------------------------------------------------------------------ */
/* <button>                                                            */
/* ------------------------------------------------------------------ */

/**
 * What a `<button type>` says it does.
 *
 * @c SUBMIT is 0 on purpose, as with the input types: it is the HTML default
 * for both an absent and an unrecognised `type`, so the zero value, the
 * language default and the degradation all agree.
 *
 * The types are recognised and *preserved*; none of them is performed. There
 * is no submit in this phase, so `SUBMIT` is a description of the element, not
 * a promise that activating it submits anything.
 */
typedef enum HtmlButtonType {
    /** `submit`, and the default when `type` is absent or unrecognised. */
    RoboHtmlButtonTypeSubmit = 0,
    RoboHtmlButtonTypeButton = 1,
    RoboHtmlButtonTypeReset = 2,
    /** A `type` naming a type this phase does not model. */
    RoboHtmlButtonTypeUnknown = 3
} HtmlButtonType;

/** Non-zero if @p type is a defined enumerator (including UNKNOWN). */
int HtmlButtonTypeIsKnown(HtmlButtonType type);

/** Stable lowercase name of @p type; never NULL, never allocated. */
const char *HtmlButtonTypeName(HtmlButtonType type);

/**
 * The type a button actually behaves as: @p declared with @c UNKNOWN mapped to
 * @c SUBMIT. Never returns @c UNKNOWN.
 */
HtmlButtonType HtmlButtonTypeEffective(
    HtmlButtonType declared);

/**
 * Classify a `button type` value. Case-insensitive, whitespace-tolerant, and
 * an empty or absent value is @c SUBMIT.
 */
HtmlButtonType HtmlButtonTypeFromName(const char *value,
                                                        size_t length);

/**
 * The form-relevant attributes of one `<button>`, read verbatim. Same
 * present/borrowed conventions as HtmlFormAttributes; `value` here is
 * the button's *declared* value, and the current one is
 * HtmlElementControlValue.
 */
typedef struct HtmlButtonAttributes {
    int typePresent;
    const char *type;
    size_t typeLength;
    HtmlButtonType typeDeclared;
    HtmlButtonType typeEffective;
    /** Non-zero when `type` was absent or recognised — i.e. not degraded. */
    int typeRecognised;

    int namePresent;
    const char *name;
    size_t nameLength;

    int valuePresent;
    const char *value;
    size_t valueLength;

    int disabled;
} HtmlButtonAttributes;

/**
 * Read the attributes of @p element as a `<button>`.
 *
 * @return RoboOk; RoboInvalidArgument for NULL @p element / @p out;
 *         RoboNotFound when @p element is not a live element;
 *         RoboNotSupported when it is not a `<button>`.
 */
Result HtmlElementButtonAttributes(
    const HtmlNodeHandle *element,
    HtmlButtonAttributes *out,
    HtmlDiag *diag);

/* ------------------------------------------------------------------ */
/* <textarea>                                                          */
/* ------------------------------------------------------------------ */

/** Largest `rows`/`cols` this model accepts; a larger value is invalid rather
 *  than clamped, so a typo cannot silently resize a control. */
#define RoboHtmlFormMaxDeclaredLines 1000000u

/**
 * The attributes of one `<textarea>`, read verbatim, with `rows` and `cols`
 * parsed.
 *
 * There is no `value` field, and that absence is deliberate: a textarea has no
 * `value` attribute in HTML. Its *declared* value is its child text, reachable
 * with HtmlNodeSubtreeText, and its *current* value is
 * HtmlElementControlValue, which reads the children when no override
 * is in effect. Modelling a `value` attribute here would invent something the
 * element does not have.
 *
 * `rows`/`cols` are non-negative integers; `_valid` means "present and a
 * non-negative integer within RoboHtmlFormMaxDeclaredLines". A sign, a
 * fraction, an empty value or anything non-numeric is invalid rather than
 * defaulted.
 */
typedef struct HtmlTextareaAttributes {
    int namePresent;
    const char *name;
    size_t nameLength;

    int rowsPresent;
    const char *rows;
    size_t rowsLength;
    int rowsValid;
    unsigned long rowsValue;

    int colsPresent;
    const char *cols;
    size_t colsLength;
    int colsValid;
    unsigned long colsValue;

    int placeholderPresent;
    const char *placeholder;
    size_t placeholderLength;

    int disabled;
    int readonly;
    int required;
} HtmlTextareaAttributes;

/**
 * Read the attributes of @p element as a `<textarea>`.
 *
 * @return RoboOk; RoboInvalidArgument for NULL @p element / @p out;
 *         RoboNotFound when @p element is not a live element;
 *         RoboNotSupported when it is not a `<textarea>`.
 */
Result HtmlElementTextareaAttributes(
    const HtmlNodeHandle *element,
    HtmlTextareaAttributes *out,
    HtmlDiag *diag);

/* ------------------------------------------------------------------ */
/* <select> and <option>                                               */
/* ------------------------------------------------------------------ */

/**
 * The attributes of one `<select>`, read verbatim. `multiple`, `disabled` and
 * `required` are presence tests.
 */
typedef struct HtmlSelectAttributes {
    int namePresent;
    const char *name;
    size_t nameLength;

    int multiple;
    int disabled;
    int required;
} HtmlSelectAttributes;

/**
 * The attributes of one `<option>`, read verbatim.
 *
 * `selected` is the *default* selectedness — the attribute as written, which is
 * what a form reset restores. The *current* selectedness of an option is not
 * modelled as an override in this phase; it follows the attribute, and
 * changing it is a later phase's operation.
 */
typedef struct HtmlOptionAttributes {
    int valuePresent;
    const char *value;
    size_t valueLength;

    /** `selected` present — the *default* selectedness. */
    int selected;
    int disabled;

    int labelPresent;
    const char *label;
    size_t labelLength;
} HtmlOptionAttributes;

/**
 * Read the attributes of @p element as a `<select>`.
 *
 * @return RoboOk; RoboInvalidArgument for NULL @p element / @p out;
 *         RoboNotFound when @p element is not a live element;
 *         RoboNotSupported when it is not a `<select>`.
 */
Result HtmlElementSelectAttributes(
    const HtmlNodeHandle *element,
    HtmlSelectAttributes *out,
    HtmlDiag *diag);

/**
 * Read the attributes of @p element as an `<option>`.
 *
 * @return RoboOk; RoboInvalidArgument for NULL @p element / @p out;
 *         RoboNotFound when @p element is not a live element;
 *         RoboNotSupported when it is not an `<option>`.
 */
Result HtmlElementOptionAttributes(
    const HtmlNodeHandle *element,
    HtmlOptionAttributes *out,
    HtmlDiag *diag);

/**
 * Number of `<option>` elements inside @p select, in document order.
 *
 * Counted by walking the subtree rather than by inspecting direct children, so
 * an option nested in an `<optgroup>` counts — it is still one of the select's
 * options, and a count that ignored it would disagree with the ordering that
 * HtmlElementOptionIndex reports.
 *
 * @return RoboOk; RoboInvalidArgument for NULL args; RoboNotFound when
 *         @p select is not a live element; RoboNotSupported when it is not a
 *         `<select>`.
 */
Result HtmlElementSelectOptionCount(
    const HtmlNodeHandle *select,
    size_t *outCount,
    HtmlDiag *diag);

/**
 * Position of @p option among its select's options, zero-based, by the same
 * document-order walk HtmlElementSelectOptionCount uses, so an index
 * is always below that count.
 *
 * @return RoboOk; RoboInvalidArgument for NULL args; RoboNotFound when
 *         @p option is not a live option in a live select (including an option
 *         that is not inside a select at all);
 *         RoboNotSupported when @p option is not an `<option>`.
 */
Result HtmlElementOptionIndex(
    const HtmlNodeHandle *option,
    size_t *outIndex,
    HtmlDiag *diag);

/**
 * The *value* of @p option: its `value` attribute when one is present,
 * otherwise its own text content.
 *
 * The fallback is the real HTML rule and the reason a `<option>Portugal</option>`
 * with no `value` still submits something; `outIsExplicit` says which of the
 * two answered, so a caller never has to guess whether the text *is* the value
 * or merely looks like it.
 *
 * The text is the option's subtree text taken verbatim, not whitespace-folded:
 * folding is a text-processing decision that belongs with the code that
 * renders or submits the value, not with the code that reads it. Buffer
 * conventions are those of HtmlElementControlValue.
 *
 * @return RoboOk; RoboInvalidArgument for NULL @p option / NULL or
 *         zero-capacity @p out; RoboNotFound when @p option is not a live
 *         element that resolves; RoboNotSupported when it is not an
 *         `<option>`.
 */
Result HtmlElementOptionValue(
    const HtmlNodeHandle *option,
    char *out,
    size_t outCap,
    size_t *outLength,
    int *outIsExplicit,
    HtmlDiag *diag);

/**
 * The option a `<select>` currently selects.
 *
 * The rule, in order, and it is deterministic rather than a guess:
 *
 *   1. the first option with the `selected` attribute;
 *   2. otherwise the first option that is not `disabled`;
 *   3. otherwise there is none.
 *
 * Step 2 is the HTML "ask for a reset" default expressed as a plain rule: a
 * select with no `selected` shows its first selectable option, and picking the
 * first *disabled* one would be a state the user could never reach. A `multiple`
 * select therefore reports its first selection here rather than a set — this
 * phase models selectedness, not a selection list.
 *
 * @return RoboOk and the option; RoboOk with @p out cleared when the select
 *         has no option (a valid, empty select); RoboInvalidArgument for
 *         NULL @p select / @p out; RoboNotFound when @p select is not a live
 *         element; RoboNotSupported when it is not a `<select>`.
 */
Result HtmlElementSelectSelectedOption(
    const HtmlNodeHandle *select,
    HtmlNodeHandle *out,
    HtmlDiag *diag);

/* ------------------------------------------------------------------ */
/* Form ownership                                                      */
/* ------------------------------------------------------------------ */

/**
 * Which rule found a control's form owner.
 *
 * The source is reported because the two rules have different consequences for
 * a later phase: an owner found by attribute is a deliberate author reference
 * that survives a move, and one found by ancestry is a positional fact that a
 * move changes. Collapsing them would make "why is this input in that form?"
 * unanswerable from the outside.
 */
typedef enum HtmlFormOwnerSource {
    /** The control has no form owner. */
    RoboHtmlFormOwnerSourceNone = 0,
    /** The `form` attribute named a `<form>` by id (wins when it resolves). */
    RoboHtmlFormOwnerSourceAttribute = 1,
    /** The nearest ancestor `<form>`. */
    RoboHtmlFormOwnerSourceAncestor = 2
} HtmlFormOwnerSource;

/** Non-zero if @p source is a defined enumerator. */
int HtmlFormOwnerSourceIsKnown(HtmlFormOwnerSource source);

/** Stable lowercase name of @p source; never NULL, never allocated. */
const char *HtmlFormOwnerSourceName(HtmlFormOwnerSource source);

/**
 * Resolve @p control to the `<form>` that owns it.
 *
 * The rule, in order, and it is a stated simplification of the HTML
 * form-associated-element algorithm rather than a claim to implement it:
 *
 *   1. a usable `form` attribute naming an id → the first element in document
 *      order with that id, when that element is a `<form>`;
 *   2. otherwise the nearest ancestor `<form>`;
 *   3. otherwise there is no owner, which is a valid state — a control outside
 *      any form is an ordinary document, and an unowned control is exactly what
 *      this reports rather than an error.
 *
 * Step 2 is a *fallback*, and the choice is deliberate: when a `form` attribute
 * names something that is missing or is not a form, this model resolves through
 * ancestry instead of refusing. Refusing would silently detach a control that
 * the author clearly meant to belong to the enclosing form, and this phase has
 * no submission to be misled by; the source field records that the attribute
 * did not answer, so a caller that *does* care can tell.
 *
 * The lookup walks the live tree and the ancestor chain on every call, so there
 * is no cache to invalidate: a control moved between forms, a form's id
 * changed, or a form removed all describe themselves correctly the next time
 * they are asked, with no mutation hook and no stale association.
 *
 * Only form-associated elements (`HtmlTagIsFormAssociated`) are
 * accepted: a `<div>` inside a `<form>` is not owned by it, and asking would be
 * a category error rather than a lookup that happens to fail.
 *
 * @return RoboOk and a resolved @p out; RoboOk with @p out cleared and
 *         @p outSource @c NONE when there is no owner;
 *         RoboInvalidArgument for NULL @p control / @p out;
 *         RoboNotFound when @p control is not a live element;
 *         RoboNotSupported when it is not a form-associated element.
 */
Result HtmlElementFormOwner(
    const HtmlNodeHandle *control,
    HtmlNodeHandle *out,
    HtmlFormOwnerSource *outSource,
    HtmlDiag *diag);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_HTML_FORM_H */
