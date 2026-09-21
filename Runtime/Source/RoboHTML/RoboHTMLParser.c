/*
 * RoboHTML parser boundary (Phase 8.1).
 * Exclusive lease + state machine only; no tokenization.
 */

#include "RoboHTML/RoboHTML.h"
#include "RoboHTML/RoboHTMLParser.h"
#include "RoboHTMLInternal.h"

#include "RoboBase/RoboLog.h"
#include "RoboMem/RoboMem.h"

#include <stddef.h>
#include <string.h>

int HtmlParserModeIsKnown(HtmlParserMode mode)
{
    switch (mode) {
    case RoboHtmlParserModeNone:
    case RoboHtmlParserModeComplete:
    case RoboHtmlParserModeIncremental:
        return 1;
    default:
        return 0;
    }
}

const char *HtmlParserModeName(HtmlParserMode mode)
{
    switch (mode) {
    case RoboHtmlParserModeNone:
        return "none";
    case RoboHtmlParserModeComplete:
        return "complete";
    case RoboHtmlParserModeIncremental:
        return "incremental";
    default:
        return "unknown";
    }
}

int HtmlParserStateIsKnown(HtmlParserState state)
{
    switch (state) {
    case RoboHtmlParserStateIdle:
    case RoboHtmlParserStateFeeding:
    case RoboHtmlParserStateComplete:
    case RoboHtmlParserStateFailed:
    case RoboHtmlParserStateDetached:
        return 1;
    default:
        return 0;
    }
}

const char *HtmlParserStateName(HtmlParserState state)
{
    switch (state) {
    case RoboHtmlParserStateIdle:
        return "idle";
    case RoboHtmlParserStateFeeding:
        return "feeding";
    case RoboHtmlParserStateComplete:
        return "complete";
    case RoboHtmlParserStateFailed:
        return "failed";
    case RoboHtmlParserStateDetached:
        return "detached";
    default:
        return "unknown";
    }
}

Result HtmlParserCreate(HtmlDocumentId documentId,
                                      HtmlParser **outParser,
                                      HtmlDiag *diag)
{
    HtmlDocumentSlot *slot;
    HtmlParser *p;

    if (!outParser) {
        return RoboHtmlDiag(diag, RoboHtmlDomainParser,
                              RoboInvalidArgument, "out_parser is NULL");
    }
    *outParser = NULL;
    if (!HtmlIsInitialized()) {
        return RoboHtmlDiag(diag, RoboHtmlDomainParser, RoboInitError,
                              "RoboHTML module not initialized");
    }
    slot = HtmlDocumentSlotFind(documentId);
    if (!slot) {
        return RoboHtmlDiag(diag, RoboHtmlDomainParser, RoboNotFound,
                              "unknown document id");
    }
    /*
     * Any lifecycle that accepts content accepts a parser, LOADING included:
     * LOADING is the state that means "a source is attached and waiting to be
     * parsed", so refusing a lease there would make the state unusable.
     * A FAILED/DESTROYING/DESTROYED document still refuses.
     */
    if (!HtmlDocumentLifecycleAllowsContent(slot->lifecycle)) {
        return RoboHtmlDiag(diag, RoboHtmlDomainParser, RoboError,
                              "document lifecycle does not accept a parser");
    }
    if (slot->leasedParser) {
        return RoboHtmlDiag(diag, RoboHtmlDomainParser, RoboBusy,
                              "document already has a parser lease");
    }
    p = (HtmlParser *)RoboCalloc(1u, sizeof(*p));
    if (!p) {
        return RoboHtmlDiag(diag, RoboHtmlDomainParser,
                              RoboOutOfMemory, "parser allocation failed");
    }
    p->magic = RoboHtmlParserMagic;
    p->documentId = documentId;
    p->state = RoboHtmlParserStateIdle;
    p->mode = RoboHtmlParserModeNone;
    p->sourceKind = RoboHtmlSourceKindNone;
    p->sourceLength = 0u;
    slot->leasedParser = p;
    *outParser = p;
    LogDebug("robo_html: parser leased doc=%llu",
                   (unsigned long long)documentId);
    return RoboOk;
}

