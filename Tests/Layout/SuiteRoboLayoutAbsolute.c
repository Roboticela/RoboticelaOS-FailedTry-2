/*
 * RoboLayout out-of-flow suite (Phase 14.0) — `layout.absolute.*`.
 *
 * Five things are under test, and the boundaries between them are the phase:
 *
 *   the **vocabulary** -- two axes, four kinds of declaration over two sides, and the
 *   containing block as a *rectangle* rather than a size, because the origin is half the
 *   answer for a box placed against a wrapper;
 *
 *   the **axis policy**, driven directly over fabricated position records, so a wrong
 *   precedence fails at the arithmetic rather than three boxes later in a coordinate. The
 *   three rules, one case each: the near side wins; the near side saying nothing lets the
 *   **far** side position the axis; and two present insets size the axis between them when
 *   the declared dimension is `auto` -- which is the rule a build that read
 *   `left: 0; right: 0` as "fill the containing block" would pass only by accident;
 *
 *   the two **doors that need the box's own size** -- the origin (`right: 0` is not a
 *   coordinate until the box has a width) and the stretched content height -- because they
 *   are the reason this phase is a second call and not an offset;
 *
 *   the **pass**: a box taken out of flow, placed against the right containing block
 *   (a positioned ancestor's padding box, or the viewport), *not* taking room in flow and
 *   *not* growing its parent. The sibling below it is the witness:
 *   `layout.absolute.block_positioned_wrapper` fails a build that placed the box correctly
 *   and left its space reserved, which is the most plausible way to get this phase half
 *   right. (This comment named that witness `layout.absolute.flow_untouched` until Phase
 *   19.5 was re-verified -- a name no case has ever had, which is exactly the sort of drift
 *   a re-verification is for; §7 of `OutOfFlow.md` is the map of the names that do exist.)
 *
 *   the **counters**, because several of them describe facts that look identical in geometry:
 *   a box sized by two insets and one that happens to be that wide, an axis anchored by its
 *   far side and one with no inset at all, a `fixed` box and an `absolute` box with no
 *   positioned ancestor -- and because the record is part of the block record's own
 *   *identity*, which is the assertion that caught it being left out of the comparison.
 *
 * The scripts here are *computed styles*, built through the computed-value makers rather
 * than as hand-written structs, for the reason 10.8's, 10.9's, 10.10's, 10.11's and 10.12's
 * suites each give: a fabricated struct would prove the pass reads a struct rather than a
 * computed CSS value, and `position` is 9.13's own typed vocabulary.
 */

#include "RoboLayoutTestSupport.h"

#include <string.h>

/* ------------------------------------------------------------------ */
/* A fabricated style that can declare anything                        */
/* ------------------------------------------------------------------ */

typedef enum layoutAbsoluteDeclKind {
    LAYOUT_ABSOLUTE_DECL_PX = 0,
    LAYOUT_ABSOLUTE_DECL_NEGATIVE_PX = 1,
    /** A px length flagged unresolved, which is what a `var()` arrives as. */
    LAYOUT_ABSOLUTE_DECL_UNRESOLVED_PX = 2,
    LAYOUT_ABSOLUTE_DECL_PERCENTAGE = 3,
    LAYOUT_ABSOLUTE_DECL_EM = 4,
    /** The keyword `auto`, or any other identifier the grammar allows. */
    LAYOUT_ABSOLUTE_DECL_KEYWORD = 5,
    /** A `CssComputedValue` of kind POSITION -- 9.13's typed vocabulary. */
    LAYOUT_ABSOLUTE_DECL_POSITION = 6
} LayoutAbsoluteDeclKind;

typedef struct layoutAbsoluteDecl {
    const char *name;
    LayoutAbsoluteDeclKind kind;
    double number;
    const char *keyword;
    CssPositioning position;
} LayoutAbsoluteDecl;

#define LAYOUT_ABSOLUTE_MAX_DECLARATIONS 20u
#define LAYOUT_ABSOLUTE_MAX_RULES 8u

typedef struct layoutAbsoluteRule {
    const char *tag;
    CssDisplay display;
    LayoutAbsoluteDecl declarations[LAYOUT_ABSOLUTE_MAX_DECLARATIONS];
    size_t declarationsCount;
} LayoutAbsoluteRule;

static void LayoutAbsoluteRuleInit(LayoutAbsoluteRule *rule,
                                      const char *tag,
                                      CssDisplay display)
{
    memset(rule, 0, sizeof(*rule));
    rule->tag = tag;
    rule->display = display;
}

static LayoutAbsoluteDecl *LayoutAbsoluteRuleAdd(LayoutAbsoluteRule *rule,
                                                          const char *name,
                                                          LayoutAbsoluteDeclKind kind)
{
    LayoutAbsoluteDecl *declaration;

    RoboCheckEqInt(rule->declarationsCount < LAYOUT_ABSOLUTE_MAX_DECLARATIONS, 1);
    declaration = &rule->declarations[rule->declarationsCount];
    memset(declaration, 0, sizeof(*declaration));
    declaration->name = name;
    declaration->kind = kind;
    declaration->position = RoboCssPositioningStatic;
    rule->declarationsCount++;
    return declaration;
}

static void LayoutAbsoluteRulePx(LayoutAbsoluteRule *rule, const char *name, double px)
{
    LayoutAbsoluteRuleAdd(rule, name, LAYOUT_ABSOLUTE_DECL_PX)->number = px;
}

static void LayoutAbsoluteRuleNegative(LayoutAbsoluteRule *rule,
                                          const char *name,
                                          double px)
{
    LayoutAbsoluteRuleAdd(rule, name, LAYOUT_ABSOLUTE_DECL_NEGATIVE_PX)->number = -px;
}

static void LayoutAbsoluteRuleUnresolved(LayoutAbsoluteRule *rule,
                                            const char *name,
                                            double px)
{
    LayoutAbsoluteRuleAdd(rule, name, LAYOUT_ABSOLUTE_DECL_UNRESOLVED_PX)->number = px;
}

static void LayoutAbsoluteRulePercentage(LayoutAbsoluteRule *rule,
                                            const char *name,
                                            double percent)
{
    LayoutAbsoluteRuleAdd(rule, name, LAYOUT_ABSOLUTE_DECL_PERCENTAGE)->number = percent;
}

static void LayoutAbsoluteRuleKeyword(LayoutAbsoluteRule *rule,
                                         const char *name,
                                         const char *keyword)
{
    LayoutAbsoluteRuleAdd(rule, name, LAYOUT_ABSOLUTE_DECL_KEYWORD)->keyword = keyword;
}

static void LayoutAbsoluteRulePosition(LayoutAbsoluteRule *rule, CssPositioning mode)
{
    LayoutAbsoluteRuleAdd(rule, "position", LAYOUT_ABSOLUTE_DECL_POSITION)->position = mode;
}

typedef struct layoutAbsoluteStyleSlot {
    char tag[16];
    CssComputedStyle *style; /* owned */
    size_t answers;
} LayoutAbsoluteStyleSlot;

typedef struct layoutAbsoluteSource {
    LayoutAbsoluteStyleSlot rules[LAYOUT_ABSOLUTE_MAX_RULES];
    size_t count;
} LayoutAbsoluteSource;

static CssComputedStyle *LayoutAbsoluteStyleBuild(const LayoutAbsoluteRule *rule)
{
    CssComputedStyle *style = NULL;
    CssComputedValue value = RoboCssCleared;
    CssValue specified = RoboCssCleared;
    size_t index;

    RoboCheckEqInt(CssComputedStyleCreate(&style, NULL), RoboOk);
    RoboCheckEqInt(CssComputedValueMakeDisplay(&value, rule->display, NULL), RoboOk);
    RoboCheckEqInt(CssComputedStyleSetEntry(style, RoboCssPropertyIdNone,
                                                       CssSliceFromText("display"), 0u,
                                                       &specified, &value, NULL),
                      RoboOk);

    for (index = 0u; index < rule->declarationsCount; index++) {
        const LayoutAbsoluteDecl *declaration = &rule->declarations[index];

        value = (CssComputedValue)RoboCssCleared;
        specified = (CssValue)RoboCssCleared;
        switch (declaration->kind) {
        case LAYOUT_ABSOLUTE_DECL_POSITION:
            RoboCheckEqInt(CssComputedValueMakePosition(&value, declaration->position,
                                                                   NULL),
                              RoboOk);
            break;
        case LAYOUT_ABSOLUTE_DECL_PERCENTAGE:
            RoboCheckEqInt(CssComputedValueMakePercentage(&value, declaration->number,
                                                                     0, NULL),
                              RoboOk);
            break;
        case LAYOUT_ABSOLUTE_DECL_KEYWORD:
            RoboCheckEqInt(CssComputedValueMakeKeyword(
                                  &value, CssSliceFromText(declaration->keyword), NULL),
                              RoboOk);
            break;
        case LAYOUT_ABSOLUTE_DECL_UNRESOLVED_PX:
            RoboCheckEqInt(CssComputedValueMakeLength(&value, declaration->number,
                                                                 RoboCssUnitPx, 0, NULL),
                              RoboOk);
            /* The flag the computed layer sets for a `var()` or a `calc()`: carried
             * intact and not a value *yet*, whatever its kind says. */
            value.unresolved = 1;
            break;
        case LAYOUT_ABSOLUTE_DECL_EM:
            RoboCheckEqInt(CssComputedValueMakeLength(&value, declaration->number,
                                                                 RoboCssUnitEm, 0, NULL),
                              RoboOk);
            break;
        case LAYOUT_ABSOLUTE_DECL_NEGATIVE_PX:
        case LAYOUT_ABSOLUTE_DECL_PX:
        default:
            RoboCheckEqInt(CssComputedValueMakeLength(&value, declaration->number,
                                                                 RoboCssUnitPx, 0, NULL),
                              RoboOk);
            break;
        }
        RoboCheckEqInt(CssComputedStyleSetEntry(
                              style, RoboCssPropertyIdNone,
                              CssSliceFromText(declaration->name), 0u, &specified, &value,
                              NULL),
                          RoboOk);
    }
    return style;
}

