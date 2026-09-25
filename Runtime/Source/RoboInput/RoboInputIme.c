/*
 * Input methods (Phase 13.37--13.39) — see `RoboInputIme.h`.
 *
 * The context is a *translator*: every door here is a decision about whether to ask the client and what to do
 * with a refusal. Two of those decisions are the layer's own rather than the client's -- a password field is
 * never asked for its text, and a candidate window with no caret to anchor to is refused rather than placed at
 * the origin -- and both are written down where they happen.
 */

#include "RoboInput/RoboInputIme.h"

#include "RoboInputInternal.h"

#include <string.h>

/* ============================================================================================== *
 * Vocabulary
 * ============================================================================================== */

static const char *const RiImePurposeNames[RoboInputImeTextPurposeCount] = {
    "none", "text", "search", "password", "email", "url", "number"
};

int InputImeTextPurposeIsKnown(InputImeTextPurpose purpose)
{
    return (int)purpose >= 0 && (size_t)purpose < (size_t)RoboInputImeTextPurposeCount;
}

const char *InputImeTextPurposeName(InputImeTextPurpose purpose)
{
    if (!InputImeTextPurposeIsKnown(purpose)) {
        return "unknown";
    }
    return RiImePurposeNames[(size_t)purpose];
}

int InputImeTextPurposeIsPrivate(InputImeTextPurpose purpose)
{
    /* Only passwords. A search field is not private in this sense: the *learning history* rule is the input
     * method's, and this build's job is to not hand over text a user did not mean to share. */
    return purpose == RoboInputImePurposePassword;
}

/* ============================================================================================== *
 * The context
 * ============================================================================================== */

void InputMethodContextInit(InputMethodContext *context)
{
    if (!context) {
        return;
    }
    memset(context, 0, sizeof(*context));
    InputTargetInit(&context->target);
    context->purpose = RoboInputImePurposeNone;
    context->lastRefusal = "";
}

static Result RiImeRefuse(InputMethodContext *context, const char *what, InputDiag *diag)
{
    context->refusals++;
    context->lastRefusal = what;
    return RoboInputDiag(diag, RoboInputDomainIme, RoboNotSupported, what);
}

Result InputMethodContextAttach(InputMethodContext *context,
                                const InputTarget *target,
                                const InputMethodClient *client,
                                InputImeTextPurpose purpose,
                                InputDiag *diag)
{
    int changed;

    if (!context) {
        return RoboInputDiag(diag, RoboInputDomainIme, RoboInvalidArgument,
                             "an input-method context is required");
    }
    if (!InputTargetIsValid(target)) {
        return RoboInputDiag(diag, RoboInputDomainIme, RoboInvalidArgument,
                             "only a valid target may hold an input method");
    }
    if (!InputImeTextPurposeIsKnown(purpose)) {
        return RoboInputDiag(diag, RoboInputDomainIme, RoboInvalidArgument,
                             "an attached editor names a defined purpose");
    }

    /*
     * A composition belongs to an editor. Moving the input method to a different target *cancels* what the
     * previous one had, because leaving it behind is how a preedit appears in a field the user has left.
     */
    changed = context->active && !InputTargetEqual(&context->target, target);
    if (changed && context->client.cancelComposition) {
        (void)context->client.cancelComposition(context->client.userdata);
        context->cancels++;
    }

    context->target = *target;
    context->active = 1;
    context->purpose = purpose;
    if (client) {
        context->client = *client;
    } else {
        memset(&context->client, 0, sizeof(context->client));
    }
    if (diag) {
        InputDiagReset(diag);
    }
    return RoboOk;
}

int InputMethodContextDetach(InputMethodContext *context)
{
    int cancelled = 0;

    if (!context) {
        return 0;
    }
    if (context->active && context->client.cancelComposition) {
        (void)context->client.cancelComposition(context->client.userdata);
        context->cancels++;
        cancelled = 1;
    }
    InputTargetInit(&context->target);
    context->active = 0;
    context->purpose = RoboInputImePurposeNone;
    memset(&context->client, 0, sizeof(context->client));
    return cancelled;
}

int InputMethodContextIsActive(const InputMethodContext *context)
{
    return context && context->active ? 1 : 0;
}

