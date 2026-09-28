/*
 * RoboLayout module surface suite (Phase 10.1) — layout.module.*
 *
 * What this build *is*, asserted rather than described. Everything here is a
 * closed vocabulary, a version, a capability table or a type identity, and each
 * one has the same failure mode: it changes and nothing that uses it notices.
 *
 * A capability that is 0 is a phase this build has not built, and each one is
 * asserted twice from two independent directions -- the struct field and the
 * function that reads the macro -- because a build that flipped one and not the
 * other would have two answers to one question.
 *
 * Since 10.10 the capability table has grown one row per phase, and each row is
 * checked *positionally* against the struct beside it: the table is written out by
 * hand in the struct's own order, so a phase that added a field and forgot a row
 * moves every field after it and fails here rather than at a caller.
 *
 * And since 10.13 only one field is 0: `painting`. Every phase through 10.13 has
 * landed, so what is left is the one phase that has not started.
 *
 * The geometry test is not decoration either. RoboLayout declares its point,
 * size and rectangle as *aliases* of RoboGraphics', and an alias is exactly the
 * kind of claim that survives a copy-paste: if somebody ever replaced the typedef
 * with a struct, every suite in this directory would still compile and the only
 * symptom would be a conversion at the paint boundary a year later. Sizes and
 * assignment compatibility are what make it a test.
 */

#include "RoboLayoutTestSupport.h"

#include "RoboGraphics/RoboGraphicsGeometry.h"

#include <string.h>

/* ------------------------------------------------------------------ */
/* Version                                                             */
/* ------------------------------------------------------------------ */

static void TestModuleVersion(void)
{
    I32 major = -1;
    I32 minor = -1;
    I32 patch = -1;

    RoboCheckEqInt(RoboLayoutVersionMajor, 0);
    RoboCheckEqInt(RoboLayoutVersionMinor, 22);
    RoboCheckEqInt(RoboLayoutVersionPatch, 0);
    RoboCheckStrEq(RoboLayoutVersionString, "0.22.0");
    RoboCheckStrEq(LayoutVersionString(), "0.22.0");

    RoboCheckEqInt(LayoutGetVersion(&major, &minor, &patch), RoboOk);
    RoboCheckEqInt(major, RoboLayoutVersionMajor);
    RoboCheckEqInt(minor, RoboLayoutVersionMinor);
    RoboCheckEqInt(patch, RoboLayoutVersionPatch);

    /* Not frozen, and honestly so: Phase 10 keeps growing this surface. */
    RoboCheckEqInt(RoboLayoutApiFrozen, 0);

    RoboCheckEqInt(LayoutGetVersion(NULL, &minor, &patch),
                      RoboInvalidArgument);
    RoboCheckEqInt(LayoutGetVersion(&major, NULL, &patch),
                      RoboInvalidArgument);
    RoboCheckEqInt(LayoutGetVersion(&major, &minor, NULL),
                      RoboInvalidArgument);
}

/* ------------------------------------------------------------------ */
/* Lifecycle                                                           */
/* ------------------------------------------------------------------ */

static void TestModuleLifecycle(void)
{
    LayoutFixture();
    RoboCheckEqInt(LayoutIsInitialized(), 1);
    /* Already up: the fixture is idempotent and this is the fact it relies on. */
    RoboCheckEqInt(LayoutInit(), RoboAlreadyExists);

    LayoutShutdown();
    RoboCheckEqInt(LayoutIsInitialized(), 0);
    /* Safe when already down: a module that owns nothing has nothing to refuse. */
    LayoutShutdown();
    RoboCheckEqInt(LayoutIsInitialized(), 0);

    RoboCheckEqInt(LayoutInit(), RoboOk);
    RoboCheckEqInt(LayoutIsInitialized(), 1);
    /* Leave it up: every other suite in this directory assumes the fixture. */
}

/* ------------------------------------------------------------------ */
/* Capabilities                                                        */
/* ------------------------------------------------------------------ */

/**
 * The flags, in declaration order, with the value this build claims for each.
 *
 * Written out by hand rather than derived: a table generated from the struct
 * would agree with the struct by construction and would therefore assert
 * nothing. The order is the struct's own, so the walk below is positional.
 */
