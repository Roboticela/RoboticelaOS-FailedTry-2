/*
 * RoboLayout margin suite (Phase 10.9) — layout.margins.*
 *
 * Four things are under test, and the boundaries between them are the phase:
 *
 *   the **vocabulary** -- three kinds, four sides, and the predicates and names over
 *   them -- which is pure;
 *
 *   the **record**: what a style declared and what the four sides *came to*, which
 *   are two different numbers and the reason this phase exists;
 *
 *   the two **policies**: the block arithmetic (distribution of the leftover beside a
 *   declared width) and the inline one (every `auto` is zero), driven directly, so
 *   that a wrong distribution fails at the arithmetic rather than three boxes later in
 *   a coordinate;
 *
 *   the **pass**: margins reaching geometry -- a box's origin, the width its own `auto`
 *   fills, the extent its parent stacks by, and an atomic inline's advance along a
 *   line.
 *
 * The scripts here are *computed styles*, built through 9.8's makers exactly as the
 * sizing suite builds them, because a fabricated struct would prove the pass reads a
 * struct rather than a computed CSS value. Where a case needs a declaration the CSS
 * property table does not know -- a negative margin is known, a percentage is known,
 * but a `var()`-shaped unresolved length is not -- the value is flagged the way the
 * computed layer flags one and the classification is what is under test.
 *
 * Elements are resolved **by tag** (`LayoutMarginBoxFor`) rather than by arena
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
typedef enum layoutMarginDeclKind {
    LAYOUT_MARGIN_DECL_PX = 0,
    /** A px length flagged unresolved, which is what a `var()` arrives as. */
    LAYOUT_MARGIN_DECL_UNRESOLVED_PX = 1,
    LAYOUT_MARGIN_DECL_PERCENTAGE = 2,
    LAYOUT_MARGIN_DECL_EM = 3,
    LAYOUT_MARGIN_DECL_KEYWORD = 4
} LayoutMarginDeclKind;

typedef struct layoutMarginDecl {
    const char *name;
    LayoutMarginDeclKind kind;
    double number;
    const char *keyword;
} LayoutMarginDecl;

#define LAYOUT_MARGIN_MAX_DECLARATIONS 10u
#define LAYOUT_MARGIN_MAX_RULES 8u

typedef struct layoutMarginRule {
    const char *tag;
    CssDisplay display;
    LayoutMarginDecl declarations[LAYOUT_MARGIN_MAX_DECLARATIONS];
    size_t declarationsCount;
} LayoutMarginRule;

static void LayoutMarginRuleInit(LayoutMarginRule *rule,
                                    const char *tag,
                                    CssDisplay display)
{
    memset(rule, 0, sizeof(*rule));
    rule->tag = tag;
    rule->display = display;
}

static void LayoutMarginRuleAdd(LayoutMarginRule *rule,
                                   const char *name,
                                   LayoutMarginDeclKind kind,
                                   double number,
                                   const char *keyword)
{
    RoboCheckEqInt(rule->declarationsCount < LAYOUT_MARGIN_MAX_DECLARATIONS, 1);
    rule->declarations[rule->declarationsCount].name = name;
    rule->declarations[rule->declarationsCount].kind = kind;
    rule->declarations[rule->declarationsCount].number = number;
    rule->declarations[rule->declarationsCount].keyword = keyword;
    rule->declarationsCount++;
}

static void LayoutMarginRulePx(LayoutMarginRule *rule, const char *name, double px)
{
    LayoutMarginRuleAdd(rule, name, LAYOUT_MARGIN_DECL_PX, px, NULL);
}

static void LayoutMarginRuleAuto(LayoutMarginRule *rule, const char *name)
{
    LayoutMarginRuleAdd(rule, name, LAYOUT_MARGIN_DECL_KEYWORD, 0.0, "auto");
}

static void LayoutMarginRulePercentage(LayoutMarginRule *rule,
                                          const char *name,
                                          double percent)
{
    LayoutMarginRuleAdd(rule, name, LAYOUT_MARGIN_DECL_PERCENTAGE, percent, NULL);
}

static void LayoutMarginRuleEm(LayoutMarginRule *rule, const char *name, double em)
{
    LayoutMarginRuleAdd(rule, name, LAYOUT_MARGIN_DECL_EM, em, NULL);
}

static void LayoutMarginRuleUnresolved(LayoutMarginRule *rule,
                                          const char *name,
                                          double px)
{
    LayoutMarginRuleAdd(rule, name, LAYOUT_MARGIN_DECL_UNRESOLVED_PX, px, NULL);
}

typedef struct layoutMarginStyle {
    char tag[16];
    CssComputedStyle *style; /* owned */
    size_t answers;
} LayoutMarginStyle;

typedef struct layoutMarginSource {
    LayoutMarginStyle rules[LAYOUT_MARGIN_MAX_RULES];
    size_t count;
} LayoutMarginSource;

/**
 * Build the style @p rule describes: a real DISPLAY value, plus whatever the rule
 * declares, all through 9.8's own makers and container.
 *
 * `display` is set even when the rule declares nothing else, because 9.12's initial
 * value is `inline` and a fixture that wanted a block box would otherwise be asserting
 * a document made of inline boxes.
 */
