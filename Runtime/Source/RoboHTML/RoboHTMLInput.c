/*
 * RoboHTML input controls — behaviour (Phase 17.3).
 *
 * Reads the contract in RoboHTMLInput.h. These are value/checkedness
 * operations over the Phase 8.14 control model; a caret and a selection are
 * RoboText's editor and are deliberately absent here.
 */

#include "RoboHTML/RoboHTMLInput.h"

#include "RoboHTMLInternal.h"

#include "RoboHTML/RoboHTMLForm.h"
#include "RoboHTML/RoboHTMLFormTree.h"
#include "RoboHTML/RoboHTMLElement.h"
#include "RoboHTML/RoboHTMLTag.h"

#include <stddef.h>
#include <string.h>

#define RoboHtmlInputBuffer 1024u

/* ------------------------------------------------------------------ */
/* Helpers                                                             */
/* ------------------------------------------------------------------ */

void HtmlInputChangeInit(HtmlInputChange *change)
{
    if (change) {
        memset(change, 0, sizeof(*change));
    }
}

static int TypeIsTextEditable(HtmlInputType type)
{
    switch (type) {
    case RoboHtmlInputTypeText:
    case RoboHtmlInputTypePassword:
    case RoboHtmlInputTypeSearch:
    case RoboHtmlInputTypeEmail:
    case RoboHtmlInputTypeNumber:
        return 1;
    default:
        return 0;
    }
}

static int ControlIsExempt(const HtmlNodeHandle *control)
{
    return HtmlElementHasAttribute(control, "disabled") ||
           HtmlElementHasAttribute(control, "readonly");
}

int HtmlInputIsTextEditable(const HtmlNodeHandle *control)
{
    HtmlTag tag;

    if (!HtmlElementIsElement(control)) {
        return 0;
    }
    tag = HtmlElementTag(control);
    if (tag == RoboHtmlTagTextarea) {
        return 1;
    }
    if (tag != RoboHtmlTagInput) {
        return 0;
    }
    {
        HtmlInputAttributes input;

        if (HtmlElementInputAttributes(control, &input, NULL) != RoboOk) {
            return 0;
        }
        return TypeIsTextEditable(input.typeEffective);
    }
}

/* Read current value into @p out (NUL-terminated); returns length. */
static Result ReadValue(const HtmlNodeHandle *control,
                        char *out,
                        size_t capacity,
                        size_t *outLength,
                        HtmlDiag *diag)
{
    size_t length = 0u;
    Result status = HtmlElementControlValue(control, out, capacity, &length,
                                            NULL, diag);

    if (status != RoboOk) {
        return status;
    }
    out[length < capacity ? length : capacity - 1u] = '\0';
    if (outLength) {
        *outLength = length;
    }
    return RoboOk;
}

static void ReportValueChange(HtmlInputChange *out, const char *value)
{
    if (!out) {
        return;
    }
    HtmlInputChangeInit(out);
    out->changed = 1;
    out->kind = RoboHtmlInputChangeValue;
    out->valueLength = strlen(value);
}

/* ------------------------------------------------------------------ */
/* Text editing                                                        */
/* ------------------------------------------------------------------ */

static Result RequireTextEditable(const HtmlNodeHandle *control,
                                  HtmlDiag *diag)
{
    if (!HtmlElementIsElement(control)) {
        return RoboHtmlDiag(diag, RoboHtmlDomainForms, RoboNotFound,
                              "the node is not a live element");
    }
    if (!HtmlInputIsTextEditable(control)) {
        return RoboHtmlDiag(diag, RoboHtmlDomainForms, RoboNotSupported,
                              "the control does not accept text");
    }
    if (ControlIsExempt(control)) {
        return RoboHtmlDiag(diag, RoboHtmlDomainForms, RoboNotSupported,
                              "the control is disabled or read-only");
    }
    return RoboOk;
}

Result HtmlInputInsertText(
    const HtmlNodeHandle *control,
    const char *text,
    size_t length,
    HtmlInputChange *out,
    HtmlDiag *diag)
{
    char buffer[RoboHtmlInputBuffer];
    size_t current = 0u;
    Result status;

    if (out) {
        HtmlInputChangeInit(out);
    }
    if (!control || (!text && length > 0u)) {
        return RoboHtmlDiag(diag, RoboHtmlDomainForms, RoboInvalidArgument,
                              "a control and text bytes are required");
    }
    status = RequireTextEditable(control, diag);
    if (status != RoboOk) {
        return status;
    }
    status = ReadValue(control, buffer, sizeof(buffer), &current, diag);
    if (status != RoboOk) {
        return status;
    }
    if (current + length + 1u > sizeof(buffer)) {
        return RoboHtmlDiag(diag, RoboHtmlDomainForms, RoboNotSupported,
                              "the edited value would exceed this build's bound");
    }
    if (length > 0u) {
        memcpy(buffer + current, text, length);
    }
    buffer[current + length] = '\0';
    status = HtmlElementSetControlValue(control, buffer, current + length, diag);
    if (status != RoboOk) {
        return status;
    }
    ReportValueChange(out, buffer);
    return RoboOk;
}