static const struct {
    const char *name;
    int expected;
} layoutCapabilityFlags[] = {
    { "object_model", 1 },
    { "constraints", 1 },
    { "result_model", 1 },
    { "context", 1 },
    { "dom_build", 1 },
    { "computed_style_link", 1 },
    { "geometry", 1 },
    { "formatting_contexts", 1 },
    { "block_layout", 1 },
    { "inline_layout", 1 },
    { "block_and_inline_layout", 1 },
    { "text_layout", 1 },
    { "line_boxes", 1 },
    { "baselines", 1 },
    { "sizing", 1 },
    { "margins", 1 },
    { "padding", 1 },
    { "border", 1 },
    { "position", 1 },
    { "out_of_flow", 1 },
    { "overflow", 1 },
    { "invalidation", 1 },
    { "font_request", 1 },
    { "intrinsic_sizing", 1 },
    { "percentage_resolution", 1 },
    { "flex_layout", 1 },
    { "grid_layout", 1 },
    { "table_layout", 1 },
    { "painting", 0 }
};

static void TestModuleCapabilities(void)
{
    LayoutCapabilities caps;
    const int *fields;
    size_t count = sizeof(layoutCapabilityFlags) / sizeof(layoutCapabilityFlags[0]);
    size_t index;

    RoboCheckEqInt(sizeof(LayoutCapabilities),
                      count * sizeof(int));

    LayoutFixture();
    LayoutCapabilitiesInit(&caps);
    fields = (const int *)&caps;
    for (index = 0u; index < count; index++) {
        RoboCheckMsg(fields[index] == 0,
                       "capabilities_init left a field asserted");
    }

    RoboCheckEqInt(LayoutGetCapabilities(NULL), RoboInvalidArgument);
    memset(&caps, 0xFF, sizeof(caps));
    RoboCheckEqInt(LayoutGetCapabilities(&caps), RoboOk);
    fields = (const int *)&caps;
    for (index = 0u; index < count; index++) {
        RoboCheckEqInt(fields[index], layoutCapabilityFlags[index].expected);
    }

    /*
     * The fields this build moves are also reachable one at a time, so a caller that
     * asks a single question is not reading a union of fifteen. And they are asserted
     * as a *group* rather than only in the table above, because the group is the
     * interesting fact: this build computes a coordinate, it computes it in *both*
     * directions, and the field that names both is the conjunction of the two -- still
     * an expression rather than a literal, so that the day one direction is switched
     * off the wider field follows it down by itself.
     */
    RoboCheckEqInt(caps.geometry, RoboLayoutGeometryImplemented);
    RoboCheckEqInt(caps.formattingContexts, 1);
    RoboCheckEqInt(caps.blockLayout, RoboLayoutBlockLayoutImplemented);
    RoboCheckEqInt(caps.inlineLayout, RoboLayoutInlineLayoutImplemented);
    RoboCheckEqInt(caps.blockAndInlineLayout,
                      RoboLayoutBlockLayoutImplemented &&
                          RoboLayoutInlineLayoutImplemented);
    RoboCheckEqInt(caps.textLayout, RoboLayoutTextLayoutImplemented);
    /*
     * And `lineBoxes`, the same pair for the same reason. Asserted as its own field
     * rather than as a consequence of `textLayout`: producing a line and measuring a
     * run are independent claims, and the build that could do the first and not the
     * second is the one this module was before 10.6.
     */
    RoboCheckEqInt(caps.lineBoxes, RoboLayoutLineBoxesImplemented);
    /*
     * And `baselines`, its own field for the same reason: 10.7's claim is that a line
     * has a floor and its members are placed on it, which is neither the line's
     * *existence* (10.6) nor a run's *measurement* (10.5). A build that produced lines
     * and placed every member at the line's top is exactly what this module was before
     * this phase, and it satisfied both of those fields.
     */
    RoboCheckEqInt(caps.baselines, RoboLayoutBaselinesImplemented);
    /*
     * And `sizing`, its own field for the same reason again: 10.8's claim is that a
     * declaration reaches the geometry -- a declared px width *and* the width a box's
     * children are laid out into -- which is neither a line's existence nor a
     * baseline. A build that had a sizing *record* and read nothing through it would
     * have satisfied every field above it.
     */
    RoboCheckEqInt(caps.sizing, RoboLayoutSizingImplemented);
    /*
     * And `margins`, its own field for the same reason one more time: 10.9's claim is
     * that a margin declaration reaches the geometry -- a box is placed at its margin
     * origin, its `auto` sides distribute the leftover beside a declared width, and a
     * parent stacks by its children's margin *boxes* -- which is neither a size nor a
     * baseline. A build with the whole margin vocabulary and no caller would have
     * satisfied every field above it.
     */
    RoboCheckEqInt(caps.margins, RoboLayoutMarginsImplemented);
    /*
     * And `padding`, the field 10.10 added -- the mirror of the one above it, and its
     * own field for the same reason: a padding reaches the geometry as *inside* the
     * box, so it changes a box's size and where its contents begin without ever moving
     * the box. A build with the margin vocabulary and no padding would have satisfied
     * every field above it.
     */
    RoboCheckEqInt(caps.padding, RoboLayoutPaddingImplemented);
    /*
     * And `border`, the field 10.11 added -- the third inset and the one whose *used*
     * value is not its declared one: `border: 5px none` is a declared five and a used
     * zero, so this field claims a rule the two fields above it have no analogue of. A
     * build with the padding vocabulary and no border would have satisfied every field
     * above it, which is why it is asserted here rather than left to the table.
     */
    RoboCheckEqInt(caps.border, RoboLayoutBorderImplemented);
    RoboCheckEqInt(caps.positioning, RoboLayoutPositioningImplemented);
    /*
     * And `overflow`, the field 10.13 added -- the first in this struct whose claim is
     * about something the box's own declarations did *not* produce. A size, an inset and
     * a displacement are all statements about the box; an overflow is a statement about
     * the contents, measured after they were placed, so a build with every field above it
     * set and no measurement could not have satisfied this one.
     */
    RoboCheckEqInt(caps.overflow, RoboLayoutOverflowImplemented);
    /*
     * And `intrinsicSizing`, the field 19.1 added -- the first in this struct whose claim
     * is about the *contents* rather than about the box: min-content and max-content are
     * what a box's children ask for before any containing block has a say, which is neither
     * a size (10.8) nor an overflow (10.13). A build with the whole sizing record and no
     * measurement of what is inside it would have satisfied every field above it.
     */
    RoboCheckEqInt(caps.intrinsicSizing, RoboLayoutIntrinsicSizingImplemented);
    /*
     * And `percentageResolution`, the field 19.2 added: a percentage keeps its magnitude and
     * resolves against the containing block once a pass hands the base over, which is a fact no
     * field above it states -- a build with every dimension, margin and padding record and no
     * percentage kind could have satisfied them all.
     */
    RoboCheckEqInt(caps.percentageResolution,
                   RoboLayoutPercentageResolutionImplemented);
    /*
     * And `flexLayout`, the field 19.3 added: a box whose inner display is `flex`
     * lays its in-flow children out as flex items along a main axis, which is a third
     * formatting context no field above it names.
     */
    RoboCheckEqInt(caps.flexLayout, RoboLayoutFlexLayoutImplemented);
    RoboCheckEqInt(caps.painting, 0);
    /* Both halves of the third row, so a build that set one of the pair without the
     * other is caught here rather than by a caller that trusted the constant. */
    RoboCheckEqInt(caps.geometry, caps.blockLayout);
}

