/*
 * RoboLayout fragmentation suite (Phase 19.7) — `layout.fragment.*`.
 *
 * Six things are under test, and the boundaries between them are the phase:
 *
 *   the **vocabulary** -- the four container kinds, the three actions and the nine
 *   reasons, each round-tripped through its own name, because a policy that answers a
 *   *reason* is only as good as the reason being the one the rule set;
 *
 *   the **kind gate** -- which container a forced break names. `page` and `region` are
 *   forced breaks this build cannot honour and must not silently round to a column
 *   break, which is the single most damaging confusion this family has;
 *
 *   the **boundary policy** -- the six rules, one case each: no context; a forced break
 *   at the start of a fragment suppressed; a forced break beating an `avoid` from either
 *   side of the boundary; an `avoid` refusing; a box's own `break-inside` refusing when
 *   it does not fit and *not* refusing when it does; and room deciding the automatic
 *   case. The order matters and is asserted through the reason rather than the action,
 *   because several rules answer the same action;
 *
 *   the **interior door** -- `break-inside` is one statement and the container decides
 *   whether it is about *it*, which is the pair of questions `CssBreakInsideIsAvoided`
 *   and `LayoutFragmentInsideRefuses` exist to keep apart;
 *
 *   the **line floors** -- `orphans`/`widows` read against 10.6's line boxes, including
 *   the two degenerate cases a break inside a run of text has to answer;
 *
 *   the **records** -- a fragment's validity and round trip, the counters' own identity,
 *   and the hundred-pass allocator balance that says this module allocates nothing.
 *
 * Nothing here asserts a *fragment*. A decision at this phase is a value with a reason in
 * it, and where the fragments fall belongs to the pass that has a fragmentainer.
 */

#include "RoboLayoutTestSupport.h"

#include <string.h>

/* ------------------------------------------------------------------ */
/* The vocabulary                                                      */
/* ------------------------------------------------------------------ */

static void TestFragmentKindVocabulary(void)
{
    static const char *const names[] = { "none", "column", "page", "region" };
    size_t index;

    RoboCheckEqInt((int)LayoutFragmentainerKindCount, 4);
    RoboCheckEqInt((int)LayoutFragmentainerKindCount,
                      (int)(sizeof(names) / sizeof(names[0])));
    for (index = 0u; index < sizeof(names) / sizeof(names[0]); index++) {
        LayoutFragmentainerKind kind = (LayoutFragmentainerKind)index;

        RoboCheckEqInt(LayoutFragmentainerKindIsKnown(kind), 1);
        RoboCheckStrEq(LayoutFragmentainerKindName(kind), names[index]);
    }
    RoboCheckEqInt(LayoutFragmentainerKindIsKnown((LayoutFragmentainerKind)99), 0);
    RoboCheckStrEq(LayoutFragmentainerKindName((LayoutFragmentainerKind)99), "");

    /* `none` is not a container; the other three are. This is the door every policy
     * question is gated on, so getting it backwards refuses every real container and
     * permits the one that cannot break. */
    RoboCheckEqInt(LayoutFragmentainerKindEstablishes(RoboLayoutFragmentainerKindNone), 0);
    RoboCheckEqInt(LayoutFragmentainerKindEstablishes(LayoutFragmentainerKindColumn), 1);
    RoboCheckEqInt(LayoutFragmentainerKindEstablishes(LayoutFragmentainerKindPage), 1);
    RoboCheckEqInt(LayoutFragmentainerKindEstablishes(LayoutFragmentainerKindRegion), 1);
    RoboCheckEqInt(LayoutFragmentainerKindEstablishes((LayoutFragmentainerKind)99), 0);
}

/**
 * The kind gate: which container a forced break names.
 *
 * The `region` row is the one that matters. RoboCSS's table says `region` is **forced**
 * and that neither a column nor a page container honours it -- because this build lays
 * out no regions -- so a policy that turned `region` into a column break would invent a
 * column and look right in every test that only asked for the action.
 */
static void TestFragmentKindOfForcedBreak(void)
{
    RoboCheckEqInt(LayoutFragmentainerKindOfForcedBreak(RoboCssBreakBetweenColumn),
                      (int)LayoutFragmentainerKindColumn);
    RoboCheckEqInt(LayoutFragmentainerKindOfForcedBreak(RoboCssBreakBetweenAlways),
                      (int)LayoutFragmentainerKindColumn);
    RoboCheckEqInt(LayoutFragmentainerKindOfForcedBreak(RoboCssBreakBetweenPage),
                      (int)LayoutFragmentainerKindPage);
    RoboCheckEqInt(LayoutFragmentainerKindOfForcedBreak(RoboCssBreakBetweenLeft),
                      (int)LayoutFragmentainerKindPage);
    RoboCheckEqInt(LayoutFragmentainerKindOfForcedBreak(RoboCssBreakBetweenRegion),
                      (int)LayoutFragmentainerKindNone);
    /* And every non-forced value names no container at all -- which is *not* "the break is
     * impossible", and the contract says so where a caller will read it. */
    RoboCheckEqInt(LayoutFragmentainerKindOfForcedBreak(RoboCssBreakBetweenAuto),
                      (int)LayoutFragmentainerKindNone);
    RoboCheckEqInt(LayoutFragmentainerKindOfForcedBreak(RoboCssBreakBetweenAvoid),
                      (int)LayoutFragmentainerKindNone);
    RoboCheckEqInt(LayoutFragmentainerKindOfForcedBreak(RoboCssBreakBetweenAvoidColumn),
                      (int)LayoutFragmentainerKindNone);
    RoboCheckEqInt(LayoutFragmentainerKindOfForcedBreak((CssBreakBetween)99),
                      (int)LayoutFragmentainerKindNone);
}

