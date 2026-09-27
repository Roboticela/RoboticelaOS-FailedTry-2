/*
 * RoboLayout padding suite (Phase 10.10) — layout.paddings.*
 *
 * Four things are under test, and the boundaries between them are the phase:
 *
 *   the **vocabulary** -- three kinds, four sides, and the predicates and names over
 *   them -- which is pure, and whose one difference from 10.9's is that a *negative*
 *   length is not one of the states a side may be in;
 *
 *   the **seam**: what a computed style declared, which is the one place this module
 *   reads a padding and the one place a declaration it cannot use becomes a count;
 *
 *   the two **derived numbers** -- the content box a border box leaves and the border
 *   box a content box asks for -- driven directly, so that a wrong subtraction fails
 *   at the arithmetic rather than three boxes later in a coordinate;
 *
 *   the **pass**: padding reaching geometry, which since this phase means a box's
 *   `size` is its border box, an `auto` width fills what the padding left, and an
 *   atomic inline's advance along a line carries it.
 *
 * The shape the phase is about is asserted every way it can be seen: a padding
 * changes a box's **size** and never its **position**, which is the exact mirror of
 * 10.9's claim and the reason the two phases are two. A case that only checked the
 * size would pass for a build that grew the box outward from its content origin, and
 * `layout.paddings.relayout` is the one that fails it.
 *
 * The scripts here are *computed styles*, built through 9.8's makers exactly as the
 * sizing and margin suites build them, because a fabricated struct would prove the
 * pass reads a struct rather than a computed CSS value. Where a case needs a
 * declaration the fixture cannot express through a CSS property table -- a negative
 * length is expressible, a percentage is, but a `var()`-shaped unresolved length is
 * not -- the value is flagged the way the computed layer flags one and the
 * classification is what is under test.
 *
 * Elements are resolved **by tag** (`LayoutPaddingBoxFor`) rather than by arena
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
typedef enum layoutPaddingDeclKind {
    LAYOUT_PADDING_DECL_PX = 0,
    /** A px length that is **negative**: invalid CSS for this property, which is the
     *  one declaration the seam is asked to refuse rather than use. */
    LAYOUT_PADDING_DECL_NEGATIVE_PX = 1,
    /** A px length flagged unresolved, which is what a `var()` arrives as. */
    LAYOUT_PADDING_DECL_UNRESOLVED_PX = 2,
    LAYOUT_PADDING_DECL_PERCENTAGE = 3,
    LAYOUT_PADDING_DECL_EM = 4,
    LAYOUT_PADDING_DECL_KEYWORD = 5
} LayoutPaddingDeclKind;

typedef struct layoutPaddingDecl {
    const char *name;
    LayoutPaddingDeclKind kind;
    double number;
    const char *keyword;
} LayoutPaddingDecl;

#define LAYOUT_PADDING_MAX_DECLARATIONS 10u
#define LAYOUT_PADDING_MAX_RULES 8u

typedef struct layoutPaddingRule {
    const char *tag;
    CssDisplay display;
    LayoutPaddingDecl declarations[LAYOUT_PADDING_MAX_DECLARATIONS];
    size_t declarationsCount;
} LayoutPaddingRule;

static void LayoutPaddingRuleInit(LayoutPaddingRule *rule,
                                     const char *tag,
                                     CssDisplay display)
{
    memset(rule, 0, sizeof(*rule));
    rule->tag = tag;
    rule->display = display;
}

static void LayoutPaddingRuleAdd(LayoutPaddingRule *rule,
                                    const char *name,
                                    LayoutPaddingDeclKind kind,
                                    double number,
                                    const char *keyword)
{
    RoboCheckEqInt(rule->declarationsCount < LAYOUT_PADDING_MAX_DECLARATIONS, 1);
    rule->declarations[rule->declarationsCount].name = name;
    rule->declarations[rule->declarationsCount].kind = kind;
    rule->declarations[rule->declarationsCount].number = number;
    rule->declarations[rule->declarationsCount].keyword = keyword;
    rule->declarationsCount++;
}

static void LayoutPaddingRulePx(LayoutPaddingRule *rule, const char *name, double px)
{
    LayoutPaddingRuleAdd(rule, name, LAYOUT_PADDING_DECL_PX, px, NULL);
}

/** A *negative* px length, which CSS forbids for this property and the seam refuses. */
static void LayoutPaddingRuleNegative(LayoutPaddingRule *rule,
                                         const char *name,
                                         double px)
{
    LayoutPaddingRuleAdd(rule, name, LAYOUT_PADDING_DECL_NEGATIVE_PX, px, NULL);
}

/**
 * A single-value shorthand written the way a stylesheet would.
 *
 * `padding: 10px` is four declarations one layer down, and that is the fact a case
 * asserts by writing the shorthand and reading four sides of geometry: the expansion
 * is 9.11's, and this fixture writing it does not make it this suite's.
 */
static void LayoutPaddingRuleShorthand(LayoutPaddingRule *rule, double px)
{
    LayoutPaddingRulePx(rule, "padding-top", px);
    LayoutPaddingRulePx(rule, "padding-right", px);
    LayoutPaddingRulePx(rule, "padding-bottom", px);
    LayoutPaddingRulePx(rule, "padding-left", px);
}

static void LayoutPaddingRulePercentage(LayoutPaddingRule *rule,
                                           const char *name,
                                           double percent)
{
    LayoutPaddingRuleAdd(rule, name, LAYOUT_PADDING_DECL_PERCENTAGE, percent, NULL);
}

static void LayoutPaddingRuleEm(LayoutPaddingRule *rule, const char *name, double em)
{
    LayoutPaddingRuleAdd(rule, name, LAYOUT_PADDING_DECL_EM, em, NULL);
}

static void LayoutPaddingRuleKeyword(LayoutPaddingRule *rule,
                                        const char *name,
                                        const char *keyword)
{
    LayoutPaddingRuleAdd(rule, name, LAYOUT_PADDING_DECL_KEYWORD, 0.0, keyword);
}

static void LayoutPaddingRuleUnresolved(LayoutPaddingRule *rule,
                                           const char *name,
                                           double px)
{
    LayoutPaddingRuleAdd(rule, name, LAYOUT_PADDING_DECL_UNRESOLVED_PX, px, NULL);
}

typedef struct layoutPaddingStyle {
    char tag[16];
    CssComputedStyle *style; /* owned */
    size_t answers;
} LayoutPaddingStyle;

typedef struct layoutPaddingSource {
    LayoutPaddingStyle rules[LAYOUT_PADDING_MAX_RULES];
    size_t count;
} LayoutPaddingSource;

/**
 * Build the style @p rule describes: a real DISPLAY value plus whatever the rule
 * declares, all through 9.8's own makers and container.
 *
 * `display` is set even when the rule declares nothing else, because 9.12's initial
 * value is `inline` and a fixture that wanted a block box would otherwise be asserting
 * a document made of inline boxes.
 */
static CssComputedStyle *LayoutPaddingStyleBuild(const LayoutPaddingRule *rule)
{
    CssComputedStyle *style = NULL;
    CssComputedValue value = RoboCssCleared;
    CssValue specified = RoboCssCleared;
    size_t index;

    RoboCheckEqInt(CssComputedStyleCreate(&style, NULL), RoboOk);
    RoboCheckEqInt(CssComputedValueMakeDisplay(&value, rule->display, NULL),
                      RoboOk);
    RoboCheckEqInt(CssComputedStyleSetEntry(style, RoboCssPropertyIdNone,
                                                       CssSliceFromText("display"),
                                                       0u, &specified, &value, NULL),
                      RoboOk);

    for (index = 0u; index < rule->declarationsCount; index++) {
        const LayoutPaddingDecl *declaration = &rule->declarations[index];

        value = (CssComputedValue)RoboCssCleared;
        specified = (CssValue)RoboCssCleared;
        switch (declaration->kind) {
        case LAYOUT_PADDING_DECL_PERCENTAGE:
            RoboCheckEqInt(CssComputedValueMakePercentage(&value,
                                                                     declaration->number, 0,
                                                                     NULL),
                              RoboOk);
            break;
        case LAYOUT_PADDING_DECL_KEYWORD:
            RoboCheckEqInt(CssComputedValueMakeKeyword(
                                  &value, CssSliceFromText(declaration->keyword), NULL),
                              RoboOk);
            break;
        case LAYOUT_PADDING_DECL_UNRESOLVED_PX:
            RoboCheckEqInt(CssComputedValueMakeLength(
                                  &value, declaration->number, RoboCssUnitPx, 0, NULL),
                              RoboOk);
            /* The flag the computed layer sets for a `var()` or a `calc()`: carried
             * intact and not a value *yet*, whatever its kind says. */
            value.unresolved = 1;
            break;
        case LAYOUT_PADDING_DECL_EM:
            RoboCheckEqInt(CssComputedValueMakeLength(
                                  &value, declaration->number, RoboCssUnitEm, 0, NULL),
                              RoboOk);
            break;
        case LAYOUT_PADDING_DECL_NEGATIVE_PX:
        case LAYOUT_PADDING_DECL_PX:
        default:
            /* Written the same way for both, and that is the point: the *style* holds
             * what it was handed -- a negative length is a computed value like any other
             * -- and it is the classification that refuses one, not the maker. A fixture
             * that could not build the declaration could not test the refusal. */
            RoboCheckEqInt(CssComputedValueMakeLength(
                                  &value, declaration->number, RoboCssUnitPx, 0, NULL),
                              RoboOk);
            break;
        }
        RoboCheckEqInt(CssComputedStyleSetEntry(
                              style, RoboCssPropertyIdNone,
                              CssSliceFromText(declaration->name), 0u, &specified,
                              &value, NULL),
                          RoboOk);
    }
    return style;
}