static void TestModuleGeometryFlag(void)
{
    LayoutCapabilities caps;

    LayoutFixture();
    /*
     * The macro, the function and the field: three doors, one answer. A build
     * that flipped one of them would have two answers to one question, and this
     * is the question a caller asks before reading a result.
     *
     * 1 since 10.3, and the *field* is still the narrow one: "this build computes
     * coordinates" is true while "this box has one" is a different question, which
     * `LayoutResultState` answers per box.
     */
    RoboCheckEqInt(RoboLayoutGeometryImplemented, 1);
    RoboCheckEqInt(LayoutGeometryIsImplemented(), 1);
    RoboCheckEqInt(LayoutGetCapabilities(&caps), RoboOk);
    RoboCheckEqInt(caps.geometry, LayoutGeometryIsImplemented());
    /* And the block direction is where that 1 comes from, which is why the two
     * constants exist rather than one. */
    RoboCheckEqInt(RoboLayoutBlockLayoutImplemented, 1);
    RoboCheckEqInt(caps.blockLayout, RoboLayoutBlockLayoutImplemented);
    /*
     * And the second direction arrives the same way, constant and field together: a
     * macro that says a feature exists and a capability field that says it does not
     * are two answers to one question, and the question is the one a caller asks
     * before trusting a line of geometry.
     */
    RoboCheckEqInt(RoboLayoutInlineLayoutImplemented, 1);
    RoboCheckEqInt(caps.inlineLayout, RoboLayoutInlineLayoutImplemented);
    RoboCheckEqInt(caps.blockAndInlineLayout, 1);
    /*
     * And the *participant* this build can now measure, through the same pair: the
     * macro and the field, so a build whose header promised text measurement and whose
     * capability record denied it would fail rather than pass on the more flattering
     * of the two answers.
     */
    RoboCheckEqInt(RoboLayoutTextLayoutImplemented, 1);
    RoboCheckEqInt(caps.textLayout, RoboLayoutTextLayoutImplemented);
}