static CssComputedStyle *LayoutAbsoluteStyleAdd(LayoutAbsoluteSource *source,
                                                            const LayoutAbsoluteRule *rule)
{
    size_t slot;
    CssComputedStyle *style;

    LayoutFixture();
    RoboCheckEqInt(source->count < LAYOUT_ABSOLUTE_MAX_RULES, 1);
    slot = source->count;
    style = LayoutAbsoluteStyleBuild(rule);
    strncpy(source->rules[slot].tag, rule->tag, sizeof(source->rules[slot].tag) - 1u);
    source->rules[slot].tag[sizeof(source->rules[slot].tag) - 1u] = '\0';
    source->rules[slot].style = style;
    source->rules[slot].answers = 0u;
    source->count++;
    return style;
}

static const CssComputedStyle *LayoutAbsoluteStyleLookup(
    void *userdata,
    const HtmlNodeHandle *element)
{
    LayoutAbsoluteSource *source = (LayoutAbsoluteSource *)userdata;
    const char *name;
    size_t index;

    if (!source || !element) {
        return NULL;
    }
    name = HtmlElementName(element, NULL);
    if (!name || name[0] == '\0') {
        return NULL;
    }
    for (index = 0u; index < source->count; index++) {
        if (strcmp(source->rules[index].tag, name) == 0) {
            source->rules[index].answers++;
            return source->rules[index].style;
        }
    }
    /* An element nothing was declared for: `display: inline`, `position: static`, and
     * those are CSS's initial values rather than a fixture default. */
    return NULL;
}

typedef struct layoutAbsoluteFixture {
    LayoutDocument doc;
    LayoutAbsoluteSource source;
    LayoutStyleSource styles;
    LayoutTree *tree;
} LayoutAbsoluteFixture;

static void LayoutAbsoluteBegin(LayoutAbsoluteFixture *fixture,
                                  const char *html,
                                  const LayoutAbsoluteRule *rules,
                                  size_t ruleCount)
{
    LayoutBuildOptions options;
    size_t index;

    memset(fixture, 0, sizeof(*fixture));
    LayoutDocumentBuild(&fixture->doc, html);
    for (index = 0u; index < ruleCount; index++) {
        (void)LayoutAbsoluteStyleAdd(&fixture->source, &rules[index]);
    }
    LayoutStyleSourceInit(&fixture->styles);
    fixture->styles.userdata = &fixture->source;
    fixture->styles.ComputedStyleFor = LayoutAbsoluteStyleLookup;
    RoboCheckEqInt(LayoutTreeCreate(&fixture->tree, NULL), RoboOk);
    LayoutBuildOptionsInit(&options);
    options.styles = &fixture->styles;
    RoboCheckEqInt(LayoutTreeBuildFromDocument(fixture->tree, fixture->doc.id,
                                                          &options, NULL, NULL),
                      RoboOk);
}

static void LayoutAbsoluteEnd(LayoutAbsoluteFixture *fixture)
{
    size_t index;

    LayoutTreeDestroy(fixture->tree);
    fixture->tree = NULL;
    for (index = 0u; index < fixture->source.count; index++) {
        CssComputedStyleDestroy(fixture->source.rules[index].style);
        fixture->source.rules[index].style = NULL;
    }
    fixture->source.count = 0u;
    LayoutDocumentRelease(&fixture->doc);
}

#define LAYOUT_ABSOLUTE_VIEWPORT_WIDTH 800
#define LAYOUT_ABSOLUTE_VIEWPORT_HEIGHT 600

static void LayoutAbsolutePass(LayoutAbsoluteFixture *fixture,
                                 int32_t viewportWidth,
                                 int32_t viewportHeight,
                                 LayoutStats *outStats)
{
    LayoutContext *context = NULL;
    LayoutSize viewport;
    LayoutStats stats;

    viewport.width = viewportWidth;
    viewport.height = viewportHeight;
    RoboCheckEqInt(LayoutContextCreate(&context, NULL), RoboOk);
    RoboCheckEqInt(LayoutContextSetViewport(context, viewport, NULL), RoboOk);
    RoboCheckEqInt(LayoutContextRun(context, fixture->tree, NULL), RoboOk);
    LayoutContextStats(context, &stats);
    LayoutContextDestroy(context);
    if (outStats) {
        *outStats = stats;
    }
}

static LayoutNodeId LayoutAbsoluteBoxFor(LayoutAbsoluteFixture *fixture,
                                                     const char *tag)
{
    HtmlNodeHandle element = LayoutDocumentFind(&fixture->doc, tag);
    LayoutNodeId id;

    RoboCheckEqInt(HtmlNodeHandleIsWellformed(&element), 1);
    id = LayoutObjectIdForDomNode(fixture->tree, &element);
    RoboCheck(id != RoboLayoutNodeIdNone);
    return id;
}

static LayoutPoint LayoutAbsoluteOriginOf(LayoutAbsoluteFixture *fixture,
                                                     LayoutNodeId id)
{
    LayoutResult result;
    LayoutPoint position;

    RoboCheckEqInt(LayoutObjectResult(fixture->tree, id, &result), RoboOk);
    RoboCheckEqInt((int)result.state, (int)RoboLayoutResultComputed);
    RoboCheckEqInt(LayoutResultPosition(&result, &position), RoboOk);
    return position;
}

static LayoutSize LayoutAbsoluteSizeOf(LayoutAbsoluteFixture *fixture,
                                                  LayoutNodeId id)
{
    LayoutResult result;
    LayoutSize size;

    RoboCheckEqInt(LayoutObjectResult(fixture->tree, id, &result), RoboOk);
    RoboCheckEqInt((int)result.state, (int)RoboLayoutResultComputed);
    RoboCheckEqInt(LayoutResultSize(&result, &size), RoboOk);
    return size;
}

/** A fabricated position record: an axis's two sides and a mode. */
static void LayoutAbsolutePositionReset(LayoutPosition *position,
                                          CssPositioning mode)
{
    LayoutPositionInit(position);
    position->mode = mode;
}

/**
 * Declare one inset on a fabricated position record.
 *
 * Written by side rather than by a setter door, because the record *is* the four sides: a
 * `LayoutInsets` is a value with four named fields, and a fabricated record that spelled
 * `insets.top` would be no more or less a position than this one. The policy under test is
 * a function of the record, and the seam that reads a *style* into one is asserted
 * separately (`layout.absolute.policy_unresolved` builds one through 9.13's makers).
 */
static void LayoutAbsolutePositionSet(LayoutPosition *position,
                                         CssPositioning mode,
                                         CssBoxSide side,
                                         LayoutInset inset)
{
    switch (side) {
    case RoboCssBoxSideTop:
        position->insets.top = inset;
        break;
    case RoboCssBoxSideRight:
        position->insets.right = inset;
        break;
    case RoboCssBoxSideBottom:
        position->insets.bottom = inset;
        break;
    case RoboCssBoxSideLeft:
    default:
        position->insets.left = inset;
        break;
    }
    position->mode = mode;
}

/* ------------------------------------------------------------------ */
/* The vocabulary                                                      */
/* ------------------------------------------------------------------ */

static void TestAbsoluteAxisVocabulary(void)
{
    RoboCheckEqInt((int)RoboLayoutAbsoluteAxisCount, 2);
    RoboCheckEqInt(LayoutAbsoluteAxisIsKnown(RoboLayoutAbsoluteAxisHorizontal), 1);
    RoboCheckEqInt(LayoutAbsoluteAxisIsKnown(RoboLayoutAbsoluteAxisVertical), 1);
    RoboCheckEqInt(LayoutAbsoluteAxisIsKnown((LayoutAbsoluteAxis)7), 0);
    RoboCheckEqInt(strcmp(LayoutAbsoluteAxisName(RoboLayoutAbsoluteAxisHorizontal),
                          "horizontal"),
                   0);
    RoboCheckEqInt(strcmp(LayoutAbsoluteAxisName(RoboLayoutAbsoluteAxisVertical),
                          "vertical"),
                   0);
    /* A name for a value outside the vocabulary is *answered*, not refused: a diagnostics
     * writer must never be the thing that fails a pass. */
    RoboCheckEqInt(strcmp(LayoutAbsoluteAxisName((LayoutAbsoluteAxis)9), "unknown"), 0);
}

static void TestAbsoluteSideVocabulary(void)
{
    LayoutAbsoluteSide absent = LayoutAbsoluteSideAbsent();
    LayoutAbsoluteEdges edges;

    RoboCheckEqInt(absent.present, 0);
    RoboCheckEqInt(absent.autoSide, 0);
    RoboCheckEqInt(absent.declared, 0);
    RoboCheckEqInt(absent.length, 0);

    LayoutAbsoluteEdgesInit(&edges);
    RoboCheckEqInt(LayoutAbsoluteEdgesEqual(&edges, &(LayoutAbsoluteEdges){
                                 LayoutAbsoluteSideAbsent(),
                                 LayoutAbsoluteSideAbsent()}),
                   1);
    /* A cleared record is *valid*: four undeclared sides is exactly what CSS's initial
     * value is, and a validity rule that refused it would refuse every document that
     * wrote no inset at all. */
    RoboCheckEqInt(LayoutAbsoluteEdgesIsValid(&edges), 1);

    edges.near = (LayoutAbsoluteSide){1, 0, 1, -10};
    edges.far = (LayoutAbsoluteSide){0, 1, 1, 0};
    RoboCheckEqInt(LayoutAbsoluteEdgesIsValid(&edges), 1);
    /* A side that claims a length without having declared one is a record that disagrees
     * with itself, and it is refused rather than believed. */
    edges.far = (LayoutAbsoluteSide){1, 0, 0, 4};
    RoboCheckEqInt(LayoutAbsoluteEdgesIsValid(&edges), 0);
}

static void TestAbsoluteContainingBlockRecord(void)
{
    LayoutAbsoluteContainingBlock block;

    LayoutAbsoluteContainingBlockInit(&block);
    RoboCheckEqInt(block.origin.x, 0);
    RoboCheckEqInt(block.origin.y, 0);
    RoboCheckEqInt(block.size.width, 0);
    /* A cleared record is *not* the initial containing block: the flag says a caller
     * decided this is the viewport, and nobody decided anything here. */
    RoboCheckEqInt(block.initial, 0);
    RoboCheckEqInt(LayoutAbsoluteContainingBlockIsValid(&block), 1);

    block.size.width = -1;
    RoboCheckEqInt(LayoutAbsoluteContainingBlockIsValid(&block), 0);
}

/* ------------------------------------------------------------------ */
/* The axis policy                                                     */
/* ------------------------------------------------------------------ */

