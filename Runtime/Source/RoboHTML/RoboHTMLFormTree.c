/*
 * RoboHTML forms — association, validation, submission and reset (Phase 17.2).
 *
 * Reads the contract in RoboHTMLFormTree.h. Everything is a read or an explicit
 * state change over the model Phase 8.14 built: association is resolved live
 * (never cached), validation is the stated constraint set, and submission is a
 * request routed through one installed handler.
 */

#include "RoboHTML/RoboHTMLFormTree.h"
#include "RoboHTML/RoboHTMLElement.h"
#include "RoboHTML/RoboHTMLTag.h"

#include "RoboHTMLInternal.h"

#include <stddef.h>
#include <string.h>

#define RoboHtmlValidateBuffer 512u

/* ------------------------------------------------------------------ */
/* Small helpers                                                       */
/* ------------------------------------------------------------------ */

int HtmlParseNumber(const char *text, size_t length, double *out)
{
    size_t i = 0u;
    int negative = 0;
    double value = 0.0;
    int digits = 0;

    if (!text || length == 0u) {
        return 0;
    }
    if (text[i] == '+' || text[i] == '-') {
        negative = (text[i] == '-');
        i++;
    }
    while (i < length && text[i] >= '0' && text[i] <= '9') {
        value = value * 10.0 + (double)(text[i] - '0');
        i++;
        digits++;
    }
    if (i < length && text[i] == '.') {
        double scale = 0.1;
        i++;
        while (i < length && text[i] >= '0' && text[i] <= '9') {
            value += (double)(text[i] - '0') * scale;
            scale *= 0.1;
            i++;
            digits++;
        }
    }
    if (digits == 0) {
        return 0;
    }
    if (i < length && (text[i] == 'e' || text[i] == 'E')) {
        int expNegative = 0;
        int exponent = 0;
        int expDigits = 0;

        i++;
        if (i < length && (text[i] == '+' || text[i] == '-')) {
            expNegative = (text[i] == '-');
            i++;
        }
        while (i < length && text[i] >= '0' && text[i] <= '9') {
            exponent = exponent * 10 + (text[i] - '0');
            i++;
            expDigits++;
        }
        if (expDigits == 0) {
            return 0;
        }
        while (exponent-- > 0) {
            if (expNegative) {
                value /= 10.0;
            } else {
                value *= 10.0;
            }
        }
    }
    if (i != length) {
        return 0;
    }
    *out = negative ? -value : value;
    return 1;
}

/* An attribute parsed as a non-negative integer, or 0 when absent/invalid. */
static size_t ParseCountAttribute(const HtmlNodeHandle *element,
                                  const char *name)
{
    const char *value = HtmlElementAttributeValue(element, name, NULL);
    size_t result = 0u;
    size_t i = 0u;

    if (!value) {
        return 0u;
    }
    while (value[i] >= '0' && value[i] <= '9') {
        result = result * 10u + (size_t)(value[i] - '0');
        i++;
    }
    return (i == 0u) ? 0u : result;
}

static int ReadControlValue(const HtmlNodeHandle *control,
                            char *out,
                            size_t capacity,
                            size_t *outLength,
                            HtmlDiag *diag)
{
    return HtmlElementControlValue(control, out, capacity, outLength, NULL, diag);
}

/* ------------------------------------------------------------------ */
/* Association                                                         */
/* ------------------------------------------------------------------ */

typedef struct FormElementScan {
    const HtmlNodeHandle *form;
    HtmlNodeVisitFn visit;
    void *visitUser;
    size_t count;
} FormElementScan;

static Result FormElementVisit(const HtmlNodeView *view, size_t depth, void *user)
{
    FormElementScan *scan = (FormElementScan *)user;
    HtmlNodeHandle owner;

    if (!HtmlTagIsFormAssociated(HtmlElementTag(&view->handle))) {
        return RoboOk;
    }
    if (HtmlElementFormOwner(&view->handle, &owner, NULL, NULL) != RoboOk) {
        return RoboOk;
    }
    if (!HtmlNodeHandleIsWellformed(&owner) ||
        !HtmlNodeHandleEqual(&owner, scan->form)) {
        return RoboOk;
    }
    scan->count++;
    if (scan->visit) {
        return scan->visit(view, depth, scan->visitUser);
    }
    return RoboOk;
}