/* ------------------------------------------------------------------ */
/* Closed vocabularies                                                 */
/* ------------------------------------------------------------------ */

static void TestModuleObjectKinds(void)
{
    size_t index;

    RoboCheckEqInt((int)RoboLayoutObjectKindCount, 3);
    RoboCheckEqInt((int)RoboLayoutObjectNone, 0);
    RoboCheckEqInt((int)RoboLayoutObjectBox, 1);
    RoboCheckEqInt((int)RoboLayoutObjectText, 2);

    RoboCheckEqInt(LayoutObjectKindIsKnown(RoboLayoutObjectNone), 1);
    RoboCheckEqInt(LayoutObjectKindIsKnown(RoboLayoutObjectBox), 1);
    RoboCheckEqInt(LayoutObjectKindIsKnown(RoboLayoutObjectText), 1);
    RoboCheckEqInt(LayoutObjectKindIsKnown((LayoutObjectKind)3), 0);
    RoboCheckEqInt(LayoutObjectKindIsKnown((LayoutObjectKind)99), 0);

    /* Walked in enumerator order: a table whose rows had drifted would still
     * answer IsKnown for every one and would show only as a name that printed
     * as another's keyword. */
    for (index = 0u; index < RoboLayoutObjectKindCount; index++) {
        LayoutObjectKind kind = (LayoutObjectKind)index;

        RoboCheckEqInt(LayoutObjectKindIsKnown(kind), 1);
        RoboCheck(LayoutObjectKindName(kind) != NULL);
    }
    RoboCheckStrEq(LayoutObjectKindName(RoboLayoutObjectNone), "none");
    RoboCheckStrEq(LayoutObjectKindName(RoboLayoutObjectBox), "box");
    RoboCheckStrEq(LayoutObjectKindName(RoboLayoutObjectText), "text");
    RoboCheckStrEq(LayoutObjectKindName((LayoutObjectKind)77), "unknown");
    RoboCheck(LayoutObjectKindName((LayoutObjectKind)77) != NULL);
}

