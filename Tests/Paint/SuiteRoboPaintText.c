/*
 * RoboPaint text suite (Phase 11.6) — paint.text.*
 *
 * The first drawing this module does whose payload is **not a number**, and the claims the
 * release makes, each pinned where it can be observed rather than argued:
 *
 *   **the characters travelled**   a run's text reaches the record list's pool, the display
 *                                  list's **own** pool, and pixels on a surface — asserted at
 *                                  each stage by reading the span back through the pool's door
 *                                  and comparing it to the document's own text;
 *
 *   **code points, not bytes**     a run of non-ASCII text is counted in code points, which is
 *                                  the unit RoboLayout measures in and the unit the run is
 *                                  painted in — so a span's `count` is a character count and not
 *                                  a byte count, and the case pins it with an accented word;
 *
 *   **the baseline is layout's**   the one number that says where the text sits is the offset
 *                                  `RoboLayoutBaseline.h` produced, read through its own door
 *                                  and never recomputed here;
 *
 *   **the geometry is layout's**   a text command acts on the rectangle RoboLayout placed the
 *                                  run at, and the same `PaintLocalBoxesMatchLayout` call
 *                                  the box suites use checks the record's local boxes against
 *                                  the tree;
 *
 *   **the order is the list's**    a `DRAW_TEXT` appears after its parent box's `CONTENT` and
 *                                  before that box's `END_NODE`, exactly as a run reads as one
 *                                  of a box's contents;
 *
 *   **the pool is a boundary**     a display list carries its characters **independently** of
 *                                  the records it was built from: clearing the record list does
 *                                  not change a character the display list draws;
 *
 *   **one pass for all three**     backgrounds, borders and text are filled in **one** walk, in
 *                                  the list's order, so a text run is drawn where CSS puts it
 *                                  relative to what surrounds it;
 *
 *   **glyphs reach a surface**     with a caller-supplied face, a run's code points resolve
 *                                  through the face's own cmap and stroke their outlines into a
 *                                  RoboGraphics raster — and an unmapped code point is drawn as
 *                                  `.notdef` and **counted** (`glyphsMissing`), the measurer's
 *                                  own rule one layer along;
 *
 *   **no font is a number**        a caller that supplies no face paints a textless document
 *                                  identically and is *told* about a document with text rather
 *                                  than handed a silently blank frame (`noFont`).
 *
 * Every case builds a real document through the real chain (HTML → CSS → layout → records →
 * display list) and paints it into a real RoboGraphics surface. Hand-built *records and
 * commands* appear only where the question is about the painter's own contract — the validity
 * rules, the pool doors, the counters — and never in place of a layout result.
 *
 * The face is **synthetic and built here**: a cmap, an hmtx, an hhea and one square outline,
 * stated as numbers so every expectation is arithmetic. No system font, no Chromium, no
 * Blink, and no test framework beyond the runtime's own.
 *
 * Chromium is reference only; nothing here is a translation of it, and this suite depends on no
 * Chromium header, library or test framework.
 */

#include "RoboPaintTestSupport.h"

#include "RoboFont/RoboFont.h"
#include "RoboFont/RoboFontTt.h"

/* 12.18 §37/§38: the bridge's own agreement case shapes with RoboText directly and compares. */
#include "RoboText/RoboText.h"

#include "RoboGraphics/RoboGraphicsBitmap.h"

#include "RoboPaint/RoboPaintColor.h"
#include "RoboPaint/RoboPaintDecoration.h"
#include "RoboPaint/RoboPaintDisplayDump.h"
#include "RoboPaint/RoboPaintDump.h"

#include <string.h>

/* The same base sheet the other paint suites use: these elements are the block containers, so a
 * fixture's boxes stack in flow order and a run sits inside the box whose element owns it. */
#define TEXT_BASE_CSS \
    "html, body, div, section, p, span { display: block }"

/* Two colours a pixel assertion can name: a text drawn in red on a white page. */
#define TEXT_RED GfxRgba8(0xff, 0x00, 0x00, 255)
#define TEXT_WHITE GfxRgba8(0xff, 0xff, 0xff, 255)

/* ------------------------------------------------------------------ */
/* A synthetic face, built here                                        */
/* ------------------------------------------------------------------ */

/*
 * The face, stated once as numbers so every case's expectation is arithmetic:
 *
 *   units per em   1000          so `font-size: 100px` makes 1 unit = 0.1px
 *   glyph 0        empty         `.notdef`, and what an unmapped code point draws as
 *   glyph 1        square 100    'A'
 *   glyph 2        square 100    'B'
 *   hmtx           advance 600 units for glyphs 1 and 2, 500 for glyph 0
 *
 * so at 100px a mapped glyph is a 10x10 device-unit square and advances 60px. The numbers are
 * chosen to be *unequal* and unmistakable: a painter that ignored the cmap, or the advances,
 * or the outline would fail on the first of them.
 */
#define TEXT_FACE_UPEM 1000u
#define TEXT_FACE_ADVANCE_NOTDEF 500u
#define TEXT_FACE_ADVANCE_GLYPH 600u
#define TEXT_FACE_NUM_GLYPHS 3u
#define TEXT_FACE_SQUARE_UNITS 100
#define TEXT_FACE_FONT_SIZE 100.0

typedef struct {
    uint32_t tag;
    const uint8_t *data;
    uint32_t length;
} TextSynthTable;

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

