/*
 * RoboCSS stylesheet model (Phase 9.1) — see RoboCSSStyleSheet.h.
 *
 * Every public call resolves its id to a slot first, and a call with an id that
 * does not resolve reports RoboNotFound rather than doing nothing quietly.
 * The one exception is asked for by name in the header: the clear-and-forget
 * calls (clear_rules) tolerate a dead id, because "make sure nothing is left"
 * has the same answer whether or not there was something.
 */

#include "RoboCSS/RoboCSSStyleSheet.h"

#include "RoboCSSInternal.h"
#include "RoboCSS/RoboCSS.h"

Result CssStylesheetCreate(CssStylesheetId *outId,
                                         CssOrigin origin,
                                         CssDiag *diag)
{
    CssStylesheetSlot *slot;
    CssStylesheetId id;

    if (!outId) {
        return RoboCssDiag(diag, RoboCssDomainStylesheet, RoboInvalidArgument,
                             "out_id is NULL");
    }
    *outId = RoboCssStylesheetIdNone;
    if (!CssOriginIsKnown(origin)) {
        return RoboCssDiag(diag, RoboCssDomainStylesheet, RoboInvalidArgument,
                             "unknown stylesheet origin");
    }
    if (!CssIsInitialized()) {
        return RoboCssDiag(diag, RoboCssDomainStylesheet, RoboInitError,
                             "RoboCSS module is not initialized");
    }
    slot = CssSlotAlloc();
    if (!slot) {
        return RoboCssDiag(diag, RoboCssDomainStylesheet, RoboOutOfMemory,
                             "stylesheet table is full");
    }
    id = CssSlotClaim(slot, origin);
    *outId = id;
    return RoboOk;
}

Result CssStylesheetDestroy(CssStylesheetId id,
                                          CssDiag *diag)
{
    CssStylesheetSlot *slot = CssSlotFind(id);

    if (id == RoboCssStylesheetIdNone) {
        return RoboCssDiag(diag, RoboCssDomainStylesheet, RoboNotFound,
                             "no stylesheet id given");
    }
    if (!slot) {
        return RoboCssDiag(diag, RoboCssDomainStylesheet, RoboNotFound,
                             "stylesheet id does not resolve");
    }
    CssSlotRelease(slot);
    return RoboOk;
}

int CssStylesheetIsLive(CssStylesheetId id)
{
    return CssSlotFind(id) != NULL;
}

CssStylesheetId CssStylesheetIdAt(size_t index)
{
    return CssSlotIdAt(index);
}

Result CssStylesheetSetOrigin(CssStylesheetId id,
                                             CssOrigin origin,
                                             CssDiag *diag)
{
    CssStylesheetSlot *slot = CssSlotFind(id);

    if (!slot) {
        return RoboCssDiag(diag, RoboCssDomainStylesheet, RoboNotFound,
                             "stylesheet id does not resolve");
    }
    if (!CssOriginIsKnown(origin)) {
        return RoboCssDiag(diag, RoboCssDomainStylesheet, RoboInvalidArgument,
                             "unknown stylesheet origin");
    }
    slot->origin = origin;
    return RoboOk;
}

CssOrigin CssStylesheetOrigin(CssStylesheetId id)
{
    CssStylesheetSlot *slot = CssSlotFind(id);

    return slot ? slot->origin : RoboCssOriginNone;
}

CssStylesheetState MakeCssStylesheetState(CssStylesheetId id)
{
    CssStylesheetSlot *slot = CssSlotFind(id);

    return slot ? slot->state : RoboCssStylesheetStateNone;
}

Result CssStylesheetSetState(CssStylesheetId id,
                                            CssStylesheetState state,
                                            CssDiag *diag)
{
    CssStylesheetSlot *slot = CssSlotFind(id);

    if (!slot) {
        return RoboCssDiag(diag, RoboCssDomainStylesheet, RoboNotFound,
                             "stylesheet id does not resolve");
    }
    if (state != RoboCssStylesheetStateCreated &&
        state != RoboCssStylesheetStateParsed &&
        state != RoboCssStylesheetStateFailed) {
        return RoboCssDiag(diag, RoboCssDomainStylesheet, RoboInvalidArgument,
                             "a live stylesheet is created, parsed or failed");
    }
    slot->state = state;
    return RoboOk;
}