/** Add @p rule to @p source, and answer the style it built. */
static CssComputedStyle *LayoutPaddingStyleAdd(LayoutPaddingSource *source,
                                                          const LayoutPaddingRule *rule)
{
    size_t slot;
    CssComputedStyle *style;

    LayoutFixture();
    RoboCheckEqInt(source->count < LAYOUT_PADDING_MAX_RULES, 1);
    slot = source->count;
    style = LayoutPaddingStyleBuild(rule);
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
static void LayoutPaddingStyleReplace(LayoutPaddingSource *source,
                                        const char *tag,
                                        const LayoutPaddingRule *rule)
{
    size_t index;

    LayoutFixture();
    for (index = 0u; index < source->count; index++) {
        if (strcmp(source->rules[index].tag, tag) == 0) {
            CssComputedStyleDestroy(source->rules[index].style);
            source->rules[index].style = LayoutPaddingStyleBuild(rule);
            return;
        }
    }
    RoboCheckMsg(0, "the tag to replace is not in this source");
}

static const CssComputedStyle *LayoutPaddingStyleLookup(
    void *userdata,
    const HtmlNodeHandle *element)
{
    LayoutPaddingSource *source = (LayoutPaddingSource *)userdata;
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
    /* An element nothing was declared for: `display: inline`, no declared padding, and
     * that is CSS's initial value rather than a fixture default. */
    return NULL;
}

/* ------------------------------------------------------------------ */
/* The fixture                                                         */
/* ------------------------------------------------------------------ */

/**
 * The px number @p style declares for @p property, asserted to be a usable length.
 *
 * The read the *fixture* needs, not the pass: a case that replaces a style wants to
 * assert the replacement landed rather than that the geometry moved for another reason.
 * Signed, because the negative-declaration case reads a declaration this build refuses
 * and has to see that the style still holds it.
 */
static int32_t LayoutPaddingEntryPx(const CssComputedStyle *style,
                                       const char *property)
{
    const CssComputedValue *value;

    RoboCheck(style != NULL);
    value = CssComputedStyleComputedByName(style,
                                                    CssSliceFromText(property));
    RoboCheck(value != NULL);
    RoboCheckEqInt((int)value->kind, (int)RoboCssComputedValueLength);
    RoboCheckEqInt((int)value->unit, (int)RoboCssUnitPx);
    RoboCheckEqInt(value->unresolved, 0);
    /* Half away from zero, which is the rule 10.8's door states -- and the fixture
     * rounds rather than the module, because this helper is reading a style to check a
     * replacement landed, not resolving a dimension. */
    return value->number < 0.0 ? (int32_t)(value->number - 0.5)
                               : (int32_t)(value->number + 0.5);
}

typedef struct layoutPaddingFixture {
    LayoutDocument doc;
    LayoutPaddingSource source;
    LayoutStyleSource styles;
    LayoutTree *tree;
} LayoutPaddingFixture;

static void LayoutPaddingBegin(LayoutPaddingFixture *fixture,
                                 const char *html,
                                 const LayoutPaddingRule *rules,
                                 size_t ruleCount)
{
    LayoutBuildOptions options;
    size_t index;

    memset(fixture, 0, sizeof(*fixture));
    LayoutDocumentBuild(&fixture->doc, html);
    for (index = 0u; index < ruleCount; index++) {
        (void)LayoutPaddingStyleAdd(&fixture->source, &rules[index]);
    }
    LayoutStyleSourceInit(&fixture->styles);
    fixture->styles.userdata = &fixture->source;
    fixture->styles.ComputedStyleFor = LayoutPaddingStyleLookup;
    RoboCheckEqInt(LayoutTreeCreate(&fixture->tree, NULL), RoboOk);
    LayoutBuildOptionsInit(&options);
    options.styles = &fixture->styles;
    RoboCheckEqInt(LayoutTreeBuildFromDocument(fixture->tree, fixture->doc.id,
                                                          &options, NULL, NULL),
                      RoboOk);
}

static void LayoutPaddingEnd(LayoutPaddingFixture *fixture)
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

#define LAYOUT_PADDING_VIEWPORT_WIDTH 800
#define LAYOUT_PADDING_VIEWPORT_HEIGHT 600

/** One pass at @p viewportWidth. */
static void LayoutPaddingPass(LayoutPaddingFixture *fixture,
                                int32_t viewportWidth,
                                LayoutStats *outStats)
{
    LayoutContext *context = NULL;
    LayoutSize viewport;
    LayoutStats stats;

    viewport.width = viewportWidth;
    viewport.height = LAYOUT_PADDING_VIEWPORT_HEIGHT;
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
static LayoutNodeId LayoutPaddingBoxFor(LayoutPaddingFixture *fixture,
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
static LayoutPoint LayoutPaddingOriginOf(LayoutPaddingFixture *fixture,
                                                    LayoutNodeId id)
{
    LayoutResult result;
    LayoutPoint position;

    RoboCheckEqInt(LayoutObjectResult(fixture->tree, id, &result), RoboOk);
    RoboCheckEqInt((int)result.state, (int)RoboLayoutResultComputed);
    RoboCheckEqInt(LayoutResultPosition(&result, &position), RoboOk);
    return position;
}

/** The size a placed box was given -- its **border** box since 10.10. */
static LayoutSize LayoutPaddingSizeOf(LayoutPaddingFixture *fixture,
                                                 LayoutNodeId id)
{
    LayoutResult result;
    LayoutSize size;

    RoboCheckEqInt(LayoutObjectResult(fixture->tree, id, &result), RoboOk);
    RoboCheckEqInt((int)result.state, (int)RoboLayoutResultComputed);
    RoboCheckEqInt(LayoutResultSize(&result, &size), RoboOk);
    return size;
}

/** The used padding a placed box reports. */
static LayoutPaddingEdges LayoutPaddingEdgesOf(LayoutPaddingFixture *fixture,
                                                          LayoutNodeId id)
{
    LayoutPaddingEdges edges;

    RoboCheckEqInt(LayoutObjectPadding(fixture->tree, id, &edges, NULL), RoboOk);
    return edges;
}

/** Assert every field of a used-padding record at once. */
static void LayoutPaddingExpectEdges(const LayoutPaddingEdges *edges,
                                        int32_t top,
                                        int32_t right,
                                        int32_t bottom,
                                        int32_t left,
                                        size_t resolved,
                                        size_t unresolved)
{
    RoboCheckEqInt(edges->top, top);
    RoboCheckEqInt(edges->right, right);
    RoboCheckEqInt(edges->bottom, bottom);
    RoboCheckEqInt(edges->left, left);
    RoboCheckEqInt(edges->resolved, resolved);
    RoboCheckEqInt(edges->unresolved, unresolved);
}

/** A record of four used values, for the derived doors below. */
static LayoutPaddingEdges GetRoboLayoutPaddingEdges(int32_t top,
                                                       int32_t right,
                                                       int32_t bottom,
                                                       int32_t left)
{
    LayoutPaddingEdges edges;

    LayoutPaddingEdgesInit(&edges);
    edges.top = top;
    edges.right = right;
    edges.bottom = bottom;
    edges.left = left;
    edges.resolved = 4u;
    return edges;
}

/** A record of four declarations, for the policy and seam cases. */
static LayoutPaddings LayoutPaddingQuad(GfxPixelX top,
                                                 GfxPixelX right,
                                                 GfxPixelX bottom,
                                                 GfxPixelX left)
{
    LayoutPaddings paddings;

    LayoutPaddingsInit(&paddings);
    paddings.top = LayoutPaddingResolved(top);
    paddings.right = LayoutPaddingResolved(right);
    paddings.bottom = LayoutPaddingResolved(bottom);
    paddings.left = LayoutPaddingResolved(left);
    return paddings;
}

/* ------------------------------------------------------------------ */
/* The vocabulary                                                      */
/* ------------------------------------------------------------------ */

static void TestPaddingsKindVocabulary(void)
{
    size_t index;

    LayoutFixture();

    RoboCheckEqInt((int)RoboLayoutPaddingKindCount, 4);
    /*
     * The zero of the vocabulary is `ABSENT`, and that is a decision rather than a
     * numbering accident: a cleared record then holds the side's *initial state*, so a
     * forgotten initialisation is not a claim that somebody wrote a padding.
     */
    RoboCheckEqInt((int)RoboLayoutPaddingAbsent, 0);
    RoboCheckEqInt((int)RoboLayoutPaddingResolved, 1);
    RoboCheckEqInt((int)RoboLayoutPaddingUnresolved, 2);
    /* 19.2's kind, before `unknown`. */
    RoboCheckEqInt((int)RoboLayoutPaddingPercentage, 3);

    for (index = 0u; index < RoboLayoutPaddingKindCount; index++) {
        LayoutPaddingKind kind = (LayoutPaddingKind)index;

        RoboCheckEqInt(LayoutPaddingKindIsKnown(kind), 1);
        RoboCheck(LayoutPaddingKindName(kind) != NULL);
    }
    RoboCheckEqInt(LayoutPaddingKindIsKnown((LayoutPaddingKind)4), 0);
    RoboCheckEqInt(LayoutPaddingKindIsKnown((LayoutPaddingKind)77), 0);
    RoboCheckStrEq(LayoutPaddingKindName(RoboLayoutPaddingAbsent), "absent");
    RoboCheckStrEq(LayoutPaddingKindName(RoboLayoutPaddingResolved), "resolved");
    RoboCheckStrEq(LayoutPaddingKindName(RoboLayoutPaddingUnresolved),
                      "unresolved");
    RoboCheckStrEq(LayoutPaddingKindName(RoboLayoutPaddingPercentage),
                      "percentage");
    RoboCheckStrEq(LayoutPaddingKindName((LayoutPaddingKind)4), "unknown");
    RoboCheck(LayoutPaddingKindName((LayoutPaddingKind)77) != NULL);
}

static void TestPaddingsOneSide(void)
{
    LayoutPadding zero = LayoutPaddingZero();
    LayoutPadding absent = LayoutPaddingAbsent();
    LayoutPadding resolved = LayoutPaddingResolved(12);
    LayoutPadding unresolved = LayoutPaddingUnresolved();
    GfxPixelX length = 0;

    LayoutFixture();

    /* `padding: 0` is an inset an author asked not to have; `ABSENT` is a side nobody
     * declared. Both are used as zero and they are different records -- the entire
     * reason this vocabulary has three kinds. */
    RoboCheckEqInt(LayoutPaddingIsResolved(&zero), 1);
    RoboCheckEqInt(LayoutPaddingIsAbsent(&zero), 0);
    RoboCheckEqInt(LayoutPaddingIsValid(&zero), 1);
    RoboCheckEqInt(LayoutPaddingLength(&zero, &length), RoboOk);
    RoboCheckEqInt(length, 0);
    RoboCheckEqInt(LayoutPaddingEqual(&zero, &absent), 0);

    RoboCheckEqInt(LayoutPaddingIsAbsent(&absent), 1);
    RoboCheckEqInt(LayoutPaddingIsResolved(&absent), 0);
    RoboCheckEqInt(LayoutPaddingIsUnresolved(&absent), 0);
    RoboCheckEqInt(LayoutPaddingLength(&absent, &length), RoboNotFound);

    RoboCheckEqInt(LayoutPaddingIsResolved(&resolved), 1);
    RoboCheckEqInt(LayoutPaddingLength(&resolved, &length), RoboOk);
    RoboCheckEqInt(length, 12);
    RoboCheckEqInt(LayoutPaddingEqual(&resolved, &zero), 0);
    {
        LayoutPadding same = LayoutPaddingResolved(12);

        RoboCheckEqInt(LayoutPaddingEqual(&resolved, &same), 1);
    }

    RoboCheckEqInt(LayoutPaddingIsUnresolved(&unresolved), 1);
    RoboCheckEqInt(LayoutPaddingLength(&unresolved, &length), RoboNotFound);
    RoboCheckEqInt(LayoutPaddingIsValid(&unresolved), 1);

    /*
     * And the one check this vocabulary has that 10.9's does not: a padding is a
     * distance *inward*, so a negative one is not a small padding -- it is a record this
     * build will not put into a size. Checked in the predicate rather than only at the
     * seam, because a caller may build the record itself.
     */
    {
        LayoutPadding negative = LayoutPaddingResolved(-1);
        LayoutPadding positive = LayoutPaddingResolved(1);

        RoboCheckEqInt(LayoutPaddingIsValid(&negative), 0);
        RoboCheckEqInt(LayoutPaddingIsValid(&positive), 1);
    }

    /* An unknown *kind* is not a record either, and that is a separate failure from a
     * bad length: one is a value nobody defined and the other is a value this property
     * does not allow. */
    {
        LayoutPadding bogus = resolved;

        bogus.kind = (LayoutPaddingKind)9;
        RoboCheckEqInt(LayoutPaddingIsValid(&bogus), 0);
        RoboCheckEqInt(LayoutPaddingIsResolved(&bogus), 0);
        RoboCheckEqInt(LayoutPaddingIsAbsent(&bogus), 0);
        RoboCheckEqInt(LayoutPaddingIsUnresolved(&bogus), 0);
    }

    /* NULL is never a padding: every predicate answers "no" and every door refuses. */
    RoboCheckEqInt(LayoutPaddingIsValid(NULL), 0);
    RoboCheckEqInt(LayoutPaddingIsResolved(NULL), 0);
    RoboCheckEqInt(LayoutPaddingIsAbsent(NULL), 0);
    RoboCheckEqInt(LayoutPaddingIsUnresolved(NULL), 0);
    RoboCheckEqInt(LayoutPaddingLength(NULL, &length), RoboInvalidArgument);
    RoboCheckEqInt(LayoutPaddingLength(&resolved, NULL), RoboInvalidArgument);
    RoboCheckEqInt(LayoutPaddingEqual(NULL, NULL), 1);
    RoboCheckEqInt(LayoutPaddingEqual(&resolved, NULL), 0);
}

static void TestPaddingsSides(void)
{
    LayoutPaddings paddings = LayoutPaddingQuad(1, 2, 3, 4);
    LayoutPaddings same = LayoutPaddingQuad(1, 2, 3, 4);
    LayoutPaddings other = LayoutPaddingQuad(1, 2, 3, 5);
    LayoutPadding side;
    size_t index;

    LayoutFixture();

    RoboCheckEqInt(LayoutPaddingsIsValid(&paddings), 1);
    RoboCheckEqInt(LayoutPaddingsEqual(&paddings, &same), 1);
    RoboCheckEqInt(LayoutPaddingsEqual(&paddings, &other), 0);

    /*
     * Every side through the one door, in the order CSS writes them: top, right,
     * bottom, left. The enum is borrowed from 9.11 rather than restated, and this case is
     * what makes that a fact -- a transposed `padding: 1px 2px 3px 4px` would be two
     * vocabularies disagreeing about which side is which.
     */
    for (index = 0u; index < RoboCssBoxSideCount; index++) {
        CssBoxSide which = (CssBoxSide)index;

        RoboCheckEqInt(LayoutPaddingsSide(&paddings, which, &side), RoboOk);
        RoboCheckEqInt(LayoutPaddingIsResolved(&side), 1);
        switch (which) {
        case RoboCssBoxSideTop:
            RoboCheckEqInt(side.length, 1);
            break;
        case RoboCssBoxSideRight:
            RoboCheckEqInt(side.length, 2);
            break;
        case RoboCssBoxSideBottom:
            RoboCheckEqInt(side.length, 3);
            break;
        case RoboCssBoxSideLeft:
        default:
            RoboCheckEqInt(side.length, 4);
            break;
        }
    }

    /* A side that is not one of the four is a *miss*, not a fourth answer: the record
     * has four sides and a caller asking for a fifth has a bug. */
    side = LayoutPaddingZero();
    RoboCheckEqInt(LayoutPaddingsSide(&paddings, (CssBoxSide)4, &side),
                      RoboNotFound);
    RoboCheckEqInt(LayoutPaddingsSide(&paddings, (CssBoxSide)99, &side),
                      RoboNotFound);
    RoboCheckEqInt(LayoutPaddingsSide(NULL, RoboCssBoxSideTop, &side),
                      RoboInvalidArgument);
    RoboCheckEqInt(LayoutPaddingsSide(&paddings, RoboCssBoxSideTop, NULL),
                      RoboInvalidArgument);

    /*
     * A cleared record is four `ABSENT` sides and not four zeros, which is asserted by
     * the *kind* rather than by the number: both read as zero in geometry, and only one
     * of them is a declaration.
     */
    {
        LayoutPaddings cleared;

        LayoutPaddingsInit(&cleared);
        RoboCheckEqInt(LayoutPaddingsIsValid(&cleared), 1);
        RoboCheckEqInt(LayoutPaddingsEqual(&cleared, &paddings), 0);
        for (index = 0u; index < RoboCssBoxSideCount; index++) {
            RoboCheckEqInt(LayoutPaddingsSide(&cleared,
                                                        (CssBoxSide)index, &side),
                              RoboOk);
            RoboCheckEqInt(LayoutPaddingIsAbsent(&side), 1);
        }
        /* And `is_valid` is about the record rather than about whether anybody wrote
         * anything: one bad side makes the whole record one this build will not size a box
         * with. */
        cleared.left = LayoutPaddingResolved(-3);
        RoboCheckEqInt(LayoutPaddingsIsValid(&cleared), 0);
    }

    RoboCheckEqInt(LayoutPaddingsIsValid(NULL), 0);
    RoboCheckEqInt(LayoutPaddingsEqual(NULL, NULL), 1);
    RoboCheckEqInt(LayoutPaddingsEqual(&paddings, NULL), 0);
}

/* ------------------------------------------------------------------ */
/* The seam                                                            */
/* ------------------------------------------------------------------ */

static void TestPaddingsSeam(void)
{
    LayoutPaddingRule rule;
    CssComputedStyle *style;
    LayoutPaddings paddings;
    LayoutPaddingEdges edges;
    LayoutPadding side;
    size_t index;

    LayoutFixture();

    /*
     * A px length, a zero, a percentage and a relative unit: two sides this build can use
     * and two it cannot. The percentage is the interesting one -- CSS resolves
     * `padding: 5%` against the containing block's *width* for every side including the
     * vertical ones, so this build arguably could; it does not, because 10.8 left every
     * percentage unresolved and resolving one here would make `padding: 5%` the only
     * percentage in the module that means something. It is `UNRESOLVED`, used as zero and
     * *counted*.
     */
    LayoutPaddingRuleInit(&rule, "div", RoboCssDisplayBlock);
    LayoutPaddingRulePx(&rule, "padding-top", 10.0);
    LayoutPaddingRulePx(&rule, "padding-right", 0.0);
    LayoutPaddingRulePercentage(&rule, "padding-bottom", 5.0);
    LayoutPaddingRuleEm(&rule, "padding-left", 3.0);
    style = LayoutPaddingStyleBuild(&rule);

    paddings = LayoutPaddingsOf(style);
    RoboCheckEqInt(LayoutPaddingsIsValid(&paddings), 1);
    RoboCheckEqInt(LayoutPaddingIsResolved(&paddings.top), 1);
    RoboCheckEqInt(paddings.top.length, 10);
    /* `padding-right: 0` is a declaration and is counted as one -- the distinction the
     * `ABSENT` kind exists for. */
    RoboCheckEqInt(LayoutPaddingIsResolved(&paddings.right), 1);
    RoboCheckEqInt(paddings.right.length, 0);
    /* A `padding-bottom: 5%` since 19.2 keeps its magnitude; `left`'s `em` is still
     * `UNRESOLVED`, because relative units are not this phase's. */
    RoboCheckEqInt(LayoutPaddingIsPercentage(&paddings.bottom), 1);
    RoboCheckEqInt((int)LayoutPaddingPercentageOf(&paddings.bottom), 5);
    RoboCheckEqInt(LayoutPaddingIsUnresolved(&paddings.left), 1);

    RoboCheckEqInt(LayoutPaddingResolve(&paddings, 200, &edges, NULL), RoboOk);
    /* 5% of the 200px containing width is 10, on the bottom; the `em` is still zero
     * and counted. */
    LayoutPaddingExpectEdges(&edges, 10, 0, 10, 0, 3u, 1u);

    CssComputedStyleDestroy(style);

    /*
     * And the three other shapes a declaration arrives in. `auto` is not a padding value
     * at all -- the grammar has no such production, and 9.11 rejects `padding: auto`
     * outright -- so a style carrying one has violated its own grammar; it is counted as a
     * declaration this build cannot use rather than given a kind of its own. A *flagged*
     * px length is what a `var()` or a `calc()` becomes: intact, and not a value yet. And
     * a **negative** length is the declaration no box dimension and no padding accepts,
     * which is the one respect in which this reader differs from 10.9's.
     */
    LayoutPaddingRuleInit(&rule, "div", RoboCssDisplayBlock);
    LayoutPaddingRuleKeyword(&rule, "padding-top", "auto");
    LayoutPaddingRuleUnresolved(&rule, "padding-right", 4.0);
    LayoutPaddingRuleNegative(&rule, "padding-bottom", -6.0);
    LayoutPaddingRulePx(&rule, "padding-left", 7.6);
    style = LayoutPaddingStyleBuild(&rule);

    paddings = LayoutPaddingsOf(style);
    RoboCheckEqInt(LayoutPaddingIsUnresolved(&paddings.top), 1);
    RoboCheckEqInt(LayoutPaddingIsUnresolved(&paddings.right), 1);
    RoboCheckEqInt(LayoutPaddingIsUnresolved(&paddings.bottom), 1);
    RoboCheckEqInt(LayoutPaddingIsResolved(&paddings.left), 1);
    /* Rounded through 10.8's door, half away from zero: `padding: 7.6px` is 8, which is
     * what a browser does with it too. */
    RoboCheckEqInt(paddings.left.length, 8);

    /* And the declaration the seam refused is still in the style, unchanged: refusing a
     * value is not editing the document. */
    RoboCheckEqInt(LayoutPaddingEntryPx(style, "padding-bottom"), -6);

    RoboCheckEqInt(LayoutPaddingResolve(&paddings, 200, &edges, NULL), RoboOk);
    LayoutPaddingExpectEdges(&edges, 0, 0, 0, 8, 1u, 3u);

    CssComputedStyleDestroy(style);

    /* A rule that declares nothing: four `ABSENT` sides, which is what `padding: 0` is
     * *not*. */
    LayoutPaddingRuleInit(&rule, "div", RoboCssDisplayBlock);
    style = LayoutPaddingStyleBuild(&rule);
    paddings = LayoutPaddingsOf(style);
    RoboCheckEqInt(LayoutPaddingIsAbsent(&paddings.top), 1);
    RoboCheckEqInt(LayoutPaddingIsAbsent(&paddings.right), 1);
    RoboCheckEqInt(LayoutPaddingIsAbsent(&paddings.bottom), 1);
    RoboCheckEqInt(LayoutPaddingIsAbsent(&paddings.left), 1);
    RoboCheckEqInt(LayoutPaddingResolve(&paddings, 200, &edges, NULL), RoboOk);
    LayoutPaddingExpectEdges(&edges, 0, 0, 0, 0, 0u, 0u);
    CssComputedStyleDestroy(style);

    /*
     * And a box with **no style at all** has made no declarations, which is an answer
     * rather than a failure: the same four `ABSENT` sides a cleared record holds.
     */
    paddings = LayoutPaddingsOf(NULL);
    RoboCheckEqInt(LayoutPaddingsIsValid(&paddings), 1);
    for (index = 0u; index < RoboCssBoxSideCount; index++) {
        RoboCheckEqInt(LayoutPaddingsSide(&paddings, (CssBoxSide)index,
                                                    &side),
                          RoboOk);
        RoboCheckEqInt(LayoutPaddingIsAbsent(&side), 1);
    }
}

/* ------------------------------------------------------------------ */
/* The used values                                                     */
/* ------------------------------------------------------------------ */

static void TestPaddingsEdgesRecord(void)
{
    LayoutPaddingEdges edges;
    LayoutPaddingEdges other;
    LayoutPaddings paddings;

    LayoutFixture();

    LayoutPaddingEdgesInit(&edges);
    RoboCheckEqInt(edges.top, 0);
    RoboCheckEqInt(edges.right, 0);
    RoboCheckEqInt(edges.bottom, 0);
    RoboCheckEqInt(edges.left, 0);
    RoboCheckEqInt(edges.resolved, 0u);
    RoboCheckEqInt(edges.unresolved, 0u);
    RoboCheckEqInt(LayoutPaddingEdgesIsValid(&edges), 1);

    other = edges;
    RoboCheckEqInt(LayoutPaddingEdgesEqual(&edges, &other), 1);
    other.unresolved = 1u;
    RoboCheckEqInt(LayoutPaddingEdgesEqual(&edges, &other), 0);
    /* The counters are part of the record rather than decoration: two resolutions that
     * agreed about the four numbers and disagreed about how many declarations they could
     * not use would be two answers to one document. */
    other.left = 0;
    RoboCheckEqInt(LayoutPaddingEdgesIsValid(&other), 1);

    /*
     * Invalid records, and each is a different way for one to be wrong: a negative side
     * is a size no renderer can draw, a count above four is a partition that does not add
     * up, and a *pair* of counts whose sum exceeds four is a record some later arithmetic
     * built wrong. The door that derives a size refuses all three -- here, so that the
     * refusal happens before a box is sized.
     */
    edges = GetRoboLayoutPaddingEdges(1, 2, 3, 4);
    edges.left = -1;
    RoboCheckEqInt(LayoutPaddingEdgesIsValid(&edges), 0);
    edges = GetRoboLayoutPaddingEdges(1, 2, 3, 4);
    edges.resolved = 5u;
    RoboCheckEqInt(LayoutPaddingEdgesIsValid(&edges), 0);
    edges = GetRoboLayoutPaddingEdges(1, 2, 3, 4);
    edges.resolved = 3u;
    edges.unresolved = 2u;
    RoboCheckEqInt(LayoutPaddingEdgesIsValid(&edges), 0);
    edges.unresolved = 1u;
    RoboCheckEqInt(LayoutPaddingEdgesIsValid(&edges), 1);
    RoboCheckEqInt(LayoutPaddingEdgesIsValid(NULL), 0);
    RoboCheckEqInt(LayoutPaddingEdgesEqual(NULL, NULL), 1);
    RoboCheckEqInt(LayoutPaddingEdgesEqual(&edges, NULL), 0);

    /*
     * The one policy, driven at its boundary: a side nobody declared and a side declared
     * with a value this build cannot use both come out as **zero** and are told apart only
     * by the counters -- which is what makes the counters the honest part of the record.
     */
    paddings = LayoutPaddingQuad(1, 2, 3, 4);
    RoboCheckEqInt(LayoutPaddingResolve(&paddings, 200, &edges, NULL), RoboOk);
    LayoutPaddingExpectEdges(&edges, 1, 2, 3, 4, 4u, 0u);

    paddings = LayoutPaddingQuad(1, 2, 3, 4);
    paddings.bottom = LayoutPaddingAbsent();
    paddings.left = LayoutPaddingUnresolved();
    RoboCheckEqInt(LayoutPaddingResolve(&paddings, 200, &edges, NULL), RoboOk);
    LayoutPaddingExpectEdges(&edges, 1, 2, 0, 0, 2u, 1u);

    /* Refusals: the argument boundary, and nothing else -- this function cannot fail on a
     * document, because every declaration has a stated answer. */
    RoboCheckEqInt(LayoutPaddingResolve(NULL, 200, &edges, NULL), RoboInvalidArgument);
    RoboCheckEqInt(LayoutPaddingResolve(&paddings, 200, NULL, NULL),
                      RoboInvalidArgument);
}

static void TestPaddingsDerivedBoxes(void)
{
    LayoutPaddingEdges edges = GetRoboLayoutPaddingEdges(0, 0, 0, 0);
    LayoutPaddingEdges invalid;
    LayoutSize content;
    LayoutSize box;
    LayoutDiag diag = RoboLayoutCleared;
    int32_t width = 0;
    int32_t sentinel = 4242;

    LayoutFixture();

    /*
     * The content box a border box leaves -- the first derived number, and the one an
     * `auto` width goes through. CSS's constraint equation has a padding term on each
     * side, so a box with `padding: 10px` and no declared width is exactly as wide as its
     * containing block rather than 20px wider.
     */
    edges = GetRoboLayoutPaddingEdges(0, 0, 0, 0);
    RoboCheckEqInt(LayoutPaddingEdgesContentWidth(&edges, 800, &width, NULL),
                      RoboOk);
    RoboCheckEqInt(width, 800);

    edges = GetRoboLayoutPaddingEdges(0, 20, 0, 10);
    RoboCheckEqInt(LayoutPaddingEdgesContentWidth(&edges, 800, &width, NULL),
                      RoboOk);
    RoboCheckEqInt(width, 770);

    /*
     * Floored at zero, once, and *not* refused: a border box narrower than its own
     * padding has no room for contents, and "no room" is a width of zero rather than a
     * negative one -- a negative available width would make every `auto` dimension in the
     * subtree depend on which of two impossible numbers a subtraction produced.
     */
    edges = GetRoboLayoutPaddingEdges(0, 5, 0, 10);
    RoboCheckEqInt(LayoutPaddingEdgesContentWidth(&edges, 7, &width, NULL),
                      RoboOk);
    RoboCheckEqInt(width, 0);

    /* NULL arguments are refused *without* a diagnostic the caller did not ask for, and
     * the out is left alone when the record is refused. */
    RoboCheckEqInt(LayoutPaddingEdgesContentWidth(NULL, 800, &width, NULL),
                      RoboInvalidArgument);
    RoboCheckEqInt(LayoutPaddingEdgesContentWidth(&edges, 800, NULL, NULL),
                      RoboInvalidArgument);

    invalid = GetRoboLayoutPaddingEdges(0, 0, 0, -1);
    width = sentinel;
    RoboCheckEqInt(LayoutPaddingEdgesContentWidth(&invalid, 800, &width, &diag),
                      RoboError);
    RoboCheckEqInt((int)diag.domain, (int)RoboLayoutDomainPadding);
    /* The refused call wrote nothing: a caller that ignored the code reads its own
     * sentinel rather than a number derived from a record this module never produces. */
    RoboCheckEqInt(width, sentinel);

    /*
     * The border box a content box asks for -- the second derived number, and the phase in
     * one call: the content box plus the padding on each axis, which is what a result's
     * `size` carries. Both axes at once because a size is what a result holds.
     */
    edges = GetRoboLayoutPaddingEdges(5, 5, 5, 5);
    content.width = 100;
    content.height = 50;
    RoboCheckEqInt(LayoutPaddingEdgesPaddingBox(&edges, content, &box, NULL),
                      RoboOk);
    RoboCheckEqInt(box.width, 110);
    RoboCheckEqInt(box.height, 60);

    /* Asymmetric, so the two axes are not one answer: top/bottom 1 and 3, left/right 4 and
     * 2 -- the border box is the content plus each pair. */
    edges = GetRoboLayoutPaddingEdges(1, 2, 3, 4);
    content.width = 100;
    content.height = 50;
    RoboCheckEqInt(LayoutPaddingEdgesPaddingBox(&edges, content, &box, NULL),
                      RoboOk);
    RoboCheckEqInt(box.width, 106);
    RoboCheckEqInt(box.height, 54);

    /*
     * And it saturates rather than wrapping. A content box already at the geometry's
     * maximum plus a padding is a document an author can write -- `width: 2147483647px`
     * with `padding: 1px` -- and the honest answer is a size that saturates, not a
     * negative rectangle a renderer would draw inside out. The clamp is on the *sum*, so
     * the box keeps the padding it cannot express rather than losing it to a subtraction.
     */
    edges = GetRoboLayoutPaddingEdges(1, 1, 1, 1);
    content.width = 2147483647;
    content.height = 2147483647;
    RoboCheckEqInt(LayoutPaddingEdgesPaddingBox(&edges, content, &box, NULL),
                      RoboOk);
    RoboCheckEqInt(box.width, 2147483647);
    RoboCheckEqInt(box.height, 2147483647);

    /* Refusals: NULL, a negative content box (a box's own extent is never negative), and
     * a record no size can be derived from. */
    RoboCheckEqInt(LayoutPaddingEdgesPaddingBox(NULL, content, &box, NULL),
                      RoboInvalidArgument);
    RoboCheckEqInt(LayoutPaddingEdgesPaddingBox(&edges, content, NULL, NULL),
                      RoboInvalidArgument);
    content.width = -1;
    content.height = 50;
    RoboCheckEqInt(LayoutPaddingEdgesPaddingBox(&edges, content, &box, NULL),
                      RoboInvalidArgument);
    content.width = 100;
    RoboCheckEqInt(LayoutPaddingEdgesPaddingBox(&invalid, content, &box, &diag),
                      RoboError);
    RoboCheckEqInt((int)diag.domain, (int)RoboLayoutDomainPadding);
}

/* ------------------------------------------------------------------ */
/* Block layout                                                        */
/* ------------------------------------------------------------------ */

static void TestPaddingsBlockDeclaredWidth(void)
{
    LayoutPaddingFixture fixture;
    LayoutPaddingRule htmlRule;
    LayoutPaddingRule bodyRule;
    LayoutPaddingRule divRule;
    LayoutPaddingRule rules[3];
    LayoutStats stats;
    LayoutPaddingEdges edges;
    LayoutPoint origin;
    LayoutSize size;

    LayoutFixture();

    /*
     * `html` and `body` are declared **block**, and that is a fact about this fixture
     * rather than about padding: an element nothing declared for computes to `display:
     * inline` (9.12's initial value, and the point of `LayoutPaddingStyleLookup`
     * answering NULL), so a document that declared only the subject would put the
     * subject's ancestors in the inline flow and never reach the subject at all.
     */
    LayoutPaddingRuleInit(&htmlRule, "html", RoboCssDisplayBlock);
    LayoutPaddingRuleInit(&bodyRule, "body", RoboCssDisplayBlock);
    LayoutPaddingRuleInit(&divRule, "div", RoboCssDisplayBlock);
    LayoutPaddingRulePx(&divRule, "width", 200.0);
    LayoutPaddingRulePx(&divRule, "height", 100.0);
    LayoutPaddingRuleShorthand(&divRule, 5.0);
    rules[0] = htmlRule;
    rules[1] = bodyRule;
    rules[2] = divRule;

    LayoutPaddingBegin(&fixture, "<html><body><div></div></body></html>", rules, 3u);
    LayoutPaddingPass(&fixture, LAYOUT_PADDING_VIEWPORT_WIDTH, &stats);

    /*
     * The phase in one assertion, and it is the `content-box` model rather than an
     * implementation detail: `width: 200px` is the **content** box (CSS's initial
     * `box-sizing`, which this build is), so the box a reader sees is 200 plus the padding
     * on each axis -- 210 by 110. A build that read the declaration as a *border* box
     * would give a child 190 and a result of 200 here.
     */
    size = LayoutPaddingSizeOf(&fixture, LayoutPaddingBoxFor(&fixture, "div"));
    RoboCheckEqInt(size.width, 210);
    RoboCheckEqInt(size.height, 110);

    /* And the padding never moved it: the origin is the containing block's content origin
     * plus the four *margins*, which here are all zero. That is the whole of the
     * difference between this phase and the last one, asserted where a reader can see it. */
    origin = LayoutPaddingOriginOf(&fixture, LayoutPaddingBoxFor(&fixture, "div"));
    RoboCheckEqInt(origin.x, 0);
    RoboCheckEqInt(origin.y, 0);

    edges = LayoutPaddingEdgesOf(&fixture, LayoutPaddingBoxFor(&fixture, "div"));
    LayoutPaddingExpectEdges(&edges, 5, 5, 5, 5, 4u, 0u);

    /* `<body>` is as tall as the child's margin box, which spans the padding because the
     * padding is inside the border box the margin box is built around. */
    size = LayoutPaddingSizeOf(&fixture, LayoutPaddingBoxFor(&fixture, "body"));
    RoboCheckEqInt(size.width, 800);
    RoboCheckEqInt(size.height, 110);

    /* `padding: 5px` is four declarations one layer down and four sides of geometry here,
     * while `html` and `body` declare none -- so the count is four and not twelve, which is
     * the whole reason an `ABSENT` side is counted nowhere. */
    RoboCheckEqInt(stats.block.paddingsApplied, 4u);
    RoboCheckEqInt(stats.block.paddingsUnresolved, 0u);

    LayoutPaddingEnd(&fixture);
}

static void TestPaddingsBlockAutoWidth(void)
{
    LayoutPaddingFixture fixture;
    LayoutPaddingRule htmlRule;
    LayoutPaddingRule bodyRule;
    LayoutPaddingRule divRule;
    LayoutPaddingRule rules[3];
    LayoutPaddingEdges edges;
    LayoutSize size;
    LayoutPoint origin;

    LayoutFixture();

    /*
     * The second consequence of the model, and the one a build could plausibly get wrong
     * without any other case noticing: an `auto` width fills what the margins **and the
     * padding** left, because CSS's constraint equation has a term for each. So a box with
     * `padding: 10px` and no declared width is exactly as wide as its containing block --
     * 800 -- rather than 820, and the padding comes out of the *contents'* room: the content
     * box is 780.
     */
    LayoutPaddingRuleInit(&htmlRule, "html", RoboCssDisplayBlock);
    LayoutPaddingRuleInit(&bodyRule, "body", RoboCssDisplayBlock);
    LayoutPaddingRuleInit(&divRule, "div", RoboCssDisplayBlock);
    LayoutPaddingRulePx(&divRule, "height", 10.0);
    LayoutPaddingRuleShorthand(&divRule, 10.0);
    rules[0] = htmlRule;
    rules[1] = bodyRule;
    rules[2] = divRule;

    LayoutPaddingBegin(&fixture, "<html><body><div></div></body></html>", rules, 3u);
    LayoutPaddingPass(&fixture, LAYOUT_PADDING_VIEWPORT_WIDTH, NULL);

    size = LayoutPaddingSizeOf(&fixture, LayoutPaddingBoxFor(&fixture, "div"));
    RoboCheckEqInt(size.width, 800);
    RoboCheckEqInt(size.height, 30);

    /* The content box, recovered the one way a caller can: the border box less the four
     * sides. That is the arithmetic this phase added so that nobody has to re-derive it,
     * and asserting it here is what makes "the padding came out of the contents' room"
     * checkable rather than implied. */
    edges = LayoutPaddingEdgesOf(&fixture, LayoutPaddingBoxFor(&fixture, "div"));
    LayoutPaddingExpectEdges(&edges, 10, 10, 10, 10, 4u, 0u);
    RoboCheckEqInt(size.width - (edges.left + edges.right), 780);
    RoboCheckEqInt(size.height - (edges.top + edges.bottom), 10);

    origin = LayoutPaddingOriginOf(&fixture, LayoutPaddingBoxFor(&fixture, "div"));
    RoboCheckEqInt(origin.x, 0);
    RoboCheckEqInt(origin.y, 0);

    LayoutPaddingEnd(&fixture);
}

static void TestPaddingsBlockSides(void)
{
    LayoutPaddingFixture fixture;
    LayoutPaddingRule htmlRule;
    LayoutPaddingRule bodyRule;
    LayoutPaddingRule divRule;
    LayoutPaddingRule rules[3];
    LayoutPaddingEdges edges;
    LayoutSize size;
    LayoutSize owner;
    LayoutPoint origin;
    LayoutNodeId divId;

    LayoutFixture();

    /*
     * Four different sides, so that a transposed pair is caught by the *geometry* rather
     * than only by the record: the border box is `width + right + left` by
     * `height + top + bottom`, and a build that had swapped left and right would give the
     * same width and a different content origin.
     */
    LayoutPaddingRuleInit(&htmlRule, "html", RoboCssDisplayBlock);
    LayoutPaddingRuleInit(&bodyRule, "body", RoboCssDisplayBlock);
    LayoutPaddingRuleInit(&divRule, "div", RoboCssDisplayBlock);
    LayoutPaddingRulePx(&divRule, "width", 100.0);
    LayoutPaddingRulePx(&divRule, "height", 50.0);
    LayoutPaddingRulePx(&divRule, "padding-top", 1.0);
    LayoutPaddingRulePx(&divRule, "padding-right", 2.0);
    LayoutPaddingRulePx(&divRule, "padding-bottom", 3.0);
    LayoutPaddingRulePx(&divRule, "padding-left", 4.0);
    rules[0] = htmlRule;
    rules[1] = bodyRule;
    rules[2] = divRule;

    LayoutPaddingBegin(&fixture, "<html><body><div></div></body></html>", rules, 3u);
    LayoutPaddingPass(&fixture, LAYOUT_PADDING_VIEWPORT_WIDTH, NULL);

    divId = LayoutPaddingBoxFor(&fixture, "div");
    edges = LayoutPaddingEdgesOf(&fixture, divId);
    LayoutPaddingExpectEdges(&edges, 1, 2, 3, 4, 4u, 0u);

    size = LayoutPaddingSizeOf(&fixture, divId);
    RoboCheckEqInt(size.width, 106);
    RoboCheckEqInt(size.height, 54);

    /* Still where its margins put it, which is nowhere: a padding is *inside* the box and
     * adds to its height without moving the box below it by any amount of its own. */
    origin = LayoutPaddingOriginOf(&fixture, divId);
    RoboCheckEqInt(origin.x, 0);
    RoboCheckEqInt(origin.y, 0);

    /* The content origin a later phase will want is the box's origin plus the top and left
     * padding -- and it is derived from the record rather than stored twice, which is what
     * the door is for. */
    RoboCheckEqInt(origin.x + edges.left, 4);
    RoboCheckEqInt(origin.y + edges.top, 1);

    owner = LayoutPaddingSizeOf(&fixture, LayoutPaddingBoxFor(&fixture, "body"));
    RoboCheckEqInt(owner.height, 54);

    LayoutPaddingEnd(&fixture);
}

static void TestPaddingsBlockVertical(void)
{
    LayoutPaddingFixture fixture;
    LayoutPaddingRule htmlRule;
    LayoutPaddingRule bodyRule;
    LayoutPaddingRule divRule;
    LayoutPaddingRule pRule;
    LayoutPaddingRule rules[4];
    LayoutPoint origin;
    LayoutSize size;
    LayoutNodeId divId;

    LayoutFixture();

    /*
     * Vertical padding, in the two directions it can be seen. Top and bottom padding grow
     * the box the author sized in the *block* direction -- which is the one thing this
     * phase changes about a height -- and the next sibling therefore starts lower, because
     * what the cursor advances by is this box's margin box and the padding is inside it.
     */
    LayoutPaddingRuleInit(&htmlRule, "html", RoboCssDisplayBlock);
    LayoutPaddingRuleInit(&bodyRule, "body", RoboCssDisplayBlock);
    LayoutPaddingRuleInit(&divRule, "div", RoboCssDisplayBlock);
    LayoutPaddingRulePx(&divRule, "height", 10.0);
    LayoutPaddingRulePx(&divRule, "padding-top", 5.0);
    LayoutPaddingRulePx(&divRule, "padding-bottom", 7.0);
    LayoutPaddingRuleInit(&pRule, "p", RoboCssDisplayBlock);
    LayoutPaddingRulePx(&pRule, "height", 10.0);
    rules[0] = htmlRule;
    rules[1] = bodyRule;
    rules[2] = divRule;
    rules[3] = pRule;

    LayoutPaddingBegin(&fixture, "<html><body><div></div><p></p></body></html>", rules, 4u);
    LayoutPaddingPass(&fixture, LAYOUT_PADDING_VIEWPORT_WIDTH, NULL);

    divId = LayoutPaddingBoxFor(&fixture, "div");
    size = LayoutPaddingSizeOf(&fixture, divId);
    RoboCheckEqInt(size.height, 22);

    /* The second block starts at the first one's *margin box*, which is 22 because the
     * padding is inside the border box rather than beside it. A build that had treated the
     * padding as external spacing would put it at 22 too -- which is why the case that
     * tells them apart is `layout.paddings.block_declared_width`: here the two agree. */
    origin = LayoutPaddingOriginOf(&fixture, LayoutPaddingBoxFor(&fixture, "p"));
    RoboCheckEqInt(origin.y, 22);
    RoboCheckEqInt(origin.x, 0);

    size = LayoutPaddingSizeOf(&fixture, LayoutPaddingBoxFor(&fixture, "body"));
    RoboCheckEqInt(size.height, 32);

    LayoutPaddingEnd(&fixture);
}

static void TestPaddingsBlockStacking(void)
{
    LayoutPaddingFixture fixture;
    LayoutPaddingRule htmlRule;
    LayoutPaddingRule bodyRule;
    LayoutPaddingRule divRule;
    LayoutPaddingRule rules[3];
    LayoutPoint origin;
    LayoutNodeId divId;

    LayoutFixture();

    /*
     * And the other way round, so that both sides of the vertical pair are seen: a box with
     * `padding-top` only puts its *contents* 5px in rather than itself 5px down. A child
     * block therefore starts at the parent's content origin, which is not shifted in the
     * child's coordinates -- the origin a child is placed in *is* its containing block's
     * content origin (RoboLayoutResult.h), so nothing has to be added for the child and
     * nothing may be.
     *
     * The parent's own declared height grows by the padding, which is where a build that
     * confused the two would fail.
     */
    LayoutPaddingRuleInit(&htmlRule, "html", RoboCssDisplayBlock);
    LayoutPaddingRuleInit(&bodyRule, "body", RoboCssDisplayBlock);
    LayoutPaddingRuleInit(&divRule, "div", RoboCssDisplayBlock);
    LayoutPaddingRulePx(&divRule, "padding-top", 5.0);
    LayoutPaddingRulePx(&divRule, "height", 10.0);
    rules[0] = htmlRule;
    rules[1] = bodyRule;
    rules[2] = divRule;

    LayoutPaddingBegin(&fixture, "<html><body><div></div></body></html>", rules, 3u);
    LayoutPaddingPass(&fixture, LAYOUT_PADDING_VIEWPORT_WIDTH, NULL);

    divId = LayoutPaddingBoxFor(&fixture, "div");
    origin = LayoutPaddingOriginOf(&fixture, divId);
    RoboCheckEqInt(origin.y, 0);

    /* A parent with no declared height and a padded top is as tall as its contents plus the
     * padding: the extent the cursor reached is 0 here, so the box is exactly the padding.
     */
    {
        LayoutPaddingRule tallRule;
        LayoutPaddingFixture second;
        LayoutPaddingRule rules2[3];

        LayoutFixture();
        LayoutPaddingRuleInit(&tallRule, "div", RoboCssDisplayBlock);
        LayoutPaddingRulePx(&tallRule, "padding-top", 5.0);
        LayoutPaddingRulePx(&tallRule, "padding-bottom", 7.0);
        rules2[0] = htmlRule;
        rules2[1] = bodyRule;
        rules2[2] = tallRule;
        LayoutPaddingBegin(&second, "<html><body><div></div></body></html>", rules2, 3u);
        LayoutPaddingPass(&second, LAYOUT_PADDING_VIEWPORT_WIDTH, NULL);
        RoboCheckEqInt(LayoutPaddingSizeOf(&second,
                                                 LayoutPaddingBoxFor(&second, "div")).height,
                          12);
        LayoutPaddingEnd(&second);
    }

    LayoutPaddingEnd(&fixture);
}

static void TestPaddingsBlockUnresolved(void)
{
    LayoutPaddingFixture fixture;
    LayoutPaddingRule htmlRule;
    LayoutPaddingRule bodyRule;
    LayoutPaddingRule divRule;
    LayoutPaddingRule rules[3];
    LayoutStats stats;
    LayoutPaddingEdges edges;
    LayoutSize size;

    LayoutFixture();

    /*
     * A document with declarations this build cannot use, which is a document and not a
     * failure: a relative unit and the `auto` the grammar does not allow for this
     * property. Both are used as **zero** and *counted*. Since 19.2 the `padding-left:
     * 5%` on the same box **does** resolve -- against the containing block's width -- so
     * it is no longer in that group, and the case now proves the two facts side by side:
     * one declaration this build resolves and two it does not.
     */
    LayoutPaddingRuleInit(&htmlRule, "html", RoboCssDisplayBlock);
    LayoutPaddingRuleInit(&bodyRule, "body", RoboCssDisplayBlock);
    LayoutPaddingRuleInit(&divRule, "div", RoboCssDisplayBlock);
    LayoutPaddingRulePx(&divRule, "width", 100.0);
    LayoutPaddingRulePx(&divRule, "height", 10.0);
    LayoutPaddingRulePercentage(&divRule, "padding-left", 5.0);
    LayoutPaddingRuleEm(&divRule, "padding-top", 3.0);
    LayoutPaddingRuleKeyword(&divRule, "padding-bottom", "auto");
    LayoutPaddingRulePx(&divRule, "padding-right", 2.0);
    rules[0] = htmlRule;
    rules[1] = bodyRule;
    rules[2] = divRule;

    LayoutPaddingBegin(&fixture, "<html><body><div></div></body></html>", rules, 3u);
    LayoutPaddingPass(&fixture, LAYOUT_PADDING_VIEWPORT_WIDTH, &stats);

    edges = LayoutPaddingEdgesOf(&fixture, LayoutPaddingBoxFor(&fixture, "div"));
    /* right 2 and left 5% (of the 800px containing width) resolved; the `em` and the
     * `auto` used as zero and counted. */
    LayoutPaddingExpectEdges(&edges, 0, 2, 0, 40, 2u, 2u);

    /* The two usable sides reach the size and the two unusable ones do not: 100 + 40 + 2. */
    size = LayoutPaddingSizeOf(&fixture, LayoutPaddingBoxFor(&fixture, "div"));
    RoboCheckEqInt(size.width, 142);
    RoboCheckEqInt(size.height, 10);

    RoboCheckEqInt(stats.block.paddingsApplied, 2u);
    RoboCheckEqInt(stats.block.paddingsUnresolved, 2u);

    LayoutPaddingEnd(&fixture);
}

static void TestPaddingsBlockNegative(void)
{
    LayoutPaddingFixture fixture;
    LayoutPaddingRule htmlRule;
    LayoutPaddingRule bodyRule;
    LayoutPaddingRule divRule;
    LayoutPaddingRule rules[3];
    LayoutStats stats;
    LayoutPaddingEdges edges;
    LayoutSize size;

    LayoutFixture();

    /*
     * `padding: -10px` is invalid CSS, and the *one* parameter's worth of difference
     * between this reader and 10.9's is what it does with it: a margin uses a negative
     * length, a padding refuses it. Nothing else in the two readers differs -- one
     * classification, asked two ways.
     *
     * Asserted end to end rather than only at the seam, because "refused" has to mean the
     * *geometry* did not move: a build that had accepted the declaration and then clamped
     * it would pass a seam-level check and box a rectangle smaller than its own contents.
     */
    LayoutPaddingRuleInit(&htmlRule, "html", RoboCssDisplayBlock);
    LayoutPaddingRuleInit(&bodyRule, "body", RoboCssDisplayBlock);
    LayoutPaddingRuleInit(&divRule, "div", RoboCssDisplayBlock);
    LayoutPaddingRulePx(&divRule, "width", 100.0);
    LayoutPaddingRulePx(&divRule, "height", 10.0);
    LayoutPaddingRuleNegative(&divRule, "padding-left", -10.0);
    rules[0] = htmlRule;
    rules[1] = bodyRule;
    rules[2] = divRule;

    LayoutPaddingBegin(&fixture, "<html><body><div></div></body></html>", rules, 3u);
    LayoutPaddingPass(&fixture, LAYOUT_PADDING_VIEWPORT_WIDTH, &stats);

    edges = LayoutPaddingEdgesOf(&fixture, LayoutPaddingBoxFor(&fixture, "div"));
    LayoutPaddingExpectEdges(&edges, 0, 0, 0, 0, 0u, 1u);
    RoboCheckEqInt(edges.left >= 0, 1);

    size = LayoutPaddingSizeOf(&fixture, LayoutPaddingBoxFor(&fixture, "div"));
    RoboCheckEqInt(size.width, 100);
    RoboCheckEqInt(size.height, 10);

    RoboCheckEqInt(stats.block.paddingsApplied, 0u);
    RoboCheckEqInt(stats.block.paddingsUnresolved, 1u);

    /* And an auto-width box with a refused padding is still exactly its containing block:
     * a refusal contributes zero rather than being carried as a negative that would have
     * made the box *wider* than its parent. */
    {
        LayoutPaddingFixture second;
        LayoutPaddingRule autoRule;
        LayoutPaddingRule rules2[3];

        LayoutFixture();
        LayoutPaddingRuleInit(&autoRule, "div", RoboCssDisplayBlock);
        LayoutPaddingRulePx(&autoRule, "height", 10.0);
        LayoutPaddingRuleNegative(&autoRule, "padding-left", -400.0);
        LayoutPaddingRuleNegative(&autoRule, "padding-top", -400.0);
        rules2[0] = htmlRule;
        rules2[1] = bodyRule;
        rules2[2] = autoRule;
        LayoutPaddingBegin(&second, "<html><body><div></div></body></html>", rules2, 3u);
        LayoutPaddingPass(&second, LAYOUT_PADDING_VIEWPORT_WIDTH, NULL);
        size = LayoutPaddingSizeOf(&second, LayoutPaddingBoxFor(&second, "div"));
        RoboCheckEqInt(size.width, 800);
        RoboCheckEqInt(size.height, 10);
        LayoutPaddingEnd(&second);
    }

    LayoutPaddingEnd(&fixture);
}

static void TestPaddingsMarginInteraction(void)
{
    LayoutPaddingFixture fixture;
    LayoutPaddingRule htmlRule;
    LayoutPaddingRule bodyRule;
    LayoutPaddingRule divRule;
    LayoutPaddingRule rules[3];
    LayoutMarginEdges margins;
    LayoutPaddingEdges padding;
    LayoutPoint origin;
    LayoutSize size;
    LayoutSize owner;
    LayoutNodeId divId;

    LayoutFixture();

    /*
     * Both at once, on one box, because that is the pair an author writes and the pair a
     * build can most plausibly confuse: a margin is *outside* the box and a padding is
     * *inside* it, and the two produce different numbers for every door but `size`.
     *
     * `margin: 10px 0 0 20px`-shaped here as the four longhands, with `padding: 5px` and a
     * declared 200 by 100. The box sits at (20, 10) -- the margins -- and its size is
     * 210 by 110 -- the declaration plus the padding. A build that had folded the two
     * together would give one of those numbers to the wrong door and the other to the
     * wrong face.
     */
    LayoutPaddingRuleInit(&htmlRule, "html", RoboCssDisplayBlock);
    LayoutPaddingRuleInit(&bodyRule, "body", RoboCssDisplayBlock);
    LayoutPaddingRuleInit(&divRule, "div", RoboCssDisplayBlock);
    LayoutPaddingRulePx(&divRule, "width", 200.0);
    LayoutPaddingRulePx(&divRule, "height", 100.0);
    LayoutPaddingRuleShorthand(&divRule, 5.0);
    LayoutPaddingRulePx(&divRule, "margin-top", 10.0);
    LayoutPaddingRulePx(&divRule, "margin-left", 20.0);
    rules[0] = htmlRule;
    rules[1] = bodyRule;
    rules[2] = divRule;

    LayoutPaddingBegin(&fixture, "<html><body><div></div></body></html>", rules, 3u);
    LayoutPaddingPass(&fixture, LAYOUT_PADDING_VIEWPORT_WIDTH, NULL);

    divId = LayoutPaddingBoxFor(&fixture, "div");
    origin = LayoutPaddingOriginOf(&fixture, divId);
    RoboCheckEqInt(origin.x, 20);
    RoboCheckEqInt(origin.y, 10);

    size = LayoutPaddingSizeOf(&fixture, divId);
    RoboCheckEqInt(size.width, 210);
    RoboCheckEqInt(size.height, 110);

    /* The two records are separate facts about the box, and neither is derivable from the
     * other: the margins are on the *outside* of the result's origin and the padding is on
     * the *inside* of its size. */
    RoboCheckEqInt(LayoutObjectMargins(fixture.tree, divId, &margins, NULL),
                      RoboOk);
    RoboCheckEqInt(margins.top, 10);
    RoboCheckEqInt(margins.left, 20);
    RoboCheckEqInt(margins.right, 0);
    RoboCheckEqInt(margins.bottom, 0);

    RoboCheckEqInt(LayoutObjectPadding(fixture.tree, divId, &padding, NULL),
                      RoboOk);
    LayoutPaddingExpectEdges(&padding, 5, 5, 5, 5, 4u, 0u);

    /* The **content** origin, which is what a child of this box is placed against: the
     * margin origin plus the top and left padding. A build that had put the padding in the
     * same place as the margins would put a child at (20, 10) instead of (25, 15). */
    RoboCheckEqInt(origin.x + padding.left, 25);
    RoboCheckEqInt(origin.y + padding.top, 15);

    /* And the parent stacks by the margin box, which is now three boxes deep: 10 + 110 +
     * 0 -- the padding being inside the 110 rather than beside it. */
    owner = LayoutPaddingSizeOf(&fixture, LayoutPaddingBoxFor(&fixture, "body"));
    RoboCheckEqInt(owner.height, 120);

    LayoutPaddingEnd(&fixture);
}

static void TestPaddingsChildrenGetContentWidth(void)
{
    LayoutPaddingFixture fixture;
    LayoutPaddingRule htmlRule;
    LayoutPaddingRule bodyRule;
    LayoutPaddingRule divRule;
    LayoutPaddingRule pRule;
    LayoutPaddingRule rules[4];
    LayoutSize size;

    LayoutFixture();

    /*
     * The one number that travels *down*: a padded parent hands its children the **content**
     * box, not the border box. So a `<div>` with `width: 200px` and `padding: 10px` is 220
     * wide outside and gives its child 200 -- the declared number, because `content-box` is
     * what this build is.
     *
     * This is the case that fails for a build which had resolved a box's own width from the
     * declaration and then handed its children the *containing block's* width, or handed them
     * the border box: both are one line away and neither shows up in the parent's own size.
     */
    LayoutPaddingRuleInit(&htmlRule, "html", RoboCssDisplayBlock);
    LayoutPaddingRuleInit(&bodyRule, "body", RoboCssDisplayBlock);
    LayoutPaddingRuleInit(&divRule, "div", RoboCssDisplayBlock);
    LayoutPaddingRulePx(&divRule, "width", 200.0);
    LayoutPaddingRuleShorthand(&divRule, 10.0);
    LayoutPaddingRuleInit(&pRule, "p", RoboCssDisplayBlock);
    LayoutPaddingRulePx(&pRule, "height", 10.0);
    rules[0] = htmlRule;
    rules[1] = bodyRule;
    rules[2] = divRule;
    rules[3] = pRule;

    LayoutPaddingBegin(&fixture, "<html><body><div><p></p></div></body></html>", rules, 4u);
    LayoutPaddingPass(&fixture, LAYOUT_PADDING_VIEWPORT_WIDTH, NULL);

    size = LayoutPaddingSizeOf(&fixture, LayoutPaddingBoxFor(&fixture, "div"));
    RoboCheckEqInt(size.width, 220);
    RoboCheckEqInt(size.height, 30);

    size = LayoutPaddingSizeOf(&fixture, LayoutPaddingBoxFor(&fixture, "p"));
    RoboCheckEqInt(size.width, 200);
    RoboCheckEqInt(size.height, 10);

    LayoutPaddingEnd(&fixture);
}

/* ------------------------------------------------------------------ */
/* Inline layout                                                       */
/* ------------------------------------------------------------------ */

static void TestPaddingsInlineAtomic(void)
{
    LayoutPaddingFixture fixture;
    LayoutPaddingRule htmlRule;
    LayoutPaddingRule bodyRule;
    LayoutPaddingRule divRule;
    LayoutPaddingRule iRule;
    LayoutPaddingRule bRule;
    LayoutPaddingRule rules[5];
    LayoutStats stats;
    LayoutPoint origin;
    LayoutSize size;
    LayoutLineBox line;
    LayoutLineId lineId = RoboLayoutLineIdNone;
    LayoutPaddingRule widthlessRule;
    LayoutPaddingRule rules2[4];
    LayoutPaddingFixture second;

    LayoutFixture();

    /*
     * An atomic inline's advance is its **border** box since this phase, which is the same
     * sentence the block pass gained one direction over: a padding is inside the box, so an
     * `inline-block` with `padding-left: 5px` and `padding-right: 15px` occupies 120 on the
     * line when it declares a 100-wide box, and the participant after it starts 120 further
     * on. A build that advanced by the content box alone would start the second at 100.
     */
    LayoutPaddingRuleInit(&htmlRule, "html", RoboCssDisplayBlock);
    LayoutPaddingRuleInit(&bodyRule, "body", RoboCssDisplayBlock);
    LayoutPaddingRuleInit(&divRule, "div", RoboCssDisplayBlock);
    LayoutPaddingRulePx(&divRule, "height", 30.0);
    LayoutPaddingRuleInit(&iRule, "i", RoboCssDisplayInlineBlock);
    LayoutPaddingRulePx(&iRule, "width", 100.0);
    LayoutPaddingRulePx(&iRule, "height", 20.0);
    LayoutPaddingRulePx(&iRule, "padding-left", 5.0);
    LayoutPaddingRulePx(&iRule, "padding-right", 15.0);
    LayoutPaddingRuleInit(&bRule, "b", RoboCssDisplayInlineBlock);
    LayoutPaddingRulePx(&bRule, "width", 50.0);
    LayoutPaddingRulePx(&bRule, "height", 10.0);
    rules[0] = htmlRule;
    rules[1] = bodyRule;
    rules[2] = divRule;
    rules[3] = iRule;
    rules[4] = bRule;

    LayoutPaddingBegin(&fixture, "<html><body><div><i></i><b></b></div></body></html>",
                         rules, 5u);
    LayoutPaddingPass(&fixture, LAYOUT_PADDING_VIEWPORT_WIDTH, &stats);

    origin = LayoutPaddingOriginOf(&fixture, LayoutPaddingBoxFor(&fixture, "i"));
    size = LayoutPaddingSizeOf(&fixture, LayoutPaddingBoxFor(&fixture, "i"));
    RoboCheckEqInt(origin.x, 0);
    RoboCheckEqInt(size.width, 120);
    RoboCheckEqInt(size.height, 20);

    origin = LayoutPaddingOriginOf(&fixture, LayoutPaddingBoxFor(&fixture, "b"));
    RoboCheckEqInt(origin.x, 120);

    RoboCheckEqInt(LayoutTreeLineCount(fixture.tree), 1u);
    RoboCheckEqInt(LayoutTreeLineAt(fixture.tree, 0u, &lineId, NULL), RoboOk);
    RoboCheckEqInt(LayoutTreeLine(fixture.tree, lineId, &line, NULL), RoboOk);
    RoboCheckEqInt(line.size.width, 120 + 50);
    RoboCheckEqInt(line.members, 2u);

    /* The line's floor is the tallest participant's border box, and the baseline of an
     * `inline-block` with no in-flow line box of its own is its bottom margin edge -- so a
     * padded participant makes the line taller by its padding too. */
    RoboCheckEqInt(line.size.height, 20);
    RoboCheckEqInt(line.baseline.ascent, 20);
    RoboCheckEqInt(line.baseline.descent, 0);

    RoboCheckEqInt(stats.inlineFlow.paddingsApplied, 2u);
    RoboCheckEqInt(stats.inlineFlow.paddingsUnresolved, 0u);

    LayoutPaddingEnd(&fixture);

    /*
     * And an `auto` width fills what the padding left, which is this direction's copy of the
     * block pass's rule: 800 less 20 less 20 is the 760 inside a participant that is exactly
     * as wide as its containing block. Four sides rather than two, because an atomic inline
     * is sized in both directions by the same door.
     */
    LayoutFixture();
    LayoutPaddingRuleInit(&widthlessRule, "i", RoboCssDisplayInlineBlock);
    LayoutPaddingRulePx(&widthlessRule, "height", 20.0);
    LayoutPaddingRulePx(&widthlessRule, "padding-left", 20.0);
    LayoutPaddingRulePx(&widthlessRule, "padding-right", 20.0);
    rules2[0] = htmlRule;
    rules2[1] = bodyRule;
    rules2[2] = divRule;
    rules2[3] = widthlessRule;
    LayoutPaddingBegin(&second, "<html><body><div><i></i></div></body></html>", rules2, 4u);
    LayoutPaddingPass(&second, LAYOUT_PADDING_VIEWPORT_WIDTH, NULL);
    size = LayoutPaddingSizeOf(&second, LayoutPaddingBoxFor(&second, "i"));
    RoboCheckEqInt(size.width, 800);
    LayoutPaddingEnd(&second);
}

static void TestPaddingsInlineTransparentNotApplied(void)
{
    LayoutPaddingFixture fixture;
    LayoutPaddingRule htmlRule;
    LayoutPaddingRule bodyRule;
    LayoutPaddingRule divRule;
    LayoutPaddingRule iRule;
    LayoutPaddingRule rules[4];
    LayoutStats stats;
    LayoutSize size;
    LayoutNodeId iId;

    LayoutFixture();

    /*
     * A **transparent inline box** with a padding, which is the one participant this build
     * deliberately does not pad. CSS does grow such a box's inline advance by its horizontal
     * padding and its line box by its vertical padding; this build applies neither, and the
     * reason is not economy: an inline box's rectangle is derived from its *contents* rather
     * than placed, so padding on it is a rule about fragmenting an inline box across lines,
     * which is the anonymous-box and line-breaking work this module has not done.
     *
     * Asserted as a value -- zero extent, no padding applied, and the door saying so -- so
     * that the goal which implements it changes an assertion rather than discovering one.
     */
    LayoutPaddingRuleInit(&htmlRule, "html", RoboCssDisplayBlock);
    LayoutPaddingRuleInit(&bodyRule, "body", RoboCssDisplayBlock);
    LayoutPaddingRuleInit(&divRule, "div", RoboCssDisplayBlock);
    LayoutPaddingRuleInit(&iRule, "i", RoboCssDisplayInline);
    LayoutPaddingRuleShorthand(&iRule, 20.0);
    rules[0] = htmlRule;
    rules[1] = bodyRule;
    rules[2] = divRule;
    rules[3] = iRule;

    LayoutPaddingBegin(&fixture, "<html><body><div><i></i></div></body></html>", rules, 4u);
    LayoutPaddingPass(&fixture, LAYOUT_PADDING_VIEWPORT_WIDTH, &stats);

    iId = LayoutPaddingBoxFor(&fixture, "i");
    size = LayoutPaddingSizeOf(&fixture, iId);
    RoboCheckEqInt(size.width, 0);
    RoboCheckEqInt(size.height, 0);

    /* And the box it is inside is as tall as the line, which is zero -- a padding on a
     * transparent inline box changes no geometry at all in this build. */
    RoboCheckEqInt(LayoutPaddingSizeOf(&fixture, LayoutPaddingBoxFor(&fixture, "div"))
                          .height,
                      0);

    /* The door *says* so rather than answering a zero that reads like an honoured
     * declaration that came to nothing. */
    {
        LayoutPaddingEdges edges;
        LayoutDiag diag = RoboLayoutCleared;

        edges.left = 77;
        RoboCheckEqInt(LayoutObjectPadding(fixture.tree, iId, &edges, &diag),
                          RoboNotFound);
        /* Cleared on the way out: a caller that ignores the code reads a stated absence. */
        RoboCheckEqInt(edges.left, 0);
        RoboCheckEqInt((int)diag.domain, (int)RoboLayoutDomainObject);
    }

    RoboCheckEqInt(stats.inlineFlow.paddingsApplied, 0u);
    RoboCheckEqInt(stats.inlineFlow.nonAtomicPlaced, 1u);

    LayoutPaddingEnd(&fixture);

    /*
     * And the contrast, in one number: the same box declared `inline-block` *does* get its
     * padding, so "not applied" is a fact about the transparent box rather than about this
     * build's arithmetic. Here the participant's border box is 40 by 40 -- a 20px padding on
     * each of the four sides of a box with no content extent of its own -- and the line is
     * 40 tall because that is its floor.
     */
    {
        LayoutPaddingFixture second;
        LayoutPaddingRule atomicRule;
        LayoutPaddingRule rules2[4];

        LayoutFixture();
        LayoutPaddingRuleInit(&atomicRule, "i", RoboCssDisplayInlineBlock);
        LayoutPaddingRuleShorthand(&atomicRule, 20.0);
        rules2[0] = htmlRule;
        rules2[1] = bodyRule;
        rules2[2] = divRule;
        rules2[3] = atomicRule;
        LayoutPaddingBegin(&second, "<html><body><div><i></i></div></body></html>", rules2,
                             4u);
        LayoutPaddingPass(&second, LAYOUT_PADDING_VIEWPORT_WIDTH, NULL);
        size = LayoutPaddingSizeOf(&second, LayoutPaddingBoxFor(&second, "i"));
        RoboCheckEqInt(size.height, 40);
        RoboCheckEqInt(LayoutPaddingSizeOf(&second,
                                                 LayoutPaddingBoxFor(&second, "div")).height,
                          40);
        LayoutPaddingEnd(&second);
    }
}

/* ------------------------------------------------------------------ */
/* The read side                                                       */
/* ------------------------------------------------------------------ */

static void TestPaddingsObjectDoor(void)
{
    LayoutPaddingFixture fixture;
    LayoutPaddingRule htmlRule;
    LayoutPaddingRule bodyRule;
    LayoutPaddingRule divRule;
    LayoutPaddingRule rules[3];
    LayoutPaddingEdges edges;
    LayoutNodeId divId;
    LayoutNodeId textId = RoboLayoutNodeIdNone;
    LayoutNodeId spanId;
    LayoutNodeId inlineId;
    size_t count;
    size_t index;

    LayoutFixture();

    LayoutPaddingRuleInit(&divRule, "div", RoboCssDisplayBlock);
    LayoutPaddingRulePx(&divRule, "padding-left", 7.0);
    LayoutPaddingRuleInit(&htmlRule, "html", RoboCssDisplayBlock);
    LayoutPaddingRuleInit(&bodyRule, "body", RoboCssDisplayBlock);
    rules[0] = htmlRule;
    rules[1] = bodyRule;
    rules[2] = divRule;

    LayoutPaddingBegin(
        &fixture, "<html><body><div>hello</div><span></span><i></i></body></html>", rules, 3u);
    LayoutPaddingPass(&fixture, LAYOUT_PADDING_VIEWPORT_WIDTH, NULL);

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

    divId = LayoutPaddingBoxFor(&fixture, "div");
    spanId = LayoutPaddingBoxFor(&fixture, "span");
    inlineId = LayoutPaddingBoxFor(&fixture, "i");

    /* A placed block box: the door answers, and answers the *used* values. */
    RoboCheckEqInt(LayoutObjectPadding(fixture.tree, divId, &edges, NULL), RoboOk);
    LayoutPaddingExpectEdges(&edges, 0, 0, 0, 7, 1u, 0u);

    /*
     * A text run is not a box at all, and a transparent inline box is one CSS applies no
     * padding to -- both are *misses* rather than zeros, because a zero with a count of
     * resolved sides would tell a caller the author's padding had been honoured and come to
     * nothing.
     */
    RoboCheck(textId != RoboLayoutNodeIdNone);
    RoboCheckEqInt(LayoutObjectPadding(fixture.tree, textId, &edges, NULL),
                      RoboNotFound);
    RoboCheckEqInt(edges.left, 0);
    RoboCheckEqInt(edges.resolved, 0u);
    RoboCheckEqInt(LayoutObjectPadding(fixture.tree, spanId, &edges, NULL),
                      RoboNotFound);

    /* An inline-level box in a *block* context is skipped rather than placed, so nothing has
     * sized it either. */
    RoboCheckEqInt(LayoutObjectPadding(fixture.tree, inlineId, &edges, NULL),
                      RoboNotFound);

    RoboCheckEqInt(LayoutObjectPadding(NULL, divId, &edges, NULL),
                      RoboInvalidArgument);
    RoboCheckEqInt(LayoutObjectPadding(fixture.tree, RoboLayoutNodeIdNone, &edges,
                                                NULL),
                      RoboInvalidArgument);
    RoboCheckEqInt(LayoutObjectPadding(fixture.tree, divId, NULL, NULL),
                      RoboInvalidArgument);
    RoboCheckEqInt(LayoutObjectPadding(fixture.tree,
                                                (LayoutNodeId)(count + 99u), &edges,
                                                NULL),
                      RoboNotFound);

    LayoutPaddingEnd(&fixture);
}

/* ------------------------------------------------------------------ */
/* Re-layout, determinism and balance                                  */
/* ------------------------------------------------------------------ */

static void TestPaddingsRelayout(void)
{
    LayoutPaddingFixture fixture;
    LayoutPaddingRule htmlRule;
    LayoutPaddingRule bodyRule;
    LayoutPaddingRule divRule;
    LayoutPaddingRule rules[3];
    LayoutPoint origin;
    LayoutPoint settled;
    LayoutSize size;
    LayoutPaddingEdges edges;
    LayoutPaddingEdges again;

    LayoutFixture();

    LayoutPaddingRuleInit(&htmlRule, "html", RoboCssDisplayBlock);
    LayoutPaddingRuleInit(&bodyRule, "body", RoboCssDisplayBlock);
    LayoutPaddingRuleInit(&divRule, "div", RoboCssDisplayBlock);
    LayoutPaddingRulePx(&divRule, "width", 100.0);
    LayoutPaddingRulePx(&divRule, "height", 10.0);
    LayoutPaddingRulePx(&divRule, "padding-left", 5.0);
    rules[0] = htmlRule;
    rules[1] = bodyRule;
    rules[2] = divRule;

    LayoutPaddingBegin(&fixture, "<html><body><div></div></body></html>", rules, 3u);
    RoboCheckEqInt(LayoutPaddingEntryPx(fixture.source.rules[2].style, "padding-left"),
                      5);

    LayoutPaddingPass(&fixture, LAYOUT_PADDING_VIEWPORT_WIDTH, NULL);
    size = LayoutPaddingSizeOf(&fixture, LayoutPaddingBoxFor(&fixture, "div"));
    RoboCheckEqInt(size.width, 105);

    /*
     * A second pass over an unchanged tree is the same document: the records agree field
     * for field, which is what makes "the padding layer holds no state between calls" a fact
     * rather than a hope.
     */
    {
        LayoutPaddingEdges first =
            LayoutPaddingEdgesOf(&fixture, LayoutPaddingBoxFor(&fixture, "div"));

        LayoutPaddingPass(&fixture, LAYOUT_PADDING_VIEWPORT_WIDTH, NULL);
        again = LayoutPaddingEdgesOf(&fixture, LayoutPaddingBoxFor(&fixture, "div"));
        RoboCheckEqInt(LayoutPaddingEdgesEqual(&first, &again), 1);
    }

    /*
     * And a *changed* declaration is a changed document. The style is rebuilt with a
     * different `padding-left`, the tree is rebuilt against it, and the pass runs again --
     * and the box gets **wider while staying put**, which is the phase's shape in one pair
     * of assertions. A build that had grown the box from its content origin would move the
     * second number and this is the case that fails it.
     */
    LayoutPaddingRulePx(&divRule, "padding-left", 40.0);
    LayoutPaddingStyleReplace(&fixture.source, "div", &divRule);
    {
        LayoutBuildOptions options;

        RoboCheckEqInt(
            LayoutPaddingEntryPx(fixture.source.rules[2].style, "padding-left"), 40);
        LayoutBuildOptionsInit(&options);
        options.styles = &fixture.styles;
        RoboCheckEqInt(LayoutTreeBuildFromDocument(fixture.tree, fixture.doc.id,
                                                              &options, NULL, NULL),
                          RoboOk);
    }
    LayoutPaddingPass(&fixture, LAYOUT_PADDING_VIEWPORT_WIDTH, NULL);
    size = LayoutPaddingSizeOf(&fixture, LayoutPaddingBoxFor(&fixture, "div"));
    origin = LayoutPaddingOriginOf(&fixture, LayoutPaddingBoxFor(&fixture, "div"));
    RoboCheckEqInt(size.width, 140);
    RoboCheckEqInt(origin.x, 0);

    /* And a changed *viewport* leaves a **declared** width where it is: the declaration is
     * not a function of the containing block, so neither is the border box built around it.
     */
    LayoutPaddingPass(&fixture, 300, NULL);
    size = LayoutPaddingSizeOf(&fixture, LayoutPaddingBoxFor(&fixture, "div"));
    settled = LayoutPaddingOriginOf(&fixture, LayoutPaddingBoxFor(&fixture, "div"));
    RoboCheckEqInt(size.width, 140);
    RoboCheckEqInt(settled.x, 0);
    RoboCheckEqInt(settled.y, 0);

    LayoutPaddingEnd(&fixture);

    /*
     * The other half of that pair, which is the one number that *does* follow the containing
     * block: an `auto` width fills what the padding left, so the same box without a declared
     * width is its containing block exactly -- 800 and then 300. This is what makes "a
     * padding never makes a box wider than its containing block" checkable rather than a
     * claim about one viewport.
     */
    LayoutFixture();
    LayoutPaddingRuleInit(&divRule, "div", RoboCssDisplayBlock);
    LayoutPaddingRulePx(&divRule, "height", 10.0);
    LayoutPaddingRuleShorthand(&divRule, 10.0);
    rules[0] = htmlRule;
    rules[1] = bodyRule;
    rules[2] = divRule;
    LayoutPaddingBegin(&fixture, "<html><body><div></div></body></html>", rules, 3u);

    LayoutPaddingPass(&fixture, LAYOUT_PADDING_VIEWPORT_WIDTH, NULL);
    size = LayoutPaddingSizeOf(&fixture, LayoutPaddingBoxFor(&fixture, "div"));
    RoboCheckEqInt(size.width, 800);
    edges = LayoutPaddingEdgesOf(&fixture, LayoutPaddingBoxFor(&fixture, "div"));

    LayoutPaddingPass(&fixture, 300, NULL);
    size = LayoutPaddingSizeOf(&fixture, LayoutPaddingBoxFor(&fixture, "div"));
    RoboCheckEqInt(size.width, 300);
    RoboCheckEqInt(size.width - (edges.left + edges.right), 280);

    LayoutPaddingEnd(&fixture);
}

static void TestPaddingsAllocatorBalance(void)
{
    size_t before = LayoutLiveBlocks();
    size_t settled;
    int round;

    LayoutFixture();

    for (round = 0; round < 3; round++) {
        LayoutPaddingFixture fixture;
        LayoutPaddingRule htmlRule;
        LayoutPaddingRule bodyRule;
        LayoutPaddingRule divRule;
        LayoutPaddingRule rules[3];

        LayoutPaddingRuleInit(&htmlRule, "html", RoboCssDisplayBlock);
        LayoutPaddingRuleInit(&bodyRule, "body", RoboCssDisplayBlock);
        LayoutPaddingRuleInit(&divRule, "div", RoboCssDisplayBlock);
        LayoutPaddingRulePx(&divRule, "width", 250.0);
        LayoutPaddingRulePx(&divRule, "height", 20.0);
        LayoutPaddingRuleShorthand(&divRule, 12.0);
        rules[0] = htmlRule;
        rules[1] = bodyRule;
        rules[2] = divRule;

        LayoutPaddingBegin(&fixture, "<html><body><div></div></body></html>", rules, 3u);
        LayoutPaddingPass(&fixture, LAYOUT_PADDING_VIEWPORT_WIDTH, NULL);
        settled = LayoutLiveBlocks();
        /* The padding layer allocates nothing of its own: the records are values on the
         * stack and on the object, and the pass adds no arena. */
        RoboCheckEqInt(LayoutLiveBlocks(), settled);
        LayoutPaddingPass(&fixture, 400, NULL);
        RoboCheckEqInt(LayoutLiveBlocks(), settled);
        LayoutPaddingEnd(&fixture);
        RoboCheckEqInt(LayoutLiveBlocks(), before);
    }
}

/* ------------------------------------------------------------------ */
/* Registration                                                        */
/* ------------------------------------------------------------------ */

void TestRegisterLayoutPadding(void);

void TestRegisterLayoutPadding(void)
{
    TestRegister("layout.paddings.kind_vocabulary", TestPaddingsKindVocabulary);
    TestRegister("layout.paddings.one_side", TestPaddingsOneSide);
    TestRegister("layout.paddings.sides", TestPaddingsSides);
    TestRegister("layout.paddings.seam", TestPaddingsSeam);
    TestRegister("layout.paddings.edges_record", TestPaddingsEdgesRecord);
    TestRegister("layout.paddings.derived_boxes", TestPaddingsDerivedBoxes);
    TestRegister("layout.paddings.block_declared_width",
                       TestPaddingsBlockDeclaredWidth);
    TestRegister("layout.paddings.block_auto_width", TestPaddingsBlockAutoWidth);
    TestRegister("layout.paddings.block_sides", TestPaddingsBlockSides);
    TestRegister("layout.paddings.block_vertical", TestPaddingsBlockVertical);
    TestRegister("layout.paddings.block_stacking", TestPaddingsBlockStacking);
    TestRegister("layout.paddings.block_unresolved", TestPaddingsBlockUnresolved);
    TestRegister("layout.paddings.block_negative", TestPaddingsBlockNegative);
    TestRegister("layout.paddings.margin_interaction",
                       TestPaddingsMarginInteraction);
    TestRegister("layout.paddings.children_get_content_width",
                       TestPaddingsChildrenGetContentWidth);
    TestRegister("layout.paddings.inline_atomic", TestPaddingsInlineAtomic);
    TestRegister("layout.paddings.inline_transparent_not_applied",
                       TestPaddingsInlineTransparentNotApplied);
    TestRegister("layout.paddings.object_door", TestPaddingsObjectDoor);
    TestRegister("layout.paddings.relayout", TestPaddingsRelayout);
    TestRegister("layout.paddings.allocator_balance", TestPaddingsAllocatorBalance);
}
