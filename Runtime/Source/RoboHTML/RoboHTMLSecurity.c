/*
 * RoboHTML Phase 17.10 — capabilities, posture, lifetime.
 *
 * Three small things, and the point of the file is that it is small: the
 * security story here is the *absence* of an interface to the host, and the
 * only mechanism this phase adds is the one a host needs to narrow what an
 * embedded document may do.
 *
 * The capability record lives in the document slot beside every other
 * per-document fact. "Never set" is not the same as "set to nothing", so a
 * separate flag distinguishes them: the default is *granted*, and a document
 * that cleared its set is indistinguishable from one whose set is empty only
 * by that flag. Getting the direction wrong here would silently deny every
 * existing caller, which is why the default is explicit rather than zeroed.
 */

#include "RoboHTML/RoboHTMLSecurity.h"

#include "RoboHTML/RoboHTML.h"
#include "RoboHTML/RoboHTMLCustomElements.h"
#include "RoboHTML/RoboHTMLDocument.h"
#include "RoboHTML/RoboHTMLImage.h"
#include "RoboHTML/RoboHTMLMutation.h"
#include "RoboHTML/RoboHTMLNode.h"
#include "RoboHTML/RoboHTMLVersion.h"

#include "RoboHTMLInternal.h"

#include <stddef.h>

/* ------------------------------------------------------------------ */
/* Vocabulary                                                          */
/* ------------------------------------------------------------------ */

int HtmlSecurityCapabilityIsKnown(HtmlSecurityCapability capability)
{
    return capability >= RoboHtmlCapabilityDomMutation &&
           capability < RoboHtmlCapabilityCount;
}

const char *HtmlSecurityCapabilityName(HtmlSecurityCapability capability)
{
    switch (capability) {
    case RoboHtmlCapabilityDomMutation:    return "dom-mutation";
    case RoboHtmlCapabilityResourceLoading: return "resource-loading";
    case RoboHtmlCapabilityNavigation:     return "navigation";
    case RoboHtmlCapabilityCustomElements: return "custom-elements";
    case RoboHtmlCapabilityShadowDom:      return "shadow-dom";
    default:                               return "unknown";
    }
}

/* ------------------------------------------------------------------ */
/* Capabilities                                                        */
/* ------------------------------------------------------------------ */

void HtmlDocumentCapabilitiesDefault(HtmlDocumentCapabilities *out)
{
    if (!out) {
        return;
    }
    out->domMutation = 1;
    out->resourceLoading = 1;
    out->navigation = 1;
    out->customElements = 1;
    out->shadowDom = 1;
}

static int CapabilityFlagValid(int flag)
{
    return flag == 0 || flag == 1;
}

Result HtmlDocumentSetCapabilities(HtmlDocumentId documentId,
                                   const HtmlDocumentCapabilities *caps,
                                   HtmlDiag *diag)
{
    HtmlDocumentSlot *slot;

    if (!caps) {
        return RoboHtmlDiag(diag, RoboHtmlDomainDocument, RoboInvalidArgument,
                            "null capabilities");
    }
    /* A field that is neither 0 nor 1 is a caller mistake, not a truthy
     * grant: guessing which is how a security switch becomes a no-op. */
    if (!CapabilityFlagValid(caps->domMutation) ||
        !CapabilityFlagValid(caps->resourceLoading) ||
        !CapabilityFlagValid(caps->navigation) ||
        !CapabilityFlagValid(caps->customElements) ||
        !CapabilityFlagValid(caps->shadowDom)) {
        return RoboHtmlDiag(diag, RoboHtmlDomainDocument, RoboInvalidArgument,
                            "a capability field must be 0 or 1");
    }
    slot = HtmlDocumentSlotFind(documentId);
    if (!slot) {
        return RoboHtmlDiag(diag, RoboHtmlDomainDocument, RoboNotFound,
                            "no such live document");
    }
    slot->capabilities = *caps;
    slot->capabilitiesSet = 1;
    return RoboOk;
}

Result HtmlDocumentGetCapabilities(HtmlDocumentId documentId,
                                   HtmlDocumentCapabilities *out,
                                   HtmlDiag *diag)
{
    HtmlDocumentSlot *slot;

    if (!out) {
        return RoboHtmlDiag(diag, RoboHtmlDomainDocument, RoboInvalidArgument,
                            "null out");
    }
    slot = HtmlDocumentSlotFind(documentId);
    if (!slot) {
        return RoboHtmlDiag(diag, RoboHtmlDomainDocument, RoboNotFound,
                            "no such live document");
    }
    if (!slot->capabilitiesSet) {
        HtmlDocumentCapabilitiesDefault(out);
        return RoboOk;
    }
    *out = slot->capabilities;
    return RoboOk;
}

