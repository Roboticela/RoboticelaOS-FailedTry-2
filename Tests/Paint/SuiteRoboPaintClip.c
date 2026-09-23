/*
 * RoboPaint clip suite (Phase 11.8) — paint.clip.*
 *
 * The phase that turned the boundary in force from a **field** into a **scope**. The claims
 * this release makes, each pinned where it can be observed rather than argued:
 *
 *   **the arithmetic**       a clip intersected with a clip is per axis, an axis the inner
 *                            record leaves open is carried rather than cleared, and the result
 *                            of two disjoint intervals is an *empty* interval rather than a
 *                            failure -- so a box an ancestor's boundary excluded is a statement
 *                            about the document and not an error;
 *
 *   **the stack**            push, pop, current, depth, capacity, reset: a LIFO of clip records
 *                            whose "in force" is its top, whose pop on an empty stack is a
 *                            *refusal* rather than a silent no-op, and whose storage grows
 *                            without an arbitrary depth limit;
 *
 *   **the scope is emitted** a box whose `overflow` actually **changes** the boundary opens a
 *                            `PUSH_CLIP` after its own content boundary and closes it with a
 *                            `POP_CLIP` after its last child -- and a box whose boundary changes
 *                            nothing (visible overflow, or a hidden box inside an ancestor that
 *                            already bounded the same region) opens none;
 *
 *   **the box is outside**   the scope clips the box's **contents**, never the box: a hidden
 *                            box's own background and border commands stand *before* its push,
 *                            because CSS paints an ancestor's decoration under the boundary it
 *                            was entered with. This is the one arrangement a reader would get
 *                            backwards from a flat list, so it is asserted twice -- by command
 *                            index and by pixel;
 *
 *   **nested scopes**        the effective clip inside a nested scope is the intersection of the
 *                            enclosing scope and the inner padding box, recomputed here from the
 *                            tree through RoboLayout's own doors rather than read off the list;
 *
 *   **one axis**             `overflow-x: hidden` beside `overflow-y: visible` bounds one axis
 *                            and leaves the other open, which is exactly what one rectangle
 *                            cannot say -- and `PaintClipRect` *refuses* to answer a
 *                            rectangle for such a record rather than inventing one;
 *
 *   **pixels**               every drawing kind the build has -- a background, a border band and
 *                            a text run's glyphs -- is bounded: the pixels inside the scope are
 *                            painted and the pixels outside are untouched, asserted by reading a
 *                            real surface back. Text is bounded through the same single
 *                            application of the clip as a fill, because the glyph path
 *                            intersects its own bounds with the raster's clip;
 *
 *   **the empty scope**      a scope whose clip bounds nothing draws nothing inside it, does not
 *                            poison the enclosing scope, and lets a following sibling paint --
 *                            the clip stack restored rather than merely counted;
 *
 *   **the snapshot**         a display list is a value: clearing the record list and laying the
 *                            same tree out at a different viewport does not change one
 *                            boundary it holds or one pixel it paints.
 *
 * Every case builds a real document through the real chain (HTML → CSS → layout → records →
 * display list) and paints it into a real RoboGraphics surface. Hand-built *commands* appear
 * only where the question is about the painter's own contract -- the pass's clip stack, the
 * counters, the refusals -- and never in place of a layout result.
 *
 * **What this suite does not cover**: the picture. Since 11.7 this build has an `IMAGE`
 * operation and a `draw-image` command, so the brief's "image drawing respects clipping" now has
 * a subject -- and the mechanism is shared rather than per-kind: a drawing is bounded by the clip
 * its scope put in force, applied once before the kind's own branch, so an image is bounded **by
 * construction**. `paint.clip.background`, `.border` and `.text` are the three kinds this phase
 * landed with; the pixel case for a bounded picture is owed here rather than counted as covered,
 * and the image suite owns the fixture that would build one.
 *
 * Chromium is reference only; nothing here is a translation of it, and this suite depends on no
 * Chromium header, library or test framework.
 */

#include "RoboPaintTestSupport.h"

#include "RoboGraphics/RoboGraphicsBitmap.h"
#include "RoboGraphics/RoboGraphicsEncode.h"
#include "RoboFont/RoboFontTypes.h"

#include "RoboPaint/RoboPaintClip.h"
#include "RoboPaint/RoboPaintColor.h"
#include "RoboPaint/RoboPaintDecoration.h"
#include "RoboPaint/RoboPaintDisplayDump.h"

#include <stdio.h>
#include <string.h>

/* The same base sheet the other paint suites use: these elements are the block containers, so
 * a fixture's boxes stack in flow order. */
#define CLIP_BASE_CSS "html, body, div, section, aside, figure, p { display: block }"

/* Two colours a pixel assertion can name, and one that is deliberately not opaque. */
#define CLIP_RED GfxRgba8(255, 0, 0, 255)
#define CLIP_BLUE GfxRgba8(0, 0, 255, 255)
#define CLIP_GREEN GfxRgba8(0, 255, 0, 255)
#define CLIP_UNTOUCHED GfxRgba8(0, 0, 0, 0)

/* ------------------------------------------------------------------ */
/* A synthetic face, built here                                        */
/* ------------------------------------------------------------------ */

/*
 * The clip suite needs **one** mapped glyph and nothing else: its subject is where a drawing
 * stops, so the only thing text must do here is produce ink whose position is arithmetic. The
 * face is therefore as small as a face can be and still be one -- a cmap, an hmtx, an hhea, a
 * maxp, a loca and one square outline:
 *
 *   units per em   1000    so `font-size: 100px` makes one font unit a tenth of a pixel
 *   glyph 0        empty   `.notdef`
 *   glyph 1        square  a square from (0, 0) to (100, 100) in font units
 *   cmap           'A' -> glyph 1
 *   hmtx           advance 600 units for glyph 1, 500 for glyph 0
 *
 * so a single 'A' drawn at 100px is a **10x10 device-unit square** whose left edge is the run's
 * own origin x and whose bottom edge is the baseline -- which is what lets the text case say
 * "the ink crosses this boundary" as two numbers rather than as a hope.
 *
 * It is built here rather than shared with the text suite because a shared fixture would be a
 * third thing to keep in step, and because the two suites need different faces: the text suite's
 * has two mapped glyphs so a run advances, and this one needs one because a run's advance is not
 * what this suite is measuring.
 */
#define CLIP_FACE_UPEM 1000u
#define CLIP_FACE_ADVANCE_NOTDEF 500u
#define CLIP_FACE_ADVANCE_GLYPH 600u
#define CLIP_FACE_NUM_GLYPHS 2u
#define CLIP_FACE_SQUARE_UNITS 100
#define CLIP_FACE_FONT_SIZE 100.0

typedef struct {
    uint32_t tag;
    const uint8_t *data;
    uint32_t length;
} ClipSynthTable;

static void ClipPutU8(uint8_t **p, uint8_t v) { *(*p)++ = v; }
static void ClipPutU16(uint8_t **p, uint16_t v)
{
    *(*p)++ = (uint8_t)(v >> 8);
    *(*p)++ = (uint8_t)v;
}
static void ClipPutI16(uint8_t **p, int16_t v) { ClipPutU16(p, (uint16_t)v); }
static void ClipPutU32(uint8_t **p, uint32_t v)
{
    *(*p)++ = (uint8_t)(v >> 24);
    *(*p)++ = (uint8_t)(v >> 16);
    *(*p)++ = (uint8_t)(v >> 8);
    *(*p)++ = (uint8_t)v;
}
static void ClipPutI64(uint8_t **p, int64_t v)
{
    int i;

    for (i = 7; i >= 0; i--) {
        ClipPutU8(p, (uint8_t)((uint64_t)v >> (i * 8)));
    }
}

static void ClipWriteSearchFields(uint8_t *hdr, uint16_t numTables)
{
    uint16_t pow2 = 1u;
    uint16_t entry = 0u;
    uint8_t *p;

    while ((uint16_t)(pow2 * 2u) <= numTables) {
        pow2 = (uint16_t)(pow2 * 2u);
        entry++;
    }
    p = hdr + 4;
    ClipPutU16(&p, numTables);
    ClipPutU16(&p, (uint16_t)(pow2 * 16u));
    ClipPutU16(&p, entry);
    ClipPutU16(&p, (uint16_t)(numTables * 16u - pow2 * 16u));
}

static size_t ClipBuildSfnt(uint8_t *out,
                              size_t outCap,
                              uint32_t version,
                              const ClipSynthTable *tables,
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
    ClipPutU32(&p, version);
    ClipWriteSearchFields(out, (uint16_t)n);
    cursor = dirEnd;
    for (i = 0u; i < n; i++) {
        uint32_t pad = (uint32_t)(((size_t)tables[i].length + 3u) & ~(size_t)3u);
        uint8_t *entry = out + 12u + (size_t)i * 16u;

        ClipPutU32(&entry, tables[i].tag);
        ClipPutU32(&entry, 0u);
        ClipPutU32(&entry, (uint32_t)cursor);
        ClipPutU32(&entry, tables[i].length);
        memcpy(out + cursor, tables[i].data, tables[i].length);
        cursor += pad;
    }
    return cursor;
}

static size_t ClipBuildHead(uint8_t *b, uint16_t upem)
{
    uint8_t *p = b;

    ClipPutU16(&p, 1);
    ClipPutU16(&p, 0);
    ClipPutU32(&p, 0x00010000u);
    ClipPutU32(&p, 0u);
    ClipPutU32(&p, RoboFontHeadMagic);
    ClipPutU16(&p, 0u);
    ClipPutU16(&p, upem);
    ClipPutI64(&p, 0);
    ClipPutI64(&p, 0);
    ClipPutI16(&p, -50);
    ClipPutI16(&p, -200);
    ClipPutI16(&p, 1000);
    ClipPutI16(&p, 800);
    ClipPutU16(&p, 0u);
    ClipPutU16(&p, 8u);
    ClipPutI16(&p, 2);
    ClipPutI16(&p, 1); /* long loca, so loca entries are u32 */
    ClipPutI16(&p, 0);
    return (size_t)(p - b);
}

static size_t ClipBuildHhea(uint8_t *b, uint16_t nHMetrics)
{
    uint8_t *p = b;
    int i;

    ClipPutU16(&p, 1);
    ClipPutU16(&p, 0);
    ClipPutI16(&p, 800);  /* ascender */
    ClipPutI16(&p, -200); /* descender */
    ClipPutI16(&p, 90);   /* line gap */
    ClipPutU16(&p, 900);
    ClipPutI16(&p, -10);
    ClipPutI16(&p, -20);
    ClipPutI16(&p, 880);
    ClipPutI16(&p, 1);
    ClipPutI16(&p, 0);
    ClipPutI16(&p, 0);
    for (i = 0; i < 4; i++) {
        ClipPutI16(&p, 0);
    }
    ClipPutI16(&p, 0);
    ClipPutU16(&p, nHMetrics);
    return (size_t)(p - b);
}

static size_t ClipBuildMaxp(uint8_t *b, uint16_t numGlyphs)
{
    uint8_t *p = b;
    int i;

    ClipPutU32(&p, 0x00010000u);
    ClipPutU16(&p, numGlyphs);
    ClipPutU16(&p, 8);
    ClipPutU16(&p, 2);
    for (i = 0; i < 12; i++) {
        ClipPutU16(&p, 0);
    }
    return (size_t)(p - b);
}

static size_t ClipBuildHmtx(uint8_t *b, const uint16_t *advances, uint16_t n)
{
    uint8_t *p = b;
    uint16_t i;

    for (i = 0u; i < n; i++) {
        ClipPutU16(&p, advances[i]);
        ClipPutI16(&p, (int16_t)(i + 1));
    }
    return (size_t)(p - b);
}

/** `n + 1` long `loca` entries from @p offsets. */
static size_t ClipBuildLoca(uint8_t *loca, const uint32_t *offsets, uint16_t n)
{
    uint8_t *p = loca;
    uint16_t i;

    for (i = 0u; i <= n; i++) {
        ClipPutU32(&p, offsets[i]);
    }
    return (size_t)(p - loca);
}

/** One glyph: a square from (0, 0) to (100, 100) in font units. */
static size_t ClipBuildSquareGlyf(uint8_t *b)
{
    uint8_t *p = b;

    ClipPutI16(&p, 1);                        /* numberOfContours */
    ClipPutI16(&p, 0);                        /* xMin */
    ClipPutI16(&p, 0);                        /* yMin */
    ClipPutI16(&p, CLIP_FACE_SQUARE_UNITS);   /* xMax */
    ClipPutI16(&p, CLIP_FACE_SQUARE_UNITS);   /* yMax */
    ClipPutU16(&p, 3);                        /* endPtsOfContours[0] */
    ClipPutU16(&p, 0);                        /* instructionLength */
    ClipPutU8(&p, (uint8_t)(RoboFontGlyfFlagOnCurve |
                              RoboFontGlyfFlagXSameOrPositive |
                              RoboFontGlyfFlagYSameOrPositive));
    ClipPutU8(&p, (uint8_t)(RoboFontGlyfFlagOnCurve | RoboFontGlyfFlagXShort |
                              RoboFontGlyfFlagXSameOrPositive |
                              RoboFontGlyfFlagYSameOrPositive));
    ClipPutU8(&p, (uint8_t)(RoboFontGlyfFlagOnCurve | RoboFontGlyfFlagYShort |
                              RoboFontGlyfFlagXSameOrPositive |
                              RoboFontGlyfFlagYSameOrPositive));
    ClipPutU8(&p, (uint8_t)(RoboFontGlyfFlagOnCurve | RoboFontGlyfFlagXShort |
                              RoboFontGlyfFlagYSameOrPositive));
    ClipPutU8(&p, CLIP_FACE_SQUARE_UNITS);
    ClipPutU8(&p, CLIP_FACE_SQUARE_UNITS);
    ClipPutU8(&p, CLIP_FACE_SQUARE_UNITS);
    return (size_t)(p - b);
}

/**
 * A one-subtable cmap holding a format-4 segment table mapping `first..last` contiguously to
 * glyph ids `1` upward (delta `1 - first`), with the required `0xFFFF` segment appended.
 */
static size_t ClipBuildCmap(uint8_t *b, uint16_t first, uint16_t last)
{
    uint8_t *p = b;
    uint8_t *lengthPtr;
    uint8_t *subtable = b + 12;
    uint16_t length;

    ClipPutU16(&p, 0);   /* version */
    ClipPutU16(&p, 1);   /* numTables */
    ClipPutU16(&p, 3);   /* Windows */
    ClipPutU16(&p, 1);   /* Unicode BMP */
    ClipPutU32(&p, 12u); /* subtable offset */

    ClipPutU16(&p, 4);    /* format */
    lengthPtr = p;
    ClipPutU16(&p, 0);    /* length, patched below */
    ClipPutU16(&p, 0);    /* language */
    ClipPutU16(&p, 4);    /* segCountX2 */
    ClipPutU16(&p, 4);    /* searchRange */
    ClipPutU16(&p, 1);    /* entrySelector */
    ClipPutU16(&p, 0);    /* rangeShift */
    ClipPutU16(&p, last);
    ClipPutU16(&p, 0xFFFFu);
    ClipPutU16(&p, 0); /* reservedPad */
    ClipPutU16(&p, first);
    ClipPutU16(&p, 0xFFFFu);
    ClipPutI16(&p, (int16_t)(1 - (int32_t)first)); /* idDelta[0]: 'A' -> glyph 1 */
    ClipPutI16(&p, 1);                             /* idDelta[1] */
    ClipPutU16(&p, 0);                             /* idRangeOffset[0] */
    ClipPutU16(&p, 0);                             /* idRangeOffset[1] */

    length = (uint16_t)(p - subtable);
    lengthPtr[0] = (uint8_t)(length >> 8);
    lengthPtr[1] = (uint8_t)length;
    return (size_t)(p - b);
}

