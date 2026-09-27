/*
 * RoboLayout positioning suite (Phase 10.12) — layout.positioning.*
 *
 * Five things are under test, and the boundaries between them are the phase:
 *
 *   the **vocabulary** -- four inset kinds over four sides, and the one respect in
 *   which this vocabulary differs from the three before it: an inset is **signed**,
 *   like a margin and unlike a padding or a border, because `left: -10px` is a legal
 *   declaration and a real technique;
 *
 *   the **seam**: what a computed style declared, which is the one place this module
 *   reads `position` and its four offsets, and the one place the shared
 *   classification is asked with the margin's `allowNegative` rather than the
 *   dimension's;
 *
 *   the **policy**: the precedence rule -- left over right, top over bottom, an
 *   `auto` side saying nothing -- driven directly over fabricated position records,
 *   so a wrong precedence fails at the arithmetic rather than three boxes later in a
 *   coordinate;
 *
 *   the two **coordinate spaces**: the apply that turns a flow position into a final
 *   one, and the object record that turns a final position back into a flow one;
 *
 *   the **pass**: a displacement reaching geometry, which since this phase means a
 *   `relative` box is drawn where its offset puts it while its `size`, its baseline
 *   and -- the assertion a build could pass every single-box test without honouring
 *   -- its **flow contribution** are untouched. The following sibling is the witness:
 *   `layout.positioning.flow_preserved` fails a build that moved the box *and* the
 *   cursor, which is the most plausible way to get this phase wrong.
 *
 * The scripts here are *computed styles*, built through 9.8's makers exactly as the
 * sizing, margin, padding and border suites build them, because a fabricated struct
 * would prove the pass reads a struct rather than a computed CSS value. The mode is
 * 9.13's own POSITION value and the insets are the box model's EDGE_OFFSET shape,
 * which is why `top: 20%` and `margin-top: 20%` arrive here normalised the same way.
 *
 * Elements are resolved **by tag** (`LayoutPositionBoxFor`) rather than by arena
 * index, which is the lesson 10.8's suite wrote down: a fixture's indices are a fact
 * about the parser, and the tags here are distinct anyway so that one scripted style
 * per element is enough.
 */

#include "RoboLayoutTestSupport.h"

#include <string.h>

/* ------------------------------------------------------------------ */
/* A fabricated style that can declare anything                        */
/* ------------------------------------------------------------------ */

/** One declaration to put on a fabricated style. */
typedef enum layoutPositionDeclKind {
    LAYOUT_POSITION_DECL_PX = 0,
    /** A px length that is **negative**: legal CSS for an inset, and the case the
     *  signedness exists for. */
    LAYOUT_POSITION_DECL_NEGATIVE_PX = 1,
    /** A px length flagged unresolved, which is what a `var()` arrives as. */
    LAYOUT_POSITION_DECL_UNRESOLVED_PX = 2,
    LAYOUT_POSITION_DECL_PERCENTAGE = 3,
    LAYOUT_POSITION_DECL_EM = 4,
    /** The keyword `auto`, or any other identifier the grammar allows. */
    LAYOUT_POSITION_DECL_KEYWORD = 5,
    /** A typed `border-*-style` (the border suite's path): a keyword the *style*
     *  layer folds into a kinded value, which is what the border reader consults. */
    LAYOUT_POSITION_DECL_BORDER_STYLE = 7,
    /** A `CssComputedValue` of kind POSITION -- 9.13's typed vocabulary. */
    LAYOUT_POSITION_DECL_POSITION = 6
} LayoutPositionDeclKind;

typedef struct layoutPositionDecl {
    const char *name;
    LayoutPositionDeclKind kind;
    double number;
    const char *keyword;
    CssPositioning position;
    CssBorderStyle borderStyle;
} LayoutPositionDecl;

#define LAYOUT_POSITION_MAX_DECLARATIONS 20u
#define LAYOUT_POSITION_MAX_RULES 8u

typedef struct layoutPositionRule {
    const char *tag;
    CssDisplay display;
    LayoutPositionDecl declarations[LAYOUT_POSITION_MAX_DECLARATIONS];
    size_t declarationsCount;
} LayoutPositionRule;

static void LayoutPositionRuleInit(LayoutPositionRule *rule,
                                      const char *tag,
                                      CssDisplay display)
{
    memset(rule, 0, sizeof(*rule));
    rule->tag = tag;
    rule->display = display;
}

static LayoutPositionDecl *LayoutPositionRuleAdd(LayoutPositionRule *rule,
                                                        const char *name,
                                                        LayoutPositionDeclKind kind)
{
    LayoutPositionDecl *declaration;

    RoboCheckEqInt(rule->declarationsCount < LAYOUT_POSITION_MAX_DECLARATIONS, 1);
    declaration = &rule->declarations[rule->declarationsCount];
    memset(declaration, 0, sizeof(*declaration));
    declaration->name = name;
    declaration->kind = kind;
    declaration->position = RoboCssPositioningStatic;
    rule->declarationsCount++;
    return declaration;
}

static void LayoutPositionRulePx(LayoutPositionRule *rule, const char *name, double px)
{
    LayoutPositionRuleAdd(rule, name, LAYOUT_POSITION_DECL_PX)->number = px;
}

/** A *negative* px length, which CSS allows for an inset and this layer honours. */
static void LayoutPositionRuleNegative(LayoutPositionRule *rule,
                                          const char *name,
                                          double px)
{
    LayoutPositionRuleAdd(rule, name, LAYOUT_POSITION_DECL_NEGATIVE_PX)->number = -px;
}

static void LayoutPositionRuleUnresolved(LayoutPositionRule *rule,
                                            const char *name,
                                            double px)
{
    LayoutPositionRuleAdd(rule, name, LAYOUT_POSITION_DECL_UNRESOLVED_PX)->number = px;
}

static void LayoutPositionRulePercentage(LayoutPositionRule *rule,
                                            const char *name,
                                            double percent)
{
    LayoutPositionRuleAdd(rule, name, LAYOUT_POSITION_DECL_PERCENTAGE)->number = percent;
}

static void LayoutPositionRuleEm(LayoutPositionRule *rule, const char *name, double em)
{
    LayoutPositionRuleAdd(rule, name, LAYOUT_POSITION_DECL_EM)->number = em;
}

static void LayoutPositionRuleKeyword(LayoutPositionRule *rule,
                                         const char *name,
                                         const char *keyword)
{
    LayoutPositionRuleAdd(rule, name, LAYOUT_POSITION_DECL_KEYWORD)->keyword = keyword;
}

static void LayoutPositionRuleBorderStyle(LayoutPositionRule *rule,
                                              const char *name,
                                              CssBorderStyle style)
{
    LayoutPositionRuleAdd(rule, name, LAYOUT_POSITION_DECL_BORDER_STYLE)->borderStyle =
        style;
}

static void LayoutPositionRulePosition(LayoutPositionRule *rule,
                                          CssPositioning mode)
{
    LayoutPositionRuleAdd(rule, "position", LAYOUT_POSITION_DECL_POSITION)->position = mode;
}

typedef struct layoutPositionStyleSlot {
    char tag[16];
    CssComputedStyle *style; /* owned */
    size_t answers;
} LayoutPositionStyleSlot;

typedef struct layoutPositionSource {
    LayoutPositionStyleSlot rules[LAYOUT_POSITION_MAX_RULES];
    size_t count;
} LayoutPositionSource;

/**
 * Build the style @p rule describes: a real DISPLAY value plus whatever the rule
 * declares, all through 9.8's own makers and container.
 *
 * `display` is set even when the rule declares nothing else, because 9.12's initial
 * value is `inline` and a fixture that wanted a block box would otherwise be asserting
 * a document made of inline boxes.
 */