uint32_t InputMethodContextCapabilities(const InputMethodContext *context)
{
    if (!context || !context->active) {
        return 0u;
    }
    /* The advertised set is what the client *claims* and what it actually *has*: a client that lists a
     * capability without the door behind it is advertising a call this layer would refuse. */
    {
        uint32_t declared = context->client.capabilities & RoboInputImeCapabilityAll;

        if (!context->client.caret) {
            declared &= ~RoboInputImeCapabilityCaretGeometry;
        }
        if (!context->client.surroundingText) {
            declared &= ~RoboInputImeCapabilitySurroundingText;
        }
        if (!context->client.insertText) {
            declared &= ~RoboInputImeCapabilityInsert;
        }
        return declared;
    }
}

Result InputMethodContextCaret(InputMethodContext *context,
                               InputMethodCaret *out,
                               InputDiag *diag)
{
    if (!context || !out) {
        return RoboInputDiag(diag, RoboInputDomainIme, RoboInvalidArgument,
                             "a context and somewhere to put the caret are required");
    }
    memset(out, 0, sizeof(*out));
    if (!context->active) {
        return RoboInputDiag(diag, RoboInputDomainIme, RoboNotFound,
                             "no editor has an input method attached");
    }
    if (!context->client.caret ||
        (context->client.capabilities & RoboInputImeCapabilityCaretGeometry) == 0u) {
        return RiImeRefuse(context, "the editor reports no caret geometry", diag);
    }
    if (context->client.caret(context->client.userdata, out) != RoboOk) {
        return RiImeRefuse(context, "the editor refused to report its caret", diag);
    }
    if (diag) {
        InputDiagReset(diag);
    }
    return RoboOk;
}

Result InputMethodContextSurroundingText(InputMethodContext *context,
                                         uint32_t before,
                                         uint32_t after,
                                         char *out,
                                         size_t capacity,
                                         size_t *outLength,
                                         InputDiag *diag)
{
    if (outLength) {
        *outLength = 0u;
    }
    if (!context || !out || capacity == 0u) {
        return RoboInputDiag(diag, RoboInputDomainIme, RoboInvalidArgument,
                             "a context, a buffer and a capacity are required");
    }
    out[0] = '\0';
    if (!context->active) {
        return RoboInputDiag(diag, RoboInputDomainIme, RoboNotFound,
                             "no editor has an input method attached");
    }
    /*
     * The privacy rule is this layer's, not the client's: a password field is never asked, however willing its
     * editor is to answer. A client that could report the text must still not be asked for it.
     */
    if (InputImeTextPurposeIsPrivate(context->purpose)) {
        return RiImeRefuse(context, "a private field reports no surrounding text", diag);
    }
    if (!context->client.surroundingText ||
        (context->client.capabilities & RoboInputImeCapabilitySurroundingText) == 0u) {
        return RiImeRefuse(context, "the editor reports no surrounding text", diag);
    }
    if (context->client.surroundingText(context->client.userdata, before, after, out, capacity,
                                        outLength) != RoboOk) {
        return RiImeRefuse(context, "the editor refused to report its surrounding text", diag);
    }
    if (diag) {
        InputDiagReset(diag);
    }
    return RoboOk;
}

Result InputMethodContextCandidateRect(InputMethodContext *context,
                                       float width,
                                       float height,
                                       const GfxRect *viewport,
                                       InputMethodCandidateRect *out,
                                       InputDiag *diag)
{
    InputMethodCaret caret;
    Result status;
    float x;
    float y;

    if (!context || !out) {
        return RoboInputDiag(diag, RoboInputDomainIme, RoboInvalidArgument,
                             "a context and somewhere to put the rectangle are required");
    }
    memset(out, 0, sizeof(*out));
    if (!(width > 0.0f) || !(height > 0.0f)) {
        return RoboInputDiag(diag, RoboInputDomainIme, RoboInvalidArgument,
                             "a candidate window has a positive size");
    }

    status = InputMethodContextCaret(context, &caret, diag);
    if (status != RoboOk) {
        /* No caret, no anchor. A candidate window placed without one is a window placed at the origin, which
         * is worse than telling the caller there is nothing to anchor to. */
        return status;
    }

    out->width = width;
    out->height = height;
    x = caret.x;
    /* Below the caret by default: the candidate must not cover the text being composed. */
    y = caret.y + caret.height;
    if (viewport) {
        float viewportTop = viewport->y;
        float viewportBottom = viewport->y + viewport->height;
        float viewportLeft = viewport->x;
        float viewportRight = viewport->x + viewport->width;

        if (y + height > viewportBottom) {
            /* Flip above the caret rather than clipping: a candidate half off the bottom of the screen is
             * unusable, and the caret's line is where the eye already is. */
            float above = caret.y - height;

            if (above >= viewportTop) {
                y = above;
                out->flipped = 1;
            } else {
                /* Neither below nor above fits. Clamp into the viewport bottom, which keeps the window on
                 * screen and leaves the caret visible where it is. */
                y = viewportBottom - height;
                if (y < viewportTop) {
                    y = viewportTop;
                }
                out->clamped = 1;
            }
        }
        if (x + width > viewportRight) {
            x = viewportRight - width;
            out->clamped = 1;
        }
        if (x < viewportLeft) {
            x = viewportLeft;
            out->clamped = 1;
        }
    }

    out->x = x;
    out->y = y;
    out->valid = 1;
    return RoboOk;
}