static void TestFragmentActionAndReasonVocabulary(void)
{
    static const char *const actions[] = { "forbidden", "allowed", "forced" };
    static const char *const reasons[] = {
        "no_context", "auto", "forced", "suppressed_at_start", "avoided",
        "avoided_inside", "no_room", "orphans", "widows"
    };
    size_t index;

    RoboCheckEqInt((int)LayoutBreakActionCount,
                      (int)(sizeof(actions) / sizeof(actions[0])));
    RoboCheckEqInt((int)LayoutBreakReasonCount,
                      (int)(sizeof(reasons) / sizeof(reasons[0])));
    for (index = 0u; index < sizeof(actions) / sizeof(actions[0]); index++) {
        RoboCheckEqInt(LayoutBreakActionIsKnown((LayoutBreakAction)index), 1);
        RoboCheckStrEq(LayoutBreakActionName((LayoutBreakAction)index), actions[index]);
    }
    for (index = 0u; index < sizeof(reasons) / sizeof(reasons[0]); index++) {
        /* `RoboLayoutReason...` -- the reasons are not contiguous by name, so the walk is
         * over the enumerators the header declares in order. */
        RoboCheckEqInt(LayoutBreakReasonIsKnown((LayoutBreakReason)index), 1);
        RoboCheckStrEq(LayoutBreakReasonName((LayoutBreakReason)index), reasons[index]);
    }
    RoboCheckEqInt(LayoutBreakActionIsKnown((LayoutBreakAction)99), 0);
    RoboCheckEqInt(LayoutBreakReasonIsKnown((LayoutBreakReason)99), 0);
    RoboCheckStrEq(LayoutBreakActionName((LayoutBreakAction)99), "");
    RoboCheckStrEq(LayoutBreakReasonName((LayoutBreakReason)99), "");
}

/* ------------------------------------------------------------------ */
/* The boundary record                                                 */
/* ------------------------------------------------------------------ */

static void TestFragmentBoundaryRecord(void)
{
    LayoutFragmentBoundary first;
    LayoutFragmentBoundary second;

    RoboCheckEqInt((int)sizeof(LayoutFragmentBoundary) > 0, 1);

    LayoutFragmentBoundaryInit(&first);
    /* The initial state is a box arriving at an **empty** container with room, and the
     * `roomLeft = 1` half is the one that matters: a zeroed record would mean "full", and a
     * policy whose zero breaks every document punishes a caller for forgetting a field. */
    RoboCheckEqInt((int)first.before, (int)RoboCssBreakBetweenAuto);
    RoboCheckEqInt((int)first.after, (int)RoboCssBreakBetweenAuto);
    RoboCheckEqInt((int)first.inside, (int)RoboCssBreakInsideAuto);
    RoboCheckEqInt(first.roomLeft, 1);
    RoboCheckEqInt(first.anythingBefore, 0);
    RoboCheckEqInt(LayoutFragmentBoundaryIsValid(&first), 1);

    second = first;
    RoboCheckEqInt(LayoutFragmentBoundaryEqual(&first, &second), 1);
    RoboCheckEqInt(LayoutFragmentBoundaryEqual(&first, NULL), 0);
    RoboCheckEqInt(LayoutFragmentBoundaryIsValid(NULL), 0);

    /* Each field moved apart is a difference, one at a time, so a comparison that dropped
     * one of the five fails here rather than three rules later. */
    second = first;
    second.before = RoboCssBreakBetweenPage;
    RoboCheckEqInt(LayoutFragmentBoundaryEqual(&first, &second), 0);
    second = first;
    second.after = RoboCssBreakBetweenAvoid;
    RoboCheckEqInt(LayoutFragmentBoundaryEqual(&first, &second), 0);
    second = first;
    first.inside = RoboCssBreakInsideAvoidColumn;
    second = first;
    second.inside = RoboCssBreakInsideAvoid;
    RoboCheckEqInt(LayoutFragmentBoundaryEqual(&first, &second), 0);
    second = first;
    second.roomLeft = 0;
    RoboCheckEqInt(LayoutFragmentBoundaryEqual(&first, &second), 0);
    second = first;
    second.anythingBefore = 1;
    RoboCheckEqInt(LayoutFragmentBoundaryEqual(&first, &second), 0);

    /* And the refusals: an unknown vocabulary value is not a boundary this build may read,
     * so a caller that built one by hand is told rather than answered as `auto`. */
    first.before = (CssBreakBetween)99;
    RoboCheckEqInt(LayoutFragmentBoundaryIsValid(&first), 0);
    LayoutFragmentBoundaryInit(&first);
    first.inside = (CssBreakInside)99;
    RoboCheckEqInt(LayoutFragmentBoundaryIsValid(&first), 0);
    LayoutFragmentBoundaryInit(&first);
    first.roomLeft = 2;
    RoboCheckEqInt(LayoutFragmentBoundaryIsValid(&first), 0);
    first.roomLeft = 1;
    first.anythingBefore = 7;
    RoboCheckEqInt(LayoutFragmentBoundaryIsValid(&first), 0);
}

static void TestFragmentDecisionRecord(void)
{
    LayoutBreakDecision decision;
    LayoutBreakDecision other;

    LayoutBreakDecisionInit(&decision);
    RoboCheckEqInt((int)decision.action, (int)LayoutBreakForbidden);
    RoboCheckEqInt((int)decision.reason, (int)LayoutBreakReasonNoContext);
    RoboCheckEqInt((int)decision.forcedBy, (int)LayoutFragmentainerKindNone);
    /* The zeroed record is a *valid* record: "nothing may break here and nothing is why",
     * which is exactly the answer for a box that establishes no fragmentation context. */
    RoboCheckEqInt(LayoutBreakDecisionIsValid(&decision), 1);

    other = decision;
    RoboCheckEqInt(LayoutBreakDecisionEqual(&decision, &other), 1);
    RoboCheckEqInt(LayoutBreakDecisionEqual(&decision, NULL), 0);
    RoboCheckEqInt(LayoutBreakDecisionIsValid(NULL), 0);

    /* The cross-field rule, which is the record saying what it means: `forcedBy` is the
     * container a *declaration* named, so it is non-NONE exactly when the action is
     * `Forced` with the forced reason -- and NONE for every other combination. */
    decision.action = LayoutBreakForced;
    decision.reason = LayoutBreakReasonForcedByDeclaration;
    decision.forcedBy = LayoutFragmentainerKindColumn;
    RoboCheckEqInt(LayoutBreakDecisionIsValid(&decision), 1);
    /* A forced break whose declaration named no container this build has: `region`, which
     * is the record the policy itself produces, so it must be valid. */
    decision.forcedBy = LayoutFragmentainerKindNone;
    RoboCheckEqInt(LayoutBreakDecisionIsValid(&decision), 1);
    /* A forced action with a reason that is not a declaration is a break nobody asked for. */
    decision.reason = LayoutBreakReasonNoRoom;
    RoboCheckEqInt(LayoutBreakDecisionIsValid(&decision), 0);
    /* And a declaration that forced nothing is not a forced break. */
    decision.action = LayoutBreakAllowed;
    decision.reason = LayoutBreakReasonForcedByDeclaration;
    RoboCheckEqInt(LayoutBreakDecisionIsValid(&decision), 0);
    /* An unforced action that names a container is contradictory too. */
    decision.action = LayoutBreakAllowed;
    decision.reason = LayoutBreakReasonAuto;
    decision.forcedBy = LayoutFragmentainerKindColumn;
    RoboCheckEqInt(LayoutBreakDecisionIsValid(&decision), 0);
    /* An unknown action or reason is never a record this build may read. */
    decision.action = (LayoutBreakAction)99;
    decision.reason = LayoutBreakReasonAuto;
    decision.forcedBy = LayoutFragmentainerKindNone;
    RoboCheckEqInt(LayoutBreakDecisionIsValid(&decision), 0);
    decision.action = LayoutBreakAllowed;
    decision.reason = (LayoutBreakReason)99;
    RoboCheckEqInt(LayoutBreakDecisionIsValid(&decision), 0);
}