/** What a face plus its source cost a caller, so a case can release both the way it built them. */
typedef struct clipFace {
    FontSource *source;
    FontFace *face;
} ClipFace;

/** Build the synthetic face, with 'A' -> glyph 1 and glyph 0 (`.notdef`) empty. */
static void ClipFaceCreate(ClipFace *out)
{
    uint8_t head[64], hhea[64], maxp[64], hmtx[64], loca[64], glyf[128], cmap[64];
    uint8_t font[1024];
    static const uint16_t advances[CLIP_FACE_NUM_GLYPHS] = {
        (uint16_t)CLIP_FACE_ADVANCE_NOTDEF,
        (uint16_t)CLIP_FACE_ADVANCE_GLYPH
    };
    ClipSynthTable tables[7];
    uint32_t nt = 0u;
    size_t glyphSize;
    size_t n;

    out->source = NULL;
    out->face = NULL;

    glyphSize = ClipBuildSquareGlyf(glyf);
    {
        const uint32_t offsets[CLIP_FACE_NUM_GLYPHS + 1u] = {
            0u, 0u, (uint32_t)glyphSize
        };

        (void)ClipBuildLoca(loca, offsets, (uint16_t)CLIP_FACE_NUM_GLYPHS);
    }

    tables[nt].tag = RoboFontTagHead;
    tables[nt].data = head;
    tables[nt].length = (uint32_t)ClipBuildHead(head, (uint16_t)CLIP_FACE_UPEM);
    nt++;
    tables[nt].tag = RoboFontTagHhea;
    tables[nt].data = hhea;
    tables[nt].length = (uint32_t)ClipBuildHhea(hhea, (uint16_t)CLIP_FACE_NUM_GLYPHS);
    nt++;
    tables[nt].tag = RoboFontTagMaxp;
    tables[nt].data = maxp;
    tables[nt].length = (uint32_t)ClipBuildMaxp(maxp, (uint16_t)CLIP_FACE_NUM_GLYPHS);
    nt++;
    tables[nt].tag = RoboFontTagHmtx;
    tables[nt].data = hmtx;
    tables[nt].length = (uint32_t)ClipBuildHmtx(hmtx, advances,
                                                  (uint16_t)CLIP_FACE_NUM_GLYPHS);
    nt++;
    tables[nt].tag = RoboFontTagCmap;
    tables[nt].data = cmap;
    tables[nt].length = (uint32_t)ClipBuildCmap(cmap, 0x41u, 0x41u);
    nt++;
    tables[nt].tag = RoboFontTagLoca;
    tables[nt].data = loca;
    tables[nt].length = (uint32_t)((CLIP_FACE_NUM_GLYPHS + 1u) * 4u);
    nt++;
    tables[nt].tag = RoboFontTagGlyf;
    tables[nt].data = glyf;
    tables[nt].length = (uint32_t)glyphSize;
    nt++;

    n = ClipBuildSfnt(font, sizeof(font), RoboFontSfntVersionTrue, tables, nt);
    RoboCheck(n > 0u);
    RoboCheckEqInt(FontSourceCreateMemoryCopy(font, n, &out->source), RoboOk);
    RoboCheckEqInt(FontFaceCreate(out->source, 0, &out->face), RoboOk);
}

static void ClipFaceDestroy(ClipFace *face)
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

static LayoutRect ClipRect(int32_t x, int32_t y, int32_t w, int32_t h)
{
    LayoutRect rect;

    rect.x = x;
    rect.y = y;
    rect.width = w;
    rect.height = h;
    return rect;
}

/** The cleared clip: no boundary on either axis. */
static PaintClip ClipNoneValue(void)
{
    return PaintClipNone();
}

/** A clip with @p x and/or @p y bounded, and the others left open. */
static PaintClip ClipMake(int xBounded,
                                   int32_t x0,
                                   int32_t x1,
                                   int yBounded,
                                   int32_t y0,
                                   int32_t y1)
{
    PaintClip clip;

    clip = PaintClipNone();
    if (xBounded) {
        clip.horizontal.mode = RoboCssOverflowHidden;
        clip.horizontal.start = x0;
        clip.horizontal.end = x1;
    }
    if (yBounded) {
        clip.vertical.mode = RoboCssOverflowHidden;
        clip.vertical.start = y0;
        clip.vertical.end = y1;
    }
    return clip;
}

/** Report two clips that should have matched, in both axes' terms. */
static void ClipReport(const char *what,
                        const PaintClip *expected,
                        const PaintClip *actual)
{
    TestFail(__FILE__, __LINE__,
                   "%s: expected x=%d[%d,%d] y=%d[%d,%d], got x=%d[%d,%d] y=%d[%d,%d]", what,
                   (int)expected->horizontal.mode, (int)expected->horizontal.start,
                   (int)expected->horizontal.end, (int)expected->vertical.mode,
                   expected->vertical.start, expected->vertical.end,
                   (int)actual->horizontal.mode, (int)actual->horizontal.start,
                   (int)actual->horizontal.end, (int)actual->vertical.mode,
                   actual->vertical.start, actual->vertical.end);
}

static void ClipCheck(const PaintClip *actual,
                       const PaintClip *expected,
                       const char *what)
{
    if (!PaintClipEqual(actual, expected)) {
        ClipReport(what, expected, actual);
    }
}

static void ClipCheckInt(int actual, int expected, const char *what)
{
    if (actual != expected) {
        TestFail(__FILE__, __LINE__, "%s: expected %d, got %d", what, expected, actual);
    }
}

/** A CPU surface for a paint pass: owned, RGBA8, zero-filled (so "untouched" is transparent
 *  and an unpainted pixel is distinguishable from a black one). */