Result CssStylesheetSetName(CssStylesheetId id,
                                           const char *name,
                                           CssDiag *diag)
{
    CssStylesheetSlot *slot = CssSlotFind(id);

    if (!slot) {
        return RoboCssDiag(diag, RoboCssDomainStylesheet, RoboNotFound,
                             "stylesheet id does not resolve");
    }
    CssCopyLabel(slot->name, sizeof(slot->name), name);
    return RoboOk;
}

const char *CssStylesheetName(CssStylesheetId id)
{
    CssStylesheetSlot *slot = CssSlotFind(id);

    return slot ? slot->name : NULL;
}

/* ------------------------------------------------------------------ */
/* Source attachment                                                   */
/* ------------------------------------------------------------------ */

Result CssStylesheetSetSourceMemory(CssStylesheetId id,
                                                    const uint8_t *bytes,
                                                    size_t length,
                                                    CssDiag *diag)
{
    CssStylesheetSlot *slot = CssSlotFind(id);

    if (!slot) {
        return RoboCssDiag(diag, RoboCssDomainStylesheet, RoboNotFound,
                             "stylesheet id does not resolve");
    }
    return CssSourceSetMemory(&slot->source, bytes, length, diag);
}

Result CssStylesheetSetSourceString(CssStylesheetId id,
                                                    const char *utf8,
                                                    CssDiag *diag)
{
    CssStylesheetSlot *slot = CssSlotFind(id);

    if (!slot) {
        return RoboCssDiag(diag, RoboCssDomainStylesheet, RoboNotFound,
                             "stylesheet id does not resolve");
    }
    return CssSourceSetString(&slot->source, utf8, diag);
}

Result CssStylesheetCopySource(CssStylesheetId id,
                                             const uint8_t *bytes,
                                             size_t length,
                                             CssDiag *diag)
{
    CssStylesheetSlot *slot = CssSlotFind(id);

    if (!slot) {
        return RoboCssDiag(diag, RoboCssDomainStylesheet, RoboNotFound,
                             "stylesheet id does not resolve");
    }
    return CssSourceCopy(&slot->source, bytes, length, diag);
}

const CssSource *CssStylesheetSource(CssStylesheetId id)
{
    CssStylesheetSlot *slot = CssSlotFind(id);

    if (!slot) {
        return NULL;
    }
    /* A stylesheet created a moment ago has no source configured; reporting the
     * slot's source object would hand back a KIND_NONE view that a caller would
     * have to know how to read. "No source" is the honest answer. */
    if (!CssSourceIsConfigured(&slot->source)) {
        return NULL;
    }
    return &slot->source;
}

size_t CssStylesheetSourceLength(CssStylesheetId id)
{
    CssStylesheetSlot *slot = CssSlotFind(id);

    return slot ? CssSourceLength(&slot->source) : 0u;
}

int CssStylesheetOwnsSource(CssStylesheetId id)
{
    CssStylesheetSlot *slot = CssSlotFind(id);

    return slot && CssSourceIsOwned(&slot->source);
}

/* ------------------------------------------------------------------ */
/* Rules                                                               */
/* ------------------------------------------------------------------ */

Result CssStylesheetAppendRule(CssStylesheetId id,
                                              CssRule *rule,
                                              CssDiag *diag)
{
    CssStylesheetSlot *slot = CssSlotFind(id);

    if (!slot) {
        return RoboCssDiag(diag, RoboCssDomainStylesheet, RoboNotFound,
                             "stylesheet id does not resolve");
    }
    return CssRuleListAppend(&slot->rules, rule, diag);
}

size_t CssStylesheetRuleCount(CssStylesheetId id)
{
    CssStylesheetSlot *slot = CssSlotFind(id);

    return slot ? CssRuleListCount(&slot->rules) : 0u;
}

const CssRule *CssStylesheetRule(CssStylesheetId id,
                                                size_t index)
{
    CssStylesheetSlot *slot = CssSlotFind(id);

    if (!slot) {
        return NULL;
    }
    return CssRuleListAt(&slot->rules, index);
}

size_t CssStylesheetTreeRuleCount(CssStylesheetId id)
{
    CssStylesheetSlot *slot = CssSlotFind(id);

    return slot ? CssRuleListTreeCount(&slot->rules) : 0u;
}

size_t CssStylesheetStyleRuleCount(CssStylesheetId id)
{
    CssStylesheetSlot *slot = CssSlotFind(id);

    return slot ? CssRuleListStyleRuleCount(&slot->rules) : 0u;
}