/* ------------------------------------------------------------------ */
/* The boundary policy -- one case per rule                            */
/* ------------------------------------------------------------------ */

/** Rule 1: a box that establishes no fragmentation context cannot break. */
static void TestFragmentRuleNoContext(void)
{
    LayoutFragmentBoundary boundary;
    LayoutBreakDecision decision;

    LayoutFragmentBoundaryInit(&boundary);
    /*
     * Every declaration at once, on a container that does not exist. The answer is a
     * *refusal* rather than an error: a `break-before: page` inside a box with no
     * fragmentation context is a declaration with nothing to act on, and the document must
     * still lay out.
     */
    boundary.before = RoboCssBreakBetweenPage;
    boundary.after = RoboCssBreakBetweenAlways;
    boundary.inside = RoboCssBreakInsideAvoidColumn;
    boundary.roomLeft = 0;

    RoboCheckEqInt(LayoutFragmentBreakDecide(&boundary, RoboLayoutFragmentainerKindNone,
                                                    &decision, NULL),
                      RoboOk);
    RoboCheckEqInt((int)decision.action, (int)LayoutBreakForbidden);
    RoboCheckEqInt((int)decision.reason, (int)LayoutBreakReasonNoContext);
    RoboCheckEqInt((int)decision.forcedBy, (int)LayoutFragmentainerKindNone);
    RoboCheckEqInt(LayoutBreakDecisionIsValid(&decision), 1);

    /* The rule is on the *kind*, not on the declarations, so an empty boundary in a
     * non-container answers the same thing. */
    LayoutFragmentBoundaryInit(&boundary);
    RoboCheckEqInt(LayoutFragmentBreakDecide(&boundary, RoboLayoutFragmentainerKindNone,
                                                    &decision, NULL),
                      RoboOk);
    RoboCheckEqInt((int)decision.reason, (int)LayoutBreakReasonNoContext);
}

/** Rule 2: a forced break at the start of a fragment is suppressed. */
static void TestFragmentRuleSuppressedAtStart(void)
{
    LayoutFragmentBoundary boundary;
    LayoutBreakDecision decision;

    LayoutFragmentBoundaryInit(&boundary);
    boundary.anythingBefore = 0;
    boundary.before = RoboCssBreakBetweenColumn;

    RoboCheckEqInt(LayoutFragmentBreakDecide(&boundary, LayoutFragmentainerKindColumn,
                                                    &decision, NULL),
                      RoboOk);
    /* **Allowed**, not `Forced`: the content starts the fragment anyway, and a break before
     * it would produce an empty column. This is the reason the action and the reason are two
     * fields -- the *declaration* was forced and the *outcome* is not. */
    RoboCheckEqInt((int)decision.action, (int)LayoutBreakAllowed);
    RoboCheckEqInt((int)decision.reason, (int)LayoutBreakReasonSuppressedAtStart);
    RoboCheckEqInt((int)decision.forcedBy, (int)LayoutFragmentainerKindNone);

    /* With something already in the fragment the same declaration forces. */
    boundary.anythingBefore = 1;
    RoboCheckEqInt(LayoutFragmentBreakDecide(&boundary, LayoutFragmentainerKindColumn,
                                                    &decision, NULL),
                      RoboOk);
    RoboCheckEqInt((int)decision.action, (int)LayoutBreakForced);
    RoboCheckEqInt((int)decision.reason, (int)LayoutBreakReasonForcedByDeclaration);
    RoboCheckEqInt((int)decision.forcedBy, (int)LayoutFragmentainerKindColumn);

    /* An `avoid` at the start is *not* suppressed -- it is trivially satisfied, and it falls
     * to rule 4, which answers the same action with the reason that says why. The distinction
     * is the whole point of counting the two separately. */
    LayoutFragmentBoundaryInit(&boundary);
    boundary.before = RoboCssBreakBetweenAvoid;
    RoboCheckEqInt(LayoutFragmentBreakDecide(&boundary, LayoutFragmentainerKindColumn,
                                                    &decision, NULL),
                      RoboOk);
    RoboCheckEqInt((int)decision.action, (int)LayoutBreakForbidden);
    RoboCheckEqInt((int)decision.reason, (int)LayoutBreakReasonAvoidedByDeclaration);
}

