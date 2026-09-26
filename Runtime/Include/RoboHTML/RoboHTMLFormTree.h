/*
 * RoboHTML forms — association, validation, submission and reset (Phase 17.2).
 *
 * Phase 8.14 modelled the *state*: a control's declared value, its current
 * value, its checkedness, and the rules that resolve a label to its control
 * and a control to its form owner. This header is the layer above that — the
 * operations an application (or RoboJS on its behalf) performs on a form:
 *
 *   elements    the controls associated with a form (`form.elements`);
 *   validation  the constraint checks a submission runs first;
 *   submit      a *controlled* submission request that never touches the
 *               network itself;
 *   reset       restore every associated control to its declared state.
 *
 * ## Nothing here fetches anything
 *
 * `form.submit()` in a browser navigates. In this runtime submission is a
 * **request**: the form is validated, and only if it passes is a caller-
 * installed handler consulted — the same seam shape the anchor module uses for
 * navigation. With no handler installed a valid submission reports
 * `RoboNotSupported`, because performing it would mean doing the networking
 * this module must never do. The submission's `action`, `method` and
 * `enctype` travel to the handler as the typed `HtmlFormAttributes` Phase 8.14
 * already reads; no new encoding is invented here.
 *
 * ## Validation is the foundation the brief asks for, not the full spec
 *
 * `required`, `disabled`, `readonly`, `minlength`, `maxlength`, `min`, `max`
 * and a basic per-type check are implemented. `pattern` is *recognised and
 * reported* but not evaluated — this build has no regular-expression engine,
 * so a control carrying a pattern sets `patternUnsupported` and is otherwise
 * judged by the constraints above. Stating that is the point: a control that
 * silently passed could be mistaken for one that matched.
 */

#ifndef ROBO_HTML_FORM_TREE_H
#define ROBO_HTML_FORM_TREE_H

#include "RoboBase/RoboError.h"
#include "RoboHTML/RoboHTMLDiagnostics.h"
#include "RoboHTML/RoboHTMLForm.h"
#include "RoboHTML/RoboHTMLNode.h"
#include "RoboHTML/RoboHTMLTraversal.h"
#include "RoboHTML/RoboHTMLTypes.h"

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------ */
/* form.elements                                                       */
/* ------------------------------------------------------------------ */

/**
 * Visit every control whose *form owner* is @p form, in document order.
 *
 * The walk is over the whole document, not the form's subtree, because a
 * control associated by a `form` attribute may live outside the form — the
 * DOM's `form.elements` includes it, and a descendant-only walk would miss it.
 * A control inside the form with no owner override is found by ancestry, which
 * is the same rule in the other direction. Nothing is cached, so moving a
 * control in or out of the form changes the next call's answer with no
 * invalidation step.
 *
 * @return RoboOk; the visitor's own code when it stops the walk;
 *         RoboInvalidArgument (NULL @p form); RoboNotFound (@p form does not
 *         resolve); RoboNotSupported (the element is not a `<form>`).
 */
Result HtmlFormGetElements(
    const HtmlNodeHandle *form,
    HtmlNodeVisitFn visit,
    void *user,
    size_t *outCount,
    HtmlDiag *diag);

/* ------------------------------------------------------------------ */
/* Validation                                                          */
/* ------------------------------------------------------------------ */

/**
 * The constraint checks that apply to one control.
 *
 * `validated` is 0 when the control is exempt — disabled, `readonly`, of a
 * type that is never validated (`button`, `submit`, `reset`, `hidden`), or not
 * a control at all — and then `valid` is 1 and every violation flag is 0.
 * That is the HTML rule: an unvalidated control cannot block a submission.
 */
typedef struct HtmlControlValidation {
    int validated;
    int valid;

    int valueMissing;   /* required, and the current value is empty */
    int tooShort;       /* minlength, and the value is shorter */
    int tooLong;        /* maxlength, and the value is longer */
    int belowMin;       /* number/range, below a parsed min */
    int aboveMax;       /* number/range, above a parsed max */
    int typeMismatch;   /* email/number/url: the value is not of the type */
    /**
     * The control carries a `pattern` this build cannot evaluate. Not a
     * violation — a stated gap. A caller that wants strictness treats it as
     * "not decided" rather than "matched".
     */
    int patternUnsupported;
} HtmlControlValidation;