int HtmlDocumentCapabilityAllowed(HtmlDocumentId documentId,
                                  HtmlSecurityCapability capability)
{
    HtmlDocumentSlot *slot = HtmlDocumentSlotFind(documentId);

    if (!slot || !HtmlSecurityCapabilityIsKnown(capability)) {
        /* An unknown document grants nothing: the safe direction. */
        return 0;
    }
    if (!slot->capabilitiesSet) {
        return 1;
    }
    switch (capability) {
    case RoboHtmlCapabilityDomMutation:
        return slot->capabilities.domMutation ? 1 : 0;
    case RoboHtmlCapabilityResourceLoading:
        return slot->capabilities.resourceLoading ? 1 : 0;
    case RoboHtmlCapabilityNavigation:
        return slot->capabilities.navigation ? 1 : 0;
    case RoboHtmlCapabilityCustomElements:
        return slot->capabilities.customElements ? 1 : 0;
    case RoboHtmlCapabilityShadowDom:
        return slot->capabilities.shadowDom ? 1 : 0;
    default:
        return 0;
    }
}

Result HtmlSecurityRequireCapability(HtmlDocumentId documentId,
                                     HtmlSecurityCapability capability,
                                     const char *what,
                                     HtmlDiag *diag)
{
    /*
     * A handle that names no document at all is an ill-formed operand, not a
     * policy question — the same answer the mutation doors give a NULL or
     * zero handle, so the gate does not shadow their own operand checks.
     */
    if (documentId == RoboHtmlDocumentIdNone) {
        return RoboHtmlDiag(diag, RoboHtmlDomainDocument, RoboInvalidArgument,
                            "the operand names no document");
    }
    /*
     * "No such document" is answered before "not granted it": a destroyed
     * document has no capability set to have narrowed, and conflating the
     * two would let a caller read a lifetime bug as a policy refusal.
     * `HtmlDocumentCapabilityAllowed` itself still answers 0 for an unknown
     * document, because a *predicate* has to pick the safe direction.
     */
    if (!HtmlDocumentIsLive(documentId)) {
        return RoboHtmlDiag(diag, RoboHtmlDomainDocument, RoboNotFound,
                            "no such live document");
    }
    if (HtmlDocumentCapabilityAllowed(documentId, capability)) {
        return RoboOk;
    }
    /*
     * PermissionDenied specifically: the operation exists and this document
     * was not granted it. Returning NotFound would let a caller conclude the
     * document has no such feature, and NotSupported that the runtime cannot
     * do it at all.
     */
    return RoboHtmlDiag(diag, RoboHtmlDomainDocument, RoboPermissionDenied,
                        what ? what : "the document does not have that "
                                      "capability");
}

/* ------------------------------------------------------------------ */
/* Posture                                                             */
/* ------------------------------------------------------------------ */

Result HtmlSecurityGetPosture(HtmlSecurityPosture *out)
{
    if (!out) {
        return RoboInvalidArgument;
    }
    out->filesystem = 0;
    out->network = 0;
    out->nativePointers = 0;
    out->processes = 0;
    out->hardware = 0;
    out->hostEnvironment = 0;
    out->scriptExecution = 0;
    out->eventListeners = 0;
    out->bytesOnlyThroughSeam = 1;
    out->hostOwnedDocument = 1;
    out->version = RoboHtmlVersionString;
    return RoboOk;
}

/* ------------------------------------------------------------------ */
/* Lifetime report                                                     */
/* ------------------------------------------------------------------ */

Result HtmlDocumentGetSecurityReport(HtmlDocumentId documentId,
                                     HtmlSecurityReport *out,
                                     HtmlDiag *diag)
{
    HtmlTreeReport tree;
    uint32_t version = 0u;

    if (!out) {
        return RoboHtmlDiag(diag, RoboHtmlDomainDocument, RoboInvalidArgument,
                            "null out");
    }
    if (!HtmlDocumentIsLive(documentId)) {
        return RoboHtmlDiag(diag, RoboHtmlDomainDocument, RoboNotFound,
                            "no such live document");
    }
    out->documentId = documentId;
    out->reachableNodes = 0u;
    out->detachedRoots = 0u;
    out->treeVersion = 0u;
    out->imageAttachments = 0u;
    out->customElementDefinitions = 0u;
    out->treeIntact = 0;
    HtmlDocumentCapabilitiesDefault(&out->capabilities);

    /*
     * The audit is the model's own, so the numbers are the runtime's
     * accounting rather than a second walk written here. A document that was
     * created but never parsed has no root walk to run and still gets a
     * report: the registry counts are the honest answer for it.
     */
    if (HtmlDocumentValidateTree(documentId, &tree, NULL) == RoboOk) {
        out->reachableNodes = tree.nodes;
        out->detachedRoots = tree.detached;
        out->treeIntact = tree.ok ? 1 : 0;
    }
    if (HtmlNodeTreeVersion(documentId, &version, NULL) == RoboOk) {
        out->treeVersion = version;
    }
    out->imageAttachments = HtmlDocumentImageCount(documentId);
    out->customElementDefinitions =
        HtmlDocumentCustomElementCount(documentId);
    (void)HtmlDocumentGetCapabilities(documentId, &out->capabilities, NULL);
    return RoboOk;
}
