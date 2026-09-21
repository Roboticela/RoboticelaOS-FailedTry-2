/*
 * RoboPaint opacity suite (Phase 11.9) — paint.opacity.*
 *
 * The phase's whole claim, in one sentence: **an element's `opacity` is accumulated along the
 * path that reaches it and applied to every drawing that path produces, on top of the colour's
 * own alpha.** Six things make that a claim worth a suite rather than an assertion, and each is a
 * section below:
 *
 *   **the record**       one real in `[0, 1]` whose cleared state is *fully opaque*, not zero.
 *                        A zeroed record that meant "invisible" would make every element of every
 *                        document that never mentions `opacity` disappear, so the direction of the
 *                        default is the first thing pinned;
 *
 *   **the accumulation** a product along a path, not a replacement and not an inheritance.
 *                        `opacity` does not inherit (RoboCSSPropertyRegistry.c argues it), so a
 *                        `0.5` child inside a `0.5` parent is a `0.25` subtree -- and the six-row
 *                        table below is where "the child replaces the parent" would show up as
 *                        four wrong rows;
 *
 *   **the scope**        the pair of commands, and its *placement*, which is the one structural
 *                        difference from 11.8's clip scope: a clip belongs to a box's contents and
 *                        an opacity belongs to the box itself, so the opacity pair opens at the
 *                        box's marker and before its first decoration. Both are the same rule read
 *                        at two points -- a record carries the state in force at its own position
 *                        -- and both halves of a pair carry what the pair establishes, which is
 *                        what makes a pair checkable from the stream;
 *
 *   **the pixels**       what the raster actually receives. The oracle for every pixel case is
 *                        `GfxCompositeSrcOverOpacity` -- the graphics module's own blend
 *                        -- rather than a number this suite computes, so the assertion is "the
 *                        faded pixel is the composite of the opaque one" and cannot drift from the
 *                        arithmetic it is testing. Three drawing kinds are pinned this way: a
 *                        background, a border band and a **text run's glyphs**;
 *
 *   **the refusals**     the ways a record list can lie about a document, each refused by the
 *                        builder. That is what makes the pair load-bearing rather than ceremonial:
 *                        without the checks a push and a pop would be two commands a consumer could
 *                        only skip, and the strength a drawing is drawn at would be a field nothing
 *                        cross-checks;
 *
 *   **the deviation**    one thing this build does **not** do the way CSS says, asserted rather
 *                        than hidden: CSS composites an element's whole subtree as a group and
 *                        applies the opacity to the flattened result, while this build multiplies
 *                        per drawing. `paint.opacity.group_deviation` states the document where
 *                        the two disagree, with both answers computed through the blend door -- so
 *                        the difference is a fact in the tests rather than a surprise in a
 *                        dependency. Group opacity needs an offscreen layer, which is a compositor
 *                        and a later phase's; the record's shape is what a group implementation
 *                        replaces.
 *
 * Chromium is reference only; nothing here is a translation of it, and this suite depends on no
 * Chromium header, library or test framework.
 */

#include "RoboPaintTestSupport.h"

#include "RoboFont/RoboFontTypes.h"
#include "RoboGraphics/RoboGraphicsBitmap.h"
#include "RoboGraphics/RoboGraphicsComposite.h"
#include "RoboGraphics/RoboGraphicsEncode.h"

#include "RoboPaint/RoboPaintColor.h"
#include "RoboPaint/RoboPaintDecoration.h"
#include "RoboPaint/RoboPaintDisplayDump.h"
#include "RoboPaint/RoboPaintOpacity.h"

#include <stdio.h>
#include <string.h>

/* The same base sheet the other paint suites use: these elements are the block containers, so a
 * fixture's boxes stack in flow order. */
#define OPAC_BASE_CSS "html, body, div, section, aside, figure, p { display: block }"

/* The colours a pixel assertion can name. */
#define OPAC_RED GfxRgba8(255, 0, 0, 255)
#define OPAC_BLUE GfxRgba8(0, 0, 255, 255)
#define OPAC_WHITE GfxRgba8(255, 255, 255, 255)
#define OPAC_UNTOUCHED GfxRgba8(0, 0, 0, 0)

/* The byte a half reaches the raster as, and the *same* door the pass uses, so no case restates
 * the rounding. */
#define OPAC_HALF_BYTE ((uint8_t)128)

/* ------------------------------------------------------------------ */
/* A synthetic face, built here                                        */
/* ------------------------------------------------------------------ */

/*
 * The text case needs **one** mapped glyph and nothing else: ink at a position, so that a pixel
 * the opaque run put a colour on can be found and the faded run compared with it. The face is
 * therefore as small as a face can be and still be one -- a cmap, an hmtx, an hhea, a maxp, a loca
 * and one square outline:
 *
 *   units per em   1000    so `font-size: 100px` makes one font unit a tenth of a pixel
 *   glyph 0        empty   `.notdef`
 *   glyph 1        square  a square from (0, 0) to (100, 100) in font units
 *   cmap           'A' -> glyph 1
 *   hmtx           advance 600 units for glyph 1, 500 for glyph 0
 *
 * It is built here rather than shared with the text and clip suites, and the reason is the one
 * those two give for building theirs: a shared fixture would be a third thing to keep in step, and
 * the three suites need three different faces -- the text suite's has two mapped glyphs so a run
 * advances, the clip suite's maps one because a run's advance is not what it measures, and this one
 * maps one because all it needs is a pixel that is red in one run and not in the other.
 */
#define OPAC_FACE_UPEM 1000u
#define OPAC_FACE_ADVANCE_NOTDEF 500u
#define OPAC_FACE_ADVANCE_GLYPH 600u
#define OPAC_FACE_NUM_GLYPHS 2u
#define OPAC_FACE_SQUARE_UNITS 100

typedef struct {
    uint32_t tag;
    const uint8_t *data;
    uint32_t length;
} OpacSynthTable;

static void OpacPutU8(uint8_t **p, uint8_t v) { *(*p)++ = v; }
static void OpacPutU16(uint8_t **p, uint16_t v)
{
    *(*p)++ = (uint8_t)(v >> 8);
    *(*p)++ = (uint8_t)v;
}
static void OpacPutI16(uint8_t **p, int16_t v) { OpacPutU16(p, (uint16_t)v); }
static void OpacPutU32(uint8_t **p, uint32_t v)
{
    *(*p)++ = (uint8_t)(v >> 24);
    *(*p)++ = (uint8_t)(v >> 16);
    *(*p)++ = (uint8_t)(v >> 8);
    *(*p)++ = (uint8_t)v;
}
static void OpacPutI64(uint8_t **p, int64_t v)
{
    int i;

    for (i = 7; i >= 0; i--) {
        OpacPutU8(p, (uint8_t)((uint64_t)v >> (i * 8)));
    }
}

static void OpacWriteSearchFields(uint8_t *hdr, uint16_t numTables)
{
    uint16_t pow2 = 1u;
    uint16_t entry = 0u;
    uint8_t *p;

    while ((uint16_t)(pow2 * 2u) <= numTables) {
        pow2 = (uint16_t)(pow2 * 2u);
        entry++;
    }
    p = hdr + 4;
    OpacPutU16(&p, numTables);
    OpacPutU16(&p, (uint16_t)(pow2 * 16u));
    OpacPutU16(&p, entry);
    OpacPutU16(&p, (uint16_t)(numTables * 16u - pow2 * 16u));
}

static size_t OpacBuildSfnt(uint8_t *out,
                              size_t outCap,
                              uint32_t version,
                              const OpacSynthTable *tables,
                              uint32_t n)
{
    size_t dirEnd = 12u + (size_t)n * 16u;
    size_t cursor;
    size_t need = dirEnd;
    uint32_t i;
    uint8_t *p;

    for (i = 0u; i < n; i++) {
        need += ((size_t)tables[i].length + 3u) & ~(size_t)3u;
    }
    if (need > outCap) {
        return 0u;
    }
    memset(out, 0, need);
    p = out;
    OpacPutU32(&p, version);
    OpacWriteSearchFields(out, (uint16_t)n);
    cursor = dirEnd;
    for (i = 0u; i < n; i++) {
        uint32_t pad = (uint32_t)(((size_t)tables[i].length + 3u) & ~(size_t)3u);
        uint8_t *entry = out + 12u + (size_t)i * 16u;

        OpacPutU32(&entry, tables[i].tag);
        OpacPutU32(&entry, 0u);
        OpacPutU32(&entry, (uint32_t)cursor);
        OpacPutU32(&entry, tables[i].length);
        memcpy(out + cursor, tables[i].data, tables[i].length);
        cursor += pad;
    }
    return cursor;
}

static size_t OpacBuildHead(uint8_t *b, uint16_t upem)
{
    uint8_t *p = b;

    OpacPutU16(&p, 1);
    OpacPutU16(&p, 0);
    OpacPutU32(&p, 0x00010000u);
    OpacPutU32(&p, 0u);
    OpacPutU32(&p, RoboFontHeadMagic);
    OpacPutU16(&p, 0u);
    OpacPutU16(&p, upem);
    OpacPutI64(&p, 0);
    OpacPutI64(&p, 0);
    OpacPutI16(&p, -50);
    OpacPutI16(&p, -200);
    OpacPutI16(&p, 1000);
    OpacPutI16(&p, 800);
    OpacPutU16(&p, 0u);
    OpacPutU16(&p, 8u);
    OpacPutI16(&p, 2);
    OpacPutI16(&p, 1); /* long loca, so loca entries are u32 */
    OpacPutI16(&p, 0);
    return (size_t)(p - b);
}

static size_t OpacBuildHhea(uint8_t *b, uint16_t nHMetrics)
{
    uint8_t *p = b;
    int i;

    OpacPutU16(&p, 1);
    OpacPutU16(&p, 0);
    OpacPutI16(&p, 800);  /* ascender */
    OpacPutI16(&p, -200); /* descender */
    OpacPutI16(&p, 90);   /* line gap */
    OpacPutU16(&p, 900);
    OpacPutI16(&p, -10);
    OpacPutI16(&p, -20);
    OpacPutI16(&p, 880);
    OpacPutI16(&p, 1);
    OpacPutI16(&p, 0);
    OpacPutI16(&p, 0);
    for (i = 0; i < 4; i++) {
        OpacPutI16(&p, 0);
    }
    OpacPutI16(&p, 0);
    OpacPutU16(&p, nHMetrics);
    return (size_t)(p - b);
}

static size_t OpacBuildMaxp(uint8_t *b, uint16_t numGlyphs)
{
    uint8_t *p = b;
    int i;

    OpacPutU32(&p, 0x00010000u);
    OpacPutU16(&p, numGlyphs);
    OpacPutU16(&p, 8);
    OpacPutU16(&p, 2);
    for (i = 0; i < 12; i++) {
        OpacPutU16(&p, 0);
    }
    return (size_t)(p - b);
}

static size_t OpacBuildHmtx(uint8_t *b, const uint16_t *advances, uint16_t n)
{
    uint8_t *p = b;
    uint16_t i;

    for (i = 0u; i < n; i++) {
        OpacPutU16(&p, advances[i]);
        OpacPutI16(&p, (int16_t)(i + 1));
    }
    return (size_t)(p - b);
}

/** `n + 1` long `loca` entries from @p offsets. */
static size_t OpacBuildLoca(uint8_t *loca, const uint32_t *offsets, uint16_t n)
{
    uint8_t *p = loca;
    uint16_t i;

    for (i = 0u; i <= n; i++) {
        OpacPutU32(&p, offsets[i]);
    }
    return (size_t)(p - loca);
}

/** One glyph: a square from (0, 0) to (100, 100) in font units. */
static size_t OpacBuildSquareGlyf(uint8_t *b)
{
    uint8_t *p = b;

    OpacPutI16(&p, 1);                      /* numberOfContours */
    OpacPutI16(&p, 0);                      /* xMin */
    OpacPutI16(&p, 0);                      /* yMin */
    OpacPutI16(&p, OPAC_FACE_SQUARE_UNITS); /* xMax */
    OpacPutI16(&p, OPAC_FACE_SQUARE_UNITS); /* yMax */
    OpacPutU16(&p, 3);                      /* endPtsOfContours[0] */
    OpacPutU16(&p, 0);                      /* instructionLength */
    OpacPutU8(&p, (uint8_t)(RoboFontGlyfFlagOnCurve |
                              RoboFontGlyfFlagXSameOrPositive |
                              RoboFontGlyfFlagYSameOrPositive));
    OpacPutU8(&p, (uint8_t)(RoboFontGlyfFlagOnCurve | RoboFontGlyfFlagXShort |
                              RoboFontGlyfFlagXSameOrPositive |
                              RoboFontGlyfFlagYSameOrPositive));
    OpacPutU8(&p, (uint8_t)(RoboFontGlyfFlagOnCurve | RoboFontGlyfFlagYShort |
                              RoboFontGlyfFlagXSameOrPositive |
                              RoboFontGlyfFlagYSameOrPositive));
    OpacPutU8(&p, (uint8_t)(RoboFontGlyfFlagOnCurve | RoboFontGlyfFlagXShort |
                              RoboFontGlyfFlagYSameOrPositive));
    OpacPutU8(&p, OPAC_FACE_SQUARE_UNITS);
    OpacPutU8(&p, OPAC_FACE_SQUARE_UNITS);
    OpacPutU8(&p, OPAC_FACE_SQUARE_UNITS);
    return (size_t)(p - b);
}

/**
 * A one-subtable cmap holding a format-4 segment table mapping @p first contiguously to glyph 1
 * (delta `1 - first`), with the required `0xFFFF` segment appended.
 */
static size_t OpacBuildCmap(uint8_t *b, uint16_t first, uint16_t last)
{
    uint8_t *p = b;
    uint8_t *lengthPtr;
    uint8_t *subtable = b + 12;
    uint16_t length;

    OpacPutU16(&p, 0);   /* version */
    OpacPutU16(&p, 1);   /* numTables */
    OpacPutU16(&p, 3);   /* Windows */
    OpacPutU16(&p, 1);   /* Unicode BMP */
    OpacPutU32(&p, 12u); /* subtable offset */

    OpacPutU16(&p, 4); /* format */
    lengthPtr = p;
    OpacPutU16(&p, 0);    /* length, patched below */
    OpacPutU16(&p, 0);    /* language */
    OpacPutU16(&p, 4);    /* segCountX2 */
    OpacPutU16(&p, 4);    /* searchRange */
    OpacPutU16(&p, 1);    /* entrySelector */
    OpacPutU16(&p, 0);    /* rangeShift */
    OpacPutU16(&p, last);
    OpacPutU16(&p, 0xFFFFu);
    OpacPutU16(&p, 0); /* reservedPad */
    OpacPutU16(&p, first);
    OpacPutU16(&p, 0xFFFFu);
    OpacPutI16(&p, (int16_t)(1 - (int32_t)first)); /* idDelta[0]: 'A' -> glyph 1 */
    OpacPutI16(&p, 1);                             /* idDelta[1] */
    OpacPutU16(&p, 0);                             /* idRangeOffset[0] */
    OpacPutU16(&p, 0);                             /* idRangeOffset[1] */

    length = (uint16_t)(p - subtable);
    lengthPtr[0] = (uint8_t)(length >> 8);
    lengthPtr[1] = (uint8_t)length;
    return (size_t)(p - b);
}

/** What a face plus its source cost a caller, so a case can release both the way it built them. */
typedef struct opacFace {
    FontSource *source;
    FontFace *face;
} OpacFace;

/** Build the synthetic face, with 'A' -> glyph 1 and glyph 0 (`.notdef`) empty. */
static void OpacFaceCreate(OpacFace *out)
{
    uint8_t head[64], hhea[64], maxp[64], hmtx[64], loca[64], glyf[128], cmap[64];
    uint8_t font[1024];
    static const uint16_t advances[OPAC_FACE_NUM_GLYPHS] = {
        (uint16_t)OPAC_FACE_ADVANCE_NOTDEF,
        (uint16_t)OPAC_FACE_ADVANCE_GLYPH
    };
    OpacSynthTable tables[7];
    uint32_t nt = 0u;
    size_t glyphSize;
    size_t n;

    out->source = NULL;
    out->face = NULL;

    glyphSize = OpacBuildSquareGlyf(glyf);
    {
        const uint32_t offsets[OPAC_FACE_NUM_GLYPHS + 1u] = { 0u, 0u, (uint32_t)glyphSize };

        (void)OpacBuildLoca(loca, offsets, (uint16_t)OPAC_FACE_NUM_GLYPHS);
    }

    tables[nt].tag = RoboFontTagHead;
    tables[nt].data = head;
    tables[nt].length = (uint32_t)OpacBuildHead(head, (uint16_t)OPAC_FACE_UPEM);
    nt++;
    tables[nt].tag = RoboFontTagHhea;
    tables[nt].data = hhea;
    tables[nt].length = (uint32_t)OpacBuildHhea(hhea, (uint16_t)OPAC_FACE_NUM_GLYPHS);
    nt++;
    tables[nt].tag = RoboFontTagMaxp;
    tables[nt].data = maxp;
    tables[nt].length = (uint32_t)OpacBuildMaxp(maxp, (uint16_t)OPAC_FACE_NUM_GLYPHS);
    nt++;
    tables[nt].tag = RoboFontTagHmtx;
    tables[nt].data = hmtx;
    tables[nt].length = (uint32_t)OpacBuildHmtx(hmtx, advances, (uint16_t)OPAC_FACE_NUM_GLYPHS);
    nt++;
    tables[nt].tag = RoboFontTagCmap;
    tables[nt].data = cmap;
    tables[nt].length = (uint32_t)OpacBuildCmap(cmap, 0x41u, 0x41u);
    nt++;
    tables[nt].tag = RoboFontTagLoca;
    tables[nt].data = loca;
    tables[nt].length = (uint32_t)((OPAC_FACE_NUM_GLYPHS + 1u) * 4u);
    nt++;
    tables[nt].tag = RoboFontTagGlyf;
    tables[nt].data = glyf;
    tables[nt].length = (uint32_t)glyphSize;
    nt++;

    n = OpacBuildSfnt(font, sizeof(font), RoboFontSfntVersionTrue, tables, nt);
    RoboCheck(n > 0u);
    RoboCheckEqInt(FontSourceCreateMemoryCopy(font, n, &out->source), RoboOk);
    RoboCheckEqInt(FontFaceCreate(out->source, 0, &out->face), RoboOk);
}

