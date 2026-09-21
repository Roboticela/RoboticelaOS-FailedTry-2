/*
 * RoboLayout sizing suite (Phase 10.8) — layout.sizing.*
 *
 * Three things are under test and they are told apart deliberately:
 *
 *   the **vocabulary** -- the three dimension kinds, the four sources, and the
 *   predicates and names over them -- which is pure and needs no record at all;
 *
 *   the **record**: what a box is asked for and what it came out as, its doors'
 *   refusals, and the two resolvers -- which is where the phase's *rules* live and
 *   is therefore the largest group here;
 *
 *   the **pass**: a declared `width` reaching the geometry of a block box *and* the
 *   width its children are laid out into, which is the one thing 10.3 could not do
 *   and the one thing every earlier suite would have gone on passing without.
 *
 * The geometry cases drive a *fabricated* computed style rather than a stylesheet,
 * and that is not a shortcut: `min-width` and its three siblings are not properties
 * RoboCSS registers, so the only way to prove the clamping path is **live** rather
 * than merely present is to hand the pass a style that declares them. A case built
 * from a stylesheet could only prove that a property the property table does not know
 * does nothing, which is a fact about the table.
 *
 * Where a stylesheet *is* the subject -- `layout.sizing.block_declared_width` and its
 * neighbours -- the case goes through CSS, because the point there is that a real
 * declared `width` arrives.
 */

#include "RoboLayoutTestSupport.h"

#include <string.h>

/* ------------------------------------------------------------------ */
/* A fabricated style that can declare anything                        */
/* ------------------------------------------------------------------ */

/** One declaration to put on a fabricated style. */
typedef enum layoutSizingDeclKind {
    LAYOUT_SIZING_DECL_PX = 0,
    LAYOUT_SIZING_DECL_PERCENTAGE = 1,
    LAYOUT_SIZING_DECL_EM = 2,
    LAYOUT_SIZING_DECL_KEYWORD = 3
} LayoutSizingDeclKind;

typedef struct layoutSizingDecl {
    const char *name;
    LayoutSizingDeclKind kind;
    double number;
    const char *keyword;
} LayoutSizingDecl;

#define LAYOUT_SIZING_MAX_DECLARATIONS 8u
#define LAYOUT_SIZING_MAX_RULES 8u

typedef struct layoutSizingRule {
    const char *tag;
    CssDisplay display;
    LayoutSizingDecl declarations[LAYOUT_SIZING_MAX_DECLARATIONS];
    size_t declarationsCount;
} LayoutSizingRule;

static void LayoutSizingRuleInit(LayoutSizingRule *rule,
                                    const char *tag,
                                    CssDisplay display)
{
    memset(rule, 0, sizeof(*rule));
    rule->tag = tag;
    rule->display = display;
}

static void LayoutSizingRuleAdd(LayoutSizingRule *rule,
                                   const char *name,
                                   LayoutSizingDeclKind kind,
                                   double number,
                                   const char *keyword)
{
    RoboCheckEqInt(rule->declarationsCount < LAYOUT_SIZING_MAX_DECLARATIONS, 1);
    rule->declarations[rule->declarationsCount].name = name;
    rule->declarations[rule->declarationsCount].kind = kind;
    rule->declarations[rule->declarationsCount].number = number;
    rule->declarations[rule->declarationsCount].keyword = keyword;
    rule->declarationsCount++;
}

static void LayoutSizingRulePx(LayoutSizingRule *rule, const char *name, double px)
{
    LayoutSizingRuleAdd(rule, name, LAYOUT_SIZING_DECL_PX, px, NULL);
}

static void LayoutSizingRulePercentage(LayoutSizingRule *rule,
                                          const char *name,
                                          double percent)
{
    LayoutSizingRuleAdd(rule, name, LAYOUT_SIZING_DECL_PERCENTAGE, percent, NULL);
}

static void LayoutSizingRuleEm(LayoutSizingRule *rule, const char *name, double em)
{
    LayoutSizingRuleAdd(rule, name, LAYOUT_SIZING_DECL_EM, em, NULL);
}

static void LayoutSizingRuleKeyword(LayoutSizingRule *rule,
                                       const char *name,
                                       const char *keyword)
{
    LayoutSizingRuleAdd(rule, name, LAYOUT_SIZING_DECL_KEYWORD, 0.0, keyword);
}

typedef struct layoutSizingStyle {
    char tag[16];
    CssComputedStyle *style; /* owned */
    size_t answers;
} LayoutSizingStyle;

typedef struct layoutSizingSource {
    LayoutSizingStyle rules[LAYOUT_SIZING_MAX_RULES];
    size_t count;
} LayoutSizingSource;

static CssUnit LayoutSizingDeclUnit(const LayoutSizingDecl *declaration)
{
    return declaration->kind == LAYOUT_SIZING_DECL_EM ? RoboCssUnitEm : RoboCssUnitPx;
}

/**
 * Build the style a rule describes: a real DISPLAY value, plus whatever the rule
 * declares, all through 9.8's own makers and container.
 *
 * A fabricated struct would prove the pass reads a struct; this proves it reads a
 * computed CSS value, which is the only input it is allowed to have.
 */
static void LayoutSizingStyleAdd(LayoutSizingSource *source,
                                    const LayoutSizingRule *rule)
{
    CssComputedStyle *style = NULL;
    CssComputedValue value = RoboCssCleared;
    CssValue specified = RoboCssCleared;
    size_t index;
    size_t slot;

    LayoutFixture();
    RoboCheckEqInt(source->count < LAYOUT_SIZING_MAX_RULES, 1);
    slot = source->count;
    RoboCheckEqInt(CssComputedStyleCreate(&style, NULL), RoboOk);
    RoboCheckEqInt(CssComputedValueMakeDisplay(&value, rule->display, NULL),
                      RoboOk);
    RoboCheckEqInt(CssComputedStyleSetEntry(style, RoboCssPropertyIdNone,
                                                       CssSliceFromText("display"),
                                                       0u, &specified, &value, NULL),
                      RoboOk);

    for (index = 0u; index < rule->declarationsCount; index++) {
        const LayoutSizingDecl *declaration = &rule->declarations[index];

        value = (CssComputedValue)RoboCssCleared;
        specified = (CssValue)RoboCssCleared;
        switch (declaration->kind) {
        case LAYOUT_SIZING_DECL_PERCENTAGE:
            RoboCheckEqInt(CssComputedValueMakePercentage(&value,
                                                                     declaration->number, 0,
                                                                     NULL),
                              RoboOk);
            break;
        case LAYOUT_SIZING_DECL_KEYWORD:
            RoboCheckEqInt(CssComputedValueMakeKeyword(
                                  &value, CssSliceFromText(declaration->keyword), NULL),
                              RoboOk);
            break;
        case LAYOUT_SIZING_DECL_EM:
        case LAYOUT_SIZING_DECL_PX:
        default:
            RoboCheckEqInt(CssComputedValueMakeLength(
                                  &value, declaration->number,
                                  LayoutSizingDeclUnit(declaration), 0, NULL),
                              RoboOk);
            break;
        }
        RoboCheckEqInt(CssComputedStyleSetEntry(
                              style, RoboCssPropertyIdNone,
                              CssSliceFromText(declaration->name), 0u, &specified,
                              &value, NULL),
                          RoboOk);
    }

    strncpy(source->rules[slot].tag, rule->tag, sizeof(source->rules[slot].tag) - 1u);
    source->rules[slot].tag[sizeof(source->rules[slot].tag) - 1u] = '\0';
    source->rules[slot].style = style;
    source->rules[slot].answers = 0u;
    source->count++;
}

static const CssComputedStyle *LayoutSizingStyleLookup(
    void *userdata,
    const HtmlNodeHandle *element)
{
    LayoutSizingSource *source = (LayoutSizingSource *)userdata;
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
    /* An element nothing was declared for: `auto` everywhere, and `inline` by CSS's
     * initial value -- which is what keeps a document's geometry a fact about the
     * declarations the case wrote rather than about the elements the parser
     * synthesized. */
    return NULL;
}

/* ------------------------------------------------------------------ */
/* The fixture                                                         */
/* ------------------------------------------------------------------ */

typedef struct layoutSizingFixture {
    LayoutDocument doc;
    LayoutSizingSource source;
    LayoutStyleSource styles;
    LayoutTree *tree;
} LayoutSizingFixture;

static void LayoutSizingBegin(LayoutSizingFixture *fixture,
                                const char *html,
                                const LayoutSizingRule *rules,
                                size_t ruleCount)
{
    LayoutBuildOptions options;
    size_t index;

    memset(fixture, 0, sizeof(*fixture));
    LayoutDocumentBuild(&fixture->doc, html);
    for (index = 0u; index < ruleCount; index++) {
        LayoutSizingStyleAdd(&fixture->source, &rules[index]);
    }
    LayoutStyleSourceInit(&fixture->styles);
    fixture->styles.userdata = &fixture->source;
    fixture->styles.ComputedStyleFor = LayoutSizingStyleLookup;
    RoboCheckEqInt(LayoutTreeCreate(&fixture->tree, NULL), RoboOk);
    LayoutBuildOptionsInit(&options);
    options.styles = &fixture->styles;
    RoboCheckEqInt(LayoutTreeBuildFromDocument(fixture->tree, fixture->doc.id,
                                                          &options, NULL, NULL),
                      RoboOk);
}