static void TestModuleDiagnosticDomains(void)
{
    size_t index;
    size_t seen[RoboLayoutDomainCount];

    RoboCheckEqInt((int)RoboLayoutDomainCount, 27);
    RoboCheckEqInt((int)RoboLayoutDomainNone, 0);
    RoboCheckEqInt(LayoutDiagnosticDomainIsKnown(RoboLayoutDomainTextLayout), 1);
    RoboCheckEqInt(LayoutDiagnosticDomainIsKnown(RoboLayoutDomainSizing), 1);
    RoboCheckEqInt(LayoutDiagnosticDomainIsKnown(RoboLayoutDomainMargins), 1);
    RoboCheckEqInt(LayoutDiagnosticDomainIsKnown(RoboLayoutDomainPadding), 1);
    RoboCheckEqInt(LayoutDiagnosticDomainIsKnown(RoboLayoutDomainBorder), 1);
    RoboCheckEqInt(LayoutDiagnosticDomainIsKnown(RoboLayoutDomainPosition), 1);
    RoboCheckEqInt(LayoutDiagnosticDomainIsKnown(RoboLayoutDomainOverflow), 1);
    RoboCheckEqInt(LayoutDiagnosticDomainIsKnown(RoboLayoutDomainInvalidation), 1);
    RoboCheckEqInt(LayoutDiagnosticDomainIsKnown(RoboLayoutDomainIntrinsic), 1);
    RoboCheckEqInt(LayoutDiagnosticDomainIsKnown(RoboLayoutDomainFlex), 1);
    RoboCheckEqInt(LayoutDiagnosticDomainIsKnown(RoboLayoutDomainGrid), 1);
    RoboCheckEqInt(LayoutDiagnosticDomainIsKnown(RoboLayoutDomainTable), 1);
    RoboCheckEqInt(LayoutDiagnosticDomainIsKnown((LayoutDiagnosticDomain)27), 0);
    RoboCheckEqInt(LayoutDiagnosticDomainIsKnown((LayoutDiagnosticDomain)200), 0);

    /* Every enumerator answers, every name is non-NULL, and no two names are the
     * same string -- which is the one thing a copy-pasted switch case breaks
     * while still compiling. */
    for (index = 0u; index < RoboLayoutDomainCount; index++) {
        LayoutDiagnosticDomain domain = (LayoutDiagnosticDomain)index;
        const char *name = LayoutDiagnosticDomainName(domain);
        size_t other;

        RoboCheckEqInt(LayoutDiagnosticDomainIsKnown(domain), 1);
        RoboCheck(name != NULL);
        if (!name) {
            continue;
        }
        seen[index] = strlen(name);
        RoboCheck(seen[index] > 0u);
        for (other = 0u; other < index; other++) {
            RoboCheck(strcmp(name,
                              LayoutDiagnosticDomainName(
                                  (LayoutDiagnosticDomain)other)) != 0);
        }
    }
    RoboCheckStrEq(LayoutDiagnosticDomainName(RoboLayoutDomainTree), "tree");
    /* The two 10.2/10.3 domains, named rather than only walked: a diagnostic that
     * said "domain 11" would be a message nobody could act on. 10.2's formatting
     * contexts are no longer last, and the assertion below says who is: the domain
     * a failure is reported in is the phase that can fail, so "the newest domain is
     * the last enumerator" is a fact about the build and not about the numbering. */
    RoboCheckStrEq(LayoutDiagnosticDomainName(
                          RoboLayoutDomainFormattingContext),
                      "formatting-context");
    RoboCheckStrEq(LayoutDiagnosticDomainName(RoboLayoutDomainBlockLayout),
                      "block-layout");
    /* Each phase's domain, named and numbered, so that "the newest domain is the
     * last enumerator" stays a fact about the build rather than about the numbering:
     * the assertion below is that a failure *lands* in the domain its phase added. */
    RoboCheckStrEq(LayoutDiagnosticDomainName(RoboLayoutDomainInlineLayout),
                      "inline-layout");
    RoboCheckStrEq(LayoutDiagnosticDomainName(RoboLayoutDomainTextLayout),
                      "text-layout");
    /* 10.8's domain, three behind the newest since 10.11. */
    RoboCheckStrEq(LayoutDiagnosticDomainName(RoboLayoutDomainSizing),
                      "sizing");
    RoboCheckStrEq(LayoutDiagnosticDomainName(RoboLayoutDomainMargins),
                      "margins");
    /* 10.10's, the one that was newest before this phase. */
    RoboCheckStrEq(LayoutDiagnosticDomainName(RoboLayoutDomainPadding),
                      "padding");
    /* 10.11's, and it is the newest: the two derived numbers above a border are the only
     * arithmetic in this layer that can be handed a record it cannot use -- a *negative*
     * side is a record this module's own resolver never produces, so one arriving at a
     * size is a caller bug and the moment to see it is before a size is derived from it. */
    RoboCheckStrEq(LayoutDiagnosticDomainName(RoboLayoutDomainBorder),
                      "border");
    RoboCheckStrEq(LayoutDiagnosticDomainName(RoboLayoutDomainPosition),
                      "position");
    RoboCheckStrEq(LayoutDiagnosticDomainName(RoboLayoutDomainOverflow),
                      "overflow");
    /* 10.14's, and it is the newest: the first domain in this module whose subject is
     * not a box's geometry but its *truth*, which is why it ends the list rather than
     * slotting beside the geometry domains whose conclusions it expires. */
    RoboCheckStrEq(LayoutDiagnosticDomainName(RoboLayoutDomainInvalidation),
                      "invalidation");
    /* 14.0's, no longer the newest: placing a box out of flow is a question about
     * *containment* rather than about a box's own geometry, which is why it ended the list
     * rather than slotting beside the layers it reads. */
    RoboCheckStrEq(LayoutDiagnosticDomainName(RoboLayoutDomainAbsolute), "absolute");
    /* 19.1's, and it *is* the newest for a stated reason: a box's intrinsic size is a
     * measurement of its contents taken before anything is placed, which is neither a box's
     * own geometry (where 14.0's conclusion lives) nor a truth expired by a change (10.14's),
     * so it ends the list rather than slotting beside the resolvers that read the answer. */
    RoboCheckStrEq(LayoutDiagnosticDomainName(RoboLayoutDomainIntrinsic), "intrinsic");
    /* 19.3's: a flex context is a third way to lay contents out, so its failures land
     * in their own domain rather than in the block one. */
    RoboCheckStrEq(LayoutDiagnosticDomainName(RoboLayoutDomainFlex), "flex");
    /* 19.4's: a grid context is a fourth way to lay contents out, and it has a second
     * axis a flex container does not. No longer the newest since 19.6. */
    RoboCheckStrEq(LayoutDiagnosticDomainName(RoboLayoutDomainGrid), "grid");
    /* 19.6's, and it *is* the newest for the same reason a fifth time: a table context
     * is a fifth way to lay contents out, and it resolves its two axes from its
     * *contents* rather than from declarations. */
    RoboCheckStrEq(LayoutDiagnosticDomainName(RoboLayoutDomainTable), "table");
    RoboCheckEqInt((int)RoboLayoutDomainTable,
                      (int)RoboLayoutDomainCount - 1);
    RoboCheckEqInt((int)RoboLayoutDomainGrid,
                      (int)RoboLayoutDomainTable - 1);
    RoboCheckEqInt((int)RoboLayoutDomainFlex,
                      (int)RoboLayoutDomainGrid - 1);
    RoboCheckEqInt((int)RoboLayoutDomainIntrinsic,
                      (int)RoboLayoutDomainFlex - 1);
    RoboCheckEqInt((int)RoboLayoutDomainAbsolute,
                      (int)RoboLayoutDomainIntrinsic - 1);
    RoboCheckEqInt((int)RoboLayoutDomainOverflow,
                      (int)RoboLayoutDomainInvalidation - 1);
    RoboCheckEqInt((int)RoboLayoutDomainPosition,
                      (int)RoboLayoutDomainOverflow - 1);
    RoboCheckEqInt((int)RoboLayoutDomainPadding,
                      (int)RoboLayoutDomainBorder - 1);
    RoboCheckEqInt((int)RoboLayoutDomainMargins,
                      (int)RoboLayoutDomainPadding - 1);
    RoboCheckEqInt((int)RoboLayoutDomainSizing,
                      (int)RoboLayoutDomainMargins - 1);
    RoboCheckEqInt((int)RoboLayoutDomainTextLayout,
                      (int)RoboLayoutDomainSizing - 1);
    RoboCheckEqInt((int)RoboLayoutDomainInlineLayout,
                      (int)RoboLayoutDomainTextLayout - 1);
    RoboCheckEqInt((int)RoboLayoutDomainBlockLayout,
                      (int)RoboLayoutDomainInlineLayout - 1);
    RoboCheckEqInt((int)RoboLayoutDomainFormattingContext,
                      (int)RoboLayoutDomainBlockLayout - 1);
    RoboCheckStrEq(LayoutDiagnosticDomainName((LayoutDiagnosticDomain)400),
                      "unknown");
}