/** Rule 3: a forced break beats an `avoid`, from either side of the boundary. */
static void TestFragmentRuleForcedWinsOverAvoided(void)
{
    LayoutFragmentBoundary boundary;
    LayoutBreakDecision decision;

    LayoutFragmentBoundaryInit(&boundary);
    boundary.anythingBefore = 1;

    /* The `avoid` on the earlier box and the forced break on the later one. */
    boundary.after = RoboCssBreakBetweenAvoid;
    boundary.before = RoboCssBreakBetweenColumn;
    RoboCheckEqInt(LayoutFragmentBreakDecide(&boundary, LayoutFragmentainerKindColumn,
                                                    &decision, NULL),
                      RoboOk);
    RoboCheckEqInt((int)decision.action, (int)LayoutBreakForced);
    RoboCheckEqInt((int)decision.reason, (int)LayoutBreakReasonForcedByDeclaration);

    /* The other way round: this is the pair a build that asked "what did the later box say
     * first" gets wrong, because the two declarations are on different boxes and neither is
     * more important in CSS. */
    LayoutFragmentBoundaryInit(&boundary);
    boundary.anythingBefore = 1;
    boundary.after = RoboCssBreakBetweenAlways;
    boundary.before = RoboCssBreakBetweenAvoidColumn;
    RoboCheckEqInt(LayoutFragmentBreakDecide(&boundary, LayoutFragmentainerKindColumn,
                                                    &decision, NULL),
                      RoboOk);
    RoboCheckEqInt((int)decision.action, (int)LayoutBreakForced);
    RoboCheckEqInt((int)decision.reason, (int)LayoutBreakReasonForcedByDeclaration);

    /*
     * And the container a `page` break names is *reported*, not narrowed: a page break inside
     * a column container is still forced -- that is CSS -- and `forcedBy` is what says the
     * declaration named a page. A build that answered `Column` here would be claiming the
     * container chose it.
     */
    LayoutFragmentBoundaryInit(&boundary);
    boundary.anythingBefore = 1;
    boundary.before = RoboCssBreakBetweenPage;
    RoboCheckEqInt(LayoutFragmentBreakDecide(&boundary, LayoutFragmentainerKindColumn,
                                                    &decision, NULL),
                      RoboOk);
    RoboCheckEqInt((int)decision.action, (int)LayoutBreakForced);
    RoboCheckEqInt((int)decision.forcedBy, (int)LayoutFragmentainerKindPage);

    /* And a `region` break, which no container here honours: forced, and named by no kind. */
    LayoutFragmentBoundaryInit(&boundary);
    boundary.anythingBefore = 1;
    boundary.before = RoboCssBreakBetweenRegion;
    RoboCheckEqInt(LayoutFragmentBreakDecide(&boundary, LayoutFragmentainerKindColumn,
                                                    &decision, NULL),
                      RoboOk);
    RoboCheckEqInt((int)decision.action, (int)LayoutBreakForced);
    RoboCheckEqInt((int)decision.forcedBy, (int)LayoutFragmentainerKindNone);
    RoboCheckEqInt(LayoutBreakDecisionIsValid(&decision), 1);
}

/** Rule 4: an `avoid` refuses the boundary, from either side. */
static void TestFragmentRuleAvoided(void)
{
    LayoutFragmentBoundary boundary;
    LayoutBreakDecision decision;

    LayoutFragmentBoundaryInit(&boundary);
    boundary.anythingBefore = 1;
    boundary.roomLeft = 1;
    boundary.after = RoboCssBreakBetweenAvoidPage;
    RoboCheckEqInt(LayoutFragmentBreakDecide(&boundary, LayoutFragmentainerKindColumn,
                                                    &decision, NULL),
                      RoboOk);
    RoboCheckEqInt((int)decision.action, (int)LayoutBreakForbidden);
    RoboCheckEqInt((int)decision.reason, (int)LayoutBreakReasonAvoidedByDeclaration);

    /* The four avoid values all refuse, and the one that names another container still refuses
     * *this boundary*: `break-after: avoid-page` on the earlier box is CSS's statement about
     * the boundary, not about the container -- which is the difference from `break-inside`,
     * where the container decides. */
    {
        static const CssBreakBetween avoids[] = {
            RoboCssBreakBetweenAvoid, RoboCssBreakBetweenAvoidPage,
            RoboCssBreakBetweenAvoidColumn, RoboCssBreakBetweenAvoidRegion
        };
        size_t index;

        for (index = 0u; index < sizeof(avoids) / sizeof(avoids[0]); index++) {
            boundary.before = avoids[index];
            RoboCheckEqInt(LayoutFragmentBreakDecide(&boundary,
                                                            LayoutFragmentainerKindColumn,
                                                            &decision, NULL),
                              RoboOk);
            RoboCheckEqInt((int)decision.reason,
                              (int)LayoutBreakReasonAvoidedByDeclaration);
        }
        boundary.before = RoboCssBreakBetweenAuto;
    }

    /* Both sides avoided is still the same answer, and not a different reason: there is one
     * refusal and two boxes that wrote it. */
    boundary.after = RoboCssBreakBetweenAvoid;
    boundary.before = RoboCssBreakBetweenAvoidRegion;
    RoboCheckEqInt(LayoutFragmentBreakDecide(&boundary, LayoutFragmentainerKindColumn,
                                                    &decision, NULL),
                      RoboOk);
    RoboCheckEqInt((int)decision.reason, (int)LayoutBreakReasonAvoidedByDeclaration);
}

/** Rule 5: a box's own `break-inside` moves it whole -- but only when it does not fit. */
static void TestFragmentRuleAvoidedInside(void)
{
    LayoutFragmentBoundary boundary;
    LayoutBreakDecision decision;

    LayoutFragmentBoundaryInit(&boundary);
    boundary.anythingBefore = 1;
    boundary.inside = RoboCssBreakInsideAvoidColumn;

    /* It does not fit: the refusal applies and the box moves whole. */
    boundary.roomLeft = 0;
    RoboCheckEqInt(LayoutFragmentBreakDecide(&boundary, LayoutFragmentainerKindColumn,
                                                    &decision, NULL),
                      RoboOk);
    RoboCheckEqInt((int)decision.action, (int)LayoutBreakForbidden);
    RoboCheckEqInt((int)decision.reason, (int)LayoutBreakReasonAvoidedInside);

    /* It **fits**: there is no break to refuse, and answering `AvoidedInside` here would report
     * a refusal nothing required. This is the half a build that read `break-inside` as an
     * unconditional refusal gets wrong. */
    boundary.roomLeft = 1;
    RoboCheckEqInt(LayoutFragmentBreakDecide(&boundary, LayoutFragmentainerKindColumn,
                                                    &decision, NULL),
                      RoboOk);
    RoboCheckEqInt((int)decision.action, (int)LayoutBreakAllowed);
    RoboCheckEqInt((int)decision.reason, (int)LayoutBreakReasonAuto);

    /* And a refusal about *another* container says nothing to this one: `avoid-page` on a box
     * in a column container, which does not fit, is refused because the container is full and
     * not because of the declaration. */
    boundary.roomLeft = 0;
    boundary.inside = RoboCssBreakInsideAvoidPage;
    RoboCheckEqInt(LayoutFragmentBreakDecide(&boundary, LayoutFragmentainerKindColumn,
                                                    &decision, NULL),
                      RoboOk);
    RoboCheckEqInt((int)decision.reason, (int)LayoutBreakReasonNoRoom);

    /* In a page container the same box is refused by its declaration. */
    RoboCheckEqInt(LayoutFragmentBreakDecide(&boundary, LayoutFragmentainerKindPage,
                                                    &decision, NULL),
                      RoboOk);
    RoboCheckEqInt((int)decision.reason, (int)LayoutBreakReasonAvoidedInside);

    /* And the generic `avoid` refuses in every container -- the one word the two RoboCSS
     * vocabularies share. */
    {
        static const LayoutFragmentainerKind kinds[] = {
            LayoutFragmentainerKindColumn, LayoutFragmentainerKindPage,
            LayoutFragmentainerKindRegion
        };
        size_t index;

        boundary.inside = RoboCssBreakInsideAvoid;
        for (index = 0u; index < sizeof(kinds) / sizeof(kinds[0]); index++) {
            RoboCheckEqInt(LayoutFragmentBreakDecide(&boundary, kinds[index],
                                                            &decision, NULL),
                              RoboOk);
            RoboCheckEqInt((int)decision.reason,
                              (int)LayoutBreakReasonAvoidedInside);
        }
    }
}

