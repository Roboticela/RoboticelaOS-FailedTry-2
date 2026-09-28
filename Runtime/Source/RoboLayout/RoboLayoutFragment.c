/*
 * RoboLayout fragmentation (Phase 19.7) — see `RoboLayoutFragment.h` for the contract.
 *
 * The file is four readers and one policy, in the order a caller meets them:
 *
 *   the **vocabulary**   the container kinds, the actions and the reasons, each with a
 *                        name and its own table row, so nothing below compares a
 *                        spelling;
 *
 *   the **boundary**     the record a caller fills in -- two `break` declarations, the
 *                        interior refusal, the room and whether anything precedes the
 *                        box -- and its validity, which is where "a declaration of `auto`
 *                        and a declaration of nothing" are told apart from a record that
 *                        was never filled in;
 *
 *   the **policy**       one function, and it is where the phase's six rules live. It is
 *                        one function rather than four for the reason
 *                        `LayoutAbsoluteResolveAxis` is: the six rules are a precedence
 *                        between four facts, and the two places a second copy would
 *                        drift are the two that matter -- whether a forced break beats an
 *                        `avoid` (it does, and from either side of the boundary), and
 *                        whether the start of a fragment suppresses a forced break (it
 *                        does, because the content starts the fragment anyway);
 *
 *   the **fragment**     the record a pass reports and the counters it reports through.
 *                        A fragment is an ordinal and a block range rather than a
 *                        rectangle, because the split is this module's question and the
 *                        geometry is the pass's answer.
 *
 * Two classification questions are asked of RoboCSS's own tables rather than re-derived
 * here. A forced break is asked what container it names
 * (`CssBreakBetweenForcesColumn` / `...ForcesPage`), and an interior refusal is asked
 * whether it is about this container -- which is why the module needs no `if (kind ==
 * ...)` anywhere except the three places a kind *is* the answer. There is deliberately no
 * second copy of the `break-between` vocabulary in this file: the words live in
 * RoboCSSFragment.h and the meanings live here, which is the same split 14.0 made
 * between `LayoutPositionOf` and the out-of-flow policy.
 *
 * No allocations, no state between calls, no cache: every function below is a function of
 * the records it was handed, exactly as 10.12's and 14.0's are, because a re-layout is
 * this module being asked again.
 */

#include "RoboLayout/RoboLayoutFragment.h"

#include "RoboLayoutInternal.h"

#include <stdio.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/* The vocabulary                                                      */
/* ------------------------------------------------------------------ */

typedef struct fragmentainerKindRow {
    LayoutFragmentainerKind kind;
    const char *name;
    int establishes;
} FragmentainerKindRow;

static const FragmentainerKindRow fragmentainerKindVocabulary[] = {
    { LayoutFragmentainerKindNone,   "none",   0 },
    { LayoutFragmentainerKindColumn, "column", 1 },
    { LayoutFragmentainerKindPage,   "page",   1 },
    { LayoutFragmentainerKindRegion, "region", 1 }
};

#define FRAGMENTAINER_KIND_COUNT \
    (sizeof(fragmentainerKindVocabulary) / sizeof(fragmentainerKindVocabulary[0]))

static const FragmentainerKindRow *FragmentainerKindRowOf(LayoutFragmentainerKind kind)
{
    size_t index;

    for (index = 0u; index < FRAGMENTAINER_KIND_COUNT; index++) {
        if (fragmentainerKindVocabulary[index].kind == kind) {
            return &fragmentainerKindVocabulary[index];
        }
    }
    return NULL;
}

int LayoutFragmentainerKindIsKnown(LayoutFragmentainerKind kind)
{
    return FragmentainerKindRowOf(kind) != NULL;
}

const char *LayoutFragmentainerKindName(LayoutFragmentainerKind kind)
{
    const FragmentainerKindRow *row = FragmentainerKindRowOf(kind);

    return row ? row->name : "";
}

int LayoutFragmentainerKindEstablishes(LayoutFragmentainerKind kind)
{
    const FragmentainerKindRow *row = FragmentainerKindRowOf(kind);

    /* An unknown kind establishes nothing, which is the answer that makes a typo a
     * refusal rather than a container. */
    return row ? row->establishes : 0;
}