static void TestModuleResultStates(void)
{
    RoboCheckEqInt((int)RoboLayoutResultStateCount, 3);
    RoboCheckEqInt((int)RoboLayoutResultNone, 0);
    RoboCheckEqInt((int)RoboLayoutResultUnresolved, 1);
    RoboCheckEqInt((int)RoboLayoutResultComputed, 2);

    RoboCheckEqInt(LayoutResultStateIsKnown(RoboLayoutResultNone), 1);
    RoboCheckEqInt(LayoutResultStateIsKnown(RoboLayoutResultUnresolved), 1);
    RoboCheckEqInt(LayoutResultStateIsKnown(RoboLayoutResultComputed), 1);
    RoboCheckEqInt(LayoutResultStateIsKnown((LayoutResultState)3), 0);

    RoboCheckStrEq(LayoutResultStateName(RoboLayoutResultNone), "none");
    RoboCheckStrEq(LayoutResultStateName(RoboLayoutResultUnresolved), "unresolved");
    RoboCheckStrEq(LayoutResultStateName(RoboLayoutResultComputed), "computed");
    RoboCheckStrEq(LayoutResultStateName((LayoutResultState)9), "unknown");
}

static void TestModulePassOutcomes(void)
{
    RoboCheckEqInt((int)RoboLayoutPassOutcomeCount, 4);
    RoboCheckEqInt((int)RoboLayoutPassNone, 0);
    RoboCheckEqInt((int)RoboLayoutPassWalked, 1);
    RoboCheckEqInt((int)RoboLayoutPassFailed, 2);
    /* 10.14's, and appended rather than inserted: the three older values keep their
     * numbers, because a caller that stored one of them must not be renumbered by a
     * phase that added a fourth. */
    RoboCheckEqInt((int)RoboLayoutPassClean, 3);

    RoboCheckEqInt(LayoutPassOutcomeIsKnown(RoboLayoutPassNone), 1);
    RoboCheckEqInt(LayoutPassOutcomeIsKnown(RoboLayoutPassWalked), 1);
    RoboCheckEqInt(LayoutPassOutcomeIsKnown(RoboLayoutPassFailed), 1);
    RoboCheckEqInt(LayoutPassOutcomeIsKnown(RoboLayoutPassClean), 1);
    RoboCheckEqInt(LayoutPassOutcomeIsKnown((LayoutPassOutcome)4), 0);

    RoboCheckStrEq(LayoutPassOutcomeName(RoboLayoutPassNone), "none");
    RoboCheckStrEq(LayoutPassOutcomeName(RoboLayoutPassWalked), "walked");
    RoboCheckStrEq(LayoutPassOutcomeName(RoboLayoutPassFailed), "failed");
    RoboCheckStrEq(LayoutPassOutcomeName(RoboLayoutPassClean), "clean");
    RoboCheckStrEq(LayoutPassOutcomeName((LayoutPassOutcome)55), "unknown");
}