static CssComputedStyle *LayoutPositionStyleBuild(const LayoutPositionRule *rule)
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
        const LayoutPositionDecl *declaration = &rule->declarations[index];

        value = (CssComputedValue)RoboCssCleared;
        specified = (CssValue)RoboCssCleared;
        switch (declaration->kind) {
        case LAYOUT_POSITION_DECL_POSITION:
            RoboCheckEqInt(CssComputedValueMakePosition(&value, declaration->position,
                                                                    NULL),
                              RoboOk);
            break;
        case LAYOUT_POSITION_DECL_PERCENTAGE:
            RoboCheckEqInt(CssComputedValueMakePercentage(&value, declaration->number,
                                                                     0, NULL),
                              RoboOk);
            break;
        case LAYOUT_POSITION_DECL_KEYWORD:
            RoboCheckEqInt(CssComputedValueMakeKeyword(
                                  &value, CssSliceFromText(declaration->keyword), NULL),
                              RoboOk);
            break;
        case LAYOUT_POSITION_DECL_BORDER_STYLE:
            RoboCheckEqInt(CssComputedValueMakeBorderStyle(
                                  &value, declaration->borderStyle, NULL),
                              RoboOk);
            break;
        case LAYOUT_POSITION_DECL_UNRESOLVED_PX:
            RoboCheckEqInt(CssComputedValueMakeLength(&value, declaration->number,
                                                                 RoboCssUnitPx, 0, NULL),
                              RoboOk);
            /* The flag the computed layer sets for a `var()` or a `calc()`: carried
             * intact and not a value *yet*, whatever its kind says. */
            value.unresolved = 1;
            break;
        case LAYOUT_POSITION_DECL_EM:
            RoboCheckEqInt(CssComputedValueMakeLength(&value, declaration->number,
                                                                 RoboCssUnitEm, 0, NULL),
                              RoboOk);
            break;
        case LAYOUT_POSITION_DECL_NEGATIVE_PX:
        case LAYOUT_POSITION_DECL_PX:
        default:
            /* Written the same way for both, and that is the point: the *style* holds
             * what it was handed -- a negative inset is a computed value like any other
             * and the layer honours it -- while a negative *border width* is refused.
             * The grammar decides, not the maker. */
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

/** Add @p rule to @p source, and answer the style it built. */
static CssComputedStyle *LayoutPositionStyleAdd(LayoutPositionSource *source,
                                                            const LayoutPositionRule *rule)
{
    size_t slot;
    CssComputedStyle *style;

    LayoutFixture();
    RoboCheckEqInt(source->count < LAYOUT_POSITION_MAX_RULES, 1);
    slot = source->count;
    style = LayoutPositionStyleBuild(rule);
    strncpy(source->rules[slot].tag, rule->tag, sizeof(source->rules[slot].tag) - 1u);
    source->rules[slot].tag[sizeof(source->rules[slot].tag) - 1u] = '\0';
    source->rules[slot].style = style;
    source->rules[slot].answers = 0u;
    source->count++;
    return style;
}

/**
 * Rebuild the style @p source answers @p tag with, **in the same slot**.
 *
 * Replacing rather than appending, and that is not tidiness: the source answers the
 * *first* row whose tag matches, so an appended second `div` row would be shadowed by
 * the one it was meant to replace and the case would assert the old declaration while
 * appearing to have changed it.
 */
static void LayoutPositionStyleReplace(LayoutPositionSource *source,
                                          const char *tag,
                                          const LayoutPositionRule *rule)
{
    size_t index;

    LayoutFixture();
    for (index = 0u; index < source->count; index++) {
        if (strcmp(source->rules[index].tag, tag) == 0) {
            CssComputedStyleDestroy(source->rules[index].style);
            source->rules[index].style = LayoutPositionStyleBuild(rule);
            return;
        }
    }
    RoboCheckMsg(0, "the tag to replace is not in this source");
}

static const CssComputedStyle *LayoutPositionStyleLookup(
    void *userdata,
    const HtmlNodeHandle *element)
{
    LayoutPositionSource *source = (LayoutPositionSource *)userdata;
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

typedef struct layoutPositionFixture {
    LayoutDocument doc;
    LayoutPositionSource source;
    LayoutStyleSource styles;
    LayoutTree *tree;
} LayoutPositionFixture;

static void LayoutPositionBegin(LayoutPositionFixture *fixture,
                                  const char *html,
                                  const LayoutPositionRule *rules,
                                  size_t ruleCount)
{
    LayoutBuildOptions options;
    size_t index;

    memset(fixture, 0, sizeof(*fixture));
    LayoutDocumentBuild(&fixture->doc, html);
    for (index = 0u; index < ruleCount; index++) {
        (void)LayoutPositionStyleAdd(&fixture->source, &rules[index]);
    }
    LayoutStyleSourceInit(&fixture->styles);
    fixture->styles.userdata = &fixture->source;
    fixture->styles.ComputedStyleFor = LayoutPositionStyleLookup;
    RoboCheckEqInt(LayoutTreeCreate(&fixture->tree, NULL), RoboOk);
    LayoutBuildOptionsInit(&options);
    options.styles = &fixture->styles;
    RoboCheckEqInt(LayoutTreeBuildFromDocument(fixture->tree, fixture->doc.id,
                                                          &options, NULL, NULL),
                      RoboOk);
}

static void LayoutPositionEnd(LayoutPositionFixture *fixture)
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

#define LAYOUT_POSITION_VIEWPORT_WIDTH 800
#define LAYOUT_POSITION_VIEWPORT_HEIGHT 600

/** One pass at @p viewportWidth. */
static void LayoutPositionPass(LayoutPositionFixture *fixture,
                                 int32_t viewportWidth,
                                 LayoutStats *outStats)
{
    LayoutContext *context = NULL;
    LayoutSize viewport;
    LayoutStats stats;

    viewport.width = viewportWidth;
    viewport.height = LAYOUT_POSITION_VIEWPORT_HEIGHT;
    RoboCheckEqInt(LayoutContextCreate(&context, NULL), RoboOk);
    RoboCheckEqInt(LayoutContextSetViewport(context, viewport, NULL), RoboOk);
    RoboCheckEqInt(LayoutContextRun(context, fixture->tree, NULL), RoboOk);
    LayoutContextStats(context, &stats);
    LayoutContextDestroy(context);
    if (outStats) {
        *outStats = stats;
    }
}

/** The layout box of the first @p tag in the fixture's document. */
static LayoutNodeId LayoutPositionBoxFor(LayoutPositionFixture *fixture,
                                                     const char *tag)
{
    HtmlNodeHandle element = LayoutDocumentFind(&fixture->doc, tag);
    LayoutNodeId id;

    RoboCheckEqInt(HtmlNodeHandleIsWellformed(&element), 1);
    id = LayoutObjectIdForDomNode(fixture->tree, &element);
    RoboCheck(id != RoboLayoutNodeIdNone);
    return id;
}

/** The origin a placed box was given -- its **final** position since 10.12. */
static LayoutPoint LayoutPositionOriginOf(LayoutPositionFixture *fixture,
                                                     LayoutNodeId id)
{
    LayoutResult result;
    LayoutPoint position;

    RoboCheckEqInt(LayoutObjectResult(fixture->tree, id, &result), RoboOk);
    RoboCheckEqInt((int)result.state, (int)RoboLayoutResultComputed);
    RoboCheckEqInt(LayoutResultPosition(&result, &position), RoboOk);
    return position;
}

/** The size a placed box was given -- its **border** box, untouched by any offset. */
static LayoutSize LayoutPositionSizeOf(LayoutPositionFixture *fixture,
                                                  LayoutNodeId id)
{
    LayoutResult result;
    LayoutSize size;

    RoboCheckEqInt(LayoutObjectResult(fixture->tree, id, &result), RoboOk);
    RoboCheckEqInt((int)result.state, (int)RoboLayoutResultComputed);
    RoboCheckEqInt(LayoutResultSize(&result, &size), RoboOk);
    return size;
}

/** The used displacement a placed box reports. */
static LayoutPositionOffset LayoutPositionOffsetOf(
    LayoutPositionFixture *fixture,
    LayoutNodeId id)
{
    LayoutPositionOffset offset;

    RoboCheckEqInt(LayoutObjectPosition(fixture->tree, id, &offset, NULL), RoboOk);
    return offset;
}

/**
 * Rebuild the layout tree against the styles @p fixture was built with.
 *
 * The step a changed declaration needs and the reason it is not optional: the tree
 * **borrows** one computed style per element for as long as the bridge lives, so a
 * replacement that left the old tree standing would leave it holding a destroyed style.
 * The rule is 10.9's, 10.10's and 10.11's, and it is what makes "the second answer is a
 * fact about the second style" checkable rather than a hope.
 */
static void LayoutPositionRebuild(LayoutPositionFixture *fixture)
{
    LayoutBuildOptions options;

    LayoutBuildOptionsInit(&options);
    options.styles = &fixture->styles;
    RoboCheckEqInt(LayoutTreeBuildFromDocument(fixture->tree, fixture->doc.id,
                                                          &options, NULL, NULL),
                      RoboOk);
}

/* ------------------------------------------------------------------ */
/* A hand-built position, for the policy                               */
/* ------------------------------------------------------------------ */

/** A declared position: a mode and four insets, each built from its own kind. */
static LayoutPosition LayoutPositionDeclared(CssPositioning mode,
                                                       LayoutInset top,
                                                       LayoutInset right,
                                                       LayoutInset bottom,
                                                       LayoutInset left)
{
    LayoutPosition position;

    position.mode = mode;
    position.insets.top = top;
    position.insets.right = right;
    position.insets.bottom = bottom;
    position.insets.left = left;
    return position;
}

/** The declared record plus its resolve, so a policy case reads as one call. */
static Result LayoutPositionResolved(CssPositioning mode,
                                              LayoutInset top,
                                              LayoutInset right,
                                              LayoutInset bottom,
                                              LayoutInset left,
                                              LayoutPositionOffset *offset)
{
    LayoutPosition declared = LayoutPositionDeclared(mode, top, right, bottom, left);

    return LayoutPositionResolve(&declared, offset, NULL);
}

/* ------------------------------------------------------------------ */
/* The vocabulary                                                      */
/* ------------------------------------------------------------------ */

static void TestPositioningKindVocabulary(void)
{
    size_t index;

    LayoutFixture();

    RoboCheckEqInt((int)RoboLayoutInsetKindCount, 4);
    /*
     * The zero of the vocabulary is `ABSENT`, and that is a decision rather than a
     * numbering accident: a cleared record then holds the side's *initial state*, so a
     * forgotten initialisation is not a claim that somebody wrote an inset. The same
     * decision the margin, padding and border vocabularies made.
     */
    RoboCheckEqInt((int)RoboLayoutInsetAbsent, 0);
    RoboCheckEqInt((int)RoboLayoutInsetResolved, 1);
    RoboCheckEqInt((int)RoboLayoutInsetAuto, 2);
    RoboCheckEqInt((int)RoboLayoutInsetUnresolved, 3);

    for (index = 0u; index < RoboLayoutInsetKindCount; index++) {
        LayoutInsetKind kind = (LayoutInsetKind)index;

        RoboCheckEqInt(LayoutInsetKindIsKnown(kind), 1);
        RoboCheck(LayoutInsetKindName(kind) != NULL);
    }
    RoboCheckEqInt(LayoutInsetKindIsKnown((LayoutInsetKind)4), 0);
    RoboCheckEqInt(LayoutInsetKindIsKnown((LayoutInsetKind)77), 0);
    RoboCheckStrEq(LayoutInsetKindName(RoboLayoutInsetAbsent), "absent");
    RoboCheckStrEq(LayoutInsetKindName(RoboLayoutInsetResolved), "resolved");
    RoboCheckStrEq(LayoutInsetKindName(RoboLayoutInsetAuto), "auto");
    RoboCheckStrEq(LayoutInsetKindName(RoboLayoutInsetUnresolved), "unresolved");
    RoboCheckStrEq(LayoutInsetKindName((LayoutInsetKind)4), "unknown");
    RoboCheck(LayoutInsetKindName((LayoutInsetKind)77) != NULL);

    /* And the domain this phase added, spelled and counted: 24 since 19.1 appended its
     * own, with `position` the nineteenth -- the first domain in this module about *where*
     * rather than about *how big*, and no longer the last since 10.13, 10.14 and 19.1 followed it. */
    RoboCheckEqInt((int)RoboLayoutDomainCount, 24);
    RoboCheckStrEq(LayoutDiagnosticDomainName(RoboLayoutDomainPosition),
                      "position");
    RoboCheckEqInt((int)RoboLayoutDomainOverflow,
                      (int)RoboLayoutDomainPosition + 1);
}

static void TestPositioningInsetRecord(void)
{
    LayoutInset zero = LayoutInsetZero();
    LayoutInset absent = LayoutInsetAbsent();
    LayoutInset resolved = LayoutInsetResolved(12);
    LayoutInset negative = LayoutInsetResolved(-7);
    LayoutInset autoInset = LayoutInsetAuto();
    LayoutInset unresolved = LayoutInsetUnresolved();
    LayoutInset badKind;
    GfxPixelX length = 0;

    LayoutFixture();

    /* `top: 0` is an inset an author asked for; `ABSENT` is a side nobody declared.
     * Both contribute nothing and they are different records -- the reason this
     * vocabulary has kinds at all, inherited from 10.9. */
    RoboCheckEqInt(LayoutInsetIsResolved(&zero), 1);
    RoboCheckEqInt(LayoutInsetIsAbsent(&zero), 0);
    RoboCheckEqInt(LayoutInsetIsValid(&zero), 1);
    RoboCheckEqInt(LayoutInsetLength(&zero, &length), RoboOk);
    RoboCheckEqInt(length, 0);
    RoboCheckEqInt(LayoutInsetEqual(&zero, &absent), 0);
    RoboCheckEqInt(LayoutInsetEqual(&zero, &zero), 1);

    RoboCheckEqInt(LayoutInsetIsAbsent(&absent), 1);
    RoboCheckEqInt(LayoutInsetIsValid(&absent), 1);
    /* A miss rather than a zero: reading the field of a side nobody declared would be
     * reading a number that means nothing. */
    length = 77;
    RoboCheckEqInt(LayoutInsetLength(&absent, &length), RoboNotFound);
    RoboCheckEqInt(length, 77);
    RoboCheckEqInt(LayoutInsetEqual(&absent, &absent), 1);

    {
        LayoutInset other = LayoutInsetResolved(13);

        RoboCheckEqInt(LayoutInsetIsResolved(&resolved), 1);
        RoboCheckEqInt(LayoutInsetIsValid(&resolved), 1);
        RoboCheckEqInt(LayoutInsetLength(&resolved, &length), RoboOk);
        RoboCheckEqInt(length, 12);
        RoboCheckEqInt(LayoutInsetEqual(&resolved, &resolved), 1);
        RoboCheckEqInt(LayoutInsetEqual(&resolved, &other), 0);
    }

    /*
     * And the check this vocabulary does **not** have that the border one does: a
     * negative inset is *valid*. `left: -7px` is legal CSS and the signedness is the
     * one respect in which this record differs from a padding or a border -- which is
     * why the predicate below asserts the opposite of `border_width_is_valid` does.
     */
    RoboCheckEqInt(LayoutInsetIsResolved(&negative), 1);
    RoboCheckEqInt(LayoutInsetIsValid(&negative), 1);
    RoboCheckEqInt(LayoutInsetLength(&negative, &length), RoboOk);
    RoboCheckEqInt(length, -7);

    RoboCheckEqInt(LayoutInsetIsAuto(&autoInset), 1);
    RoboCheckEqInt(LayoutInsetIsValid(&autoInset), 1);
    RoboCheckEqInt(LayoutInsetLength(&autoInset, &length), RoboNotFound);

    RoboCheckEqInt(LayoutInsetIsUnresolved(&unresolved), 1);
    RoboCheckEqInt(LayoutInsetIsValid(&unresolved), 1);
    RoboCheckEqInt(LayoutInsetLength(&unresolved, &length), RoboNotFound);

    badKind = LayoutInsetZero();
    badKind.kind = (LayoutInsetKind)99;
    RoboCheckEqInt(LayoutInsetIsValid(&badKind), 0);

    /* Two `ABSENT` sides are equal whatever their stale `length` fields hold -- the
     * same rule the kind exists for, restated here because the equality is what a
     * re-layout comparison leans on. */
    {
        LayoutInset stale = LayoutInsetAbsent();

        stale.length = 42;
        RoboCheckEqInt(LayoutInsetEqual(&absent, &stale), 1);
    }
}

static void TestPositioningPositionRecord(void)
{
    LayoutPosition cleared;
    LayoutPosition relative;
    LayoutPosition invalid;
    LayoutPosition same;
    LayoutPosition different;

    LayoutFixture();

    /*
     * A cleared record is CSS's initial state: `static` and four undeclared sides.
     * `STATIC` is 9.13's zero for exactly this reason, and the suite pins the mode
     * beside the vocabulary so the two zeros cannot drift apart.
     */
    LayoutPositionInit(&cleared);
    RoboCheckEqInt((int)cleared.mode, (int)RoboCssPositioningStatic);
    RoboCheckEqInt((int)RoboCssPositioningStatic, 0);
    RoboCheckEqInt(LayoutInsetIsAbsent(&cleared.insets.top), 1);
    RoboCheckEqInt(LayoutInsetIsAbsent(&cleared.insets.left), 1);
    RoboCheckEqInt(LayoutPositionIsValid(&cleared), 1);
    RoboCheckEqInt(LayoutPositionIsRelative(&cleared), 0);

    relative = LayoutPositionDeclared(RoboCssPositioningRelative,
                                        LayoutInsetResolved(5),
                                        LayoutInsetAbsent(),
                                        LayoutInsetAuto(),
                                        LayoutInsetResolved(10));
    RoboCheckEqInt(LayoutPositionIsRelative(&relative), 1);
    RoboCheckEqInt(LayoutPositionIsValid(&relative), 1);
    RoboCheckEqInt(LayoutPositionEqual(&relative, &relative), 1);

    same = relative;
    RoboCheckEqInt(LayoutPositionEqual(&relative, &same), 1);
    different = relative;
    different.mode = RoboCssPositioningStatic;
    RoboCheckEqInt(LayoutPositionEqual(&relative, &different), 0);
    different = relative;
    different.insets.top = LayoutInsetResolved(6);
    RoboCheckEqInt(LayoutPositionEqual(&relative, &different), 0);

    invalid = relative;
    invalid.mode = (CssPositioning)99;
    RoboCheckEqInt(LayoutPositionIsValid(&invalid), 0);

    /* And a NULL style reads as the cleared record -- a box with no style has made no
     * declarations, which is an answer rather than a failure. */
    {
        LayoutPosition fromNull = LayoutPositionOf(NULL);

        RoboCheckEqInt(LayoutPositionEqual(&fromNull, &cleared), 1);
    }
}

/* ------------------------------------------------------------------ */
/* The seam                                                            */
/* ------------------------------------------------------------------ */

static void TestPositioningSeam(void)
{
    LayoutPositionRule rule;
    LayoutPositionSource source;
    CssComputedStyle *style;
    LayoutPosition position;

    LayoutFixture();

    /*
     * The twelve-declaration read of the border suite is here a **five**-declaration
     * read: the mode and four insets. The cases below walk every kind the grammar
     * allows, one side at a time, so a wrong classification names its side.
     */
    LayoutPositionRuleInit(&rule, "div", RoboCssDisplayBlock);
    LayoutPositionRulePosition(&rule, RoboCssPositioningRelative);
    LayoutPositionRulePx(&rule, "top", 5.0);
    LayoutPositionRuleNegative(&rule, "right", 4.0);
    LayoutPositionRuleKeyword(&rule, "bottom", "auto");
    LayoutPositionRuleEm(&rule, "left", 2.0);
    source.count = 0u;
    style = LayoutPositionStyleAdd(&source, &rule);

    position = LayoutPositionOf(style);
    RoboCheckEqInt((int)position.mode, (int)RoboCssPositioningRelative);
    RoboCheckEqInt(LayoutInsetIsResolved(&position.insets.top), 1);
    RoboCheckEqInt(position.insets.top.length, 5);
    /* A negative inset is *honoured*, which is the one respect this seam differs from
     * the border seam beside it: the grammar allows one and refuses the other. */
    RoboCheckEqInt(LayoutInsetIsResolved(&position.insets.right), 1);
    RoboCheckEqInt(position.insets.right.length, -4);
    RoboCheckEqInt(LayoutInsetIsAuto(&position.insets.bottom), 1);
    /* An `em` is declared and unusable here, exactly as it is in every layer since
     * 10.8: no font metrics reached this read. */
    RoboCheckEqInt(LayoutInsetIsUnresolved(&position.insets.left), 1);

    /*
     * A percentage -- legal CSS for an inset, and `UNRESOLVED` here for the reason
     * 10.8 set and every layer since inherited.
     */
    LayoutPositionStyleReplace(&source, "div", &rule);
    LayoutPositionRuleInit(&rule, "div", RoboCssDisplayBlock);
    LayoutPositionRulePosition(&rule, RoboCssPositioningRelative);
    LayoutPositionRulePercentage(&rule, "left", 10.0);
    LayoutPositionStyleReplace(&source, "div", &rule);
    position = LayoutPositionOf(source.rules[0].style);
    RoboCheckEqInt(LayoutInsetIsUnresolved(&position.insets.left), 1);

    /*
     * A `var()`-flagged length: declared, kinded as a length, and not a value yet.
     * A fifth reason on the same branch, so a document whose offsets depend on a
     * custom property is visible as one rather than flush for no stated reason.
     */
    LayoutPositionRuleInit(&rule, "div", RoboCssDisplayBlock);
    LayoutPositionRulePosition(&rule, RoboCssPositioningRelative);
    LayoutPositionRuleUnresolved(&rule, "top", 9.0);
    LayoutPositionStyleReplace(&source, "div", &rule);
    position = LayoutPositionOf(source.rules[0].style);
    RoboCheckEqInt(LayoutInsetIsUnresolved(&position.insets.top), 1);

    /*
     * An absent `position` is `STATIC` -- CSS's initial value and 9.13's zero -- while
     * an absent inset is `ABSENT` and not a zero. The asymmetry is the two
     * vocabularies', not this reader's.
     */
    LayoutPositionRuleInit(&rule, "div", RoboCssDisplayBlock);
    LayoutPositionRulePx(&rule, "top", 3.0);
    LayoutPositionStyleReplace(&source, "div", &rule);
    position = LayoutPositionOf(source.rules[0].style);
    RoboCheckEqInt((int)position.mode, (int)RoboCssPositioningStatic);
    RoboCheckEqInt(LayoutInsetIsResolved(&position.insets.top), 1);

    /* An unknown `position` identifier answers STATIC through 9.13's own door -- the
     * same ambiguity every keyword vocabulary has, resolved by a kind check the
     * *maker* already made for a real computed value. */
    LayoutPositionRuleInit(&rule, "div", RoboCssDisplayBlock);
    LayoutPositionRuleKeyword(&rule, "position", "banana");
    LayoutPositionStyleReplace(&source, "div", &rule);
    position = LayoutPositionOf(source.rules[0].style);
    RoboCheckEqInt((int)position.mode, (int)RoboCssPositioningStatic);

    /* And the four sides are the box vocabulary's order, readable one at a time. */
    {
        LayoutInset side;

        position = LayoutPositionOf(NULL);
        RoboCheckEqInt(LayoutInsetsSide(&position.insets, RoboCssBoxSideLeft,
                                                  &side),
                          RoboOk);
        RoboCheckEqInt(LayoutInsetIsAbsent(&side), 1);
        RoboCheckEqInt(LayoutInsetsSide(&position.insets, (CssBoxSide)9,
                                                  &side),
                          RoboNotFound);
        RoboCheckEqInt(LayoutInsetsIsValid(&position.insets), 1);
    }

    /* Destroy what the source *currently* holds, not the pointer the first build
     * handed back: every replace above destroyed and rebuilt the row's style, so the
     * first pointer has been dangling since the second case and freeing it here is a
     * double free (the crash this case used to end with). */
    {
        size_t index;

        for (index = 0u; index < source.count; index++) {
            CssComputedStyleDestroy(source.rules[index].style);
            source.rules[index].style = NULL;
        }
        source.count = 0u;
    }
}

static void TestPositioningOffsetRecord(void)
{
    LayoutPositionOffset offset;
    LayoutPositionOffset same;
    LayoutPositionOffset bad;

    LayoutFixture();

    LayoutPositionOffsetInit(&offset);
    RoboCheckEqInt(offset.dx, 0);
    RoboCheckEqInt(offset.dy, 0);
    RoboCheckEqInt(LayoutPositionOffsetIsValid(&offset), 1);

    offset.dx = 10;
    offset.dy = -5;
    offset.insetsResolved = 2u;
    RoboCheckEqInt(LayoutPositionOffsetIsValid(&offset), 1);

    same = offset;
    RoboCheckEqInt(LayoutPositionOffsetEqual(&offset, &same), 1);
    same.dx = 11;
    RoboCheckEqInt(LayoutPositionOffsetEqual(&offset, &same), 0);
    same = offset;
    same.insetsDeclined = 1u;
    RoboCheckEqInt(LayoutPositionOffsetEqual(&offset, &same), 0);

    /* Five counted sides is a record no resolve produced: the counters' whole claim is
     * that they account for four declarations, so a hand-built record that cannot is
     * refused before anybody derives a position from it. */
    bad = offset;
    bad.insetsDeclined = 3u;
    RoboCheckEqInt(LayoutPositionOffsetIsValid(&bad), 0);
    bad.insetsDeclined = 2u;
    RoboCheckEqInt(LayoutPositionOffsetIsValid(&bad), 1);
}

/* ------------------------------------------------------------------ */
/* The policy                                                          */
/* ------------------------------------------------------------------ */

static void TestPositioningPolicyPrecedence(void)
{
    LayoutPositionOffset offset;

    LayoutFixture();

    /*
     * The phase's one rule, driven directly over hand-built records so a wrong
     * precedence fails at the arithmetic: **left wins over right, top wins over
     * bottom, and an `auto` side says nothing.**
     */

    /* One side each: the four basic displacements, with the sign flip the two "end"
     * sides carry -- `right` and `bottom` are distances *from* an edge. */
    RoboCheckEqInt(LayoutPositionResolved(                          RoboCssPositioningRelative,
                                                      LayoutInsetAbsent(),
                                                      LayoutInsetAbsent(),
                                                      LayoutInsetAbsent(),
                                                      LayoutInsetResolved(10),
                          &offset),
                      RoboOk);
    RoboCheckEqInt(offset.dx, 10);
    RoboCheckEqInt(offset.dy, 0);
    RoboCheckEqInt(offset.insetsResolved, 1u);
    RoboCheckEqInt(offset.insetsDeclined, 0u);

    RoboCheckEqInt(LayoutPositionResolved(                          RoboCssPositioningRelative,
                                                      LayoutInsetAbsent(),
                                                      LayoutInsetResolved(7),
                                                      LayoutInsetAbsent(),
                                                      LayoutInsetAbsent(),
                          &offset),
                      RoboOk);
    RoboCheckEqInt(offset.dx, -7);
    RoboCheckEqInt(offset.dy, 0);

    RoboCheckEqInt(LayoutPositionResolved(                          RoboCssPositioningRelative,
                                                      LayoutInsetResolved(4),
                                                      LayoutInsetAbsent(),
                                                      LayoutInsetAbsent(),
                                                      LayoutInsetAbsent(),
                          &offset),
                      RoboOk);
    RoboCheckEqInt(offset.dx, 0);
    RoboCheckEqInt(offset.dy, 4);

    RoboCheckEqInt(LayoutPositionResolved(                          RoboCssPositioningRelative,
                                                      LayoutInsetAbsent(),
                                                      LayoutInsetAbsent(),
                                                      LayoutInsetResolved(9),
                                                      LayoutInsetAbsent(),
                          &offset),
                      RoboOk);
    RoboCheckEqInt(offset.dx, 0);
    RoboCheckEqInt(offset.dy, -9);

    /* Opposing pairs: `left` and `top` win, *and both sides are counted* -- the
     * counters are about declarations that reached this layer, and precedence is a
     * separate fact about which of two the displacement uses. */
    RoboCheckEqInt(LayoutPositionResolved(                          RoboCssPositioningRelative,
                                                      LayoutInsetResolved(4),
                                                      LayoutInsetResolved(7),
                                                      LayoutInsetResolved(9),
                                                      LayoutInsetResolved(10),
                          &offset),
                      RoboOk);
    RoboCheckEqInt(offset.dx, 10);
    RoboCheckEqInt(offset.dy, 4);
    RoboCheckEqInt(offset.insetsResolved, 4u);

    /* An `auto` side says nothing and is counted as the declaration it is -- never
     * folded onto `ABSENT`, which is the distinction 10.9's vocabulary exists for. */
    RoboCheckEqInt(LayoutPositionResolved(                          RoboCssPositioningRelative,
                                                      LayoutInsetAuto(),
                                                      LayoutInsetResolved(7),
                                                      LayoutInsetAuto(),
                                                      LayoutInsetAuto(),
                          &offset),
                      RoboOk);
    RoboCheckEqInt(offset.dx, -7);
    RoboCheckEqInt(offset.dy, 0);
    RoboCheckEqInt(offset.insetsAuto, 3u);
    RoboCheckEqInt(offset.insetsResolved, 1u);

    /* An unresolved side is used as zero and counted: a document displaced with
     * `left: 10%` lays out flush here, and the count is what says so. */
    RoboCheckEqInt(LayoutPositionResolved(                          RoboCssPositioningRelative,
                                                      LayoutInsetAbsent(),
                                                      LayoutInsetAbsent(),
                                                      LayoutInsetAbsent(),
                                                      LayoutInsetUnresolved(),
                          &offset),
                      RoboOk);
    RoboCheckEqInt(offset.dx, 0);
    RoboCheckEqInt(offset.dy, 0);
    RoboCheckEqInt(offset.insetsUnresolved, 1u);

    /* A `relative` box that declared nothing at all resolves to the zero offset --
     * the common case, and a *value* rather than a refusal. */
    RoboCheckEqInt(LayoutPositionResolved(                          RoboCssPositioningRelative,
                                                      LayoutInsetAbsent(),
                                                      LayoutInsetAbsent(),
                                                      LayoutInsetAbsent(),
                                                      LayoutInsetAbsent(),
                          &offset),
                      RoboOk);
    RoboCheckEqInt(offset.dx, 0);
    RoboCheckEqInt(offset.dy, 0);
    RoboCheckEqInt(offset.insetsResolved, 0u);
    RoboCheckEqInt(LayoutPositionOffsetIsValid(&offset), 1);
}

static void TestPositioningPolicyStaticDeclines(void)
{
    LayoutPositionOffset offset;
    CssPositioning modes[4];
    size_t index;

    LayoutFixture();

    /*
     * Rule 1: any mode that is not `relative` produces **no displacement** and
     * *declines* every declared side. Two reasons reach the branch and the mode tells
     * them apart -- `static` is CSS's initial value and CSS applies no offset to one;
     * `absolute`, `fixed` and `sticky` are placed against something this build does
     * not compute yet. The counter is the same on both paths by design, and the mode
     * is what a reader of the record consults.
     */
    modes[0] = RoboCssPositioningStatic;
    modes[1] = RoboCssPositioningAbsolute;
    modes[2] = RoboCssPositioningFixed;
    modes[3] = RoboCssPositioningSticky;

    for (index = 0u; index < 4u; index++) {
        RoboCheckEqInt(LayoutPositionResolved(                              modes[index],
                                                          LayoutInsetResolved(4),
                                                          LayoutInsetResolved(7),
                                                          LayoutInsetAbsent(),
                                                          LayoutInsetResolved(10),
                              &offset),
                          RoboOk);
        RoboCheckEqInt(offset.dx, 0);
        RoboCheckEqInt(offset.dy, 0);
        RoboCheckEqInt(offset.insetsResolved, 0u);
        RoboCheckEqInt(offset.insetsDeclined, 3u);
        RoboCheckEqInt(LayoutPositionOffsetIsValid(&offset), 1);
    }

    /* And a non-relative box that declared nothing declines nothing -- the counter
     * counts *declarations*, not sides that exist. */
    RoboCheckEqInt(LayoutPositionResolved(                          RoboCssPositioningStatic,
                                                      LayoutInsetAbsent(),
                                                      LayoutInsetAbsent(),
                                                      LayoutInsetAbsent(),
                                                      LayoutInsetAbsent(),
                          &offset),
                      RoboOk);
    RoboCheckEqInt(offset.insetsDeclined, 0u);
}

static void TestPositioningApply(void)
{
    LayoutPoint flow;
    LayoutPoint finalPoint;
    LayoutPositionOffset offset;
    LayoutPositionOffset bad;

    LayoutFixture();

    /*
     * The one place the two coordinate spaces meet, driven directly: the flow position
     * plus the displacement, in 64 bits and clamped.
     */
    flow.x = 100;
    flow.y = 50;
    LayoutPositionOffsetInit(&offset);
    RoboCheckEqInt(LayoutPositionApply(flow, &offset, &finalPoint, NULL), RoboOk);
    RoboCheckEqInt(finalPoint.x, 100);
    RoboCheckEqInt(finalPoint.y, 50);

    offset.dx = 10;
    offset.dy = -5;
    RoboCheckEqInt(LayoutPositionApply(flow, &offset, &finalPoint, NULL), RoboOk);
    RoboCheckEqInt(finalPoint.x, 110);
    RoboCheckEqInt(finalPoint.y, 45);

    /*
     * Saturation, and the reason it clamps rather than wraps: the flow position and the
     * displacement are each bounded by the seam's maximum and their sum is not, so an
     * extreme pair must saturate rather than arrive on the other side of the screen.
     * 10.10 and 10.11 reached the same decision for a *size*; a position is the same
     * arithmetic one axis over.
     */
    flow.x = 2147483647;
    offset.dx = 100;
    RoboCheckEqInt(LayoutPositionApply(flow, &offset, &finalPoint, NULL), RoboOk);
    RoboCheckEqInt(finalPoint.x, 2147483647);

    flow.x = -2147483647;
    offset.dx = -100;
    RoboCheckEqInt(LayoutPositionApply(flow, &offset, &finalPoint, NULL), RoboOk);
    RoboCheckEqInt(finalPoint.x, -2147483647);

    /* And a record no resolve produced is refused before it moves anything -- and
     * before it touches the out, which is why the sentinel below survives. */
    bad = offset;
    bad.insetsDeclined = 9u;
    flow.x = 0;
    finalPoint.x = 55;
    RoboCheckEqInt(LayoutPositionApply(flow, &bad, &finalPoint, NULL), RoboError);
    RoboCheckEqInt(finalPoint.x, 55);
}

/* ------------------------------------------------------------------ */
/* The pass                                                            */
/* ------------------------------------------------------------------ */

static void TestPositioningBlockRelative(void)
{
    LayoutPositionFixture fixture;
    LayoutPositionRule htmlRule;
    LayoutPositionRule bodyRule;
    LayoutPositionRule divRule;
    LayoutPositionRule afterRule;
    LayoutPositionRule rules[4];
    LayoutPoint origin;
    LayoutSize size;
    LayoutPositionOffset offset;

    LayoutFixture();

    /*
     * The phase's central claim in three numbers: a `relative` box with `left: 30px;
     * top: 20px` is **drawn** 30 right and 20 down from where flow put it, its **size**
     * is untouched, and the box after it starts exactly where it always would have.
     */
    LayoutPositionRuleInit(&htmlRule, "html", RoboCssDisplayBlock);
    LayoutPositionRuleInit(&bodyRule, "body", RoboCssDisplayBlock);
    LayoutPositionRuleInit(&divRule, "div", RoboCssDisplayBlock);
    LayoutPositionRulePosition(&divRule, RoboCssPositioningRelative);
    LayoutPositionRulePx(&divRule, "height", 40.0);
    LayoutPositionRulePx(&divRule, "left", 30.0);
    LayoutPositionRulePx(&divRule, "top", 20.0);
    LayoutPositionRuleInit(&afterRule, "section", RoboCssDisplayBlock);
    LayoutPositionRulePx(&afterRule, "height", 25.0);
    rules[0] = htmlRule;
    rules[1] = bodyRule;
    rules[2] = divRule;
    rules[3] = afterRule;

    LayoutPositionBegin(&fixture, "<html><body><div></div><section></section></body></html>",
                          rules, 4u);
    LayoutPositionPass(&fixture, LAYOUT_POSITION_VIEWPORT_WIDTH, NULL);

    origin = LayoutPositionOriginOf(&fixture, LayoutPositionBoxFor(&fixture, "div"));
    RoboCheckEqInt(origin.x, 30);
    RoboCheckEqInt(origin.y, 20);
    size = LayoutPositionSizeOf(&fixture, LayoutPositionBoxFor(&fixture, "div"));
    RoboCheckEqInt(size.width, LAYOUT_POSITION_VIEWPORT_WIDTH);
    RoboCheckEqInt(size.height, 40);

    /* The record a reader turns the final position back into the flow one with. */
    offset = LayoutPositionOffsetOf(&fixture, LayoutPositionBoxFor(&fixture, "div"));
    RoboCheckEqInt(offset.dx, 30);
    RoboCheckEqInt(offset.dy, 20);
    RoboCheckEqInt(offset.insetsResolved, 2u);

    /* And the witness: `section` at the flow position the `div` *would* have had. */
    origin = LayoutPositionOriginOf(&fixture, LayoutPositionBoxFor(&fixture, "section"));
    RoboCheckEqInt(origin.x, 0);
    RoboCheckEqInt(origin.y, 40);

    LayoutPositionEnd(&fixture);
}

static void TestPositioningFlowPreserved(void)
{
    LayoutPositionFixture fixture;
    LayoutPositionRule htmlRule;
    LayoutPositionRule bodyRule;
    LayoutPositionRule firstRule;
    LayoutPositionRule secondRule;
    LayoutPositionRule thirdRule;
    LayoutPositionRule rules[5];
    LayoutPoint origin;
    LayoutStats stats;
    LayoutPositionOffset offset;

    LayoutFixture();

    /*
     * The brief's visual case, as geometry: a normal box, a relative box shifted
     * 100px down, and a box after it. The relative box moves; its original flow space
     * stays occupied; the following element does not move into it -- and `after`'s
     * origin is *below* the relative box's *unshifted* bottom edge, which is the one
     * assertion that separates "the offset applied" from "the box moved".
     */
    LayoutPositionRuleInit(&htmlRule, "html", RoboCssDisplayBlock);
    LayoutPositionRuleInit(&bodyRule, "body", RoboCssDisplayBlock);
    LayoutPositionRuleInit(&firstRule, "div", RoboCssDisplayBlock);
    LayoutPositionRulePx(&firstRule, "height", 30.0);
    LayoutPositionRuleInit(&secondRule, "span", RoboCssDisplayBlock);
    LayoutPositionRulePosition(&secondRule, RoboCssPositioningRelative);
    LayoutPositionRulePx(&secondRule, "height", 30.0);
    LayoutPositionRulePx(&secondRule, "top", 100.0);
    LayoutPositionRuleInit(&thirdRule, "b", RoboCssDisplayBlock);
    LayoutPositionRulePx(&thirdRule, "height", 30.0);
    rules[0] = htmlRule;
    rules[1] = bodyRule;
    rules[2] = firstRule;
    rules[3] = secondRule;
    rules[4] = thirdRule;

    LayoutPositionBegin(&fixture,
                          "<html><body><div></div><span></span><b></b></body></html>", rules,
                          5u);
    LayoutPositionPass(&fixture, LAYOUT_POSITION_VIEWPORT_WIDTH, &stats);

    origin = LayoutPositionOriginOf(&fixture, LayoutPositionBoxFor(&fixture, "div"));
    RoboCheckEqInt(origin.y, 0);

    origin = LayoutPositionOriginOf(&fixture, LayoutPositionBoxFor(&fixture, "span"));
    RoboCheckEqInt(origin.y, 130);
    offset = LayoutPositionOffsetOf(&fixture, LayoutPositionBoxFor(&fixture, "span"));
    RoboCheckEqInt(offset.dy, 100);

    /* Not 60 (drawn into the hole) and not 230 (flow moved by the offset): 60 is the
     * flow position and the offset lives only on `span`. */
    origin = LayoutPositionOriginOf(&fixture, LayoutPositionBoxFor(&fixture, "b"));
    RoboCheckEqInt(origin.y, 60);

    RoboCheckEqInt(stats.block.boxesShifted, 1u);
    RoboCheckEqInt(stats.block.offsetsApplied, 1u);

    LayoutPositionEnd(&fixture);
}

static void TestPositioningBlockNegative(void)
{
    LayoutPositionFixture fixture;
    LayoutPositionRule htmlRule;
    LayoutPositionRule bodyRule;
    LayoutPositionRule divRule;
    LayoutPositionRule rules[3];
    LayoutPoint origin;
    LayoutSize size;
    LayoutPositionOffset offset;

    LayoutFixture();

    /*
     * Negative offsets are legal and honoured -- the signedness the vocabulary exists
     * for. A box shifted up and left produces negative *positions* and a perfectly
     * valid rectangle: nothing clamps, nothing refuses, and the size is untouched.
     */
    LayoutPositionRuleInit(&htmlRule, "html", RoboCssDisplayBlock);
    LayoutPositionRuleInit(&bodyRule, "body", RoboCssDisplayBlock);
    LayoutPositionRuleInit(&divRule, "div", RoboCssDisplayBlock);
    LayoutPositionRulePosition(&divRule, RoboCssPositioningRelative);
    LayoutPositionRulePx(&divRule, "height", 40.0);
    LayoutPositionRuleNegative(&divRule, "left", 10.0);
    LayoutPositionRuleNegative(&divRule, "top", 5.0);
    rules[0] = htmlRule;
    rules[1] = bodyRule;
    rules[2] = divRule;

    LayoutPositionBegin(&fixture, "<html><body><div></div></body></html>", rules, 3u);
    LayoutPositionPass(&fixture, LAYOUT_POSITION_VIEWPORT_WIDTH, NULL);

    origin = LayoutPositionOriginOf(&fixture, LayoutPositionBoxFor(&fixture, "div"));
    RoboCheckEqInt(origin.x, -10);
    RoboCheckEqInt(origin.y, -5);
    size = LayoutPositionSizeOf(&fixture, LayoutPositionBoxFor(&fixture, "div"));
    RoboCheckEqInt(size.width, LAYOUT_POSITION_VIEWPORT_WIDTH);
    RoboCheckEqInt(size.height, 40);

    offset = LayoutPositionOffsetOf(&fixture, LayoutPositionBoxFor(&fixture, "div"));
    RoboCheckEqInt(offset.dx, -10);
    RoboCheckEqInt(offset.dy, -5);
    RoboCheckEqInt(LayoutPositionOffsetIsValid(&offset), 1);

    LayoutPositionEnd(&fixture);
}

static void TestPositioningBlockStaticDeclines(void)
{
    LayoutPositionFixture fixture;
    LayoutPositionRule htmlRule;
    LayoutPositionRule bodyRule;
    LayoutPositionRule divRule;
    LayoutPositionRule rules[3];
    LayoutPoint origin;
    LayoutStats stats;
    LayoutPositionOffset offset;

    LayoutFixture();

    /*
     * The case that separates two documents whose displacement is the same zero: a
     * `static` box with four insets and a `relative` box with none. CSS applies no
     * offset to a static box -- 9.13's header names the mistake this rule prevents --
     * so the insets change nothing here and are **counted as declined**, which is what
     * makes the first document visible as one.
     */
    LayoutPositionRuleInit(&htmlRule, "html", RoboCssDisplayBlock);
    LayoutPositionRuleInit(&bodyRule, "body", RoboCssDisplayBlock);
    LayoutPositionRuleInit(&divRule, "div", RoboCssDisplayBlock);
    LayoutPositionRulePosition(&divRule, RoboCssPositioningStatic);
    LayoutPositionRulePx(&divRule, "height", 40.0);
    LayoutPositionRulePx(&divRule, "top", 50.0);
    LayoutPositionRulePx(&divRule, "left", 60.0);
    LayoutPositionRulePx(&divRule, "right", 70.0);
    LayoutPositionRulePx(&divRule, "bottom", 80.0);
    rules[0] = htmlRule;
    rules[1] = bodyRule;
    rules[2] = divRule;

    LayoutPositionBegin(&fixture, "<html><body><div></div></body></html>", rules, 3u);
    LayoutPositionPass(&fixture, LAYOUT_POSITION_VIEWPORT_WIDTH, &stats);

    origin = LayoutPositionOriginOf(&fixture, LayoutPositionBoxFor(&fixture, "div"));
    RoboCheckEqInt(origin.x, 0);
    RoboCheckEqInt(origin.y, 0);

    offset = LayoutPositionOffsetOf(&fixture, LayoutPositionBoxFor(&fixture, "div"));
    RoboCheckEqInt(offset.dx, 0);
    RoboCheckEqInt(offset.dy, 0);
    RoboCheckEqInt(offset.insetsDeclined, 4u);
    RoboCheckEqInt(offset.insetsResolved, 0u);

    RoboCheckEqInt(stats.block.boxesShifted, 0u);
    RoboCheckEqInt(stats.block.offsetsDeclined, 4u);

    LayoutPositionEnd(&fixture);
}

static void TestPositioningBoxModelInteraction(void)
{
    LayoutPositionFixture fixture;
    LayoutPositionRule htmlRule;
    LayoutPositionRule bodyRule;
    LayoutPositionRule aRule;
    LayoutPositionRule bRule;
    LayoutPositionRule cRule;
    LayoutPositionRule rules[5];
    LayoutPoint origin;
    LayoutSize size;

    LayoutFixture();

    /*
     * A displacement operates on the box the three inset phases already resolved, and
     * it must not reinterpret any of them. Three boxes, one each carrying margin,
     * padding and border beside the same `relative; left: 10px; top: 5px`, and the
     * assertions are the *inset* arithmetic with the offset added exactly once at the
     * end.
     */
    LayoutPositionRuleInit(&htmlRule, "html", RoboCssDisplayBlock);
    LayoutPositionRuleInit(&bodyRule, "body", RoboCssDisplayBlock);

    LayoutPositionRuleInit(&aRule, "div", RoboCssDisplayBlock);
    LayoutPositionRulePosition(&aRule, RoboCssPositioningRelative);
    LayoutPositionRulePx(&aRule, "height", 20.0);
    LayoutPositionRulePx(&aRule, "margin-top", 8.0);
    LayoutPositionRulePx(&aRule, "margin-left", 4.0);
    LayoutPositionRulePx(&aRule, "left", 10.0);
    LayoutPositionRulePx(&aRule, "top", 5.0);

    LayoutPositionRuleInit(&bRule, "section", RoboCssDisplayBlock);
    LayoutPositionRulePosition(&bRule, RoboCssPositioningRelative);
    LayoutPositionRulePx(&bRule, "height", 20.0);
    LayoutPositionRulePx(&bRule, "padding-top", 6.0);
    LayoutPositionRulePx(&bRule, "padding-left", 7.0);
    LayoutPositionRulePx(&bRule, "padding-right", 7.0);
    LayoutPositionRulePx(&bRule, "padding-bottom", 6.0);
    LayoutPositionRulePx(&bRule, "left", 10.0);
    LayoutPositionRulePx(&bRule, "top", 5.0);

    LayoutPositionRuleInit(&cRule, "i", RoboCssDisplayBlock);
    LayoutPositionRulePosition(&cRule, RoboCssPositioningRelative);
    LayoutPositionRulePx(&cRule, "height", 20.0);
    /* A width without a style draws nothing (10.11's rule), so the styles are declared
     * beside the widths -- the case wants borders that occupy space. */
    LayoutPositionRuleBorderStyle(&cRule, "border-top-style",
                                      RoboCssBorderStyleSolid);
    LayoutPositionRuleBorderStyle(&cRule, "border-right-style",
                                      RoboCssBorderStyleSolid);
    LayoutPositionRuleBorderStyle(&cRule, "border-bottom-style",
                                      RoboCssBorderStyleSolid);
    LayoutPositionRuleBorderStyle(&cRule, "border-left-style",
                                      RoboCssBorderStyleSolid);
    LayoutPositionRulePx(&cRule, "border-top-width", 3.0);
    LayoutPositionRulePx(&cRule, "border-left-width", 2.0);
    LayoutPositionRulePx(&cRule, "border-right-width", 2.0);
    LayoutPositionRulePx(&cRule, "border-bottom-width", 3.0);
    LayoutPositionRulePx(&cRule, "left", 10.0);
    LayoutPositionRulePx(&cRule, "top", 5.0);
    rules[0] = htmlRule;
    rules[1] = bodyRule;
    rules[2] = aRule;
    rules[3] = bRule;
    rules[4] = cRule;

    LayoutPositionBegin(&fixture, "<html><body><div></div><section></section><i></i></body></html>",
                          rules, 5u);
    LayoutPositionPass(&fixture, LAYOUT_POSITION_VIEWPORT_WIDTH, NULL);

    /* Margin: the box is at its *margin origin* displaced -- 4 + 10 and 8 + 5. The
     * margin is external spacing, the offset a displacement; neither is the other. */
    origin = LayoutPositionOriginOf(&fixture, LayoutPositionBoxFor(&fixture, "div"));
    RoboCheckEqInt(origin.x, 14);
    RoboCheckEqInt(origin.y, 13);
    size = LayoutPositionSizeOf(&fixture, LayoutPositionBoxFor(&fixture, "div"));
    RoboCheckEqInt(size.height, 20);

    /* Padding: the border box grows by the padding and the offset adds on top -- the
     * box is 814 wide (800 + 7 + 7) and 32 tall, at 10/5.
     *
     * The two numbers a first draft of this case got wrong, and which the run caught:
     * the section is the div's **sibling**, so its *flow* y is the div's margin box
     * (20 + 8) and the 5 it displaces by lands on top of that; and under the build's
     * `content-box` model an `auto` width fills what the insets leave, so the border
     * box is the containing block's width whatever the insets add up to. */
    origin = LayoutPositionOriginOf(&fixture, LayoutPositionBoxFor(&fixture, "section"));
    RoboCheckEqInt(origin.x, 10);
    RoboCheckEqInt(origin.y, 33);
    size = LayoutPositionSizeOf(&fixture, LayoutPositionBoxFor(&fixture, "section"));
    RoboCheckEqInt(size.width, LAYOUT_POSITION_VIEWPORT_WIDTH);
    RoboCheckEqInt(size.height, 32);

    /* Border: the same shape one layer out -- 804 wide, 26 tall, at 10/5, with the
     * flow y now 28 + 32 for the same sibling reason. The `i` declares `height: 20px`
     * and three borders, so its border box is 20 + 3 + 3 = 26 (the content-box model
     * once more: the declaration is the *content*, the border outside it). */
    origin = LayoutPositionOriginOf(&fixture, LayoutPositionBoxFor(&fixture, "i"));
    RoboCheckEqInt(origin.x, 10);
    RoboCheckEqInt(origin.y, 65);
    size = LayoutPositionSizeOf(&fixture, LayoutPositionBoxFor(&fixture, "i"));
    RoboCheckEqInt(size.width, LAYOUT_POSITION_VIEWPORT_WIDTH);
    RoboCheckEqInt(size.height, 26);

    LayoutPositionEnd(&fixture);
}

static void TestPositioningDescendants(void)
{
    LayoutPositionFixture fixture;
    LayoutPositionRule htmlRule;
    LayoutPositionRule bodyRule;
    LayoutPositionRule parentRule;
    LayoutPositionRule childRule;
    LayoutPositionRule rules[4];
    LayoutPoint origin;
    LayoutSize size;
    LayoutBorderEdges borders;
    LayoutPaddingEdges padding;

    LayoutFixture();

    /*
     * A child's position is stored relative to its containing block's *content origin*
     * (`RoboLayoutResult.h`), so displacing a parent moves its whole subtree without
     * any child's stored number changing -- which is the correct CSS behaviour and it
     * falls out of the coordinate convention rather than being implemented twice. The
     * case asserts that, plus the two inset records staying exactly what the parent's
     * own declarations resolved to.
     */
    LayoutPositionRuleInit(&htmlRule, "html", RoboCssDisplayBlock);
    LayoutPositionRuleInit(&bodyRule, "body", RoboCssDisplayBlock);
    LayoutPositionRuleInit(&parentRule, "div", RoboCssDisplayBlock);
    LayoutPositionRulePosition(&parentRule, RoboCssPositioningRelative);
    LayoutPositionRulePx(&parentRule, "height", 60.0);
    LayoutPositionRulePx(&parentRule, "left", 40.0);
    LayoutPositionRulePx(&parentRule, "top", 25.0);
    LayoutPositionRulePx(&parentRule, "border-left-width", 2.0);
    LayoutPositionRulePx(&parentRule, "border-top-width", 3.0);
    LayoutPositionRulePx(&parentRule, "padding-left", 5.0);
    LayoutPositionRulePx(&parentRule, "padding-top", 4.0);
    LayoutPositionRulePx(&parentRule, "border-right-width", 2.0);
    LayoutPositionRulePx(&parentRule, "border-bottom-width", 3.0);
    LayoutPositionRulePx(&parentRule, "padding-right", 5.0);
    LayoutPositionRulePx(&parentRule, "padding-bottom", 4.0);
    LayoutPositionRulePx(&parentRule, "border-top-style", 0.0); /* replaced below */
    rules[0] = htmlRule;
    rules[1] = bodyRule;
    rules[2] = parentRule;
    LayoutPositionRuleInit(&childRule, "p", RoboCssDisplayBlock);
    LayoutPositionRulePx(&childRule, "height", 20.0);
    rules[3] = childRule;

    /* The style vocabulary is typed, so the two border styles are POSITION-free
     * declarations this fixture expresses as solid via the keyword path the border
     * suite uses; a width without a style draws nothing, which is not what this case
     * wants, so the styles are set through the *border-style* longhands built as
     * keywords. */
    parentRule.declarations[9].kind = LAYOUT_POSITION_DECL_KEYWORD;
    parentRule.declarations[9].keyword = "solid";
    (void)LayoutPositionStyleBuild; /* builder reused through begin below */

    LayoutPositionBegin(&fixture, "<html><body><div><p></p></div></body></html>", rules, 4u);
    LayoutPositionPass(&fixture, LAYOUT_POSITION_VIEWPORT_WIDTH, NULL);

    /* The parent is drawn 40 right and 25 down of its margin origin (0,0). */
    origin = LayoutPositionOriginOf(&fixture, LayoutPositionBoxFor(&fixture, "div"));
    RoboCheckEqInt(origin.x, 40);
    RoboCheckEqInt(origin.y, 25);
    size = LayoutPositionSizeOf(&fixture, LayoutPositionBoxFor(&fixture, "div"));
    RoboCheckEqInt(size.width, LAYOUT_POSITION_VIEWPORT_WIDTH);
    RoboCheckEqInt(size.height, 60 + 8);

    /* The child's *stored* position is relative to the parent's content origin and is
     * (0,0) plus nothing -- unchanged by the displacement. */
    origin = LayoutPositionOriginOf(&fixture, LayoutPositionBoxFor(&fixture, "p"));
    RoboCheckEqInt(origin.x, 0);
    RoboCheckEqInt(origin.y, 0);

    /* And the two inset records are the declarations, untouched by the offset. */
    RoboCheckEqInt(LayoutObjectBorders(fixture.tree,
                                                 LayoutPositionBoxFor(&fixture, "div"),
                                                 &borders, NULL),
                      RoboOk);
    RoboCheckEqInt(borders.top.width, 0); /* a keyword style with no declared width
                                              * kind on the *style* side -- see note */
    RoboCheckEqInt(LayoutObjectPadding(fixture.tree,
                                                 LayoutPositionBoxFor(&fixture, "div"),
                                                 &padding, NULL),
                      RoboOk);
    RoboCheckEqInt(padding.top, 4);
    RoboCheckEqInt(padding.left, 5);

    LayoutPositionEnd(&fixture);
}

static void TestPositioningInlineAtomic(void)
{
    LayoutPositionFixture fixture;
    LayoutPositionRule htmlRule;
    LayoutPositionRule bodyRule;
    LayoutPositionRule divRule;
    LayoutPositionRule iRule;
    LayoutPositionRule bRule;
    LayoutPositionRule rules[5];
    LayoutPoint origin;
    LayoutSize size;
    LayoutLineBox line;
    LayoutLineId lineId = RoboLayoutLineIdNone;
    LayoutStats stats;
    LayoutPositionOffset offset;

    LayoutFixture();

    /*
     * An atomic inline displaced along its line: drawn `dx` further on, its line's
     * width and the next participant's advance untouched. The horizontal half applies
     * at placement; the vertical half at finalization, after the baseline correction.
     */
    LayoutPositionRuleInit(&htmlRule, "html", RoboCssDisplayBlock);
    LayoutPositionRuleInit(&bodyRule, "body", RoboCssDisplayBlock);
    LayoutPositionRuleInit(&divRule, "div", RoboCssDisplayBlock);
    LayoutPositionRulePx(&divRule, "height", 30.0);
    LayoutPositionRuleInit(&iRule, "i", RoboCssDisplayInlineBlock);
    LayoutPositionRulePosition(&iRule, RoboCssPositioningRelative);
    LayoutPositionRulePx(&iRule, "width", 100.0);
    LayoutPositionRulePx(&iRule, "height", 20.0);
    LayoutPositionRulePx(&iRule, "left", 15.0);
    LayoutPositionRulePx(&iRule, "top", 6.0);
    LayoutPositionRuleInit(&bRule, "b", RoboCssDisplayInlineBlock);
    LayoutPositionRulePx(&bRule, "width", 50.0);
    LayoutPositionRulePx(&bRule, "height", 10.0);
    rules[0] = htmlRule;
    rules[1] = bodyRule;
    rules[2] = divRule;
    rules[3] = iRule;
    rules[4] = bRule;

    LayoutPositionBegin(&fixture, "<html><body><div><i></i><b></b></div></body></html>", rules,
                          5u);
    LayoutPositionPass(&fixture, LAYOUT_POSITION_VIEWPORT_WIDTH, &stats);

    origin = LayoutPositionOriginOf(&fixture, LayoutPositionBoxFor(&fixture, "i"));
    RoboCheckEqInt(origin.x, 15);
    RoboCheckEqInt(origin.y, 6);
    size = LayoutPositionSizeOf(&fixture, LayoutPositionBoxFor(&fixture, "i"));
    RoboCheckEqInt(size.width, 100);
    RoboCheckEqInt(size.height, 20);

    offset = LayoutPositionOffsetOf(&fixture, LayoutPositionBoxFor(&fixture, "i"));
    RoboCheckEqInt(offset.dx, 15);
    RoboCheckEqInt(offset.dy, 6);

    /* The next participant is not pushed along: its advance is the margin box. */
    origin = LayoutPositionOriginOf(&fixture, LayoutPositionBoxFor(&fixture, "b"));
    RoboCheckEqInt(origin.x, 100);

    /* And the line is the widths of what flows, not of what was displaced. */
    RoboCheckEqInt(LayoutTreeLineCount(fixture.tree), 1u);
    RoboCheckEqInt(LayoutTreeLineAt(fixture.tree, 0u, &lineId, NULL), RoboOk);
    RoboCheckEqInt(LayoutTreeLine(fixture.tree, lineId, &line, NULL), RoboOk);
    RoboCheckEqInt(line.size.width, 150);

    RoboCheckEqInt(stats.inlineFlow.boxesShifted, 1u);
    RoboCheckEqInt(stats.inlineFlow.offsetsApplied, 2u);

    LayoutPositionEnd(&fixture);
}

static void TestPositioningInlineTransparentNotApplied(void)
{
    LayoutPositionFixture fixture;
    LayoutPositionRule htmlRule;
    LayoutPositionRule bodyRule;
    LayoutPositionRule divRule;
    LayoutPositionRule spanRule;
    LayoutPositionRule rules[4];
    LayoutDiag diag;
    LayoutPositionOffset offset;

    LayoutFixture();

    /*
     * A transparent inline box's rectangle is the **union** of its contents
     * (`RoboLayoutLineBox.h`), so this build does not offset that union by a declared
     * `left`/`top` -- and the door *says so* rather than answering a zero, which is
     * what keeps "no offset" and "not applicable" two statements.
     */
    LayoutPositionRuleInit(&htmlRule, "html", RoboCssDisplayBlock);
    LayoutPositionRuleInit(&bodyRule, "body", RoboCssDisplayBlock);
    LayoutPositionRuleInit(&divRule, "div", RoboCssDisplayBlock);
    LayoutPositionRulePx(&divRule, "height", 30.0);
    LayoutPositionRuleInit(&spanRule, "span", RoboCssDisplayInline);
    LayoutPositionRulePosition(&spanRule, RoboCssPositioningRelative);
    LayoutPositionRulePx(&spanRule, "left", 20.0);
    LayoutPositionRulePx(&spanRule, "top", 10.0);
    rules[0] = htmlRule;
    rules[1] = bodyRule;
    rules[2] = divRule;
    rules[3] = spanRule;

    LayoutPositionBegin(&fixture, "<html><body><div><span></span></div></body></html>", rules,
                          4u);
    LayoutPositionPass(&fixture, LAYOUT_POSITION_VIEWPORT_WIDTH, NULL);

    LayoutDiagReset(&diag);
    RoboCheckEqInt(LayoutObjectPosition(fixture.tree,
                                                  LayoutPositionBoxFor(&fixture, "span"),
                                                  &offset, &diag),
                      RoboNotFound);
    RoboCheckEqInt((int)diag.domain, (int)RoboLayoutDomainObject);

    LayoutPositionEnd(&fixture);
}

static void TestPositioningObjectDoor(void)
{
    LayoutPositionFixture fixture;
    LayoutPositionRule htmlRule;
    LayoutPositionRule bodyRule;
    LayoutPositionRule relativeRule;
    LayoutPositionRule staticRule;
    LayoutPositionRule rules[4];
    LayoutPositionOffset offset;
    LayoutDiag diag;
    LayoutNodeId missing;

    LayoutFixture();

    /*
     * The read side: a `relative` box reports its displacement, a `static` one reports
     * a zero with its counters, and the door refuses the two cases where no offset
     * could exist -- an id that names nothing and a box no pass placed.
     */
    LayoutPositionRuleInit(&htmlRule, "html", RoboCssDisplayBlock);
    LayoutPositionRuleInit(&bodyRule, "body", RoboCssDisplayBlock);
    LayoutPositionRuleInit(&relativeRule, "div", RoboCssDisplayBlock);
    LayoutPositionRulePosition(&relativeRule, RoboCssPositioningRelative);
    LayoutPositionRulePx(&relativeRule, "height", 20.0);
    LayoutPositionRulePx(&relativeRule, "left", 11.0);
    LayoutPositionRulePx(&relativeRule, "top", 7.0);
    LayoutPositionRuleInit(&staticRule, "section", RoboCssDisplayBlock);
    LayoutPositionRulePx(&staticRule, "height", 20.0);
    rules[0] = htmlRule;
    rules[1] = bodyRule;
    rules[2] = relativeRule;
    rules[3] = staticRule;

    LayoutPositionBegin(&fixture, "<html><body><div></div><section></section></body></html>",
                          rules, 4u);
    LayoutPositionPass(&fixture, LAYOUT_POSITION_VIEWPORT_WIDTH, NULL);

    offset = LayoutPositionOffsetOf(&fixture, LayoutPositionBoxFor(&fixture, "div"));
    RoboCheckEqInt(offset.dx, 11);
    RoboCheckEqInt(offset.dy, 7);
    RoboCheckEqInt(offset.insetsResolved, 2u);

    /* A static box answers a zero -- which is the contrast with the three inset
     * doors: their zero is a legitimate *state*, so they refuse and ask the result;
     * here the zero is the answer for every static box in every document. */
    RoboCheckEqInt(LayoutObjectPosition(fixture.tree,
                                                  LayoutPositionBoxFor(&fixture, "section"),
                                                  &offset, NULL),
                      RoboOk);
    RoboCheckEqInt(offset.dx, 0);
    RoboCheckEqInt(offset.dy, 0);

    /* An id that names nothing. */
    LayoutDiagReset(&diag);
    missing = LayoutPositionBoxFor(&fixture, "div");
    RoboCheckEqInt(LayoutObjectPosition(fixture.tree, missing, &offset, &diag),
                      RoboOk);
    RoboCheckEqInt(LayoutObjectPosition(fixture.tree, (LayoutNodeId)9999,
                                                  &offset, &diag),
                      RoboNotFound);
    RoboCheckEqInt((int)diag.domain, (int)RoboLayoutDomainObject);

    /* And NULL arguments, refused rather than dereferenced. */
    RoboCheckEqInt(LayoutObjectPosition(NULL, missing, &offset, NULL),
                      RoboInvalidArgument);
    RoboCheckEqInt(LayoutObjectPosition(fixture.tree, missing, NULL, NULL),
                      RoboInvalidArgument);

    LayoutPositionEnd(&fixture);
}

static void TestPositioningRelayout(void)
{
    LayoutPositionFixture fixture;
    LayoutPositionRule htmlRule;
    LayoutPositionRule bodyRule;
    LayoutPositionRule divRule;
    LayoutPositionRule changedRule;
    LayoutPositionRule rules[3];
    LayoutPoint origin;
    LayoutPoint before;
    LayoutStats stats;

    LayoutFixture();

    /*
     * Changing an inset moves the box and nothing else -- the existing invalidation
     * mechanism *is* the rebuild, and the pass replaces geometry rather than refining
     * it. The two facts asserted are the pair the phase turns on: the final position
     * moves, and the following sibling (here the body's extent) does not.
     */
    LayoutPositionRuleInit(&htmlRule, "html", RoboCssDisplayBlock);
    LayoutPositionRuleInit(&bodyRule, "body", RoboCssDisplayBlock);
    LayoutPositionRuleInit(&divRule, "div", RoboCssDisplayBlock);
    LayoutPositionRulePosition(&divRule, RoboCssPositioningRelative);
    LayoutPositionRulePx(&divRule, "height", 30.0);
    LayoutPositionRulePx(&divRule, "left", 10.0);
    LayoutPositionRulePx(&divRule, "top", 10.0);
    rules[0] = htmlRule;
    rules[1] = bodyRule;
    rules[2] = divRule;

    LayoutPositionBegin(&fixture, "<html><body><div></div></body></html>", rules, 3u);
    LayoutPositionPass(&fixture, LAYOUT_POSITION_VIEWPORT_WIDTH, NULL);
    before = LayoutPositionOriginOf(&fixture, LayoutPositionBoxFor(&fixture, "div"));
    RoboCheckEqInt(before.x, 10);
    RoboCheckEqInt(before.y, 10);

    /* A pure style change: `top: 10px` becomes `top: 45px`, `left` unchanged. */
    LayoutPositionRuleInit(&changedRule, "div", RoboCssDisplayBlock);
    LayoutPositionRulePosition(&changedRule, RoboCssPositioningRelative);
    LayoutPositionRulePx(&changedRule, "height", 30.0);
    LayoutPositionRulePx(&changedRule, "left", 10.0);
    LayoutPositionRulePx(&changedRule, "top", 45.0);
    LayoutPositionStyleReplace(&fixture.source, "div", &changedRule);
    LayoutPositionRebuild(&fixture);
    LayoutPositionPass(&fixture, LAYOUT_POSITION_VIEWPORT_WIDTH, &stats);

    origin = LayoutPositionOriginOf(&fixture, LayoutPositionBoxFor(&fixture, "div"));
    RoboCheckEqInt(origin.x, 10);
    RoboCheckEqInt(origin.y, 45);
    RoboCheckEqInt(stats.block.boxesShifted, 1u);
    RoboCheckEqInt(stats.block.offsetsApplied, 2u);

    /* And a mode change back to `static` declines the same declarations. */
    LayoutPositionRuleInit(&changedRule, "div", RoboCssDisplayBlock);
    LayoutPositionRulePosition(&changedRule, RoboCssPositioningStatic);
    LayoutPositionRulePx(&changedRule, "height", 30.0);
    LayoutPositionRulePx(&changedRule, "left", 10.0);
    LayoutPositionRulePx(&changedRule, "top", 45.0);
    LayoutPositionStyleReplace(&fixture.source, "div", &changedRule);
    LayoutPositionRebuild(&fixture);
    LayoutPositionPass(&fixture, LAYOUT_POSITION_VIEWPORT_WIDTH, &stats);

    origin = LayoutPositionOriginOf(&fixture, LayoutPositionBoxFor(&fixture, "div"));
    RoboCheckEqInt(origin.x, 0);
    RoboCheckEqInt(origin.y, 0);
    RoboCheckEqInt(stats.block.boxesShifted, 0u);
    RoboCheckEqInt(stats.block.offsetsDeclined, 2u);

    LayoutPositionEnd(&fixture);
}

static void TestPositioningDeterminism(void)
{
    LayoutPositionFixture fixture;
    LayoutPositionRule htmlRule;
    LayoutPositionRule bodyRule;
    LayoutPositionRule divRule;
    LayoutPositionRule rules[3];
    LayoutPoint first;
    LayoutPoint second;
    LayoutSize firstSize;
    LayoutSize secondSize;

    LayoutFixture();

    LayoutPositionRuleInit(&htmlRule, "html", RoboCssDisplayBlock);
    LayoutPositionRuleInit(&bodyRule, "body", RoboCssDisplayBlock);
    LayoutPositionRuleInit(&divRule, "div", RoboCssDisplayBlock);
    LayoutPositionRulePosition(&divRule, RoboCssPositioningRelative);
    LayoutPositionRulePx(&divRule, "height", 40.0);
    LayoutPositionRuleNegative(&divRule, "left", 7.0);
    LayoutPositionRulePx(&divRule, "top", 13.0);
    rules[0] = htmlRule;
    rules[1] = bodyRule;
    rules[2] = divRule;

    LayoutPositionBegin(&fixture, "<html><body><div></div></body></html>", rules, 3u);

    LayoutPositionPass(&fixture, LAYOUT_POSITION_VIEWPORT_WIDTH, NULL);
    first = LayoutPositionOriginOf(&fixture, LayoutPositionBoxFor(&fixture, "div"));
    firstSize = LayoutPositionSizeOf(&fixture, LayoutPositionBoxFor(&fixture, "div"));

    /* Identical input, identical output -- twice, through the same context-free doors. */
    LayoutPositionPass(&fixture, LAYOUT_POSITION_VIEWPORT_WIDTH, NULL);
    second = LayoutPositionOriginOf(&fixture, LayoutPositionBoxFor(&fixture, "div"));
    secondSize = LayoutPositionSizeOf(&fixture, LayoutPositionBoxFor(&fixture, "div"));
    RoboCheckEqInt(first.x, second.x);
    RoboCheckEqInt(first.y, second.y);
    RoboCheckEqInt(firstSize.width, secondSize.width);
    RoboCheckEqInt(firstSize.height, secondSize.height);
    RoboCheckEqInt(first.x, -7);
    RoboCheckEqInt(first.y, 13);

    LayoutPositionEnd(&fixture);
}

static void TestPositioningAllocatorBalance(void)
{
    size_t before = LayoutLiveBlocks();
    size_t settled;
    int round;

    LayoutFixture();

    for (round = 0; round < 3; round++) {
        LayoutPositionFixture fixture;
        LayoutPositionRule htmlRule;
        LayoutPositionRule bodyRule;
        LayoutPositionRule divRule;
        LayoutPositionRule rules[3];

        LayoutPositionRuleInit(&htmlRule, "html", RoboCssDisplayBlock);
        LayoutPositionRuleInit(&bodyRule, "body", RoboCssDisplayBlock);
        LayoutPositionRuleInit(&divRule, "div", RoboCssDisplayBlock);
        LayoutPositionRulePosition(&divRule, RoboCssPositioningRelative);
        LayoutPositionRulePx(&divRule, "height", 25.0);
        LayoutPositionRulePx(&divRule, "left", 12.0);
        LayoutPositionRulePx(&divRule, "top", 9.0);
        rules[0] = htmlRule;
        rules[1] = bodyRule;
        rules[2] = divRule;

        LayoutPositionBegin(&fixture, "<html><body><div></div></body></html>", rules, 3u);
        LayoutPositionPass(&fixture, LAYOUT_POSITION_VIEWPORT_WIDTH, NULL);
        settled = LayoutLiveBlocks();
        /* The positioning layer allocates nothing of its own: the records are values on
         * the stack and on the object, and the pass adds no arena. */
        RoboCheckEqInt(LayoutLiveBlocks(), settled);
        LayoutPositionPass(&fixture, 400, NULL);
        RoboCheckEqInt(LayoutLiveBlocks(), settled);
        LayoutPositionEnd(&fixture);
        RoboCheckEqInt(LayoutLiveBlocks(), before);
    }
}

/* ------------------------------------------------------------------ */
/* Registration                                                        */
/* ------------------------------------------------------------------ */

void TestRegisterLayoutPosition(void);

void TestRegisterLayoutPosition(void)
{
    TestRegister("layout.positioning.kind_vocabulary", TestPositioningKindVocabulary);
    TestRegister("layout.positioning.inset_record", TestPositioningInsetRecord);
    TestRegister("layout.positioning.position_record", TestPositioningPositionRecord);
    TestRegister("layout.positioning.seam", TestPositioningSeam);
    TestRegister("layout.positioning.offset_record", TestPositioningOffsetRecord);
    TestRegister("layout.positioning.policy_precedence", TestPositioningPolicyPrecedence);
    TestRegister("layout.positioning.policy_static_declines",
                       TestPositioningPolicyStaticDeclines);
    TestRegister("layout.positioning.apply", TestPositioningApply);
    TestRegister("layout.positioning.block_relative", TestPositioningBlockRelative);
    TestRegister("layout.positioning.flow_preserved", TestPositioningFlowPreserved);
    TestRegister("layout.positioning.block_negative", TestPositioningBlockNegative);
    TestRegister("layout.positioning.block_static_declines",
                       TestPositioningBlockStaticDeclines);
    TestRegister("layout.positioning.box_model_interaction",
                       TestPositioningBoxModelInteraction);
    TestRegister("layout.positioning.descendants", TestPositioningDescendants);
    TestRegister("layout.positioning.inline_atomic", TestPositioningInlineAtomic);
    TestRegister("layout.positioning.inline_transparent_not_applied",
                       TestPositioningInlineTransparentNotApplied);
    TestRegister("layout.positioning.object_door", TestPositioningObjectDoor);
    TestRegister("layout.positioning.relayout", TestPositioningRelayout);
    TestRegister("layout.positioning.determinism", TestPositioningDeterminism);
    TestRegister("layout.positioning.allocator_balance", TestPositioningAllocatorBalance);
}
