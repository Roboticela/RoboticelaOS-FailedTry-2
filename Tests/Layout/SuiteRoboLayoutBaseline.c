/*
 * RoboLayout baseline suite (Phase 10.7) — layout.baseline.*
 *
 * The phase where a participant stops being aligned to the top of its line and
 * starts being aligned to a *floor*. Everything here is one of two claims: that the
 * metrics arithmetic is exact, or that two participants which should share a floor do.
 *
 * Six groups, in the order the phase is built:
 *
 *   the vocabulary   three kinds walked rather than a representative typed out, with
 *                    the one fact that separates NONE from a zero extent
 *
 *   the record       init, equality, validity, extent and the offset door, including
 *                    the sum that cannot overflow
 *
 *   the run rule     half-leading: the gap split across the two terms, exact to the
 *                    pixel for an odd gap, and the degradation an unmeasured run takes
 *
 *   the fallback     the bottom-edge rule, and the negative extent that must not
 *                    become a negative ascent
 *
 *   the line         a real pass: every line's height is its ascent plus its descent,
 *                    its baseline sits where the two say, and a **mixed-size line** is
 *                    as tall as its members need to sit on one floor
 *
 *   the pass         object metrics, the two-line case, empty content, re-layout,
 *                    determinism, and the accounting over a whole tree
 */

#include "RoboLayoutTestSupport.h"

#include <string.h>

/* ------------------------------------------------------------------ */
/* The scripted measurer                                               */
/* ------------------------------------------------------------------ */

/** One scripted answer: a width per code point, and a font's three line terms. */
typedef struct layoutBaselineScript {
    int32_t perCodePoint;
    /**
     * Additional ascent/descent **per code point**, so one script can hand two runs
     * different metrics.
     *
     * Without these the measurer answers every run identically, and a case claiming
     * "two sizes, one floor" would be testing two objects that happened to be the same
     * size -- which is exactly the shape the phase is supposed not to need. `0` for both
     * leaves every other case in the suite measuring as before.
     */
    int32_t ascentPerCodePoint;
    int32_t descentPerCodePoint;
    int32_t ascent;
    int32_t descent;
    int32_t gap;
    size_t calls;
} LayoutBaselineScript;

/**
 * The measurer: `perCodePoint` px per code point, and a font whose line metrics
 * are the three terms the case asked for.
 *
 * `gap` is a field here where the line suite's script had none, and that is what the
 * phase needs: the run's `height` is `ascent + descent + gap`, so a script that could
 * not set a gap could not tell "the metrics' extent equals the run's height" from
 * "the gap was dropped".
 */
static Result LayoutBaselineScriptMeasure(void *userdata,
                                                    const LayoutTextRequest *request,
                                                    LayoutTextRun *outRun)
{
    LayoutBaselineScript *script = (LayoutBaselineScript *)userdata;

    if (!script || !request || !outRun || !request->style) {
        return RoboInvalidArgument;
    }
    script->calls++;
    outRun->source = request->textNode;
    outRun->range = request->range;
    outRun->direction = request->style->direction;
    outRun->codePoints = LayoutTextRangeLength(request->range);
    outRun->width = (int32_t)outRun->codePoints * script->perCodePoint;
    outRun->ascent =
        script->ascent + (int32_t)outRun->codePoints * script->ascentPerCodePoint;
    outRun->descent =
        script->descent + (int32_t)outRun->codePoints * script->descentPerCodePoint;
    outRun->height = outRun->ascent + outRun->descent + script->gap;
    return RoboOk;
}

static LayoutTextMetrics LayoutBaselineScriptMetrics(
    LayoutBaselineScript *script)
{
    LayoutTextMetrics metrics;

    LayoutTextMetricsInit(&metrics);
    metrics.userdata = script;
    metrics.measure = LayoutBaselineScriptMeasure;
    return metrics;
}

/* ------------------------------------------------------------------ */
/* The fixture                                                         */
/* ------------------------------------------------------------------ */

/** One scripted answer: an element with this tag computes to this. */
typedef struct layoutBaselineRule {
    const char *tag;
    CssDisplay display;
    double widthPx;
    double heightPx;
} LayoutBaselineRule;

#define LAYOUT_BASELINE_MAX_RULES 8u

/**
 * The number of rules in a `LayoutBaselineRule` array, derived from the array.
 *
 * Written as a macro over the array rather than as a literal at each call site because
 * a literal is a second statement of a fact the compiler already knows, and the two can
 * disagree silently: a case whose array grew a rule it then forgot to count would run
 * with its last few rules *dropped*, and the symptoms -- a box with the initial display,
 * or an atomic with no declared size -- read as layout bugs rather than as a truncated
 * fixture.
 */
#define LAYOUT_BASELINE_RULE_COUNT(rules) (sizeof(rules) / sizeof((rules)[0]))

typedef struct layoutBaselineStyle {
    char tag[16];
    CssComputedStyle *style; /* owned */
} LayoutBaselineStyle;

typedef struct layoutBaselineFixture {
    LayoutDocument doc;
    LayoutBaselineStyle rules[LAYOUT_BASELINE_MAX_RULES];
    size_t ruleCount;
    LayoutStyleSource styles;
    LayoutTree *tree;
} LayoutBaselineFixture;

static const CssComputedStyle *LayoutBaselineStyleLookup(
    void *userdata,
    const HtmlNodeHandle *element)
{
    LayoutBaselineFixture *fixture = (LayoutBaselineFixture *)userdata;
    const char *name;
    size_t i;

    if (!fixture || !element) {
        return NULL;
    }
    name = HtmlElementName(element, NULL);
    if (!name || name[0] == '\0') {
        return NULL;
    }
    for (i = 0u; i < fixture->ruleCount; i++) {
        if (strcmp(fixture->rules[i].tag, name) == 0) {
            return fixture->rules[i].style;
        }
    }
    /* Nothing declared: a box with no style, whose display is the property's initial
     * value. Same rule every other layout suite states. */
    return NULL;
}

/** Add a computed style for @p rule's tag, with the entries the rule asked for. */
static void LayoutBaselineStyleAdd(LayoutBaselineFixture *fixture,
                                      const LayoutBaselineRule *rule)
{
    CssComputedStyle *style = NULL;
    CssComputedValue value = RoboCssCleared;
    CssValue specified = RoboCssCleared;
    size_t existing;

    LayoutFixture();
    RoboCheckEqInt(fixture->ruleCount < LAYOUT_BASELINE_MAX_RULES, 1);
    for (existing = 0u; existing < fixture->ruleCount; existing++) {
        RoboCheck(strcmp(fixture->rules[existing].tag, rule->tag) != 0);
    }
    RoboCheckEqInt(CssComputedStyleCreate(&style, NULL), RoboOk);
    RoboCheckEqInt(CssComputedValueMakeDisplay(&value, rule->display, NULL),
                      RoboOk);
    RoboCheckEqInt(CssComputedStyleSetEntry(style, RoboCssPropertyIdNone,
                                                       CssSliceFromText("display"),
                                                       0u, &specified, &value, NULL),
                      RoboOk);
    /*
     * Read the display back out of the style that was just built, so a rule whose
     * container silently refused the entry is a fixture failure at the rule rather than a
     * layout surprise later in the case: the builder reads exactly this door
     * (`LayoutStyleDisplay`), so "the rule's display" and "what a box built from
     * it sees" are the same question and are asked here once.
     */
    RoboCheckEqInt((int)LayoutStyleDisplay(style), (int)rule->display);
    /* A *real* computed LENGTH, through 9.8's own maker: the pass resolves a declared
     * size by asking for a LENGTH in px (RoboLayoutInternal.h), so a suite that wrote
     * the value by hand would be testing a struct rather than the rule. */
    if (rule->widthPx > 0.0) {
        value = (CssComputedValue)RoboCssCleared;
        specified = (CssValue)RoboCssCleared;
        RoboCheckEqInt(CssComputedValueMakeLength(&value, rule->widthPx,
                                                             RoboCssUnitPx, 0, NULL),
                          RoboOk);
        RoboCheckEqInt(CssComputedStyleSetEntry(style, RoboCssPropertyIdNone,
                                                           CssSliceFromText("width"),
                                                           0u, &specified, &value, NULL),
                          RoboOk);
    }
    if (rule->heightPx > 0.0) {
        value = (CssComputedValue)RoboCssCleared;
        specified = (CssValue)RoboCssCleared;
        RoboCheckEqInt(CssComputedValueMakeLength(&value, rule->heightPx,
                                                             RoboCssUnitPx, 0, NULL),
                          RoboOk);
        RoboCheckEqInt(CssComputedStyleSetEntry(style, RoboCssPropertyIdNone,
                                                           CssSliceFromText("height"),
                                                           0u, &specified, &value, NULL),
                          RoboOk);
    }
    strncpy(fixture->rules[fixture->ruleCount].tag, rule->tag,
            sizeof(fixture->rules[fixture->ruleCount].tag) - 1u);
    fixture->rules[fixture->ruleCount]
        .tag[sizeof(fixture->rules[fixture->ruleCount].tag) - 1u] = '\0';
    fixture->rules[fixture->ruleCount].style = style;
    fixture->ruleCount++;
}