/* ------------------------------------------------------------------ */
/* Geometry is borrowed, not redeclared                                */
/* ------------------------------------------------------------------ */

static void TestModuleGeometryAliases(void)
{
    LayoutPoint point;
    LayoutSize size;
    LayoutRect rect;
    LayoutRectF rectF;
    GfxPoint gfxPoint;
    GfxSize gfxSize;
    GfxRect gfxRect;
    GfxRectF gfxRectF;

    /* Same type, by size... */
    RoboCheckEqInt(sizeof(LayoutPoint), sizeof(GfxPoint));
    RoboCheckEqInt(sizeof(LayoutSize), sizeof(GfxSize));
    RoboCheckEqInt(sizeof(LayoutRect), sizeof(GfxRect));
    RoboCheckEqInt(sizeof(LayoutRectF), sizeof(GfxRectF));

    /* ...and by assignment, which is the claim that actually matters: an object
     * built as a layout rectangle is passed to paint as a graphics rectangle
     * with no conversion, and one field write is visible from the other name. */
    gfxPoint.x = 7;
    gfxPoint.y = -3;
    point = gfxPoint;
    RoboCheckEqInt(point.x, 7);
    RoboCheckEqInt(point.y, -3);

    gfxSize.width = 128;
    gfxSize.height = 64;
    size = gfxSize;
    RoboCheckEqInt(size.width, 128);
    RoboCheckEqInt(size.height, 64);

    gfxRect.x = 1;
    gfxRect.y = 2;
    gfxRect.width = 3;
    gfxRect.height = 4;
    rect = gfxRect;
    RoboCheckEqInt(rect.x, 1);
    RoboCheckEqInt(rect.width, 3);
    RoboCheckEqInt(rect.height, 4);

    /* The float form is the same type too, so a sub-pixel rectangle travels to
     * paint the same way -- it is *not* implicitly convertible from the integer
     * one, which is a deliberate property of RoboGraphics rather than something
     * layout may paper over. */
    gfxRectF.x = 0.5f;
    gfxRectF.y = 1.5f;
    gfxRectF.width = 2.5f;
    gfxRectF.height = 3.5f;
    rectF = gfxRectF;
    RoboCheckEqInt((int)rectF.width, 2);
    RoboCheckEqInt((int)(rectF.height * 2.0f), 7);
}

/* ------------------------------------------------------------------ */
/* Diagnostics                                                         */
/* ------------------------------------------------------------------ */