static CssComputedStyle *LayoutMarginStyleBuild(const LayoutMarginRule *rule)
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
        const LayoutMarginDecl *declaration = &rule->declarations[index];

        value = (CssComputedValue)RoboCssCleared;
        specified = (CssValue)RoboCssCleared;
        switch (declaration->kind) {
        case LAYOUT_MARGIN_DECL_PERCENTAGE:
            RoboCheckEqInt(CssComputedValueMakePercentage(&value,
                                                                     declaration->number, 0,
                                                                     NULL),
                              RoboOk);
            break;
        case LAYOUT_MARGIN_DECL_KEYWORD:
            RoboCheckEqInt(CssComputedValueMakeKeyword(
                                  &value, CssSliceFromText(declaration->keyword), NULL),
                              RoboOk);
            break;
        case LAYOUT_MARGIN_DECL_UNRESOLVED_PX:
            RoboCheckEqInt(CssComputedValueMakeLength(
                                  &value, declaration->number, RoboCssUnitPx, 0, NULL),
                              RoboOk);
            /* The flag the computed layer sets for a `var()` or a `calc()`: carried
             * intact and not a value *yet*, whatever its kind says. */
            value.unresolved = 1;
            break;
        case LAYOUT_MARGIN_DECL_EM:
            RoboCheckEqInt(CssComputedValueMakeLength(
                                  &value, declaration->number, RoboCssUnitEm, 0, NULL),
                              RoboOk);
            break;
        case LAYOUT_MARGIN_DECL_PX:
        default:
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
static CssComputedStyle *LayoutMarginStyleAdd(LayoutMarginSource *source,
                                                         const LayoutMarginRule *rule)
{
    size_t slot;
    CssComputedStyle *style;

    LayoutFixture();
    RoboCheckEqInt(source->count < LAYOUT_MARGIN_MAX_RULES, 1);
    slot = source->count;
    style = LayoutMarginStyleBuild(rule);
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
 * *first* row whose tag matches, so an appended second `div` row would be shadowed by the
 * one it was meant to replace and the case would assert the old declaration while
 * appearing to have changed it.
 */
static void LayoutMarginStyleReplace(LayoutMarginSource *source,
                                       const char *tag,
                                       const LayoutMarginRule *rule)
{
    size_t index;

    LayoutFixture();
    for (index = 0u; index < source->count; index++) {
        if (strcmp(source->rules[index].tag, tag) == 0) {
            CssComputedStyleDestroy(source->rules[index].style);
            source->rules[index].style = LayoutMarginStyleBuild(rule);
            return;
        }
    }
    RoboCheckMsg(0, "the tag to replace is not in this source");
}

static const CssComputedStyle *LayoutMarginStyleLookup(
    void *userdata,
    const HtmlNodeHandle *element)
{
    LayoutMarginSource *source = (LayoutMarginSource *)userdata;
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
    /* An element nothing was declared for: `display: inline`, no declared margins, and
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
 */
static int32_t LayoutMarginEntryPx(const CssComputedStyle *style,
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

typedef struct layoutMarginFixture {
    LayoutDocument doc;
    LayoutMarginSource source;
    LayoutStyleSource styles;
    LayoutTree *tree;
} LayoutMarginFixture;

static void LayoutMarginBegin(LayoutMarginFixture *fixture,
                                const char *html,
                                const LayoutMarginRule *rules,
                                size_t ruleCount)
{
    LayoutBuildOptions options;
    size_t index;

    memset(fixture, 0, sizeof(*fixture));
    LayoutDocumentBuild(&fixture->doc, html);
    for (index = 0u; index < ruleCount; index++) {
        (void)LayoutMarginStyleAdd(&fixture->source, &rules[index]);
    }
    LayoutStyleSourceInit(&fixture->styles);
    fixture->styles.userdata = &fixture->source;
    fixture->styles.ComputedStyleFor = LayoutMarginStyleLookup;
    RoboCheckEqInt(LayoutTreeCreate(&fixture->tree, NULL), RoboOk);
    LayoutBuildOptionsInit(&options);
    options.styles = &fixture->styles;
    RoboCheckEqInt(LayoutTreeBuildFromDocument(fixture->tree, fixture->doc.id,
                                                          &options, NULL, NULL),
                      RoboOk);
}

static void LayoutMarginEnd(LayoutMarginFixture *fixture)
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

#define LAYOUT_MARGIN_VIEWPORT_WIDTH 800
#define LAYOUT_MARGIN_VIEWPORT_HEIGHT 600

/** One pass at @p viewportWidth. */
static void LayoutMarginPass(LayoutMarginFixture *fixture,
                               int32_t viewportWidth,
                               LayoutStats *outStats)
{
    LayoutContext *context = NULL;
    LayoutSize viewport;
    LayoutStats stats;

    viewport.width = viewportWidth;
    viewport.height = LAYOUT_MARGIN_VIEWPORT_HEIGHT;
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
static LayoutNodeId LayoutMarginBoxFor(LayoutMarginFixture *fixture,
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
static LayoutPoint LayoutMarginOriginOf(LayoutMarginFixture *fixture,
                                                   LayoutNodeId id)
{
    LayoutResult result;
    LayoutPoint position;

    RoboCheckEqInt(LayoutObjectResult(fixture->tree, id, &result), RoboOk);
    RoboCheckEqInt((int)result.state, (int)RoboLayoutResultComputed);
    RoboCheckEqInt(LayoutResultPosition(&result, &position), RoboOk);
    return position;
}

/** The size a placed box was given. */
static LayoutSize LayoutMarginSizeOf(LayoutMarginFixture *fixture,
                                                LayoutNodeId id)
{
    LayoutResult result;
    LayoutSize size;

    RoboCheckEqInt(LayoutObjectResult(fixture->tree, id, &result), RoboOk);
    RoboCheckEqInt((int)result.state, (int)RoboLayoutResultComputed);
    RoboCheckEqInt(LayoutResultSize(&result, &size), RoboOk);
    return size;
}

/** The used margins a placed box reports. */
static LayoutMarginEdges LayoutMarginEdgesOf(LayoutMarginFixture *fixture,
                                                        LayoutNodeId id)
{
    LayoutMarginEdges edges;

    RoboCheckEqInt(LayoutObjectMargins(fixture->tree, id, &edges, NULL), RoboOk);
    return edges;
}

/* ------------------------------------------------------------------ */
/* The vocabulary                                                      */
/* ------------------------------------------------------------------ */

static void TestMarginsKindVocabulary(void)
{
    LayoutMarginKind kinds[RoboLayoutMarginKindCount];
    size_t index;
    size_t seen[RoboLayoutMarginKindCount];

    LayoutFixture();

    kinds[0] = RoboLayoutMarginAbsent;
    kinds[1] = RoboLayoutMarginAuto;
    kinds[2] = RoboLayoutMarginResolved;
    kinds[3] = RoboLayoutMarginUnresolved;
    kinds[4] = RoboLayoutMarginPercentage;
    RoboCheckEqInt(RoboLayoutMarginKindCount, 5u);

    memset(seen, 0, sizeof(seen));
    for (index = 0u; index < RoboLayoutMarginKindCount; index++) {
        LayoutMarginKind kind = kinds[index];

        /* The kinds are walked and each is asserted to be inside the count, so a fourth
         * enumerator that nobody gave a name to would fail here rather than at a
         * stranger's switch statement. */
        RoboCheckEqInt(LayoutMarginKindIsKnown(kind), 1);
        RoboCheck((size_t)kind < RoboLayoutMarginKindCount);
        seen[(size_t)kind]++;
        RoboCheck(LayoutMarginKindName(kind) != NULL);
    }
    for (index = 0u; index < RoboLayoutMarginKindCount; index++) {
        /* Every index named exactly once: the vocabulary is a partition of its range
         * rather than a list with a hole in it. */
        RoboCheckEqInt(seen[index], 1u);
    }

    RoboCheckStrEq(LayoutMarginKindName(RoboLayoutMarginAbsent), "absent");
    RoboCheckStrEq(LayoutMarginKindName(RoboLayoutMarginAuto), "auto");
    RoboCheckStrEq(LayoutMarginKindName(RoboLayoutMarginResolved), "resolved");
    RoboCheckStrEq(LayoutMarginKindName(RoboLayoutMarginUnresolved),
                      "unresolved");
    RoboCheckStrEq(LayoutMarginKindName(RoboLayoutMarginPercentage),
                      "percentage");
    RoboCheckStrEq(LayoutMarginKindName((LayoutMarginKind)7), "unknown");
    RoboCheckEqInt(LayoutMarginKindIsKnown((LayoutMarginKind)5), 0);
    RoboCheckEqInt(LayoutMarginKindIsKnown((LayoutMarginKind)99), 0);
}

static void TestMarginsOneSide(void)
{
    LayoutMargin zero = LayoutMarginZero();
    LayoutMargin otherZero;
    LayoutMargin resolved = LayoutMarginResolved(10);
    LayoutMargin otherResolved = LayoutMarginResolved(11);
    LayoutMargin negative = LayoutMarginResolved(-10);
    LayoutMargin autoMargin = LayoutMarginAuto();
    LayoutMargin otherAuto;
    LayoutMargin unresolved = LayoutMarginUnresolved();
    LayoutMargin cleared;
    LayoutMargin otherAbsent;
    LayoutMargin *nothing = NULL;
    GfxPixelX length = -1;

    LayoutFixture();

    /*
     * `ABSENT` is the zero of the kind vocabulary, so a record a caller merely cleared
     * says "nobody declared this side" rather than meaning `auto`. That is the same
     * decision RoboCSS makes for `position: static`, and it is why a forgotten
     * initialisation cannot turn a box into a centred one.
     *
     * It is *not* the same record as `LayoutMarginZero()`, and the two are one
     * used value and two facts: `margin: 0` is a declaration an author wrote, an absent
     * side is one nobody did, and only the counters can tell them apart. Both are zeros
     * in geometry, and a cleared record's `length` is 0 for the same reason a C struct's
     * is -- which is why reading one is a *miss* rather than a zero.
     */
    memset(&cleared, 0, sizeof(cleared));
    otherAbsent = LayoutMarginAbsent();
    otherZero = LayoutMarginZero();
    RoboCheckEqInt(LayoutMarginIsAbsent(&cleared), 1);
    RoboCheckEqInt(cleared.length, 0);
    RoboCheckEqInt(LayoutMarginIsValid(&cleared), 1);
    RoboCheckEqInt(LayoutMarginIsResolved(&cleared), 0);
    RoboCheckEqInt(LayoutMarginEqual(&cleared, &otherAbsent), 1);
    RoboCheckEqInt(LayoutMarginEqual(&cleared, &zero), 0);
    RoboCheckEqInt(LayoutMarginEqual(&zero, &otherZero), 1);
    length = -1;
    RoboCheckEqInt(LayoutMarginLength(&cleared, &length), RoboNotFound);
    RoboCheckEqInt(length, -1);

    RoboCheckEqInt(LayoutMarginIsAuto(&zero), 0);
    RoboCheckEqInt(LayoutMarginIsResolved(&zero), 1);
    RoboCheckEqInt(LayoutMarginIsUnresolved(&zero), 0);
    RoboCheckEqInt(LayoutMarginLength(&zero, &length), RoboOk);
    RoboCheckEqInt(length, 0);

    /* The distinction the phase exists to keep: `auto` distributes and `0` does not, so
     * one of them is a number and the other is a question. */
    RoboCheckEqInt(LayoutMarginIsAuto(&autoMargin), 1);
    RoboCheckEqInt(LayoutMarginIsResolved(&autoMargin), 0);
    RoboCheckEqInt(LayoutMarginEqual(&autoMargin, &zero), 0);
    otherAuto = LayoutMarginAuto();
    RoboCheckEqInt(LayoutMarginEqual(&autoMargin, &otherAuto), 1);

    /* A *negative* length is a value, which is the one respect this vocabulary differs
     * from a box dimension's. */
    RoboCheckEqInt(LayoutMarginIsResolved(&negative), 1);
    RoboCheckEqInt(negative.length, -10);
    RoboCheckEqInt(LayoutMarginIsValid(&negative), 1);
    RoboCheckEqInt(LayoutMarginLength(&negative, &length), RoboOk);
    RoboCheckEqInt(length, -10);

    RoboCheckEqInt(LayoutMarginIsUnresolved(&unresolved), 1);
    RoboCheckEqInt(LayoutMarginEqual(&unresolved, &zero), 0);
    RoboCheckEqInt(LayoutMarginEqual(&resolved, &otherResolved), 0);
    RoboCheckEqInt(LayoutMarginEqual(&resolved, &negative), 0);

    /* A length read from a kind that carries none is a *miss* rather than the zero the
     * field happens to hold -- the same door 10.8's dimension has, for the same reason. */
    length = -1;
    RoboCheckEqInt(LayoutMarginLength(&autoMargin, &length), RoboNotFound);
    RoboCheckEqInt(length, -1);
    RoboCheckEqInt(LayoutMarginLength(&unresolved, &length), RoboNotFound);
    RoboCheckEqInt(length, -1);

    RoboCheckEqInt(LayoutMarginIsValid(&zero), 1);
    RoboCheckEqInt(LayoutMarginIsValid(&autoMargin), 1);
    RoboCheckEqInt(LayoutMarginIsValid(&unresolved), 1);
    /* An out-of-range magnitude is refused as a *value*, and an `auto` whose meaningless
     * length field is garbage is still valid -- the field is not read for that kind. */
    {
        LayoutMargin huge = LayoutMarginResolved(0);

        huge.length = (GfxPixelX)0x7FFFFFFF;
        RoboCheckEqInt(LayoutMarginIsValid(&huge), 1);
    }
    {
        LayoutMargin bogus;

        bogus.kind = (LayoutMarginKind)9;
        bogus.length = 0;
        RoboCheckEqInt(LayoutMarginIsValid(&bogus), 0);
    }

    RoboCheckEqInt(LayoutMarginIsAuto(nothing), 0);
    RoboCheckEqInt(LayoutMarginIsResolved(nothing), 0);
    RoboCheckEqInt(LayoutMarginIsUnresolved(nothing), 0);
    RoboCheckEqInt(LayoutMarginIsAbsent(nothing), 0);
    RoboCheckEqInt(LayoutMarginIsValid(nothing), 0);
    RoboCheckEqInt(LayoutMarginLength(nothing, &length), RoboInvalidArgument);
    RoboCheckEqInt(LayoutMarginLength(&zero, NULL), RoboInvalidArgument);
    RoboCheckEqInt(LayoutMarginEqual(NULL, NULL), 1);
    RoboCheckEqInt(LayoutMarginEqual(&zero, NULL), 0);
}

static void TestMarginsSides(void)
{
    LayoutMargins margins;
    LayoutMargins other;
    LayoutMargins *nothing = NULL;
    LayoutMargin side;

    LayoutFixture();

    /* The cleared four sides are the initial state: four absent sides, whose used value
     * is CSS's initial value -- a zero nobody declared. */
    LayoutMarginsInit(&margins);
    RoboCheckEqInt(LayoutMarginIsAbsent(&margins.top), 1);
    RoboCheckEqInt(LayoutMarginIsAbsent(&margins.left), 1);
    RoboCheckEqInt(LayoutMarginsIsValid(&margins), 1);
    RoboCheckEqInt(margins.top.length, 0);
    RoboCheckEqInt(margins.right.length, 0);
    RoboCheckEqInt(margins.bottom.length, 0);
    RoboCheckEqInt(margins.left.length, 0);

    margins.top = LayoutMarginResolved(1);
    margins.right = LayoutMarginResolved(2);
    margins.bottom = LayoutMarginResolved(3);
    margins.left = LayoutMarginResolved(-4);

    /* The four sides are addressable by 9.11's own enum, so the order is the CSS
     * shorthand's order and not a second definition of it. */
    RoboCheckEqInt(LayoutMarginsSide(&margins, RoboCssBoxSideTop, &side),
                      RoboOk);
    RoboCheckEqInt(side.length, 1);
    RoboCheckEqInt(LayoutMarginsSide(&margins, RoboCssBoxSideRight, &side),
                      RoboOk);
    RoboCheckEqInt(side.length, 2);
    RoboCheckEqInt(LayoutMarginsSide(&margins, RoboCssBoxSideBottom, &side),
                      RoboOk);
    RoboCheckEqInt(side.length, 3);
    RoboCheckEqInt(LayoutMarginsSide(&margins, RoboCssBoxSideLeft, &side),
                      RoboOk);
    RoboCheckEqInt(side.length, -4);
    RoboCheckEqInt(RoboCssBoxSideCount, 4u);

    side = LayoutMarginAuto();
    RoboCheckEqInt(LayoutMarginsSide(&margins, (CssBoxSide)4, &side),
                      RoboNotFound);
    RoboCheckEqInt(LayoutMarginIsAuto(&side), 1); /* untouched */

    other = margins;
    RoboCheckEqInt(LayoutMarginsEqual(&margins, &other), 1);
    other.left = LayoutMarginAuto();
    RoboCheckEqInt(LayoutMarginsEqual(&margins, &other), 0);
    RoboCheckEqInt(LayoutMarginsEqual(NULL, NULL), 1);
    RoboCheckEqInt(LayoutMarginsEqual(&margins, NULL), 0);

    RoboCheckEqInt(LayoutMarginsIsValid(nothing), 0);
    RoboCheckEqInt(LayoutMarginsSide(nothing, RoboCssBoxSideTop, &side),
                      RoboInvalidArgument);
    RoboCheckEqInt(LayoutMarginsSide(&margins, RoboCssBoxSideTop, NULL),
                      RoboInvalidArgument);
    /* `init` on NULL is a no-op rather than a crash, asserted by calling it. */
    LayoutMarginsInit(NULL);
}

/* ------------------------------------------------------------------ */
/* The record                                                          */
/* ------------------------------------------------------------------ */

static void TestMarginsSeam(void)
{
    LayoutMarginSource source;
    LayoutMarginRule divRule;
    LayoutMarginRule spanRule;
    LayoutMarginRule emRule;
    LayoutMarginRule varRule;
    LayoutMarginRule plainRule;
    LayoutMargins margins;
    LayoutMargins initial;
    const CssComputedStyle *style;

    memset(&source, 0, sizeof(source));
    LayoutFixture();

    /* One style with all four kinds on it at once, because the reader is a loop over
     * four longhands and a case that read one declaration per style would not prove the
     * loop. */
    LayoutMarginRuleInit(&divRule, "div", RoboCssDisplayBlock);
    LayoutMarginRulePx(&divRule, "margin-top", -5.0);
    LayoutMarginRuleAuto(&divRule, "margin-right");
    LayoutMarginRulePercentage(&divRule, "margin-bottom", 5.0);
    LayoutMarginRulePx(&divRule, "margin-left", 12.0);

    /* A relative unit is a declaration this build cannot use, and it is *not* the same
     * answer as `auto`. */
    LayoutMarginRuleInit(&emRule, "em", RoboCssDisplayBlock);
    LayoutMarginRuleEm(&emRule, "margin-left", 2.0);

    /* And the shape a `var()` arrives in. */
    LayoutMarginRuleInit(&varRule, "i", RoboCssDisplayBlock);
    LayoutMarginRuleUnresolved(&varRule, "margin-left", 30.0);

    /* A style that declares no margin at all, and one that declares a *fractional*
     * length so the rounding rule is visible. */
    LayoutMarginRuleInit(&plainRule, "p", RoboCssDisplayBlock);
    LayoutMarginRuleInit(&spanRule, "span", RoboCssDisplayInline);
    LayoutMarginRulePx(&spanRule, "margin-left", -10.5);

    style = LayoutMarginStyleAdd(&source, &divRule);
    RoboCheck(style != NULL);
    (void)LayoutMarginStyleAdd(&source, &emRule);
    (void)LayoutMarginStyleAdd(&source, &varRule);
    (void)LayoutMarginStyleAdd(&source, &plainRule);
    (void)LayoutMarginStyleAdd(&source, &spanRule);

    /* div: a negative length is usable, `auto` is its own kind, a percentage is not. */
    margins = LayoutMarginsOf(style);
    RoboCheckEqInt((int)margins.top.kind, (int)RoboLayoutMarginResolved);
    RoboCheckEqInt(margins.top.length, -5);
    RoboCheckEqInt((int)margins.right.kind, (int)RoboLayoutMarginAuto);
    /* A `margin-bottom: 5%` since 19.2 keeps its magnitude rather than being
     * `UNRESOLVED`; the *resolution* happens in the policy, which has the width. */
    RoboCheckEqInt((int)margins.bottom.kind, (int)RoboLayoutMarginPercentage);
    RoboCheckEqInt((int)LayoutMarginPercentageOf(&margins.bottom), 5);
    RoboCheckEqInt((int)margins.left.kind, (int)RoboLayoutMarginResolved);
    RoboCheckEqInt(margins.left.length, 12);
    RoboCheckEqInt(LayoutMarginsIsValid(&margins), 1);

    /* An `em` margin: declared, unusable, and **not** silently a length. And `top`,
     * which the rule never wrote, is *absent* -- the state that a declared `margin-top:
     * 0` is not. */
    margins = LayoutMarginsOf(source.rules[1].style);
    RoboCheckEqInt((int)margins.left.kind, (int)RoboLayoutMarginUnresolved);
    RoboCheckEqInt((int)margins.top.kind, (int)RoboLayoutMarginAbsent);
    RoboCheckEqInt(margins.top.length, 0);

    /* And one flagged unresolved, which is a length by kind and not a value. */
    margins = LayoutMarginsOf(source.rules[2].style);
    RoboCheckEqInt((int)margins.left.kind, (int)RoboLayoutMarginUnresolved);

    /* A style that declares no margin reads as four *absent* sides: CSS's initial value
     * is what the geometry uses for them, and the record still says nobody wrote one. */
    initial = (LayoutMargins){0};
    LayoutMarginsInit(&initial);
    margins = LayoutMarginsOf(source.rules[3].style);
    RoboCheckEqInt(LayoutMarginIsAbsent(&margins.top), 1);
    RoboCheckEqInt(margins.top.length, 0);
    RoboCheckEqInt(LayoutMarginsEqual(&margins, &initial), 1);
    /* And a record whose sides *were* declared is not that one, because the comparison
     * reads every side's kind and not only its length -- which is what makes "did this
     * document declare any margins?" a question the record can answer. */
    margins = LayoutMarginsOf(style);
    RoboCheckEqInt(LayoutMarginsEqual(&margins, &initial), 0);

    /* Half away from zero, which 10.9 made true for a negative length: the door has
     * claimed it since 10.8 and nothing signed had reached it until now. */
    margins = LayoutMarginsOf(source.rules[4].style);
    RoboCheckEqInt(margins.left.length, -11);

    /* A NULL style is not a failure: a box with no style has made no declarations, and
     * the record a caller already initialised says so. */
    margins = LayoutMarginsOf(NULL);
    RoboCheckEqInt(LayoutMarginIsAbsent(&margins.top), 1);
    RoboCheckEqInt(margins.top.length, 0);
    RoboCheckEqInt(LayoutMarginsEqual(&margins, &initial), 1);
}

static void TestMarginsEdgesRecord(void)
{
    LayoutMarginEdges edges;
    LayoutMarginEdges other;
    int32_t outer = -1;

    LayoutFixture();

    LayoutMarginEdgesInit(&edges, LAYOUT_MARGIN_VIEWPORT_WIDTH);
    RoboCheckEqInt(edges.top, 0);
    RoboCheckEqInt(edges.right, 0);
    RoboCheckEqInt(edges.bottom, 0);
    RoboCheckEqInt(edges.left, 0);
    RoboCheckEqInt(edges.availableContentWidth, LAYOUT_MARGIN_VIEWPORT_WIDTH);
    RoboCheckEqInt(edges.resolved, 0u);
    RoboCheckEqInt(edges.autosUsed, 0u);
    RoboCheckEqInt(edges.unresolved, 0u);
    RoboCheckEqInt(LayoutMarginEdgesIsValid(&edges), 1);

    /* A containing block of nothing is zero available rather than a negative one. */
    LayoutMarginEdgesInit(&edges, -5);
    RoboCheckEqInt(edges.availableContentWidth, 0);
    RoboCheckEqInt(LayoutMarginEdgesIsValid(&edges), 1);

    /* The two refusals: a negative availability is a size arithmetic that can produce a
     * rectangle nobody can draw, and counters that exceed four sides are five sides. */
    edges.availableContentWidth = -1;
    RoboCheckEqInt(LayoutMarginEdgesIsValid(&edges), 0);
    edges.availableContentWidth = 10;
    edges.resolved = 2u;
    edges.autosUsed = 2u;
    edges.unresolved = 1u;
    RoboCheckEqInt(LayoutMarginEdgesIsValid(&edges), 0);
    edges.autosUsed = 1u;
    RoboCheckEqInt(LayoutMarginEdgesIsValid(&edges), 1);

    other = edges;
    RoboCheckEqInt(LayoutMarginEdgesEqual(&edges, &other), 1);
    /* Every field, because the record is a *conclusion*: two resolutions that agreed
     * about the numbers and disagreed about how many declarations they could not use
     * would be two different answers to one document. */
    other.unresolved = 0u;
    RoboCheckEqInt(LayoutMarginEdgesEqual(&edges, &other), 0);
    other = edges;
    other.left = -7;
    RoboCheckEqInt(LayoutMarginEdgesEqual(&edges, &other), 0);
    RoboCheckEqInt(LayoutMarginEdgesEqual(NULL, NULL), 1);
    RoboCheckEqInt(LayoutMarginEdgesEqual(&edges, NULL), 0);

    /* The advance an inline participant moves the cursor by. Negative margins make it
     * smaller than the content -- which is a layout an author can ask for, so it is
     * deliberately not clamped. */
    LayoutMarginEdgesInit(&edges, LAYOUT_MARGIN_VIEWPORT_WIDTH);
    edges.left = 5;
    edges.right = 15;
    RoboCheckEqInt(LayoutMarginEdgesOuterWidth(&edges, 100, &outer), RoboOk);
    RoboCheckEqInt(outer, 120);
    edges.left = -5;
    edges.right = -15;
    RoboCheckEqInt(LayoutMarginEdgesOuterWidth(&edges, 100, &outer), RoboOk);
    RoboCheckEqInt(outer, 80);
    RoboCheckEqInt(LayoutMarginEdgesOuterWidth(NULL, 100, &outer),
                      RoboInvalidArgument);
    RoboCheckEqInt(LayoutMarginEdgesOuterWidth(&edges, 100, NULL),
                      RoboInvalidArgument);
    RoboCheckEqInt(LayoutMarginEdgesIsValid(NULL), 0);
    LayoutMarginEdgesInit(NULL, 10); /* no-op, asserted by not crashing */
}

/* ------------------------------------------------------------------ */
/* The two policies                                                    */
/* ------------------------------------------------------------------ */

/** A four-side record with the two horizontal sides set. */
static LayoutMargins LayoutMarginPair(int32_t left, int32_t right)
{
    LayoutMargins margins;

    LayoutMarginsInit(&margins);
    margins.left = LayoutMarginResolved(left);
    margins.right = LayoutMarginResolved(right);
    return margins;
}

static void LayoutMarginExpectEdges(const LayoutMarginEdges *edges,
                                       int32_t top,
                                       int32_t right,
                                       int32_t bottom,
                                       int32_t left,
                                       int32_t available,
                                       size_t resolved,
                                       size_t autos,
                                       size_t unresolved)
{
    RoboCheckEqInt(edges->top, top);
    RoboCheckEqInt(edges->right, right);
    RoboCheckEqInt(edges->bottom, bottom);
    RoboCheckEqInt(edges->left, left);
    RoboCheckEqInt(edges->availableContentWidth, available);
    RoboCheckEqInt(edges->resolved, resolved);
    RoboCheckEqInt(edges->autosUsed, autos);
    RoboCheckEqInt(edges->unresolved, unresolved);
    RoboCheckEqInt(LayoutMarginEdgesIsValid(edges), 1);
}

static void TestMarginsBlockBasic(void)
{
    LayoutMargins margins;
    LayoutMarginEdges edges;
    LayoutDimension autoWidth = LayoutDimensionAuto();

    LayoutFixture();

    LayoutMarginsInit(&margins);
    RoboCheckEqInt(LayoutMarginsResolveBlock(&margins, 800, &autoWidth, &edges,
                                                       NULL),
                      RoboOk);
    /* No declaration anywhere, so nothing was applied and nothing was left over. */
    LayoutMarginExpectEdges(&edges, 0, 0, 0, 0, 800, 0u, 0u, 0u);

    /* A `width: 0` box and a box with no width are the same document as far as the
     * margins are concerned -- the *distribution* is what a settled width changes. */
    {
        LayoutDimension zeroWidth = LayoutDimensionDefinite(0);

        RoboCheckEqInt(LayoutMarginsResolveBlock(&margins, 800, &zeroWidth,
                                                           &edges, NULL),
                          RoboOk);
        LayoutMarginExpectEdges(&edges, 0, 0, 0, 0, 800, 0u, 0u, 0u);
    }

    RoboCheckEqInt(LayoutMarginsResolveBlock(NULL, 800, &autoWidth, &edges,
                                                       NULL),
                      RoboInvalidArgument);
    RoboCheckEqInt(LayoutMarginsResolveBlock(&margins, 800, &autoWidth, NULL,
                                                       NULL),
                      RoboInvalidArgument);
    /* A NULL dimension is not a failure: "this box declares no width" and "the caller
     * has no dimension to show" both mean nothing to distribute. */
    RoboCheckEqInt(LayoutMarginsResolveBlock(&margins, 800, NULL, &edges, NULL),
                      RoboOk);
    LayoutMarginExpectEdges(&edges, 0, 0, 0, 0, 800, 0u, 0u, 0u);
}

static void TestMarginsBlockSides(void)
{
    LayoutMargins margins = LayoutMarginPair(10, 20);
    LayoutMarginEdges edges;
    LayoutDimension autoWidth = LayoutDimensionAuto();

    LayoutFixture();

    margins.top = LayoutMarginResolved(3);
    margins.bottom = LayoutMarginResolved(4);

    RoboCheckEqInt(LayoutMarginsResolveBlock(&margins, 800, &autoWidth, &edges,
                                                       NULL),
                      RoboOk);
    /* Used as written, every one of the four, and the containing block's contents left
     * over after the two horizontal ones. */
    LayoutMarginExpectEdges(&edges, 3, 20, 4, 10, 770, 4u, 0u, 0u);

    /* The vertical sides do **not** come out of the available width: a box's height is
     * not a number its containing block offers it in this direction. */
    RoboCheckEqInt(edges.availableContentWidth, 800 - 10 - 20);
}

static void TestMarginsBlockNegative(void)
{
    LayoutMargins margins = LayoutMarginPair(-10, -20);
    LayoutMarginEdges edges;
    LayoutDimension autoWidth = LayoutDimensionAuto();

    LayoutFixture();
    margins.top = LayoutMarginResolved(-5);
    margins.bottom = LayoutMarginResolved(-6);

    RoboCheckEqInt(LayoutMarginsResolveBlock(&margins, 800, &autoWidth, &edges,
                                                       NULL),
                      RoboOk);
    /*
     * A negative margin is honoured in the edges themselves -- the box then overhangs
     * its containing block -- while the width left for its *contents* grows, because
     * taking away a negative leaves more. Nothing is clamped on the way in; the one
     * clamp is on the availability, and it is a floor rather than a bound.
     */
    LayoutMarginExpectEdges(&edges, -5, -20, -6, -10, 830, 4u, 0u, 0u);

    /* A large negative pair grows the availability rather than being refused: nothing is
     * clamped on the way in, and there is no upper bound a containing block has to obey. */
    margins = LayoutMarginPair(-900, -900);
    RoboCheckEqInt(LayoutMarginsResolveBlock(&margins, 800, &autoWidth, &edges,
                                                       NULL),
                      RoboOk);
    LayoutMarginExpectEdges(&edges, 0, -900, 0, -900, 2600, 2u, 0u, 0u);

    /* And it is a *floor* rather than a bound: a pair of margins wider than the
     * containing block leaves zero for the contents rather than a negative width. */
    margins = LayoutMarginPair(900, 900);
    RoboCheckEqInt(LayoutMarginsResolveBlock(&margins, 800, &autoWidth, &edges,
                                                       NULL),
                      RoboOk);
    LayoutMarginExpectEdges(&edges, 0, 900, 0, 900, 0, 2u, 0u, 0u);
}

static void TestMarginsBlockAutoCenter(void)
{
    LayoutMargins margins;
    LayoutMarginEdges edges;
    LayoutDimension width = LayoutDimensionDefinite(200);

    LayoutFixture();

    /* `margin: 0 auto` with a declared width: the two autos split the leftover, which is
     * what centring a block in CSS *is*. */
    margins = LayoutMarginPair(0, 0);
    margins.left = LayoutMarginAuto();
    margins.right = LayoutMarginAuto();
    RoboCheckEqInt(LayoutMarginsResolveBlock(&margins, 800, &width, &edges,
                                                       NULL),
                      RoboOk);
    LayoutMarginExpectEdges(&edges, 0, 300, 0, 300, 200, 0u, 2u, 0u);

    /* One `auto` takes all of it. */
    margins = LayoutMarginPair(0, 0);
    margins.left = LayoutMarginAuto();
    RoboCheckEqInt(LayoutMarginsResolveBlock(&margins, 800, &width, &edges,
                                                       NULL),
                      RoboOk);
    LayoutMarginExpectEdges(&edges, 0, 0, 0, 600, 200, 1u, 1u, 0u);

    margins = LayoutMarginPair(0, 0);
    margins.right = LayoutMarginAuto();
    RoboCheckEqInt(LayoutMarginsResolveBlock(&margins, 800, &width, &edges,
                                                       NULL),
                      RoboOk);
    LayoutMarginExpectEdges(&edges, 0, 600, 0, 0, 200, 1u, 1u, 0u);

    /* And a declared side is *added* to rather than replaced: `margin-left: 100px` with
     * `margin-right: auto` gives the right side 500. */
    margins = LayoutMarginPair(100, 0);
    margins.right = LayoutMarginAuto();
    RoboCheckEqInt(LayoutMarginsResolveBlock(&margins, 800, &width, &edges,
                                                       NULL),
                      RoboOk);
    LayoutMarginExpectEdges(&edges, 0, 500, 0, 100, 200, 1u, 1u, 0u);
}

static void TestMarginsBlockAutoOdd(void)
{
    LayoutMargins margins;
    LayoutMarginEdges edges;
    LayoutDimension width = LayoutDimensionDefinite(199);

    LayoutFixture();

    /*
     * The odd pixel goes **right**, so a centred box in an odd leftover sits one pixel
     * left of centre. CSS says the two used values are equal, which integers cannot
     * always do; this case is where the choice is written down.
     */
    margins = LayoutMarginPair(0, 0);
    margins.left = LayoutMarginAuto();
    margins.right = LayoutMarginAuto();
    RoboCheckEqInt(LayoutMarginsResolveBlock(&margins, 800, &width, &edges,
                                                       NULL),
                      RoboOk);
    LayoutMarginExpectEdges(&edges, 0, 301, 0, 300, 199, 0u, 2u, 0u);
}

static void TestMarginsBlockAutoUnderAutoWidth(void)
{
    LayoutMargins margins;
    LayoutMarginEdges edges;
    LayoutDimension autoWidth = LayoutDimensionAuto();
    LayoutDimension percentWidth;

    LayoutFixture();

    percentWidth = LayoutDimensionUnresolved();

    /*
     * An `auto` **width** fills everything, so an `auto` margin beside it has nothing to
     * take: CSS says its used value is 0. This is the case that says why the block door
     * takes the *dimension* rather than a number -- the same declaration means 300 when
     * the width is settled and 0 when it is not.
     */
    margins = LayoutMarginPair(0, 0);
    margins.left = LayoutMarginAuto();
    margins.right = LayoutMarginAuto();
    RoboCheckEqInt(LayoutMarginsResolveBlock(&margins, 800, &autoWidth, &edges,
                                                       NULL),
                      RoboOk);
    LayoutMarginExpectEdges(&edges, 0, 0, 0, 0, 800, 0u, 0u, 0u);

    /* A width this build cannot resolve takes the same path, because the *fill* rule for
     * it is the same rule. */
    RoboCheckEqInt(LayoutMarginsResolveBlock(&margins, 800, &percentWidth,
                                                       &edges, NULL),
                      RoboOk);
    LayoutMarginExpectEdges(&edges, 0, 0, 0, 0, 800, 0u, 0u, 0u);

    /* A declared side is still applied, and the autos are still zero: the box overflows
     * nothing, it fills what is left. */
    margins = LayoutMarginPair(30, 0);
    margins.left = LayoutMarginResolved(30);
    margins.right = LayoutMarginAuto();
    RoboCheckEqInt(LayoutMarginsResolveBlock(&margins, 800, &autoWidth, &edges,
                                                       NULL),
                      RoboOk);
    LayoutMarginExpectEdges(&edges, 0, 0, 0, 30, 770, 1u, 0u, 0u);
}

static void TestMarginsBlockOverconstrained(void)
{
    LayoutMargins margins;
    LayoutMarginEdges edges;
    LayoutDimension width = LayoutDimensionDefinite(900);

    LayoutFixture();

    /*
     * Over-constrained: the width alone is wider than the containing block, so there is
     * no leftover to distribute and both autos stay at zero. CSS resolves this by
     * adjusting `margin-right`; this build does not adjust a margin its author did not
     * leave `auto`, and the overflow is 10.13's to report.
     */
    margins = LayoutMarginPair(0, 0);
    margins.left = LayoutMarginAuto();
    margins.right = LayoutMarginAuto();
    RoboCheckEqInt(LayoutMarginsResolveBlock(&margins, 800, &width, &edges,
                                                       NULL),
                      RoboOk);
    LayoutMarginExpectEdges(&edges, 0, 0, 0, 0, 800, 0u, 0u, 0u);

    /* A leftover of exactly zero is the same case rather than a third one. */
    width = LayoutDimensionDefinite(800);
    RoboCheckEqInt(LayoutMarginsResolveBlock(&margins, 800, &width, &edges,
                                                       NULL),
                      RoboOk);
    LayoutMarginExpectEdges(&edges, 0, 0, 0, 0, 800, 0u, 0u, 0u);
}

static void TestMarginsBlockUnresolved(void)
{
    LayoutMargins margins;
    LayoutMarginEdges edges;
    LayoutDimension width = LayoutDimensionDefinite(200);

    LayoutFixture();

    /*
     * A percentage resolves against the containing block's **width** in CSS, for every
     * side -- and this build has a containing width, so it *could*. It does not, because
     * 10.8 left every percentage unresolved and resolving one here would make
     * `margin: 5%` the only percentage in the module that means something. The used
     * value is zero and the declaration is *counted*, so a document spaced with
     * percentages lays out flush and says so.
     */
    LayoutMarginsInit(&margins);
    margins.left = LayoutMarginUnresolved();
    margins.bottom = LayoutMarginUnresolved();
    RoboCheckEqInt(LayoutMarginsResolveBlock(&margins, 800, &width, &edges,
                                                       NULL),
                      RoboOk);
    LayoutMarginExpectEdges(&edges, 0, 0, 0, 0, 800, 0u, 0u, 2u);

    /* And an unresolved side next to a resolved one does not stop the resolved one, or
     * the distribution: the counting is orthogonal to the arithmetic. */
    margins.left = LayoutMarginResolved(50);
    margins.right = LayoutMarginAuto();
    RoboCheckEqInt(LayoutMarginsResolveBlock(&margins, 800, &width, &edges,
                                                       NULL),
                      RoboOk);
    LayoutMarginExpectEdges(&edges, 0, 550, 0, 50, 200, 1u, 1u, 1u);
}

static void TestMarginsBlockVerticalAuto(void)
{
    LayoutMargins margins;
    LayoutMarginEdges edges;
    LayoutDimension autoWidth = LayoutDimensionAuto();

    LayoutFixture();

    /*
     * `margin-top: auto` distributes nothing in normal flow -- the keyword means
     * something vertically only for an absolutely positioned box, which is 10.12 -- so it
     * is zero and it is **not** counted as used. A caller that saw `autosUsed == 2` for
     * this document would be told a centring happened.
     */
    LayoutMarginsInit(&margins);
    margins.top = LayoutMarginAuto();
    margins.bottom = LayoutMarginAuto();
    RoboCheckEqInt(LayoutMarginsResolveBlock(&margins, 800, &autoWidth, &edges,
                                                       NULL),
                      RoboOk);
    LayoutMarginExpectEdges(&edges, 0, 0, 0, 0, 800, 0u, 0u, 0u);
}

static void TestMarginsInlinePolicy(void)
{
    LayoutMargins margins;
    LayoutMarginEdges edges;

    LayoutFixture();

    /*
     * The same declarations the block policy centres with, and every `auto` is zero:
     * distribution is a block-level normal-flow rule, and an inline-level participant has
     * no width constraint to be under- or over-. This is why there are two doors rather
     * than one with a flag -- a caller that reached for the block one would centre an
     * `inline-block` inside its line, which is not a thing CSS does.
     */
    margins = LayoutMarginPair(0, 0);
    margins.left = LayoutMarginAuto();
    margins.right = LayoutMarginAuto();
    RoboCheckEqInt(LayoutMarginsResolveInline(&margins, 800, &edges, NULL),
                      RoboOk);
    LayoutMarginExpectEdges(&edges, 0, 0, 0, 0, 800, 0u, 0u, 0u);

    /* Declared sides *are* used, and they come out of what an `auto` width may fill: an
     * `inline-block` with a 5px left and a 15px right margin has 780 to fill. */
    margins = LayoutMarginPair(5, 15);
    RoboCheckEqInt(LayoutMarginsResolveInline(&margins, 800, &edges, NULL),
                      RoboOk);
    LayoutMarginExpectEdges(&edges, 0, 15, 0, 5, 780, 2u, 0u, 0u);

    /* Negative margins grow it, and the floor still applies. */
    margins = LayoutMarginPair(-5, -15);
    RoboCheckEqInt(LayoutMarginsResolveInline(&margins, 800, &edges, NULL),
                      RoboOk);
    LayoutMarginExpectEdges(&edges, 0, -15, 0, -5, 820, 2u, 0u, 0u);

    margins = LayoutMarginPair(900, 900);
    RoboCheckEqInt(LayoutMarginsResolveInline(&margins, 800, &edges, NULL),
                      RoboOk);
    LayoutMarginExpectEdges(&edges, 0, 900, 0, 900, 0, 2u, 0u, 0u);

    /* A percentage is unusable here for the same reason it is there. */
    margins.left = LayoutMarginUnresolved();
    margins.right = LayoutMarginUnresolved();
    RoboCheckEqInt(LayoutMarginsResolveInline(&margins, 800, &edges, NULL),
                      RoboOk);
    LayoutMarginExpectEdges(&edges, 0, 0, 0, 0, 800, 0u, 0u, 2u);

    RoboCheckEqInt(LayoutMarginsResolveInline(NULL, 800, &edges, NULL),
                      RoboInvalidArgument);
    RoboCheckEqInt(LayoutMarginsResolveInline(&margins, 800, NULL, NULL),
                      RoboInvalidArgument);
}

/* ------------------------------------------------------------------ */
/* The pass                                                            */
/* ------------------------------------------------------------------ */

static void TestMarginsBlockGeometry(void)
{
    LayoutMarginFixture fixture;
    LayoutMarginRule htmlRule;
    LayoutMarginRule bodyRule;
    LayoutMarginRule divRule;
    LayoutMarginRule rules[3];
    LayoutStats stats;
    LayoutPoint origin;
    LayoutSize size;
    LayoutMarginEdges edges;

    LayoutFixture();

    LayoutMarginRuleInit(&htmlRule, "html", RoboCssDisplayBlock);
    LayoutMarginRuleInit(&bodyRule, "body", RoboCssDisplayBlock);
    LayoutMarginRuleInit(&divRule, "div", RoboCssDisplayBlock);
    LayoutMarginRulePx(&divRule, "width", 300.0);
    LayoutMarginRulePx(&divRule, "height", 40.0);
    LayoutMarginRulePx(&divRule, "margin-top", 10.0);
    LayoutMarginRulePx(&divRule, "margin-right", 20.0);
    LayoutMarginRulePx(&divRule, "margin-bottom", 30.0);
    LayoutMarginRulePx(&divRule, "margin-left", 40.0);
    rules[0] = htmlRule;
    rules[1] = bodyRule;
    rules[2] = divRule;

    LayoutMarginBegin(&fixture, "<html><body><div></div></body></html>", rules, 3u);
    LayoutMarginPass(&fixture, LAYOUT_MARGIN_VIEWPORT_WIDTH, &stats);

    /*
     * The box is placed at its **margin origin** while its size stays its content box:
     * `origin.x` is the containing block's content origin, and the box starts 40px and
     * 10px further in. A build that added the margins to the size would fail both halves
     * of this pair.
     */
    origin = LayoutMarginOriginOf(&fixture, LayoutMarginBoxFor(&fixture, "div"));
    size = LayoutMarginSizeOf(&fixture, LayoutMarginBoxFor(&fixture, "div"));
    RoboCheckEqInt(origin.x, 40);
    RoboCheckEqInt(origin.y, 10);
    RoboCheckEqInt(size.width, 300);
    RoboCheckEqInt(size.height, 40);

    /* And the used margins are readable from the box, which is what a paint phase needs
     * and what the result cannot carry. */
    edges = LayoutMarginEdgesOf(&fixture, LayoutMarginBoxFor(&fixture, "div"));
    LayoutMarginExpectEdges(&edges, 10, 20, 30, 40, 800 - 60, 4u, 0u, 0u);

    /* The parent's height is the child's **margin box**: 10 + 40 + 30. */
    size = LayoutMarginSizeOf(&fixture, LayoutMarginBoxFor(&fixture, "body"));
    RoboCheckEqInt(size.height, 80);
    size = LayoutMarginSizeOf(&fixture, LayoutMarginBoxFor(&fixture, "html"));
    RoboCheckEqInt(size.height, 80);

    RoboCheckEqInt(stats.block.marginsApplied, 4u);
    RoboCheckEqInt(stats.block.marginsAutoUsed, 0u);
    RoboCheckEqInt(stats.block.marginsUnresolved, 0u);

    LayoutMarginEnd(&fixture);
}

static void TestMarginsAutoCenterGeometry(void)
{
    LayoutMarginFixture fixture;
    LayoutMarginRule bodyRule;
    LayoutMarginRule divRule;
    LayoutMarginRule pRule;
    LayoutMarginRule rules[3];
    LayoutStats stats;
    LayoutPoint origin;
    LayoutSize size;

    LayoutFixture();

    LayoutMarginRuleInit(&bodyRule, "body", RoboCssDisplayBlock);
    LayoutMarginRuleInit(&divRule, "div", RoboCssDisplayBlock);
    LayoutMarginRulePx(&divRule, "width", 300.0);
    LayoutMarginRulePx(&divRule, "height", 20.0);
    LayoutMarginRuleAuto(&divRule, "margin-left");
    LayoutMarginRuleAuto(&divRule, "margin-right");
    LayoutMarginRuleInit(&pRule, "p", RoboCssDisplayBlock);
    LayoutMarginRulePx(&pRule, "height", 5.0);
    rules[0] = bodyRule;
    rules[1] = divRule;
    rules[2] = pRule;

    LayoutMarginBegin(&fixture, "<html><body><div><p></p></div></body></html>", rules,
                       3u);
    LayoutMarginPass(&fixture, LAYOUT_MARGIN_VIEWPORT_WIDTH, &stats);

    /* Centred: (800 - 300) / 2. */
    origin = LayoutMarginOriginOf(&fixture, LayoutMarginBoxFor(&fixture, "div"));
    RoboCheckEqInt(origin.x, 250);
    RoboCheckEqInt(origin.y, 0);

    /*
     * And the *child* resolves against the card's 300 rather than the viewport's 800,
     * which is the half of the claim a build that centred the card and then handed its
     * children the viewport would pass by accident.
     */
    size = LayoutMarginSizeOf(&fixture, LayoutMarginBoxFor(&fixture, "p"));
    RoboCheckEqInt(size.width, 300);
    RoboCheckEqInt(stats.block.marginsAutoUsed, 2u);
    RoboCheckEqInt(stats.block.marginsApplied, 0u);

    LayoutMarginEnd(&fixture);
}

static void TestMarginsMarginBoxFlowsDown(void)
{
    LayoutMarginFixture fixture;
    LayoutMarginRule bodyRule;
    LayoutMarginRule divRule;
    LayoutMarginRule pRule;
    LayoutMarginRule rules[3];
    LayoutStats stats;
    LayoutPoint origin;
    LayoutSize size;

    LayoutFixture();

    /*
     * A parent with horizontal margins and an `auto` width: the parent is 100 in from the
     * viewport and 600 wide, and its child -- which declares nothing -- is 600 wide at
     * the parent's own content origin. Two levels of the same rule, asserted together
     * because a build that reduced the available width for one level and not the other
     * would pass either half alone.
     */
    LayoutMarginRuleInit(&bodyRule, "body", RoboCssDisplayBlock);
    LayoutMarginRulePx(&bodyRule, "margin-left", 100.0);
    LayoutMarginRulePx(&bodyRule, "margin-right", 100.0);
    LayoutMarginRuleInit(&divRule, "div", RoboCssDisplayBlock);
    LayoutMarginRulePx(&divRule, "height", 10.0);
    LayoutMarginRuleInit(&pRule, "p", RoboCssDisplayBlock);
    LayoutMarginRulePx(&pRule, "height", 5.0);
    rules[0] = bodyRule;
    rules[1] = divRule;
    rules[2] = pRule;

    LayoutMarginBegin(&fixture, "<html><body><div></div></body></html>", rules, 3u);
    LayoutMarginPass(&fixture, LAYOUT_MARGIN_VIEWPORT_WIDTH, &stats);

    origin = LayoutMarginOriginOf(&fixture, LayoutMarginBoxFor(&fixture, "body"));
    size = LayoutMarginSizeOf(&fixture, LayoutMarginBoxFor(&fixture, "body"));
    RoboCheckEqInt(origin.x, 100);
    RoboCheckEqInt(size.width, 600);

    origin = LayoutMarginOriginOf(&fixture, LayoutMarginBoxFor(&fixture, "div"));
    size = LayoutMarginSizeOf(&fixture, LayoutMarginBoxFor(&fixture, "div"));
    RoboCheckEqInt(origin.x, 0);
    RoboCheckEqInt(size.width, 600);

    LayoutMarginEnd(&fixture);
}

static void TestMarginsBlockStacking(void)
{
    LayoutMarginFixture fixture;
    LayoutMarginRule bodyRule;
    LayoutMarginRule pRule;
    LayoutMarginRule h1Rule;
    LayoutMarginRule ulRule;
    LayoutMarginRule rules[4];
    LayoutStats stats;
    LayoutPoint origin;
    LayoutSize size;

    LayoutFixture();

    /*
     * Three siblings with distinct heights **and distinct tags**, because the scripted
     * source answers per tag: a fixture keyed off a class would be exercising the
     * selector matcher, and this case is about the cursor.
     */
    LayoutMarginRuleInit(&bodyRule, "body", RoboCssDisplayBlock);
    LayoutMarginRuleInit(&pRule, "p", RoboCssDisplayBlock);
    LayoutMarginRulePx(&pRule, "height", 100.0);
    LayoutMarginRulePx(&pRule, "margin-top", 10.0);
    LayoutMarginRulePx(&pRule, "margin-bottom", 20.0);
    LayoutMarginRuleInit(&h1Rule, "h1", RoboCssDisplayBlock);
    LayoutMarginRulePx(&h1Rule, "height", 50.0);
    LayoutMarginRulePx(&h1Rule, "margin-top", 5.0);
    LayoutMarginRulePx(&h1Rule, "margin-bottom", 15.0);
    LayoutMarginRuleInit(&ulRule, "ul", RoboCssDisplayBlock);
    LayoutMarginRulePx(&ulRule, "height", 75.0);
    rules[0] = bodyRule;
    rules[1] = pRule;
    rules[2] = h1Rule;
    rules[3] = ulRule;

    LayoutMarginBegin(&fixture, "<html><body><p></p><h1></h1><ul></ul></body></html>",
                       rules, 4u);
    LayoutMarginPass(&fixture, LAYOUT_MARGIN_VIEWPORT_WIDTH, &stats);

    /* Each sibling below the last one's **margin box**: the cursor carries
     * `height + margin-bottom + the next one's margin-top`, with no collapsing. */
    origin = LayoutMarginOriginOf(&fixture, LayoutMarginBoxFor(&fixture, "p"));
    RoboCheckEqInt(origin.y, 10);
    origin = LayoutMarginOriginOf(&fixture, LayoutMarginBoxFor(&fixture, "h1"));
    RoboCheckEqInt(origin.y, 10 + 100 + 20 + 5);
    origin = LayoutMarginOriginOf(&fixture, LayoutMarginBoxFor(&fixture, "ul"));
    RoboCheckEqInt(origin.y, 135 + 50 + 15 + 0);

    /* `<body>`'s height is the extent the three margin boxes reached. */
    size = LayoutMarginSizeOf(&fixture, LayoutMarginBoxFor(&fixture, "body"));
    RoboCheckEqInt(size.height, 200 + 75);

    RoboCheckEqInt(stats.block.marginsApplied, 4u); /* p and h1, two each */
    LayoutMarginEnd(&fixture);
}

static void TestMarginsBlockNoCollapse(void)
{
    LayoutMarginFixture fixture;
    LayoutMarginRule bodyRule;
    LayoutMarginRule pRule;
    LayoutMarginRule h1Rule;
    LayoutMarginRule rules[3];
    LayoutStats stats;
    LayoutPoint origin;
    LayoutSize size;

    LayoutFixture();

    LayoutMarginRuleInit(&bodyRule, "body", RoboCssDisplayBlock);
    LayoutMarginRuleInit(&pRule, "p", RoboCssDisplayBlock);
    LayoutMarginRulePx(&pRule, "height", 10.0);
    LayoutMarginRulePx(&pRule, "margin-bottom", 20.0);
    LayoutMarginRuleInit(&h1Rule, "h1", RoboCssDisplayBlock);
    LayoutMarginRulePx(&h1Rule, "height", 10.0);
    LayoutMarginRulePx(&h1Rule, "margin-top", 30.0);
    rules[0] = bodyRule;
    rules[1] = pRule;
    rules[2] = h1Rule;

    LayoutMarginBegin(&fixture, "<html><body><p></p><h1></h1></body></html>", rules, 3u);
    LayoutMarginPass(&fixture, LAYOUT_MARGIN_VIEWPORT_WIDTH, &stats);

    /*
     * **The one place this phase knowingly differs from the specification.** Two adjacent
     * vertical margins add here (20 + 30 = 50) where CSS would collapse them to the
     * larger (30). This case asserts the *sum* deliberately: it is the assertion a later
     * goal changes when collapsing arrives, and the reason it is a case rather than a
     * comment is that a build which lost the margin arithmetic by collapsing early would
     * otherwise look correct in every document whose margins happened to be equal.
     */
    origin = LayoutMarginOriginOf(&fixture, LayoutMarginBoxFor(&fixture, "h1"));
    RoboCheckEqInt(origin.y, 10 + 20 + 30);

    size = LayoutMarginSizeOf(&fixture, LayoutMarginBoxFor(&fixture, "body"));
    RoboCheckEqInt(size.height, 60 + 10);

    LayoutMarginEnd(&fixture);
}

static void TestMarginsInlineAdvance(void)
{
    LayoutMarginFixture fixture;
    LayoutMarginRule htmlRule;
    LayoutMarginRule bodyRule;
    LayoutMarginRule divRule;
    LayoutMarginRule iRule;
    LayoutMarginRule bRule;
    LayoutMarginRule rules[5];
    LayoutStats stats;
    LayoutPoint origin;
    LayoutSize size;
    LayoutLineBox line;
    LayoutLineId lineId = RoboLayoutLineIdNone;
    LayoutMarginRule widthlessRule;
    LayoutMarginRule rules2[4];
    LayoutMarginFixture second;

    LayoutFixture();

    /*
     * `html` and `body` are declared **block**, and that is a fact about this fixture
     * rather than about margins: an element nothing declared for computes to `display:
     * inline` (9.12's initial value, and the point of `LayoutMarginStyleLookup`
     * answering NULL), so a document that declared only the subject would put the
     * subject's ancestors in the inline flow and never reach the subject at all -- it
     * would be placed by no pass and the case would be measuring a document it did not
     * write. Every other suite here declares them for the same reason.
     */
    LayoutMarginRuleInit(&htmlRule, "html", RoboCssDisplayBlock);
    LayoutMarginRuleInit(&bodyRule, "body", RoboCssDisplayBlock);
    LayoutMarginRuleInit(&divRule, "div", RoboCssDisplayBlock);
    LayoutMarginRulePx(&divRule, "height", 30.0);
    LayoutMarginRuleInit(&iRule, "i", RoboCssDisplayInlineBlock);
    LayoutMarginRulePx(&iRule, "width", 100.0);
    LayoutMarginRulePx(&iRule, "height", 20.0);
    LayoutMarginRulePx(&iRule, "margin-left", 5.0);
    LayoutMarginRulePx(&iRule, "margin-right", 15.0);
    LayoutMarginRuleInit(&bRule, "b", RoboCssDisplayInlineBlock);
    LayoutMarginRulePx(&bRule, "width", 50.0);
    LayoutMarginRulePx(&bRule, "height", 10.0);
    rules[0] = htmlRule;
    rules[1] = bodyRule;
    rules[2] = divRule;
    rules[3] = iRule;
    rules[4] = bRule;

    LayoutMarginBegin(&fixture, "<html><body><div><i></i><b></b></div></body></html>",
                       rules, 5u);
    LayoutMarginPass(&fixture, LAYOUT_MARGIN_VIEWPORT_WIDTH, &stats);

    /*
     * The advance is the **margin box**: the first atomic is placed 5px in from the
     * cursor, is 100 wide, and pushes the next one 15px further on. So the second starts
     * at 120 -- and a build that advanced by the content box alone would start it at 100.
     */
    origin = LayoutMarginOriginOf(&fixture, LayoutMarginBoxFor(&fixture, "i"));
    size = LayoutMarginSizeOf(&fixture, LayoutMarginBoxFor(&fixture, "i"));
    RoboCheckEqInt(origin.x, 5);
    RoboCheckEqInt(size.width, 100);
    origin = LayoutMarginOriginOf(&fixture, LayoutMarginBoxFor(&fixture, "b"));
    RoboCheckEqInt(origin.x, 120);

    /* The line's accounted width is the sum of the advances, and the two members are on
     * one line because they fit. */
    RoboCheckEqInt(LayoutTreeLineCount(fixture.tree), 1u);
    RoboCheckEqInt(LayoutTreeLineAt(fixture.tree, 0u, &lineId, NULL), RoboOk);
    RoboCheckEqInt(LayoutTreeLine(fixture.tree, lineId, &line, NULL), RoboOk);
    RoboCheckEqInt(line.size.width, 120 + 50);
    RoboCheckEqInt(line.members, 2u);

    RoboCheckEqInt(stats.inlineFlow.marginsApplied, 2u);
    RoboCheckEqInt(stats.inlineFlow.marginsUnresolved, 0u);

    LayoutMarginEnd(&fixture);

    /*
     * And an `auto` width fills what the margins left: the same participant with no
     * declared width is 780 wide (800 - 5 - 15) rather than the whole line. 10.4's
     * fallback, with the margins taken out -- which is the one number an inline
     * participant's sizing layer needs from this phase.
     */
    LayoutFixture();
    LayoutMarginRuleInit(&widthlessRule, "i", RoboCssDisplayInlineBlock);
    LayoutMarginRulePx(&widthlessRule, "height", 20.0);
    LayoutMarginRulePx(&widthlessRule, "margin-left", 5.0);
    LayoutMarginRulePx(&widthlessRule, "margin-right", 15.0);
    rules2[0] = htmlRule;
    rules2[1] = bodyRule;
    rules2[2] = divRule;
    rules2[3] = widthlessRule;
    LayoutMarginBegin(&second, "<html><body><div><i></i></div></body></html>", rules2,
                       4u);
    LayoutMarginPass(&second, LAYOUT_MARGIN_VIEWPORT_WIDTH, NULL);
    size = LayoutMarginSizeOf(&second, LayoutMarginBoxFor(&second, "i"));
    RoboCheckEqInt(size.width, 780);
    LayoutMarginEnd(&second);
}

static void TestMarginsInlineBaseline(void)
{
    LayoutMarginFixture fixture;
    LayoutMarginRule htmlRule;
    LayoutMarginRule bodyRule;
    LayoutMarginRule divRule;
    LayoutMarginRule iRule;
    LayoutMarginRule rules[4];
    LayoutLineBox line;
    LayoutLineId lineId = RoboLayoutLineIdNone;
    LayoutPoint origin;
    LayoutSize size;
    LayoutBaselineMetrics metrics;

    LayoutFixture();

    /*
     * An `inline-block` with a bottom margin. CSS specifies that its baseline -- with no
     * in-flow line box of its own -- is its **bottom margin edge**, so the box sits 6px
     * above the floor and makes the line 26px tall rather than 20. A build that passed
     * `height` alone would put the box's border edge on the floor and quietly ignore a
     * declaration the author wrote.
     */
    LayoutMarginRuleInit(&divRule, "div", RoboCssDisplayBlock);
    LayoutMarginRuleInit(&iRule, "i", RoboCssDisplayInlineBlock);
    LayoutMarginRulePx(&iRule, "width", 40.0);
    LayoutMarginRulePx(&iRule, "height", 20.0);
    LayoutMarginRulePx(&iRule, "margin-bottom", 6.0);
    LayoutMarginRuleInit(&htmlRule, "html", RoboCssDisplayBlock);
    LayoutMarginRuleInit(&bodyRule, "body", RoboCssDisplayBlock);
    rules[0] = htmlRule;
    rules[1] = bodyRule;
    rules[2] = divRule;
    rules[3] = iRule;

    LayoutMarginBegin(&fixture, "<html><body><div><i></i></div></body></html>", rules,
                       4u);
    LayoutMarginPass(&fixture, LAYOUT_MARGIN_VIEWPORT_WIDTH, NULL);

    RoboCheckEqInt(LayoutTreeLineCount(fixture.tree), 1u);
    RoboCheckEqInt(LayoutTreeLineAt(fixture.tree, 0u, &lineId, NULL), RoboOk);
    RoboCheckEqInt(LayoutTreeLine(fixture.tree, lineId, &line, NULL), RoboOk);
    RoboCheckEqInt(line.size.height, 26);
    RoboCheckEqInt(line.baseline.ascent, 26);
    RoboCheckEqInt(line.baseline.descent, 0);
    RoboCheckEqInt(LayoutLineBoxBaseline(&line, &metrics), RoboOk);
    RoboCheckEqInt(metrics.ascent, 26);
    RoboCheckEqInt(line.size.width, 40);

    /* The box itself is 20 tall -- a margin never changes a box's size -- and it is
     * placed at the line's top, so its bottom *margin* edge is what rests on the floor. */
    origin = LayoutMarginOriginOf(&fixture, LayoutMarginBoxFor(&fixture, "i"));
    size = LayoutMarginSizeOf(&fixture, LayoutMarginBoxFor(&fixture, "i"));
    RoboCheckEqInt(origin.y, 0);
    RoboCheckEqInt(size.height, 20);

    /* And the owner's height is the line's, which is the margin box's. */
    size = LayoutMarginSizeOf(&fixture, LayoutMarginBoxFor(&fixture, "div"));
    RoboCheckEqInt(size.height, 26);

    LayoutMarginEnd(&fixture);
}

static void TestMarginsObjectDoor(void)
{
    LayoutMarginFixture fixture;
    LayoutMarginRule htmlRule;
    LayoutMarginRule bodyRule;
    LayoutMarginRule divRule;
    LayoutMarginRule rules[3];
    LayoutMarginEdges edges;
    LayoutNodeId divId;
    LayoutNodeId textId = RoboLayoutNodeIdNone;
    LayoutNodeId spanId;
    LayoutNodeId inlineId;
    size_t count;
    size_t index;

    LayoutFixture();

    LayoutMarginRuleInit(&divRule, "div", RoboCssDisplayBlock);
    LayoutMarginRulePx(&divRule, "margin-left", 7.0);
    LayoutMarginRuleInit(&htmlRule, "html", RoboCssDisplayBlock);
    LayoutMarginRuleInit(&bodyRule, "body", RoboCssDisplayBlock);
    rules[0] = htmlRule;
    rules[1] = bodyRule;
    rules[2] = divRule;

    LayoutMarginBegin(&fixture,
                       "<html><body><div>hello</div><span></span><b></b></body></html>",
                       rules, 3u);
    LayoutMarginPass(&fixture, LAYOUT_MARGIN_VIEWPORT_WIDTH, NULL);

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

    divId = LayoutMarginBoxFor(&fixture, "div");
    spanId = LayoutMarginBoxFor(&fixture, "span");
    inlineId = LayoutMarginBoxFor(&fixture, "b");

    /* A placed block box: the door answers, and answers the *used* values. */
    RoboCheckEqInt(LayoutObjectMargins(fixture.tree, divId, &edges, NULL),
                      RoboOk);
    RoboCheckEqInt(edges.left, 7);
    RoboCheckEqInt(edges.resolved, 1u);

    /*
     * A text run is not a box at all, and a transparent inline box is one CSS applies no
     * margins to -- both are *misses* rather than zeros, because a zero with a count of
     * resolved sides would tell a caller the author's margin had been honoured and come
     * to nothing.
     */
    RoboCheck(textId != RoboLayoutNodeIdNone);
    RoboCheckEqInt(LayoutObjectMargins(fixture.tree, textId, &edges, NULL),
                      RoboNotFound);
    /* Cleared on the way out rather than left holding whatever the caller's stack had. */
    RoboCheckEqInt(edges.left, 0);
    RoboCheckEqInt(edges.availableContentWidth, 0);
    RoboCheckEqInt(LayoutObjectMargins(fixture.tree, spanId, &edges, NULL),
                      RoboNotFound);

    /* An inline-level box in a *block* context is skipped rather than placed, so nothing
     * has used its margins either. */
    RoboCheckEqInt(LayoutObjectMargins(fixture.tree, inlineId, &edges, NULL),
                      RoboNotFound);

    RoboCheckEqInt(LayoutObjectMargins(NULL, divId, &edges, NULL),
                      RoboInvalidArgument);
    RoboCheckEqInt(LayoutObjectMargins(fixture.tree, RoboLayoutNodeIdNone,
                                                &edges, NULL),
                      RoboInvalidArgument);
    RoboCheckEqInt(LayoutObjectMargins(fixture.tree, divId, NULL, NULL),
                      RoboInvalidArgument);
    RoboCheckEqInt(LayoutObjectMargins(fixture.tree,
                                                (LayoutNodeId)(count + 99u), &edges,
                                                NULL),
                      RoboNotFound);

    LayoutMarginEnd(&fixture);
}

static void TestMarginsRelayout(void)
{
    LayoutMarginFixture fixture;
    LayoutMarginRule bodyRule;
    LayoutMarginRule divRule;
    LayoutMarginRule rules[2];
    LayoutStats stats;
    LayoutPoint origin;
    LayoutPoint settled;

    LayoutFixture();

    LayoutMarginRuleInit(&bodyRule, "body", RoboCssDisplayBlock);
    LayoutMarginRuleInit(&divRule, "div", RoboCssDisplayBlock);
    LayoutMarginRulePx(&divRule, "width", 100.0);
    LayoutMarginRulePx(&divRule, "height", 10.0);
    LayoutMarginRulePx(&divRule, "margin-left", 10.0);
    rules[0] = bodyRule;
    rules[1] = divRule;

    LayoutMarginBegin(&fixture, "<html><body><div></div></body></html>", rules, 2u);
    RoboCheckEqInt(LayoutMarginEntryPx(fixture.source.rules[1].style, "margin-left"),
                      10);

    LayoutMarginPass(&fixture, LAYOUT_MARGIN_VIEWPORT_WIDTH, &stats);
    origin = LayoutMarginOriginOf(&fixture, LayoutMarginBoxFor(&fixture, "div"));
    RoboCheckEqInt(origin.x, 10);

    /* A second pass over an unchanged tree is the same document: the records agree field
     * for field, which is what makes "the margin layer holds no state between calls" a
     * fact rather than a hope. */
    {
        LayoutMarginEdges first = LayoutMarginEdgesOf(
            &fixture, LayoutMarginBoxFor(&fixture, "div"));
        LayoutMarginEdges again;

        LayoutMarginPass(&fixture, LAYOUT_MARGIN_VIEWPORT_WIDTH, NULL);
        again = LayoutMarginEdgesOf(&fixture, LayoutMarginBoxFor(&fixture, "div"));
        RoboCheckEqInt(LayoutMarginEdgesEqual(&first, &again), 1);
    }

    /*
     * And a *changed* declaration is a changed document. The style is rebuilt with a
     * different `margin-left`, the tree is rebuilt against it, and the pass runs again:
     * the box moves, which is the whole of the requirement that a margin change
     * invalidates the geometry derived from it. (The invalidation itself is 10.3's --
     * a rebuild releases the classification and every result with it -- and this case
     * drives it rather than re-implementing it.)
     */
    LayoutMarginRulePx(&divRule, "margin-left", 40.0);
    LayoutMarginStyleReplace(&fixture.source, "div", &divRule);
    {
        LayoutBuildOptions options;

        RoboCheckEqInt(
            LayoutMarginEntryPx(fixture.source.rules[1].style, "margin-left"), 40);
        LayoutBuildOptionsInit(&options);
        options.styles = &fixture.styles;
        RoboCheckEqInt(LayoutTreeBuildFromDocument(fixture.tree, fixture.doc.id,
                                                              &options, NULL, NULL),
                          RoboOk);
    }
    LayoutMarginPass(&fixture, LAYOUT_MARGIN_VIEWPORT_WIDTH, NULL);
    origin = LayoutMarginOriginOf(&fixture, LayoutMarginBoxFor(&fixture, "div"));
    RoboCheckEqInt(origin.x, 40);

    /* And a changed *viewport* leaves a **declared** margin where it is: the declaration
     * is not a function of the containing block, and this asserts that the phase did not
     * make one. (A distributed `auto`, by contrast, moves -- which
     * `layout.margins.auto_center_geometry` reads at another width.) */
    LayoutMarginPass(&fixture, 300, NULL);
    settled = LayoutMarginOriginOf(&fixture, LayoutMarginBoxFor(&fixture, "div"));
    RoboCheckEqInt(settled.x, 40);
    RoboCheckEqInt(settled.y, 0);

    LayoutMarginEnd(&fixture);
}

static void TestMarginsAllocatorBalance(void)
{
    size_t before = LayoutLiveBlocks();
    size_t settled;
    int round;

    LayoutFixture();

    for (round = 0; round < 3; round++) {
        LayoutMarginFixture fixture;
        LayoutMarginRule bodyRule;
        LayoutMarginRule divRule;
        LayoutMarginRule rules[2];

        LayoutMarginRuleInit(&bodyRule, "body", RoboCssDisplayBlock);
        LayoutMarginRuleInit(&divRule, "div", RoboCssDisplayBlock);
        LayoutMarginRulePx(&divRule, "width", 250.0);
        LayoutMarginRulePx(&divRule, "height", 20.0);
        LayoutMarginRuleAuto(&divRule, "margin-left");
        LayoutMarginRuleAuto(&divRule, "margin-right");
        rules[0] = bodyRule;
        rules[1] = divRule;

        LayoutMarginBegin(&fixture, "<html><body><div></div></body></html>", rules, 2u);
        LayoutMarginPass(&fixture, LAYOUT_MARGIN_VIEWPORT_WIDTH, NULL);
        settled = LayoutLiveBlocks();
        /* The margin layer allocates nothing of its own: the records are values on the
         * stack and on the object, and the pass adds no arena. */
        RoboCheckEqInt(LayoutLiveBlocks(), settled);
        LayoutMarginPass(&fixture, 400, NULL);
        RoboCheckEqInt(LayoutLiveBlocks(), settled);
        LayoutMarginEnd(&fixture);
        RoboCheckEqInt(LayoutLiveBlocks(), before);
    }
}

/* ------------------------------------------------------------------ */
/* Registration                                                        */
/* ------------------------------------------------------------------ */

void TestRegisterLayoutMargin(void);

void TestRegisterLayoutMargin(void)
{
    TestRegister("layout.margins.kind_vocabulary", TestMarginsKindVocabulary);
    TestRegister("layout.margins.one_side", TestMarginsOneSide);
    TestRegister("layout.margins.sides", TestMarginsSides);
    TestRegister("layout.margins.seam", TestMarginsSeam);
    TestRegister("layout.margins.edges_record", TestMarginsEdgesRecord);
    TestRegister("layout.margins.block_basic", TestMarginsBlockBasic);
    TestRegister("layout.margins.block_sides", TestMarginsBlockSides);
    TestRegister("layout.margins.block_negative", TestMarginsBlockNegative);
    TestRegister("layout.margins.block_auto_center", TestMarginsBlockAutoCenter);
    TestRegister("layout.margins.block_auto_odd", TestMarginsBlockAutoOdd);
    TestRegister("layout.margins.block_auto_under_auto_width",
                       TestMarginsBlockAutoUnderAutoWidth);
    TestRegister("layout.margins.block_overconstrained",
                       TestMarginsBlockOverconstrained);
    TestRegister("layout.margins.block_unresolved", TestMarginsBlockUnresolved);
    TestRegister("layout.margins.block_vertical_auto",
                       TestMarginsBlockVerticalAuto);
    TestRegister("layout.margins.inline_policy", TestMarginsInlinePolicy);
    TestRegister("layout.margins.block_geometry", TestMarginsBlockGeometry);
    TestRegister("layout.margins.auto_center_geometry",
                       TestMarginsAutoCenterGeometry);
    TestRegister("layout.margins.margin_box_flows_down",
                       TestMarginsMarginBoxFlowsDown);
    TestRegister("layout.margins.block_stacking", TestMarginsBlockStacking);
    TestRegister("layout.margins.block_no_collapse", TestMarginsBlockNoCollapse);
    TestRegister("layout.margins.inline_advance", TestMarginsInlineAdvance);
    TestRegister("layout.margins.inline_baseline", TestMarginsInlineBaseline);
    TestRegister("layout.margins.object_door", TestMarginsObjectDoor);
    TestRegister("layout.margins.relayout", TestMarginsRelayout);
    TestRegister("layout.margins.allocator_balance", TestMarginsAllocatorBalance);
}