static void OpacFaceDestroy(OpacFace *face)
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

static LayoutRect OpacRect(int32_t x, int32_t y, int32_t w, int32_t h)
{
    LayoutRect rect;

    rect.x = x;
    rect.y = y;
    rect.width = w;
    rect.height = h;
    return rect;
}

/** A record holding @p strength, spelled the way a case reads it. */
static PaintOpacity OpacOf(double strength)
{
    return PaintOpacityFromValue(strength);
}

static void OpacCheckInt(int actual, int expected, const char *what)
{
    if (actual != expected) {
        TestFail(__FILE__, __LINE__, "%s: expected %d, got %d", what, expected, actual);
    }
}

/** Assert a strength, printing both values' own decimal forms. */
static void OpacCheckValue(double actual, double expected, const char *what)
{
    if (!(actual == expected)) {
        TestFail(__FILE__, __LINE__, "%s: expected %.17g, got %.17g", what, expected,
                       actual);
    }
}

static void OpacCheckRecord(const PaintOpacity *record,
                              double expected,
                              const char *what)
{
    OpacCheckValue(PaintOpacityValue(record), expected, what);
}

/** A CPU surface for a paint pass: owned, RGBA8, zero-filled (so "untouched" is transparent and
 *  an unpainted pixel is distinguishable from a black one). */
static GfxSurface *OpacSurface(uint32_t width,
                                        uint32_t height,
                                        GfxSurfaceAccess access)
{
    GfxSurfaceDesc desc;
    GfxSurface *surface = NULL;

    GfxSurfaceDescInit(&desc);
    desc.width = width;
    desc.height = height;
    desc.format = RoboGfxFormatRgba8;
    desc.access = access;
    RoboCheckEqInt(GfxSurfaceCreate(&desc, &surface), RoboOk);
    RoboCheck(surface != NULL);
    return surface;
}

/**
 * Build a display list from a record list, asserting the build succeeds.
 *
 * The stats are **zeroed before the call**, so a refused build (which leaves the output struct
 * untouched) shows up as zeroes and the message above it names the refusal, rather than a case
 * reporting whatever the stack happened to hold.
 */
static void OpacBuild(const PaintList *records,
                       PaintDisplayList **outList,
                       PaintDisplayListStats *outStats)
{
    PaintDisplayListBuilder *builder = NULL;
    PaintDiag diag;
    Result status;

    if (outStats) {
        memset(outStats, 0, sizeof *outStats);
    }
    RoboCheckEqInt(PaintDisplayListCreate(outList, NULL), RoboOk);
    RoboCheckEqInt(PaintDisplayListBuilderCreate(&builder, NULL), RoboOk);
    PaintDiagReset(&diag);
    status = PaintDisplayListBuilderBuild(builder, records, *outList, outStats, &diag);
    if (status != RoboOk) {
        TestFail(__FILE__, __LINE__, "the display build refused the records: %s (line %d)",
                       diag.message ? diag.message : "(no message)", diag.line);
    }
    PaintDisplayListBuilderDestroy(builder);
}

/**
 * Build a display list, reporting the outcome rather than asserting it.
 *
 * The refusal cases need a build that is *expected* to fail, so the status and the message come
 * back to the caller -- and the list is handed back **emptied**, which is itself one of the
 * claims.
 */
static Result OpacBuildExpectRefusal(const PaintList *records,
                                               PaintDisplayList **outList,
                                               PaintDiag *outDiag)
{
    PaintDisplayListBuilder *builder = NULL;
    Result status;

    RoboCheckEqInt(PaintDisplayListCreate(outList, NULL), RoboOk);
    RoboCheckEqInt(PaintDisplayListBuilderCreate(&builder, NULL), RoboOk);
    PaintDiagReset(outDiag);
    status = PaintDisplayListBuilderBuild(builder, records, *outList, NULL, outDiag);
    PaintDisplayListBuilderDestroy(builder);
    return status;
}

/** Paint @p list into @p target, asserting the pass succeeds. */
static void OpacPaint(const PaintDisplayList *list,
                       GfxSurface *target,
                       FontFace *face,
                       PaintDecorationStats *outStats)
{
    PaintDiag diag;
    Result status;

    PaintDiagReset(&diag);
    status = PaintDecorationPaint(list, target, face, outStats, &diag);
    if (status != RoboOk) {
        TestFail(__FILE__, __LINE__, "the paint pass refused the list: %s (%s:%d)",
                       diag.message ? diag.message : "(no message)",
                       diag.file ? diag.file : "?", diag.line);
    }
}

/** Read the pixel at (x, y), failing when the point is outside the surface. */
static GfxColorRgba8 OpacRead(GfxSurface *surface, int32_t x, int32_t y)
{
    GfxColorRgba8 color;
    int32_t width = (int32_t)GfxSurfaceWidth(surface);
    int32_t height = (int32_t)GfxSurfaceHeight(surface);

    memset(&color, 0, sizeof(color));
    if (x < 0 || y < 0 || x >= width || y >= height) {
        TestFail(__FILE__, __LINE__, "the sample %d,%d is outside the %dx%d surface",
                       (int)x, (int)y, (int)width, (int)height);
        return color;
    }
    RoboCheckEqInt(GfxSurfaceGetPixel(surface, (uint32_t)x, (uint32_t)y, &color),
                      RoboOk);
    return color;
}

static void OpacPixelIs(GfxSurface *surface,
                          int32_t x,
                          int32_t y,
                          GfxColorRgba8 expected,
                          const char *what)
{
    GfxColorRgba8 actual = OpacRead(surface, x, y);

    if (actual.r != expected.r || actual.g != expected.g || actual.b != expected.b ||
        actual.a != expected.a) {
        TestFail(__FILE__, __LINE__,
                       "%s: at %d,%d expected %d,%d,%d,%d, got %d,%d,%d,%d", what, (int)x,
                       (int)y, (int)expected.r, (int)expected.g, (int)expected.b,
                       (int)expected.a, (int)actual.r, (int)actual.g, (int)actual.b,
                       (int)actual.a);
    }
}

/** Non-zero when any pixel in the half-open rectangle [x0,x1) x [y0,y1) is not transparent. */
static int OpacAnyInk(GfxSurface *surface,
                        int32_t x0,
                        int32_t y0,
                        int32_t x1,
                        int32_t y1)
{
    int32_t x;
    int32_t y;

    for (y = y0; y < y1; y++) {
        for (x = x0; x < x1; x++) {
            if (OpacRead(surface, x, y).a != 0u) {
                return 1;
            }
        }
    }
    return 0;
}

/** The first inked pixel of the rectangle [x0,x1) x [y0,y1), or 0 when there is none. */
static int OpacFirstInk(GfxSurface *surface,
                          int32_t x0,
                          int32_t y0,
                          int32_t x1,
                          int32_t y1,
                          int32_t *outX,
                          int32_t *outY)
{
    int32_t x;
    int32_t y;

    for (y = y0; y < y1; y++) {
        for (x = x0; x < x1; x++) {
            if (OpacRead(surface, x, y).a != 0u) {
                if (outX) {
                    *outX = x;
                }
                if (outY) {
                    *outY = y;
                }
                return 1;
            }
        }
    }
    return 0;
}

/**
 * The colour @p opaque becomes when the *same* drawing is put down at @p strength over @p under.
 *
 * The oracle is `GfxCompositeSrcOverOpacity` -- the **graphics module's own blend** --
 * rather than a number this suite computes, so a case asserts a *relationship* ("the faded pixel
 * is the composite of the opaque one") that cannot drift from the arithmetic it is testing. The
 * destination is passed in because a colour over white and the same colour over nothing are
 * different pictures, and it is the fixture's business which one it is.
 */
static GfxColorRgba8 OpacExpected(GfxColorRgba8 opaque,
                                            GfxColorRgba8 under,
                                            double strength)
{
    PaintOpacity record = OpacOf(strength);

    return GfxCompositeSrcOverOpacity(opaque, under,
                                               PaintOpacityToByte(&record));
}