static void LayoutSizingEnd(LayoutSizingFixture *fixture)
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

#define LAYOUT_SIZING_VIEWPORT_WIDTH 800
#define LAYOUT_SIZING_VIEWPORT_HEIGHT 600

/** One pass at @p viewportWidth, optionally with a measurer installed. */
static void LayoutSizingPass(LayoutSizingFixture *fixture,
                               int32_t viewportWidth,
                               const LayoutTextMetrics *metrics,
                               LayoutStats *outStats)
{
    LayoutContext *context = NULL;
    LayoutSize viewport;
    LayoutStats stats;

    viewport.width = viewportWidth;
    viewport.height = LAYOUT_SIZING_VIEWPORT_HEIGHT;
    RoboCheckEqInt(LayoutContextCreate(&context, NULL), RoboOk);
    RoboCheckEqInt(LayoutContextSetViewport(context, viewport, NULL), RoboOk);
    if (metrics) {
        RoboCheckEqInt(LayoutContextSetTextMetrics(context, metrics), RoboOk);
    }
    RoboCheckEqInt(LayoutContextRun(context, fixture->tree, NULL), RoboOk);
    LayoutContextStats(context, &stats);
    LayoutContextDestroy(context);
    if (outStats) {
        *outStats = stats;
    }
}

/**
 * The layout box of the first @p tag in the fixture's document.
 *
 * By tag rather than by arena index, and that is not a preference: RoboHTML's tree
 * builder inserts the elements a well-formed document requires -- an implicit
 * `<head>` above all -- so a fixture's *indices* are a fact about the parser and a
 * case that counted them would be asserting the wrong thing the day the parser
 * gained one more. The block suite resolves its card the same way and for the same
 * reason.
 */
static LayoutNodeId LayoutSizingBoxFor(LayoutSizingFixture *fixture,
                                                   const char *tag)
{
    HtmlNodeHandle element = LayoutDocumentFind(&fixture->doc, tag);
    LayoutNodeId id;

    RoboCheckEqInt(HtmlNodeHandleIsWellformed(&element), 1);
    id = LayoutObjectIdForDomNode(fixture->tree, &element);
    RoboCheck(id != RoboLayoutNodeIdNone);
    return id;
}

/** The size a placed box was given. */
static LayoutSize LayoutSizingSizeOf(LayoutSizingFixture *fixture,
                                                LayoutNodeId id)
{
    LayoutResult result;
    LayoutSize size;

    RoboCheckEqInt(LayoutObjectResult(fixture->tree, id, &result), RoboOk);
    RoboCheckEqInt((int)result.state, (int)RoboLayoutResultComputed);
    RoboCheckEqInt(LayoutResultSize(&result, &size), RoboOk);
    return size;
}

/* ------------------------------------------------------------------ */
/* The vocabulary                                                      */
/* ------------------------------------------------------------------ */

static void TestSizingDimension(void)
{
    LayoutDimension autoDimension = LayoutDimensionAuto();
    LayoutDimension otherAuto;
    LayoutDimension definite = LayoutDimensionDefinite(10);
    LayoutDimension otherDefinite = LayoutDimensionDefinite(11);
    LayoutDimension unresolved = LayoutDimensionUnresolved();
    LayoutDimension negative = LayoutDimensionDefinite(-1);
    LayoutDimension cleared;
    LayoutDimension *nothing = NULL;
    GfxPixelX length = -1;

    LayoutFixture();

    /* A cleared record is `auto`, not a zero length: 0 is a size an author can ask
     * for, and a default that meant both would be read as one. */
    LayoutDimensionInit(&cleared);
    LayoutDimensionInit(&otherAuto);
    RoboCheckEqInt(LayoutDimensionIsAuto(&cleared), 1);
    RoboCheckEqInt(cleared.length, 0);
    RoboCheckEqInt(LayoutDimensionEqual(&cleared, &autoDimension), 1);
    RoboCheckEqInt(LayoutDimensionEqual(&autoDimension, &otherAuto), 1);

    RoboCheckEqInt(LayoutDimensionIsAuto(&definite), 0);
    RoboCheckEqInt(LayoutDimensionIsDefinite(&definite), 1);
    RoboCheckEqInt(LayoutDimensionIsUnresolved(&definite), 0);
    RoboCheckEqInt(LayoutDimensionLength(&definite, &length), RoboOk);
    RoboCheckEqInt(length, 10);

    /* The distinction the phase exists to keep: `50%` and `auto` resolve the same
     * way today and must not be the same value. */
    RoboCheckEqInt(LayoutDimensionIsAuto(&unresolved), 0);
    RoboCheckEqInt(LayoutDimensionIsUnresolved(&unresolved), 1);
    RoboCheckEqInt(LayoutDimensionEqual(&unresolved, &autoDimension), 0);
    RoboCheckEqInt(LayoutDimensionEqual(&definite, &otherDefinite), 0);

    /* A length read from a non-definite dimension is a miss rather than a zero --
     * and the miss is what tells a caller it has nothing to use. */
    RoboCheckEqInt(LayoutDimensionLength(&autoDimension, &length), RoboNotFound);
    RoboCheckEqInt(length, 10); /* untouched */
    RoboCheckEqInt(LayoutDimensionLength(&unresolved, &length), RoboNotFound);

    RoboCheckEqInt(LayoutDimensionIsValid(&autoDimension), 1);
    RoboCheckEqInt(LayoutDimensionIsValid(&unresolved), 1);
    RoboCheckEqInt(LayoutDimensionIsValid(&definite), 1);
    /* A negative length is refused as a *value*, and it is refused where it is
     * stored rather than clamped where it is used. */
    RoboCheckEqInt(LayoutDimensionIsValid(&negative), 0);

    RoboCheckEqInt(LayoutDimensionIsAuto(nothing), 0);
    RoboCheckEqInt(LayoutDimensionIsDefinite(nothing), 0);
    RoboCheckEqInt(LayoutDimensionIsValid(nothing), 0);
    RoboCheckEqInt(LayoutDimensionLength(nothing, &length), RoboInvalidArgument);
    RoboCheckEqInt(LayoutDimensionLength(&definite, NULL), RoboInvalidArgument);
    RoboCheckEqInt(LayoutDimensionEqual(&definite, nothing), 0);
    RoboCheckEqInt(LayoutDimensionEqual(nothing, nothing), 1);

    RoboCheckStrEq(LayoutDimensionKindName(RoboLayoutDimensionAuto), "auto");
    RoboCheckStrEq(LayoutDimensionKindName(RoboLayoutDimensionDefinite),
                      "definite");
    RoboCheckStrEq(LayoutDimensionKindName(RoboLayoutDimensionUnresolved),
                      "unresolved");
    RoboCheckStrEq(LayoutDimensionKindName((LayoutDimensionKind)77),
                      "unknown");
}

static void TestSizingVocabulary(void)
{
    LayoutFixture();

    RoboCheckEqInt(LayoutDimensionKindIsKnown(RoboLayoutDimensionAuto), 1);
    RoboCheckEqInt(LayoutDimensionKindIsKnown(RoboLayoutDimensionDefinite), 1);
    RoboCheckEqInt(LayoutDimensionKindIsKnown(RoboLayoutDimensionUnresolved), 1);
    RoboCheckEqInt(LayoutDimensionKindIsKnown((LayoutDimensionKind)9), 0);
    RoboCheckEqInt((int)RoboLayoutDimensionKindCount, 3);

    RoboCheckEqInt(LayoutDimensionSourceIsKnown(RoboLayoutDimensionSourceNone),
                      1);
    RoboCheckEqInt(
        LayoutDimensionSourceIsKnown(RoboLayoutDimensionSourceDeclared), 1);
    RoboCheckEqInt(
        LayoutDimensionSourceIsKnown(RoboLayoutDimensionSourceAvailable), 1);
    RoboCheckEqInt(
        LayoutDimensionSourceIsKnown(RoboLayoutDimensionSourceContent), 1);
    RoboCheckEqInt(LayoutDimensionSourceIsKnown((LayoutDimensionSource)9),
                      0);
    RoboCheckEqInt((int)RoboLayoutDimensionSourceCount, 4);

    RoboCheckStrEq(LayoutDimensionSourceName(RoboLayoutDimensionSourceNone),
                      "none");
    RoboCheckStrEq(LayoutDimensionSourceName(RoboLayoutDimensionSourceDeclared),
                      "declared");
    RoboCheckStrEq(LayoutDimensionSourceName(RoboLayoutDimensionSourceAvailable),
                      "available");
    RoboCheckStrEq(LayoutDimensionSourceName(RoboLayoutDimensionSourceContent),
                      "content");
    RoboCheckStrEq(LayoutDimensionSourceName((LayoutDimensionSource)77),
                      "unknown");
    RoboCheckEqInt(LayoutDimensionSourceEqual(RoboLayoutDimensionSourceDeclared,
                                                        RoboLayoutDimensionSourceDeclared),
                      1);
    RoboCheckEqInt(LayoutDimensionSourceEqual(RoboLayoutDimensionSourceDeclared,
                                                        RoboLayoutDimensionSourceContent),
                      0);
}