static void TestAbsolutePolicyNearWins(void)
{
    LayoutPosition position;
    LayoutAbsoluteAxisUse use;
    LayoutDimension definite;

    /* `left` and `right` both declared **and a definite width**: the near side is used, the
     * far one is over-constrained and ignored, and the axis is *not* sized by its insets.
     * This is the pair of questions a build gets wrong together -- reading "two insets are
     * present" without also asking what the declared dimension was. */
    LayoutDimensionInit(&definite);
    definite.kind = RoboLayoutDimensionDefinite;
    definite.length = 100;

    LayoutAbsolutePositionReset(&position, RoboCssPositioningAbsolute);
    LayoutAbsolutePositionSet(&position, RoboCssPositioningAbsolute, RoboCssBoxSideLeft,
                                 LayoutInsetResolved(10));
    LayoutAbsolutePositionSet(&position, RoboCssPositioningAbsolute, RoboCssBoxSideRight,
                                 LayoutInsetResolved(4));
    RoboCheckEqInt(LayoutAbsoluteResolveAxis(&position,
                                                          RoboLayoutAbsoluteAxisHorizontal,
                                                          &definite, 400, &use, NULL),
                      RoboOk);
    RoboCheckEqInt(use.edges.near.present, 1);
    RoboCheckEqInt(use.edges.near.length, 10);
    RoboCheckEqInt(use.anchorFar, 0);
    RoboCheckEqInt(use.sizedByInsets, 0);
    RoboCheckEqInt(use.marginBoxExtent, -1);
    RoboCheckEqInt(use.declaredOverConstrained, 1);
    RoboCheckEqInt((int)use.sidesUsed, 2);
    RoboCheckEqInt((int)use.sidesAuto, 0);
    RoboCheckEqInt((int)use.sidesUnresolved, 0);

    /* `top` over `bottom` with a definite height: the vertical half of the same rule. */
    LayoutAbsolutePositionReset(&position, RoboCssPositioningAbsolute);
    LayoutAbsolutePositionSet(&position, RoboCssPositioningAbsolute, RoboCssBoxSideTop,
                                 LayoutInsetResolved(7));
    LayoutAbsolutePositionSet(&position, RoboCssPositioningAbsolute,
                                 RoboCssBoxSideBottom, LayoutInsetResolved(3));
    RoboCheckEqInt(LayoutAbsoluteResolveAxis(&position, RoboLayoutAbsoluteAxisVertical,
                                                          &definite, 300, &use, NULL),
                      RoboOk);
    RoboCheckEqInt(use.edges.near.length, 7);
    RoboCheckEqInt(use.anchorFar, 0);
    RoboCheckEqInt(use.declaredOverConstrained, 1);

    /* And the same two insets with **no** definite dimension: now they size the axis, and
     * the near side is still the one that positions it. Both facts at once, because a build
     * that got one right and the other wrong is the case this pair exists for. */
    LayoutAbsolutePositionReset(&position, RoboCssPositioningAbsolute);
    LayoutAbsolutePositionSet(&position, RoboCssPositioningAbsolute, RoboCssBoxSideLeft,
                                 LayoutInsetResolved(10));
    LayoutAbsolutePositionSet(&position, RoboCssPositioningAbsolute, RoboCssBoxSideRight,
                                 LayoutInsetResolved(4));
    RoboCheckEqInt(LayoutAbsoluteResolveAxis(&position,
                                                          RoboLayoutAbsoluteAxisHorizontal,
                                                          NULL, 400, &use, NULL),
                      RoboOk);
    RoboCheckEqInt(use.anchorFar, 0);
    RoboCheckEqInt(use.sizedByInsets, 1);
    RoboCheckEqInt(use.marginBoxExtent, 400 - 10 - 4);
    RoboCheckEqInt(use.declaredOverConstrained, 0);
}

static void TestAbsolutePolicyFarAnchors(void)
{
    LayoutPosition position;
    LayoutAbsoluteAxisUse use;

    /* `right: 8px` alone: nothing on the near side, so the **far** side positions this
     * axis -- and the extent is the box's own, which is why `marginBoxExtent` stays `-1`. */
    LayoutAbsolutePositionReset(&position, RoboCssPositioningFixed);
    LayoutAbsolutePositionSet(&position, RoboCssPositioningFixed, RoboCssBoxSideRight,
                                 LayoutInsetResolved(8));
    RoboCheckEqInt(LayoutAbsoluteResolveAxis(&position,
                                                          RoboLayoutAbsoluteAxisHorizontal,
                                                          NULL, 400, &use, NULL),
                      RoboOk);
    RoboCheckEqInt(use.anchorFar, 1);
    RoboCheckEqInt(use.edges.near.present, 0);
    RoboCheckEqInt(use.edges.far.present, 1);
    RoboCheckEqInt(use.marginBoxExtent, -1);
    RoboCheckEqInt(use.sizedByInsets, 0);

    /* The same side written `auto` and the far side declared: the far side still decides,
     * and the `auto` is *counted* rather than treated as nobody having written anything. */
    LayoutAbsolutePositionReset(&position, RoboCssPositioningAbsolute);
    LayoutAbsolutePositionSet(&position, RoboCssPositioningAbsolute, RoboCssBoxSideLeft,
                                 LayoutInsetAuto());
    LayoutAbsolutePositionSet(&position, RoboCssPositioningAbsolute, RoboCssBoxSideRight,
                                 LayoutInsetResolved(8));
    RoboCheckEqInt(LayoutAbsoluteResolveAxis(&position,
                                                          RoboLayoutAbsoluteAxisHorizontal,
                                                          NULL, 400, &use, NULL),
                      RoboOk);
    RoboCheckEqInt(use.anchorFar, 1);
    RoboCheckEqInt((int)use.sidesAuto, 1);
    RoboCheckEqInt((int)use.sidesUsed, 1);
}

static void TestAbsolutePolicySizeFromInsets(void)
{
    LayoutPosition position;
    LayoutAbsoluteAxisUse use;
    LayoutDimension autoWidth;

    LayoutAbsolutePositionReset(&position, RoboCssPositioningAbsolute);
    LayoutAbsolutePositionSet(&position, RoboCssPositioningAbsolute, RoboCssBoxSideLeft,
                                 LayoutInsetResolved(20));
    LayoutAbsolutePositionSet(&position, RoboCssPositioningAbsolute, RoboCssBoxSideRight,
                                 LayoutInsetResolved(30));
    LayoutDimensionInit(&autoWidth);
    RoboCheckEqInt(LayoutAbsoluteResolveAxis(&position,
                                                          RoboLayoutAbsoluteAxisHorizontal,
                                                          &autoWidth, 400, &use, NULL),
                      RoboOk);
    /* **The phase in one assertion**: `left: 20px; right: 30px` with `width: auto` sizes
     * the box to 400 - 20 - 30 = 350, which is not "the containing block" and not "the
     * contents". */
    RoboCheckEqInt(use.sizedByInsets, 1);
    RoboCheckEqInt(use.marginBoxExtent, 350);
    RoboCheckEqInt(use.anchorFar, 0);
    RoboCheckEqInt(use.declaredOverConstrained, 0);

    /* A **definite** declared width beside the same two insets is over-constrained: the
     * near inset and the size win, and the record says the document wrote one. */
    {
        LayoutDimension definite;

        LayoutDimensionInit(&definite);
        definite.kind = RoboLayoutDimensionDefinite;
        definite.length = 100;
        RoboCheckEqInt(LayoutAbsoluteResolveAxis(&position,
                                                              RoboLayoutAbsoluteAxisHorizontal,
                                                              &definite, 400, &use, NULL),
                          RoboOk);
        RoboCheckEqInt(use.sizedByInsets, 0);
        RoboCheckEqInt(use.marginBoxExtent, -1);
        RoboCheckEqInt(use.declaredOverConstrained, 1);
    }

    /* Insets larger than the containing block leave no room rather than a negative
     * extent, which is the same floor the padding layer gives a box narrower than its
     * own padding. */
    LayoutAbsolutePositionReset(&position, RoboCssPositioningAbsolute);
    LayoutAbsolutePositionSet(&position, RoboCssPositioningAbsolute, RoboCssBoxSideLeft,
                                 LayoutInsetResolved(300));
    LayoutAbsolutePositionSet(&position, RoboCssPositioningAbsolute, RoboCssBoxSideRight,
                                 LayoutInsetResolved(300));
    RoboCheckEqInt(LayoutAbsoluteResolveAxis(&position,
                                                          RoboLayoutAbsoluteAxisHorizontal,
                                                          NULL, 400, &use, NULL),
                      RoboOk);
    RoboCheckEqInt(use.sizedByInsets, 1);
    RoboCheckEqInt(use.marginBoxExtent, 0);
}

static void TestAbsolutePolicyUnresolvedAndRefusals(void)
{
    LayoutPosition position;
    LayoutAbsoluteAxisUse use;

    /* A percentage is a declaration this build cannot use: it reaches the layer, it is
     * **counted**, and its used value is zero. */
    LayoutAbsolutePositionReset(&position, RoboCssPositioningAbsolute);
    LayoutAbsolutePositionSet(&position, RoboCssPositioningAbsolute, RoboCssBoxSideLeft,
                                 LayoutInsetUnresolved());
    RoboCheckEqInt(LayoutAbsoluteResolveAxis(&position,
                                                          RoboLayoutAbsoluteAxisHorizontal,
                                                          NULL, 400, &use, NULL),
                      RoboOk);
    RoboCheckEqInt(use.edges.near.declared, 1);
    RoboCheckEqInt(use.edges.near.present, 0);
    RoboCheckEqInt(use.edges.near.autoSide, 0);
    RoboCheckEqInt((int)use.sidesUnresolved, 1);
    RoboCheckEqInt((int)use.sidesUsed, 0);
    RoboCheckEqInt(use.anchorFar, 0);

    /* A negative length is *usable*: `left: -10px` is legal CSS and a real technique. */
    LayoutAbsolutePositionReset(&position, RoboCssPositioningAbsolute);
    LayoutAbsolutePositionSet(&position, RoboCssPositioningAbsolute, RoboCssBoxSideLeft,
                                 LayoutInsetResolved(-10));
    RoboCheckEqInt(LayoutAbsoluteResolveAxis(&position,
                                                          RoboLayoutAbsoluteAxisHorizontal,
                                                          NULL, 400, &use, NULL),
                      RoboOk);
    RoboCheckEqInt(use.edges.near.length, -10);

    /* The refusals: a NULL out, an unknown axis, a negative containing extent. */
    RoboCheckEqInt(LayoutAbsoluteResolveAxis(NULL, RoboLayoutAbsoluteAxisHorizontal, NULL,
                                                          400, &use, NULL),
                      RoboInvalidArgument);
    RoboCheckEqInt(LayoutAbsoluteResolveAxis(&position, RoboLayoutAbsoluteAxisHorizontal,
                                                          NULL, 400, NULL, NULL),
                      RoboInvalidArgument);
    RoboCheckEqInt(LayoutAbsoluteResolveAxis(&position, (LayoutAbsoluteAxis)5, NULL, 400,
                                                          &use, NULL),
                      RoboInvalidArgument);
    RoboCheckEqInt(LayoutAbsoluteResolveAxis(&position, RoboLayoutAbsoluteAxisHorizontal,
                                                          NULL, -1, &use, NULL),
                      RoboInvalidArgument);
}