/** Rule 6: room decides the automatic case. */
static void TestFragmentRuleRoom(void)
{
    LayoutFragmentBoundary boundary;
    LayoutBreakDecision decision;

    LayoutFragmentBoundaryInit(&boundary);
    boundary.anythingBefore = 1;
    boundary.roomLeft = 1;
    RoboCheckEqInt(LayoutFragmentBreakDecide(&boundary, LayoutFragmentainerKindColumn,
                                                    &decision, NULL),
                      RoboOk);
    RoboCheckEqInt((int)decision.action, (int)LayoutBreakAllowed);
    RoboCheckEqInt((int)decision.reason, (int)LayoutBreakReasonAuto);
    RoboCheckEqInt(LayoutBreakDecisionIsValid(&decision), 1);

    boundary.roomLeft = 0;
    RoboCheckEqInt(LayoutFragmentBreakDecide(&boundary, LayoutFragmentainerKindColumn,
                                                    &decision, NULL),
                      RoboOk);
    /* `Forbidden` and `NoRoom` together mean "the contents continue here because there is
     * nowhere else for them to go, and the container is full" -- which is the only break no
     * author asked for, and the pair that distinguishes it from every refusal above. */
    RoboCheckEqInt((int)decision.action, (int)LayoutBreakForbidden);
    RoboCheckEqInt((int)decision.reason, (int)LayoutBreakReasonNoRoom);

    /* Every container kind answers the automatic case the same way. */
    {
        static const LayoutFragmentainerKind kinds[] = {
            LayoutFragmentainerKindColumn, LayoutFragmentainerKindPage,
            LayoutFragmentainerKindRegion
        };
        size_t index;

        boundary.roomLeft = 1;
        for (index = 0u; index < sizeof(kinds) / sizeof(kinds[0]); index++) {
            RoboCheckEqInt(LayoutFragmentBreakDecide(&boundary, kinds[index],
                                                            &decision, NULL),
                              RoboOk);
            RoboCheckEqInt((int)decision.reason, (int)LayoutBreakReasonAuto);
        }
    }
}

/** The door's refusals: a NULL argument, an unreadable boundary or an unknown kind. */
static void TestFragmentDecisionRefusals(void)
{
    LayoutFragmentBoundary boundary;
    LayoutBreakDecision decision;

    LayoutFragmentBoundaryInit(&boundary);
    RoboCheckEqInt(LayoutFragmentBreakDecide(NULL, LayoutFragmentainerKindColumn,
                                                     &decision, NULL),
                      RoboInvalidArgument);
    RoboCheckEqInt(LayoutFragmentBreakDecide(&boundary, LayoutFragmentainerKindColumn,
                                                     NULL, NULL),
                      RoboInvalidArgument);
    RoboCheckEqInt(LayoutFragmentBreakDecide(&boundary,
                                                     (LayoutFragmentainerKind)99,
                                                     &decision, NULL),
                      RoboInvalidArgument);
    boundary.inside = (CssBreakInside)99;
    RoboCheckEqInt(LayoutFragmentBreakDecide(&boundary, LayoutFragmentainerKindColumn,
                                                     &decision, NULL),
                      RoboInvalidArgument);
    /* And the decision the refusal left behind is the cleared one, so a caller that ignored
     * the status reads "nothing may break here" rather than a stale answer. */
    RoboCheckEqInt((int)decision.action, (int)LayoutBreakForbidden);
    RoboCheckEqInt((int)decision.reason, (int)LayoutBreakReasonNoContext);
}

/* ------------------------------------------------------------------ */
/* The interior door                                                   */
/* ------------------------------------------------------------------ */

static void TestFragmentInsideRefuses(void)
{
    static const LayoutFragmentainerKind containers[] = {
        LayoutFragmentainerKindColumn, LayoutFragmentainerKindPage,
        LayoutFragmentainerKindRegion
    };
    size_t index;

    /* `auto` refuses nowhere, and neither does an unknown value. */
    for (index = 0u; index < sizeof(containers) / sizeof(containers[0]); index++) {
        RoboCheckEqInt(LayoutFragmentInsideRefuses(RoboCssBreakInsideAuto,
                                                            containers[index]),
                          0);
        RoboCheckEqInt(LayoutFragmentInsideRefuses((CssBreakInside)99,
                                                            containers[index]),
                          0);
        /* `avoid` refuses in **every** container: it is the one word the two vocabularies
         * share and it means the same thing on both sides. */
        RoboCheckEqInt(LayoutFragmentInsideRefuses(RoboCssBreakInsideAvoid,
                                                            containers[index]),
                          1);
    }

    /* Each container-named refusal refuses in its own container and in no other -- which is
     * the pair of questions the CSS predicate and this door exist to keep apart. */
    RoboCheckEqInt(LayoutFragmentInsideRefuses(RoboCssBreakInsideAvoidColumn,
                                                        LayoutFragmentainerKindColumn),
                      1);
    RoboCheckEqInt(LayoutFragmentInsideRefuses(RoboCssBreakInsideAvoidColumn,
                                                        LayoutFragmentainerKindPage),
                      0);
    RoboCheckEqInt(LayoutFragmentInsideRefuses(RoboCssBreakInsideAvoidColumn,
                                                        LayoutFragmentainerKindRegion),
                      0);
    RoboCheckEqInt(LayoutFragmentInsideRefuses(RoboCssBreakInsideAvoidPage,
                                                        LayoutFragmentainerKindPage),
                      1);
    RoboCheckEqInt(LayoutFragmentInsideRefuses(RoboCssBreakInsideAvoidPage,
                                                        LayoutFragmentainerKindColumn),
                      0);
    RoboCheckEqInt(LayoutFragmentInsideRefuses(RoboCssBreakInsideAvoidRegion,
                                                        LayoutFragmentainerKindRegion),
                      1);
    RoboCheckEqInt(LayoutFragmentInsideRefuses(RoboCssBreakInsideAvoidRegion,
                                                        LayoutFragmentainerKindPage),
                      0);

    /* And a non-container refuses nothing, because there is no boundary to refuse. */
    RoboCheckEqInt(LayoutFragmentInsideRefuses(RoboCssBreakInsideAvoid,
                                                        RoboLayoutFragmentainerKindNone),
                      0);
    RoboCheckEqInt(LayoutFragmentInsideRefuses(RoboCssBreakInsideAvoidColumn,
                                                        RoboLayoutFragmentainerKindNone),
                      0);
    RoboCheckEqInt(LayoutFragmentInsideRefuses(RoboCssBreakInsideAvoidColumn,
                                                        (LayoutFragmentainerKind)99),
                      0);

    /* The CSS predicate and this door disagree on purpose, which is the phase's point: the
     * vocabulary answers "is this a refusal" and only the container answers "is it about me". */
    RoboCheckEqInt(CssBreakInsideIsAvoided(RoboCssBreakInsideAvoidPage), 1);
    RoboCheckEqInt(LayoutFragmentInsideRefuses(RoboCssBreakInsideAvoidPage,
                                                        LayoutFragmentainerKindColumn),
                      0);
}