/** The @p nth command of @p list whose kind is @p kind, or 0 when there is none. */
static int OpacCommandOfKind(const PaintDisplayList *list,
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

/** How many commands of @p kind @p list holds. */
static size_t OpacCountOfKind(const PaintDisplayList *list,
                                 PaintDisplayCommandKind kind)
{
    return PaintDisplayListCountOfKind(list, kind);
}

/** The index of the @p nth command of @p kind whose node is @p node, or `(size_t)-1`. */
static size_t OpacIndexOfKindForNode(const PaintDisplayList *list,
                                          PaintDisplayCommandKind kind,
                                          LayoutNodeId node,
                                          size_t nth)
{
    size_t index;
    size_t seen = 0u;

    for (index = 0u; index < PaintDisplayListCount(list); index++) {
        PaintDisplayCommand command;

        if (PaintDisplayListAt(list, index, &command) != RoboOk) {
            break;
        }
        if (command.kind != kind || command.node != node) {
            continue;
        }
        if (seen == nth) {
            return index;
        }
        seen++;
    }
    return (size_t)-1;
}

/** The index of the @p nth record of @p kind whose node is @p node, or `(size_t)-1`. */
static size_t OpacOpIndexForNode(const PaintResult *result,
                                     PaintOpKind kind,
                                     LayoutNodeId node,
                                     size_t nth)
{
    size_t index;
    size_t seen = 0u;

    for (index = 0u; index < PaintListCount(result->list); index++) {
        PaintOperation operation;

        if (PaintListAt(result->list, index, &operation) != RoboOk) {
            break;
        }
        if (operation.kind != kind || operation.node != node) {
            continue;
        }
        if (seen == nth) {
            return index;
        }
        seen++;
    }
    return (size_t)-1;
}

/** The record at @p index, asserted to be in range. */
static PaintOperation OpacOpAt(const PaintResult *result, size_t index)
{
    PaintOperation operation;

    PaintOperationInit(&operation);
    if (index == (size_t)-1) {
        TestFail(__FILE__, __LINE__, "the record index is not in the list");
        return operation;
    }
    RoboCheckEqInt((int)PaintListAt(result->list, index, &operation), (int)RoboOk);
    return operation;
}

/** Assert the record at @p index in @p result, which is an lvalue a caller can address. */
static void OpacCheckRecordAt(const PaintResult *result,
                                 size_t index,
                                 double expected,
                                 const char *what)
{
    PaintOperation operation = OpacOpAt(result, index);

    OpacCheckRecord(&operation.opacity, expected, what);
}

/** The @p nth record of @p kind in @p result, asserted to exist. */
static PaintOperation OpacOpOfKind(const PaintResult *result,
                                              PaintOpKind kind,
                                              size_t nth)
{
    PaintOperation operation;

    PaintOperationInit(&operation);
    RoboCheckEqInt(PaintNthOfKind(result, kind, nth, &operation), 1);
    return operation;
}

/**
 * Every record of @p result that stands **inside** @p node's own scope, checked against
 * @p expected.
 *
 * The box's two **markers** are skipped, and that is the point rather than a detail: a scope
 * opens after the marker and closes before the end marker, so the markers carry the strength of
 * the box that *contains* it. A helper that included them would be asserting the enclosing
 * strength and calling it the box's, which is a different (and wrong) claim -- and
 * `paint.opacity.scope_placement` is where the markers' own strengths are pinned.
 */
static void OpacNodeIs(const PaintResult *result,
                         LayoutNodeId node,
                         double expected,
                         const char *what)
{
    size_t index;
    int seen = 0;

    for (index = 0u; index < PaintListCount(result->list); index++) {
        PaintOperation operation;

        if (PaintListAt(result->list, index, &operation) != RoboOk) {
            break;
        }
        if (operation.node != node) {
            continue;
        }
        if (operation.kind == RoboPaintOpBeginBox ||
            operation.kind == RoboPaintOpEndBox) {
            continue;
        }
        OpacCheckRecord(&operation.opacity, expected, what);
        seen++;
    }
    RoboCheck(seen > 0);
}

/** Every record of @p result, checked against @p expected -- the whole list. */
static void OpacListIs(const PaintResult *result, double expected, const char *what)
{
    size_t index;

    for (index = 0u; index < PaintListCount(result->list); index++) {
        PaintOperation operation;

        if (PaintListAt(result->list, index, &operation) != RoboOk) {
            break;
        }
        OpacCheckRecord(&operation.opacity, expected, what);
    }
}

/** In layout coordinates: a box with no insets, so its three rectangles are one. */
static PaintBoxes OpacBoxes(int32_t x, int32_t y, int32_t w, int32_t h)
{
    PaintBoxes boxes;

    PaintBoxesInit(&boxes);
    boxes.border = OpacRect(x, y, w, h);
    boxes.padding = boxes.border;
    boxes.content = boxes.border;
    boxes.insetsMeasured = 0;
    return boxes;
}

/**
 * Append one hand-built **record** to @p list, asserting the append succeeds.
 *
 * The colour and the style follow the kind exactly as the clip suite's helper does -- a drawing
 * carries a colour the record layer accepts, a structural record carries `none` -- while the
 * *strength* is a parameter that deliberately does not follow the kind: an opacity outside
 * `[0, 1]` is refused on every kind, so a case can put one on a marker and see it refused.
 */
static void OpacAppendRecord(PaintList *list,
                               PaintOpKind kind,
                               LayoutNodeId node,
                               size_t depth,
                               PaintBoxes boxes,
                               PaintOpacity opacity)
{
    PaintOperation operation;
    PaintDiag diag;

    PaintOperationInit(&operation);
    operation.kind = kind;
    operation.node = node;
    operation.depth = depth;
    operation.boxes = boxes;
    operation.origin = MakeGfxPoint(0, 0);
    operation.opacity = opacity;
    operation.color = PaintOpKindDraws(kind)
                              ? CssColorMake(255u, 0u, 0u, 255u)
                              : CssColorNone();
    operation.style = PaintOpKindIsBorder(kind) ? RoboCssBorderStyleSolid
                                                         : RoboCssBorderStyleNone;
    /*
     * The refusal the list would give is one sentence about four different rules; this says which
     * of them a case has tripped, which is the difference between a failing case that names its own
     * bug and one that sends a reader to `PaintOperationIsValid`.
     */
    if (!PaintOperationIsValid(&operation)) {
        TestFail(__FILE__, __LINE__,
                       "a hand-built record is not a record: kind %d node %d known=%d boxes=%d "
                       "clip=%d drawable=%d text-is-a-run=%d colour-none=%d style-none=%d "
                       "opacity-valid=%d opacity-opaque=%d",
                       (int)kind, (int)node, PaintOpKindIsKnown(kind),
                       PaintBoxesIsValid(&operation.boxes),
                       PaintClipIsValid(&operation.clip),
                       CssColorIsValid(operation.color),
                       PaintTextRunIsValid(&operation.text),
                       (int)(operation.color.kind == RoboCssColorNone),
                       (int)(operation.style == RoboCssBorderStyleNone),
                       PaintOpacityIsValid(&operation.opacity),
                       PaintOpacityIsOpaque(&operation.opacity));
    }
    PaintDiagReset(&diag);
    if (PaintListAppend(list, &operation, &diag) != RoboOk) {
        TestFail(__FILE__, __LINE__, "a hand-built record was refused: kind %d node %d: %s",
                       (int)kind, (int)node,
                       diag.message ? diag.message : "(no message)");
    }
}

/** An empty record list, asserted to have been created. */
static PaintList *OpacEmptyBox(void)
{
    PaintList *records = NULL;

    RoboCheckEqInt(PaintListCreate(&records, NULL), RoboOk);
    return records;
}

/**
 * A fresh record list holding one open box with a **decoration already drawn**: the first of the
 * two states an opacity push may not follow.
 */
static PaintList *OpacPaintedBox(LayoutNodeId node)
{
    PaintList *records = OpacEmptyBox();

    OpacAppendRecord(records, RoboPaintOpBeginBox, node, 0u, OpacBoxes(0, 0, 10, 10),
                       PaintOpacityOpaque());
    OpacAppendRecord(records, RoboPaintOpBackground, node, 0u, OpacBoxes(0, 0, 10, 10),
                       PaintOpacityOpaque());
    return records;
}

/**
 * A fresh record list holding one open box **past its content boundary**: the second state an
 * opacity push may not follow.
 *
 * The caller destroys it, which is what lets every refusal be its own list: a list cannot lose
 * its last record, and a case that edited one shared list would be testing the previous case's
 * leftovers.
 */
static PaintList *OpacOpenBox(LayoutNodeId node)
{
    PaintList *records = OpacPaintedBox(node);

    OpacAppendRecord(records, RoboPaintOpContent, node, 0u, OpacBoxes(0, 0, 10, 10),
                       PaintOpacityOpaque());
    return records;
}

/**
 * A fresh record list holding one open box with its own opacity scope already open -- marker,
 * push, background, content boundary -- ready for its pop and its end marker.
 *
 * The **order** is the whole of what this helper pins: the push stands immediately after the
 * marker and before the box's first decoration, which is where the traversal emits it and what
 * the builder's "an opacity scope opens after the box it belongs to has begun drawing" refusal
 * is about.
 */
static PaintList *OpacScopedBox(LayoutNodeId node, double strength)
{
    PaintList *records = OpacEmptyBox();
    PaintOpacity record = OpacOf(strength);

    OpacAppendRecord(records, RoboPaintOpBeginBox, node, 0u, OpacBoxes(0, 0, 10, 10),
                       PaintOpacityOpaque());
    OpacAppendRecord(records, RoboPaintOpPushOpacity, node, 0u, OpacBoxes(0, 0, 10, 10),
                       record);
    OpacAppendRecord(records, RoboPaintOpBackground, node, 0u, OpacBoxes(0, 0, 10, 10),
                       record);
    OpacAppendRecord(records, RoboPaintOpContent, node, 0u, OpacBoxes(0, 0, 10, 10),
                       record);
    return records;
}

/** The pop that closes @p node's scope at @p strength, appended to @p records. */
static void OpacPopBox(PaintList *records,
                         LayoutNodeId node,
                         double strength)
{
    OpacAppendRecord(records, RoboPaintOpPopOpacity, node, 0u, OpacBoxes(0, 0, 10, 10),
                       OpacOf(strength));
}

/** Close @p records' open box, so a case can then add something after it. */
static void OpacCloseBox(PaintList *records, LayoutNodeId node)
{
    OpacAppendRecord(records, RoboPaintOpEndBox, node, 0u, OpacBoxes(0, 0, 10, 10),
                       PaintOpacityOpaque());
}

/**
 * Walk every **drawing** command of @p list that belongs to @p node and check its strength.
 *
 * The display-list twin of `OpacNodeIs`, and the narrow one is what a nested document needs:
 * "every drawing in the list" is a different claim from "every drawing of this box", and in a
 * doubly faded subtree the outer box's own background is at the outer strength while its child's
 * is at the product -- so a helper that lumped them would have to assert a number that is not
 * any of the strengths in the document.
 */
static void OpacDrawsOfAre(const PaintDisplayList *list,
                              LayoutNodeId node,
                              double expected,
                              const char *what)
{
    size_t index;
    int seen = 0;

    for (index = 0u; index < PaintDisplayListCount(list); index++) {
        PaintDisplayCommand command;

        if (PaintDisplayListAt(list, index, &command) != RoboOk) {
            break;
        }
        if (!PaintDisplayCommandKindDraws(command.kind) || command.node != node) {
            continue;
        }
        OpacCheckRecord(&command.opacity, expected, what);
        seen++;
    }
    RoboCheck(seen > 0);
}

/**
 * Walk every **drawing** command of @p list and check its strength is @p expected.
 *
 * Drawing only, because a structural command's strength is the state at *its* position rather
 * than at the box that owns it: a box's `end-node` stands after its own scope has closed and
 * therefore carries the enclosing strength, which is a fact about the pair's placement rather than
 * about the scope.
 */
static void OpacDrawsAre(const PaintDisplayList *list,
                           double expected,
                           const char *what)
{
    size_t index;
    int seen = 0;

    for (index = 0u; index < PaintDisplayListCount(list); index++) {
        PaintDisplayCommand command;

        if (PaintDisplayListAt(list, index, &command) != RoboOk) {
            break;
        }
        if (!PaintDisplayCommandKindDraws(command.kind)) {
            continue;
        }
        OpacCheckRecord(&command.opacity, expected, what);
        seen++;
    }
    RoboCheck(seen > 0);
}

/* ------------------------------------------------------------------ */
/* 1. The record                                                       */
/* ------------------------------------------------------------------ */

static void TestOpacityRecord(void)
{
    PaintOpacity record;
    volatile double zero = 0.0;

    PaintFixture();

    /*
     * The cleared state, and the direction that is the whole reason this type has a door for it: a
     * record nothing asked about is **fully opaque**, so a document that never mentions `opacity`
     * draws exactly as it did before the property existed. A zeroed record would mean invisible,
     * and every element of every such document would disappear.
     */
    memset(&record, 0x5a, sizeof(record));
    PaintOpacityInit(&record);
    OpacCheckValue(record.value, 1.0, "the cleared record's value");
    OpacCheckInt(PaintOpacityIsValid(&record), 1, "a cleared record is valid");
    OpacCheckInt(PaintOpacityIsOpaque(&record), 1, "a cleared record is opaque");
    OpacCheckInt(PaintOpacityIsTransparent(&record), 0,
                   "a cleared record is not transparent");
    OpacCheckInt(PaintOpacityEqual(&record, &(PaintOpacity){ 1.0 }), 1,
                   "a cleared record equals `1`");
    {
        PaintOpacity byValue = PaintOpacityOpaque();

        OpacCheckInt(PaintOpacityEqual(&byValue, &record), 1,
                       "the by-value door agrees with init");
    }

    /* NULL is not a record, but every question about *the state* has an answer for it -- and two
     * of those answers point in opposite directions, which is the pair worth pinning: nothing in
     * force is full strength, and it is emphatically not a transparent scope. */
    OpacCheckValue(PaintOpacityValue(NULL), 1.0, "the value of a NULL");
    OpacCheckInt(PaintOpacityIsValid(NULL), 0, "NULL is not a record");
    OpacCheckInt(PaintOpacityIsOpaque(NULL), 1, "nothing in force is opaque");
    OpacCheckInt(PaintOpacityIsTransparent(NULL), 0,
                   "nothing in force is not a transparent scope");

    /* The construction door clamps rather than refusing: a declaration that arrived out of range
     * was already clamped where it was computed, so what this protects is a caller's arithmetic
     * between boxes. */
    OpacCheckValue(OpacOf(0.0).value, 0.0, "zero");
    OpacCheckValue(OpacOf(0.25).value, 0.25, "a quarter");
    OpacCheckValue(OpacOf(2.0).value, 1.0, "above one clamps to one");
    OpacCheckValue(OpacOf(-3.0).value, 0.0, "below zero clamps to zero");
    {
        /* A NaN becomes the **opaque** record, which is the direction `RoboCSSOpacity.h` argues:
         * a bug in a caller's arithmetic must render the way "nothing was declared" does, which is
         * visible, rather than the way "invisible" does, which is not. */
        PaintOpacity nanRecord = OpacOf(zero / zero);

        OpacCheckInt(PaintOpacityIsValid(&nanRecord), 1, "a NaN makes a record");
        OpacCheckInt(PaintOpacityIsOpaque(&nanRecord), 1, "a NaN makes an opaque one");
    }

    /* The predicates at their boundaries, which is where a table of them is worth having. */
    {
        PaintOpacity zeroRecord = OpacOf(0.0);
        PaintOpacity half = OpacOf(0.5);

        OpacCheckInt(PaintOpacityIsValid(&zeroRecord), 1, "zero is a record");
        OpacCheckInt(PaintOpacityIsTransparent(&zeroRecord), 1, "zero is transparent");
        OpacCheckInt(PaintOpacityIsOpaque(&zeroRecord), 0, "zero is not opaque");
        OpacCheckInt(PaintOpacityIsTransparent(&half), 0, "a half is not transparent");
        OpacCheckInt(PaintOpacityIsOpaque(&half), 0, "a half is not opaque");
    }

    /* Equality is the number **exactly**, and this case says so: a tolerance here would turn a
     * determinism failure into a pass, which is the one thing a comparison in this module must not
     * do. `0.1 + 0.2` is not `0.3` in binary floating point, so two records built from them are not
     * equal -- and neither are two records a millionth apart. */
    OpacCheckInt(PaintOpacityEqual(&(PaintOpacity){ 0.5 },
                                            &(PaintOpacity){ 0.5 }),
                   1, "two halves are equal");
    OpacCheckInt(PaintOpacityEqual(&(PaintOpacity){ 0.5 },
                                            &(PaintOpacity){ 0.25 }),
                   0, "a half and a quarter are not");
    {
        PaintOpacity inexact = OpacOf(0.1 + 0.2);
        PaintOpacity exact = OpacOf(0.3);

        OpacCheckInt(PaintOpacityEqual(&inexact, &exact), 0,
                       "and there is no tolerance: 0.1 + 0.2 is not 0.3");
    }
    OpacCheckInt(PaintOpacityEqual(NULL, NULL), 1, "two NULLs are equal");
    OpacCheckInt(PaintOpacityEqual(NULL, &(PaintOpacity){ 1.0 }), 0,
                   "a NULL is not a record");
}

static void TestOpacityRecordSafety(void)
{
    size_t row;

    PaintFixture();

    /*
     * The accumulation, over the whole table the brief names -- and the table is the point:
     * "the child replaces the parent" would give four wrong rows out of six, and an
     * implementation that *averaged* would give all six wrong.
     */
    {
        static const double table[6][3] = {
            { 1.0, 1.0, 1.0 },  { 1.0, 0.5, 0.5 }, { 0.5, 1.0, 0.5 },
            { 0.5, 0.5, 0.25 }, { 0.0, 0.5, 0.0 }, { 0.5, 0.0, 0.0 }
        };

        for (row = 0u; row < 6u; row++) {
            PaintOpacity outer = OpacOf(table[row][0]);
            PaintOpacity inner = OpacOf(table[row][1]);
            PaintOpacity product = PaintOpacityMultiply(&outer, &inner);

            OpacCheckValue(product.value, table[row][2], "the nested product table");
        }
    }

    /* A NULL operand is read as **opaque** (nothing in force), so the call is safe on a
     * destructively cleared pointer -- and the answer is the other operand's strength, whichever
     * side went missing. */
    {
        PaintOpacity outer = OpacOf(0.5);

        OpacCheckValue(PaintOpacityMultiply(NULL, &outer).value, 0.5,
                         "a NULL outer leaves the inner strength");
        OpacCheckValue(PaintOpacityMultiply(&outer, NULL).value, 0.5,
                         "a NULL inner leaves the outer strength");
        OpacCheckValue(PaintOpacityMultiply(NULL, NULL).value, 1.0,
                         "two NULLs are fully opaque");
    }

    /* And the product of two records is a record, always: the clamp cannot fire for two records
     * this build produced, and that is exactly why the *result* is worth asserting rather than the
     * arithmetic. */
    {
        PaintOpacity product = PaintOpacityMultiply(&(PaintOpacity){ 0.0 },
                                                                  &(PaintOpacity){ 0.0 });

        OpacCheckInt(PaintOpacityIsValid(&product), 1, "the product is a record");
        OpacCheckValue(product.value, 0.0, "zero times zero is zero");
    }
}

static void TestOpacityByteConversion(void)
{
    PaintFixture();

    /*
     * The one conversion between this module's `[0, 1]` and the raster's global opacity, and the
     * rounding is the decision: **to nearest**, so `0.5` becomes `128` rather than `127`. The
     * table states the arithmetic once; the pass's own cases then pin it at a pixel, and the
     * `255` case is what makes a fully opaque drawing one a raster nothing has changed produces.
     */
    OpacCheckInt(PaintOpacityToByte(&(PaintOpacity){ 0.0 }), 0,
                   "zero is invisible");
    OpacCheckInt(PaintOpacityToByte(&(PaintOpacity){ 0.25 }), 64,
                   "a quarter rounds to 64");
    OpacCheckInt(PaintOpacityToByte(&(PaintOpacity){ 0.5 }), OPAC_HALF_BYTE,
                   "a half rounds to 128");
    OpacCheckInt(PaintOpacityToByte(&(PaintOpacity){ 0.75 }), 191,
                   "three quarters round to 191");
    OpacCheckInt(PaintOpacityToByte(&(PaintOpacity){ 1.0 }), 255,
                   "one is exactly the raster's own default");

    /* The safe direction for a converter: a value nothing can read must not put ink on a surface,
     * and answering `255` would turn a caller's arithmetic bug into a fully drawn document. */
    OpacCheckInt(PaintOpacityToByte(NULL), 0, "a NULL converts to invisible");
    OpacCheckInt(PaintOpacityToByte(&(PaintOpacity){ 2.0 }), 0,
                   "a record outside the range converts to invisible");
}

/* ------------------------------------------------------------------ */
/* 2. The stack                                                        */
/* ------------------------------------------------------------------ */

static void TestOpacityStack(void)
{
    PaintOpacityStack *stack = NULL;
    PaintOpacity current;
    PaintOpacity half = OpacOf(0.5);
    PaintOpacity quarter = OpacOf(0.25);
    size_t baseline = PaintLiveBlocks();

    PaintFixture();

    RoboCheckEqInt((int)PaintOpacityStackCreate(NULL, NULL),
                      (int)RoboInvalidArgument);
    RoboCheckEqInt((int)PaintOpacityStackCreate(&stack, NULL), (int)RoboOk);
    RoboCheck(stack != NULL);
    RoboCheckEqInt((int)PaintOpacityStackDepth(stack), 0);
    RoboCheckEqInt((int)PaintOpacityStackCapacity(stack), 0);
    OpacCheckInt(PaintOpacityStackIsEmpty(stack), 1, "a fresh stack is empty");

    /* An empty stack answers the **opaque** record: "nothing is in force" is the ordinary state of
     * a document that declares no opacity, not a miss -- and a NULL stack answers it too, so a
     * caller may read a destructively cleared pointer without a branch. */
    memset(&current, 0x5a, sizeof(current));
    PaintOpacityStackCurrent(stack, &current);
    OpacCheckValue(current.value, 1.0, "an empty stack answers full strength");
    memset(&current, 0x5a, sizeof(current));
    PaintOpacityStackCurrent(NULL, &current);
    OpacCheckValue(current.value, 1.0, "a NULL stack answers full strength");
    PaintOpacityStackCurrent(stack, NULL);

    /* A push, a read and a pop: the innermost scope is what is in force, and the pair returns the
     * stack to exactly where it was. */
    RoboCheckEqInt((int)PaintOpacityStackPush(stack, &half, NULL), (int)RoboOk);
    RoboCheckEqInt((int)PaintOpacityStackDepth(stack), 1);
    PaintOpacityStackCurrent(stack, &current);
    OpacCheckValue(current.value, 0.5, "the pushed record is in force");
    RoboCheckEqInt((int)PaintOpacityStackPush(stack, &quarter, NULL), (int)RoboOk);
    PaintOpacityStackCurrent(stack, &current);
    OpacCheckValue(current.value, 0.25, "the innermost record is in force");
    RoboCheckEqInt((int)PaintOpacityStackPop(stack, NULL), (int)RoboOk);
    PaintOpacityStackCurrent(stack, &current);
    OpacCheckValue(current.value, 0.5, "popping restores the enclosing strength");
    RoboCheckEqInt((int)PaintOpacityStackPop(stack, NULL), (int)RoboOk);
    OpacCheckInt(PaintOpacityStackIsEmpty(stack), 1, "both scopes closed");

    /* A pop with nothing to pop is a **refusal**, not a silent no-op: answering OK would let a
     * consumer that lost track of its own pushes go on drawing at a strength that is not in force.
     * The refusal is safe -- the stack is untouched. */
    RoboCheckEqInt((int)PaintOpacityStackPop(stack, NULL), (int)RoboNotFound);
    RoboCheckEqInt((int)PaintOpacityStackDepth(stack), 0);
    RoboCheckEqInt((int)PaintOpacityStackPop(NULL, NULL), (int)RoboInvalidArgument);

    /* The argument checks: a NULL stack and a NULL record are both argument errors, and neither
     * pushes anything. */
    RoboCheckEqInt((int)PaintOpacityStackPush(NULL, &half, NULL),
                      (int)RoboInvalidArgument);
    RoboCheckEqInt((int)PaintOpacityStackPush(stack, NULL, NULL),
                      (int)RoboInvalidArgument);
    RoboCheckEqInt((int)PaintOpacityStackDepth(stack), 0);

    /* A transparent record is a legal push: a scope that draws nothing is a fact about a document,
     * and the container has no opinion about values. The *command* vocabulary refuses an opaque
     * scope, because a scope that establishes nothing is a command a consumer could only skip --
     * a different rule, at a different layer, and this pair of assertions is where they are
     * distinguished. */
    RoboCheckEqInt((int)PaintOpacityStackPush(stack, &(PaintOpacity){ 0.0 },
                                                         NULL),
                      (int)RoboOk);
    PaintOpacityStackCurrent(stack, &current);
    OpacCheckInt(PaintOpacityIsTransparent(&current), 1, "a zero scope is in force");
    {
        PaintOpacity opaque = PaintOpacityOpaque();

        RoboCheckEqInt((int)PaintOpacityStackPush(stack, &opaque, NULL),
                          (int)RoboOk);
    }
    PaintOpacityStackCurrent(stack, &current);
    OpacCheckInt(PaintOpacityIsOpaque(&current), 1, "and so is an opaque one");

    /* Reset empties it and keeps its capacity, which is what a caller reusing a stack per pass
     * relies on. */
    PaintOpacityStackReset(stack);
    RoboCheckEqInt((int)PaintOpacityStackDepth(stack), 0);
    RoboCheckEqInt((int)PaintOpacityStackCapacity(stack), 8);
    PaintOpacityStackReset(NULL);

    PaintOpacityStackDestroy(stack);
    PaintOpacityStackDestroy(NULL);
    RoboCheckEqInt((int)PaintLiveBlocks(), (int)baseline);
}

static void TestOpacityStackGrowth(void)
{
    PaintOpacityStack *stack = NULL;
    PaintOpacity current;
    size_t index;

    PaintFixture();

    RoboCheckEqInt((int)PaintOpacityStackCreate(&stack, NULL), (int)RoboOk);
    RoboCheckEqInt((int)PaintOpacityStackCapacity(stack), 0);

    /*
     * Twenty scopes, each a strength of its own, so the value in force at depth `n` is `1 - n/40`
     * and reading the top back is a statement about **every** slot rather than about the first.
     *
     * The growth is the point: the stack doubles from nothing, and the *new block* is filled with
     * the **opaque** record rather than zeroed. A zeroed slot would be a transparent record, which
     * is a legal and therefore *plausible* value, so the fill is what keeps a record past the count
     * from reading as the strongest possible scope. The fill is not observable through the public
     * API, which is why this case reads the value back **after** the growth and after the pops
     * rather than trusting it: a stale or zeroed slot would show up as the wrong strength here.
     */
    for (index = 0u; index < 20u; index++) {
        PaintOpacity pushed = OpacOf(1.0 - ((double)index / 40.0));

        RoboCheckEqInt((int)PaintOpacityStackPush(stack, &pushed, NULL), (int)RoboOk);
    }
    RoboCheckEqInt((int)PaintOpacityStackDepth(stack), 20);
    RoboCheckEqInt((int)PaintOpacityStackCapacity(stack), 32);
    PaintOpacityStackCurrent(stack, &current);
    OpacCheckValue(current.value, 1.0 - (19.0 / 40.0), "the top of twenty scopes");

    for (index = 0u; index < 20u; index++) {
        RoboCheckEqInt((int)PaintOpacityStackPop(stack, NULL), (int)RoboOk);
    }
    OpacCheckInt(PaintOpacityStackIsEmpty(stack), 1, "twenty pushes, twenty pops");

    /* And a push after the pops reads the value it pushed, not a stale one. */
    RoboCheckEqInt((int)PaintOpacityStackPush(stack, &(PaintOpacity){ 0.25 },
                                                         NULL),
                      (int)RoboOk);
    PaintOpacityStackCurrent(stack, &current);
    OpacCheckValue(current.value, 0.25, "a push after the pops reads its own value");

    PaintOpacityStackDestroy(stack);
}

/* ------------------------------------------------------------------ */
/* 3. The traversal                                                    */
/* ------------------------------------------------------------------ */

static void TestOpacityDefault(void)
{
    PaintFixtureDoc fixture;
    PaintResult result;
    size_t baseline = PaintLiveBlocks();

    PaintFixture();

    PaintBeginLaidOut(&fixture,
                         "<html><body><div class=\"plain\"></div></body></html>",
                         OPAC_BASE_CSS " .plain { width: 20px; height: 20px;"
                         " background-color: #ff0000 }");
    PaintOnce(&fixture, &result);

    /*
     * A document that declares no opacity produces a list with **no opacity command in it**, and
     * every record in it carries full strength. That is what makes the phase additive rather than
     * a rewrite: every drawing this build produced before it draws at exactly the same strength it
     * did before, because full strength is the raster's own default and is never written.
     */
    RoboCheckEqInt((int)result.stats.opacityDeclared, 0);
    RoboCheckEqInt((int)result.stats.opacityScopes, 0);
    RoboCheckEqInt((int)PaintCountOf(&result, RoboPaintOpPushOpacity), 0);
    RoboCheckEqInt((int)PaintCountOf(&result, RoboPaintOpPopOpacity), 0);
    RoboCheck(PaintListCount(result.list) > 0u);
    OpacListIs(&result, 1.0, "every record of a document that declares no opacity");

    PaintResultDispose(&result);
    PaintEnd(&fixture);
    RoboCheckEqInt((int)PaintLiveBlocks(), (int)baseline);
}

static void TestOpacityExplicitOne(void)
{
    PaintFixtureDoc fixture;
    PaintResult silent;
    PaintResult declared;
    size_t baseline = PaintLiveBlocks();

    PaintFixture();

    /*
     * The **same** document twice, the only difference being a declaration that changes nothing.
     * `1` is what an element that says nothing already has, so the two lists must be
     * byte-identical -- which is a stronger statement than "the pixels match": it says the phase
     * emits no command at all for a declaration that establishes nothing, and that its two counters
     * stay at zero.
     */
    PaintBeginLaidOut(&fixture,
                         "<html><body><div class=\"plain\"></div></body></html>",
                         OPAC_BASE_CSS " .plain { width: 20px; height: 20px;"
                         " background-color: #ff0000 }");
    PaintOnce(&fixture, &silent);
    PaintEnd(&fixture);

    PaintBeginLaidOut(&fixture,
                         "<html><body><div class=\"plain\"></div></body></html>",
                         OPAC_BASE_CSS " .plain { width: 20px; height: 20px;"
                         " background-color: #ff0000; opacity: 1 }");
    PaintOnce(&fixture, &declared);
    RoboCheckEqInt((int)declared.stats.opacityDeclared, 0);
    RoboCheckEqInt((int)declared.stats.opacityScopes, 0);
    PaintEnd(&fixture);

    OpacCheckInt(PaintListEqual(silent.list, declared.list), 1,
                   "`opacity: 1` draws the same list as saying nothing");
    OpacCheckInt((int)PaintListCount(silent.list),
                   (int)PaintListCount(declared.list), "and the same number of records");

    PaintResultDispose(&declared);
    PaintResultDispose(&silent);
    RoboCheckEqInt((int)PaintLiveBlocks(), (int)baseline);
}

static void TestOpacityDeclared(void)
{
    PaintFixtureDoc fixture;
    PaintResult result;
    LayoutNodeId fade;
    LayoutNodeId kid;
    PaintDisplayList *list = NULL;
    PaintDisplayListStats stats;
    size_t baseline = PaintLiveBlocks();

    PaintFixture();

    PaintBeginLaidOut(&fixture,
                         "<html><body><div class=\"fade\"><div class=\"kid\"></div></div>"
                         "</body></html>",
                         OPAC_BASE_CSS " .fade { width: 40px; height: 40px;"
                         " background-color: #ff0000; opacity: 0.5 }"
                         " .kid { width: 10px; height: 10px; background-color: #0000ff }");
    PaintOnce(&fixture, &result);
    fade = PaintBox(&fixture, "fade");
    kid = PaintBox(&fixture, "kid");

    /* One declaration, one scope, and the whole subtree carries the box's strength -- its own
     * background *and* its child's. `declared` and `scopes` agreeing at 1 is the ordinary case: the
     * declaration changed something. */
    RoboCheckEqInt((int)result.stats.opacityDeclared, 1);
    RoboCheckEqInt((int)result.stats.opacityScopes, 1);
    RoboCheckEqInt((int)PaintCountOf(&result, RoboPaintOpPushOpacity), 1);
    RoboCheckEqInt((int)PaintCountOf(&result, RoboPaintOpPopOpacity), 1);
    OpacNodeIs(&result, fade, 0.5, "every record of the faded box");
    OpacNodeIs(&result, kid, 0.5, "the child inherits the strength");

    /* The pair carries the strength it establishes, in both halves: that is what makes a pair
     * checkable from the stream. */
    {
        PaintOperation push = OpacOpOfKind(&result, RoboPaintOpPushOpacity, 0u);
        PaintOperation pop = OpacOpOfKind(&result, RoboPaintOpPopOpacity, 0u);

        OpacCheckRecord(&push.opacity, 0.5, "the push carries what it establishes");
        OpacCheckRecord(&pop.opacity, 0.5, "and so does the pop");
        RoboCheckEqInt((int)push.node, (int)fade);
        RoboCheckEqInt((int)pop.node, (int)fade);
        OpacCheckInt(PaintOpacityEqual(&push.opacity, &pop.opacity), 1,
                       "the pair is self-identifying");
    }

    /* The display list converts both, with the stats agreeing and every drawing under the scope
     * carrying the strength. */
    OpacBuild(result.list, &list, &stats);
    RoboCheckEqInt((int)stats.opacityScopes, 1);
    RoboCheckEqInt((int)OpacCountOfKind(list, RoboPaintDisplayCmdPushOpacity), 1);
    RoboCheckEqInt((int)OpacCountOfKind(list, RoboPaintDisplayCmdPopOpacity), 1);
    OpacDrawsAre(list, 0.5, "every drawing under the scope");
    PaintDisplayListDestroy(list);

    PaintResultDispose(&result);
    PaintEnd(&fixture);
    RoboCheckEqInt((int)PaintLiveBlocks(), (int)baseline);
}

static void TestOpacityScopePlacement(void)
{
    PaintFixtureDoc fixture;
    PaintResult result;
    LayoutNodeId fade;
    LayoutNodeId kid;
    size_t begin;
    size_t push;
    size_t background;
    size_t content;
    size_t pop;
    size_t end;

    PaintFixture();

    /*
     * **The one structural difference from 11.8's clip scope**, and the reason the two pairs open
     * at different points in a box's life: a clip belongs to a box's *contents* -- CSS paints the
     * box's own background under its parent's boundary -- while an opacity belongs to the box
     * *itself*, because CSS paints a `0.5` box's own `background-color` at half strength. So the
     * opacity pair opens **after the box's marker and before its first decoration**, and closes
     * before its `end-node`.
     *
     * A build that placed this pair at the content boundary (the clip's placement) would draw the
     * box's own background and border at full strength, which is a rendering bug no interval
     * assertion would catch -- and which `paint.opacity.border_pixels` then catches at a pixel.
     */
    PaintBeginLaidOut(&fixture,
                         "<html><body><div class=\"fade\"><div class=\"kid\"></div></div>"
                         "</body></html>",
                         OPAC_BASE_CSS " .fade { width: 40px; height: 40px;"
                         " background-color: #ff0000; opacity: 0.5 }"
                         " .kid { width: 10px; height: 10px; background-color: #0000ff }");
    PaintOnce(&fixture, &result);
    fade = PaintBox(&fixture, "fade");
    kid = PaintBox(&fixture, "kid");

    begin = OpacOpIndexForNode(&result, RoboPaintOpBeginBox, fade, 0u);
    push = OpacOpIndexForNode(&result, RoboPaintOpPushOpacity, fade, 0u);
    background = OpacOpIndexForNode(&result, RoboPaintOpBackground, fade, 0u);
    content = OpacOpIndexForNode(&result, RoboPaintOpContent, fade, 0u);
    pop = OpacOpIndexForNode(&result, RoboPaintOpPopOpacity, fade, 0u);
    end = OpacOpIndexForNode(&result, RoboPaintOpEndBox, fade, 0u);
    RoboCheck(begin != (size_t)-1 && push != (size_t)-1 && background != (size_t)-1 &&
               content != (size_t)-1 && pop != (size_t)-1 && end != (size_t)-1);

    OpacCheckInt((int)push, (int)(begin + 1u), "the scope opens immediately after the marker");
    OpacCheckInt(background > push, 1, "the box's own background is inside the scope");
    OpacCheckInt(content > background, 1, "and so is its content boundary");
    OpacCheckInt(pop > content, 1, "the scope closes after the content boundary");
    OpacCheckInt(end > pop, 1, "and before the box's end marker");
    OpacCheckInt((int)(pop + 1u), (int)end, "nothing stands between the pop and the end");
    OpacCheckInt((int)OpacOpAt(&result, begin).depth, (int)OpacOpAt(&result, push).depth,
                   "the pair and the marker agree about the depth");

    /*
     * Every record's strength is the strength in force **at its own position**: the marker and the
     * end marker stand outside the pair and therefore carry the enclosing (full) strength, while
     * everything between them carries the box's. That single rule is what makes the two scope kinds
     * readable the same way even though their pairs nest differently -- and it is the same rule
     * that makes `pop-clip` and `end-box` disagree about the clip in 11.8.
     */
    OpacCheckRecordAt(&result, begin, 1.0, "the marker stands outside the scope");
    OpacCheckRecordAt(&result, push, 0.5, "the push is the scope");
    OpacCheckRecordAt(&result, background, 0.5, "the background is inside");
    OpacCheckRecordAt(&result, content, 0.5, "the content boundary too");
    OpacCheckRecordAt(&result, pop, 0.5, "the pop carries the scope");
    OpacCheckRecordAt(&result, end, 1.0, "the end marker is back outside");

    /* And a child box's own marker is inside its parent's scope. */
    {
        size_t kidBegin = OpacOpIndexForNode(&result, RoboPaintOpBeginBox, kid, 0u);

        OpacCheckRecordAt(&result, kidBegin, 0.5,
                             "a child's marker is inside its parent's scope");
    }

    PaintResultDispose(&result);
    PaintEnd(&fixture);
}

static void TestOpacityNested(void)
{
    PaintFixtureDoc fixture;
    PaintResult result;
    LayoutNodeId outer;
    LayoutNodeId inner;
    PaintDisplayList *list = NULL;
    PaintDisplayListStats stats;

    PaintFixture();

    PaintBeginLaidOut(&fixture,
                         "<html><body><div class=\"outer\"><div class=\"inner\"></div></div>"
                         "</body></html>",
                         OPAC_BASE_CSS " .outer { width: 60px; height: 60px;"
                         " background-color: #ff0000; opacity: 0.5 }"
                         " .inner { width: 20px; height: 20px; background-color: #0000ff;"
                         " opacity: 0.5 }");
    PaintOnce(&fixture, &result);
    outer = PaintBox(&fixture, "outer");
    inner = PaintBox(&fixture, "inner");

    /* Two declarations, two scopes, and the child drawn at a **quarter** -- the product, not the
     * child's value. "The child replaces the parent" is the mistake this case exists for, and it
     * would leave the inner box at `0.5`. */
    RoboCheckEqInt((int)result.stats.opacityDeclared, 2);
    RoboCheckEqInt((int)result.stats.opacityScopes, 2);
    RoboCheckEqInt((int)PaintCountOf(&result, RoboPaintOpPushOpacity), 2);
    OpacNodeIs(&result, outer, 0.5, "the outer box's own records");
    OpacNodeIs(&result, inner, 0.25, "the inner box's records");

    /* The two pushes are properly nested and carry the strengths they establish. */
    {
        PaintOperation first = OpacOpOfKind(&result, RoboPaintOpPushOpacity, 0u);
        PaintOperation second = OpacOpOfKind(&result, RoboPaintOpPushOpacity, 1u);

        OpacCheckRecord(&first.opacity, 0.5, "the outer push");
        OpacCheckRecord(&second.opacity, 0.25, "the inner push is the product");
        OpacCheckInt(second.depth > first.depth, 1, "and it nests deeper");
        RoboCheckEqInt((int)first.node, (int)outer);
        RoboCheckEqInt((int)second.node, (int)inner);
    }

    OpacBuild(result.list, &list, &stats);
    RoboCheckEqInt((int)stats.opacityScopes, 2);
    OpacDrawsOfAre(list, inner, 0.25, "the inner box's drawings");
    OpacDrawsOfAre(list, outer, 0.5, "and the outer box's own, one scope out");
    PaintDisplayListDestroy(list);

    PaintResultDispose(&result);
    PaintEnd(&fixture);
}

static void TestOpacityNestingTable(void)
{
    /*
     * The brief's six cases, run through the **whole pipeline** rather than through the multiply
     * door: the same assertion about the inner box's records, six times, with the two counters
     * beside it. Every fact of each row is written down, because the point of a table is that a
     * reader can check it rather than reconstruct it.
     *
     * The counters are the interesting half. `declared` counts boxes whose declaration is not
     * opaque; `scopes` counts the ones that actually **changed** the strength in force -- so
     * `(0, 0.5)` is the row where the two disagree, and it is exactly the row a build that pushed a
     * scope for every declaration would get wrong. A `0.5` box inside a `0` ancestor is already
     * drawn at nothing; its product changes nothing, and a scope that establishes nothing is a
     * command a consumer could only skip.
     */
    static const struct {
        const char *outerText;
        const char *innerText;
        double outer;
        double inner;
        int declared;
        int scopes;
    } rows[6] = {
        { "1", "1", 1.0, 1.0, 0, 0 },
        { "1", "0.5", 1.0, 0.5, 1, 1 },
        { "0.5", "1", 0.5, 1.0, 1, 1 },
        { "0.5", "0.5", 0.5, 0.5, 2, 2 },
        { "0", "0.5", 0.0, 0.5, 2, 1 },
        { "0.5", "0", 0.5, 0.0, 2, 2 }
    };
    size_t row;

    PaintFixture();

    for (row = 0u; row < 6u; row++) {
        PaintFixtureDoc fixture;
        PaintResult result;
        char css[512];

        (void)snprintf(css, sizeof(css),
                       OPAC_BASE_CSS
                       " .outer { width: 60px; height: 60px; background-color: #ff0000;"
                       " opacity: %s }"
                       " .inner { width: 20px; height: 20px; background-color: #0000ff;"
                       " opacity: %s }",
                       rows[row].outerText, rows[row].innerText);
        PaintBeginLaidOut(&fixture,
                             "<html><body><div class=\"outer\"><div class=\"inner\"></div>"
                             "</div></body></html>",
                             css);
        PaintOnce(&fixture, &result);

        OpacCheckInt((int)result.stats.opacityDeclared, rows[row].declared,
                       "declarations that are not opaque");
        OpacCheckInt((int)result.stats.opacityScopes, rows[row].scopes,
                       "scopes that changed the strength in force");
        OpacNodeIs(&result, PaintBox(&fixture, "outer"), rows[row].outer,
                     "the outer box's strength");
        OpacNodeIs(&result, PaintBox(&fixture, "inner"), rows[row].outer * rows[row].inner,
                     "the inner box's product");

        PaintResultDispose(&result);
        PaintEnd(&fixture);
    }
}

static void TestOpacitySiblingIsolation(void)
{
    PaintFixtureDoc fixture;
    PaintResult result;
    LayoutNodeId fade;
    LayoutNodeId plain;

    PaintFixture();

    /*
     * Two siblings, the second declaring nothing. The faded one's scope closes before its end
     * marker and therefore long before the next sibling begins, so the plain box's records must
     * carry full strength. A build that left the scope open -- or that accumulated the strength
     * into the traversal's own state instead of passing it down as an argument -- would draw the
     * second sibling faint, which is the leak this case exists for.
     */
    PaintBeginLaidOut(&fixture,
                         "<html><body><div class=\"fade\"></div><div class=\"plain\"></div>"
                         "</body></html>",
                         OPAC_BASE_CSS " .fade { width: 20px; height: 20px;"
                         " background-color: #ff0000; opacity: 0.5 }"
                         " .plain { width: 20px; height: 20px; background-color: #00ff00 }");
    PaintOnce(&fixture, &result);
    fade = PaintBox(&fixture, "fade");
    plain = PaintBox(&fixture, "plain");

    RoboCheckEqInt((int)result.stats.opacityScopes, 1);
    OpacNodeIs(&result, fade, 0.5, "the faded sibling");
    OpacNodeIs(&result, plain, 1.0, "the plain sibling is untouched by its sibling's scope");

    /* And the plain sibling's drawing sits **after** the pop, in the sequence. */
    RoboCheck(OpacOpIndexForNode(&result, RoboPaintOpPopOpacity, fade, 0u) <
               OpacOpIndexForNode(&result, RoboPaintOpBeginBox, plain, 0u));

    PaintResultDispose(&result);
    PaintEnd(&fixture);
}

static void TestOpacityZero(void)
{
    PaintFixtureDoc fixture;
    PaintResult result;
    LayoutNodeId invisible;
    PaintDisplayList *list = NULL;

    PaintFixture();

    /*
     * A strength of `0` is a legal value and a meaningful one, and it is **not** the same state as
     * an empty stack: an ancestor opacity has reduced to nothing draws nothing, which is a fact
     * about the document rather than about the container. The traversal still emits the whole
     * skeleton -- the box is a box, its subtree is walked, and every record says `0` -- so a
     * consumer that suppressed the *work* rather than the *ink* would be the one that broke.
     */
    PaintBeginLaidOut(&fixture,
                         "<html><body><div class=\"gone\"><div class=\"kid\"></div></div>"
                         "</body></html>",
                         OPAC_BASE_CSS " .gone { width: 20px; height: 20px;"
                         " background-color: #ff0000; opacity: 0 }"
                         " .kid { width: 10px; height: 10px; background-color: #0000ff }");
    PaintOnce(&fixture, &result);
    invisible = PaintBox(&fixture, "gone");

    RoboCheckEqInt((int)result.stats.opacityDeclared, 1);
    RoboCheckEqInt((int)result.stats.opacityScopes, 1);
    RoboCheckEqInt((int)PaintCountOf(&result, RoboPaintOpBackground), 2);
    OpacNodeIs(&result, invisible, 0.0, "the invisible box's records are zero");
    OpacNodeIs(&result, PaintBox(&fixture, "kid"), 0.0, "and so is its subtree's");
    {
        PaintOperation push = OpacOpOfKind(&result, RoboPaintOpPushOpacity, 0u);

        OpacCheckInt(PaintOpacityIsTransparent(&push.opacity), 1,
                       "the push is a transparent scope");
        OpacCheckInt(PaintOpacityIsOpaque(&push.opacity), 0,
                       "and not an opaque one -- the two states are different");
    }

    /* The display list keeps it and the builder raises no objection: a scope that establishes a
     * strength *different* from what is in force is a real scope, whatever the strength is. */
    OpacBuild(result.list, &list, NULL);
    RoboCheckEqInt((int)OpacCountOfKind(list, RoboPaintDisplayCmdPushOpacity), 1);
    PaintDisplayListDestroy(list);

    PaintResultDispose(&result);
    PaintEnd(&fixture);
}

static void TestOpacityText(void)
{
    PaintFixtureDoc fixture;
    PaintResult result;
    LayoutNodeId faded;
    size_t index;
    int runs = 0;

    PaintFixture();

    /*
     * **The one place a style must not be read twice.** A text run borrows the style of the element
     * that owns the text node, and that element opened its own opacity scope around the run before
     * the run was reached. A painter that read the declaration again at the run would apply it
     * twice, so a `0.5` element's text would draw at `0.25` while its background drew at `0.5` --
     * a difference nothing else in the pipeline would notice, and one this case catches at the
     * record.
     */
    PaintBeginLaidOut(&fixture,
                         "<html><body><div class=\"faded\">A</div></body></html>",
                         OPAC_BASE_CSS " .faded { font-size: 100px; color: #ff0000;"
                         " opacity: 0.5 }");
    PaintOnce(&fixture, &result);
    faded = PaintBox(&fixture, "faded");

    RoboCheckEqInt((int)result.stats.textRuns, 1);
    RoboCheckEqInt((int)result.stats.texts, 1);
    RoboCheckEqInt((int)result.stats.opacityScopes, 1);

    for (index = 0u; index < PaintListCount(result.list); index++) {
        PaintOperation operation;

        RoboCheckEqInt((int)PaintListAt(result.list, index, &operation), (int)RoboOk);
        if (operation.kind != RoboPaintOpText) {
            continue;
        }
        runs++;
        /* The run's strength is the element's, not the element's squared. */
        OpacCheckRecord(&operation.opacity, 0.5, "the run's strength");
        RoboCheck(operation.node != faded);
    }
    OpacCheckInt(runs, 1, "one run was painted");

    /* And the element's own records carry the same strength, so the text and the box it sits in
     * are drawn at one strength rather than at two. */
    OpacNodeIs(&result, faded, 0.5, "the element that owns the run");

    PaintResultDispose(&result);
    PaintEnd(&fixture);
}

static void TestOpacityDeterminism(void)
{
    PaintFixtureDoc fixture;
    PaintResult first;
    PaintResult second;
    PaintDisplayList *listFirst = NULL;
    PaintDisplayList *listSecond = NULL;
    PaintDisplayListStats statsFirst;
    PaintDisplayListStats statsSecond;
    PaintDecorationStats paintFirst;
    PaintDecorationStats paintSecond;
    GfxSurface *left = NULL;
    GfxSurface *right = NULL;
    size_t baseline = PaintLiveBlocks();
    int32_t x;
    int32_t y;
    int differences = 0;

    PaintFixture();

    /*
     * The same document painted twice through two independently built pipelines: the record lists
     * must be equal, the display lists equal, the counters agree and the pixels identical. Nothing
     * here reads a pointer or an allocation order, which is what makes "the same DOM and CSS produce
     * the same picture" a statement about the phase rather than about the allocator.
     */
    PaintBeginLaidOut(&fixture,
                         "<html><body><div class=\"outer\"><div class=\"inner\"></div></div>"
                         "<div class=\"plain\"></div></body></html>",
                         OPAC_BASE_CSS " .outer { width: 40px; height: 40px;"
                         " background-color: #ff0000; opacity: 0.5 }"
                         " .inner { width: 10px; height: 10px; background-color: #0000ff;"
                         " opacity: 0.5 }"
                         " .plain { width: 20px; height: 20px; background-color: #00ff00 }");
    PaintOnce(&fixture, &first);
    PaintOnce(&fixture, &second);

    OpacCheckInt(PaintListEqual(first.list, second.list), 1,
                   "two passes over one tree produce one list");

    OpacBuild(first.list, &listFirst, &statsFirst);
    OpacBuild(second.list, &listSecond, &statsSecond);
    OpacCheckInt(PaintDisplayListEqual(listFirst, listSecond), 1,
                   "two builds over one list produce one display list");
    RoboCheckEqInt((int)statsFirst.opacityScopes, (int)statsSecond.opacityScopes);
    RoboCheckEqInt((int)statsFirst.commands, (int)statsSecond.commands);

    left = OpacSurface(800u, 600u, RoboGfxSurfaceAccessReadWrite);
    right = OpacSurface(800u, 600u, RoboGfxSurfaceAccessReadWrite);
    OpacPaint(listFirst, left, NULL, &paintFirst);
    OpacPaint(listSecond, right, NULL, &paintSecond);
    RoboCheckEqInt((int)paintFirst.faded, (int)paintSecond.faded);
    OpacCheckInt((int)paintFirst.opacityScopes, (int)paintSecond.opacityScopes,
                   "the pass entered the same scopes");
    OpacCheckInt((int)paintFirst.opacityDepthMax, (int)paintSecond.opacityDepthMax,
                   "and stood the same depth deep");

    for (y = 0; y < 80; y++) {
        for (x = 0; x < 80; x++) {
            GfxColorRgba8 a = OpacRead(left, x, y);
            GfxColorRgba8 b = OpacRead(right, x, y);

            if (a.r != b.r || a.g != b.g || a.b != b.b || a.a != b.a) {
                differences++;
            }
        }
    }
    OpacCheckInt(differences, 0, "the two pictures are the same picture");

    GfxSurfaceRelease(right);
    GfxSurfaceRelease(left);
    PaintDisplayListDestroy(listSecond);
    PaintDisplayListDestroy(listFirst);
    PaintResultDispose(&second);
    PaintResultDispose(&first);
    PaintEnd(&fixture);
    RoboCheckEqInt((int)PaintLiveBlocks(), (int)baseline);
}

static void TestOpacityMutationIsolation(void)
{
    PaintFixtureDoc fixture;
    PaintResult result;
    PaintDisplayList *before = NULL;
    PaintDisplayList *after = NULL;
    PaintDisplayListStats afterStats;
    PaintDecorationStats first;
    PaintDecorationStats second;
    PaintOpacity stored;
    GfxSurface *one = NULL;
    GfxSurface *two = NULL;
    int32_t x;
    int32_t y;
    int differences = 0;

    PaintFixture();

    /*
     * `<html>` carries the strength, and the mutation is the strongest legal change to the sources
     * a display list was built from: **clear the record list** and lay the same tree out at a
     * different viewport. The viewport is what makes the mutation observable -- it moves every
     * rectangle in the list -- and the clearing is what says the list's origin no longer exists.
     * The list already built is a snapshot, so it holds the strengths it was built with and paints
     * exactly as it did.
     *
     * (The viewport, rather than a second sheet, because a style refresh would leave the tree
     * holding pointers into style storage the bridge is free to relocate -- a hazard that belongs
     * to the style fixture rather than to this phase. What this case pins is opacity's half of the
     * claim: the stored strength is a value in the list, not a reference to the declaration.)
     */
    PaintBeginLaidOut(&fixture,
                         "<html><body><div class=\"fade\"></div></body></html>",
                         OPAC_BASE_CSS " .fade { width: 40px; height: 40px;"
                         " background-color: #ff0000; opacity: 0.5 }");
    PaintOnce(&fixture, &result);
    OpacBuild(result.list, &before, NULL);
    {
        PaintDisplayCommand snapshot;

        RoboCheckEqInt(OpacCommandOfKind(before, RoboPaintDisplayCmdPushOpacity, 0u,
                                               &snapshot),
                          1);
        stored = snapshot.opacity;
        OpacCheckRecord(&stored, 0.5, "the strength the first list was built with");
    }

    one = OpacSurface(800u, 600u, RoboGfxSurfaceAccessReadWrite);
    OpacPaint(before, one, NULL, &first);

    /* The mutation. */
    PaintListClear(result.list);
    RoboCheckEqInt((int)PaintListCount(result.list), 0);
    PaintLayout(&fixture, 400, 600);
    {
        PaintContext *context = NULL;

        RoboCheckEqInt((int)PaintContextCreate(&context, NULL), (int)RoboOk);
        PaintRun(&fixture, context, result.list);
        PaintContextDestroy(context);
    }
    OpacBuild(result.list, &after, &afterStats);

    /* The mutation is **observable**: the new list differs, so the isolation claim is not
     * vacuous. */
    OpacCheckInt(PaintDisplayListEqual(before, after), 0,
                   "the two display lists differ");
    RoboCheckEqInt((int)afterStats.opacityScopes, 1);
    /* And the new list still carries the same *strength* -- the declaration never changed, because
     * the thing that changed was the geometry. */
    {
        PaintDisplayCommand fresh;

        RoboCheckEqInt(OpacCommandOfKind(after, RoboPaintDisplayCmdPushOpacity, 0u,
                                               &fresh),
                          1);
        OpacCheckInt(PaintOpacityEqual(&fresh.opacity, &stored), 1,
                       "the strength did not change, so a difference between the lists is a "
                       "difference in geometry");
    }

    /* And the first list still paints exactly as it did: a display command is a snapshot, so the
     * pixels it produces cannot follow a source that changed after it was built. */
    two = OpacSurface(800u, 600u, RoboGfxSurfaceAccessReadWrite);
    OpacPaint(before, two, NULL, &second);
    OpacCheckInt((int)first.faded, (int)second.faded, "a paint after the sources changed");
    OpacCheckInt((int)first.draws, (int)second.draws, "and the same number of drawings");
    for (y = 0; y < 60; y++) {
        for (x = 0; x < 60; x++) {
            GfxColorRgba8 a = OpacRead(one, x, y);
            GfxColorRgba8 b = OpacRead(two, x, y);

            if (a.r != b.r || a.g != b.g || a.b != b.b || a.a != b.a) {
                differences++;
            }
        }
    }
    OpacCheckInt(differences, 0, "the first list paints the same picture as before");

    GfxSurfaceRelease(two);
    GfxSurfaceRelease(one);
    PaintDisplayListDestroy(after);
    PaintDisplayListDestroy(before);
    PaintResultDispose(&result);
    PaintEnd(&fixture);
}

/* ------------------------------------------------------------------ */
/* 4. The display list                                                 */
/* ------------------------------------------------------------------ */

static void TestOpacityDisplayCommands(void)
{
    PaintFixtureDoc fixture;
    PaintResult result;
    LayoutNodeId fade;
    PaintDisplayList *list = NULL;
    PaintDisplayListStats stats;
    PaintDisplayCommand push;
    PaintDisplayCommand pop;
    PaintDisplayCommand background;
    GfxSurface *surface = NULL;
    PaintDecorationStats paintStats;
    size_t begin;
    size_t pushAt;
    size_t popAt;
    size_t end;
    size_t backgroundAt;
    size_t baseline = PaintLiveBlocks();

    PaintFixture();

    PaintBeginLaidOut(&fixture,
                         "<html><body><div class=\"fade\"></div></body></html>",
                         OPAC_BASE_CSS " .fade { width: 40px; height: 40px;"
                         " background-color: #ff0000; opacity: 0.5 }");
    PaintOnce(&fixture, &result);
    fade = PaintBox(&fixture, "fade");
    OpacBuild(result.list, &list, &stats);

    RoboCheckEqInt((int)stats.opacityScopes, 1);
    RoboCheckEqInt(OpacCommandOfKind(list, RoboPaintDisplayCmdPushOpacity, 0u, &push),
                      1);
    RoboCheckEqInt(OpacCommandOfKind(list, RoboPaintDisplayCmdPopOpacity, 0u, &pop),
                      1);
    RoboCheckEqInt(OpacCommandOfKind(list, RoboPaintDisplayCmdDrawBackground, 0u,
                                           &background),
                      1);

    /* The scope's commands name the box and its depth, exactly as its marker does, and the
     * rectangle they act on is the **border box** -- the whole of what the scope tints. (The clip
     * pair's is the padding box, because that is the boundary the box's overflow resolves to; the
     * two pairs name two different rectangles of one box, each the true answer to its own
     * question.) */
    OpacCheckInt((int)push.node, (int)fade, "the push names the box");
    OpacCheckInt((int)pop.node, (int)fade, "and so does the pop");
    RoboCheckEqInt((int)push.depth, (int)pop.depth);
    OpacCheckInt((int)push.rect.x, 0, "the scope's rectangle is the border box");
    OpacCheckInt((int)push.rect.width, 40, "on the horizontal axis");
    OpacCheckInt((int)push.rect.height, 40, "and the vertical one");
    OpacCheckInt((int)background.rect.width, (int)push.rect.width,
                   "and it is the rectangle the background fills");

    /* The command carries the strength as a snapshot, and the pop carries the strength its push
     * carried, so the pair is checkable from the list alone. */
    OpacCheckRecord(&push.opacity, 0.5, "the push command");
    OpacCheckRecord(&pop.opacity, 0.5, "the pop command");
    OpacCheckRecord(&background.opacity, 0.5, "the background inside the scope");

    /* The pair's ordering: the push after the node marker and before the drawing, the pop after
     * the drawing and before the end marker. */
    begin = OpacIndexOfKindForNode(list, RoboPaintDisplayCmdBeginNode, fade, 0u);
    pushAt = OpacIndexOfKindForNode(list, RoboPaintDisplayCmdPushOpacity, fade, 0u);
    popAt = OpacIndexOfKindForNode(list, RoboPaintDisplayCmdPopOpacity, fade, 0u);
    end = OpacIndexOfKindForNode(list, RoboPaintDisplayCmdEndNode, fade, 0u);
    backgroundAt = OpacIndexOfKindForNode(list, RoboPaintDisplayCmdDrawBackground,
                                                fade, 0u);
    RoboCheck(begin != (size_t)-1 && pushAt != (size_t)-1 && popAt != (size_t)-1 &&
               end != (size_t)-1 && backgroundAt != (size_t)-1);
    OpacCheckInt((int)pushAt, (int)(begin + 1u), "the push follows the node marker");
    OpacCheckInt(backgroundAt > pushAt, 1, "the drawing is inside the scope");
    OpacCheckInt((int)(popAt + 1u), (int)end, "and the pop precedes the end marker");

    /* The paint pass: the scope is entered, the raster's opacity is set for the drawing, and the
     * pass's own counters say one scope and one faded drawing. */
    surface = OpacSurface(64u, 64u, RoboGfxSurfaceAccessReadWrite);
    OpacPaint(list, surface, NULL, &paintStats);
    OpacCheckInt((int)paintStats.opacityScopes, 1, "the pass entered the scope");
    OpacCheckInt((int)paintStats.opacityDepthMax, 1, "one deep");
    OpacCheckInt((int)paintStats.faded, 1, "one drawing was drawn below full strength");
    GfxSurfaceRelease(surface);

    PaintDisplayListDestroy(list);
    PaintResultDispose(&result);
    PaintEnd(&fixture);
    RoboCheckEqInt((int)PaintLiveBlocks(), (int)baseline);
}

static void TestOpacityHandBuiltScope(void)
{
    PaintList *records = NULL;
    PaintDisplayList *list = NULL;
    PaintDisplayListStats stats;

    PaintFixture();

    /*
     * A **hand-built** record list that opens and closes one scope around one box. The builder has
     * no tree to consult, so this is the case that shows the checks are about the *sequence* rather
     * than about the traversal: a properly nested, narrowing, self-identifying pair is accepted
     * from a caller that has no document at all.
     */
    RoboCheckEqInt((int)PaintListCreate(&records, NULL), (int)RoboOk);
    OpacAppendRecord(records, RoboPaintOpBeginBox, 7u, 0u, OpacBoxes(0, 0, 10, 10),
                       PaintOpacityOpaque());
    OpacAppendRecord(records, RoboPaintOpPushOpacity, 7u, 0u, OpacBoxes(0, 0, 10, 10),
                       OpacOf(0.5));
    OpacAppendRecord(records, RoboPaintOpBackground, 7u, 0u, OpacBoxes(0, 0, 10, 10),
                       OpacOf(0.5));
    OpacAppendRecord(records, RoboPaintOpContent, 7u, 0u, OpacBoxes(0, 0, 10, 10),
                       OpacOf(0.5));
    OpacAppendRecord(records, RoboPaintOpPopOpacity, 7u, 0u, OpacBoxes(0, 0, 10, 10),
                       OpacOf(0.5));
    OpacAppendRecord(records, RoboPaintOpEndBox, 7u, 0u, OpacBoxes(0, 0, 10, 10),
                       PaintOpacityOpaque());

    OpacBuild(records, &list, &stats);
    RoboCheckEqInt((int)stats.opacityScopes, 1);
    RoboCheckEqInt((int)stats.commands, 6);
    RoboCheckEqInt((int)OpacCountOfKind(list, RoboPaintDisplayCmdPushOpacity), 1);
    RoboCheckEqInt((int)OpacCountOfKind(list, RoboPaintDisplayCmdPopOpacity), 1);
    {
        PaintDisplayCommand push;
        PaintDisplayCommand background;

        RoboCheckEqInt(OpacCommandOfKind(list, RoboPaintDisplayCmdPushOpacity, 0u,
                                               &push),
                          1);
        RoboCheckEqInt(OpacCommandOfKind(list, RoboPaintDisplayCmdDrawBackground, 0u,
                                               &background),
                          1);
        OpacCheckRecord(&push.opacity, 0.5, "the hand-built push");
        OpacCheckRecord(&background.opacity, 0.5, "the hand-built drawing");
    }
    PaintDisplayListDestroy(list);

    /* And a hand-built list with **no** scope commands keeps working exactly as it did before the
     * phase: its drawings' own strengths are the only statement of opacity it has. */
    PaintListClear(records);
    OpacAppendRecord(records, RoboPaintOpBeginBox, 7u, 0u, OpacBoxes(0, 0, 10, 10),
                       PaintOpacityOpaque());
    OpacAppendRecord(records, RoboPaintOpBackground, 7u, 0u, OpacBoxes(0, 0, 10, 10),
                       OpacOf(0.5));
    OpacAppendRecord(records, RoboPaintOpContent, 7u, 0u, OpacBoxes(0, 0, 10, 10),
                       PaintOpacityOpaque());
    OpacAppendRecord(records, RoboPaintOpEndBox, 7u, 0u, OpacBoxes(0, 0, 10, 10),
                       PaintOpacityOpaque());
    OpacBuild(records, &list, &stats);
    RoboCheckEqInt((int)stats.opacityScopes, 0);
    RoboCheckEqInt((int)stats.commands, 4);
    PaintDisplayListDestroy(list);

    PaintListDestroy(records);
}

/**
 * Build @p records, assert the build is **refused**, and give the caller the refusal.
 *
 * A helper rather than the three lines written out eleven times, and the two claims it makes
 * together are the ones a refused build owes its caller: an error code, a message, and a list that
 * is **empty** rather than the previous build's.
 */
static void OpacRefuse(const PaintList *records, const char *what)
{
    PaintDisplayList *list = NULL;
    PaintDiag diag;
    Result status = OpacBuildExpectRefusal(records, &list, &diag);

    if (status != RoboError) {
        TestFail(__FILE__, __LINE__, "%s: expected a refusal, got %d", what, (int)status);
    }
    if (!diag.message) {
        TestFail(__FILE__, __LINE__, "%s: the refusal carries no message", what);
    }
    OpacCheckInt((int)diag.domain, (int)RoboPaintDomainDisplayList, what);
    OpacCheckInt((int)PaintDisplayListCount(list), 0, "a refused build leaves no list");
    PaintDisplayListDestroy(list);
}

static void TestOpacityBuilderRefusals(void)
{
    PaintList *records = NULL;

    PaintFixture();

    /*
     * The malformations a record list can carry that no traversal of this build produces, each
     * refused. The refusals are what makes the pair load-bearing rather than ceremonial: a push and
     * a pop that a consumer could only skip would leave the strength a drawing is drawn at a field
     * nothing cross-checks -- and the worst case, a scope that does not narrow, is the one where
     * the *picture* would be wrong rather than unverifiable.
     *
     * Each case builds its own list, because a list cannot lose a record and a case that edited one
     * shared list would be testing the previous case's leftovers.
     */

    /* A push **before any box is open**. */
    RoboCheckEqInt((int)PaintListCreate(&records, NULL), (int)RoboOk);
    OpacAppendRecord(records, RoboPaintOpPushOpacity, 7u, 0u, OpacBoxes(0, 0, 10, 10),
                       OpacOf(0.5));
    OpacRefuse(records, "a push with no open box");
    PaintListDestroy(records);

    /* A push naming a box that is **not the innermost open one**. */
    records = OpacOpenBox(7u);
    OpacAppendRecord(records, RoboPaintOpPushOpacity, 8u, 0u, OpacBoxes(0, 0, 10, 10),
                       OpacOf(0.5));
    OpacRefuse(records, "a push naming another box");
    PaintListDestroy(records);

    /* A push at a **depth that does not match** the box's. */
    records = OpacOpenBox(7u);
    OpacAppendRecord(records, RoboPaintOpPushOpacity, 7u, 3u, OpacBoxes(0, 0, 10, 10),
                       OpacOf(0.5));
    OpacRefuse(records, "a push at the wrong depth");
    PaintListDestroy(records);

    /* A push **after the box's content boundary**. */
    records = OpacOpenBox(7u);
    OpacAppendRecord(records, RoboPaintOpPushOpacity, 7u, 0u, OpacBoxes(0, 0, 10, 10),
                       OpacOf(0.5));
    OpacRefuse(records, "a push after the content boundary");
    PaintListDestroy(records);

    /*
     * A push **after the box has begun drawing** -- the one rule that distinguishes this pair from
     * the clip's, and the one a build that placed the scope at the content boundary would break.
     * The list is the case above with the content boundary removed, so the *only* thing wrong with
     * it is that a decoration was painted before the scope opened.
     */
    records = OpacPaintedBox(7u);
    OpacAppendRecord(records, RoboPaintOpPushOpacity, 7u, 0u, OpacBoxes(0, 0, 10, 10),
                       OpacOf(0.5));
    OpacRefuse(records, "a push after the box's first decoration");
    PaintListDestroy(records);

    /*
     * A push of an **opaque** record: a scope that establishes nothing.
     *
     * Refused one layer **earlier** than the cases around it, and that is the interesting part:
     * `PaintOperationIsValid` holds every record to the two closing rules, so such a record
     * never enters a list at all -- the assertion is therefore at the record, and the *list* is
     * asserted untouched, which is the same two-part claim the builder's refusals make one boundary
     * down. A builder reading such a record would be reading one that came from somewhere else.
     */
    records = OpacEmptyBox();
    {
        PaintOperation scope;
        PaintDiag diag;

        PaintOperationInit(&scope);
        scope.kind = RoboPaintOpPushOpacity;
        scope.node = 7u;
        scope.boxes = OpacBoxes(0, 0, 10, 10);
        scope.opacity = PaintOpacityOpaque();
        OpacCheckInt(PaintOperationIsValid(&scope), 0,
                       "an opaque scope is not a record");
        PaintDiagReset(&diag);
        OpacCheckInt((int)PaintListAppend(records, &scope, &diag),
                       (int)RoboInvalidArgument, "and a list refuses it");
        RoboCheck(diag.message != NULL);
        OpacCheckInt((int)PaintListCount(records), 0,
                       "the refused record left the list empty");
    }
    PaintListDestroy(records);

    /* A box that opens **two** opacity scopes. */
    records = OpacScopedBox(7u, 0.5);
    OpacAppendRecord(records, RoboPaintOpPushOpacity, 7u, 0u, OpacBoxes(0, 0, 10, 10),
                       OpacOf(0.25));
    OpacPopBox(records, 7u, 0.25);
    OpacCloseBox(records, 7u);
    OpacRefuse(records, "a box that opens two scopes");
    PaintListDestroy(records);

    /*
     * A push **stronger** than the strength it is inside. Opacity composes by multiplication, so a
     * scope an ancestor reduced can only reduce further -- a push that raised it would be a box
     * whose `opacity` cancelled an ancestor's, which CSS has no way to express.
     */
    records = OpacScopedBox(7u, 0.25);
    OpacAppendRecord(records, RoboPaintOpBeginBox, 9u, 1u, OpacBoxes(0, 0, 10, 10),
                       OpacOf(0.25));
    OpacAppendRecord(records, RoboPaintOpPushOpacity, 9u, 1u, OpacBoxes(0, 0, 10, 10),
                       OpacOf(0.5));
    OpacRefuse(records, "a scope stronger than the one it is inside");
    PaintListDestroy(records);

    /* A **pop with nothing to pop**: no scope was ever opened. */
    records = OpacOpenBox(7u);
    OpacPopBox(records, 7u, 0.5);
    OpacRefuse(records, "a pop with no scope");
    PaintListDestroy(records);

    /* A pop whose strength is **not the one its push carried**. The pair is compared, not
     * trusted. */
    records = OpacScopedBox(7u, 0.5);
    OpacPopBox(records, 7u, 0.25);
    OpacCloseBox(records, 7u);
    OpacRefuse(records, "a pop that closes with another strength");
    PaintListDestroy(records);

    /* A box that **closes with its scope still open**. The box stack alone would not catch it: the
     * frame is about to be discarded, and the scope it left open is a command with no matching pop
     * until the end of the build -- a different and later failure. */
    records = OpacScopedBox(7u, 0.5);
    OpacCloseBox(records, 7u);
    OpacRefuse(records, "a box closing with a scope open");
    PaintListDestroy(records);

    /* A **drawing inside a scope carrying a different strength** -- the rule that makes the scope
     * the authority for what is drawn inside it. */
    records = OpacScopedBox(7u, 0.5);
    OpacAppendRecord(records, RoboPaintOpBackground, 7u, 0u, OpacBoxes(0, 0, 10, 10),
                       OpacOf(0.25));
    OpacPopBox(records, 7u, 0.5);
    OpacCloseBox(records, 7u);
    OpacRefuse(records, "a command inside a scope carrying another strength");
    PaintListDestroy(records);

    /*
     * A scope left open when the **record list ends**. The per-box check above catches a box that
     * closed with one still open; this catches a list that simply stops while a box -- and so a
     * strength -- is still open.
     *
     * It is the case the box stack would report one step later, and it is separate because the
     * *failure* is separate: the per-box refusal is a box that lied about its own life, while this
     * one is a list that ends mid-scope, leaving a consumer that replayed it with a strength in
     * force for whatever it draws next. A list cannot close a box's scope for it, so the refusal is
     * the only answer that does not invent a drawing strength.
     */
    records = OpacScopedBox(7u, 0.5);
    OpacRefuse(records, "a list that ends with a scope still open");
    PaintListDestroy(records);

    /* And the **valid** list, built last: the builder left no scope behind across the refusals,
     * which is the property a caller reusing one depends on. */
    records = OpacScopedBox(7u, 0.5);
    OpacPopBox(records, 7u, 0.5);
    OpacCloseBox(records, 7u);
    {
        PaintDisplayList *list = NULL;
        PaintDisplayListStats stats;

        OpacBuild(records, &list, &stats);
        RoboCheckEqInt((int)stats.opacityScopes, 1);
        RoboCheckEqInt((int)stats.commands, 6);
        PaintDisplayListDestroy(list);
    }
    PaintListDestroy(records);
}

static void TestOpacityPassRefusals(void)
{
    PaintDisplayList *list = NULL;
    GfxSurface *surface = NULL;
    PaintDiag diag;
    Result status;
    size_t baseline = PaintLiveBlocks();

    PaintFixture();

    /*
     * A pop with nothing to pop. The list's own validity rule cannot see it -- each command is a
     * record -- so this is the pass's contract, and it refuses rather than skipping: a consumer that
     * closed a strength that was never established would go on drawing at a strength that is not in
     * force.
     */
    RoboCheckEqInt((int)PaintDisplayListCreate(&list, NULL), (int)RoboOk);
    {
        PaintDisplayCommand command;

        PaintDisplayCommandInit(&command);
        command.kind = RoboPaintDisplayCmdPopOpacity;
        command.node = 1u;
        command.depth = 0u;
        command.rect = OpacRect(0, 0, 10, 10);
        command.opacity = OpacOf(0.5);
        RoboCheckEqInt((int)PaintDisplayListAppend(list, &command, NULL),
                          (int)RoboOk);
    }
    surface = OpacSurface(64u, 64u, RoboGfxSurfaceAccessReadWrite);
    PaintDiagReset(&diag);
    status = PaintDecorationPaint(list, surface, NULL, NULL, &diag);
    RoboCheckEqInt((int)status, (int)RoboNotFound);
    RoboCheckEqInt((int)diag.domain, (int)RoboPaintDomainOpacity);
    RoboCheck(diag.message != NULL);
    GfxSurfaceRelease(surface);

    /* A missing target: the pass's argument check, unchanged by the phase. */
    PaintDiagReset(&diag);
    status = PaintDecorationPaint(list, NULL, NULL, NULL, &diag);
    RoboCheckEqInt((int)status, (int)RoboInvalidArgument);

    /* An empty list is a legitimate pass that draws nothing, with no scopes and no fades. */
    PaintDisplayListClear(list);
    surface = OpacSurface(16u, 16u, RoboGfxSurfaceAccessReadWrite);
    {
        PaintDecorationStats stats;

        OpacPaint(list, surface, NULL, &stats);
        OpacCheckInt((int)stats.commands, 0, "an empty list has no commands");
        OpacCheckInt((int)stats.opacityScopes, 0, "and no scopes");
        OpacCheckInt((int)stats.faded, 0, "and nothing was faded");
    }
    GfxSurfaceRelease(surface);

    PaintDisplayListDestroy(list);
    RoboCheckEqInt((int)PaintLiveBlocks(), (int)baseline);
}

/* ------------------------------------------------------------------ */
/* 5. The pixels                                                       */
/* ------------------------------------------------------------------ */

/**
 * Paint @p html and @p css at 800x600 and hand back the surface, **with the fixture torn down**.
 *
 * The list and the surface are both independent of the document once they exist -- a display list
 * is a snapshot and a surface is the graphics module's -- so a case can build its control, tear the
 * document down, build the faded variant and compare the two pictures. That is what lets the
 * comparison be between **two independently constructed documents** rather than between two passes
 * over one.
 */
static GfxSurface *OpacPicture(const char *html,
                                        const char *css,
                                        FontFace *face,
                                        PaintDecorationStats *outStats)
{
    PaintFixtureDoc fixture;
    PaintResult result;
    PaintDisplayList *list = NULL;
    GfxSurface *surface = OpacSurface(800u, 600u, RoboGfxSurfaceAccessReadWrite);

    PaintBeginLaidOut(&fixture, html, css);
    PaintOnce(&fixture, &result);
    OpacBuild(result.list, &list, NULL);
    OpacPaint(list, surface, face, outStats);
    PaintDisplayListDestroy(list);
    PaintResultDispose(&result);
    PaintEnd(&fixture);
    return surface;
}

#define OPAC_BACK_CSS " .back { width: 100px; height: 100px; background-color: #ffffff }"

/* The text case's two sheets, macros rather than variables because the other sheets are
 * concatenated around them and a string literal is what a concatenation needs. */
#define OPAC_TEXT_CSS " .txt { font-size: 100px; color: #ff0000 }"
#define OPAC_TEXT_FADED_CSS " .txt { font-size: 100px; color: #ff0000; opacity: 0.5 }"

static void TestOpacityBackgroundPixels(void)
{
    static const char *const html =
        "<html><body><div class=\"back\"><div class=\"front\"></div></div></body></html>";
    GfxSurface *plain = NULL;
    GfxSurface *faded = NULL;
    PaintDecorationStats plainStats;
    PaintDecorationStats fadedStats;
    GfxColorRgba8 expected;
    GfxColorRgba8 actual;
    size_t baseline = PaintLiveBlocks();

    PaintFixture();

    /*
     * The brief's own document: a white parent with a red child inside it, painted twice -- once as
     * written and once with the child at half strength. The two documents differ in **one
     * declaration** and therefore in nothing else: `opacity` does not affect layout, so the geometry
     * is identical and every pixel's *position* is comparable.
     */
    plain = OpacPicture(html,
                         OPAC_BASE_CSS OPAC_BACK_CSS
                         " .front { width: 50px; height: 50px; background-color: #ff0000 }",
                         NULL, &plainStats);
    faded = OpacPicture(html,
                         OPAC_BASE_CSS OPAC_BACK_CSS
                         " .front { width: 50px; height: 50px; background-color: #ff0000;"
                         " opacity: 0.5 }",
                         NULL, &fadedStats);

    /* The control: red inside the child, white inside the parent and outside the child, and nothing
     * at all outside the parent. */
    OpacPixelIs(plain, 10, 10, OPAC_RED, "the child's own fill");
    OpacPixelIs(plain, 80, 80, OPAC_WHITE, "the parent's fill outside the child");
    OpacPixelIs(plain, 200, 200, OPAC_UNTOUCHED, "outside the painted document");

    /* The parent is **not** in a scope, so it is drawn at full strength -- the mutation isolates one
     * declaration, not the whole document. */
    OpacPixelIs(faded, 80, 80, OPAC_WHITE, "the unscoped parent is untouched");
    OpacPixelIs(faded, 200, 200, OPAC_UNTOUCHED, "and nothing leaked outside it");

    /*
     * And the child is the composite of what it was. The exact answer is `(255, 127, 127, 255)` --
     * `mul_div255(255, 128)` is 128, so the blend removes 128 from both of the white channels --
     * but the assertion is written through the blend door, so what is pinned is the *relationship*
     * and the three inline checks below name the number.
     */
    expected = OpacExpected(OPAC_RED, OPAC_WHITE, 0.5);
    actual = OpacRead(faded, 10, 10);
    OpacPixelIs(faded, 10, 10, expected, "the faded child is the composite of the opaque one");
    OpacCheckInt((int)actual.r, 255, "and it is still red");
    OpacCheckInt((int)actual.g, 127, "at `255 - mul_div255(255, 128)`");
    OpacCheckInt((int)actual.b, 127, "on both of the white channels");
    OpacCheckInt((int)actual.a, 255, "and still opaque, because the parent is");

    /* And it is **drawn**, not skipped: every pixel of its 50x50 box still carries ink, which is
     * what distinguishes "faded" from "not painted at all" -- the same distinction the record
     * layer's `is_transparent` draws, checked here at the surface. */
    OpacCheckInt(OpacAnyInk(faded, 0, 0, 50, 50), 1,
                   "the faded child's whole box carries ink");
    OpacCheckInt(OpacAnyInk(faded, 150, 150, 170, 170), 0,
                   "and the space outside the parent carries none");

    /* The pass's counters: one faded drawing, and the scope entered once. */
    OpacCheckInt((int)plainStats.faded, 0, "the control fades nothing");
    OpacCheckInt((int)fadedStats.faded, 1, "and the faded document fades exactly one drawing");

    GfxSurfaceRelease(faded);
    GfxSurfaceRelease(plain);
    RoboCheckEqInt((int)PaintLiveBlocks(), (int)baseline);
}

static void TestOpacityBorderPixels(void)
{
    static const char *const html =
        "<html><body><div class=\"back\"><div class=\"box\"></div></div></body></html>";
    GfxSurface *plain = NULL;
    GfxSurface *faded = NULL;
    GfxColorRgba8 expected;
    GfxColorRgba8 actual;
    size_t baseline = PaintLiveBlocks();

    PaintFixture();

    /*
     * A **border band** over a white parent, at half strength. The band is the second drawing kind,
     * and the one that proves the scope's placement is right: if the pair opened at the box's
     * *content boundary* (the clip's placement) the box's own border would be drawn at full strength
     * and this pixel would be pure red.
     */
    plain = OpacPicture(html,
                         OPAC_BASE_CSS OPAC_BACK_CSS
                         " .box { width: 40px; height: 40px; border: 4px solid #ff0000 }",
                         NULL, NULL);
    faded = OpacPicture(html,
                         OPAC_BASE_CSS OPAC_BACK_CSS
                         " .box { width: 40px; height: 40px; border: 4px solid #ff0000;"
                         " opacity: 0.5 }",
                         NULL, NULL);

    /* The control's top band is the four rows above the box's content: a sample two pixels in. */
    OpacPixelIs(plain, 10, 2, OPAC_RED, "the control's top border band");
    /* Inside the box there is nothing: it declares no background, so the parent's white shows. */
    OpacPixelIs(plain, 10, 20, OPAC_WHITE, "inside the box, nothing is drawn");

    OpacPixelIs(faded, 10, 20, OPAC_WHITE, "and the faded version leaves the same interior");
    expected = OpacExpected(OPAC_RED, OPAC_WHITE, 0.5);
    actual = OpacRead(faded, 10, 2);
    OpacPixelIs(faded, 10, 2, expected, "the faded band is the composite of the opaque one");
    OpacCheckInt((int)actual.g, 127, "the band's green channel is halved");
    OpacCheckInt((int)actual.a, 255, "and it is still opaque over an opaque parent");

    GfxSurfaceRelease(faded);
    GfxSurfaceRelease(plain);
    RoboCheckEqInt((int)PaintLiveBlocks(), (int)baseline);
}

static void TestOpacityTextPixels(void)
{
    static const char *const html =
        "<html><body><div class=\"back\"><div class=\"txt\">A</div></div></body></html>";
    GfxSurface *plain = NULL;
    GfxSurface *faded = NULL;
    OpacFace face;
    PaintDecorationStats plainStats;
    PaintDecorationStats fadedStats;
    GfxColorRgba8 opaque;
    GfxColorRgba8 expected;
    GfxColorRgba8 actual;
    int32_t inkX = 0;
    int32_t inkY = 0;
    size_t baseline = PaintLiveBlocks();

    PaintFixture();
    OpacFaceCreate(&face);

    /*
     * The third drawing kind, and the only one whose ink position this suite does not compute: a
     * single 'A' at 100px is a ten-device-unit square outline whose bottom-left corner is the run's
     * origin, so **the first inked pixel of the opaque run** is the sample. Finding it rather than
     * deriving it is the point -- the assertion is then about a pixel the control put a colour on,
     * and the glyph's geometry stays RoboFont's business rather than this suite's.
     *
     * The face is passed only to the text cases, because text is the one kind that needs a resource:
     * the fill cases pass NULL and are drawn exactly the same way.
     */
    plain = OpacPicture(html, OPAC_BASE_CSS OPAC_BACK_CSS OPAC_TEXT_CSS, face.face,
                         &plainStats);
    OpacCheckInt((int)plainStats.noFont, 0, "the control drew with the face");
    RoboCheckEqInt(OpacFirstInk(plain, 0, 0, 100, 100, &inkX, &inkY), 1);
    opaque = OpacRead(plain, inkX, inkY);
    OpacCheckInt((int)opaque.a, 255, "the control's glyph ink is opaque");

    faded = OpacPicture(html, OPAC_BASE_CSS OPAC_BACK_CSS OPAC_TEXT_FADED_CSS, face.face,
                         &fadedStats);

    /* The claim: the faded run put the composite of that colour where the opaque one put the
     * colour. A run's ink overhangs its advance box, which is why the *pixel* is the sample rather
     * than the rectangle. */
    expected = OpacExpected(opaque, OPAC_WHITE, 0.5);
    actual = OpacRead(faded, inkX, inkY);
    if (actual.r != expected.r || actual.g != expected.g || actual.b != expected.b ||
        actual.a != expected.a) {
        TestFail(__FILE__, __LINE__,
                       "the faded glyph at %d,%d: expected %d,%d,%d,%d, got %d,%d,%d,%d",
                       (int)inkX, (int)inkY, (int)expected.r, (int)expected.g,
                       (int)expected.b, (int)expected.a, (int)actual.r, (int)actual.g,
                       (int)actual.b, (int)actual.a);
    }

    /* And the counters: the run really was drawn, the scope was entered and the drawing was faded.
     * `glyphs` is what makes "text respects opacity" a statement about a glyph rather than about a
     * command. */
    OpacCheckInt((int)plainStats.glyphs, 1, "the control drew one glyph");
    OpacCheckInt((int)fadedStats.glyphs, 1, "and so did the faded document");
    OpacCheckInt((int)fadedStats.opacityScopes, 1, "the faded run stood in one scope");
    OpacCheckInt((int)fadedStats.faded, 1, "and its drawing was faded");

    OpacFaceDestroy(&face);
    GfxSurfaceRelease(faded);
    GfxSurfaceRelease(plain);
    RoboCheckEqInt((int)PaintLiveBlocks(), (int)baseline);
}

static void TestOpacityNoOpacityUnchanged(void)
{
    GfxSurface *surface = NULL;
    PaintDecorationStats stats;
    size_t baseline = PaintLiveBlocks();

    PaintFixture();

    /*
     * A document that declares no opacity, painted: the raster's own opacity is never set to
     * anything but its default, so `faded` is **zero** and the pixels are the ones the build
     * produced before this phase. This is the case that would catch a pass that set the strength
     * from a scope that is not there -- an empty stack answers full strength, and a store of `255`
     * is what a build with no opacity in it performs, which is the raster's own default.
     */
    surface = OpacPicture("<html><body><div class=\"plain\"></div></body></html>",
                           OPAC_BASE_CSS " .plain { width: 20px; height: 20px;"
                           " background-color: #ff0000 }",
                           NULL, &stats);
    OpacPixelIs(surface, 5, 5, OPAC_RED, "an unscoped fill is exactly its colour");
    OpacCheckInt((int)stats.faded, 0, "and nothing was faded");
    OpacCheckInt((int)stats.opacityScopes, 0, "in no scope at all");
    GfxSurfaceRelease(surface);
    RoboCheckEqInt((int)PaintLiveBlocks(), (int)baseline);
}

static void TestOpacityGroupDeviation(void)
{
    static const char *const html =
        "<html><body><div class=\"group\"><div class=\"one\"></div><div class=\"two\"></div>"
        "</div></body></html>";
    GfxSurface *surface = NULL;
    PaintDecorationStats stats;
    GfxColorRgba8 blueHalf;
    GfxColorRgba8 redHalf;
    GfxColorRgba8 overlap;
    GfxColorRgba8 perDrawing;
    GfxColorRgba8 group;
    size_t baseline = PaintLiveBlocks();

    PaintFixture();

    /*
     * **The deviation, asserted rather than hidden.** CSS composites an element's whole subtree as a
     * group and applies the opacity to the flattened result; this build multiplies the effective
     * opacity per drawing. The two agree about every drawing's *existence* and about the whole
     * document's, and they differ in the colour of a pixel where one descendant's ink covers
     * another's:
     *
     *   CSS    `0.5 * (red over blue)`          -- the overlap keeps full blue under opaque red, so
     *                                              the flattened group is opaque red and one half
     *                                              of the whole thing is one half-alpha red;
     *   this   `(0.5 * red) over (0.5 * blue)` -- the blue is still there at half, and the red at
     *                                              half goes over it.
     *
     * Both are defensible pictures and only one is what CSS specifies, which is why the difference
     * is a **number in a test** rather than a paragraph in a header. Group opacity needs an
     * offscreen layer to composite into, and that is a compositor and a later phase's; the record's
     * shape is what a group implementation replaces.
     *
     * The document is two overlapping children of one `0.5` parent: `.two` is moved over `.one`
     * with a relative offset, which this build supports and which is the cheapest way to make two
     * siblings overlap. Both children are at half strength, each drawn once where it stands.
     */
    surface = OpacPicture(html,
                           OPAC_BASE_CSS " .group { width: 60px; height: 60px; opacity: 0.5 }"
                           " .one { width: 40px; height: 40px; background-color: #0000ff }"
                           " .two { width: 40px; height: 40px; background-color: #ff0000;"
                           " position: relative; top: -20px; left: 20px }",
                           NULL, &stats);

    /*
     * The three regions the two inks make. `.one` is at (0,0)-(40,40); `.two` is drawn at
     * (20,20)-(60,60) after the relative offset and after `.one` in document order.
     */
    blueHalf = OpacRead(surface, 5, 5);    /* `.one` alone */
    redHalf = OpacRead(surface, 50, 30);   /* `.two` alone */
    overlap = OpacRead(surface, 30, 30);    /* both */

    OpacCheckInt((int)blueHalf.r, 0, "the blue child alone has no red");
    OpacCheckInt((int)blueHalf.b, 255, "and keeps its blue");
    OpacCheckInt((int)blueHalf.a, 128, "at half alpha");
    OpacCheckInt((int)redHalf.r, 255, "the red child alone is red");
    OpacCheckInt((int)redHalf.a, 128, "at half alpha");

    /* This build's answer, computed through the blend door from the two single-ink pixels: each
     * ink at its own half, composited in document order. */
    perDrawing = GfxCompositeSrcOverOpacity(redHalf, blueHalf, 255u);
    if (overlap.r != perDrawing.r || overlap.g != perDrawing.g || overlap.b != perDrawing.b ||
        overlap.a != perDrawing.a) {
        TestFail(__FILE__, __LINE__,
                       "the overlap: expected the per-drawing composite %d,%d,%d,%d, got "
                       "%d,%d,%d,%d",
                       (int)perDrawing.r, (int)perDrawing.g, (int)perDrawing.b,
                       (int)perDrawing.a, (int)overlap.r, (int)overlap.g, (int)overlap.b,
                       (int)overlap.a);
    }

    /* And the group answer, which is **not** what a surface here holds: opaque red flattened, then
     * halved once. The blue the overlap keeps -- `85`, where the group form leaves `0` -- is the
     * deviation, and it is the assertion below that makes it a fact rather than a claim. */
    group = GfxCompositeSrcOverOpacity(OPAC_RED, OPAC_UNTOUCHED, OPAC_HALF_BYTE);
    OpacCheckInt((int)group.b, 0, "the group form leaves no blue in the overlap");
    OpacCheckInt((int)overlap.b, 85, "this build leaves the blue below the red, at its half");
    OpacCheckInt((int)(overlap.b == group.b), 0,
                   "and those two are the deviation, asserted as a disagreement");
    OpacCheckInt((int)stats.faded, 2, "two drawings, each faded once");

    GfxSurfaceRelease(surface);
    RoboCheckEqInt((int)PaintLiveBlocks(), (int)baseline);
}

static void TestOpacityEndToEnd(void)
{
    static const char *const html =
        "<html><body><div class=\"outer\"><div class=\"inner\">A</div></div></body></html>";
    GfxSurface *surface = NULL;
    OpacFace face;
    PaintDecorationStats paintStats;
    GfxColorRgba8 outerPixel;
    GfxColorRgba8 innerPixel;

    PaintFixture();
    OpacFaceCreate(&face);

    /*
     * Nested opacity, a background per box and a text run, through the whole pipeline -- the
     * capstone document, with all three drawing kinds of this release inside one another and a real
     * face to turn the run's characters into glyphs.
     */
    surface = OpacPicture(html,
                           OPAC_BASE_CSS " .outer { width: 100px; height: 100px;"
                           " background-color: #ffffff; opacity: 0.5 }"
                           " .inner { width: 60px; height: 60px; background-color: #ff0000;"
                           " opacity: 0.5; font-size: 100px; color: #0000ff }",
                           face.face, &paintStats);

    /*
     * Three faded drawings -- the outer's white, the inner's red and, since the run is inside the
     * inner box, the run's glyph -- which is the phase's "every drawing" claim counted rather than
     * argued. The run is the interesting one: it carries the **element's** strength rather than the
     * element's squared, because the scope was entered before the run was reached.
     *
     * The outer's white is half-composited over nothing, so its colour survives and its **alpha** is
     * halved; the inner's red is a quarter, composited over that half-white. The two samples are far
     * from the glyph, whose ink is a ten-unit square at the run's origin in the inner box's top-left
     * corner.
     */
    outerPixel = OpacRead(surface, 90, 90);
    innerPixel = OpacRead(surface, 55, 55);

    OpacCheckInt((int)paintStats.faded, 3, "three drawings were drawn below full strength");
    OpacCheckInt((int)paintStats.texts, 1, "one text run reached the surface");
    OpacCheckInt((int)paintStats.glyphs, 1, "as one glyph");
    OpacCheckInt((int)paintStats.noFont, 0, "with the face the caller supplied");
    OpacCheckInt((int)outerPixel.r, 255, "the outer fill is white");
    OpacCheckInt((int)outerPixel.g, 255, "on all three channels");
    OpacCheckInt((int)outerPixel.a, 128, "at half alpha, over nothing");
    OpacCheckInt((int)innerPixel.r, 255, "the inner fill is still red");
    OpacCheckInt((int)(innerPixel.g < 255), 1, "with something of the white below it gone");
    OpacCheckInt((int)(innerPixel.g > 127), 1, "and more of it left than a plain half would");
    OpacCheckInt((int)innerPixel.g, (int)innerPixel.b, "the two channels that are not red agree");

    /*
     * The faded surface is a **real image**, not just an assertion: RoboGraphics' own encoder --
     * the one Phase 6 shipped, not a second one -- turns it into a PNG, whose signature and width
     * are read back here. That is what makes the blend inspectable rather than merely asserted: a
     * reader who wants to *see* the half-white with the quarter-red inside it runs this case and
     * opens the buffer.
     */
    {
        GfxEncodeOptions encodeOpts;
        GfxEncodedBuffer encoded;
        static const uint8_t pngSignature[8] = { 0x89u, 'P', 'N', 'G', 0x0du, 0x0au, 0x1au, 0x0au };

        memset(&encoded, 0, sizeof(encoded));
        GfxEncodeOptionsInit(&encodeOpts);
        RoboCheckEqInt(GfxSurfaceEncode(surface, &encodeOpts, &encoded), RoboOk);
        RoboCheck(encoded.size > 8u);
        RoboCheckEqInt(memcmp(encoded.data, pngSignature, sizeof(pngSignature)), 0);
        RoboCheckEqInt((int)((uint32_t)encoded.data[16] << 24 | (uint32_t)encoded.data[17] << 16 |
                                (uint32_t)encoded.data[18] << 8 | (uint32_t)encoded.data[19]),
                          (int)GfxSurfaceWidth(surface));
        GfxEncodedBufferRelease(&encoded);
    }

    OpacFaceDestroy(&face);
    GfxSurfaceRelease(surface);
}

/* ------------------------------------------------------------------ */
/* 6. The dumps                                                        */
/* ------------------------------------------------------------------ */

static void TestOpacityDump(void)
{
    PaintFixtureDoc fixture;
    PaintResult result;
    PaintDisplayList *list = NULL;
    char line[RoboPaintDumpLineMax + 1u];
    char displayLine[RoboPaintDisplayDumpLineMax + 1u];
    size_t length = 0u;
    size_t baseline = PaintLiveBlocks();

    PaintFixture();

    PaintBeginLaidOut(&fixture,
                         "<html><body><div class=\"fade\"></div></body></html>",
                         OPAC_BASE_CSS " .fade { width: 40px; height: 40px;"
                         " background-color: #ff0000; opacity: 0.5 }");
    PaintOnce(&fixture, &result);

    /*
     * The **record** line, with the strength printed for every kind exactly as the clip is: what is
     * in force is a question every record answers, so a field that appeared only on a scope would
     * leave a drawing's strength unreadable from the drawing a dump is being read for.
     */
    {
        PaintOperation push = OpacOpOfKind(&result, RoboPaintOpPushOpacity, 0u);
        PaintOperation background = OpacOpOfKind(&result, RoboPaintOpBackground, 0u);
        PaintOperation begin = OpacOpOfKind(&result, RoboPaintOpBeginBox, 0u);

        memset(line, 0, sizeof(line));
        RoboCheckEqInt((int)PaintOperationWrite(&push, line, sizeof(line), &length,
                                                          NULL),
                          (int)RoboOk);
        RoboCheck(length > 0u);
        RoboCheck(strstr(line, "push-opacity") != NULL);
        RoboCheck(strstr(line, "opacity=0.50") != NULL);

        memset(line, 0, sizeof(line));
        RoboCheckEqInt((int)PaintOperationWrite(&background, line, sizeof(line),
                                                          &length, NULL),
                          (int)RoboOk);
        RoboCheck(strstr(line, "opacity=0.50") != NULL);

        /* And a fully opaque record prints its own value rather than nothing, so a reader never has
         * to ask whether a missing field means "one" or "not printed". */
        memset(line, 0, sizeof(line));
        RoboCheckEqInt((int)PaintOperationWrite(&begin, line, sizeof(line), &length,
                                                          NULL),
                          (int)RoboOk);
        RoboCheck(strstr(line, "opacity=1.00") != NULL);
    }

    /* The **display** line, with the commands naming the scope and its strength. */
    OpacBuild(result.list, &list, NULL);
    {
        PaintDisplayCommand push;
        PaintDisplayCommand pop;

        RoboCheckEqInt(OpacCommandOfKind(list, RoboPaintDisplayCmdPushOpacity, 0u,
                                               &push),
                          1);
        RoboCheckEqInt(OpacCommandOfKind(list, RoboPaintDisplayCmdPopOpacity, 0u,
                                               &pop),
                          1);
        memset(displayLine, 0, sizeof(displayLine));
        RoboCheckEqInt((int)PaintDisplayCommandWrite(&push, displayLine,
                                                                sizeof(displayLine), &length,
                                                                NULL),
                          (int)RoboOk);
        RoboCheck(strstr(displayLine, "push-opacity") != NULL);
        RoboCheck(strstr(displayLine, "opacity=0.50") != NULL);
        memset(displayLine, 0, sizeof(displayLine));
        RoboCheckEqInt((int)PaintDisplayCommandWrite(&pop, displayLine,
                                                                sizeof(displayLine), &length,
                                                                NULL),
                          (int)RoboOk);
        RoboCheck(strstr(displayLine, "pop-opacity") != NULL);
        RoboCheck(strstr(displayLine, "opacity=0.50") != NULL);
    }

    /*
     * **The bounds, measured.** The headers derive them from the widest line each can produce, and
     * the arithmetic puts the operation line at the same width it always was and the display line
     * thirteen characters wider -- 362 before this phase, 375 after it. A bound is only worth
     * having if something checks it, so this builds the widest line of each kind that this build can
     * produce -- all fields at their widest printed forms, a fully bounded clip, and a strength that
     * is not the default -- and asserts it fits.
     */
    {
        PaintOperation wideRecord;
        PaintDisplayCommand wideCommand;

        PaintOperationInit(&wideRecord);
        wideRecord.kind = RoboPaintOpBorderBottom;
        wideRecord.node = (LayoutNodeId)999999u;
        wideRecord.depth = 99u;
        wideRecord.boxes = OpacBoxes(INT32_MIN, INT32_MIN, INT32_MAX, INT32_MAX);
        wideRecord.origin = MakeGfxPoint(0, 0);
        wideRecord.opacity = OpacOf(0.5);
        wideRecord.color = CssColorMake(255u, 255u, 255u, 255u);
        wideRecord.style = RoboCssBorderStyleSolid;
        wideRecord.clip.horizontal.mode = RoboCssOverflowHidden;
        wideRecord.clip.horizontal.start = INT32_MIN;
        wideRecord.clip.horizontal.end = INT32_MAX;
        wideRecord.clip.vertical.mode = RoboCssOverflowHidden;
        wideRecord.clip.vertical.start = INT32_MIN;
        wideRecord.clip.vertical.end = INT32_MAX;

        memset(line, 0, sizeof(line));
        RoboCheckEqInt((int)PaintOperationWrite(&wideRecord, line, sizeof(line),
                                                          &length, NULL),
                          (int)RoboOk);
        RoboCheck(length < (size_t)RoboPaintDumpLineMax);

        PaintDisplayCommandInit(&wideCommand);
        wideCommand.kind = RoboPaintDisplayCmdDrawBorder;
        wideCommand.node = wideRecord.node;
        wideCommand.depth = wideRecord.depth;
        wideCommand.rect = OpacRect(INT32_MIN, INT32_MIN, INT32_MAX, INT32_MAX);
        wideCommand.clip = wideRecord.clip;
        wideCommand.opacity = wideRecord.opacity;
        wideCommand.color = wideRecord.color;
        wideCommand.style = wideRecord.style;
        memset(displayLine, 0, sizeof(displayLine));
        RoboCheckEqInt((int)PaintDisplayCommandWrite(&wideCommand, displayLine,
                                                                sizeof(displayLine), &length,
                                                                NULL),
                          (int)RoboOk);
        RoboCheck(length < (size_t)RoboPaintDisplayDumpLineMax);
    }

    PaintDisplayListDestroy(list);
    PaintResultDispose(&result);
    PaintEnd(&fixture);
    RoboCheckEqInt((int)PaintLiveBlocks(), (int)baseline);
}

/* ------------------------------------------------------------------ */
/* 7. Empty and malformed                                              */
/* ------------------------------------------------------------------ */

static void TestOpacityEmptyAndMalformed(void)
{
    PaintList *records = NULL;
    PaintDisplayList *list = NULL;
    PaintDisplayListStats stats;
    PaintOperation operation;
    PaintDisplayCommand command;
    PaintDiag diag;
    volatile double zero = 0.0;
    size_t baseline = PaintLiveBlocks();

    PaintFixture();

    /* An empty record list is a legitimate build: a document that draws nothing has an empty
     * display list and no scopes. */
    RoboCheckEqInt((int)PaintListCreate(&records, NULL), (int)RoboOk);
    OpacBuild(records, &list, &stats);
    RoboCheckEqInt((int)stats.opacityScopes, 0);
    RoboCheckEqInt((int)PaintDisplayListCount(list), 0);
    PaintDisplayListDestroy(list);
    list = NULL;
    PaintListDestroy(records);

    /*
     * **A malformed record never enters a list.** An opacity outside `[0, 1]` is not a value at all
     * -- unlike a clip, where a bounding axis is a legal record whatever it bounds -- so the record
     * layer's validity rule refuses it on **every** kind, and the list's append is the boundary that
     * keeps it out. The cases are the ways a caller could get one: above one, below zero, infinite
     * and not-a-number.
     */
    {
        static const double bad[4] = { 1.5, -0.5, 1.0e300 * 1.0e300, 0.0 };
        size_t index;

        RoboCheckEqInt((int)PaintListCreate(&records, NULL), (int)RoboOk);
        for (index = 0u; index < 4u; index++) {
            PaintOperationInit(&operation);
            operation.kind = RoboPaintOpBackground;
            operation.node = 1u;
            operation.depth = 0u;
            operation.boxes = OpacBoxes(0, 0, 10, 10);
            operation.color = CssColorMake(255u, 0u, 0u, 255u);
            /* The fourth case is a NaN, built from the volatile zero so no optimiser can fold it
             * into a constant. */
            operation.opacity.value = (index == 3u) ? (zero / zero) : bad[index];
            OpacCheckInt(PaintOperationIsValid(&operation), 0,
                           "an out-of-range record is not a record");
            PaintDiagReset(&diag);
            RoboCheckEqInt((int)PaintListAppend(records, &operation, &diag),
                              (int)RoboInvalidArgument);
        }
        RoboCheckEqInt((int)PaintListCount(records), 0);
        PaintListDestroy(records);
        records = NULL;
    }

    /* The same rule one stage along, on a display command. */
    RoboCheckEqInt((int)PaintDisplayListCreate(&list, NULL), (int)RoboOk);
    PaintDisplayCommandInit(&command);
    command.kind = RoboPaintDisplayCmdDrawBackground;
    command.node = 1u;
    command.rect = OpacRect(0, 0, 10, 10);
    command.color = CssColorMake(255u, 0u, 0u, 255u);
    command.opacity.value = 2.0;
    OpacCheckInt(PaintDisplayCommandIsValid(&command), 0,
                   "an out-of-range command is not a command");
    PaintDiagReset(&diag);
    RoboCheckEqInt((int)PaintDisplayListAppend(list, &command, &diag),
                      (int)RoboInvalidArgument);
    RoboCheckEqInt((int)PaintDisplayListCount(list), 0);

    /* A scope command whose strength is opaque is refused by the vocabulary: a scope that
     * establishes nothing is a command a consumer could only skip. */
    PaintDisplayCommandInit(&command);
    command.kind = RoboPaintDisplayCmdPushOpacity;
    command.node = 1u;
    command.rect = OpacRect(0, 0, 10, 10);
    command.opacity = PaintOpacityOpaque();
    OpacCheckInt(PaintDisplayCommandIsValid(&command), 0,
                   "an opaque scope command is not a command");
    command.opacity = OpacOf(0.5);
    OpacCheckInt(PaintDisplayCommandIsValid(&command), 1,
                   "a real scope command is a command");
    PaintDisplayListDestroy(list);
    list = NULL;

    /* An empty document: the pass succeeds, nothing is declared and no scope opens. */
    {
        PaintFixtureDoc fixture;
        PaintResult result;

        PaintBeginLaidOut(&fixture, "<html><body></body></html>", OPAC_BASE_CSS);
        PaintOnce(&fixture, &result);
        RoboCheckEqInt((int)result.stats.opacityDeclared, 0);
        RoboCheckEqInt((int)result.stats.opacityScopes, 0);
        OpacListIs(&result, 1.0, "an empty document declares nothing");
        PaintResultDispose(&result);
        PaintEnd(&fixture);
    }

    /* A **childless** faded box: the scope is a statement about the box rather than about its
     * subtree, exactly as its content boundary is. */
    {
        PaintFixtureDoc fixture;
        PaintResult result;

        PaintBeginLaidOut(&fixture,
                             "<html><body><div class=\"solo\"></div></body></html>",
                             OPAC_BASE_CSS " .solo { width: 10px; height: 0px; opacity: 0 }");
        PaintOnce(&fixture, &result);
        RoboCheckEqInt((int)result.stats.opacityScopes, 1);
        RoboCheckEqInt((int)PaintCountOf(&result, RoboPaintOpPushOpacity), 1);
        RoboCheckEqInt((int)PaintCountOf(&result, RoboPaintOpPopOpacity), 1);
        PaintResultDispose(&result);
        PaintEnd(&fixture);
    }

    /* A document with no computed styles at all declares nothing, and every record is opaque --
     * "missing style" answers the same way "no declaration" does. */
    {
        PaintFixtureDoc fixture;
        PaintResult result;

        PaintBeginLaidOut(&fixture, "<html><body><p>x</p></body></html>", OPAC_BASE_CSS);
        PaintOnce(&fixture, &result);
        OpacListIs(&result, 1.0, "a document with no declarations is fully opaque");
        PaintResultDispose(&result);
        PaintEnd(&fixture);
    }

    RoboCheckEqInt((int)PaintLiveBlocks(), (int)baseline);
}

/* ------------------------------------------------------------------ */
/* Registration                                                        */
/* ------------------------------------------------------------------ */

void TestRegisterPaintOpacity(void)
{
    TestRegister("paint.opacity.record", TestOpacityRecord);
    TestRegister("paint.opacity.record_safety", TestOpacityRecordSafety);
    TestRegister("paint.opacity.byte_conversion", TestOpacityByteConversion);
    TestRegister("paint.opacity.stack", TestOpacityStack);
    TestRegister("paint.opacity.stack_growth", TestOpacityStackGrowth);
    TestRegister("paint.opacity.default", TestOpacityDefault);
    TestRegister("paint.opacity.explicit_one", TestOpacityExplicitOne);
    TestRegister("paint.opacity.declared", TestOpacityDeclared);
    TestRegister("paint.opacity.scope_placement", TestOpacityScopePlacement);
    TestRegister("paint.opacity.nested", TestOpacityNested);
    TestRegister("paint.opacity.nesting_table", TestOpacityNestingTable);
    TestRegister("paint.opacity.sibling_isolation", TestOpacitySiblingIsolation);
    TestRegister("paint.opacity.zero", TestOpacityZero);
    TestRegister("paint.opacity.text", TestOpacityText);
    TestRegister("paint.opacity.determinism", TestOpacityDeterminism);
    TestRegister("paint.opacity.mutation_isolation", TestOpacityMutationIsolation);
    TestRegister("paint.opacity.display_commands", TestOpacityDisplayCommands);
    TestRegister("paint.opacity.hand_built_scope", TestOpacityHandBuiltScope);
    TestRegister("paint.opacity.builder_refusals", TestOpacityBuilderRefusals);
    TestRegister("paint.opacity.pass_refusals", TestOpacityPassRefusals);
    TestRegister("paint.opacity.background_pixels", TestOpacityBackgroundPixels);
    TestRegister("paint.opacity.border_pixels", TestOpacityBorderPixels);
    TestRegister("paint.opacity.text_pixels", TestOpacityTextPixels);
    TestRegister("paint.opacity.no_opacity_unchanged", TestOpacityNoOpacityUnchanged);
    TestRegister("paint.opacity.group_deviation", TestOpacityGroupDeviation);
    TestRegister("paint.opacity.end_to_end", TestOpacityEndToEnd);
    TestRegister("paint.opacity.dump", TestOpacityDump);
    TestRegister("paint.opacity.empty_and_malformed", TestOpacityEmptyAndMalformed);
}