/* ------------------------------------------------------------------ */
/* The record                                                          */
/* ------------------------------------------------------------------ */

static void TestSizingRecord(void)
{
    LayoutSizing sizing;
    LayoutSizing other;
    LayoutDimension dimension;
    LayoutSize value;
    GfxPixelX width = -1;
    GfxPixelY height = -1;

    LayoutFixture();

    LayoutSizingInit(&sizing);
    /* A cleared record describes a box nothing has been said about, and it is a
     * *legal* record rather than an invalid one. */
    RoboCheckEqInt(LayoutSizingIsValid(&sizing), 1);
    RoboCheckEqInt(LayoutDimensionIsAuto(&sizing.width), 1);
    RoboCheckEqInt(LayoutDimensionIsAuto(&sizing.height), 1);
    RoboCheckEqInt(LayoutSizingHasAvailableWidth(&sizing), 0);
    RoboCheckEqInt(LayoutSizingHasAvailableHeight(&sizing), 0);
    RoboCheckEqInt(LayoutSizingHasMinimum(&sizing), 0);
    RoboCheckEqInt(LayoutSizingHasMaximum(&sizing), 0);
    RoboCheckEqInt(LayoutSizingHasContent(&sizing), 0);
    RoboCheckEqInt(LayoutSizingWidthSource(&sizing),
                      RoboLayoutDimensionSourceNone);
    RoboCheckEqInt(LayoutSizingHeightSource(&sizing),
                      RoboLayoutDimensionSourceNone);
    RoboCheckEqInt(LayoutSizingResolved(&sizing, &value), RoboNotFound);

    /* The two availability flags are separate, and that is the phase's answer to a
     * block box: it is given a width and *no* height, because a block's height is its
     * contents'. A single flag would have to claim the height was zero. */
    RoboCheckEqInt(LayoutSizingSetAvailableWidth(&sizing, 640, NULL), RoboOk);
    RoboCheckEqInt(LayoutSizingHasAvailableWidth(&sizing), 1);
    RoboCheckEqInt(LayoutSizingHasAvailableHeight(&sizing), 0);
    RoboCheckEqInt(LayoutSizingAvailableWidth(&sizing, &width), RoboOk);
    RoboCheckEqInt(width, 640);
    RoboCheckEqInt(LayoutSizingAvailableHeight(&sizing, &height),
                      RoboNotFound);

    RoboCheckEqInt(LayoutSizingSetAvailableHeight(&sizing, 480, NULL), RoboOk);
    RoboCheckEqInt(LayoutSizingAvailableHeight(&sizing, &height), RoboOk);
    RoboCheckEqInt(height, 480);

    /* A known zero is accepted and a negative extent is not: the two are different
     * inputs and a caller that sent -1 meant something it cannot mean. */
    RoboCheckEqInt(LayoutSizingSetAvailableWidth(&sizing, 0, NULL), RoboOk);
    RoboCheckEqInt(LayoutSizingAvailableWidth(&sizing, &width), RoboOk);
    RoboCheckEqInt(width, 0);

    RoboCheckEqInt(LayoutSizingSetAvailableWidth(&sizing, -1, NULL),
                      RoboInvalidArgument);
    RoboCheckEqInt(LayoutSizingSetAvailableHeight(&sizing, -1, NULL),
                      RoboInvalidArgument);
    RoboCheckEqInt(LayoutSizingSetAvailableWidth(NULL, 1, NULL),
                      RoboInvalidArgument);

    /* A negative declared length is refused where it is *stored*. */
    RoboCheckEqInt(LayoutSizingSetWidth(&sizing,
                                                  LayoutDimensionDefinite(-4), NULL),
                      RoboInvalidArgument);
    RoboCheckEqInt(LayoutSizingSetWidth(&sizing,
                                                  LayoutDimensionDefinite(120), NULL),
                      RoboOk);
    RoboCheckEqInt(LayoutSizingSetHeight(&sizing,
                                                   LayoutDimensionUnresolved(), NULL),
                      RoboOk);
    RoboCheckEqInt(LayoutSizingWidth(&sizing).kind,
                      RoboLayoutDimensionDefinite);
    RoboCheckEqInt(LayoutSizingWidth(&sizing).length, 120);
    RoboCheckEqInt(LayoutSizingHeight(&sizing).kind,
                      RoboLayoutDimensionUnresolved);
    RoboCheckEqInt(LayoutSizingSetWidth(NULL, LayoutDimensionAuto(), NULL),
                      RoboInvalidArgument);
    RoboCheckEqInt(LayoutSizingSetHeight(NULL, LayoutDimensionAuto(), NULL),
                      RoboInvalidArgument);

    /* The bounds, and the one thing this record has to catch before the clamp sees
     * it: a pair that contradicts itself is *invalid*, not clamped into an answer. */
    value.width = 100;
    value.height = 50;
    RoboCheckEqInt(LayoutSizingSetMinimum(&sizing, value, NULL), RoboOk);
    RoboCheckEqInt(LayoutSizingHasMinimum(&sizing), 1);
    RoboCheckEqInt(LayoutSizingIsValid(&sizing), 1);
    value.width = 40;
    value.height = 50;
    RoboCheckEqInt(LayoutSizingSetMaximum(&sizing, value, NULL), RoboOk);
    /* 100 minimum against a 40 maximum: no size can satisfy both. */
    RoboCheckEqInt(LayoutSizingIsValid(&sizing), 0);
    RoboCheckEqInt(LayoutSizingMinimum(&sizing, &value), RoboOk);
    RoboCheckEqInt(value.width, 100);
    RoboCheckEqInt(LayoutSizingMaximum(&sizing, &value), RoboOk);
    RoboCheckEqInt(value.width, 40);
    value.width = -1;
    RoboCheckEqInt(LayoutSizingSetMinimum(&sizing, value, NULL),
                      RoboInvalidArgument);

    /* The contents, and its own refusal. */
    value.width = 30;
    value.height = 7;
    RoboCheckEqInt(LayoutSizingSetContent(&sizing, value, NULL), RoboOk);
    RoboCheckEqInt(LayoutSizingHasContent(&sizing), 1);
    RoboCheckEqInt(LayoutSizingContent(&sizing, &value), RoboOk);
    RoboCheckEqInt(value.height, 7);
    value.height = -1;
    RoboCheckEqInt(LayoutSizingSetContent(&sizing, value, NULL),
                      RoboInvalidArgument);

    RoboCheckEqInt(LayoutSizingWidthClamped(&sizing), 0);
    RoboCheckEqInt(LayoutSizingHeightClamped(&sizing), 0);

    /* Equality, and the rule every record in this module follows: only the fields
     * that are *set* are compared, because an unset field's bytes are the caller's
     * stack. */
    other = sizing;
    RoboCheckEqInt(LayoutSizingEqual(&sizing, &other), 1);
    other.hasContent = 0;
    RoboCheckEqInt(LayoutSizingEqual(&sizing, &other), 0);
    RoboCheckEqInt(LayoutSizingEqual(&sizing, NULL), 0);
    RoboCheckEqInt(LayoutSizingEqual(NULL, NULL), 1);

    /* Every getter answers for NULL, because a caller does not have to own a record
     * to ask about one -- and a missing record is "nothing is known", which is what
     * the cleared state already means. */
    RoboCheckEqInt(LayoutSizingHasAvailableWidth(NULL), 0);
    RoboCheckEqInt(LayoutSizingHasMinimum(NULL), 0);
    RoboCheckEqInt(LayoutSizingHasContent(NULL), 0);
    RoboCheckEqInt(LayoutSizingWidthClamped(NULL), 0);
    RoboCheckEqInt(LayoutSizingHeightClamped(NULL), 0);
    dimension = LayoutSizingWidth(NULL);
    RoboCheckEqInt(LayoutDimensionIsAuto(&dimension), 1);
    dimension = LayoutSizingHeight(NULL);
    RoboCheckEqInt(LayoutDimensionIsAuto(&dimension), 1);
    RoboCheckEqInt(LayoutSizingWidthSource(NULL), RoboLayoutDimensionSourceNone);
    RoboCheckEqInt(LayoutSizingHeightSource(NULL),
                      RoboLayoutDimensionSourceNone);
    RoboCheckEqInt(LayoutSizingAvailableWidth(NULL, &width), RoboInvalidArgument);
    RoboCheckEqInt(LayoutSizingMinimum(NULL, &value), RoboInvalidArgument);
    RoboCheckEqInt(LayoutSizingMaximum(NULL, &value), RoboInvalidArgument);
    RoboCheckEqInt(LayoutSizingContent(NULL, &value), RoboInvalidArgument);
    RoboCheckEqInt(LayoutSizingResolved(NULL, &value), RoboInvalidArgument);
    RoboCheckEqInt(LayoutSizingIsValid(NULL), 0);

    LayoutSizingClear(&sizing);
    RoboCheckEqInt(LayoutDimensionIsAuto(&sizing.width), 1);
    RoboCheckEqInt(LayoutSizingHasMinimum(&sizing), 0);
    LayoutSizingClear(NULL);
}

