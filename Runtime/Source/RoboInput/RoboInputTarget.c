/*
 * RoboInput targets (Phase 12.1) — see RoboInputTarget.h.
 *
 * Every door here fills a *record of identity* and checks that the record does not say two things at once.
 * The validation is the substance of the file: a target that carried both a window and an application, or a
 * document tip with no window to have hit it in, would be a record whose reader has to guess which field
 * the producer meant -- and a reader that guesses is a dispatcher that routes to the wrong place.
 *
 * Nothing in this file resolves anything. A handle is copied, checked for well-formedness (which is a
 * question about the *handle*, answerable by RoboHTML itself) and never resolved into a node; a layout id is
 * copied and never looked up. That is what makes a target safe to carry on an event that outlives the tree
 * it was concluded in.
 */

#include "RoboInput/RoboInputTarget.h"

#include "RoboInputInternal.h"

#include "RoboHTML/RoboHTMLNode.h"

#include <stdint.h>

int InputTargetKindIsKnown(InputTargetKind kind)
{
    return kind >= RoboInputTargetNone && kind < (InputTargetKind)RoboInputTargetKindCount;
}

const char *InputTargetKindName(InputTargetKind kind)
{
    switch (kind) {
    case RoboInputTargetNone:
        return "none";
    case RoboInputTargetWindow:
        return "window";
    case RoboInputTargetApplication:
        return "application";
    case RoboInputTargetDocument:
        return "document";
    default:
        break;
    }
    return "unknown";
}

void InputTargetInit(InputTarget *target)
{
    if (!target) {
        return;
    }
    target->kind = RoboInputTargetNone;
    target->window = RoboWindowIdNone;
    target->application = RoboInputApplicationIdNone;
    target->display = RoboDisplayIdNone;
    HtmlNodeHandleClear(&target->domNode);
    target->layoutNode = RoboLayoutNodeIdNone;
    target->flags = 0u;
    target->space = RoboInputSpaceUnknown;
}

/** Non-zero when the record carries no document identity at all. */
static int TargetDocumentIdentityIsEmpty(const InputTarget *target)
{
    return !HtmlNodeHandleIsWellformed(&target->domNode) &&
           target->layoutNode == RoboLayoutNodeIdNone;
}

/** Non-zero when the record carries no *routing* identity: no application and no document tip. */
static int TargetPayloadIsEmpty(const InputTarget *target)
{
    return target->application == RoboInputApplicationIdNone && target->flags == 0u &&
           TargetDocumentIdentityIsEmpty(target);
}

int InputTargetIsValid(const InputTarget *target)
{
    if (!target) {
        return 0;
    }
    if (!InputTargetKindIsKnown(target->kind)) {
        return 0;
    }
    /* A target names the plane its routing was concluded in, and `UNKNOWN` is not a plane: a target whose
     * plane is unset could not be compared against the event it belongs to, which is the one check that
     * makes a target trustworthy. */
    if (target->space == RoboInputSpaceUnknown || !InputSpaceIsKnown(target->space)) {
        return 0;
    }
    if ((target->flags & ~RoboInputTargetFlagAll) != 0u) {
        return 0;
    }

    switch (target->kind) {
    case RoboInputTargetNone:
        /* About nothing: nothing may be filled in. */
        return target->window == RoboWindowIdNone && TargetPayloadIsEmpty(target);
    case RoboInputTargetWindow:
        return target->window != RoboWindowIdNone && TargetPayloadIsEmpty(target);
    case RoboInputTargetApplication:
        return target->application != RoboInputApplicationIdNone &&
               target->window == RoboWindowIdNone && target->flags == 0u &&
               TargetDocumentIdentityIsEmpty(target);
    case RoboInputTargetDocument:
        /* A window, and something under the point: at least one of the two identities, because both absent
         * would be a `WINDOW` target wearing a document's kind. */
        return target->window != RoboWindowIdNone && target->application == RoboInputApplicationIdNone &&
               !TargetDocumentIdentityIsEmpty(target);
    default:
        break;
    }
    return 0;
}

int InputTargetHasDocument(const InputTarget *target)
{
    return target != NULL && target->kind == RoboInputTargetDocument;
}

int InputTargetIsWindow(const InputTarget *target, WindowId window)
{
    return target != NULL && window != RoboWindowIdNone && target->window == window;
}