Result HtmlParserAttachSource(HtmlParser *parser,
                                             const HtmlSource *src,
                                             HtmlDiag *diag)
{
    HtmlDocumentSlot *slot;
    HtmlParserMode mode;

    if (!parser || parser->magic != RoboHtmlParserMagic) {
        return RoboHtmlDiag(diag, RoboHtmlDomainParser,
                              RoboInvalidArgument, "parser is invalid");
    }
    if (!src) {
        return RoboHtmlDiag(diag, RoboHtmlDomainParser,
                              RoboInvalidArgument, "source is NULL");
    }
    slot = HtmlDocumentSlotFind(parser->documentId);
    if (!slot) {
        parser->state = RoboHtmlParserStateDetached;
        return RoboHtmlDiag(diag, RoboHtmlDomainParser, RoboNotFound,
                              "owning document no longer exists");
    }
    if (parser->state == RoboHtmlParserStateFeeding) {
        return RoboHtmlDiag(diag, RoboHtmlDomainParser, RoboBusy,
                              "source already attached");
    }
    if (parser->state == RoboHtmlParserStateFailed ||
        parser->state == RoboHtmlParserStateDetached ||
        parser->state == RoboHtmlParserStateComplete) {
        return RoboHtmlDiag(diag, RoboHtmlDomainParser, RoboError,
                              "parser state does not accept a source");
    }
    if (!HtmlDocumentLifecycleAllowsContent(slot->lifecycle)) {
        return RoboHtmlDiag(diag, RoboHtmlDomainParser, RoboError,
                              "document lifecycle does not allow parsing");
    }
    if (slot->loadPhase == RoboHtmlLoadPhaseAcquiring) {
        return RoboHtmlDiag(diag, RoboHtmlDomainParser, RoboBusy,
                              "resource acquisition outstanding");
    }
    if (src->kind == RoboHtmlSourceKindStream) {
        mode = RoboHtmlParserModeIncremental;
    } else {
        mode = RoboHtmlParserModeComplete;
    }
    /* Record declared encoding metadata through the encoding boundary. */
    if (src->encoding[0] != '\0') {
        (void)HtmlDocumentInputEncodingDeclared(parser->documentId,
                                                         src->encoding, diag);
    }
    parser->mode = mode;
    parser->sourceKind = src->kind;
    parser->sourceLength = HtmlSourceLength(src);
    parser->state = RoboHtmlParserStateFeeding;
    slot->sourceKind = src->kind;
    slot->sourceLength = parser->sourceLength;
    if (src->name[0] != '\0') {
        memcpy(slot->sourceName, src->name, sizeof(slot->sourceName) - 1u);
        slot->sourceName[sizeof(slot->sourceName) - 1u] = '\0';
    }
    (void)HtmlDocumentApplyTransition(
        slot, RoboHtmlDocumentLifecycleLoading);
    (void)HtmlDiagSet(diag, RoboOk, RoboHtmlDomainParser,
                             "source attached", __FILE__, __LINE__);
    LogDebug("robo_html: parser source attached doc=%llu mode=%s len=%llu",
                   (unsigned long long)parser->documentId,
                   HtmlParserModeName(mode),
                   (unsigned long long)parser->sourceLength);
    return RoboOk;
}

Result HtmlParserDetach(HtmlParser *parser,
                                      HtmlDiag *diag)
{
    HtmlDocumentSlot *slot;

    if (!parser || parser->magic != RoboHtmlParserMagic) {
        return RoboHtmlDiag(diag, RoboHtmlDomainParser,
                              RoboInvalidArgument, "parser is invalid");
    }
    if (parser->state == RoboHtmlParserStateDetached) {
        return RoboOk;
    }
    slot = HtmlDocumentSlotFind(parser->documentId);
    if (slot) {
        if (slot->leasedParser == parser) {
            slot->leasedParser = NULL;
        }
        /*
         * The source is abandoned, not finished, so the document settles
         * at INTERACTIVE: whatever was parsed into it is live, and no more
         * input is coming.
         */
        if (slot->lifecycle == RoboHtmlDocumentLifecycleLoading) {
            (void)HtmlDocumentApplyTransition(
                slot, RoboHtmlDocumentLifecycleInteractive);
        }
    }
    parser->documentId = RoboHtmlDocumentIdNone;
    parser->state = RoboHtmlParserStateDetached;
    parser->mode = RoboHtmlParserModeNone;
    parser->sourceKind = RoboHtmlSourceKindNone;
    parser->sourceLength = 0u;
    return RoboOk;
}

Result HtmlParserGetState(const HtmlParser *parser,
                                         HtmlParserState *outState)
{
    if (!parser || parser->magic != RoboHtmlParserMagic || !outState) {
        return RoboInvalidArgument;
    }
    *outState = parser->state;
    return RoboOk;
}

Result HtmlParserGetMode(const HtmlParser *parser,
                                        HtmlParserMode *outMode)
{
    if (!parser || parser->magic != RoboHtmlParserMagic || !outMode) {
        return RoboInvalidArgument;
    }
    *outMode = parser->mode;
    return RoboOk;
}

HtmlDocumentId HtmlParserDocument(
    const HtmlParser *parser)
{
    if (!parser || parser->magic != RoboHtmlParserMagic) {
        return RoboHtmlDocumentIdNone;
    }
    return parser->documentId;
}

void HtmlParserDestroy(HtmlParser *parser)
{
    if (!parser || parser->magic != RoboHtmlParserMagic) {
        return;
    }
    (void)HtmlParserDetach(parser, NULL);
    parser->magic = 0u;
    RoboFree(parser);
}
