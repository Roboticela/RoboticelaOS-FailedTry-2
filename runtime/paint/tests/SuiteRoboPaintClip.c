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
 *                            cannot say -- and `robo_paint_clip_rect` *refuses* to answer a
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
 * **What this suite cannot test**: image painting. There is no `IMAGE` operation and no
 * `draw-image` command in this build (Phase 11.7 is not implemented), so the brief's "image
 * drawing respects clipping" has no subject here. The mechanism is shared rather than
 * per-kind, though -- a drawing is bounded by the clip its scope put in force, applied once
 * before the kind's own branch -- so an image kind added later is bounded by construction and
 * its case belongs beside `paint.clip.text.drawing_is_bounded` rather than in a new mechanism.
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
#define CLIP_RED robo_gfx_rgba8(255, 0, 0, 255)
#define CLIP_BLUE robo_gfx_rgba8(0, 0, 255, 255)
#define CLIP_GREEN robo_gfx_rgba8(0, 255, 0, 255)
#define CLIP_UNTOUCHED robo_gfx_rgba8(0, 0, 0, 0)

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
} clip_synth_table_t;

static void clip_put_u8(uint8_t **p, uint8_t v) { *(*p)++ = v; }
static void clip_put_u16(uint8_t **p, uint16_t v)
{
    *(*p)++ = (uint8_t)(v >> 8);
    *(*p)++ = (uint8_t)v;
}
static void clip_put_i16(uint8_t **p, int16_t v) { clip_put_u16(p, (uint16_t)v); }
static void clip_put_u32(uint8_t **p, uint32_t v)
{
    *(*p)++ = (uint8_t)(v >> 24);
    *(*p)++ = (uint8_t)(v >> 16);
    *(*p)++ = (uint8_t)(v >> 8);
    *(*p)++ = (uint8_t)v;
}
static void clip_put_i64(uint8_t **p, int64_t v)
{
    int i;

    for (i = 7; i >= 0; i--) {
        clip_put_u8(p, (uint8_t)((uint64_t)v >> (i * 8)));
    }
}

static void clip_write_search_fields(uint8_t *hdr, uint16_t num_tables)
{
    uint16_t pow2 = 1u;
    uint16_t entry = 0u;
    uint8_t *p;

    while ((uint16_t)(pow2 * 2u) <= num_tables) {
        pow2 = (uint16_t)(pow2 * 2u);
        entry++;
    }
    p = hdr + 4;
    clip_put_u16(&p, num_tables);
    clip_put_u16(&p, (uint16_t)(pow2 * 16u));
    clip_put_u16(&p, entry);
    clip_put_u16(&p, (uint16_t)(num_tables * 16u - pow2 * 16u));
}

static size_t clip_build_sfnt(uint8_t *out,
                              size_t out_cap,
                              uint32_t version,
                              const clip_synth_table_t *tables,
                              uint32_t n)
{
    size_t dir_end = 12u + (size_t)n * 16u;
    size_t cursor;
    size_t need = dir_end;
    uint32_t i;
    uint8_t *p;

    for (i = 0u; i < n; i++) {
        need += ((size_t)tables[i].length + 3u) & ~(size_t)3u;
    }
    if (need > out_cap) {
        return 0u;
    }
    memset(out, 0, need);
    p = out;
    clip_put_u32(&p, version);
    clip_write_search_fields(out, (uint16_t)n);
    cursor = dir_end;
    for (i = 0u; i < n; i++) {
        uint32_t pad = (uint32_t)(((size_t)tables[i].length + 3u) & ~(size_t)3u);
        uint8_t *entry = out + 12u + (size_t)i * 16u;

        clip_put_u32(&entry, tables[i].tag);
        clip_put_u32(&entry, 0u);
        clip_put_u32(&entry, (uint32_t)cursor);
        clip_put_u32(&entry, tables[i].length);
        memcpy(out + cursor, tables[i].data, tables[i].length);
        cursor += pad;
    }
    return cursor;
}

static size_t clip_build_head(uint8_t *b, uint16_t upem)
{
    uint8_t *p = b;

    clip_put_u16(&p, 1);
    clip_put_u16(&p, 0);
    clip_put_u32(&p, 0x00010000u);
    clip_put_u32(&p, 0u);
    clip_put_u32(&p, ROBO_FONT_HEAD_MAGIC);
    clip_put_u16(&p, 0u);
    clip_put_u16(&p, upem);
    clip_put_i64(&p, 0);
    clip_put_i64(&p, 0);
    clip_put_i16(&p, -50);
    clip_put_i16(&p, -200);
    clip_put_i16(&p, 1000);
    clip_put_i16(&p, 800);
    clip_put_u16(&p, 0u);
    clip_put_u16(&p, 8u);
    clip_put_i16(&p, 2);
    clip_put_i16(&p, 1); /* long loca, so loca entries are u32 */
    clip_put_i16(&p, 0);
    return (size_t)(p - b);
}

static size_t clip_build_hhea(uint8_t *b, uint16_t n_h_metrics)
{
    uint8_t *p = b;
    int i;

    clip_put_u16(&p, 1);
    clip_put_u16(&p, 0);
    clip_put_i16(&p, 800);  /* ascender */
    clip_put_i16(&p, -200); /* descender */
    clip_put_i16(&p, 90);   /* line gap */
    clip_put_u16(&p, 900);
    clip_put_i16(&p, -10);
    clip_put_i16(&p, -20);
    clip_put_i16(&p, 880);
    clip_put_i16(&p, 1);
    clip_put_i16(&p, 0);
    clip_put_i16(&p, 0);
    for (i = 0; i < 4; i++) {
        clip_put_i16(&p, 0);
    }
    clip_put_i16(&p, 0);
    clip_put_u16(&p, n_h_metrics);
    return (size_t)(p - b);
}

static size_t clip_build_maxp(uint8_t *b, uint16_t num_glyphs)
{
    uint8_t *p = b;
    int i;

    clip_put_u32(&p, 0x00010000u);
    clip_put_u16(&p, num_glyphs);
    clip_put_u16(&p, 8);
    clip_put_u16(&p, 2);
    for (i = 0; i < 12; i++) {
        clip_put_u16(&p, 0);
    }
    return (size_t)(p - b);
}

static size_t clip_build_hmtx(uint8_t *b, const uint16_t *advances, uint16_t n)
{
    uint8_t *p = b;
    uint16_t i;

    for (i = 0u; i < n; i++) {
        clip_put_u16(&p, advances[i]);
        clip_put_i16(&p, (int16_t)(i + 1));
    }
    return (size_t)(p - b);
}

/** `n + 1` long `loca` entries from @p offsets. */
static size_t clip_build_loca(uint8_t *loca, const uint32_t *offsets, uint16_t n)
{
    uint8_t *p = loca;
    uint16_t i;

    for (i = 0u; i <= n; i++) {
        clip_put_u32(&p, offsets[i]);
    }
    return (size_t)(p - loca);
}

/** One glyph: a square from (0, 0) to (100, 100) in font units. */
static size_t clip_build_square_glyf(uint8_t *b)
{
    uint8_t *p = b;

    clip_put_i16(&p, 1);                        /* numberOfContours */
    clip_put_i16(&p, 0);                        /* xMin */
    clip_put_i16(&p, 0);                        /* yMin */
    clip_put_i16(&p, CLIP_FACE_SQUARE_UNITS);   /* xMax */
    clip_put_i16(&p, CLIP_FACE_SQUARE_UNITS);   /* yMax */
    clip_put_u16(&p, 3);                        /* endPtsOfContours[0] */
    clip_put_u16(&p, 0);                        /* instructionLength */
    clip_put_u8(&p, (uint8_t)(ROBO_FONT_GLYF_FLAG_ON_CURVE |
                              ROBO_FONT_GLYF_FLAG_X_SAME_OR_POSITIVE |
                              ROBO_FONT_GLYF_FLAG_Y_SAME_OR_POSITIVE));
    clip_put_u8(&p, (uint8_t)(ROBO_FONT_GLYF_FLAG_ON_CURVE | ROBO_FONT_GLYF_FLAG_X_SHORT |
                              ROBO_FONT_GLYF_FLAG_X_SAME_OR_POSITIVE |
                              ROBO_FONT_GLYF_FLAG_Y_SAME_OR_POSITIVE));
    clip_put_u8(&p, (uint8_t)(ROBO_FONT_GLYF_FLAG_ON_CURVE | ROBO_FONT_GLYF_FLAG_Y_SHORT |
                              ROBO_FONT_GLYF_FLAG_X_SAME_OR_POSITIVE |
                              ROBO_FONT_GLYF_FLAG_Y_SAME_OR_POSITIVE));
    clip_put_u8(&p, (uint8_t)(ROBO_FONT_GLYF_FLAG_ON_CURVE | ROBO_FONT_GLYF_FLAG_X_SHORT |
                              ROBO_FONT_GLYF_FLAG_Y_SAME_OR_POSITIVE));
    clip_put_u8(&p, CLIP_FACE_SQUARE_UNITS);
    clip_put_u8(&p, CLIP_FACE_SQUARE_UNITS);
    clip_put_u8(&p, CLIP_FACE_SQUARE_UNITS);
    return (size_t)(p - b);
}

/**
 * A one-subtable cmap holding a format-4 segment table mapping `first..last` contiguously to
 * glyph ids `1` upward (delta `1 - first`), with the required `0xFFFF` segment appended.
 */
static size_t clip_build_cmap(uint8_t *b, uint16_t first, uint16_t last)
{
    uint8_t *p = b;
    uint8_t *length_ptr;
    uint8_t *subtable = b + 12;
    uint16_t length;

    clip_put_u16(&p, 0);   /* version */
    clip_put_u16(&p, 1);   /* numTables */
    clip_put_u16(&p, 3);   /* Windows */
    clip_put_u16(&p, 1);   /* Unicode BMP */
    clip_put_u32(&p, 12u); /* subtable offset */

    clip_put_u16(&p, 4);    /* format */
    length_ptr = p;
    clip_put_u16(&p, 0);    /* length, patched below */
    clip_put_u16(&p, 0);    /* language */
    clip_put_u16(&p, 4);    /* segCountX2 */
    clip_put_u16(&p, 4);    /* searchRange */
    clip_put_u16(&p, 1);    /* entrySelector */
    clip_put_u16(&p, 0);    /* rangeShift */
    clip_put_u16(&p, last);
    clip_put_u16(&p, 0xFFFFu);
    clip_put_u16(&p, 0); /* reservedPad */
    clip_put_u16(&p, first);
    clip_put_u16(&p, 0xFFFFu);
    clip_put_i16(&p, (int16_t)(1 - (int32_t)first)); /* idDelta[0]: 'A' -> glyph 1 */
    clip_put_i16(&p, 1);                             /* idDelta[1] */
    clip_put_u16(&p, 0);                             /* idRangeOffset[0] */
    clip_put_u16(&p, 0);                             /* idRangeOffset[1] */

    length = (uint16_t)(p - subtable);
    length_ptr[0] = (uint8_t)(length >> 8);
    length_ptr[1] = (uint8_t)length;
    return (size_t)(p - b);
}

/** What a face plus its source cost a caller, so a case can release both the way it built them. */
typedef struct clip_face {
    robo_font_source_t *source;
    robo_font_face_t *face;
} clip_face_t;

/** Build the synthetic face, with 'A' -> glyph 1 and glyph 0 (`.notdef`) empty. */
static void clip_face_create(clip_face_t *out)
{
    uint8_t head[64], hhea[64], maxp[64], hmtx[64], loca[64], glyf[128], cmap[64];
    uint8_t font[1024];
    static const uint16_t advances[CLIP_FACE_NUM_GLYPHS] = {
        (uint16_t)CLIP_FACE_ADVANCE_NOTDEF,
        (uint16_t)CLIP_FACE_ADVANCE_GLYPH
    };
    clip_synth_table_t tables[7];
    uint32_t nt = 0u;
    size_t glyph_size;
    size_t n;

    out->source = NULL;
    out->face = NULL;

    glyph_size = clip_build_square_glyf(glyf);
    {
        const uint32_t offsets[CLIP_FACE_NUM_GLYPHS + 1u] = {
            0u, 0u, (uint32_t)glyph_size
        };

        (void)clip_build_loca(loca, offsets, (uint16_t)CLIP_FACE_NUM_GLYPHS);
    }

    tables[nt].tag = ROBO_FONT_TAG_HEAD;
    tables[nt].data = head;
    tables[nt].length = (uint32_t)clip_build_head(head, (uint16_t)CLIP_FACE_UPEM);
    nt++;
    tables[nt].tag = ROBO_FONT_TAG_HHEA;
    tables[nt].data = hhea;
    tables[nt].length = (uint32_t)clip_build_hhea(hhea, (uint16_t)CLIP_FACE_NUM_GLYPHS);
    nt++;
    tables[nt].tag = ROBO_FONT_TAG_MAXP;
    tables[nt].data = maxp;
    tables[nt].length = (uint32_t)clip_build_maxp(maxp, (uint16_t)CLIP_FACE_NUM_GLYPHS);
    nt++;
    tables[nt].tag = ROBO_FONT_TAG_HMTX;
    tables[nt].data = hmtx;
    tables[nt].length = (uint32_t)clip_build_hmtx(hmtx, advances,
                                                  (uint16_t)CLIP_FACE_NUM_GLYPHS);
    nt++;
    tables[nt].tag = ROBO_FONT_TAG_CMAP;
    tables[nt].data = cmap;
    tables[nt].length = (uint32_t)clip_build_cmap(cmap, 0x41u, 0x41u);
    nt++;
    tables[nt].tag = ROBO_FONT_TAG_LOCA;
    tables[nt].data = loca;
    tables[nt].length = (uint32_t)((CLIP_FACE_NUM_GLYPHS + 1u) * 4u);
    nt++;
    tables[nt].tag = ROBO_FONT_TAG_GLYF;
    tables[nt].data = glyf;
    tables[nt].length = (uint32_t)glyph_size;
    nt++;

    n = clip_build_sfnt(font, sizeof(font), ROBO_FONT_SFNT_VERSION_TRUE, tables, nt);
    ROBO_CHECK(n > 0u);
    ROBO_CHECK_EQ_INT(robo_font_source_create_memory_copy(font, n, &out->source), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_font_face_create(out->source, 0, &out->face), ROBO_OK);
}

static void clip_face_destroy(clip_face_t *face)
{
    if (face->face) {
        robo_font_face_release(face->face);
        face->face = NULL;
    }
    if (face->source) {
        robo_font_source_release(face->source);
        face->source = NULL;
    }
}

/* ------------------------------------------------------------------ */
/* Local helpers                                                       */
/* ------------------------------------------------------------------ */

static robo_layout_rect_t clip_rect(int32_t x, int32_t y, int32_t w, int32_t h)
{
    robo_layout_rect_t rect;

    rect.x = x;
    rect.y = y;
    rect.width = w;
    rect.height = h;
    return rect;
}

/** The cleared clip: no boundary on either axis. */
static robo_paint_clip_t clip_none_value(void)
{
    return robo_paint_clip_none();
}

/** A clip with @p x and/or @p y bounded, and the others left open. */
static robo_paint_clip_t clip_make(int x_bounded,
                                   int32_t x0,
                                   int32_t x1,
                                   int y_bounded,
                                   int32_t y0,
                                   int32_t y1)
{
    robo_paint_clip_t clip;

    clip = robo_paint_clip_none();
    if (x_bounded) {
        clip.horizontal.mode = ROBO_CSS_OVERFLOW_HIDDEN;
        clip.horizontal.start = x0;
        clip.horizontal.end = x1;
    }
    if (y_bounded) {
        clip.vertical.mode = ROBO_CSS_OVERFLOW_HIDDEN;
        clip.vertical.start = y0;
        clip.vertical.end = y1;
    }
    return clip;
}