LayoutFragmentainerKind LayoutFragmentainerKindOfForcedBreak(CssBreakBetween force)
{
    /*
     * The three questions are asked in the order the answers matter, and the order is
     * forced rather than tidy: `always` is the generic forced break and both of RoboCSS's
     * questions answer it, so a caller that asked "is it a page break?" first would have to
     * decide which container `always` *is* -- and the honest answer is neither, it is both.
     * Asking the column question first makes `always` a column, which is the one container
     * this build lays out, and that is a stated choice rather than a tie broken silently.
     */
    if (CssBreakBetweenForcesColumn(force)) {
        return LayoutFragmentainerKindColumn;
    }
    if (CssBreakBetweenForcesPage(force)) {
        return LayoutFragmentainerKindPage;
    }
    /* `region` is the case this function exists to get right: it is **forced** (RoboCSS's
     * table says so) and it names a container this build does not lay out, so the answer is
     * NONE -- a kind, not a failure, and the caller still sees `Forced` on the action. */
    return LayoutFragmentainerKindNone;
}

typedef struct breakActionRow {
    LayoutBreakAction action;
    const char *name;
} BreakActionRow;

static const BreakActionRow breakActionVocabulary[] = {
    { LayoutBreakForbidden, "forbidden" },
    { LayoutBreakAllowed,   "allowed" },
    { LayoutBreakForced,    "forced" }
};

#define BREAK_ACTION_COUNT \
    (sizeof(breakActionVocabulary) / sizeof(breakActionVocabulary[0]))

int LayoutBreakActionIsKnown(LayoutBreakAction action)
{
    size_t index;

    for (index = 0u; index < BREAK_ACTION_COUNT; index++) {
        if (breakActionVocabulary[index].action == action) {
            return 1;
        }
    }
    return 0;
}

const char *LayoutBreakActionName(LayoutBreakAction action)
{
    size_t index;

    for (index = 0u; index < BREAK_ACTION_COUNT; index++) {
        if (breakActionVocabulary[index].action == action) {
            return breakActionVocabulary[index].name;
        }
    }
    return "";
}

typedef struct breakReasonRow {
    LayoutBreakReason reason;
    const char *name;
} BreakReasonRow;

static const BreakReasonRow breakReasonVocabulary[] = {
    { LayoutBreakReasonNoContext,             "no_context" },
    { LayoutBreakReasonAuto,                  "auto" },
    { LayoutBreakReasonForcedByDeclaration,   "forced" },
    { LayoutBreakReasonSuppressedAtStart,     "suppressed_at_start" },
    { LayoutBreakReasonAvoidedByDeclaration,  "avoided" },
    { LayoutBreakReasonAvoidedInside,         "avoided_inside" },
    { LayoutBreakReasonNoRoom,                "no_room" },
    { LayoutBreakReasonOrphansFloor,          "orphans" },
    { LayoutBreakReasonWidowsFloor,           "widows" }
};

#define BREAK_REASON_COUNT \
    (sizeof(breakReasonVocabulary) / sizeof(breakReasonVocabulary[0]))

int LayoutBreakReasonIsKnown(LayoutBreakReason reason)
{
    size_t index;

    for (index = 0u; index < BREAK_REASON_COUNT; index++) {
        if (breakReasonVocabulary[index].reason == reason) {
            return 1;
        }
    }
    return 0;
}

const char *LayoutBreakReasonName(LayoutBreakReason reason)
{
    size_t index;

    for (index = 0u; index < BREAK_REASON_COUNT; index++) {
        if (breakReasonVocabulary[index].reason == reason) {
            return breakReasonVocabulary[index].name;
        }
    }
    return "";
}

/* ------------------------------------------------------------------ */
/* The boundary                                                        */
/* ------------------------------------------------------------------ */

void LayoutFragmentBoundaryInit(LayoutFragmentBoundary *boundary)
{
    if (!boundary) {
        return;
    }
    boundary->before = RoboCssBreakBetweenAuto;
    boundary->after = RoboCssBreakBetweenAuto;
    boundary->inside = RoboCssBreakInsideAuto;
    /* Room **set**, and nothing before it cleared: the state of a box arriving at an empty
     * container with space. A zeroed record would mean "full", and a policy whose zero
     * breaks every document is a policy that punishes a caller for forgetting a field. */
    boundary->roomLeft = 1;
    boundary->anythingBefore = 0;
}

