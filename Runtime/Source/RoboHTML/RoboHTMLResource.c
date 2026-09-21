/*
 * RoboHTML resource/loading boundary (Phase 8.1).
 * Delegated-acquisition slot only; no I/O of any kind in RoboHTML.
 */

#include "RoboHTML/RoboHTMLResource.h"
#include "RoboHTMLInternal.h"

#include "RoboBase/RoboLog.h"

int HtmlLoadPhaseIsKnown(HtmlLoadPhase phase)
{
    switch (phase) {
    case RoboHtmlLoadPhaseNone:
    case RoboHtmlLoadPhaseAcquiring:
    case RoboHtmlLoadPhaseParsing:
    case RoboHtmlLoadPhaseDone:
    case RoboHtmlLoadPhaseFailed:
        return 1;
    default:
        return 0;
    }
}

const char *HtmlLoadPhaseName(HtmlLoadPhase phase)
{
    switch (phase) {
    case RoboHtmlLoadPhaseNone:
        return "none";
    case RoboHtmlLoadPhaseAcquiring:
        return "acquiring";
    case RoboHtmlLoadPhaseParsing:
        return "parsing";
    case RoboHtmlLoadPhaseDone:
        return "done";
    case RoboHtmlLoadPhaseFailed:
        return "failed";
    default:
        return "unknown";
    }
}

Result HtmlResourceBegin(HtmlDocumentId documentId,
                                       HtmlLoader *loader,
                                       HtmlDiag *diag)
{
    HtmlDocumentSlot *slot = HtmlDocumentSlotFind(documentId);

    if (!slot) {
        return RoboHtmlDiag(diag, RoboHtmlDomainResource,
                              RoboNotFound, "unknown document id");
    }
    if (slot->loadPhase == RoboHtmlLoadPhaseAcquiring) {
        return RoboHtmlDiag(diag, RoboHtmlDomainResource, RoboBusy,
                              "acquisition already outstanding");
    }
    if (!HtmlDocumentLifecycleAllowsSourceAttach(slot->lifecycle)) {
        return RoboHtmlDiag(diag, RoboHtmlDomainResource, RoboError,
                              "lifecycle does not allow acquisition");
    }
    slot->loadPhase = RoboHtmlLoadPhaseAcquiring;
    slot->loader = loader;
    LogDebug("robo_html: acquisition begin doc=%llu loader=%p",
                   (unsigned long long)documentId, (void *)loader);
    return RoboOk;
}

Result HtmlResourceEnd(HtmlDocumentId documentId,
                                     int ok,
                                     HtmlDiag *diag)
{
    HtmlDocumentSlot *slot = HtmlDocumentSlotFind(documentId);

    if (!slot) {
        return RoboHtmlDiag(diag, RoboHtmlDomainResource,
                              RoboNotFound, "unknown document id");
    }
    if (slot->loadPhase != RoboHtmlLoadPhaseAcquiring) {
        /* Idempotent no-op when nothing is outstanding. */
        return RoboOk;
    }
    slot->loader = NULL;
    if (ok) {
        slot->loadPhase = RoboHtmlLoadPhaseNone;
    } else {
        slot->loadPhase = RoboHtmlLoadPhaseFailed;
        (void)HtmlDocumentMarkFailed(documentId,
                                             RoboHtmlDomainResource,
                                             "acquisition failed", diag);
    }
    LogDebug("robo_html: acquisition end doc=%llu ok=%d",
                   (unsigned long long)documentId, ok);
    return RoboOk;
}

Result HtmlResourceCancel(HtmlDocumentId documentId,
                                        HtmlDiag *diag)
{
    HtmlDocumentSlot *slot = HtmlDocumentSlotFind(documentId);

    if (!slot) {
        return RoboHtmlDiag(diag, RoboHtmlDomainResource,
                              RoboNotFound, "unknown document id");
    }
    if (slot->loadPhase != RoboHtmlLoadPhaseAcquiring) {
        return RoboOk;
    }
    slot->loadPhase = RoboHtmlLoadPhaseNone;
    slot->loader = NULL;
    (void)HtmlDiagSet(diag, RoboOk, RoboHtmlDomainResource,
                             "acquisition cancelled", __FILE__, __LINE__);
    LogDebug("robo_html: acquisition cancelled doc=%llu",
                   (unsigned long long)documentId);
    return RoboOk;
}

Result HtmlResourceGetPhase(HtmlDocumentId documentId,
                                           HtmlLoadPhase *outPhase)
{
    const HtmlDocumentSlot *slot =
        HtmlDocumentSlotFind(documentId);

    if (!slot || !outPhase) {
        return RoboNotFound;
    }
    *outPhase = slot->loadPhase;
    return RoboOk;
}