/** Report two clips that should have matched, in both axes' terms. */
static void clip_report(const char *what,
                        const robo_paint_clip_t *expected,
                        const robo_paint_clip_t *actual)
{
    robo_test_fail(__FILE__, __LINE__,
                   "%s: expected x=%d[%d,%d] y=%d[%d,%d], got x=%d[%d,%d] y=%d[%d,%d]", what,
                   (int)expected->horizontal.mode, (int)expected->horizontal.start,
                   (int)expected->horizontal.end, (int)expected->vertical.mode,
                   expected->vertical.start, expected->vertical.end,
                   (int)actual->horizontal.mode, (int)actual->horizontal.start,
                   (int)actual->horizontal.end, (int)actual->vertical.mode,
                   actual->vertical.start, actual->vertical.end);
}

static void clip_check(const robo_paint_clip_t *actual,
                       const robo_paint_clip_t *expected,
                       const char *what)
{
    if (!robo_paint_clip_equal(actual, expected)) {
        clip_report(what, expected, actual);
    }
}

static void clip_check_int(int actual, int expected, const char *what)
{
    if (actual != expected) {
        robo_test_fail(__FILE__, __LINE__, "%s: expected %d, got %d", what, expected, actual);
    }
}

/** A CPU surface for a paint pass: owned, RGBA8, zero-filled (so "untouched" is transparent
 *  and an unpainted pixel is distinguishable from a black one). */