Result InputMethodContextCompose(InputMethodContext *context,
                                 const char *text,
                                 size_t length,
                                 uint32_t clauses,
                                 InputDiag *diag)
{
    if (!context) {
        return RoboInputDiag(diag, RoboInputDomainIme, RoboInvalidArgument,
                             "a context is required");
    }
    if (!text && length > 0u) {
        return RoboInputDiag(diag, RoboInputDomainIme, RoboInvalidArgument,
                             "composing text of a non-zero length is required");
    }
    if (clauses > RoboInputImeMaxClauses) {
        return RoboInputDiag(diag, RoboInputDomainIme, RoboInvalidArgument,
                             "a composition declares at most a bounded number of clauses");
    }
    if (!context->active) {
        return RoboInputDiag(diag, RoboInputDomainIme, RoboNotFound,
                             "no editor has an input method attached");
    }
    if (!context->client.setComposition) {
        return RiImeRefuse(context, "the editor cannot hold a composition", diag);
    }
    if (context->client.setComposition(context->client.userdata, text ? text : "", length) != RoboOk) {
        return RiImeRefuse(context, "the editor refused the composition", diag);
    }
    context->compositions++;
    if (diag) {
        InputDiagReset(diag);
    }
    return RoboOk;
}

Result InputMethodContextCommit(InputMethodContext *context,
                                const char *text,
                                size_t length,
                                InputDiag *diag)
{
    if (!context) {
        return RoboInputDiag(diag, RoboInputDomainIme, RoboInvalidArgument,
                             "a context is required");
    }
    if (!context->active) {
        return RoboInputDiag(diag, RoboInputDomainIme, RoboNotFound,
                             "no editor has an input method attached");
    }
    if (!context->client.commitComposition) {
        return RiImeRefuse(context, "the editor cannot commit a composition", diag);
    }
    if (context->client.commitComposition(context->client.userdata, text, length) != RoboOk) {
        return RiImeRefuse(context, "the editor refused the commit", diag);
    }
    context->commits++;
    if (diag) {
        InputDiagReset(diag);
    }
    return RoboOk;
}

Result InputMethodContextCancel(InputMethodContext *context, InputDiag *diag)
{
    if (!context) {
        return RoboInputDiag(diag, RoboInputDomainIme, RoboInvalidArgument,
                             "a context is required");
    }
    if (!context->active) {
        return RoboInputDiag(diag, RoboInputDomainIme, RoboNotFound,
                             "no editor has an input method attached");
    }
    if (!context->client.cancelComposition) {
        return RiImeRefuse(context, "the editor cannot cancel a composition", diag);
    }
    if (context->client.cancelComposition(context->client.userdata) != RoboOk) {
        return RiImeRefuse(context, "the editor refused the cancellation", diag);
    }
    context->cancels++;
    if (diag) {
        InputDiagReset(diag);
    }
    return RoboOk;
}

Result InputMethodContextInsertText(InputMethodContext *context,
                                    const char *text,
                                    size_t length,
                                    InputDiag *diag)
{
    if (!context) {
        return RoboInputDiag(diag, RoboInputDomainIme, RoboInvalidArgument,
                             "a context is required");
    }
    if (!context->active) {
        return RoboInputDiag(diag, RoboInputDomainIme, RoboNotFound,
                             "no editor has an input method attached");
    }
    if (!context->client.insertText) {
        return RiImeRefuse(context, "the editor cannot insert text", diag);
    }
    if (context->client.insertText(context->client.userdata, text, length) != RoboOk) {
        return RiImeRefuse(context, "the editor refused the insertion", diag);
    }
    if (diag) {
        InputDiagReset(diag);
    }
    return RoboOk;
}