Result HtmlInputDeleteBackward(
    const HtmlNodeHandle *control,
    HtmlInputChange *out,
    HtmlDiag *diag)
{
    char buffer[RoboHtmlInputBuffer];
    size_t current = 0u;
    Result status;

    if (out) {
        HtmlInputChangeInit(out);
    }
    if (!control) {
        return RoboHtmlDiag(diag, RoboHtmlDomainForms, RoboInvalidArgument,
                              "a control is required");
    }
    status = RequireTextEditable(control, diag);
    if (status != RoboOk) {
        return status;
    }
    status = ReadValue(control, buffer, sizeof(buffer), &current, diag);
    if (status != RoboOk) {
        return status;
    }
    if (current == 0u) {
        return RoboOk; /* nothing to delete is a no-op, not a failure */
    }
    /* Back up over one UTF-8 scalar: continuation bytes are 10xxxxxx. */
    {
        size_t cut = current - 1u;

        while (cut > 0u && (buffer[cut] & 0xC0) == 0x80) {
            cut--;
        }
        buffer[cut] = '\0';
        status = HtmlElementSetControlValue(control, buffer, cut, diag);
    }
    if (status != RoboOk) {
        return status;
    }
    ReportValueChange(out, buffer);
    return RoboOk;
}

Result HtmlInputClear(
    const HtmlNodeHandle *control,
    HtmlInputChange *out,
    HtmlDiag *diag)
{
    Result status;

    if (out) {
        HtmlInputChangeInit(out);
    }
    if (!control) {
        return RoboHtmlDiag(diag, RoboHtmlDomainForms, RoboInvalidArgument,
                              "a control is required");
    }
    status = RequireTextEditable(control, diag);
    if (status != RoboOk) {
        return status;
    }
    status = HtmlElementSetControlValue(control, "", 0u, diag);
    if (status != RoboOk) {
        return status;
    }
    ReportValueChange(out, "");
    return RoboOk;
}

/* ------------------------------------------------------------------ */
/* Checkbox / radio                                                    */
/* ------------------------------------------------------------------ */

static Result RequireCheckable(const HtmlNodeHandle *control,
                               HtmlInputType *outType,
                               HtmlDiag *diag)
{
    HtmlInputAttributes input;

    if (!HtmlElementIsElement(control)) {
        return RoboHtmlDiag(diag, RoboHtmlDomainForms, RoboNotFound,
                              "the node is not a live element");
    }
    if (HtmlElementTag(control) != RoboHtmlTagInput) {
        return RoboHtmlDiag(diag, RoboHtmlDomainForms, RoboNotSupported,
                              "the control has no checkedness");
    }
    if (HtmlElementInputAttributes(control, &input, diag) != RoboOk) {
        return diag ? diag->code : RoboNotFound;
    }
    if (input.typeEffective != RoboHtmlInputTypeCheckbox &&
        input.typeEffective != RoboHtmlInputTypeRadio) {
        return RoboHtmlDiag(diag, RoboHtmlDomainForms, RoboNotSupported,
                              "the control has no checkedness");
    }
    if (outType) {
        *outType = input.typeEffective;
    }
    return RoboOk;
}

Result HtmlInputSetChecked(
    const HtmlNodeHandle *control,
    int checked,
    HtmlInputChange *out,
    HtmlDiag *diag)
{
    HtmlInputType type = RoboHtmlInputTypeText;
    int previous = 0;
    Result status;

    if (out) {
        HtmlInputChangeInit(out);
    }
    if (!control) {
        return RoboHtmlDiag(diag, RoboHtmlDomainForms, RoboInvalidArgument,
                              "a control is required");
    }
    status = RequireCheckable(control, &type, diag);
    if (status != RoboOk) {
        return status;
    }
    if (HtmlElementHasAttribute(control, "disabled")) {
        return RoboHtmlDiag(diag, RoboHtmlDomainForms, RoboNotSupported,
                              "the control is disabled");
    }
    (void)type;
    if (HtmlElementControlChecked(control, &previous, NULL, NULL) != RoboOk) {
        previous = 0;
    }
    status = HtmlElementSetControlChecked(control, checked ? 1 : 0, diag);
    if (status != RoboOk) {
        return status;
    }
    if (out) {
        out->changed = (previous != (checked ? 1 : 0)) ? 1 : 0;
        out->kind = out->changed ? RoboHtmlInputChangeChecked
                                 : RoboHtmlInputChangeNone;
        out->checked = checked ? 1 : 0;
    }
    return RoboOk;
}