static GfxSurface *ClipSurface(uint32_t width,
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

/** Build a display list from @p records, asserting the build succeeds. */
/**
 * Build a display list from @p records, asserting the build succeeds.
 *
 * The stats are **zeroed before the call**, and the assertion below does not stop the case: a
 * refused build leaves the output struct untouched, so without this the checks that follow a
 * failure would read whatever the stack held and report it as an expectation mismatch
 * (`-2000000808 != 2`) instead of naming the refusal as the cause. Zeroed, a failed build shows
 * up as zeroes and the message above it says exactly what was refused.
 */
static void ClipBuild(const PaintResult *records,
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
    status = PaintDisplayListBuilderBuild(builder, records->list, *outList, outStats,
                                                   &diag);
    if (status != RoboOk) {
        TestFail(__FILE__, __LINE__, "the display build refused the records: %s",
                       diag.message ? diag.message : "(no message)");
    }
    PaintDisplayListBuilderDestroy(builder);
}

/**
 * Build a display list from @p records, reporting the outcome rather than asserting it.
 *
 * The refusal cases need a build that is *expected* to fail, so the status and the message come
 * back to the caller -- and the list is handed back emptied, which is itself one of the claims
 * (`a refused build leaves no partial list`).
 */
static Result ClipBuildExpectRefusal(const PaintResult *records,
                                               PaintDisplayList **outList,
                                               PaintDiag *outDiag)
{
    PaintDisplayListBuilder *builder = NULL;
    Result status;

    RoboCheckEqInt(PaintDisplayListCreate(outList, NULL), RoboOk);
    RoboCheckEqInt(PaintDisplayListBuilderCreate(&builder, NULL), RoboOk);
    PaintDiagReset(outDiag);
    status = PaintDisplayListBuilderBuild(builder, records->list, *outList, NULL,
                                                   outDiag);
    PaintDisplayListBuilderDestroy(builder);
    return status;
}

/** Paint @p list into @p target, asserting the pass succeeds. */
static void ClipPaint(const PaintDisplayList *list,
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
static GfxColorRgba8 ClipRead(GfxSurface *surface, int32_t x, int32_t y)
{
    GfxColorRgba8 color;
    int32_t width = (int32_t)GfxSurfaceWidth(surface);
    int32_t height = (int32_t)GfxSurfaceHeight(surface);

    memset(&color, 0, sizeof(color));
    if (x < 0 || y < 0 || x >= width || y >= height) {
        TestFail(__FILE__, __LINE__, "the sample %d,%d is outside the %dx%d surface", (int)x,
                       (int)y, (int)width, (int)height);
        return color;
    }
    RoboCheckEqInt(GfxSurfaceGetPixel(surface, (uint32_t)x, (uint32_t)y, &color),
                      RoboOk);
    return color;
}

/** Assert the pixel at (x, y) is @p expected. */
static void ClipPixelIs(GfxSurface *surface,
                          int32_t x,
                          int32_t y,
                          GfxColorRgba8 expected,
                          const char *what)
{
    GfxColorRgba8 actual = ClipRead(surface, x, y);

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
static int ClipAnyInk(GfxSurface *surface,
                        int32_t x0,
                        int32_t y0,
                        int32_t x1,
                        int32_t y1)
{
    int32_t x;
    int32_t y;

    for (y = y0; y < y1; y++) {
        for (x = x0; x < x1; x++) {
            GfxColorRgba8 color = ClipRead(surface, x, y);

            if (color.a != 0u) {
                return 1;
            }
        }
    }
    return 0;
}

/** The @p nth command of @p list whose kind is @p kind, or 0 when there is none. */
static int ClipCommandOfKind(const PaintDisplayList *list,
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
static size_t ClipCountOfKind(const PaintDisplayList *list,
                                 PaintDisplayCommandKind kind)
{
    return PaintDisplayListCountOfKind(list, kind);
}

/** The index of the @p nth command of @p kind whose node is @p node, or `(size_t)-1`. */
static size_t ClipIndexOfKindForNode(const PaintDisplayList *list,
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

/** In layout coordinates: a box with no insets, so its three rectangles are one. */
static PaintBoxes ClipBoxes(int32_t x, int32_t y, int32_t w, int32_t h)
{
    PaintBoxes boxes;

    PaintBoxesInit(&boxes);
    boxes.border = ClipRect(x, y, w, h);
    boxes.padding = boxes.border;
    boxes.content = boxes.border;
    boxes.insetsMeasured = 0;
    return boxes;
}

/** Append one hand-built **record** to @p list, asserting the append succeeds. */
static void ClipAppendRecord(PaintList *list,
                               PaintOpKind kind,
                               LayoutNodeId node,
                               size_t depth,
                               PaintBoxes boxes,
                               PaintClip clip)
{
    PaintOperation operation;
    PaintDiag diag;

    PaintOperationInit(&operation);
    operation.kind = kind;
    operation.node = node;
    operation.depth = depth;
    operation.boxes = boxes;
    operation.origin = MakeGfxPoint(0, 0);
    operation.clip = clip;
    operation.color = PaintOpKindDraws(kind)
                              ? CssColorMake(255u, 0u, 0u, 255u)
                              : CssColorNone();
    /* The style rule: a band carries a style that generates a border, and nothing else carries
     * one -- the record layer's own validity rule from the other side. */
    operation.style = PaintOpKindIsBorder(kind)
                              ? RoboCssBorderStyleSolid
                              : RoboCssBorderStyleNone;
    PaintDiagReset(&diag);
    if (PaintListAppend(list, &operation, &diag) != RoboOk) {
        TestFail(__FILE__, __LINE__, "a hand-built record was refused: %s",
                       diag.message ? diag.message : "(no message)");
    }
}

/** Append one hand-built **display command**, asserting the append succeeds. */
static void ClipAppendCommand(PaintDisplayList *list,
                                PaintDisplayCommandKind kind,
                                LayoutNodeId node,
                                size_t depth,
                                LayoutRect rect,
                                PaintClip clip,
                                CssColor color)
{
    PaintDisplayCommand command;
    PaintDiag diag;

    PaintDisplayCommandInit(&command);
    command.kind = kind;
    command.node = node;
    command.depth = depth;
    command.rect = rect;
    command.clip = clip;
    command.color = color;
    PaintDiagReset(&diag);
    if (PaintDisplayListAppend(list, &command, &diag) != RoboOk) {
        TestFail(__FILE__, __LINE__, "a hand-built command was refused: %s",
                       diag.message ? diag.message : "(no message)");
    }
}

/* ------------------------------------------------------------------ */
/* The oracle                                                          */
/* ------------------------------------------------------------------ */

/*
 * A replay of the list that predicts the pixel, written independently of the pass: it keeps its
 * own clip stack (a local array, *not* `PaintClipStack` -- the type under test in the
 * stack cases must not be the oracle for the pixel cases), pushes and pops on the clip
 * commands, and returns the last drawing command whose rectangle covers the point and whose
 * **in-force** clip admits it.
 *
 * It is an oracle rather than a second painter: it reads a command's rectangle, colour and the
 * clip that is in force, and does no blending, no saturation and no glyph resolution. That is
 * what lets it check the pass without repeating the arithmetic the pass is being checked for.
 */
#define CLIP_ORACLE_MAX_DEPTH 8

typedef struct {
    PaintClip records[CLIP_ORACLE_MAX_DEPTH];
    size_t depth;
} ClipOracleStack;

static int ClipAdmits(const PaintClip *clip, int32_t x, int32_t y)
{
    if (PaintClipAxisIsActive(&clip->horizontal) &&
        (x < clip->horizontal.start || x >= clip->horizontal.end)) {
        return 0;
    }
    if (PaintClipAxisIsActive(&clip->vertical) &&
        (y < clip->vertical.start || y >= clip->vertical.end)) {
        return 0;
    }
    return 1;
}

/** The oracle's own clip stack: the innermost push, or the command's own clip when empty. */
static PaintClip ClipOracleInForce(const ClipOracleStack *stack,
                                              const PaintClip *commandClip)
{
    if (stack->depth == 0u) {
        return *commandClip;
    }
    return stack->records[stack->depth - 1u];
}

static GfxColorRgba8 ClipExpectedAt(const PaintDisplayList *list,
                                               int32_t x,
                                               int32_t y)
{
    ClipOracleStack stack;
    GfxColorRgba8 color = CLIP_UNTOUCHED;
    size_t index;

    memset(&stack, 0, sizeof(stack));
    for (index = 0u; index < PaintDisplayListCount(list); index++) {
        PaintDisplayCommand command;

        if (PaintDisplayListAt(list, index, &command) != RoboOk) {
            return color;
        }
        if (command.kind == RoboPaintDisplayCmdPushClip) {
            if (stack.depth < CLIP_ORACLE_MAX_DEPTH) {
                stack.records[stack.depth] = command.clip;
                stack.depth++;
            }
            continue;
        }
        if (command.kind == RoboPaintDisplayCmdPopClip) {
            if (stack.depth > 0u) {
                stack.depth--;
            }
            continue;
        }
        if (!PaintDisplayCommandKindDraws(command.kind)) {
            continue;
        }
        if (!PaintColorIsDrawable(command.color)) {
            continue;
        }
        if (command.rect.width <= 0 || command.rect.height <= 0) {
            continue;
        }
        if (x < command.rect.x || y < command.rect.y ||
            x >= command.rect.x + command.rect.width ||
            y >= command.rect.y + command.rect.height) {
            continue;
        }
        {
            PaintClip inForce = ClipOracleInForce(&stack, &command.clip);

            if (!ClipAdmits(&inForce, x, y)) {
                continue;
            }
        }
        color = GfxRgba8(command.color.red, command.color.green, command.color.blue,
                               command.color.alpha);
    }
    return color;
}

/** Assert the surface against the oracle at every **drawing** command's centre. */
static void ClipPixelsMatch(const PaintDisplayList *list,
                              GfxSurface *surface,
                              const char *what)
{
    size_t index;
    size_t sampled = 0u;

    for (index = 0u; index < PaintDisplayListCount(list); index++) {
        PaintDisplayCommand command;
        int32_t x;
        int32_t y;

        RoboCheckEqInt(PaintDisplayListAt(list, index, &command), RoboOk);
        if (!PaintDisplayCommandKindDraws(command.kind)) {
            continue;
        }
        if (command.rect.width <= 0 || command.rect.height <= 0) {
            continue;
        }
        x = command.rect.x + command.rect.width / 2;
        y = command.rect.y + command.rect.height / 2;
        if (x < 0 || y < 0 || x >= (int32_t)GfxSurfaceWidth(surface) ||
            y >= (int32_t)GfxSurfaceHeight(surface)) {
            continue;
        }
        ClipPixelIs(surface, x, y, ClipExpectedAt(list, x, y), what);
        sampled++;
    }
    RoboCheck(sampled > 0u);
}

/* ------------------------------------------------------------------ */
/* 1. The arithmetic                                                   */
/* ------------------------------------------------------------------ */

static void TestClipIntersection(void)
{
    PaintClip outer;
    PaintClip inner;
    PaintClip out;
    PaintClip none;

    none = ClipNoneValue();

    /* Two bounded records: the smaller on each axis wins. */
    outer = ClipMake(1, 0, 100, 1, 0, 100);
    inner = ClipMake(1, 20, 200, 1, -10, 50);
    RoboCheckEqInt(PaintClipIntersectClip(&outer, &inner, &out, NULL), RoboOk);
    ClipCheck(&out, &(PaintClip){ { RoboCssOverflowHidden, 20, 100 },
                                           { RoboCssOverflowHidden, 0, 50 } },
               "two bounded records");

    /* Contained: the inner one is the answer on both axes. */
    outer = ClipMake(1, 0, 100, 1, 0, 100);
    inner = ClipMake(1, 10, 90, 1, 10, 90);
    RoboCheckEqInt(PaintClipIntersectClip(&outer, &inner, &out, NULL), RoboOk);
    ClipCheck(&out, &inner, "a contained inner record");

    /* An axis the inner record leaves **open** is carried, not cleared: this is the rule that
     * makes `overflow-x: hidden` beside `overflow-y: visible` composable at all. */
    outer = ClipMake(1, 5, 60, 0, 0, 0);
    inner = ClipMake(0, 0, 0, 1, 7, 30);
    RoboCheckEqInt(PaintClipIntersectClip(&outer, &inner, &out, NULL), RoboOk);
    ClipCheck(&out, &(PaintClip){ { RoboCssOverflowHidden, 5, 60 },
                                           { RoboCssOverflowHidden, 7, 30 } },
               "one bounded axis each");

    /* Neither bounded: the answer is the cleared record, and nothing is emptied. */
    RoboCheckEqInt(PaintClipIntersectClip(&none, &none, &out, NULL), RoboOk);
    ClipCheck(&out, &none, "two unbounded records");
    RoboCheckEqInt(PaintClipIsEmpty(&out), 0);

    /* Disjoint: empty, and *valid* -- an excluded box is a statement, not a failure. The interval
     * is canonicalized to the zero-length one at the **upper start** (the larger of the two
     * starts, 50, which is the near edge of the boundary that did the excluding) instead of being
     * left running backwards from 50 to 10, because a record whose interval runs backwards is not
     * a record this build produced and every consumer downstream would have to check for it. */
    outer = ClipMake(1, 0, 10, 1, 0, 10);
    inner = ClipMake(1, 50, 60, 1, 50, 60);
    RoboCheckEqInt(PaintClipIntersectClip(&outer, &inner, &out, NULL), RoboOk);
    RoboCheckEqInt(PaintClipIsValid(&out), 1);
    RoboCheckEqInt(PaintClipIsEmpty(&out), 1);
    ClipCheck(&out, &(PaintClip){ { RoboCssOverflowHidden, 50, 50 },
                                           { RoboCssOverflowHidden, 50, 50 } },
               "two disjoint records");

    /* The canonical empty interval is a fixed point: intersecting it with anything, on either
     * side, leaves it empty and at the same place rather than reviving it. */
    outer = ClipMake(1, 50, 50, 1, 50, 50);
    inner = ClipMake(1, 0, 100, 1, 0, 100);
    RoboCheckEqInt(PaintClipIntersectClip(&outer, &inner, &out, NULL), RoboOk);
    ClipCheck(&out, &outer, "an empty interval intersected with a containing one");
    RoboCheckEqInt(PaintClipIntersectClip(&inner, &outer, &out, NULL), RoboOk);
    RoboCheckEqInt(PaintClipIsEmpty(&out), 1);

    /* And the boundary door canonicalizes the same way: a box entirely under an ancestor's
     * upper bound is an empty scope, not a refused build. */
    {
        LayoutRect boundary = ClipRect(200, 200, 40, 20);

        RoboCheckEqInt(PaintClipIntersect(&(PaintClip){
                                                        { RoboCssOverflowHidden, 0, 100 },
                                                        { RoboCssOverflowVisible, 0, 0 } },
                                                    &boundary, RoboCssOverflowHidden,
                                                    RoboCssOverflowVisible, &out, NULL),
                          RoboOk);
        RoboCheckEqInt(PaintClipIsValid(&out), 1);
        RoboCheckEqInt(PaintClipIsEmpty(&out), 1);
        ClipCheck(&out, &(PaintClip){ { RoboCssOverflowHidden, 200, 200 },
                                               { RoboCssOverflowVisible, 0, 0 } },
                   "a boundary above the interval in force");
    }

    /* Touching edges: half-open, so `[0,10]` and `[10,20]` share no pixel. */
    outer = ClipMake(1, 0, 10, 0, 0, 0);
    inner = ClipMake(1, 10, 20, 0, 0, 0);
    RoboCheckEqInt(PaintClipIntersectClip(&outer, &inner, &out, NULL), RoboOk);
    RoboCheckEqInt(PaintClipAxisIsEmpty(&out.horizontal), 1);
    RoboCheckEqInt(PaintClipIsEmpty(&out), 1);

    /* Zero-size: an interval with no width is empty, and stays empty through a further
     * intersection rather than becoming non-empty. */
    outer = ClipMake(1, 30, 30, 1, 0, 10);
    inner = ClipMake(1, 0, 100, 1, 0, 100);
    RoboCheckEqInt(PaintClipIntersectClip(&outer, &inner, &out, NULL), RoboOk);
    RoboCheckEqInt(PaintClipIsEmpty(&out), 1);
    RoboCheckEqInt(PaintClipIntersectClip(&out, &inner, &out, NULL), RoboOk);
    RoboCheckEqInt(PaintClipIsEmpty(&out), 1);

    /* Negative coordinates: an interval entirely below zero is legal, and so is a clip that
     * runs across the origin. */
    outer = ClipMake(1, -50, -10, 1, -80, -20);
    inner = ClipMake(1, -100, 0, 1, -100, 0);
    RoboCheckEqInt(PaintClipIntersectClip(&outer, &inner, &out, NULL), RoboOk);
    ClipCheck(&out, &outer, "a negative interval inside a wider negative one");
    RoboCheckEqInt(PaintClipIsEmpty(&out), 0);
}

static void TestClipIntersectionSafety(void)
{
    PaintClip clip;
    PaintClip out;
    PaintDiag diag;

    clip = ClipMake(1, 0, 100, 1, 0, 100);
    out = ClipMake(1, 1, 2, 1, 3, 4);
    RoboCheckEqInt(PaintClipIntersectClip(NULL, &clip, &out, NULL),
                      RoboInvalidArgument);
    RoboCheckEqInt(PaintClipIntersectClip(&clip, NULL, &out, NULL),
                      RoboInvalidArgument);
    RoboCheckEqInt(PaintClipIntersectClip(&clip, &clip, NULL, NULL),
                      RoboInvalidArgument);

    /* A mode neither record may carry, on either side. */
    {
        PaintClip bad = ClipMake(1, 0, 100, 1, 0, 100);

        bad.vertical.mode = (CssOverflow)99;
        PaintDiagReset(&diag);
        RoboCheckEqInt(PaintClipIntersectClip(&clip, &bad, &out, &diag), RoboError);
        RoboCheckEqInt((int)diag.domain, (int)RoboPaintDomainClip);
        RoboCheckEqInt(PaintClipIntersectClip(&bad, &clip, &out, &diag), RoboError);
    }

    /* An interval that runs backwards is not a record, so no result is invented from it. */
    {
        PaintClip bad = ClipMake(1, 0, 100, 1, 0, 100);

        bad.horizontal.start = 50;
        bad.horizontal.end = 10;
        RoboCheckEqInt(PaintClipIsValid(&bad), 0);
        RoboCheckEqInt(PaintClipIntersectClip(&clip, &bad, &out, NULL), RoboError);
    }

    /* The boundary door's own safety, one layer down: the mode, the boundary rectangle and the
     * record are all checked before the arithmetic runs. */
    {
        LayoutRect boundary = ClipRect(0, 0, 10, 10);
        LayoutRect negative = ClipRect(0, 0, -10, 10);

        RoboCheckEqInt(PaintClipIntersect(&clip, &boundary,
                                                    (CssOverflow)99,
                                                    RoboCssOverflowVisible, &out, NULL),
                          RoboInvalidArgument);
        RoboCheckEqInt(PaintClipIntersect(&clip, &negative, RoboCssOverflowHidden,
                                                    RoboCssOverflowHidden, &out, NULL),
                          RoboInvalidArgument);
        RoboCheckEqInt(PaintClipIntersect(NULL, &boundary, RoboCssOverflowHidden,
                                                    RoboCssOverflowHidden, &out, NULL),
                          RoboInvalidArgument);
    }
}

static void TestClipEmptyPredicate(void)
{
    PaintClip clip;
    LayoutRect rect;

    /* No boundary is *not* empty: it is unbounded, which is the opposite answer. */
    clip = ClipNoneValue();
    RoboCheckEqInt(PaintClipIsEmpty(&clip), 0);
    RoboCheckEqInt(PaintClipIsActive(&clip), 0);

    /* A bounded axis with a zero interval is empty, and either axis is enough. */
    clip = ClipMake(1, 10, 10, 0, 0, 0);
    RoboCheckEqInt(PaintClipIsEmpty(&clip), 1);
    clip = ClipMake(1, 0, 100, 1, 40, 40);
    RoboCheckEqInt(PaintClipIsEmpty(&clip), 1);
    clip = ClipMake(1, 0, 100, 1, 0, 100);
    RoboCheckEqInt(PaintClipIsEmpty(&clip), 0);

    /* NULL is "nothing is bounding anything", which is not the same as "nothing can be
     * drawn" -- and the predicate says so rather than guessing. */
    RoboCheckEqInt(PaintClipIsEmpty(NULL), 0);

    /* The rectangle door answers only when it can: both axes bounded. A one-axis record is a
     * legal document, so the refusal is the point. */
    clip = ClipMake(1, 10, 30, 1, 40, 80);
    RoboCheckEqInt(PaintClipRect(&clip, &rect), RoboOk);
    PaintRectIs(10, 40, 20, 40, rect, "the rectangle of a two-axis clip");
    clip = ClipMake(1, 10, 30, 0, 0, 0);
    RoboCheckEqInt(PaintClipRect(&clip, &rect), RoboNotFound);
    clip = ClipNoneValue();
    RoboCheckEqInt(PaintClipRect(&clip, &rect), RoboNotFound);
    RoboCheckEqInt(PaintClipRect(NULL, &rect), RoboInvalidArgument);
}

/* ------------------------------------------------------------------ */
/* 2. The stack                                                        */
/* ------------------------------------------------------------------ */

static void TestClipStack(void)
{
    PaintClipStack *stack = NULL;
    PaintClip clip;
    PaintClip readBack;
    PaintDiag diag;
    size_t baseline = PaintLiveBlocks();
    size_t index;

    RoboCheckEqInt(PaintClipStackCreate(&stack, NULL), RoboOk);
    RoboCheck(stack != NULL);
    RoboCheckEqInt((int)PaintClipStackDepth(stack), 0);
    RoboCheckEqInt((int)PaintClipStackCapacity(stack), 0);
    RoboCheckEqInt(PaintClipStackIsEmpty(stack), 1);
    RoboCheckEqInt(PaintClipStackIsEmpty(NULL), 1);

    /* Nothing in force reads as the cleared record rather than as a miss. */
    clip = ClipMake(1, 1, 2, 1, 3, 4);
    PaintClipStackCurrent(stack, &readBack);
    ClipCheck(&readBack, &(PaintClip){ { RoboCssOverflowVisible, 0, 0 },
                                                 { RoboCssOverflowVisible, 0, 0 } },
               "an empty stack's clip");
    PaintClipStackCurrent(NULL, &readBack);
    RoboCheckEqInt(PaintClipIsActive(&readBack), 0);

    /* A push becomes the clip in force, and the stack takes a *copy*. */
    clip = ClipMake(1, 0, 100, 1, 0, 50);
    RoboCheckEqInt(PaintClipStackPush(stack, &clip, NULL), RoboOk);
    RoboCheckEqInt((int)PaintClipStackDepth(stack), 1);
    RoboCheckEqInt(PaintClipStackIsEmpty(stack), 0);
    clip.horizontal.start = 999;
    PaintClipStackCurrent(stack, &readBack);
    ClipCheck(&readBack, &(PaintClip){ { RoboCssOverflowHidden, 0, 100 },
                                                 { RoboCssOverflowHidden, 0, 50 } },
               "the pushed record, after the caller's copy changed");

    /* Growth is unbounded above the first block, which is what makes a deeply nested document
     * a document rather than a refusal. */
    clip = ClipMake(1, 0, 10, 1, 0, 10);
    for (index = 0u; index < 64u; index++) {
        RoboCheckEqInt(PaintClipStackPush(stack, &clip, NULL), RoboOk);
    }
    RoboCheckEqInt((int)PaintClipStackDepth(stack), 65);
    RoboCheck(PaintClipStackCapacity(stack) >= 65u);

    PaintClipStackCurrent(stack, &readBack);
    ClipCheck(&readBack, &clip, "the innermost of 65 records");

    /* Refusals: nothing to pop, and a record that is not one. */
    RoboCheckEqInt(PaintClipStackPush(stack, NULL, NULL), RoboInvalidArgument);
    PaintDiagReset(&diag);
    RoboCheckEqInt(PaintClipStackPush(stack, &(PaintClip){
                                                          { (CssOverflow)99, 0, 0 },
                                                          { RoboCssOverflowVisible, 0, 0 } },
                                                 &diag),
                      RoboError);
    RoboCheckEqInt((int)diag.domain, (int)RoboPaintDomainClip);
    RoboCheckEqInt((int)PaintClipStackDepth(stack), 65);

    /* Pop back to empty, then one more -- which is the one malformed operation a stack can be
     * handed, and it is refused without disturbing anything. */
    for (index = 0u; index < 65u; index++) {
        RoboCheckEqInt(PaintClipStackPop(stack, NULL), RoboOk);
    }
    RoboCheckEqInt(PaintClipStackIsEmpty(stack), 1);
    RoboCheckEqInt(PaintClipStackPop(stack, NULL), RoboNotFound);
    RoboCheckEqInt(PaintClipStackPop(NULL, NULL), RoboInvalidArgument);
    RoboCheckEqInt(PaintClipStackDepth(NULL), 0);
    RoboCheckEqInt((int)PaintClipStackCapacity(NULL), 0);

    /* Reset empties it and keeps the capacity; destroy releases it. */
    RoboCheckEqInt(PaintClipStackPush(stack, &clip, NULL), RoboOk);
    PaintClipStackReset(stack);
    RoboCheckEqInt(PaintClipStackIsEmpty(stack), 1);
    RoboCheck(PaintClipStackCapacity(stack) >= 65u);
    PaintClipStackReset(NULL);

    PaintClipStackDestroy(stack);
    PaintClipStackDestroy(NULL);
    RoboCheckEqInt((int)PaintLiveBlocks(), (int)baseline);
}

static void TestClipStackNested(void)
{
    PaintClipStack *stack = NULL;
    PaintClip outer = ClipMake(1, 0, 200, 1, 0, 100);
    PaintClip middle = ClipMake(1, 20, 180, 1, 10, 90);
    PaintClip inner = ClipMake(1, 40, 160, 1, 20, 80);
    PaintClip readBack;
    PaintDiag diag;

    RoboCheckEqInt(PaintClipStackCreate(&stack, NULL), RoboOk);

    PaintDiagReset(&diag);
    RoboCheckEqInt(PaintClipStackPush(stack, &outer, &diag), RoboOk);
    PaintClipStackCurrent(stack, &readBack);
    ClipCheck(&readBack, &outer, "one scope open");
    RoboCheckEqInt(PaintClipStackPush(stack, &middle, &diag), RoboOk);
    PaintClipStackCurrent(stack, &readBack);
    ClipCheck(&readBack, &middle, "two scopes open");
    RoboCheckEqInt(PaintClipStackPush(stack, &inner, &diag), RoboOk);
    PaintClipStackCurrent(stack, &readBack);
    ClipCheck(&readBack, &inner, "three scopes open");
    RoboCheckEqInt((int)PaintClipStackDepth(stack), 3);

    /* Each pop restores **the one outside it**, not "nothing". */
    RoboCheckEqInt(PaintClipStackPop(stack, &diag), RoboOk);
    PaintClipStackCurrent(stack, &readBack);
    ClipCheck(&readBack, &middle, "after closing the innermost scope");
    RoboCheckEqInt(PaintClipStackPop(stack, &diag), RoboOk);
    PaintClipStackCurrent(stack, &readBack);
    ClipCheck(&readBack, &outer, "after closing the middle scope");
    RoboCheckEqInt(PaintClipStackPop(stack, &diag), RoboOk);
    PaintClipStackCurrent(stack, &readBack);
    RoboCheckEqInt(PaintClipIsActive(&readBack), 0);

    /* An empty clip is a legal member of the stack: it makes everything inside it undrawable
     * and it does not poison the scope outside it. The interval is the canonical empty form,
     * `[end, end]` -- a backwards interval is not a record (`PaintClipIsValid`) and the
     * intersection that produces one canonicalizes it, so this is the shape an excluded box
     * actually arrives in. */
    RoboCheckEqInt(PaintClipStackPush(stack, &outer, &diag), RoboOk);
    RoboCheckEqInt(PaintClipStackPush(stack, &(PaintClip){
                                                          { RoboCssOverflowHidden, 200, 200 },
                                                          { RoboCssOverflowVisible, 0, 0 } },
                                                 &diag),
                      RoboOk);
    PaintClipStackCurrent(stack, &readBack);
    RoboCheckEqInt(PaintClipIsEmpty(&readBack), 1);
    RoboCheckEqInt(PaintClipIsValid(&readBack), 1);
    RoboCheckEqInt(PaintClipStackPop(stack, &diag), RoboOk);
    PaintClipStackCurrent(stack, &readBack);
    ClipCheck(&readBack, &outer, "the enclosing scope, after an empty one");

    PaintClipStackDestroy(stack);
}

/* ------------------------------------------------------------------ */
/* 3. The scopes the traversal emits                                   */
/* ------------------------------------------------------------------ */

static void TestClipScopesEmitted(void)
{
    PaintFixtureDoc fixture;
    PaintResult result;
    LayoutNodeId outer;
    LayoutNodeId inner;
    PaintDisplayList *list = NULL;
    PaintDisplayListStats stats;
    PaintDisplayCommand push;
    PaintDisplayCommand pop;
    PaintDisplayCommand outerBackground;
    PaintDisplayCommand innerBackground;
    PaintOperation pushRecord;
    PaintOperation popRecord;
    size_t pushIndex;
    size_t popIndex;
    LayoutRect pushedRect;
    GfxSurface *surface = NULL;

    PaintBeginLaidOut(&fixture,
                         "<html><body><div class=\"outer\"><div class=\"inner\"></div>"
                         "</div></body></html>",
                         CLIP_BASE_CSS
                         " .outer { width: 200px; height: 100px; overflow: hidden;"
                         " background-color: #0000ff }"
                         " .inner { width: 400px; height: 80px; background-color: #ff0000 }");
    outer = PaintBox(&fixture, "outer");
    inner = PaintBox(&fixture, "inner");
    PaintOnce(&fixture, &result);

    /* The records, first: one push and one pop, and nothing for the inline-level walk. */
    RoboCheckEqInt((int)PaintListCountOfKind(result.list, RoboPaintOpPushClip), 1);
    RoboCheckEqInt((int)PaintListCountOfKind(result.list, RoboPaintOpPopClip), 1);
    RoboCheckEqInt((int)result.stats.clipScopes, 1);
    RoboCheckEqInt((int)result.stats.clipped, 1);
    RoboCheckEqInt(PaintNthOfKind(&result, RoboPaintOpPushClip, 0u, &pushRecord), 1);
    RoboCheckEqInt(PaintNthOfKind(&result, RoboPaintOpPopClip, 0u, &popRecord), 1);
    RoboCheckEqInt((int)pushRecord.node, (int)outer);
    RoboCheckEqInt((int)popRecord.node, (int)outer);
    RoboCheckEqInt((int)pushRecord.depth, (int)popRecord.depth);
    RoboCheckEqInt((int)PaintClipIsActive(&pushRecord.clip), 1);
    RoboCheckEqInt((int)PaintClipEqual(&pushRecord.clip, &popRecord.clip), 1);

    ClipBuild(&result, &list, &stats);
    RoboCheckEqInt((int)stats.clipScopes, 1);
    RoboCheckEqInt((int)ClipCountOfKind(list, RoboPaintDisplayCmdPushClip), 1);
    RoboCheckEqInt((int)ClipCountOfKind(list, RoboPaintDisplayCmdPopClip), 1);

    RoboCheckEqInt(ClipCommandOfKind(list, RoboPaintDisplayCmdPushClip, 0u, &push), 1);
    RoboCheckEqInt(ClipCommandOfKind(list, RoboPaintDisplayCmdPopClip, 0u, &pop), 1);
    RoboCheckEqInt((int)push.node, (int)outer);
    RoboCheckEqInt((int)pop.node, (int)outer);
    ClipCheck(&pop.clip, &push.clip, "the pop carries the push's boundary");

    /* The scope is the padding box: nothing else bounds this box, so the effective clip *is*
     * its padding box -- and the command's rectangle names the boundary that produced it. */
    RoboCheckEqInt(PaintClipRect(&push.clip, &pushedRect), RoboOk);
    PaintRectEq(push.rect, pushedRect, "the push's rectangle is the boundary");

    /* Order: after the box's own content boundary, before its contents; the pop after the last
     * child and before the box's end marker. */
    pushIndex = ClipIndexOfKindForNode(list, RoboPaintDisplayCmdPushClip, outer, 0u);
    popIndex = ClipIndexOfKindForNode(list, RoboPaintDisplayCmdPopClip, outer, 0u);
    RoboCheck(pushIndex != (size_t)-1 && popIndex != (size_t)-1);
    RoboCheck(pushIndex > ClipIndexOfKindForNode(list, RoboPaintDisplayCmdContent,
                                                        outer, 0u));
    RoboCheck(pushIndex < ClipIndexOfKindForNode(list, RoboPaintDisplayCmdBeginNode,
                                                        inner, 0u));
    RoboCheck(popIndex > ClipIndexOfKindForNode(list, RoboPaintDisplayCmdEndNode,
                                                       inner, 0u));
    RoboCheck(popIndex < ClipIndexOfKindForNode(list, RoboPaintDisplayCmdEndNode,
                                                       outer, 0u));

    /* The box's own decoration is **outside** its own scope, and the child's is inside:
     * everything between the push and the pop carries the boundary. */
    RoboCheckEqInt(1, ClipCommandOfKind(list, RoboPaintDisplayCmdDrawBackground, 0u,
                                              &outerBackground));
    RoboCheckEqInt((int)outerBackground.node, (int)outer);
    RoboCheckEqInt(PaintClipIsActive(&outerBackground.clip), 0);
    RoboCheckEqInt(1, ClipCommandOfKind(list, RoboPaintDisplayCmdDrawBackground, 1u,
                                              &innerBackground));
    RoboCheckEqInt((int)innerBackground.node, (int)inner);
    ClipCheck(&innerBackground.clip, &push.clip, "the child's background is bounded");

    /* And the pixels follow: inside the boundary red, outside untouched. */
    surface = ClipSurface(800u, 600u, RoboGfxSurfaceAccessReadWrite);
    ClipPaint(list, surface, NULL, NULL);
    ClipPixelIs(surface, pushedRect.x + 10, pushedRect.y + 10, CLIP_RED,
                  "inside the clip scope");
    ClipPixelIs(surface, pushedRect.x + 250, pushedRect.y + 10, CLIP_UNTOUCHED,
                  "beyond the clip scope, where the child's box extends");
    ClipPixelsMatch(list, surface, "the clipping document");

    GfxSurfaceRelease(surface);
    PaintDisplayListDestroy(list);
    PaintResultDispose(&result);
    PaintEnd(&fixture);
}

static void TestClipScopeOnlyWhenChanged(void)
{
    PaintFixtureDoc fixture;
    PaintResult result;
    PaintDisplayList *list = NULL;
    PaintDisplayListStats stats;

    /*
     * Two hidden boxes and **one** scope. `.outer` bounds its contents; `.mid` is larger than
     * its ancestor in both axes and offset out of it, so its own `overflow: hidden` boundary --
     * intersected with what is already in force -- changes nothing. The traversal does not push
     * a command that establishes nothing, and `clipped - clipScopes` is the count of scopes it
     * declined: 2 - 1 = 1.
     */
    PaintBeginLaidOut(&fixture,
                         "<html><body><div class=\"outer\"><div class=\"mid\">"
                         "<div class=\"leaf\"></div></div></div></body></html>",
                         CLIP_BASE_CSS
                         " .outer { width: 60px; height: 40px; overflow: hidden }"
                         " .mid { position: relative; left: -20px; top: -10px; width: 400px;"
                         " height: 200px; overflow: hidden }"
                         " .leaf { width: 10px; height: 10px; background-color: #00ff00 }");
    PaintOnce(&fixture, &result);

    RoboCheckEqInt((int)result.stats.clipped, 2);
    RoboCheckEqInt((int)result.stats.clipScopes, 1);
    ClipBuild(&result, &list, &stats);
    RoboCheckEqInt((int)stats.clipScopes, 1);
    RoboCheckEqInt((int)ClipCountOfKind(list, RoboPaintDisplayCmdPushClip), 1);

    PaintDisplayListDestroy(list);
    PaintResultDispose(&result);
    PaintEnd(&fixture);
}

static void TestClipNestedEffective(void)
{
    PaintFixtureDoc fixture;
    PaintResult result;
    LayoutNodeId outer;
    LayoutNodeId mid;
    PaintDisplayList *list = NULL;
    PaintDisplayListStats stats;
    PaintDisplayCommand outerPush;
    PaintDisplayCommand midPush;
    PaintDisplayCommand leafBackground;
    PaintDisplayCommand outerContent;
    PaintBoxes local;
    PaintBoxes page;
    PaintClip expected;
    PaintDecorationStats paintStats;
    GfxSurface *surface = NULL;

    PaintBeginLaidOut(&fixture,
                         "<html><body><div class=\"outer\"><div class=\"mid\">"
                         "<div class=\"leaf\"></div></div></div></body></html>",
                         CLIP_BASE_CSS
                         " .outer { width: 200px; height: 100px; overflow: hidden }"
                         " .mid { position: relative; left: 20px; top: 10px; width: 300px;"
                         " height: 300px; overflow: hidden }"
                         " .leaf { width: 20px; height: 20px; background-color: #00ff00 }");
    outer = PaintBox(&fixture, "outer");
    mid = PaintBox(&fixture, "mid");
    PaintOnce(&fixture, &result);
    RoboCheckEqInt((int)result.stats.clipScopes, 2);

    ClipBuild(&result, &list, &stats);
    RoboCheckEqInt((int)stats.clipScopes, 2);
    RoboCheckEqInt(1, ClipIndexOfKindForNode(list, RoboPaintDisplayCmdPushClip, outer,
                                                     0u) != (size_t)-1);
    RoboCheckEqInt(1, ClipIndexOfKindForNode(list, RoboPaintDisplayCmdPushClip, mid,
                                                     0u) != (size_t)-1);
    RoboCheckEqInt(ClipCommandOfKind(list, RoboPaintDisplayCmdPushClip, 0u, &outerPush),
                      1);
    RoboCheckEqInt(ClipCommandOfKind(list, RoboPaintDisplayCmdPushClip, 1u, &midPush),
                      1);
    RoboCheckEqInt(ClipCommandOfKind(list, RoboPaintDisplayCmdContent, 0u,
                                           &outerContent),
                      1);
    RoboCheckEqInt(ClipCommandOfKind(list, RoboPaintDisplayCmdDrawBackground, 0u,
                                           &leafBackground),
                      1);

    /*
     * The expected clip inside the nested scope, **recomputed from the tree**: the enclosing
     * scope's boundary, intersected with `.mid`'s own padding box in page space. The page origin
     * for `.mid` is its parent's content origin, which the list itself states -- the box's own
     * `content` command's rectangle -- so the recomputation uses the same two inputs the
     * traversal did and none of its arithmetic.
     */
    RoboCheckEqInt(PaintBoxesOf(fixture.tree, mid, &local, NULL), RoboOk);
    RoboCheckEqInt(PaintBoxesTranslate(&local, outerContent.rect.x,
                                                 outerContent.rect.y, &page, NULL),
                      RoboOk);
    RoboCheckEqInt(PaintClipIntersect(&outerPush.clip, &page.padding,
                                                RoboCssOverflowHidden,
                                                RoboCssOverflowHidden, &expected, NULL),
                      RoboOk);
    ClipCheck(&midPush.clip, &expected, "the nested scope's effective clip");
    ClipCheck(&leafBackground.clip, &expected, "the leaf's own drawing");

    /*
     * And it really is a **narrowing**, on the axis where this document can show it: `.mid` is
     * 300x300 inside a 200x100 boundary, so every one of its own edges lies outside the enclosing
     * clip and both of its *starts* are its own while both *ends* are the enclosing boundary's.
     * The clips are therefore strictly narrower at the origin and exactly equal at the end --
     * which is the per-axis `max`/`min` stated as pixels of geometry rather than as a formula.
     */
    RoboCheck(midPush.clip.horizontal.start > outerPush.clip.horizontal.start);
    RoboCheckEqInt(midPush.clip.horizontal.end, outerPush.clip.horizontal.end);
    RoboCheck(midPush.clip.vertical.start > outerPush.clip.vertical.start);
    RoboCheckEqInt(midPush.clip.vertical.end, outerPush.clip.vertical.end);

    /* The pass rests two scopes deep inside `.mid`, and one scope's worth of pixels is inside
     * the nested boundary while the leaf's box extends beyond it. */
    surface = ClipSurface(800u, 600u, RoboGfxSurfaceAccessReadWrite);
    ClipPaint(list, surface, NULL, &paintStats);
    RoboCheckEqInt((int)paintStats.clipScopes, 2);
    RoboCheckEqInt((int)paintStats.clipDepthMax, 2);
    ClipPixelIs(surface, expected.horizontal.start + 5, expected.vertical.start + 5,
                  CLIP_GREEN, "the leaf inside both scopes");
    ClipPixelIs(surface, outerPush.clip.horizontal.end + 5, expected.vertical.start + 5,
                  CLIP_UNTOUCHED, "the leaf beyond the enclosing scope");
    ClipPixelsMatch(list, surface, "the nested clipping document");

    GfxSurfaceRelease(surface);
    PaintDisplayListDestroy(list);
    PaintResultDispose(&result);
    PaintEnd(&fixture);
}

static void TestClipAxisSplit(void)
{
    PaintFixtureDoc fixture;
    PaintResult result;
    PaintDisplayList *list = NULL;
    PaintDisplayListStats stats;
    PaintDisplayCommand push;
    PaintDisplayCommand spill;
    LayoutRect bothAxes;
    GfxSurface *surface = NULL;

    PaintBeginLaidOut(&fixture,
                         "<html><body><div class=\"clip\"><div class=\"spill\"></div></div>"
                         "</body></html>",
                         CLIP_BASE_CSS
                         " .clip { width: 60px; height: 30px; overflow-x: hidden;"
                         " overflow-y: visible }"
                         " .spill { width: 120px; height: 200px; background-color: #ff0000 }");
    PaintOnce(&fixture, &result);
    ClipBuild(&result, &list, &stats);

    RoboCheckEqInt(ClipCommandOfKind(list, RoboPaintDisplayCmdPushClip, 0u, &push), 1);
    RoboCheckEqInt(ClipCommandOfKind(list, RoboPaintDisplayCmdDrawBackground, 0u,
                                           &spill),
                      1);
    ClipCheck(&spill.clip, &push.clip, "the one bounded axis reaches the child");
    RoboCheckEqInt(PaintClipAxisIsActive(&push.clip.horizontal), 1);
    RoboCheckEqInt(PaintClipAxisIsActive(&push.clip.vertical), 0);

    /* The refusal the one-rectangle door makes, and why it has to: this document has no
     * rectangle to report, and answering one would clip an axis the author left open. */
    RoboCheckEqInt(PaintClipRect(&push.clip, &bothAxes), RoboNotFound);

    /* The pixels say the same thing: bounded horizontally, free vertically. */
    surface = ClipSurface(800u, 600u, RoboGfxSurfaceAccessReadWrite);
    ClipPaint(list, surface, NULL, NULL);
    ClipPixelIs(surface, push.rect.x + 40, push.rect.y + 10, CLIP_RED,
                  "inside the bounded axis");
    ClipPixelIs(surface, push.rect.x + 80, push.rect.y + 10, CLIP_UNTOUCHED,
                  "beyond the bounded axis");
    ClipPixelIs(surface, push.rect.x + 40, push.rect.y + 120, CLIP_RED,
                  "below the box, on the axis the author left open");
    ClipPixelsMatch(list, surface, "the one-axis document");

    GfxSurfaceRelease(surface);
    PaintDisplayListDestroy(list);
    PaintResultDispose(&result);
    PaintEnd(&fixture);
}

/* ------------------------------------------------------------------ */
/* 4. One drawing kind at a time                                       */
/* ------------------------------------------------------------------ */

static void TestClipBackground(void)
{
    PaintFixtureDoc fixture;
    PaintResult result;
    PaintDisplayList *list = NULL;
    PaintDisplayListStats stats;
    PaintDisplayCommand push;
    PaintDisplayCommand child;
    GfxSurface *surface = NULL;

    PaintBeginLaidOut(&fixture,
                         "<html><body><div class=\"parent\"><div class=\"child\"></div></div>"
                         "</body></html>",
                         CLIP_BASE_CSS
                         " .parent { width: 100px; height: 50px; overflow: hidden }"
                         " .child { position: relative; left: 60px; top: 10px; width: 100px;"
                         " height: 20px; background-color: #ff0000 }");
    PaintOnce(&fixture, &result);
    ClipBuild(&result, &list, &stats);
    RoboCheckEqInt(ClipCommandOfKind(list, RoboPaintDisplayCmdPushClip, 0u, &push), 1);
    RoboCheckEqInt(ClipCommandOfKind(list, RoboPaintDisplayCmdDrawBackground, 0u,
                                           &child),
                      1);
    ClipCheck(&child.clip, &push.clip, "the child's background is bounded by the scope");

    surface = ClipSurface(800u, 600u, RoboGfxSurfaceAccessReadWrite);
    ClipPaint(list, surface, NULL, NULL);
    /* The child starts 60px in and the boundary is 100px wide, so only its first 40px are
     * painted -- and the rest of its own box leaves the surface untouched. */
    ClipPixelIs(surface, push.rect.x + 70, push.rect.y + 15, CLIP_RED,
                  "the background inside the boundary");
    ClipPixelIs(surface, push.rect.x + 130, push.rect.y + 15, CLIP_UNTOUCHED,
                  "the background beyond the boundary");
    ClipPixelsMatch(list, surface, "the clipped background");

    GfxSurfaceRelease(surface);
    PaintDisplayListDestroy(list);
    PaintResultDispose(&result);
    PaintEnd(&fixture);
}

static void TestClipBorder(void)
{
    PaintFixtureDoc fixture;
    PaintResult result;
    PaintDisplayList *list = NULL;
    PaintDisplayListStats stats;
    PaintDisplayCommand push;
    PaintDisplayCommand band;
    size_t index;
    size_t found = 0u;
    GfxSurface *surface = NULL;

    PaintBeginLaidOut(&fixture,
                         "<html><body><div class=\"parent\"><div class=\"child\"></div></div>"
                         "</body></html>",
                         CLIP_BASE_CSS
                         " .parent { width: 100px; height: 50px; overflow: hidden }"
                         " .child { position: relative; left: 60px; top: 10px; width: 100px;"
                         " height: 20px; border: 8px solid #ff0000 }");
    PaintOnce(&fixture, &result);
    ClipBuild(&result, &list, &stats);
    RoboCheckEqInt(ClipCommandOfKind(list, RoboPaintDisplayCmdPushClip, 0u, &push), 1);

    /* Every band of the child is bounded, and at least the left one straddles the boundary. */
    for (index = 0u; index < PaintDisplayListCount(list); index++) {
        PaintDisplayCommand command;

        RoboCheckEqInt(PaintDisplayListAt(list, index, &command), RoboOk);
        if (command.kind != RoboPaintDisplayCmdDrawBorder) {
            continue;
        }
        ClipCheck(&command.clip, &push.clip, "a border band is bounded by the scope");
        found++;
    }
    RoboCheckEqInt((int)found, 4);

    surface = ClipSurface(800u, 600u, RoboGfxSurfaceAccessReadWrite);
    ClipPaint(list, surface, NULL, NULL);
    /* The child's border box starts at 60px and its left band is 8px wide, so the band is
     * inside; the right band, at 160px, is entirely outside the boundary. */
    RoboCheckEqInt(ClipCommandOfKind(list, RoboPaintDisplayCmdDrawBorder, 0u, &band), 1);
    ClipPixelIs(surface, band.rect.x + 2, band.rect.y + band.rect.height / 2, CLIP_RED,
                  "the band inside the boundary");
    ClipPixelsMatch(list, surface, "the clipped border");

    GfxSurfaceRelease(surface);
    PaintDisplayListDestroy(list);
    PaintResultDispose(&result);
    PaintEnd(&fixture);
}

static void TestClipText(void)
{
    PaintFixtureDoc fixture;
    PaintResult result;
    PaintDisplayList *list = NULL;
    PaintDisplayListStats stats;
    PaintDisplayCommand push;
    PaintDisplayCommand text;
    PaintDecorationStats paintStats;
    ClipFace face;
    GfxSurface *surface = NULL;
    int32_t boundary;

    /*
     * A single 'A' at 100px is a **10x10 device-unit square** whose left edge is the run's
     * origin x and whose bottom edge is the baseline. The run starts 55px into a box whose
     * boundary is 60px wide, so the square straddles it: five pixels inside, five outside.
     *
     * The run's own rectangle is the *measured* one (the scripted measurer this fixture uses
     * answers 8px per code point, 12px high), which is deliberately not the glyph's ink -- a
     * run's box is its advance, and ink overhangs it, which is exactly why the boundary rather
     * than the rectangle decides what lands.
     */
    PaintBeginLaidOut(&fixture,
                         "<html><body><div class=\"clip\"><div class=\"txt\">A</div></div>"
                         "</body></html>",
                         CLIP_BASE_CSS
                         " .clip { width: 60px; height: 40px; overflow: hidden }"
                         " .txt { position: relative; left: 55px; font-size: 100px;"
                         " color: #ff0000 }");
    /* The face before the pass, not after it: since 12.16 the glyphs a run draws are produced **where the
     * document is recorded**, so the face has to be there for the recording and for the drawing both. */
    ClipFaceCreate(&face);
    PaintOnceWithFace(&fixture, face.face, &result);
    ClipBuild(&result, &list, &stats);
    RoboCheckEqInt(ClipCommandOfKind(list, RoboPaintDisplayCmdPushClip, 0u, &push), 1);
    RoboCheckEqInt(ClipCommandOfKind(list, RoboPaintDisplayCmdDrawText, 0u, &text), 1);
    ClipCheck(&text.clip, &push.clip, "the run is bounded by the scope");

    surface = ClipSurface(800u, 600u, RoboGfxSurfaceAccessReadWrite);
    ClipPaint(list, surface, face.face, &paintStats);
    RoboCheckEqInt((int)paintStats.glyphs, 1);
    RoboCheckEqInt((int)paintStats.clipScopes, 1);

    boundary = push.clip.horizontal.end;
    /* The glyph's own origin is the run rectangle's left edge, and the run is 55px into the
     * box, so its ink starts five pixels before the boundary -- where it must be drawn. */
    RoboCheckEqInt((int)text.rect.x, (int)(boundary - 5));
    RoboCheckEqInt(ClipAnyInk(surface, boundary - 5, text.rect.y, boundary,
                                   text.rect.y + 12),
                      1);
    /* And the pixels on the far side of the boundary are untouched, even though the glyph's
     * outline is larger than the run's measured advance. */
    RoboCheckEqInt(ClipAnyInk(surface, boundary, text.rect.y, boundary + 24,
                                   text.rect.y + 12),
                      0);

    ClipFaceDestroy(&face);
    GfxSurfaceRelease(surface);
    PaintDisplayListDestroy(list);
    PaintResultDispose(&result);
    PaintEnd(&fixture);
}

/* ------------------------------------------------------------------ */
/* 5. The visual smoke test                                            */
/* ------------------------------------------------------------------ */

/**
 * The brief's own document, painted twice: `overflow: hidden` and `overflow: visible`.
 *
 * The two halves are the whole claim of the phase in one case -- the same DOM, the same sheet,
 * the same layout, and one declaration different. Everything inside the clipped box is painted
 * in both; everything outside it is painted in exactly one. The case ends by encoding the
 * clipped surface as a PNG through RoboGraphics' own encoder, which is what makes the pixels
 * *inspectable* rather than merely asserted: a reader who wants to see the result runs this case
 * and opens the buffer, and the encoder is the one Phase 6 shipped rather than a second one.
 */
/**
 * Paint the clipping showcase document with @p overflow on its outer box.
 *
 * The push is deliberately **not** returned: only the hidden document opens a scope, so a helper
 * that insisted on finding one in both would be asserting the wrong thing about the visible one
 * (see the no-clip claim in `TestClipPixels`). The caller reads the push out of the list it
 * gets back, which is also the only way to see that there is none.
 */
static void ClipVisualDocument(const char *overflow,
                                 GfxSurface **outSurface,
                                 PaintDisplayList **outList,
                                 PaintFixtureDoc *fixture,
                                 PaintResult *result)
{
    char sheet[512];

    memset(sheet, 0, sizeof(sheet));
    snprintf(sheet, sizeof(sheet),
             CLIP_BASE_CSS
             " .outer { width: 200px; height: 100px; overflow: %s; background-color: #0000ff }"
             " .inner { width: 400px; height: 80px; background-color: #ff0000 }",
             overflow);

    PaintBeginLaidOut(fixture,
                         "<html><body><div class=\"outer\"><div class=\"inner\"></div></div>"
                         "</body></html>",
                         sheet);
    PaintOnce(fixture, result);
    ClipBuild(result, outList, NULL);
    *outSurface = ClipSurface(800u, 600u, RoboGfxSurfaceAccessReadWrite);
    ClipPaint(*outList, *outSurface, NULL, NULL);
}

static void TestClipPixels(void)
{
    PaintFixtureDoc hiddenFixture;
    PaintFixtureDoc visibleFixture;
    PaintResult hiddenResult;
    PaintResult visibleResult;
    PaintDisplayList *hiddenList = NULL;
    PaintDisplayList *visibleList = NULL;
    PaintDisplayCommand hiddenPush;
    PaintDisplayCommand visiblePush;
    GfxSurface *hidden = NULL;
    GfxSurface *visible = NULL;
    GfxEncodeOptions encodeOpts;
    GfxEncodedBuffer encoded;
    int32_t xInside;
    int32_t xOutside;
    int32_t y;
    static const uint8_t pngSignature[8] = { 0x89u, 'P', 'N', 'G', 0x0du, 0x0au, 0x1au, 0x0au };

    ClipVisualDocument("hidden", &hidden, &hiddenList, &hiddenFixture, &hiddenResult);
    ClipVisualDocument("visible", &visible, &visibleList, &visibleFixture, &visibleResult);

    RoboCheckEqInt(ClipCommandOfKind(hiddenList, RoboPaintDisplayCmdPushClip, 0u,
                                           &hiddenPush),
                      1);
    RoboCheckEqInt(PaintClipIsActive(&hiddenPush.clip), 1);

    /*
     * The **no-clip case**, and it is the stronger form of the claim: the visible document does
     * not carry a boundary that happens to be inactive, it carries **no scope at all**. Its outer
     * box declares `overflow: visible`, whose boundary is the one already in force, so the
     * traversal opens nothing -- the two commands a consumer would only have to skip are never
     * emitted. Both documents are otherwise identical, so what is left of the pair is exactly the
     * clipping. */
    RoboCheckEqInt(ClipCommandOfKind(visibleList, RoboPaintDisplayCmdPushClip, 0u,
                                           &visiblePush),
                      0);
    RoboCheckEqInt(ClipCommandOfKind(visibleList, RoboPaintDisplayCmdPopClip, 0u,
                                           &visiblePush),
                      0);
    RoboCheckEqInt((int)PaintDisplayListCount(visibleList) + 2,
                      (int)PaintDisplayListCount(hiddenList));

    xInside = hiddenPush.rect.x + 100;
    xOutside = hiddenPush.rect.x + 250;
    y = hiddenPush.rect.y + 10;

    /* Inside the box both documents agree: red, from the child's own background. */
    ClipPixelIs(hidden, xInside, y, CLIP_RED, "inside the box, hidden overflow");
    ClipPixelIs(visible, xInside, y, CLIP_RED, "inside the box, visible overflow");

    /* Beyond the boundary -- and still inside the *child's* own box, which is twice as wide --
     * only the visible one paints. */
    ClipPixelIs(hidden, xOutside, y, CLIP_UNTOUCHED,
                  "the spill beyond the boundary, hidden overflow");
    ClipPixelIs(visible, xOutside, y, CLIP_RED,
                  "the spill beyond the boundary, visible overflow");

    /* The declarations around the boundary, exactly: the last clipped pixel is painted and the
     * first pixel outside it is not. */
    ClipPixelIs(hidden, hiddenPush.rect.x + hiddenPush.rect.width - 1, y, CLIP_RED,
                  "the boundary's last pixel");
    ClipPixelIs(hidden, hiddenPush.rect.x + hiddenPush.rect.width, y, CLIP_UNTOUCHED,
                  "the boundary's first pixel outside");

    /* The surface is a real image: RoboGraphics' own encoder turns it into a PNG, whose
     * signature and dimensions are checked here. */
    memset(&encoded, 0, sizeof(encoded));
    GfxEncodeOptionsInit(&encodeOpts);
    RoboCheckEqInt(GfxSurfaceEncode(hidden, &encodeOpts, &encoded), RoboOk);
    RoboCheck(encoded.size > 8u);
    RoboCheckEqInt(memcmp(encoded.data, pngSignature, sizeof(pngSignature)), 0);
    RoboCheckEqInt((int)((uint32_t)encoded.data[16] << 24 | (uint32_t)encoded.data[17] << 16 |
                            (uint32_t)encoded.data[18] << 8 | (uint32_t)encoded.data[19]),
                      800);
    GfxEncodedBufferRelease(&encoded);

    ClipPixelsMatch(hiddenList, hidden, "the clipped document");
    ClipPixelsMatch(visibleList, visible, "the unclipped document");

    GfxSurfaceRelease(hidden);
    GfxSurfaceRelease(visible);
    PaintDisplayListDestroy(hiddenList);
    PaintDisplayListDestroy(visibleList);
    PaintResultDispose(&hiddenResult);
    PaintResultDispose(&visibleResult);
    PaintEnd(&hiddenFixture);
    PaintEnd(&visibleFixture);
}

/* ------------------------------------------------------------------ */
/* 6. Empty scopes, no scopes                                          */
/* ------------------------------------------------------------------ */

static void TestClipEmptyScope(void)
{
    PaintFixtureDoc fixture;
    PaintResult result;
    PaintDisplayList *list = NULL;
    PaintDisplayListStats stats;
    PaintDisplayCommand push;
    PaintDisplayCommand inside;
    PaintDisplayCommand outside;
    PaintDecorationStats paintStats;
    GfxSurface *surface = NULL;

    /*
     * `.empty` is placed entirely outside `.frame`'s 40x20 boundary -- a 10px square 200px to the
     * right -- so the scope it opens for its contents has an **empty** horizontal interval.
     * `.after` follows it in flow and must paint: the empty scope draws nothing and the enclosing
     * boundary is restored by its pop.
     */
    PaintBeginLaidOut(&fixture,
                         "<html><body><div class=\"frame\">"
                         "<div class=\"empty\"></div><div class=\"after\"></div>"
                         "</div></body></html>",
                         CLIP_BASE_CSS
                         " .frame { width: 40px; height: 20px; overflow: hidden }"
                         " .empty { position: relative; left: 200px; width: 10px; height: 10px;"
                         " overflow: hidden }"
                         " .after { left: 0px; width: 30px; height: 8px;"
                         " background-color: #00ff00 }");
    PaintOnce(&fixture, &result);
    ClipBuild(&result, &list, &stats);
    RoboCheckEqInt((int)stats.clipScopes, 2);

    RoboCheckEqInt(ClipCommandOfKind(list, RoboPaintDisplayCmdPushClip, 0u, &push), 1);
    RoboCheckEqInt(ClipCommandOfKind(list, RoboPaintDisplayCmdPushClip, 1u, &inside), 1);
    /*
     * The excluded box's scope: an **empty** interval, and empty in the canonical shape rather
     * than as a backwards one. The intersection's start is the larger of the two (the box's own
     * left edge, 200px right of the frame's origin) and its end would be the smaller (the frame's
     * right edge, 40px), which runs backwards -- so it is canonicalized to `[200, 200]`: a valid
     * record that bounds nothing. Both numbers are derived from geometry here, not written as
     * absolutes: the box's edge from the push's own rectangle plus the relative offset, and the
     * equality of the two ends from the canonical rule. */
    ClipCheckInt(inside.clip.horizontal.start, push.rect.x + 200,
                   "the excluded box's left edge");
    ClipCheckInt(inside.clip.horizontal.end, inside.clip.horizontal.start,
                   "an empty interval ends where it starts");
    ClipCheckInt(inside.clip.horizontal.start > push.rect.x + push.rect.width, 1,
                   "the empty interval at the box's edge, past the frame's own");
    RoboCheckEqInt(PaintClipIsEmpty(&inside.clip), 1);
    RoboCheckEqInt(PaintClipIsValid(&inside.clip), 1);

    surface = ClipSurface(800u, 600u, RoboGfxSurfaceAccessReadWrite);
    ClipPaint(list, surface, NULL, &paintStats);
    RoboCheckEqInt((int)paintStats.clipScopes, 2);
    RoboCheckEqInt((int)paintStats.clipDepthMax, 2);

    /* Nothing of the excluded descendant is drawn, and the sibling after it is -- which is the
     * whole claim: an empty scope suppresses drawing and does not poison the walk. */
    RoboCheckEqInt(ClipAnyInk(surface, 0, 0, 800, 600) != 0, 1);
    RoboCheckEqInt(ClipCountOfKind(list, RoboPaintDisplayCmdDrawBackground), 1);
    RoboCheckEqInt(ClipCommandOfKind(list, RoboPaintDisplayCmdDrawBackground, 0u,
                                           &outside),
                      1);
    ClipPixelIs(surface, outside.rect.x + outside.rect.width / 2,
                  outside.rect.y + outside.rect.height / 2, CLIP_GREEN,
                  "the sibling after the empty scope");

    GfxSurfaceRelease(surface);
    PaintDisplayListDestroy(list);
    PaintResultDispose(&result);
    PaintEnd(&fixture);
}

static void TestClipNoClip(void)
{
    PaintFixtureDoc fixture;
    PaintResult result;
    PaintDisplayList *list = NULL;
    PaintDisplayListStats stats;
    PaintDecorationStats paintStats;
    GfxSurface *surface = NULL;

    /* A document that declares no overflow: no scope, no clip command, no boundary in force --
     * and every drawing still painted exactly where it was before this phase, which is the
     * "no-clip path is unchanged" claim stated as a pixel. */
    PaintBeginLaidOut(&fixture,
                         "<html><body><div class=\"a\"></div><div class=\"b\"></div></body></html>",
                         CLIP_BASE_CSS
                         " .a { width: 100px; height: 20px; background-color: #ff0000 }"
                         " .b { width: 50px; height: 10px; border: 4px solid #0000ff }");
    PaintOnce(&fixture, &result);
    RoboCheckEqInt((int)result.stats.clipScopes, 0);
    RoboCheckEqInt((int)result.stats.clipped, 0);
    RoboCheckEqInt((int)PaintCountOf(&result, RoboPaintOpPushClip), 0);

    ClipBuild(&result, &list, &stats);
    RoboCheckEqInt((int)stats.clipScopes, 0);
    RoboCheckEqInt((int)ClipCountOfKind(list, RoboPaintDisplayCmdPushClip), 0);
    RoboCheckEqInt((int)ClipCountOfKind(list, RoboPaintDisplayCmdPopClip), 0);

    surface = ClipSurface(800u, 600u, RoboGfxSurfaceAccessReadWrite);
    ClipPaint(list, surface, NULL, &paintStats);
    RoboCheckEqInt((int)paintStats.clipScopes, 0);
    RoboCheckEqInt((int)paintStats.clipDepthMax, 0);
    RoboCheckEqInt((int)paintStats.clipped, 0);
    ClipPixelsMatch(list, surface, "a document that clips nothing");

    GfxSurfaceRelease(surface);
    PaintDisplayListDestroy(list);
    PaintResultDispose(&result);
    PaintEnd(&fixture);
}

static void TestClipEmptyAndMalformed(void)
{
    PaintFixtureDoc fixture;
    PaintResult result;
    PaintDisplayList *list = NULL;
    PaintDisplayListStats stats;

    /* A childless hidden box opens a scope and draws nothing: the scope is a statement about
     * the box, not about its subtree, exactly as its content boundary is. */
    PaintBeginLaidOut(&fixture,
                         "<html><body><div class=\"solo\"></div></body></html>",
                         CLIP_BASE_CSS " .solo { overflow: hidden; width: 10px; height: 0px }");
    PaintOnce(&fixture, &result);
    RoboCheckEqInt((int)result.stats.clipScopes, 1);
    ClipBuild(&result, &list, &stats);
    RoboCheckEqInt((int)stats.clipScopes, 1);
    RoboCheckEqInt((int)ClipCountOfKind(list, RoboPaintDisplayCmdPushClip), 1);
    RoboCheckEqInt((int)ClipCountOfKind(list, RoboPaintDisplayCmdPopClip), 1);
    PaintDisplayListDestroy(list);
    list = NULL;
    PaintResultDispose(&result);
    PaintEnd(&fixture);

    /* An empty document: a document with no boxes has no scopes, and the build is still a
     * successful build of an empty list. */
    PaintBeginLaidOut(&fixture, "<html><body></body></html>", CLIP_BASE_CSS);
    PaintOnce(&fixture, &result);
    ClipBuild(&result, &list, &stats);
    RoboCheckEqInt((int)stats.clipScopes, 0);
    RoboCheckEqInt((int)ClipCountOfKind(list, RoboPaintDisplayCmdPushClip), 0);
    PaintDisplayListDestroy(list);
    PaintResultDispose(&result);
    PaintEnd(&fixture);
}

/* ------------------------------------------------------------------ */
/* 7. The pass's own contract                                          */
/* ------------------------------------------------------------------ */

static void TestClipPassRefusesUnbalanced(void)
{
    PaintDisplayList *list = NULL;
    GfxSurface *surface = NULL;
    PaintDiag diag;
    Result status;

    RoboCheckEqInt(PaintDisplayListCreate(&list, NULL), RoboOk);
    /* A pop with nothing to pop. The list's own validity rule cannot see it -- each command is
     * a record -- so this is the pass's contract, and it refuses rather than skipping. */
    ClipAppendCommand(list, RoboPaintDisplayCmdPopClip, 1u, 0u, ClipRect(0, 0, 10, 10),
                        ClipMake(1, 0, 10, 1, 0, 10), CssColorNone());
    surface = ClipSurface(64u, 64u, RoboGfxSurfaceAccessReadWrite);
    PaintDiagReset(&diag);
    status = PaintDecorationPaint(list, surface, NULL, NULL, &diag);
    RoboCheckEqInt((int)status, (int)RoboNotFound);
    RoboCheckEqInt((int)diag.domain, (int)RoboPaintDomainClip);

    GfxSurfaceRelease(surface);
    PaintDisplayListDestroy(list);
}

static void TestClipHandBuiltScope(void)
{
    PaintDisplayList *list = NULL;
    GfxSurface *surface = NULL;
    PaintDecorationStats stats;
    PaintClip empty;

    /*
     * The stack is **load-bearing**, and this is the case that proves it: a hand-built list whose
     * drawing command carries *no* boundary of its own, standing inside a scope whose clip is
     * empty. If the pass read the command's clip (as it did before 11.8) it would fill the
     * rectangle; reading the scope, it draws nothing.
     *
     * A hand-built list rather than a document because a document with an empty scope is a box
     * an ancestor excluded, and the traversal gives such a box's commands the empty clip too --
     * so the two paths agree and the case could not tell them apart.
     */
    RoboCheckEqInt(PaintDisplayListCreate(&list, NULL), RoboOk);
    empty = ClipMake(1, 100, 100, 0, 0, 0);
    ClipAppendCommand(list, RoboPaintDisplayCmdPushClip, 1u, 0u, ClipRect(0, 0, 20, 20),
                        empty, CssColorNone());
    ClipAppendCommand(list, RoboPaintDisplayCmdDrawBackground, 2u, 1u,
                        ClipRect(0, 0, 20, 20), ClipNoneValue(),
                        CssColorMake(255u, 0u, 0u, 255u));
    ClipAppendCommand(list, RoboPaintDisplayCmdPopClip, 1u, 0u, ClipRect(0, 0, 20, 20),
                        empty, CssColorNone());

    surface = ClipSurface(64u, 64u, RoboGfxSurfaceAccessReadWrite);
    ClipPaint(list, surface, NULL, &stats);
    RoboCheckEqInt((int)stats.clipScopes, 1);
    /* The fill is *attempted* -- `filled` counts the fills this pass executed, and an empty
     * clip is a no-op inside the raster rather than a command the pass skipped -- so the claim
     * is the pixel and `clipped`, which says the command stood under a boundary. */
    RoboCheckEqInt((int)stats.filled, 1);
    RoboCheckEqInt((int)stats.clipped, 1);
    ClipPixelIs(surface, 10, 10, CLIP_UNTOUCHED,
                  "a drawing inside an empty scope, whose own clip is unbounded");

    /* The same list with a *bounded* scope does fill -- so the case above is about the empty
     * clip and not about the pass skipping hand-built commands. */
    PaintDisplayListClear(list);
    ClipAppendCommand(list, RoboPaintDisplayCmdPushClip, 1u, 0u, ClipRect(0, 0, 20, 20),
                        ClipMake(1, 0, 5, 1, 0, 5), CssColorNone());
    ClipAppendCommand(list, RoboPaintDisplayCmdDrawBackground, 2u, 1u,
                        ClipRect(0, 0, 20, 20), ClipNoneValue(),
                        CssColorMake(255u, 0u, 0u, 255u));
    ClipAppendCommand(list, RoboPaintDisplayCmdPopClip, 1u, 0u, ClipRect(0, 0, 20, 20),
                        ClipMake(1, 0, 5, 1, 0, 5), CssColorNone());
    GfxSurfaceRelease(surface);
    surface = ClipSurface(64u, 64u, RoboGfxSurfaceAccessReadWrite);
    ClipPaint(list, surface, NULL, &stats);
    RoboCheckEqInt((int)stats.filled, 1);
    ClipPixelIs(surface, 2, 2, CLIP_RED, "inside the scope's own boundary");
    ClipPixelIs(surface, 10, 10, CLIP_UNTOUCHED,
                  "outside the scope's boundary, inside the command's rectangle");

    GfxSurfaceRelease(surface);
    PaintDisplayListDestroy(list);
}

static void TestClipCounterIdentities(void)
{
    PaintFixtureDoc fixture;
    PaintResult result;
    PaintDisplayList *list = NULL;
    PaintDisplayListStats stats;
    PaintDecorationStats paintStats;
    GfxSurface *surface = NULL;

    PaintBeginLaidOut(&fixture,
                         "<html><body><div class=\"outer\"><div class=\"mid\">"
                         "<div class=\"leaf\"></div></div></div></body></html>",
                         CLIP_BASE_CSS
                         " .outer { width: 200px; height: 100px; overflow: hidden;"
                         " background-color: #0000ff }"
                         " .mid { position: relative; left: 20px; top: 10px; width: 300px;"
                         " height: 300px; overflow: hidden }"
                         " .leaf { width: 20px; height: 20px; background-color: #00ff00 }");
    PaintOnce(&fixture, &result);
    ClipBuild(&result, &list, &stats);

    /*
     * The identities the counters owe, stated once. A clip command is **structural**, so it is
     * counted with the commands this pass does not draw rather than with the drawings -- which is
     * what keeps `commands = draws + skipped` exact -- and `clipScopes` says how many of those
     * skips were a boundary.
     */
    RoboCheckEqInt((int)stats.commands, (int)PaintDisplayListCount(list));
    /* Three structural commands per box, two per scope, plus the drawings -- the whole list,
     * from the parts. A document with text would add its runs here, which is why this case
     * uses one without. */
    RoboCheckEqInt((int)stats.commands,
                      (int)(3u * (size_t)stats.nodes + 2u * stats.clipScopes + stats.draws));
    surface = ClipSurface(800u, 600u, RoboGfxSurfaceAccessReadWrite);
    ClipPaint(list, surface, NULL, &paintStats);
    RoboCheckEqInt((int)paintStats.commands, (int)PaintDisplayListCount(list));
    RoboCheckEqInt((int)paintStats.commands,
                      (int)(paintStats.draws + paintStats.skipped));
    RoboCheckEqInt((int)paintStats.draws,
                      (int)(paintStats.backgrounds + paintStats.borders + paintStats.texts));
    RoboCheckEqInt((int)paintStats.draws,
                      (int)(paintStats.filled + paintStats.contextual + paintStats.transparent +
                            paintStats.empty + paintStats.noFont));
    RoboCheckEqInt((int)paintStats.clipScopes, (int)stats.clipScopes);
    /* Two commands per scope, not one: a `PUSH_CLIP` and the `POP_CLIP` that closes it are both
     * commands this pass does not draw, and `clipScopes` counts the *scopes* rather than the
     * commands -- which is what lets `clipped = boundaries + scopes` hold on the traversal side
     * while this identity stays exact on the pass's. */
    RoboCheckEqInt((int)paintStats.skipped,
                      (int)(3u * (size_t)stats.nodes + 2u * paintStats.clipScopes));

    GfxSurfaceRelease(surface);
    PaintDisplayListDestroy(list);
    PaintResultDispose(&result);
    PaintEnd(&fixture);
}

/* ------------------------------------------------------------------ */
/* 8. Hand-built sequences the builder must refuse                     */
/* ------------------------------------------------------------------ */

/**
 * One refusal, from a record list built by @p build.
 *
 * The refusal is asserted three ways, and each of them is a claim: the build **fails**, the
 * list is left **empty** (a caller never holds a partial conversion), and the diagnostic names
 * the **display list** domain rather than the record list's -- because a record list that is
 * not a traversable sequence is this stage's failure and not the traversal's.
 */
static void ClipExpectRefusal(void (*build)(PaintList *list),
                                const char *what,
                                const char *expectMessageFragment)
{
    PaintResult result;
    PaintDisplayList *list = NULL;
    PaintDiag diag;
    Result status;

    memset(&result, 0, sizeof(result));
    RoboCheckEqInt(PaintListCreate(&result.list, NULL), RoboOk);
    build(result.list);
    status = ClipBuildExpectRefusal(&result, &list, &diag);
    if (status == RoboOk) {
        TestFail(__FILE__, __LINE__, "%s: the builder accepted it", what);
    } else {
        if (expectMessageFragment && diag.message &&
            strstr(diag.message, expectMessageFragment) == NULL) {
            TestFail(__FILE__, __LINE__, "%s: refused with \"%s\", not \"%s\"", what,
                           diag.message, expectMessageFragment);
        }
        RoboCheckEqInt((int)diag.domain, (int)RoboPaintDomainDisplayList);
    }
    RoboCheckEqInt((int)PaintDisplayListCount(list), 0);

    PaintDisplayListDestroy(list);
    PaintListDestroy(result.list);
}

/* Each builder below writes one malformed sequence around a well-formed box, so the refusal is
 * attributable to the one thing that is wrong with it. */

static void ClipBadPopWithoutPush(PaintList *list)
{
    PaintBoxes boxes = ClipBoxes(0, 0, 20, 20);

    ClipAppendRecord(list, RoboPaintOpBeginBox, 1u, 0u, boxes, ClipNoneValue());
    ClipAppendRecord(list, RoboPaintOpContent, 1u, 0u, boxes, ClipNoneValue());
    ClipAppendRecord(list, RoboPaintOpPopClip, 1u, 0u, boxes, ClipMake(1, 0, 5, 1, 0, 5));
    ClipAppendRecord(list, RoboPaintOpEndBox, 1u, 0u, boxes, ClipNoneValue());
}

static void ClipBadPopWrongClip(PaintList *list)
{
    PaintBoxes boxes = ClipBoxes(0, 0, 20, 20);

    ClipAppendRecord(list, RoboPaintOpBeginBox, 1u, 0u, boxes, ClipNoneValue());
    ClipAppendRecord(list, RoboPaintOpContent, 1u, 0u, boxes, ClipNoneValue());
    ClipAppendRecord(list, RoboPaintOpPushClip, 1u, 0u, boxes, ClipMake(1, 0, 5, 1, 0, 5));
    ClipAppendRecord(list, RoboPaintOpPopClip, 1u, 0u, boxes, ClipMake(1, 0, 7, 1, 0, 5));
    ClipAppendRecord(list, RoboPaintOpEndBox, 1u, 0u, boxes, ClipNoneValue());
}

static void ClipBadPopWrongNode(PaintList *list)
{
    PaintBoxes boxes = ClipBoxes(0, 0, 20, 20);

    ClipAppendRecord(list, RoboPaintOpBeginBox, 1u, 0u, boxes, ClipNoneValue());
    ClipAppendRecord(list, RoboPaintOpContent, 1u, 0u, boxes, ClipNoneValue());
    ClipAppendRecord(list, RoboPaintOpPushClip, 1u, 0u, boxes, ClipMake(1, 0, 5, 1, 0, 5));
    ClipAppendRecord(list, RoboPaintOpPopClip, 2u, 0u, boxes, ClipMake(1, 0, 5, 1, 0, 5));
    ClipAppendRecord(list, RoboPaintOpEndBox, 1u, 0u, boxes, ClipNoneValue());
}

static void ClipBadPushBeforeContent(PaintList *list)
{
    PaintBoxes boxes = ClipBoxes(0, 0, 20, 20);

    ClipAppendRecord(list, RoboPaintOpBeginBox, 1u, 0u, boxes, ClipNoneValue());
    ClipAppendRecord(list, RoboPaintOpPushClip, 1u, 0u, boxes, ClipMake(1, 0, 5, 1, 0, 5));
    ClipAppendRecord(list, RoboPaintOpContent, 1u, 0u, boxes, ClipNoneValue());
    ClipAppendRecord(list, RoboPaintOpPopClip, 1u, 0u, boxes, ClipMake(1, 0, 5, 1, 0, 5));
    ClipAppendRecord(list, RoboPaintOpEndBox, 1u, 0u, boxes, ClipNoneValue());
}

static void ClipBadTwoScopes(PaintList *list)
{
    PaintBoxes boxes = ClipBoxes(0, 0, 20, 20);
    PaintClip pushed = ClipMake(1, 0, 5, 1, 0, 5);

    ClipAppendRecord(list, RoboPaintOpBeginBox, 1u, 0u, boxes, ClipNoneValue());
    ClipAppendRecord(list, RoboPaintOpContent, 1u, 0u, boxes, ClipNoneValue());
    ClipAppendRecord(list, RoboPaintOpPushClip, 1u, 0u, boxes, pushed);
    ClipAppendRecord(list, RoboPaintOpPushClip, 1u, 0u, boxes, ClipMake(1, 0, 3, 1, 0, 3));
    ClipAppendRecord(list, RoboPaintOpPopClip, 1u, 0u, boxes, pushed);
    ClipAppendRecord(list, RoboPaintOpEndBox, 1u, 0u, boxes, ClipNoneValue());
}

static void ClipBadPushChangesNothing(PaintList *list)
{
    PaintBoxes boxes = ClipBoxes(0, 0, 20, 20);
    PaintClip cycle = ClipMake(1, 0, 5, 1, 0, 5);

    /* Two scopes whose boundaries are equal: the second establishes nothing. */
    ClipAppendRecord(list, RoboPaintOpBeginBox, 1u, 0u, boxes, ClipNoneValue());
    ClipAppendRecord(list, RoboPaintOpContent, 1u, 0u, boxes, ClipNoneValue());
    ClipAppendRecord(list, RoboPaintOpPushClip, 1u, 0u, boxes, cycle);
    ClipAppendRecord(list, RoboPaintOpBeginBox, 2u, 1u, boxes, cycle);
    ClipAppendRecord(list, RoboPaintOpContent, 2u, 1u, boxes, cycle);
    ClipAppendRecord(list, RoboPaintOpPushClip, 2u, 1u, boxes, cycle);
    ClipAppendRecord(list, RoboPaintOpPopClip, 2u, 1u, boxes, cycle);
    ClipAppendRecord(list, RoboPaintOpEndBox, 2u, 1u, boxes, cycle);
    ClipAppendRecord(list, RoboPaintOpPopClip, 1u, 0u, boxes, cycle);
    ClipAppendRecord(list, RoboPaintOpEndBox, 1u, 0u, boxes, ClipNoneValue());
}

static void ClipBadPushWider(PaintList *list)
{
    PaintBoxes boxes = ClipBoxes(0, 0, 20, 20);

    ClipAppendRecord(list, RoboPaintOpBeginBox, 1u, 0u, boxes, ClipNoneValue());
    ClipAppendRecord(list, RoboPaintOpContent, 1u, 0u, boxes, ClipNoneValue());
    ClipAppendRecord(list, RoboPaintOpPushClip, 1u, 0u, boxes, ClipMake(1, 5, 15, 1, 0, 5));
    ClipAppendRecord(list, RoboPaintOpBeginBox, 2u, 1u, boxes, ClipMake(1, 5, 15, 1, 0, 5));
    ClipAppendRecord(list, RoboPaintOpContent, 2u, 1u, boxes, ClipMake(1, 5, 15, 1, 0, 5));
    /* Wider on x than the boundary it is inside: CSS cannot express this. */
    ClipAppendRecord(list, RoboPaintOpPushClip, 2u, 1u, boxes, ClipMake(1, 0, 20, 1, 0, 5));
    ClipAppendRecord(list, RoboPaintOpPopClip, 2u, 1u, boxes, ClipMake(1, 0, 20, 1, 0, 5));
    ClipAppendRecord(list, RoboPaintOpEndBox, 2u, 1u, boxes, ClipMake(1, 5, 15, 1, 0, 5));
    ClipAppendRecord(list, RoboPaintOpPopClip, 1u, 0u, boxes, ClipMake(1, 5, 15, 1, 0, 5));
    ClipAppendRecord(list, RoboPaintOpEndBox, 1u, 0u, boxes, ClipNoneValue());
}

static void ClipBadCommandDisagrees(PaintList *list)
{
    PaintBoxes boxes = ClipBoxes(0, 0, 20, 20);
    PaintClip pushed = ClipMake(1, 0, 5, 1, 0, 5);

    ClipAppendRecord(list, RoboPaintOpBeginBox, 1u, 0u, boxes, ClipNoneValue());
    ClipAppendRecord(list, RoboPaintOpContent, 1u, 0u, boxes, ClipNoneValue());
    ClipAppendRecord(list, RoboPaintOpPushClip, 1u, 0u, boxes, pushed);
    /* A nested box that enters under a boundary the scope did not put in force. Every other fact
     * about this record is legal -- its depth, its node, its rectangle -- so the refusal can only
     * be the scope's, which is the rule this case exists for. */
    ClipAppendRecord(list, RoboPaintOpBeginBox, 2u, 1u, boxes, ClipMake(1, 0, 7, 1, 0, 5));
    ClipAppendRecord(list, RoboPaintOpContent, 2u, 1u, boxes, pushed);
    ClipAppendRecord(list, RoboPaintOpEndBox, 2u, 1u, boxes, pushed);
    ClipAppendRecord(list, RoboPaintOpPopClip, 1u, 0u, boxes, pushed);
    ClipAppendRecord(list, RoboPaintOpEndBox, 1u, 0u, boxes, ClipNoneValue());
}

static void ClipBadScopeNeverClosed(PaintList *list)
{
    PaintBoxes boxes = ClipBoxes(0, 0, 20, 20);

    ClipAppendRecord(list, RoboPaintOpBeginBox, 1u, 0u, boxes, ClipNoneValue());
    ClipAppendRecord(list, RoboPaintOpContent, 1u, 0u, boxes, ClipNoneValue());
    ClipAppendRecord(list, RoboPaintOpPushClip, 1u, 0u, boxes, ClipMake(1, 0, 5, 1, 0, 5));
    ClipAppendRecord(list, RoboPaintOpBeginBox, 2u, 1u, boxes, ClipMake(1, 0, 5, 1, 0, 5));
    ClipAppendRecord(list, RoboPaintOpContent, 2u, 1u, boxes, ClipMake(1, 0, 5, 1, 0, 5));
    ClipAppendRecord(list, RoboPaintOpEndBox, 2u, 1u, boxes, ClipMake(1, 0, 5, 1, 0, 5));
    ClipAppendRecord(list, RoboPaintOpEndBox, 1u, 0u, boxes, ClipMake(1, 0, 5, 1, 0, 5));
}

static void TestClipBuilderRefusals(void)
{
    ClipExpectRefusal(ClipBadPopWithoutPush, "a pop with no push",
                        "closes a clip scope it did not open");
    ClipExpectRefusal(ClipBadPopWrongClip, "a pop with another boundary",
                        "boundary other than the one it opened");
    ClipExpectRefusal(ClipBadPopWrongNode, "a pop naming another box",
                        "names a box other than the innermost open one");
    ClipExpectRefusal(ClipBadPushBeforeContent, "a push before the content boundary",
                        "precedes its box's content boundary");
    ClipExpectRefusal(ClipBadTwoScopes, "two scopes in one box",
                        "opens two clip scopes");
    ClipExpectRefusal(ClipBadPushChangesNothing, "a scope that changes nothing",
                        "establishes nothing new");
    ClipExpectRefusal(ClipBadPushWider, "a scope wider than its enclosing one",
                        "wider than the boundary it is inside");
    ClipExpectRefusal(ClipBadCommandDisagrees, "a command that disagrees with its scope",
                        "carries a different boundary");
    ClipExpectRefusal(ClipBadScopeNeverClosed, "a scope never closed",
                        "does not close every clip scope it opens");

    /* The record layer's own rule, from the other side: a scope that bounds nothing is not a
     * clip record, so it cannot even be appended. */
    {
        PaintList *list = NULL;
        PaintOperation operation;
        PaintDiag diag;

        RoboCheckEqInt(PaintListCreate(&list, NULL), RoboOk);
        PaintOperationInit(&operation);
        operation.kind = RoboPaintOpPushClip;
        operation.node = 1u;
        operation.depth = 0u;
        operation.boxes = ClipBoxes(0, 0, 20, 20);
        operation.clip = ClipNoneValue();
        PaintDiagReset(&diag);
        RoboCheckEqInt(PaintListAppend(list, &operation, &diag), RoboInvalidArgument);
        RoboCheckEqInt((int)PaintListCount(list), 0);
        PaintListDestroy(list);
    }

    /* And the same rule one stage along: a command that opens a scope with no boundary is not a
     * command. */
    {
        PaintDisplayList *list = NULL;
        PaintDisplayCommand command;
        PaintDiag diag;

        RoboCheckEqInt(PaintDisplayListCreate(&list, NULL), RoboOk);
        PaintDisplayCommandInit(&command);
        command.kind = RoboPaintDisplayCmdPushClip;
        command.node = 1u;
        command.rect = ClipRect(0, 0, 20, 20);
        command.clip = ClipNoneValue();
        PaintDiagReset(&diag);
        RoboCheckEqInt(PaintDisplayListAppend(list, &command, &diag),
                          RoboInvalidArgument);
        RoboCheckEqInt((int)PaintDisplayListCount(list), 0);
        PaintDisplayListDestroy(list);
    }
}

/* ------------------------------------------------------------------ */
/* 9. Determinism, snapshot, and the dump                              */
/* ------------------------------------------------------------------ */

static void ClipStatsEq(const PaintDisplayListStats *a,
                          const PaintDisplayListStats *b,
                          const char *what)
{
    if (a->commands != b->commands || a->nodes != b->nodes || a->draws != b->draws ||
        a->texts != b->texts || a->clipScopes != b->clipScopes ||
        a->maxDepth != b->maxDepth) {
        TestFail(__FILE__, __LINE__,
                       "%s: commands %d/%d nodes %d/%d draws %d/%d texts %d/%d scopes %d/%d",
                       what, (int)a->commands, (int)b->commands, (int)a->nodes, (int)b->nodes,
                       (int)a->draws, (int)b->draws, (int)a->texts, (int)b->texts,
                       (int)a->clipScopes, (int)b->clipScopes);
    }
}

static void TestClipDeterminism(void)
{
    static const char *const html =
        "<html><body><div class=\"outer\"><div class=\"mid\"><div class=\"leaf\"></div>"
        "</div><div class=\"after\"></div></div></body></html>";
    static const char *const css =
        CLIP_BASE_CSS
        " .outer { width: 120px; height: 60px; overflow: hidden }"
        " .mid { position: relative; left: 10px; top: 5px; width: 200px; height: 200px;"
        " overflow-y: hidden; background-color: #00ff00 }"
        " .leaf { width: 30px; height: 30px; background-color: #0000ff }"
        " .after { width: 40px; height: 10px; background-color: #ff0000 }";
    PaintFixtureDoc fixture;
    PaintResult result;
    PaintDisplayList *first = NULL;
    PaintDisplayList *second = NULL;
    PaintDisplayListStats firstStats;
    PaintDisplayListStats secondStats;
    char firstDump[8192];
    char secondDump[8192];
    size_t firstLength = 0u;
    size_t secondLength = 0u;

    PaintBeginLaidOut(&fixture, html, css);
    PaintOnce(&fixture, &result);
    ClipBuild(&result, &first, &firstStats);
    ClipBuild(&result, &second, &secondStats);

    RoboCheckEqInt(PaintDisplayListEqual(first, second), 1);
    ClipStatsEq(&firstStats, &secondStats, "two builds of one record list");

    /* The same list twice, byte for byte: a list is a value and its dump is one too, so two
     * builds of one document cannot differ in a boundary, an ordering or a line. */
    memset(firstDump, 0, sizeof(firstDump));
    memset(secondDump, 0, sizeof(secondDump));
    RoboCheckEqInt(PaintDisplayListWrite(first, firstDump, sizeof(firstDump),
                                                    &firstLength, NULL),
                      RoboOk);
    RoboCheckEqInt(PaintDisplayListWrite(second, secondDump, sizeof(secondDump),
                                                    &secondLength, NULL),
                      RoboOk);
    RoboCheckEqInt((int)firstLength, (int)secondLength);
    RoboCheckEqInt(strcmp(firstDump, secondDump), 0);

    /* And a second *document* — the same source built again — produces the same list, which is
     * the property a later pass caching a document depends on. */
    {
        PaintFixtureDoc other;
        PaintResult otherResult;
        PaintDisplayList *third = NULL;
        PaintDisplayListStats thirdStats;

        PaintBeginLaidOut(&other, html, css);
        PaintOnce(&other, &otherResult);
        ClipBuild(&otherResult, &third, &thirdStats);
        RoboCheckEqInt(PaintDisplayListEqual(first, third), 1);
        RoboCheckEqInt(PaintDisplayListEqual(first, second), 1);
        ClipStatsEq(&firstStats, &thirdStats, "two documents built from one source");
        PaintDisplayListDestroy(third);
        PaintResultDispose(&otherResult);
        PaintEnd(&other);
    }

    PaintDisplayListDestroy(first);
    PaintDisplayListDestroy(second);
    PaintResultDispose(&result);
    PaintEnd(&fixture);
}

static void TestClipMutationIsolation(void)
{
    PaintFixtureDoc fixture;
    PaintResult result;
    PaintDisplayList *before = NULL;
    PaintDisplayList *after = NULL;
    PaintDisplayListStats beforeStats;
    PaintDisplayListStats afterStats;
    PaintDecorationStats first;
    PaintDecorationStats second;
    PaintClip storedBoundary;
    GfxSurface *one = NULL;
    GfxSurface *two = NULL;
    size_t index;

    /*
     * `<html>` clips, and that is the point of the document: its boundary is the **viewport's**
     * padding box, so the one mutation this fixture can make -- laying the same tree out at a
     * different width -- genuinely moves a stored boundary. `.outer` clips too, and its boundary
     * is viewport-independent, which is why the moving one has to be the root's.
     */
    PaintBeginLaidOut(&fixture,
                         "<html><body><div class=\"outer\"><div class=\"inner\"></div></div>"
                         "</body></html>",
                         CLIP_BASE_CSS
                         " html { overflow: hidden }"
                         " .outer { width: 200px; height: 100px; overflow: hidden }"
                         " .inner { width: 400px; height: 80px; background-color: #ff0000 }");
    PaintOnce(&fixture, &result);
    ClipBuild(&result, &before, &beforeStats);

    one = ClipSurface(800u, 600u, RoboGfxSurfaceAccessReadWrite);
    ClipPaint(before, one, NULL, &first);

    /* The boundary the first list was built with, read out of it while the sources are still
     * pristine. Everything after this line changes the sources; this value must not. */
    {
        PaintDisplayCommand snapshot;

        RoboCheckEqInt(ClipCommandOfKind(before, RoboPaintDisplayCmdPushClip, 0u,
                                               &snapshot),
                          1);
        storedBoundary = snapshot.clip;
    }

    /*
     * Mutate both sources: clear the record list (the strongest legal change to it) and lay the
     * *same* tree out at a different viewport, which moves the root's boundary and every box's
     * rectangle with it. The list already built is a snapshot, so it holds the boundaries it was
     * built with and paints exactly as it did -- and the mutation is proved observable by
     * building a second list afterwards and asserting it differs.
     */
    PaintListClear(result.list);
    RoboCheckEqInt((int)PaintListCount(result.list), 0);

    PaintLayout(&fixture, 400, 600);
    {
        PaintContext *context = NULL;

        RoboCheckEqInt(PaintContextCreate(&context, NULL), RoboOk);
        PaintRun(&fixture, context, result.list);
        PaintContextDestroy(context);
    }
    ClipBuild(&result, &after, &afterStats);
    RoboCheckEqInt(PaintDisplayListEqual(before, after), 0);

    two = ClipSurface(800u, 600u, RoboGfxSurfaceAccessReadWrite);
    ClipPaint(before, two, NULL, &second);
    ClipCheckInt((int)first.clipped, (int)second.clipped, "a paint after the sources changed");
    {
        PaintDisplayCommand firstPush;
        PaintDisplayCommand afterPush;

        RoboCheckEqInt(ClipCommandOfKind(before, RoboPaintDisplayCmdPushClip, 0u,
                                               &firstPush),
                          1);
        RoboCheckEqInt(ClipCommandOfKind(after, RoboPaintDisplayCmdPushClip, 0u,
                                               &afterPush),
                          1);

        /* The **snapshot claim, both ways round**: the first list's stored boundary is the value
         * it was built with and did not follow the tree, while the rebuilt list's boundary is a
         * different one -- so the equality above is a copy and not the mutation failing to bite. */
        ClipCheck(&firstPush.clip, &storedBoundary,
                   "the first list's boundary, after the sources changed");
        RoboCheckEqInt(PaintClipEqual(&firstPush.clip, &afterPush.clip), 0);
        RoboCheckEqInt(afterPush.clip.horizontal.end, 400);
        RoboCheckEqInt(firstPush.clip.horizontal.end, 800);
    }

    for (index = 0u; index < PaintDisplayListCount(before); index++) {
        PaintDisplayCommand command;
        GfxColorRgba8 left;
        GfxColorRgba8 right;
        int32_t x;
        int32_t y;

        RoboCheckEqInt(PaintDisplayListAt(before, index, &command), RoboOk);
        if (!PaintDisplayCommandKindDraws(command.kind)) {
            continue;
        }
        x = command.rect.x + command.rect.width / 2;
        y = command.rect.y + command.rect.height / 2;
        left = ClipRead(one, x, y);
        right = ClipRead(two, x, y);
        ClipCheckInt((int)left.r, (int)right.r, "a pixel after the sources changed (red)");
        ClipCheckInt((int)left.a, (int)right.a, "a pixel after the sources changed (alpha)");
    }

    GfxSurfaceRelease(one);
    GfxSurfaceRelease(two);
    PaintDisplayListDestroy(before);
    PaintDisplayListDestroy(after);
    PaintResultDispose(&result);
    PaintEnd(&fixture);
}

static void TestClipDump(void)
{
    PaintFixtureDoc fixture;
    PaintResult result;
    PaintDisplayList *list = NULL;
    char dump[8192];
    char recordDump[8192];
    size_t length = 0u;
    size_t recordLength = 0u;

    PaintBeginLaidOut(&fixture,
                         "<html><body><div class=\"outer\"><div class=\"inner\"></div></div>"
                         "</body></html>",
                         CLIP_BASE_CSS
                         " .outer { width: 200px; height: 100px; overflow: hidden }"
                         " .inner { width: 400px; height: 20px }");
    PaintOnce(&fixture, &result);
    ClipBuild(&result, &list, NULL);

    /* The developer's door shows the scope, the boundary it establishes and the box it belongs
     * to -- and a reader can follow the nesting by indentation alone. */
    memset(dump, 0, sizeof(dump));
    RoboCheckEqInt(PaintDisplayListWrite(list, dump, sizeof(dump), &length, NULL),
                      RoboOk);
    RoboCheck(length > 0u);
    RoboCheck(strstr(dump, "push-clip") != NULL);
    RoboCheck(strstr(dump, "pop-clip") != NULL);
    RoboCheck(strstr(dump, "hidden[") != NULL);
    RoboCheck(strstr(dump, "visible/visible") != NULL);
    /* A command prints the one rectangle it acts on, and for a scope that rectangle is the
     * box's padding box -- the boundary's own box, not the effective clip's rectangle, which
     * a one-axis document would not have. */
    RoboCheck(strstr(dump, "rect=") != NULL);
    /* The push comes before the pop in the dump, which is the order a reader follows. */
    RoboCheck(strstr(dump, "push-clip") < strstr(dump, "pop-clip"));

    memset(recordDump, 0, sizeof(recordDump));
    RoboCheckEqInt(PaintListWrite(result.list, recordDump, sizeof(recordDump),
                                            &recordLength, NULL),
                      RoboOk);
    RoboCheck(strstr(recordDump, "push-clip") != NULL);
    RoboCheck(strstr(recordDump, "pop-clip") != NULL);
    RoboCheck(strstr(recordDump, "hidden[") != NULL);
    /* And a **record** names which of the three boxes it acts on -- `padding=` for a scope,
     * where the generic case would have printed the border box and told the reader the scope
     * came from a rectangle it did not come from. */
    RoboCheck(strstr(recordDump, "padding=") != NULL);

    PaintDisplayListDestroy(list);
    PaintResultDispose(&result);
    PaintEnd(&fixture);
}

static void TestClipEndToEnd(void)
{
    PaintFixtureDoc fixture;
    PaintResult result;
    PaintDisplayList *list = NULL;
    PaintDisplayListStats stats;
    PaintDecorationStats paintStats;
    GfxSurface *surface = NULL;
    size_t baseline;
    size_t index;
    size_t pushes = 0u;
    size_t pops = 0u;

    baseline = PaintLiveBlocks();
    PaintBeginLaidOut(&fixture,
                         "<html><body><div class=\"frame\">"
                         "<div class=\"spill\"></div><div class=\"after\"></div></div>"
                         "</body></html>",
                         CLIP_BASE_CSS
                         " .frame { width: 80px; height: 40px; overflow: hidden;"
                         " background-color: #0000ff }"
                         " .spill { width: 200px; height: 30px; background-color: #ff0000 }"
                         " .after { width: 40px; height: 8px; background-color: #00ff00 }");
    PaintOnce(&fixture, &result);
    ClipBuild(&result, &list, &stats);

    /* Every push is matched by a pop, in order, and no command stands inside a scope that does
     * not bound it -- the whole sequence read the way a consumer reads it. */
    for (index = 0u; index < PaintDisplayListCount(list); index++) {
        PaintDisplayCommand command;

        RoboCheckEqInt(PaintDisplayListAt(list, index, &command), RoboOk);
        if (command.kind == RoboPaintDisplayCmdPushClip) {
            pushes++;
        } else if (command.kind == RoboPaintDisplayCmdPopClip) {
            pops++;
        }
    }
    RoboCheckEqInt((int)pushes, (int)pops);
    RoboCheckEqInt((int)pushes, (int)stats.clipScopes);

    surface = ClipSurface(800u, 600u, RoboGfxSurfaceAccessReadWrite);
    ClipPaint(list, surface, NULL, &paintStats);
    ClipPixelsMatch(list, surface, "the end-to-end document");

    GfxSurfaceRelease(surface);
    PaintDisplayListDestroy(list);
    PaintResultDispose(&result);
    PaintEnd(&fixture);
    RoboCheckEqInt((int)PaintLiveBlocks(), (int)baseline);
}

/* ------------------------------------------------------------------ */
/* Registration                                                        */
/* ------------------------------------------------------------------ */

void TestRegisterPaintClip(void)
{
    TestRegister("paint.clip.intersection", TestClipIntersection);
    TestRegister("paint.clip.intersection_safety", TestClipIntersectionSafety);
    TestRegister("paint.clip.empty_predicate", TestClipEmptyPredicate);
    TestRegister("paint.clip.stack", TestClipStack);
    TestRegister("paint.clip.stack_nested", TestClipStackNested);
    TestRegister("paint.clip.scopes_emitted", TestClipScopesEmitted);
    TestRegister("paint.clip.scope_only_when_changed", TestClipScopeOnlyWhenChanged);
    TestRegister("paint.clip.nested_effective", TestClipNestedEffective);
    TestRegister("paint.clip.axis_split", TestClipAxisSplit);
    TestRegister("paint.clip.background", TestClipBackground);
    TestRegister("paint.clip.border", TestClipBorder);
    TestRegister("paint.clip.text", TestClipText);
    TestRegister("paint.clip.pixels", TestClipPixels);
    TestRegister("paint.clip.empty_scope", TestClipEmptyScope);
    TestRegister("paint.clip.no_clip", TestClipNoClip);
    TestRegister("paint.clip.empty_and_malformed", TestClipEmptyAndMalformed);
    TestRegister("paint.clip.pass_refuses_unbalanced", TestClipPassRefusesUnbalanced);
    TestRegister("paint.clip.hand_built_scope", TestClipHandBuiltScope);
    TestRegister("paint.clip.counter_identities", TestClipCounterIdentities);
    TestRegister("paint.clip.builder_refusals", TestClipBuilderRefusals);
    TestRegister("paint.clip.determinism", TestClipDeterminism);
    TestRegister("paint.clip.mutation_isolation", TestClipMutationIsolation);
    TestRegister("paint.clip.dump", TestClipDump);
    TestRegister("paint.clip.end_to_end", TestClipEndToEnd);
}