/* ============================================================================================== *
 * Diagnostics
 * ============================================================================================== */

Result InputMethodContextWrite(const InputMethodContext *context,
                               char *buffer,
                               size_t capacity,
                               size_t *outLength,
                               InputDiag *diag)
{
    InputWriter text;

    if (!RiWriterArgumentsAreValid(buffer, capacity, outLength)) {
        return RoboInputDiag(diag, RoboInputDomainDump, RoboInvalidArgument,
                             "a dump needs a buffer, a capacity and somewhere to put the length");
    }
    RiWriterInit(&text, buffer, capacity);
    if (!context) {
        RiWriterText(&text, "ime <none>\n");
        return RiWriterFinish(&text, outLength, diag);
    }

    RiWriterText(&text, "ime active=");
    RiWriterU64(&text, (uint64_t)(context->active ? 1u : 0u));
    RiWriterFieldText(&text, " target",
                      context->active ? InputTargetKindName(context->target.kind) : "none");
    RiWriterFieldText(&text, " purpose", InputImeTextPurposeName(context->purpose));
    RiWriterFieldU64(&text, " capabilities", (uint64_t)InputMethodContextCapabilities(context));
    RiWriterFieldU64(&text, " compositions", (uint64_t)context->compositions);
    RiWriterFieldU64(&text, " commits", (uint64_t)context->commits);
    RiWriterFieldU64(&text, " cancels", (uint64_t)context->cancels);
    RiWriterFieldU64(&text, " refusals", (uint64_t)context->refusals);
    RiWriterChar(&text, '\n');
    return RiWriterFinish(&text, outLength, diag);
}

Result InputMethodCaretWrite(const InputMethodCaret *caret,
                             char *buffer,
                             size_t capacity,
                             size_t *outLength,
                             InputDiag *diag)
{
    InputWriter text;

    if (!RiWriterArgumentsAreValid(buffer, capacity, outLength)) {
        return RoboInputDiag(diag, RoboInputDomainDump, RoboInvalidArgument,
                             "a dump needs a buffer, a capacity and somewhere to put the length");
    }
    RiWriterInit(&text, buffer, capacity);
    if (!caret) {
        RiWriterText(&text, "ime-caret <none>\n");
        return RiWriterFinish(&text, outLength, diag);
    }

    RiWriterText(&text, "ime-caret valid=");
    RiWriterU64(&text, (uint64_t)(caret->valid ? 1u : 0u));
    RiWriterFieldI32(&text, " x", (int32_t)caret->x);
    RiWriterFieldI32(&text, " y", (int32_t)caret->y);
    RiWriterFieldI32(&text, " width", (int32_t)caret->width);
    RiWriterFieldI32(&text, " height", (int32_t)caret->height);
    RiWriterFieldU64(&text, " horizontal", (uint64_t)(caret->horizontal ? 1u : 0u));
    RiWriterChar(&text, '\n');
    return RiWriterFinish(&text, outLength, diag);
}

Result InputMethodCandidateRectWrite(const InputMethodCandidateRect *rect,
                                     char *buffer,
                                     size_t capacity,
                                     size_t *outLength,
                                     InputDiag *diag)
{
    InputWriter text;

    if (!RiWriterArgumentsAreValid(buffer, capacity, outLength)) {
        return RoboInputDiag(diag, RoboInputDomainDump, RoboInvalidArgument,
                             "a dump needs a buffer, a capacity and somewhere to put the length");
    }
    RiWriterInit(&text, buffer, capacity);
    if (!rect) {
        RiWriterText(&text, "ime-candidate <none>\n");
        return RiWriterFinish(&text, outLength, diag);
    }

    RiWriterText(&text, "ime-candidate valid=");
    RiWriterU64(&text, (uint64_t)(rect->valid ? 1u : 0u));
    RiWriterFieldI32(&text, " x", (int32_t)rect->x);
    RiWriterFieldI32(&text, " y", (int32_t)rect->y);
    RiWriterFieldI32(&text, " width", (int32_t)rect->width);
    RiWriterFieldI32(&text, " height", (int32_t)rect->height);
    RiWriterFieldU64(&text, " flipped", (uint64_t)(rect->flipped ? 1u : 0u));
    RiWriterFieldU64(&text, " clamped", (uint64_t)(rect->clamped ? 1u : 0u));
    RiWriterChar(&text, '\n');
    return RiWriterFinish(&text, outLength, diag);
}