Result HtmlInputToggleChecked(
    const HtmlNodeHandle *control,
    HtmlInputChange *out,
    HtmlDiag *diag)
{
    int checked = 0;

    if (!control) {
        return RoboHtmlDiag(diag, RoboHtmlDomainForms, RoboInvalidArgument,
                              "a control is required");
    }
    {
        Result status = HtmlElementControlChecked(control, &checked, NULL,
                                                  diag);

        if (status != RoboOk) {
            return status;
        }
    }
    return HtmlInputSetChecked(control, !checked, out, diag);
}

/* Radio group: same `name` in the same form owner. */
typedef struct RadioGroupScan {
    const HtmlNodeHandle *selected;
    const char *name;
    size_t cleared;
} RadioGroupScan;

static Result RadioClearVisit(const HtmlNodeView *view, size_t depth, void *user)
{
    RadioGroupScan *scan = (RadioGroupScan *)user;
    HtmlInputAttributes input;
    const char *name;

    (void)depth;
    if (HtmlNodeHandleEqual(&view->handle, scan->selected)) {
        return RoboOk;
    }
    if (HtmlElementTag(&view->handle) != RoboHtmlTagInput) {
        return RoboOk;
    }
    if (HtmlElementInputAttributes(&view->handle, &input, NULL) != RoboOk ||
        input.typeEffective != RoboHtmlInputTypeRadio) {
        return RoboOk;
    }
    name = HtmlElementAttributeValue(&view->handle, "name", NULL);
    if (!name || strcmp(name, scan->name) != 0) {
        return RoboOk;
    }
    {
        int checked = 0;

        if (HtmlElementControlChecked(&view->handle, &checked, NULL, NULL) ==
                RoboOk &&
            checked) {
            if (HtmlElementSetControlChecked(&view->handle, 0, NULL) ==
                RoboOk) {
                scan->cleared++;
            }
        }
    }
    return RoboOk;
}

Result HtmlInputRadioSelect(
    const HtmlNodeHandle *control,
    HtmlInputChange *out,
    HtmlDiag *diag)
{
    HtmlInputType type = RoboHtmlInputTypeText;
    const char *name;
    HtmlNodeHandle owner;
    int checked = 0;
    Result status;

    if (out) {
        HtmlInputChangeInit(out);
    }
    if (!control) {
        return RoboHtmlDiag(diag, RoboHtmlDomainForms, RoboInvalidArgument,
                              "a control is required");
    }
    status = RequireCheckable(control, &type, diag);
    if (status != RoboOk) {
        return status;
    }
    if (type != RoboHtmlInputTypeRadio) {
        return RoboHtmlDiag(diag, RoboHtmlDomainForms, RoboNotSupported,
                              "the control is not a radio");
    }
    if (HtmlElementHasAttribute(control, "disabled")) {
        return RoboHtmlDiag(diag, RoboHtmlDomainForms, RoboNotSupported,
                              "the control is disabled");
    }
    if (HtmlElementControlChecked(control, &checked, NULL, NULL) != RoboOk) {
        checked = 0;
    }
    status = HtmlElementSetControlChecked(control, 1, diag);
    if (status != RoboOk) {
        return status;
    }
    name = HtmlElementAttributeValue(control, "name", NULL);
    if (name && name[0] != '\0') {
        RadioGroupScan scan;

        scan.selected = control;
        scan.name = name;
        scan.cleared = 0u;
        HtmlNodeHandleClear(&owner);
        if (HtmlElementFormOwner(control, &owner, NULL, NULL) != RoboOk) {
            HtmlNodeHandleClear(&owner);
        }
        if (HtmlNodeHandleIsWellformed(&owner)) {
            (void)HtmlFormGetElements(&owner, RadioClearVisit, &scan, NULL, NULL);
        } else {
            HtmlNodeHandle root = HtmlDocumentRoot(control->documentId);

            if (HtmlNodeHandleIsWellformed(&root)) {
                (void)HtmlNodeWalkDescendants(&root, RoboHtmlNodeFilterElements,
                                              RadioClearVisit, &scan, NULL);
            }
        }
        if (out) {
            out->groupCleared = scan.cleared;
        }
    }
    if (out) {
        out->changed = (!checked) ? 1 : 0;
        out->kind = out->changed ? RoboHtmlInputChangeChecked
                                 : RoboHtmlInputChangeNone;
        out->checked = 1;
    }
    return RoboOk;
}