/**
 * Clear @p target and give it a plane: the first thing every `set_*` door below does.
 *
 * Clearing first is what makes a refusal *safe*: a caller that reuses one target record for event after
 * event and gets a refusal is left with an empty record rather than the previous event's routing, so a
 * mistake cannot silently re-deliver the last target.
 */
static Result SetSpace(InputTarget *target,
                               InputSpace space,
                               InputDiag *diag)
{
    if (!target) {
        return RoboInputDiag(diag, RoboInputDomainTarget, RoboInvalidArgument,
                               "a target needs a record to fill");
    }
    if (!InputSpaceIsKnown(space)) {
        return RoboInputDiag(diag, RoboInputDomainTarget, RoboInvalidArgument,
                               "a target needs a defined plane");
    }
    InputTargetInit(target);
    target->space = space;
    return RoboOk;
}

Result InputTargetSetNone(InputTarget *target,
                                         InputSpace space,
                                         InputDiag *diag)
{
    return SetSpace(target, space, diag);
}

Result InputTargetSetWindow(InputTarget *target,
                                           WindowId window,
                                           InputSpace space,
                                           InputDiag *diag)
{
    Result r = SetSpace(target, space, diag);

    if (r != RoboOk) {
        return r;
    }
    if (window == RoboWindowIdNone) {
        return RoboInputDiag(diag, RoboInputDomainTarget, RoboInvalidArgument,
                               "a window target needs a window");
    }
    target->kind = RoboInputTargetWindow;
    target->window = window;
    return RoboOk;
}

Result InputTargetSetApplication(InputTarget *target,
                                                InputApplicationId application,
                                                InputSpace space,
                                                InputDiag *diag)
{
    Result r = SetSpace(target, space, diag);

    if (r != RoboOk) {
        return r;
    }
    if (application == RoboInputApplicationIdNone) {
        return RoboInputDiag(diag, RoboInputDomainTarget, RoboInvalidArgument,
                               "an application target needs an application");
    }
    target->kind = RoboInputTargetApplication;
    target->application = application;
    return RoboOk;
}

Result InputTargetSetDocument(InputTarget *target,
                                             WindowId window,
                                             const HtmlNodeHandle *domNode,
                                             LayoutNodeId layoutNode,
                                             uint32_t flags,
                                             InputSpace space,
                                             InputDiag *diag)
{
    Result r = SetSpace(target, space, diag);

    if (r != RoboOk) {
        return r;
    }
    if (window == RoboWindowIdNone) {
        return RoboInputDiag(diag, RoboInputDomainTarget, RoboInvalidArgument,
                               "a document target needs the window it was hit in");
    }
    if ((flags & ~RoboInputTargetFlagAll) != 0u) {
        return RoboInputDiag(diag, RoboInputDomainTarget, RoboInvalidArgument,
                               "a document target was given a flag this build does not define");
    }
    if (domNode) {
        target->domNode = *domNode;
    }
    target->layoutNode = layoutNode;
    if (TargetDocumentIdentityIsEmpty(target)) {
        return RoboInputDiag(diag, RoboInputDomainTarget, RoboInvalidArgument,
                               "a document target needs the node under the point");
    }
    target->kind = RoboInputTargetDocument;
    target->window = window;
    target->flags = flags;
    return RoboOk;
}

int InputTargetDocument(const InputTarget *target,
                               HtmlNodeHandle *outDomNode,
                               LayoutNodeId *outNode)
{
    if (outDomNode) {
        HtmlNodeHandleClear(outDomNode);
    }
    if (outNode) {
        *outNode = RoboLayoutNodeIdNone;
    }
    if (!target || target->kind != RoboInputTargetDocument) {
        return 0;
    }
    if (outDomNode) {
        *outDomNode = target->domNode;
    }
    if (outNode) {
        *outNode = target->layoutNode;
    }
    return 1;
}

int InputTargetHasFlag(const InputTarget *target, uint32_t flag)
{
    return target != NULL && (target->flags & flag) != 0u;
}

int InputTargetEqual(const InputTarget *a, const InputTarget *b)
{
    if (a == b) {
        return 1;
    }
    if (!a || !b) {
        return 0;
    }
    return a->kind == b->kind && a->window == b->window && a->application == b->application &&
           a->display == b->display && a->layoutNode == b->layoutNode && a->flags == b->flags &&
           a->space == b->space && a->domNode.documentId == b->domNode.documentId &&
           a->domNode.nodeId == b->domNode.nodeId && a->domNode.generation == b->domNode.generation;
}