/* ------------------------------------------------------------------ */
/* The line floors                                                     */
/* ------------------------------------------------------------------ */

static void TestFragmentLineFloors(void)
{
    LayoutBreakDecision decision;

    /* Ten lines, breaking after the fifth, with CSS's initial floors of two and two: four
     * lines stay above the break and five travel below it, so both floors are met. */
    RoboCheckEqInt(LayoutFragmentBreakLines(10, 5, 2, 2, &decision, NULL), RoboOk);
    RoboCheckEqInt((int)decision.action, (int)LayoutBreakAllowed);
    RoboCheckEqInt((int)decision.reason, (int)LayoutBreakReasonAuto);

    /* `orphans`: breaking after the first line with a floor of two leaves one line behind. */
    RoboCheckEqInt(LayoutFragmentBreakLines(10, 1, 2, 2, &decision, NULL), RoboOk);
    RoboCheckEqInt((int)decision.action, (int)LayoutBreakForbidden);
    RoboCheckEqInt((int)decision.reason, (int)LayoutBreakReasonOrphansFloor);

    /* `widows`: breaking after the ninth line carries one line over. The two refusals are
     * separate reasons because *which* floor was breached is the only useful half. */
    RoboCheckEqInt(LayoutFragmentBreakLines(10, 9, 2, 2, &decision, NULL), RoboOk);
    RoboCheckEqInt((int)decision.action, (int)LayoutBreakForbidden);
    RoboCheckEqInt((int)decision.reason, (int)LayoutBreakReasonWidowsFloor);

    /* A break that leaves *exactly* the floor is satisfied, not refused -- the `<=` a build
     * gets backwards one line too strict. */
    RoboCheckEqInt(LayoutFragmentBreakLines(10, 2, 2, 2, &decision, NULL), RoboOk);
    RoboCheckEqInt((int)decision.reason, (int)LayoutBreakReasonAuto);
    RoboCheckEqInt(LayoutFragmentBreakLines(10, 8, 2, 2, &decision, NULL), RoboOk);
    RoboCheckEqInt((int)decision.reason, (int)LayoutBreakReasonAuto);
    RoboCheckEqInt(LayoutFragmentBreakLines(4, 2, 2, 2, &decision, NULL), RoboOk);
    RoboCheckEqInt((int)decision.reason, (int)LayoutBreakReasonAuto);

    /* The two degenerate cases, both refusals with `NoRoom` rather than a floor: a break
     * before the first line would empty the fragment, and a break at or after the last line is
     * not a break *inside* the text. Reporting `OrphansFloor` for either would blame a floor
     * that was never consulted. */
    RoboCheckEqInt(LayoutFragmentBreakLines(10, 0, 1, 1, &decision, NULL), RoboOk);
    RoboCheckEqInt((int)decision.reason, (int)LayoutBreakReasonNoRoom);
    RoboCheckEqInt(LayoutFragmentBreakLines(10, 10, 1, 1, &decision, NULL), RoboOk);
    RoboCheckEqInt((int)decision.reason, (int)LayoutBreakReasonNoRoom);
    RoboCheckEqInt(LayoutFragmentBreakLines(10, 11, 1, 1, &decision, NULL), RoboOk);
    RoboCheckEqInt((int)decision.reason, (int)LayoutBreakReasonNoRoom);

    /* One line is never breakable: it can satisfy neither floor, and both answers are
     * `NoRoom` because there is nowhere inside it for a break to fall. */
    RoboCheckEqInt(LayoutFragmentBreakLines(1, 1, 0, 0, &decision, NULL), RoboOk);
    RoboCheckEqInt((int)decision.reason, (int)LayoutBreakReasonNoRoom);

    /* Floors of zero make every interior position legal, which is the only way to write "break
     * anywhere" and the reason the door takes the floors rather than reading a default. */
    RoboCheckEqInt(LayoutFragmentBreakLines(5, 1, 0, 0, &decision, NULL), RoboOk);
    RoboCheckEqInt((int)decision.reason, (int)LayoutBreakReasonAuto);
    RoboCheckEqInt(LayoutFragmentBreakLines(5, 4, 0, 0, &decision, NULL), RoboOk);
    RoboCheckEqInt((int)decision.reason, (int)LayoutBreakReasonAuto);

    /* The door's refusals: a NULL out, and a negative count or floor. */
    RoboCheckEqInt(LayoutFragmentBreakLines(10, 5, 2, 2, NULL, NULL),
                      RoboInvalidArgument);
    RoboCheckEqInt(LayoutFragmentBreakLines(-1, 5, 2, 2, &decision, NULL),
                      RoboInvalidArgument);
    RoboCheckEqInt(LayoutFragmentBreakLines(10, -1, 2, 2, &decision, NULL),
                      RoboInvalidArgument);
    RoboCheckEqInt(LayoutFragmentBreakLines(10, 5, -2, 2, &decision, NULL),
                      RoboInvalidArgument);
    RoboCheckEqInt(LayoutFragmentBreakLines(10, 5, 2, -2, &decision, NULL),
                      RoboInvalidArgument);
}