/**
 * Evaluate @p control's constraint checks.
 * @return RoboOk; RoboInvalidArgument; RoboNotFound (@p control does not
 *         resolve); RoboNotSupported (the element is not a form control).
 */
Result HtmlElementValidateControl(
    const HtmlNodeHandle *control,
    HtmlControlValidation *out,
    HtmlDiag *diag);

/** Aggregate of a form's control validations. */
typedef struct HtmlFormValidation {
    size_t controls;    /* associated controls seen */
    size_t validated;   /* controls actually validated */
    size_t invalid;     /* controls whose check failed */
    size_t unsupported; /* controls carrying an unevaluatable pattern */
    int valid;          /* non-zero when nothing failed */
} HtmlFormValidation;

/**
 * Validate every control associated with @p form (`form.checkValidity()`).
 * @return RoboOk; RoboInvalidArgument; RoboNotFound; RoboNotSupported.
 */
Result HtmlFormValidate(
    const HtmlNodeHandle *form,
    HtmlFormValidation *out,
    HtmlDiag *diag);

/* ------------------------------------------------------------------ */
/* Submission                                                          */
/* ------------------------------------------------------------------ */

/**
 * A caller-installed submission handler.
 *
 * It is handed the form, the form's attributes (the typed view from
 * RoboHTMLForm.h) and the number of controls, and it performs whatever a
 * submission means to that caller. Returning non-RoboOk fails the request. The
 * handler must not mutate the document; it is called during `HtmlFormSubmit`.
 */
typedef Result (*HtmlFormSubmitFn)(const HtmlNodeHandle *form,
                                   const HtmlFormAttributes *attributes,
                                   size_t controlCount,
                                   void *userdata,
                                   HtmlDiag *diag);

/**
 * Install (or clear, with NULL) the document's submission handler. One per
 * document, replaced at any time — the same shape as the selector matcher.
 */
Result HtmlDocumentSetFormSubmitHandler(
    HtmlDocumentId documentId,
    HtmlFormSubmitFn handler,
    void *userdata,
    HtmlDiag *diag);

/** What `HtmlFormSubmit` did. */
typedef struct HtmlSubmitOutcome {
    int validated;
    int valid;               /* non-zero when the form passed validation */
    int requested;           /* non-zero when the handler ran */
    size_t invalidControls;  /* controls that failed, when validation failed */
} HtmlSubmitOutcome;

/**
 * Validate @p form and, if it passes, ask the document's submission handler to
 * perform it.
 *
 * @return RoboOk when validation ran (the outcome says whether it passed and
 *         whether a handler was asked); RoboNotSupported when the form is
 *         valid but no handler is installed — a submission this build cannot
 *         perform must say so rather than pretend; RoboInvalidArgument;
 *         RoboNotFound; RoboNotSupported when @p form is not a `<form>`.
 */
Result HtmlFormSubmit(
    const HtmlNodeHandle *form,
    HtmlSubmitOutcome *out,
    HtmlDiag *diag);

/* ------------------------------------------------------------------ */
/* Reset                                                               */
/* ------------------------------------------------------------------ */

/**
 * Restore every associated control to its declared state: drop the value
 * override and the checkedness override. Removing an override that is not
 * there is a successful no-op, so a reset never fails on an untouched field.
 *
 * @return RoboOk; RoboInvalidArgument; RoboNotFound; RoboNotSupported
 *         (@p form is not a `<form>`).
 */
Result HtmlFormReset(
    const HtmlNodeHandle *form,
    size_t *outReset,
    HtmlDiag *diag);

/** Capability table for this surface. */
typedef struct HtmlFormTreeCapabilities {
    int association;
    int elements;
    int validation;
    int submit;
    int reset;
    int patternValidation;    /* 0: no regex engine in this build */
    int submissionNetworking; /* 0: submission is a request the caller answers */
} HtmlFormTreeCapabilities;

/** Fill @p out. RoboInvalidArgument on NULL. */
Result HtmlFormTreeGetCapabilities(HtmlFormTreeCapabilities *out);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_HTML_FORM_TREE_H */