/* ------------------------------------------------------------------ */
/* Resolving                                                           */
/* ------------------------------------------------------------------ */

static void TestSizingWidth(void)
{
    LayoutSizing sizing;
    LayoutSize resolved;
    GfxPixelX width = 0;

    LayoutFixture();

    /* A declared length is taken as it stands, and the *source* says so. */
    LayoutSizingInit(&sizing);
    RoboCheckEqInt(LayoutSizingSetAvailableWidth(&sizing, 640, NULL), RoboOk);
    RoboCheckEqInt(LayoutSizingSetWidth(&sizing,
                                                  LayoutDimensionDefinite(300), NULL),
                      RoboOk);
    RoboCheckEqInt(LayoutSizingResolveWidth(&sizing, NULL), RoboOk);
    RoboCheckEqInt(LayoutSizingAvailableWidth(&sizing, &width), RoboOk);
    RoboCheckEqInt(width, 640);
    RoboCheckEqInt(sizing.resolved.width, 300);
    RoboCheckEqInt(LayoutSizingWidthSource(&sizing),
                      RoboLayoutDimensionSourceDeclared);
    RoboCheckEqInt(LayoutSizingWidthClamped(&sizing), 0);

    /* An `auto` width fills the containing block, and the source says *that* -- the
     * same number from a different question, which is the whole reason the source
     * exists. */
    LayoutSizingInit(&sizing);
    RoboCheckEqInt(LayoutSizingSetAvailableWidth(&sizing, 640, NULL), RoboOk);
    RoboCheckEqInt(LayoutSizingResolveWidth(&sizing, NULL), RoboOk);
    RoboCheckEqInt(sizing.resolved.width, 640);
    RoboCheckEqInt(LayoutSizingWidthSource(&sizing),
                      RoboLayoutDimensionSourceAvailable);

    /* An unresolvable width takes the same branch and the same source, and the
     * *dimension* remembers what it was: this is the merge happening at resolution
     * rather than at the seam. */
    LayoutSizingInit(&sizing);
    RoboCheckEqInt(LayoutSizingSetAvailableWidth(&sizing, 640, NULL), RoboOk);
    RoboCheckEqInt(LayoutSizingSetWidth(&sizing,
                                                  LayoutDimensionUnresolved(), NULL),
                      RoboOk);
    RoboCheckEqInt(LayoutSizingResolveWidth(&sizing, NULL), RoboOk);
    RoboCheckEqInt(sizing.resolved.width, 640);
    RoboCheckEqInt(LayoutSizingWidthSource(&sizing),
                      RoboLayoutDimensionSourceAvailable);
    RoboCheckEqInt(sizing.width.kind, RoboLayoutDimensionUnresolved);

    /* An `auto` width with no containing block is a *failure*, not a zero: a
     * caller that has not said how wide the containing block is has not given this
     * layer an input, and a zero-sized box would be the silent wrong answer. */
    LayoutSizingInit(&sizing);
    RoboCheckEqInt(LayoutSizingResolveWidth(&sizing, NULL), RoboNotFound);
    RoboCheckEqInt(sizing.resolved.width, 0);

    /* A known zero *is* an answer, and it is not the failure above. */
    LayoutSizingInit(&sizing);
    RoboCheckEqInt(LayoutSizingSetAvailableWidth(&sizing, 0, NULL), RoboOk);
    RoboCheckEqInt(LayoutSizingResolveWidth(&sizing, NULL), RoboOk);
    RoboCheckEqInt(sizing.resolved.width, 0);

    /* An undefined kind is refused rather than quietly treated as `auto`. */
    LayoutSizingInit(&sizing);
    RoboCheckEqInt(LayoutSizingSetAvailableWidth(&sizing, 640, NULL), RoboOk);
    sizing.width.kind = (LayoutDimensionKind)42;
    RoboCheckEqInt(LayoutSizingResolveWidth(&sizing, NULL), RoboInvalidArgument);
    RoboCheckEqInt(LayoutSizingResolveWidth(NULL, NULL), RoboInvalidArgument);

    /* The pair, and the half that is not rolled back on failure. */
    LayoutSizingInit(&sizing);
    RoboCheckEqInt(LayoutSizingSetAvailableWidth(&sizing, 640, NULL), RoboOk);
    RoboCheckEqInt(LayoutSizingResolve(&sizing, NULL), RoboNotFound);
    RoboCheckEqInt(LayoutSizingResolved(&sizing, &resolved), RoboNotFound);
    RoboCheckEqInt(sizing.resolved.width, 640); /* resolved, and kept */
    RoboCheckEqInt(LayoutSizingWidthSource(&sizing),
                      RoboLayoutDimensionSourceAvailable);
    RoboCheckEqInt(LayoutSizingHeightSource(&sizing),
                      RoboLayoutDimensionSourceNone);
    RoboCheckEqInt(LayoutSizingResolve(NULL, NULL), RoboInvalidArgument);
}

static void TestSizingHeight(void)
{
    LayoutSizing sizing;
    LayoutSize content;
    LayoutSize resolved;

    LayoutFixture();

    content.width = 100;
    content.height = 44;

    /* A declared height, and an `auto` height from the contents. */
    LayoutSizingInit(&sizing);
    RoboCheckEqInt(LayoutSizingSetHeight(&sizing,
                                                   LayoutDimensionDefinite(90), NULL),
                      RoboOk);
    RoboCheckEqInt(LayoutSizingSetContent(&sizing, content, NULL), RoboOk);
    RoboCheckEqInt(LayoutSizingResolveHeight(&sizing, NULL), RoboOk);
    RoboCheckEqInt(sizing.resolved.height, 90);
    RoboCheckEqInt(LayoutSizingHeightSource(&sizing),
                      RoboLayoutDimensionSourceDeclared);

    LayoutSizingInit(&sizing);
    RoboCheckEqInt(LayoutSizingSetContent(&sizing, content, NULL), RoboOk);
    RoboCheckEqInt(LayoutSizingResolveHeight(&sizing, NULL), RoboOk);
    RoboCheckEqInt(sizing.resolved.height, 44);
    RoboCheckEqInt(LayoutSizingHeightSource(&sizing),
                      RoboLayoutDimensionSourceContent);

    /* A percentage height is `UNRESOLVED` and takes the contents -- today. There is
     * deliberately no available *height* anywhere in this case, which is the phase's
     * statement that a block box's height is never its containing block's. */
    LayoutSizingInit(&sizing);
    RoboCheckEqInt(LayoutSizingSetAvailableHeight(&sizing, 600, NULL), RoboOk);
    RoboCheckEqInt(LayoutSizingSetHeight(&sizing,
                                                   LayoutDimensionUnresolved(), NULL),
                      RoboOk);
    RoboCheckEqInt(LayoutSizingSetContent(&sizing, content, NULL), RoboOk);
    RoboCheckEqInt(LayoutSizingResolveHeight(&sizing, NULL), RoboOk);
    RoboCheckEqInt(sizing.resolved.height, 44);
    RoboCheckEqInt(LayoutSizingHeightSource(&sizing),
                      RoboLayoutDimensionSourceContent);
    RoboCheckEqInt(sizing.height.kind, RoboLayoutDimensionUnresolved);

    /* An `auto` height with no contents extent is a *different* failure from an
     * `auto` width with no available width, and the two are told apart by which half
     * failed rather than by one code for both. */
    LayoutSizingInit(&sizing);
    RoboCheckEqInt(LayoutSizingResolveHeight(&sizing, NULL), RoboNotFound);

    /* A contents extent of zero is an answer: an empty box is zero tall. */
    content.width = 0;
    content.height = 0;
    LayoutSizingInit(&sizing);
    RoboCheckEqInt(LayoutSizingSetContent(&sizing, content, NULL), RoboOk);
    RoboCheckEqInt(LayoutSizingResolveHeight(&sizing, NULL), RoboOk);
    RoboCheckEqInt(sizing.resolved.height, 0);

    /* Both at once, which is the block pass's shape. */
    content.height = 25;
    LayoutSizingInit(&sizing);
    RoboCheckEqInt(LayoutSizingSetAvailableWidth(&sizing, 500, NULL), RoboOk);
    RoboCheckEqInt(LayoutSizingSetContent(&sizing, content, NULL), RoboOk);
    RoboCheckEqInt(LayoutSizingResolve(&sizing, NULL), RoboOk);
    RoboCheckEqInt(LayoutSizingResolved(&sizing, &resolved), RoboOk);
    RoboCheckEqInt(resolved.width, 500);
    RoboCheckEqInt(resolved.height, 25);

    sizing.height.kind = (LayoutDimensionKind)42;
    RoboCheckEqInt(LayoutSizingResolveHeight(&sizing, NULL), RoboInvalidArgument);
    RoboCheckEqInt(LayoutSizingResolveHeight(NULL, NULL), RoboInvalidArgument);
}

