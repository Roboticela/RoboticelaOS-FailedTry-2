/*
 * RoboHTML client/render integration boundary (Phase 8.1).
 * Hook installation + synchronous emission; RoboHTML never draws.
 */

#include "RoboHTML/RoboHTMLClient.h"
#include "RoboHTMLInternal.h"

#include <stddef.h>
#include <string.h>

int HtmlInvalidationReasonIsKnown(
    HtmlInvalidationReason reason)
{
    switch (reason) {
    case RoboHtmlInvalidationNone:
    case RoboHtmlInvalidationSource:
    case RoboHtmlInvalidationLifecycle:
    case RoboHtmlInvalidationParsing:
    case RoboHtmlInvalidationStyle:
    case RoboHtmlInvalidationLayout:
    case RoboHtmlInvalidationPaint:
        return 1;
    default:
        return 0;
    }
}

const char *HtmlInvalidationReasonName(
    HtmlInvalidationReason reason)
{
    switch (reason) {
    case RoboHtmlInvalidationNone:
        return "none";
    case RoboHtmlInvalidationSource:
        return "source";
    case RoboHtmlInvalidationLifecycle:
        return "lifecycle";
    case RoboHtmlInvalidationParsing:
        return "parsing";
    case RoboHtmlInvalidationStyle:
        return "style";
    case RoboHtmlInvalidationLayout:
        return "layout";
    case RoboHtmlInvalidationPaint:
        return "paint";
    default:
        return "unknown";
    }
}

Result HtmlDocumentSetClient(
    HtmlDocumentId documentId,
    const HtmlClientHooks *hooks,
    void *userdata,
    HtmlDiag *diag)
{
    HtmlDocumentSlot *slot = HtmlDocumentSlotFind(documentId);

    if (!slot) {
        return RoboHtmlDiag(diag, RoboHtmlDomainDocument,
                              RoboNotFound, "unknown document id");
    }
    if (hooks && slot->client.OnDocumentLifecycle) {
        return RoboHtmlDiag(diag, RoboHtmlDomainClient,
                              RoboAlreadyExists,
                              "client already installed; clear first");
    }
    if (hooks) {
        slot->client = *hooks;
        slot->clientUserdata = userdata;
    } else {
        memset(&slot->client, 0, sizeof(slot->client));
        slot->clientUserdata = NULL;
    }
    return RoboOk;
}

int HtmlDocumentHasClient(HtmlDocumentId documentId)
{
    const HtmlDocumentSlot *slot =
        HtmlDocumentSlotFind(documentId);

    if (!slot) {
        return 0;
    }
    return slot->client.OnDocumentLifecycle != NULL ||
           slot->client.OnInvalidate != NULL;
}

void HtmlEmitLifecycle(HtmlDocumentSlot *slot,
                              HtmlDocumentLifecycle from,
                              HtmlDocumentLifecycle to)
{
    if (!slot || !slot->client.OnDocumentLifecycle) {
        return;
    }
    slot->client.OnDocumentLifecycle(slot->id, from, to,
                                       slot->clientUserdata);
}

void HtmlEmitInvalidate(HtmlDocumentSlot *slot,
                               HtmlInvalidationReason reason)
{
    if (!slot || !slot->client.OnInvalidate) {
        return;
    }
    slot->client.OnInvalidate(slot->id, reason, slot->clientUserdata);
}