Result CssStylesheetRemoveRule(CssStylesheetId id,
                                              size_t index,
                                              CssDiag *diag)
{
    CssStylesheetSlot *slot = CssSlotFind(id);
    size_t i;

    if (!slot) {
        return RoboCssDiag(diag, RoboCssDomainStylesheet, RoboNotFound,
                             "stylesheet id does not resolve");
    }
    if (index >= slot->rules.count) {
        return RoboCssDiag(diag, RoboCssDomainStylesheet, RoboNotFound,
                             "rule index is past the end of the stylesheet");
    }
    CssRuleDispose(&slot->rules.items[index]);
    /* Close the gap by moving the tail down, which is the same operation as
     * removing from the middle of any ordered list and keeps the remaining
     * rules in source order. */
    for (i = index + 1u; i < slot->rules.count; i++) {
        slot->rules.items[i - 1u] = slot->rules.items[i];
        CssRuleInit(&slot->rules.items[i]);
    }
    slot->rules.count--;
    return RoboOk;
}

void CssStylesheetClearRules(CssStylesheetId id)
{
    CssStylesheetSlot *slot = CssSlotFind(id);

    if (!slot) {
        return;
    }
    CssRuleListClear(&slot->rules);
}

const CssRuleList *CssStylesheetRules(CssStylesheetId id)
{
    CssStylesheetSlot *slot = CssSlotFind(id);

    return slot ? &slot->rules : NULL;
}

/* ------------------------------------------------------------------ */
/* Parse boundary                                                      */
/* ------------------------------------------------------------------ */

Result CssStylesheetParse(CssStylesheetId id,
                                        CssError *outError,
                                        CssDiag *diag)
{
    CssStylesheetSlot *slot = CssSlotFind(id);
    CssRuleList parsed = RoboCssCleared;
    CssError problem = RoboCssCleared;
    Result result;

    if (!slot) {
        CssErrorSet(outError, RoboCssErrorKindNone, RoboNotFound, NULL,
                           "stylesheet id does not resolve");
        return RoboCssDiag(diag, RoboCssDomainStylesheet, RoboNotFound,
                             "stylesheet id does not resolve");
    }
    /* Transactional: the parse builds into its own list, and only a parse that
     * returned RoboOk replaces the stylesheet's rules. So a second parse is
     * idempotent rather than additive, and an attempt that could not run leaves
     * the rules exactly as they were. */
    result = CssGrammarParseStylesheet(&parsed, &slot->source, slot->origin,
                                               &problem);
    if (result != RoboOk) {
        slot->state = RoboCssStylesheetStateFailed;
        slot->hasError = 1;
        slot->error = problem;
        CssRuleListDispose(&parsed);
        if (outError) {
            *outError = problem;
        }
        return RoboCssDiag(diag, RoboCssDomainParser, result,
                             CssErrorMessage(&problem));
    }
    CssRuleListDispose(&slot->rules);
    slot->rules = parsed;
    /* A recovered parse is still a parse: the rules that survived are real
     * rules, so the state is PARSED and what went wrong lives in the error. */
    slot->state = RoboCssStylesheetStateParsed;
    /* The first CSS problem is kept on the stylesheet whether or not the caller
     * asked for a copy, which is what makes it survive a call site that did not
     * want it. The caller's record is optional and must stay optional:
     * CssErrorSet tolerates a NULL destination, so this does too. */
    slot->hasError = CssErrorIsError(&problem) ? 1 : 0;
    slot->error = problem;
    if (outError) {
        *outError = problem;
    }
    return RoboOk;
}

int CssStylesheetLastError(CssStylesheetId id,
                                   CssError *outError)
{
    CssStylesheetSlot *slot = CssSlotFind(id);

    if (!slot || !slot->hasError) {
        CssErrorInit(outError);
        return 0;
    }
    if (outError) {
        *outError = slot->error;
    }
    return 1;
}

void CssStylesheetSetLastError(CssStylesheetId id,
                                        const CssError *error)
{
    CssStylesheetSlot *slot = CssSlotFind(id);

    if (!slot) {
        return;
    }
    if (!error || error->kind == RoboCssErrorKindNone) {
        slot->hasError = 0;
        CssErrorInit(&slot->error);
        return;
    }
    slot->hasError = 1;
    slot->error = *error;
}