static void LayoutBaselineBegin(LayoutBaselineFixture *fixture,
                                  const char *html,
                                  const LayoutBaselineRule *rules,
                                  size_t count)
{
    LayoutBuildOptions options;
    size_t index;

    memset(fixture, 0, sizeof(*fixture));
    LayoutDocumentBuild(&fixture->doc, html);
    for (index = 0u; index < count; index++) {
        LayoutBaselineStyleAdd(fixture, &rules[index]);
    }
    LayoutStyleSourceInit(&fixture->styles);
    fixture->styles.userdata = fixture;
    fixture->styles.ComputedStyleFor = LayoutBaselineStyleLookup;
    RoboCheckEqInt(LayoutTreeCreate(&fixture->tree, NULL), RoboOk);
    LayoutBuildOptionsInit(&options);
    options.styles = &fixture->styles;
    RoboCheckEqInt(LayoutTreeBuildFromDocument(fixture->tree, fixture->doc.id,
                                                          &options, NULL, NULL),
                      RoboOk);
}

static void LayoutBaselineEnd(LayoutBaselineFixture *fixture)
{
    size_t index;

    LayoutTreeDestroy(fixture->tree);
    fixture->tree = NULL;
    for (index = 0u; index < fixture->ruleCount; index++) {
        CssComputedStyleDestroy(fixture->rules[index].style);
        fixture->rules[index].style = NULL;
    }
    fixture->ruleCount = 0u;
    LayoutDocumentRelease(&fixture->doc);
}