Result HtmlFormGetElements(
    const HtmlNodeHandle *form,
    HtmlNodeVisitFn visit,
    void *user,
    size_t *outCount,
    HtmlDiag *diag)
{
    FormElementScan scan;
    HtmlNodeHandle root;

    if (outCount) {
        *outCount = 0u;
    }
    if (!form) {
        return RoboHtmlDiag(diag, RoboHtmlDomainForms, RoboInvalidArgument,
                              "a form is required");
    }
    if (!HtmlElementIsElement(form)) {
        return RoboHtmlDiag(diag, RoboHtmlDomainForms, RoboNotFound,
                              "the node is not a live element");
    }
    if (!HtmlElementIsNamed(form, "form")) {
        return RoboHtmlDiag(diag, RoboHtmlDomainForms, RoboNotSupported,
                              "the element is not a form");
    }
    root = HtmlDocumentRoot(form->documentId);
    if (!HtmlNodeHandleIsWellformed(&root)) {
        return RoboHtmlDiag(diag, RoboHtmlDomainDocument, RoboNotFound,
                              "unknown or destroyed document");
    }
    scan.form = form;
    scan.visit = visit;
    scan.visitUser = user;
    scan.count = 0u;
    {
        Result status = HtmlNodeWalkDescendants(&root, RoboHtmlNodeFilterElements,
                                                FormElementVisit, &scan, diag);

        if (status != RoboOk) {
            return status;
        }
    }
    if (outCount) {
        *outCount = scan.count;
    }
    return RoboOk;
}

/* ------------------------------------------------------------------ */
/* Validation                                                          */
/* ------------------------------------------------------------------ */

static int BasicEmailValid(const char *text, size_t length)
{
    size_t at = (size_t)-1;
    size_t i;
    int dotAfter = 0;

    for (i = 0u; i < length; i++) {
        if (text[i] == '@') {
            if (at != (size_t)-1) {
                return 0;
            }
            at = i;
        } else if (text[i] == ' ') {
            return 0;
        }
    }
    if (at == (size_t)-1 || at == 0u || at + 1u >= length) {
        return 0;
    }
    for (i = at + 1u; i < length; i++) {
        if (text[i] == '.') {
            dotAfter = 1;
        }
    }
    return dotAfter;
}

Result HtmlElementValidateControl(
    const HtmlNodeHandle *control,
    HtmlControlValidation *out,
    HtmlDiag *diag)
{
    HtmlTag tag;

    if (!control || !out) {
        return RoboHtmlDiag(diag, RoboHtmlDomainForms, RoboInvalidArgument,
                              "a control and an output are required");
    }
    memset(out, 0, sizeof(*out));
    out->valid = 1;
    if (!HtmlElementIsElement(control)) {
        return RoboHtmlDiag(diag, RoboHtmlDomainForms, RoboNotFound,
                              "the node is not a live element");
    }
    tag = HtmlElementTag(control);
    if (!HtmlTagIsFormAssociated(tag)) {
        return RoboHtmlDiag(diag, RoboHtmlDomainForms, RoboNotSupported,
                              "the element is not a form control");
    }
    /* Disabled controls are barred from constraint validation. */
    if (HtmlElementHasAttribute(control, "disabled")) {
        return RoboOk;
    }
    if (tag == RoboHtmlTagInput) {
        HtmlInputAttributes input;

        if (HtmlElementInputAttributes(control, &input, diag) != RoboOk) {
            return diag ? diag->code : RoboNotFound;
        }
        if (input.readonly) {
            return RoboOk;
        }
        if (input.typeEffective == RoboHtmlInputTypeCheckbox ||
            input.typeEffective == RoboHtmlInputTypeRadio) {
            int checked = 0;

            out->validated = 1;
            if (HtmlElementControlChecked(control, &checked, NULL, diag) ==
                RoboOk) {
                if (input.required && !checked) {
                    out->valueMissing = 1;
                    out->valid = 0;
                }
            }
            return RoboOk;
        }
        if (input.typeEffective == RoboHtmlInputTypeSubmit ||
            input.typeEffective == RoboHtmlInputTypeButton ||
            input.typeEffective == RoboHtmlInputTypeReset ||
            input.typeEffective == RoboHtmlInputTypeHidden) {
            return RoboOk;
        }
        {
            char value[RoboHtmlValidateBuffer];
            size_t length = 0u;

            if (ReadControlValue(control, value, sizeof(value), &length, diag) !=
                RoboOk) {
                return diag ? diag->code : RoboNotFound;
            }
            out->validated = 1;
            if (input.required && length == 0u) {
                out->valueMissing = 1;
            }
            if (input.typeEffective == RoboHtmlInputTypeNumber && length > 0u) {
                double parsed = 0.0;

                if (!HtmlParseNumber(value, length, &parsed)) {
                    out->typeMismatch = 1;
                } else {
                    if (input.minPresent && input.minValid && parsed < input.minValue) {
                        out->belowMin = 1;
                    }
                    if (input.maxPresent && input.maxValid && parsed > input.maxValue) {
                        out->aboveMax = 1;
                    }
                }
            }
            if (input.typeEffective == RoboHtmlInputTypeEmail && length > 0u &&
                !BasicEmailValid(value, length)) {
                out->typeMismatch = 1;
            }
        }
    } else if (tag == RoboHtmlTagTextarea) {
        char value[RoboHtmlValidateBuffer];
        size_t length = 0u;

        if (ReadControlValue(control, value, sizeof(value), &length, diag) !=
            RoboOk) {
            return diag ? diag->code : RoboNotFound;
        }
        out->validated = 1;
        if (HtmlElementHasAttribute(control, "required") && length == 0u) {
            out->valueMissing = 1;
        }
    } else if (tag == RoboHtmlTagSelect) {
        HtmlNodeHandle selected;

        out->validated = 1;
        if (HtmlElementSelectSelectedOption(control, &selected, diag) ==
                RoboOk &&
            !HtmlNodeHandleIsWellformed(&selected) &&
            HtmlElementHasAttribute(control, "required")) {
            out->valueMissing = 1;
        }
    } else {
        /* button/output/fieldset: no constraint checks modelled. */
        return RoboOk;
    }

    /* minlength/maxlength apply to the text-bearing controls. */
    if (out->validated) {
        size_t minLength = ParseCountAttribute(control, "minlength");
        size_t maxLength = ParseCountAttribute(control, "maxlength");

        if (minLength > 0u || maxLength > 0u) {
            char value[RoboHtmlValidateBuffer];
            size_t length = 0u;

            if (ReadControlValue(control, value, sizeof(value), &length, diag) ==
                RoboOk) {
                if (minLength > 0u && length < minLength) {
                    out->tooShort = 1;
                }
                if (maxLength > 0u && length > maxLength) {
                    out->tooLong = 1;
                }
            }
        }
    }

    if (HtmlElementHasAttribute(control, "pattern")) {
        out->patternUnsupported = 1;
    }
    out->valid = (out->valueMissing || out->tooShort || out->tooLong ||
                  out->belowMin || out->aboveMax || out->typeMismatch)
                     ? 0
                     : 1;
    return RoboOk;
}