/* ------------------------------------------------------------------ */
/* A fragment                                                          */
/* ------------------------------------------------------------------ */

static void TestFragmentRecord(void)
{
    LayoutFragment first;
    LayoutFragment second;
    char line[RoboLayoutFragmentTextMax];
    size_t length = 0u;

    LayoutFragmentInit(&first);
    RoboCheckEqInt((int)first.node, (int)RoboLayoutNodeIdNone);
    RoboCheckEqInt((int)first.kind, (int)LayoutFragmentainerKindNone);
    RoboCheckEqInt(first.index, 0u);
    RoboCheckEqInt((int)first.blockOffset, 0);
    RoboCheckEqInt((int)first.blockExtent, 0);
    RoboCheckEqInt(first.last, 0);

    /* The cleared record is **not** valid, and that is the design: a fragment with no node
     * and no container is a record nobody produced. */
    RoboCheckEqInt(LayoutFragmentIsValid(&first), 0);
    RoboCheckEqInt(LayoutFragmentIsValid(NULL), 0);

    first.node = (LayoutNodeId)7u;
    first.kind = LayoutFragmentainerKindColumn;
    first.index = 2u;
    first.blockOffset = 40;
    first.blockExtent = 25;
    first.last = 0;
    RoboCheckEqInt(LayoutFragmentIsValid(&first), 1);

    second = first;
    RoboCheckEqInt(LayoutFragmentEqual(&first, &second), 1);
    RoboCheckEqInt(LayoutFragmentEqual(&first, NULL), 0);

    /* Each field moved apart is a difference, and the ordinal is the one a build is most
     * likely to leave out of a comparison: two fragments of the same box with the same extent
     * at different offsets are different fragments. */
    second = first;
    second.index = 3u;
    RoboCheckEqInt(LayoutFragmentEqual(&first, &second), 0);
    second = first;
    second.blockOffset = 41;
    RoboCheckEqInt(LayoutFragmentEqual(&first, &second), 0);
    second = first;
    second.blockExtent = 26;
    RoboCheckEqInt(LayoutFragmentEqual(&first, &second), 0);
    second = first;
    second.node = (LayoutNodeId)8u;
    RoboCheckEqInt(LayoutFragmentEqual(&first, &second), 0);
    second = first;
    second.last = 1;
    RoboCheckEqInt(LayoutFragmentEqual(&first, &second), 0);

    /* The refusals: a fragment of no box, of a kind that establishes no container, a negative
     * offset or extent, and a `last` that is neither 0 nor 1. */
    second = first;
    second.node = RoboLayoutNodeIdNone;
    RoboCheckEqInt(LayoutFragmentIsValid(&second), 0);
    second = first;
    second.kind = LayoutFragmentainerKindNone;
    RoboCheckEqInt(LayoutFragmentIsValid(&second), 0);
    second = first;
    second.kind = (LayoutFragmentainerKind)99;
    RoboCheckEqInt(LayoutFragmentIsValid(&second), 0);
    second = first;
    second.blockOffset = -1;
    RoboCheckEqInt(LayoutFragmentIsValid(&second), 0);
    second = first;
    second.blockExtent = -1;
    RoboCheckEqInt(LayoutFragmentIsValid(&second), 0);
    second = first;
    second.last = 2;
    RoboCheckEqInt(LayoutFragmentIsValid(&second), 0);

    /* The dump is complete-or-absent: it reports the length a complete line needs and leaves
     * the buffer empty when it does not fit, so one call sizes a caller's buffer. */
    RoboCheckEqInt(LayoutFragmentWrite(&first, line, sizeof(line), &length, NULL), RoboOk);
    RoboCheck(length > 0u);
    RoboCheck(length < sizeof(line));
    /* The container's name and both coordinates are in the line: a reader debugging a column
     * break needs the ordinal, the box and where the fragment starts. */
    RoboCheck(strstr(line, "container=column") != NULL);
    RoboCheck(strstr(line, "node=7") != NULL);
    RoboCheck(strstr(line, "offset=40") != NULL);
    RoboCheck(strstr(line, "extent=25") != NULL);

    {
        char small[8];
        size_t needed = 0u;

        RoboCheckEqInt(LayoutFragmentWrite(&first, small, sizeof(small), &needed, NULL),
                          RoboNotSupported);
        RoboCheckEqInt((int)needed, (int)length);
        RoboCheckStrEq(small, "");
    }
    RoboCheckEqInt(LayoutFragmentWrite(NULL, line, sizeof(line), &length, NULL),
                      RoboInvalidArgument);
    RoboCheckEqInt(LayoutFragmentWrite(&first, NULL, sizeof(line), &length, NULL),
                      RoboInvalidArgument);
    RoboCheckEqInt(LayoutFragmentWrite(&first, line, 0u, &length, NULL),
                      RoboInvalidArgument);
    RoboCheckEqInt(LayoutFragmentWrite(&first, line, sizeof(line), NULL, NULL),
                      RoboInvalidArgument);

    /* A record that is not valid still dumps, because a diagnostic must be able to report the
     * record it was handed rather than refusing to describe it. */
    second = first;
    second.node = RoboLayoutNodeIdNone;
    RoboCheckEqInt(LayoutFragmentWrite(&second, line, sizeof(line), &length, NULL),
                      RoboOk);
}

/* ------------------------------------------------------------------ */
/* The counters                                                        */
/* ------------------------------------------------------------------ */