/** One pass, with a viewport and optionally a measurer installed on the context. */
static void LayoutBaselinePass(LayoutBaselineFixture *fixture,
                                 int32_t viewportWidth,
                                 const LayoutTextMetrics *metrics,
                                 LayoutStats *outStats)
{
    LayoutContext *context = NULL;
    LayoutSize viewport;
    LayoutStats stats;

    viewport.width = viewportWidth;
    viewport.height = 600;
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

/** A viewport wider than any fixture's content, so a case only wraps on purpose. */
#define LAYOUT_BASELINE_WIDE_VIEWPORT 800

/** The block container every fixture below shares: `<html><body><p>`. */
#define LAYOUT_BASELINE_BLOCK_RULES                              \
    { "html", RoboCssDisplayBlock, 0.0, 0.0 },                \
    { "body", RoboCssDisplayBlock, 0.0, 0.0 },                \
    { "p", RoboCssDisplayBlock, 0.0, 0.0 }

/** The box built for the first element named @p tag, asserted to exist. */
static LayoutNodeId LayoutBaselineBoxOf(LayoutBaselineFixture *fixture,
                                                    const char *tag)
{
    HtmlNodeHandle element = LayoutDocumentFind(&fixture->doc, tag);
    LayoutNodeId id;

    RoboCheckEqInt(HtmlNodeHandleIsWellformed(&element), 1);
    id = LayoutObjectIdForDomNode(fixture->tree, &element);
    RoboCheck(id != RoboLayoutNodeIdNone);
    return id;
}

/**
 * The first **text object** at or below @p parent, in layout order.
 *
 * Depth-first rather than direct-children-only, and the difference is not a
 * convenience: a run lives inside the inline box that holds it, so `<p><span>ab</span>`
 * has a text object one level down from the paragraph and a walk that looked only at
 * the paragraph's children would find no run at all -- and would then report a
 * missing run where the tree actually has one, which is the failure mode this helper
 * exists to avoid. Recursion terminates because a text object is a leaf.
 */
static LayoutNodeId LayoutBaselineTextObject(
    LayoutBaselineFixture *fixture,
    LayoutNodeId parent)
{
    LayoutNodeId child = RoboLayoutNodeIdNone;
    Result status;

    status = LayoutObjectFirstChild(fixture->tree, parent, &child);
    if (status != RoboOk) {
        return RoboLayoutNodeIdNone;
    }
    for (;;) {
        if (MakeLayoutObjectKind(fixture->tree, child) == RoboLayoutObjectText) {
            return child;
        }
        {
            LayoutNodeId found = LayoutBaselineTextObject(fixture, child);

            if (found != RoboLayoutNodeIdNone) {
                return found;
            }
        }
        status = LayoutObjectNextSibling(fixture->tree, child, &child);
        if (status != RoboOk) {
            return RoboLayoutNodeIdNone;
        }
    }
}

/** Read line @p index of @p tree, asserted to exist. */
static void LayoutBaselineLine(LayoutBaselineFixture *fixture,
                                 size_t index,
                                 LayoutLineBox *out)
{
    LayoutLineId id = RoboLayoutLineIdNone;

    RoboCheckEqInt(LayoutTreeLineAt(fixture->tree, index, &id, NULL), RoboOk);
    RoboCheckEqInt(LayoutTreeLine(fixture->tree, id, out, NULL), RoboOk);
    RoboCheckEqInt((int)out->state, (int)RoboLayoutLineFinal);
}

/** Read object @p id's geometry, asserted to be COMPUTED. */
static void LayoutBaselineGeometry(LayoutBaselineFixture *fixture,
                                     LayoutNodeId id,
                                     LayoutPoint *outPosition,
                                     LayoutSize *outSize)
{
    LayoutResult result;

    RoboCheckEqInt(LayoutObjectResult(fixture->tree, id, &result), RoboOk);
    RoboCheckEqInt((int)result.state, (int)RoboLayoutResultComputed);
    RoboCheckEqInt(LayoutResultPosition(&result, outPosition), RoboOk);
    RoboCheckEqInt(LayoutResultSize(&result, outSize), RoboOk);
}

/** Read object @p id's baseline metrics, asserted to be usable. */
static void LayoutBaselineOf(LayoutBaselineFixture *fixture,
                               LayoutNodeId id,
                               LayoutBaselineMetrics *out)
{
    RoboCheckEqInt(LayoutObjectBaseline(fixture->tree, id, out, NULL), RoboOk);
    RoboCheckEqInt(LayoutBaselineMetricsIsValid(out), 1);
}

/** Object @p id's y as stored, which is relative to the box that contains it. */
static int32_t LayoutBaselineYOf(LayoutBaselineFixture *fixture,
                                    LayoutNodeId id)
{
    LayoutResult result;
    LayoutPoint position;

    RoboCheckEqInt(LayoutObjectResult(fixture->tree, id, &result), RoboOk);
    RoboCheckEqInt((int)result.state, (int)RoboLayoutResultComputed);
    RoboCheckEqInt(LayoutResultPosition(&result, &position), RoboOk);
    return position.y;
}

/**
 * Object @p id's y in @p owner's space, by summing the boxes between them.
 *
 * The stored y of a box is relative to its parent (RoboLayoutInline.c's rebasing
 * pass), so a member nested inside a transparent inline box is stated relative to
 * that box and not to the line. Adding the chain up to the context owner is what makes
 * a member comparable with the line it is on -- and the chain, rather than a single
 * parent, because the nesting can be deeper than one box.
 */
static int32_t LayoutBaselineYInOwner(LayoutBaselineFixture *fixture,
                                          LayoutNodeId id,
                                          LayoutNodeId owner)
{
    int32_t y = LayoutBaselineYOf(fixture, id);
    LayoutNodeId walk = id;
    LayoutNodeId parent = RoboLayoutNodeIdNone;

    while (walk != owner && LayoutObjectParent(fixture->tree, walk, &parent) == RoboOk &&
           parent != RoboLayoutNodeIdNone) {
        y += LayoutBaselineYOf(fixture, parent);
        walk = parent;
    }
    RoboCheckEqInt((int)walk, (int)owner);
    return y;
}

/* ------------------------------------------------------------------ */
/* The vocabulary                                                      */
/* ------------------------------------------------------------------ */

static void TestBaselineKindVocabulary(void)
{
    static const struct {
        LayoutBaselineKind kind;
        const char *name;
        int usable;
    } rows[] = {
        { RoboLayoutBaselineNone, "none", 0 },
        { RoboLayoutBaselineText, "text", 1 },
        { RoboLayoutBaselineBox, "box", 1 }
    };
    size_t index;

    /*
     * Walked rather than spot-checked, the discipline every closed enumeration in this
     * module gets (RoboLayoutTypes.c, RoboLayoutFormattingContext.c): an enumerator
     * added without a row is a visible omission rather than a gap.
     */
    RoboCheckEqInt((int)RoboLayoutBaselineKindCount, 3);
    RoboCheckEqInt((int)(sizeof(rows) / sizeof(rows[0])),
                      (int)RoboLayoutBaselineKindCount);
    for (index = 0u; index < sizeof(rows) / sizeof(rows[0]); index++) {
        RoboCheckEqInt(LayoutBaselineKindIsKnown(rows[index].kind), 1);
        RoboCheckStrEq(LayoutBaselineKindName(rows[index].kind), rows[index].name);
        RoboCheckEqInt(LayoutBaselineKindIsUsable(rows[index].kind),
                          rows[index].usable);
    }

    /*
     * The pair the phase turns on: NONE is *known* and *unusable*, and a zero extent is
     * usable. A build that answered "is this usable?" with "is this a kind?" would let
     * a cleared record be used as a baseline, and a build that refused a zero extent
     * would fail on a document nothing measured -- which is a legitimate way to run
     * this module (RoboLayoutText.h).
     */
    RoboCheckEqInt(LayoutBaselineKindIsKnown(RoboLayoutBaselineNone), 1);
    RoboCheckEqInt(LayoutBaselineKindIsUsable(RoboLayoutBaselineNone), 0);
    RoboCheckEqInt(LayoutBaselineKindIsUsable(RoboLayoutBaselineText), 1);

    /* One past the last row, and a negative: both refused, so the vocabulary is closed
     * at three rather than merely observed to have three members. */
    RoboCheckEqInt(LayoutBaselineKindIsKnown((LayoutBaselineKind)3), 0);
    RoboCheckEqInt(LayoutBaselineKindIsUsable((LayoutBaselineKind)3), 0);
    RoboCheckEqInt(LayoutBaselineKindIsKnown((LayoutBaselineKind)-1), 0);
    RoboCheckStrEq(LayoutBaselineKindName((LayoutBaselineKind)3), "unknown");
}

/* ------------------------------------------------------------------ */
/* The record                                                          */
/* ------------------------------------------------------------------ */

static void TestBaselineMetricsRecord(void)
{
    LayoutBaselineMetrics empty;
    LayoutBaselineMetrics a;
    LayoutBaselineMetrics b;
    int32_t offset = -1;

    LayoutBaselineMetricsInit(&empty);
    RoboCheckEqInt((int)empty.kind, (int)RoboLayoutBaselineNone);
    RoboCheckEqInt(empty.ascent, 0);
    RoboCheckEqInt(empty.descent, 0);

    /* A cleared record is *invalid* -- usable kind required -- while a zero extent with
     * a real kind is valid. The distinction is the one `is_usable` draws. */
    RoboCheckEqInt(LayoutBaselineMetricsIsValid(&empty), 0);
    RoboCheckEqInt(LayoutBaselineMetricsExtent(&empty), 0);
    RoboCheckEqInt(LayoutBaselineOffset(&empty, &offset), RoboNotFound);
    RoboCheckEqInt(offset, 0);

    a.kind = RoboLayoutBaselineText;
    a.ascent = 9;
    a.descent = 2;
    RoboCheckEqInt(LayoutBaselineMetricsIsValid(&a), 1);
    RoboCheckEqInt(LayoutBaselineMetricsExtent(&a), 11);
    RoboCheckEqInt(LayoutBaselineOffset(&a, &offset), RoboOk);
    RoboCheckEqInt(offset, 9);

    /* Equality includes the kind: two records with one extent and two anchors are two
     * answers, and a comparison that ignored the kind would call them the same. */
    b = a;
    RoboCheckEqInt(LayoutBaselineMetricsEqual(&a, &b), 1);
    b.kind = RoboLayoutBaselineBox;
    RoboCheckEqInt(LayoutBaselineMetricsEqual(&a, &b), 0);
    b = a;
    b.descent = 3;
    RoboCheckEqInt(LayoutBaselineMetricsEqual(&a, &b), 0);

    /* A zero extent with a usable kind is valid, and it is the shape an unmeasured run
     * produces -- so it must not be refused. */
    b.kind = RoboLayoutBaselineText;
    b.ascent = 0;
    b.descent = 0;
    RoboCheckEqInt(LayoutBaselineMetricsIsValid(&b), 1);
    RoboCheckEqInt(LayoutBaselineMetricsExtent(&b), 0);
    RoboCheckEqInt(LayoutBaselineOffset(&b, &offset), RoboOk);
    RoboCheckEqInt(offset, 0);

    /* A negative term is refused rather than clamped: every other arithmetic in this
     * module refuses an impossible value instead of repairing it (RoboLayoutResult.h),
     * and a negative ascent would silently become a *placement* two phases later. */
    b.ascent = -1;
    RoboCheckEqInt(LayoutBaselineMetricsIsValid(&b), 0);
    b.ascent = 0;
    b.descent = -1;
    RoboCheckEqInt(LayoutBaselineMetricsIsValid(&b), 0);

    /* The sum is clamped rather than wrapped, so `extent` is total: an int32 wrap here
     * would hand a caller a negative height, which is the one value the rest of the
     * module treats as impossible. */
    a.kind = RoboLayoutBaselineText;
    a.ascent = INT32_MAX;
    a.descent = INT32_MAX;
    RoboCheckEqInt(LayoutBaselineMetricsExtent(&a), INT32_MAX);

    /* NULL tolerance: the module's records are safe on NULL by convention. */
    RoboCheckEqInt(LayoutBaselineMetricsEqual(NULL, NULL), 1);
    RoboCheckEqInt(LayoutBaselineMetricsEqual(&a, NULL), 0);
    RoboCheckEqInt(LayoutBaselineMetricsIsValid(NULL), 0);
    RoboCheckEqInt(LayoutBaselineMetricsExtent(NULL), 0);
    RoboCheckEqInt(LayoutBaselineOffset(NULL, &offset), RoboInvalidArgument);
    RoboCheckEqInt(LayoutBaselineOffset(&a, NULL), RoboInvalidArgument);
    LayoutBaselineMetricsInit(NULL);
}

/* ------------------------------------------------------------------ */
/* The run rule: where the line gap becomes half-leading               */
/* ------------------------------------------------------------------ */

static void TestBaselineMetricsFromRun(void)
{
    LayoutTextRun run;
    LayoutBaselineMetrics metrics;

    /* An even gap splits evenly, and the extent is the run's height to the pixel. */
    LayoutTextRunInit(&run);
    run.ascent = 10;
    run.descent = 4;
    run.height = 18; /* gap 4 -> 12 above, 6 below */
    RoboCheckEqInt(LayoutBaselineMetricsFromRun(&run, &metrics), RoboOk);
    RoboCheckEqInt((int)metrics.kind, (int)RoboLayoutBaselineText);
    RoboCheckEqInt(metrics.ascent, 12);
    RoboCheckEqInt(metrics.descent, 6);
    RoboCheckEqInt(LayoutBaselineMetricsExtent(&metrics), run.height);

    /*
     * An odd gap puts its spare pixel **below** the baseline, and the extent is still
     * the run's height exactly. This is the case a build that halved the gap twice
     * (as `gap / 2` on both terms) would fail by one pixel -- which is why the second
     * term is written as `gap - half` and why the case exists.
     */
    run.ascent = 10;
    run.descent = 4;
    run.height = 15; /* gap 1 -> 10 above, 5 below */
    RoboCheckEqInt(LayoutBaselineMetricsFromRun(&run, &metrics), RoboOk);
    RoboCheckEqInt(metrics.ascent, 10);
    RoboCheckEqInt(metrics.descent, 5);
    RoboCheckEqInt(LayoutBaselineMetricsExtent(&metrics), 15);

    /* No gap at all: the metrics are the run's own two terms, unchanged. */
    run.ascent = 7;
    run.descent = 3;
    run.height = 10;
    RoboCheckEqInt(LayoutBaselineMetricsFromRun(&run, &metrics), RoboOk);
    RoboCheckEqInt(metrics.ascent, 7);
    RoboCheckEqInt(metrics.descent, 3);

    /*
     * A run with **no measurement** -- the documented degradation -- yields a TEXT
     * baseline of zero extent, not a NONE one. The claim being tested is that an
     * unmeasured run is *anchored* and has nothing above or below it, which is a
     * statement about the document rather than a missing answer; a NONE kind here
     * would make the line unreportable and the arena would refuse to store it.
     */
    LayoutTextRunInit(&run);
    RoboCheckEqInt(LayoutBaselineMetricsFromRun(&run, &metrics), RoboOk);
    RoboCheckEqInt((int)metrics.kind, (int)RoboLayoutBaselineText);
    RoboCheckEqInt(metrics.ascent, 0);
    RoboCheckEqInt(metrics.descent, 0);
    RoboCheckEqInt(LayoutBaselineMetricsIsValid(&metrics), 1);

    /*
     * A third-party measurer can report a height *smaller* than ascent plus descent --
     * the run record's contract does not forbid it and a scripted measurer is exactly
     * where it happens. That is read as no gap, so the metrics' extent is the larger
     * of the two numbers and a line containing the run still encloses it.
     */
    LayoutTextRunInit(&run);
    run.ascent = 12;
    run.descent = 5;
    run.height = 9;
    RoboCheckEqInt(LayoutBaselineMetricsFromRun(&run, &metrics), RoboOk);
    RoboCheckEqInt(metrics.ascent, 12);
    RoboCheckEqInt(metrics.descent, 5);
    RoboCheckEqInt(LayoutBaselineMetricsExtent(&metrics), 17);

    /* A negative term in the run record -- which the seam's own contract forbids but a
     * hand-built record can carry -- is clamped to zero rather than propagated into a
     * negative ascent. */
    LayoutTextRunInit(&run);
    run.ascent = -4;
    run.descent = -4;
    run.height = 0;
    RoboCheckEqInt(LayoutBaselineMetricsFromRun(&run, &metrics), RoboOk);
    RoboCheckEqInt(metrics.ascent, 0);
    RoboCheckEqInt(metrics.descent, 0);
    RoboCheckEqInt(LayoutBaselineMetricsIsValid(&metrics), 1);

    RoboCheckEqInt(LayoutBaselineMetricsFromRun(NULL, &metrics),
                      RoboInvalidArgument);
    RoboCheckEqInt(LayoutBaselineMetricsFromRun(&run, NULL),
                      RoboInvalidArgument);
}

/* ------------------------------------------------------------------ */
/* The fallback                                                        */
/* ------------------------------------------------------------------ */

static void TestBaselineMetricsFromExtent(void)
{
    LayoutBaselineMetrics metrics;

    /* The whole extent is above the baseline: the object's bottom edge is its floor. */
    RoboCheckEqInt(LayoutBaselineMetricsFromExtent(24, &metrics), RoboOk);
    RoboCheckEqInt((int)metrics.kind, (int)RoboLayoutBaselineBox);
    RoboCheckEqInt(metrics.ascent, 24);
    RoboCheckEqInt(metrics.descent, 0);
    RoboCheckEqInt(LayoutBaselineMetricsExtent(&metrics), 24);

    /* Zero is a legitimate answer: an empty inline box sits on the baseline with no
     * extent, and the invariant `ascent + descent == extent` holds at zero. */
    RoboCheckEqInt(LayoutBaselineMetricsFromExtent(0, &metrics), RoboOk);
    RoboCheckEqInt(metrics.ascent, 0);
    RoboCheckEqInt(metrics.descent, 0);
    RoboCheckEqInt(LayoutBaselineMetricsIsValid(&metrics), 1);

    /* A negative extent is clamped, so the door is total and the invariant survives. */
    RoboCheckEqInt(LayoutBaselineMetricsFromExtent(-5, &metrics), RoboOk);
    RoboCheckEqInt(metrics.ascent, 0);
    RoboCheckEqInt(metrics.descent, 0);

    RoboCheckEqInt(LayoutBaselineMetricsFromExtent(4, NULL),
                      RoboInvalidArgument);
}

/* ------------------------------------------------------------------ */
/* A line, after a real pass                                           */
/* ------------------------------------------------------------------ */

static void TestBaselineLineFromText(void)
{
    static const LayoutBaselineRule rules[] = {
        LAYOUT_BASELINE_BLOCK_RULES,
        { "span", RoboCssDisplayInline, 0.0, 0.0 }
    };
    LayoutBaselineFixture fixture;
    LayoutBaselineScript script;
    LayoutBaselineMetrics metrics;
    LayoutTextMetrics measurer;
    LayoutLineBox line;
    int32_t offset = -1;
    int32_t baselineY = -1;

    LayoutFixture();
    LayoutBaselineBegin(&fixture, "<html><body><p><span>abc</span></p></body></html>",
                          rules, LAYOUT_BASELINE_RULE_COUNT(rules));

    memset(&script, 0, sizeof(script));
    script.perCodePoint = 10;
    script.ascent = 12;
    script.descent = 4;
    script.gap = 2;
    measurer = LayoutBaselineScriptMetrics(&script);

    LayoutBaselinePass(&fixture, LAYOUT_BASELINE_WIDE_VIEWPORT, &measurer, NULL);

    RoboCheckEqInt(LayoutTreeLineCount(fixture.tree), 1u);
    LayoutBaselineLine(&fixture, 0u, &line);

    /*
     * The line's baseline came from a font, its height is its two terms, and the three
     * numbers cannot disagree -- because the height *is* their sum rather than a fourth
     * quantity that happens to match.
     */
    RoboCheckEqInt((int)line.baseline.kind, (int)RoboLayoutBaselineText);
    RoboCheckEqInt(line.baseline.ascent, 13); /* 12 + gap/2 */
    RoboCheckEqInt(line.baseline.descent, 5); /* 4 + the rest */
    RoboCheckEqInt(line.size.height, 18);
    RoboCheckEqInt(line.size.height,
                      LayoutBaselineMetricsExtent(&line.baseline));

    /* The two spaces the phase distinguishes: the line's own, and its owner's. */
    RoboCheckEqInt(LayoutLineBoxBaselineOffset(&line, &offset), RoboOk);
    RoboCheckEqInt(offset, 13);
    RoboCheckEqInt(LayoutLineBoxBaselineY(&line, &baselineY), RoboOk);
    RoboCheckEqInt(baselineY, line.origin.y + 13);

    /* The metrics by value, and equal to the fields. */
    RoboCheckEqInt(LayoutLineBoxBaseline(&line, &metrics), RoboOk);
    RoboCheckEqInt(LayoutBaselineMetricsEqual(&metrics, &line.baseline), 1);

    LayoutBaselineEnd(&fixture);
}

static void TestBaselineObjectMetrics(void)
{
    static const LayoutBaselineRule rules[] = {
        LAYOUT_BASELINE_BLOCK_RULES,
        { "span", RoboCssDisplayInline, 0.0, 0.0 }
    };
    LayoutBaselineFixture fixture;
    LayoutBaselineScript script;
    LayoutBaselineMetrics metrics;
    LayoutTextMetrics measurer;
    LayoutNodeId textObject;

    LayoutFixture();
    LayoutBaselineBegin(&fixture, "<html><body><p><span>abc</span></p></body></html>",
                          rules, LAYOUT_BASELINE_RULE_COUNT(rules));

    memset(&script, 0, sizeof(script));
    script.perCodePoint = 10;
    script.ascent = 12;
    script.descent = 4;
    script.gap = 0;
    measurer = LayoutBaselineScriptMetrics(&script);
    LayoutBaselinePass(&fixture, LAYOUT_BASELINE_WIDE_VIEWPORT, &measurer, NULL);

    textObject = LayoutBaselineTextObject(&fixture, LayoutBaselineBoxOf(&fixture, "p"));
    RoboCheck(textObject != RoboLayoutNodeIdNone);
    LayoutBaselineOf(&fixture, textObject, &metrics);

    /* A text object's baseline is its font's, without the gap. */
    RoboCheckEqInt((int)metrics.kind, (int)RoboLayoutBaselineText);
    RoboCheckEqInt(metrics.ascent, 12);
    RoboCheckEqInt(metrics.descent, 4);

    /*
     * An object that was **not placed** is a miss rather than a zero. The `<span>` here
     * is a transparent inline box that placed nothing (it holds only a text run, which
     * *is* placed, so the span is transparent-with-contents) -- so it is a participant
     * of the context and a member of no line, and there is no floor to report.
     * `RoboNotFound` is the documented answer (RoboLayoutObject.h), and asserting it
     * is what keeps \"it has no baseline\" from being read as \"its baseline is zero\".
     */
    {
        LayoutNodeId span = LayoutBaselineBoxOf(&fixture, "span");
        LayoutBaselineMetrics spanMetrics;
        LayoutResult result;

        RoboCheckEqInt(LayoutObjectBaseline(fixture.tree, span, &spanMetrics, NULL),
                          RoboNotFound);
        RoboCheckEqInt((int)spanMetrics.kind, (int)RoboLayoutBaselineNone);
        /* ...and it *does* have geometry: the two facts are different, which is what
         * makes this a miss about membership and not about placement. */
        RoboCheckEqInt(LayoutObjectResult(fixture.tree, span, &result), RoboOk);
        RoboCheckEqInt((int)result.state, (int)RoboLayoutResultComputed);
    }

    /* NULL and id refusals, one each. */
    RoboCheckEqInt(LayoutObjectBaseline(NULL, textObject, &metrics, NULL),
                      RoboInvalidArgument);
    RoboCheckEqInt(LayoutObjectBaseline(fixture.tree, textObject, NULL, NULL),
                      RoboInvalidArgument);
    RoboCheckEqInt(LayoutObjectBaseline(fixture.tree, RoboLayoutNodeIdNone,
                                                 &metrics, NULL),
                      RoboInvalidArgument);
    RoboCheckEqInt(LayoutObjectBaseline(fixture.tree, (LayoutNodeId)999u,
                                                 &metrics, NULL),
                      RoboNotFound);
    RoboCheckEqInt((int)metrics.kind, (int)RoboLayoutBaselineNone);

    LayoutBaselineEnd(&fixture);
}

/* ------------------------------------------------------------------ */
/* The phase's central claim: two sizes, one floor                     */
/* ------------------------------------------------------------------ */

static void TestBaselineSharedFloor(void)
{
    static const LayoutBaselineRule rules[] = {
        LAYOUT_BASELINE_BLOCK_RULES,
        { "span", RoboCssDisplayInline, 0.0, 0.0 },
        { "b", RoboCssDisplayInline, 0.0, 0.0 }
    };
    LayoutBaselineFixture fixture;
    LayoutBaselineScript script;
    LayoutBaselineMetrics tallMetrics;
    LayoutBaselineMetrics shortMetrics;
    LayoutTextMetrics measurer;
    LayoutLineBox line;
    LayoutPoint tallPosition;
    LayoutSize tallSize;
    LayoutPoint shortPosition;
    LayoutSize shortSize;
    int32_t tallBaselineY;
    int32_t shortBaselineY;

    LayoutFixture();
    /*
     * One line holding two text runs of **different font metrics**, which is the shape
     * the whole phase exists for: `<span>` is the taller run, `<b>` the shorter.
     */
    LayoutBaselineBegin(&fixture,
                          "<html><body><p><span>abcd</span><b>e</b></p></body></html>",
                          rules, LAYOUT_BASELINE_RULE_COUNT(rules));

    memset(&script, 0, sizeof(script));
    script.perCodePoint = 10;
    /* Four px of ascent and one of descent *per code point*, on top of a base, so the
     * four-code-point run and the one-code-point run genuinely differ: 20/5 against
     * 8/2. A script that answered both identically would leave this case asserting
     * that two equal things are equal. */
    script.ascentPerCodePoint = 4;
    script.descentPerCodePoint = 1;
    script.ascent = 4;
    script.descent = 1;
    script.gap = 0;
    measurer = LayoutBaselineScriptMetrics(&script);
    LayoutBaselinePass(&fixture, LAYOUT_BASELINE_WIDE_VIEWPORT, &measurer, NULL);

    RoboCheckEqInt(LayoutTreeLineCount(fixture.tree), 1u);
    LayoutBaselineLine(&fixture, 0u, &line);

    /*
     * The point of the case is the *invariant* rather than the numbers: a line whose
     * members differ in size must still place them on one floor, and each member's own
     * baseline must land there. The two members are compared through the metrics the
     * tree stored for them, so the assertions do not assume the rule they check.
     *
     * The runs are read rather than the boxes that hold them: a transparent box's
     * rectangle is the union of its contents, so reading the box and the run would mix
     * two coordinate answers for one placement.
     */
    /*
     * A nested member's y is stated in its **parent's** space, not the line's: 10.4's
     * rebasing pass moves every participant into the box that contains it after the
     * unions are known. So a run inside a transparent inline box is read by adding that
     * box's own y -- which is why both numbers are read here rather than the run alone.
     */
    {
        LayoutNodeId p = LayoutBaselineBoxOf(&fixture, "p");
        LayoutNodeId spanRun =
            LayoutBaselineTextObject(&fixture, LayoutBaselineBoxOf(&fixture, "span"));
        LayoutNodeId bRun =
            LayoutBaselineTextObject(&fixture, LayoutBaselineBoxOf(&fixture, "b"));

        RoboCheck(spanRun != RoboLayoutNodeIdNone);
        RoboCheck(bRun != RoboLayoutNodeIdNone);
        LayoutBaselineOf(&fixture, spanRun, &tallMetrics);
        LayoutBaselineOf(&fixture, bRun, &shortMetrics);
        LayoutBaselineGeometry(&fixture, spanRun, &tallPosition, &tallSize);
        LayoutBaselineGeometry(&fixture, bRun, &shortPosition, &shortSize);
        /* Restated in the paragraph's space, which is the space the line is in. */
        tallPosition.y = LayoutBaselineYInOwner(&fixture, spanRun, p);
        shortPosition.y = LayoutBaselineYInOwner(&fixture, bRun, p);
    }

    /* The two runs really are different sizes, which is what makes the floor shared
     * rather than trivially the same. */
    RoboCheckEqInt(tallMetrics.ascent, 20);
    RoboCheckEqInt(shortMetrics.ascent, 8);
    RoboCheck(tallMetrics.ascent != shortMetrics.ascent);

    tallBaselineY = tallPosition.y + tallMetrics.ascent;
    shortBaselineY = shortPosition.y + shortMetrics.ascent;
    RoboCheckEqInt(tallBaselineY, shortBaselineY);

    /* And the shared floor *is* the line's, in the line's own space and its owner's. */
    RoboCheckEqInt(tallBaselineY, line.origin.y + line.baseline.ascent);
    RoboCheckEqInt((int)line.baseline.kind, (int)RoboLayoutBaselineText);

    /* Every member is inside its line's rectangle, both vertically and horizontally --
     * the property the union walk depends on and the one a wrong maximum would break. */
    RoboCheck(tallPosition.y >= line.origin.y);
    RoboCheck(tallPosition.y + tallSize.height <= line.origin.y + line.size.height);
    RoboCheck(shortPosition.y >= line.origin.y);
    RoboCheck(shortPosition.y + shortSize.height <= line.origin.y + line.size.height);

    LayoutBaselineEnd(&fixture);
}

static void TestBaselineMixedSizeIsNotTopAligned(void)
{
    static const LayoutBaselineRule rules[] = {
        LAYOUT_BASELINE_BLOCK_RULES,
        { "span", RoboCssDisplayInline, 0.0, 0.0 },
        /*
         * Both atomics declare a **width** as well as a height: an atomic inline with
         * `width: auto` takes the containing block's width (10.4's rule, and CSS's),
         * so two of them would each fill the line and the second would wrap -- and the
         * case would then compare a member against a line it is not on.
         *
         * `<i>` and `<u>` rather than two invented tags, and that is a fact about the
         * HTML parser rather than about layout: a start tag it does not know is not kept
         * inside the paragraph, so a fixture built on `<tall>` would place that box as a
         * *block sibling* after the paragraph -- and the case would then be comparing two
         * lines' geometry while claiming to compare one line's members.
         */
        { "i", RoboCssDisplayInlineBlock, 30.0, 10.0 },
        { "u", RoboCssDisplayInlineBlock, 40.0, 40.0 }
    };
    LayoutBaselineFixture fixture;
    LayoutBaselineScript script;
    LayoutTextMetrics measurer;
    LayoutStats stats;
    LayoutLineBox line;
    LayoutPoint smallPosition;
    LayoutSize smallSize;
    LayoutPoint tallPosition;
    LayoutSize tallSize;

    LayoutFixture();
    LayoutBaselineBegin(&fixture,
                          "<html><body><p><i></i><u></u></p></body></html>", rules, LAYOUT_BASELINE_RULE_COUNT(rules));

    memset(&script, 0, sizeof(script));
    script.perCodePoint = 10;
    script.ascent = 12;
    script.descent = 4;
    measurer = LayoutBaselineScriptMetrics(&script);
    LayoutBaselinePass(&fixture, LAYOUT_BASELINE_WIDE_VIEWPORT, &measurer, &stats);

    /* The fixture's own claim, checked before the geometry that depends on it: both
     * boxes are atomics and both sizes came from their declarations rather than from
     * what they contained (nothing). Without this the case's numbers would be read as a
     * statement about baselines when they were a statement about a lost declaration. */
    RoboCheckEqInt((int)stats.inlineFlow.atomicPlaced, 2);
    RoboCheckEqInt((int)stats.inlineFlow.heightsDeclared, 2);
    RoboCheckEqInt((int)stats.inlineFlow.widthsDeclared, 2);

    /* The paragraph really holds both boxes, checked before the geometry: a case whose
     * second box had been moved out of the paragraph by the parser would be measuring
     * some other element's rectangle while claiming to measure a member. */
    {
        LayoutNodeId p = LayoutBaselineBoxOf(&fixture, "p");
        LayoutNodeId child = RoboLayoutNodeIdNone;
        int children = 0;

        if (LayoutObjectFirstChild(fixture.tree, p, &child) == RoboOk) {
            for (;;) {
                children++;
                if (LayoutObjectNextSibling(fixture.tree, child, &child) != RoboOk) {
                    break;
                }
            }
        }
        RoboCheckEqInt(children, 2);
    }
    /* And both boxes carry the declaration the rules gave them, which is the other
     * half of the same claim: a box inside the paragraph whose style never matched the
     * source would be a block or an unstyled inline, and the numbers below would then
     * be about a different document than the one the rules describe. */
    RoboCheckEqInt((int)LayoutObjectDisplay(fixture.tree,
                                                      LayoutBaselineBoxOf(&fixture, "i")),
                      (int)RoboCssDisplayInlineBlock);
    RoboCheckEqInt((int)LayoutObjectDisplay(fixture.tree,
                                                      LayoutBaselineBoxOf(&fixture, "u")),
                      (int)RoboCssDisplayInlineBlock);
    RoboCheckEqInt(LayoutTreeLineCount(fixture.tree), 1u);
    LayoutBaselineLine(&fixture, 0u, &line);

    /*
     * Two **atomic** inlines of different declared heights, so the phase's rule is
     * visible without text metrics in the way: both took the bottom-edge fallback, so
     * their baselines are their feet and their feet must coincide. Before this phase
     * both sat at the line's top and the 10px box's bottom edge was 30px above the
     * 40px box's -- the failure this case names.
     */
    LayoutBaselineGeometry(&fixture, LayoutBaselineBoxOf(&fixture, "i"),
                             &smallPosition, &smallSize);
    LayoutBaselineGeometry(&fixture, LayoutBaselineBoxOf(&fixture, "u"),
                             &tallPosition, &tallSize);

    RoboCheckEqInt(smallSize.height, 10);
    RoboCheckEqInt(tallSize.height, 40);
    RoboCheckEqInt(smallPosition.y + smallSize.height,
                      tallPosition.y + tallSize.height);
    RoboCheckEqInt(tallPosition.y, line.origin.y);
    RoboCheckEqInt(line.size.height, 40);
    RoboCheckEqInt((int)line.baseline.kind, (int)RoboLayoutBaselineBox);
    RoboCheckEqInt(line.baseline.ascent, 40);
    RoboCheckEqInt(line.baseline.descent, 0);

    LayoutBaselineEnd(&fixture);
}

static void TestBaselineDescentDeepensTheLine(void)
{
    static const LayoutBaselineRule rules[] = {
        LAYOUT_BASELINE_BLOCK_RULES,
        { "span", RoboCssDisplayInline, 0.0, 0.0 }
    };
    LayoutBaselineFixture fixture;
    LayoutBaselineScript script;
    LayoutBaselineMetrics metrics;
    LayoutTextMetrics measurer;
    LayoutLineBox line;
    int32_t baselineY = -1;

    LayoutFixture();
    LayoutBaselineBegin(&fixture, "<html><body><p><span>ab</span></p></body></html>",
                          rules, LAYOUT_BASELINE_RULE_COUNT(rules));

    /*
     * A run whose descent is large and ascent small: the line's *floor* moves down to
     * make room below it, and the member is placed so that its own descent fits. A
     * build that only ever looked at the ascent would put the line's floor at the top
     * of this run and let it hang out of the bottom of the rectangle.
     */
    memset(&script, 0, sizeof(script));
    script.perCodePoint = 10;
    script.ascent = 4;
    script.descent = 26;
    script.gap = 0;
    measurer = LayoutBaselineScriptMetrics(&script);
    LayoutBaselinePass(&fixture, LAYOUT_BASELINE_WIDE_VIEWPORT, &measurer, NULL);

    LayoutBaselineLine(&fixture, 0u, &line);
    RoboCheckEqInt(line.baseline.ascent, 4);
    RoboCheckEqInt(line.baseline.descent, 26);
    RoboCheckEqInt(line.size.height, 30);
    RoboCheckEqInt(LayoutLineBoxBaselineY(&line, &baselineY), RoboOk);
    RoboCheckEqInt(baselineY, line.origin.y + 4);

    LayoutBaselineOf(&fixture,
                       LayoutBaselineTextObject(&fixture, LayoutBaselineBoxOf(&fixture, "p")),
                       &metrics);
    /* The run sits at the line's top, because the line's ascent *is* its ascent -- and
     * that is the check that the maximum is being read the right way round. */
    {
        LayoutPoint position;
        LayoutSize size;

        LayoutBaselineGeometry(
            &fixture, LayoutBaselineTextObject(&fixture, LayoutBaselineBoxOf(&fixture, "p")),
            &position, &size);
        RoboCheckEqInt(position.y, line.origin.y);
        RoboCheckEqInt(size.height, 30);
    }

    LayoutBaselineEnd(&fixture);
}

/* ------------------------------------------------------------------ */
/* The pass: several lines, empty content, re-layout, accounting        */
/* ------------------------------------------------------------------ */

static void TestBaselineMultipleLines(void)
{
    static const LayoutBaselineRule rules[] = {
        LAYOUT_BASELINE_BLOCK_RULES,
        { "span", RoboCssDisplayInline, 0.0, 0.0 }
    };
    LayoutBaselineFixture fixture;
    LayoutBaselineScript script;
    LayoutTextMetrics measurer;
    LayoutStats stats;
    LayoutLineBox line;
    size_t index;
    int32_t expectedTop = 0;

    LayoutFixture();
    /*
     * Three runs and a viewport narrower than two of them, which is the arithmetic the
     * case needs: 40px per run against 50px of line means one run fits, the second
     * wraps, and the third wraps again. Two runs would give two lines however narrow
     * the viewport is, because a first member is placed even when it overflows.
     */
    LayoutBaselineBegin(&fixture,
                          "<html><body><p><span>aaaa</span><span>bbbb</span>"
                          "<span>cccc</span></p></body></html>",
                          rules, LAYOUT_BASELINE_RULE_COUNT(rules));

    memset(&script, 0, sizeof(script));
    script.perCodePoint = 10; /* 40px per run, 50px viewport -> three lines */
    script.ascent = 12;
    script.descent = 4;
    script.gap = 0;
    measurer = LayoutBaselineScriptMetrics(&script);
    LayoutBaselinePass(&fixture, 50, &measurer, &stats);

    RoboCheckEqInt(LayoutTreeLineCount(fixture.tree), 3u);
    RoboCheckEqInt((int)stats.inlineFlow.lines, 3);
    RoboCheckEqInt((int)stats.inlineFlow.linesFromTextBaseline, 3);
    RoboCheckEqInt((int)stats.inlineFlow.linesFromFallbackBaseline, 0);

    /*
     * Each line has its own baseline, at its own y, and the lines stack by
     * `ascent + descent` -- which with scripted metrics is the run's height, so the
     * vertical arithmetic is the number a reader can check by hand.
     */
    for (index = 0u; index < 3u; index++) {
        int32_t offset = -1;
        int32_t baselineY = -1;

        LayoutBaselineLine(&fixture, index, &line);
        RoboCheckEqInt((int)line.index, (int)index);
        RoboCheckEqInt(line.origin.y, expectedTop);
        RoboCheckEqInt(line.baseline.ascent, 12);
        RoboCheckEqInt(line.baseline.descent, 4);
        RoboCheckEqInt(line.size.height, 16);
        RoboCheckEqInt(LayoutLineBoxBaselineOffset(&line, &offset), RoboOk);
        RoboCheckEqInt(offset, 12);
        RoboCheckEqInt(LayoutLineBoxBaselineY(&line, &baselineY), RoboOk);
        RoboCheckEqInt(baselineY, expectedTop + 12);
        RoboCheckEqInt((int)line.baseline.kind, (int)RoboLayoutBaselineText);
        expectedTop += 16;
    }

    LayoutBaselineEnd(&fixture);
}

static void TestBaselineEmptyContent(void)
{
    static const LayoutBaselineRule rules[] = {
        LAYOUT_BASELINE_BLOCK_RULES,
        { "span", RoboCssDisplayInline, 0.0, 0.0 }
    };
    LayoutBaselineFixture fixture;
    LayoutBaselineScript script;
    LayoutTextMetrics measurer;
    LayoutStats stats;
    LayoutLineBox line;

    /* An **empty** block container: nothing on a line, so no line at all -- and
     * therefore no baseline to be confused about. The baseline model does not change
     * 10.6's rule that a line exists because something was placed on it. */
    LayoutFixture();
    LayoutBaselineBegin(&fixture, "<html><body><p></p></body></html>", rules, LAYOUT_BASELINE_RULE_COUNT(rules));
    LayoutBaselinePass(&fixture, LAYOUT_BASELINE_WIDE_VIEWPORT, NULL, &stats);
    RoboCheckEqInt(LayoutTreeLineCount(fixture.tree), 0u);
    RoboCheckEqInt((int)stats.inlineFlow.lines, 0);
    RoboCheckEqInt((int)stats.inlineFlow.linesFromTextBaseline, 0);
    RoboCheckEqInt((int)stats.inlineFlow.linesFromFallbackBaseline, 0);
    LayoutBaselineEnd(&fixture);

    /*
     * An **empty span** as the only content: the span is a leaf (no children to walk),
     * so it is a member of a line -- and its baseline is the fallback with no extent.
     * The line exists, is 0 tall, and its floor is at its top. That is the deterministic
     * answer for content with no metrics, and it is deliberately *not* \"no line\".
     */
    LayoutBaselineBegin(&fixture, "<html><body><p><span></span></p></body></html>", rules, LAYOUT_BASELINE_RULE_COUNT(rules));
    LayoutBaselinePass(&fixture, LAYOUT_BASELINE_WIDE_VIEWPORT, NULL, &stats);
    RoboCheckEqInt(LayoutTreeLineCount(fixture.tree), 1u);
    LayoutBaselineLine(&fixture, 0u, &line);
    RoboCheckEqInt((int)line.baseline.kind, (int)RoboLayoutBaselineBox);
    RoboCheckEqInt(line.baseline.ascent, 0);
    RoboCheckEqInt(line.baseline.descent, 0);
    RoboCheckEqInt(line.size.height, 0);
    RoboCheckEqInt((int)stats.inlineFlow.linesFromFallbackBaseline, 1);
    RoboCheckEqInt((int)stats.inlineFlow.linesFromTextBaseline, 0);
    LayoutBaselineEnd(&fixture);

    /*
     * Text that nothing measured: a TEXT baseline of zero extent. This is the row that
     * separates \"anchored on text, nothing measured\" from \"no baseline\", and it is
     * the case that keeps the arena's refusal from being reachable in the field.
     */
    LayoutBaselineBegin(&fixture, "<html><body><p><span>abc</span></p></body></html>", rules, LAYOUT_BASELINE_RULE_COUNT(rules));
    LayoutBaselinePass(&fixture, LAYOUT_BASELINE_WIDE_VIEWPORT, NULL, &stats);
    RoboCheckEqInt(LayoutTreeLineCount(fixture.tree), 1u);
    LayoutBaselineLine(&fixture, 0u, &line);
    RoboCheckEqInt((int)line.baseline.kind, (int)RoboLayoutBaselineText);
    RoboCheckEqInt(line.baseline.ascent, 0);
    RoboCheckEqInt(line.baseline.descent, 0);
    RoboCheckEqInt(line.size.height, 0);
    RoboCheckEqInt((int)stats.inlineFlow.linesFromTextBaseline, 1);
    RoboCheckEqInt((int)stats.text.unmeasured, 1);
    LayoutBaselineEnd(&fixture);

    /* And the script with a zero extent but one that *did* measure, so the difference
     * between the two rows above is the measurement and not the numbers. */
    LayoutBaselineBegin(&fixture, "<html><body><p><span>abc</span></p></body></html>", rules, LAYOUT_BASELINE_RULE_COUNT(rules));
    memset(&script, 0, sizeof(script));
    script.perCodePoint = 10;
    script.ascent = 0;
    script.descent = 0;
    script.gap = 0;
    measurer = LayoutBaselineScriptMetrics(&script);
    LayoutBaselinePass(&fixture, LAYOUT_BASELINE_WIDE_VIEWPORT, &measurer, &stats);
    LayoutBaselineLine(&fixture, 0u, &line);
    RoboCheckEqInt((int)line.baseline.kind, (int)RoboLayoutBaselineText);
    RoboCheckEqInt(line.size.height, 0);
    RoboCheckEqInt((int)stats.text.measured, 1);
    RoboCheckEqInt((int)stats.text.unmeasured, 0);
    LayoutBaselineEnd(&fixture);
}

static void TestBaselineRelayout(void)
{
    static const LayoutBaselineRule rules[] = {
        LAYOUT_BASELINE_BLOCK_RULES,
        { "span", RoboCssDisplayInline, 0.0, 0.0 }
    };
    LayoutBaselineFixture fixture;
    LayoutBaselineScript script;
    LayoutTextMetrics measurer;
    LayoutLineBox line;
    LayoutLineBox firstPass;

    LayoutFixture();
    /* Three one-code-point runs, so a narrow viewport has something to wrap. */
    LayoutBaselineBegin(&fixture,
                          "<html><body><p><span>a</span><span>b</span><span>c</span></p>"
                          "</body></html>",
                          rules, LAYOUT_BASELINE_RULE_COUNT(rules));

    memset(&script, 0, sizeof(script));
    script.perCodePoint = 10;
    script.ascent = 10;
    script.descent = 2;
    script.gap = 0;
    measurer = LayoutBaselineScriptMetrics(&script);
    LayoutBaselinePass(&fixture, LAYOUT_BASELINE_WIDE_VIEWPORT, &measurer, NULL);
    LayoutBaselineLine(&fixture, 0u, &line);
    firstPass = line;
    RoboCheckEqInt(line.baseline.ascent, 10);
    RoboCheckEqInt(line.size.height, 12);

    /*
     * A second pass with **different metrics** must move the baseline, and the fact that
     * it can is what makes the first pass's numbers a measurement rather than a
     * constant. The line's id is reused (ids restart at 1), so this also pins that a
     * baseline is replaced rather than accumulated.
     */
    script.ascent = 30;
    script.descent = 6;
    LayoutBaselinePass(&fixture, LAYOUT_BASELINE_WIDE_VIEWPORT, &measurer, NULL);
    LayoutBaselineLine(&fixture, 0u, &line);
    RoboCheckEqInt(LayoutTreeLineCount(fixture.tree), 1u);
    RoboCheckEqInt(line.id, firstPass.id);
    RoboCheckEqInt(line.baseline.ascent, 30);
    RoboCheckEqInt(line.baseline.descent, 6);
    RoboCheckEqInt(line.size.height, 36);
    RoboCheckEqInt(LayoutBaselineMetricsEqual(&line.baseline, &firstPass.baseline),
                      0);

    /* And a viewport that fits exactly one run re-wraps to three lines with three
     * baselines -- the dependent geometry recomputed from the same tree, each line
     * stacked by the extent the second pass's metrics gave it. */
    LayoutBaselinePass(&fixture, 10, &measurer, NULL);
    RoboCheckEqInt(LayoutTreeLineCount(fixture.tree), 3u);
    LayoutBaselineLine(&fixture, 1u, &line);
    RoboCheckEqInt(line.origin.y, 36);
    RoboCheckEqInt(line.baseline.ascent, 30);

    LayoutBaselineEnd(&fixture);
}

static void TestBaselineDeterminism(void)
{
    static const LayoutBaselineRule rules[] = {
        LAYOUT_BASELINE_BLOCK_RULES,
        { "span", RoboCssDisplayInline, 0.0, 0.0 },
        /* A declared width, so the atomic sits between the two runs on one line
         * instead of taking the whole line and wrapping. */
        { "i", RoboCssDisplayInlineBlock, 10.0, 18.0 }
    };
    LayoutBaselineFixture fixture;
    LayoutBaselineScript script;
    LayoutTextMetrics measurer;
    LayoutLineBox first;
    LayoutLineBox second;
    LayoutStats firstStats;
    LayoutStats secondStats;

    LayoutFixture();
    LayoutBaselineBegin(&fixture,
                          "<html><body><p><span>ab</span><i></i><span>cd</span></p></body></html>",
                          rules, LAYOUT_BASELINE_RULE_COUNT(rules));

    memset(&script, 0, sizeof(script));
    script.perCodePoint = 10;
    script.ascent = 14;
    script.descent = 3;
    script.gap = 4;
    measurer = LayoutBaselineScriptMetrics(&script);

    LayoutBaselinePass(&fixture, LAYOUT_BASELINE_WIDE_VIEWPORT, &measurer, &firstStats);
    LayoutBaselineLine(&fixture, 0u, &first);

    /* The same pass again on the same tree: the record must come back identical, which
     * rules out an accumulator that carried a previous pass's maximum forward. */
    LayoutBaselinePass(&fixture, LAYOUT_BASELINE_WIDE_VIEWPORT, &measurer, &secondStats);
    LayoutBaselineLine(&fixture, 0u, &second);

    RoboCheckEqInt(LayoutLineBoxEqual(&first, &second), 1);
    RoboCheckEqInt(LayoutInlineStatsEqual(&firstStats.inlineFlow,
                                                    &secondStats.inlineFlow),
                      1);
    /*
     * The line's extent is eighteen above and five below, and neither number is the
     * text's alone: the run's own half-leading gives 16/5, and the 18px atomic raises
     * the ascent to 18 while leaving the descent at 5. A build that took its ascent
     * from the text only would answer 21 here and would still satisfy every other
     * assertion in the case.
     */
    RoboCheckEqInt(first.baseline.ascent, 18);
    RoboCheckEqInt(first.baseline.descent, 5);
    RoboCheckEqInt(first.size.height, 23);
    /*
     * And the kind is the *box*'s, because the kind belongs to the first member among
     * the tallest and the 18px atomic out-reaches both text runs. A reader might expect
     * TEXT on a line that has text on it; the rule is stated this way so that the answer
     * is a fact about document order rather than about which participant a walk happened
     * to visit first, and this assertion is what keeps the rule from being quietly
     * rewritten into "prefer text".
     */
    RoboCheckEqInt((int)first.baseline.kind, (int)RoboLayoutBaselineBox);

    LayoutBaselineEnd(&fixture);
}

static void TestBaselineAccounting(void)
{
    static const LayoutBaselineRule rules[] = {
        LAYOUT_BASELINE_BLOCK_RULES,
        { "span", RoboCssDisplayInline, 0.0, 0.0 },
        { "i", RoboCssDisplayInlineBlock, 10.0, 24.0 }
    };
    LayoutBaselineFixture fixture;
    LayoutBaselineScript script;
    LayoutTextMetrics measurer;
    LayoutStats stats;
    LayoutLineBox line;
    LayoutLineId id = RoboLayoutLineIdNone;
    size_t lines;
    size_t index;
    size_t membersTotal = 0u;

    LayoutFixture();
    LayoutBaselineBegin(&fixture,
                          "<html><body><p><span>ab</span><i></i><span>cd</span></p></body></html>",
                          rules, LAYOUT_BASELINE_RULE_COUNT(rules));

    memset(&script, 0, sizeof(script));
    script.perCodePoint = 10;
    script.ascent = 20;
    script.descent = 5;
    script.gap = 0;
    measurer = LayoutBaselineScriptMetrics(&script);
    LayoutBaselinePass(&fixture, 60, &measurer, &stats);

    lines = LayoutTreeLineCount(fixture.tree);
    RoboCheck(lines > 0u);

    /*
     * The identity the phase adds to the record: the two baseline-kind counters
     * partition the lines. Every stored line has exactly one kind and this build
     * produces exactly two, so a build that invented a third or forgot to count one
     * fails here rather than in a later phase's arithmetic.
     */
    RoboCheckEqInt((int)(stats.inlineFlow.linesFromTextBaseline +
                            stats.inlineFlow.linesFromFallbackBaseline),
                      (int)lines);
    RoboCheckEqInt((int)stats.inlineFlow.lines, (int)lines);

    /* And per line: the height is the extent, the floor is inside the rectangle, every
     * member is inside it, and the member's own baseline lands on the line's. */
    for (index = 0u; index < lines; index++) {
        LayoutNodeId owner = LayoutBaselineBoxOf(&fixture, "p");
        int32_t baselineY = -1;
        size_t memberIndex;

        RoboCheckEqInt(LayoutTreeLineAt(fixture.tree, index, &id, NULL), RoboOk);
        RoboCheckEqInt(LayoutTreeLine(fixture.tree, id, &line, NULL), RoboOk);
        RoboCheckEqInt(line.size.height,
                          LayoutBaselineMetricsExtent(&line.baseline));
        RoboCheckEqInt(LayoutBaselineMetricsIsValid(&line.baseline), 1);
        RoboCheckEqInt(LayoutLineBoxBaselineY(&line, &baselineY), RoboOk);
        RoboCheck(baselineY >= line.origin.y);
        RoboCheck(baselineY <= line.origin.y + line.size.height);
        RoboCheckEqInt(baselineY, line.origin.y + line.baseline.ascent);

        /* Every member's own baseline is the line's, read back through the object door
         * so the assertion goes through the same surface a later phase would. */
        for (memberIndex = 0u; memberIndex < line.members; memberIndex++) {
            LayoutNodeId node = RoboLayoutNodeIdNone;
            LayoutBaselineMetrics memberMetrics;
            LayoutPoint position;
            LayoutSize size;

            RoboCheckEqInt(GetLayoutLineMember(fixture.tree, id, memberIndex, &node,
                                                     NULL),
                              RoboOk);
            LayoutBaselineOf(&fixture, node, &memberMetrics);
            LayoutBaselineGeometry(&fixture, node, &position, &size);
            /* A member's stored y is relative to the box that holds it, so the
             * comparison with the line is made in the owner's space. */
            position.y = LayoutBaselineYInOwner(&fixture, node, owner);
            RoboCheckEqInt(position.y + memberMetrics.ascent, baselineY);
            RoboCheck(position.y >= line.origin.y);
            RoboCheck(position.y + size.height <= line.origin.y + line.size.height);
        }
        membersTotal += line.members;
    }

    /* The membership count is still the placements, not the participants: the phase
     * changed where a member goes and not which objects are members. */
    RoboCheckEqInt((int)membersTotal, (int)stats.inlineFlow.lineMembers);

    /* The extent the block pass needs is still the last line's bottom edge. */
    {
        LayoutNodeId p = LayoutBaselineBoxOf(&fixture, "p");
        LayoutResult result;
        LayoutSize size;

        RoboCheckEqInt(LayoutObjectResult(fixture.tree, p, &result), RoboOk);
        RoboCheckEqInt((int)result.state, (int)RoboLayoutResultComputed);
        RoboCheckEqInt(LayoutResultSize(&result, &size), RoboOk);
        RoboCheckEqInt(size.height, line.origin.y + line.size.height);
    }

    LayoutBaselineEnd(&fixture);
}

static void TestBaselineValidation(void)
{
    static const LayoutBaselineRule rules[] = {
        LAYOUT_BASELINE_BLOCK_RULES,
        { "span", RoboCssDisplayInline, 0.0, 0.0 }
    };
    LayoutBaselineFixture fixture;
    LayoutLineBox line;
    LayoutBaselineMetrics metrics;
    int32_t offset = -1;

    LayoutFixture();
    LayoutBaselineBegin(&fixture, "<html><body><p></p></body></html>", rules, LAYOUT_BASELINE_RULE_COUNT(rules));
    LayoutBaselinePass(&fixture, LAYOUT_BASELINE_WIDE_VIEWPORT, NULL, NULL);

    /* A tree with no lines: every line door reports the miss rather than answering
     * zero, which is the discipline the whole module follows. */
    RoboCheckEqInt(LayoutTreeLineAt(fixture.tree, 0u, NULL, NULL),
                      RoboInvalidArgument);
    RoboCheckEqInt(LayoutTreeLine(fixture.tree, RoboLayoutLineIdNone, &line, NULL),
                      RoboInvalidArgument);
    RoboCheckEqInt(LayoutTreeLine(NULL, (LayoutLineId)1u, &line, NULL),
                      RoboInvalidArgument);

    /*
     * And a line record a caller built by hand has **no** usable baseline, so the two
     * baseline doors refuse it. That is the shape of a record `LayoutLineAdd`
     * would not have stored -- the arena refuses a NONE baseline -- so the doors are
     * consistent with the arena rather than merely tolerant.
     */
    LayoutLineBoxInit(&line);
    RoboCheckEqInt(LayoutLineBoxBaseline(&line, &metrics), RoboOk);
    RoboCheckEqInt((int)metrics.kind, (int)RoboLayoutBaselineNone);
    RoboCheckEqInt(LayoutLineBoxBaselineOffset(&line, &offset), RoboNotFound);
    RoboCheckEqInt(offset, 0);
    {
        int32_t baselineY = -1;

        RoboCheckEqInt(LayoutLineBoxBaselineY(&line, &baselineY), RoboNotFound);
        RoboCheckEqInt(baselineY, 0);
    }

    RoboCheckEqInt(LayoutLineBoxBaseline(NULL, &metrics), RoboInvalidArgument);
    RoboCheckEqInt(LayoutLineBoxBaseline(&line, NULL), RoboInvalidArgument);
    RoboCheckEqInt(LayoutLineBoxBaselineOffset(NULL, &offset),
                      RoboInvalidArgument);
    RoboCheckEqInt(LayoutLineBoxBaselineOffset(&line, NULL),
                      RoboInvalidArgument);
    RoboCheckEqInt(LayoutLineBoxBaselineY(NULL, &offset), RoboInvalidArgument);
    RoboCheckEqInt(LayoutLineBoxBaselineY(&line, NULL), RoboInvalidArgument);

    /* The offset conversion is clamped rather than wrapped, so a line near the top of
     * the pixel range cannot report a floor above its own origin. */
    line.origin.y = INT32_MAX;
    line.baseline.kind = RoboLayoutBaselineBox;
    line.baseline.ascent = 10;
    line.baseline.descent = 0;
    RoboCheckEqInt(LayoutLineBoxBaselineY(&line, &offset), RoboOk);
    RoboCheckEqInt(offset, INT32_MAX);

    LayoutBaselineEnd(&fixture);
}

static void TestBaselineAllocatorBalance(void)
{
    static const LayoutBaselineRule rules[] = {
        LAYOUT_BASELINE_BLOCK_RULES,
        { "span", RoboCssDisplayInline, 0.0, 0.0 }
    };
    LayoutBaselineFixture fixture;
    LayoutBaselineScript script;
    LayoutTextMetrics measurer;
    size_t before;
    size_t settled;
    int pass;

    LayoutFixture();

    /*
     * Taken **before the tree exists**, so the final assertion brackets the whole
     * lifetime of the module's storage rather than only the passes: nothing a baseline
     * costs may outlive the tree that owned it. Equality rather than a strict decrease,
     * for the reason every other `allocator_balance` case in this directory gives -- the
     * counter behind `LayoutLiveBlocks` is a debug-build instrument and reads 0 in a
     * release build, so an assertion that demanded a drop would pass in Debug and fail
     * in Release while saying nothing about baselines in either configuration.
     */
    before = LayoutLiveBlocks();
    LayoutBaselineBegin(&fixture, "<html><body><p><span>abc</span></p></body></html>",
                          rules, LAYOUT_BASELINE_RULE_COUNT(rules));

    memset(&script, 0, sizeof(script));
    script.perCodePoint = 10;
    script.ascent = 12;
    script.descent = 4;
    script.gap = 2;
    measurer = LayoutBaselineScriptMetrics(&script);

    /*
     * One *settled* pass, so the loop's baseline is taken after the arena has reached
     * its working capacity -- an arena that reserves on demand and keeps what it
     * reserved is not a leak, and a case that called it one would start failing as soon
     * as the document needed a second line.
     *
     * What the loop then asserts is the *repeat*: four further passes over an unchanged
     * tree must leave the count exactly where the settled pass found it. The phase added
     * no arena of its own -- a baseline lives on the object and on the line -- so the
     * number must not move at all.
     */
    LayoutBaselinePass(&fixture, LAYOUT_BASELINE_WIDE_VIEWPORT, &measurer, NULL);
    RoboCheckEqInt(LayoutTreeLineCount(fixture.tree), 1u);
    settled = LayoutLiveBlocks();

    for (pass = 0; pass < 4; pass++) {
        LayoutBaselinePass(&fixture, LAYOUT_BASELINE_WIDE_VIEWPORT, &measurer, NULL);
        RoboCheckEqInt(LayoutTreeLineCount(fixture.tree), 1u);
        RoboCheckEqInt(LayoutLiveBlocks(), settled);
    }

    LayoutBaselineEnd(&fixture);
    RoboCheckEqInt(LayoutLiveBlocks(), before);
}

/* ------------------------------------------------------------------ */
/* Registration                                                        */
/* ------------------------------------------------------------------ */

void TestRegisterLayoutBaseline(void)
{
    TestRegister("layout.baseline.kind_vocabulary", TestBaselineKindVocabulary);
    TestRegister("layout.baseline.metrics_record", TestBaselineMetricsRecord);
    TestRegister("layout.baseline.metrics_from_run", TestBaselineMetricsFromRun);
    TestRegister("layout.baseline.metrics_from_extent",
                       TestBaselineMetricsFromExtent);
    TestRegister("layout.baseline.line_from_text", TestBaselineLineFromText);
    TestRegister("layout.baseline.object_metrics", TestBaselineObjectMetrics);
    TestRegister("layout.baseline.shared_floor", TestBaselineSharedFloor);
    TestRegister("layout.baseline.mixed_size_is_not_top_aligned",
                       TestBaselineMixedSizeIsNotTopAligned);
    TestRegister("layout.baseline.descent_deepens_the_line",
                       TestBaselineDescentDeepensTheLine);
    TestRegister("layout.baseline.multiple_lines", TestBaselineMultipleLines);
    TestRegister("layout.baseline.empty_content", TestBaselineEmptyContent);
    TestRegister("layout.baseline.relayout", TestBaselineRelayout);
    TestRegister("layout.baseline.determinism", TestBaselineDeterminism);
    TestRegister("layout.baseline.accounting", TestBaselineAccounting);
    TestRegister("layout.baseline.validation", TestBaselineValidation);
    TestRegister("layout.baseline.allocator_balance", TestBaselineAllocatorBalance);
}