/* ------------------------------------------------------------------ */
/* Stepping                                                            */
/* ------------------------------------------------------------------ */

static void FormatNumber(double value, char *out, size_t capacity)
{
    int negative = (value < 0.0);
    double magnitude = negative ? -value : value;
    long long whole = (long long)magnitude;
    double fraction = magnitude - (double)whole;
    size_t used = 0u;

    if (negative) {
        out[used++] = '-';
    }
    /* Whole part (bounded to fit). */
    {
        char digits[32];
        int count = 0;

        if (whole == 0) {
            digits[count++] = '0';
        }
        while (whole > 0 && count < (int)sizeof(digits)) {
            digits[count++] = (char)('0' + (whole % 10));
            whole /= 10;
        }
        while (count > 0 && used + 1u < capacity) {
            out[used++] = digits[--count];
        }
    }
    if (fraction > 0.0005) {
        int decimals = 3;

        if (used + 1u < capacity) {
            out[used++] = '.';
        }
        while (decimals-- > 0 && used + 1u < capacity) {
            int digit;

            fraction *= 10.0;
            digit = (int)fraction;
            out[used++] = (char)('0' + digit);
            fraction -= (double)digit;
        }
        /* Trim trailing zeros (and a bare dot). */
        while (used > 0u && out[used - 1u] == '0') {
            used--;
        }
        if (used > 0u && out[used - 1u] == '.') {
            used--;
        }
    }
    if (used + 1u > capacity) {
        used = capacity - 1u;
    }
    out[used] = '\0';
}

Result HtmlInputStep(
    const HtmlNodeHandle *control,
    int steps,
    HtmlInputChange *out,
    HtmlDiag *diag)
{
    HtmlInputAttributes input;
    char buffer[RoboHtmlInputBuffer];
    size_t length = 0u;
    double current = 0.0;
    double step;
    double next;
    char formatted[64];
    Result status;

    if (out) {
        HtmlInputChangeInit(out);
    }
    if (!control) {
        return RoboHtmlDiag(diag, RoboHtmlDomainForms, RoboInvalidArgument,
                              "a control is required");
    }
    if (!HtmlElementIsElement(control)) {
        return RoboHtmlDiag(diag, RoboHtmlDomainForms, RoboNotFound,
                              "the node is not a live element");
    }
    if (HtmlElementInputAttributes(control, &input, diag) != RoboOk) {
        return diag ? diag->code : RoboNotFound;
    }
    if (input.typeEffective != RoboHtmlInputTypeNumber) {
        return RoboHtmlDiag(diag, RoboHtmlDomainForms, RoboNotSupported,
                              "the control is not a number input");
    }
    if (ControlIsExempt(control)) {
        return RoboHtmlDiag(diag, RoboHtmlDomainForms, RoboNotSupported,
                              "the control is disabled or read-only");
    }
    status = ReadValue(control, buffer, sizeof(buffer), &length, diag);
    if (status != RoboOk) {
        return status;
    }
    if (length > 0u && !HtmlParseNumber(buffer, length, &current)) {
        return RoboHtmlDiag(diag, RoboHtmlDomainForms, RoboParseError,
                              "the current value is not a number");
    }
    step = (input.stepPresent && input.stepValid && input.stepValue > 0.0)
               ? input.stepValue
               : 1.0;
    next = current + step * (double)steps;
    if (input.minPresent && input.minValid && next < input.minValue) {
        next = input.minValue;
    }
    if (input.maxPresent && input.maxValid && next > input.maxValue) {
        next = input.maxValue;
    }
    FormatNumber(next, formatted, sizeof(formatted));
    status = HtmlElementSetControlValue(control, formatted,
                                        strlen(formatted), diag);
    if (status != RoboOk) {
        return status;
    }
    ReportValueChange(out, formatted);
    return RoboOk;
}

/* ------------------------------------------------------------------ */
/* Capabilities                                                        */
/* ------------------------------------------------------------------ */

Result HtmlInputGetCapabilities(HtmlInputCapabilities *out)
{
    if (!out) {
        return RoboInvalidArgument;
    }
    out->textEditing = 1;
    out->checkbox = 1;
    out->radioGroups = 1;
    out->stepping = 1;
    out->caretModel = 0;
    out->eventDispatch = 0;
    return RoboOk;
}