static void TestAbsoluteAxisUseRecord(void)
{
    LayoutAbsoluteAxisUse use;
    LayoutAbsoluteAxisUse other;

    LayoutAbsoluteAxisUseInit(&use);
    RoboCheckEqInt(use.marginBoxExtent, -1);
    RoboCheckEqInt(LayoutAbsoluteAxisUseIsValid(&use), 1);
    other = use;
    RoboCheckEqInt(LayoutAbsoluteAxisUseEqual(&use, &other), 1);
    other.sidesUsed = 1u;
    RoboCheckEqInt(LayoutAbsoluteAxisUseEqual(&use, &other), 0);

    /* A record that claims a stretched axis with no extent, and one that carries an extent
     * without claiming it, are both records that disagree with themselves. */
    use.sizedByInsets = 1;
    use.marginBoxExtent = -1;
    RoboCheckEqInt(LayoutAbsoluteAxisUseIsValid(&use), 0);
    use.sizedByInsets = 0;
    use.marginBoxExtent = 40;
    RoboCheckEqInt(LayoutAbsoluteAxisUseIsValid(&use), 0);
    use.marginBoxExtent = -1;
    use.sidesUsed = 3u;
    RoboCheckEqInt(LayoutAbsoluteAxisUseIsValid(&use), 0);
}

/* ------------------------------------------------------------------ */
/* The two doors that need the box's own size                          */
/* ------------------------------------------------------------------ */

static LayoutAbsolutePlacement AbsolutePlacementFor(LayoutPosition *position,
                                                      LayoutSize cbSize,
                                                      LayoutPoint cbOrigin,
                                                      LayoutDimension *width,
                                                      LayoutDimension *height)
{
    LayoutAbsolutePlacement placement;
    LayoutAbsoluteContainingBlock block;

    LayoutAbsoluteContainingBlockInit(&block);
    block.origin = cbOrigin;
    block.size = cbSize;
    block.initial = 0;
    memset(&placement, 0, sizeof(placement));
    RoboCheckEqInt(LayoutAbsolutePlacementBuild(position, &block, width, height,
                                                          &placement, NULL, NULL),
                      RoboOk);
    return placement;
}

static void TestAbsolutePlacementOrigin(void)
{
    LayoutPosition position;
    LayoutAbsolutePlacement placement;
    LayoutPoint origin;
    LayoutSize marginBox;

    /* The near axis: the insets are a displacement from the containing block's origin, so
     * the box's own size is irrelevant. */
    LayoutAbsolutePositionReset(&position, RoboCssPositioningAbsolute);
    LayoutAbsolutePositionSet(&position, RoboCssPositioningAbsolute, RoboCssBoxSideLeft,
                                 LayoutInsetResolved(12));
    LayoutAbsolutePositionSet(&position, RoboCssPositioningAbsolute, RoboCssBoxSideTop,
                                 LayoutInsetResolved(9));
    placement = AbsolutePlacementFor(&position, MakeGfxSize(400, 300), MakeGfxPoint(5, 7),
                                             NULL, NULL);
    marginBox = MakeGfxSize(100, 40);
    RoboCheckEqInt(LayoutAbsolutePlacementOrigin(&placement, marginBox, &origin, NULL),
                      RoboOk);
    RoboCheckEqInt(origin.x, 5 + 12);
    RoboCheckEqInt(origin.y, 7 + 9);

    /* The far axis, and this is the assertion the phase exists for: `right: 8px` means the
     * box's **right edge** is 8px in from the containing block's right edge, so the
     * coordinate needs the box's own width. */
    LayoutAbsolutePositionReset(&position, RoboCssPositioningFixed);
    LayoutAbsolutePositionSet(&position, RoboCssPositioningFixed, RoboCssBoxSideRight,
                                 LayoutInsetResolved(8));
    LayoutAbsolutePositionSet(&position, RoboCssPositioningFixed, RoboCssBoxSideBottom,
                                 LayoutInsetResolved(20));
    placement = AbsolutePlacementFor(&position, MakeGfxSize(400, 300), MakeGfxPoint(0, 0),
                                             NULL, NULL);
    marginBox = MakeGfxSize(100, 40);
    RoboCheckEqInt(LayoutAbsolutePlacementOrigin(&placement, marginBox, &origin, NULL),
                      RoboOk);
    RoboCheckEqInt(origin.x, 400 - 8 - 100);
    RoboCheckEqInt(origin.y, 300 - 20 - 40);

    /* A margin box wider than the space between the containing block's origin and its far
     * edge is a *negative* coordinate, and it is answered rather than clamped to zero: an
     * author who writes `right: 500px` on a 400px containing block has asked for a box that
     * starts before it, and the clamp is for the geometry's range and nothing else. */
    placement = AbsolutePlacementFor(&position, MakeGfxSize(400, 300), MakeGfxPoint(0, 0),
                                             NULL, NULL);
    marginBox = MakeGfxSize(500, 40);
    RoboCheckEqInt(LayoutAbsolutePlacementOrigin(&placement, marginBox, &origin, NULL),
                      RoboOk);
    RoboCheckEqInt(origin.x, 400 - 8 - 500);
}

static void TestAbsolutePlacementOriginClamps(void)
{
    LayoutPosition position;
    LayoutAbsolutePlacement placement;
    LayoutPoint origin;
    LayoutSize marginBox;

    /* The geometry's maximum plus anything is still the maximum: an extreme inset and an
     * extreme containing block must saturate rather than wrap into a negative coordinate
     * (10.15's rule, applied to the one sum this phase adds). */
    LayoutAbsolutePositionReset(&position, RoboCssPositioningAbsolute);
    LayoutAbsolutePositionSet(&position, RoboCssPositioningAbsolute, RoboCssBoxSideLeft,
                                 LayoutInsetResolved(2147483647));
    placement = AbsolutePlacementFor(&position, MakeGfxSize(2147483647, 10),
                                             MakeGfxPoint(2147483647, 0), NULL, NULL);
    marginBox = MakeGfxSize(0, 0);
    RoboCheckEqInt(LayoutAbsolutePlacementOrigin(&placement, marginBox, &origin, NULL),
                      RoboOk);
    RoboCheck(origin.x > 0);
    RoboCheckEqInt(origin.x, 2147483647);
}

static void TestAbsoluteContentHeight(void)
{
    LayoutPosition position;
    LayoutAbsolutePlacement placement;
    int32_t height = -1;

    /* Two insets size the **margin box**, so the content box is what is left after the
     * margins, the borders and the paddings the pass has already resolved. */
    LayoutAbsolutePositionReset(&position, RoboCssPositioningAbsolute);
    LayoutAbsolutePositionSet(&position, RoboCssPositioningAbsolute, RoboCssBoxSideTop,
                                 LayoutInsetResolved(10));
    LayoutAbsolutePositionSet(&position, RoboCssPositioningAbsolute, RoboCssBoxSideBottom,
                                 LayoutInsetResolved(30));
    placement = AbsolutePlacementFor(&position, MakeGfxSize(400, 500), MakeGfxPoint(0, 0),
                                             NULL, NULL);
    RoboCheckEqInt(placement.sizedByInsetsY, 1);
    RoboCheckEqInt(placement.marginBoxHeight, 500 - 10 - 30);
    RoboCheckEqInt(LayoutAbsoluteContentHeight(&placement, 45, &height, NULL), RoboOk);
    RoboCheckEqInt(height, 500 - 10 - 30 - 45);

    /* More insets than there is room: floored at zero rather than negative, for the reason
     * a stretched width is. */
    RoboCheckEqInt(LayoutAbsoluteContentHeight(&placement, 2000, &height, NULL), RoboOk);
    RoboCheckEqInt(height, 0);

    /* An axis whose insets did not size it has no height to convert, and asking is a
     * *miss* rather than a zero: a caller that treated the zero as an answer would give the
     * box a height nothing declared. */
    LayoutAbsolutePositionReset(&position, RoboCssPositioningAbsolute);
    LayoutAbsolutePositionSet(&position, RoboCssPositioningAbsolute, RoboCssBoxSideTop,
                                 LayoutInsetResolved(10));
    placement = AbsolutePlacementFor(&position, MakeGfxSize(400, 500), MakeGfxPoint(0, 0),
                                             NULL, NULL);
    RoboCheckEqInt(placement.sizedByInsetsY, 0);
    RoboCheckEqInt(placement.marginBoxHeight, -1);
    height = 77;
    RoboCheckEqInt(LayoutAbsoluteContentHeight(&placement, 45, &height, NULL),
                      RoboNotFound);
    RoboCheckEqInt(height, 77);
}