static void TestFragmentCounters(void)
{
    LayoutFragmentCounters a;
    LayoutFragmentCounters b;

    LayoutFragmentCountersInit(&a);
    b = a;

    RoboCheckEqInt(a.containers, 0u);
    RoboCheckEqInt(a.columnContainers, 0u);
    RoboCheckEqInt(a.fragments, 0u);
    RoboCheckEqInt(a.forcedBreaks, 0u);
    RoboCheckEqInt(a.avoidedBreaks, 0u);
    RoboCheckEqInt(a.automaticBreaks, 0u);
    RoboCheckEqInt(a.suppressedAtStart, 0u);
    RoboCheckEqInt(a.orphansRefusals, 0u);
    RoboCheckEqInt(a.widowsRefusals, 0u);
    RoboCheckEqInt(a.insideRefusals, 0u);

    RoboCheckEqInt(LayoutFragmentCountersEqual(&a, &b), 1);
    RoboCheckEqInt(LayoutFragmentCountersEqual(NULL, NULL), 1);
    RoboCheckEqInt(LayoutFragmentCountersEqual(&a, NULL), 0);

    /*
     * Every field moved apart is a difference, one at a time. The three the *mix* turns on --
     * a forced break, an avoided one and an automatic one -- are asserted individually because
     * a comparison that dropped one of them would make a document that declared nothing look
     * identical to one that declared a `break-before` the policy ignored.
     */
    b = a;
    b.containers = 1u;
    RoboCheckEqInt(LayoutFragmentCountersEqual(&a, &b), 0);
    b = a;
    b.columnContainers = 1u;
    RoboCheckEqInt(LayoutFragmentCountersEqual(&a, &b), 0);
    b = a;
    b.fragments = 1u;
    RoboCheckEqInt(LayoutFragmentCountersEqual(&a, &b), 0);
    b = a;
    b.forcedBreaks = 1u;
    RoboCheckEqInt(LayoutFragmentCountersEqual(&a, &b), 0);
    b = a;
    b.avoidedBreaks = 1u;
    RoboCheckEqInt(LayoutFragmentCountersEqual(&a, &b), 0);
    b = a;
    b.automaticBreaks = 1u;
    RoboCheckEqInt(LayoutFragmentCountersEqual(&a, &b), 0);
    b = a;
    b.suppressedAtStart = 1u;
    RoboCheckEqInt(LayoutFragmentCountersEqual(&a, &b), 0);
    b = a;
    b.orphansRefusals = 1u;
    RoboCheckEqInt(LayoutFragmentCountersEqual(&a, &b), 0);
    b = a;
    b.widowsRefusals = 1u;
    RoboCheckEqInt(LayoutFragmentCountersEqual(&a, &b), 0);
    b = a;
    b.insideRefusals = 1u;
    RoboCheckEqInt(LayoutFragmentCountersEqual(&a, &b), 0);

    /* And none of the copies above was a reference. */
    RoboCheckEqInt(a.containers, 0u);
    RoboCheckEqInt(a.forcedBreaks, 0u);
    RoboCheckEqInt(a.insideRefusals, 0u);
}

/*
 * The whole policy is a pure function of the records it was handed, so a hundred repetitions
 * of every door must produce the same answers and allocate nothing. This is the case that says
 * the module has no cache, no arena and no state between calls -- which is what makes it safe
 * for a pass to ask the same boundary again after a re-layout.
 */
static void TestFragmentDeterminismAndAllocatorBalance(void)
{
    LayoutFragmentBoundary boundary;
    LayoutBreakDecision first;
    LayoutBreakDecision second;
    LayoutFragment fragment;
    char line[RoboLayoutFragmentTextMax];
    size_t settled = 0u;
    size_t length = 0u;
    int pass = 0;

    LayoutFragmentBoundaryInit(&boundary);
    boundary.anythingBefore = 1;
    boundary.roomLeft = 0;
    boundary.before = RoboCssBreakBetweenColumn;
    boundary.inside = RoboCssBreakInsideAvoidColumn;

    LayoutFragmentInit(&fragment);
    fragment.node = (LayoutNodeId)3u;
    fragment.kind = LayoutFragmentainerKindColumn;
    fragment.index = 1u;
    fragment.blockOffset = 12;
    fragment.blockExtent = 8;

    settled = LayoutLiveBlocks();
    for (pass = 0; pass < 100; pass++) {
        RoboCheckEqInt(LayoutFragmentBreakDecide(&boundary,
                                                          LayoutFragmentainerKindColumn,
                                                          &first, NULL),
                          RoboOk);
        RoboCheckEqInt(LayoutFragmentBreakDecide(&boundary,
                                                          LayoutFragmentainerKindColumn,
                                                          &second, NULL),
                          RoboOk);
        RoboCheckEqInt(LayoutBreakDecisionEqual(&first, &second), 1);
        RoboCheckEqInt(LayoutFragmentBreakLines(30, 10, 2, 2, &second, NULL), RoboOk);
        RoboCheckEqInt((int)second.reason, (int)LayoutBreakReasonAuto);
        RoboCheckEqInt(LayoutFragmentWrite(&fragment, line, sizeof(line), &length, NULL),
                          RoboOk);
    }
    RoboCheckEqInt(LayoutLiveBlocks(), settled);

    /* And the answer is the one the rules give: forced by the declaration, because a forced
     * break beats the interior refusal and outranks the container being full. */
    RoboCheckEqInt((int)first.action, (int)LayoutBreakForced);
    RoboCheckEqInt((int)first.reason, (int)LayoutBreakReasonForcedByDeclaration);
}

/* ------------------------------------------------------------------ */
/* Registration                                                        */
/* ------------------------------------------------------------------ */

void TestRegisterLayoutFragment(void);

void TestRegisterLayoutFragment(void)
{
    TestRegister("layout.fragment.kind_vocabulary", TestFragmentKindVocabulary);
    TestRegister("layout.fragment.forced_break_kind", TestFragmentKindOfForcedBreak);
    TestRegister("layout.fragment.action_vocabulary",
                       TestFragmentActionAndReasonVocabulary);
    TestRegister("layout.fragment.boundary_record", TestFragmentBoundaryRecord);
    TestRegister("layout.fragment.decision_record", TestFragmentDecisionRecord);
    TestRegister("layout.fragment.no_context", TestFragmentRuleNoContext);
    TestRegister("layout.fragment.suppressed_at_start",
                       TestFragmentRuleSuppressedAtStart);
    TestRegister("layout.fragment.forced_beats_avoided",
                       TestFragmentRuleForcedWinsOverAvoided);
    TestRegister("layout.fragment.avoided", TestFragmentRuleAvoided);
    TestRegister("layout.fragment.avoided_inside", TestFragmentRuleAvoidedInside);
    TestRegister("layout.fragment.room", TestFragmentRuleRoom);
    TestRegister("layout.fragment.decide_refusals", TestFragmentDecisionRefusals);
    TestRegister("layout.fragment.inside_refuses", TestFragmentInsideRefuses);
    TestRegister("layout.fragment.line_floors", TestFragmentLineFloors);
    TestRegister("layout.fragment.record", TestFragmentRecord);
    TestRegister("layout.fragment.counters", TestFragmentCounters);
    TestRegister("layout.fragment.determinism", TestFragmentDeterminismAndAllocatorBalance);
}