static void TestSizingClamp(void)
{
    LayoutSizing sizing;
    LayoutSizing none;
    LayoutSize bound;

    LayoutFixture();

    /* A maximum pulls the declared width down, and the flag says it moved. */
    LayoutSizingInit(&sizing);
    RoboCheckEqInt(LayoutSizingSetWidth(&sizing,
                                                  LayoutDimensionDefinite(300), NULL),
                      RoboOk);
    bound.width = 200;
    bound.height = 1000;
    RoboCheckEqInt(LayoutSizingSetMaximum(&sizing, bound, NULL), RoboOk);
    RoboCheckEqInt(LayoutSizingResolveWidth(&sizing, NULL), RoboOk);
    RoboCheckEqInt(sizing.resolved.width, 200);
    RoboCheckEqInt(LayoutSizingWidthClamped(&sizing), 1);
    /* The declared dimension is untouched: the *used* value moved, not the record of
     * what was asked for. */
    RoboCheckEqInt(sizing.width.length, 300);

    /* A minimum pulls an `auto` width up. */
    LayoutSizingInit(&sizing);
    RoboCheckEqInt(LayoutSizingSetAvailableWidth(&sizing, 100, NULL), RoboOk);
    bound.width = 250;
    bound.height = 0;
    RoboCheckEqInt(LayoutSizingSetMinimum(&sizing, bound, NULL), RoboOk);
    RoboCheckEqInt(LayoutSizingResolveWidth(&sizing, NULL), RoboOk);
    RoboCheckEqInt(sizing.resolved.width, 250);
    RoboCheckEqInt(LayoutSizingWidthClamped(&sizing), 1);

    /* Minimum wins a tie -- 10.1's clamp decided that once, and this record reaches it
     * rather than restating it. */
    LayoutSizingInit(&sizing);
    RoboCheckEqInt(LayoutSizingSetAvailableWidth(&sizing, 100, NULL), RoboOk);
    bound.width = 300;
    bound.height = 0;
    RoboCheckEqInt(LayoutSizingSetMinimum(&sizing, bound, NULL), RoboOk);
    bound.width = 300;
    bound.height = 0;
    RoboCheckEqInt(LayoutSizingSetMaximum(&sizing, bound, NULL), RoboOk);
    RoboCheckEqInt(LayoutSizingResolveWidth(&sizing, NULL), RoboOk);
    RoboCheckEqInt(sizing.resolved.width, 300);

    /* A height clamp, which is the same arithmetic on the other field. */
    LayoutSizingInit(&sizing);
    bound.width = 0;
    bound.height = 120;
    RoboCheckEqInt(LayoutSizingSetMinimum(&sizing, bound, NULL), RoboOk);
    RoboCheckEqInt(LayoutSizingResolveHeight(&sizing, NULL), RoboNotFound);
    bound.width = 0;
    bound.height = 0;
    {
        LayoutSize content;

        content.width = 0;
        content.height = 40;
        RoboCheckEqInt(LayoutSizingSetContent(&sizing, content, NULL), RoboOk);
    }
    RoboCheckEqInt(LayoutSizingResolveHeight(&sizing, NULL), RoboOk);
    RoboCheckEqInt(sizing.resolved.height, 120);
    RoboCheckEqInt(LayoutSizingHeightClamped(&sizing), 1);

    /* Contradictory bounds are *reported*, in the same code path that would have
     * clamped, rather than resolved in either's favour. */
    {
        LayoutSizing contradictory;
        LayoutSize contradicting;

        LayoutSizingInit(&contradictory);
        contradicting.width = 80;
        contradicting.height = 0;
        RoboCheckEqInt(LayoutSizingSetMinimum(&contradictory, contradicting, NULL),
                          RoboOk);
        contradicting.width = 40;
        contradicting.height = 0;
        RoboCheckEqInt(LayoutSizingSetMaximum(&contradictory, contradicting, NULL),
                          RoboOk);
        RoboCheckEqInt(LayoutSizingIsValid(&contradictory), 0);
        RoboCheckEqInt(LayoutSizingResolveWidth(&contradictory, NULL),
                          RoboNotFound);
    }

    /* Nothing bound is not a clamp that moved nothing -- the flag stays clear, so a
     * caller can tell "the bounds left it alone" from "there were no bounds". */
    LayoutSizingInit(&none);
    RoboCheckEqInt(LayoutSizingSetWidth(&none,
                                                  LayoutDimensionDefinite(77), NULL),
                      RoboOk);
    RoboCheckEqInt(LayoutSizingResolveWidth(&none, NULL), RoboOk);
    RoboCheckEqInt(none.resolved.width, 77);
    RoboCheckEqInt(LayoutSizingWidthClamped(&none), 0);
}

static void TestSizingReresolve(void)
{
    LayoutSizing sizing;
    LayoutSizing first;
    LayoutSize content;
    size_t round;

    LayoutFixture();

    /*
     * The re-layout story, and it is a property of the shape rather than a feature:
     * nothing here is cached, so resolving again over a changed input gets the new
     * answer and no stale half of the old one. This is what §17 of the phase asks
     * for and what a caller changing a viewport, a declaration or a font relies on.
     */
    LayoutSizingInit(&sizing);
    RoboCheckEqInt(LayoutSizingSetAvailableWidth(&sizing, 800, NULL), RoboOk);
    content.width = 800;
    content.height = 30;
    RoboCheckEqInt(LayoutSizingSetContent(&sizing, content, NULL), RoboOk);
    RoboCheckEqInt(LayoutSizingResolve(&sizing, NULL), RoboOk);
    RoboCheckEqInt(sizing.resolved.width, 800);

    RoboCheckEqInt(LayoutSizingSetAvailableWidth(&sizing, 360, NULL), RoboOk);
    content.width = 360;
    content.height = 60;
    RoboCheckEqInt(LayoutSizingSetContent(&sizing, content, NULL), RoboOk);
    RoboCheckEqInt(LayoutSizingResolve(&sizing, NULL), RoboOk);
    RoboCheckEqInt(sizing.resolved.width, 360);
    RoboCheckEqInt(sizing.resolved.height, 60);
    RoboCheckEqInt(LayoutSizingWidthSource(&sizing),
                      RoboLayoutDimensionSourceAvailable);
    RoboCheckEqInt(LayoutSizingHeightSource(&sizing),
                      RoboLayoutDimensionSourceContent);

    /* A declared width arriving later replaces the filled one, source and all. */
    RoboCheckEqInt(LayoutSizingSetWidth(&sizing,
                                                  LayoutDimensionDefinite(120), NULL),
                      RoboOk);
    RoboCheckEqInt(LayoutSizingResolve(&sizing, NULL), RoboOk);
    RoboCheckEqInt(sizing.resolved.width, 120);
    RoboCheckEqInt(LayoutSizingWidthSource(&sizing),
                      RoboLayoutDimensionSourceDeclared);
    RoboCheckEqInt(sizing.resolved.height, 60);

    /* And resolving twice over unchanged inputs is idempotent, which is the
     * determinism every later goal's invalidation scheme depends on. */
    first = sizing;
    for (round = 0u; round < 5u; round++) {
        LayoutSizing again = sizing;

        RoboCheckEqInt(LayoutSizingResolve(&again, NULL), RoboOk);
        RoboCheckEqInt(LayoutSizingEqual(&again, &first), 1);
    }
}

/* ------------------------------------------------------------------ */
/* The CSS seam                                                        */
/* ------------------------------------------------------------------ */

/**
 * A computed style carrying @p declarations, for the seam cases below.
 *
 * The suite's own builder, reached without a document: the seam is one function and
 * these cases are about what it answers for a style nobody had to lay out.
 */