static void TestAbsolutePlacementBuild(void)
{
    LayoutPosition position;
    LayoutAbsolutePlacement placement;
    LayoutAbsoluteCounters counters;
    LayoutAbsoluteContainingBlock block;

    /* A dock, as the shell actually writes one: `left: 0; right: 0; bottom: 0` with a
     * declared height. It stretches horizontally, anchors on the bottom, and the record
     * says all three. */
    LayoutAbsolutePositionReset(&position, RoboCssPositioningAbsolute);
    LayoutAbsolutePositionSet(&position, RoboCssPositioningAbsolute, RoboCssBoxSideLeft,
                                 LayoutInsetResolved(0));
    LayoutAbsolutePositionSet(&position, RoboCssPositioningAbsolute, RoboCssBoxSideRight,
                                 LayoutInsetResolved(0));
    LayoutAbsolutePositionSet(&position, RoboCssPositioningAbsolute,
                                 RoboCssBoxSideBottom, LayoutInsetResolved(0));
    LayoutAbsoluteContainingBlockInit(&block);
    block.origin = MakeGfxPoint(0, 0);
    block.size = MakeGfxSize(800, 600);
    block.initial = 1;
    {
        LayoutDimension width;
        LayoutDimension height;

        LayoutDimensionInit(&width);
        LayoutDimensionInit(&height);
        height.kind = RoboLayoutDimensionDefinite;
        height.length = 56;
        LayoutAbsoluteCountersInit(&counters);
        RoboCheckEqInt(LayoutAbsolutePlacementBuild(&position, &block, &width, &height,
                                                               &placement, &counters, NULL),
                          RoboOk);
    }
    RoboCheckEqInt(placement.sizedByInsetsX, 1);
    RoboCheckEqInt(placement.anchorFarX, 0);
    RoboCheckEqInt(placement.anchorFarY, 1);
    RoboCheckEqInt(placement.sizedByInsetsY, 0);
    RoboCheckEqInt(placement.availableWidth, 800);
    RoboCheckEqInt(placement.containingBlock.initial, 1);
    RoboCheckEqInt(placement.mode, RoboCssPositioningAbsolute);
    RoboCheckEqInt(LayoutAbsolutePlacementIsValid(&placement), 1);
    RoboCheckEqInt((int)counters.boxesOutOfFlow, 0);
    RoboCheckEqInt((int)counters.sidesUsed, 3);
    RoboCheckEqInt((int)counters.axesFromInsets, 1);
    RoboCheckEqInt((int)counters.axesAnchoredFar, 1);

    /* A round trip: the record is a value, so two builds of one document are equal. */
    {
        LayoutAbsolutePlacement other;
        LayoutPosition again = position;
        LayoutDimension width;
        LayoutDimension height;

        LayoutDimensionInit(&width);
        LayoutDimensionInit(&height);
        height.kind = RoboLayoutDimensionDefinite;
        height.length = 56;
        RoboCheckEqInt(LayoutAbsolutePlacementBuild(&again, &block, &width, &height,
                                                               &other, NULL, NULL),
                          RoboOk);
        RoboCheckEqInt(LayoutAbsolutePlacementEqual(&placement, &other), 1);
    }

    /* And the refusal a caller must not be able to talk their way past: an **in-flow**
     * record is not a placement, because nothing would be placed against anything. */
    {
        LayoutPosition staticPosition;

        LayoutAbsolutePositionReset(&staticPosition, RoboCssPositioningStatic);
        RoboCheckEqInt(LayoutAbsolutePlacementBuild(&staticPosition, &block, NULL, NULL,
                                                               &placement, NULL, NULL),
                          RoboInvalidArgument);
    }
}

static void TestAbsoluteClassification(void)
{
    LayoutPosition position;
    LayoutPosition cleared;

    memset(&cleared, 0, sizeof(cleared));
    /* A NULL record and a cleared one are both `static` -- CSS's initial value -- and a
     * static box is in flow, so neither is out of flow and neither is viewport-anchored. */
    RoboCheckEqInt(LayoutAbsoluteIsOutOfFlow(NULL), 0);
    RoboCheckEqInt(LayoutAbsoluteIsOutOfFlow(&cleared), 0);
    RoboCheckEqInt(LayoutAbsoluteIsViewportAnchored(NULL), 0);

    LayoutAbsolutePositionReset(&position, RoboCssPositioningStatic);
    RoboCheckEqInt(LayoutAbsoluteIsOutOfFlow(&position), 0);
    RoboCheckEqInt(LayoutAbsoluteIsViewportAnchored(&position), 0);

    LayoutAbsolutePositionReset(&position, RoboCssPositioningRelative);
    RoboCheckEqInt(LayoutAbsoluteIsOutOfFlow(&position), 0);
    RoboCheckEqInt(LayoutAbsoluteIsViewportAnchored(&position), 0);

    LayoutAbsolutePositionReset(&position, RoboCssPositioningSticky);
    /* `sticky` is in flow here, exactly as 10.12 left it: there is no scrollport yet. */
    RoboCheckEqInt(LayoutAbsoluteIsOutOfFlow(&position), 0);
    RoboCheckEqInt(LayoutAbsoluteIsViewportAnchored(&position), 0);

    LayoutAbsolutePositionReset(&position, RoboCssPositioningAbsolute);
    RoboCheckEqInt(LayoutAbsoluteIsOutOfFlow(&position), 1);
    RoboCheckEqInt(LayoutAbsoluteIsViewportAnchored(&position), 0);

    LayoutAbsolutePositionReset(&position, RoboCssPositioningFixed);
    RoboCheckEqInt(LayoutAbsoluteIsOutOfFlow(&position), 1);
    RoboCheckEqInt(LayoutAbsoluteIsViewportAnchored(&position), 1);
}

/* ------------------------------------------------------------------ */
/* The pass                                                            */
/* ------------------------------------------------------------------ */

static void TestAbsoluteBlockAgainstPositionedWrapper(void)
{
    LayoutAbsoluteFixture fixture;
    LayoutAbsoluteRule rules[5];
    LayoutStats stats;
    LayoutNodeId section;
    LayoutNodeId card;
    LayoutNodeId after;
    LayoutPoint origin;
    LayoutSize size;

    LayoutAbsoluteRuleInit(&rules[0], "html", RoboCssDisplayBlock);
    LayoutAbsoluteRuleInit(&rules[1], "body", RoboCssDisplayBlock);
    /* The containing block: a `relative` wrapper with a border and a padding, because the
     * containing block is its **padding box** -- the offset that distinguishes the two is
     * the assertion, and a wrapper with no insets could not make it. */
    LayoutAbsoluteRuleInit(&rules[2], "section", RoboCssDisplayBlock);
    LayoutAbsoluteRulePosition(&rules[2], RoboCssPositioningRelative);
    LayoutAbsoluteRulePx(&rules[2], "height", 200.0);
    LayoutAbsoluteRuleInit(&rules[3], "div", RoboCssDisplayBlock);
    LayoutAbsoluteRulePosition(&rules[3], RoboCssPositioningAbsolute);
    LayoutAbsoluteRulePx(&rules[3], "left", 4.0);
    LayoutAbsoluteRulePx(&rules[3], "top", 3.0);
    LayoutAbsoluteRulePx(&rules[3], "width", 50.0);
    LayoutAbsoluteRulePx(&rules[3], "height", 30.0);
    LayoutAbsoluteRuleInit(&rules[4], "aside", RoboCssDisplayBlock);
    LayoutAbsoluteRulePx(&rules[4], "height", 15.0);

    LayoutAbsoluteBegin(&fixture,
                            "<html><body><section><div></div><aside></aside></section></body></html>",
                            rules, 5u);
    LayoutAbsolutePass(&fixture, LAYOUT_ABSOLUTE_VIEWPORT_WIDTH,
                              LAYOUT_ABSOLUTE_VIEWPORT_HEIGHT, &stats);

    section = LayoutAbsoluteBoxFor(&fixture, "section");
    card = LayoutAbsoluteBoxFor(&fixture, "div");
    after = LayoutAbsoluteBoxFor(&fixture, "aside");

    /* The absolute box is placed at the wrapper's padding box origin plus its insets. The
     * wrapper has no padding in this case, so the padding box *is* the border box and the
     * number is unambiguous -- the padding-edge rule has its own case below, where the two
     * differ. */
    origin = LayoutAbsoluteOriginOf(&fixture, card);
    RoboCheckEqInt(LayoutAbsoluteOriginOf(&fixture, section).x, 0);
    RoboCheckEqInt(origin.x, 4);
    RoboCheckEqInt(origin.y, 3);
    size = LayoutAbsoluteSizeOf(&fixture, card);
    RoboCheckEqInt(size.width, 50);
    RoboCheckEqInt(size.height, 30);

    /* And the witness: the sibling **after** the absolute box starts where flow would have
     * put it had the absolute box never existed, and the wrapper's height is its declared
     * one rather than the absolute box's extent. A build that left the box's space reserved
     * fails here and passes every single-box assertion above. */
    RoboCheckEqInt(LayoutAbsoluteOriginOf(&fixture, after).y, 0);
    RoboCheckEqInt(LayoutAbsoluteSizeOf(&fixture, section).height, 200);
    RoboCheckEqInt(LayoutAbsoluteSizeOf(&fixture, section).width,
                      LAYOUT_ABSOLUTE_VIEWPORT_WIDTH);

    RoboCheckEqInt((int)stats.block.absolute.boxesOutOfFlow, 1);
    RoboCheckEqInt((int)stats.block.absolute.viewportAnchored, 0);
    /* Two inset declarations reached the layer and were used. */
    RoboCheckEqInt((int)stats.block.absolute.sidesUsed, 2);
    /* The absolute box took no room, so nothing in this document was displaced. */
    RoboCheckEqInt((int)stats.block.boxesShifted, 0);
    RoboCheckEqInt((int)stats.block.offsetsDeclined, 0);

    LayoutAbsoluteEnd(&fixture);
}

static void TestAbsoluteBlockAgainstViewport(void)
{
    LayoutAbsoluteFixture fixture;
    LayoutAbsoluteRule rules[4];
    LayoutStats stats;
    LayoutNodeId box;
    LayoutPoint origin;

    /* No ancestor is positioned, so the containing block is the **initial** one: the
     * viewport, whatever the box's static ancestors are doing. The `section` has a border
     * and a padding to make the difference visible. */
    LayoutAbsoluteRuleInit(&rules[0], "html", RoboCssDisplayBlock);
    LayoutAbsoluteRuleInit(&rules[1], "body", RoboCssDisplayBlock);
    LayoutAbsoluteRuleInit(&rules[2], "section", RoboCssDisplayBlock);
    LayoutAbsoluteRulePx(&rules[2], "border-left-width", 9.0);
    LayoutAbsoluteRulePx(&rules[2], "padding-left", 40.0);
    LayoutAbsoluteRulePx(&rules[2], "padding-top", 25.0);
    LayoutAbsoluteRuleInit(&rules[3], "div", RoboCssDisplayBlock);
    LayoutAbsoluteRulePosition(&rules[3], RoboCssPositioningAbsolute);
    LayoutAbsoluteRulePx(&rules[3], "left", 12.0);
    LayoutAbsoluteRulePx(&rules[3], "top", 9.0);
    LayoutAbsoluteRulePx(&rules[3], "width", 30.0);
    LayoutAbsoluteRulePx(&rules[3], "height", 20.0);

    LayoutAbsoluteBegin(&fixture, "<html><body><section><div></div></section></body></html>",
                            rules, 4u);
    LayoutAbsolutePass(&fixture, LAYOUT_ABSOLUTE_VIEWPORT_WIDTH,
                              LAYOUT_ABSOLUTE_VIEWPORT_HEIGHT, &stats);

    box = LayoutAbsoluteBoxFor(&fixture, "div");
    origin = LayoutAbsoluteOriginOf(&fixture, box);
    RoboCheckEqInt(origin.x, 12);
    RoboCheckEqInt(origin.y, 9);
    RoboCheckEqInt((int)stats.block.absolute.viewportAnchored, 1);
    RoboCheckEqInt((int)stats.block.absolute.boxesOutOfFlow, 1);

    LayoutAbsoluteEnd(&fixture);
}