int LayoutFragmentBoundaryIsValid(const LayoutFragmentBoundary *boundary)
{
    if (!boundary) {
        return 0;
    }
    if (!CssBreakBetweenIsKnown(boundary->before) ||
        !CssBreakBetweenIsKnown(boundary->after) ||
        !CssBreakInsideIsKnown(boundary->inside)) {
        return 0;
    }
    if (boundary->roomLeft != 0 && boundary->roomLeft != 1) {
        return 0;
    }
    if (boundary->anythingBefore != 0 && boundary->anythingBefore != 1) {
        return 0;
    }
    return 1;
}

int LayoutFragmentBoundaryEqual(const LayoutFragmentBoundary *a,
                                const LayoutFragmentBoundary *b)
{
    if (!a || !b) {
        return a == b;
    }
    return a->before == b->before && a->after == b->after && a->inside == b->inside &&
           a->roomLeft == b->roomLeft && a->anythingBefore == b->anythingBefore;
}

/* ------------------------------------------------------------------ */
/* The decision                                                        */
/* ------------------------------------------------------------------ */

void LayoutBreakDecisionInit(LayoutBreakDecision *decision)
{
    if (!decision) {
        return;
    }
    decision->action = LayoutBreakForbidden;
    decision->reason = LayoutBreakReasonNoContext;
    decision->forcedBy = LayoutFragmentainerKindNone;
}

int LayoutBreakDecisionIsValid(const LayoutBreakDecision *decision)
{
    if (!decision) {
        return 0;
    }
    if (!LayoutBreakActionIsKnown(decision->action) ||
        !LayoutBreakReasonIsKnown(decision->reason)) {
        return 0;
    }
    if (!LayoutFragmentainerKindIsKnown(decision->forcedBy)) {
        return 0;
    }
    /*
     * The one cross-field rule, and it is the record saying what it means: `forcedBy` is
     * the container a *declaration* named, so it is non-NONE exactly when the action is
     * `Forced` **and** the reason is `ForcedByDeclaration`. A `Forced` action with any other
     * reason would be a break nobody asked for, and a reason of `ForcedByDeclaration` with a
     * `Forbidden` action would be a declaration that forced nothing -- both are records this
     * build must not read, and a caller that built one by hand is told so.
     */
    if (decision->action == LayoutBreakForced) {
        if (decision->reason != LayoutBreakReasonForcedByDeclaration) {
            return 0;
        }
    } else if (decision->reason == LayoutBreakReasonForcedByDeclaration) {
        return 0;
    }
    if (decision->reason == LayoutBreakReasonForcedByDeclaration) {
        /* `forcedBy` may be NONE here and that is on purpose: `region` is a forced break no
         * container in this build names, so the declaration forced a break the container
         * did not choose. Requiring a non-NONE kind would refuse a record the policy itself
         * produces. */
        return 1;
    }
    return decision->forcedBy == LayoutFragmentainerKindNone;
}

int LayoutBreakDecisionEqual(const LayoutBreakDecision *a,
                             const LayoutBreakDecision *b)
{
    if (!a || !b) {
        return a == b;
    }
    return a->action == b->action && a->reason == b->reason &&
           a->forcedBy == b->forcedBy;
}

/** Write one decision into @p out. The three setters, so no branch repeats the field
 *  list -- and so a field added later is one edit rather than one per rule. */
static void FragmentDecide(LayoutBreakDecision *out,
                           LayoutBreakAction action,
                           LayoutBreakReason reason,
                           LayoutFragmentainerKind forcedBy)
{
    out->action = action;
    out->reason = reason;
    out->forcedBy = forcedBy;
}

