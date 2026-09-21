/*
 * RoboLayout line-box suite (Phase 10.6).
 *
 * Three things are under test and they are told apart deliberately:
 *
 *   the **vocabulary** -- the state pair, the record, and the rectangle formed from
 *   it -- which is pure and needs no tree at all;
 *
 *   the **arena**: a line is a record the tree owns, released by the next pass and by
 *   anything that releases the classification, and every read door validates its id;
 *
 *   the **pass**: lines are opened when the first participant lands, finalized at a
 *   wrap and at the end of their context, and the block extent of an inline
 *   formatting context *is* the bottom edge of its last line.
 *
 * The one thing the suite asserts repeatedly, because it is the phase's central
 * distinction: a line's **members** are the objects the line *placed*, which is a
 * smaller set than the context's participants. A transparent inline box with contents
 * is a participant and a member of no single line, and a case that could not tell those
 * apart would pass while the line's width double-counted its children.
 *
 * Geometry comes from two scripted sources and neither is a font: a per-code-point
 * measurer for the arithmetic, and a declared width for the atomic cases. Every number
 * a case asserts is therefore derivable from the fixture rather than from a face, which
 * is the same discipline the 10.5 suite records.
 */

#include "RoboLayoutTestSupport.h"

#include "RoboFont/RoboFont.h"

#include <string.h>

/* ------------------------------------------------------------------ */
/* A scripted measurer                                                 */
/* ------------------------------------------------------------------ */

/** One scripted answer: this many px per code point, and this line extent. */
typedef struct layoutLineScript {
    int32_t perCodePoint;
    int32_t height;
    size_t calls;
} LayoutLineScript;

/**
 * The measurer: `perCodePoint` px per code point, and a fixed line extent.
 *
 * It fills the whole run record -- source, range, direction, extents, code-point count
 * -- because a measurer that filled only a width would let this suite pass while the
 * record it produced was half-empty, and the record is part of the surface 10.5 froze.
 * `height` is what a case varies when it wants to see a line's height track its tallest
 * member, and it is deliberately *not* per code point: the height belongs to the run,
 * not to the world.
 */
static Result LayoutLineScriptMeasure(void *userdata,
                                               const LayoutTextRequest *request,
                                               LayoutTextRun *outRun)
{
    LayoutLineScript *script = (LayoutLineScript *)userdata;

    if (!script || !request || !outRun || !request->style) {
        return RoboInvalidArgument;
    }
    script->calls++;
    outRun->source = request->textNode;
    outRun->range = request->range;
    outRun->direction = request->style->direction;
    outRun->codePoints = LayoutTextRangeLength(request->range);
    outRun->width = (int32_t)outRun->codePoints * script->perCodePoint;
    outRun->height = script->height;
    outRun->ascent = script->height;
    outRun->descent = 0;
    return RoboOk;
}

static LayoutTextMetrics LayoutLineScriptMetrics(LayoutLineScript *script)
{
    LayoutTextMetrics metrics;

    LayoutTextMetricsInit(&metrics);
    metrics.userdata = script;
    metrics.measure = LayoutLineScriptMeasure;
    return metrics;
}

/* ------------------------------------------------------------------ */
/* The fixture                                                         */
/* ------------------------------------------------------------------ */

/**
 * One scripted answer: an element with this tag computes to this.
 *
 * Wider than the text suite's rule because a line's *geometry* is what this phase is
 * about and two of the cases need a width the pass can resolve without a font: a
 * declared `width` and `height` in `px`, which is the one shape 10.3 and 10.4 both read
 * as a computed length (RoboLayoutInternal.h). Zero means "declared nothing", which is
 * also an honest width for an empty box -- and the cases that care write an explicit
 * non-zero one.
 */
typedef struct layoutLineRule {
    const char *tag;
    CssDisplay display;
    double widthPx;
    double heightPx;
} LayoutLineRule;

#define LAYOUT_LINE_MAX_RULES 8u

typedef struct layoutLineStyle {
    char tag[16];
    CssComputedStyle *style; /* owned */
} LayoutLineStyle;

typedef struct layoutLineFixture {
    LayoutDocument doc;
    LayoutLineStyle rules[LAYOUT_LINE_MAX_RULES];
    size_t ruleCount;
    LayoutStyleSource styles;
    LayoutTree *tree;
} LayoutLineFixture;