static size_t TextBuildHhea(uint8_t *b, uint16_t nHMetrics)
{
    uint8_t *p = b;
    int i;

    TextPutU16(&p, 1);
    TextPutU16(&p, 0);
    TextPutI16(&p, 800);  /* ascender */
    TextPutI16(&p, -200); /* descender */
    TextPutI16(&p, 90);   /* line gap */
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

/** `n + 1` long `loca` entries from @p offsets. */
static size_t TextBuildLoca(uint8_t *loca, const uint32_t *offsets, uint16_t n)
{
    uint8_t *p = loca;
    uint16_t i;

    for (i = 0u; i <= n; i++) {
        TextPutU32(&p, offsets[i]);
    }
    return (size_t)(p - loca);
}

/** One glyph: a square from (0, 0) to (100, 100) in FUnits. */
static size_t TextBuildSquareGlyf(uint8_t *b)
{
    uint8_t *p = b;

    TextPutI16(&p, 1);                   /* numberOfContours */
    TextPutI16(&p, 0);                   /* xMin */
    TextPutI16(&p, 0);                   /* yMin */
    TextPutI16(&p, TEXT_FACE_SQUARE_UNITS); /* xMax */
    TextPutI16(&p, TEXT_FACE_SQUARE_UNITS); /* yMax */
    TextPutU16(&p, 3);                   /* endPtsOfContours[0] */
    TextPutU16(&p, 0);                   /* instructionLength */
    TextPutU8(&p, (uint8_t)(RoboFontGlyfFlagOnCurve |
                              RoboFontGlyfFlagXSameOrPositive |
                              RoboFontGlyfFlagYSameOrPositive));
    TextPutU8(&p, (uint8_t)(RoboFontGlyfFlagOnCurve | RoboFontGlyfFlagXShort |
                              RoboFontGlyfFlagXSameOrPositive |
                              RoboFontGlyfFlagYSameOrPositive));
    TextPutU8(&p, (uint8_t)(RoboFontGlyfFlagOnCurve | RoboFontGlyfFlagYShort |
                              RoboFontGlyfFlagXSameOrPositive |
                              RoboFontGlyfFlagYSameOrPositive));
    TextPutU8(&p, (uint8_t)(RoboFontGlyfFlagOnCurve | RoboFontGlyfFlagXShort |
                              RoboFontGlyfFlagYSameOrPositive));
    TextPutU8(&p, TEXT_FACE_SQUARE_UNITS);
    TextPutU8(&p, TEXT_FACE_SQUARE_UNITS);
    TextPutU8(&p, TEXT_FACE_SQUARE_UNITS);
    return (size_t)(p - b);
}

/**
 * A one-subtable cmap holding a format-4 segment table, mapping `first..last` contiguously to
 * glyph ids `1` upward (delta `1 - first`).
 *
 * The required `0xFFFF` segment is appended, because a format-4 table without it is not one.
 */
static size_t TextBuildCmap(uint8_t *b, uint16_t first, uint16_t last)
{
    uint8_t *p = b;
    uint8_t *lengthPtr;
    uint8_t *subtable = b + 12;

    TextPutU16(&p, 0);   /* version */
    TextPutU16(&p, 1);   /* numTables */
    TextPutU16(&p, 3);   /* Windows */
    TextPutU16(&p, 1);   /* Unicode BMP */
    TextPutU32(&p, 12u); /* subtable offset */

    TextPutU16(&p, 4);    /* format */
    lengthPtr = p;
    TextPutU16(&p, 0);    /* length, patched below */
    TextPutU16(&p, 0);    /* language */
    TextPutU16(&p, 4);    /* segCountX2: two segments */
    TextPutU16(&p, 4);    /* searchRange */
    TextPutU16(&p, 1);    /* entrySelector */
    TextPutU16(&p, 0);    /* rangeShift */
    TextPutU16(&p, last);
    TextPutU16(&p, 0xFFFFu);
    TextPutU16(&p, 0); /* reservedPad */
    TextPutU16(&p, first);
    TextPutU16(&p, 0xFFFFu);
    TextPutI16(&p, (int16_t)(1 - (int32_t)first)); /* idDelta[0]: 'A' -> glyph 1 */
    TextPutI16(&p, 1);                             /* idDelta[1]: the 0xFFFF segment */
    TextPutU16(&p, 0);                             /* idRangeOffset[0] */
    TextPutU16(&p, 0);                             /* idRangeOffset[1] */

    {
        uint16_t length = (uint16_t)(p - subtable);

        lengthPtr[0] = (uint8_t)(length >> 8);
        lengthPtr[1] = (uint8_t)length;
    }
    return (size_t)(p - b);
}

/** What a face plus its source cost a caller, so a case can release both the way it built
 *  them. */
typedef struct textFace {
    FontSource *source;
    FontFace *face;
} TextFace;

/**
 * Build the synthetic face, with 'A' -> glyph 1 and 'B' -> glyph 2.
 *
 * Both mapped glyphs carry the square outline (glyph 0, `.notdef`, is empty), so a mapped code
 * point draws pixels and an unmapped one draws none -- which is what makes the pixel and the
 * missing-glyph cases different claims rather than the same one twice.
 */
static void TextFaceCreate(TextFace *out)
{
    uint8_t head[64], hhea[64], maxp[64], hmtx[64], loca[64], glyf[128], cmap[64];
    uint8_t font[1024];
    static const uint16_t advances[TEXT_FACE_NUM_GLYPHS] = {
        (uint16_t)TEXT_FACE_ADVANCE_NOTDEF,
        (uint16_t)TEXT_FACE_ADVANCE_GLYPH,
        (uint16_t)TEXT_FACE_ADVANCE_GLYPH
    };
    TextSynthTable tables[7];
    uint32_t nt = 0u;
    size_t glyphSize;
    size_t n;
    uint8_t *gp;

    out->source = NULL;
    out->face = NULL;

    glyphSize = TextBuildSquareGlyf(glyf);
    /* Two copies: one for each mapped glyph, with glyph 0 left empty. */
    gp = glyf + glyphSize;
    (void)TextBuildSquareGlyf(glyf); /* already written; keep the second copy explicit */
    memcpy(gp, glyf, glyphSize);

    {
        const uint32_t offsets[TEXT_FACE_NUM_GLYPHS + 1u] = {
            0u, 0u, (uint32_t)glyphSize, (uint32_t)(glyphSize * 2u)
        };

        (void)TextBuildLoca(loca, offsets, (uint16_t)TEXT_FACE_NUM_GLYPHS);
    }

    tables[nt].tag = RoboFontTagHead;
    tables[nt].data = head;
    tables[nt].length = (uint32_t)TextBuildHead(head, (uint16_t)TEXT_FACE_UPEM);
    nt++;
    tables[nt].tag = RoboFontTagHhea;
    tables[nt].data = hhea;
    tables[nt].length = (uint32_t)TextBuildHhea(hhea, (uint16_t)TEXT_FACE_NUM_GLYPHS);
    nt++;
    tables[nt].tag = RoboFontTagMaxp;
    tables[nt].data = maxp;
    tables[nt].length = (uint32_t)TextBuildMaxp(maxp, (uint16_t)TEXT_FACE_NUM_GLYPHS);
    nt++;
    tables[nt].tag = RoboFontTagHmtx;
    tables[nt].data = hmtx;
    tables[nt].length = (uint32_t)TextBuildHmtx(hmtx, advances,
                                                 (uint16_t)TEXT_FACE_NUM_GLYPHS);
    nt++;
    tables[nt].tag = RoboFontTagCmap;
    tables[nt].data = cmap;
    /* 'A' (0x41) -> glyph 1, 'B' (0x42) -> glyph 2. */
    tables[nt].length = (uint32_t)TextBuildCmap(cmap, 0x41u, 0x42u);
    nt++;
    tables[nt].tag = RoboFontTagLoca;
    tables[nt].data = loca;
    tables[nt].length = (uint32_t)((TEXT_FACE_NUM_GLYPHS + 1u) * 4u);
    nt++;
    tables[nt].tag = RoboFontTagGlyf;
    tables[nt].data = glyf;
    tables[nt].length = (uint32_t)(glyphSize * 2u);
    nt++;

    n = TextBuildSfnt(font, sizeof(font), RoboFontSfntVersionTrue, tables, nt);
    RoboCheck(n > 0u);
    RoboCheckEqInt(FontSourceCreateMemoryCopy(font, n, &out->source), RoboOk);
    RoboCheckEqInt(FontFaceCreate(out->source, 0, &out->face), RoboOk);
}

static void TextFaceDestroy(TextFace *face)
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
/* Local helpers                                                       */
/* ------------------------------------------------------------------ */

/** A CPU surface for a decoration pass: owned, RGBA8, zero-filled (so "untouched" is
 *  transparent and an unpainted pixel is distinguishable from a black one). */
static GfxSurface *TextSurface(uint32_t width, uint32_t height)
{
    GfxSurfaceDesc desc;
    GfxSurface *surface = NULL;

    GfxSurfaceDescInit(&desc);
    desc.width = width;
    desc.height = height;
    desc.format = RoboGfxFormatRgba8;
    desc.access = RoboGfxSurfaceAccessReadWrite;
    RoboCheckEqInt(GfxSurfaceCreate(&desc, &surface), RoboOk);
    RoboCheck(surface != NULL);
    return surface;
}

/** Build a display list from @p records, asserting the build succeeds. */
static void TextBuild(const PaintResult *records,
                       PaintDisplayList **outList,
                       PaintDisplayListStats *outStats)
{
    PaintDisplayListBuilder *builder = NULL;
    PaintDiag diag;
    Result status;

    RoboCheckEqInt(PaintDisplayListCreate(outList, NULL), RoboOk);
    RoboCheckEqInt(PaintDisplayListBuilderCreate(&builder, NULL), RoboOk);
    PaintDiagReset(&diag);
    status = PaintDisplayListBuilderBuild(builder, records->list, *outList, outStats,
                                                   &diag);
    if (status != RoboOk) {
        TestFail(__FILE__, __LINE__, "the display build refused the records: %s",
                       diag.message ? diag.message : "(no message)");
    }
    PaintDisplayListBuilderDestroy(builder);
}

/** Paint @p list into @p target with @p face (may be NULL), asserting the pass succeeds. */
static void TextPaint(const PaintDisplayList *list,
                       GfxSurface *target,
                       FontFace *face,
                       PaintDecorationStats *outStats)
{
    PaintDiag diag;
    Result status;

    PaintDiagReset(&diag);
    status = PaintDecorationPaint(list, target, face, outStats, &diag);
    if (status != RoboOk) {
        TestFail(__FILE__, __LINE__, "the decoration pass refused the list: %s (%s:%d)",
                       diag.message ? diag.message : "(no message)",
                       diag.file ? diag.file : "?", diag.line);
    }
}

/** The @p nth command of kind @p kind, or 0 when there is none. */
static int TextCommandOfKind(const PaintDisplayList *list,
                                PaintDisplayCommandKind kind,
                                size_t nth,
                                PaintDisplayCommand *out)
{
    size_t index;
    size_t seen = 0u;

    for (index = 0u; index < PaintDisplayListCount(list); index++) {
        PaintDisplayCommand command;

        if (PaintDisplayListAt(list, index, &command) != RoboOk) {
            return 0;
        }
        if (command.kind != kind) {
            continue;
        }
        if (seen == nth) {
            if (out) {
                *out = command;
            }
            return 1;
        }
        seen++;
    }
    return 0;
}

/** The index of the @p nth command of kind @p kind, or `(size_t)-1`. */
static size_t TextIndexOfKind(const PaintDisplayList *list,
                                 PaintDisplayCommandKind kind,
                                 size_t nth)
{
    size_t index;
    size_t seen = 0u;

    for (index = 0u; index < PaintDisplayListCount(list); index++) {
        PaintDisplayCommand command;

        if (PaintDisplayListAt(list, index, &command) != RoboOk) {
            return (size_t)-1;
        }
        if (command.kind == kind) {
            if (seen == nth) {
                return index;
            }
            seen++;
        }
    }
    return (size_t)-1;
}

/** Read @p span out of @p pool into @p out, asserting it reads exactly @p count code points. */
static void TextReadSpan(const PaintTextPool *pool,
                           PaintTextSpan span,
                           uint32_t *out,
                           size_t count,
                           const char *what)
{
    size_t got = 0u;

    memset(out, 0, count * sizeof(*out));
    if (PaintTextPoolRead(pool, span, out, count, &got, NULL) != RoboOk) {
        TestFail(__FILE__, __LINE__, "%s: the span could not be read", what);
        return;
    }
    if (got != count) {
        TestFail(__FILE__, __LINE__, "%s: expected %d code points, read %d", what,
                       (int)count, (int)got);
    }
}

/**
 * Assert the @p span of @p pool holds exactly @p count code points named by @p expected.
 *
 * The count is **given** rather than derived from the expected array's byte length, because
 * a span counts **code points** -- the unit RoboLayout measures in -- and a fixture that
 * spelled a four-character accented word as five bytes would be asserting the wrong unit in
 * the one case the phase's central claim is about.
 */
static void TextExpectPoints(const PaintTextPool *pool,
                               PaintTextSpan span,
                               const uint32_t *expected,
                               size_t count,
                               const char *what)
{
    uint32_t buffer[64];
    size_t index;

    if (count > 64u) {
        TestFail(__FILE__, __LINE__, "%s: the expectation is longer than the buffer",
                       what);
        return;
    }
    if (span.count != (uint32_t)count) {
        TestFail(__FILE__, __LINE__, "%s: expected %d code points, span holds %d", what,
                       (int)count, (int)span.count);
        return;
    }
    TextReadSpan(pool, span, buffer, count, what);
    for (index = 0u; index < count; index++) {
        if (buffer[index] != expected[index]) {
            TestFail(__FILE__, __LINE__, "%s: code point %d is U+%04X, expected U+%04X",
                           what, (int)index, (unsigned)buffer[index],
                           (unsigned)expected[index]);
            return;
        }
    }
}

/** Assert the @p span of @p pool holds exactly the ASCII text @p expected. */
static void TextExpectChars(const PaintTextPool *pool,
                              PaintTextSpan span,
                              const char *expected,
                              const char *what)
{
    uint32_t buffer[64];
    size_t length = strlen(expected);
    size_t index;

    if (length > 64u) {
        TestFail(__FILE__, __LINE__, "%s: the expectation is longer than the buffer",
                       what);
        return;
    }
    for (index = 0u; index < length; index++) {
        buffer[index] = (uint32_t)(unsigned char)expected[index];
    }
    TextExpectPoints(pool, span, buffer, length, what);
}

/** Non-zero when @p haystack contains @p needle. */
static int TextContains(const char *haystack, const char *needle)
{
    return haystack != NULL && needle != NULL && strstr(haystack, needle) != NULL;
}

/* ------------------------------------------------------------------ */
/* 1. The record                                                       */
/* ------------------------------------------------------------------ */

static void TestTextRecordShape(void)
{
    PaintFixtureDoc fixture;
    PaintResult result;
    PaintOperation operation;
    PaintTextRun run;
    LayoutBaselineMetrics baseline;
    LayoutTextStyle style;
    PaintTextPool *pool;
    const CssComputedStyle *computed;
    int32_t expectedBaseline = 0;

    PaintBeginLaidOut(&fixture, "<html><body><div class=\"box\">Hello</div></body></html>",
                         TEXT_BASE_CSS " .box { width: 300px; height: 60px }");
    PaintOnce(&fixture, &result);

    /* Exactly one run was reached and it produced one record. */
    RoboCheckEqInt((int)result.stats.textRuns, 1);
    RoboCheckEqInt((int)result.stats.texts, 1);
    RoboCheckEqInt((int)result.stats.textSkipped, 0);
    RoboCheckEqInt((int)PaintCountOf(&result, RoboPaintOpText), 1);

    operation = PaintRequire(&result, RoboPaintOpText);
    RoboCheckEqInt((int)PaintOperationKind(&operation),
                      (int)RoboPaintOpText);
    RoboCheckEqInt(PaintOperationIsDrawing(&operation), 1);
    RoboCheckEqInt(PaintOperationIsStructural(&operation), 0);
    RoboCheckEqInt(PaintOperationIsValid(&operation), 1);

    /* The record's local boxes are what layout wrote -- the same door the box suites use. */
    PaintLocalBoxesMatchLayout(&fixture, operation);

    /* The characters are the document's, read through the list's own pool. */
    run = PaintOperationText(&operation);
    RoboCheckEqInt(PaintTextRunIsValid(&run), 1);
    pool = PaintListTextPool(result.list);
    RoboCheck(pool != NULL);
    RoboCheckEqInt(PaintTextPoolContains(pool, run.span), 1);
    RoboCheckEqInt((int)PaintTextPoolCount(pool), 5);
    TextExpectChars(pool, run.span, "Hello", "the run's characters");

    /* The baseline is layout's, through its own door. */
    RoboCheckEqInt(LayoutObjectBaseline(fixture.tree, operation.node, &baseline, NULL),
                      RoboOk);
    RoboCheckEqInt(LayoutBaselineOffset(&baseline, &expectedBaseline), RoboOk);
    RoboCheckEqInt(run.baselineOffset, expectedBaseline);
    RoboCheck(PaintTextRunBaselineOffset(&run) == expectedBaseline);

    /* The font inputs are the measurer's, by value. */
    computed = LayoutObjectStyle(fixture.tree, operation.node);
    RoboCheck(computed != NULL);
    LayoutTextStyleInit(&style);
    RoboCheckEqInt(LayoutTextStyleResolve(computed, &style), RoboOk);
    RoboCheck(PaintTextRunFont(&run).fontSizePx == style.fontSizePx);
    RoboCheckEqInt(PaintTextRunFont(&run).fontWeight, style.fontWeight);
    RoboCheckEqInt(PaintTextRunFont(&run).italic, style.italic);
    RoboCheckEqInt((int)PaintTextRunFont(&run).direction, (int)style.direction);

    /* And every other kind carries a cleared run -- the "one record, one meaning" rule. */
    {
        PaintOperation background;

        if (PaintNthOfKind(&result, RoboPaintOpBackground, 0u, &background)) {
            RoboCheckEqInt(PaintOperationText(&background).span.count, 0);
        }
    }

    PaintResultDispose(&result);
    PaintEnd(&fixture);
}

static void TestTextCharactersAreCodePoints(void)
{
    PaintFixtureDoc fixture;
    PaintResult result;
    PaintOperation operation;
    PaintTextRun run;
    PaintTextPool *pool;
    /* 'cafe' with an accented e: four code points, five UTF-8 bytes, so the last of them is
     * U+00E9 rather than two bytes that happen to spell it. */
    static const uint32_t expected[4] = { 0x0063u, 0x0061u, 0x0066u, 0x00E9u };

    PaintBeginLaidOut(&fixture,
                         "<html><body><div class=\"box\">caf\xc3\xa9</div></body></html>",
                         TEXT_BASE_CSS " .box { width: 300px; height: 60px }");
    PaintOnce(&fixture, &result);

    operation = PaintRequire(&result, RoboPaintOpText);
    run = PaintOperationText(&operation);
    pool = PaintListTextPool(result.list);

    /* A span counts **code points**, so five bytes and four characters are four. */
    RoboCheckEqInt((int)run.span.count, 4);
    RoboCheckEqInt((int)PaintTextPoolCount(pool), 4);
    TextExpectPoints(pool, run.span, expected, 4, "the accented run");

    PaintResultDispose(&result);
    PaintEnd(&fixture);
}

/* ------------------------------------------------------------------ */
/* 2. The command                                                      */
/* ------------------------------------------------------------------ */

static void TestTextCommandAndSpan(void)
{
    PaintFixtureDoc fixture;
    PaintResult result;
    PaintDisplayList *list = NULL;
    PaintDisplayListStats stats;
    PaintDisplayCommand command;
    PaintOperation operation;
    PaintTextRun recordRun;
    PaintTextRun commandRun;
    PaintTextPool *recordPool;
    PaintTextPool *commandPool;
    uint32_t characters[16];
    size_t got = 0u;

    PaintBeginLaidOut(&fixture, "<html><body><div class=\"box\">AB</div></body></html>",
                         TEXT_BASE_CSS " .box { width: 300px; height: 60px }");
    PaintOnce(&fixture, &result);
    memset(&stats, 0, sizeof(stats));
    TextBuild(&result, &list, &stats);

    RoboCheckEqInt((int)stats.texts, 1);
    RoboCheckEqInt((int)PaintDisplayListCountOfKind(
                          list, RoboPaintDisplayCmdDrawText),
                      1);
    RoboCheckEqInt(TextCommandOfKind(list, RoboPaintDisplayCmdDrawText, 0u,
                                           &command),
                      1);
    RoboCheckEqInt((int)MakePaintDisplayCommandKind(&command),
                      (int)RoboPaintDisplayCmdDrawText);
    RoboCheckEqInt(PaintDisplayCommandIsDrawing(&command), 1);
    RoboCheckEqInt(PaintDisplayCommandIsValid(&command), 1);
    RoboCheckEqInt(PaintDisplayCommandKindDraws(
                          RoboPaintDisplayCmdDrawText),
                      1);

    /* The rectangle the command acts on is the placement layout produced. */
    operation = PaintRequire(&result, RoboPaintOpText);
    PaintRectEq(PaintDisplayCommandRect(&command), operation.boxes.border,
                  "the text command's rectangle");

    /* The run crossed the boundary: same offset, same font, same length. */
    recordRun = PaintOperationText(&operation);
    commandRun = PaintDisplayCommandText(&command);
    RoboCheck(PaintTextRunEqual(&recordRun, &commandRun));
    RoboCheckEqInt(commandRun.baselineOffset, recordRun.baselineOffset);
    RoboCheckEqInt((int)commandRun.span.count, (int)recordRun.span.count);

    /* Two pools, and the display list's is its own: the characters read back from **its**
     * pool through **its** span, without consulting the record list at all. */
    recordPool = PaintListTextPool(result.list);
    commandPool = PaintDisplayListTextPool(list);
    RoboCheck(commandPool != NULL);
    /* Two different containers: the display list carries its own characters. */
    RoboCheck(recordPool != commandPool);
    RoboCheckEqInt(PaintTextPoolContains(commandPool, commandRun.span), 1);
    TextExpectChars(commandPool, commandRun.span, "AB",
                      "the command's characters");
    RoboCheckEqInt(PaintTextPoolCount(commandPool), 2);
    RoboCheckEqInt(PaintDisplayCommandText(&command).span.first,
                      commandRun.span.first);

    /* And the same span read out of the record list's pool says the same thing, which is the
     * copy being a copy rather than a renumbering that changed the characters. */
    RoboCheckEqInt(PaintTextPoolRead(recordPool, recordRun.span, characters,
                                               16u, &got, NULL),
                      RoboOk);
    RoboCheckEqInt((int)got, 2);

    PaintDisplayListDestroy(list);
    PaintResultDispose(&result);
    PaintEnd(&fixture);
}

static void TestTextOrderAndDepth(void)
{
    PaintFixtureDoc fixture;
    PaintResult result;
    PaintDisplayList *list = NULL;
    PaintDisplayListStats stats;
    PaintDisplayCommand command;
    PaintOperation operation;
    LayoutNodeId box;
    size_t contentIndex;
    size_t textIndex;
    size_t endIndex;

    PaintBeginLaidOut(&fixture, "<html><body><div class=\"box\">AB</div></body></html>",
                         TEXT_BASE_CSS " .box { width: 300px; height: 60px }");
    PaintOnce(&fixture, &result);
    memset(&stats, 0, sizeof(stats));
    TextBuild(&result, &list, &stats);

    operation = PaintRequire(&result, RoboPaintOpText);
    box = RoboLayoutNodeIdNone;
    /* The run's owner is the element whose text node this is -- the box that placed it. */
    RoboCheckEqInt(LayoutObjectParent(fixture.tree, operation.node, &box), RoboOk);
    RoboCheck(box != RoboLayoutNodeIdNone);

    /*
     * The `DRAW_TEXT` sits **inside** the owning box: after its `CONTENT` boundary and before
     * its `END_NODE`. That is what puts a run's painting in the right place relative to the
     * box's own background and border without a second ordering rule.
     */
    contentIndex = (size_t)-1;
    endIndex = (size_t)-1;
    {
        size_t index;

        for (index = 0u; index < PaintDisplayListCount(list); index++) {
            PaintDisplayCommand each;

            RoboCheckEqInt(PaintDisplayListAt(list, index, &each), RoboOk);
            if (each.node != box) {
                continue;
            }
            if (each.kind == RoboPaintDisplayCmdContent) {
                contentIndex = index;
            }
            if (each.kind == RoboPaintDisplayCmdEndNode) {
                endIndex = index;
            }
        }
    }
    textIndex = TextIndexOfKind(list, RoboPaintDisplayCmdDrawText, 0u);
    RoboCheck(contentIndex != (size_t)-1);
    RoboCheck(endIndex != (size_t)-1);
    RoboCheck(textIndex != (size_t)-1);
    RoboCheck(contentIndex < textIndex);
    RoboCheck(textIndex < endIndex);

    /* And the run is one level deeper than the box that owns it, in both spaces. */
    RoboCheckEqInt(TextCommandOfKind(list, RoboPaintDisplayCmdDrawText, 0u,
                                           &command),
                      1);
    {
        size_t index;
        int compared = 0;

        for (index = 0u; index < PaintListCount(result.list); index++) {
            PaintOperation each;

            RoboCheckEqInt(PaintListAt(result.list, index, &each), RoboOk);
            if (each.kind == RoboPaintOpBeginBox && each.node == box) {
                RoboCheckEqInt((int)PaintOperationDepth(&operation),
                                  (int)PaintOperationDepth(&each) + 1);
                RoboCheckEqInt((int)PaintOperationDepth(&operation),
                                  (int)PaintDisplayCommandDepth(&command));
                compared = 1;
                break;
            }
        }
        RoboCheckEqInt(compared, 1);
    }

    PaintDisplayListDestroy(list);
    PaintResultDispose(&result);
    PaintEnd(&fixture);
}

static void TestTextMultipleRuns(void)
{
    PaintFixtureDoc fixture;
    PaintResult result;
    PaintTextPool *pool;
    PaintOperation first;
    PaintOperation second;
    size_t index;

    PaintBeginLaidOut(&fixture,
                         "<html><body><div class=\"box\">"
                         "<span class=\"a\">One</span>"
                         "<span class=\"b\">Two</span>"
                         "</div></body></html>",
                         TEXT_BASE_CSS
                         " .box { width: 300px; height: 80px } .a, .b { height: 30px }");
    PaintOnce(&fixture, &result);

    RoboCheckEqInt((int)result.stats.textRuns, 2);
    RoboCheckEqInt((int)result.stats.texts, 2);
    RoboCheckEqInt((int)PaintCountOf(&result, RoboPaintOpText), 2);

    RoboCheckEqInt(PaintNthOfKind(&result, RoboPaintOpText, 0u, &first), 1);
    RoboCheckEqInt(PaintNthOfKind(&result, RoboPaintOpText, 1u, &second), 1);
    pool = PaintListTextPool(result.list);
    TextExpectChars(pool, PaintOperationText(&first).span, "One",
                      "the first run");
    TextExpectChars(pool, PaintOperationText(&second).span, "Two",
                      "the second run");

    /* Document order is list order, and no pair of runs shares a span. */
    RoboCheck(PaintOperationText(&first).span.first <
               PaintOperationText(&second).span.first);
    RoboCheck(PaintOperationText(&first).span.first +
                   PaintOperationText(&first).span.count <=
               PaintOperationText(&second).span.first);

    /* The two runs are two different objects, so each is deeper than a different box. */
    RoboCheck(PaintOperationNode(&first) != PaintOperationNode(&second));
    for (index = 0u; index < PaintListCount(result.list); index++) {
        PaintOperation each;

        RoboCheckEqInt(PaintListAt(result.list, index, &each), RoboOk);
        if (each.kind == RoboPaintOpText) {
            PaintLocalBoxesMatchLayout(&fixture, each);
        }
    }

    PaintResultDispose(&result);
    PaintEnd(&fixture);
}

static void TestTextWrappedLines(void)
{
    PaintFixtureDoc fixture;
    PaintResult result;
    PaintOperation first;
    PaintOperation second;

    /*
     * Two runs, one line each, because the first fills the line and the second cannot follow
     * it. The measurer charges 8px per code point, so each "AB" is 16px wide; the box is 24px,
     * so the second run meets a cursor of 16 and wraps instead of overhanging.
     *
     * The two runs are **two participants** rather than one text node for two line fragments,
     * because a text node is measured as a unit in this build: a run wider than its line wraps
     * whole rather than being split (`RoboLayoutInline.c`). What the case pins is that the
     * wrap is the *layout's* -- two runs placed at two line tops -- and that paint reads those
     * placements rather than deriving a second line of its own.
     */
    PaintBeginLaidOut(&fixture,
                         "<html><body><div class=\"box\">AB<span class=\"in\">AB</span>"
                         "</div></body></html>",
                         TEXT_BASE_CSS
                         " .box { width: 24px; height: 100px } .box .in { display: inline }");
    PaintOnce(&fixture, &result);

    RoboCheckEqInt((int)result.stats.textRuns, 2);
    RoboCheckEqInt((int)result.stats.texts, (int)result.stats.textRuns);
    RoboCheckEqInt((int)PaintCountOf(&result, RoboPaintOpText), 2);
    RoboCheck(PaintNthOfKind(&result, RoboPaintOpText, 0u, &first) == 1);
    RoboCheck(PaintNthOfKind(&result, RoboPaintOpText, 1u, &second) == 1);
    /* Two lines, so two different vertical placements -- and neither was invented here. */
    RoboCheck(first.boxes.border.y != second.boxes.border.y);
    RoboCheck(second.boxes.border.y > first.boxes.border.y);
    {
        PaintTextPool *pool = PaintListTextPool(result.list);

        TextExpectChars(pool, PaintOperationText(&first).span, "AB",
                          "the first line's run");
        TextExpectChars(pool, PaintOperationText(&second).span, "AB",
                          "the second line's run");
    }

    PaintResultDispose(&result);
    PaintEnd(&fixture);
}

/* ------------------------------------------------------------------ */
/* 3. Colour, baseline, pixels                                         */
/* ------------------------------------------------------------------ */

static void TestTextColor(void)
{
    PaintFixtureDoc fixture;
    PaintResult result;
    PaintDisplayList *list = NULL;
    PaintDisplayListStats stats;
    PaintDisplayCommand command;
    PaintOperation operation;

    PaintBeginLaidOut(&fixture, "<html><body><div class=\"box\">AB</div></body></html>",
                         TEXT_BASE_CSS
                         " .box { width: 300px; height: 60px; color: #ff0000 }");
    PaintOnce(&fixture, &result);

    operation = PaintRequire(&result, RoboPaintOpText);
    PaintColorEq(PaintOperationColor(&operation), CssColorMake(0xff, 0x00, 0x00, 255),
                   "the text record's colour");

    memset(&stats, 0, sizeof(stats));
    TextBuild(&result, &list, &stats);
    RoboCheckEqInt(TextCommandOfKind(list, RoboPaintDisplayCmdDrawText, 0u,
                                           &command),
                      1);
    PaintColorEq(PaintDisplayCommandColor(&command),
                   CssColorMake(0xff, 0x00, 0x00, 255), "the text command's colour");

    PaintDisplayListDestroy(list);
    PaintResultDispose(&result);
    PaintEnd(&fixture);
}

static void TestTextPixels(void)
{
    PaintFixtureDoc fixture;
    PaintResult result;
    PaintDisplayList *list = NULL;
    PaintDisplayListStats stats;
    PaintDecorationStats decoration;
    TextFace face;
    GfxSurface *surface;
    GfxColorRgba8 color;
    uint32_t marked = 0u;
    uint32_t x;
    uint32_t y;

    TextFaceCreate(&face);
    PaintBeginLaidOut(&fixture, "<html><body><div class=\"box\">AB</div></body></html>",
                         TEXT_BASE_CSS
                         " .box { width: 300px; height: 60px; background-color: #ffffff;"
                         " color: #ff0000; font-size: 100px }");
    PaintOnceWithFace(&fixture, face.face, &result);
    memset(&stats, 0, sizeof(stats));
    TextBuild(&result, &list, &stats);

    surface = TextSurface(300u, 100u);
    memset(&decoration, 0, sizeof(decoration));
    TextPaint(list, surface, face.face, &decoration);

    RoboCheckEqInt((int)decoration.texts, 1);
    RoboCheckEqInt((int)decoration.noFont, 0);
    RoboCheckEqInt((int)decoration.glyphs, 2);
    RoboCheckEqInt((int)decoration.glyphsMissing, 0);
    RoboCheckEqInt((int)decoration.commands,
                      (int)(decoration.draws + decoration.skipped));
    RoboCheckEqInt((int)decoration.draws,
                      (int)(decoration.backgrounds + decoration.borders + decoration.texts));
    RoboCheckEqInt((int)decoration.draws,
                      (int)(decoration.filled + decoration.contextual + decoration.transparent +
                            decoration.empty + decoration.noFont));

    /* The glyphs reach the surface: a mapped code point strokes its outline in the run's own
     * colour, so those pixels exist and nothing else on the page is that colour. */
    for (y = 0u; y < GfxSurfaceHeight(surface); y++) {
        for (x = 0u; x < GfxSurfaceWidth(surface); x++) {
            RoboCheckEqInt(GfxSurfaceGetPixel(surface, x, y, &color), RoboOk);
            if (color.r == TEXT_RED.r && color.g == TEXT_RED.g && color.b == TEXT_RED.b) {
                marked++;
            }
        }
    }
    RoboCheck(marked > 0u);

    GfxSurfaceRelease(surface);
    PaintDisplayListDestroy(list);
    PaintResultDispose(&result);
    PaintEnd(&fixture);
    TextFaceDestroy(&face);
}

/*
 * Phase 12.1: the text a *document* draws is **coverage-antialiased**.
 *
 * The pixel case above pins that glyphs reach the surface at all; this one pins what they look
 * like, and it is the assertion the phase exists for. At a size that is not a whole number of the
 * glyph's own grid, every ink edge lands inside a pixel, so a rasterizer that thresholded would
 * produce only the text's colour and the page's. A partial pixel keeps the red channel (red over
 * white keeps it) and gains some green, and at least one such pixel must exist — with a fully
 * covered one beside it, so "antialiased" is not confused with "blurred".
 */
static void TestTextCoverageAntialiasing(void)
{
    PaintFixtureDoc fixture;
    PaintResult result;
    PaintDisplayList *list = NULL;
    PaintDisplayListStats stats;
    PaintDecorationStats decoration;
    TextFace face;
    GfxSurface *surface;
    GfxColorRgba8 color;
    uint32_t partial = 0u;
    uint32_t full = 0u;
    uint32_t x;
    uint32_t y;

    TextFaceCreate(&face);
    PaintBeginLaidOut(&fixture, "<html><body><div class=\"box\">AB</div></body></html>",
                         TEXT_BASE_CSS
                         " .box { width: 300px; height: 60px; background-color: #ffffff;"
                         " color: #ff0000; font-size: 33px }");
    PaintOnceWithFace(&fixture, face.face, &result);
    memset(&stats, 0, sizeof(stats));
    TextBuild(&result, &list, &stats);

    surface = TextSurface(300u, 100u);
    memset(&decoration, 0, sizeof(decoration));
    TextPaint(list, surface, face.face, &decoration);
    RoboCheckEqInt((int)decoration.texts, 1);
    RoboCheckEqInt((int)decoration.glyphs, 2);

    for (y = 0u; y < GfxSurfaceHeight(surface); y++) {
        for (x = 0u; x < GfxSurfaceWidth(surface); x++) {
            RoboCheckEqInt(GfxSurfaceGetPixel(surface, x, y, &color), RoboOk);
            if (color.r == TEXT_RED.r && color.g > 0u && color.g < 255u) {
                partial++;
            }
            if (color.r == TEXT_RED.r && color.g == 0u && color.b == 0u) {
                full++;
            }
        }
    }
    RoboCheck(partial > 0u);
    RoboCheck(full > 0u);

    GfxSurfaceRelease(surface);
    PaintDisplayListDestroy(list);
    PaintResultDispose(&result);
    PaintEnd(&fixture);
    TextFaceDestroy(&face);
}

static void TestTextNoFont(void)
{
    PaintFixtureDoc fixture;
    PaintResult result;
    PaintDisplayList *list = NULL;
    PaintDisplayListStats stats;
    PaintDecorationStats decoration;
    GfxSurface *surface;
    GfxColorRgba8 color;
    uint32_t marked = 0u;
    uint32_t x;
    uint32_t y;

    PaintBeginLaidOut(&fixture, "<html><body><div class=\"box\">AB</div></body></html>",
                         TEXT_BASE_CSS
                         " .box { width: 300px; height: 60px; background-color: #ffffff;"
                         " color: #ff0000; font-size: 100px }");
    PaintOnce(&fixture, &result);
    memset(&stats, 0, sizeof(stats));
    TextBuild(&result, &list, &stats);

    surface = TextSurface(300u, 100u);
    memset(&decoration, 0, sizeof(decoration));
    TextPaint(list, surface, NULL, &decoration);

    /* The text command is seen and **not** drawn, and the reason is the call rather than the
     * document -- which is exactly what `noFont` is for. */
    RoboCheckEqInt((int)decoration.texts, 1);
    RoboCheckEqInt((int)decoration.noFont, 1);
    RoboCheckEqInt((int)decoration.glyphs, 0);
    RoboCheckEqInt((int)decoration.draws,
                      (int)(decoration.backgrounds + decoration.borders + decoration.texts));
    RoboCheckEqInt((int)decoration.draws,
                      (int)(decoration.filled + decoration.contextual + decoration.transparent +
                            decoration.empty + decoration.noFont));

    for (y = 0u; y < GfxSurfaceHeight(surface); y++) {
        for (x = 0u; x < GfxSurfaceWidth(surface); x++) {
            RoboCheckEqInt(GfxSurfaceGetPixel(surface, x, y, &color), RoboOk);
            if (color.r == TEXT_RED.r && color.g == TEXT_RED.g && color.b == TEXT_RED.b) {
                marked++;
            }
        }
    }
    RoboCheckEqInt((int)marked, 0);

    GfxSurfaceRelease(surface);
    PaintDisplayListDestroy(list);
    PaintResultDispose(&result);
    PaintEnd(&fixture);
}

static void TestTextMissingGlyph(void)
{
    PaintFixtureDoc fixture;
    PaintResult result;
    PaintDisplayList *list = NULL;
    PaintDisplayListStats stats;
    PaintDecorationStats decoration;
    TextFace face;
    GfxSurface *surface;

    TextFaceCreate(&face);
    /* 'Z' and 'z' are not in the face's cmap, so both draw as `.notdef`. */
    PaintBeginLaidOut(&fixture, "<html><body><div class=\"box\">Zz</div></body></html>",
                         TEXT_BASE_CSS
                         " .box { width: 300px; height: 60px; color: #ff0000;"
                         " font-size: 100px }");
    PaintOnceWithFace(&fixture, face.face, &result);
    memset(&stats, 0, sizeof(stats));
    TextBuild(&result, &list, &stats);

    surface = TextSurface(300u, 100u);
    memset(&decoration, 0, sizeof(decoration));
    TextPaint(list, surface, face.face, &decoration);

    RoboCheckEqInt((int)decoration.texts, 1);
    RoboCheckEqInt((int)decoration.glyphs, 2);
    /* Counted, not skipped: the measurer's own number one layer along. */
    RoboCheckEqInt((int)decoration.glyphsMissing, 2);

    GfxSurfaceRelease(surface);
    PaintDisplayListDestroy(list);
    PaintResultDispose(&result);
    PaintEnd(&fixture);
    TextFaceDestroy(&face);
}

static void TestTextRtl(void)
{
    PaintFixtureDoc fixture;
    PaintResult result;
    PaintDisplayList *list = NULL;
    PaintDisplayListStats stats;
    PaintDecorationStats decoration;
    PaintOperation operation;
    PaintTextRun run;
    TextFace face;
    GfxSurface *surface;

    TextFaceCreate(&face);
    PaintBeginLaidOut(&fixture, "<html><body><div class=\"box\">AB</div></body></html>",
                         TEXT_BASE_CSS
                         " .box { width: 300px; height: 60px; color: #ff0000;"
                         " font-size: 100px; direction: rtl }");
    PaintOnceWithFace(&fixture, face.face, &result);

    operation = PaintRequire(&result, RoboPaintOpText);
    run = PaintOperationText(&operation);
    RoboCheckEqInt((int)run.font.direction, (int)RoboLayoutTextDirectionRtl);

    memset(&stats, 0, sizeof(stats));
    TextBuild(&result, &list, &stats);
    surface = TextSurface(300u, 100u);
    memset(&decoration, 0, sizeof(decoration));
    TextPaint(list, surface, face.face, &decoration);

    /* Drawn in logical order and **named**: this build has no bidi algorithm, so the fact that
     * a simplification happened is a number a reader sees. */
    RoboCheckEqInt((int)decoration.texts, 1);
    RoboCheckEqInt((int)decoration.rtl, 1);

    GfxSurfaceRelease(surface);
    PaintDisplayListDestroy(list);
    PaintResultDispose(&result);
    PaintEnd(&fixture);
    TextFaceDestroy(&face);
}

/* ------------------------------------------------------------------ */
/* 4. Determinism, isolation, empties                                  */
/* ------------------------------------------------------------------ */

static void TestTextDeterminism(void)
{
    PaintFixtureDoc first;
    PaintFixtureDoc second;
    PaintResult firstResult;
    PaintResult secondResult;
    PaintDisplayList *firstList = NULL;
    PaintDisplayList *secondList = NULL;
    PaintDisplayListStats firstStats;
    PaintDisplayListStats secondStats;
    char firstDump[4096];
    char secondDump[4096];
    size_t firstLength = 0u;
    size_t secondLength = 0u;

    PaintBeginLaidOut(&first, "<html><body><div class=\"box\">Hello</div></body></html>",
                         TEXT_BASE_CSS " .box { width: 300px; height: 60px }");
    PaintOnce(&first, &firstResult);
    memset(&firstStats, 0, sizeof(firstStats));
    TextBuild(&firstResult, &firstList, &firstStats);

    PaintBeginLaidOut(&second, "<html><body><div class=\"box\">Hello</div></body></html>",
                         TEXT_BASE_CSS " .box { width: 300px; height: 60px }");
    PaintOnce(&second, &secondResult);
    memset(&secondStats, 0, sizeof(secondStats));
    TextBuild(&secondResult, &secondList, &secondStats);

    /* Identical input, identical output -- spans, characters, geometry, order and counts. */
    RoboCheckEqInt(PaintDisplayListEqual(firstList, secondList), 1);
    RoboCheckEqInt((int)PaintListCount(firstResult.list),
                      (int)PaintListCount(secondResult.list));
    RoboCheckEqInt((int)PaintTextPoolCount(
                          PaintListTextPool(firstResult.list)),
                      (int)PaintTextPoolCount(
                          PaintListTextPool(secondResult.list)));
    RoboCheck((int)firstResult.stats.texts == (int)secondResult.stats.texts);
    RoboCheck((int)firstStats.texts == (int)secondStats.texts);

    /* And the debug dump is deterministic too -- no addresses, no allocation order. */
    RoboCheckEqInt(PaintDisplayListWrite(firstList, firstDump, sizeof(firstDump),
                                                    &firstLength, NULL),
                      RoboOk);
    RoboCheckEqInt(PaintDisplayListWrite(secondList, secondDump,
                                                    sizeof(secondDump), &secondLength, NULL),
                      RoboOk);
    RoboCheckEqInt((int)firstLength, (int)secondLength);
    RoboCheck(strcmp(firstDump, secondDump) == 0);

    PaintDisplayListDestroy(firstList);
    PaintDisplayListDestroy(secondList);
    PaintResultDispose(&firstResult);
    PaintResultDispose(&secondResult);
    PaintEnd(&first);
    PaintEnd(&second);
}

static void TestTextMutationIsolation(void)
{
    PaintFixtureDoc fixture;
    PaintResult result;
    PaintDisplayList *list = NULL;
    PaintDisplayListStats stats;
    PaintDisplayCommand command;
    PaintTextPool *displayPool;
    TextFace face;
    GfxSurface *surface;
    GfxColorRgba8 color;
    char before[4096];
    char after[4096];
    size_t beforeLength = 0u;
    size_t afterLength = 0u;
    uint32_t marked = 0u;
    uint32_t x;
    uint32_t y;

    TextFaceCreate(&face);
    PaintBeginLaidOut(&fixture, "<html><body><div class=\"box\">AB</div></body></html>",
                         TEXT_BASE_CSS
                         " .box { width: 300px; height: 60px; color: #ff0000;"
                         " font-size: 100px }");
    PaintOnceWithFace(&fixture, face.face, &result);
    memset(&stats, 0, sizeof(stats));
    TextBuild(&result, &list, &stats);

    RoboCheckEqInt(PaintDisplayListWrite(list, before, sizeof(before), &beforeLength,
                                                    NULL),
                      RoboOk);

    /*
     * The records are gone -- every operation and every character they held. The display list
     * still carries its own copy, which is the whole reason it has a pool of its own.
     */
    PaintResultDispose(&result);

    displayPool = PaintDisplayListTextPool(list);
    RoboCheck(displayPool != NULL);
    RoboCheckEqInt((int)PaintTextPoolCount(displayPool), 2);
    RoboCheckEqInt(TextCommandOfKind(list, RoboPaintDisplayCmdDrawText, 0u,
                                           &command),
                      1);
    TextExpectChars(displayPool, PaintDisplayCommandText(&command).span, "AB",
                      "the characters after the records were freed");
    RoboCheckEqInt(PaintDisplayListWrite(list, after, sizeof(after), &afterLength,
                                                    NULL),
                      RoboOk);
    RoboCheckEqInt((int)beforeLength, (int)afterLength);
    RoboCheck(strcmp(before, after) == 0);

    /* And it still paints, so the isolation is about the snapshot and not about a dead list. */
    surface = TextSurface(300u, 100u);
    {
        PaintDecorationStats decoration;

        memset(&decoration, 0, sizeof(decoration));
        TextPaint(list, surface, face.face, &decoration);
        RoboCheckEqInt((int)decoration.glyphs, 2);
    }
    for (y = 0u; y < GfxSurfaceHeight(surface); y++) {
        for (x = 0u; x < GfxSurfaceWidth(surface); x++) {
            RoboCheckEqInt(GfxSurfaceGetPixel(surface, x, y, &color), RoboOk);
            if (color.r == TEXT_RED.r && color.g == TEXT_RED.g && color.b == TEXT_RED.b) {
                marked++;
            }
        }
    }
    RoboCheck(marked > 0u);

    GfxSurfaceRelease(surface);
    PaintDisplayListDestroy(list);
    PaintEnd(&fixture);
    TextFaceDestroy(&face);
}

static void TestTextEmptyCases(void)
{
    PaintFixtureDoc fixture;
    PaintResult result;
    PaintDisplayList *list = NULL;
    PaintDisplayListStats stats;
    PaintDecorationStats decoration;
    GfxSurface *surface;

    /* A document with no text at all: no run reached, no record, and a call with no face is
     * told nothing about a font it did not need. */
    PaintBeginLaidOut(&fixture, "<html><body><div class=\"box\"></div></body></html>",
                         TEXT_BASE_CSS
                         " .box { width: 40px; height: 20px; background-color: #ffffff }");
    PaintOnce(&fixture, &result);

    RoboCheckEqInt((int)result.stats.textRuns, 0);
    RoboCheckEqInt((int)result.stats.texts, 0);
    RoboCheckEqInt((int)result.stats.textSkipped, 0);
    RoboCheckEqInt((int)PaintCountOf(&result, RoboPaintOpText), 0);
    RoboCheckEqInt((int)result.stats.textRuns,
                      (int)(result.stats.texts + result.stats.textSkipped));

    memset(&stats, 0, sizeof(stats));
    TextBuild(&result, &list, &stats);
    RoboCheckEqInt((int)stats.texts, 0);
    RoboCheckEqInt((int)PaintDisplayListCountOfKind(
                          list, RoboPaintDisplayCmdDrawText),
                      0);
    RoboCheckEqInt((int)PaintTextPoolCount(
                          PaintDisplayListTextPool(list)),
                      0);

    surface = TextSurface(80u, 40u);
    memset(&decoration, 0, sizeof(decoration));
    TextPaint(list, surface, NULL, &decoration);
    RoboCheckEqInt((int)decoration.texts, 0);
    RoboCheckEqInt((int)decoration.noFont, 0);
    RoboCheckEqInt((int)decoration.commands,
                      (int)(decoration.draws + decoration.skipped));
    RoboCheckEqInt((int)decoration.draws,
                      (int)(decoration.backgrounds + decoration.borders + decoration.texts));

    GfxSurfaceRelease(surface);
    PaintDisplayListDestroy(list);
    PaintResultDispose(&result);
    PaintEnd(&fixture);
}

static void TestTextHandBuiltRules(void)
{
    PaintOperation operation;
    PaintDisplayCommand command;
    PaintTextRun run;
    PaintList *list = NULL;
    PaintTextSpan span;
    PaintDiag diag;
    PaintBoxes boxes;
    LayoutRect rect;

    PaintFixture();

    /* A box with no inset layers: one rectangle, three times, and the flag clear. */
    PaintBoxesInit(&boxes);
    rect = MakeGfxRect(0, 0, 10, 10);
    boxes.border = rect;
    boxes.padding = rect;
    boxes.content = rect;
    RoboCheckEqInt(PaintBoxesIsValid(&boxes), 1);

    /* A run with no characters is not a run, and an operation carrying one is refused. */
    PaintTextRunInit(&run);
    RoboCheckEqInt(PaintTextRunIsValid(&run), 0);
    run.span = PaintTextSpanAt(0u);
    run.baselineOffset = 8;
    RoboCheckEqInt(PaintTextRunIsValid(&run), 0);

    run.span.count = 1u;
    run.baselineOffset = -1;
    RoboCheckEqInt(PaintTextRunIsValid(&run), 0);

    run.baselineOffset = 8;
    run.span.count = 0u;
    PaintOperationInit(&operation);
    operation.kind = RoboPaintOpText;
    operation.node = 1;
    operation.boxes = boxes;
    operation.color = CssColorMake(0x00, 0x00, 0x00, 255);
    operation.text = run;
    RoboCheckEqInt(PaintOperationIsValid(&operation), 0);

    /* The same record with one character is a record -- and the list checks its span against
     * its own pool, so a span naming characters the pool does not hold is refused there. */
    run.span.count = 1u;
    operation.text = run;
    RoboCheckEqInt(PaintOperationIsValid(&operation), 1);
    RoboCheckEqInt(PaintListCreate(&list, NULL), RoboOk);
    PaintDiagReset(&diag);
    RoboCheckEqInt(PaintListAppend(list, &operation, &diag), RoboInvalidArgument);

    span = PaintTextSpanAt(0u);
    RoboCheckEqInt(PaintListAddText(list, (const uint32_t[]){ 'A' }, 1u, &span,
                                               &diag),
                      RoboOk);
    RoboCheckEqInt((int)span.first, 0);
    RoboCheckEqInt((int)span.count, 1);
    operation.text.span = span;
    RoboCheckEqInt(PaintListAppend(list, &operation, &diag), RoboOk);

    /* And the command's rule is the record's, one field of a kind along. */
    memset(&command, 0, sizeof(command));
    command.kind = RoboPaintDisplayCmdDrawText;
    RoboCheckEqInt(PaintDisplayCommandIsValid(&command), 0);

    PaintListDestroy(list);
}

/*
 * Hand-build a text record with @p glyphCount glyphs, @p sideways of them turned, and the pen on the axis
 * @p vertical selects.
 *
 * The record is assembled the way the **recorder** assembles one -- characters into the list's text pool,
 * glyphs into its glyph pool, the face registered for its token -- because what this case is about is what the
 * drawing pass does with a record that says "this glyph is sideways", and a record the document pipeline
 * cannot yet produce is exactly the one a test has to build. The numbers are the fixture face's own: a 1000
 * unit em, a 100 unit square glyph, a 600 unit advance, drawn at 100 px, so one font unit is 0.1 px and the
 * square is ten pixels on a side.
 */
static void PaintSidewaysRecord(PaintList *list,
                                FontFace *face,
                                int glyphCount,
                                int sideways,
                                int vertical)
{
    PaintGlyph glyphs[2];
    PaintGlyphSpan glyphSpan;
    PaintTextSpan textSpan;
    PaintFontToken token = PaintFontTokenNone;
    PaintOperation operation;
    PaintBoxes boxes;
    PaintDiag diag;
    uint32_t codePoints[2] = {'A', 'B'};
    int index;

    PaintDiagReset(&diag);
    PaintBoxesInit(&boxes);
    boxes.border = MakeGfxRect(0, 0, 300, 60);
    boxes.padding = boxes.border;
    boxes.content = boxes.border;

    /*
     * The structural records first, because a text run is a **child** of the box that owns it: the display
     * builder refuses a drawing that belongs to no open box, and it is right to -- the order it enforces is
     * the order the traversal produces. So this record says what the tree would have said: a box opened at
     * depth 0, its content boundary, and the run one level inside it under a different node.
     */
    PaintOperationInit(&operation);
    operation.kind = RoboPaintOpBeginBox;
    operation.node = 1;
    /* Depth 0: the outermost box nests where the stack is empty. */
    operation.depth = 0u;
    operation.boxes = boxes;
    operation.origin = MakeGfxPoint(0, 0);
    operation.clip = PaintClipNone();
    operation.color = CssColorNone();
    RoboCheckEqInt(PaintListAppend(list, &operation, &diag), RoboOk);
    /* The box's own content boundary, at the box's own depth and naming it. */
    operation.kind = RoboPaintOpContent;
    operation.depth = 0u;
    RoboCheckEqInt(PaintListAppend(list, &operation, &diag), RoboOk);

    RoboCheckEqInt(PaintListAddText(list, codePoints, (size_t)glyphCount, &textSpan, &diag), RoboOk);
    for (index = 0; index < glyphCount; index++) {
        PaintGlyphInit(&glyphs[index]);
        /* The fixture maps `A` and `B` to glyphs 1 and 2, both the same square. */
        glyphs[index].glyphId = (uint16_t)(1 + index);
        glyphs[index].cluster = (uint32_t)index;
        glyphs[index].sourceFirst = (uint32_t)index;
        glyphs[index].sourceCount = 1u;
        glyphs[index].advanceX = vertical ? 0.0f : 60.0f;
        glyphs[index].advanceY = vertical ? 60.0f : 0.0f;
        if (sideways) {
            glyphs[index].flags |= (uint32_t)PaintGlyphFlagSideways;
        }
        RoboCheckEqInt(PaintGlyphIsSideways(&glyphs[index]), sideways ? 1 : 0);
    }
    RoboCheckEqInt(PaintListAddGlyphs(list, glyphs, (size_t)glyphCount, &glyphSpan, &diag), RoboOk);
    RoboCheckEqInt(PaintFontTableAdd(PaintListFontTable(list), face, &token, &diag), RoboOk);
    RoboCheck(token != PaintFontTokenNone);

    PaintOperationInit(&operation);
    operation.kind = RoboPaintOpText;
    operation.node = 2;
    /* One deeper than the box that owns the run, and a **different** node: a run is a child, not a box. */
    operation.depth = 1u;
    operation.boxes = boxes;
    operation.origin = MakeGfxPoint(0, 0);
    operation.clip = PaintClipNone();
    operation.color = CssColorMake(0xff, 0x00, 0x00, 255);
    PaintTextRunInit(&operation.text);
    operation.text.span = textSpan;
    /* A baseline 20 px below the run's top, so the two sides of it are two different sets of rows. */
    operation.text.baselineOffset = 20;
    operation.text.glyphs = glyphSpan;
    operation.text.fontToken = token;
    operation.text.font.fontSizePx = 100.0;
    operation.text.font.direction = RoboLayoutTextDirectionLtr;
    RoboCheckEqInt(PaintOperationIsValid(&operation), 1);
    RoboCheckEqInt(PaintListAppend(list, &operation, &diag), RoboOk);

    /* And the box closes, because a record list that leaves one open is refused. */
    PaintOperationInit(&operation);
    operation.kind = RoboPaintOpEndBox;
    operation.node = 1;
    operation.depth = 0u;
    operation.boxes = boxes;
    operation.origin = MakeGfxPoint(0, 0);
    operation.clip = PaintClipNone();
    operation.color = CssColorNone();
    RoboCheckEqInt(PaintListAppend(list, &operation, &diag), RoboOk);
}

/** The rows of @p surface that carry any pixel of @p color; `first`/`last` are 0 and 0 when there is none. */
static void PaintInkRows(GfxSurface *surface,
                         GfxColorRgba8 color,
                         uint32_t *outFirst,
                         uint32_t *outLast,
                         uint32_t *outCount)
{
    uint32_t x;
    uint32_t y;

    *outFirst = 0u;
    *outLast = 0u;
    *outCount = 0u;
    for (y = 0u; y < GfxSurfaceHeight(surface); y++) {
        for (x = 0u; x < GfxSurfaceWidth(surface); x++) {
            GfxColorRgba8 pixel;

            RoboCheckEqInt(GfxSurfaceGetPixel(surface, x, y, &pixel), RoboOk);
            if (pixel.r == color.r && pixel.g == color.g && pixel.b == color.b && pixel.a != 0u) {
                if (*outCount == 0u) {
                    *outFirst = y;
                }
                *outLast = y;
                (*outCount)++;
            }
        }
    }
}

/*
 * A sideways glyph is **turned** (Phase 12.20), and this case reads that off the pixels.
 *
 * The fixture's glyph is a ten-pixel square sitting above and to the right of its baseline origin, so the
 * drawing pass's own answer is arithmetic a reader can check: upright, its ink occupies the ten rows
 * *above* the baseline and none below; a quarter turn clockwise, the same ten rows *below* it and none
 * above, with the same hundred pixels inked either way -- a turn is an isometry, not a resampling. The
 * column then does both at once: two glyphs advancing down the pen on `advanceY` with `advanceX` zero (a
 * vertical run, whose pair of advances *is* the writing mode) and each drawn turned, which lands one hundred
 * pixels below the baseline and another hundred a further sixty down, with nothing in between.
 *
 * `text.vertical.mark` and `graphics.glyph.rotation` prove RoboText's angle and the rasterizer's transform; this
 * case proves the seam between them -- that a record saying `sideways` reaches the rasterizer turned, and that
 * the pen still advances by the shaper's numbers rather than by anything it derives from the rotation.
 */
static void TestTextSideways(void)
{
    PaintFixtureDoc fixture;
    PaintResult upright;
    PaintResult sideways;
    PaintResult column;
    PaintDisplayList *uprightList = NULL;
    PaintDisplayList *sidewaysList = NULL;
    PaintDisplayList *columnList = NULL;
    PaintDisplayListStats stats;
    PaintDecorationStats decoration;
    TextFace face;
    GfxSurface *surface;
    uint32_t first;
    uint32_t last;
    uint32_t count;

    TextFaceCreate(&face);
    PaintBegin(&fixture, "<html><body></body></html>", TEXT_BASE_CSS);

    PaintResultInit(&upright);
    PaintSidewaysRecord(upright.list, face.face, 1, 0, 0);
    PaintResultInit(&sideways);
    PaintSidewaysRecord(sideways.list, face.face, 1, 1, 0);
    PaintResultInit(&column);
    PaintSidewaysRecord(column.list, face.face, 2, 1, 1);

    TextBuild(&upright, &uprightList, &stats);
    TextBuild(&sideways, &sidewaysList, &stats);
    TextBuild(&column, &columnList, &stats);

    surface = TextSurface(300u, 100u);
    memset(&decoration, 0, sizeof(decoration));
    TextPaint(uprightList, surface, face.face, &decoration);
    RoboCheckEqInt((int)decoration.glyphs, 1);
    RoboCheckEqInt((int)decoration.glyphsMissing, 0);
    PaintInkRows(surface, TEXT_RED, &first, &last, &count);
    /* Above the baseline at `y = 20`: rows 10..19, the square's own ten rows of ten pixels. */
    RoboCheckEqInt((int)first, 10);
    RoboCheckEqInt((int)last, 19);
    RoboCheckEqInt((int)count, 100);
    GfxSurfaceRelease(surface);

    surface = TextSurface(300u, 100u);
    TextPaint(sidewaysList, surface, face.face, &decoration);
    PaintInkRows(surface, TEXT_RED, &first, &last, &count);
    RoboCheckEqInt((int)first, 20);
    RoboCheckEqInt((int)last, 29);
    RoboCheckEqInt((int)count, 100);
    GfxSurfaceRelease(surface);

    /* The column: two turned glyphs a pen step apart, on the y axis, with a gap between them because
     * `advanceX` is zero and the pen never moves sideways. */
    surface = TextSurface(300u, 200u);
    TextPaint(columnList, surface, face.face, &decoration);
    RoboCheckEqInt((int)decoration.glyphs, 2);
    PaintInkRows(surface, TEXT_RED, &first, &last, &count);
    /* Rows 20..29 and 80..89: ten rows for each turned square, the pen having stepped sixty. */
    RoboCheckEqInt((int)first, 20);
    RoboCheckEqInt((int)last, 89);
    RoboCheckEqInt((int)count, 200);
    {
        uint32_t y;
        uint32_t gap = 0u;

        for (y = 30u; y < 80u; y++) {
            uint32_t x;
            GfxColorRgba8 pixel;

            for (x = 0u; x < GfxSurfaceWidth(surface); x++) {
                RoboCheckEqInt(GfxSurfaceGetPixel(surface, x, y, &pixel), RoboOk);
                if (pixel.r == TEXT_RED.r && pixel.g == TEXT_RED.g && pixel.b == TEXT_RED.b &&
                    pixel.a != 0u) {
                    gap++;
                }
            }
        }
        RoboCheckEqInt((int)gap, 0);
    }
    GfxSurfaceRelease(surface);

    PaintDisplayListDestroy(uprightList);
    PaintDisplayListDestroy(sidewaysList);
    PaintDisplayListDestroy(columnList);
    PaintResultDispose(&upright);
    PaintResultDispose(&sideways);
    PaintResultDispose(&column);
    PaintEnd(&fixture);
    TextFaceDestroy(&face);
}

static void TestTextPoolDoors(void)
{
    PaintTextPool *pool = NULL;
    PaintTextPool *other = NULL;
    PaintTextSpan span;
    PaintTextSpan copied;
    PaintTextSpan bogus;
    PaintDiag diag;
    uint32_t buffer[8];
    size_t got = 0u;
    size_t capacity;
    static const uint32_t hello[5] = { 'H', 'e', 'l', 'l', 'o' };

    PaintFixture();

    RoboCheckEqInt(PaintTextPoolCreate(&pool, NULL), RoboOk);
    RoboCheckEqInt(PaintTextPoolCreate(&other, NULL), RoboOk);
    RoboCheckEqInt((int)PaintTextPoolCount(pool), 0);
    RoboCheck(PaintTextPoolCodePoints(pool) == NULL);
    RoboCheckEqInt(PaintTextPoolContains(pool, PaintTextSpanAt(0u)), 1);

    PaintDiagReset(&diag);
    /* A run of zero characters is refused: it would name a position, not a run. */
    RoboCheckEqInt(PaintTextPoolAppend(pool, hello, 0u, &span, &diag),
                      RoboInvalidArgument);
    RoboCheckEqInt(PaintTextPoolAppend(pool, NULL, 1u, &span, &diag),
                      RoboInvalidArgument);
    RoboCheckEqInt(PaintTextPoolAppend(pool, hello, 5u, &span, &diag), RoboOk);
    RoboCheckEqInt((int)span.first, 0);
    RoboCheckEqInt((int)span.count, 5);
    RoboCheckEqInt((int)PaintTextPoolCount(pool), 5);
    capacity = PaintTextPoolCapacity(pool);
    RoboCheck(capacity >= 5u);

    /* The read door validates the span, then copies. */
    RoboCheckEqInt(PaintTextPoolRead(pool, span, buffer, 8u, &got, &diag), RoboOk);
    RoboCheckEqInt((int)got, 5);
    RoboCheck(memcmp(buffer, hello, sizeof(hello)) == 0);

    bogus = PaintTextSpanAt(3u);
    bogus.count = 5u;
    RoboCheckEqInt(PaintTextPoolContains(pool, bogus), 0);
    RoboCheckEqInt(PaintTextPoolRead(pool, bogus, buffer, 8u, &got, &diag),
                      RoboInvalidArgument);

    /* A buffer that cannot hold the run reports how much it would have needed. */
    got = 0u;
    RoboCheckEqInt(PaintTextPoolRead(pool, span, buffer, 2u, &got, &diag),
                      RoboNotSupported);
    RoboCheckEqInt((int)got, 5);

    /* The copy door is the one the builder uses: the characters move and the span renumbers. */
    RoboCheckEqInt(PaintTextSpanAt(4u).count, 0);
    RoboCheckEqInt(PaintTextPoolCopy(other, pool, span, &copied, &diag), RoboOk);
    RoboCheckEqInt((int)PaintTextPoolCount(other), 5);
    RoboCheckEqInt((int)copied.count, 5);
    RoboCheckEqInt(PaintTextPoolContains(other, copied), 1);
    memset(buffer, 0, sizeof(buffer));
    RoboCheckEqInt(PaintTextPoolRead(other, copied, buffer, 8u, &got, &diag),
                      RoboOk);
    RoboCheck(memcmp(buffer, hello, sizeof(hello)) == 0);

    /* An empty span and a span outside the source are both refused by the copy door. */
    RoboCheckEqInt(PaintTextPoolCopy(other, pool, PaintTextSpanAt(0u),
                                                &copied, &diag),
                      RoboInvalidArgument);
    RoboCheckEqInt(PaintTextPoolCopy(other, pool, bogus, &copied, &diag),
                      RoboInvalidArgument);

    /* Clearing keeps the storage -- a renderer that repaints reuses it -- and drops every
     * character with it. */
    PaintTextPoolClear(pool);
    RoboCheckEqInt((int)PaintTextPoolCount(pool), 0);
    RoboCheck(PaintTextPoolCapacity(pool) >= capacity);
    RoboCheckEqInt(PaintTextPoolContains(pool, span), 0);

    /* NULL is a defined answer for every door. */
    RoboCheckEqInt((int)PaintTextPoolCount(NULL), 0);
    RoboCheckEqInt((int)PaintTextPoolCapacity(NULL), 0);
    RoboCheckEqInt(PaintTextPoolContains(NULL, span), 0);
    RoboCheck(PaintTextPoolCodePoints(NULL) == NULL);

    PaintTextPoolDestroy(pool);
    PaintTextPoolDestroy(other);
    PaintTextPoolDestroy(NULL); /* documented as safe */
}

static void TestTextDump(void)
{
    PaintFixtureDoc fixture;
    PaintResult result;
    PaintDisplayList *list = NULL;
    PaintDisplayListStats stats;
    char records[8192];
    char commands[8192];
    size_t recordsLength = 0u;
    size_t commandsLength = 0u;

    PaintBeginLaidOut(&fixture, "<html><body><div class=\"box\">Hi</div></body></html>",
                         TEXT_BASE_CSS
                         " .box { width: 300px; height: 60px; color: #ff0000 }");
    PaintOnce(&fixture, &result);

    RoboCheckEqInt(PaintListWrite(result.list, records, sizeof(records),
                                            &recordsLength, NULL),
                      RoboOk);
    /* The dump names the run's characters, so a reader of a record dump can see what a text
     * operation is about without opening the pool. */
    RoboCheck(TextContains(records, "text=\"Hi\""));

    memset(&stats, 0, sizeof(stats));
    TextBuild(&result, &list, &stats);
    RoboCheckEqInt(PaintDisplayListWrite(list, commands, sizeof(commands),
                                                    &commandsLength, NULL),
                      RoboOk);
    RoboCheck(TextContains(commands, "draw-text"));
    RoboCheck(TextContains(commands, "text=\"Hi\""));

    PaintDisplayListDestroy(list);
    PaintResultDispose(&result);
    PaintEnd(&fixture);
}

/* ------------------------------------------------------------------ */
/* Registration                                                        */
/* ------------------------------------------------------------------ */

/*
 * Phase 12.16: the run is **shaped where it is recorded**, and the paint path owns no Unicode-to-glyph
 * mapping at all.
 *
 * The five cases below pin the phase's contract at the places it can be observed rather than argued:
 *
 *   `recorded`        the record's glyph span and its face token, and the numbers in them -- ids, advances,
 *                     source ranges, clusters -- against the synthetic face's own stated arithmetic;
 *
 *   `snapshot`        the payoff, and the reason the phase exists: a display list's glyphs and faces are its
 *                     **own copies**, so a replay after the record list is destroyed draws the same ink, and it
 *                     does not even need the caller to hand a face in;
 *
 *   `pool_doors`      the glyph pool's own contract, including the container rule the record list enforces:
 *                     a text operation whose glyph span names nothing in the list is refused at the door;
 *
 *   `font_table_doors` the token vocabulary -- idempotent add, both sentinels as *misses*, the copy that
 *                     renumbers a run's face into another list's table, and the dump;
 *
 *   `without_a_face`  the other legal recording: characters and **no** glyphs, which is what a caller that
 *                     installed no face gets and what the drawing pass counts as `noFont`.
 */

/* ------------------------------------------------------------------ */
/* 12.16: the shaped run in the record                                 */
/* ------------------------------------------------------------------ */

/** The first text operation of @p list, asserted to exist. */
static PaintOperation TextOnlyOperation(const PaintList *list)
{
    PaintOperation operation;
    size_t index;

    for (index = 0u; index < PaintListCount(list); index++) {
        RoboCheckEqInt(PaintListAt(list, index, &operation), RoboOk);
        if (operation.kind == RoboPaintOpText) {
            return operation;
        }
    }
    TestFail(__FILE__, __LINE__, "the list holds no text operation");
    PaintOperationInit(&operation);
    return operation;
}

static void TestTextGlyphRunRecorded(void)
{
    PaintFixtureDoc fixture;
    PaintResult result;
    PaintOperation operation;
    PaintTextRun run;
    TextFace face;
    PaintGlyphSpan span;
    PaintGlyph glyphs[4];
    size_t count = 0u;
    FontFace *resolved = NULL;
    const PaintFontTable *table;
    float expectedAdvance;

    TextFaceCreate(&face);
    PaintBeginLaidOut(&fixture, "<html><body><div class=\"box\">AB</div></body></html>",
                         TEXT_BASE_CSS
                         " .box { width: 300px; height: 60px; color: #ff0000;"
                         " font-size: 100px }");
    PaintOnceWithFace(&fixture, face.face, &result);

    operation = TextOnlyOperation(result.list);
    run = operation.text;

    /* The two halves of the record: the characters the document has, and the glyphs the face produced. */
    RoboCheckEqInt((int)PaintTextSpanIsEmpty(PaintTextRunSpan(&run)), 0);
    RoboCheckEqInt((int)run.span.count, 2);
    span = PaintTextRunGlyphs(&run);
    RoboCheckEqInt((int)PaintGlyphSpanIsEmpty(span), 0);
    RoboCheckEqInt((int)span.count, 2);

    /* The face, by token, and the token is an entry of the **list's** table -- which is what makes the
     * record meaningful to the container it sits in rather than to whoever shaped it. */
    RoboCheck(PaintTextRunFontToken(&run) >= PaintFontTokenFirstAssigned);
    table = PaintListFontTable(result.list);
    RoboCheckEqInt((int)PaintFontTableCount(table), 1);
    RoboCheckEqInt(PaintFontTableResolve(table, run.fontToken, &resolved, NULL), RoboOk);
    RoboCheck(resolved == face.face);

    /* And the glyphs themselves, through the pool's own read door: two of them, in logical order, mapping the
     * two characters one-to-one. `A` is glyph 1 and `B` is glyph 2 in this face, and each advances 600 units of
     * a 1000-unit em at 100px -- so every number here is the face's arithmetic rather than a snapshot. */
    RoboCheckEqInt(PaintGlyphPoolRead(PaintListGlyphPool(result.list), span, glyphs, 4u, &count,
                                        NULL),
                      RoboOk);
    RoboCheckEqInt((int)count, 2);
    expectedAdvance = (float)((double)TEXT_FACE_ADVANCE_GLYPH * TEXT_FACE_FONT_SIZE /
                              (double)TEXT_FACE_UPEM);
    RoboCheckEqInt((int)glyphs[0].glyphId, 1);
    RoboCheckEqInt((int)glyphs[1].glyphId, 2);
    RoboCheck(glyphs[0].advanceX == expectedAdvance);
    RoboCheck(glyphs[1].advanceX == expectedAdvance);
    RoboCheck(glyphs[0].advanceY == 0.0f);
    RoboCheck(glyphs[0].offsetX == 0.0f && glyphs[0].offsetY == 0.0f);
    /* One glyph per character, one cluster each, and the source range is **relative to the run**: the 'A' is
     * scalar 0 of this run and the 'B' is scalar 1, whatever the document offset of the run happens to be. */
    RoboCheckEqInt((int)glyphs[0].sourceFirst, 0);
    RoboCheckEqInt((int)glyphs[0].sourceCount, 1);
    RoboCheckEqInt((int)glyphs[1].sourceFirst, 1);
    RoboCheckEqInt((int)glyphs[1].sourceCount, 1);
    RoboCheckEqInt((int)glyphs[0].cluster, 0);
    RoboCheckEqInt((int)glyphs[1].cluster, 1);
    /* Mapped characters with usable metrics: neither is a miss and neither is a control. */
    RoboCheckEqInt((int)(glyphs[0].flags & PaintGlyphFlagMissing), 0);
    RoboCheckEqInt((int)(glyphs[1].flags & PaintGlyphFlagMissing), 0);
    RoboCheckEqInt(PaintGlyphIsControl(&glyphs[0]), 0);
    RoboCheckEqInt(PaintGlyphIsValid(&glyphs[0]), 1);
    RoboCheckEqInt(PaintGlyphEqual(&glyphs[0], &glyphs[1]), 0);

    PaintResultDispose(&result);
    PaintEnd(&fixture);
    TextFaceDestroy(&face);
}

/**
 * §37/§38 of 12.18: what the record carries is what the shaper produced, and it is still that after the
 * display list's own copy.
 *
 * The case one stage past `paint.glyphrun.recorded`. That one asserts the recorded glyphs are *the face's
 * arithmetic* -- ids and advances this suite can compute by hand. This one closes the other direction: shape
 * the same string with RoboText over the same face, and require the record's glyphs to be **that run's own
 * numbers**, bit for bit, including the clusters and the source ranges, and require the same after the
 * display list has renumbered the spans into its own pools. Two consequences are pinned rather than argued:
 * the recorder does not re-derive a position (a second pen would show up as a differing `offsetX` or a
 * differing prefix sum), and 12.16's copy does not strip a cluster (a display list draws the same glyphs the
 * shaped run drew, which is what a later selection or caret needs).
 */
static void TestTextGlyphAgreesWithShaping(void)
{
    PaintFixtureDoc fixture;
    PaintResult result;
    PaintDisplayList *list = NULL;
    PaintDisplayListStats stats;
    PaintOperation operation;
    PaintTextRun run;
    PaintTextRun drawnRun;
    TextFace face;
    RoboTextFontContext font;
    RoboTextFontStyle style;
    RoboTextShapeOptions options;
    RoboTextShapeResult *shaped = NULL;
    const RoboTextGlyphRun *glyphRun;
    const RoboTextMeasurement *measurement;
    PaintGlyph recorded[4];
    PaintGlyph drawn[4];
    PaintDisplayCommand command;
    size_t count = 0u;
    float pen = 0.0f;
    int found = 0;

    TextFaceCreate(&face);
    /*
     * The face's own tables, parsed the way the fixture that owns this face would parse them: `cmap` because
     * a mapping is asked of it, and metrics because the run's advances are. In the document path this has
     * already happened by the time the recorder shapes -- the measurer asks the same tables first -- so a
     * case that shapes *ahead* of the document has to ask for them.
     */
    RoboCheckEqInt((int)FontFaceParseCmap(face.face), (int)RoboOk);
    RoboCheckEqInt((int)FontFaceParseMetrics(face.face), (int)RoboOk);
    /* And the same two steps the recorder takes (`RoboPaintContext.c`'s `PaintTextFontFromStyle`): a style with
     * the run's size, over an initialized context. Shaping on an uninitialized one is an argument error, and
     * that is the door refusing a caller rather than a defect to work around. */
    RoboTextFontContextInit(&font);
    RoboTextFontStyleInit(&style);
    style.sizePx = (float)TEXT_FACE_FONT_SIZE;
    RoboCheckEqInt((int)RoboTextFontContextSet(&font, face.face, &style), (int)RoboOk);
    RoboTextShapeOptionsInit(&options);
    options.baseDirection = RoboTextDirectionLtr;
    {
        RoboTextDiag diag;
        Result status;

        RoboTextDiagReset(&diag);
        status = RoboTextShape(RoboTextViewMake("AB", 2u), &font, &options, &shaped, &diag);
        if (status != RoboOk) {
            TestFail(__FILE__, __LINE__, "shaping refused: %s (%s:%d)",
                     diag.message ? diag.message : "(no message)",
                     diag.file ? diag.file : "?", diag.line);
        }
    }
    RoboCheck(shaped != NULL);
    RoboCheckEqInt((int)RoboTextShapeResultGlyphRunCount(shaped), 1);
    glyphRun = RoboTextShapeResultGlyphRun(shaped, 0u);
    RoboCheckEqInt((int)RoboTextGlyphRunCount(glyphRun), 2);
    measurement = RoboTextShapeResultMeasurement(shaped);
    RoboCheck(measurement != NULL);

    PaintBeginLaidOut(&fixture, "<html><body><div class=\"box\">AB</div></body></html>",
                      TEXT_BASE_CSS
                      " .box { width: 300px; height: 60px; color: #ff0000; font-size: 100px }");
    PaintOnceWithFace(&fixture, face.face, &result);

    operation = TextOnlyOperation(result.list);
    run = operation.text;
    RoboCheckEqInt(PaintGlyphPoolRead(PaintListGlyphPool(result.list), PaintTextRunGlyphs(&run),
                                      recorded, 4u, &count, NULL),
                   RoboOk);
    RoboCheckEqInt((int)count, (int)RoboTextGlyphRunCount(glyphRun));

    for (size_t g = 0u; g < count; g++) {
        const RoboTextGlyph *glyph = RoboTextGlyphRunGlyph(glyphRun, (RoboTextGlyphIndex)g);

        /* The id, the cluster and the source range are integers, and equality is the right question for
         * them. The three floats have been through the module's one scaling door twice, so they are
         * compared exactly rather than within a tolerance: "the same arithmetic" is a stronger claim than
         * "close", and a pen computed twice would fail this one. */
        RoboCheckEqInt((int)recorded[g].glyphId, (int)glyph->glyphId);
        RoboCheckEqInt((int)recorded[g].cluster, (int)glyph->cluster);
        RoboCheckEqInt((int)recorded[g].sourceFirst,
                       (int)(glyph->source.start - RoboTextGlyphRunSourceRange(glyphRun).start));
        RoboCheckEqInt((int)recorded[g].sourceCount,
                       (int)(glyph->source.end - glyph->source.start));
        RoboCheck(recorded[g].advanceX == glyph->advanceX);
        RoboCheck(recorded[g].advanceY == glyph->advanceY);
        RoboCheck(recorded[g].offsetX == glyph->offsetX);
        RoboCheck(recorded[g].offsetY == glyph->offsetY);
        pen += recorded[g].advanceX;
    }
    /* The pen the drawing pass accumulates is the width layout measured, which is the shaped run's own
     * advance sum -- one number reached three ways, and the case exists because they must be one. */
    RoboCheck(pen == RoboTextGlyphRunAdvance(glyphRun));
    RoboCheck(pen == measurement->width);

    /* The display list's copy: the same glyphs, in its own pool, after the records that produced them have
     * been renumbered into it. A cluster stripped here would be a cluster stripped from what the painter
     * draws, and the run's clusters are what selection will be built on. */
    memset(&stats, 0, sizeof(stats));
    TextBuild(&result, &list, &stats);
    for (size_t index = 0u; index < PaintDisplayListCount(list); index++) {
        RoboCheckEqInt(PaintDisplayListAt(list, index, &command), RoboOk);
        if (command.kind != RoboPaintDisplayCmdDrawText) {
            continue;
        }
        found = 1;
        drawnRun = command.text;
        RoboCheckEqInt(PaintGlyphPoolRead(PaintDisplayListGlyphPool(list),
                                          PaintTextRunGlyphs(&drawnRun), drawn, 4u, &count, NULL),
                       RoboOk);
        RoboCheckEqInt((int)count, 2);
        RoboCheckEqInt(PaintGlyphEqual(&drawn[0], &recorded[0]), 1);
        RoboCheckEqInt(PaintGlyphEqual(&drawn[1], &recorded[1]), 1);
        RoboCheckEqInt((int)drawn[0].cluster, 0);
        RoboCheckEqInt((int)drawn[1].cluster, 1);
    }
    RoboCheckEqInt(found, 1);

    PaintDisplayListDestroy(list);
    PaintResultDispose(&result);
    RoboTextShapeResultDestroy(shaped);
    PaintEnd(&fixture);
    TextFaceDestroy(&face);
}

/**
 * The snapshot, one stage past `paint.text.mutation_isolation`.
 *
 * That case pins that a display list carries its characters independently of the records it was built from;
 * this one pins the **shaped** half of the same property, and it is stronger in the two ways 12.16 adds: the
 * display list owns the glyphs *and* the face its tokens name, so it draws the same ink after the record list
 * is gone -- and with no face argument at all, because the run's token resolves inside the list it came in.
 */
static void TestTextGlyphSnapshotOutlivesRecords(void)
{
    PaintFixtureDoc fixture;
    PaintResult result;
    PaintDisplayList *list = NULL;
    PaintDisplayListStats stats;
    PaintDecorationStats decoration;
    TextFace face;
    GfxSurface *surface;
    uint32_t marked = 0u;
    uint32_t x;
    uint32_t y;
    GfxColorRgba8 color;
    size_t index;
    PaintDisplayCommand command;
    int found = 0;

    TextFaceCreate(&face);
    PaintBeginLaidOut(&fixture, "<html><body><div class=\"box\">AB</div></body></html>",
                         TEXT_BASE_CSS
                         " .box { width: 300px; height: 60px; background-color: #ffffff;"
                         " color: #ff0000; font-size: 100px }");
    PaintOnceWithFace(&fixture, face.face, &result);
    memset(&stats, 0, sizeof(stats));
    TextBuild(&result, &list, &stats);

    for (index = 0u; index < PaintDisplayListCount(list); index++) {
        RoboCheckEqInt(PaintDisplayListAt(list, index, &command), RoboOk);
        if (command.kind == RoboPaintDisplayCmdDrawText) {
            FontFace *resolved = NULL;

            found = 1;
            RoboCheckEqInt((int)command.text.glyphs.count, 2);
            /* The command's token is an entry of the **display list's** table, which holds the same face by
             * address -- so the token was renumbered into the copy rather than left naming the other list. */
            RoboCheckEqInt(PaintFontTableResolve(PaintDisplayListFontTable(list),
                                                        command.text.fontToken, &resolved, NULL),
                              RoboOk);
            RoboCheck(resolved == face.face);
        }
    }
    RoboCheckEqInt(found, 1);
    RoboCheckEqInt((int)PaintGlyphPoolCount(PaintDisplayListGlyphPool(list)), 2);
    RoboCheckEqInt((int)PaintFontTableCount(PaintDisplayListFontTable(list)), 1);

    /* The records go away, and the face is **not** handed to the drawing pass: the list's own table answers. */
    PaintResultDispose(&result);
    surface = TextSurface(300u, 100u);
    memset(&decoration, 0, sizeof(decoration));
    TextPaint(list, surface, NULL, &decoration);

    RoboCheckEqInt((int)decoration.texts, 1);
    RoboCheckEqInt((int)decoration.noFont, 0);
    RoboCheckEqInt((int)decoration.glyphs, 2);
    RoboCheckEqInt((int)decoration.glyphsMissing, 0);
    for (y = 0u; y < GfxSurfaceHeight(surface); y++) {
        for (x = 0u; x < GfxSurfaceWidth(surface); x++) {
            RoboCheckEqInt(GfxSurfaceGetPixel(surface, x, y, &color), RoboOk);
            if (color.r == TEXT_RED.r && color.g == TEXT_RED.g && color.b == TEXT_RED.b) {
                marked++;
            }
        }
    }
    RoboCheck(marked > 0u);

    GfxSurfaceRelease(surface);
    PaintDisplayListDestroy(list);
    PaintEnd(&fixture);
    TextFaceDestroy(&face);
}

/*
 * The glyph pool and the font table, through their own doors -- the pool the way `paint.text.pool_doors` asks
 * about the text pool, and the table because a token that is not *asked about* is a token nobody has proved
 * means anything.
 */
static void TestTextGlyphPoolDoors(void)
{
    PaintGlyphPool *pool = NULL;
    PaintGlyph glyph;
    PaintGlyph read[2];
    PaintGlyphSpan span;
    PaintGlyphSpan other;
    size_t count = 0u;

    PaintGlyphInit(&glyph);
    glyph.glyphId = 7u;
    glyph.sourceCount = 1u;
    glyph.advanceX = 3.5f;
    RoboCheckEqInt(PaintGlyphIsValid(&glyph), 1);

    /* Hostile: every door on NULL, and the off-by-one span. */
    RoboCheckEqInt(PaintGlyphPoolCreate(NULL, NULL), RoboInvalidArgument);
    RoboCheckEqInt(PaintGlyphPoolAppend(NULL, &glyph, 1u, &span, NULL), RoboInvalidArgument);
    RoboCheckEqInt(PaintGlyphPoolAppend(pool, NULL, 1u, &span, NULL), RoboInvalidArgument);
    RoboCheckEqInt(PaintGlyphPoolContains(NULL, span), 0);
    RoboCheckEqInt((int)PaintGlyphPoolCount(NULL), 0);
    RoboCheckEqInt((int)PaintGlyphPoolCapacity(NULL), 0);
    RoboCheck(PaintGlyphPoolGlyphs(NULL) == NULL);
    PaintGlyphPoolClear(NULL);
    PaintGlyphPoolDestroy(NULL);

    RoboCheckEqInt(PaintGlyphPoolCreate(&pool, NULL), RoboOk);
    RoboCheckEqInt((int)PaintGlyphPoolCount(pool), 0);
    /* A count of zero is a **position**, not a run, and the sentinel is how a caller spells it. */
    RoboCheckEqInt(PaintGlyphPoolAppend(pool, &glyph, 0u, &span, NULL), RoboInvalidArgument);
    RoboCheckEqInt((int)PaintGlyphSpanIsEmpty(PaintGlyphSpanAt(4u)), 1);

    RoboCheckEqInt(PaintGlyphPoolAppend(pool, &glyph, 1u, &span, NULL), RoboOk);
    RoboCheckEqInt((int)span.first, 0);
    RoboCheckEqInt((int)span.count, 1);
    RoboCheckEqInt((int)PaintGlyphPoolCount(pool), 1);
    RoboCheckEqInt(PaintGlyphPoolContains(pool, span), 1);
    glyph.glyphId = 9u;
    RoboCheckEqInt(PaintGlyphPoolAppend(pool, &glyph, 1u, &other, NULL), RoboOk);
    RoboCheckEqInt((int)other.first, 1);
    RoboCheckEqInt(PaintGlyphPoolContains(pool, other), 1);
    /* Past the end is not contained, and neither is an empty span -- by construction, since a pool holds runs. */
    RoboCheckEqInt(PaintGlyphPoolContains(pool, PaintGlyphSpanAt(2u)), 0);
    RoboCheckEqInt(PaintGlyphPoolContains(pool, PaintGlyphSpanAt(1u)), 0);

    RoboCheckEqInt(PaintGlyphPoolRead(pool, span, read, 2u, &count, NULL), RoboOk);
    RoboCheckEqInt((int)count, 1);
    RoboCheckEqInt((int)read[0].glyphId, 7);
    /* Too small a buffer is `RoboNotSupported`, with the count it would have needed -- the same
     * complete-or-absent rule every writer here states. */
    RoboCheckEqInt(PaintGlyphPoolRead(pool, span, read, 0u, &count, NULL), RoboNotSupported);
    RoboCheckEqInt((int)count, 1);
    /* And a span the pool does not hold is an argument error rather than a short read -- the two are different
     * questions and a door that answered the first with the second would be hiding a caller's bug. */
    RoboCheckEqInt(PaintGlyphPoolRead(pool, PaintGlyphSpanAt(9u), read, 2u, &count, NULL),
                      RoboInvalidArgument);
    RoboCheckEqInt((int)count, 0);
    PaintGlyphPoolClear(pool);
    RoboCheckEqInt((int)PaintGlyphPoolCount(pool), 0);
    RoboCheckEqInt((int)PaintGlyphPoolCapacity(pool) > 0, 1);
    PaintGlyphPoolDestroy(pool);
}

static void TestTextFontTableDoors(void)
{
    PaintFontTable *table = NULL;
    PaintFontTable *copy = NULL;
    TextFace face;
    TextFace second;
    PaintFontToken token = PaintFontTokenNone;
    PaintFontToken again = PaintFontTokenNone;
    PaintFontToken copied = PaintFontTokenNone;
    FontFace *resolved = NULL;
    char dump[256];
    size_t written = 0u;

    TextFaceCreate(&face);
    TextFaceCreate(&second);

    RoboCheckEqInt(PaintFontTableCreate(NULL, NULL), RoboInvalidArgument);
    RoboCheckEqInt((int)PaintFontTableCount(NULL), 0);
    RoboCheck(PaintFontTablePrimary(NULL) == NULL);
    PaintFontTableClear(NULL);
    PaintFontTableDestroy(NULL);

    RoboCheckEqInt(PaintFontTableCreate(&table, NULL), RoboOk);
    RoboCheckEqInt(PaintFontTableAdd(table, NULL, &token, NULL), RoboInvalidArgument);
    RoboCheckEqInt((int)token, (int)PaintFontTokenNone);
    RoboCheckEqInt(PaintFontTableAdd(table, face.face, &token, NULL), RoboOk);
    RoboCheck(token >= PaintFontTokenFirstAssigned);
    /* **Idempotent**: the same face answers with the token it already has, so a document's runs do not grow
     * the table with the text. */
    RoboCheckEqInt(PaintFontTableAdd(table, face.face, &again, NULL), RoboOk);
    RoboCheckEqInt((int)again, (int)token);
    RoboCheckEqInt((int)PaintFontTableCount(table), 1);
    RoboCheckEqInt(PaintFontTableTokenOf(table, face.face, &again, NULL), RoboOk);
    RoboCheckEqInt((int)again, (int)token);
    RoboCheckEqInt(PaintFontTableTokenOf(table, second.face, &again, NULL), RoboNotFound);
    RoboCheckEqInt(PaintFontTableResolve(table, token, &resolved, NULL), RoboOk);
    RoboCheck(resolved == face.face);
    /* Both sentinels are **misses**, each for its own reason: `None` names no face, and `Primary` names one this
     * table has never seen -- the drawing pass's own argument. */
    RoboCheckEqInt(PaintFontTableResolve(table, PaintFontTokenNone, &resolved, NULL), RoboNotFound);
    RoboCheckEqInt(PaintFontTableResolve(table, PaintFontTokenPrimary, &resolved, NULL),
                      RoboNotFound);
    RoboCheck(PaintFontTablePrimary(table) == face.face);

    RoboCheckEqInt(PaintFontTableCreate(&copy, NULL), RoboOk);
    /* The sentinels carry across; an assigned token is resolved and re-added. */
    RoboCheckEqInt(PaintFontTableCopy(copy, table, PaintFontTokenNone, &copied, NULL), RoboOk);
    RoboCheckEqInt((int)copied, (int)PaintFontTokenNone);
    RoboCheckEqInt(PaintFontTableCopy(copy, table, PaintFontTokenPrimary, &copied, NULL), RoboOk);
    RoboCheckEqInt((int)copied, (int)PaintFontTokenPrimary);
    RoboCheckEqInt(PaintFontTableCopy(copy, table, token, &copied, NULL), RoboOk);
    RoboCheckEqInt(PaintFontTableResolve(copy, copied, &resolved, NULL), RoboOk);
    RoboCheck(resolved == face.face);
    /* A token the source does not hold is a miss, and the copy is not left holding a face for it. */
    RoboCheckEqInt(PaintFontTableCopy(copy, table, (PaintFontToken)99u, &copied, NULL),
                      RoboNotFound);
    RoboCheckEqInt((int)PaintFontTableCount(copy), 1);

    RoboCheckEqInt(PaintFontTableWrite(table, dump, sizeof(dump), &written), RoboOk);
    RoboCheck(written > 0u);
    RoboCheck(strstr(dump, "faces 1") != NULL);
    RoboCheckEqInt(PaintFontTableWrite(table, dump, 2u, &written), RoboNotSupported);
    RoboCheckEqInt((int)dump[0], 0);

    PaintFontTableClear(table);
    RoboCheckEqInt((int)PaintFontTableCount(table), 0);
    PaintFontTableDestroy(copy);
    PaintFontTableDestroy(table);
    TextFaceDestroy(&second);
    TextFaceDestroy(&face);
}

/**
 * The container rule, at the record list's door: a `TEXT` operation whose **glyph** span names no run in the
 * list is refused -- the character-span rule 11.6 states, for the half 12.16 added.
 */
static void TestTextGlyphSpanRules(void)
{
    PaintList *list = NULL;
    PaintOperation operation;
    PaintGlyph glyphs[2];
    PaintGlyphSpan span;
    size_t index;

    RoboCheckEqInt(PaintListCreate(&list, NULL), RoboOk);
    RoboCheck(PaintListGlyphPool(list) != NULL);
    RoboCheck(PaintListFontTable(list) != NULL);

    for (index = 0u; index < 2u; index++) {
        PaintGlyphInit(&glyphs[index]);
        glyphs[index].glyphId = (uint16_t)(index + 1u);
        glyphs[index].sourceCount = 1u;
        glyphs[index].advanceX = 60.0f;
    }
    RoboCheckEqInt(PaintListAddGlyphs(list, glyphs, 2u, &span, NULL), RoboOk);
    RoboCheckEqInt((int)span.count, 2);
    RoboCheckEqInt(PaintListAddGlyphs(NULL, glyphs, 2u, NULL, NULL), RoboInvalidArgument);

    /* A run carrying glyphs and the face they came from. */
    PaintOperationInit(&operation);
    operation.kind = RoboPaintOpText;
    operation.node = (LayoutNodeId)1u;
    PaintTextRunInit(&operation.text);
    {
        LayoutRect rect = MakeGfxRect(0, 0, 10, 10);
        PaintTextSpan textSpan;

        PaintBoxesInit(&operation.boxes);
        operation.boxes.border = rect;
        operation.boxes.padding = rect;
        operation.boxes.content = rect;
        operation.color = CssColorMake(0x00, 0x00, 0x00, 255);
        RoboCheckEqInt(PaintListAddText(list, (const uint32_t[]){ 0x41u, 0x42u }, 2u, &textSpan,
                                                NULL),
                          RoboOk);
        operation.text.span = textSpan;
        operation.text.glyphs = span;
        operation.text.fontToken = PaintFontTokenPrimary;
        RoboCheckEqInt(PaintOperationIsValid(&operation), 1);
    }
    RoboCheckEqInt(PaintTextRunIsValid(&operation.text), 1);
    RoboCheckEqInt(PaintListAppend(list, &operation, NULL), RoboOk);
    RoboCheckEqInt((int)PaintListCount(list), 1);

    /* Glyphs the list does not hold: refused at the door, and the list is untouched. */
    operation.text.glyphs = PaintGlyphSpanAt(0u);
    operation.text.glyphs.count = 4u;
    RoboCheckEqInt(PaintListAppend(list, &operation, NULL), RoboInvalidArgument);
    RoboCheckEqInt((int)PaintListCount(list), 1);
    /* And the pairing itself: glyphs with the `None` token, or no glyphs with a face, are not a record. */
    operation.text.fontToken = PaintFontTokenNone;
    RoboCheckEqInt(PaintTextRunIsValid(&operation.text), 0);
    operation.text.glyphs = PaintGlyphSpanAt(0u);
    operation.text.fontToken = PaintFontTokenPrimary;
    RoboCheckEqInt(PaintTextRunIsValid(&operation.text), 0);

    PaintListDestroy(list);
}

/**
 * The degenerate token, end to end: a hand-built `draw-text` carrying `PaintFontTokenPrimary`.
 *
 * The common case, and the one a caller with a single face uses: the record says "the face you were handed"
 * rather than "entry N of a table", so the same display list draws through the pass's own `face` argument -- and
 * a pass given no face counts the run `noFont` rather than guessing at one. Two frames over one list, which
 * is what makes the sentinel's *meaning* visible: nothing about the list changed between them.
 */
static void TestTextGlyphPrimaryToken(void)
{
    PaintDisplayList *list = NULL;
    PaintDisplayCommand command;
    PaintTextRun run;
    PaintTextSpan textSpan;
    PaintGlyphSpan glyphSpan;
    PaintGlyph glyph;
    PaintDecorationStats decoration;
    TextFace face;
    GfxSurface *surface;

    TextFaceCreate(&face);
    RoboCheckEqInt(PaintDisplayListCreate(&list, NULL), RoboOk);
    RoboCheckEqInt(PaintTextPoolAppend(PaintDisplayListTextPool(list),
                                         (const uint32_t[]){ 'A' }, 1u, &textSpan, NULL),
                      RoboOk);
    PaintGlyphInit(&glyph);
    glyph.glyphId = 1u;
    glyph.sourceCount = 1u;
    glyph.advanceX = (float)((double)TEXT_FACE_ADVANCE_GLYPH * TEXT_FACE_FONT_SIZE /
                             (double)TEXT_FACE_UPEM);
    RoboCheckEqInt(PaintGlyphPoolAppend(PaintDisplayListGlyphPool(list), &glyph, 1u,
                                             &glyphSpan, NULL),
                      RoboOk);

    PaintTextRunInit(&run);
    run.span = textSpan;
    run.baselineOffset = 8;
    run.glyphs = glyphSpan;
    run.fontToken = PaintFontTokenPrimary;
    run.font.fontSizePx = TEXT_FACE_FONT_SIZE;
    RoboCheckEqInt(PaintTextRunIsValid(&run), 1);

    PaintDisplayCommandInit(&command);
    command.kind = RoboPaintDisplayCmdDrawText;
    command.node = (LayoutNodeId)1u;
    command.rect = MakeGfxRect(0, 0, 100, 20);
    command.color = CssColorMake(0xff, 0x00, 0x00, 255);
    command.text = run;
    RoboCheckEqInt(PaintDisplayCommandIsValid(&command), 1);
    RoboCheckEqInt(PaintDisplayListAppend(list, &command, NULL), RoboOk);
    /* The sentinel is not an entry, so the list's own table is empty -- and it does not have to be anything
     * else for the list to draw. */
    RoboCheckEqInt((int)PaintFontTableCount(PaintDisplayListFontTable(list)), 0);

    surface = TextSurface(200u, 60u);
    memset(&decoration, 0, sizeof(decoration));
    TextPaint(list, surface, face.face, &decoration);
    RoboCheckEqInt((int)decoration.texts, 1);
    RoboCheckEqInt((int)decoration.noFont, 0);
    RoboCheckEqInt((int)decoration.glyphs, 1);

    /* And the same list with no face: the run is counted, not drawn. */
    memset(&decoration, 0, sizeof(decoration));
    TextPaint(list, surface, NULL, &decoration);
    RoboCheckEqInt((int)decoration.texts, 1);
    RoboCheckEqInt((int)decoration.noFont, 1);
    RoboCheckEqInt((int)decoration.glyphs, 0);

    GfxSurfaceRelease(surface);
    PaintDisplayListDestroy(list);
    TextFaceDestroy(&face);
}

/** The other legal recording: characters, no face, no glyphs -- and what a drawing pass says about it. */
static void TestTextGlyphRunWithoutAFace(void)
{
    PaintFixtureDoc fixture;
    PaintResult result;
    PaintOperation operation;

    PaintBeginLaidOut(&fixture, "<html><body><div class=\"box\">AB</div></body></html>",
                         TEXT_BASE_CSS
                         " .box { width: 300px; height: 60px; color: #ff0000;"
                         " font-size: 100px }");
    PaintOnce(&fixture, &result);

    operation = TextOnlyOperation(result.list);
    /* The characters are the document's and are there; the glyphs are what the missing face costs, and the
     * record spells that with the **empty span + `None`** pairing rather than with glyphs nobody could draw. */
    RoboCheckEqInt((int)operation.text.span.count, 2);
    RoboCheckEqInt((int)PaintGlyphSpanIsEmpty(PaintTextRunGlyphs(&operation.text)), 1);
    RoboCheckEqInt((int)PaintTextRunFontToken(&operation.text), (int)PaintFontTokenNone);
    RoboCheckEqInt((int)PaintGlyphPoolCount(PaintListGlyphPool(result.list)), 0);
    RoboCheckEqInt((int)PaintFontTableCount(PaintListFontTable(result.list)), 0);

    PaintResultDispose(&result);
    PaintEnd(&fixture);
}

/* ------------------------------------------------------------------ */
/* Registration                                                        */
/* ------------------------------------------------------------------ */

void TestRegisterPaintText(void)
{
    TestRegister("paint.text.record_shape", TestTextRecordShape);
    TestRegister("paint.text.characters_are_code_points",
                       TestTextCharactersAreCodePoints);
    TestRegister("paint.text.command_and_span", TestTextCommandAndSpan);
    TestRegister("paint.text.order_and_depth", TestTextOrderAndDepth);
    TestRegister("paint.text.multiple_runs", TestTextMultipleRuns);
    TestRegister("paint.text.wrapped_lines", TestTextWrappedLines);
    TestRegister("paint.text.color", TestTextColor);
    TestRegister("paint.text.pixels", TestTextPixels);
    TestRegister("paint.text.coverage_antialiasing", TestTextCoverageAntialiasing);
    TestRegister("paint.text.sideways", TestTextSideways);
    TestRegister("paint.text.no_font", TestTextNoFont);
    TestRegister("paint.text.missing_glyph", TestTextMissingGlyph);
    TestRegister("paint.text.rtl", TestTextRtl);
    TestRegister("paint.text.determinism", TestTextDeterminism);
    TestRegister("paint.text.mutation_isolation", TestTextMutationIsolation);
    TestRegister("paint.text.empty_cases", TestTextEmptyCases);
    TestRegister("paint.text.hand_built_rules", TestTextHandBuiltRules);
    TestRegister("paint.text.pool_doors", TestTextPoolDoors);
    TestRegister("paint.text.dump", TestTextDump);
    TestRegister("paint.glyphrun.recorded", TestTextGlyphRunRecorded);
    TestRegister("paint.glyphrun.agrees_with_shaping", TestTextGlyphAgreesWithShaping);
    TestRegister("paint.glyphrun.snapshot", TestTextGlyphSnapshotOutlivesRecords);
    TestRegister("paint.glyphrun.without_a_face", TestTextGlyphRunWithoutAFace);
    TestRegister("paint.glyphrun.pool_doors", TestTextGlyphPoolDoors);
    TestRegister("paint.glyphrun.font_table_doors", TestTextFontTableDoors);
    TestRegister("paint.glyphrun.span_rules", TestTextGlyphSpanRules);
    TestRegister("paint.glyphrun.primary_token", TestTextGlyphPrimaryToken);
}