static CssComputedStyle *LayoutSizingSeamStyle(const char *name,
                                                          LayoutSizingDeclKind kind,
                                                          double number,
                                                          const char *keyword)
{
    CssComputedStyle *style = NULL;
    CssComputedValue value = RoboCssCleared;
    CssValue specified = RoboCssCleared;

    LayoutFixture();
    if (!name) {
        return NULL;
    }
    RoboCheckEqInt(CssComputedStyleCreate(&style, NULL), RoboOk);
    switch (kind) {
    case LAYOUT_SIZING_DECL_PERCENTAGE:
        RoboCheckEqInt(CssComputedValueMakePercentage(&value, number, 0, NULL),
                          RoboOk);
        break;
    case LAYOUT_SIZING_DECL_EM:
        RoboCheckEqInt(CssComputedValueMakeLength(&value, number, RoboCssUnitEm,
                                                             0, NULL),
                          RoboOk);
        break;
    case LAYOUT_SIZING_DECL_KEYWORD:
        RoboCheckEqInt(
            CssComputedValueMakeKeyword(&value, CssSliceFromText(keyword), NULL),
            RoboOk);
        break;
    case LAYOUT_SIZING_DECL_PX:
    default:
        RoboCheckEqInt(CssComputedValueMakeLength(&value, number, RoboCssUnitPx,
                                                             0, NULL),
                          RoboOk);
        break;
    }
    RoboCheckEqInt(CssComputedStyleSetEntry(style, RoboCssPropertyIdNone,
                                                       CssSliceFromText(name), 0u,
                                                       &specified, &value, NULL),
                      RoboOk);
    return style;
}

static void TestSizingSeam(void)
{
    CssComputedStyle *absent = NULL;
    CssComputedStyle *keywordAuto;
    CssComputedStyle *keywordOther;
    CssComputedStyle *px;
    CssComputedStyle *fractional;
    CssComputedStyle *percent;
    CssComputedStyle *em;
    LayoutDimension dimension;
    GfxPixelX length = 0;

    /* A style with no entry at all: `auto`, by CSS's initial value. */
    RoboCheckEqInt(CssComputedStyleCreate(&absent, NULL), RoboOk);

    keywordAuto = LayoutSizingSeamStyle("width", LAYOUT_SIZING_DECL_KEYWORD, 0.0, "auto");
    keywordOther = LayoutSizingSeamStyle("width", LAYOUT_SIZING_DECL_KEYWORD, 0.0,
                                             "inherit");
    px = LayoutSizingSeamStyle("width", LAYOUT_SIZING_DECL_PX, 300.0, NULL);
    fractional = LayoutSizingSeamStyle("width", LAYOUT_SIZING_DECL_PX, 10.5, NULL);
    percent = LayoutSizingSeamStyle("width", LAYOUT_SIZING_DECL_PERCENTAGE, 50.0, NULL);
    em = LayoutSizingSeamStyle("width", LAYOUT_SIZING_DECL_EM, 2.0, NULL);

    /* Absent, and the keyword `auto`: one kind, because CSS makes them one value. */
    LayoutDimensionInit(&dimension);
    RoboCheckEqInt(LayoutSizingReadDimension(absent, "width", &dimension), 1);
    RoboCheckEqInt(LayoutDimensionIsAuto(&dimension), 1);
    RoboCheckEqInt(LayoutSizingReadDimension(keywordAuto, "width", &dimension), 1);
    RoboCheckEqInt(LayoutDimensionIsAuto(&dimension), 1);

    /* A keyword that names no length is *declared* and unusable, which is
     * `UNRESOLVED` and not `auto`: an author asked for something, and folding that
     * into CSS's default would lose the fact. */
    RoboCheckEqInt(LayoutSizingReadDimension(keywordOther, "width", &dimension), 1);
    RoboCheckEqInt(LayoutDimensionIsUnresolved(&dimension), 1);

    /* A px length, and the rounding: `10.5px` becomes 11, because truncating would
     * make every fractional declared width come out a pixel short of what was
     * written. */
    RoboCheckEqInt(LayoutSizingReadDimension(px, "width", &dimension), 1);
    RoboCheckEqInt(LayoutDimensionIsDefinite(&dimension), 1);
    RoboCheckEqInt(LayoutDimensionLength(&dimension, &length), RoboOk);
    RoboCheckEqInt(length, 300);
    RoboCheckEqInt(LayoutSizingReadDimension(fractional, "width", &dimension), 1);
    RoboCheckEqInt(LayoutDimensionLength(&dimension, &length), RoboOk);
    RoboCheckEqInt(length, 11);

    /* A percentage and a relative unit: `UNRESOLVED`, which is the kind that will
     * change when a containing dimension is known. */
    RoboCheckEqInt(LayoutSizingReadDimension(percent, "width", &dimension), 1);
    RoboCheckEqInt(LayoutDimensionIsUnresolved(&dimension), 1);
    RoboCheckEqInt(LayoutSizingReadDimension(em, "width", &dimension), 1);
    RoboCheckEqInt(LayoutDimensionIsUnresolved(&dimension), 1);

    /* A property the style does not declare is `auto` even when the style is
     * rich -- the read is per property, not per style. */
    RoboCheckEqInt(LayoutSizingReadDimension(px, "height", &dimension), 1);
    RoboCheckEqInt(LayoutDimensionIsAuto(&dimension), 1);

    /* And the contract that lets a caller read four properties without a branch:
     * `out` is left as it found it when the question cannot be asked. */
    dimension = LayoutDimensionDefinite(9);
    RoboCheckEqInt(LayoutSizingReadDimension(NULL, "width", &dimension), 0);
    RoboCheckEqInt(LayoutDimensionLength(&dimension, &length), RoboOk);
    RoboCheckEqInt(length, 9);
    RoboCheckEqInt(LayoutSizingReadDimension(px, NULL, &dimension), 0);
    RoboCheckEqInt(LayoutSizingReadDimension(px, "width", NULL), 0);

    CssComputedStyleDestroy(absent);
    CssComputedStyleDestroy(keywordAuto);
    CssComputedStyleDestroy(keywordOther);
    CssComputedStyleDestroy(px);
    CssComputedStyleDestroy(fractional);
    CssComputedStyleDestroy(percent);
    CssComputedStyleDestroy(em);
}

/* ------------------------------------------------------------------ */
/* The pass                                                            */
/* ------------------------------------------------------------------ */

/**
 * A declared width reaches the geometry -- *and* the width the children are laid
 * out into.
 *
 * The second half is the phase's real content. A build that resolved its own width
 * and then handed its children the containing block's would put a 300px box's block
 * child at 800px wide, which is a plausible-looking number and the wrong document.
 */
static void TestSizingBlockDeclaredWidth(void)
{
    LayoutSizingFixture fixture;
    LayoutSizingRule rules[4];
    LayoutStats stats;
    LayoutSize size;

    LayoutSizingRuleInit(&rules[0], "html", RoboCssDisplayBlock);
    LayoutSizingRuleInit(&rules[1], "body", RoboCssDisplayBlock);
    LayoutSizingRuleInit(&rules[2], "div", RoboCssDisplayBlock);
    LayoutSizingRulePx(&rules[2], "width", 300.0);
    LayoutSizingRuleInit(&rules[3], "section", RoboCssDisplayBlock);

    LayoutSizingBegin(&fixture,
                        "<html><body><div><section></section></div></body></html>", rules, 4u);
    LayoutSizingPass(&fixture, LAYOUT_SIZING_VIEWPORT_WIDTH, NULL, &stats);

    /* html, head, body, div, section: five boxes, of which `<head>` is inline-level
     * because nothing declared a display for it -- which is CSS's initial value and
     * this fixture's way of saying "this document declares what it declares". */
    RoboCheckEqInt(LayoutTreeObjectCount(fixture.tree), 5u);
    RoboCheckEqInt(stats.block.blocksPlaced, 4u);
    /* One box obeyed its author and three filled what they were given. */
    RoboCheckEqInt(stats.block.widthsDeclared, 1u);
    RoboCheckEqInt(stats.block.widthsFilled, 3u);
    RoboCheckEqInt(stats.block.widthsDeclared + stats.block.widthsFilled, 4u);

    size = LayoutSizingSizeOf(&fixture, LayoutSizingBoxFor(&fixture, "div"));
    RoboCheckEqInt(size.width, 300);
    /* The child of the 300px box is 300 wide, not the viewport's 800 -- which is
     * what makes the declared width *contain* something rather than only describe
     * the box itself. */
    size = LayoutSizingSizeOf(&fixture, LayoutSizingBoxFor(&fixture, "section"));
    RoboCheckEqInt(size.width, 300);
    /* And an ancestor above the declared box still fills the viewport. */
    size = LayoutSizingSizeOf(&fixture, LayoutSizingBoxFor(&fixture, "body"));
    RoboCheckEqInt(size.width, LAYOUT_SIZING_VIEWPORT_WIDTH);

    LayoutSizingEnd(&fixture);
}

/**
 * An `auto` width fills the containing block, at every level -- 10.3's rule, now
 * *decided* by the sizing layer rather than assumed by it.
 */