int LayoutFragmentInsideRefuses(CssBreakInside inside, LayoutFragmentainerKind kind)
{
    if (!CssBreakInsideIsKnown(inside) || !LayoutFragmentainerKindEstablishes(kind)) {
        return 0;
    }
    /* `avoid` refuses everywhere, and the three container-named refusals refuse in the one
     * they name. The generic question is asked first because it is the *whole* answer for
     * `avoid` -- and because `avoid` is the value both vocabularies share, which is the fact
     * RoboCSSFragment.h spells out. */
    if (inside == RoboCssBreakInsideAvoid) {
        return 1;
    }
    switch (kind) {
    case LayoutFragmentainerKindColumn:
        return inside == RoboCssBreakInsideAvoidColumn;
    case LayoutFragmentainerKindPage:
        return inside == RoboCssBreakInsideAvoidPage;
    case LayoutFragmentainerKindRegion:
        return inside == RoboCssBreakInsideAvoidRegion;
    default:
        return 0;
    }
}

/** The container a **forced** break names, or NONE when neither declaration forces. */
static LayoutFragmentainerKind FragmentForcedContainer(
    const LayoutFragmentBoundary *boundary)
{
    LayoutFragmentainerKind byBefore =
        LayoutFragmentainerKindOfForcedBreak(boundary->before);

    if (byBefore != LayoutFragmentainerKindNone) {
        return byBefore;
    }
    return LayoutFragmentainerKindOfForcedBreak(boundary->after);
}

/**
 * Whether either declaration at this boundary **forces** a break.
 *
 * Asked of the declarations directly (`CssBreakBetweenIsForced`) and deliberately **not**
 * through the container a forced break names. The two questions look like one and are not:
 * `region` is forced and names a container this build does not have, so
 * `FragmentForcedContainer` answers NONE for it -- and a policy that tested the container
 * instead of the declaration would let `break-before: region` fall through to the room rule
 * and break nothing. That is a real defect this suite caught, and the two functions are kept
 * apart so it cannot come back: `forced` is a fact about the declaration and the container
 * is a fact about the container.
 */
static int FragmentForces(const LayoutFragmentBoundary *boundary)
{
    return CssBreakBetweenIsForced(boundary->before) ||
           CssBreakBetweenIsForced(boundary->after);
}

Result LayoutFragmentBreakDecide(const LayoutFragmentBoundary *boundary,
                                 LayoutFragmentainerKind kind,
                                 LayoutBreakDecision *out,
                                 LayoutDiag *diag)
{
    LayoutFragmentainerKind forcedBy;

    if (!boundary || !out) {
        return RoboLayoutDiag(diag, RoboLayoutDomainFragment, RoboInvalidArgument,
                              "a break decision needs a boundary and an out");
    }
    /*
     * Cleared **before** the validity checks, and the order is deliberate: a refusal must
     * leave behind the record that says nothing may break here rather than whatever the
     * caller's variable held. A caller that ignored the status then reads a refusal instead
     * of a stale answer, which is the only failure mode a status code can have.
     */
    LayoutBreakDecisionInit(out);
    if (!LayoutFragmentBoundaryIsValid(boundary)) {
        return RoboLayoutDiag(diag, RoboLayoutDomainFragment, RoboInvalidArgument,
                              "the boundary is not a record this build may read");
    }
    if (!LayoutFragmentainerKindIsKnown(kind)) {
        return RoboLayoutDiag(diag, RoboLayoutDomainFragment, RoboInvalidArgument,
                              "the container kind is not one this build knows");
    }

    /* Rule 1: a box that establishes no fragmentation context cannot break. The check is on
     * the *kind* rather than on the declarations, because a `break-before: page` inside a
     * non-container is a declaration with nothing to act on -- the answer is a refusal that
     * names the reason, not an error, so the document still lays out. */
    if (!LayoutFragmentainerKindEstablishes(kind)) {
        FragmentDecide(out, LayoutBreakForbidden, LayoutBreakReasonNoContext,
                       LayoutFragmentainerKindNone);
        return RoboOk;
    }

    forcedBy = FragmentForcedContainer(boundary);

    /* Rule 2: nothing precedes the box, so a break here would produce an empty fragment.
     * CSS suppresses it, and this is the boundary where the suppression is *counted*: a
     * document that wrote `break-before: page` on the first box of a container asked for an
     * empty page, and saying no is an answer. An `avoid` at the start is not suppressed --
     * it is trivially satisfied, because there is nothing to separate -- and it falls through
     * to rule 4, which answers the same action with the reason that says why. */
    if (!boundary->anythingBefore && FragmentForces(boundary)) {
        FragmentDecide(out, LayoutBreakAllowed, LayoutBreakReasonSuppressedAtStart,
                       LayoutFragmentainerKindNone);
        return RoboOk;
    }

    /* Rule 3: a forced break wins over an `avoid`, from either side of the boundary and
     * however the two are ordered -- which is CSS's rule and the reason `forcedBy` is not
     * narrowed to the container's own kind. A `page` break inside a *column* container is
     * still forced; `forcedBy` records that the declaration named a page, which is the fact a
     * caller reporting the document needs. */
    if (FragmentForces(boundary)) {
        FragmentDecide(out, LayoutBreakForced, LayoutBreakReasonForcedByDeclaration,
                       forcedBy);
        return RoboOk;
    }

    /* Rule 4: a declaration forbade a break. Asked of both sides, because the two
     * declarations meet here and either one refuses the boundary. */
    if (CssBreakBetweenIsAvoided(boundary->before) ||
        CssBreakBetweenIsAvoided(boundary->after)) {
        FragmentDecide(out, LayoutBreakForbidden, LayoutBreakReasonAvoidedByDeclaration,
                       LayoutFragmentainerKindNone);
        return RoboOk;
    }

    /* Rule 5: the box refuses to be split *and* it does not fit, so it moves whole to the
     * next fragment. Both halves are required: a box whose `break-inside` is `avoid` and that
     * fits needs no break at all, and answering `Forbidden`/`AvoidedInside` there would be
     * reporting a refusal that nothing required. */
    if (!boundary->roomLeft && LayoutFragmentInsideRefuses(boundary->inside, kind)) {
        FragmentDecide(out, LayoutBreakForbidden, LayoutBreakReasonAvoidedInside,
                       LayoutFragmentainerKindNone);
        return RoboOk;
    }

    /* Rule 6: room decides the automatic case -- the only break no author asked for. */
    if (boundary->roomLeft) {
        FragmentDecide(out, LayoutBreakAllowed, LayoutBreakReasonAuto,
                       LayoutFragmentainerKindNone);
        return RoboOk;
    }
    FragmentDecide(out, LayoutBreakForbidden, LayoutBreakReasonNoRoom,
                   LayoutFragmentainerKindNone);
    return RoboOk;
}