static void TestModuleDiagnostics(void)
{
    LayoutDiag diag = RoboLayoutCleared;
    Result code;

    LayoutFixture();

    LayoutDiagReset(&diag);
    RoboCheckEqInt(diag.code, RoboOk);
    RoboCheckEqInt((int)diag.domain, (int)RoboLayoutDomainNone);
    RoboCheck(diag.message == NULL);
    LayoutDiagReset(NULL);

    RoboCheckEqInt(LayoutDiagSet(&diag, RoboNotFound, RoboLayoutDomainTree,
                                           "no such box", __FILE__, __LINE__),
                      RoboNotFound);
    RoboCheckEqInt(diag.code, RoboNotFound);
    RoboCheckEqInt((int)diag.domain, (int)RoboLayoutDomainTree);
    RoboCheckStrEq(diag.message, "no such box");
    RoboCheck(diag.file != NULL);
    RoboCheck(diag.line > 0);

    /* A record with no caller-owned home is not a failure: the result code is
     * authoritative and the diag is optional. */
    RoboCheckEqInt(LayoutDiagRecord(NULL, RoboInvalidArgument,
                                              RoboLayoutDomainObject, "no record",
                                              __FILE__, __LINE__),
                      RoboInvalidArgument);
    RoboCheckEqInt(LayoutDiagSet(NULL, RoboError, RoboLayoutDomainModule,
                                           "no record", __FILE__, __LINE__),
                      RoboInvalidArgument);

    /* RoboOk clears rather than reports. */
    RoboCheckEqInt(LayoutDiagSet(&diag, RoboOk, RoboLayoutDomainTree,
                                           "nothing wrong", __FILE__, __LINE__),
                      RoboOk);
    RoboCheckEqInt(diag.code, RoboOk);

    /* The macro evaluates to its code with or without a record. */
    code = RoboLayoutDiag(&diag, RoboLayoutDomainConstraints, RoboInvalidArgument,
                            "negative size");
    RoboCheckEqInt(code, RoboInvalidArgument);
    RoboCheckEqInt(diag.code, RoboInvalidArgument);
    code = RoboLayoutDiag(NULL, RoboLayoutDomainConstraints, RoboInvalidArgument,
                            "negative size");
    RoboCheckEqInt(code, RoboInvalidArgument);
}

/* ------------------------------------------------------------------ */
/* The module owns nothing                                             */
/* ------------------------------------------------------------------ */

static void TestModuleAllocatorBalance(void)
{
    size_t before;
    LayoutCapabilities caps;
    LayoutDiag diag = RoboLayoutCleared;
    char buffer[32];
    size_t i;

    LayoutFixture();
    before = LayoutLiveBlocks();

    /* Every module-level door, called hard: none of them may allocate. That is
     * what "the module owns no global state" means, and it is the reason init
     * cannot fail for an allocation reason. */
    for (i = 0u; i < 64u; i++) {
        RoboCheckEqInt(LayoutGetCapabilities(&caps), RoboOk);
        RoboCheckEqInt(LayoutGetVersion(&caps.objectModel, &caps.constraints,
                                                  &caps.resultModel),
                          RoboOk);
        RoboCheckStrEq(LayoutVersionString(), "0.22.0");
        RoboCheckEqInt(LayoutGeometryIsImplemented(), 1);
        memset(buffer, 0, sizeof(buffer));
        strncpy(buffer, LayoutObjectKindName(RoboLayoutObjectBox),
                sizeof(buffer) - 1u);
        strncpy(buffer, LayoutDiagnosticDomainName(RoboLayoutDomainResult),
                sizeof(buffer) - 1u);
        strncpy(buffer, LayoutResultStateName(RoboLayoutResultUnresolved),
                sizeof(buffer) - 1u);
        strncpy(buffer, LayoutPassOutcomeName(RoboLayoutPassWalked),
                sizeof(buffer) - 1u);
        RoboCheckEqInt(LayoutDiagRecord(&diag, RoboError,
                                                  RoboLayoutDomainModule,
                                                  "guess", __FILE__, __LINE__),
                          RoboError);
    }
    LayoutDiagReset(&diag);

    RoboCheckEqInt(LayoutLiveBlocks(), before);
}

/* ------------------------------------------------------------------ */

void TestRegisterLayout(void);

void TestRegisterLayout(void)
{
    TestRegister("layout.module.version", TestModuleVersion);
    TestRegister("layout.module.lifecycle", TestModuleLifecycle);
    TestRegister("layout.module.capabilities", TestModuleCapabilities);
    TestRegister("layout.module.geometry_flag", TestModuleGeometryFlag);
    TestRegister("layout.module.object_kinds", TestModuleObjectKinds);
    TestRegister("layout.module.diagnostic_domains", TestModuleDiagnosticDomains);
    TestRegister("layout.module.result_states", TestModuleResultStates);
    TestRegister("layout.module.pass_outcomes", TestModulePassOutcomes);
    TestRegister("layout.module.geometry_aliases", TestModuleGeometryAliases);
    TestRegister("layout.module.diagnostics", TestModuleDiagnostics);
    TestRegister("layout.module.allocator_balance", TestModuleAllocatorBalance);
}