static void TestSizingBlockAutoWidth(void)
{
    LayoutSizingFixture fixture;
    LayoutSizingRule rules[3];
    LayoutStats stats;
    LayoutSize size;

    LayoutSizingRuleInit(&rules[0], "html", RoboCssDisplayBlock);
    LayoutSizingRuleInit(&rules[1], "body", RoboCssDisplayBlock);
    LayoutSizingRuleInit(&rules[2], "div", RoboCssDisplayBlock);

    LayoutSizingBegin(&fixture, "<html><body><div></div></body></html>", rules, 3u);
    LayoutSizingPass(&fixture, 640, NULL, &stats);

    RoboCheckEqInt(stats.block.widthsDeclared, 0u);
    RoboCheckEqInt(stats.block.widthsFilled, 3u);

    size = LayoutSizingSizeOf(&fixture, LayoutSizingBoxFor(&fixture, "html"));
    RoboCheckEqInt(size.width, 640);
    size = LayoutSizingSizeOf(&fixture, LayoutSizingBoxFor(&fixture, "div"));
    RoboCheckEqInt(size.width, 640);

    /* A viewport is an *input*, so a second pass at another width is another
     * document: 800 here, and the nested box is 800 too. */
    LayoutSizingPass(&fixture, 800, NULL, &stats);
    size = LayoutSizingSizeOf(&fixture, LayoutSizingBoxFor(&fixture, "div"));
    RoboCheckEqInt(size.width, 800);

    LayoutSizingEnd(&fixture);
}

/**
 * A percentage width is *read* as `UNRESOLVED` and *resolved* by filling the
 * containing block, and the two halves of that are both asserted.
 *
 * The dimension is the half a later goal will change, so the case reaches for it
 * through the seam rather than only through the geometry -- a build that folded
 * `50%` into `auto` at the seam would produce the same 300px box here and be wrong
 * about the document.
 */
static void TestSizingBlockUnresolvedWidth(void)
{
    LayoutSizingFixture fixture;
    LayoutSizingRule rules[3];
    CssComputedStyle *style;
    LayoutDimension dimension;
    LayoutStats stats;
    LayoutSize size;

    LayoutSizingRuleInit(&rules[0], "html", RoboCssDisplayBlock);
    LayoutSizingRuleInit(&rules[1], "body", RoboCssDisplayBlock);
    LayoutSizingRuleInit(&rules[2], "div", RoboCssDisplayBlock);
    LayoutSizingRulePercentage(&rules[2], "width", 50.0);

    LayoutSizingBegin(&fixture, "<html><body><div></div></body></html>", rules, 3u);
    LayoutSizingPass(&fixture, 300, NULL, &stats);

    /* The seam's answer, on the very style the pass used. */
    style = fixture.source.rules[2].style;
    RoboCheckEqInt(LayoutSizingReadDimension(style, "width", &dimension), 1);
    RoboCheckEqInt(LayoutDimensionIsUnresolved(&dimension), 1);
    RoboCheckEqInt(LayoutDimensionIsAuto(&dimension), 0);

    /* And the pass's: the containing block's width, counted as a *filled* width
     * rather than a declared one, because that is what it is. */
    size = LayoutSizingSizeOf(&fixture, LayoutSizingBoxFor(&fixture, "div"));
    RoboCheckEqInt(size.width, 300);
    RoboCheckEqInt(stats.block.widthsDeclared, 0u);
    RoboCheckEqInt(stats.block.widthsFilled, 3u);

    LayoutSizingEnd(&fixture);
}

/**
 * `auto` and a relative unit, declared rather than absent, reach the same two
 * branches -- and the *counters* say which one each took.
 *
 * A `width: auto` written on purpose and a width nothing declared at all are one
 * value in CSS, and this case is what proves the pass reads them that way rather
 * than by accident: the keyword arrives as a `KEYWORD` value and the unit as a
 * `LENGTH` in `em`, and both must fill the containing block while a `px` length
 * does not.
 */
static void TestSizingBlockKeywordAndUnit(void)
{
    LayoutSizingFixture fixture;
    LayoutSizingRule rules[3];
    LayoutStats stats;
    LayoutSize size;

    LayoutSizingRuleInit(&rules[0], "html", RoboCssDisplayBlock);
    LayoutSizingRuleInit(&rules[1], "body", RoboCssDisplayBlock);
    LayoutSizingRuleInit(&rules[2], "div", RoboCssDisplayBlock);
    LayoutSizingRuleKeyword(&rules[2], "width", "auto");
    LayoutSizingRuleEm(&rules[2], "height", 2.0);

    LayoutSizingBegin(&fixture, "<html><body><div></div></body></html>", rules, 3u);
    LayoutSizingPass(&fixture, 512, NULL, &stats);

    /* The declared `auto` fills the containing block, exactly as an absent width
     * does -- and both are counted as *filled*, because that is the answer the
     * resolver reached. */
    size = LayoutSizingSizeOf(&fixture, LayoutSizingBoxFor(&fixture, "div"));
    RoboCheckEqInt(size.width, 512);
    /* The `2em` is a length this build cannot turn into pixels, so the height comes
     * from the contents -- which are none, so zero. */
    RoboCheckEqInt(size.height, 0);
    RoboCheckEqInt(stats.block.widthsDeclared, 0u);
    RoboCheckEqInt(stats.block.widthsFilled, 3u);
    RoboCheckEqInt(stats.block.heightsExplicit, 0u);
    RoboCheckEqInt(stats.block.heightsFromContent, 3u);

    LayoutSizingEnd(&fixture);
}

/**
 * The bounds path, live -- driven by a fabricated style, because no property
 * RoboCSS registers feeds it.
 *
 * This is the case that proves the "clean place" 10.8 was asked to leave for
 * `min-width` is a working path and not a field nothing can reach.
 */
static void TestSizingBlockBounds(void)
{
    LayoutSizingFixture fixture;
    LayoutSizingRule rules[3];
    LayoutSize size;

    LayoutSizingRuleInit(&rules[0], "html", RoboCssDisplayBlock);
    LayoutSizingRuleInit(&rules[1], "body", RoboCssDisplayBlock);
    LayoutSizingRuleInit(&rules[2], "div", RoboCssDisplayBlock);
    LayoutSizingRulePx(&rules[2], "width", 300.0);
    LayoutSizingRulePx(&rules[2], "min-width", 40.0);
    LayoutSizingRulePx(&rules[2], "max-width", 200.0);
    LayoutSizingRulePx(&rules[2], "min-height", 44.0);
    LayoutSizingRulePx(&rules[2], "max-height", 120.0);

    LayoutSizingBegin(&fixture, "<html><body><div></div></body></html>", rules, 3u);
    LayoutSizingPass(&fixture, LAYOUT_SIZING_VIEWPORT_WIDTH, NULL, NULL);

    /* The declared 300 is pulled down to the 200 maximum, and the 0-height box is
     * raised to the 44 minimum. */
    size = LayoutSizingSizeOf(&fixture, LayoutSizingBoxFor(&fixture, "div"));
    RoboCheckEqInt(size.width, 200);
    RoboCheckEqInt(size.height, 44);

    /* And the clamp propagates the way a size does: the box's own 200 is what its
     * contents would be laid out into, and its parent is still the viewport's. */
    size = LayoutSizingSizeOf(&fixture, LayoutSizingBoxFor(&fixture, "body"));
    RoboCheckEqInt(size.width, LAYOUT_SIZING_VIEWPORT_WIDTH);
    RoboCheckEqInt(size.height, 44);

    LayoutSizingEnd(&fixture);
}

/**
 * A bound whose *other* dimension is not declared is not recorded, and the case
 * says so by outcome: a `max-width` on its own must not turn every height into the
 * zero this build has no `max-height` for.
 */
static void TestSizingBlockHalfBound(void)
{
    LayoutSizingFixture fixture;
    LayoutSizingRule rules[3];
    LayoutSize size;

    LayoutSizingRuleInit(&rules[0], "html", RoboCssDisplayBlock);
    LayoutSizingRuleInit(&rules[1], "body", RoboCssDisplayBlock);
    LayoutSizingRuleInit(&rules[2], "div", RoboCssDisplayBlock);
    LayoutSizingRulePx(&rules[2], "width", 300.0);
    /* Only one dimension of the maximum is known. */
    LayoutSizingRulePx(&rules[2], "max-width", 120.0);
    LayoutSizingRulePx(&rules[2], "height", 60.0);

    LayoutSizingBegin(&fixture, "<html><body><div></div></body></html>", rules, 3u);
    LayoutSizingPass(&fixture, LAYOUT_SIZING_VIEWPORT_WIDTH, NULL, NULL);

    /* The width is *not* clamped, and the height is not zeroed: a bound is recorded
     * only when both of its dimensions name a length, because 10.1's constraints
     * record holds a bound as one size. */
    size = LayoutSizingSizeOf(&fixture, LayoutSizingBoxFor(&fixture, "div"));
    RoboCheckEqInt(size.width, 300);
    RoboCheckEqInt(size.height, 60);

    LayoutSizingEnd(&fixture);
}

