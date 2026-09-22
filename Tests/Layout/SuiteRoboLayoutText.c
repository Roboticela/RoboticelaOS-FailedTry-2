/*
 * RoboLayout text suite (Phase 10.5) — layout.text.*
 *
 * The first suite in this directory whose subject is *text*: ranges in code points,
 * a font asked how wide a run is, a text node becoming a layout object, and the
 * cursor advancing by a measurement instead of by a declared width.
 *
 * Six groups, in the order the phase is built:
 *
 *   the model        ranges, direction, the resolved font inputs, the run record and
 *                    the seam's four answers (no source, no callback, a failure, an
 *                    answer)
 *
 *   reading text     code points and byte spans through RoboUnicode, and the
 *                    whitespace predicate the builder's one text rule is written on
 *
 *   the builder      a text node generating a TEXT object that borrows its parent
 *                    element's style, and a whitespace-only node generating nothing
 *
 *   the font         a **synthetic face** built in this file — cmap, hmtx, hhea,
 *                    head, maxp, loca, glyf — asked for real advances, so that
 *                    "measurement sums font units and converts once" is asserted
 *                    against arithmetic rather than against a scripted number
 *
 *   the pass         text placed along a line, counts, wrapping, direction, the
 *                    unmeasured degradation, and the rebase that a text run inside a
 *                    transparent inline box forces
 *
 *   the pipeline     the real CSS path: `font-size` through the cascade, resolved
 *                    into font inputs, measured by the real font, and read back out of
 *                    the tree — plus determinism, re-layout, the accounting identity
 *                    and the allocator
 *
 * Why two measurers. Most cases here install a **scripted** source that answers in
 * round numbers, for the reason the 10.4 suite gives for scripted styles: a case whose
 * subject is cursor arithmetic should fail because the arithmetic is wrong and not
 * because a font table was. The cases whose subject *is* measurement use the real
 * RoboFont-backed measurer over the synthetic face below, and one case installs it
 * into a context driven by a real stylesheet, which is the whole vertical slice in one
 * test.
 */

#include "RoboLayoutTestSupport.h"

#include "RoboFont/RoboFont.h"

#include <string.h>

/* ------------------------------------------------------------------ */
/* A scripted measurer                                                 */
/* ------------------------------------------------------------------ */

/** One scripted answer: this many px per code point, and this line extent. */
typedef struct layoutTextScript {
    int32_t perCodePoint;
    int32_t height;
    int32_t ascent;
    int32_t descent;
    /** Calls the seam made. What makes "measured once per run" assertable. */
    size_t calls;
    /** When non-zero, the callback fails rather than answering. */
    int fail;
} LayoutTextScript;

/**
 * The scripted measurer.
 *
 * It fills *everything* the run record carries -- source, range, direction, extents
 * and code-point count -- because a measurer that filled only a width would let the
 * suite passes while the record it produced was half-empty, and the record is part of
 * the phase's surface.
 *
 * The code-point count comes from the *range*, not from a decode, and that is exactly
 * right for a scripted answer: the range a request carries is what the measurable
 * span is, and a scripted source that decoded the DOM itself would be re-implementing
 * the layer under test.
 */
static Result LayoutTextScriptMeasure(void *userdata,
                                               const LayoutTextRequest *request,
                                               LayoutTextRun *outRun)
{
    LayoutTextScript *script = (LayoutTextScript *)userdata;

    if (!script || !request || !outRun || !request->style) {
        return RoboInvalidArgument;
    }
    script->calls++;
    if (script->fail) {
        return RoboError;
    }
    outRun->source = request->textNode;
    outRun->range = request->range;
    outRun->direction = request->style->direction;
    outRun->codePoints = LayoutTextRangeLength(request->range);
    outRun->width = (int32_t)outRun->codePoints * script->perCodePoint;
    outRun->height = script->height;
    outRun->ascent = script->ascent;
    outRun->descent = script->descent;
    return RoboOk;
}

static LayoutTextMetrics LayoutTextScriptMetrics(LayoutTextScript *script)
{
    LayoutTextMetrics metrics;

    LayoutTextMetricsInit(&metrics);
    metrics.userdata = script;
    metrics.measure = LayoutTextScriptMeasure;
    return metrics;
}

/**
 * A measurer whose answer comes from the **resolved CSS font size**.
 *
 * One px per code point per px of `font-size`, which is the shortest arithmetic that
 * makes a CSS declaration visible in a geometry: a `<p>` with `font-size: 10px` and two
 * code points in it comes out twenty px wide, and *nothing* but the cascade could have
 * put the ten there. A scripted source that ignored the style would make the pipeline
 * case a test of the seam rather than of the pipeline.
 */
static Result LayoutTextStyleMeasure(void *userdata,
                                              const LayoutTextRequest *request,
                                              LayoutTextRun *outRun)
{
    LayoutTextScript *script = (LayoutTextScript *)userdata;
    int32_t size;

    if (!script || !request || !outRun || !request->style) {
        return RoboInvalidArgument;
    }
    script->calls++;
    size = (int32_t)(request->style->fontSizePx + 0.5);
    outRun->source = request->textNode;
    outRun->range = request->range;
    outRun->direction = request->style->direction;
    outRun->codePoints = LayoutTextRangeLength(request->range);
    outRun->width = (int32_t)outRun->codePoints * size;
    outRun->height = size;
    outRun->ascent = size;
    outRun->descent = 0;
    return RoboOk;
}

static LayoutTextMetrics LayoutTextStyleMetrics(LayoutTextScript *script)
{
    LayoutTextMetrics metrics;

    LayoutTextMetricsInit(&metrics);
    metrics.userdata = script;
    metrics.measure = LayoutTextStyleMeasure;
    return metrics;
}

/* ------------------------------------------------------------------ */
/* A synthetic face, built here                                        */
/* ------------------------------------------------------------------ */

static void TextPutU8(uint8_t **p, uint8_t v) { *(*p)++ = v; }
static void TextPutU16(uint8_t **p, uint16_t v)
{
    *(*p)++ = (uint8_t)(v >> 8);
    *(*p)++ = (uint8_t)v;
}
static void TextPutI16(uint8_t **p, int16_t v) { TextPutU16(p, (uint16_t)v); }
static void TextPutU32(uint8_t **p, uint32_t v)
{
    *(*p)++ = (uint8_t)(v >> 24);
    *(*p)++ = (uint8_t)(v >> 16);
    *(*p)++ = (uint8_t)(v >> 8);
    *(*p)++ = (uint8_t)v;
}
static void TextPutI64(uint8_t **p, int64_t v)
{
    int i;

    for (i = 7; i >= 0; i--) {
        TextPutU8(p, (uint8_t)((uint64_t)v >> (i * 8)));
    }
}

typedef struct {
    uint32_t tag;
    const uint8_t *data;
    uint32_t length;
} TextSynthTable;

static void TextWriteSearchFields(uint8_t *hdr, uint16_t numTables)
{
    uint16_t pow2 = 1u;
    uint16_t entry = 0u;
    uint8_t *p;

    while ((uint16_t)(pow2 * 2u) <= numTables) {
        pow2 = (uint16_t)(pow2 * 2u);
        entry++;
    }
    p = hdr + 4;
    TextPutU16(&p, numTables);
    TextPutU16(&p, (uint16_t)(pow2 * 16u));
    TextPutU16(&p, entry);
    TextPutU16(&p, (uint16_t)(numTables * 16u - pow2 * 16u));
}

static size_t TextBuildSfnt(uint8_t *out,
                              size_t outCap,
                              uint32_t version,
                              const TextSynthTable *tables,
                              uint32_t n)
{
    size_t dirEnd = 12u + (size_t)n * 16u;
    size_t cursor;
    size_t need = dirEnd;
    uint32_t i;

    for (i = 0u; i < n; i++) {
        need += ((size_t)tables[i].length + 3u) & ~(size_t)3u;
    }
    if (need > outCap) {
        return 0u;
    }
    memset(out, 0, need);
    {
        uint8_t *p = out;

        TextPutU32(&p, version);
    }
    TextWriteSearchFields(out, (uint16_t)n);
    cursor = dirEnd;
    for (i = 0u; i < n; i++) {
        uint32_t pad = (uint32_t)(((size_t)tables[i].length + 3u) & ~(size_t)3u);
        uint8_t *p = out + 12u + (size_t)i * 16u;

        TextPutU32(&p, tables[i].tag);
        TextPutU32(&p, 0u);
        TextPutU32(&p, (uint32_t)cursor);
        TextPutU32(&p, tables[i].length);
        memcpy(out + cursor, tables[i].data, tables[i].length);
        cursor += pad;
    }
    return cursor;
}

static size_t TextBuildHead(uint8_t *b, uint16_t upem)
{
    uint8_t *p = b;

    TextPutU16(&p, 1);
    TextPutU16(&p, 0);
    TextPutU32(&p, 0x00010000u);
    TextPutU32(&p, 0u);
    TextPutU32(&p, RoboFontHeadMagic);
    TextPutU16(&p, 0u);
    TextPutU16(&p, upem);
    TextPutI64(&p, 0);
    TextPutI64(&p, 0);
    TextPutI16(&p, -50);
    TextPutI16(&p, -200);
    TextPutI16(&p, 1000);
    TextPutI16(&p, 800);
    TextPutU16(&p, 0u);
    TextPutU16(&p, 8u);
    TextPutI16(&p, 2);
    TextPutI16(&p, 1); /* long loca, so loca entries are u32 */
    TextPutI16(&p, 0);
    return (size_t)(p - b);
}

static size_t TextBuildHhea(uint8_t *b, uint16_t nHMetrics, int16_t asc, int16_t desc,
                              int16_t gap)
{
    uint8_t *p = b;
    int i;

    TextPutU16(&p, 1);
    TextPutU16(&p, 0);
    TextPutI16(&p, asc);
    TextPutI16(&p, desc);
    TextPutI16(&p, gap);
    TextPutU16(&p, 900);
    TextPutI16(&p, -10);
    TextPutI16(&p, -20);
    TextPutI16(&p, 880);
    TextPutI16(&p, 1);
    TextPutI16(&p, 0);
    TextPutI16(&p, 0);
    for (i = 0; i < 4; i++) {
        TextPutI16(&p, 0);
    }
    TextPutI16(&p, 0);
    TextPutU16(&p, nHMetrics);
    return (size_t)(p - b);
}

static size_t TextBuildMaxp(uint8_t *b, uint16_t numGlyphs)
{
    uint8_t *p = b;
    int i;

    TextPutU32(&p, 0x00010000u);
    TextPutU16(&p, numGlyphs);
    TextPutU16(&p, 8);
    TextPutU16(&p, 2);
    for (i = 0; i < 12; i++) {
        TextPutU16(&p, 0);
    }
    return (size_t)(p - b);
}

/** `n` full horizontal metrics: every glyph has its own advance. */
static size_t TextBuildHmtx(uint8_t *b, const uint16_t *advances, uint16_t n)
{
    uint8_t *p = b;
    uint16_t i;

    for (i = 0u; i < n; i++) {
        TextPutU16(&p, advances[i]);
        TextPutI16(&p, (int16_t)(i + 1));
    }
    return (size_t)(p - b);
}

static size_t TextBuildLoca(uint8_t *loca, uint16_t n)
{
    uint8_t *p = loca;
    uint16_t i;

    for (i = 0u; i <= n; i++) {
        TextPutU32(&p, 0u);
    }
    return (size_t)(p - loca);
}

/**
 * A one-subtable cmap holding a format-4 segment table.
 *
 * `first`/`last` are one contiguous run of code points mapped to glyph ids
 * `first_glyph` upward -- contiguous on purpose, because `idDelta` is the only
 * mapping a format-4 segment needs and a test fixture whose mapping needed a glyph
 * index array would be testing RoboFont's `idRangeOffset` rather than this module.
 * The required `0xFFFF` segment is appended, because a format-4 table without it is
 * not one.
 */
static size_t TextBuildCmap(uint8_t *b, uint16_t first, uint16_t last, int16_t delta)
{
    uint8_t *p = b;
    uint8_t *lengthPtr;
    uint8_t *subtable = b + 12;

    TextPutU16(&p, 0);  /* version */
    TextPutU16(&p, 1);  /* numTables */
    TextPutU16(&p, 3);  /* Windows */
    TextPutU16(&p, 1);  /* Unicode BMP */
    TextPutU32(&p, 12u); /* subtable offset */

    TextPutU16(&p, 4);  /* format */
    lengthPtr = p;
    TextPutU16(&p, 0);  /* length, patched below */
    TextPutU16(&p, 0);  /* language */
    TextPutU16(&p, 4);  /* segCountX2: two segments */
    TextPutU16(&p, 4);  /* searchRange */
    TextPutU16(&p, 1);  /* entrySelector */
    TextPutU16(&p, 0);  /* rangeShift */
    TextPutU16(&p, last);
    TextPutU16(&p, 0xFFFFu);
    TextPutU16(&p, 0);  /* reservedPad */
    TextPutU16(&p, first);
    TextPutU16(&p, 0xFFFFu);
    TextPutI16(&p, delta);
    TextPutI16(&p, 1);
    TextPutU16(&p, 0);
    TextPutU16(&p, 0);

    {
        uint16_t length = (uint16_t)(p - subtable);

        lengthPtr[0] = (uint8_t)(length >> 8);
        lengthPtr[1] = (uint8_t)length;
    }
    return (size_t)(p - b);
}

static size_t TextBuildPost(uint8_t *b)
{
    uint8_t *p = b;

    TextPutU32(&p, 0x00030000u);
    TextPutU32(&p, 0u);
    TextPutI16(&p, -125);
    TextPutI16(&p, 50);
    TextPutU32(&p, 0u);
    TextPutU32(&p, 0u);
    TextPutU32(&p, 0u);
    TextPutU32(&p, 0u);
    TextPutU32(&p, 0u);
    return (size_t)(p - b);
}

/*
 * The face, stated once as numbers so every case's expectation is arithmetic:
 *
 *   units per em   1000          so `font-size: 100px` makes 1 unit = 0.1px
 *   glyph 0        advance 500   `.notdef`, and what an unmapped code point measures at
 *   glyph 1        advance 600   'A'
 *   glyph 2        advance 300   'B'
 *   hhea           ascent 800, descent -200, gap 90
 *
 * so `A` is 60px, `B` is 30px and `AB` is 90px at 100px, and the line extent is
 * 800 + 200 + 90 = 1090 units = 109px. The numbers are chosen to be *unequal* and
 * unmistakable: a measurer that returned the font size, or the code-point count, or a
 * constant, would fail on the first of them.
 */