static robo_gfx_surface_t *clip_surface(uint32_t width,
                                        uint32_t height,
                                        robo_gfx_surface_access_t access)
{
    robo_gfx_surface_desc_t desc;
    robo_gfx_surface_t *surface = NULL;

    robo_gfx_surface_desc_init(&desc);
    desc.width = width;
    desc.height = height;
    desc.format = ROBO_GFX_FORMAT_RGBA8;
    desc.access = access;
    ROBO_CHECK_EQ_INT(robo_gfx_surface_create(&desc, &surface), ROBO_OK);
    ROBO_CHECK(surface != NULL);
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
static void clip_build(const paint_result_t *records,
                       robo_paint_display_list_t **out_list,
                       robo_paint_display_list_stats_t *out_stats)
{
    robo_paint_display_list_builder_t *builder = NULL;
    robo_paint_diag_t diag;
    robo_result_t status;

    if (out_stats) {
        memset(out_stats, 0, sizeof *out_stats);
    }
    ROBO_CHECK_EQ_INT(robo_paint_display_list_create(out_list, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_paint_display_list_builder_create(&builder, NULL), ROBO_OK);
    robo_paint_diag_reset(&diag);
    status = robo_paint_display_list_builder_build(builder, records->list, *out_list, out_stats,
                                                   &diag);
    if (status != ROBO_OK) {
        robo_test_fail(__FILE__, __LINE__, "the display build refused the records: %s",
                       diag.message ? diag.message : "(no message)");
    }
    robo_paint_display_list_builder_destroy(builder);
}

/**
 * Build a display list from @p records, reporting the outcome rather than asserting it.
 *
 * The refusal cases need a build that is *expected* to fail, so the status and the message come
 * back to the caller -- and the list is handed back emptied, which is itself one of the claims
 * (`a refused build leaves no partial list`).
 */
static robo_result_t clip_build_expect_refusal(const paint_result_t *records,
                                               robo_paint_display_list_t **out_list,
                                               robo_paint_diag_t *out_diag)
{
    robo_paint_display_list_builder_t *builder = NULL;
    robo_result_t status;

    ROBO_CHECK_EQ_INT(robo_paint_display_list_create(out_list, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_paint_display_list_builder_create(&builder, NULL), ROBO_OK);
    robo_paint_diag_reset(out_diag);
    status = robo_paint_display_list_builder_build(builder, records->list, *out_list, NULL,
                                                   out_diag);
    robo_paint_display_list_builder_destroy(builder);
    return status;
}

/** Paint @p list into @p target, asserting the pass succeeds. */
static void clip_paint(const robo_paint_display_list_t *list,
                       robo_gfx_surface_t *target,
                       robo_font_face_t *face,
                       robo_paint_decoration_stats_t *out_stats)
{
    robo_paint_diag_t diag;
    robo_result_t status;

    robo_paint_diag_reset(&diag);
    status = robo_paint_decoration_paint(list, target, face, out_stats, &diag);
    if (status != ROBO_OK) {
        robo_test_fail(__FILE__, __LINE__, "the paint pass refused the list: %s (%s:%d)",
                       diag.message ? diag.message : "(no message)",
                       diag.file ? diag.file : "?", diag.line);
    }
}

/** Read the pixel at (x, y), failing when the point is outside the surface. */
static robo_gfx_color_rgba8_t clip_read(robo_gfx_surface_t *surface, int32_t x, int32_t y)
{
    robo_gfx_color_rgba8_t color;
    int32_t width = (int32_t)robo_gfx_surface_width(surface);
    int32_t height = (int32_t)robo_gfx_surface_height(surface);

    memset(&color, 0, sizeof(color));
    if (x < 0 || y < 0 || x >= width || y >= height) {
        robo_test_fail(__FILE__, __LINE__, "the sample %d,%d is outside the %dx%d surface", (int)x,
                       (int)y, (int)width, (int)height);
        return color;
    }
    ROBO_CHECK_EQ_INT(robo_gfx_surface_get_pixel(surface, (uint32_t)x, (uint32_t)y, &color),
                      ROBO_OK);
    return color;
}

/** Assert the pixel at (x, y) is @p expected. */
static void clip_pixel_is(robo_gfx_surface_t *surface,
                          int32_t x,
                          int32_t y,
                          robo_gfx_color_rgba8_t expected,
                          const char *what)
{
    robo_gfx_color_rgba8_t actual = clip_read(surface, x, y);

    if (actual.r != expected.r || actual.g != expected.g || actual.b != expected.b ||
        actual.a != expected.a) {
        robo_test_fail(__FILE__, __LINE__,
                       "%s: at %d,%d expected %d,%d,%d,%d, got %d,%d,%d,%d", what, (int)x,
                       (int)y, (int)expected.r, (int)expected.g, (int)expected.b,
                       (int)expected.a, (int)actual.r, (int)actual.g, (int)actual.b,
                       (int)actual.a);
    }
}

/** Non-zero when any pixel in the half-open rectangle [x0,x1) x [y0,y1) is not transparent. */
static int clip_any_ink(robo_gfx_surface_t *surface,
                        int32_t x0,
                        int32_t y0,
                        int32_t x1,
                        int32_t y1)
{
    int32_t x;
    int32_t y;

    for (y = y0; y < y1; y++) {
        for (x = x0; x < x1; x++) {
            robo_gfx_color_rgba8_t color = clip_read(surface, x, y);

            if (color.a != 0u) {
                return 1;
            }
        }
    }
    return 0;
}

/** The @p nth command of @p list whose kind is @p kind, or 0 when there is none. */
static int clip_command_of_kind(const robo_paint_display_list_t *list,
                                robo_paint_display_command_kind_t kind,
                                size_t nth,
                                robo_paint_display_command_t *out)
{
    size_t index;
    size_t seen = 0u;

    for (index = 0u; index < robo_paint_display_list_count(list); index++) {
        robo_paint_display_command_t command;

        if (robo_paint_display_list_at(list, index, &command) != ROBO_OK) {
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
static size_t clip_count_of_kind(const robo_paint_display_list_t *list,
                                 robo_paint_display_command_kind_t kind)
{
    return robo_paint_display_list_count_of_kind(list, kind);
}

/** The index of the @p nth command of @p kind whose node is @p node, or `(size_t)-1`. */
static size_t clip_index_of_kind_for_node(const robo_paint_display_list_t *list,
                                          robo_paint_display_command_kind_t kind,
                                          robo_layout_node_id_t node,
                                          size_t nth)
{
    size_t index;
    size_t seen = 0u;

    for (index = 0u; index < robo_paint_display_list_count(list); index++) {
        robo_paint_display_command_t command;

        if (robo_paint_display_list_at(list, index, &command) != ROBO_OK) {
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
static robo_paint_boxes_t clip_boxes(int32_t x, int32_t y, int32_t w, int32_t h)
{
    robo_paint_boxes_t boxes;

    robo_paint_boxes_init(&boxes);
    boxes.border = clip_rect(x, y, w, h);
    boxes.padding = boxes.border;
    boxes.content = boxes.border;
    boxes.insets_measured = 0;
    return boxes;
}

/** Append one hand-built **record** to @p list, asserting the append succeeds. */
static void clip_append_record(robo_paint_list_t *list,
                               robo_paint_op_kind_t kind,
                               robo_layout_node_id_t node,
                               size_t depth,
                               robo_paint_boxes_t boxes,
                               robo_paint_clip_t clip)
{
    robo_paint_operation_t operation;
    robo_paint_diag_t diag;

    robo_paint_operation_init(&operation);
    operation.kind = kind;
    operation.node = node;
    operation.depth = depth;
    operation.boxes = boxes;
    operation.origin = robo_gfx_point(0, 0);
    operation.clip = clip;
    operation.color = robo_paint_op_kind_draws(kind)
                              ? robo_css_color_make(255u, 0u, 0u, 255u)
                              : robo_css_color_none();
    /* The style rule: a band carries a style that generates a border, and nothing else carries
     * one -- the record layer's own validity rule from the other side. */
    operation.style = robo_paint_op_kind_is_border(kind)
                              ? ROBO_CSS_BORDER_STYLE_SOLID
                              : ROBO_CSS_BORDER_STYLE_NONE;
    robo_paint_diag_reset(&diag);
    if (robo_paint_list_append(list, &operation, &diag) != ROBO_OK) {
        robo_test_fail(__FILE__, __LINE__, "a hand-built record was refused: %s",
                       diag.message ? diag.message : "(no message)");
    }
}

/** Append one hand-built **display command**, asserting the append succeeds. */
static void clip_append_command(robo_paint_display_list_t *list,
                                robo_paint_display_command_kind_t kind,
                                robo_layout_node_id_t node,
                                size_t depth,
                                robo_layout_rect_t rect,
                                robo_paint_clip_t clip,
                                robo_css_color_t color)
{
    robo_paint_display_command_t command;
    robo_paint_diag_t diag;

    robo_paint_display_command_init(&command);
    command.kind = kind;
    command.node = node;
    command.depth = depth;
    command.rect = rect;
    command.clip = clip;
    command.color = color;
    robo_paint_diag_reset(&diag);
    if (robo_paint_display_list_append(list, &command, &diag) != ROBO_OK) {
        robo_test_fail(__FILE__, __LINE__, "a hand-built command was refused: %s",
                       diag.message ? diag.message : "(no message)");
    }
}

/* ------------------------------------------------------------------ */
/* The oracle                                                          */
/* ------------------------------------------------------------------ */

/*
 * A replay of the list that predicts the pixel, written independently of the pass: it keeps its
 * own clip stack (a local array, *not* `robo_paint_clip_stack_t` -- the type under test in the
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
    robo_paint_clip_t records[CLIP_ORACLE_MAX_DEPTH];
    size_t depth;
} clip_oracle_stack_t;

static int clip_admits(const robo_paint_clip_t *clip, int32_t x, int32_t y)
{
    if (robo_paint_clip_axis_is_active(&clip->horizontal) &&
        (x < clip->horizontal.start || x >= clip->horizontal.end)) {
        return 0;
    }
    if (robo_paint_clip_axis_is_active(&clip->vertical) &&
        (y < clip->vertical.start || y >= clip->vertical.end)) {
        return 0;
    }
    return 1;
}

/** The oracle's own clip stack: the innermost push, or the command's own clip when empty. */
static robo_paint_clip_t clip_oracle_in_force(const clip_oracle_stack_t *stack,
                                              const robo_paint_clip_t *command_clip)
{
    if (stack->depth == 0u) {
        return *command_clip;
    }
    return stack->records[stack->depth - 1u];
}

static robo_gfx_color_rgba8_t clip_expected_at(const robo_paint_display_list_t *list,
                                               int32_t x,
                                               int32_t y)
{
    clip_oracle_stack_t stack;
    robo_gfx_color_rgba8_t color = CLIP_UNTOUCHED;
    size_t index;

    memset(&stack, 0, sizeof(stack));
    for (index = 0u; index < robo_paint_display_list_count(list); index++) {
        robo_paint_display_command_t command;

        if (robo_paint_display_list_at(list, index, &command) != ROBO_OK) {
            return color;
        }
        if (command.kind == ROBO_PAINT_DISPLAY_CMD_PUSH_CLIP) {
            if (stack.depth < CLIP_ORACLE_MAX_DEPTH) {
                stack.records[stack.depth] = command.clip;
                stack.depth++;
            }
            continue;
        }
        if (command.kind == ROBO_PAINT_DISPLAY_CMD_POP_CLIP) {
            if (stack.depth > 0u) {
                stack.depth--;
            }
            continue;
        }
        if (!robo_paint_display_command_kind_draws(command.kind)) {
            continue;
        }
        if (!robo_paint_color_is_drawable(command.color)) {
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
            robo_paint_clip_t in_force = clip_oracle_in_force(&stack, &command.clip);

            if (!clip_admits(&in_force, x, y)) {
                continue;
            }
        }
        color = robo_gfx_rgba8(command.color.red, command.color.green, command.color.blue,
                               command.color.alpha);
    }
    return color;
}

/** Assert the surface against the oracle at every **drawing** command's centre. */
static void clip_pixels_match(const robo_paint_display_list_t *list,
                              robo_gfx_surface_t *surface,
                              const char *what)
{
    size_t index;
    size_t sampled = 0u;

    for (index = 0u; index < robo_paint_display_list_count(list); index++) {
        robo_paint_display_command_t command;
        int32_t x;
        int32_t y;

        ROBO_CHECK_EQ_INT(robo_paint_display_list_at(list, index, &command), ROBO_OK);
        if (!robo_paint_display_command_kind_draws(command.kind)) {
            continue;
        }
        if (command.rect.width <= 0 || command.rect.height <= 0) {
            continue;
        }
        x = command.rect.x + command.rect.width / 2;
        y = command.rect.y + command.rect.height / 2;
        if (x < 0 || y < 0 || x >= (int32_t)robo_gfx_surface_width(surface) ||
            y >= (int32_t)robo_gfx_surface_height(surface)) {
            continue;
        }
        clip_pixel_is(surface, x, y, clip_expected_at(list, x, y), what);
        sampled++;
    }
    ROBO_CHECK(sampled > 0u);
}

/* ------------------------------------------------------------------ */
/* 1. The arithmetic                                                   */
/* ------------------------------------------------------------------ */

static void test_clip_intersection(void)
{
    robo_paint_clip_t outer;
    robo_paint_clip_t inner;
    robo_paint_clip_t out;
    robo_paint_clip_t none;

    none = clip_none_value();

    /* Two bounded records: the smaller on each axis wins. */
    outer = clip_make(1, 0, 100, 1, 0, 100);
    inner = clip_make(1, 20, 200, 1, -10, 50);
    ROBO_CHECK_EQ_INT(robo_paint_clip_intersect_clip(&outer, &inner, &out, NULL), ROBO_OK);
    clip_check(&out, &(robo_paint_clip_t){ { ROBO_CSS_OVERFLOW_HIDDEN, 20, 100 },
                                           { ROBO_CSS_OVERFLOW_HIDDEN, 0, 50 } },
               "two bounded records");

    /* Contained: the inner one is the answer on both axes. */
    outer = clip_make(1, 0, 100, 1, 0, 100);
    inner = clip_make(1, 10, 90, 1, 10, 90);
    ROBO_CHECK_EQ_INT(robo_paint_clip_intersect_clip(&outer, &inner, &out, NULL), ROBO_OK);
    clip_check(&out, &inner, "a contained inner record");

    /* An axis the inner record leaves **open** is carried, not cleared: this is the rule that
     * makes `overflow-x: hidden` beside `overflow-y: visible` composable at all. */
    outer = clip_make(1, 5, 60, 0, 0, 0);
    inner = clip_make(0, 0, 0, 1, 7, 30);
    ROBO_CHECK_EQ_INT(robo_paint_clip_intersect_clip(&outer, &inner, &out, NULL), ROBO_OK);
    clip_check(&out, &(robo_paint_clip_t){ { ROBO_CSS_OVERFLOW_HIDDEN, 5, 60 },
                                           { ROBO_CSS_OVERFLOW_HIDDEN, 7, 30 } },
               "one bounded axis each");

    /* Neither bounded: the answer is the cleared record, and nothing is emptied. */
    ROBO_CHECK_EQ_INT(robo_paint_clip_intersect_clip(&none, &none, &out, NULL), ROBO_OK);
    clip_check(&out, &none, "two unbounded records");
    ROBO_CHECK_EQ_INT(robo_paint_clip_is_empty(&out), 0);

    /* Disjoint: empty, and *valid* -- an excluded box is a statement, not a failure. The interval
     * is canonicalized to the zero-length one at the **upper start** (the larger of the two
     * starts, 50, which is the near edge of the boundary that did the excluding) instead of being
     * left running backwards from 50 to 10, because a record whose interval runs backwards is not
     * a record this build produced and every consumer downstream would have to check for it. */
    outer = clip_make(1, 0, 10, 1, 0, 10);
    inner = clip_make(1, 50, 60, 1, 50, 60);
    ROBO_CHECK_EQ_INT(robo_paint_clip_intersect_clip(&outer, &inner, &out, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_paint_clip_is_valid(&out), 1);
    ROBO_CHECK_EQ_INT(robo_paint_clip_is_empty(&out), 1);
    clip_check(&out, &(robo_paint_clip_t){ { ROBO_CSS_OVERFLOW_HIDDEN, 50, 50 },
                                           { ROBO_CSS_OVERFLOW_HIDDEN, 50, 50 } },
               "two disjoint records");

    /* The canonical empty interval is a fixed point: intersecting it with anything, on either
     * side, leaves it empty and at the same place rather than reviving it. */
    outer = clip_make(1, 50, 50, 1, 50, 50);
    inner = clip_make(1, 0, 100, 1, 0, 100);
    ROBO_CHECK_EQ_INT(robo_paint_clip_intersect_clip(&outer, &inner, &out, NULL), ROBO_OK);
    clip_check(&out, &outer, "an empty interval intersected with a containing one");
    ROBO_CHECK_EQ_INT(robo_paint_clip_intersect_clip(&inner, &outer, &out, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_paint_clip_is_empty(&out), 1);

    /* And the boundary door canonicalizes the same way: a box entirely under an ancestor's
     * upper bound is an empty scope, not a refused build. */
    {
        robo_layout_rect_t boundary = clip_rect(200, 200, 40, 20);

        ROBO_CHECK_EQ_INT(robo_paint_clip_intersect(&(robo_paint_clip_t){
                                                        { ROBO_CSS_OVERFLOW_HIDDEN, 0, 100 },
                                                        { ROBO_CSS_OVERFLOW_VISIBLE, 0, 0 } },
                                                    &boundary, ROBO_CSS_OVERFLOW_HIDDEN,
                                                    ROBO_CSS_OVERFLOW_VISIBLE, &out, NULL),
                          ROBO_OK);
        ROBO_CHECK_EQ_INT(robo_paint_clip_is_valid(&out), 1);
        ROBO_CHECK_EQ_INT(robo_paint_clip_is_empty(&out), 1);
        clip_check(&out, &(robo_paint_clip_t){ { ROBO_CSS_OVERFLOW_HIDDEN, 200, 200 },
                                               { ROBO_CSS_OVERFLOW_VISIBLE, 0, 0 } },
                   "a boundary above the interval in force");
    }

    /* Touching edges: half-open, so `[0,10]` and `[10,20]` share no pixel. */
    outer = clip_make(1, 0, 10, 0, 0, 0);
    inner = clip_make(1, 10, 20, 0, 0, 0);
    ROBO_CHECK_EQ_INT(robo_paint_clip_intersect_clip(&outer, &inner, &out, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_paint_clip_axis_is_empty(&out.horizontal), 1);
    ROBO_CHECK_EQ_INT(robo_paint_clip_is_empty(&out), 1);

    /* Zero-size: an interval with no width is empty, and stays empty through a further
     * intersection rather than becoming non-empty. */
    outer = clip_make(1, 30, 30, 1, 0, 10);
    inner = clip_make(1, 0, 100, 1, 0, 100);
    ROBO_CHECK_EQ_INT(robo_paint_clip_intersect_clip(&outer, &inner, &out, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_paint_clip_is_empty(&out), 1);
    ROBO_CHECK_EQ_INT(robo_paint_clip_intersect_clip(&out, &inner, &out, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_paint_clip_is_empty(&out), 1);

    /* Negative coordinates: an interval entirely below zero is legal, and so is a clip that
     * runs across the origin. */
    outer = clip_make(1, -50, -10, 1, -80, -20);
    inner = clip_make(1, -100, 0, 1, -100, 0);
    ROBO_CHECK_EQ_INT(robo_paint_clip_intersect_clip(&outer, &inner, &out, NULL), ROBO_OK);
    clip_check(&out, &outer, "a negative interval inside a wider negative one");
    ROBO_CHECK_EQ_INT(robo_paint_clip_is_empty(&out), 0);
}

static void test_clip_intersection_safety(void)
{
    robo_paint_clip_t clip;
    robo_paint_clip_t out;
    robo_paint_diag_t diag;

    clip = clip_make(1, 0, 100, 1, 0, 100);
    out = clip_make(1, 1, 2, 1, 3, 4);
    ROBO_CHECK_EQ_INT(robo_paint_clip_intersect_clip(NULL, &clip, &out, NULL),
                      ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_paint_clip_intersect_clip(&clip, NULL, &out, NULL),
                      ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_paint_clip_intersect_clip(&clip, &clip, NULL, NULL),
                      ROBO_INVALID_ARGUMENT);

    /* A mode neither record may carry, on either side. */
    {
        robo_paint_clip_t bad = clip_make(1, 0, 100, 1, 0, 100);

        bad.vertical.mode = (robo_css_overflow_t)99;
        robo_paint_diag_reset(&diag);
        ROBO_CHECK_EQ_INT(robo_paint_clip_intersect_clip(&clip, &bad, &out, &diag), ROBO_ERROR);
        ROBO_CHECK_EQ_INT((int)diag.domain, (int)ROBO_PAINT_DOMAIN_CLIP);
        ROBO_CHECK_EQ_INT(robo_paint_clip_intersect_clip(&bad, &clip, &out, &diag), ROBO_ERROR);
    }

    /* An interval that runs backwards is not a record, so no result is invented from it. */
    {
        robo_paint_clip_t bad = clip_make(1, 0, 100, 1, 0, 100);

        bad.horizontal.start = 50;
        bad.horizontal.end = 10;
        ROBO_CHECK_EQ_INT(robo_paint_clip_is_valid(&bad), 0);
        ROBO_CHECK_EQ_INT(robo_paint_clip_intersect_clip(&clip, &bad, &out, NULL), ROBO_ERROR);
    }

    /* The boundary door's own safety, one layer down: the mode, the boundary rectangle and the
     * record are all checked before the arithmetic runs. */
    {
        robo_layout_rect_t boundary = clip_rect(0, 0, 10, 10);
        robo_layout_rect_t negative = clip_rect(0, 0, -10, 10);

        ROBO_CHECK_EQ_INT(robo_paint_clip_intersect(&clip, &boundary,
                                                    (robo_css_overflow_t)99,
                                                    ROBO_CSS_OVERFLOW_VISIBLE, &out, NULL),
                          ROBO_INVALID_ARGUMENT);
        ROBO_CHECK_EQ_INT(robo_paint_clip_intersect(&clip, &negative, ROBO_CSS_OVERFLOW_HIDDEN,
                                                    ROBO_CSS_OVERFLOW_HIDDEN, &out, NULL),
                          ROBO_INVALID_ARGUMENT);
        ROBO_CHECK_EQ_INT(robo_paint_clip_intersect(NULL, &boundary, ROBO_CSS_OVERFLOW_HIDDEN,
                                                    ROBO_CSS_OVERFLOW_HIDDEN, &out, NULL),
                          ROBO_INVALID_ARGUMENT);
    }
}

static void test_clip_empty_predicate(void)
{
    robo_paint_clip_t clip;
    robo_layout_rect_t rect;

    /* No boundary is *not* empty: it is unbounded, which is the opposite answer. */
    clip = clip_none_value();
    ROBO_CHECK_EQ_INT(robo_paint_clip_is_empty(&clip), 0);
    ROBO_CHECK_EQ_INT(robo_paint_clip_is_active(&clip), 0);

    /* A bounded axis with a zero interval is empty, and either axis is enough. */
    clip = clip_make(1, 10, 10, 0, 0, 0);
    ROBO_CHECK_EQ_INT(robo_paint_clip_is_empty(&clip), 1);
    clip = clip_make(1, 0, 100, 1, 40, 40);
    ROBO_CHECK_EQ_INT(robo_paint_clip_is_empty(&clip), 1);
    clip = clip_make(1, 0, 100, 1, 0, 100);
    ROBO_CHECK_EQ_INT(robo_paint_clip_is_empty(&clip), 0);

    /* NULL is "nothing is bounding anything", which is not the same as "nothing can be
     * drawn" -- and the predicate says so rather than guessing. */
    ROBO_CHECK_EQ_INT(robo_paint_clip_is_empty(NULL), 0);

    /* The rectangle door answers only when it can: both axes bounded. A one-axis record is a
     * legal document, so the refusal is the point. */
    clip = clip_make(1, 10, 30, 1, 40, 80);
    ROBO_CHECK_EQ_INT(robo_paint_clip_rect(&clip, &rect), ROBO_OK);
    paint_rect_is(10, 40, 20, 40, rect, "the rectangle of a two-axis clip");
    clip = clip_make(1, 10, 30, 0, 0, 0);
    ROBO_CHECK_EQ_INT(robo_paint_clip_rect(&clip, &rect), ROBO_NOT_FOUND);
    clip = clip_none_value();
    ROBO_CHECK_EQ_INT(robo_paint_clip_rect(&clip, &rect), ROBO_NOT_FOUND);
    ROBO_CHECK_EQ_INT(robo_paint_clip_rect(NULL, &rect), ROBO_INVALID_ARGUMENT);
}

/* ------------------------------------------------------------------ */
/* 2. The stack                                                        */
/* ------------------------------------------------------------------ */

static void test_clip_stack(void)
{
    robo_paint_clip_stack_t *stack = NULL;
    robo_paint_clip_t clip;
    robo_paint_clip_t read_back;
    robo_paint_diag_t diag;
    size_t baseline = paint_live_blocks();
    size_t index;

    ROBO_CHECK_EQ_INT(robo_paint_clip_stack_create(&stack, NULL), ROBO_OK);
    ROBO_CHECK(stack != NULL);
    ROBO_CHECK_EQ_INT((int)robo_paint_clip_stack_depth(stack), 0);
    ROBO_CHECK_EQ_INT((int)robo_paint_clip_stack_capacity(stack), 0);
    ROBO_CHECK_EQ_INT(robo_paint_clip_stack_is_empty(stack), 1);
    ROBO_CHECK_EQ_INT(robo_paint_clip_stack_is_empty(NULL), 1);

    /* Nothing in force reads as the cleared record rather than as a miss. */
    clip = clip_make(1, 1, 2, 1, 3, 4);
    robo_paint_clip_stack_current(stack, &read_back);
    clip_check(&read_back, &(robo_paint_clip_t){ { ROBO_CSS_OVERFLOW_VISIBLE, 0, 0 },
                                                 { ROBO_CSS_OVERFLOW_VISIBLE, 0, 0 } },
               "an empty stack's clip");
    robo_paint_clip_stack_current(NULL, &read_back);
    ROBO_CHECK_EQ_INT(robo_paint_clip_is_active(&read_back), 0);

    /* A push becomes the clip in force, and the stack takes a *copy*. */
    clip = clip_make(1, 0, 100, 1, 0, 50);
    ROBO_CHECK_EQ_INT(robo_paint_clip_stack_push(stack, &clip, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT((int)robo_paint_clip_stack_depth(stack), 1);
    ROBO_CHECK_EQ_INT(robo_paint_clip_stack_is_empty(stack), 0);
    clip.horizontal.start = 999;
    robo_paint_clip_stack_current(stack, &read_back);
    clip_check(&read_back, &(robo_paint_clip_t){ { ROBO_CSS_OVERFLOW_HIDDEN, 0, 100 },
                                                 { ROBO_CSS_OVERFLOW_HIDDEN, 0, 50 } },
               "the pushed record, after the caller's copy changed");

    /* Growth is unbounded above the first block, which is what makes a deeply nested document
     * a document rather than a refusal. */
    clip = clip_make(1, 0, 10, 1, 0, 10);
    for (index = 0u; index < 64u; index++) {
        ROBO_CHECK_EQ_INT(robo_paint_clip_stack_push(stack, &clip, NULL), ROBO_OK);
    }
    ROBO_CHECK_EQ_INT((int)robo_paint_clip_stack_depth(stack), 65);
    ROBO_CHECK(robo_paint_clip_stack_capacity(stack) >= 65u);

    robo_paint_clip_stack_current(stack, &read_back);
    clip_check(&read_back, &clip, "the innermost of 65 records");

    /* Refusals: nothing to pop, and a record that is not one. */
    ROBO_CHECK_EQ_INT(robo_paint_clip_stack_push(stack, NULL, NULL), ROBO_INVALID_ARGUMENT);
    robo_paint_diag_reset(&diag);
    ROBO_CHECK_EQ_INT(robo_paint_clip_stack_push(stack, &(robo_paint_clip_t){
                                                          { (robo_css_overflow_t)99, 0, 0 },
                                                          { ROBO_CSS_OVERFLOW_VISIBLE, 0, 0 } },
                                                 &diag),
                      ROBO_ERROR);
    ROBO_CHECK_EQ_INT((int)diag.domain, (int)ROBO_PAINT_DOMAIN_CLIP);
    ROBO_CHECK_EQ_INT((int)robo_paint_clip_stack_depth(stack), 65);

    /* Pop back to empty, then one more -- which is the one malformed operation a stack can be
     * handed, and it is refused without disturbing anything. */
    for (index = 0u; index < 65u; index++) {
        ROBO_CHECK_EQ_INT(robo_paint_clip_stack_pop(stack, NULL), ROBO_OK);
    }
    ROBO_CHECK_EQ_INT(robo_paint_clip_stack_is_empty(stack), 1);
    ROBO_CHECK_EQ_INT(robo_paint_clip_stack_pop(stack, NULL), ROBO_NOT_FOUND);
    ROBO_CHECK_EQ_INT(robo_paint_clip_stack_pop(NULL, NULL), ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_paint_clip_stack_depth(NULL), 0);
    ROBO_CHECK_EQ_INT((int)robo_paint_clip_stack_capacity(NULL), 0);

    /* Reset empties it and keeps the capacity; destroy releases it. */
    ROBO_CHECK_EQ_INT(robo_paint_clip_stack_push(stack, &clip, NULL), ROBO_OK);
    robo_paint_clip_stack_reset(stack);
    ROBO_CHECK_EQ_INT(robo_paint_clip_stack_is_empty(stack), 1);
    ROBO_CHECK(robo_paint_clip_stack_capacity(stack) >= 65u);
    robo_paint_clip_stack_reset(NULL);

    robo_paint_clip_stack_destroy(stack);
    robo_paint_clip_stack_destroy(NULL);
    ROBO_CHECK_EQ_INT((int)paint_live_blocks(), (int)baseline);
}

static void test_clip_stack_nested(void)
{
    robo_paint_clip_stack_t *stack = NULL;
    robo_paint_clip_t outer = clip_make(1, 0, 200, 1, 0, 100);
    robo_paint_clip_t middle = clip_make(1, 20, 180, 1, 10, 90);
    robo_paint_clip_t inner = clip_make(1, 40, 160, 1, 20, 80);
    robo_paint_clip_t read_back;
    robo_paint_diag_t diag;

    ROBO_CHECK_EQ_INT(robo_paint_clip_stack_create(&stack, NULL), ROBO_OK);

    robo_paint_diag_reset(&diag);
    ROBO_CHECK_EQ_INT(robo_paint_clip_stack_push(stack, &outer, &diag), ROBO_OK);
    robo_paint_clip_stack_current(stack, &read_back);
    clip_check(&read_back, &outer, "one scope open");
    ROBO_CHECK_EQ_INT(robo_paint_clip_stack_push(stack, &middle, &diag), ROBO_OK);
    robo_paint_clip_stack_current(stack, &read_back);
    clip_check(&read_back, &middle, "two scopes open");
    ROBO_CHECK_EQ_INT(robo_paint_clip_stack_push(stack, &inner, &diag), ROBO_OK);
    robo_paint_clip_stack_current(stack, &read_back);
    clip_check(&read_back, &inner, "three scopes open");
    ROBO_CHECK_EQ_INT((int)robo_paint_clip_stack_depth(stack), 3);

    /* Each pop restores **the one outside it**, not "nothing". */
    ROBO_CHECK_EQ_INT(robo_paint_clip_stack_pop(stack, &diag), ROBO_OK);
    robo_paint_clip_stack_current(stack, &read_back);
    clip_check(&read_back, &middle, "after closing the innermost scope");
    ROBO_CHECK_EQ_INT(robo_paint_clip_stack_pop(stack, &diag), ROBO_OK);
    robo_paint_clip_stack_current(stack, &read_back);
    clip_check(&read_back, &outer, "after closing the middle scope");
    ROBO_CHECK_EQ_INT(robo_paint_clip_stack_pop(stack, &diag), ROBO_OK);
    robo_paint_clip_stack_current(stack, &read_back);
    ROBO_CHECK_EQ_INT(robo_paint_clip_is_active(&read_back), 0);

    /* An empty clip is a legal member of the stack: it makes everything inside it undrawable
     * and it does not poison the scope outside it. The interval is the canonical empty form,
     * `[end, end]` -- a backwards interval is not a record (`robo_paint_clip_is_valid`) and the
     * intersection that produces one canonicalizes it, so this is the shape an excluded box
     * actually arrives in. */
    ROBO_CHECK_EQ_INT(robo_paint_clip_stack_push(stack, &outer, &diag), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_paint_clip_stack_push(stack, &(robo_paint_clip_t){
                                                          { ROBO_CSS_OVERFLOW_HIDDEN, 200, 200 },
                                                          { ROBO_CSS_OVERFLOW_VISIBLE, 0, 0 } },
                                                 &diag),
                      ROBO_OK);
    robo_paint_clip_stack_current(stack, &read_back);
    ROBO_CHECK_EQ_INT(robo_paint_clip_is_empty(&read_back), 1);
    ROBO_CHECK_EQ_INT(robo_paint_clip_is_valid(&read_back), 1);
    ROBO_CHECK_EQ_INT(robo_paint_clip_stack_pop(stack, &diag), ROBO_OK);
    robo_paint_clip_stack_current(stack, &read_back);
    clip_check(&read_back, &outer, "the enclosing scope, after an empty one");

    robo_paint_clip_stack_destroy(stack);
}

/* ------------------------------------------------------------------ */
/* 3. The scopes the traversal emits                                   */
/* ------------------------------------------------------------------ */

static void test_clip_scopes_emitted(void)
{
    paint_fixture_doc_t fixture;
    paint_result_t result;
    robo_layout_node_id_t outer;
    robo_layout_node_id_t inner;
    robo_paint_display_list_t *list = NULL;
    robo_paint_display_list_stats_t stats;
    robo_paint_display_command_t push;
    robo_paint_display_command_t pop;
    robo_paint_display_command_t outer_background;
    robo_paint_display_command_t inner_background;
    robo_paint_operation_t push_record;
    robo_paint_operation_t pop_record;
    size_t push_index;
    size_t pop_index;
    robo_layout_rect_t pushed_rect;
    robo_gfx_surface_t *surface = NULL;

    paint_begin_laid_out(&fixture,
                         "<html><body><div class=\"outer\"><div class=\"inner\"></div>"
                         "</div></body></html>",
                         CLIP_BASE_CSS
                         " .outer { width: 200px; height: 100px; overflow: hidden;"
                         " background-color: #0000ff }"
                         " .inner { width: 400px; height: 80px; background-color: #ff0000 }");
    outer = paint_box(&fixture, "outer");
    inner = paint_box(&fixture, "inner");
    paint_once(&fixture, &result);

    /* The records, first: one push and one pop, and nothing for the inline-level walk. */
    ROBO_CHECK_EQ_INT((int)robo_paint_list_count_of_kind(result.list, ROBO_PAINT_OP_PUSH_CLIP), 1);
    ROBO_CHECK_EQ_INT((int)robo_paint_list_count_of_kind(result.list, ROBO_PAINT_OP_POP_CLIP), 1);
    ROBO_CHECK_EQ_INT((int)result.stats.clip_scopes, 1);
    ROBO_CHECK_EQ_INT((int)result.stats.clipped, 1);
    ROBO_CHECK_EQ_INT(paint_nth_of_kind(&result, ROBO_PAINT_OP_PUSH_CLIP, 0u, &push_record), 1);
    ROBO_CHECK_EQ_INT(paint_nth_of_kind(&result, ROBO_PAINT_OP_POP_CLIP, 0u, &pop_record), 1);
    ROBO_CHECK_EQ_INT((int)push_record.node, (int)outer);
    ROBO_CHECK_EQ_INT((int)pop_record.node, (int)outer);
    ROBO_CHECK_EQ_INT((int)push_record.depth, (int)pop_record.depth);
    ROBO_CHECK_EQ_INT((int)robo_paint_clip_is_active(&push_record.clip), 1);
    ROBO_CHECK_EQ_INT((int)robo_paint_clip_equal(&push_record.clip, &pop_record.clip), 1);

    clip_build(&result, &list, &stats);
    ROBO_CHECK_EQ_INT((int)stats.clip_scopes, 1);
    ROBO_CHECK_EQ_INT((int)clip_count_of_kind(list, ROBO_PAINT_DISPLAY_CMD_PUSH_CLIP), 1);
    ROBO_CHECK_EQ_INT((int)clip_count_of_kind(list, ROBO_PAINT_DISPLAY_CMD_POP_CLIP), 1);

    ROBO_CHECK_EQ_INT(clip_command_of_kind(list, ROBO_PAINT_DISPLAY_CMD_PUSH_CLIP, 0u, &push), 1);
    ROBO_CHECK_EQ_INT(clip_command_of_kind(list, ROBO_PAINT_DISPLAY_CMD_POP_CLIP, 0u, &pop), 1);
    ROBO_CHECK_EQ_INT((int)push.node, (int)outer);
    ROBO_CHECK_EQ_INT((int)pop.node, (int)outer);
    clip_check(&pop.clip, &push.clip, "the pop carries the push's boundary");

    /* The scope is the padding box: nothing else bounds this box, so the effective clip *is*
     * its padding box -- and the command's rectangle names the boundary that produced it. */
    ROBO_CHECK_EQ_INT(robo_paint_clip_rect(&push.clip, &pushed_rect), ROBO_OK);
    paint_rect_eq(push.rect, pushed_rect, "the push's rectangle is the boundary");

    /* Order: after the box's own content boundary, before its contents; the pop after the last
     * child and before the box's end marker. */
    push_index = clip_index_of_kind_for_node(list, ROBO_PAINT_DISPLAY_CMD_PUSH_CLIP, outer, 0u);
    pop_index = clip_index_of_kind_for_node(list, ROBO_PAINT_DISPLAY_CMD_POP_CLIP, outer, 0u);
    ROBO_CHECK(push_index != (size_t)-1 && pop_index != (size_t)-1);
    ROBO_CHECK(push_index > clip_index_of_kind_for_node(list, ROBO_PAINT_DISPLAY_CMD_CONTENT,
                                                        outer, 0u));
    ROBO_CHECK(push_index < clip_index_of_kind_for_node(list, ROBO_PAINT_DISPLAY_CMD_BEGIN_NODE,
                                                        inner, 0u));
    ROBO_CHECK(pop_index > clip_index_of_kind_for_node(list, ROBO_PAINT_DISPLAY_CMD_END_NODE,
                                                       inner, 0u));
    ROBO_CHECK(pop_index < clip_index_of_kind_for_node(list, ROBO_PAINT_DISPLAY_CMD_END_NODE,
                                                       outer, 0u));

    /* The box's own decoration is **outside** its own scope, and the child's is inside:
     * everything between the push and the pop carries the boundary. */
    ROBO_CHECK_EQ_INT(1, clip_command_of_kind(list, ROBO_PAINT_DISPLAY_CMD_DRAW_BACKGROUND, 0u,
                                              &outer_background));
    ROBO_CHECK_EQ_INT((int)outer_background.node, (int)outer);
    ROBO_CHECK_EQ_INT(robo_paint_clip_is_active(&outer_background.clip), 0);
    ROBO_CHECK_EQ_INT(1, clip_command_of_kind(list, ROBO_PAINT_DISPLAY_CMD_DRAW_BACKGROUND, 1u,
                                              &inner_background));
    ROBO_CHECK_EQ_INT((int)inner_background.node, (int)inner);
    clip_check(&inner_background.clip, &push.clip, "the child's background is bounded");

    /* And the pixels follow: inside the boundary red, outside untouched. */
    surface = clip_surface(800u, 600u, ROBO_GFX_SURFACE_ACCESS_READ_WRITE);
    clip_paint(list, surface, NULL, NULL);
    clip_pixel_is(surface, pushed_rect.x + 10, pushed_rect.y + 10, CLIP_RED,
                  "inside the clip scope");
    clip_pixel_is(surface, pushed_rect.x + 250, pushed_rect.y + 10, CLIP_UNTOUCHED,
                  "beyond the clip scope, where the child's box extends");
    clip_pixels_match(list, surface, "the clipping document");

    robo_gfx_surface_release(surface);
    robo_paint_display_list_destroy(list);
    paint_result_dispose(&result);
    paint_end(&fixture);
}

static void test_clip_scope_only_when_changed(void)
{
    paint_fixture_doc_t fixture;
    paint_result_t result;
    robo_paint_display_list_t *list = NULL;
    robo_paint_display_list_stats_t stats;

    /*
     * Two hidden boxes and **one** scope. `.outer` bounds its contents; `.mid` is larger than
     * its ancestor in both axes and offset out of it, so its own `overflow: hidden` boundary --
     * intersected with what is already in force -- changes nothing. The traversal does not push
     * a command that establishes nothing, and `clipped - clip_scopes` is the count of scopes it
     * declined: 2 - 1 = 1.
     */
    paint_begin_laid_out(&fixture,
                         "<html><body><div class=\"outer\"><div class=\"mid\">"
                         "<div class=\"leaf\"></div></div></div></body></html>",
                         CLIP_BASE_CSS
                         " .outer { width: 60px; height: 40px; overflow: hidden }"
                         " .mid { position: relative; left: -20px; top: -10px; width: 400px;"
                         " height: 200px; overflow: hidden }"
                         " .leaf { width: 10px; height: 10px; background-color: #00ff00 }");
    paint_once(&fixture, &result);

    ROBO_CHECK_EQ_INT((int)result.stats.clipped, 2);
    ROBO_CHECK_EQ_INT((int)result.stats.clip_scopes, 1);
    clip_build(&result, &list, &stats);
    ROBO_CHECK_EQ_INT((int)stats.clip_scopes, 1);
    ROBO_CHECK_EQ_INT((int)clip_count_of_kind(list, ROBO_PAINT_DISPLAY_CMD_PUSH_CLIP), 1);

    robo_paint_display_list_destroy(list);
    paint_result_dispose(&result);
    paint_end(&fixture);
}

static void test_clip_nested_effective(void)
{
    paint_fixture_doc_t fixture;
    paint_result_t result;
    robo_layout_node_id_t outer;
    robo_layout_node_id_t mid;
    robo_paint_display_list_t *list = NULL;
    robo_paint_display_list_stats_t stats;
    robo_paint_display_command_t outer_push;
    robo_paint_display_command_t mid_push;
    robo_paint_display_command_t leaf_background;
    robo_paint_display_command_t outer_content;
    robo_paint_boxes_t local;
    robo_paint_boxes_t page;
    robo_paint_clip_t expected;
    robo_paint_decoration_stats_t paint_stats;
    robo_gfx_surface_t *surface = NULL;

    paint_begin_laid_out(&fixture,
                         "<html><body><div class=\"outer\"><div class=\"mid\">"
                         "<div class=\"leaf\"></div></div></div></body></html>",
                         CLIP_BASE_CSS
                         " .outer { width: 200px; height: 100px; overflow: hidden }"
                         " .mid { position: relative; left: 20px; top: 10px; width: 300px;"
                         " height: 300px; overflow: hidden }"
                         " .leaf { width: 20px; height: 20px; background-color: #00ff00 }");
    outer = paint_box(&fixture, "outer");
    mid = paint_box(&fixture, "mid");
    paint_once(&fixture, &result);
    ROBO_CHECK_EQ_INT((int)result.stats.clip_scopes, 2);

    clip_build(&result, &list, &stats);
    ROBO_CHECK_EQ_INT((int)stats.clip_scopes, 2);
    ROBO_CHECK_EQ_INT(1, clip_index_of_kind_for_node(list, ROBO_PAINT_DISPLAY_CMD_PUSH_CLIP, outer,
                                                     0u) != (size_t)-1);
    ROBO_CHECK_EQ_INT(1, clip_index_of_kind_for_node(list, ROBO_PAINT_DISPLAY_CMD_PUSH_CLIP, mid,
                                                     0u) != (size_t)-1);
    ROBO_CHECK_EQ_INT(clip_command_of_kind(list, ROBO_PAINT_DISPLAY_CMD_PUSH_CLIP, 0u, &outer_push),
                      1);
    ROBO_CHECK_EQ_INT(clip_command_of_kind(list, ROBO_PAINT_DISPLAY_CMD_PUSH_CLIP, 1u, &mid_push),
                      1);
    ROBO_CHECK_EQ_INT(clip_command_of_kind(list, ROBO_PAINT_DISPLAY_CMD_CONTENT, 0u,
                                           &outer_content),
                      1);
    ROBO_CHECK_EQ_INT(clip_command_of_kind(list, ROBO_PAINT_DISPLAY_CMD_DRAW_BACKGROUND, 0u,
                                           &leaf_background),
                      1);

    /*
     * The expected clip inside the nested scope, **recomputed from the tree**: the enclosing
     * scope's boundary, intersected with `.mid`'s own padding box in page space. The page origin
     * for `.mid` is its parent's content origin, which the list itself states -- the box's own
     * `content` command's rectangle -- so the recomputation uses the same two inputs the
     * traversal did and none of its arithmetic.
     */
    ROBO_CHECK_EQ_INT(robo_paint_boxes_of(fixture.tree, mid, &local, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_paint_boxes_translate(&local, outer_content.rect.x,
                                                 outer_content.rect.y, &page, NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_paint_clip_intersect(&outer_push.clip, &page.padding,
                                                ROBO_CSS_OVERFLOW_HIDDEN,
                                                ROBO_CSS_OVERFLOW_HIDDEN, &expected, NULL),
                      ROBO_OK);
    clip_check(&mid_push.clip, &expected, "the nested scope's effective clip");
    clip_check(&leaf_background.clip, &expected, "the leaf's own drawing");

    /*
     * And it really is a **narrowing**, on the axis where this document can show it: `.mid` is
     * 300x300 inside a 200x100 boundary, so every one of its own edges lies outside the enclosing
     * clip and both of its *starts* are its own while both *ends* are the enclosing boundary's.
     * The clips are therefore strictly narrower at the origin and exactly equal at the end --
     * which is the per-axis `max`/`min` stated as pixels of geometry rather than as a formula.
     */
    ROBO_CHECK(mid_push.clip.horizontal.start > outer_push.clip.horizontal.start);
    ROBO_CHECK_EQ_INT(mid_push.clip.horizontal.end, outer_push.clip.horizontal.end);
    ROBO_CHECK(mid_push.clip.vertical.start > outer_push.clip.vertical.start);
    ROBO_CHECK_EQ_INT(mid_push.clip.vertical.end, outer_push.clip.vertical.end);

    /* The pass rests two scopes deep inside `.mid`, and one scope's worth of pixels is inside
     * the nested boundary while the leaf's box extends beyond it. */
    surface = clip_surface(800u, 600u, ROBO_GFX_SURFACE_ACCESS_READ_WRITE);
    clip_paint(list, surface, NULL, &paint_stats);
    ROBO_CHECK_EQ_INT((int)paint_stats.clip_scopes, 2);
    ROBO_CHECK_EQ_INT((int)paint_stats.clip_depth_max, 2);
    clip_pixel_is(surface, expected.horizontal.start + 5, expected.vertical.start + 5,
                  CLIP_GREEN, "the leaf inside both scopes");
    clip_pixel_is(surface, outer_push.clip.horizontal.end + 5, expected.vertical.start + 5,
                  CLIP_UNTOUCHED, "the leaf beyond the enclosing scope");
    clip_pixels_match(list, surface, "the nested clipping document");

    robo_gfx_surface_release(surface);
    robo_paint_display_list_destroy(list);
    paint_result_dispose(&result);
    paint_end(&fixture);
}

static void test_clip_axis_split(void)
{
    paint_fixture_doc_t fixture;
    paint_result_t result;
    robo_paint_display_list_t *list = NULL;
    robo_paint_display_list_stats_t stats;
    robo_paint_display_command_t push;
    robo_paint_display_command_t spill;
    robo_layout_rect_t both_axes;
    robo_gfx_surface_t *surface = NULL;

    paint_begin_laid_out(&fixture,
                         "<html><body><div class=\"clip\"><div class=\"spill\"></div></div>"
                         "</body></html>",
                         CLIP_BASE_CSS
                         " .clip { width: 60px; height: 30px; overflow-x: hidden;"
                         " overflow-y: visible }"
                         " .spill { width: 120px; height: 200px; background-color: #ff0000 }");
    paint_once(&fixture, &result);
    clip_build(&result, &list, &stats);

    ROBO_CHECK_EQ_INT(clip_command_of_kind(list, ROBO_PAINT_DISPLAY_CMD_PUSH_CLIP, 0u, &push), 1);
    ROBO_CHECK_EQ_INT(clip_command_of_kind(list, ROBO_PAINT_DISPLAY_CMD_DRAW_BACKGROUND, 0u,
                                           &spill),
                      1);
    clip_check(&spill.clip, &push.clip, "the one bounded axis reaches the child");
    ROBO_CHECK_EQ_INT(robo_paint_clip_axis_is_active(&push.clip.horizontal), 1);
    ROBO_CHECK_EQ_INT(robo_paint_clip_axis_is_active(&push.clip.vertical), 0);

    /* The refusal the one-rectangle door makes, and why it has to: this document has no
     * rectangle to report, and answering one would clip an axis the author left open. */
    ROBO_CHECK_EQ_INT(robo_paint_clip_rect(&push.clip, &both_axes), ROBO_NOT_FOUND);

    /* The pixels say the same thing: bounded horizontally, free vertically. */
    surface = clip_surface(800u, 600u, ROBO_GFX_SURFACE_ACCESS_READ_WRITE);
    clip_paint(list, surface, NULL, NULL);
    clip_pixel_is(surface, push.rect.x + 40, push.rect.y + 10, CLIP_RED,
                  "inside the bounded axis");
    clip_pixel_is(surface, push.rect.x + 80, push.rect.y + 10, CLIP_UNTOUCHED,
                  "beyond the bounded axis");
    clip_pixel_is(surface, push.rect.x + 40, push.rect.y + 120, CLIP_RED,
                  "below the box, on the axis the author left open");
    clip_pixels_match(list, surface, "the one-axis document");

    robo_gfx_surface_release(surface);
    robo_paint_display_list_destroy(list);
    paint_result_dispose(&result);
    paint_end(&fixture);
}

/* ------------------------------------------------------------------ */
/* 4. One drawing kind at a time                                       */
/* ------------------------------------------------------------------ */

static void test_clip_background(void)
{
    paint_fixture_doc_t fixture;
    paint_result_t result;
    robo_paint_display_list_t *list = NULL;
    robo_paint_display_list_stats_t stats;
    robo_paint_display_command_t push;
    robo_paint_display_command_t child;
    robo_gfx_surface_t *surface = NULL;

    paint_begin_laid_out(&fixture,
                         "<html><body><div class=\"parent\"><div class=\"child\"></div></div>"
                         "</body></html>",
                         CLIP_BASE_CSS
                         " .parent { width: 100px; height: 50px; overflow: hidden }"
                         " .child { position: relative; left: 60px; top: 10px; width: 100px;"
                         " height: 20px; background-color: #ff0000 }");
    paint_once(&fixture, &result);
    clip_build(&result, &list, &stats);
    ROBO_CHECK_EQ_INT(clip_command_of_kind(list, ROBO_PAINT_DISPLAY_CMD_PUSH_CLIP, 0u, &push), 1);
    ROBO_CHECK_EQ_INT(clip_command_of_kind(list, ROBO_PAINT_DISPLAY_CMD_DRAW_BACKGROUND, 0u,
                                           &child),
                      1);
    clip_check(&child.clip, &push.clip, "the child's background is bounded by the scope");

    surface = clip_surface(800u, 600u, ROBO_GFX_SURFACE_ACCESS_READ_WRITE);
    clip_paint(list, surface, NULL, NULL);
    /* The child starts 60px in and the boundary is 100px wide, so only its first 40px are
     * painted -- and the rest of its own box leaves the surface untouched. */
    clip_pixel_is(surface, push.rect.x + 70, push.rect.y + 15, CLIP_RED,
                  "the background inside the boundary");
    clip_pixel_is(surface, push.rect.x + 130, push.rect.y + 15, CLIP_UNTOUCHED,
                  "the background beyond the boundary");
    clip_pixels_match(list, surface, "the clipped background");

    robo_gfx_surface_release(surface);
    robo_paint_display_list_destroy(list);
    paint_result_dispose(&result);
    paint_end(&fixture);
}

static void test_clip_border(void)
{
    paint_fixture_doc_t fixture;
    paint_result_t result;
    robo_paint_display_list_t *list = NULL;
    robo_paint_display_list_stats_t stats;
    robo_paint_display_command_t push;
    robo_paint_display_command_t band;
    size_t index;
    size_t found = 0u;
    robo_gfx_surface_t *surface = NULL;

    paint_begin_laid_out(&fixture,
                         "<html><body><div class=\"parent\"><div class=\"child\"></div></div>"
                         "</body></html>",
                         CLIP_BASE_CSS
                         " .parent { width: 100px; height: 50px; overflow: hidden }"
                         " .child { position: relative; left: 60px; top: 10px; width: 100px;"
                         " height: 20px; border: 8px solid #ff0000 }");
    paint_once(&fixture, &result);
    clip_build(&result, &list, &stats);
    ROBO_CHECK_EQ_INT(clip_command_of_kind(list, ROBO_PAINT_DISPLAY_CMD_PUSH_CLIP, 0u, &push), 1);

    /* Every band of the child is bounded, and at least the left one straddles the boundary. */
    for (index = 0u; index < robo_paint_display_list_count(list); index++) {
        robo_paint_display_command_t command;

        ROBO_CHECK_EQ_INT(robo_paint_display_list_at(list, index, &command), ROBO_OK);
        if (command.kind != ROBO_PAINT_DISPLAY_CMD_DRAW_BORDER) {
            continue;
        }
        clip_check(&command.clip, &push.clip, "a border band is bounded by the scope");
        found++;
    }
    ROBO_CHECK_EQ_INT((int)found, 4);

    surface = clip_surface(800u, 600u, ROBO_GFX_SURFACE_ACCESS_READ_WRITE);
    clip_paint(list, surface, NULL, NULL);
    /* The child's border box starts at 60px and its left band is 8px wide, so the band is
     * inside; the right band, at 160px, is entirely outside the boundary. */
    ROBO_CHECK_EQ_INT(clip_command_of_kind(list, ROBO_PAINT_DISPLAY_CMD_DRAW_BORDER, 0u, &band), 1);
    clip_pixel_is(surface, band.rect.x + 2, band.rect.y + band.rect.height / 2, CLIP_RED,
                  "the band inside the boundary");
    clip_pixels_match(list, surface, "the clipped border");

    robo_gfx_surface_release(surface);
    robo_paint_display_list_destroy(list);
    paint_result_dispose(&result);
    paint_end(&fixture);
}

static void test_clip_text(void)
{
    paint_fixture_doc_t fixture;
    paint_result_t result;
    robo_paint_display_list_t *list = NULL;
    robo_paint_display_list_stats_t stats;
    robo_paint_display_command_t push;
    robo_paint_display_command_t text;
    robo_paint_decoration_stats_t paint_stats;
    clip_face_t face;
    robo_gfx_surface_t *surface = NULL;
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
    paint_begin_laid_out(&fixture,
                         "<html><body><div class=\"clip\"><div class=\"txt\">A</div></div>"
                         "</body></html>",
                         CLIP_BASE_CSS
                         " .clip { width: 60px; height: 40px; overflow: hidden }"
                         " .txt { position: relative; left: 55px; font-size: 100px;"
                         " color: #ff0000 }");
    paint_once(&fixture, &result);
    clip_build(&result, &list, &stats);
    ROBO_CHECK_EQ_INT(clip_command_of_kind(list, ROBO_PAINT_DISPLAY_CMD_PUSH_CLIP, 0u, &push), 1);
    ROBO_CHECK_EQ_INT(clip_command_of_kind(list, ROBO_PAINT_DISPLAY_CMD_DRAW_TEXT, 0u, &text), 1);
    clip_check(&text.clip, &push.clip, "the run is bounded by the scope");

    clip_face_create(&face);
    surface = clip_surface(800u, 600u, ROBO_GFX_SURFACE_ACCESS_READ_WRITE);
    clip_paint(list, surface, face.face, &paint_stats);
    ROBO_CHECK_EQ_INT((int)paint_stats.glyphs, 1);
    ROBO_CHECK_EQ_INT((int)paint_stats.clip_scopes, 1);

    boundary = push.clip.horizontal.end;
    /* The glyph's own origin is the run rectangle's left edge, and the run is 55px into the
     * box, so its ink starts five pixels before the boundary -- where it must be drawn. */
    ROBO_CHECK_EQ_INT((int)text.rect.x, (int)(boundary - 5));
    ROBO_CHECK_EQ_INT(clip_any_ink(surface, boundary - 5, text.rect.y, boundary,
                                   text.rect.y + 12),
                      1);
    /* And the pixels on the far side of the boundary are untouched, even though the glyph's
     * outline is larger than the run's measured advance. */
    ROBO_CHECK_EQ_INT(clip_any_ink(surface, boundary, text.rect.y, boundary + 24,
                                   text.rect.y + 12),
                      0);

    clip_face_destroy(&face);
    robo_gfx_surface_release(surface);
    robo_paint_display_list_destroy(list);
    paint_result_dispose(&result);
    paint_end(&fixture);
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
 * (see the no-clip claim in `test_clip_pixels`). The caller reads the push out of the list it
 * gets back, which is also the only way to see that there is none.
 */
static void clip_visual_document(const char *overflow,
                                 robo_gfx_surface_t **out_surface,
                                 robo_paint_display_list_t **out_list,
                                 paint_fixture_doc_t *fixture,
                                 paint_result_t *result)
{
    char sheet[512];

    memset(sheet, 0, sizeof(sheet));
    snprintf(sheet, sizeof(sheet),
             CLIP_BASE_CSS
             " .outer { width: 200px; height: 100px; overflow: %s; background-color: #0000ff }"
             " .inner { width: 400px; height: 80px; background-color: #ff0000 }",
             overflow);

    paint_begin_laid_out(fixture,
                         "<html><body><div class=\"outer\"><div class=\"inner\"></div></div>"
                         "</body></html>",
                         sheet);
    paint_once(fixture, result);
    clip_build(result, out_list, NULL);
    *out_surface = clip_surface(800u, 600u, ROBO_GFX_SURFACE_ACCESS_READ_WRITE);
    clip_paint(*out_list, *out_surface, NULL, NULL);
}

static void test_clip_pixels(void)
{
    paint_fixture_doc_t hidden_fixture;
    paint_fixture_doc_t visible_fixture;
    paint_result_t hidden_result;
    paint_result_t visible_result;
    robo_paint_display_list_t *hidden_list = NULL;
    robo_paint_display_list_t *visible_list = NULL;
    robo_paint_display_command_t hidden_push;
    robo_paint_display_command_t visible_push;
    robo_gfx_surface_t *hidden = NULL;
    robo_gfx_surface_t *visible = NULL;
    robo_gfx_encode_options_t encode_opts;
    robo_gfx_encoded_buffer_t encoded;
    int32_t x_inside;
    int32_t x_outside;
    int32_t y;
    static const uint8_t png_signature[8] = { 0x89u, 'P', 'N', 'G', 0x0du, 0x0au, 0x1au, 0x0au };

    clip_visual_document("hidden", &hidden, &hidden_list, &hidden_fixture, &hidden_result);
    clip_visual_document("visible", &visible, &visible_list, &visible_fixture, &visible_result);

    ROBO_CHECK_EQ_INT(clip_command_of_kind(hidden_list, ROBO_PAINT_DISPLAY_CMD_PUSH_CLIP, 0u,
                                           &hidden_push),
                      1);
    ROBO_CHECK_EQ_INT(robo_paint_clip_is_active(&hidden_push.clip), 1);

    /*
     * The **no-clip case**, and it is the stronger form of the claim: the visible document does
     * not carry a boundary that happens to be inactive, it carries **no scope at all**. Its outer
     * box declares `overflow: visible`, whose boundary is the one already in force, so the
     * traversal opens nothing -- the two commands a consumer would only have to skip are never
     * emitted. Both documents are otherwise identical, so what is left of the pair is exactly the
     * clipping. */
    ROBO_CHECK_EQ_INT(clip_command_of_kind(visible_list, ROBO_PAINT_DISPLAY_CMD_PUSH_CLIP, 0u,
                                           &visible_push),
                      0);
    ROBO_CHECK_EQ_INT(clip_command_of_kind(visible_list, ROBO_PAINT_DISPLAY_CMD_POP_CLIP, 0u,
                                           &visible_push),
                      0);
    ROBO_CHECK_EQ_INT((int)robo_paint_display_list_count(visible_list) + 2,
                      (int)robo_paint_display_list_count(hidden_list));

    x_inside = hidden_push.rect.x + 100;
    x_outside = hidden_push.rect.x + 250;
    y = hidden_push.rect.y + 10;

    /* Inside the box both documents agree: red, from the child's own background. */
    clip_pixel_is(hidden, x_inside, y, CLIP_RED, "inside the box, hidden overflow");
    clip_pixel_is(visible, x_inside, y, CLIP_RED, "inside the box, visible overflow");

    /* Beyond the boundary -- and still inside the *child's* own box, which is twice as wide --
     * only the visible one paints. */
    clip_pixel_is(hidden, x_outside, y, CLIP_UNTOUCHED,
                  "the spill beyond the boundary, hidden overflow");
    clip_pixel_is(visible, x_outside, y, CLIP_RED,
                  "the spill beyond the boundary, visible overflow");

    /* The declarations around the boundary, exactly: the last clipped pixel is painted and the
     * first pixel outside it is not. */
    clip_pixel_is(hidden, hidden_push.rect.x + hidden_push.rect.width - 1, y, CLIP_RED,
                  "the boundary's last pixel");
    clip_pixel_is(hidden, hidden_push.rect.x + hidden_push.rect.width, y, CLIP_UNTOUCHED,
                  "the boundary's first pixel outside");

    /* The surface is a real image: RoboGraphics' own encoder turns it into a PNG, whose
     * signature and dimensions are checked here. */
    memset(&encoded, 0, sizeof(encoded));
    robo_gfx_encode_options_init(&encode_opts);
    ROBO_CHECK_EQ_INT(robo_gfx_surface_encode(hidden, &encode_opts, &encoded), ROBO_OK);
    ROBO_CHECK(encoded.size > 8u);
    ROBO_CHECK_EQ_INT(memcmp(encoded.data, png_signature, sizeof(png_signature)), 0);
    ROBO_CHECK_EQ_INT((int)((uint32_t)encoded.data[16] << 24 | (uint32_t)encoded.data[17] << 16 |
                            (uint32_t)encoded.data[18] << 8 | (uint32_t)encoded.data[19]),
                      800);
    robo_gfx_encoded_buffer_release(&encoded);

    clip_pixels_match(hidden_list, hidden, "the clipped document");
    clip_pixels_match(visible_list, visible, "the unclipped document");

    robo_gfx_surface_release(hidden);
    robo_gfx_surface_release(visible);
    robo_paint_display_list_destroy(hidden_list);
    robo_paint_display_list_destroy(visible_list);
    paint_result_dispose(&hidden_result);
    paint_result_dispose(&visible_result);
    paint_end(&hidden_fixture);
    paint_end(&visible_fixture);
}

/* ------------------------------------------------------------------ */
/* 6. Empty scopes, no scopes                                          */
/* ------------------------------------------------------------------ */

static void test_clip_empty_scope(void)
{
    paint_fixture_doc_t fixture;
    paint_result_t result;
    robo_paint_display_list_t *list = NULL;
    robo_paint_display_list_stats_t stats;
    robo_paint_display_command_t push;
    robo_paint_display_command_t inside;
    robo_paint_display_command_t outside;
    robo_paint_decoration_stats_t paint_stats;
    robo_gfx_surface_t *surface = NULL;

    /*
     * `.empty` is placed entirely outside `.frame`'s 40x20 boundary -- a 10px square 200px to the
     * right -- so the scope it opens for its contents has an **empty** horizontal interval.
     * `.after` follows it in flow and must paint: the empty scope draws nothing and the enclosing
     * boundary is restored by its pop.
     */
    paint_begin_laid_out(&fixture,
                         "<html><body><div class=\"frame\">"
                         "<div class=\"empty\"></div><div class=\"after\"></div>"
                         "</div></body></html>",
                         CLIP_BASE_CSS
                         " .frame { width: 40px; height: 20px; overflow: hidden }"
                         " .empty { position: relative; left: 200px; width: 10px; height: 10px;"
                         " overflow: hidden }"
                         " .after { left: 0px; width: 30px; height: 8px;"
                         " background-color: #00ff00 }");
    paint_once(&fixture, &result);
    clip_build(&result, &list, &stats);
    ROBO_CHECK_EQ_INT((int)stats.clip_scopes, 2);

    ROBO_CHECK_EQ_INT(clip_command_of_kind(list, ROBO_PAINT_DISPLAY_CMD_PUSH_CLIP, 0u, &push), 1);
    ROBO_CHECK_EQ_INT(clip_command_of_kind(list, ROBO_PAINT_DISPLAY_CMD_PUSH_CLIP, 1u, &inside), 1);
    /*
     * The excluded box's scope: an **empty** interval, and empty in the canonical shape rather
     * than as a backwards one. The intersection's start is the larger of the two (the box's own
     * left edge, 200px right of the frame's origin) and its end would be the smaller (the frame's
     * right edge, 40px), which runs backwards -- so it is canonicalized to `[200, 200]`: a valid
     * record that bounds nothing. Both numbers are derived from geometry here, not written as
     * absolutes: the box's edge from the push's own rectangle plus the relative offset, and the
     * equality of the two ends from the canonical rule. */
    clip_check_int(inside.clip.horizontal.start, push.rect.x + 200,
                   "the excluded box's left edge");
    clip_check_int(inside.clip.horizontal.end, inside.clip.horizontal.start,
                   "an empty interval ends where it starts");
    clip_check_int(inside.clip.horizontal.start > push.rect.x + push.rect.width, 1,
                   "the empty interval at the box's edge, past the frame's own");
    ROBO_CHECK_EQ_INT(robo_paint_clip_is_empty(&inside.clip), 1);
    ROBO_CHECK_EQ_INT(robo_paint_clip_is_valid(&inside.clip), 1);

    surface = clip_surface(800u, 600u, ROBO_GFX_SURFACE_ACCESS_READ_WRITE);
    clip_paint(list, surface, NULL, &paint_stats);
    ROBO_CHECK_EQ_INT((int)paint_stats.clip_scopes, 2);
    ROBO_CHECK_EQ_INT((int)paint_stats.clip_depth_max, 2);

    /* Nothing of the excluded descendant is drawn, and the sibling after it is -- which is the
     * whole claim: an empty scope suppresses drawing and does not poison the walk. */
    ROBO_CHECK_EQ_INT(clip_any_ink(surface, 0, 0, 800, 600) != 0, 1);
    ROBO_CHECK_EQ_INT(clip_count_of_kind(list, ROBO_PAINT_DISPLAY_CMD_DRAW_BACKGROUND), 1);
    ROBO_CHECK_EQ_INT(clip_command_of_kind(list, ROBO_PAINT_DISPLAY_CMD_DRAW_BACKGROUND, 0u,
                                           &outside),
                      1);
    clip_pixel_is(surface, outside.rect.x + outside.rect.width / 2,
                  outside.rect.y + outside.rect.height / 2, CLIP_GREEN,
                  "the sibling after the empty scope");

    robo_gfx_surface_release(surface);
    robo_paint_display_list_destroy(list);
    paint_result_dispose(&result);
    paint_end(&fixture);
}

static void test_clip_no_clip(void)
{
    paint_fixture_doc_t fixture;
    paint_result_t result;
    robo_paint_display_list_t *list = NULL;
    robo_paint_display_list_stats_t stats;
    robo_paint_decoration_stats_t paint_stats;
    robo_gfx_surface_t *surface = NULL;

    /* A document that declares no overflow: no scope, no clip command, no boundary in force --
     * and every drawing still painted exactly where it was before this phase, which is the
     * "no-clip path is unchanged" claim stated as a pixel. */
    paint_begin_laid_out(&fixture,
                         "<html><body><div class=\"a\"></div><div class=\"b\"></div></body></html>",
                         CLIP_BASE_CSS
                         " .a { width: 100px; height: 20px; background-color: #ff0000 }"
                         " .b { width: 50px; height: 10px; border: 4px solid #0000ff }");
    paint_once(&fixture, &result);
    ROBO_CHECK_EQ_INT((int)result.stats.clip_scopes, 0);
    ROBO_CHECK_EQ_INT((int)result.stats.clipped, 0);
    ROBO_CHECK_EQ_INT((int)paint_count_of(&result, ROBO_PAINT_OP_PUSH_CLIP), 0);

    clip_build(&result, &list, &stats);
    ROBO_CHECK_EQ_INT((int)stats.clip_scopes, 0);
    ROBO_CHECK_EQ_INT((int)clip_count_of_kind(list, ROBO_PAINT_DISPLAY_CMD_PUSH_CLIP), 0);
    ROBO_CHECK_EQ_INT((int)clip_count_of_kind(list, ROBO_PAINT_DISPLAY_CMD_POP_CLIP), 0);

    surface = clip_surface(800u, 600u, ROBO_GFX_SURFACE_ACCESS_READ_WRITE);
    clip_paint(list, surface, NULL, &paint_stats);
    ROBO_CHECK_EQ_INT((int)paint_stats.clip_scopes, 0);
    ROBO_CHECK_EQ_INT((int)paint_stats.clip_depth_max, 0);
    ROBO_CHECK_EQ_INT((int)paint_stats.clipped, 0);
    clip_pixels_match(list, surface, "a document that clips nothing");

    robo_gfx_surface_release(surface);
    robo_paint_display_list_destroy(list);
    paint_result_dispose(&result);
    paint_end(&fixture);
}

static void test_clip_empty_and_malformed(void)
{
    paint_fixture_doc_t fixture;
    paint_result_t result;
    robo_paint_display_list_t *list = NULL;
    robo_paint_display_list_stats_t stats;

    /* A childless hidden box opens a scope and draws nothing: the scope is a statement about
     * the box, not about its subtree, exactly as its content boundary is. */
    paint_begin_laid_out(&fixture,
                         "<html><body><div class=\"solo\"></div></body></html>",
                         CLIP_BASE_CSS " .solo { overflow: hidden; width: 10px; height: 0px }");
    paint_once(&fixture, &result);
    ROBO_CHECK_EQ_INT((int)result.stats.clip_scopes, 1);
    clip_build(&result, &list, &stats);
    ROBO_CHECK_EQ_INT((int)stats.clip_scopes, 1);
    ROBO_CHECK_EQ_INT((int)clip_count_of_kind(list, ROBO_PAINT_DISPLAY_CMD_PUSH_CLIP), 1);
    ROBO_CHECK_EQ_INT((int)clip_count_of_kind(list, ROBO_PAINT_DISPLAY_CMD_POP_CLIP), 1);
    robo_paint_display_list_destroy(list);
    list = NULL;
    paint_result_dispose(&result);
    paint_end(&fixture);

    /* An empty document: a document with no boxes has no scopes, and the build is still a
     * successful build of an empty list. */
    paint_begin_laid_out(&fixture, "<html><body></body></html>", CLIP_BASE_CSS);
    paint_once(&fixture, &result);
    clip_build(&result, &list, &stats);
    ROBO_CHECK_EQ_INT((int)stats.clip_scopes, 0);
    ROBO_CHECK_EQ_INT((int)clip_count_of_kind(list, ROBO_PAINT_DISPLAY_CMD_PUSH_CLIP), 0);
    robo_paint_display_list_destroy(list);
    paint_result_dispose(&result);
    paint_end(&fixture);
}

/* ------------------------------------------------------------------ */
/* 7. The pass's own contract                                          */
/* ------------------------------------------------------------------ */

static void test_clip_pass_refuses_unbalanced(void)
{
    robo_paint_display_list_t *list = NULL;
    robo_gfx_surface_t *surface = NULL;
    robo_paint_diag_t diag;
    robo_result_t status;

    ROBO_CHECK_EQ_INT(robo_paint_display_list_create(&list, NULL), ROBO_OK);
    /* A pop with nothing to pop. The list's own validity rule cannot see it -- each command is
     * a record -- so this is the pass's contract, and it refuses rather than skipping. */
    clip_append_command(list, ROBO_PAINT_DISPLAY_CMD_POP_CLIP, 1u, 0u, clip_rect(0, 0, 10, 10),
                        clip_make(1, 0, 10, 1, 0, 10), robo_css_color_none());
    surface = clip_surface(64u, 64u, ROBO_GFX_SURFACE_ACCESS_READ_WRITE);
    robo_paint_diag_reset(&diag);
    status = robo_paint_decoration_paint(list, surface, NULL, NULL, &diag);
    ROBO_CHECK_EQ_INT((int)status, (int)ROBO_NOT_FOUND);
    ROBO_CHECK_EQ_INT((int)diag.domain, (int)ROBO_PAINT_DOMAIN_CLIP);

    robo_gfx_surface_release(surface);
    robo_paint_display_list_destroy(list);
}

static void test_clip_hand_built_scope(void)
{
    robo_paint_display_list_t *list = NULL;
    robo_gfx_surface_t *surface = NULL;
    robo_paint_decoration_stats_t stats;
    robo_paint_clip_t empty;

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
    ROBO_CHECK_EQ_INT(robo_paint_display_list_create(&list, NULL), ROBO_OK);
    empty = clip_make(1, 100, 100, 0, 0, 0);
    clip_append_command(list, ROBO_PAINT_DISPLAY_CMD_PUSH_CLIP, 1u, 0u, clip_rect(0, 0, 20, 20),
                        empty, robo_css_color_none());
    clip_append_command(list, ROBO_PAINT_DISPLAY_CMD_DRAW_BACKGROUND, 2u, 1u,
                        clip_rect(0, 0, 20, 20), clip_none_value(),
                        robo_css_color_make(255u, 0u, 0u, 255u));
    clip_append_command(list, ROBO_PAINT_DISPLAY_CMD_POP_CLIP, 1u, 0u, clip_rect(0, 0, 20, 20),
                        empty, robo_css_color_none());

    surface = clip_surface(64u, 64u, ROBO_GFX_SURFACE_ACCESS_READ_WRITE);
    clip_paint(list, surface, NULL, &stats);
    ROBO_CHECK_EQ_INT((int)stats.clip_scopes, 1);
    /* The fill is *attempted* -- `filled` counts the fills this pass executed, and an empty
     * clip is a no-op inside the raster rather than a command the pass skipped -- so the claim
     * is the pixel and `clipped`, which says the command stood under a boundary. */
    ROBO_CHECK_EQ_INT((int)stats.filled, 1);
    ROBO_CHECK_EQ_INT((int)stats.clipped, 1);
    clip_pixel_is(surface, 10, 10, CLIP_UNTOUCHED,
                  "a drawing inside an empty scope, whose own clip is unbounded");

    /* The same list with a *bounded* scope does fill -- so the case above is about the empty
     * clip and not about the pass skipping hand-built commands. */
    robo_paint_display_list_clear(list);
    clip_append_command(list, ROBO_PAINT_DISPLAY_CMD_PUSH_CLIP, 1u, 0u, clip_rect(0, 0, 20, 20),
                        clip_make(1, 0, 5, 1, 0, 5), robo_css_color_none());
    clip_append_command(list, ROBO_PAINT_DISPLAY_CMD_DRAW_BACKGROUND, 2u, 1u,
                        clip_rect(0, 0, 20, 20), clip_none_value(),
                        robo_css_color_make(255u, 0u, 0u, 255u));
    clip_append_command(list, ROBO_PAINT_DISPLAY_CMD_POP_CLIP, 1u, 0u, clip_rect(0, 0, 20, 20),
                        clip_make(1, 0, 5, 1, 0, 5), robo_css_color_none());
    robo_gfx_surface_release(surface);
    surface = clip_surface(64u, 64u, ROBO_GFX_SURFACE_ACCESS_READ_WRITE);
    clip_paint(list, surface, NULL, &stats);
    ROBO_CHECK_EQ_INT((int)stats.filled, 1);
    clip_pixel_is(surface, 2, 2, CLIP_RED, "inside the scope's own boundary");
    clip_pixel_is(surface, 10, 10, CLIP_UNTOUCHED,
                  "outside the scope's boundary, inside the command's rectangle");

    robo_gfx_surface_release(surface);
    robo_paint_display_list_destroy(list);
}

static void test_clip_counter_identities(void)
{
    paint_fixture_doc_t fixture;
    paint_result_t result;
    robo_paint_display_list_t *list = NULL;
    robo_paint_display_list_stats_t stats;
    robo_paint_decoration_stats_t paint_stats;
    robo_gfx_surface_t *surface = NULL;

    paint_begin_laid_out(&fixture,
                         "<html><body><div class=\"outer\"><div class=\"mid\">"
                         "<div class=\"leaf\"></div></div></div></body></html>",
                         CLIP_BASE_CSS
                         " .outer { width: 200px; height: 100px; overflow: hidden;"
                         " background-color: #0000ff }"
                         " .mid { position: relative; left: 20px; top: 10px; width: 300px;"
                         " height: 300px; overflow: hidden }"
                         " .leaf { width: 20px; height: 20px; background-color: #00ff00 }");
    paint_once(&fixture, &result);
    clip_build(&result, &list, &stats);

    /*
     * The identities the counters owe, stated once. A clip command is **structural**, so it is
     * counted with the commands this pass does not draw rather than with the drawings -- which is
     * what keeps `commands = draws + skipped` exact -- and `clip_scopes` says how many of those
     * skips were a boundary.
     */
    ROBO_CHECK_EQ_INT((int)stats.commands, (int)robo_paint_display_list_count(list));
    /* Three structural commands per box, two per scope, plus the drawings -- the whole list,
     * from the parts. A document with text would add its runs here, which is why this case
     * uses one without. */
    ROBO_CHECK_EQ_INT((int)stats.commands,
                      (int)(3u * (size_t)stats.nodes + 2u * stats.clip_scopes + stats.draws));
    surface = clip_surface(800u, 600u, ROBO_GFX_SURFACE_ACCESS_READ_WRITE);
    clip_paint(list, surface, NULL, &paint_stats);
    ROBO_CHECK_EQ_INT((int)paint_stats.commands, (int)robo_paint_display_list_count(list));
    ROBO_CHECK_EQ_INT((int)paint_stats.commands,
                      (int)(paint_stats.draws + paint_stats.skipped));
    ROBO_CHECK_EQ_INT((int)paint_stats.draws,
                      (int)(paint_stats.backgrounds + paint_stats.borders + paint_stats.texts));
    ROBO_CHECK_EQ_INT((int)paint_stats.draws,
                      (int)(paint_stats.filled + paint_stats.contextual + paint_stats.transparent +
                            paint_stats.empty + paint_stats.no_font));
    ROBO_CHECK_EQ_INT((int)paint_stats.clip_scopes, (int)stats.clip_scopes);
    /* Two commands per scope, not one: a `PUSH_CLIP` and the `POP_CLIP` that closes it are both
     * commands this pass does not draw, and `clip_scopes` counts the *scopes* rather than the
     * commands -- which is what lets `clipped = boundaries + scopes` hold on the traversal side
     * while this identity stays exact on the pass's. */
    ROBO_CHECK_EQ_INT((int)paint_stats.skipped,
                      (int)(3u * (size_t)stats.nodes + 2u * paint_stats.clip_scopes));

    robo_gfx_surface_release(surface);
    robo_paint_display_list_destroy(list);
    paint_result_dispose(&result);
    paint_end(&fixture);
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
static void clip_expect_refusal(void (*build)(robo_paint_list_t *list),
                                const char *what,
                                const char *expect_message_fragment)
{
    paint_result_t result;
    robo_paint_display_list_t *list = NULL;
    robo_paint_diag_t diag;
    robo_result_t status;

    memset(&result, 0, sizeof(result));
    ROBO_CHECK_EQ_INT(robo_paint_list_create(&result.list, NULL), ROBO_OK);
    build(result.list);
    status = clip_build_expect_refusal(&result, &list, &diag);
    if (status == ROBO_OK) {
        robo_test_fail(__FILE__, __LINE__, "%s: the builder accepted it", what);
    } else {
        if (expect_message_fragment && diag.message &&
            strstr(diag.message, expect_message_fragment) == NULL) {
            robo_test_fail(__FILE__, __LINE__, "%s: refused with \"%s\", not \"%s\"", what,
                           diag.message, expect_message_fragment);
        }
        ROBO_CHECK_EQ_INT((int)diag.domain, (int)ROBO_PAINT_DOMAIN_DISPLAY_LIST);
    }
    ROBO_CHECK_EQ_INT((int)robo_paint_display_list_count(list), 0);

    robo_paint_display_list_destroy(list);
    robo_paint_list_destroy(result.list);
}

/* Each builder below writes one malformed sequence around a well-formed box, so the refusal is
 * attributable to the one thing that is wrong with it. */

static void clip_bad_pop_without_push(robo_paint_list_t *list)
{
    robo_paint_boxes_t boxes = clip_boxes(0, 0, 20, 20);

    clip_append_record(list, ROBO_PAINT_OP_BEGIN_BOX, 1u, 0u, boxes, clip_none_value());
    clip_append_record(list, ROBO_PAINT_OP_CONTENT, 1u, 0u, boxes, clip_none_value());
    clip_append_record(list, ROBO_PAINT_OP_POP_CLIP, 1u, 0u, boxes, clip_make(1, 0, 5, 1, 0, 5));
    clip_append_record(list, ROBO_PAINT_OP_END_BOX, 1u, 0u, boxes, clip_none_value());
}

static void clip_bad_pop_wrong_clip(robo_paint_list_t *list)
{
    robo_paint_boxes_t boxes = clip_boxes(0, 0, 20, 20);

    clip_append_record(list, ROBO_PAINT_OP_BEGIN_BOX, 1u, 0u, boxes, clip_none_value());
    clip_append_record(list, ROBO_PAINT_OP_CONTENT, 1u, 0u, boxes, clip_none_value());
    clip_append_record(list, ROBO_PAINT_OP_PUSH_CLIP, 1u, 0u, boxes, clip_make(1, 0, 5, 1, 0, 5));
    clip_append_record(list, ROBO_PAINT_OP_POP_CLIP, 1u, 0u, boxes, clip_make(1, 0, 7, 1, 0, 5));
    clip_append_record(list, ROBO_PAINT_OP_END_BOX, 1u, 0u, boxes, clip_none_value());
}

static void clip_bad_pop_wrong_node(robo_paint_list_t *list)
{
    robo_paint_boxes_t boxes = clip_boxes(0, 0, 20, 20);

    clip_append_record(list, ROBO_PAINT_OP_BEGIN_BOX, 1u, 0u, boxes, clip_none_value());
    clip_append_record(list, ROBO_PAINT_OP_CONTENT, 1u, 0u, boxes, clip_none_value());
    clip_append_record(list, ROBO_PAINT_OP_PUSH_CLIP, 1u, 0u, boxes, clip_make(1, 0, 5, 1, 0, 5));
    clip_append_record(list, ROBO_PAINT_OP_POP_CLIP, 2u, 0u, boxes, clip_make(1, 0, 5, 1, 0, 5));
    clip_append_record(list, ROBO_PAINT_OP_END_BOX, 1u, 0u, boxes, clip_none_value());
}

static void clip_bad_push_before_content(robo_paint_list_t *list)
{
    robo_paint_boxes_t boxes = clip_boxes(0, 0, 20, 20);

    clip_append_record(list, ROBO_PAINT_OP_BEGIN_BOX, 1u, 0u, boxes, clip_none_value());
    clip_append_record(list, ROBO_PAINT_OP_PUSH_CLIP, 1u, 0u, boxes, clip_make(1, 0, 5, 1, 0, 5));
    clip_append_record(list, ROBO_PAINT_OP_CONTENT, 1u, 0u, boxes, clip_none_value());
    clip_append_record(list, ROBO_PAINT_OP_POP_CLIP, 1u, 0u, boxes, clip_make(1, 0, 5, 1, 0, 5));
    clip_append_record(list, ROBO_PAINT_OP_END_BOX, 1u, 0u, boxes, clip_none_value());
}

static void clip_bad_two_scopes(robo_paint_list_t *list)
{
    robo_paint_boxes_t boxes = clip_boxes(0, 0, 20, 20);
    robo_paint_clip_t pushed = clip_make(1, 0, 5, 1, 0, 5);

    clip_append_record(list, ROBO_PAINT_OP_BEGIN_BOX, 1u, 0u, boxes, clip_none_value());
    clip_append_record(list, ROBO_PAINT_OP_CONTENT, 1u, 0u, boxes, clip_none_value());
    clip_append_record(list, ROBO_PAINT_OP_PUSH_CLIP, 1u, 0u, boxes, pushed);
    clip_append_record(list, ROBO_PAINT_OP_PUSH_CLIP, 1u, 0u, boxes, clip_make(1, 0, 3, 1, 0, 3));
    clip_append_record(list, ROBO_PAINT_OP_POP_CLIP, 1u, 0u, boxes, pushed);
    clip_append_record(list, ROBO_PAINT_OP_END_BOX, 1u, 0u, boxes, clip_none_value());
}

static void clip_bad_push_changes_nothing(robo_paint_list_t *list)
{
    robo_paint_boxes_t boxes = clip_boxes(0, 0, 20, 20);
    robo_paint_clip_t cycle = clip_make(1, 0, 5, 1, 0, 5);

    /* Two scopes whose boundaries are equal: the second establishes nothing. */
    clip_append_record(list, ROBO_PAINT_OP_BEGIN_BOX, 1u, 0u, boxes, clip_none_value());
    clip_append_record(list, ROBO_PAINT_OP_CONTENT, 1u, 0u, boxes, clip_none_value());
    clip_append_record(list, ROBO_PAINT_OP_PUSH_CLIP, 1u, 0u, boxes, cycle);
    clip_append_record(list, ROBO_PAINT_OP_BEGIN_BOX, 2u, 1u, boxes, cycle);
    clip_append_record(list, ROBO_PAINT_OP_CONTENT, 2u, 1u, boxes, cycle);
    clip_append_record(list, ROBO_PAINT_OP_PUSH_CLIP, 2u, 1u, boxes, cycle);
    clip_append_record(list, ROBO_PAINT_OP_POP_CLIP, 2u, 1u, boxes, cycle);
    clip_append_record(list, ROBO_PAINT_OP_END_BOX, 2u, 1u, boxes, cycle);
    clip_append_record(list, ROBO_PAINT_OP_POP_CLIP, 1u, 0u, boxes, cycle);
    clip_append_record(list, ROBO_PAINT_OP_END_BOX, 1u, 0u, boxes, clip_none_value());
}

static void clip_bad_push_wider(robo_paint_list_t *list)
{
    robo_paint_boxes_t boxes = clip_boxes(0, 0, 20, 20);

    clip_append_record(list, ROBO_PAINT_OP_BEGIN_BOX, 1u, 0u, boxes, clip_none_value());
    clip_append_record(list, ROBO_PAINT_OP_CONTENT, 1u, 0u, boxes, clip_none_value());
    clip_append_record(list, ROBO_PAINT_OP_PUSH_CLIP, 1u, 0u, boxes, clip_make(1, 5, 15, 1, 0, 5));
    clip_append_record(list, ROBO_PAINT_OP_BEGIN_BOX, 2u, 1u, boxes, clip_make(1, 5, 15, 1, 0, 5));
    clip_append_record(list, ROBO_PAINT_OP_CONTENT, 2u, 1u, boxes, clip_make(1, 5, 15, 1, 0, 5));
    /* Wider on x than the boundary it is inside: CSS cannot express this. */
    clip_append_record(list, ROBO_PAINT_OP_PUSH_CLIP, 2u, 1u, boxes, clip_make(1, 0, 20, 1, 0, 5));
    clip_append_record(list, ROBO_PAINT_OP_POP_CLIP, 2u, 1u, boxes, clip_make(1, 0, 20, 1, 0, 5));
    clip_append_record(list, ROBO_PAINT_OP_END_BOX, 2u, 1u, boxes, clip_make(1, 5, 15, 1, 0, 5));
    clip_append_record(list, ROBO_PAINT_OP_POP_CLIP, 1u, 0u, boxes, clip_make(1, 5, 15, 1, 0, 5));
    clip_append_record(list, ROBO_PAINT_OP_END_BOX, 1u, 0u, boxes, clip_none_value());
}

static void clip_bad_command_disagrees(robo_paint_list_t *list)
{
    robo_paint_boxes_t boxes = clip_boxes(0, 0, 20, 20);
    robo_paint_clip_t pushed = clip_make(1, 0, 5, 1, 0, 5);

    clip_append_record(list, ROBO_PAINT_OP_BEGIN_BOX, 1u, 0u, boxes, clip_none_value());
    clip_append_record(list, ROBO_PAINT_OP_CONTENT, 1u, 0u, boxes, clip_none_value());
    clip_append_record(list, ROBO_PAINT_OP_PUSH_CLIP, 1u, 0u, boxes, pushed);
    /* A nested box that enters under a boundary the scope did not put in force. Every other fact
     * about this record is legal -- its depth, its node, its rectangle -- so the refusal can only
     * be the scope's, which is the rule this case exists for. */
    clip_append_record(list, ROBO_PAINT_OP_BEGIN_BOX, 2u, 1u, boxes, clip_make(1, 0, 7, 1, 0, 5));
    clip_append_record(list, ROBO_PAINT_OP_CONTENT, 2u, 1u, boxes, pushed);
    clip_append_record(list, ROBO_PAINT_OP_END_BOX, 2u, 1u, boxes, pushed);
    clip_append_record(list, ROBO_PAINT_OP_POP_CLIP, 1u, 0u, boxes, pushed);
    clip_append_record(list, ROBO_PAINT_OP_END_BOX, 1u, 0u, boxes, clip_none_value());
}

static void clip_bad_scope_never_closed(robo_paint_list_t *list)
{
    robo_paint_boxes_t boxes = clip_boxes(0, 0, 20, 20);

    clip_append_record(list, ROBO_PAINT_OP_BEGIN_BOX, 1u, 0u, boxes, clip_none_value());
    clip_append_record(list, ROBO_PAINT_OP_CONTENT, 1u, 0u, boxes, clip_none_value());
    clip_append_record(list, ROBO_PAINT_OP_PUSH_CLIP, 1u, 0u, boxes, clip_make(1, 0, 5, 1, 0, 5));
    clip_append_record(list, ROBO_PAINT_OP_BEGIN_BOX, 2u, 1u, boxes, clip_make(1, 0, 5, 1, 0, 5));
    clip_append_record(list, ROBO_PAINT_OP_CONTENT, 2u, 1u, boxes, clip_make(1, 0, 5, 1, 0, 5));
    clip_append_record(list, ROBO_PAINT_OP_END_BOX, 2u, 1u, boxes, clip_make(1, 0, 5, 1, 0, 5));
    clip_append_record(list, ROBO_PAINT_OP_END_BOX, 1u, 0u, boxes, clip_make(1, 0, 5, 1, 0, 5));
}

static void test_clip_builder_refusals(void)
{
    clip_expect_refusal(clip_bad_pop_without_push, "a pop with no push",
                        "closes a clip scope it did not open");
    clip_expect_refusal(clip_bad_pop_wrong_clip, "a pop with another boundary",
                        "boundary other than the one it opened");
    clip_expect_refusal(clip_bad_pop_wrong_node, "a pop naming another box",
                        "names a box other than the innermost open one");
    clip_expect_refusal(clip_bad_push_before_content, "a push before the content boundary",
                        "precedes its box's content boundary");
    clip_expect_refusal(clip_bad_two_scopes, "two scopes in one box",
                        "opens two clip scopes");
    clip_expect_refusal(clip_bad_push_changes_nothing, "a scope that changes nothing",
                        "establishes nothing new");
    clip_expect_refusal(clip_bad_push_wider, "a scope wider than its enclosing one",
                        "wider than the boundary it is inside");
    clip_expect_refusal(clip_bad_command_disagrees, "a command that disagrees with its scope",
                        "carries a different boundary");
    clip_expect_refusal(clip_bad_scope_never_closed, "a scope never closed",
                        "does not close every clip scope it opens");

    /* The record layer's own rule, from the other side: a scope that bounds nothing is not a
     * clip record, so it cannot even be appended. */
    {
        robo_paint_list_t *list = NULL;
        robo_paint_operation_t operation;
        robo_paint_diag_t diag;

        ROBO_CHECK_EQ_INT(robo_paint_list_create(&list, NULL), ROBO_OK);
        robo_paint_operation_init(&operation);
        operation.kind = ROBO_PAINT_OP_PUSH_CLIP;
        operation.node = 1u;
        operation.depth = 0u;
        operation.boxes = clip_boxes(0, 0, 20, 20);
        operation.clip = clip_none_value();
        robo_paint_diag_reset(&diag);
        ROBO_CHECK_EQ_INT(robo_paint_list_append(list, &operation, &diag), ROBO_INVALID_ARGUMENT);
        ROBO_CHECK_EQ_INT((int)robo_paint_list_count(list), 0);
        robo_paint_list_destroy(list);
    }

    /* And the same rule one stage along: a command that opens a scope with no boundary is not a
     * command. */
    {
        robo_paint_display_list_t *list = NULL;
        robo_paint_display_command_t command;
        robo_paint_diag_t diag;

        ROBO_CHECK_EQ_INT(robo_paint_display_list_create(&list, NULL), ROBO_OK);
        robo_paint_display_command_init(&command);
        command.kind = ROBO_PAINT_DISPLAY_CMD_PUSH_CLIP;
        command.node = 1u;
        command.rect = clip_rect(0, 0, 20, 20);
        command.clip = clip_none_value();
        robo_paint_diag_reset(&diag);
        ROBO_CHECK_EQ_INT(robo_paint_display_list_append(list, &command, &diag),
                          ROBO_INVALID_ARGUMENT);
        ROBO_CHECK_EQ_INT((int)robo_paint_display_list_count(list), 0);
        robo_paint_display_list_destroy(list);
    }
}

/* ------------------------------------------------------------------ */
/* 9. Determinism, snapshot, and the dump                              */
/* ------------------------------------------------------------------ */

static void clip_stats_eq(const robo_paint_display_list_stats_t *a,
                          const robo_paint_display_list_stats_t *b,
                          const char *what)
{
    if (a->commands != b->commands || a->nodes != b->nodes || a->draws != b->draws ||
        a->texts != b->texts || a->clip_scopes != b->clip_scopes ||
        a->max_depth != b->max_depth) {
        robo_test_fail(__FILE__, __LINE__,
                       "%s: commands %d/%d nodes %d/%d draws %d/%d texts %d/%d scopes %d/%d",
                       what, (int)a->commands, (int)b->commands, (int)a->nodes, (int)b->nodes,
                       (int)a->draws, (int)b->draws, (int)a->texts, (int)b->texts,
                       (int)a->clip_scopes, (int)b->clip_scopes);
    }
}

static void test_clip_determinism(void)
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
    paint_fixture_doc_t fixture;
    paint_result_t result;
    robo_paint_display_list_t *first = NULL;
    robo_paint_display_list_t *second = NULL;
    robo_paint_display_list_stats_t first_stats;
    robo_paint_display_list_stats_t second_stats;
    char first_dump[8192];
    char second_dump[8192];
    size_t first_length = 0u;
    size_t second_length = 0u;

    paint_begin_laid_out(&fixture, html, css);
    paint_once(&fixture, &result);
    clip_build(&result, &first, &first_stats);
    clip_build(&result, &second, &second_stats);

    ROBO_CHECK_EQ_INT(robo_paint_display_list_equal(first, second), 1);
    clip_stats_eq(&first_stats, &second_stats, "two builds of one record list");

    /* The same list twice, byte for byte: a list is a value and its dump is one too, so two
     * builds of one document cannot differ in a boundary, an ordering or a line. */
    memset(first_dump, 0, sizeof(first_dump));
    memset(second_dump, 0, sizeof(second_dump));
    ROBO_CHECK_EQ_INT(robo_paint_display_list_write(first, first_dump, sizeof(first_dump),
                                                    &first_length, NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_paint_display_list_write(second, second_dump, sizeof(second_dump),
                                                    &second_length, NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT((int)first_length, (int)second_length);
    ROBO_CHECK_EQ_INT(strcmp(first_dump, second_dump), 0);

    /* And a second *document* — the same source built again — produces the same list, which is
     * the property a later pass caching a document depends on. */
    {
        paint_fixture_doc_t other;
        paint_result_t other_result;
        robo_paint_display_list_t *third = NULL;
        robo_paint_display_list_stats_t third_stats;

        paint_begin_laid_out(&other, html, css);
        paint_once(&other, &other_result);
        clip_build(&other_result, &third, &third_stats);
        ROBO_CHECK_EQ_INT(robo_paint_display_list_equal(first, third), 1);
        ROBO_CHECK_EQ_INT(robo_paint_display_list_equal(first, second), 1);
        clip_stats_eq(&first_stats, &third_stats, "two documents built from one source");
        robo_paint_display_list_destroy(third);
        paint_result_dispose(&other_result);
        paint_end(&other);
    }

    robo_paint_display_list_destroy(first);
    robo_paint_display_list_destroy(second);
    paint_result_dispose(&result);
    paint_end(&fixture);
}

static void test_clip_mutation_isolation(void)
{
    paint_fixture_doc_t fixture;
    paint_result_t result;
    robo_paint_display_list_t *before = NULL;
    robo_paint_display_list_t *after = NULL;
    robo_paint_display_list_stats_t before_stats;
    robo_paint_display_list_stats_t after_stats;
    robo_paint_decoration_stats_t first;
    robo_paint_decoration_stats_t second;
    robo_paint_clip_t stored_boundary;
    robo_gfx_surface_t *one = NULL;
    robo_gfx_surface_t *two = NULL;
    size_t index;

    /*
     * `<html>` clips, and that is the point of the document: its boundary is the **viewport's**
     * padding box, so the one mutation this fixture can make -- laying the same tree out at a
     * different width -- genuinely moves a stored boundary. `.outer` clips too, and its boundary
     * is viewport-independent, which is why the moving one has to be the root's.
     */
    paint_begin_laid_out(&fixture,
                         "<html><body><div class=\"outer\"><div class=\"inner\"></div></div>"
                         "</body></html>",
                         CLIP_BASE_CSS
                         " html { overflow: hidden }"
                         " .outer { width: 200px; height: 100px; overflow: hidden }"
                         " .inner { width: 400px; height: 80px; background-color: #ff0000 }");
    paint_once(&fixture, &result);
    clip_build(&result, &before, &before_stats);

    one = clip_surface(800u, 600u, ROBO_GFX_SURFACE_ACCESS_READ_WRITE);
    clip_paint(before, one, NULL, &first);

    /* The boundary the first list was built with, read out of it while the sources are still
     * pristine. Everything after this line changes the sources; this value must not. */
    {
        robo_paint_display_command_t snapshot;

        ROBO_CHECK_EQ_INT(clip_command_of_kind(before, ROBO_PAINT_DISPLAY_CMD_PUSH_CLIP, 0u,
                                               &snapshot),
                          1);
        stored_boundary = snapshot.clip;
    }

    /*
     * Mutate both sources: clear the record list (the strongest legal change to it) and lay the
     * *same* tree out at a different viewport, which moves the root's boundary and every box's
     * rectangle with it. The list already built is a snapshot, so it holds the boundaries it was
     * built with and paints exactly as it did -- and the mutation is proved observable by
     * building a second list afterwards and asserting it differs.
     */
    robo_paint_list_clear(result.list);
    ROBO_CHECK_EQ_INT((int)robo_paint_list_count(result.list), 0);

    paint_layout(&fixture, 400, 600);
    {
        robo_paint_context_t *context = NULL;

        ROBO_CHECK_EQ_INT(robo_paint_context_create(&context, NULL), ROBO_OK);
        paint_run(&fixture, context, result.list);
        robo_paint_context_destroy(context);
    }
    clip_build(&result, &after, &after_stats);
    ROBO_CHECK_EQ_INT(robo_paint_display_list_equal(before, after), 0);

    two = clip_surface(800u, 600u, ROBO_GFX_SURFACE_ACCESS_READ_WRITE);
    clip_paint(before, two, NULL, &second);
    clip_check_int((int)first.clipped, (int)second.clipped, "a paint after the sources changed");
    {
        robo_paint_display_command_t first_push;
        robo_paint_display_command_t after_push;

        ROBO_CHECK_EQ_INT(clip_command_of_kind(before, ROBO_PAINT_DISPLAY_CMD_PUSH_CLIP, 0u,
                                               &first_push),
                          1);
        ROBO_CHECK_EQ_INT(clip_command_of_kind(after, ROBO_PAINT_DISPLAY_CMD_PUSH_CLIP, 0u,
                                               &after_push),
                          1);

        /* The **snapshot claim, both ways round**: the first list's stored boundary is the value
         * it was built with and did not follow the tree, while the rebuilt list's boundary is a
         * different one -- so the equality above is a copy and not the mutation failing to bite. */
        clip_check(&first_push.clip, &stored_boundary,
                   "the first list's boundary, after the sources changed");
        ROBO_CHECK_EQ_INT(robo_paint_clip_equal(&first_push.clip, &after_push.clip), 0);
        ROBO_CHECK_EQ_INT(after_push.clip.horizontal.end, 400);
        ROBO_CHECK_EQ_INT(first_push.clip.horizontal.end, 800);
    }

    for (index = 0u; index < robo_paint_display_list_count(before); index++) {
        robo_paint_display_command_t command;
        robo_gfx_color_rgba8_t left;
        robo_gfx_color_rgba8_t right;
        int32_t x;
        int32_t y;

        ROBO_CHECK_EQ_INT(robo_paint_display_list_at(before, index, &command), ROBO_OK);
        if (!robo_paint_display_command_kind_draws(command.kind)) {
            continue;
        }
        x = command.rect.x + command.rect.width / 2;
        y = command.rect.y + command.rect.height / 2;
        left = clip_read(one, x, y);
        right = clip_read(two, x, y);
        clip_check_int((int)left.r, (int)right.r, "a pixel after the sources changed (red)");
        clip_check_int((int)left.a, (int)right.a, "a pixel after the sources changed (alpha)");
    }

    robo_gfx_surface_release(one);
    robo_gfx_surface_release(two);
    robo_paint_display_list_destroy(before);
    robo_paint_display_list_destroy(after);
    paint_result_dispose(&result);
    paint_end(&fixture);
}

static void test_clip_dump(void)
{
    paint_fixture_doc_t fixture;
    paint_result_t result;
    robo_paint_display_list_t *list = NULL;
    char dump[8192];
    char record_dump[8192];
    size_t length = 0u;
    size_t record_length = 0u;

    paint_begin_laid_out(&fixture,
                         "<html><body><div class=\"outer\"><div class=\"inner\"></div></div>"
                         "</body></html>",
                         CLIP_BASE_CSS
                         " .outer { width: 200px; height: 100px; overflow: hidden }"
                         " .inner { width: 400px; height: 20px }");
    paint_once(&fixture, &result);
    clip_build(&result, &list, NULL);

    /* The developer's door shows the scope, the boundary it establishes and the box it belongs
     * to -- and a reader can follow the nesting by indentation alone. */
    memset(dump, 0, sizeof(dump));
    ROBO_CHECK_EQ_INT(robo_paint_display_list_write(list, dump, sizeof(dump), &length, NULL),
                      ROBO_OK);
    ROBO_CHECK(length > 0u);
    ROBO_CHECK(strstr(dump, "push-clip") != NULL);
    ROBO_CHECK(strstr(dump, "pop-clip") != NULL);
    ROBO_CHECK(strstr(dump, "hidden[") != NULL);
    ROBO_CHECK(strstr(dump, "visible/visible") != NULL);
    /* A command prints the one rectangle it acts on, and for a scope that rectangle is the
     * box's padding box -- the boundary's own box, not the effective clip's rectangle, which
     * a one-axis document would not have. */
    ROBO_CHECK(strstr(dump, "rect=") != NULL);
    /* The push comes before the pop in the dump, which is the order a reader follows. */
    ROBO_CHECK(strstr(dump, "push-clip") < strstr(dump, "pop-clip"));

    memset(record_dump, 0, sizeof(record_dump));
    ROBO_CHECK_EQ_INT(robo_paint_list_write(result.list, record_dump, sizeof(record_dump),
                                            &record_length, NULL),
                      ROBO_OK);
    ROBO_CHECK(strstr(record_dump, "push-clip") != NULL);
    ROBO_CHECK(strstr(record_dump, "pop-clip") != NULL);
    ROBO_CHECK(strstr(record_dump, "hidden[") != NULL);
    /* And a **record** names which of the three boxes it acts on -- `padding=` for a scope,
     * where the generic case would have printed the border box and told the reader the scope
     * came from a rectangle it did not come from. */
    ROBO_CHECK(strstr(record_dump, "padding=") != NULL);

    robo_paint_display_list_destroy(list);
    paint_result_dispose(&result);
    paint_end(&fixture);
}

static void test_clip_end_to_end(void)
{
    paint_fixture_doc_t fixture;
    paint_result_t result;
    robo_paint_display_list_t *list = NULL;
    robo_paint_display_list_stats_t stats;
    robo_paint_decoration_stats_t paint_stats;
    robo_gfx_surface_t *surface = NULL;
    size_t baseline;
    size_t index;
    size_t pushes = 0u;
    size_t pops = 0u;

    baseline = paint_live_blocks();
    paint_begin_laid_out(&fixture,
                         "<html><body><div class=\"frame\">"
                         "<div class=\"spill\"></div><div class=\"after\"></div></div>"
                         "</body></html>",
                         CLIP_BASE_CSS
                         " .frame { width: 80px; height: 40px; overflow: hidden;"
                         " background-color: #0000ff }"
                         " .spill { width: 200px; height: 30px; background-color: #ff0000 }"
                         " .after { width: 40px; height: 8px; background-color: #00ff00 }");
    paint_once(&fixture, &result);
    clip_build(&result, &list, &stats);

    /* Every push is matched by a pop, in order, and no command stands inside a scope that does
     * not bound it -- the whole sequence read the way a consumer reads it. */
    for (index = 0u; index < robo_paint_display_list_count(list); index++) {
        robo_paint_display_command_t command;

        ROBO_CHECK_EQ_INT(robo_paint_display_list_at(list, index, &command), ROBO_OK);
        if (command.kind == ROBO_PAINT_DISPLAY_CMD_PUSH_CLIP) {
            pushes++;
        } else if (command.kind == ROBO_PAINT_DISPLAY_CMD_POP_CLIP) {
            pops++;
        }
    }
    ROBO_CHECK_EQ_INT((int)pushes, (int)pops);
    ROBO_CHECK_EQ_INT((int)pushes, (int)stats.clip_scopes);

    surface = clip_surface(800u, 600u, ROBO_GFX_SURFACE_ACCESS_READ_WRITE);
    clip_paint(list, surface, NULL, &paint_stats);
    clip_pixels_match(list, surface, "the end-to-end document");

    robo_gfx_surface_release(surface);
    robo_paint_display_list_destroy(list);
    paint_result_dispose(&result);
    paint_end(&fixture);
    ROBO_CHECK_EQ_INT((int)paint_live_blocks(), (int)baseline);
}

/* ------------------------------------------------------------------ */
/* Registration                                                        */
/* ------------------------------------------------------------------ */

void robo_test_register_paint_clip(void)
{
    robo_test_register("paint.clip.intersection", test_clip_intersection);
    robo_test_register("paint.clip.intersection_safety", test_clip_intersection_safety);
    robo_test_register("paint.clip.empty_predicate", test_clip_empty_predicate);
    robo_test_register("paint.clip.stack", test_clip_stack);
    robo_test_register("paint.clip.stack_nested", test_clip_stack_nested);
    robo_test_register("paint.clip.scopes_emitted", test_clip_scopes_emitted);
    robo_test_register("paint.clip.scope_only_when_changed", test_clip_scope_only_when_changed);
    robo_test_register("paint.clip.nested_effective", test_clip_nested_effective);
    robo_test_register("paint.clip.axis_split", test_clip_axis_split);
    robo_test_register("paint.clip.background", test_clip_background);
    robo_test_register("paint.clip.border", test_clip_border);
    robo_test_register("paint.clip.text", test_clip_text);
    robo_test_register("paint.clip.pixels", test_clip_pixels);
    robo_test_register("paint.clip.empty_scope", test_clip_empty_scope);
    robo_test_register("paint.clip.no_clip", test_clip_no_clip);
    robo_test_register("paint.clip.empty_and_malformed", test_clip_empty_and_malformed);
    robo_test_register("paint.clip.pass_refuses_unbalanced", test_clip_pass_refuses_unbalanced);
    robo_test_register("paint.clip.hand_built_scope", test_clip_hand_built_scope);
    robo_test_register("paint.clip.counter_identities", test_clip_counter_identities);
    robo_test_register("paint.clip.builder_refusals", test_clip_builder_refusals);
    robo_test_register("paint.clip.determinism", test_clip_determinism);
    robo_test_register("paint.clip.mutation_isolation", test_clip_mutation_isolation);
    robo_test_register("paint.clip.dump", test_clip_dump);
    robo_test_register("paint.clip.end_to_end", test_clip_end_to_end);
}