Result LayoutFragmentBreakLines(int totalLines,
                                int breakAfterLine,
                                int orphans,
                                int widows,
                                LayoutBreakDecision *out,
                                LayoutDiag *diag)
{
    if (!out) {
        return RoboLayoutDiag(diag, RoboLayoutDomainFragment, RoboInvalidArgument,
                              "a line-floor decision needs an out");
    }
    /* Cleared first, for the reason `LayoutFragmentBreakDecide` clears first: a refusal
     * leaves a refusal behind rather than a stale record. */
    LayoutBreakDecisionInit(out);
    if (totalLines < 0 || breakAfterLine < 0 || orphans < 0 || widows < 0) {
        return RoboLayoutDiag(diag, RoboLayoutDomainFragment, RoboInvalidArgument,
                              "a line count and a line floor are never negative");
    }

    /*
     * The two degenerate cases first, and both are refusals with a reason rather than
     * assertions: breaking before the first line would empty the fragment, and breaking at or
     * after the last line is not a break *inside* the text at all. `NoRoom` is the honest
     * reason for both -- "no break can fall here" -- because neither is a floor being
     * breached, and reporting `OrphansFloor` for a break that has no lines before it would
     * blame the wrong rule.
     */
    if (breakAfterLine < 1 || breakAfterLine >= totalLines) {
        FragmentDecide(out, LayoutBreakForbidden, LayoutBreakReasonNoRoom,
                       LayoutFragmentainerKindNone);
        return RoboOk;
    }

    /* Two floors, asked in the order the fragment is read: what stays, then what travels.
     * `<=` on the second is the half a build gets wrong -- a break that leaves exactly
     * `widows` lines ahead is *satisfied*, not refused. */
    if (breakAfterLine < orphans) {
        FragmentDecide(out, LayoutBreakForbidden, LayoutBreakReasonOrphansFloor,
                       LayoutFragmentainerKindNone);
        return RoboOk;
    }
    if (totalLines - breakAfterLine < widows) {
        FragmentDecide(out, LayoutBreakForbidden, LayoutBreakReasonWidowsFloor,
                       LayoutFragmentainerKindNone);
        return RoboOk;
    }
    FragmentDecide(out, LayoutBreakAllowed, LayoutBreakReasonAuto,
                   LayoutFragmentainerKindNone);
    return RoboOk;
}