static void TestAbsoluteStretchFillsContainingBlock(void)
{
    LayoutAbsoluteFixture fixture;
    LayoutAbsoluteRule rules[4];
    LayoutStats stats;
    LayoutNodeId wall;
    LayoutPoint origin;
    LayoutSize size;

    /* The wallpaper, exactly as the desktop shell writes it. It fills the whole initial
     * containing block because **both** insets are present, which is the phase's size rule
     * reached through a real document rather than through the policy alone. */
    LayoutAbsoluteRuleInit(&rules[0], "html", RoboCssDisplayBlock);
    LayoutAbsoluteRuleInit(&rules[1], "body", RoboCssDisplayBlock);
    LayoutAbsoluteRuleInit(&rules[2], "section", RoboCssDisplayBlock);
    LayoutAbsoluteRuleInit(&rules[3], "div", RoboCssDisplayBlock);
    LayoutAbsoluteRulePosition(&rules[3], RoboCssPositioningAbsolute);
    LayoutAbsoluteRulePx(&rules[3], "left", 0.0);
    LayoutAbsoluteRulePx(&rules[3], "right", 0.0);
    LayoutAbsoluteRulePx(&rules[3], "top", 0.0);
    LayoutAbsoluteRulePx(&rules[3], "bottom", 0.0);

    LayoutAbsoluteBegin(&fixture, "<html><body><section><div></div></section></body></html>",
                            rules, 4u);
    LayoutAbsolutePass(&fixture, 640, 480, &stats);

    wall = LayoutAbsoluteBoxFor(&fixture, "div");
    origin = LayoutAbsoluteOriginOf(&fixture, wall);
    size = LayoutAbsoluteSizeOf(&fixture, wall);
    RoboCheckEqInt(origin.x, 0);
    RoboCheckEqInt(origin.y, 0);
    RoboCheckEqInt(size.width, 640);
    RoboCheckEqInt(size.height, 480);
    RoboCheckEqInt((int)stats.block.absolute.axesFromInsets, 2);

    LayoutAbsoluteEnd(&fixture);
}

/*
 * A block-level child of a positioned box is laid out **inside it**, and this case is the one that
 * caught the defect Phase 14.0 shipped without it.
 *
 * The classification pass used to apply its transparency rule — "a block container with a block-level
 * child participates in whatever it participates in" — to *positioned* boxes as well, and the rule is
 * true only for a box **in flow**: an out-of-flow box's children are laid out inside it, after its
 * containing block resolves, so their participating context has to be one this box owns. A build
 * without the distinction refused every such document, with `a block-level box participates in a
 * context that is not a block formatting context`, which is exactly what the desktop shell's own
 * compiled page turned out to be: a fixed window layer holding absolutely positioned slots holding
 * block-level windows.
 *
 * The assertion is deliberately the geometry rather than "the pass returned Ok": the reader is
 * `LayoutAbsoluteOriginOf`/`…SizeOf`, both of which require the box's result to be **computed**, so a
 * build that classified the child correctly and then never placed it fails here too.
 */
static void TestAbsoluteBlockChildOfPositioned(void)
{
    LayoutAbsoluteFixture fixture;
    LayoutAbsoluteRule rules[4];
    LayoutStats stats;
    LayoutNodeId section;
    LayoutPoint origin;
    LayoutSize size;

    LayoutAbsoluteRuleInit(&rules[0], "html", RoboCssDisplayBlock);
    LayoutAbsoluteRuleInit(&rules[1], "body", RoboCssDisplayBlock);
    /* The slot: out of flow, sized by its own declarations. */
    LayoutAbsoluteRuleInit(&rules[2], "div", RoboCssDisplayBlock);
    LayoutAbsoluteRulePosition(&rules[2], RoboCssPositioningAbsolute);
    LayoutAbsoluteRulePx(&rules[2], "left", 20.0);
    LayoutAbsoluteRulePx(&rules[2], "top", 10.0);
    LayoutAbsoluteRulePx(&rules[2], "width", 300.0);
    LayoutAbsoluteRulePx(&rules[2], "height", 200.0);
    /* And the block-level child that has to be laid out inside it. */
    LayoutAbsoluteRuleInit(&rules[3], "section", RoboCssDisplayBlock);
    LayoutAbsoluteRulePx(&rules[3], "height", 40.0);

    LayoutAbsoluteBegin(&fixture,
                            "<html><body><div><section></section></div></body></html>",
                            rules, 4u);
    LayoutAbsolutePass(&fixture, 640, 480, &stats);

    section = LayoutAbsoluteBoxFor(&fixture, "section");
    origin = LayoutAbsoluteOriginOf(&fixture, section);
    size = LayoutAbsoluteSizeOf(&fixture, section);
    /*
     * The child's width is the slot's content width and its height is its own declaration — and its
     * origin is **(0, 0)**, which is the pass's two coordinate conventions meeting in one case rather
     * than an oversight: an **in-flow** box's stored position is relative to its parent's content edge,
     * while an **out-of-flow** box's is the answer its containing block's pass computed (which is why
     * the slot reads 20,10 and the child inside it reads 0,0). Paint adds the parent's accumulated
     * origin, so the child ends up where the slot is; a build that stored page coordinates here would
     * place the child twice as far into the document as its parent.
     */
    RoboCheckEqInt(origin.x, 0);
    RoboCheckEqInt(origin.y, 0);
    RoboCheckEqInt(size.width, 300);
    RoboCheckEqInt(size.height, 40);
    RoboCheckEqInt(LayoutAbsoluteOriginOf(&fixture,
                                                       LayoutAbsoluteBoxFor(&fixture, "div")).x, 20);
    /* And the slot itself is still out of flow, which is a different fact about the same document. */
    RoboCheckEqInt((int)stats.block.absolute.boxesOutOfFlow, 1);

    LayoutAbsoluteEnd(&fixture);
}

static void TestAbsoluteFixedIgnoresPositionedAncestor(void)
{
    LayoutAbsoluteFixture fixture;
    LayoutAbsoluteRule rules[5];
    LayoutStats stats;
    LayoutNodeId box;
    LayoutPoint origin;
    LayoutSize size;

    /* The dock: `fixed`, `right: 0; bottom: 0`, a declared size. Its `section` ancestor is
     * positioned and offset -- and must be ignored, which is the one thing that separates
     * `fixed` from `absolute`. */
    LayoutAbsoluteRuleInit(&rules[0], "html", RoboCssDisplayBlock);
    LayoutAbsoluteRuleInit(&rules[1], "body", RoboCssDisplayBlock);
    LayoutAbsoluteRuleInit(&rules[2], "section", RoboCssDisplayBlock);
    LayoutAbsoluteRulePosition(&rules[2], RoboCssPositioningRelative);
    LayoutAbsoluteRulePx(&rules[2], "left", 100.0);
    LayoutAbsoluteRulePx(&rules[2], "top", 50.0);
    LayoutAbsoluteRulePx(&rules[2], "height", 300.0);
    LayoutAbsoluteRuleInit(&rules[3], "nav", RoboCssDisplayBlock);
    LayoutAbsoluteRulePosition(&rules[3], RoboCssPositioningFixed);
    LayoutAbsoluteRulePx(&rules[3], "right", 0.0);
    LayoutAbsoluteRulePx(&rules[3], "bottom", 0.0);
    LayoutAbsoluteRulePx(&rules[3], "width", 420.0);
    LayoutAbsoluteRulePx(&rules[3], "height", 56.0);
    LayoutAbsoluteRuleInit(&rules[4], "aside", RoboCssDisplayBlock);

    LayoutAbsoluteBegin(&fixture, "<html><body><section><nav></nav></section></body></html>",
                            rules, 5u);
    LayoutAbsolutePass(&fixture, LAYOUT_ABSOLUTE_VIEWPORT_WIDTH,
                              LAYOUT_ABSOLUTE_VIEWPORT_HEIGHT, &stats);

    box = LayoutAbsoluteBoxFor(&fixture, "nav");
    origin = LayoutAbsoluteOriginOf(&fixture, box);
    size = LayoutAbsoluteSizeOf(&fixture, box);
    RoboCheckEqInt(size.width, 420);
    RoboCheckEqInt(size.height, 56);
    RoboCheckEqInt(origin.x, LAYOUT_ABSOLUTE_VIEWPORT_WIDTH - 420);
    RoboCheckEqInt(origin.y, LAYOUT_ABSOLUTE_VIEWPORT_HEIGHT - 56);
    RoboCheckEqInt((int)stats.block.absolute.viewportAnchored, 1);
    RoboCheckEqInt((int)stats.block.absolute.axesAnchoredFar, 2);

    /* And a second pass at a different viewport moves it, because the initial containing
     * block is an *input to the pass* rather than a fact about the document: a build that
     * cached the viewport on the tree would answer the first rectangle twice. */
    LayoutAbsolutePass(&fixture, 1024, 768, &stats);
    origin = LayoutAbsoluteOriginOf(&fixture, box);
    RoboCheckEqInt(origin.x, 1024 - 420);
    RoboCheckEqInt(origin.y, 768 - 56);

    LayoutAbsoluteEnd(&fixture);
}