/**
 * An atomic inline's width is its declaration and its auto height is its contents'
 * extent -- the two-phase resolve, asserted where it is a *document* rather than a
 * record.
 */
static void TestSizingAtomicInline(void)
{
    LayoutSizingFixture fixture;
    LayoutSizingRule rules[4];
    LayoutStats stats;
    LayoutSize size;

    LayoutSizingRuleInit(&rules[0], "html", RoboCssDisplayBlock);
    LayoutSizingRuleInit(&rules[1], "body", RoboCssDisplayBlock);
    LayoutSizingRuleInit(&rules[2], "p", RoboCssDisplayBlock);
    LayoutSizingRuleInit(&rules[3], "i", RoboCssDisplayInlineBlock);
    LayoutSizingRulePx(&rules[3], "width", 150.0);

    LayoutSizingBegin(&fixture,
                        "<html><body><p><i><b></b></i></p></body></html>", rules, 4u);
    LayoutSizingPass(&fixture, LAYOUT_SIZING_VIEWPORT_WIDTH, NULL, &stats);

    /* The atomic inline's declared width, and its height from the contents -- which
     * are one `<b>` the pass stacks, and which has no declared height. */
    {
        LayoutNodeId atomic = LayoutSizingBoxFor(&fixture, "i");

        size = LayoutSizingSizeOf(&fixture, atomic);
        RoboCheckEqInt(size.width, 150);
        RoboCheckEqInt(size.height, 0);
    }
    RoboCheckEqInt(stats.inlineFlow.atomicPlaced, 1u);
    RoboCheckEqInt(stats.inlineFlow.widthsDeclared, 1u);
    RoboCheckEqInt(stats.inlineFlow.widthsFilled, 0u);
    RoboCheckEqInt(stats.inlineFlow.heightsDeclared, 0u);
    RoboCheckEqInt(stats.inlineFlow.heightsFromContent, 1u);

    LayoutSizingEnd(&fixture);
}

/**
 * Sizing must not turn line boxes into CSS boxes.
 *
 * A wrapped inline document's lines are still derived from the participants they
 * placed: each line is as wide as what was put on it, the context's width is what
 * the *participants* could take, and nothing in the sizing layer is consulted for a
 * line at all. Asserted over a document that really wraps, so the difference between
 * "the line's own width" and "the context's width" is visible.
 */
static void TestSizingLineRegression(void)
{
    LayoutSizingFixture fixture;
    LayoutSizingRule rules[6];
    LayoutStats stats;
    size_t lines;
    size_t index;

    LayoutSizingRuleInit(&rules[0], "html", RoboCssDisplayBlock);
    LayoutSizingRuleInit(&rules[1], "body", RoboCssDisplayBlock);
    LayoutSizingRuleInit(&rules[2], "p", RoboCssDisplayBlock);
    LayoutSizingRuleInit(&rules[3], "span", RoboCssDisplayInlineBlock);
    LayoutSizingRulePx(&rules[3], "width", 60.0);
    LayoutSizingRulePx(&rules[3], "height", 20.0);
    LayoutSizingRuleInit(&rules[4], "em", RoboCssDisplayInlineBlock);
    LayoutSizingRulePx(&rules[4], "width", 90.0);
    LayoutSizingRulePx(&rules[4], "height", 20.0);
    LayoutSizingRuleInit(&rules[5], "b", RoboCssDisplayBlock);
    LayoutSizingRulePx(&rules[5], "height", 10.0);

    /* 60 + 90 + 90 = 240 into a 200-wide line: the third one wraps. The point is
     * that the *line* is 150 wide -- what it placed -- where the context is 200. */
    LayoutSizingBegin(&fixture,
                        "<html><body><p><span></span><em></em><em></em></p></body></html>",
                        rules, 6u);
    LayoutSizingPass(&fixture, 200, NULL, &stats);

    lines = LayoutTreeLineCount(fixture.tree);
    RoboCheckEqInt(lines, 2u);
    RoboCheckEqInt(stats.inlineFlow.lines, 2u);
    RoboCheckEqInt(stats.inlineFlow.wraps, 1u);
    RoboCheckEqInt(stats.inlineFlow.lineMembers, 3u);

    for (index = 0u; index < lines; index++) {
        LayoutLineId id = RoboLayoutLineIdNone;
        LayoutLineBox line;

        RoboCheckEqInt(LayoutTreeLineAt(fixture.tree, index, &id, NULL), RoboOk);
        RoboCheckEqInt(LayoutTreeLine(fixture.tree, id, &line, NULL), RoboOk);
        /* Every line fits inside the context and none of them *is* the context:
         * a build that had made each line as wide as the available width would make
         * the first line 200 rather than 150. */
        RoboCheckEqInt(line.availableWidth, 200);
        if (index == 0u) {
            RoboCheckEqInt(line.size.width, 150);
        } else {
            RoboCheckEqInt(line.size.width, 90);
        }
        RoboCheckEqInt(line.size.height, 20);
    }

    /* The declared heights are the members' and the lines' -- a line is exactly as
     * tall as its tallest member, which is 10.7's rule and unchanged here. */
    RoboCheckEqInt(stats.inlineFlow.heightsDeclared, 3u);

    LayoutSizingEnd(&fixture);
}

/**
 * Nothing is cached between passes and nothing leaks, asserted over a whole
 * document rather than over a record.
 *
 * The balance is taken *before the tree exists* so it brackets the arenas' whole
 * lifetime, which is the shape every other `allocator_balance` case in this
 * directory settled on -- a counter that only brackets the reuse would pass while a
 * tree's own arena leaked.
 */
static void TestSizingAllocatorBalance(void)
{
    size_t before;
    int pass;
    int width;

    LayoutFixture();
    before = LayoutLiveBlocks();

    for (pass = 0; pass < 6; pass++) {
        LayoutSizingFixture fixture;
        LayoutSizingRule rules[3];
        LayoutStats stats;
        size_t settled;

        LayoutSizingRuleInit(&rules[0], "html", RoboCssDisplayBlock);
        LayoutSizingRuleInit(&rules[1], "body", RoboCssDisplayBlock);
        LayoutSizingRuleInit(&rules[2], "div", RoboCssDisplayBlock);
        LayoutSizingRulePx(&rules[2], "width", 250.0);

        LayoutSizingBegin(&fixture, "<html><body><div></div></body></html>", rules, 3u);

        /* The viewport is an input, so a pass at each of two widths is two answers
         * from one tree -- and the second must not leave the first's numbers or its
         * arena's. The declared 250 is the *same* number in both, which is the point:
         * an author's width does not move when the window does. */
        for (width = 0; width < 2; width++) {
            LayoutSize size;
            int32_t viewport = width == 0 ? LAYOUT_SIZING_VIEWPORT_WIDTH : 300;

            LayoutSizingPass(&fixture, viewport, NULL, &stats);
            size = LayoutSizingSizeOf(&fixture, LayoutSizingBoxFor(&fixture, "div"));
            RoboCheckEqInt(size.width, 250);
            RoboCheckEqInt(stats.block.widthsDeclared, 1u);
            settled = LayoutLiveBlocks();
            RoboCheckEqInt(LayoutLiveBlocks(), settled);
        }

        LayoutSizingEnd(&fixture);
        RoboCheckEqInt(LayoutLiveBlocks(), before);
    }
}

/* ------------------------------------------------------------------ */
/* Registration                                                        */
/* ------------------------------------------------------------------ */

void TestRegisterLayoutSizing(void);

void TestRegisterLayoutSizing(void)
{
    TestRegister("layout.sizing.dimension", TestSizingDimension);
    TestRegister("layout.sizing.vocabulary", TestSizingVocabulary);
    TestRegister("layout.sizing.record", TestSizingRecord);
    TestRegister("layout.sizing.width", TestSizingWidth);
    TestRegister("layout.sizing.height", TestSizingHeight);
    TestRegister("layout.sizing.clamp", TestSizingClamp);
    TestRegister("layout.sizing.reresolve", TestSizingReresolve);
    TestRegister("layout.sizing.seam", TestSizingSeam);
    TestRegister("layout.sizing.block_declared_width", TestSizingBlockDeclaredWidth);
    TestRegister("layout.sizing.block_auto_width", TestSizingBlockAutoWidth);
    TestRegister("layout.sizing.block_unresolved_width",
                       TestSizingBlockUnresolvedWidth);
    TestRegister("layout.sizing.block_keyword_and_unit",
                       TestSizingBlockKeywordAndUnit);
    TestRegister("layout.sizing.block_bounds", TestSizingBlockBounds);
    TestRegister("layout.sizing.block_half_bound", TestSizingBlockHalfBound);
    TestRegister("layout.sizing.atomic_inline", TestSizingAtomicInline);
    TestRegister("layout.sizing.line_regression", TestSizingLineRegression);
    TestRegister("layout.sizing.allocator_balance", TestSizingAllocatorBalance);
}