/* ------------------------------------------------------------------ */
/* A fragment                                                          */
/* ------------------------------------------------------------------ */

void LayoutFragmentInit(LayoutFragment *fragment)
{
    if (!fragment) {
        return;
    }
    memset(fragment, 0, sizeof(*fragment));
    fragment->node = RoboLayoutNodeIdNone;
    fragment->kind = LayoutFragmentainerKindNone;
}

int LayoutFragmentIsValid(const LayoutFragment *fragment)
{
    if (!fragment) {
        return 0;
    }
    if (fragment->node == RoboLayoutNodeIdNone) {
        return 0;
    }
    /* A fragment lives in a container, so the kind must establish one: a fragment of a box
     * that establishes no fragmentation context is not a record this build produced. */
    if (!LayoutFragmentainerKindEstablishes(fragment->kind)) {
        return 0;
    }
    if (fragment->blockOffset < 0 || fragment->blockExtent < 0) {
        return 0;
    }
    if (fragment->last != 0 && fragment->last != 1) {
        return 0;
    }
    return 1;
}

int LayoutFragmentEqual(const LayoutFragment *a, const LayoutFragment *b)
{
    if (!a || !b) {
        return a == b;
    }
    return a->node == b->node && a->kind == b->kind && a->index == b->index &&
           a->blockOffset == b->blockOffset && a->blockExtent == b->blockExtent &&
           a->last == b->last;
}

Result LayoutFragmentWrite(const LayoutFragment *fragment,
                           char *buffer,
                           size_t capacity,
                           size_t *outLength,
                           LayoutDiag *diag)
{
    char line[RoboLayoutFragmentTextMax];
    int written = 0;
    size_t length = 0u;

    if (!fragment || !buffer || !outLength || capacity == 0u) {
        return RoboLayoutDiag(diag, RoboLayoutDomainFragment, RoboInvalidArgument,
                              "a fragment dump needs a record, a buffer and an out");
    }
    buffer[0] = '\0';
    *outLength = 0u;

    written = snprintf(line, sizeof(line),
                       "fragment node=%u container=%s index=%u offset=%d extent=%d "
                       "last=%d",
                       (unsigned)fragment->node,
                       LayoutFragmentainerKindName(fragment->kind),
                       (unsigned)fragment->index,
                       (int)fragment->blockOffset,
                       (int)fragment->blockExtent,
                       fragment->last);
    if (written < 0) {
        return RoboLayoutDiag(diag, RoboLayoutDomainFragment, RoboError,
                              "the fragment did not format");
    }
    length = (size_t)written;
    *outLength = length;
    if (length >= capacity || length >= sizeof(line)) {
        return RoboLayoutDiag(diag, RoboLayoutDomainFragment, RoboNotSupported,
                              "the fragment does not fit");
    }
    memcpy(buffer, line, length + 1u);
    return RoboOk;
}

/* ------------------------------------------------------------------ */
/* The counters                                                        */
/* ------------------------------------------------------------------ */

void LayoutFragmentCountersInit(LayoutFragmentCounters *counters)
{
    if (!counters) {
        return;
    }
    memset(counters, 0, sizeof(*counters));
}

int LayoutFragmentCountersEqual(const LayoutFragmentCounters *a,
                                const LayoutFragmentCounters *b)
{
    if (!a || !b) {
        return a == b;
    }
    return a->containers == b->containers &&
           a->columnContainers == b->columnContainers &&
           a->fragments == b->fragments &&
           a->forcedBreaks == b->forcedBreaks &&
           a->avoidedBreaks == b->avoidedBreaks &&
           a->automaticBreaks == b->automaticBreaks &&
           a->suppressedAtStart == b->suppressedAtStart &&
           a->orphansRefusals == b->orphansRefusals &&
           a->widowsRefusals == b->widowsRefusals &&
           a->insideRefusals == b->insideRefusals;
}
