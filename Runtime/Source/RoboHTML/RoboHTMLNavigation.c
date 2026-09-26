/*
 * RoboHTML navigation controller (Phase 17.4).
 *
 * Reads the contract in RoboHTMLNavigation.h. The handler and the location
 * live on the document slot; the target resolution and activation stay
 * Phase 8.13's, which this layer drives rather than re-implements.
 */

#include "RoboHTML/RoboHTMLNavigation.h"
#include "RoboHTML/RoboHTMLTreeBuilder.h"

#include "RoboHTMLInternal.h"

#include <stddef.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/* Element readers                                                     */
/* ------------------------------------------------------------------ */

static Result ReadAnchorAttribute(const HtmlNodeHandle *element,
                                  const char *name,
                                  char *out,
                                  size_t outCap,
                                  size_t *outLength,
                                  HtmlDiag *diag)
{
    const char *value;
    size_t length = 0u;

    if (!element || !out || outCap == 0u) {
        return RoboHtmlDiag(diag, RoboHtmlDomainAnchor, RoboInvalidArgument,
                              "an element, an output buffer and its capacity are required");
    }
    if (!HtmlElementIsElement(element)) {
        return RoboHtmlDiag(diag, RoboHtmlDomainAnchor, RoboNotFound,
                              "the node is not a live element");
    }
    if (!HtmlElementIsNamed(element, "a")) {
        return RoboHtmlDiag(diag, RoboHtmlDomainAnchor, RoboNotSupported,
                              "the element is not an anchor");
    }
    value = HtmlElementAttributeValue(element, name, &length);
    if (!value) {
        out[0] = '\0';
        if (outLength) {
            *outLength = 0u;
        }
        return RoboOk;
    }
    if (length >= outCap) {
        return RoboHtmlDiag(diag, RoboHtmlDomainAnchor, RoboNotSupported,
                              "the attribute does not fit the output buffer");
    }
    memcpy(out, value, length);
    out[length] = '\0';
    if (outLength) {
        *outLength = length;
    }
    return RoboOk;
}

Result HtmlElementHref(
    const HtmlNodeHandle *element,
    char *out,
    size_t outCap,
    size_t *outLength,
    HtmlDiag *diag)
{
    return ReadAnchorAttribute(element, "href", out, outCap, outLength, diag);
}

Result HtmlElementTarget(
    const HtmlNodeHandle *element,
    char *out,
    size_t outCap,
    size_t *outLength,
    HtmlDiag *diag)
{
    return ReadAnchorAttribute(element, "target", out, outCap, outLength, diag);
}

/* ------------------------------------------------------------------ */
/* Controller state                                                    */
/* ------------------------------------------------------------------ */

Result HtmlDocumentSetNavigationHandler(
    HtmlDocumentId documentId,
    const HtmlNavigationHandler *handler,
    HtmlDiag *diag)
{
    HtmlDocumentSlot *slot = HtmlDocumentSlotFind(documentId);

    if (!slot) {
        return RoboHtmlDiag(diag, RoboHtmlDomainDocument, RoboNotFound,
                              "unknown or destroyed document");
    }
    if (handler && HtmlNavigationHandlerIsUsable(handler)) {
        slot->navigationHandler = *handler;
        slot->navigationHasHandler = 1;
    } else {
        HtmlNavigationHandlerInit(&slot->navigationHandler);
        slot->navigationHasHandler = 0;
    }
    return RoboOk;
}

int HtmlDocumentHasNavigationHandler(HtmlDocumentId documentId)
{
    HtmlDocumentSlot *slot = HtmlDocumentSlotFind(documentId);

    return (slot && slot->navigationHasHandler) ? 1 : 0;
}

static void CopyBounded(char *destination,
                        size_t capacity,
                        size_t *outLength,
                        const char *source,
                        size_t length)
{
    if (length >= capacity) {
        length = capacity - 1u;
    }
    if (length > 0u && source) {
        memcpy(destination, source, length);
    }
    destination[length] = '\0';
    *outLength = length;
}

static void RecordLocation(HtmlDocumentSlot *slot,
                           const HtmlNavigationOutcome *out)
{
    if (!out || !out->kind) {
        return;
    }
    if (out->kind == RoboHtmlNavigationFragment) {
        CopyBounded(slot->navigationLocation.fragment,
                    sizeof(slot->navigationLocation.fragment),
                    &slot->navigationLocation.fragmentLength, out->fragment,
                    out->fragmentLength);
        slot->navigationLocation.navigations++;
    } else if (out->kind == RoboHtmlNavigationRoute ||
               out->kind == RoboHtmlNavigationExternal) {
        CopyBounded(slot->navigationLocation.route,
                    sizeof(slot->navigationLocation.route),
                    &slot->navigationLocation.routeLength, out->resolved,
                    out->resolvedLength);
        slot->navigationLocation.fragment[0] = '\0';
        slot->navigationLocation.fragmentLength = 0u;
        slot->navigationLocation.navigations++;
    }
}