/* ------------------------------------------------------------------ */
/* form.validate                                                       */
/* ------------------------------------------------------------------ */

typedef struct FormValidationScan {
    HtmlFormValidation *report;
} FormValidationScan;

static Result FormValidationVisit(const HtmlNodeView *view, size_t depth,
                                  void *user)
{
    FormValidationScan *scan = (FormValidationScan *)user;
    HtmlControlValidation control;

    (void)depth;
    scan->report->controls++;
    if (HtmlElementValidateControl(&view->handle, &control, NULL) != RoboOk) {
        return RoboOk;
    }
    if (control.validated) {
        scan->report->validated++;
        if (!control.valid) {
            scan->report->invalid++;
        }
    }
    if (control.patternUnsupported) {
        scan->report->unsupported++;
    }
    return RoboOk;
}

Result HtmlFormValidate(
    const HtmlNodeHandle *form,
    HtmlFormValidation *out,
    HtmlDiag *diag)
{
    FormValidationScan scan;

    if (!form || !out) {
        return RoboHtmlDiag(diag, RoboHtmlDomainForms, RoboInvalidArgument,
                              "a form and an output are required");
    }
    if (!HtmlElementIsElement(form)) {
        return RoboHtmlDiag(diag, RoboHtmlDomainForms, RoboNotFound,
                              "the node is not a live element");
    }
    if (!HtmlElementIsNamed(form, "form")) {
        return RoboHtmlDiag(diag, RoboHtmlDomainForms, RoboNotSupported,
                              "the element is not a form");
    }
    memset(out, 0, sizeof(*out));
    out->valid = 1;
    scan.report = out;
    {
        Result status = HtmlFormGetElements(form, FormValidationVisit, &scan,
                                            NULL, diag);

        if (status != RoboOk) {
            return status;
        }
    }
    out->valid = (out->invalid == 0u) ? 1 : 0;
    return RoboOk;
}

/* ------------------------------------------------------------------ */
/* Submission                                                          */
/* ------------------------------------------------------------------ */