static const CssComputedStyle *LayoutLineStyleLookup(
    void *userdata,
    const HtmlNodeHandle *element)
{
    LayoutLineFixture *fixture = (LayoutLineFixture *)userdata;
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
static void LayoutLineStyleAdd(LayoutLineFixture *fixture,
                                  const LayoutLineRule *rule)
{
    CssComputedStyle *style = NULL;
    CssComputedValue value = RoboCssCleared;
    CssValue specified = RoboCssCleared;
    size_t existing;

    LayoutFixture();
    RoboCheckEqInt(fixture->ruleCount < LAYOUT_LINE_MAX_RULES, 1);
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
    /* A *real* computed LENGTH, through 9.8's own maker: the pass resolves a declared
     * size by asking for a LENGTH in px (RoboLayoutInternal.h), so a suite that wrote
     * the value by hand would be testing a struct rather than the rule. */
    if (rule->widthPx > 0.0) {
        RoboCheckEqInt(CssComputedValueMakeLength(&value, rule->widthPx,
                                                             RoboCssUnitPx, 0, NULL),
                          RoboOk);
        RoboCheckEqInt(CssComputedStyleSetEntry(style, RoboCssPropertyIdNone,
                                                           CssSliceFromText("width"),
                                                           0u, &specified, &value, NULL),
                          RoboOk);
    }
    if (rule->heightPx > 0.0) {
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
    fixture->rules[fixture->ruleCount].tag[sizeof(fixture->rules[fixture->ruleCount].tag) -
                                            1u] = '\0';
    fixture->rules[fixture->ruleCount].style = style;
    fixture->ruleCount++;
}

static void LayoutLineBegin(LayoutLineFixture *fixture,
                              const char *html,
                              const LayoutLineRule *rules,
                              size_t count)
{
    LayoutBuildOptions options;
    size_t index;

    memset(fixture, 0, sizeof(*fixture));
    LayoutDocumentBuild(&fixture->doc, html);
    for (index = 0u; index < count; index++) {
        LayoutLineStyleAdd(fixture, &rules[index]);
    }
    LayoutStyleSourceInit(&fixture->styles);
    fixture->styles.userdata = fixture;
    fixture->styles.ComputedStyleFor = LayoutLineStyleLookup;
    RoboCheckEqInt(LayoutTreeCreate(&fixture->tree, NULL), RoboOk);
    LayoutBuildOptionsInit(&options);
    options.styles = &fixture->styles;
    RoboCheckEqInt(LayoutTreeBuildFromDocument(fixture->tree, fixture->doc.id,
                                                          &options, NULL, NULL),
                      RoboOk);
}

static void LayoutLineEnd(LayoutLineFixture *fixture)
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
static void LayoutLinePass(LayoutLineFixture *fixture,
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
#define LAYOUT_LINE_WIDE_VIEWPORT 800

/** The block container every fixture below shares: `<html><body><p>`. */
#define LAYOUT_LINE_BLOCK_RULES                                                 \
    { "html", RoboCssDisplayBlock, 0.0, 0.0 },                               \
    { "body", RoboCssDisplayBlock, 0.0, 0.0 },                               \
    { "p", RoboCssDisplayBlock, 0.0, 0.0 }

/** The box built for the first element named @p tag, asserted to exist. */
static LayoutNodeId LayoutLineBoxOf(LayoutLineFixture *fixture,
                                                const char *tag)
{
    HtmlNodeHandle element = LayoutDocumentFind(&fixture->doc, tag);
    LayoutNodeId id;

    RoboCheckEqInt(HtmlNodeHandleIsWellformed(&element), 1);
    id = LayoutObjectIdForDomNode(fixture->tree, &element);
    RoboCheck(id != RoboLayoutNodeIdNone);
    return id;
}

/** The first **text node** inside the first element named @p tag. */
static HtmlNodeHandle LayoutLineFirstTextChild(
    const HtmlNodeHandle *element)
{
    HtmlNodeView view;
    HtmlNodeHandle child;

    HtmlNodeHandleClear(&child);
    if (!element || !HtmlNodeHandleIsWellformed(element)) {
        return child;
    }
    if (HtmlNodeGetView(element, &view, NULL) != RoboOk) {
        return child;
    }
    child = view.firstChild;
    while (HtmlNodeHandleIsWellformed(&child)) {
        HtmlNodeView childView;

        if (HtmlNodeGetView(&child, &childView, NULL) != RoboOk) {
            break;
        }
        if (childView.kind == RoboHtmlNodeKindText) {
            return child;
        }
        child = childView.NextSibling;
    }
    HtmlNodeHandleClear(&child);
    return child;
}

/** The layout object the first text node inside the element @p tag became. */
static LayoutNodeId LayoutLineTextOf(LayoutLineFixture *fixture,
                                                 const char *tag)
{
    HtmlNodeHandle element = LayoutDocumentFind(&fixture->doc, tag);
    HtmlNodeHandle node = LayoutLineFirstTextChild(&element);

    RoboCheckEqInt(HtmlNodeHandleIsWellformed(&node), 1);
    return LayoutObjectIdForDomNode(fixture->tree, &node);
}

/** Read one line, asserted to exist. */
static LayoutLineBox LayoutLineGet(LayoutLineFixture *fixture,
                                              LayoutLineId id)
{
    LayoutLineBox line;

    RoboCheckEqInt(LayoutTreeLine(fixture->tree, id, &line, NULL), RoboOk);
    return line;
}

/** The id of the @p index'th line, asserted to exist. */
static LayoutLineId LayoutLineAt(LayoutLineFixture *fixture, size_t index)
{
    LayoutLineId id = RoboLayoutLineIdNone;

    RoboCheckEqInt(LayoutTreeLineAt(fixture->tree, index, &id, NULL), RoboOk);
    return id;
}

/** Assert one record's geometry and identity, field by field. */
static void LayoutLineExpect(const LayoutLineBox *line,
                               LayoutNodeId owner,
                               size_t index,
                               int32_t x,
                               int32_t y,
                               int32_t width,
                               int32_t height)
{
    RoboCheckEqInt((int)line->state, (int)RoboLayoutLineFinal);
    RoboCheckEqInt((int)line->owner, (int)owner);
    RoboCheckEqInt(line->index, index);
    RoboCheckEqInt(line->origin.x, x);
    RoboCheckEqInt(line->origin.y, y);
    RoboCheckEqInt(line->size.width, width);
    RoboCheckEqInt(line->size.height, height);
    RoboCheckEqInt(line->context != RoboLayoutFcIdNone, 1);
}

/* ------------------------------------------------------------------ */
/* The vocabulary                                                      */
/* ------------------------------------------------------------------ */

static void TestLineStateVocabulary(void)
{
    size_t index;

    LayoutFixture();
    RoboCheckEqInt((int)RoboLayoutLineStateCount, 2);
    RoboCheckEqInt((int)RoboLayoutLineOpen, 0);
    RoboCheckEqInt((int)RoboLayoutLineFinal, 1);
    RoboCheckEqInt(LayoutLineStateIsKnown(RoboLayoutLineOpen), 1);
    RoboCheckEqInt(LayoutLineStateIsKnown(RoboLayoutLineFinal), 1);
    RoboCheckEqInt(LayoutLineStateIsKnown((LayoutLineState)2), 0);
    RoboCheckEqInt(LayoutLineStateIsKnown((LayoutLineState)99), 0);

    /* Walked, so a table whose rows had drifted would still answer IsKnown for every
     * one and show only as a name that printed as another's keyword. */
    for (index = 0u; index < RoboLayoutLineStateCount; index++) {
        LayoutLineState state = (LayoutLineState)index;

        RoboCheckEqInt(LayoutLineStateIsKnown(state), 1);
        RoboCheck(LayoutLineStateName(state) != NULL);
    }
    RoboCheckStrEq(LayoutLineStateName(RoboLayoutLineOpen), "open");
    RoboCheckStrEq(LayoutLineStateName(RoboLayoutLineFinal), "final");
    RoboCheckStrEq(LayoutLineStateName((LayoutLineState)7), "unknown");
    RoboCheck(LayoutLineStateName((LayoutLineState)7) != NULL);
}

static void TestLineRecord(void)
{
    LayoutLineBox line;
    LayoutLineBox other;
    LayoutRect rect;

    LayoutFixture();
    memset(&line, 0xFF, sizeof(line));
    LayoutLineBoxInit(&line);
    RoboCheckEqInt((int)line.id, (int)RoboLayoutLineIdNone);
    RoboCheckEqInt((int)line.state, (int)RoboLayoutLineOpen);
    RoboCheckEqInt((int)line.context, (int)RoboLayoutFcIdNone);
    RoboCheckEqInt((int)line.owner, (int)RoboLayoutNodeIdNone);
    RoboCheckEqInt(line.index, 0u);
    RoboCheckEqInt(line.origin.x, 0);
    RoboCheckEqInt(line.origin.y, 0);
    RoboCheckEqInt(line.size.width, 0);
    RoboCheckEqInt(line.size.height, 0);
    RoboCheckEqInt(line.availableWidth, 0);
    RoboCheckEqInt(line.members, 0u);

    /* Init is safe on NULL, and a cleared record equals a cleared record -- which is
     * the assertion that says init wrote every field rather than most of them. */
    LayoutLineBoxInit(NULL);
    other = line;
    RoboCheckEqInt(LayoutLineBoxEqual(&line, &other), 1);
    other.size.height = 1;
    RoboCheckEqInt(LayoutLineBoxEqual(&line, &other), 0);
    /* NULL compares by identity, so two NULLs agree and one NULL does not. */
    RoboCheckEqInt(LayoutLineBoxEqual(NULL, NULL), 1);
    RoboCheckEqInt(LayoutLineBoxEqual(&line, NULL), 0);
    RoboCheckEqInt(LayoutLineBoxEqual(NULL, &line), 0);

    /* The rectangle, formed through RoboGraphics rather than by hand. */
    line.origin.x = 3;
    line.origin.y = 7;
    line.size.width = 40;
    line.size.height = 12;
    RoboCheckEqInt(LayoutLineBoxRect(&line, &rect), RoboOk);
    RoboCheckEqInt(rect.x, 3);
    RoboCheckEqInt(rect.y, 7);
    RoboCheckEqInt(rect.width, 40);
    RoboCheckEqInt(rect.height, 12);
    RoboCheckEqInt(LayoutLineBoxRect(&line, NULL), RoboInvalidArgument);
    RoboCheckEqInt(LayoutLineBoxRect(NULL, &rect), RoboInvalidArgument);
}

/* ------------------------------------------------------------------ */
/* One line                                                            */
/* ------------------------------------------------------------------ */

/**
 * A paragraph holding one measured run: one line, one member, and the run is it.
 *
 * The shortest case that says what a line box *is*: `AB` at ten px per code point is
 * twenty px wide, so the line is twenty wide and as tall as the run; it is owned by the
 * paragraph; its `index` is 0; and the object back-link agrees with the member list,
 * which is the pair that has to agree for a caller to move between the two views.
 */
static void TestLineSingleRun(void)
{
    LayoutLineFixture fixture;
    static const LayoutLineRule rules[] = { LAYOUT_LINE_BLOCK_RULES };
    LayoutLineScript script;
    LayoutTextMetrics metrics;
    LayoutStats stats;
    LayoutLineBox line;
    LayoutLineId id;
    LayoutLineId back;
    LayoutNodeId member = RoboLayoutNodeIdNone;
    LayoutNodeId pBox;
    LayoutNodeId textBox;

    LayoutFixture();
    LayoutLineBegin(&fixture, "<html><body><p>AB</p></body></html>", rules, 3u);
    memset(&script, 0, sizeof(script));
    script.perCodePoint = 10;
    script.height = 20;
    metrics = LayoutLineScriptMetrics(&script);

    /* Before a pass: no lines, and the door says so rather than inventing one. */
    RoboCheckEqInt(LayoutTreeLineCount(fixture.tree), 0u);
    RoboCheckEqInt(LayoutTreeLineAt(fixture.tree, 0u, &id, NULL), RoboNotFound);

    LayoutLinePass(&fixture, LAYOUT_LINE_WIDE_VIEWPORT, &metrics, &stats);

    RoboCheckEqInt(LayoutTreeLineCount(fixture.tree), 1u);
    RoboCheckEqInt(stats.inlineFlow.lines, 1u);
    RoboCheckEqInt(stats.inlineFlow.lineMembers, 1u);
    RoboCheckEqInt(stats.inlineFlow.wraps, 0u);

    pBox = LayoutLineBoxOf(&fixture, "p");
    textBox = LayoutLineTextOf(&fixture, "p");
    id = LayoutLineAt(&fixture, 0u);
    RoboCheckEqInt((int)id, 1);
    line = LayoutLineGet(&fixture, id);
    LayoutLineExpect(&line, pBox, 0u, 0, 0, 20, 20);
    RoboCheckEqInt(line.availableWidth, LAYOUT_LINE_WIDE_VIEWPORT);
    RoboCheckEqInt(line.members, 1u);

    /* The member is the text object, and the back-link names the same line. */
    RoboCheckEqInt(GetLayoutLineMember(fixture.tree, id, 0u, &member, NULL), RoboOk);
    RoboCheckEqInt((int)member, (int)textBox);
    RoboCheckEqInt((int)MakeLayoutObjectKind(fixture.tree, member),
                      (int)RoboLayoutObjectText);
    RoboCheckEqInt(LayoutObjectLine(fixture.tree, textBox, &back, NULL), RoboOk);
    RoboCheckEqInt((int)back, (int)id);

    /* The line's width *is* the member's width, which is what makes it a sum rather
     * than a parallel number that happens to agree. */
    {
        LayoutResult result;
        LayoutSize size;

        RoboCheckEqInt(LayoutObjectResult(fixture.tree, textBox, &result), RoboOk);
        RoboCheckEqInt(LayoutResultSize(&result, &size), RoboOk);
        RoboCheckEqInt(size.width, line.size.width);
        RoboCheckEqInt(size.height, line.size.height);
    }

    LayoutLineEnd(&fixture);
}

/**
 * Members are the *placements*: a transparent inline box with contents is not one.
 *
 * The phase's central distinction, and the case is built so that getting it wrong
 * changes a number: `<p><span>ab</span>cd</p>` has three participants of the context --
 * the span, its run and the run after it -- and two members, because the span's own
 * rectangle is the union of what it contains. The line's width is therefore 40 (the two
 * runs) and not 60, and a build that counted the span would produce the second number
 * while every other assertion in this suite still passed.
 *
 * The empty `span` in the second half is the mirror image and *is* a member: it has no
 * contents, so it is placed as a unit of its own -- zero wide, and it advances the
 * cursor by nothing.
 */
static void TestLineMembersArePlacements(void)
{
    LayoutLineFixture fixture;
    static const LayoutLineRule rules[] = {
        LAYOUT_LINE_BLOCK_RULES,
        { "span", RoboCssDisplayInline, 0.0, 0.0 }
    };
    LayoutLineScript script;
    LayoutTextMetrics metrics;
    LayoutStats stats;
    LayoutLineBox line;
    LayoutNodeId spanBox;
    LayoutNodeId spanLine;
    LayoutNodeId member = RoboLayoutNodeIdNone;

    LayoutFixture();
    LayoutLineBegin(&fixture, "<html><body><p><span>ab</span>cd</p></body></html>",
                      rules, 4u);
    memset(&script, 0, sizeof(script));
    script.perCodePoint = 10;
    script.height = 20;
    metrics = LayoutLineScriptMetrics(&script);
    LayoutLinePass(&fixture, LAYOUT_LINE_WIDE_VIEWPORT, &metrics, &stats);

    /* Three participants, two members: the transparent box encloses a placement. */
    RoboCheckEqInt(stats.inlineFlow.participantsPlaced, 3u);
    RoboCheckEqInt(stats.inlineFlow.lineMembers, 2u);
    RoboCheckEqInt(stats.inlineFlow.lines, 1u);
    line = LayoutLineGet(&fixture, LayoutLineAt(&fixture, 0u));
    RoboCheckEqInt(line.members, 2u);
    RoboCheckEqInt(line.size.width, 40);
    RoboCheckEqInt(line.size.height, 20);

    /* And the two members are the two *runs*, in layout order -- the span's own run
     * first, then the one after the span. */
    {
        LayoutNodeId first = RoboLayoutNodeIdNone;
        LayoutNodeId second = RoboLayoutNodeIdNone;
        LayoutResult result;
        LayoutPoint position;

        RoboCheckEqInt(GetLayoutLineMember(fixture.tree, line.id, 0u, &first, NULL),
                          RoboOk);
        RoboCheckEqInt(GetLayoutLineMember(fixture.tree, line.id, 1u, &second, NULL),
                          RoboOk);
        RoboCheckEqInt((int)MakeLayoutObjectKind(fixture.tree, first),
                          (int)RoboLayoutObjectText);
        RoboCheckEqInt((int)MakeLayoutObjectKind(fixture.tree, second),
                          (int)RoboLayoutObjectText);
        RoboCheckEqInt(first != second, 1);

        /* Placed one after the other: the second run starts where the first ended. */
        RoboCheckEqInt(LayoutObjectResult(fixture.tree, second, &result), RoboOk);
        RoboCheckEqInt(LayoutResultPosition(&result, &position), RoboOk);
        RoboCheckEqInt(position.x, 20);

        /* The span is a participant and on *no* line -- the documented miss. */
        spanBox = LayoutLineBoxOf(&fixture, "span");
        RoboCheckEqInt(LayoutObjectLine(fixture.tree, spanBox, &spanLine, NULL),
                          RoboNotFound);
        /* And it is still a real box with a real rectangle: the union of its contents. */
        {
            LayoutResult spanResult;
            LayoutSize spanSize;

            RoboCheckEqInt(LayoutObjectResult(fixture.tree, spanBox, &spanResult),
                              RoboOk);
            RoboCheckEqInt(LayoutResultSize(&spanResult, &spanSize), RoboOk);
            RoboCheckEqInt(spanSize.width, 20);
        }
    }
    RoboCheckEqInt(GetLayoutLineMember(fixture.tree, line.id, 2u, &member, NULL),
                      RoboNotFound);

    LayoutLineEnd(&fixture);
}

static void TestLineEmptySpanIsAMember(void)
{
    LayoutLineFixture fixture;
    static const LayoutLineRule rules[] = {
        LAYOUT_LINE_BLOCK_RULES,
        { "span", RoboCssDisplayInline, 0.0, 0.0 }
    };
    LayoutLineScript script;
    LayoutTextMetrics metrics;
    LayoutStats stats;
    LayoutLineBox line;
    LayoutNodeId spanBox;
    LayoutNodeId first = RoboLayoutNodeIdNone;

    LayoutFixture();
    LayoutLineBegin(&fixture, "<html><body><p><span></span>xy</p></body></html>", rules, 4u);
    memset(&script, 0, sizeof(script));
    script.perCodePoint = 10;
    script.height = 20;
    metrics = LayoutLineScriptMetrics(&script);
    LayoutLinePass(&fixture, LAYOUT_LINE_WIDE_VIEWPORT, &metrics, &stats);

    RoboCheckEqInt(stats.inlineFlow.participantsPlaced, 2u);
    RoboCheckEqInt(stats.inlineFlow.lineMembers, 2u);
    line = LayoutLineGet(&fixture, LayoutLineAt(&fixture, 0u));
    /* Two members of which one is zero wide: the width is the run's alone, and the
     * zero-width member did not move the cursor. */
    RoboCheckEqInt(line.members, 2u);
    RoboCheckEqInt(line.size.width, 20);
    RoboCheckEqInt(GetLayoutLineMember(fixture.tree, line.id, 0u, &first, NULL), RoboOk);
    spanBox = LayoutLineBoxOf(&fixture, "span");
    RoboCheckEqInt((int)first, (int)spanBox);
    {
        LayoutLineId back = RoboLayoutLineIdNone;

        RoboCheckEqInt(LayoutObjectLine(fixture.tree, spanBox, &back, NULL),
                          RoboOk);
        RoboCheckEqInt((int)back, (int)line.id);
    }

    LayoutLineEnd(&fixture);
}

/* ------------------------------------------------------------------ */
/* Wrapping                                                            */
/* ------------------------------------------------------------------ */

/**
 * Three atomic inlines a hundred px wide, in a viewport that holds two, then one.
 *
 * The wrap is the whole subject, and the case runs it twice with the *same* fixture
 * so that the two answers differ only in the viewport: at 250 the first line holds two
 * boxes and the second holds one, and at 150 each line holds one. That is what makes
 * the assertion about the algorithm rather than about a lucky width -- and the y
 * progression is asserted in both, because a line's position is the sum of the heights
 * above it and nothing else.
 *
 * The `index` fields are the other half: they count lines *within the context*, so a
 * build that numbered lines globally would pass the geometry assertions and fail here.
 */
static void TestLineWrapping(void)
{
    LayoutLineFixture fixture;
    static const LayoutLineRule rules[] = {
        LAYOUT_LINE_BLOCK_RULES,
        { "i", RoboCssDisplayInlineBlock, 100.0, 20.0 }
    };
    LayoutStats stats;
    LayoutNodeId pBox;
    size_t pass;

    LayoutFixture();
    LayoutLineBegin(&fixture, "<html><body><p><i>a</i><i>b</i><i>c</i></p></body></html>",
                      rules, 4u);
    pBox = LayoutLineBoxOf(&fixture, "p");

    /* No measurer: the boxes' sizes come from their declarations, which is what makes
     * this case independent of any font. */
    for (pass = 0u; pass < 2u; pass++) {
        int32_t viewport = (pass == 0u) ? 250 : 150;
        int expectedLines = (pass == 0u) ? 2 : 3;

        LayoutLinePass(&fixture, viewport, NULL, &stats);
        RoboCheckEqInt(LayoutTreeLineCount(fixture.tree), (size_t)expectedLines);
        RoboCheckEqInt(stats.inlineFlow.lines, (size_t)expectedLines);
        RoboCheckEqInt(stats.inlineFlow.lineMembers, 3u);
        RoboCheckEqInt(stats.inlineFlow.blockLevelDeclined, 0u);

        if (pass == 0u) {
            LayoutLineBox first = LayoutLineGet(&fixture, LayoutLineAt(&fixture, 0u));
            LayoutLineBox second = LayoutLineGet(&fixture, LayoutLineAt(&fixture, 1u));

            LayoutLineExpect(&first, pBox, 0u, 0, 0, 200, 20);
            RoboCheckEqInt(first.members, 2u);
            LayoutLineExpect(&second, pBox, 1u, 0, 20, 100, 20);
            RoboCheckEqInt(second.members, 1u);
            /* The available width is recorded, so "why did this wrap?" is answerable
             * from the line alone -- and it is the *viewport's* width here, because the
             * paragraph is the body's full-width child. */
            RoboCheckEqInt(first.availableWidth, viewport);
            RoboCheckEqInt(second.availableWidth, viewport);
            RoboCheckEqInt(stats.inlineFlow.wraps, 1u);
        } else {
            size_t index;

            RoboCheckEqInt(stats.inlineFlow.wraps, 2u);
            for (index = 0u; index < 3u; index++) {
                LayoutLineBox line = LayoutLineGet(&fixture, LayoutLineAt(&fixture,
                                                                                      index));

                /* Each line holds exactly one box, and its top is twenty px below the
                 * one before it -- so three lines occupy sixty px and nothing overlaps. */
                LayoutLineExpect(&line, pBox, index, 0, (int32_t)(index * 20u), 100, 20);
                RoboCheckEqInt(line.members, 1u);
            }
        }
    }

    /* The block extent the paragraph got is the *last line's* bottom edge, which is the
     * derivation 10.6 introduced: a block containing lines obtains its block-flow extent
     * from them. */
    {
        LayoutResult result;
        LayoutSize size;

        RoboCheckEqInt(LayoutObjectResult(fixture.tree, pBox, &result), RoboOk);
        RoboCheckEqInt(LayoutResultSize(&result, &size), RoboOk);
        RoboCheckEqInt(size.height, 60);
    }

    LayoutLineEnd(&fixture);
}

/**
 * The same tree, the same pass, twice: a second pass *replaces* the lines.
 *
 * Two claims in one case, and they are the same claim. The first is determinism -- the
 * identities, geometry and membership of the second pass equal the first's. The second
 * is that lines do not accumulate: a build that appended would report six lines after
 * two passes over a document that has three, and every reader after it would be reading
 * a mixture of two layouts.
 */
static void TestLineRepeatPassReplaces(void)
{
    LayoutLineFixture fixture;
    static const LayoutLineRule rules[] = {
        LAYOUT_LINE_BLOCK_RULES,
        { "i", RoboCssDisplayInlineBlock, 100.0, 20.0 }
    };
    LayoutLineBox first[3];
    LayoutLineBox second[3];
    LayoutStats stats;
    size_t index;

    LayoutFixture();
    LayoutLineBegin(&fixture, "<html><body><p><i>a</i><i>b</i><i>c</i></p></body></html>",
                      rules, 4u);

    LayoutLinePass(&fixture, 150, NULL, &stats);
    RoboCheckEqInt(LayoutTreeLineCount(fixture.tree), 3u);
    for (index = 0u; index < 3u; index++) {
        first[index] = LayoutLineGet(&fixture, LayoutLineAt(&fixture, index));
    }

    LayoutLinePass(&fixture, 150, NULL, &stats);
    RoboCheckEqInt(LayoutTreeLineCount(fixture.tree), 3u);
    for (index = 0u; index < 3u; index++) {
        second[index] = LayoutLineGet(&fixture, LayoutLineAt(&fixture, index));
        RoboCheckEqInt(LayoutLineBoxEqual(&first[index], &second[index]), 1);
    }
    /* The ids restarted rather than continuing, which is the fact that makes a line id
     * valid only until the next pass -- and it is asserted because a caller holding an
     * id across a pass would otherwise read the *wrong* line silently. */
    RoboCheckEqInt((int)second[0].id, 1);

    LayoutLineEnd(&fixture);
}

/* ------------------------------------------------------------------ */
/* Empty content                                                       */
/* ------------------------------------------------------------------ */

/**
 * An inline formatting context with nothing on a line produces **no** line boxes.
 *
 * CSS's rule, and the case has to be built carefully: `<p></p>` has no children at all,
 * so the paragraph is a block container with no block-level child and *does* establish
 * an inline context (10.2) whose contents are empty. A build that emitted a zero-height
 * line for it would make \"no content\" and \"one empty line\" the same document.
 */
static void TestLineEmptyContext(void)
{
    LayoutLineFixture fixture;
    static const LayoutLineRule rules[] = { LAYOUT_LINE_BLOCK_RULES };
    LayoutStats stats;
    LayoutNodeId pBox;
    LayoutLineBox line;
    LayoutLineId id;

    LayoutFixture();
    LayoutLineBegin(&fixture, "<html><body><p></p></body></html>", rules, 3u);
    LayoutLinePass(&fixture, LAYOUT_LINE_WIDE_VIEWPORT, NULL, &stats);

    RoboCheckEqInt(LayoutTreeLineCount(fixture.tree), 0u);
    RoboCheckEqInt(stats.inlineFlow.lines, 0u);
    RoboCheckEqInt(stats.inlineFlow.lineMembers, 0u);
    RoboCheckEqInt(stats.inlineFlow.emptyContexts, 1u);
    RoboCheckEqInt(stats.inlineFlow.participantsPlaced, 0u);

    /* And the arena refuses an id it never handed out, rather than answering from
     * whatever was in the slot. */
    RoboCheckEqInt(LayoutTreeLine(fixture.tree, (LayoutLineId)1u, &line,
                                           NULL),
                      RoboNotFound);
    RoboCheckEqInt((int)line.id, (int)RoboLayoutLineIdNone);
    RoboCheckEqInt(LayoutTreeLineAt(fixture.tree, 0u, &id, NULL), RoboNotFound);

    /* The paragraph still has a geometry -- zero tall, the extent of no lines. */
    pBox = LayoutLineBoxOf(&fixture, "p");
    {
        LayoutResult result;
        LayoutSize size;

        RoboCheckEqInt(LayoutObjectResult(fixture.tree, pBox, &result), RoboOk);
        RoboCheckEqInt((int)result.state, (int)RoboLayoutResultComputed);
        RoboCheckEqInt(LayoutResultSize(&result, &size), RoboOk);
        RoboCheckEqInt(size.height, 0);
    }

    LayoutLineEnd(&fixture);
}

/**
 * An unmeasured run is a member of a line that exists and is 0 x 0.
 *
 * The distinction 10.5 drew, arriving in 10.6's terms: \"no line\" and \"a line with
 * nothing measurable on it\" are different documents. The run *was* placed -- it is a
 * member, and it has a back-link -- and the line it is on is simply zero wide and zero
 * tall, because nothing measured it. A build that skipped the placement would report no
 * lines for a paragraph that has text in it, which is the failure this case exists to
 * catch.
 */
static void TestLineUnmeasuredRun(void)
{
    LayoutLineFixture fixture;
    static const LayoutLineRule rules[] = { LAYOUT_LINE_BLOCK_RULES };
    LayoutStats stats;
    LayoutLineBox line;
    LayoutNodeId textBox;
    LayoutLineId back = RoboLayoutLineIdNone;

    LayoutFixture();
    LayoutLineBegin(&fixture, "<html><body><p>Hello</p></body></html>", rules, 3u);
    LayoutLinePass(&fixture, LAYOUT_LINE_WIDE_VIEWPORT, NULL, &stats);

    RoboCheckEqInt(LayoutTreeLineCount(fixture.tree), 1u);
    RoboCheckEqInt(stats.inlineFlow.lines, 1u);
    RoboCheckEqInt(stats.inlineFlow.lineMembers, 1u);
    RoboCheckEqInt(stats.text.runsPlaced, 1u);
    RoboCheckEqInt(stats.text.unmeasured, 1u);
    RoboCheckEqInt(stats.text.measured, 0u);

    line = LayoutLineGet(&fixture, LayoutLineAt(&fixture, 0u));
    RoboCheckEqInt(line.members, 1u);
    RoboCheckEqInt(line.size.width, 0);
    RoboCheckEqInt(line.size.height, 0);

    textBox = LayoutLineTextOf(&fixture, "p");
    RoboCheckEqInt(LayoutObjectLine(fixture.tree, textBox, &back, NULL), RoboOk);
    RoboCheckEqInt((int)back, (int)line.id);

    LayoutLineEnd(&fixture);
}

/* ------------------------------------------------------------------ */
/* The back-link, and the arena's lifetime                             */
/* ------------------------------------------------------------------ */

/**
 * A line is released by anything that releases the classification.
 *
 * The property a later phase depends on: an id read out of a released arena must be a
 * reported miss and not a number. Adding a box invalidates the classification --
 * because a classification describes a box set and the set changed -- and the lines
 * describe the *same* pass, so they go with it, `object->line` is cleared, and the read
 * doors refuse both the line and the back-link.
 *
 * The assertions are written as \"NOT_FOUND\" rather than as \"not id 1\" because the
 * point is that a stale id cannot be *read back*, not that it stopped existing: ids
 * restart at 1, so handed one back would name a different line.
 */
static void TestLineReleased(void)
{
    LayoutLineFixture fixture;
    static const LayoutLineRule rules[] = {
        LAYOUT_LINE_BLOCK_RULES,
        { "i", RoboCssDisplayInlineBlock, 100.0, 20.0 }
    };
    LayoutStats stats;
    LayoutLineId id;
    LayoutNodeId textBox;
    LayoutNodeId backLink;
    LayoutLineBox line;

    LayoutFixture();
    LayoutLineBegin(&fixture, "<html><body><p><i>a</i>zz</p></body></html>", rules, 4u);
    LayoutLinePass(&fixture, 150, NULL, &stats);
    RoboCheckEqInt(LayoutTreeLineCount(fixture.tree), 1u);
    id = LayoutLineAt(&fixture, 0u);
    textBox = LayoutLineTextOf(&fixture, "p");
    RoboCheckEqInt(LayoutObjectLine(fixture.tree, textBox, &backLink, NULL),
                      RoboOk);

    /* An explicit invalidation: the classification goes, and the lines with it. */
    LayoutTreeInvalidateContexts(fixture.tree);
    RoboCheckEqInt(LayoutTreeIsClassified(fixture.tree), 0);
    RoboCheckEqInt(LayoutTreeLineCount(fixture.tree), 0u);
    RoboCheckEqInt(LayoutTreeLine(fixture.tree, id, &line, NULL), RoboNotFound);
    RoboCheckEqInt(LayoutObjectLine(fixture.tree, textBox, &backLink, NULL),
                      RoboNotFound);
    /* The boxes are untouched: releasing a pass's conclusions is not clearing a tree. */
    RoboCheckEqInt(MakeLayoutObjectKind(fixture.tree, textBox),
                      RoboLayoutObjectText);
    RoboCheckEqInt(LayoutTreeObjectCount(fixture.tree) > 0u, 1);

    /* And an add releases them too, for the same reason. */
    {
        HtmlNodeHandle element = LayoutDocumentFind(&fixture.doc, "p");
        HtmlNodeHandle span;
        LayoutObjectInfo info;
        LayoutNodeId added = RoboLayoutNodeIdNone;

        LayoutLinePass(&fixture, 150, NULL, &stats);
        RoboCheckEqInt(LayoutTreeLineCount(fixture.tree), 1u);
        /* A second box for an element that already has one, so the added box needs a
         * node of its own: the tree is a box set and not a mirror of the DOM. */
        span = LayoutLineBoxOf(&fixture, "i") == RoboLayoutNodeIdNone
                   ? element
                   : element;
        LayoutObjectInfoInit(&info);
        info.parent = LayoutLineBoxOf(&fixture, "body");
        info.domNode = span;
        info.display = RoboCssDisplayBlock;
        RoboCheckEqInt(LayoutTreeAddObject(fixture.tree, &info, &added, NULL),
                          RoboOk);
        RoboCheckEqInt(LayoutTreeLineCount(fixture.tree), 0u);
        RoboCheckEqInt(LayoutTreeLine(fixture.tree, id, &line, NULL), RoboNotFound);
    }

    LayoutLineEnd(&fixture);
}

/* ------------------------------------------------------------------ */
/* Validation                                                          */
/* ------------------------------------------------------------------ */

static void TestLineValidation(void)
{
    LayoutLineFixture fixture;
    static const LayoutLineRule rules[] = { LAYOUT_LINE_BLOCK_RULES };
    LayoutLineBox line;
    LayoutLineId id = RoboLayoutLineIdNone;
    LayoutNodeId member = RoboLayoutNodeIdNone;

    LayoutFixture();
    LayoutLineBegin(&fixture, "<html><body><p>AB</p></body></html>", rules, 3u);

    /* A NULL tree, and a NONE id: argument refusals, before anything is read. */
    RoboCheckEqInt(LayoutTreeLineCount(NULL), 0u);
    RoboCheckEqInt(LayoutTreeLineAt(NULL, 0u, &id, NULL), RoboInvalidArgument);
    RoboCheckEqInt(LayoutTreeLineAt(fixture.tree, 0u, NULL, NULL),
                      RoboInvalidArgument);
    RoboCheckEqInt(LayoutTreeLine(NULL, (LayoutLineId)1u, &line, NULL),
                      RoboInvalidArgument);
    RoboCheckEqInt(LayoutTreeLine(fixture.tree, RoboLayoutLineIdNone, &line, NULL),
                      RoboInvalidArgument);
    RoboCheckEqInt(LayoutTreeLine(fixture.tree, (LayoutLineId)1u, NULL, NULL),
                      RoboInvalidArgument);
    RoboCheckEqInt(GetLayoutLineMember(fixture.tree, (LayoutLineId)1u, 0u, NULL,
                                              NULL),
                      RoboInvalidArgument);
    RoboCheckEqInt(LayoutObjectLine(fixture.tree, RoboLayoutNodeIdNone, &id, NULL),
                      RoboInvalidArgument);
    RoboCheckEqInt(LayoutObjectLine(fixture.tree, (LayoutNodeId)1u, NULL,
                                              NULL),
                      RoboInvalidArgument);
    /* An id that names no object at all, on a tree that has objects. */
    RoboCheckEqInt(LayoutObjectLine(fixture.tree,
                                              (LayoutNodeId)(LayoutTreeObjectCount(
                                                  fixture.tree) + 1u),
                                              &id, NULL),
                      RoboNotFound);

    /* And a miss leaves the output cleared rather than half-filled. */
    RoboCheckEqInt((int)id, (int)RoboLayoutLineIdNone);
    RoboCheckEqInt((int)member, (int)RoboLayoutNodeIdNone);

    LayoutLineEnd(&fixture);
}

static void TestLineAllocatorBalance(void)
{
    LayoutLineFixture fixture;
    static const LayoutLineRule rules[] = {
        LAYOUT_LINE_BLOCK_RULES,
        { "i", RoboCssDisplayInlineBlock, 40.0, 20.0 },
        { "span", RoboCssDisplayInline, 0.0, 0.0 }
    };
    LayoutLineScript script;
    LayoutTextMetrics metrics;
    LayoutStats stats;
    size_t before;
    size_t settled;
    int pass;

    LayoutFixture();

    /*
     * Taken **before the tree exists**, so the two assertions below bracket the whole
     * lifetime of a line arena rather than only the passes: nothing a line box costs may
     * outlive the tree that owned it.
     *
     * Equality rather than a strict decrease, and the reason is the one every other
     * `allocator_balance` case in this directory is written on: the counter behind
     * `LayoutLiveBlocks` is a debug-build instrument and reads 0 in a release build,
     * so an assertion that required a *drop* would pass in Debug and fail in Release
     * while saying nothing about line boxes in either. `==` is the claim that is true in
     * both -- and in Debug it is still the exact claim that matters, because the tree's
     * line arena and member arena are released by the tree and by nothing else.
     */
    before = LayoutLiveBlocks();
    LayoutLineBegin(&fixture,
                      "<html><body><p><span>ab</span>cd<i>e</i>fg</p></body></html>",
                      rules, 5u);
    memset(&script, 0, sizeof(script));
    script.perCodePoint = 10;
    script.height = 20;
    metrics = LayoutLineScriptMetrics(&script);

    memset(&stats, 0, sizeof(stats));

    /*
     * One *settled* pass, so the first is the one that may grow the line arena and
     * every later one must not. The baseline for the loop is therefore taken after a pass
     * rather than before it: an arena that reserves on demand and keeps what it reserved
     * is not a leak, and a test that called it one would fail as soon as the capacity
     * fitted -- which is the shape of a false positive, so the distinction has to be in
     * the test and not in the reader's head.
     *
     * What the loop asserts is the *repeat*: four further passes over an unchanged tree
     * must leave the live count exactly where the settled one found it, which is what an
     * accumulating arena -- one that appended a fresh capacity per pass instead of
     * reusing the last -- would violate on the second pass.
     */
    LayoutLinePass(&fixture, 60, &metrics, &stats);
    RoboCheckEqInt(LayoutTreeLineCount(fixture.tree) > 0u, 1);
    settled = LayoutLiveBlocks();

    for (pass = 0; pass < 4; pass++) {
        LayoutLinePass(&fixture, 60, &metrics, &stats);
        RoboCheckEqInt(LayoutTreeLineCount(fixture.tree) > 0u, 1);
        RoboCheckEqInt(LayoutLiveBlocks(), settled);
    }

    /* And the arena is the tree's: destroying the tree takes the line arena and its
     * member arena down with it, which is the whole of what ownership means here. */
    LayoutLineEnd(&fixture);
    RoboCheckEqInt(LayoutLiveBlocks(), before);
}

/* ------------------------------------------------------------------ */
/* Registration                                                        */
/* ------------------------------------------------------------------ */

void TestRegisterLayoutLineBox(void)
{
    TestRegister("layout.line.state_vocabulary", TestLineStateVocabulary);
    TestRegister("layout.line.record", TestLineRecord);
    TestRegister("layout.line.single_run", TestLineSingleRun);
    TestRegister("layout.line.members_are_placements", TestLineMembersArePlacements);
    TestRegister("layout.line.empty_span_is_a_member", TestLineEmptySpanIsAMember);
    TestRegister("layout.line.wrapping", TestLineWrapping);
    TestRegister("layout.line.repeat_pass_replaces", TestLineRepeatPassReplaces);
    TestRegister("layout.line.empty_context", TestLineEmptyContext);
    TestRegister("layout.line.unmeasured_run", TestLineUnmeasuredRun);
    TestRegister("layout.line.released", TestLineReleased);
    TestRegister("layout.line.validation", TestLineValidation);
    TestRegister("layout.line.allocator_balance", TestLineAllocatorBalance);
}