Result HtmlDocumentActivateLink(
    const HtmlNodeHandle *anchor,
    HtmlNavigationOutcome *out,
    HtmlDiag *diag)
{
    HtmlDocumentSlot *slot;
    const HtmlNavigationHandler *handler = NULL;
    Result status;

    if (!anchor || !out) {
        return RoboHtmlDiag(diag, RoboHtmlDomainAnchor, RoboInvalidArgument,
                              "an anchor and an outcome are required");
    }
    slot = HtmlDocumentSlotFind(anchor->documentId);
    if (!slot) {
        return RoboHtmlDiag(diag, RoboHtmlDomainDocument, RoboNotFound,
                              "unknown or destroyed document");
    }
    /* Phase 17.10: navigation is a capability. A document without it refuses
     * *before* the handler is consulted, so an embedded document cannot even
     * ask the application's router to go somewhere. */
    {
        /* Propagated, not folded: NotFound (the document is gone) and
         * PermissionDenied (it was not granted navigation) are different
         * answers, and only one of them is a policy decision. */
        Result gate = HtmlSecurityRequireCapability(
            anchor->documentId, RoboHtmlCapabilityNavigation,
            "the document was not granted navigation", diag);

        if (gate != RoboOk) {
            return gate;
        }
    }
    if (slot->navigationHasHandler) {
        handler = &slot->navigationHandler;
    }
    status = HtmlElementActivate(anchor, handler, out, diag);
    if (status != RoboOk) {
        return status;
    }
    RecordLocation(slot, out);
    return RoboOk;
}

Result HtmlDocumentNavigateToFragment(
    HtmlDocumentId documentId,
    const char *fragment,
    size_t fragmentLength,
    HtmlNavigationOutcome *out,
    HtmlDiag *diag)
{
    HtmlDocumentSlot *slot = HtmlDocumentSlotFind(documentId);
    HtmlNodeHandle node;
    Result status;

    if (slot) {
        Result gate = HtmlSecurityRequireCapability(
            documentId, RoboHtmlCapabilityNavigation,
            "the document was not granted navigation", diag);

        if (gate != RoboOk) {
            return gate;
        }
    }
    if (!slot) {
        return RoboHtmlDiag(diag, RoboHtmlDomainDocument, RoboNotFound,
                              "unknown or destroyed document");
    }
    if (!fragment && fragmentLength > 0u) {
        return RoboHtmlDiag(diag, RoboHtmlDomainAnchor, RoboInvalidArgument,
                              "a fragment is required");
    }
    if (out) {
        memset(out, 0, sizeof(*out));
    }
    status = HtmlNavigationFindFragment(documentId, fragment, fragmentLength,
                                        &node, diag);
    if (status == RoboError) {
        return status;
    }
    if (out) {
        out->kind = (status == RoboOk) ? RoboHtmlNavigationFragment
                                       : RoboHtmlNavigationFragmentMissing;
        if (status == RoboOk) {
            out->fragmentNode = node;
            out->fragmentFound = 1;
        }
        out->fragment = fragment;
        out->fragmentLength = fragmentLength;
        out->targetKind = RoboHtmlTargetFragment;
    }
    CopyBounded(slot->navigationLocation.fragment,
                sizeof(slot->navigationLocation.fragment),
                &slot->navigationLocation.fragmentLength, fragment,
                fragmentLength);
    slot->navigationLocation.navigations++;
    return RoboOk;
}

Result HtmlDocumentCurrentLocation(
    HtmlDocumentId documentId,
    HtmlNavigationLocation *out,
    HtmlDiag *diag)
{
    HtmlDocumentSlot *slot = HtmlDocumentSlotFind(documentId);

    if (!out) {
        return RoboHtmlDiag(diag, RoboHtmlDomainDocument, RoboInvalidArgument,
                              "an output is required");
    }
    if (!slot) {
        return RoboHtmlDiag(diag, RoboHtmlDomainDocument, RoboNotFound,
                              "unknown or destroyed document");
    }
    *out = slot->navigationLocation;
    return RoboOk;
}

Result HtmlDocumentLoadSource(
    HtmlDocumentId documentId,
    const HtmlSource *source,
    HtmlDiag *diag)
{
    if (!source) {
        return RoboHtmlDiag(diag, RoboHtmlDomainDocument, RoboInvalidArgument,
                              "a source is required");
    }
    if (!HtmlDocumentSlotFind(documentId)) {
        return RoboHtmlDiag(diag, RoboHtmlDomainDocument, RoboNotFound,
                              "unknown or destroyed document");
    }
    return HtmlBuildTree(documentId, source, NULL, diag);
}

/* ------------------------------------------------------------------ */
/* Capabilities                                                        */
/* ------------------------------------------------------------------ */

Result HtmlNavigationGetCapabilities(HtmlNavigationCapabilities *out)
{
    if (!out) {
        return RoboInvalidArgument;
    }
    out->elementHref = 1;
    out->controller = 1;
    out->fragmentNavigation = 1;
    out->locationState = 1;
    out->documentLoad = 1;
    out->preventDefault = 0;
    out->networking = 0;
    return RoboOk;
}