#define LAYOUT_TEXT_FACE_UPEM 1000u
#define LAYOUT_TEXT_GLYPH_NOTDEF_ADVANCE 500u
#define LAYOUT_TEXT_GLYPH_A_ADVANCE 600u
#define LAYOUT_TEXT_GLYPH_B_ADVANCE 300u
#define LAYOUT_TEXT_FACE_NUM_GLYPHS 3u
#define LAYOUT_TEXT_FACE_FONT_SIZE 100.0

/** What a face plus its source cost a caller, so a case can release both the way it
 *  built them. */
typedef struct layoutTextFace {
    FontSource *source;
    FontFace *face;
} LayoutTextFace;

static void LayoutTextFaceCreate(LayoutTextFace *out)
{
    uint8_t head[64], hhea[64], maxp[64], hmtx[64], loca[64], glyf[8], post[64];
    uint8_t cmap[64];
    uint8_t font[1024];
    static const uint16_t advances[LAYOUT_TEXT_FACE_NUM_GLYPHS] = {
        (uint16_t)LAYOUT_TEXT_GLYPH_NOTDEF_ADVANCE,
        (uint16_t)LAYOUT_TEXT_GLYPH_A_ADVANCE,
        (uint16_t)LAYOUT_TEXT_GLYPH_B_ADVANCE
    };
    TextSynthTable tables[8];
    uint32_t nt = 0u;
    size_t n;

    out->source = NULL;
    out->face = NULL;

    tables[nt].tag = RoboFontTagHead;
    tables[nt].data = head;
    tables[nt].length = (uint32_t)TextBuildHead(head, (uint16_t)LAYOUT_TEXT_FACE_UPEM);
    nt++;
    tables[nt].tag = RoboFontTagHhea;
    tables[nt].data = hhea;
    tables[nt].length = (uint32_t)TextBuildHhea(hhea, LAYOUT_TEXT_FACE_NUM_GLYPHS, 800, -200,
                                                  90);
    nt++;
    tables[nt].tag = RoboFontTagMaxp;
    tables[nt].data = maxp;
    tables[nt].length = (uint32_t)TextBuildMaxp(maxp, LAYOUT_TEXT_FACE_NUM_GLYPHS);
    nt++;
    tables[nt].tag = RoboFontTagHmtx;
    tables[nt].data = hmtx;
    tables[nt].length = (uint32_t)TextBuildHmtx(hmtx, advances, LAYOUT_TEXT_FACE_NUM_GLYPHS);
    nt++;
    tables[nt].tag = RoboFontTagCmap;
    tables[nt].data = cmap;
    /* 'A' (0x41) -> glyph 1, 'B' (0x42) -> glyph 2, one segment, delta 1 - 0x41. */
    tables[nt].length = (uint32_t)TextBuildCmap(cmap, 0x41u, 0x42u, (int16_t)(1 - 0x41));
    nt++;
    tables[nt].tag = RoboFontTagLoca;
    tables[nt].data = loca;
    tables[nt].length = (uint32_t)TextBuildLoca(loca, LAYOUT_TEXT_FACE_NUM_GLYPHS);
    nt++;
    tables[nt].tag = RoboFontTagGlyf;
    tables[nt].data = glyf;
    tables[nt].length = 0u; /* outlines are not what a measurement reads */
    nt++;
    tables[nt].tag = RoboFontTagPost;
    tables[nt].data = post;
    tables[nt].length = (uint32_t)TextBuildPost(post);
    nt++;

    n = TextBuildSfnt(font, sizeof(font), RoboFontSfntVersionTrue, tables, nt);
    RoboCheck(n > 0u);
    RoboCheckEqInt(FontSourceCreateMemoryCopy(font, n, &out->source), RoboOk);
    RoboCheckEqInt(FontFaceCreate(out->source, 0, &out->face), RoboOk);
}

static void LayoutTextFaceDestroy(LayoutTextFace *face)
{
    if (face->face) {
        FontFaceRelease(face->face);
        face->face = NULL;
    }
    if (face->source) {
        FontSourceRelease(face->source);
        face->source = NULL;
    }
}

/* ------------------------------------------------------------------ */
/* The fixture                                                         */
/* ------------------------------------------------------------------ */

typedef struct layoutTextRule {
    const char *tag;
    CssDisplay display;
} LayoutTextRule;

#define LAYOUT_TEXT_MAX_RULES 8u

/*
 * One rule of the fixture: a tag and the computed style the fixture declares for it. Named for the
 * *rule* rather than for the style, because `LayoutTextStyle` is the public resolved-record the
 * measurer takes (`RoboLayoutText.h`) -- this suite uses both, and the collision this typedef used to
 * cause is what stopped the file compiling at all.
 */
typedef struct layoutTextFixtureRule {
    char tag[16];
    CssComputedStyle *style; /* owned */
} LayoutTextFixtureRule;

typedef struct layoutTextFixture {
    LayoutDocument doc;
    LayoutTextFixtureRule rules[LAYOUT_TEXT_MAX_RULES];
    size_t ruleCount;
    LayoutStyleSource styles;
    LayoutTree *tree;
} LayoutTextFixture;

static const CssComputedStyle *LayoutTextStyleLookup(
    void *userdata,
    const HtmlNodeHandle *element)
{
    LayoutTextFixture *fixture = (LayoutTextFixture *)userdata;
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
    /* Nothing declared: the *initial* display, which is `inline`. Same rule the other
     * suites state -- a fixture's geometry depends on what the test wrote. */
    return NULL;
}

static void LayoutTextFixtureRuleAdd(LayoutTextFixture *fixture,
                                     const LayoutTextRule *rule)
{
    CssComputedStyle *style = NULL;
    CssComputedValue value = RoboCssCleared;
    CssValue specified = RoboCssCleared;
    size_t existing;

    LayoutFixture();
    RoboCheckEqInt(fixture->ruleCount < LAYOUT_TEXT_MAX_RULES, 1);
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
    strncpy(fixture->rules[fixture->ruleCount].tag, rule->tag,
            sizeof(fixture->rules[fixture->ruleCount].tag) - 1u);
    fixture->rules[fixture->ruleCount].tag[sizeof(fixture->rules[fixture->ruleCount].tag) -
                                            1u] = '\0';
    fixture->rules[fixture->ruleCount].style = style;
    fixture->ruleCount++;
}

static void LayoutTextBegin(LayoutTextFixture *fixture,
                             const char *html,
                             const LayoutTextRule *rules,
                             size_t count)
{
    LayoutBuildOptions options;
    size_t index;

    memset(fixture, 0, sizeof(*fixture));
    LayoutDocumentBuild(&fixture->doc, html);
    for (index = 0u; index < count; index++) {
        LayoutTextFixtureRuleAdd(fixture, &rules[index]);
    }
    LayoutStyleSourceInit(&fixture->styles);
    fixture->styles.userdata = fixture;
    fixture->styles.ComputedStyleFor = LayoutTextStyleLookup;
    RoboCheckEqInt(LayoutTreeCreate(&fixture->tree, NULL), RoboOk);
    LayoutBuildOptionsInit(&options);
    options.styles = &fixture->styles;
    RoboCheckEqInt(LayoutTreeBuildFromDocument(fixture->tree, fixture->doc.id,
                                                          &options, NULL, NULL),
                      RoboOk);
}

static void LayoutTextEnd(LayoutTextFixture *fixture)
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
static void LayoutTextPass(LayoutTextFixture *fixture,
                            int32_t viewportWidth,
                            const LayoutTextMetrics *metrics,
                            LayoutStats *outStats,
                            Result *outStatus)
{
    LayoutContext *context = NULL;
    LayoutSize viewport;
    LayoutStats stats;
    Result status;

    viewport.width = viewportWidth;
    viewport.height = 600;
    RoboCheckEqInt(LayoutContextCreate(&context, NULL), RoboOk);
    RoboCheckEqInt(LayoutContextSetViewport(context, viewport, NULL), RoboOk);
    if (metrics) {
        RoboCheckEqInt(LayoutContextSetTextMetrics(context, metrics), RoboOk);
    }
    status = LayoutContextRun(context, fixture->tree, NULL);
    LayoutContextStats(context, &stats);
    LayoutContextDestroy(context);
    if (outStats) {
        *outStats = stats;
    }
    if (outStatus) {
        *outStatus = status;
    }
}

/** A viewport wider than any fixture's content, so a case only wraps on purpose. */
#define LAYOUT_TEXT_WIDE_VIEWPORT 800

/** The block container every fixture below shares: `<html><body><p>`. */
#define LAYOUT_TEXT_BLOCK_RULES                         \
    { "html", RoboCssDisplayBlock },                 \
    { "body", RoboCssDisplayBlock },                 \
    { "p", RoboCssDisplayBlock }

/** The box built for the first element named @p tag. */
static LayoutNodeId LayoutTextBoxOf(LayoutTextFixture *fixture,
                                               const char *tag)
{
    HtmlNodeHandle element = LayoutDocumentFind(&fixture->doc, tag);

    RoboCheckEqInt(HtmlNodeHandleIsWellformed(&element), 1);
    return LayoutObjectIdForDomNode(fixture->tree, &element);
}

/**
 * The first **text node** inside the first element named @p tag.
 *
 * The DOM's own walk rather than a target format, because the question a case asks is
 * "which text node is under this element", and the parser decides that.
 */
