/*
 * RoboLayout border suite (Phase 10.11) — layout.borders.*
 *
 * Four things are under test, and the boundaries between them are the phase:
 *
 *   the **vocabulary** -- three width kinds, four sides, ten styles and 9.9's colour
 *   record -- which is pure, and whose one difference from 10.10's padding is that a
 *   *non-negative* length is only half the story: a style that does not draw makes
 *   even a resolved five a used zero;
 *
 *   the **seam**: what a computed style declared, which is the one place this module
 *   reads twelve longhands and the one place `thin`, `medium` and `thick` become
 *   numbers;
 *
 *   the two **derived numbers** -- the padding box a containing block leaves once the
 *   borders are out of it, and the border box a padding box asks for -- driven
 *   directly, so that a wrong subtraction fails at the arithmetic rather than three
 *   boxes later in a coordinate;
 *
 *   the **pass**: a border reaching geometry, which since this phase means a box's
 *   `size` is its border box, an `auto` width fills what the margins, the border *and*
 *   the padding left, an atomic inline's advance carries it, and the whole record --
 *   used width, style and colour per side -- is on the object for paint.
 *
 * The rule the phase turns on is asserted every way it can be seen: **a border's used
 * width is zero unless its style generates one**. A `border: 5px none` box is the same
 * box as one with no border declaration at all, which is why
 * `layout.borders.noneStyle` and `layout.borders.edges_record` are two cases rather
 * than one, and why the third counter (`bordersDrawn`) exists beside the two that
 * count declarations.
 *
 * The other claim worth pinning, and the one a build could pass every size assertion
 * without honouring: a border grows the box **inward**. `layout.borders.block_auto_width`
 * is the case that fails a build which grew the box past its containing block, and
 * `layout.borders.padding_interaction` is the one that fails a build which placed
 * contents at `padding-left` and forgot the border.
 *
 * The scripts here are *computed styles*, built through 9.8's makers exactly as the
 * sizing, margin and padding suites build them, because a fabricated struct would prove
 * the pass reads a struct rather than a computed CSS value. Where a case needs a
 * declaration the fixture's property table cannot express through a CSS literal -- an
 * unresolved length is a flag on the value, and the styles and colours are their own
 * makers -- the value is built the way the computed layer builds it and the seam is what
 * is under test.
 *
 * Elements are resolved **by tag** (`LayoutBorderBoxFor`) rather than by arena index,
 * which is the lesson 10.8's suite wrote down: a fixture's indices are a fact about the
 * parser, and the tags here are distinct anyway so that one scripted style per element is
 * enough.
 */

#include "RoboLayoutTestSupport.h"

#include <string.h>

/* ------------------------------------------------------------------ */
/* A fabricated style that can declare anything                        */
/* ------------------------------------------------------------------ */

/** One declaration to put on a fabricated style. */
typedef enum layoutBorderDeclKind {
    LAYOUT_BORDER_DECL_PX = 0,
    /** A px length that is **negative**: invalid CSS for a border width, and the one
     *  declaration the seam is asked to refuse rather than use. */
    LAYOUT_BORDER_DECL_NEGATIVE_PX = 1,
    /** A px length flagged unresolved, which is what a `var()` arrives as. */
    LAYOUT_BORDER_DECL_UNRESOLVED_PX = 2,
    LAYOUT_BORDER_DECL_PERCENTAGE = 3,
    LAYOUT_BORDER_DECL_EM = 4,
    LAYOUT_BORDER_DECL_KEYWORD = 5,
    /** A `CssComputedValue` of kind BORDER_STYLE -- 9.15's typed vocabulary. */
    LAYOUT_BORDER_DECL_BORDER_STYLE = 6,
    /** A `CssComputedValue` of kind COLOR -- 9.9's record. */
    LAYOUT_BORDER_DECL_COLOR = 7
} LayoutBorderDeclKind;

typedef struct layoutBorderDecl {
    const char *name;
    LayoutBorderDeclKind kind;
    double number;
    const char *keyword;
    CssBorderStyle borderStyle;
    CssColor color;
} LayoutBorderDecl;

#define LAYOUT_BORDER_MAX_DECLARATIONS 24u
#define LAYOUT_BORDER_MAX_RULES 8u

typedef struct layoutBorderRule {
    const char *tag;
    CssDisplay display;
    LayoutBorderDecl declarations[LAYOUT_BORDER_MAX_DECLARATIONS];
    size_t declarationsCount;
} LayoutBorderRule;

static void LayoutBorderRuleInit(LayoutBorderRule *rule,
                                    const char *tag,
                                    CssDisplay display)
{
    memset(rule, 0, sizeof(*rule));
    rule->tag = tag;
    rule->display = display;
}

static LayoutBorderDecl *LayoutBorderRuleAdd(LayoutBorderRule *rule,
                                                    const char *name,
                                                    LayoutBorderDeclKind kind)
{
    LayoutBorderDecl *declaration;

    RoboCheckEqInt(rule->declarationsCount < LAYOUT_BORDER_MAX_DECLARATIONS, 1);
    declaration = &rule->declarations[rule->declarationsCount];
    memset(declaration, 0, sizeof(*declaration));
    declaration->name = name;
    declaration->kind = kind;
    declaration->borderStyle = RoboCssBorderStyleNone;
    declaration->color = CssColorNone();
    rule->declarationsCount++;
    return declaration;
}

static void LayoutBorderRulePx(LayoutBorderRule *rule, const char *name, double px)
{
    LayoutBorderRuleAdd(rule, name, LAYOUT_BORDER_DECL_PX)->number = px;
}

/** A *negative* px length, which CSS forbids for a border width and the seam refuses. */
static void LayoutBorderRuleNegative(LayoutBorderRule *rule,
                                        const char *name,
                                        double px)
{
    LayoutBorderRuleAdd(rule, name, LAYOUT_BORDER_DECL_NEGATIVE_PX)->number = px;
}

static void LayoutBorderRuleUnresolved(LayoutBorderRule *rule,
                                          const char *name,
                                          double px)
{
    LayoutBorderRuleAdd(rule, name, LAYOUT_BORDER_DECL_UNRESOLVED_PX)->number = px;
}

static void LayoutBorderRulePercentage(LayoutBorderRule *rule,
                                          const char *name,
                                          double percent)
{
    LayoutBorderRuleAdd(rule, name, LAYOUT_BORDER_DECL_PERCENTAGE)->number = percent;
}

static void LayoutBorderRuleEm(LayoutBorderRule *rule, const char *name, double em)
{
    LayoutBorderRuleAdd(rule, name, LAYOUT_BORDER_DECL_EM)->number = em;
}

/** A keyword, which for a width is `thin`, `medium`, `thick` -- or one the grammar does
 *  not allow, which the seam must report as a declaration it cannot use. */
static void LayoutBorderRuleKeyword(LayoutBorderRule *rule,
                                       const char *name,
                                       const char *keyword)
{
    LayoutBorderRuleAdd(rule, name, LAYOUT_BORDER_DECL_KEYWORD)->keyword = keyword;
}

static void LayoutBorderRuleBorderStyle(LayoutBorderRule *rule,
                                            const char *name,
                                            CssBorderStyle style)
{
    LayoutBorderRuleAdd(rule, name, LAYOUT_BORDER_DECL_BORDER_STYLE)->borderStyle = style;
}

static void LayoutBorderRuleColor(LayoutBorderRule *rule,
                                     const char *name,
                                     CssColor color)
{
    LayoutBorderRuleAdd(rule, name, LAYOUT_BORDER_DECL_COLOR)->color = color;
}

/**
 * One `border-width` shorthand written the way a stylesheet would.
 *
 * `border-width: 5px` is four declarations one layer down, and that is the fact a case
 * asserts by writing the shorthand and reading four sides of geometry: the expansion is
 * 9.15's, and this fixture writing it does not make it this suite's.
 */
static void LayoutBorderRuleWidthAll(LayoutBorderRule *rule, double px)
{
    LayoutBorderRulePx(rule, "border-top-width", px);
    LayoutBorderRulePx(rule, "border-right-width", px);
    LayoutBorderRulePx(rule, "border-bottom-width", px);
    LayoutBorderRulePx(rule, "border-left-width", px);
}

static void LayoutBorderRuleStyleAll(LayoutBorderRule *rule,
                                         CssBorderStyle style)
{
    LayoutBorderRuleBorderStyle(rule, "border-top-style", style);
    LayoutBorderRuleBorderStyle(rule, "border-right-style", style);
    LayoutBorderRuleBorderStyle(rule, "border-bottom-style", style);
    LayoutBorderRuleBorderStyle(rule, "border-left-style", style);
}

static void LayoutBorderRuleColorAll(LayoutBorderRule *rule, CssColor color)
{
    LayoutBorderRuleColor(rule, "border-top-color", color);
    LayoutBorderRuleColor(rule, "border-right-color", color);
    LayoutBorderRuleColor(rule, "border-bottom-color", color);
    LayoutBorderRuleColor(rule, "border-left-color", color);
}

/** The `border: Npx solid` a case means when it writes one. */
static void LayoutBorderRuleShorthand(LayoutBorderRule *rule, double px)
{
    LayoutBorderRuleWidthAll(rule, px);
    LayoutBorderRuleStyleAll(rule, RoboCssBorderStyleSolid);
}

typedef struct layoutBorderStyleSlot {
    char tag[16];
    CssComputedStyle *style; /* owned */
    size_t answers;
} LayoutBorderStyleSlot;

typedef struct layoutBorderSource {
    LayoutBorderStyleSlot rules[LAYOUT_BORDER_MAX_RULES];
    size_t count;
} LayoutBorderSource;

/**
 * Build the style @p rule describes: a real DISPLAY value plus whatever the rule
 * declares, all through 9.8's own makers and container.
 *
 * `display` is set even when the rule declares nothing else, because 9.12's initial
 * value is `inline` and a fixture that wanted a block box would otherwise be asserting
 * a document made of inline boxes.
 */
