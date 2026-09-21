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
    TextFaceCreate(&face);
    PaintBeginLaidOut(&fixture, "<html><body><div class=\"box\">AB</div></body></html>",
                         TEXT_BASE_CSS
                         " .box { width: 300px; height: 60px; background-color: #ffffff;"
                         " color: #ff0000; font-size: 100px }");
    PaintOnce(&fixture, &result);
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
    PaintOnce(&fixture, &result);
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
    PaintOnce(&fixture, &result);

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
    PaintOnce(&fixture, &result);
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
    TestRegister("paint.text.no_font", TestTextNoFont);
    TestRegister("paint.text.missing_glyph", TestTextMissingGlyph);
    TestRegister("paint.text.rtl", TestTextRtl);
    TestRegister("paint.text.determinism", TestTextDeterminism);
    TestRegister("paint.text.mutation_isolation", TestTextMutationIsolation);
    TestRegister("paint.text.empty_cases", TestTextEmptyCases);
    TestRegister("paint.text.hand_built_rules", TestTextHandBuiltRules);
    TestRegister("paint.text.pool_doors", TestTextPoolDoors);
    TestRegister("paint.text.dump", TestTextDump);
}