static HtmlNodeHandle LayoutTextFirstTextChild(
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

static HtmlNodeHandle LayoutTextNodeOf(LayoutTextFixture *fixture,
                                                   const char *tag)
{
    HtmlNodeHandle element = LayoutDocumentFind(&fixture->doc, tag);

    return LayoutTextFirstTextChild(&element);
}

/** The layout object a text node became. */
static LayoutNodeId LayoutTextObjectOf(LayoutTextFixture *fixture,
                                                   const char *tag)
{
    HtmlNodeHandle node = LayoutTextNodeOf(fixture, tag);

    RoboCheckEqInt(HtmlNodeHandleIsWellformed(&node), 1);
    return LayoutObjectIdForDomNode(fixture->tree, &node);
}

/** Assert one object's geometry, and that the geometry is *known*. */
static void LayoutTextExpectIn(const LayoutTree *tree,
                                  LayoutNodeId id,
                                  int32_t x,
                                  int32_t y,
                                  int32_t width,
                                  int32_t height)
{
    LayoutResult result;
    LayoutPoint position;
    LayoutSize size;

    RoboCheckEqInt(LayoutObjectResult(tree, id, &result), RoboOk);
    RoboCheckEqInt((int)result.state, (int)RoboLayoutResultComputed);
    RoboCheckEqInt(LayoutResultPosition(&result, &position), RoboOk);
    RoboCheckEqInt(LayoutResultSize(&result, &size), RoboOk);
    RoboCheckEqInt(position.x, x);
    RoboCheckEqInt(position.y, y);
    RoboCheckEqInt(size.width, width);
    RoboCheckEqInt(size.height, height);
}

static void LayoutTextExpect(LayoutTextFixture *fixture,
                              LayoutNodeId id,
                              int32_t x,
                              int32_t y,
                              int32_t width,
                              int32_t height)
{
    LayoutTextExpectIn(fixture->tree, id, x, y, width, height);
}

static void LayoutTextExpectUnresolved(LayoutTextFixture *fixture,
                                          LayoutNodeId id)
{
    LayoutResult result;

    RoboCheckEqInt(LayoutObjectResult(fixture->tree, id, &result), RoboOk);
    RoboCheckEqInt((int)result.state, (int)RoboLayoutResultUnresolved);
}

/** A request built by hand, for the cases that drive the seam directly. */
static LayoutTextRequest GetRoboLayoutTextRequest(const HtmlNodeHandle *node,
                                                      LayoutTextRange range,
                                                      const LayoutTextStyle *style)
{
    LayoutTextRequest request;

    memset(&request, 0, sizeof(request));
    request.textNode = *node;
    request.range = range;
    request.style = style;
    return request;
}

/* ------------------------------------------------------------------ */
/* The model                                                           */
/* ------------------------------------------------------------------ */

static void TestTextRange(void)
{
    LayoutTextRange range;

    range = LayoutTextRangeAt(3u);
    RoboCheckEqInt(range.start, 3u);
    RoboCheckEqInt(range.end, 3u);
    RoboCheckEqInt(LayoutTextRangeIsEmpty(range), 1);
    RoboCheckEqInt(LayoutTextRangeLength(range), 0u);
    /* An empty range is a *position*, and a position is not inverted: the distinction
     * is what makes a caret addressable. */
    RoboCheckEqInt(LayoutTextRangeIsInverted(range), 0);
    RoboCheckEqInt(LayoutTextRangeIsValid(range, 3u), 1);

    range = LayoutTextRangeFull(4u);
    RoboCheckEqInt(range.start, 0u);
    RoboCheckEqInt(range.end, 4u);
    RoboCheckEqInt(LayoutTextRangeLength(range), 4u);
    RoboCheckEqInt(LayoutTextRangeIsEmpty(range), 0);
    RoboCheckEqInt(LayoutTextRangeIsValid(range, 4u), 1);
    /* One past the last code point is not inside the text. */
    RoboCheckEqInt(LayoutTextRangeIsValid(range, 3u), 0);
    RoboCheckEqInt(LayoutTextRangeContains(range, 0u), 1);
    RoboCheckEqInt(LayoutTextRangeContains(range, 3u), 1);
    RoboCheckEqInt(LayoutTextRangeContains(range, 4u), 0);
    RoboCheckEqInt(LayoutTextRangeContains(range, 99u), 0);

    /* An inverted range is not a span of anything, however long the text is. */
    range.start = 5u;
    range.end = 2u;
    RoboCheckEqInt(LayoutTextRangeIsInverted(range), 1);
    RoboCheckEqInt(LayoutTextRangeIsEmpty(range), 1);
    RoboCheckEqInt(LayoutTextRangeLength(range), 0u);
    RoboCheckEqInt(LayoutTextRangeIsValid(range, 100u), 0);
}

static void TestTextDirection(void)
{
    RoboCheckEqInt(LayoutTextDirectionIsKnown(RoboLayoutTextDirectionLtr), 1);
    RoboCheckEqInt(LayoutTextDirectionIsKnown(RoboLayoutTextDirectionRtl), 1);
    RoboCheckEqInt(LayoutTextDirectionIsKnown((LayoutTextDirection)7), 0);
    RoboCheck(strcmp(LayoutTextDirectionName(RoboLayoutTextDirectionLtr), "ltr") ==
               0);
    RoboCheck(strcmp(LayoutTextDirectionName(RoboLayoutTextDirectionRtl), "rtl") ==
               0);
    RoboCheck(strcmp(LayoutTextDirectionName((LayoutTextDirection)7),
                      "unknown") == 0);
    RoboCheckEqInt((int)RoboLayoutTextDirectionCount, 2);
}

static void TestTextStyleInitial(void)
{
    LayoutTextStyle style;

    LayoutTextStyleInit(NULL); /* documented as safe */
    LayoutTextStyleInit(&style);
    RoboCheck(style.fontSizePx == RoboLayoutTextInitialFontSizePx);
    RoboCheckEqInt(style.fontWeight, RoboLayoutTextWeightNormal);
    RoboCheckEqInt(style.italic, 0);
    RoboCheckEqInt((int)style.direction, (int)RoboLayoutTextDirectionLtr);

    /* Nothing computed at all is not a failure: it is the property's own initial
     * value, exactly as a style-less element computes `display: inline`. */
    RoboCheckEqInt(LayoutTextStyleResolve(NULL, &style), RoboOk);
    RoboCheck(style.fontSizePx == RoboLayoutTextInitialFontSizePx);
    RoboCheckEqInt(style.fontWeight, RoboLayoutTextWeightNormal);
    RoboCheckEqInt(style.italic, 0);
    RoboCheckEqInt((int)style.direction, (int)RoboLayoutTextDirectionLtr);
    RoboCheckEqInt(LayoutTextStyleResolve(NULL, NULL), RoboInvalidArgument);
}

/** One style built from real computed values, through 9.8's own makers. */
static void LayoutTextStyleSetLength(CssComputedStyle *style,
                                         const char *property,
                                         double number,
                                         CssUnit unit)
{
    CssComputedValue value = RoboCssCleared;
    CssValue specified = RoboCssCleared;

    RoboCheckEqInt(CssComputedValueMakeLength(&value, number, unit, 0, NULL),
                      RoboOk);
    RoboCheckEqInt(CssComputedStyleSetEntry(style, RoboCssPropertyIdNone,
                                                       CssSliceFromText(property), 0u,
                                                       &specified, &value, NULL),
                      RoboOk);
}

static void LayoutTextStyleSetKeyword(CssComputedStyle *style,
                                          const char *property,
                                          const char *keyword)
{
    CssComputedValue value = RoboCssCleared;
    CssValue specified = RoboCssCleared;

    RoboCheckEqInt(CssComputedValueMakeKeyword(&value,
                                                          CssSliceFromText(keyword),
                                                          NULL),
                      RoboOk);
    RoboCheckEqInt(CssComputedStyleSetEntry(style, RoboCssPropertyIdNone,
                                                       CssSliceFromText(property), 0u,
                                                       &specified, &value, NULL),
                      RoboOk);
}

static void LayoutTextStyleSetNumber(CssComputedStyle *style,
                                         const char *property,
                                         double number)
{
    CssComputedValue value = RoboCssCleared;
    CssValue specified = RoboCssCleared;

    RoboCheckEqInt(CssComputedValueMakeNumber(&value, number, 1, NULL), RoboOk);
    RoboCheckEqInt(CssComputedStyleSetEntry(style, RoboCssPropertyIdNone,
                                                       CssSliceFromText(property), 0u,
                                                       &specified, &value, NULL),
                      RoboOk);
}

/**
 * Four properties, read from a real computed style.
 *
 * The point of the case is that each of the four *forms* RoboCss can produce is
 * consumed: a LENGTH in px, a NUMBER (the numeric font weight), and two KEYWORDs
 * (`font-style`, `direction`). Anything the build cannot read takes the property's
 * initial value, and the two lines that assert `50%` and `2em` doing exactly that are
 * the ones that matter: converting either without a parent font or a containing block
 * would be inventing a number.
 */
static void TestTextStyleResolve(void)
{
    CssComputedStyle *style = NULL;
    LayoutTextStyle resolved;

    LayoutFixture();
    RoboCheckEqInt(CssComputedStyleCreate(&style, NULL), RoboOk);

    /* Defaults for an empty style: every property's initial value. */
    RoboCheckEqInt(LayoutTextStyleResolve(style, &resolved), RoboOk);
    RoboCheck(style && resolved.fontSizePx == RoboLayoutTextInitialFontSizePx);
    RoboCheckEqInt(resolved.fontWeight, RoboLayoutTextWeightNormal);
    RoboCheckEqInt(resolved.italic, 0);

    LayoutTextStyleSetLength(style, "font-size", 24.0, RoboCssUnitPx);
    LayoutTextStyleSetNumber(style, "font-weight", 600.0);
    LayoutTextStyleSetKeyword(style, "font-style", "italic");
    LayoutTextStyleSetKeyword(style, "direction", "rtl");
    RoboCheckEqInt(LayoutTextStyleResolve(style, &resolved), RoboOk);
    RoboCheck(resolved.fontSizePx == 24.0);
    RoboCheckEqInt(resolved.fontWeight, 600);
    RoboCheckEqInt(resolved.italic, 1);
    RoboCheckEqInt((int)resolved.direction, (int)RoboLayoutTextDirectionRtl);

    /* `oblique` is the same flag: CSS has two ways to say "not upright". */
    LayoutTextStyleSetKeyword(style, "font-style", "oblique");
    RoboCheckEqInt(LayoutTextStyleResolve(style, &resolved), RoboOk);
    RoboCheckEqInt(resolved.italic, 1);
    LayoutTextStyleSetKeyword(style, "font-style", "normal");
    RoboCheckEqInt(LayoutTextStyleResolve(style, &resolved), RoboOk);
    RoboCheckEqInt(resolved.italic, 0);

    /* The two keyword weights CSS defines, and the two relative ones resolved against
     * the initial weight because this record has no parent to be relative to. */
    LayoutTextStyleSetKeyword(style, "font-weight", "bold");
    RoboCheckEqInt(LayoutTextStyleResolve(style, &resolved), RoboOk);
    RoboCheckEqInt(resolved.fontWeight, RoboLayoutTextWeightBold);
    LayoutTextStyleSetKeyword(style, "font-weight", "normal");
    RoboCheckEqInt(LayoutTextStyleResolve(style, &resolved), RoboOk);
    RoboCheckEqInt(resolved.fontWeight, RoboLayoutTextWeightNormal);

    /* A weight the font could not have is clamped into CSS's own range rather than
     * passed through: `font-weight: 5000` must not ask a face for a 5000 face. */
    LayoutTextStyleSetNumber(style, "font-weight", 5000.0);
    RoboCheckEqInt(LayoutTextStyleResolve(style, &resolved), RoboOk);
    RoboCheckEqInt(resolved.fontWeight, 1000);
    LayoutTextStyleSetNumber(style, "font-weight", 0.0);
    RoboCheckEqInt(LayoutTextStyleResolve(style, &resolved), RoboOk);
    RoboCheckEqInt(resolved.fontWeight, 1);

    /* A relative unit and a percentage are not resolvable here, and take the initial
     * value -- which is a CSS fact and not a fallback. */
    LayoutTextStyleSetLength(style, "font-size", 2.0, RoboCssUnitEm);
    RoboCheckEqInt(LayoutTextStyleResolve(style, &resolved), RoboOk);
    RoboCheck(resolved.fontSizePx == RoboLayoutTextInitialFontSizePx);
    {
        CssComputedValue value = RoboCssCleared;
        CssValue specified = RoboCssCleared;

        RoboCheckEqInt(CssComputedValueMakePercentage(&value, 50.0, 0, NULL),
                          RoboOk);
        RoboCheckEqInt(CssComputedStyleSetEntry(style, RoboCssPropertyIdNone,
                                                           CssSliceFromText("font-size"),
                                                           0u, &specified, &value, NULL),
                          RoboOk);
    }
    RoboCheckEqInt(LayoutTextStyleResolve(style, &resolved), RoboOk);
    RoboCheck(resolved.fontSizePx == RoboLayoutTextInitialFontSizePx);

    /* An absolute unit that RoboCss normalises to px *is* usable, which is 9.8's rule
     * showing up in a font size. */
    LayoutTextStyleSetLength(style, "font-size", 1.0, RoboCssUnitIn);
    RoboCheckEqInt(LayoutTextStyleResolve(style, &resolved), RoboOk);
    RoboCheck(resolved.fontSizePx > 0.0);

    CssComputedStyleDestroy(style);
}

static void TestTextRunAndMetricsRecord(void)
{
    LayoutTextRun run;
    LayoutTextMetrics metrics;

    LayoutTextRunInit(&run);
    RoboCheckEqInt(HtmlNodeHandleIsWellformed(&run.source), 0);
    RoboCheckEqInt(LayoutTextRangeIsEmpty(run.range), 1);
    RoboCheckEqInt((int)run.direction, (int)RoboLayoutTextDirectionLtr);
    RoboCheckEqInt(run.width, 0);
    RoboCheckEqInt(run.height, 0);
    RoboCheckEqInt(run.ascent, 0);
    RoboCheckEqInt(run.descent, 0);
    RoboCheckEqInt(run.codePoints, 0u);
    RoboCheckEqInt(run.glyphsMissing, 0u);
    LayoutTextRunInit(NULL); /* documented as safe */

    LayoutTextMetricsInit(&metrics);
    RoboCheckEqInt(LayoutTextMetricsIsUsable(&metrics), 0);
    RoboCheckEqInt(LayoutTextMetricsIsUsable(NULL), 0);
    metrics.userdata = NULL;
    metrics.measure = LayoutTextScriptMeasure;
    RoboCheckEqInt(LayoutTextMetricsIsUsable(&metrics), 1);
    LayoutTextMetricsInit(NULL); /* documented as safe */
}

/** The text-side stats record, which is where "did anything measure?" lives. */
static void TestTextStatsRecord(void)
{
    LayoutTextStats a, b;

    LayoutTextStatsInit(&a);
    LayoutTextStatsInit(&b);
    RoboCheckEqInt(LayoutTextStatsEqual(&a, &b), 1);
    RoboCheckEqInt(a.runsPlaced, 0u);
    RoboCheckEqInt(a.measured, 0u);
    RoboCheckEqInt(a.unmeasured, 0u);
    RoboCheckEqInt(a.empty, 0u);
    RoboCheckEqInt(a.codePoints, 0u);
    RoboCheckEqInt(a.glyphsMissing, 0u);
    RoboCheckEqInt(a.rtl, 0u);
    RoboCheckEqInt(a.widthMax, 0);
    RoboCheckEqInt(a.heightMax, 0);
    RoboCheckEqInt(a.depthMax, 0u);

    a.widthMax = 10;
    RoboCheckEqInt(LayoutTextStatsEqual(&a, &b), 0);
    b.widthMax = 10;
    RoboCheckEqInt(LayoutTextStatsEqual(&a, &b), 1);
    /* Every field counts, and the two that are easy to forget are the two that carry
     * the phase's honesty: an unmeasured run's count and a glyph the font lacked. */
    b.unmeasured = 1u;
    RoboCheckEqInt(LayoutTextStatsEqual(&a, &b), 0);
    b.unmeasured = 0u;
    b.glyphsMissing = 1u;
    RoboCheckEqInt(LayoutTextStatsEqual(&a, &b), 0);
    b.glyphsMissing = 0u;
    b.rtl = 1u;
    RoboCheckEqInt(LayoutTextStatsEqual(&a, &b), 0);
    b.rtl = 0u;
    b.depthMax = 3u;
    RoboCheckEqInt(LayoutTextStatsEqual(&a, &b), 0);

    RoboCheckEqInt(LayoutTextStatsEqual(NULL, NULL), 1);
    RoboCheckEqInt(LayoutTextStatsEqual(&a, NULL), 0);
    RoboCheckEqInt(LayoutTextStatsEqual(NULL, &a), 0);
    LayoutTextStatsInit(NULL); /* documented as safe */
}

/* ------------------------------------------------------------------ */
/* Reading text                                                        */
/* ------------------------------------------------------------------ */

/**
 * Code points, not bytes.
 *
 * The three strings here are the three ways a byte offset and a character position
 * come apart: an accented Latin letter (two bytes, one code point), Arabic (two bytes
 * per letter, one code point each, and *right to left*), and an emoji (four bytes, one
 * code point). A build that counted bytes would answer 6, 8 and 4.
 */
static void TestTextCodePoints(void)
{
    LayoutTextFixture fixture;
    static const LayoutTextRule rules[] = { LAYOUT_TEXT_BLOCK_RULES };
    HtmlNodeHandle node;
    uint32_t count = 0u;

    LayoutFixture();
    LayoutTextBegin(&fixture, "<html><body><p>h\xC3\xA9llo \xD8\xB3\xD9\x84\xD8\xA7\xD9\x85"
                                " \xF0\x9F\x91\x8B</p></body></html>",
                      rules, 3u);

    node = LayoutTextNodeOf(&fixture, "p");
    RoboCheckEqInt(HtmlNodeHandleIsWellformed(&node), 1);
    /* "héllo سلام 👋" is 5 + 1 + 4 + 1 + 1 code points. */
    RoboCheckEqInt(LayoutTextNodeCodePoints(&node, &count, NULL), RoboOk);
    RoboCheckEqInt(count, 12u);

    /* A one-byte text node answers one, so the count is not accidentally constant. */
    LayoutTextEnd(&fixture);
    LayoutTextBegin(&fixture, "<html><body><p>A</p></body></html>", rules, 3u);
    node = LayoutTextNodeOf(&fixture, "p");
    RoboCheckEqInt(LayoutTextNodeCodePoints(&node, &count, NULL), RoboOk);
    RoboCheckEqInt(count, 1u);

    /* A node handle that names an element is refused rather than read: an element has
     * no text payload, and answering zero would look like an empty text node. */
    {
        HtmlNodeHandle element = LayoutDocumentFind(&fixture.doc, "p");

        RoboCheckEqInt(LayoutTextNodeCodePoints(&element, &count, NULL),
                          RoboInvalidArgument);
    }
    RoboCheckEqInt(LayoutTextNodeCodePoints(&node, NULL, NULL),
                      RoboInvalidArgument);
    node = LayoutTextNodeOf(&fixture, "nosuch");
    RoboCheckEqInt(HtmlNodeHandleIsWellformed(&node), 0);
    RoboCheckEqInt(LayoutTextNodeCodePoints(&node, &count, NULL),
                      RoboInvalidArgument);

    LayoutTextEnd(&fixture);
}

/**
 * The byte span a code-point range denotes.
 *
 * The mirror of the case above, and the one that proves the *bridge* is exact: for
 * "héllo", asking for code points 1..2 must answer byte 1 length 2 -- the `é` alone --
 * and asking for the whole string must answer 6 bytes for 5 code points. Both
 * endpoints have to land on boundaries, and the two refusal cases are the reason the
 * implementation walks rather than counts.
 */
static void TestTextRangeBytes(void)
{
    LayoutTextFixture fixture;
    static const LayoutTextRule rules[] = { LAYOUT_TEXT_BLOCK_RULES };
    HtmlNodeHandle node;
    size_t start = 0u, length = 0u;

    LayoutFixture();
    LayoutTextBegin(&fixture, "<html><body><p>h\xC3\xA9llo</p></body></html>", rules, 3u);
    node = LayoutTextNodeOf(&fixture, "p");
    RoboCheckEqInt(LayoutTextNodeCodePoints(&node, NULL, NULL),
                      RoboInvalidArgument);

    /* The whole run: 5 code points, 6 bytes. */
    RoboCheckEqInt(LayoutTextRangeBytes(&node, LayoutTextRangeFull(5u),
                                                  &start, &length, NULL),
                      RoboOk);
    RoboCheckEqInt((int)start, 0);
    RoboCheckEqInt((int)length, 6);

    /* The `é` alone. */
    {
        LayoutTextRange range;

        range.start = 1u;
        range.end = 2u;
        RoboCheckEqInt(LayoutTextRangeBytes(&node, range, &start, &length, NULL),
                          RoboOk);
        RoboCheckEqInt((int)start, 1);
        RoboCheckEqInt((int)length, 2);
    }

    /* `llo` -- two bytes of accented letter skipped, then three plain ones. */
    {
        LayoutTextRange range;

        range.start = 2u;
        range.end = 5u;
        RoboCheckEqInt(LayoutTextRangeBytes(&node, range, &start, &length, NULL),
                          RoboOk);
        RoboCheckEqInt((int)start, 3);
        RoboCheckEqInt((int)length, 3);
    }

    /* An empty range is a caret: a zero-length span at the right byte offset. */
    {
        LayoutTextRange caret = LayoutTextRangeAt(2u);

        RoboCheckEqInt(LayoutTextRangeBytes(&node, caret, &start, &length, NULL),
                          RoboOk);
        RoboCheckEqInt((int)start, 3);
        RoboCheckEqInt((int)length, 0);
    }

    /* Past the end, beyond what the node holds, and inverted: all three are refused --
     * and the two outs are cleared by the refusal rather than left holding the last
     * successful answer. */
    RoboCheckEqInt(LayoutTextRangeBytes(&node, LayoutTextRangeFull(6u),
                                                  &start, &length, NULL),
                      RoboInvalidArgument);
    RoboCheckEqInt((int)start, 0);
    RoboCheckEqInt((int)length, 0);
    {
        LayoutTextRange range;

        range.start = 4u;
        range.end = 2u;
        RoboCheckEqInt(LayoutTextRangeBytes(&node, range, &start, &length, NULL),
                          RoboInvalidArgument);
    }
    RoboCheckEqInt(LayoutTextRangeBytes(&node, LayoutTextRangeFull(5u),
                                                  NULL, &length, NULL),
                      RoboInvalidArgument);
    RoboCheckEqInt(LayoutTextRangeBytes(&node, LayoutTextRangeFull(5u),
                                                  &start, NULL, NULL),
                      RoboInvalidArgument);

    LayoutTextEnd(&fixture);

    /* A four-byte emoji, so the bridge is asserted at every UTF-8 width and not only at
     * the two-byte one. */
    LayoutTextBegin(&fixture, "<html><body><p>\xF0\x9F\x91\x8B</p></body></html>", rules,
                      3u);
    node = LayoutTextNodeOf(&fixture, "p");
    RoboCheckEqInt(LayoutTextRangeBytes(&node, LayoutTextRangeFull(1u),
                                                  &start, &length, NULL),
                      RoboOk);
    RoboCheckEqInt((int)start, 0);
    RoboCheckEqInt((int)length, 4);
    LayoutTextEnd(&fixture);
}

static void TestTextWhitespace(void)
{
    RoboCheckEqInt(LayoutTextIsCollapsibleWhitespace("", 0u), 1);
    RoboCheckEqInt(LayoutTextIsCollapsibleWhitespace(NULL, 0u), 1);
    RoboCheckEqInt(LayoutTextIsCollapsibleWhitespace(" \t\n\f\r", 5u), 1);
    RoboCheckEqInt(LayoutTextIsCollapsibleWhitespace(" \tA ", 4u), 0);
    /* CSS's own five, and *only* those five: a non-breaking space is content. */
    RoboCheckEqInt(LayoutTextIsCollapsibleWhitespace("\xC2\xA0", 2u), 0);
    /* A prefix that is whitespace is not the whole of it. */
    RoboCheckEqInt(LayoutTextIsCollapsibleWhitespace("   x", 4u), 0);
}

/* ------------------------------------------------------------------ */
/* The seam                                                            */
/* ------------------------------------------------------------------ */

/**
 * The seam's four answers, which is the whole of its contract.
 *
 *   no source         a zero run with the source and range echoed, and RoboOk --
 *                     the documented degradation, and what keeps a build with no face
 *                     laying a document out exactly as 10.4 did;
 *   a source that     the same, because "no callback" is not a failure either;
 *     answers nothing
 *   a refusal         RoboInvalidArgument, always *before* the source is consulted,
 *                     so a caller's bug is not reported as the measurer's;
 *   a failure         whatever the callback returned, with the run cleared first so a
 *                     failure never leaves a half-filled record.
 */
static void TestTextSeam(void)
{
    LayoutTextFixture fixture;
    static const LayoutTextRule rules[] = { LAYOUT_TEXT_BLOCK_RULES };
    HtmlNodeHandle node;
    LayoutTextRequest request;
    LayoutTextStyle style;
    LayoutTextRun run;
    LayoutTextScript script;
    LayoutTextMetrics metrics;
    LayoutTextMetrics empty;

    LayoutFixture();
    LayoutTextBegin(&fixture, "<html><body><p>AB</p></body></html>", rules, 3u);
    node = LayoutTextNodeOf(&fixture, "p");
    LayoutTextStyleInit(&style);
    request = GetRoboLayoutTextRequest(&node, LayoutTextRangeFull(2u), &style);

    memset(&script, 0, sizeof(script));
    metrics = LayoutTextScriptMetrics(&script);
    LayoutTextMetricsInit(&empty);

    /* No source at all. */
    RoboCheckEqInt(LayoutTextMeasure(NULL, &request, &run, NULL), RoboOk);
    RoboCheckEqInt(run.width, 0);
    RoboCheckEqInt(run.height, 0);
    RoboCheckEqInt(run.codePoints, 0u);
    RoboCheckEqInt(LayoutTextRangeLength(run.range), 2u);
    RoboCheckEqInt(HtmlNodeHandleEqual(&run.source, &node), 1);
    RoboCheckEqInt(script.calls, 0u);

    /* A source whose callback is NULL: the same answer, and the callback still not
     * called -- which is the distinction the pass counts as *unmeasured*. */
    RoboCheckEqInt(LayoutTextMeasure(&empty, &request, &run, NULL), RoboOk);
    RoboCheckEqInt(run.width, 0);
    RoboCheckEqInt(script.calls, 0u);

    /* A source that answers. */
    script.perCodePoint = 7;
    script.height = 11;
    script.ascent = 9;
    script.descent = 2;
    RoboCheckEqInt(LayoutTextMeasure(&metrics, &request, &run, NULL), RoboOk);
    RoboCheckEqInt(script.calls, 1u);
    RoboCheckEqInt(run.width, 14);
    RoboCheckEqInt(run.height, 11);
    RoboCheckEqInt(run.ascent, 9);
    RoboCheckEqInt(run.descent, 2);
    RoboCheckEqInt(run.codePoints, 2u);

    /* Refusals, each of which must leave the run cleared rather than half-filled. */
    RoboCheckEqInt(LayoutTextMeasure(&metrics, NULL, &run, NULL),
                      RoboInvalidArgument);
    RoboCheckEqInt(run.width, 0);
    RoboCheckEqInt(LayoutTextMeasure(&metrics, &request, NULL, NULL),
                      RoboInvalidArgument);
    RoboCheckEqInt(LayoutTextMeasure(NULL, NULL, &run, NULL),
                      RoboInvalidArgument);
    {
        HtmlNodeHandle unformed;
        LayoutTextRequest bad;

        HtmlNodeHandleClear(&unformed);
        bad = GetRoboLayoutTextRequest(&unformed, LayoutTextRangeFull(0u), &style);
        RoboCheckEqInt(LayoutTextMeasure(&metrics, &bad, &run, NULL),
                          RoboInvalidArgument);
        bad = GetRoboLayoutTextRequest(&node, LayoutTextRangeFull(2u), NULL);
        RoboCheckEqInt(LayoutTextMeasure(&metrics, &bad, &run, NULL),
                          RoboInvalidArgument);
        RoboCheckEqInt(script.calls, 1u);
    }

    /* A failure from the source travels, and the run is cleared. */
    script.fail = 1;
    RoboCheckEqInt(LayoutTextMeasure(&metrics, &request, &run, NULL), RoboError);
    RoboCheckEqInt(run.width, 0);
    RoboCheckEqInt(script.calls, 2u);

    LayoutTextEnd(&fixture);
}

/** `measure_node`: the whole node, the owner's style, the initial values when there is
 *  none. */
static void TestTextMeasureNode(void)
{
    LayoutTextFixture fixture;
    static const LayoutTextRule rules[] = { LAYOUT_TEXT_BLOCK_RULES };
    HtmlNodeHandle node;
    LayoutTextRun run;
    LayoutTextScript script;
    LayoutTextMetrics metrics;

    LayoutFixture();
    LayoutTextBegin(&fixture, "<html><body><p>AB</p></body></html>", rules, 3u);
    node = LayoutTextNodeOf(&fixture, "p");

    memset(&script, 0, sizeof(script));
    script.perCodePoint = 5;
    script.height = 20;
    metrics = LayoutTextScriptMetrics(&script);

    /* The *range* is built from the node's code points, so it is the whole node. */
    RoboCheckEqInt(LayoutTextMeasureNode(&node, NULL, &metrics, &run, NULL),
                      RoboOk);
    RoboCheckEqInt(run.range.start, 0u);
    RoboCheckEqInt(run.range.end, 2u);
    RoboCheckEqInt(run.width, 10);
    RoboCheckEqInt(run.codePoints, 2u);
    /* A NULL owner style is the initial font inputs, not a failure -- and this is
     * where a case can assert the measurer saw them. */
    RoboCheckEqInt((int)script.calls, 1);

    RoboCheckEqInt(LayoutTextMeasureNode(&node, NULL, NULL, &run, NULL), RoboOk);
    RoboCheckEqInt(run.width, 0);
    RoboCheckEqInt(run.range.end, 2u);
    RoboCheckEqInt(run.codePoints, 0u);

    RoboCheckEqInt(LayoutTextMeasureNode(NULL, NULL, &metrics, &run, NULL),
                      RoboInvalidArgument);
    RoboCheckEqInt(LayoutTextMeasureNode(&node, NULL, &metrics, NULL, NULL),
                      RoboInvalidArgument);
    {
        HtmlNodeHandle element = LayoutDocumentFind(&fixture.doc, "p");

        RoboCheckEqInt(LayoutTextMeasureNode(&element, NULL, &metrics, &run, NULL),
                          RoboInvalidArgument);
    }

    LayoutTextEnd(&fixture);
}

/* ------------------------------------------------------------------ */
/* The builder                                                         */
/* ------------------------------------------------------------------ */

/**
 * A text node generates a TEXT object, and the object is not a box.
 *
 * Four facts, and each is a thing a later phase reads: the kind, the absence of
 * children, the *borrowed parent style* (which is what makes inherited properties
 * reach a run), and the derived `inline` display (which is what makes it a participant
 * of a line without either pass special-casing it). The report's two numbers are
 * asserted beside them, because the whitespace rule is the half of this rule that is
 * easy to get wrong in the direction that leaves a run of spaces between elements.
 */
static void TestTextBuild(void)
{
    LayoutTextFixture fixture;
    static const LayoutTextRule rules[] = { LAYOUT_TEXT_BLOCK_RULES };
    LayoutBuildOptions options;
    LayoutBuildReport report;
    LayoutTree *tree = NULL;
    LayoutNodeId textBox;
    LayoutNodeId pBox;
    HtmlNodeHandle textNode;
    HtmlNodeHandle pElement;
    HtmlNodeHandle reported;
    LayoutObjectKind kind;

    LayoutFixture();
    /*
     * This case builds through the *report*, so it drives the builder itself rather
     * than `LayoutTextBegin` -- which means it also owns the fixture's zeroing.
     */
    memset(&fixture, 0, sizeof(fixture));
    /* One text node with content, and the whitespace the indentation produces. */
    LayoutDocumentBuild(&fixture.doc,
                          "<html>\n"
                          "  <body>\n"
                          "    <p>Hello</p>\n"
                          "  </body>\n"
                          "</html>\n");
    LayoutTextFixtureRuleAdd(&fixture, &rules[0]);
    LayoutTextFixtureRuleAdd(&fixture, &rules[1]);
    LayoutTextFixtureRuleAdd(&fixture, &rules[2]);
    LayoutStyleSourceInit(&fixture.styles);
    fixture.styles.userdata = &fixture;
    fixture.styles.ComputedStyleFor = LayoutTextStyleLookup;
    RoboCheckEqInt(LayoutTreeCreate(&tree, NULL), RoboOk);
    LayoutBuildOptionsInit(&options);
    options.styles = &fixture.styles;
    RoboCheckEqInt(LayoutTreeBuildFromDocument(tree, fixture.doc.id, &options,
                                                          &report, NULL),
                      RoboOk);

    RoboCheckEqInt(report.textsCreated, 1u);
    /* The document's own newlines and indentation: one text node between `<html>` and
     * `<body>`, one around `<p>`, one before `</body>` and one after `</html>`. */
    RoboCheckEqInt(report.textsSkippedWhitespace, 3u);
    RoboCheckEqInt(report.elementsSeen, 4u); /* html, head, body, p */
    RoboCheckEqInt(report.boxesCreated, 4u);
    RoboCheckEqInt(LayoutTreeObjectCount(tree), 5u); /* + one text run */

    textNode = LayoutTextNodeOf(&fixture, "p");
    textBox = LayoutObjectIdForDomNode(tree, &textNode);
    RoboCheck(textBox != RoboLayoutNodeIdNone);

    kind = MakeLayoutObjectKind(tree, textBox);
    RoboCheckEqInt((int)kind, (int)RoboLayoutObjectText);
    RoboCheckEqInt(LayoutObjectHasChildren(tree, textBox), 0);
    RoboCheckEqInt((int)LayoutObjectDisplay(tree, textBox),
                      (int)RoboCssDisplayInline);

    /* The node it came from is the *text node*, and the accessor is the same door a
     * box uses -- a handle is a node handle. */
    RoboCheckEqInt(LayoutObjectDomNode(tree, textBox, &reported), RoboOk);
    RoboCheckEqInt(HtmlNodeHandleEqual(&reported, &textNode), 1);

    /* The borrowed style is the parent element's, pointer-identical. */
    pElement = LayoutDocumentFind(&fixture.doc, "p");
    pBox = LayoutObjectIdForDomNode(tree, &pElement);
    RoboCheck(LayoutObjectStyle(tree, textBox) ==
               LayoutObjectStyle(tree, pBox));
    RoboCheck(LayoutObjectStyle(tree, textBox) != NULL);

    /* Its parent is the `<p>`'s box, and it is the last of its children. */
    {
        LayoutNodeId parent = RoboLayoutNodeIdNone;

        RoboCheckEqInt(LayoutObjectParent(tree, textBox, &parent), RoboOk);
        RoboCheckEqInt(parent, pBox);
    }

    LayoutTreeDestroy(tree);
    LayoutTextEnd(&fixture);

    /* And the whitespace rule from the other side: a `<p>` holding only spaces
     * generates nothing at all. */
    LayoutTextBegin(&fixture, "<html><body><p>   </p></body></html>", rules, 3u);
    RoboCheckEqInt(LayoutTextObjectOf(&fixture, "p"), RoboLayoutNodeIdNone);
    LayoutTextEnd(&fixture);
}

/**
 * The text-node accessors: what an object's `domNode` is for a text object, and what
 * reading a run before any pass says.
 */
static void TestTextObjectModel(void)
{
    LayoutTextFixture fixture;
    static const LayoutTextRule rules[] = { LAYOUT_TEXT_BLOCK_RULES };
    LayoutNodeId textBox;
    LayoutNodeId pBox;
    LayoutTextRun run;
    LayoutStats stats;

    LayoutFixture();
    LayoutTextBegin(&fixture, "<html><body><p>AB</p></body></html>", rules, 3u);
    textBox = LayoutTextObjectOf(&fixture, "p");
    pBox = LayoutTextBoxOf(&fixture, "p");
    RoboCheck(textBox != RoboLayoutNodeIdNone);

    /* Before any pass, there is no measurement to read -- and the refusal is a miss
     * rather than a zeroed run, because a zeroed run *is* a shape a caller could read
     * a width out of. */
    RoboCheckEqInt(LayoutObjectTextRun(fixture.tree, textBox, &run, NULL),
                      RoboNotFound);
    RoboCheckEqInt(LayoutObjectTextRun(fixture.tree, textBox, NULL, NULL),
                      RoboInvalidArgument);
    RoboCheckEqInt(LayoutObjectTextRun(fixture.tree, RoboLayoutNodeIdNone,
                                                &run, NULL),
                      RoboInvalidArgument);
    RoboCheckEqInt(LayoutObjectTextRun(NULL, textBox, &run, NULL),
                      RoboInvalidArgument);
    /* A box is not a text run, whatever its geometry. */
    RoboCheckEqInt(LayoutObjectTextRun(fixture.tree, pBox, &run, NULL),
                      RoboNotFound);

    LayoutTextPass(&fixture, LAYOUT_TEXT_WIDE_VIEWPORT, NULL, &stats, NULL);
    /* After a pass with no measurer the run is zero -- but it *was* measured, so the
     * record is readable and says zero, which is the distinction the accessor's
     * refusal was drawing. */
    RoboCheckEqInt(LayoutObjectTextRun(fixture.tree, textBox, &run, NULL), RoboOk);
    RoboCheckEqInt(run.width, 0);
    RoboCheckEqInt(run.range.end, 2u);
    RoboCheckEqInt(run.codePoints, 0u);

    LayoutTextEnd(&fixture);
}

/* ------------------------------------------------------------------ */
/* The real measurer                                                   */
/* ------------------------------------------------------------------ */

static LayoutTextStyle LayoutTextFaceStyle(double fontSizePx)
{
    LayoutTextStyle style;

    LayoutTextStyleInit(&style);
    style.fontSizePx = fontSizePx;
    return style;
}

/**
 * The measurer over a real face, at three sizes.
 *
 * This is the case that says what "measurement" *is* in this build: the sum of the
 * glyphs' own `hmtx` advances, in font units, converted once. The three numbers -- 60
 * for `A`, 90 for `AB`, 30 for `B` -- come from a face whose advances are 600/300 in a
 * 1000-unit em at 100px, so a measurer that returned the font size, the code-point
 * count, or the same number twice would fail on the first assertion. The half-size and
 * double-size runs then pin the *ratio*, which is the property a per-glyph rounding
 * would quietly break.
 */
static void TestTextFontMeasure(void)
{
    LayoutTextFixture fixture;
    static const LayoutTextRule rules[] = { LAYOUT_TEXT_BLOCK_RULES };
    LayoutTextFace face;
    LayoutTextFont measurer;
    LayoutTextRun run;
    LayoutTextStyle style;
    HtmlNodeHandle node;
    HtmlNodeHandle element;
    FontMetrics line;
    float ascentPx = 0.0f;
    float descentPx = 0.0f;
    float gapPx = 0.0f;
    LayoutTextRequest request;

    LayoutFixture();
    LayoutTextBegin(&fixture, "<html><body><p>A</p></body></html>", rules, 3u);
    node = LayoutTextNodeOf(&fixture, "p");

    LayoutTextFaceCreate(&face);
    LayoutTextFontInit(&measurer);
    RoboCheckEqInt(LayoutTextFontIsAttached(&measurer), 0);
    RoboCheckEqInt(LayoutTextFontAttach(&measurer, face.face), RoboOk);
    RoboCheckEqInt(LayoutTextFontIsAttached(&measurer), 1);
    RoboCheckEqInt(LayoutTextFontAttach(NULL, face.face), RoboInvalidArgument);
    RoboCheckEqInt(LayoutTextFontAttach(&measurer, NULL), RoboInvalidArgument);

    /* What the face itself says, which is what the run below has to agree with.
     * RoboFont's table parses are *lazy*, so the metrics are asked for once here
     * before they are read -- the same call the measurer makes on attach, made
     * explicitly so that this case reads the face rather than a measurer's copy of
     * it. */
    RoboCheckEqInt(FontFaceParseMetrics(face.face), RoboOk);
    RoboCheckEqInt(FontFaceLayoutMetrics(face.face, &line), RoboOk);
    RoboCheckEqInt((int)line.unitsPerEm, (int)LAYOUT_TEXT_FACE_UPEM);
    RoboCheckEqInt((int)line.ascent, 800);
    RoboCheckEqInt((int)line.descent, 200); /* a magnitude, and RoboFont says so */
    RoboCheckEqInt((int)line.lineGap, 90);
    RoboCheckEqInt(FontScaleFontUnits(line.ascent, line.unitsPerEm,
                                                (FontPixelSize)LAYOUT_TEXT_FACE_FONT_SIZE,
                                                RoboFontRoundNearest, &ascentPx),
                      RoboOk);
    RoboCheckEqInt(FontScaleFontUnits(line.descent, line.unitsPerEm,
                                                (FontPixelSize)LAYOUT_TEXT_FACE_FONT_SIZE,
                                                RoboFontRoundNearest, &descentPx),
                      RoboOk);
    RoboCheckEqInt(FontScaleFontUnits(line.lineGap, line.unitsPerEm,
                                                (FontPixelSize)LAYOUT_TEXT_FACE_FONT_SIZE,
                                                RoboFontRoundNearest, &gapPx),
                      RoboOk);

    style = LayoutTextFaceStyle(LAYOUT_TEXT_FACE_FONT_SIZE);

    /* `A` -> glyph 1, advance 600 units -> 60px. */
    request = GetRoboLayoutTextRequest(&node, LayoutTextRangeFull(1u), &style);
    RoboCheckEqInt(LayoutTextFontMeasure(&measurer, &request, &run, NULL), RoboOk);
    RoboCheckEqInt(run.width, 60);
    RoboCheckEqInt(run.codePoints, 1u);
    RoboCheckEqInt(run.glyphsMissing, 0u);
    RoboCheckEqInt(run.ascent, (int32_t)(ascentPx + 0.5f));
    RoboCheckEqInt(run.descent, (int32_t)(descentPx + 0.5f));
    RoboCheckEqInt(run.height, run.ascent + run.descent + (int32_t)(gapPx + 0.5f));
    RoboCheckEqInt(run.height, 109);
    RoboCheckEqInt((int)run.direction, (int)RoboLayoutTextDirectionLtr);
    RoboCheckEqInt(HtmlNodeHandleEqual(&run.source, &node), 1);

    /* An empty range is a real range: a zero-size run, not a failure. */
    request = GetRoboLayoutTextRequest(&node, LayoutTextRangeAt(0u), &style);
    RoboCheckEqInt(LayoutTextFontMeasure(&measurer, &request, &run, NULL), RoboOk);
    RoboCheckEqInt(run.width, 0);
    RoboCheckEqInt(run.codePoints, 0u);
    /* The line extent is the *face's*, not the run's: a line has a height whether or
     * not anything is on it. */
    RoboCheckEqInt(run.height, 109);

    /* Two glyphs of *different* advances, so a single-glyph face would not pass. */
    LayoutTextEnd(&fixture);
    LayoutTextBegin(&fixture, "<html><body><p>AB</p></body></html>", rules, 3u);
    node = LayoutTextNodeOf(&fixture, "p");
    request = GetRoboLayoutTextRequest(&node, LayoutTextRangeFull(2u), &style);
    RoboCheckEqInt(LayoutTextFontMeasure(&measurer, &request, &run, NULL), RoboOk);
    RoboCheckEqInt(run.width, 90);

    style.fontSizePx = LAYOUT_TEXT_FACE_FONT_SIZE / 2.0;
    RoboCheckEqInt(LayoutTextFontMeasure(&measurer, &request, &run, NULL), RoboOk);
    RoboCheckEqInt(run.width, 45);
    style.fontSizePx = LAYOUT_TEXT_FACE_FONT_SIZE * 2.0;
    RoboCheckEqInt(LayoutTextFontMeasure(&measurer, &request, &run, NULL), RoboOk);
    RoboCheckEqInt(run.width, 180);

    /* A code point the face has no glyph for: measured at `.notdef` (500 units -> 50px)
     * and *counted*, which is the only way a caller can learn the width it read is for
     * text the font cannot draw. `H` is not in this face's cmap; `h` is not either. */
    {
        LayoutTextFixture other;
        HtmlNodeHandle missing;

        LayoutTextBegin(&other, "<html><body><p>Z</p></body></html>", rules, 3u);
        missing = LayoutTextNodeOf(&other, "p");
        style.fontSizePx = LAYOUT_TEXT_FACE_FONT_SIZE;
        request = GetRoboLayoutTextRequest(&missing, LayoutTextRangeFull(1u), &style);
        RoboCheckEqInt(LayoutTextFontMeasure(&measurer, &request, &run, NULL),
                          RoboOk);
        RoboCheckEqInt(run.glyphsMissing, 1u);
        RoboCheckEqInt(run.width,
                          (int32_t)(LAYOUT_TEXT_GLYPH_NOTDEF_ADVANCE / 10u)); /* 50px */
        LayoutTextEnd(&other);
    }

    /* The range is refused when it does not lie inside the node, and a node that does
     * not resolve is a miss -- both before the face is asked. */
    request = GetRoboLayoutTextRequest(&node, LayoutTextRangeFull(3u), &style);
    RoboCheckEqInt(LayoutTextFontMeasure(&measurer, &request, &run, NULL),
                      RoboInvalidArgument);
    request = GetRoboLayoutTextRequest(&node, LayoutTextRangeFull(2u), NULL);
    RoboCheckEqInt(LayoutTextFontMeasure(&measurer, &request, &run, NULL),
                      RoboInvalidArgument);
    RoboCheckEqInt(LayoutTextFontMeasure(&measurer, &request, NULL, NULL),
                      RoboInvalidArgument);
    RoboCheckEqInt(LayoutTextFontMeasure(NULL, &request, &run, NULL),
                      RoboInvalidArgument);

    /* Detached is a refusal, not a zero: a measurer with no face cannot answer. */
    LayoutTextFontDetach(&measurer);
    RoboCheckEqInt(LayoutTextFontIsAttached(&measurer), 0);
    request = GetRoboLayoutTextRequest(&node, LayoutTextRangeFull(2u), &style);
    RoboCheckEqInt(LayoutTextFontMeasure(&measurer, &request, &run, NULL),
                      RoboError);
    LayoutTextFontDetach(&measurer); /* idempotent */
    LayoutTextFontDetach(NULL);      /* and safe on NULL */

    /* An element handle is refused too: a run can only live in a text node. */
    element = LayoutDocumentFind(&fixture.doc, "p");
    request = GetRoboLayoutTextRequest(&element, LayoutTextRangeFull(1u), &style);
    RoboCheckEqInt(LayoutTextFontMeasure(&measurer, &request, &run, NULL),
                      RoboInvalidArgument);

    LayoutTextEnd(&fixture);
    LayoutTextFaceDestroy(&face);
}

/**
 * The measurer as a *source*: the adapter the pass installs, and its own counters.
 *
 * `runsMeasured` and `glyphsMissing` live on the measurer, so a caller can read what
 * its font did without the pass reporting it twice -- and they are asserted here rather
 * than only through the run, because the measurer's numbers survive the run being
 * discarded and the pass's survive the measurer being detached.
 */
static void TestTextFontSource(void)
{
    LayoutTextFixture fixture;
    static const LayoutTextRule rules[] = { LAYOUT_TEXT_BLOCK_RULES };
    LayoutTextFace face;
    LayoutTextFont measurer;
    LayoutTextMetrics metrics;
    LayoutTextRun run;
    LayoutTextStyle style;
    HtmlNodeHandle node;
    LayoutTextRequest request;

    LayoutFixture();
    LayoutTextBegin(&fixture, "<html><body><p>AB</p></body></html>", rules, 3u);
    node = LayoutTextNodeOf(&fixture, "p");
    LayoutTextFaceCreate(&face);
    LayoutTextFontInit(&measurer);
    RoboCheckEqInt(measurer.runsMeasured, 0u);
    RoboCheckEqInt(measurer.glyphsMissing, 0u);
    RoboCheckEqInt(LayoutTextFontAttach(&measurer, face.face), RoboOk);

    metrics = LayoutTextFontMetrics(&measurer);
    RoboCheckEqInt(LayoutTextMetricsIsUsable(&metrics), 1);
    style = LayoutTextFaceStyle(LAYOUT_TEXT_FACE_FONT_SIZE);
    request = GetRoboLayoutTextRequest(&node, LayoutTextRangeFull(2u), &style);

    /* Through the *seam*, not through the measurer directly: this is the call the pass
     * makes, and the run it produces has to be the same one. */
    RoboCheckEqInt(LayoutTextMeasure(&metrics, &request, &run, NULL), RoboOk);
    RoboCheckEqInt(run.width, 90);
    RoboCheckEqInt(measurer.runsMeasured, 1u);
    RoboCheckEqInt((int)measurer.glyphsMissing, 0);

    /* A run the face cannot draw increments the measurer's own counter, which is the
     * fact a caller reads when it wants to know whether the font is adequate for the
     * document rather than whether one run was. */
    {
        LayoutTextFixture other;
        HtmlNodeHandle missing;

        LayoutTextBegin(&other, "<html><body><p>ZZ</p></body></html>", rules, 3u);
        missing = LayoutTextNodeOf(&other, "p");
        request = GetRoboLayoutTextRequest(&missing, LayoutTextRangeFull(2u), &style);
        RoboCheckEqInt(LayoutTextMeasure(&metrics, &request, &run, NULL), RoboOk);
        RoboCheckEqInt(run.glyphsMissing, 2u);
        RoboCheckEqInt(measurer.glyphsMissing, 2u);
        RoboCheckEqInt(measurer.runsMeasured, 2u);
        LayoutTextEnd(&other);
    }

    /* A detached measurer still yields a *record*; the record simply has no callback,
     * which is exactly what the seam reads as "measures nothing". That is the whole
     * reason the source is a record and not a bare function pointer. */
    LayoutTextFontDetach(&measurer);
    metrics = LayoutTextFontMetrics(&measurer);
    RoboCheckEqInt(LayoutTextMetricsIsUsable(&metrics), 0);
    request = GetRoboLayoutTextRequest(&node, LayoutTextRangeFull(2u), &style);
    RoboCheckEqInt(LayoutTextMeasure(&metrics, &request, &run, NULL), RoboOk);
    RoboCheckEqInt(run.width, 0);
    /* And an empty record from no font at all is unusable rather than a crash. */
    metrics = LayoutTextFontMetrics(NULL);
    RoboCheckEqInt(LayoutTextMetricsIsUsable(&metrics), 0);

    LayoutTextFaceDestroy(&face);
    LayoutTextEnd(&fixture);
}

/* ------------------------------------------------------------------ */
/* The pass                                                            */
/* ------------------------------------------------------------------ */

/** The text object inside the fixture's `<p>`, asserted to exist. */
static LayoutNodeId LayoutTextRequireObject(LayoutTextFixture *fixture)
{
    LayoutNodeId id = LayoutTextObjectOf(fixture, "p");

    RoboCheck(id != RoboLayoutNodeIdNone);
    RoboCheckEqInt((int)MakeLayoutObjectKind(fixture->tree, id),
                      (int)RoboLayoutObjectText);
    return id;
}

/**
 * A text run placed along a line, measured.
 *
 * The case the phase exists for, and it asserts the *whole* chain at once: the text
 * node became an object, the object was measured through the seam, the measurement
 * became geometry, the `<p>`'s height became the lines' extent, all three records
 * counted it, and the run is readable back out of the tree. Every number here comes
 * from a scripted measurer whose answer is 10px per code point, so a build that
 * measured nothing cannot produce it.
 */
static void TestTextPassMeasured(void)
{
    LayoutTextFixture fixture;
    static const LayoutTextRule rules[] = { LAYOUT_TEXT_BLOCK_RULES };
    LayoutNodeId textBox;
    LayoutNodeId pBox;
    HtmlNodeHandle textNode;
    LayoutTextScript script;
    LayoutTextMetrics metrics;
    LayoutStats stats;
    LayoutTextRun run;

    LayoutFixture();
    LayoutTextBegin(&fixture, "<html><body><p>AB</p></body></html>", rules, 3u);
    memset(&script, 0, sizeof(script));
    script.perCodePoint = 10;
    script.height = 20;
    script.ascent = 16;
    script.descent = 4;
    metrics = LayoutTextScriptMetrics(&script);

    LayoutTextPass(&fixture, LAYOUT_TEXT_WIDE_VIEWPORT, &metrics, &stats, NULL);
    textBox = LayoutTextRequireObject(&fixture);
    pBox = LayoutTextBoxOf(&fixture, "p");
    textNode = LayoutTextNodeOf(&fixture, "p");

    /* The cursor began at zero, so the first participant is at the origin. */
    LayoutTextExpect(&fixture, textBox, 0, 0, 20, 20);
    /* And the block extent of the box that owns the inline context is the line's. */
    LayoutTextExpect(&fixture, pBox, 0, 0, LAYOUT_TEXT_WIDE_VIEWPORT, 20);
    RoboCheckEqInt(script.calls, 1u);

    RoboCheckEqInt(stats.text.runsPlaced, 1u);
    RoboCheckEqInt(stats.text.measured, 1u);
    RoboCheckEqInt(stats.text.unmeasured, 0u);
    RoboCheckEqInt(stats.text.empty, 0u);
    RoboCheckEqInt(stats.text.codePoints, 2u);
    RoboCheckEqInt(stats.text.glyphsMissing, 0u);
    RoboCheckEqInt(stats.text.rtl, 0u);
    RoboCheckEqInt(stats.text.widthMax, 20);
    RoboCheckEqInt(stats.text.heightMax, 20);
    RoboCheckEqInt(stats.text.depthMax, 3u); /* html 0, head/body 1, p 2, text 3 */

    /* A text run is a participant of the line, and it is *not* one of the two box
     * counters: it is not a box, and counting it as one would make those two describe
     * a set of things no formatting context has a rule about. */
    RoboCheckEqInt(stats.inlineFlow.participantsPlaced, 1u);
    RoboCheckEqInt(stats.inlineFlow.atomicPlaced, 0u);
    RoboCheckEqInt(stats.inlineFlow.nonAtomicPlaced, 0u);
    RoboCheckEqInt(stats.inlineFlow.lines, 1u);
    RoboCheckEqInt(stats.inlineFlow.wraps, 0u);
    RoboCheckEqInt(stats.inlineFlow.emptyContexts, 0u);
    RoboCheckEqInt(stats.boxesComputed,
                      stats.block.blocksPlaced + stats.inlineFlow.participantsPlaced);

    /* The run, read back out of the tree: what was measured, and the extents a line
     * box and a baseline will need without re-measuring. */
    RoboCheckEqInt(LayoutObjectTextRun(fixture.tree, textBox, &run, NULL), RoboOk);
    RoboCheckEqInt(run.width, 20);
    RoboCheckEqInt(run.height, 20);
    RoboCheckEqInt(run.ascent, 16);
    RoboCheckEqInt(run.descent, 4);
    RoboCheckEqInt(run.codePoints, 2u);
    RoboCheckEqInt(run.glyphsMissing, 0u);
    RoboCheckEqInt(run.range.start, 0u);
    RoboCheckEqInt(run.range.end, 2u);
    RoboCheckEqInt((int)run.direction, (int)RoboLayoutTextDirectionLtr);
    RoboCheckEqInt(HtmlNodeHandleEqual(&run.source, &textNode), 1);

    LayoutTextEnd(&fixture);
}

/**
 * The degradation, stated as a number.
 *
 * No measurer is installed, and the document lays out *exactly* as 10.4 left it -- a
 * `<p>` holding only text is still zero-high -- while the counters say why: one run was
 * placed, it was *not* measured, and it is still a participant of the line. That
 * combination is what lets a build with no font be a legitimate configuration rather
 * than a broken one, and it is the property that made this phase land without moving a
 * 10.4 expectation.
 */
static void TestTextPassUnmeasured(void)
{
    LayoutTextFixture fixture;
    static const LayoutTextRule rules[] = { LAYOUT_TEXT_BLOCK_RULES };
    LayoutNodeId textBox;
    LayoutNodeId pBox;
    LayoutStats stats;
    LayoutTextRun run;

    LayoutFixture();
    LayoutTextBegin(&fixture, "<html><body><p>AB</p></body></html>", rules, 3u);
    LayoutTextPass(&fixture, LAYOUT_TEXT_WIDE_VIEWPORT, NULL, &stats, NULL);
    textBox = LayoutTextRequireObject(&fixture);
    pBox = LayoutTextBoxOf(&fixture, "p");

    /* Placed, at the origin, with a measured width of zero -- which is 10.4's answer
     * for a `<span>` holding only text, now with a *reason* attached. */
    LayoutTextExpect(&fixture, textBox, 0, 0, 0, 0);
    LayoutTextExpect(&fixture, pBox, 0, 0, LAYOUT_TEXT_WIDE_VIEWPORT, 0);
    RoboCheckEqInt(stats.text.runsPlaced, 1u);
    RoboCheckEqInt(stats.text.measured, 0u);
    RoboCheckEqInt(stats.text.unmeasured, 1u);
    RoboCheckEqInt(stats.text.codePoints, 0u);
    RoboCheckEqInt(stats.text.widthMax, 0);
    RoboCheckEqInt(stats.text.heightMax, 0);

    /* Still a participant, and still a line: "nobody measured this" is not "this is
     * not on the line". */
    RoboCheckEqInt(stats.inlineFlow.participantsPlaced, 1u);
    RoboCheckEqInt(stats.inlineFlow.lines, 1u);
    RoboCheckEqInt(stats.inlineFlow.emptyContexts, 0u);

    /* And the record is readable -- zero, and *not* the same statement as the
     * accessor's refusal before any pass ran. */
    RoboCheckEqInt(LayoutObjectTextRun(fixture.tree, textBox, &run, NULL), RoboOk);
    RoboCheckEqInt(run.width, 0);
    RoboCheckEqInt(run.range.end, 2u);
    RoboCheckEqInt(run.codePoints, 0u);

    LayoutTextEnd(&fixture);
}

/**
 * Two runs and a transparent inline box between them.
 *
 * The case that proves a text run is a participant of the *same* context as a `span`'s
 * children and not a box of its own: the cursor walks 0 -> 20 -> 40 across two text
 * nodes that are siblings only through a transparent box, and the box itself comes out
 * as the union of the second run. The last assertion is the coordinate convention
 * surviving nesting, with a text run as the thing being rebased.
 */
static void TestTextTwoRunsAndRebase(void)
{
    LayoutTextFixture fixture;
    static const LayoutTextRule rules[] = {
        LAYOUT_TEXT_BLOCK_RULES,
        { "span", RoboCssDisplayInline }
    };
    LayoutNodeId spanBox;
    LayoutNodeId pBox;
    LayoutNodeId innerText;
    HtmlNodeHandle spanNode;
    HtmlNodeView spanView;
    LayoutTextScript script;
    LayoutTextMetrics metrics;
    LayoutStats stats;

    LayoutFixture();
    LayoutTextBegin(&fixture, "<html><body><p>AB<span>CD</span></p></body></html>", rules,
                      4u);
    memset(&script, 0, sizeof(script));
    script.perCodePoint = 10;
    script.height = 20;
    metrics = LayoutTextScriptMetrics(&script);
    LayoutTextPass(&fixture, LAYOUT_TEXT_WIDE_VIEWPORT, &metrics, &stats, NULL);

    pBox = LayoutTextBoxOf(&fixture, "p");
    spanBox = LayoutTextBoxOf(&fixture, "span");
    LayoutTextExpect(&fixture, LayoutTextRequireObject(&fixture), 0, 0, 20, 20);
    /* The span is the union of what its contents reached: x 20..40, the line's height. */
    LayoutTextExpect(&fixture, spanBox, 20, 0, 20, 20);
    LayoutTextExpect(&fixture, pBox, 0, 0, LAYOUT_TEXT_WIDE_VIEWPORT, 20);

    /* The run *inside* the span: the span's own origin subtracted, so it sits at its
     * own containing block's origin rather than at the line's. */
    spanNode = LayoutDocumentFind(&fixture.doc, "span");
    RoboCheckEqInt(HtmlNodeGetView(&spanNode, &spanView, NULL), RoboOk);
    innerText = LayoutObjectIdForDomNode(fixture.tree, &spanView.firstChild);
    RoboCheck(innerText != RoboLayoutNodeIdNone);
    LayoutTextExpect(&fixture, innerText, 0, 0, 20, 20);

    RoboCheckEqInt(stats.text.runsPlaced, 2u);
    RoboCheckEqInt(stats.text.measured, 2u);
    RoboCheckEqInt(stats.text.codePoints, 4u);
    RoboCheckEqInt(stats.inlineFlow.participantsPlaced, 3u); /* two runs + the span */
    RoboCheckEqInt(stats.inlineFlow.nonAtomicPlaced, 1u);   /* the span, and only it */
    RoboCheckEqInt(stats.inlineFlow.atomicPlaced, 0u);
    RoboCheckEqInt(stats.inlineFlow.lines, 1u);
    RoboCheckEqInt(script.calls, 2u);

    LayoutTextEnd(&fixture);
}

/**
 * Wrapping, with a text run as the thing that does not fit.
 *
 * A measured run is one of the two participants whose width is known before it is
 * placed (the other is an atomic inline), so it is one of the two the wrap rule can act
 * on. The first half asserts the rule's other edge: a run wider than its *empty* line is
 * placed and overflows rather than starting lines forever.
 */
static void TestTextWrapping(void)
{
    LayoutTextFixture fixture;
    static const LayoutTextRule rules[] = {
        LAYOUT_TEXT_BLOCK_RULES,
        { "i", RoboCssDisplayInline }
    };
    LayoutTextScript script;
    LayoutTextMetrics metrics;
    LayoutStats stats;
    LayoutNodeId pBox;
    LayoutNodeId first;
    LayoutNodeId second;
    HtmlNodeHandle iNode;
    HtmlNodeView iView;

    LayoutFixture();
    memset(&script, 0, sizeof(script));
    script.perCodePoint = 30;
    script.height = 20;
    metrics = LayoutTextScriptMetrics(&script);

    /* One run of 60px in a 50px line: placed anyway, and the line count stays 1. */
    LayoutTextBegin(&fixture, "<html><body><p>AB</p></body></html>", rules, 4u);
    LayoutTextPass(&fixture, 50, &metrics, &stats, NULL);
    first = LayoutTextRequireObject(&fixture);
    LayoutTextExpect(&fixture, first, 0, 0, 60, 20);
    RoboCheckEqInt(stats.inlineFlow.lines, 1u);
    RoboCheckEqInt(stats.inlineFlow.wraps, 0u);
    LayoutTextEnd(&fixture);

    /* Two runs of 60px in a 50px line: the second cannot fit after the cursor, so the
     * line moves down and the cursor returns to the start. */
    LayoutTextBegin(&fixture, "<html><body><p>AB<i>CD</i></p></body></html>", rules, 4u);
    LayoutTextPass(&fixture, 50, &metrics, &stats, NULL);
    pBox = LayoutTextBoxOf(&fixture, "p");
    first = LayoutTextRequireObject(&fixture);
    LayoutTextExpect(&fixture, first, 0, 0, 60, 20);

    iNode = LayoutDocumentFind(&fixture.doc, "i");
    RoboCheckEqInt(HtmlNodeGetView(&iNode, &iView, NULL), RoboOk);
    second = LayoutObjectIdForDomNode(fixture.tree, &iView.firstChild);
    RoboCheck(second != RoboLayoutNodeIdNone);
    /* Placed on the second line, in the line's space... */
    {
        LayoutResult result;
        LayoutPoint position;

        RoboCheckEqInt(LayoutObjectResult(fixture.tree, second, &result), RoboOk);
        RoboCheckEqInt(LayoutResultPosition(&result, &position), RoboOk);
        /* ...which makes its position inside the transparent box's own space
         * (0, 0) -- the rebase again, now over a wrapped line. */
        RoboCheckEqInt(position.x, 0);
        RoboCheckEqInt(position.y, 0);
    }
    RoboCheckEqInt(stats.inlineFlow.lines, 2u);
    RoboCheckEqInt(stats.inlineFlow.wraps, 1u);
    /* The `<p>`'s height is both lines: the cursor's extent, not the tallest one. */
    LayoutTextExpect(&fixture, pBox, 0, 0, 50, 40);
    RoboCheckEqInt(stats.text.runsPlaced, 2u);
    RoboCheckEqInt(stats.text.heightMax, 20);

    LayoutTextEnd(&fixture);
}

/**
 * The declared direction reaches the run, and the record counts it.
 *
 * Direction is *metadata* at this stage: nothing reorders anything, because the bidi
 * algorithm is a later goal's. What the case proves is that a declaration reached the
 * measurer -- `request->style->direction` -- and came back on the run, which is the
 * contract a later bidirectional pass is written against. The style is installed on the
 * rule by hand rather than through a stylesheet because the scripted fixture has no
 * cascade; the *pipeline* case below is where a `direction` declaration written in CSS
 * is asserted end to end.
 */
static void TestTextDirectionInPass(void)
{
    LayoutTextFixture fixture;
    static const LayoutTextRule rules[] = { LAYOUT_TEXT_BLOCK_RULES };
    LayoutTextScript script;
    LayoutTextMetrics metrics;
    LayoutStats stats;
    LayoutTextRun run;
    LayoutNodeId textBox;

    LayoutFixture();
    LayoutTextBegin(&fixture, "<html><body><p>AB</p></body></html>", rules, 3u);
    /* The `<p>`'s own style carries the declaration, and a text object borrows its
     * parent's style -- so this is the same path a real cascade takes. */
    LayoutTextStyleSetKeyword(fixture.rules[2].style, "direction", "rtl");
    memset(&script, 0, sizeof(script));
    script.perCodePoint = 10;
    script.height = 20;
    metrics = LayoutTextScriptMetrics(&script);
    LayoutTextPass(&fixture, LAYOUT_TEXT_WIDE_VIEWPORT, &metrics, &stats, NULL);

    textBox = LayoutTextRequireObject(&fixture);
    RoboCheckEqInt(LayoutObjectTextRun(fixture.tree, textBox, &run, NULL), RoboOk);
    RoboCheckEqInt((int)run.direction, (int)RoboLayoutTextDirectionRtl);
    RoboCheckEqInt(stats.text.rtl, 1u);
    RoboCheckEqInt(stats.text.measured, 1u);

    LayoutTextEnd(&fixture);
}

/**
 * A text run in a **block-direction** context is declined, not badly placed.
 *
 * `<div>` holding both text and a `<p>` establishes a block context (10.2's rule sees a
 * block-level child), so the inline run in it has no line to be on: CSS wraps it in an
 * anonymous block box first and this build makes none. The run is counted
 * (`inlineLevelSkipped`), left UNRESOLVED, and its run record is refused by the
 * accessor -- because a zeroed run read as an answer is exactly the plausible number
 * this module refuses to hand out.
 */
static void TestTextBlockContext(void)
{
    LayoutTextFixture fixture;
    static const LayoutTextRule rules[] = {
        LAYOUT_TEXT_BLOCK_RULES,
        { "div", RoboCssDisplayBlock }
    };
    HtmlNodeHandle divNode;
    HtmlNodeView divView;
    HtmlNodeHandle child;
    LayoutNodeId runBox = RoboLayoutNodeIdNone;
    LayoutTextScript script;
    LayoutTextMetrics metrics;
    LayoutStats stats;
    LayoutTextRun run;
    size_t objects;

    LayoutFixture();
    LayoutTextBegin(&fixture, "<html><body><div>AB<p>CD</p></div></body></html>", rules,
                      4u);
    memset(&script, 0, sizeof(script));
    script.perCodePoint = 10;
    script.height = 20;
    metrics = LayoutTextScriptMetrics(&script);
    LayoutTextPass(&fixture, LAYOUT_TEXT_WIDE_VIEWPORT, &metrics, &stats, NULL);

    divNode = LayoutDocumentFind(&fixture.doc, "div");
    RoboCheckEqInt(HtmlNodeGetView(&divNode, &divView, NULL), RoboOk);
    child = divView.firstChild;
    while (HtmlNodeHandleIsWellformed(&child)) {
        HtmlNodeView view;

        if (HtmlNodeGetView(&child, &view, NULL) != RoboOk) {
            break;
        }
        if (view.kind == RoboHtmlNodeKindText) {
            runBox = LayoutObjectIdForDomNode(fixture.tree, &child);
            break;
        }
        child = view.NextSibling;
    }
    RoboCheck(runBox != RoboLayoutNodeIdNone);
    LayoutTextExpectUnresolved(&fixture, runBox);
    RoboCheckEqInt(LayoutObjectTextRun(fixture.tree, runBox, &run, NULL),
                      RoboNotFound);

    /* Declined *instead of* measured: a measurement with nowhere to go is not a result,
     * and the measurer was never asked about this node. */
    RoboCheckEqInt(stats.text.runsPlaced, 1u); /* the run inside the `<p>` */
    RoboCheckEqInt(script.calls, 1u);
    RoboCheckEqInt(stats.block.inlineLevelSkipped >= 1u, 1);

    /* And the identity still closes over both kinds of object. */
    objects = LayoutTreeObjectCount(fixture.tree);
    RoboCheckEqInt(stats.block.blocksPlaced + stats.block.inlineLevelSkipped +
                          stats.inlineFlow.participantsPlaced + stats.block.unreached,
                      objects);

    LayoutTextEnd(&fixture);
}

/**
 * Determinism, and what changes when the viewport does.
 *
 * Twice at one width is the same numbers and the same geometry; a narrower width adds a
 * line and moves the wrapped run down. The case uses the two-run fixture because a
 * single run never wraps (it would be alone on its line), so a viewport change could
 * not be told from a build that ignored it.
 */
static void TestTextRelayout(void)
{
    LayoutTextFixture fixture;
    static const LayoutTextRule rules[] = {
        LAYOUT_TEXT_BLOCK_RULES,
        { "i", RoboCssDisplayInline }
    };
    LayoutTextScript script;
    LayoutTextMetrics metrics;
    LayoutStats first, second, narrow;
    LayoutNodeId pBox;

    LayoutFixture();
    LayoutTextBegin(&fixture, "<html><body><p>AB<i>CD</i></p></body></html>", rules, 4u);
    memset(&script, 0, sizeof(script));
    script.perCodePoint = 10;
    script.height = 20;
    metrics = LayoutTextScriptMetrics(&script);
    pBox = LayoutTextBoxOf(&fixture, "p");

    LayoutTextPass(&fixture, LAYOUT_TEXT_WIDE_VIEWPORT, &metrics, &first, NULL);
    LayoutTextExpect(&fixture, pBox, 0, 0, LAYOUT_TEXT_WIDE_VIEWPORT, 20);
    LayoutTextPass(&fixture, LAYOUT_TEXT_WIDE_VIEWPORT, &metrics, &second, NULL);
    RoboCheckEqInt(LayoutTextStatsEqual(&first.text, &second.text), 1);
    RoboCheckEqInt(LayoutInlineStatsEqual(&first.inlineFlow, &second.inlineFlow),
                      1);
    RoboCheckEqInt(LayoutBlockStatsEqual(&first.block, &second.block), 1);
    LayoutTextExpect(&fixture, pBox, 0, 0, LAYOUT_TEXT_WIDE_VIEWPORT, 20);

    /* 10px + 10px in a 15px line: one participant per line. */
    LayoutTextPass(&fixture, 15, &metrics, &narrow, NULL);
    RoboCheckEqInt(narrow.inlineFlow.lines, 2u);
    RoboCheckEqInt(narrow.inlineFlow.wraps, 1u);
    RoboCheckEqInt(narrow.text.runsPlaced, 2u);
    RoboCheckEqInt(narrow.text.codePoints, 4u);
    LayoutTextExpect(&fixture, pBox, 0, 0, 15, 40);

    LayoutTextEnd(&fixture);
}

/* ------------------------------------------------------------------ */
/* The pipeline                                                        */
/* ------------------------------------------------------------------ */

/** Everything the real CSS path needs, and nothing it owns. */
typedef struct layoutTextPipeline {
    LayoutDocument doc;
    LayoutCssFixture css;
    LayoutDom *dom;
    LayoutTree *tree;
    LayoutStyleSource source;
} LayoutTextPipeline;

static void LayoutTextPipelineBegin(LayoutTextPipeline *pipeline,
                                       const char *html,
                                       const char *css)
{
    LayoutFixture();
    memset(pipeline, 0, sizeof(*pipeline));
    LayoutDocumentBuild(&pipeline->doc, html);
    LayoutCssBegin(&pipeline->css);
    LayoutCssSheet(&pipeline->css, css);
    RoboCheckEqInt(LayoutDomCreate(pipeline->css.context, pipeline->doc.id,
                                            &pipeline->dom, NULL),
                      RoboOk);
    RoboCheckEqInt(LayoutDomRefresh(pipeline->dom, NULL), RoboOk);
    RoboCheckEqInt(LayoutDomStyleSource(pipeline->dom, &pipeline->source, NULL),
                      RoboOk);
}

static LayoutTree *LayoutTextPipelineBuild(LayoutTextPipeline *pipeline)
{
    LayoutBuildOptions options;

    RoboCheckEqInt(LayoutTreeCreate(&pipeline->tree, NULL), RoboOk);
    LayoutBuildOptionsInit(&options);
    options.styles = &pipeline->source;
    RoboCheckEqInt(LayoutTreeBuildFromDocument(pipeline->tree, pipeline->doc.id,
                                                          &options, NULL, NULL),
                      RoboOk);
    return pipeline->tree;
}

static void LayoutTextPipelineEnd(LayoutTextPipeline *pipeline)
{
    if (pipeline->tree) {
        LayoutTreeDestroy(pipeline->tree);
        pipeline->tree = NULL;
    }
    if (pipeline->dom) {
        LayoutDomDestroy(pipeline->dom);
        pipeline->dom = NULL;
    }
    LayoutCssEnd(&pipeline->css);
    LayoutDocumentRelease(&pipeline->doc);
}

/**
 * The whole vertical slice, in one case: CSS declaration -> computed style -> resolved
 * font inputs -> a real font -> geometry -> read back out of the tree.
 *
 * `font-size: 100px` is the only thing in the document that could make the run 90px
 * wide, and 90px is 600 + 300 font units in a 1000-unit em -- so the assertion fails if
 * the cascade, the inheritance of the font size into the text node's borrowed style,
 * the resolution into font inputs, the cmap, or the single scaling step is wrong. The
 * second half does the same at 50px, which is the assertion that the number came from
 * the *declaration* rather than from a constant that happened to match.
 */
static void TestTextPipeline(void)
{
    LayoutTextPipeline pipeline;
    LayoutTextFace face;
    LayoutTextFont measurer;
    LayoutTextMetrics metrics;
    LayoutContext *context = NULL;
    LayoutSize viewport;
    LayoutStats stats;
    LayoutTextRun run;
    LayoutNodeId textBox;
    LayoutNodeId pBox;
    HtmlNodeHandle textNode;
    HtmlNodeHandle element;
    FontMetrics line;

    LayoutTextFaceCreate(&face);
    LayoutTextFontInit(&measurer);
    RoboCheckEqInt(LayoutTextFontAttach(&measurer, face.face), RoboOk);
    metrics = LayoutTextFontMetrics(&measurer);
    RoboCheckEqInt(FontFaceParseMetrics(face.face), RoboOk);
    RoboCheckEqInt(FontFaceLayoutMetrics(face.face, &line), RoboOk);

    LayoutTextPipelineBegin(&pipeline,
                               "<html><body><p>AB</p></body></html>",
                               "html, body, p { display: block }\n"
                               "p { font-size: 100px }\n");
    LayoutTextPipelineBuild(&pipeline);
    /* html, head, body, p, and one text run -- the five objects the parser's document
     * generates, with the text node counted because it carries something. */
    RoboCheckEqInt(LayoutTreeObjectCount(pipeline.tree), 5u);

    viewport.width = LAYOUT_TEXT_WIDE_VIEWPORT;
    viewport.height = 600;
    RoboCheckEqInt(LayoutContextCreate(&context, NULL), RoboOk);
    RoboCheckEqInt(LayoutContextSetViewport(context, viewport, NULL), RoboOk);
    RoboCheckEqInt(LayoutContextSetTextMetrics(context, &metrics), RoboOk);
    {
        LayoutTextMetrics readBack;

        RoboCheckEqInt(LayoutContextTextMetrics(context, &readBack), RoboOk);
        RoboCheckEqInt(LayoutTextMetricsIsUsable(&readBack), 1);
        RoboCheckEqInt(readBack.measure == metrics.measure, 1);
    }
    RoboCheckEqInt(LayoutContextRun(context, pipeline.tree, NULL), RoboOk);
    LayoutContextStats(context, &stats);
    LayoutContextDestroy(context);

    element = LayoutDocumentFind(&pipeline.doc, "p");
    pBox = LayoutObjectIdForDomNode(pipeline.tree, &element);
    textNode = LayoutTextFirstTextChild(&element);
    textBox = LayoutObjectIdForDomNode(pipeline.tree, &textNode);
    RoboCheck(textBox != RoboLayoutNodeIdNone);

    RoboCheckEqInt(LayoutObjectTextRun(pipeline.tree, textBox, &run, NULL), RoboOk);
    RoboCheckEqInt(run.width, 90);
    RoboCheckEqInt(run.codePoints, 2u);
    RoboCheckEqInt(run.glyphsMissing, 0u);
    /*
     * The line extent, checked against the *face*: ascent + descent + gap, each scaled
     * once. Deriving the expectation from RoboFont's own metrics rather than writing
     * 109 keeps the case about the measurer's arithmetic rather than about which table
     * this synthetic face's line metrics came from -- and the assertion that the three
     * components add up to the height is the structural half.
     */
    {
        float ascentPx = 0.0f;
        float descentPx = 0.0f;
        float gapPx = 0.0f;

        RoboCheckEqInt(FontScaleFontUnits(line.ascent, line.unitsPerEm,
                                                    (FontPixelSize)100.0,
                                                    RoboFontRoundNearest, &ascentPx),
                          RoboOk);
        RoboCheckEqInt(FontScaleFontUnits(line.descent, line.unitsPerEm,
                                                    (FontPixelSize)100.0,
                                                    RoboFontRoundNearest, &descentPx),
                          RoboOk);
        RoboCheckEqInt(FontScaleFontUnits(line.lineGap, line.unitsPerEm,
                                                    (FontPixelSize)100.0,
                                                    RoboFontRoundNearest, &gapPx),
                          RoboOk);
        RoboCheckEqInt(run.ascent, (int32_t)(ascentPx + 0.5f));
        RoboCheckEqInt(run.descent, (int32_t)(descentPx + 0.5f));
        RoboCheckEqInt(run.height, run.ascent + run.descent + (int32_t)(gapPx + 0.5f));
        RoboCheckEqInt(run.height > 0, 1);
    }
    LayoutTextExpectIn(pipeline.tree, textBox, 0, 0, 90, run.height);
    LayoutTextExpectIn(pipeline.tree, pBox, 0, 0, LAYOUT_TEXT_WIDE_VIEWPORT, run.height);

    RoboCheckEqInt(stats.text.runsPlaced, 1u);
    RoboCheckEqInt(stats.text.measured, 1u);
    RoboCheckEqInt(stats.text.unmeasured, 0u);
    RoboCheckEqInt(stats.text.codePoints, 2u);
    RoboCheckEqInt(stats.text.widthMax, 90);
    RoboCheckEqInt(measurer.runsMeasured, 1u);
    RoboCheckEqInt(stats.inlineFlow.participantsPlaced, 1u);

    LayoutTextPipelineEnd(&pipeline);

    /* Half the declaration, half the width. */
    LayoutTextPipelineBegin(&pipeline,
                               "<html><body><p>AB</p></body></html>",
                               "html, body, p { display: block }\n"
                               "p { font-size: 50px }\n");
    LayoutTextPipelineBuild(&pipeline);
    RoboCheckEqInt(LayoutContextCreate(&context, NULL), RoboOk);
    RoboCheckEqInt(LayoutContextSetViewport(context, viewport, NULL), RoboOk);
    RoboCheckEqInt(LayoutContextSetTextMetrics(context, &metrics), RoboOk);
    RoboCheckEqInt(LayoutContextRun(context, pipeline.tree, NULL), RoboOk);
    LayoutContextDestroy(context);
    element = LayoutDocumentFind(&pipeline.doc, "p");
    textNode = LayoutTextFirstTextChild(&element);
    textBox = LayoutObjectIdForDomNode(pipeline.tree, &textNode);
    RoboCheckEqInt(LayoutObjectTextRun(pipeline.tree, textBox, &run, NULL), RoboOk);
    RoboCheckEqInt(run.width, 45);
    LayoutTextPipelineEnd(&pipeline);

    LayoutTextFaceDestroy(&face);
}

/**
 * `direction: rtl` **through the cascade**, and a `direction` the cascade never saw.
 *
 * The same vertical slice as above with one more declaration in it, because the
 * direction is the one property whose whole assertion is "it arrived": nothing reorders
 * anything yet, so the only way to test that a declaration reached the measurer is to
 * find it on the run that came back.
 */
static void TestTextPipelineDirection(void)
{
    LayoutTextPipeline pipeline;
    LayoutTextScript script;
    LayoutTextMetrics metrics;
    LayoutContext *context = NULL;
    LayoutSize viewport;
    LayoutStats stats;
    LayoutTextRun run;
    LayoutNodeId textBox;
    HtmlNodeHandle element;
    HtmlNodeHandle textNode;

    memset(&script, 0, sizeof(script));
    script.perCodePoint = 4;
    script.height = 12;
    metrics = LayoutTextStyleMetrics(&script);

    LayoutTextPipelineBegin(&pipeline,
                               "<html><body><p>AB</p></body></html>",
                               "html, body, p { display: block }\n"
                               "p { direction: rtl; font-size: 12px }\n");
    LayoutTextPipelineBuild(&pipeline);

    viewport.width = LAYOUT_TEXT_WIDE_VIEWPORT;
    viewport.height = 600;
    RoboCheckEqInt(LayoutContextCreate(&context, NULL), RoboOk);
    RoboCheckEqInt(LayoutContextSetViewport(context, viewport, NULL), RoboOk);
    RoboCheckEqInt(LayoutContextSetTextMetrics(context, &metrics), RoboOk);
    RoboCheckEqInt(LayoutContextRun(context, pipeline.tree, NULL), RoboOk);
    LayoutContextStats(context, &stats);
    LayoutContextDestroy(context);

    element = LayoutDocumentFind(&pipeline.doc, "p");
    textNode = LayoutTextFirstTextChild(&element);
    textBox = LayoutObjectIdForDomNode(pipeline.tree, &textNode);
    RoboCheckEqInt(LayoutObjectTextRun(pipeline.tree, textBox, &run, NULL), RoboOk);
    RoboCheckEqInt((int)run.direction, (int)RoboLayoutTextDirectionRtl);
    RoboCheckEqInt(stats.text.rtl, 1u);

    /* The style-driven measurer answers one px per code point per px of `font-size`, so
     * the *cascade's* 12px is visible in the geometry. */
    RoboCheckEqInt(run.width, 24);
    RoboCheckEqInt(run.height, 12);

    LayoutTextPipelineEnd(&pipeline);
}

/**
 * The accounting identity, over a document that has text in it.
 *
 * `blocksPlaced + inlineLevelSkipped + participantsPlaced + unreached == objects`
 * is the property that says every object has a story -- and since 10.5 the objects
 * include text runs, which are placed as participants, declined as inline-level, or
 * unreached exactly as boxes are. A build that created a text object and then forgot
 * about it would have four right-looking counts and a document that did not add up.
 */
static void TestTextAccounting(void)
{
    LayoutTextFixture fixture;
    static const LayoutTextRule rules[] = {
        LAYOUT_TEXT_BLOCK_RULES,
        { "div", RoboCssDisplayBlock },
        { "span", RoboCssDisplayInline }
    };
    LayoutTextScript script;
    LayoutTextMetrics metrics;
    LayoutStats stats;
    size_t objects;

    LayoutFixture();
    LayoutTextBegin(&fixture,
                      "<html><body>"
                      "<p>one</p>"
                      "<div>two<span>three</span></div>"
                      "<p>four</p>"
                      "</body></html>",
                      rules, 5u);
    memset(&script, 0, sizeof(script));
    script.perCodePoint = 6;
    script.height = 18;
    metrics = LayoutTextScriptMetrics(&script);
    LayoutTextPass(&fixture, LAYOUT_TEXT_WIDE_VIEWPORT, &metrics, &stats, NULL);

    objects = LayoutTreeObjectCount(fixture.tree);
    RoboCheckEqInt(stats.block.blocksPlaced + stats.block.inlineLevelSkipped +
                          stats.inlineFlow.participantsPlaced + stats.block.unreached,
                      objects);
    RoboCheckEqInt(stats.boxesComputed,
                      stats.block.blocksPlaced + stats.inlineFlow.participantsPlaced);
    RoboCheckEqInt(stats.boxesUnresolved,
                      objects - stats.block.blocksPlaced -
                          stats.inlineFlow.participantsPlaced);

    /*
     * Four text nodes in this document carry content (`one`, `two`, `three`, `four`), and
     * all four are on lines: the `<p>` boxes and the `<div>` each establish an inline
     * context, and the `<span>` is transparent. So the *runs* equal the content nodes,
     * and the two sums below are asserted together because a build that created a text
     * object and never placed it would break the first without breaking the second.
     */
    RoboCheckEqInt(stats.text.runsPlaced, 4u);
    RoboCheckEqInt(stats.text.measured, 4u);
    RoboCheckEqInt(stats.text.codePoints, 3u + 3u + 5u + 4u);

    LayoutTextEnd(&fixture);
}

/** A fixture that includes a real face, and everything a pass needs to measure with it. */
static void TestTextAllocatorBalance(void)
{
    LayoutTextFixture fixture;
    static const LayoutTextRule rules[] = {
        LAYOUT_TEXT_BLOCK_RULES,
        { "span", RoboCssDisplayInline }
    };
    LayoutTextFace face;
    LayoutTextFont measurer;
    LayoutTextMetrics metrics;
    size_t before;
    int pass;

    LayoutFixture();
    before = LayoutLiveBlocks();
    LayoutTextFaceCreate(&face);
    LayoutTextFontInit(&measurer);
    RoboCheckEqInt(LayoutTextFontAttach(&measurer, face.face), RoboOk);
    metrics = LayoutTextFontMetrics(&measurer);

    for (pass = 0; pass < 8; pass++) {
        LayoutStats stats;

        LayoutTextBegin(&fixture,
                          "<html><body><p>hello<span>world</span></p></body></html>", rules,
                          4u);
        LayoutTextPass(&fixture, LAYOUT_TEXT_WIDE_VIEWPORT, &metrics, &stats, NULL);
        LayoutTextPass(&fixture, LAYOUT_TEXT_WIDE_VIEWPORT, &metrics, &stats, NULL);
        LayoutTextEnd(&fixture);
    }

    LayoutTextFontDetach(&measurer);
    LayoutTextFaceDestroy(&face);
    RoboCheckEqInt(LayoutLiveBlocks(), before);
}

/* ------------------------------------------------------------------ */

void TestRegisterLayoutText(void);

void TestRegisterLayoutText(void)
{
    TestRegister("layout.text.range", TestTextRange);
    TestRegister("layout.text.direction", TestTextDirection);
    TestRegister("layout.text.style_initial", TestTextStyleInitial);
    TestRegister("layout.text.style_resolve", TestTextStyleResolve);
    TestRegister("layout.text.run_record", TestTextRunAndMetricsRecord);
    TestRegister("layout.text.stats_record", TestTextStatsRecord);
    TestRegister("layout.text.code_points", TestTextCodePoints);
    TestRegister("layout.text.range_bytes", TestTextRangeBytes);
    TestRegister("layout.text.whitespace", TestTextWhitespace);
    TestRegister("layout.text.seam", TestTextSeam);
    TestRegister("layout.text.measure_node", TestTextMeasureNode);
    TestRegister("layout.text.build", TestTextBuild);
    TestRegister("layout.text.object_model", TestTextObjectModel);
    TestRegister("layout.text.font_measure", TestTextFontMeasure);
    TestRegister("layout.text.font_source", TestTextFontSource);
    TestRegister("layout.text.pass_measured", TestTextPassMeasured);
    TestRegister("layout.text.pass_unmeasured", TestTextPassUnmeasured);
    TestRegister("layout.text.two_runs", TestTextTwoRunsAndRebase);
    TestRegister("layout.text.wrapping", TestTextWrapping);
    TestRegister("layout.text.direction_in_pass", TestTextDirectionInPass);
    TestRegister("layout.text.block_context", TestTextBlockContext);
    TestRegister("layout.text.relayout", TestTextRelayout);
    TestRegister("layout.text.pipeline", TestTextPipeline);
    TestRegister("layout.text.pipeline_direction", TestTextPipelineDirection);
    TestRegister("layout.text.accounting", TestTextAccounting);
    TestRegister("layout.text.allocator_balance", TestTextAllocatorBalance);
}
