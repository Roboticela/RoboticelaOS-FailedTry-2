/*
 * RoboHTML future-DOM boundary (Phase 8.1) — capabilities, origins, and
 * the mutation hook foundation. No DOM behavior.
 */

#include "RoboHTML/RoboHTMLDom.h"
#include "RoboHTMLInternal.h"

#include <stddef.h>
#include <string.h>

int HtmlNodeOriginIsKnown(HtmlNodeOrigin origin)
{
    switch (origin) {
    case RoboHtmlNodeOriginNone:
    case RoboHtmlNodeOriginDocumentRoot:
    case RoboHtmlNodeOriginParser:
    case RoboHtmlNodeOriginDom:
    case RoboHtmlNodeOriginTest:
        return 1;
    default:
        return 0;
    }
}

const char *HtmlNodeOriginName(HtmlNodeOrigin origin)
{
    switch (origin) {
    case RoboHtmlNodeOriginNone:
        return "none";
    case RoboHtmlNodeOriginDocumentRoot:
        return "document-root";
    case RoboHtmlNodeOriginParser:
        return "parser";
    case RoboHtmlNodeOriginDom:
        return "dom";
    case RoboHtmlNodeOriginTest:
        return "test";
    default:
        return "unknown";
    }
}

void HtmlDomCapabilitiesInit(HtmlDomCapabilities *caps)
{
    if (!caps) {
        return;
    }
    memset(caps, 0, sizeof(*caps));
}

Result HtmlDomQueryCapabilities(
    HtmlDomCapabilities *out)
{
    if (!out) {
        return RoboInvalidArgument;
    }
    HtmlDomCapabilitiesInit(out);
    /*
     * Phase 8.3: the tree builder constructs real document trees, so tree
     * construction is available. Phase 8.6: elements own a real attribute
     * model (lookup, presence, order, add/set/remove), so attribute storage
     * is available. Phase 8.7: character data is a real node kind with a
     * model over it (content access, set/append), so text nodes are
     * available. Phase 8.8: the tree can be navigated and enumerated
     * deterministically from any node, in both directions, so traversal is
     * available. Phase 8.9: the tree's shape can be changed — append,
     * insert before, remove, replace and bulk child removal — so mutation is
     * available, which is what makes traversal's read-only claim no longer
     * the whole story. Phase 8.12: an image is a resource — `<img>`
     * attributes as a typed view, a resource carrying real decoded pixels,
     * resolve/get/detach per element and an attachment table per document —
     * so images are available, with the caveat that bytes come from a
     * caller-installed resolver and this build ships only an in-memory
     * asset table. Phase 8.13: an `<a>` resolves its `href` into a navigation
     * target and an activation produces a deterministic outcome, answered
     * locally for a fragment and delegated to a caller-installed handler for
     * a route or a permitted URI — with a refused scheme never reaching the
     * handler at all. The node DOM (`domNodes`), events and JavaScript
     * bindings remain later-phase work; `attributes` still means the
     * attribute model, `textNodes` the text model, `images` a resource
     * model and `links` a navigation model, none of them a complete DOM
     * surface. Phase 8.14: the form controls are a typed model over the same
     * attributes — an input's type, a control's form owner, a label's
     * target, and a control's declared-versus-current value — so forms are
     * available, without submission, encoding, validation or interaction.
     */
    out->treeBuilding = 1;
    out->attributes = 1;
    out->textNodes = 1;
    out->traversal = 1;
    out->mutation = 1;
    out->elementTags = 1;
    out->images = 1;
    out->links = 1;
    out->forms = 1;
    return RoboOk;
}

int HtmlMutationKindIsKnown(HtmlMutationKind kind)
{
    switch (kind) {
    case RoboHtmlMutationNone:
    case RoboHtmlMutationNodeRegistered:
    case RoboHtmlMutationDocumentLifecycle:
    case RoboHtmlMutationTreeMutated:
        return 1;
    default:
        return 0;
    }
}

void HtmlEmitMutation(const HtmlMutationEvent *event,
                             void *userdata)
{
    /* userdata carries the owning document slot (see RoboHTMLInternal.h). */
    HtmlDocumentSlot *slot = (HtmlDocumentSlot *)userdata;

    if (!event || !slot || !slot->MutationHook) {
        return;
    }
    slot->MutationHook(event, slot->mutationUserdata);
}

Result HtmlDocumentSetMutationHook(
    HtmlDocumentId documentId,
    HtmlMutationHookFn hook,
    void *userdata,
    HtmlDiag *diag)
{
    HtmlDocumentSlot *slot = HtmlDocumentSlotFind(documentId);

    if (!slot) {
        return RoboHtmlDiag(diag, RoboHtmlDomainMutation, RoboNotFound,
                              "unknown document id");
    }
    if (hook && slot->MutationHook) {
        return RoboHtmlDiag(diag, RoboHtmlDomainMutation,
                              RoboAlreadyExists,
                              "mutation hook already installed; clear first");
    }
    slot->MutationHook = hook;
    slot->mutationUserdata = userdata;
    return RoboOk;
}