static void TestAbsoluteStaticWrapperIsTransparent(void)
{
    LayoutAbsoluteFixture fixture;
    LayoutAbsoluteRule rules[5];
    LayoutStats stats;
    LayoutNodeId card;
    LayoutPoint origin;

    /* A `static` wrapper between the positioned ancestor and the absolute box does **not**
     * become the containing block: the walk descends through it, which is the containment
     * rule stated as a case. The wrapper has a padding of its own, so a build that stopped
     * at the nearest *ancestor* would answer a different coordinate. */
    LayoutAbsoluteRuleInit(&rules[0], "html", RoboCssDisplayBlock);
    LayoutAbsoluteRuleInit(&rules[1], "body", RoboCssDisplayBlock);
    LayoutAbsoluteRuleInit(&rules[2], "section", RoboCssDisplayBlock);
    LayoutAbsoluteRulePosition(&rules[2], RoboCssPositioningRelative);
    LayoutAbsoluteRulePx(&rules[2], "height", 100.0);
    LayoutAbsoluteRuleInit(&rules[3], "article", RoboCssDisplayBlock);
    LayoutAbsoluteRulePx(&rules[3], "padding-left", 60.0);
    LayoutAbsoluteRulePx(&rules[3], "border-left-width", 17.0);
    LayoutAbsoluteRulePx(&rules[3], "height", 40.0);
    LayoutAbsoluteRuleInit(&rules[4], "div", RoboCssDisplayBlock);
    LayoutAbsoluteRulePosition(&rules[4], RoboCssPositioningAbsolute);
    LayoutAbsoluteRulePx(&rules[4], "left", 5.0);
    LayoutAbsoluteRulePx(&rules[4], "top", 6.0);
    LayoutAbsoluteRulePx(&rules[4], "width", 25.0);
    LayoutAbsoluteRulePx(&rules[4], "height", 15.0);

    LayoutAbsoluteBegin(&fixture,
                            "<html><body><section><article><div></div></article></section></body></html>",
                            rules, 5u);
    LayoutAbsolutePass(&fixture, LAYOUT_ABSOLUTE_VIEWPORT_WIDTH,
                              LAYOUT_ABSOLUTE_VIEWPORT_HEIGHT, &stats);

    card = LayoutAbsoluteBoxFor(&fixture, "div");
    origin = LayoutAbsoluteOriginOf(&fixture, card);
    RoboCheckEqInt(origin.x, 5);
    RoboCheckEqInt(origin.y, 6);
    RoboCheckEqInt((int)stats.block.absolute.boxesOutOfFlow, 1);
    RoboCheckEqInt((int)stats.block.absolute.viewportAnchored, 0);

    LayoutAbsoluteEnd(&fixture);
}

/**
 * The three declaration kinds a real document can write and the policy must classify:
 * a **negative** length (usable -- `left: -8px` is legal CSS), the keyword `auto` (a
 * declaration that says nothing, and *not* the same statement as nobody having written
 * one) and a **percentage** (declared, and not a value this build can use).
 *
 * End to end, because the policy suite above proves the classification of a fabricated
 * record and this proves that a *computed style* reaches it -- which is the seam a build
 * can get wrong while every policy assertion still passes.
 */
/**
 * The containing block is the ancestor's **padding** box, and this case is the one that says
 * so with a number no other rule produces.
 *
 * The wrapper's padding is 20 across and 10 down, so its padding box begins *before* its
 * content edge -- and every coordinate this pass writes is relative to the content edge (the
 * pass hands a child a cursor starting at the content origin, which is why the in-flow
 * sibling below is placed at zero). The absolute box is therefore at `4 - 20` and `3 - 10`:
 * a build that placed it at the *content* edge answers (4, 3), a build that used the border
 * box agrees with it by accident, and neither could tell this document from one whose author
 * had written `left: -16px` -- which is exactly why the padding is in the fixture and the
 * rule is asserted rather than assumed.
 */
static void TestAbsoluteContainingBlockIsPaddingEdge(void)
{
    LayoutAbsoluteFixture fixture;
    LayoutAbsoluteRule rules[5];
    LayoutStats stats;
    LayoutNodeId card;
    LayoutNodeId sibling;
    LayoutPoint origin;

    LayoutAbsoluteRuleInit(&rules[0], "html", RoboCssDisplayBlock);
    LayoutAbsoluteRuleInit(&rules[1], "body", RoboCssDisplayBlock);
    LayoutAbsoluteRuleInit(&rules[2], "section", RoboCssDisplayBlock);
    LayoutAbsoluteRulePosition(&rules[2], RoboCssPositioningRelative);
    LayoutAbsoluteRulePx(&rules[2], "padding-left", 20.0);
    LayoutAbsoluteRulePx(&rules[2], "padding-top", 10.0);
    LayoutAbsoluteRulePx(&rules[2], "height", 80.0);
    LayoutAbsoluteRuleInit(&rules[3], "div", RoboCssDisplayBlock);
    LayoutAbsoluteRulePosition(&rules[3], RoboCssPositioningAbsolute);
    LayoutAbsoluteRulePx(&rules[3], "left", 4.0);
    LayoutAbsoluteRulePx(&rules[3], "top", 3.0);
    LayoutAbsoluteRulePx(&rules[3], "width", 30.0);
    LayoutAbsoluteRulePx(&rules[3], "height", 20.0);
    LayoutAbsoluteRuleInit(&rules[4], "aside", RoboCssDisplayBlock);
    LayoutAbsoluteRulePx(&rules[4], "height", 5.0);

    LayoutAbsoluteBegin(&fixture,
                            "<html><body><section><div></div><aside></aside></section></body></html>",
                            rules, 5u);
    LayoutAbsolutePass(&fixture, LAYOUT_ABSOLUTE_VIEWPORT_WIDTH,
                              LAYOUT_ABSOLUTE_VIEWPORT_HEIGHT, &stats);

    card = LayoutAbsoluteBoxFor(&fixture, "div");
    sibling = LayoutAbsoluteBoxFor(&fixture, "aside");
    origin = LayoutAbsoluteOriginOf(&fixture, card);
    RoboCheckEqInt(origin.x, 4 - 20);
    RoboCheckEqInt(origin.y, 3 - 10);
    /* The in-flow sibling, in the same space: at the content origin, because that is where a
     * padded container's flow starts and where zero means zero. */
    RoboCheckEqInt(LayoutAbsoluteOriginOf(&fixture, sibling).x, 0);
    RoboCheckEqInt(LayoutAbsoluteOriginOf(&fixture, sibling).y, 0);
    RoboCheckEqInt((int)stats.block.absolute.boxesOutOfFlow, 1);
    RoboCheckEqInt((int)stats.block.absolute.axesFromInsets, 0);

    LayoutAbsoluteEnd(&fixture);
}

static void TestAbsoluteInsetKindsThroughStyles(void)
{
    LayoutAbsoluteFixture fixture;
    LayoutAbsoluteRule rules[6];
    LayoutStats stats;
    LayoutNodeId box;
    LayoutNodeId other;
    LayoutPoint origin;

    LayoutAbsoluteRuleInit(&rules[0], "html", RoboCssDisplayBlock);
    LayoutAbsoluteRuleInit(&rules[1], "body", RoboCssDisplayBlock);
    LayoutAbsoluteRuleInit(&rules[2], "section", RoboCssDisplayBlock);
    LayoutAbsoluteRulePosition(&rules[2], RoboCssPositioningRelative);
    LayoutAbsoluteRulePx(&rules[2], "height", 300.0);
    LayoutAbsoluteRuleInit(&rules[3], "div", RoboCssDisplayBlock);
    LayoutAbsoluteRulePosition(&rules[3], RoboCssPositioningAbsolute);
    LayoutAbsoluteRuleNegative(&rules[3], "left", 8.0);
    LayoutAbsoluteRuleKeyword(&rules[3], "right", "auto");
    LayoutAbsoluteRulePercentage(&rules[3], "top", 25.0);
    LayoutAbsoluteRulePx(&rules[3], "width", 60.0);
    LayoutAbsoluteRulePx(&rules[3], "height", 40.0);
    /* And the same unusability arrived at from the other direction: a px length flagged
     * unresolved, which is what `top: var(--inset)` computes to. */
    LayoutAbsoluteRuleInit(&rules[4], "aside", RoboCssDisplayBlock);
    LayoutAbsoluteRulePosition(&rules[4], RoboCssPositioningAbsolute);
    LayoutAbsoluteRuleUnresolved(&rules[4], "top", 12.0);
    LayoutAbsoluteRulePx(&rules[4], "left", 3.0);
    LayoutAbsoluteRulePx(&rules[4], "width", 20.0);
    LayoutAbsoluteRulePx(&rules[4], "height", 10.0);
    LayoutAbsoluteRuleInit(&rules[5], "main", RoboCssDisplayBlock);

    LayoutAbsoluteBegin(&fixture,
                            "<html><body><section><div></div><aside></aside></section></body></html>",
                            rules, 5u);
    LayoutAbsolutePass(&fixture, LAYOUT_ABSOLUTE_VIEWPORT_WIDTH,
                              LAYOUT_ABSOLUTE_VIEWPORT_HEIGHT, &stats);

    box = LayoutAbsoluteBoxFor(&fixture, "div");
    origin = LayoutAbsoluteOriginOf(&fixture, box);
    /* A negative length moves the box *out* of its containing block, which is what makes
     * it a technique rather than a bug: -8 from the wrapper's padding-box origin. */
    RoboCheckEqInt(origin.x, -8);
    /* The percentage is declared and unusable, so its used value is zero -- and the box
     * sits at the containing block's origin rather than being dropped. */
    RoboCheckEqInt(origin.y, 0);
    /* `right: auto` beside a `left` is the one case where the far side is *counted* and
     * not used: `sidesAuto` separates it from a document that wrote no `right` at all. */
    RoboCheckEqInt((int)stats.block.absolute.sidesUsed, 2);
    RoboCheckEqInt((int)stats.block.absolute.sidesAuto, 1);
    RoboCheckEqInt((int)stats.block.absolute.sidesUnresolved, 2);
    RoboCheckEqInt((int)stats.block.absolute.axesAnchoredFar, 0);

    other = LayoutAbsoluteBoxFor(&fixture, "aside");
    origin = LayoutAbsoluteOriginOf(&fixture, other);
    RoboCheckEqInt(origin.x, 3);
    RoboCheckEqInt(origin.y, 0);

    LayoutAbsoluteEnd(&fixture);
}