Result HtmlDocumentSetFormSubmitHandler(
    HtmlDocumentId documentId,
    HtmlFormSubmitFn handler,
    void *userdata,
    HtmlDiag *diag)
{
    HtmlDocumentSlot *slot = HtmlDocumentSlotFind(documentId);

    if (!slot) {
        return RoboHtmlDiag(diag, RoboHtmlDomainDocument, RoboNotFound,
                              "unknown or destroyed document");
    }
    slot->formSubmit = handler;
    slot->formSubmitUserdata = handler ? userdata : NULL;
    return RoboOk;
}

Result HtmlFormSubmit(
    const HtmlNodeHandle *form,
    HtmlSubmitOutcome *out,
    HtmlDiag *diag)
{
    HtmlFormValidation validation;
    HtmlDocumentSlot *slot;
    HtmlFormAttributes attributes;
    Result status;

    if (!form || !out) {
        return RoboHtmlDiag(diag, RoboHtmlDomainForms, RoboInvalidArgument,
                              "a form and an output are required");
    }
    memset(out, 0, sizeof(*out));
    if (!HtmlElementIsElement(form)) {
        return RoboHtmlDiag(diag, RoboHtmlDomainForms, RoboNotFound,
                              "the node is not a live element");
    }
    if (!HtmlElementIsNamed(form, "form")) {
        return RoboHtmlDiag(diag, RoboHtmlDomainForms, RoboNotSupported,
                              "the element is not a form");
    }
    status = HtmlFormValidate(form, &validation, diag);
    if (status != RoboOk) {
        return status;
    }
    out->validated = 1;
    out->valid = validation.valid;
    out->invalidControls = validation.invalid;
    if (!validation.valid) {
        /* A blocked submission is a successful *call* with a negative answer. */
        return RoboOk;
    }
    slot = HtmlDocumentSlotFind(form->documentId);
    if (!slot || !slot->formSubmit) {
        return RoboHtmlDiag(diag, RoboHtmlDomainForms, RoboNotSupported,
                              "no submission handler is installed");
    }
    if (HtmlElementFormAttributes(form, &attributes, diag) != RoboOk) {
        return diag ? diag->code : RoboNotFound;
    }
    status = slot->formSubmit(form, &attributes, validation.controls,
                              slot->formSubmitUserdata, diag);
    if (status != RoboOk) {
        return status;
    }
    out->requested = 1;
    return RoboOk;
}

/* ------------------------------------------------------------------ */
/* Reset                                                               */
/* ------------------------------------------------------------------ */

typedef struct FormResetScan {
    size_t count;
} FormResetScan;

static Result FormResetVisit(const HtmlNodeView *view, size_t depth, void *user)
{
    FormResetScan *scan = (FormResetScan *)user;

    (void)depth;
    /* Both drops are no-ops when no override exists; a non-checkable control
     * refuses the checkedness clear, which is expected and ignored. */
    (void)HtmlElementClearControlValue(&view->handle, NULL);
    (void)HtmlElementClearControlChecked(&view->handle, NULL);
    scan->count++;
    return RoboOk;
}

Result HtmlFormReset(
    const HtmlNodeHandle *form,
    size_t *outReset,
    HtmlDiag *diag)
{
    FormResetScan scan;

    if (outReset) {
        *outReset = 0u;
    }
    if (!form) {
        return RoboHtmlDiag(diag, RoboHtmlDomainForms, RoboInvalidArgument,
                              "a form is required");
    }
    if (!HtmlElementIsElement(form)) {
        return RoboHtmlDiag(diag, RoboHtmlDomainForms, RoboNotFound,
                              "the node is not a live element");
    }
    if (!HtmlElementIsNamed(form, "form")) {
        return RoboHtmlDiag(diag, RoboHtmlDomainForms, RoboNotSupported,
                              "the element is not a form");
    }
    scan.count = 0u;
    {
        Result status = HtmlFormGetElements(form, FormResetVisit, &scan, NULL,
                                            diag);

        if (status != RoboOk) {
            return status;
        }
    }
    if (outReset) {
        *outReset = scan.count;
    }
    return RoboOk;
}

/* ------------------------------------------------------------------ */
/* Capabilities                                                        */
/* ------------------------------------------------------------------ */

Result HtmlFormTreeGetCapabilities(HtmlFormTreeCapabilities *out)
{
    if (!out) {
        return RoboInvalidArgument;
    }
    out->association = 1;
    out->elements = 1;
    out->validation = 1;
    out->submit = 1;
    out->reset = 1;
    out->patternValidation = 0;
    out->submissionNetworking = 0;
    return RoboOk;
}