static CssComputedStyle *LayoutBorderStyleBuild(const LayoutBorderRule *rule)
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
        const LayoutBorderDecl *declaration = &rule->declarations[index];

        value = (CssComputedValue)RoboCssCleared;
        specified = (CssValue)RoboCssCleared;
        switch (declaration->kind) {
        case LAYOUT_BORDER_DECL_PERCENTAGE:
            RoboCheckEqInt(CssComputedValueMakePercentage(&value, declaration->number,
                                                                     0, NULL),
                              RoboOk);
            break;
        case LAYOUT_BORDER_DECL_KEYWORD:
            RoboCheckEqInt(CssComputedValueMakeKeyword(
                                  &value, CssSliceFromText(declaration->keyword), NULL),
                              RoboOk);
            break;
        case LAYOUT_BORDER_DECL_BORDER_STYLE:
            RoboCheckEqInt(CssComputedValueMakeBorderStyle(
                                  &value, declaration->borderStyle, NULL),
                              RoboOk);
            break;
        case LAYOUT_BORDER_DECL_COLOR:
            RoboCheckEqInt(CssComputedValueMakeColor(&value, declaration->color, NULL),
                              RoboOk);
            break;
        case LAYOUT_BORDER_DECL_UNRESOLVED_PX:
            RoboCheckEqInt(CssComputedValueMakeLength(&value, declaration->number,
                                                                 RoboCssUnitPx, 0, NULL),
                              RoboOk);
            /* The flag the computed layer sets for a `var()` or a `calc()`: carried
             * intact and not a value *yet*, whatever its kind says. */
            value.unresolved = 1;
            break;
        case LAYOUT_BORDER_DECL_EM:
            RoboCheckEqInt(CssComputedValueMakeLength(&value, declaration->number,
                                                                 RoboCssUnitEm, 0, NULL),
                              RoboOk);
            break;
        case LAYOUT_BORDER_DECL_NEGATIVE_PX:
        case LAYOUT_BORDER_DECL_PX:
        default:
            /* Written the same way for both, and that is the point: the *style* holds
             * what it was handed -- a negative length is a computed value like any other
             * -- and it is the classification that refuses one, not the maker. A fixture
             * that could not build the declaration could not test the refusal. */
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
static CssComputedStyle *LayoutBorderStyleAdd(LayoutBorderSource *source,
                                                          const LayoutBorderRule *rule)
{
    size_t slot;
    CssComputedStyle *style;

    LayoutFixture();
    RoboCheckEqInt(source->count < LAYOUT_BORDER_MAX_RULES, 1);
    slot = source->count;
    style = LayoutBorderStyleBuild(rule);
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
static void LayoutBorderStyleReplace(LayoutBorderSource *source,
                                       const char *tag,
                                       const LayoutBorderRule *rule)
{
    size_t index;

    LayoutFixture();
    for (index = 0u; index < source->count; index++) {
        if (strcmp(source->rules[index].tag, tag) == 0) {
            CssComputedStyleDestroy(source->rules[index].style);
            source->rules[index].style = LayoutBorderStyleBuild(rule);
            return;
        }
    }
    RoboCheckMsg(0, "the tag to replace is not in this source");
}

static const CssComputedStyle *LayoutBorderStyleLookup(
    void *userdata,
    const HtmlNodeHandle *element)
{
    LayoutBorderSource *source = (LayoutBorderSource *)userdata;
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
    /* An element nothing was declared for: `display: inline`, no declared border, and
     * that is CSS's initial value rather than a fixture default. */
    return NULL;
}

typedef struct layoutBorderFixture {
    LayoutDocument doc;
    LayoutBorderSource source;
    LayoutStyleSource styles;
    LayoutTree *tree;
} LayoutBorderFixture;

static void LayoutBorderBegin(LayoutBorderFixture *fixture,
                                const char *html,
                                const LayoutBorderRule *rules,
                                size_t ruleCount)
{
    LayoutBuildOptions options;
    size_t index;

    memset(fixture, 0, sizeof(*fixture));
    LayoutDocumentBuild(&fixture->doc, html);
    for (index = 0u; index < ruleCount; index++) {
        (void)LayoutBorderStyleAdd(&fixture->source, &rules[index]);
    }
    LayoutStyleSourceInit(&fixture->styles);
    fixture->styles.userdata = &fixture->source;
    fixture->styles.ComputedStyleFor = LayoutBorderStyleLookup;
    RoboCheckEqInt(LayoutTreeCreate(&fixture->tree, NULL), RoboOk);
    LayoutBuildOptionsInit(&options);
    options.styles = &fixture->styles;
    RoboCheckEqInt(LayoutTreeBuildFromDocument(fixture->tree, fixture->doc.id,
                                                          &options, NULL, NULL),
                      RoboOk);
}

static void LayoutBorderEnd(LayoutBorderFixture *fixture)
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

#define LAYOUT_BORDER_VIEWPORT_WIDTH 800
#define LAYOUT_BORDER_VIEWPORT_HEIGHT 600

/** One pass at @p viewportWidth. */
static void LayoutBorderPass(LayoutBorderFixture *fixture,
                               int32_t viewportWidth,
                               LayoutStats *outStats)
{
    LayoutContext *context = NULL;
    LayoutSize viewport;
    LayoutStats stats;

    viewport.width = viewportWidth;
    viewport.height = LAYOUT_BORDER_VIEWPORT_HEIGHT;
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
static LayoutNodeId LayoutBorderBoxFor(LayoutBorderFixture *fixture,
                                                   const char *tag)
{
    HtmlNodeHandle element = LayoutDocumentFind(&fixture->doc, tag);
    LayoutNodeId id;

    RoboCheckEqInt(HtmlNodeHandleIsWellformed(&element), 1);
    id = LayoutObjectIdForDomNode(fixture->tree, &element);
    RoboCheck(id != RoboLayoutNodeIdNone);
    return id;
}

/** The origin a placed box was given, in its containing block's space. */
static LayoutPoint LayoutBorderOriginOf(LayoutBorderFixture *fixture,
                                                   LayoutNodeId id)
{
    LayoutResult result;
    LayoutPoint position;

    RoboCheckEqInt(LayoutObjectResult(fixture->tree, id, &result), RoboOk);
    RoboCheckEqInt((int)result.state, (int)RoboLayoutResultComputed);
    RoboCheckEqInt(LayoutResultPosition(&result, &position), RoboOk);
    return position;
}

/** The size a placed box was given -- its **border** box since 10.11. */
static LayoutSize LayoutBorderSizeOf(LayoutBorderFixture *fixture,
                                                LayoutNodeId id)
{
    LayoutResult result;
    LayoutSize size;

    RoboCheckEqInt(LayoutObjectResult(fixture->tree, id, &result), RoboOk);
    RoboCheckEqInt((int)result.state, (int)RoboLayoutResultComputed);
    RoboCheckEqInt(LayoutResultSize(&result, &size), RoboOk);
    return size;
}

/** The used border a placed box reports. */
static LayoutBorderEdges LayoutBorderEdgesOf(LayoutBorderFixture *fixture,
                                                         LayoutNodeId id)
{
    LayoutBorderEdges edges;

    RoboCheckEqInt(LayoutObjectBorders(fixture->tree, id, &edges, NULL), RoboOk);
    return edges;
}

/**
 * Rebuild the layout tree against the styles @p fixture was built with.
 *
 * The step a changed declaration needs and the reason it is not optional: the tree
 * **borrows** one computed style per element for as long as the bridge lives, so a
 * replacement that left the old tree standing would leave it holding a destroyed style.
 * The rule is 10.9's and 10.10's, and it is what makes "the second answer is a fact about
 * the second style" checkable rather than a hope.
 */
static void LayoutBorderRebuild(LayoutBorderFixture *fixture)
{
    LayoutBuildOptions options;

    LayoutBuildOptionsInit(&options);
    options.styles = &fixture->styles;
    RoboCheckEqInt(LayoutTreeBuildFromDocument(fixture->tree, fixture->doc.id,
                                                          &options, NULL, NULL),
                      RoboOk);
}

/** The four used widths of @p edges, in CSS's side order. */
static void LayoutBorderWidthsOf(const LayoutBorderEdges *edges,
                                    GfxPixelX out[RoboCssBoxSideCount])
{
    size_t index;

    for (index = 0u; index < RoboCssBoxSideCount; index++) {
        RoboCheckEqInt(LayoutBorderEdgesWidth(edges, (CssBoxSide)index,
                                                        &out[index]),
                          RoboOk);
    }
}

/* ------------------------------------------------------------------ */
/* A hand-built record, for the arithmetic                             */
/* ------------------------------------------------------------------ */

/** A declared side: a width kind, a style and a colour. */
static LayoutBorderSide LayoutBorderSideMake(LayoutBorderWidth width,
                                                        CssBorderStyle style,
                                                        CssColor color)
{
    LayoutBorderSide side;

    side.width = width;
    side.style = style;
    side.color = color;
    return side;
}

/** The four declared sides of a record, each built from its own three facts. */
static LayoutBorders LayoutBorderDeclared(int32_t topPx,
                                                   CssBorderStyle topStyle,
                                                   int32_t rightPx,
                                                   CssBorderStyle rightStyle,
                                                   int32_t bottomPx,
                                                   CssBorderStyle bottomStyle,
                                                   int32_t leftPx,
                                                   CssBorderStyle leftStyle)
{
    LayoutBorders borders;

    borders.top = LayoutBorderSideMake(LayoutBorderWidthResolved(topPx), topStyle,
                                          CssColorNone());
    borders.right = LayoutBorderSideMake(LayoutBorderWidthResolved(rightPx),
                                            rightStyle, CssColorNone());
    borders.bottom = LayoutBorderSideMake(LayoutBorderWidthResolved(bottomPx),
                                             bottomStyle, CssColorNone());
    borders.left = LayoutBorderSideMake(LayoutBorderWidthResolved(leftPx), leftStyle,
                                           CssColorNone());
    return borders;
}

/** A resolved used-side record: the widths given, every style generating a border. */
static LayoutBorderEdges LayoutBorderUsed(int32_t top,
                                                    int32_t right,
                                                    int32_t bottom,
                                                    int32_t left)
{
    LayoutBorderEdges edges;
    LayoutBorders declared = LayoutBorderDeclared(
        top, RoboCssBorderStyleSolid, right, RoboCssBorderStyleSolid, bottom,
        RoboCssBorderStyleSolid, left, RoboCssBorderStyleSolid);

    RoboCheckEqInt(LayoutBorderResolve(&declared, &edges, NULL), RoboOk);
    return edges;
}

/* ------------------------------------------------------------------ */
/* The vocabulary                                                      */
/* ------------------------------------------------------------------ */

static void TestBordersKindVocabulary(void)
{
    size_t index;

    LayoutFixture();

    RoboCheckEqInt((int)RoboLayoutBorderWidthKindCount, 3);
    /*
     * The zero of the vocabulary is `ABSENT`, and that is a decision rather than a
     * numbering accident: a cleared record then holds the side's *initial state*, so a
     * forgotten initialisation is not a claim that somebody wrote a border.
     */
    RoboCheckEqInt((int)RoboLayoutBorderWidthAbsent, 0);
    RoboCheckEqInt((int)RoboLayoutBorderWidthResolved, 1);
    RoboCheckEqInt((int)RoboLayoutBorderWidthUnresolved, 2);

    for (index = 0u; index < RoboLayoutBorderWidthKindCount; index++) {
        LayoutBorderWidthKind kind = (LayoutBorderWidthKind)index;

        RoboCheckEqInt(LayoutBorderWidthKindIsKnown(kind), 1);
        RoboCheck(LayoutBorderWidthKindName(kind) != NULL);
    }
    RoboCheckEqInt(LayoutBorderWidthKindIsKnown(
                          (LayoutBorderWidthKind)3),
                      0);
    RoboCheckEqInt(LayoutBorderWidthKindIsKnown(
                          (LayoutBorderWidthKind)77),
                      0);
    RoboCheckStrEq(LayoutBorderWidthKindName(RoboLayoutBorderWidthAbsent),
                      "absent");
    RoboCheckStrEq(LayoutBorderWidthKindName(RoboLayoutBorderWidthResolved),
                      "resolved");
    RoboCheckStrEq(LayoutBorderWidthKindName(RoboLayoutBorderWidthUnresolved),
                      "unresolved");
    RoboCheckStrEq(LayoutBorderWidthKindName((LayoutBorderWidthKind)3),
                      "unknown");
    RoboCheck(LayoutBorderWidthKindName((LayoutBorderWidthKind)77) != NULL);

    /*
     * And the three numbers this build picks for CSS's three keywords, stated as values
     * because they are a rendering decision rather than a parse: 9.15 keeps `thin`,
     * `medium` and `thick` as keywords, and *here* is where they become 1, 3 and 5. A
     * later goal that changes them changes three macros and this case, and nothing else.
     */
    RoboCheckEqInt(RoboLayoutBorderThinPx, 1);
    RoboCheckEqInt(RoboLayoutBorderMediumPx, 3);
    RoboCheckEqInt(RoboLayoutBorderThickPx, 5);
}

static void TestBordersWidthRecord(void)
{
    LayoutBorderWidth zero = LayoutBorderWidthZero();
    LayoutBorderWidth absent = LayoutBorderWidthAbsent();
    LayoutBorderWidth resolved = LayoutBorderWidthResolved(12);
    LayoutBorderWidth unresolved = LayoutBorderWidthUnresolved();
    LayoutBorderWidth negative = LayoutBorderWidthResolved(-5);
    LayoutBorderWidth badKind;
    GfxPixelX length = 0;

    LayoutFixture();

    /* `border-width: 0` is a border an author asked not to have; `ABSENT` is a side
     * nobody declared. Both are used as zero and they are different records -- the
     * entire reason this vocabulary has three kinds. */
    RoboCheckEqInt(LayoutBorderWidthIsResolved(&zero), 1);
    RoboCheckEqInt(LayoutBorderWidthIsAbsent(&zero), 0);
    RoboCheckEqInt(LayoutBorderWidthIsValid(&zero), 1);
    RoboCheckEqInt(LayoutBorderWidthLength(&zero, &length), RoboOk);
    RoboCheckEqInt(length, 0);
    RoboCheckEqInt(LayoutBorderWidthEqual(&zero, &absent), 0);
    RoboCheckEqInt(LayoutBorderWidthEqual(&zero, &zero), 1);

    RoboCheckEqInt(LayoutBorderWidthIsAbsent(&absent), 1);
    RoboCheckEqInt(LayoutBorderWidthIsValid(&absent), 1);
    /* A miss rather than a zero: reading the field of a side nobody declared would be
     * reading a number that means nothing. */
    length = 77;
    RoboCheckEqInt(LayoutBorderWidthLength(&absent, &length), RoboNotFound);
    RoboCheckEqInt(length, 77);
    RoboCheckEqInt(LayoutBorderWidthEqual(&absent, &absent), 1);

    {
        LayoutBorderWidth other = LayoutBorderWidthResolved(13);

        RoboCheckEqInt(LayoutBorderWidthIsResolved(&resolved), 1);
        RoboCheckEqInt(LayoutBorderWidthIsValid(&resolved), 1);
        RoboCheckEqInt(LayoutBorderWidthLength(&resolved, &length), RoboOk);
        RoboCheckEqInt(length, 12);
        RoboCheckEqInt(LayoutBorderWidthEqual(&resolved, &resolved), 1);
        RoboCheckEqInt(LayoutBorderWidthEqual(&resolved, &other), 0);
    }

    RoboCheckEqInt(LayoutBorderWidthIsUnresolved(&unresolved), 1);
    RoboCheckEqInt(LayoutBorderWidthIsValid(&unresolved), 1);
    RoboCheckEqInt(LayoutBorderWidthLength(&unresolved, &length), RoboNotFound);

    /*
     * And the check this vocabulary has that the margin one does not: a border width is
     * never negative. `border-width: -5px` is invalid CSS, so a record holding one was
     * not produced by this module's own reader -- which is exactly why stating it as a
     * predicate is worth the line.
     */
    RoboCheckEqInt(LayoutBorderWidthIsResolved(&negative), 1);
    RoboCheckEqInt(LayoutBorderWidthIsValid(&negative), 0);
    RoboCheckEqInt(LayoutBorderWidthLength(&negative, &length), RoboOk);
    RoboCheckEqInt(length, -5);

    badKind = LayoutBorderWidthZero();
    badKind.kind = (LayoutBorderWidthKind)99;
    RoboCheckEqInt(LayoutBorderWidthIsValid(&badKind), 0);

    /* Two `ABSENT` sides are equal whatever their stale `length` fields hold -- the
     * reason the kind exists rather than the length alone. */
    {
        LayoutBorderWidth first = LayoutBorderWidthAbsent();
        LayoutBorderWidth second = LayoutBorderWidthAbsent();

        first.length = 999;
        second.length = -999;
        RoboCheckEqInt(LayoutBorderWidthEqual(&first, &second), 1);
    }

    /* NULL is not a record: a predicate answers 0 and the equal comparison answers only
     * for two NULLs. */
    RoboCheckEqInt(LayoutBorderWidthIsValid(NULL), 0);
    RoboCheckEqInt(LayoutBorderWidthIsResolved(NULL), 0);
    RoboCheckEqInt(LayoutBorderWidthLength(NULL, &length), RoboInvalidArgument);
    RoboCheckEqInt(LayoutBorderWidthLength(&resolved, NULL), RoboInvalidArgument);
}

static void TestBordersSideRecord(void)
{
    size_t index;

    LayoutFixture();

    {
        LayoutBorderSide none = LayoutBorderSideNone();
        LayoutBorderSide other = LayoutBorderSideNone();

        RoboCheckEqInt(LayoutBorderWidthIsAbsent(&none.width), 1);
        RoboCheckEqInt((int)none.style, (int)RoboCssBorderStyleNone);
        RoboCheckEqInt((int)none.color.kind, (int)RoboCssColorNone);
        RoboCheckEqInt(LayoutBorderSideIsValid(&none), 1);
        RoboCheckEqInt(LayoutBorderSideGenerates(&none), 0);
        RoboCheckEqInt(LayoutBorderSideEqual(&none, &other), 1);
    }

    /*
     * The `generates` question, walked over the whole style vocabulary rather than a
     * representative: a *declared* width of five draws with eight of the ten styles and
     * does not with `none` or `hidden`. The two that do not are kept as distinct values
     * that answer this one question the same way -- see the note two assertions below.
     */
    for (index = 0u; index < RoboCssBorderStyleCount; index++) {
        CssBorderStyle style = (CssBorderStyle)index;
        LayoutBorderSide side = LayoutBorderSideMake(
            LayoutBorderWidthResolved(5), style, CssColorNone());

        RoboCheckEqInt(CssBorderStyleIsKnown(style), 1);
        RoboCheckEqInt(LayoutBorderSideGenerates(&side),
                          CssBorderStyleGeneratesBorder(style));
        RoboCheck(side.style == style);
    }
    RoboCheckEqInt(CssBorderStyleGeneratesBorder(RoboCssBorderStyleNone), 0);
    /* `hidden` is the *second* style that draws nothing, and it is 9.15's own answer
     * rather than this phase's: the two differ only in border collapsing, where `hidden`
     * outranks the rest -- a table rule this build has no tables for. So the two are kept
     * as distinct values that answer this one question the same way. */
    RoboCheckEqInt(CssBorderStyleGeneratesBorder(RoboCssBorderStyleHidden), 0);
    RoboCheckEqInt(CssBorderStyleGeneratesBorder(RoboCssBorderStyleSolid), 1);
    RoboCheckEqInt(CssBorderStyleGeneratesBorder((CssBorderStyle)77), 0);

    /* And a width that is zero, nobody's or unusable draws nothing whatever the style
     * is, which is the other half of the same question. */
    {
        LayoutBorderSide zero = LayoutBorderSideMake(
            LayoutBorderWidthZero(), RoboCssBorderStyleSolid,
            CssColorNone());
        LayoutBorderSide absent = LayoutBorderSideMake(
            LayoutBorderWidthAbsent(), RoboCssBorderStyleDouble,
            CssColorNone());
        LayoutBorderSide unresolved = LayoutBorderSideMake(
            LayoutBorderWidthUnresolved(), RoboCssBorderStyleDashed,
            CssColorNone());

        RoboCheckEqInt(LayoutBorderSideGenerates(&zero), 0);
        RoboCheckEqInt(LayoutBorderSideGenerates(&absent), 0);
        RoboCheckEqInt(LayoutBorderSideGenerates(&unresolved), 0);
    }

    /* Validity: a style that is not one of the ten, and a negative width, are both
     * refused. */
    {
        LayoutBorderSide badStyle = LayoutBorderSideMake(
            LayoutBorderWidthResolved(5), (CssBorderStyle)99,
            CssColorNone());
        LayoutBorderSide badWidth = LayoutBorderSideMake(
            LayoutBorderWidthResolved(-1), RoboCssBorderStyleSolid,
            CssColorNone());

        RoboCheckEqInt(LayoutBorderSideIsValid(&badStyle), 0);
        RoboCheckEqInt(LayoutBorderSideIsValid(&badWidth), 0);
        RoboCheckEqInt(LayoutBorderSideIsValid(NULL), 0);
    }

    /* Equality is all three facts, and for the colour the *kind*: a `currentColor` and a
     * resolved colour that happened to share channels are different values. */
    {
        LayoutBorderSide current = LayoutBorderSideMake(
            LayoutBorderWidthResolved(5), RoboCssBorderStyleSolid,
            CssColorCurrent());
        LayoutBorderSide same = LayoutBorderSideMake(
            LayoutBorderWidthResolved(5), RoboCssBorderStyleSolid,
            CssColorCurrent());
        LayoutBorderSide black = LayoutBorderSideMake(
            LayoutBorderWidthResolved(5), RoboCssBorderStyleSolid,
            CssColorMake(0, 0, 0, 255));
        LayoutBorderSide otherStyle = LayoutBorderSideMake(
            LayoutBorderWidthResolved(5), RoboCssBorderStyleDashed,
            CssColorCurrent());

        RoboCheckEqInt(LayoutBorderSideEqual(&current, &same), 1);
        RoboCheckEqInt(LayoutBorderSideEqual(&current, &black), 0);
        RoboCheckEqInt(LayoutBorderSideEqual(&current, &otherStyle), 0);
    }
}

/* ------------------------------------------------------------------ */
/* The seam                                                            */
/* ------------------------------------------------------------------ */

static void TestBordersSeam(void)
{
    LayoutBorderRule rule;
    CssComputedStyle *style;
    LayoutBorders borders;
    LayoutBorderSide side;
    GfxPixelX length = 0;

    LayoutFixture();

    /*
     * Twelve longhands, four readers, and the one place `thin`, `medium` and `thick`
     * become numbers. The shared classification folds every keyword but `auto` onto
     * UNUSABLE -- correctly, because no box dimension takes a keyword that names a
     * length -- so the width reader consults the keyword vocabulary on that branch and
     * nowhere else.
     */
    LayoutBorderRuleInit(&rule, "div", RoboCssDisplayBlock);
    LayoutBorderRulePx(&rule, "border-top-width", 5.0);
    LayoutBorderRuleBorderStyle(&rule, "border-top-style", RoboCssBorderStyleSolid);
    LayoutBorderRuleColor(&rule, "border-top-color", CssColorMake(255, 0, 0, 255));
    LayoutBorderRuleKeyword(&rule, "border-right-width", "thin");
    LayoutBorderRuleBorderStyle(&rule, "border-right-style", RoboCssBorderStyleDashed);
    LayoutBorderRuleKeyword(&rule, "border-bottom-width", "medium");
    LayoutBorderRuleBorderStyle(&rule, "border-bottom-style", RoboCssBorderStyleDouble);
    LayoutBorderRuleKeyword(&rule, "border-left-width", "thick");
    LayoutBorderRuleBorderStyle(&rule, "border-left-style", RoboCssBorderStyleDotted);
    style = LayoutBorderStyleBuild(&rule);

    borders = LayoutBordersOf(style);
    RoboCheckEqInt(LayoutBordersIsValid(&borders), 1);

    RoboCheckEqInt(LayoutBorderWidthLength(&borders.top.width, &length), RoboOk);
    RoboCheckEqInt(length, 5);
    RoboCheckEqInt(LayoutBorderWidthLength(&borders.right.width, &length), RoboOk);
    RoboCheckEqInt(length, RoboLayoutBorderThinPx);
    RoboCheckEqInt(LayoutBorderWidthLength(&borders.bottom.width, &length), RoboOk);
    RoboCheckEqInt(length, RoboLayoutBorderMediumPx);
    RoboCheckEqInt(LayoutBorderWidthLength(&borders.left.width, &length), RoboOk);
    RoboCheckEqInt(length, RoboLayoutBorderThickPx);

    RoboCheckEqInt((int)borders.top.style, (int)RoboCssBorderStyleSolid);
    RoboCheckEqInt((int)borders.right.style, (int)RoboCssBorderStyleDashed);
    RoboCheckEqInt((int)borders.bottom.style, (int)RoboCssBorderStyleDouble);
    RoboCheckEqInt((int)borders.left.style, (int)RoboCssBorderStyleDotted);

    /* The colour is 9.9's record, carried unaltered: red where the author wrote one, and
     * `NONE` -- "nobody declared one" rather than `transparent` -- on the other three. */
    RoboCheckEqInt((int)borders.top.color.kind, (int)RoboCssColorRgba);
    RoboCheckEqInt(borders.top.color.red, 255);
    RoboCheckEqInt(borders.top.color.green, 0);
    RoboCheckEqInt(borders.top.color.blue, 0);
    RoboCheckEqInt(borders.top.color.alpha, 255);
    RoboCheckEqInt((int)borders.right.color.kind, (int)RoboCssColorNone);

    RoboCheckEqInt(LayoutBordersSide(&borders, RoboCssBoxSideTop, &side), RoboOk);
    RoboCheckEqInt(LayoutBorderSideEqual(&side, &borders.top), 1);
    RoboCheckEqInt(LayoutBordersSide(&borders, (CssBoxSide)9, &side),
                      RoboNotFound);
    RoboCheckEqInt(LayoutBordersSide(NULL, RoboCssBoxSideTop, &side),
                      RoboInvalidArgument);
    RoboCheckEqInt(LayoutBordersSide(&borders, RoboCssBoxSideTop, NULL),
                      RoboInvalidArgument);

    CssComputedStyleDestroy(style);

    /*
     * `border-color: red blue green black` writes four longhands, so all four colours
     * have to reach their own side rather than one of them travelling for the rest. And
     * `currentColor` survives as a *contextual* colour: this layer has a style rather than
     * an element, so the layer that does have one resolves it -- copying channels here
     * would be inventing a second colour system.
     */
    {
        LayoutBorderRule coloured;

        LayoutBorderRuleInit(&coloured, "div", RoboCssDisplayBlock);
        LayoutBorderRuleColorAll(&coloured, CssColorMake(1, 2, 3, 4));
        style = LayoutBorderStyleBuild(&coloured);
        borders = LayoutBordersOf(style);
        RoboCheckEqInt((int)borders.top.color.kind, (int)RoboCssColorRgba);
        RoboCheckEqInt(borders.right.color.red, 1);
        RoboCheckEqInt(borders.right.color.green, 2);
        RoboCheckEqInt(borders.bottom.color.blue, 3);
        RoboCheckEqInt(borders.left.color.alpha, 4);
        RoboCheckEqInt(borders.left.color.kind, RoboCssColorRgba);
        CssComputedStyleDestroy(style);

        LayoutBorderRuleInit(&coloured, "div", RoboCssDisplayBlock);
        LayoutBorderRulePx(&coloured, "border-top-width", 3.0);
        LayoutBorderRuleBorderStyle(&coloured, "border-top-style",
                                        RoboCssBorderStyleSolid);
        LayoutBorderRuleColor(&coloured, "border-top-color", CssColorCurrent());
        style = LayoutBorderStyleBuild(&coloured);
        borders = LayoutBordersOf(style);
        RoboCheckEqInt((int)borders.top.color.kind, (int)RoboCssColorCurrent);
        RoboCheckEqInt(CssColorIsCurrent(borders.top.color), 1);
        CssComputedStyleDestroy(style);
    }

    /*
     * A document that declared nothing: four undeclared sides, `none` styles and no
     * colours. That is CSS's initial state read through this vocabulary -- and the
     * geometry must treat it as no border rather than as `medium`, which is what the
     * `ABSENT` kind is for.
     */
    {
        LayoutBorderRule bare;

        LayoutBorderRuleInit(&bare, "div", RoboCssDisplayBlock);
        style = LayoutBorderStyleBuild(&bare);
        borders = LayoutBordersOf(style);
        RoboCheckEqInt(LayoutBorderWidthIsAbsent(&borders.top.width), 1);
        RoboCheckEqInt(LayoutBorderWidthIsAbsent(&borders.right.width), 1);
        RoboCheckEqInt(LayoutBorderWidthIsAbsent(&borders.bottom.width), 1);
        RoboCheckEqInt(LayoutBorderWidthIsAbsent(&borders.left.width), 1);
        RoboCheckEqInt((int)borders.top.style, (int)RoboCssBorderStyleNone);
        RoboCheckEqInt((int)borders.top.color.kind, (int)RoboCssColorNone);
        RoboCheckEqInt(LayoutBordersEqual(&borders,
                                                    &(LayoutBorders){0}),
                          1);
        CssComputedStyleDestroy(style);
    }

    /* A NULL style makes no declarations, which is four undeclared sides rather than a
     * failure -- a box with no style has declared nothing. */
    borders = LayoutBordersOf(NULL);
    RoboCheckEqInt(LayoutBordersIsValid(&borders), 1);
    RoboCheckEqInt(LayoutBorderWidthIsAbsent(&borders.top.width), 1);
    RoboCheckEqInt(LayoutBorderWidthIsAbsent(&borders.left.width), 1);

    /*
     * Every declaration this build cannot use, one per property shape: a relative unit,
     * a percentage (which CSS does not define for a border width at all -- so unlike a
     * percentage padding, an `UNRESOLVED` width here is never a percentage this build
     * declined to resolve), a negative length, and the `auto` the grammar forbids.
     */
    {
        LayoutBorderRule bad;

        LayoutBorderRuleInit(&bad, "div", RoboCssDisplayBlock);
        LayoutBorderRuleEm(&bad, "border-top-width", 2.0);
        LayoutBorderRulePercentage(&bad, "border-right-width", 50.0);
        LayoutBorderRuleNegative(&bad, "border-bottom-width", -5.0);
        LayoutBorderRuleKeyword(&bad, "border-left-width", "auto");
        style = LayoutBorderStyleBuild(&bad);
        borders = LayoutBordersOf(style);
        RoboCheckEqInt(LayoutBorderWidthIsUnresolved(&borders.top.width), 1);
        RoboCheckEqInt(LayoutBorderWidthIsUnresolved(&borders.right.width), 1);
        RoboCheckEqInt(LayoutBorderWidthIsUnresolved(&borders.bottom.width), 1);
        RoboCheckEqInt(LayoutBorderWidthIsUnresolved(&borders.left.width), 1);
        /* A negative length is the one that has a *value* to refuse rather than a shape,
         * so its number is still visible on the classification's refusal. */
        RoboCheckEqInt(LayoutBordersIsValid(&borders), 1);
        CssComputedStyleDestroy(style);
    }

    /*
     * And a `var()`-shaped length: a px value flagged unresolved, which is the shape the
     * computed layer gives a declaration whose substitution has not happened. It reaches
     * the same `UNRESOLVED` kind for a different reason than the four above, and the
     * count is what a document depending on one is visible through.
     */
    {
        LayoutBorderRule flagged;
        LayoutBorderEdges edges;

        LayoutBorderRuleInit(&flagged, "div", RoboCssDisplayBlock);
        LayoutBorderRuleUnresolved(&flagged, "border-top-width", 4.0);
        LayoutBorderRuleBorderStyle(&flagged, "border-top-style",
                                        RoboCssBorderStyleSolid);
        style = LayoutBorderStyleBuild(&flagged);
        borders = LayoutBordersOf(style);
        RoboCheckEqInt(LayoutBorderWidthIsUnresolved(&borders.top.width), 1);
        RoboCheckEqInt(LayoutBorderResolve(&borders, &edges, NULL), RoboOk);
        RoboCheckEqInt(edges.top.width, 0);
        RoboCheckEqInt(edges.widthsUnresolved, 1u);
        RoboCheckEqInt(edges.widthsResolved, 0u);
        CssComputedStyleDestroy(style);
    }
}

/* ------------------------------------------------------------------ */
/* The used values                                                     */
/* ------------------------------------------------------------------ */

static void TestBordersEdgesRecord(void)
{
    LayoutBorders declared;
    LayoutBorderEdges edges;
    GfxPixelX widths[RoboCssBoxSideCount];
    GfxPixelX width = 0;

    LayoutFixture();

    {
        LayoutBorderEdges cleared;

        LayoutBorderEdgesInit(&cleared);
        RoboCheckEqInt(cleared.top.width, 0);
        RoboCheckEqInt(cleared.right.width, 0);
        RoboCheckEqInt(cleared.bottom.width, 0);
        RoboCheckEqInt(cleared.left.width, 0);
        RoboCheckEqInt(cleared.widthsResolved, 0u);
        RoboCheckEqInt(cleared.widthsUnresolved, 0u);
        RoboCheckEqInt(cleared.bordersDrawn, 0u);
        RoboCheckEqInt(LayoutBorderEdgesIsValid(&cleared), 1);
    }

    /*
     * The fourth resolve rule, and the phase's whole novelty, in one record: a declared
     * five whose style does not draw is a used **zero**. `right` is that side; `bottom`
     * is a declaration this build cannot use; `left` is a side nobody declared; and only
     * `top` takes room.
     */
    declared.top = LayoutBorderSideMake(LayoutBorderWidthResolved(5),
                                          RoboCssBorderStyleSolid,
                                          CssColorMake(10, 20, 30, 255));
    declared.right = LayoutBorderSideMake(LayoutBorderWidthResolved(5),
                                            RoboCssBorderStyleNone, CssColorNone());
    declared.bottom = LayoutBorderSideMake(LayoutBorderWidthUnresolved(),
                                              RoboCssBorderStyleSolid,
                                              CssColorNone());
    declared.left = LayoutBorderSideMake(LayoutBorderWidthAbsent(),
                                            RoboCssBorderStyleSolid,
                                            CssColorNone());
    RoboCheckEqInt(LayoutBordersIsValid(&declared), 1);
    RoboCheckEqInt(LayoutBorderResolve(&declared, &edges, NULL), RoboOk);

    LayoutBorderWidthsOf(&edges, widths);
    RoboCheckEqInt(widths[RoboCssBoxSideTop], 5);
    RoboCheckEqInt(widths[RoboCssBoxSideRight], 0);
    RoboCheckEqInt(widths[RoboCssBoxSideBottom], 0);
    RoboCheckEqInt(widths[RoboCssBoxSideLeft], 0);

    /*
     * And the three counters answer three questions: two sides were *declared* with a
     * width this build can use, one was declared with one it cannot, one was declared
     * with none at all, and exactly one takes room and will draw.
     */
    RoboCheckEqInt(edges.widthsResolved, 2u);
    RoboCheckEqInt(edges.widthsUnresolved, 1u);
    RoboCheckEqInt(edges.bordersDrawn, 1u);
    RoboCheckEqInt(LayoutBorderEdgesIsValid(&edges), 1);

    /* The style and the colour travel with the side whatever its used width came to, so
     * a paint pass reads one record and re-reads no declaration. */
    RoboCheckEqInt((int)edges.top.style, (int)RoboCssBorderStyleSolid);
    RoboCheckEqInt(edges.top.color.red, 10);
    RoboCheckEqInt(edges.top.color.green, 20);
    RoboCheckEqInt(edges.top.color.blue, 30);
    RoboCheckEqInt((int)edges.right.style, (int)RoboCssBorderStyleNone);
    RoboCheckEqInt((int)edges.left.style, (int)RoboCssBorderStyleSolid);

    RoboCheckEqInt(LayoutBorderEdgesWidth(&edges, RoboCssBoxSideTop, &width),
                      RoboOk);
    RoboCheckEqInt(width, 5);
    RoboCheckEqInt(LayoutBorderEdgesWidth(&edges, (CssBoxSide)77, &width),
                      RoboInvalidArgument);
    RoboCheckEqInt(LayoutBorderEdgesWidth(NULL, RoboCssBoxSideTop, &width),
                      RoboInvalidArgument);
    RoboCheckEqInt(LayoutBorderEdgesWidth(&edges, RoboCssBoxSideTop, NULL),
                      RoboInvalidArgument);

    /* Equality is every field including the counters: two records that agreed about the
     * four numbers and disagreed about how many declarations they could not use are two
     * different answers to one document. */
    RoboCheckEqInt(LayoutBorderEdgesEqual(&edges, &edges), 1);
    {
        LayoutBorderEdges same = edges;

        RoboCheckEqInt(LayoutBorderEdgesEqual(&edges, &same), 1);
        same.bordersDrawn = 0u;
        RoboCheckEqInt(LayoutBorderEdgesEqual(&edges, &same), 0);
        same = edges;
        same.right.color = CssColorMake(1, 2, 3, 255);
        RoboCheckEqInt(LayoutBorderEdgesEqual(&edges, &same), 0);
        same = edges;
        same.top.style = RoboCssBorderStyleDashed;
        RoboCheckEqInt(LayoutBorderEdgesEqual(&edges, &same), 0);
    }

    /*
     * And the invariant a hand-built record cannot smuggle past: a used width above zero
     * **implies** the style draws. A record that violated it would be a border geometry
     * charged for and paint drew as nothing.
     */
    {
        LayoutBorderEdges bad = LayoutBorderUsed(5, 0, 0, 0);

        RoboCheckEqInt(LayoutBorderEdgesIsValid(&bad), 1);
        bad.top.width = 5;
        bad.top.style = RoboCssBorderStyleNone;
        RoboCheckEqInt(LayoutBorderEdgesIsValid(&bad), 0);

        bad = LayoutBorderUsed(5, 0, 0, 0);
        bad.left.width = -1;
        RoboCheckEqInt(LayoutBorderEdgesIsValid(&bad), 0);

        bad = LayoutBorderUsed(5, 5, 5, 5);
        bad.widthsResolved = 5u;
        RoboCheckEqInt(LayoutBorderEdgesIsValid(&bad), 0);

        bad = LayoutBorderUsed(5, 5, 5, 5);
        bad.bordersDrawn = 5u;
        RoboCheckEqInt(LayoutBorderEdgesIsValid(&bad), 0);

        bad = LayoutBorderUsed(5, 5, 5, 5);
        bad.widthsResolved = 3u;
        RoboCheckEqInt(LayoutBorderEdgesIsValid(&bad), 0);

        RoboCheckEqInt(LayoutBorderEdgesIsValid(NULL), 0);
    }

    RoboCheckEqInt(LayoutBorderResolve(NULL, &edges, NULL), RoboInvalidArgument);
    RoboCheckEqInt(LayoutBorderResolve(&declared, NULL, NULL), RoboInvalidArgument);
}

/* ------------------------------------------------------------------ */
/* The two derived numbers                                             */
/* ------------------------------------------------------------------ */

static void TestBordersDerivedBoxes(void)
{
    LayoutBorderEdges edges = LayoutBorderUsed(5, 10, 15, 20);
    LayoutSize paddingBox;
    LayoutSize borderBox;

    paddingBox.width = 0;
    paddingBox.height = 0;
    borderBox.width = 0;
    borderBox.height = 0;
    LayoutDiag diag = RoboLayoutCleared;
    int32_t available = 0;

    LayoutFixture();

    /*
     * Downward: the containing block less the two horizontal borders, which is the
     * number 10.10's own content-width door is then handed. A box whose borders are 20
     * on the left and 10 on the right has 770 of padding box inside an 800-wide
     * containing block -- not 800, and not 770 minus anything else.
     */
    RoboCheckEqInt(LayoutBorderEdgesPaddingBoxWidth(&edges, 800, &available,
                                                                &diag),
                      RoboOk);
    RoboCheckEqInt(available, 770);

    /* A containing block narrower than the box's own borders leaves **no** room rather
     * than a negative width, and the floor is on the result. */
    RoboCheckEqInt(LayoutBorderEdgesPaddingBoxWidth(&edges, 12, &available, &diag),
                      RoboOk);
    RoboCheckEqInt(available, 0);

    RoboCheckEqInt(LayoutBorderEdgesPaddingBoxWidth(&edges, 40, &available, &diag),
                      RoboOk);
    RoboCheckEqInt(available, 10);

    /* A record no size can be derived from is refused *before* a size is derived from it,
     * with a diagnostic rather than a floor. */
    {
        LayoutBorderEdges bad = edges;
        Result status;
        int32_t untouched = 4242;

        bad.left.width = -3;
        status = LayoutBorderEdgesPaddingBoxWidth(&bad, 800, &untouched, &diag);
        RoboCheckEqInt(status, RoboError);
        RoboCheckEqInt(untouched, 4242);
        RoboCheckEqInt((int)diag.domain, (int)RoboLayoutDomainBorder);
        RoboCheckEqInt((int)diag.code, (int)RoboError);

        LayoutDiagReset(&diag);
        status = LayoutBorderEdgesBorderBox(&bad, paddingBox, &borderBox, &diag);
        RoboCheckEqInt(status, RoboError);
        RoboCheckEqInt((int)diag.domain, (int)RoboLayoutDomainBorder);
    }

    /* Upward: the padding box plus the four sides, one addition per axis, which is the
     * last number before a result. */
    paddingBox.width = 200;
    paddingBox.height = 100;
    RoboCheckEqInt(LayoutBorderEdgesBorderBox(&edges, paddingBox, &borderBox,
                                                         &diag),
                      RoboOk);
    RoboCheckEqInt(borderBox.width, 200 + 20 + 10);
    RoboCheckEqInt(borderBox.height, 100 + 5 + 15);

    /* A padding box that is itself zero: the border box is the border alone, which is
     * the shape an `inline-block` with no contents has. */
    paddingBox.width = 0;
    paddingBox.height = 0;
    RoboCheckEqInt(LayoutBorderEdgesBorderBox(&edges, paddingBox, &borderBox,
                                                         &diag),
                      RoboOk);
    RoboCheckEqInt(borderBox.width, 30);
    RoboCheckEqInt(borderBox.height, 20);

    /*
     * And the saturation, which is the arithmetic finding of this layer: a padding box at
     * the seam's maximum is a number whose *sum* with a border is not representable, so it
     * clamps rather than wrapping into a negative rectangle. The two clamps compose with
     * 10.10's because each saturates at the same bound.
     */
    paddingBox.width = 2147483647;
    paddingBox.height = 2147483647;
    RoboCheckEqInt(LayoutBorderEdgesBorderBox(&edges, paddingBox, &borderBox,
                                                         &diag),
                      RoboOk);
    RoboCheckEqInt(borderBox.width, 2147483647);
    RoboCheckEqInt(borderBox.height, 2147483647);

    /* A negative padding box is a caller bug rather than a document: refused, with the
     * output untouched. */
    {
        LayoutSize negative;
        LayoutSize untouched;

        negative.width = -1;
        negative.height = 10;
        untouched.width = 4242;
        untouched.height = 4242;
        RoboCheckEqInt(LayoutBorderEdgesBorderBox(&edges, negative, &untouched,
                                                             &diag),
                          RoboInvalidArgument);
        RoboCheckEqInt(untouched.width, 4242);
    }

    /* NULLs, both doors. */
    RoboCheckEqInt(LayoutBorderEdgesPaddingBoxWidth(NULL, 800, &available, &diag),
                      RoboInvalidArgument);
    RoboCheckEqInt(LayoutBorderEdgesPaddingBoxWidth(&edges, 800, NULL, &diag),
                      RoboInvalidArgument);
    RoboCheckEqInt(LayoutBorderEdgesBorderBox(NULL, paddingBox, &borderBox,
                                                         &diag),
                      RoboInvalidArgument);
    RoboCheckEqInt(LayoutBorderEdgesBorderBox(&edges, paddingBox, NULL, &diag),
                      RoboInvalidArgument);
}

/* ------------------------------------------------------------------ */
/* The block pass                                                      */
/* ------------------------------------------------------------------ */

static void TestBordersBlockDeclaredWidth(void)
{
    LayoutBorderFixture fixture;
    LayoutBorderRule htmlRule;
    LayoutBorderRule bodyRule;
    LayoutBorderRule divRule;
    LayoutBorderRule rules[3];
    LayoutStats stats;
    LayoutPoint origin;
    LayoutSize size;
    LayoutBorderEdges edges;
    LayoutPaddingEdges padding;
    LayoutNodeId divId;

    LayoutFixture();

    /*
     * A declared width is the **content** box under this build's `content-box` model, so
     * the box a caller reads is that plus the two horizontal borders: 200 declared, 5 and
     * 5 declared, 210 on screen. The `position` is the *border* box's origin, and a
     * border does not move a box -- it grows it -- so it is unchanged.
     */
    LayoutBorderRuleInit(&htmlRule, "html", RoboCssDisplayBlock);
    LayoutBorderRuleInit(&bodyRule, "body", RoboCssDisplayBlock);
    LayoutBorderRuleInit(&divRule, "div", RoboCssDisplayBlock);
    LayoutBorderRulePx(&divRule, "width", 200.0);
    LayoutBorderRulePx(&divRule, "height", 50.0);
    LayoutBorderRulePx(&divRule, "border-left-width", 5.0);
    LayoutBorderRulePx(&divRule, "border-right-width", 5.0);
    LayoutBorderRuleStyleAll(&divRule, RoboCssBorderStyleSolid);
    rules[0] = htmlRule;
    rules[1] = bodyRule;
    rules[2] = divRule;

    LayoutBorderBegin(&fixture, "<html><body><div></div></body></html>", rules, 3u);
    LayoutBorderPass(&fixture, LAYOUT_BORDER_VIEWPORT_WIDTH, &stats);

    divId = LayoutBorderBoxFor(&fixture, "div");
    origin = LayoutBorderOriginOf(&fixture, divId);
    size = LayoutBorderSizeOf(&fixture, divId);
    RoboCheckEqInt(origin.x, 0);
    RoboCheckEqInt(origin.y, 0);
    RoboCheckEqInt(size.width, 210);
    RoboCheckEqInt(size.height, 50);

    /* And the record a paint pass reads, and the two insets that compose a content
     * origin: the border first, then the padding (none here). */
    edges = LayoutBorderEdgesOf(&fixture, divId);
    RoboCheckEqInt(edges.left.width, 5);
    RoboCheckEqInt(edges.right.width, 5);
    RoboCheckEqInt(edges.top.width, 0);
    RoboCheckEqInt(edges.widthsResolved, 2u);
    RoboCheckEqInt(edges.bordersDrawn, 2u);
    RoboCheckEqInt((int)edges.left.style, (int)RoboCssBorderStyleSolid);

    RoboCheckEqInt(LayoutObjectPadding(fixture.tree, divId, &padding, NULL), RoboOk);
    RoboCheckEqInt(padding.left, 0);
    RoboCheckEqInt(padding.top, 0);

    RoboCheckEqInt(stats.block.bordersApplied, 2u);
    RoboCheckEqInt(stats.block.bordersUnresolved, 0u);
    RoboCheckEqInt(stats.block.bordersDrawn, 2u);

    LayoutBorderEnd(&fixture);
}

static void TestBordersBlockAutoWidth(void)
{
    LayoutBorderFixture fixture;
    LayoutBorderRule htmlRule;
    LayoutBorderRule bodyRule;
    LayoutBorderRule divRule;
    LayoutBorderRule rules[3];
    LayoutSize size;

    LayoutFixture();

    /*
     * CSS's constraint equation has a border term on each side, so a box with
     * `border: 10px solid` and **no declared width** is exactly as wide as its containing
     * block rather than 20 wider. A build that grew the box outward would answer 820
     * here, and this is the case that fails it.
     */
    LayoutBorderRuleInit(&htmlRule, "html", RoboCssDisplayBlock);
    LayoutBorderRuleInit(&bodyRule, "body", RoboCssDisplayBlock);
    LayoutBorderRuleInit(&divRule, "div", RoboCssDisplayBlock);
    LayoutBorderRulePx(&divRule, "height", 20.0);
    LayoutBorderRulePx(&divRule, "border-left-width", 10.0);
    LayoutBorderRulePx(&divRule, "border-right-width", 10.0);
    LayoutBorderRuleStyleAll(&divRule, RoboCssBorderStyleSolid);
    rules[0] = htmlRule;
    rules[1] = bodyRule;
    rules[2] = divRule;

    LayoutBorderBegin(&fixture, "<html><body><div></div></body></html>", rules, 3u);
    LayoutBorderPass(&fixture, LAYOUT_BORDER_VIEWPORT_WIDTH, NULL);

    size = LayoutBorderSizeOf(&fixture, LayoutBorderBoxFor(&fixture, "div"));
    RoboCheckEqInt(size.width, LAYOUT_BORDER_VIEWPORT_WIDTH);
    RoboCheckEqInt(size.height, 20);

    LayoutBorderEnd(&fixture);

    /* The contrast: the same box with no border is also 800 wide, so "it fills its
     * containing block" is a fact about both rather than about the border. */
    {
        LayoutBorderFixture second;
        LayoutBorderRule bare;

        LayoutFixture();
        LayoutBorderRuleInit(&bare, "div", RoboCssDisplayBlock);
        LayoutBorderRulePx(&bare, "height", 20.0);
        LayoutBorderBegin(&second, "<html><body><div></div></body></html>",
                            (LayoutBorderRuleTable[]){htmlRule, bodyRule, bare}, 3u);
        LayoutBorderPass(&second, LAYOUT_BORDER_VIEWPORT_WIDTH, NULL);
        size = LayoutBorderSizeOf(&second, LayoutBorderBoxFor(&second, "div"));
        RoboCheckEqInt(size.width, LAYOUT_BORDER_VIEWPORT_WIDTH);
        LayoutBorderEnd(&second);
    }
}

static void TestBordersBlockSides(void)
{
    LayoutBorderFixture fixture;
    LayoutBorderRule htmlRule;
    LayoutBorderRule bodyRule;
    LayoutBorderRule divRule;
    LayoutBorderRule rules[3];
    LayoutBorderEdges edges;
    LayoutSize size;
    GfxPixelX widths[RoboCssBoxSideCount];
    LayoutNodeId divId;

    LayoutFixture();

    /*
     * Four different sides at once, so that each contributes its own term and none is
     * silently read from another: the two horizontal ones come out of the width (which is
     * `auto`, so the box still fills its containing block exactly), and the two vertical
     * ones grow the height.
     */
    LayoutBorderRuleInit(&htmlRule, "html", RoboCssDisplayBlock);
    LayoutBorderRuleInit(&bodyRule, "body", RoboCssDisplayBlock);
    LayoutBorderRuleInit(&divRule, "div", RoboCssDisplayBlock);
    LayoutBorderRulePx(&divRule, "height", 10.0);
    LayoutBorderRulePx(&divRule, "border-top-width", 3.0);
    LayoutBorderRulePx(&divRule, "border-right-width", 4.0);
    LayoutBorderRulePx(&divRule, "border-bottom-width", 5.0);
    LayoutBorderRulePx(&divRule, "border-left-width", 6.0);
    LayoutBorderRuleStyleAll(&divRule, RoboCssBorderStyleSolid);
    rules[0] = htmlRule;
    rules[1] = bodyRule;
    rules[2] = divRule;

    LayoutBorderBegin(&fixture, "<html><body><div></div></body></html>", rules, 3u);
    LayoutBorderPass(&fixture, LAYOUT_BORDER_VIEWPORT_WIDTH, NULL);

    divId = LayoutBorderBoxFor(&fixture, "div");
    size = LayoutBorderSizeOf(&fixture, divId);
    /* The containing block exactly, even though 10 of its 800 is border: 800 - 4 - 6 =
     * 790 of content, plus the 10 back = 800. */
    RoboCheckEqInt(size.width, LAYOUT_BORDER_VIEWPORT_WIDTH);
    RoboCheckEqInt(size.height, 10 + 3 + 5);

    edges = LayoutBorderEdgesOf(&fixture, divId);
    LayoutBorderWidthsOf(&edges, widths);
    RoboCheckEqInt(widths[RoboCssBoxSideTop], 3);
    RoboCheckEqInt(widths[RoboCssBoxSideRight], 4);
    RoboCheckEqInt(widths[RoboCssBoxSideBottom], 5);
    RoboCheckEqInt(widths[RoboCssBoxSideLeft], 6);
    RoboCheckEqInt(edges.bordersDrawn, 4u);

    LayoutBorderEnd(&fixture);
}

static void TestBordersBlockVertical(void)
{
    LayoutBorderFixture fixture;
    LayoutBorderRule htmlRule;
    LayoutBorderRule bodyRule;
    LayoutBorderRule divRule;
    LayoutBorderRule iRule;
    LayoutBorderRule rules[4];
    LayoutSize size;
    LayoutPoint origin;

    LayoutFixture();

    /*
     * A border is inside the box, so `border-top: 15px` moves the contents **down**
     * without moving the box, and `border-bottom: 25px` grows the box below them. The
     * box's own origin is unchanged -- that is what makes a border a border rather than a
     * margin, and it is the assertion 10.9's suite makes the other way round.
     */
    LayoutBorderRuleInit(&htmlRule, "html", RoboCssDisplayBlock);
    LayoutBorderRuleInit(&bodyRule, "body", RoboCssDisplayBlock);
    LayoutBorderRuleInit(&divRule, "div", RoboCssDisplayBlock);
    LayoutBorderRulePx(&divRule, "border-top-width", 15.0);
    LayoutBorderRulePx(&divRule, "border-bottom-width", 25.0);
    LayoutBorderRuleStyleAll(&divRule, RoboCssBorderStyleSolid);
    LayoutBorderRuleInit(&iRule, "i", RoboCssDisplayBlock);
    LayoutBorderRulePx(&iRule, "height", 10.0);
    rules[0] = htmlRule;
    rules[1] = bodyRule;
    rules[2] = divRule;
    rules[3] = iRule;

    LayoutBorderBegin(&fixture, "<html><body><div><i></i></div></body></html>", rules, 4u);
    LayoutBorderPass(&fixture, LAYOUT_BORDER_VIEWPORT_WIDTH, NULL);

    origin = LayoutBorderOriginOf(&fixture, LayoutBorderBoxFor(&fixture, "div"));
    RoboCheckEqInt(origin.x, 0);
    RoboCheckEqInt(origin.y, 0);

    /* The child is positioned relative to its containing block's **content** origin, so
     * it is at 0 whatever the border-top is -- the border is accounted for by the parent's
     * content origin, not by shifting the child. */
    origin = LayoutBorderOriginOf(&fixture, LayoutBorderBoxFor(&fixture, "i"));
    RoboCheckEqInt(origin.x, 0);
    RoboCheckEqInt(origin.y, 0);

    size = LayoutBorderSizeOf(&fixture, LayoutBorderBoxFor(&fixture, "div"));
    RoboCheckEqInt(size.height, 10 + 15 + 25);
    size = LayoutBorderSizeOf(&fixture, LayoutBorderBoxFor(&fixture, "i"));
    RoboCheckEqInt(size.height, 10);

    LayoutBorderEnd(&fixture);
}

static void TestBordersBlockStacking(void)
{
    LayoutBorderFixture fixture;
    LayoutBorderRule htmlRule;
    LayoutBorderRule bodyRule;
    LayoutBorderRule divRule;
    LayoutBorderRule iRule;
    LayoutBorderRule bRule;
    LayoutBorderRule rules[5];
    LayoutSize size;
    LayoutPoint origin;

    LayoutFixture();

    /*
     * What a parent stacks by is the child's **border** box, and the margin it declared is
     * still *outside* it: `i` is 20 tall (10 declared plus 5 and 5 of border), `b` declares
     * a 4px top margin and 6px of border-top, so its origin is 24.
     */
    LayoutBorderRuleInit(&htmlRule, "html", RoboCssDisplayBlock);
    LayoutBorderRuleInit(&bodyRule, "body", RoboCssDisplayBlock);
    LayoutBorderRuleInit(&divRule, "div", RoboCssDisplayBlock);
    LayoutBorderRuleInit(&iRule, "i", RoboCssDisplayBlock);
    LayoutBorderRulePx(&iRule, "height", 10.0);
    LayoutBorderRulePx(&iRule, "border-top-width", 5.0);
    LayoutBorderRulePx(&iRule, "border-bottom-width", 5.0);
    LayoutBorderRuleStyleAll(&iRule, RoboCssBorderStyleSolid);
    LayoutBorderRuleInit(&bRule, "b", RoboCssDisplayBlock);
    LayoutBorderRulePx(&bRule, "height", 10.0);
    LayoutBorderRulePx(&bRule, "margin-top", 4.0);
    LayoutBorderRulePx(&bRule, "border-top-width", 6.0);
    LayoutBorderRuleStyleAll(&bRule, RoboCssBorderStyleSolid);
    rules[0] = htmlRule;
    rules[1] = bodyRule;
    rules[2] = divRule;
    rules[3] = iRule;
    rules[4] = bRule;

    LayoutBorderBegin(&fixture, "<html><body><div><i></i><b></b></div></body></html>", rules,
                        5u);
    LayoutBorderPass(&fixture, LAYOUT_BORDER_VIEWPORT_WIDTH, NULL);

    origin = LayoutBorderOriginOf(&fixture, LayoutBorderBoxFor(&fixture, "i"));
    RoboCheckEqInt(origin.y, 0);
    size = LayoutBorderSizeOf(&fixture, LayoutBorderBoxFor(&fixture, "i"));
    RoboCheckEqInt(size.height, 20);

    origin = LayoutBorderOriginOf(&fixture, LayoutBorderBoxFor(&fixture, "b"));
    RoboCheckEqInt(origin.y, 24);
    size = LayoutBorderSizeOf(&fixture, LayoutBorderBoxFor(&fixture, "b"));
    RoboCheckEqInt(size.height, 16);

    LayoutBorderEnd(&fixture);
}

static void TestBordersBlockUnresolved(void)
{
    LayoutBorderFixture fixture;
    LayoutBorderRule htmlRule;
    LayoutBorderRule bodyRule;
    LayoutBorderRule divRule;
    LayoutBorderRule rules[3];
    LayoutStats stats;
    LayoutBorderEdges edges;
    LayoutSize size;
    LayoutNodeId divId;

    LayoutFixture();

    /*
     * A border this build cannot use takes no room, and is *counted* so that a document
     * whose geometry depends on one is visible as such rather than merely flush: the box
     * is 800 wide exactly as a box with no border is, and the pass says two declarations
     * were refused.
     */
    LayoutBorderRuleInit(&htmlRule, "html", RoboCssDisplayBlock);
    LayoutBorderRuleInit(&bodyRule, "body", RoboCssDisplayBlock);
    LayoutBorderRuleInit(&divRule, "div", RoboCssDisplayBlock);
    LayoutBorderRulePx(&divRule, "height", 20.0);
    LayoutBorderRuleEm(&divRule, "border-left-width", 2.0);
    LayoutBorderRulePercentage(&divRule, "border-right-width", 50.0);
    LayoutBorderRuleStyleAll(&divRule, RoboCssBorderStyleSolid);
    rules[0] = htmlRule;
    rules[1] = bodyRule;
    rules[2] = divRule;

    LayoutBorderBegin(&fixture, "<html><body><div></div></body></html>", rules, 3u);
    LayoutBorderPass(&fixture, LAYOUT_BORDER_VIEWPORT_WIDTH, &stats);

    divId = LayoutBorderBoxFor(&fixture, "div");
    size = LayoutBorderSizeOf(&fixture, divId);
    RoboCheckEqInt(size.width, LAYOUT_BORDER_VIEWPORT_WIDTH);

    edges = LayoutBorderEdgesOf(&fixture, divId);
    RoboCheckEqInt(edges.left.width, 0);
    RoboCheckEqInt(edges.right.width, 0);
    RoboCheckEqInt(edges.widthsUnresolved, 2u);
    RoboCheckEqInt(edges.bordersDrawn, 0u);

    RoboCheckEqInt(stats.block.bordersApplied, 0u);
    RoboCheckEqInt(stats.block.bordersUnresolved, 2u);
    RoboCheckEqInt(stats.block.bordersDrawn, 0u);

    LayoutBorderEnd(&fixture);
}

static void TestBordersNoneStyle(void)
{
    LayoutBorderFixture fixture;
    LayoutBorderRule htmlRule;
    LayoutBorderRule bodyRule;
    LayoutBorderRule divRule;
    LayoutBorderRule rules[3];
    LayoutStats stats;
    LayoutBorderEdges edges;
    LayoutSize size;
    LayoutNodeId divId;

    LayoutFixture();

    /*
     * The phase's one new rule, and the most plausible way to get it wrong: twelve
     * declarations reached this layer and **not one of them takes room**, because
     * `border-style: none` gives every side a used width of zero. A build that used the
     * declared widths would answer 110 by 20 here.
     */
    LayoutBorderRuleInit(&htmlRule, "html", RoboCssDisplayBlock);
    LayoutBorderRuleInit(&bodyRule, "body", RoboCssDisplayBlock);
    LayoutBorderRuleInit(&divRule, "div", RoboCssDisplayBlock);
    LayoutBorderRulePx(&divRule, "width", 100.0);
    LayoutBorderRulePx(&divRule, "height", 10.0);
    LayoutBorderRuleWidthAll(&divRule, 5.0);
    LayoutBorderRuleStyleAll(&divRule, RoboCssBorderStyleNone);
    rules[0] = htmlRule;
    rules[1] = bodyRule;
    rules[2] = divRule;

    LayoutBorderBegin(&fixture, "<html><body><div></div></body></html>", rules, 3u);
    LayoutBorderPass(&fixture, LAYOUT_BORDER_VIEWPORT_WIDTH, &stats);

    divId = LayoutBorderBoxFor(&fixture, "div");
    size = LayoutBorderSizeOf(&fixture, divId);
    RoboCheckEqInt(size.width, 100);
    RoboCheckEqInt(size.height, 10);

    edges = LayoutBorderEdgesOf(&fixture, divId);
    RoboCheckEqInt(edges.widthsResolved, 4u);
    RoboCheckEqInt(edges.widthsUnresolved, 0u);
    RoboCheckEqInt(edges.bordersDrawn, 0u);
    RoboCheckEqInt(edges.top.width, 0);
    RoboCheckEqInt(edges.left.width, 0);
    /* The *style* still travels with the side, so a paint pass reads `none` rather than
     * inferring it from a zero width. */
    RoboCheckEqInt((int)edges.left.style, (int)RoboCssBorderStyleNone);

    /* Four declarations resolved and none drawn: the pair a single counter could not
     * report. */
    RoboCheckEqInt(stats.block.bordersApplied, 4u);
    RoboCheckEqInt(stats.block.bordersDrawn, 0u);

    LayoutBorderEnd(&fixture);

    /* And the contrast in one number: the same declarations with `solid` are 110 by 20. */
    {
        LayoutBorderFixture second;
        LayoutBorderRule solid;

        LayoutFixture();
        LayoutBorderRuleInit(&solid, "div", RoboCssDisplayBlock);
        LayoutBorderRulePx(&solid, "width", 100.0);
        LayoutBorderRulePx(&solid, "height", 10.0);
        LayoutBorderRuleShorthand(&solid, 5.0);
        LayoutBorderBegin(&second, "<html><body><div></div></body></html>",
                            (LayoutBorderRuleTable[]){htmlRule, bodyRule, solid}, 3u);
        LayoutBorderPass(&second, LAYOUT_BORDER_VIEWPORT_WIDTH, NULL);
        size = LayoutBorderSizeOf(&second, LayoutBorderBoxFor(&second, "div"));
        RoboCheckEqInt(size.width, 110);
        RoboCheckEqInt(size.height, 20);
        LayoutBorderEnd(&second);
    }

    /*
     * `hidden` is the *second* style that draws nothing, so it takes the same geometry
     * answer as `none` -- and the distinction is kept as a **value** rather than lost:
     * CSS's difference between the two is a border-collapsing rule for tables, and the
     * record a paint pass reads still says which of the two an author wrote. A build that
     * reported `none` for a `hidden` side would have thrown away the one fact that will
     * matter the day tables arrive.
     */
    {
        LayoutBorderFixture third;
        LayoutBorderRule hidden;

        LayoutFixture();
        LayoutBorderRuleInit(&hidden, "div", RoboCssDisplayBlock);
        LayoutBorderRulePx(&hidden, "width", 100.0);
        LayoutBorderRulePx(&hidden, "height", 10.0);
        LayoutBorderRuleShorthand(&hidden, 5.0);
        LayoutBorderRuleStyleAll(&hidden, RoboCssBorderStyleHidden);
        LayoutBorderBegin(&third, "<html><body><div></div></body></html>",
                            (LayoutBorderRuleTable[]){htmlRule, bodyRule, hidden}, 3u);
        LayoutBorderPass(&third, LAYOUT_BORDER_VIEWPORT_WIDTH, NULL);
        size = LayoutBorderSizeOf(&third, LayoutBorderBoxFor(&third, "div"));
        RoboCheckEqInt(size.width, 100);
        RoboCheckEqInt(size.height, 10);
        edges = LayoutBorderEdgesOf(&third, LayoutBorderBoxFor(&third, "div"));
        RoboCheckEqInt((int)edges.left.style, (int)RoboCssBorderStyleHidden);
        RoboCheckEqInt(edges.widthsResolved, 4u);
        RoboCheckEqInt(edges.bordersDrawn, 0u);
        LayoutBorderEnd(&third);
    }
}

static void TestBordersMarginInteraction(void)
{
    LayoutBorderFixture fixture;
    LayoutBorderRule htmlRule;
    LayoutBorderRule bodyRule;
    LayoutBorderRule divRule;
    LayoutBorderRule iRule;
    LayoutBorderRule rules[4];
    LayoutPoint origin;
    LayoutSize size;

    LayoutFixture();

    /*
     * Margin outside, border and padding inside, in one box: `margin: 10px`,
     * `border: 5px solid`, `padding: 5px`, `width: 100px`. The origin is the margin's
     * (10, 10), the size is the *border* box (120 by 40), and what the parent stacks by is
     * that size plus the two margins (60). Three numbers, three different questions, and a
     * build that conflated any two of them would answer one of them wrong here.
     */
    LayoutBorderRuleInit(&htmlRule, "html", RoboCssDisplayBlock);
    LayoutBorderRuleInit(&bodyRule, "body", RoboCssDisplayBlock);
    LayoutBorderRuleInit(&divRule, "div", RoboCssDisplayBlock);
    LayoutBorderRulePx(&divRule, "width", 100.0);
    LayoutBorderRulePx(&divRule, "height", 20.0);
    LayoutBorderRulePx(&divRule, "margin-top", 10.0);
    LayoutBorderRulePx(&divRule, "margin-right", 10.0);
    LayoutBorderRulePx(&divRule, "margin-bottom", 10.0);
    LayoutBorderRulePx(&divRule, "margin-left", 10.0);
    LayoutBorderRuleShorthand(&divRule, 5.0);
    LayoutBorderRulePx(&divRule, "padding-top", 5.0);
    LayoutBorderRulePx(&divRule, "padding-right", 5.0);
    LayoutBorderRulePx(&divRule, "padding-bottom", 5.0);
    LayoutBorderRulePx(&divRule, "padding-left", 5.0);
    LayoutBorderRuleInit(&iRule, "i", RoboCssDisplayBlock);
    LayoutBorderRulePx(&iRule, "height", 3.0);
    rules[0] = htmlRule;
    rules[1] = bodyRule;
    rules[2] = divRule;
    rules[3] = iRule;

    LayoutBorderBegin(&fixture, "<html><body><div></div><i></i></body></html>", rules, 4u);
    LayoutBorderPass(&fixture, LAYOUT_BORDER_VIEWPORT_WIDTH, NULL);

    origin = LayoutBorderOriginOf(&fixture, LayoutBorderBoxFor(&fixture, "div"));
    RoboCheckEqInt(origin.x, 10);
    RoboCheckEqInt(origin.y, 10);
    size = LayoutBorderSizeOf(&fixture, LayoutBorderBoxFor(&fixture, "div"));
    RoboCheckEqInt(size.width, 100 + 10 + 10);
    RoboCheckEqInt(size.height, 20 + 10 + 10);

    /* And the following sibling starts below the margin box: 40 of border box plus the
     * 10 below. */
    origin = LayoutBorderOriginOf(&fixture, LayoutBorderBoxFor(&fixture, "i"));
    RoboCheckEqInt(origin.y, 60);

    LayoutBorderEnd(&fixture);

    /* One isolation, so the terms cannot cancel: a border with no margin moves nothing,
     * and a margin with no border changes no size. */
    {
        LayoutBorderFixture second;
        LayoutBorderRule bordered;

        LayoutFixture();
        LayoutBorderRuleInit(&bordered, "div", RoboCssDisplayBlock);
        LayoutBorderRulePx(&bordered, "width", 100.0);
        LayoutBorderRulePx(&bordered, "height", 20.0);
        LayoutBorderRuleShorthand(&bordered, 5.0);
        LayoutBorderBegin(&second, "<html><body><div></div><i></i></body></html>",
                            (LayoutBorderRuleTable[]){htmlRule, bodyRule, bordered,
                                                     iRule},
                            4u);
        LayoutBorderPass(&second, LAYOUT_BORDER_VIEWPORT_WIDTH, NULL);
        origin = LayoutBorderOriginOf(&second, LayoutBorderBoxFor(&second, "div"));
        RoboCheckEqInt(origin.x, 0);
        RoboCheckEqInt(origin.y, 0);
        origin = LayoutBorderOriginOf(&second, LayoutBorderBoxFor(&second, "i"));
        RoboCheckEqInt(origin.y, 30);
        LayoutBorderEnd(&second);
    }
}

static void TestBordersPaddingInteraction(void)
{
    LayoutBorderFixture fixture;
    LayoutBorderRule htmlRule;
    LayoutBorderRule bodyRule;
    LayoutBorderRule divRule;
    LayoutBorderRule rules[3];
    LayoutBorderEdges borders;
    LayoutPaddingEdges padding;
    LayoutSize size;
    LayoutNodeId divId;

    LayoutFixture();

    /*
     * The order the box is built in, and the assertion that fails a build which placed the
     * contents at `padding-left` and forgot the border: `border-left: 5px` with
     * `padding-left: 10px` puts the contents 15 inside, not 10 and not 5. Both records are
     * on the object, and they compose in one order -- border first, then padding.
     */
    LayoutBorderRuleInit(&htmlRule, "html", RoboCssDisplayBlock);
    LayoutBorderRuleInit(&bodyRule, "body", RoboCssDisplayBlock);
    LayoutBorderRuleInit(&divRule, "div", RoboCssDisplayBlock);
    LayoutBorderRulePx(&divRule, "width", 100.0);
    LayoutBorderRulePx(&divRule, "height", 10.0);
    LayoutBorderRulePx(&divRule, "border-left-width", 5.0);
    LayoutBorderRulePx(&divRule, "border-top-width", 7.0);
    LayoutBorderRuleStyleAll(&divRule, RoboCssBorderStyleSolid);
    LayoutBorderRulePx(&divRule, "padding-left", 10.0);
    LayoutBorderRulePx(&divRule, "padding-top", 3.0);
    rules[0] = htmlRule;
    rules[1] = bodyRule;
    rules[2] = divRule;

    LayoutBorderBegin(&fixture, "<html><body><div></div></body></html>", rules, 3u);
    LayoutBorderPass(&fixture, LAYOUT_BORDER_VIEWPORT_WIDTH, NULL);

    divId = LayoutBorderBoxFor(&fixture, "div");
    borders = LayoutBorderEdgesOf(&fixture, divId);
    RoboCheckEqInt(LayoutObjectPadding(fixture.tree, divId, &padding, NULL),
                      RoboOk);
    RoboCheckEqInt(borders.left.width, 5);
    RoboCheckEqInt(padding.left, 10);
    RoboCheckEqInt(borders.left.width + padding.left, 15);
    RoboCheckEqInt(borders.top.width, 7);
    RoboCheckEqInt(padding.top, 3);
    RoboCheckEqInt(borders.top.width + padding.top, 10);

    /* And the size is the composition of both, one addition per layer: 100 + 10 of
     * padding + 5 of border. */
    size = LayoutBorderSizeOf(&fixture, divId);
    RoboCheckEqInt(size.width, 115);
    RoboCheckEqInt(size.height, 10 + 3 + 7);

    LayoutBorderEnd(&fixture);
}

static void TestBordersChildrenGetContentWidth(void)
{
    LayoutBorderFixture fixture;
    LayoutBorderRule htmlRule;
    LayoutBorderRule bodyRule;
    LayoutBorderRule divRule;
    LayoutBorderRule sectionRule;
    LayoutBorderRule pRule;
    LayoutBorderRule rules[5];
    LayoutSize size;

    LayoutFixture();

    /*
     * What a box's children are laid out into is its **content** width, so 20 of border on
     * each side of an 800-wide containing block leave 760 for a child that declares no
     * width of its own -- and the border is charged twice down the tree, once at each
     * level.
     */
    LayoutBorderRuleInit(&htmlRule, "html", RoboCssDisplayBlock);
    LayoutBorderRuleInit(&bodyRule, "body", RoboCssDisplayBlock);
    LayoutBorderRuleInit(&divRule, "div", RoboCssDisplayBlock);
    LayoutBorderRulePx(&divRule, "border-left-width", 20.0);
    LayoutBorderRulePx(&divRule, "border-right-width", 20.0);
    LayoutBorderRuleStyleAll(&divRule, RoboCssBorderStyleSolid);
    LayoutBorderRuleInit(&sectionRule, "section", RoboCssDisplayBlock);
    LayoutBorderRulePx(&sectionRule, "border-left-width", 10.0);
    LayoutBorderRuleStyleAll(&sectionRule, RoboCssBorderStyleSolid);
    LayoutBorderRuleInit(&pRule, "p", RoboCssDisplayBlock);
    LayoutBorderRulePx(&pRule, "height", 5.0);
    rules[0] = htmlRule;
    rules[1] = bodyRule;
    rules[2] = divRule;
    rules[3] = sectionRule;
    rules[4] = pRule;

    LayoutBorderBegin(&fixture,
                        "<html><body><div><section><p></p></section></div></body></html>", rules,
                        5u);
    LayoutBorderPass(&fixture, LAYOUT_BORDER_VIEWPORT_WIDTH, NULL);

    size = LayoutBorderSizeOf(&fixture, LayoutBorderBoxFor(&fixture, "div"));
    RoboCheckEqInt(size.width, 800);

    /* 800 - 40 = 760 of content for the section, which then puts 10 of its own border
     * inside that, so the section is 760 wide and the paragraph is 750. */
    size = LayoutBorderSizeOf(&fixture, LayoutBorderBoxFor(&fixture, "section"));
    RoboCheckEqInt(size.width, 760);
    size = LayoutBorderSizeOf(&fixture, LayoutBorderBoxFor(&fixture, "p"));
    RoboCheckEqInt(size.width, 750);

    LayoutBorderEnd(&fixture);
}

/* ------------------------------------------------------------------ */
/* The inline pass                                                     */
/* ------------------------------------------------------------------ */

static void TestBordersInlineAtomic(void)
{
    LayoutBorderFixture fixture;
    LayoutBorderRule htmlRule;
    LayoutBorderRule bodyRule;
    LayoutBorderRule divRule;
    LayoutBorderRule iRule;
    LayoutBorderRule bRule;
    LayoutBorderRule rules[5];
    LayoutStats stats;
    LayoutPoint origin;
    LayoutSize size;
    LayoutLineBox line;
    LayoutLineId lineId = RoboLayoutLineIdNone;
    LayoutBorderRule widthlessRule;
    LayoutBorderFixture second;

    LayoutFixture();

    /*
     * An atomic inline's advance is its **border** box since this phase, which is the same
     * sentence the block pass gained one direction over: an `inline-block` with 5 of
     * border-left and 15 of border-right occupies 120 on the line when it declares a
     * 100-wide box, and the participant after it starts 120 further on.
     */
    LayoutBorderRuleInit(&htmlRule, "html", RoboCssDisplayBlock);
    LayoutBorderRuleInit(&bodyRule, "body", RoboCssDisplayBlock);
    LayoutBorderRuleInit(&divRule, "div", RoboCssDisplayBlock);
    LayoutBorderRulePx(&divRule, "height", 30.0);
    LayoutBorderRuleInit(&iRule, "i", RoboCssDisplayInlineBlock);
    LayoutBorderRulePx(&iRule, "width", 100.0);
    LayoutBorderRulePx(&iRule, "height", 20.0);
    LayoutBorderRulePx(&iRule, "border-left-width", 5.0);
    LayoutBorderRulePx(&iRule, "border-right-width", 15.0);
    LayoutBorderRuleStyleAll(&iRule, RoboCssBorderStyleSolid);
    LayoutBorderRuleInit(&bRule, "b", RoboCssDisplayInlineBlock);
    LayoutBorderRulePx(&bRule, "width", 50.0);
    LayoutBorderRulePx(&bRule, "height", 10.0);
    rules[0] = htmlRule;
    rules[1] = bodyRule;
    rules[2] = divRule;
    rules[3] = iRule;
    rules[4] = bRule;

    LayoutBorderBegin(&fixture, "<html><body><div><i></i><b></b></div></body></html>", rules,
                        5u);
    LayoutBorderPass(&fixture, LAYOUT_BORDER_VIEWPORT_WIDTH, &stats);

    origin = LayoutBorderOriginOf(&fixture, LayoutBorderBoxFor(&fixture, "i"));
    size = LayoutBorderSizeOf(&fixture, LayoutBorderBoxFor(&fixture, "i"));
    RoboCheckEqInt(origin.x, 0);
    RoboCheckEqInt(size.width, 120);
    RoboCheckEqInt(size.height, 20);

    origin = LayoutBorderOriginOf(&fixture, LayoutBorderBoxFor(&fixture, "b"));
    RoboCheckEqInt(origin.x, 120);

    RoboCheckEqInt(LayoutTreeLineCount(fixture.tree), 1u);
    RoboCheckEqInt(LayoutTreeLineAt(fixture.tree, 0u, &lineId, NULL), RoboOk);
    RoboCheckEqInt(LayoutTreeLine(fixture.tree, lineId, &line, NULL), RoboOk);
    RoboCheckEqInt(line.size.width, 120 + 50);
    RoboCheckEqInt(line.members, 2u);
    RoboCheckEqInt(line.size.height, 20);
    RoboCheckEqInt(line.baseline.ascent, 20);
    RoboCheckEqInt(line.baseline.descent, 0);

    RoboCheckEqInt(stats.inlineFlow.bordersApplied, 2u);
    RoboCheckEqInt(stats.inlineFlow.bordersUnresolved, 0u);
    RoboCheckEqInt(stats.inlineFlow.bordersDrawn, 2u);

    LayoutBorderEnd(&fixture);

    /*
     * And an `auto` width fills what the borders left, which is this direction's copy of
     * the block pass's rule: 800 less 20 less 20 of border is the 760 inside a participant
     * that is exactly as wide as its containing block.
     */
    LayoutFixture();
    LayoutBorderRuleInit(&widthlessRule, "i", RoboCssDisplayInlineBlock);
    LayoutBorderRulePx(&widthlessRule, "height", 20.0);
    LayoutBorderRulePx(&widthlessRule, "border-left-width", 20.0);
    LayoutBorderRulePx(&widthlessRule, "border-right-width", 20.0);
    LayoutBorderRuleStyleAll(&widthlessRule, RoboCssBorderStyleSolid);
    LayoutBorderBegin(&second, "<html><body><div><i></i></div></body></html>",
                        (LayoutBorderRuleTable[]){htmlRule, bodyRule, divRule,
                                                 widthlessRule},
                        4u);
    LayoutBorderPass(&second, LAYOUT_BORDER_VIEWPORT_WIDTH, NULL);
    size = LayoutBorderSizeOf(&second, LayoutBorderBoxFor(&second, "i"));
    RoboCheckEqInt(size.width, LAYOUT_BORDER_VIEWPORT_WIDTH);
    LayoutBorderEnd(&second);
}

static void TestBordersInlineTransparentNotApplied(void)
{
    LayoutBorderFixture fixture;
    LayoutBorderRule htmlRule;
    LayoutBorderRule bodyRule;
    LayoutBorderRule divRule;
    LayoutBorderRule iRule;
    LayoutBorderRule rules[4];
    LayoutStats stats;
    LayoutSize size;
    LayoutNodeId iId;

    LayoutFixture();

    /*
     * A **transparent inline box** with a border, which is the one participant this build
     * deliberately does not border. CSS does grow such a box's inline advance by its
     * horizontal border and its line box by its vertical one; this build applies neither,
     * and the reason is not economy: an inline box's rectangle is derived from its
     * *contents* rather than placed, so a border on it is a rule about fragmenting an
     * inline box across lines, which is the anonymous-box and line-breaking work this
     * module has not done.
     *
     * Asserted as a value -- zero extent, nothing applied, and the door saying so -- so
     * that the goal which implements it changes an assertion rather than discovering one.
     */
    LayoutBorderRuleInit(&htmlRule, "html", RoboCssDisplayBlock);
    LayoutBorderRuleInit(&bodyRule, "body", RoboCssDisplayBlock);
    LayoutBorderRuleInit(&divRule, "div", RoboCssDisplayBlock);
    LayoutBorderRuleInit(&iRule, "i", RoboCssDisplayInline);
    LayoutBorderRuleShorthand(&iRule, 20.0);
    rules[0] = htmlRule;
    rules[1] = bodyRule;
    rules[2] = divRule;
    rules[3] = iRule;

    LayoutBorderBegin(&fixture, "<html><body><div><i></i></div></body></html>", rules, 4u);
    LayoutBorderPass(&fixture, LAYOUT_BORDER_VIEWPORT_WIDTH, &stats);

    iId = LayoutBorderBoxFor(&fixture, "i");
    size = LayoutBorderSizeOf(&fixture, iId);
    RoboCheckEqInt(size.width, 0);
    RoboCheckEqInt(size.height, 0);
    RoboCheckEqInt(
        LayoutBorderSizeOf(&fixture, LayoutBorderBoxFor(&fixture, "div")).height, 0);

    /* The door *says* so rather than answering a zero that reads like an honoured
     * declaration that came to nothing. */
    {
        LayoutBorderEdges edges;
        LayoutDiag diag = RoboLayoutCleared;

        edges.left.width = 77;
        RoboCheckEqInt(LayoutObjectBorders(fixture.tree, iId, &edges, &diag),
                          RoboNotFound);
        /* Cleared on the way out: a caller that ignores the code reads a stated absence. */
        RoboCheckEqInt(edges.left.width, 0);
        RoboCheckEqInt(edges.widthsResolved, 0u);
        RoboCheckEqInt((int)diag.domain, (int)RoboLayoutDomainObject);
    }

    RoboCheckEqInt(stats.inlineFlow.bordersApplied, 0u);
    RoboCheckEqInt(stats.inlineFlow.nonAtomicPlaced, 1u);

    LayoutBorderEnd(&fixture);

    /*
     * And the contrast, in one number: the same box declared `inline-block` *does* get its
     * border, so "not applied" is a fact about the transparent box rather than about this
     * build's arithmetic. Here the participant's border box is 40 by 40 -- a 20px border on
     * each of the four sides of a box with no content extent of its own -- and the line is
     * 40 tall because that is its floor.
     */
    {
        LayoutBorderFixture second;
        LayoutBorderRule atomicRule;
        LayoutBorderRule rules2[4];

        LayoutFixture();
        LayoutBorderRuleInit(&atomicRule, "i", RoboCssDisplayInlineBlock);
        LayoutBorderRuleShorthand(&atomicRule, 20.0);
        rules2[0] = htmlRule;
        rules2[1] = bodyRule;
        rules2[2] = divRule;
        rules2[3] = atomicRule;
        LayoutBorderBegin(&second, "<html><body><div><i></i></div></body></html>", rules2,
                            4u);
        LayoutBorderPass(&second, LAYOUT_BORDER_VIEWPORT_WIDTH, NULL);
        size = LayoutBorderSizeOf(&second, LayoutBorderBoxFor(&second, "i"));
        RoboCheckEqInt(size.height, 40);
        RoboCheckEqInt(
            LayoutBorderSizeOf(&second, LayoutBorderBoxFor(&second, "div")).height, 40);
        LayoutBorderEnd(&second);
    }
}

/* ------------------------------------------------------------------ */
/* The read side                                                       */
/* ------------------------------------------------------------------ */

static void TestBordersObjectDoor(void)
{
    LayoutBorderFixture fixture;
    LayoutBorderRule htmlRule;
    LayoutBorderRule bodyRule;
    LayoutBorderRule divRule;
    LayoutBorderRule rules[3];
    LayoutBorderEdges edges;
    LayoutNodeId divId;
    LayoutNodeId textId = RoboLayoutNodeIdNone;
    LayoutNodeId spanId;
    LayoutNodeId inlineId;
    size_t count;
    size_t index;

    LayoutFixture();

    LayoutBorderRuleInit(&divRule, "div", RoboCssDisplayBlock);
    LayoutBorderRulePx(&divRule, "border-left-width", 7.0);
    LayoutBorderRuleBorderStyle(&divRule, "border-left-style",
                                    RoboCssBorderStyleDouble);
    LayoutBorderRuleColor(&divRule, "border-left-color", CssColorMake(9, 8, 7, 255));
    LayoutBorderRuleInit(&htmlRule, "html", RoboCssDisplayBlock);
    LayoutBorderRuleInit(&bodyRule, "body", RoboCssDisplayBlock);
    rules[0] = htmlRule;
    rules[1] = bodyRule;
    rules[2] = divRule;

    LayoutBorderBegin(&fixture, "<html><body><div>hello</div><span></span><i></i></body></html>",
                        rules, 3u);
    LayoutBorderPass(&fixture, LAYOUT_BORDER_VIEWPORT_WIDTH, NULL);

    count = LayoutTreeObjectCount(fixture.tree);
    for (index = 0u; index < count; index++) {
        LayoutNodeId id = RoboLayoutNodeIdNone;
        LayoutObject object;

        RoboCheckEqInt(LayoutObjectIdAt(fixture.tree, index, &id), RoboOk);
        RoboCheckEqInt(LayoutObjectGet(fixture.tree, id, &object, NULL), RoboOk);
        if (object.kind == RoboLayoutObjectText) {
            textId = id;
        }
    }

    divId = LayoutBorderBoxFor(&fixture, "div");
    spanId = LayoutBorderBoxFor(&fixture, "span");
    inlineId = LayoutBorderBoxFor(&fixture, "i");

    /* A placed block box: the door answers, and answers the *used* values -- one side
     * declared, one side drawn, and the style and colour that produced it. */
    RoboCheckEqInt(LayoutObjectBorders(fixture.tree, divId, &edges, NULL), RoboOk);
    RoboCheckEqInt(edges.left.width, 7);
    RoboCheckEqInt(edges.top.width, 0);
    RoboCheckEqInt(edges.right.width, 0);
    RoboCheckEqInt(edges.bottom.width, 0);
    RoboCheckEqInt(edges.widthsResolved, 1u);
    RoboCheckEqInt(edges.widthsUnresolved, 0u);
    RoboCheckEqInt(edges.bordersDrawn, 1u);
    RoboCheckEqInt((int)edges.left.style, (int)RoboCssBorderStyleDouble);
    RoboCheckEqInt(edges.left.color.red, 9);
    RoboCheckEqInt(LayoutBorderEdgesIsValid(&edges), 1);

    /*
     * A text run is not a box at all, and a transparent inline box is one CSS applies no
     * border to -- both are *misses* rather than zeros, because a zero with a count of
     * resolved sides would tell a caller the author's border had been honoured and come to
     * nothing.
     */
    RoboCheck(textId != RoboLayoutNodeIdNone);
    RoboCheckEqInt(LayoutObjectBorders(fixture.tree, textId, &edges, NULL),
                      RoboNotFound);
    RoboCheckEqInt(edges.left.width, 0);
    RoboCheckEqInt(edges.widthsResolved, 0u);
    RoboCheckEqInt(LayoutObjectBorders(fixture.tree, spanId, &edges, NULL),
                      RoboNotFound);

    /* An inline-level box in a *block* context is skipped rather than placed, so nothing has
     * sized it either. */
    RoboCheckEqInt(LayoutObjectBorders(fixture.tree, inlineId, &edges, NULL),
                      RoboNotFound);

    RoboCheckEqInt(LayoutObjectBorders(NULL, divId, &edges, NULL),
                      RoboInvalidArgument);
    RoboCheckEqInt(LayoutObjectBorders(fixture.tree, RoboLayoutNodeIdNone, &edges,
                                                NULL),
                      RoboInvalidArgument);
    RoboCheckEqInt(LayoutObjectBorders(fixture.tree, divId, NULL, NULL),
                      RoboInvalidArgument);
    RoboCheckEqInt(LayoutObjectBorders(fixture.tree, (LayoutNodeId)(count + 99u),
                                                &edges, NULL),
                      RoboNotFound);

    LayoutBorderEnd(&fixture);
}

/* ------------------------------------------------------------------ */
/* Re-layout, determinism and balance                                  */
/* ------------------------------------------------------------------ */

static void TestBordersRelayout(void)
{
    LayoutBorderFixture fixture;
    LayoutBorderRule htmlRule;
    LayoutBorderRule bodyRule;
    LayoutBorderRule divRule;
    LayoutBorderRule rules[3];
    LayoutSize size;
    LayoutPoint origin;

    LayoutFixture();

    LayoutBorderRuleInit(&htmlRule, "html", RoboCssDisplayBlock);
    LayoutBorderRuleInit(&bodyRule, "body", RoboCssDisplayBlock);
    LayoutBorderRuleInit(&divRule, "div", RoboCssDisplayBlock);
    LayoutBorderRulePx(&divRule, "width", 100.0);
    LayoutBorderRulePx(&divRule, "height", 10.0);
    LayoutBorderRulePx(&divRule, "border-left-width", 2.0);
    LayoutBorderRuleStyleAll(&divRule, RoboCssBorderStyleSolid);
    rules[0] = htmlRule;
    rules[1] = bodyRule;
    rules[2] = divRule;

    LayoutBorderBegin(&fixture, "<html><body><div></div></body></html>", rules, 3u);
    LayoutBorderPass(&fixture, LAYOUT_BORDER_VIEWPORT_WIDTH, NULL);

    size = LayoutBorderSizeOf(&fixture, LayoutBorderBoxFor(&fixture, "div"));
    RoboCheckEqInt(size.width, 102);
    RoboCheckEqInt(size.height, 10);

    /*
     * The declaration changes and the pass runs again -- no invalidation flag, because
     * every door below is a function of the record it is handed and every record on a box is
     * rewritten by a pass. What a *re-layout* means here is that the second answer is a fact
     * about the second style and not a leftover of the first, which is why the geometry is
     * asserted *and* the record it came from.
     */
    LayoutBorderRulePx(&divRule, "border-left-width", 30.0);
    LayoutBorderRulePx(&divRule, "border-bottom-width", 4.0);
    LayoutBorderStyleReplace(&fixture.source, "div", &divRule);
    LayoutBorderRebuild(&fixture);

    LayoutBorderPass(&fixture, LAYOUT_BORDER_VIEWPORT_WIDTH, NULL);

    size = LayoutBorderSizeOf(&fixture, LayoutBorderBoxFor(&fixture, "div"));
    RoboCheckEqInt(size.width, 130);
    RoboCheckEqInt(size.height, 14);
    /* And the origin is still the box's own, because a border never moved it. */
    origin = LayoutBorderOriginOf(&fixture, LayoutBorderBoxFor(&fixture, "div"));
    RoboCheckEqInt(origin.x, 0);
    RoboCheckEqInt(origin.y, 0);
    {
        LayoutBorderEdges edges =
            LayoutBorderEdgesOf(&fixture, LayoutBorderBoxFor(&fixture, "div"));

        RoboCheckEqInt(edges.left.width, 30);
        RoboCheckEqInt(edges.bottom.width, 4);
        RoboCheckEqInt(edges.top.width, 0);
    }

    /*
     * A **style-only** change moves no geometry and is still visible: the same five becomes
     * `none`, the box returns to its declared 100, and the record says four declarations
     * arrived and none draws. This is the pair that makes the two counters earn their keep.
     */
    LayoutBorderRuleStyleAll(&divRule, RoboCssBorderStyleNone);
    LayoutBorderStyleReplace(&fixture.source, "div", &divRule);
    LayoutBorderRebuild(&fixture);

    LayoutBorderPass(&fixture, LAYOUT_BORDER_VIEWPORT_WIDTH, NULL);
    size = LayoutBorderSizeOf(&fixture, LayoutBorderBoxFor(&fixture, "div"));
    RoboCheckEqInt(size.width, 100);
    RoboCheckEqInt(size.height, 10);
    {
        LayoutBorderEdges edges =
            LayoutBorderEdgesOf(&fixture, LayoutBorderBoxFor(&fixture, "div"));

        /* Two widths still reached the layer and neither of them draws, which is the pair a
         * single counter could not report: the declared 30 and 4 are facts about a *style*,
         * and the used zero is the conclusion the fourth resolve rule draws from it. */
        RoboCheckEqInt(edges.widthsResolved, 2u);
        RoboCheckEqInt(edges.bordersDrawn, 0u);
        RoboCheckEqInt(edges.left.width, 0);
        RoboCheckEqInt(edges.bottom.width, 0);
        RoboCheckEqInt((int)edges.left.style, (int)RoboCssBorderStyleNone);
    }

    LayoutBorderEnd(&fixture);
}

static void TestBordersDeterminism(void)
{
    LayoutBorderFixture fixture;
    LayoutBorderRule htmlRule;
    LayoutBorderRule bodyRule;
    LayoutBorderRule divRule;
    LayoutBorderRule rules[3];
    LayoutSize first;
    LayoutSize second;

    LayoutFixture();

    LayoutBorderRuleInit(&htmlRule, "html", RoboCssDisplayBlock);
    LayoutBorderRuleInit(&bodyRule, "body", RoboCssDisplayBlock);
    LayoutBorderRuleInit(&divRule, "div", RoboCssDisplayBlock);
    LayoutBorderRulePx(&divRule, "width", 120.0);
    LayoutBorderRulePx(&divRule, "height", 40.0);
    LayoutBorderRulePx(&divRule, "border-top-width", 1.0);
    LayoutBorderRulePx(&divRule, "border-right-width", 2.0);
    LayoutBorderRulePx(&divRule, "border-bottom-width", 3.0);
    LayoutBorderRulePx(&divRule, "border-left-width", 4.0);
    LayoutBorderRuleStyleAll(&divRule, RoboCssBorderStyleSolid);
    rules[0] = htmlRule;
    rules[1] = bodyRule;
    rules[2] = divRule;

    LayoutBorderBegin(&fixture, "<html><body><div></div></body></html>", rules, 3u);

    LayoutBorderPass(&fixture, LAYOUT_BORDER_VIEWPORT_WIDTH, NULL);
    first = LayoutBorderSizeOf(&fixture, LayoutBorderBoxFor(&fixture, "div"));

    /* Identical input, identical output -- twice, through the same context-free doors. */
    LayoutBorderPass(&fixture, LAYOUT_BORDER_VIEWPORT_WIDTH, NULL);
    second = LayoutBorderSizeOf(&fixture, LayoutBorderBoxFor(&fixture, "div"));
    RoboCheckEqInt(first.width, second.width);
    RoboCheckEqInt(first.height, second.height);
    RoboCheckEqInt(first.width, 120 + 4 + 2);
    RoboCheckEqInt(first.height, 40 + 1 + 3);

    LayoutBorderEnd(&fixture);
}

static void TestBordersAllocatorBalance(void)
{
    size_t before = LayoutLiveBlocks();
    size_t settled;
    int round;

    LayoutFixture();

    for (round = 0; round < 3; round++) {
        LayoutBorderFixture fixture;
        LayoutBorderRule htmlRule;
        LayoutBorderRule bodyRule;
        LayoutBorderRule divRule;
        LayoutBorderRule rules[3];

        LayoutBorderRuleInit(&htmlRule, "html", RoboCssDisplayBlock);
        LayoutBorderRuleInit(&bodyRule, "body", RoboCssDisplayBlock);
        LayoutBorderRuleInit(&divRule, "div", RoboCssDisplayBlock);
        LayoutBorderRulePx(&divRule, "width", 250.0);
        LayoutBorderRulePx(&divRule, "height", 20.0);
        LayoutBorderRuleShorthand(&divRule, 12.0);
        rules[0] = htmlRule;
        rules[1] = bodyRule;
        rules[2] = divRule;

        LayoutBorderBegin(&fixture, "<html><body><div></div></body></html>", rules, 3u);
        LayoutBorderPass(&fixture, LAYOUT_BORDER_VIEWPORT_WIDTH, NULL);
        settled = LayoutLiveBlocks();
        /* The border layer allocates nothing of its own: the records are values on the
         * stack and on the object, and the pass adds no arena. */
        RoboCheckEqInt(LayoutLiveBlocks(), settled);
        LayoutBorderPass(&fixture, 400, NULL);
        RoboCheckEqInt(LayoutLiveBlocks(), settled);
        LayoutBorderEnd(&fixture);
        RoboCheckEqInt(LayoutLiveBlocks(), before);
    }
}

/* ------------------------------------------------------------------ */
/* Registration                                                        */
/* ------------------------------------------------------------------ */

void TestRegisterLayoutBorder(void);

void TestRegisterLayoutBorder(void)
{
    TestRegister("layout.borders.kind_vocabulary", TestBordersKindVocabulary);
    TestRegister("layout.borders.width_record", TestBordersWidthRecord);
    TestRegister("layout.borders.side_record", TestBordersSideRecord);
    TestRegister("layout.borders.seam", TestBordersSeam);
    TestRegister("layout.borders.edges_record", TestBordersEdgesRecord);
    TestRegister("layout.borders.derived_boxes", TestBordersDerivedBoxes);
    TestRegister("layout.borders.block_declared_width", TestBordersBlockDeclaredWidth);
    TestRegister("layout.borders.block_auto_width", TestBordersBlockAutoWidth);
    TestRegister("layout.borders.block_sides", TestBordersBlockSides);
    TestRegister("layout.borders.block_vertical", TestBordersBlockVertical);
    TestRegister("layout.borders.block_stacking", TestBordersBlockStacking);
    TestRegister("layout.borders.block_unresolved", TestBordersBlockUnresolved);
    TestRegister("layout.borders.none_style", TestBordersNoneStyle);
    TestRegister("layout.borders.margin_interaction", TestBordersMarginInteraction);
    TestRegister("layout.borders.padding_interaction", TestBordersPaddingInteraction);
    TestRegister("layout.borders.children_get_content_width",
                       TestBordersChildrenGetContentWidth);
    TestRegister("layout.borders.inline_atomic", TestBordersInlineAtomic);
    TestRegister("layout.borders.inline_transparent_not_applied",
                       TestBordersInlineTransparentNotApplied);
    TestRegister("layout.borders.object_door", TestBordersObjectDoor);
    TestRegister("layout.borders.relayout", TestBordersRelayout);
    TestRegister("layout.borders.determinism", TestBordersDeterminism);
    TestRegister("layout.borders.allocator_balance", TestBordersAllocatorBalance);
}