static void TestAbsoluteNestedContainingBlocks(void)
{
    LayoutAbsoluteFixture fixture;
    LayoutAbsoluteRule rules[5];
    LayoutStats stats;
    LayoutNodeId inner;

    /* Two positioned ancestors and one absolute box: the **nearest** one wins. The outer
     * box's insets are large enough that a build which walked to the root would answer a
     * visibly different coordinate. */
    LayoutAbsoluteRuleInit(&rules[0], "html", RoboCssDisplayBlock);
    LayoutAbsoluteRuleInit(&rules[1], "body", RoboCssDisplayBlock);
    LayoutAbsoluteRuleInit(&rules[2], "section", RoboCssDisplayBlock);
    LayoutAbsoluteRulePosition(&rules[2], RoboCssPositioningRelative);
    LayoutAbsoluteRulePx(&rules[2], "left", 200.0);
    LayoutAbsoluteRulePx(&rules[2], "top", 150.0);
    LayoutAbsoluteRulePx(&rules[2], "height", 400.0);
    LayoutAbsoluteRuleInit(&rules[3], "article", RoboCssDisplayBlock);
    LayoutAbsoluteRulePosition(&rules[3], RoboCssPositioningRelative);
    LayoutAbsoluteRulePx(&rules[3], "left", 30.0);
    LayoutAbsoluteRulePx(&rules[3], "top", 25.0);
    LayoutAbsoluteRulePx(&rules[3], "height", 120.0);
    LayoutAbsoluteRuleInit(&rules[4], "div", RoboCssDisplayBlock);
    LayoutAbsoluteRulePosition(&rules[4], RoboCssPositioningAbsolute);
    LayoutAbsoluteRulePx(&rules[4], "left", 7.0);
    LayoutAbsoluteRulePx(&rules[4], "top", 8.0);
    LayoutAbsoluteRulePx(&rules[4], "width", 11.0);
    LayoutAbsoluteRulePx(&rules[4], "height", 9.0);

    LayoutAbsoluteBegin(&fixture,
                            "<html><body><section><article><div></div></article></section></body></html>",
                            rules, 5u);
    LayoutAbsolutePass(&fixture, LAYOUT_ABSOLUTE_VIEWPORT_WIDTH,
                              LAYOUT_ABSOLUTE_VIEWPORT_HEIGHT, &stats);

    inner = LayoutAbsoluteBoxFor(&fixture, "div");
    /* The **nearest** positioned ancestor is the `article`, not the `section`: the box is 7
     * and 8 inside the article's padding box, and the article's own 30/25 offset is *not* in
     * this number -- a stored position here is relative to the containing block it is inside,
     * and the caller accumulates down the tree (10.11's convention, and the reason the
     * numbers are small). A build that walked to the root would answer 237 and 183. */
    RoboCheckEqInt(LayoutAbsoluteOriginOf(&fixture, inner).x, 7);
    RoboCheckEqInt(LayoutAbsoluteOriginOf(&fixture, inner).y, 8);
    RoboCheckEqInt((int)stats.block.absolute.boxesOutOfFlow, 1);

    LayoutAbsoluteEnd(&fixture);
}

static void TestAbsoluteCountersAndDeterminism(void)
{
    LayoutAbsoluteFixture fixture;
    LayoutAbsoluteRule rules[4];
    LayoutStats first;
    LayoutStats second;

    /* The counters a determinism case is for: a second pass over an unchanged tree must
     * produce the same record, and a build that accumulated into a record the caller owned
     * -- or that counted a box twice -- shows up here and nowhere else. */
    LayoutAbsoluteRuleInit(&rules[0], "html", RoboCssDisplayBlock);
    LayoutAbsoluteRuleInit(&rules[1], "body", RoboCssDisplayBlock);
    LayoutAbsoluteRuleInit(&rules[2], "section", RoboCssDisplayBlock);
    LayoutAbsoluteRulePosition(&rules[2], RoboCssPositioningAbsolute);
    LayoutAbsoluteRulePx(&rules[2], "left", 0.0);
    LayoutAbsoluteRulePx(&rules[2], "top", 0.0);
    LayoutAbsoluteRulePx(&rules[2], "width", 300.0);
    LayoutAbsoluteRulePx(&rules[2], "height", 200.0);
    LayoutAbsoluteRuleInit(&rules[3], "div", RoboCssDisplayBlock);
    LayoutAbsoluteRulePosition(&rules[3], RoboCssPositioningAbsolute);
    LayoutAbsoluteRulePx(&rules[3], "right", 10.0);
    LayoutAbsoluteRulePx(&rules[3], "top", 10.0);
    LayoutAbsoluteRulePx(&rules[3], "width", 40.0);
    LayoutAbsoluteRulePx(&rules[3], "height", 40.0);

    LayoutAbsoluteBegin(&fixture, "<html><body><section><div></div></section></body></html>",
                            rules, 4u);
    LayoutAbsolutePass(&fixture, LAYOUT_ABSOLUTE_VIEWPORT_WIDTH,
                              LAYOUT_ABSOLUTE_VIEWPORT_HEIGHT, &first);
    LayoutAbsolutePass(&fixture, LAYOUT_ABSOLUTE_VIEWPORT_WIDTH,
                              LAYOUT_ABSOLUTE_VIEWPORT_HEIGHT, &second);

    /* Two out-of-flow boxes: the section, placed against the viewport, and the div, placed
     * against the section's padding box -- which is a containing block *because* the
     * section is positioned, and is the nesting this phase's walk exists for. */
    RoboCheckEqInt((int)first.block.absolute.boxesOutOfFlow, 2);
    RoboCheckEqInt((int)first.block.absolute.viewportAnchored, 1);
    RoboCheckEqInt((int)first.block.absolute.axesAnchoredFar, 1);
    RoboCheckEqInt((int)first.block.absolute.sidesUsed, 4);
    RoboCheckEqInt((int)first.block.absolute.sidesAuto, 0);
    RoboCheckEqInt((int)first.block.absolute.sidesUnresolved, 0);
    RoboCheckEqInt(LayoutBlockStatsEqual(&first.block, &second.block), 1);

    /*
     * And the **negative half**, which is the assertion the positive one cannot make: the
     * absolute record is part of the block record's own identity, so two records that placed
     * different out-of-flow boxes are not equal. This is the shape 19.6 pinned its `table`
     * record with, and it is here because the comparison was the one nested record
     * `LayoutBlockStatsEqual` left out -- a build that dropped it again agrees with every
     * number this case asserts above and fails only here.
     */
    {
        LayoutBlockStats left;
        LayoutBlockStats right;

        LayoutBlockStatsInit(&left);
        right = left;
        RoboCheckEqInt(LayoutBlockStatsEqual(&left, &right), 1);
        RoboCheckEqInt(LayoutAbsoluteCountersEqual(&left.absolute, &right.absolute), 1);
        /* One counter apart is a difference, and the seven are read field by field: a
         * `viewportAnchored` count is what a pass that reached the wrong containing block
         * changes, and a `boxesOutOfFlow` count is what one that walked the wrong subtree
         * changes. */
        right.absolute.viewportAnchored = 1u;
        RoboCheckEqInt(LayoutAbsoluteCountersEqual(&left.absolute, &right.absolute), 0);
        RoboCheckEqInt(LayoutBlockStatsEqual(&left, &right), 0);
        right = left;
        right.absolute.boxesOutOfFlow = 1u;
        RoboCheckEqInt(LayoutBlockStatsEqual(&left, &right), 0);
        right = left;
        right.absolute.axesAnchoredFar = 1u;
        RoboCheckEqInt(LayoutBlockStatsEqual(&left, &right), 0);
        /* And the record is still the zero it was: none of the copies above was a reference. */
        RoboCheckEqInt(left.absolute.boxesOutOfFlow, 0u);
        RoboCheckEqInt(left.absolute.viewportAnchored, 0u);
        RoboCheckEqInt(left.absolute.axesAnchoredFar, 0u);
    }

    /* And the allocator balance: this phase adds no arena, no cache and no per-pass state,
     * so a pass allocates nothing that outlives it -- and one that leaked a placement
     * record would be visible as a growing count over a hundred passes. */
    {
        size_t settled = LayoutLiveBlocks();
        int pass = 0;

        for (pass = 0; pass < 8; pass++) {
            LayoutAbsolutePass(&fixture, LAYOUT_ABSOLUTE_VIEWPORT_WIDTH,
                                      LAYOUT_ABSOLUTE_VIEWPORT_HEIGHT, NULL);
        }
        RoboCheckEqInt(LayoutLiveBlocks(), settled);
    }

    LayoutAbsoluteEnd(&fixture);
}

/* ------------------------------------------------------------------ */
/* Registration                                                        */
/* ------------------------------------------------------------------ */

void TestRegisterLayoutAbsolute(void);

void TestRegisterLayoutAbsolute(void)
{
    TestRegister("layout.absolute.axis_vocabulary", TestAbsoluteAxisVocabulary);
    TestRegister("layout.absolute.side_record", TestAbsoluteSideVocabulary);
    TestRegister("layout.absolute.containing_block_record",
                       TestAbsoluteContainingBlockRecord);
    TestRegister("layout.absolute.policy_near_wins", TestAbsolutePolicyNearWins);
    TestRegister("layout.absolute.policy_far_anchors", TestAbsolutePolicyFarAnchors);
    TestRegister("layout.absolute.policy_size_from_insets",
                       TestAbsolutePolicySizeFromInsets);
    TestRegister("layout.absolute.policy_unresolved", TestAbsolutePolicyUnresolvedAndRefusals);
    TestRegister("layout.absolute.axis_use_record", TestAbsoluteAxisUseRecord);
    TestRegister("layout.absolute.placement_origin", TestAbsolutePlacementOrigin);
    TestRegister("layout.absolute.placement_origin_clamps", TestAbsolutePlacementOriginClamps);
    TestRegister("layout.absolute.content_height", TestAbsoluteContentHeight);
    TestRegister("layout.absolute.placement_build", TestAbsolutePlacementBuild);
    TestRegister("layout.absolute.classification", TestAbsoluteClassification);
    TestRegister("layout.absolute.block_positioned_wrapper",
                       TestAbsoluteBlockAgainstPositionedWrapper);
    TestRegister("layout.absolute.block_viewport", TestAbsoluteBlockAgainstViewport);
    TestRegister("layout.absolute.block_stretch", TestAbsoluteStretchFillsContainingBlock);
    TestRegister("layout.absolute.block_child_of_positioned",
                       TestAbsoluteBlockChildOfPositioned);
    TestRegister("layout.absolute.block_fixed", TestAbsoluteFixedIgnoresPositionedAncestor);
    TestRegister("layout.absolute.static_wrapper_transparent",
                       TestAbsoluteStaticWrapperIsTransparent);
    TestRegister("layout.absolute.containing_block_padding_edge",
                       TestAbsoluteContainingBlockIsPaddingEdge);
    TestRegister("layout.absolute.inset_kinds", TestAbsoluteInsetKindsThroughStyles);
    TestRegister("layout.absolute.nested_containing_blocks",
                       TestAbsoluteNestedContainingBlocks);
    TestRegister("layout.absolute.counters_determinism",
                       TestAbsoluteCountersAndDeterminism);
}
