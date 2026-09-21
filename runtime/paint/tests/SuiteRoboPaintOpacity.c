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
 *                        `robo_gfx_composite_src_over_opacity` -- the graphics module's own blend
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
#define OPAC_RED robo_gfx_rgba8(255, 0, 0, 255)
#define OPAC_BLUE robo_gfx_rgba8(0, 0, 255, 255)
#define OPAC_WHITE robo_gfx_rgba8(255, 255, 255, 255)
#define OPAC_UNTOUCHED robo_gfx_rgba8(0, 0, 0, 0)

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
} opac_synth_table_t;

static void opac_put_u8(uint8_t **p, uint8_t v) { *(*p)++ = v; }
static void opac_put_u16(uint8_t **p, uint16_t v)
{
    *(*p)++ = (uint8_t)(v >> 8);
    *(*p)++ = (uint8_t)v;
}
static void opac_put_i16(uint8_t **p, int16_t v) { opac_put_u16(p, (uint16_t)v); }
static void opac_put_u32(uint8_t **p, uint32_t v)
{
    *(*p)++ = (uint8_t)(v >> 24);
    *(*p)++ = (uint8_t)(v >> 16);
    *(*p)++ = (uint8_t)(v >> 8);
    *(*p)++ = (uint8_t)v;
}
static void opac_put_i64(uint8_t **p, int64_t v)
{
    int i;

    for (i = 7; i >= 0; i--) {
        opac_put_u8(p, (uint8_t)((uint64_t)v >> (i * 8)));
    }
}

static void opac_write_search_fields(uint8_t *hdr, uint16_t num_tables)
{
    uint16_t pow2 = 1u;
    uint16_t entry = 0u;
    uint8_t *p;

    while ((uint16_t)(pow2 * 2u) <= num_tables) {
        pow2 = (uint16_t)(pow2 * 2u);
        entry++;
    }
    p = hdr + 4;
    opac_put_u16(&p, num_tables);
    opac_put_u16(&p, (uint16_t)(pow2 * 16u));
    opac_put_u16(&p, entry);
    opac_put_u16(&p, (uint16_t)(num_tables * 16u - pow2 * 16u));
}

static size_t opac_build_sfnt(uint8_t *out,
                              size_t out_cap,
                              uint32_t version,
                              const opac_synth_table_t *tables,
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
    opac_put_u32(&p, version);
    opac_write_search_fields(out, (uint16_t)n);
    cursor = dir_end;
    for (i = 0u; i < n; i++) {
        uint32_t pad = (uint32_t)(((size_t)tables[i].length + 3u) & ~(size_t)3u);
        uint8_t *entry = out + 12u + (size_t)i * 16u;

        opac_put_u32(&entry, tables[i].tag);
        opac_put_u32(&entry, 0u);
        opac_put_u32(&entry, (uint32_t)cursor);
        opac_put_u32(&entry, tables[i].length);
        memcpy(out + cursor, tables[i].data, tables[i].length);
        cursor += pad;
    }
    return cursor;
}

static size_t opac_build_head(uint8_t *b, uint16_t upem)
{
    uint8_t *p = b;

    opac_put_u16(&p, 1);
    opac_put_u16(&p, 0);
    opac_put_u32(&p, 0x00010000u);
    opac_put_u32(&p, 0u);
    opac_put_u32(&p, ROBO_FONT_HEAD_MAGIC);
    opac_put_u16(&p, 0u);
    opac_put_u16(&p, upem);
    opac_put_i64(&p, 0);
    opac_put_i64(&p, 0);
    opac_put_i16(&p, -50);
    opac_put_i16(&p, -200);
    opac_put_i16(&p, 1000);
    opac_put_i16(&p, 800);
    opac_put_u16(&p, 0u);
    opac_put_u16(&p, 8u);
    opac_put_i16(&p, 2);
    opac_put_i16(&p, 1); /* long loca, so loca entries are u32 */
    opac_put_i16(&p, 0);
    return (size_t)(p - b);
}

static size_t opac_build_hhea(uint8_t *b, uint16_t n_h_metrics)
{
    uint8_t *p = b;
    int i;

    opac_put_u16(&p, 1);
    opac_put_u16(&p, 0);
    opac_put_i16(&p, 800);  /* ascender */
    opac_put_i16(&p, -200); /* descender */
    opac_put_i16(&p, 90);   /* line gap */
    opac_put_u16(&p, 900);
    opac_put_i16(&p, -10);
    opac_put_i16(&p, -20);
    opac_put_i16(&p, 880);
    opac_put_i16(&p, 1);
    opac_put_i16(&p, 0);
    opac_put_i16(&p, 0);
    for (i = 0; i < 4; i++) {
        opac_put_i16(&p, 0);
    }
    opac_put_i16(&p, 0);
    opac_put_u16(&p, n_h_metrics);
    return (size_t)(p - b);
}

static size_t opac_build_maxp(uint8_t *b, uint16_t num_glyphs)
{
    uint8_t *p = b;
    int i;

    opac_put_u32(&p, 0x00010000u);
    opac_put_u16(&p, num_glyphs);
    opac_put_u16(&p, 8);
    opac_put_u16(&p, 2);
    for (i = 0; i < 12; i++) {
        opac_put_u16(&p, 0);
    }
    return (size_t)(p - b);
}

static size_t opac_build_hmtx(uint8_t *b, const uint16_t *advances, uint16_t n)
{
    uint8_t *p = b;
    uint16_t i;

    for (i = 0u; i < n; i++) {
        opac_put_u16(&p, advances[i]);
        opac_put_i16(&p, (int16_t)(i + 1));
    }
    return (size_t)(p - b);
}

/** `n + 1` long `loca` entries from @p offsets. */
static size_t opac_build_loca(uint8_t *loca, const uint32_t *offsets, uint16_t n)
{
    uint8_t *p = loca;
    uint16_t i;

    for (i = 0u; i <= n; i++) {
        opac_put_u32(&p, offsets[i]);
    }
    return (size_t)(p - loca);
}

/** One glyph: a square from (0, 0) to (100, 100) in font units. */
static size_t opac_build_square_glyf(uint8_t *b)
{
    uint8_t *p = b;

    opac_put_i16(&p, 1);                      /* numberOfContours */
    opac_put_i16(&p, 0);                      /* xMin */
    opac_put_i16(&p, 0);                      /* yMin */
    opac_put_i16(&p, OPAC_FACE_SQUARE_UNITS); /* xMax */
    opac_put_i16(&p, OPAC_FACE_SQUARE_UNITS); /* yMax */
    opac_put_u16(&p, 3);                      /* endPtsOfContours[0] */
    opac_put_u16(&p, 0);                      /* instructionLength */
    opac_put_u8(&p, (uint8_t)(ROBO_FONT_GLYF_FLAG_ON_CURVE |
                              ROBO_FONT_GLYF_FLAG_X_SAME_OR_POSITIVE |
                              ROBO_FONT_GLYF_FLAG_Y_SAME_OR_POSITIVE));
    opac_put_u8(&p, (uint8_t)(ROBO_FONT_GLYF_FLAG_ON_CURVE | ROBO_FONT_GLYF_FLAG_X_SHORT |
                              ROBO_FONT_GLYF_FLAG_X_SAME_OR_POSITIVE |
                              ROBO_FONT_GLYF_FLAG_Y_SAME_OR_POSITIVE));
    opac_put_u8(&p, (uint8_t)(ROBO_FONT_GLYF_FLAG_ON_CURVE | ROBO_FONT_GLYF_FLAG_Y_SHORT |
                              ROBO_FONT_GLYF_FLAG_X_SAME_OR_POSITIVE |
                              ROBO_FONT_GLYF_FLAG_Y_SAME_OR_POSITIVE));
    opac_put_u8(&p, (uint8_t)(ROBO_FONT_GLYF_FLAG_ON_CURVE | ROBO_FONT_GLYF_FLAG_X_SHORT |
                              ROBO_FONT_GLYF_FLAG_Y_SAME_OR_POSITIVE));
    opac_put_u8(&p, OPAC_FACE_SQUARE_UNITS);
    opac_put_u8(&p, OPAC_FACE_SQUARE_UNITS);
    opac_put_u8(&p, OPAC_FACE_SQUARE_UNITS);
    return (size_t)(p - b);
}

/**
 * A one-subtable cmap holding a format-4 segment table mapping @p first contiguously to glyph 1
 * (delta `1 - first`), with the required `0xFFFF` segment appended.
 */
static size_t opac_build_cmap(uint8_t *b, uint16_t first, uint16_t last)
{
    uint8_t *p = b;
    uint8_t *length_ptr;
    uint8_t *subtable = b + 12;
    uint16_t length;

    opac_put_u16(&p, 0);   /* version */
    opac_put_u16(&p, 1);   /* numTables */
    opac_put_u16(&p, 3);   /* Windows */
    opac_put_u16(&p, 1);   /* Unicode BMP */
    opac_put_u32(&p, 12u); /* subtable offset */

    opac_put_u16(&p, 4); /* format */
    length_ptr = p;
    opac_put_u16(&p, 0);    /* length, patched below */
    opac_put_u16(&p, 0);    /* language */
    opac_put_u16(&p, 4);    /* segCountX2 */
    opac_put_u16(&p, 4);    /* searchRange */
    opac_put_u16(&p, 1);    /* entrySelector */
    opac_put_u16(&p, 0);    /* rangeShift */
    opac_put_u16(&p, last);
    opac_put_u16(&p, 0xFFFFu);
    opac_put_u16(&p, 0); /* reservedPad */
    opac_put_u16(&p, first);
    opac_put_u16(&p, 0xFFFFu);
    opac_put_i16(&p, (int16_t)(1 - (int32_t)first)); /* idDelta[0]: 'A' -> glyph 1 */
    opac_put_i16(&p, 1);                             /* idDelta[1] */
    opac_put_u16(&p, 0);                             /* idRangeOffset[0] */
    opac_put_u16(&p, 0);                             /* idRangeOffset[1] */

    length = (uint16_t)(p - subtable);
    length_ptr[0] = (uint8_t)(length >> 8);
    length_ptr[1] = (uint8_t)length;
    return (size_t)(p - b);
}

/** What a face plus its source cost a caller, so a case can release both the way it built them. */
typedef struct opac_face {
    robo_font_source_t *source;
    robo_font_face_t *face;
} opac_face_t;

/** Build the synthetic face, with 'A' -> glyph 1 and glyph 0 (`.notdef`) empty. */
static void opac_face_create(opac_face_t *out)
{
    uint8_t head[64], hhea[64], maxp[64], hmtx[64], loca[64], glyf[128], cmap[64];
    uint8_t font[1024];
    static const uint16_t advances[OPAC_FACE_NUM_GLYPHS] = {
        (uint16_t)OPAC_FACE_ADVANCE_NOTDEF,
        (uint16_t)OPAC_FACE_ADVANCE_GLYPH
    };
    opac_synth_table_t tables[7];
    uint32_t nt = 0u;
    size_t glyph_size;
    size_t n;

    out->source = NULL;
    out->face = NULL;

    glyph_size = opac_build_square_glyf(glyf);
    {
        const uint32_t offsets[OPAC_FACE_NUM_GLYPHS + 1u] = { 0u, 0u, (uint32_t)glyph_size };

        (void)opac_build_loca(loca, offsets, (uint16_t)OPAC_FACE_NUM_GLYPHS);
    }

    tables[nt].tag = ROBO_FONT_TAG_HEAD;
    tables[nt].data = head;
    tables[nt].length = (uint32_t)opac_build_head(head, (uint16_t)OPAC_FACE_UPEM);
    nt++;
    tables[nt].tag = ROBO_FONT_TAG_HHEA;
    tables[nt].data = hhea;
    tables[nt].length = (uint32_t)opac_build_hhea(hhea, (uint16_t)OPAC_FACE_NUM_GLYPHS);
    nt++;
    tables[nt].tag = ROBO_FONT_TAG_MAXP;
    tables[nt].data = maxp;
    tables[nt].length = (uint32_t)opac_build_maxp(maxp, (uint16_t)OPAC_FACE_NUM_GLYPHS);
    nt++;
    tables[nt].tag = ROBO_FONT_TAG_HMTX;
    tables[nt].data = hmtx;
    tables[nt].length = (uint32_t)opac_build_hmtx(hmtx, advances, (uint16_t)OPAC_FACE_NUM_GLYPHS);
    nt++;
    tables[nt].tag = ROBO_FONT_TAG_CMAP;
    tables[nt].data = cmap;
    tables[nt].length = (uint32_t)opac_build_cmap(cmap, 0x41u, 0x41u);
    nt++;
    tables[nt].tag = ROBO_FONT_TAG_LOCA;
    tables[nt].data = loca;
    tables[nt].length = (uint32_t)((OPAC_FACE_NUM_GLYPHS + 1u) * 4u);
    nt++;
    tables[nt].tag = ROBO_FONT_TAG_GLYF;
    tables[nt].data = glyf;
    tables[nt].length = (uint32_t)glyph_size;
    nt++;

    n = opac_build_sfnt(font, sizeof(font), ROBO_FONT_SFNT_VERSION_TRUE, tables, nt);
    ROBO_CHECK(n > 0u);
    ROBO_CHECK_EQ_INT(robo_font_source_create_memory_copy(font, n, &out->source), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_font_face_create(out->source, 0, &out->face), ROBO_OK);
}

static void opac_face_destroy(opac_face_t *face)
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

static robo_layout_rect_t opac_rect(int32_t x, int32_t y, int32_t w, int32_t h)
{
    robo_layout_rect_t rect;

    rect.x = x;
    rect.y = y;
    rect.width = w;
    rect.height = h;
    return rect;
}

/** A record holding @p strength, spelled the way a case reads it. */
static robo_paint_opacity_t opac_of(double strength)
{
    return robo_paint_opacity_from_value(strength);
}

static void opac_check_int(int actual, int expected, const char *what)
{
    if (actual != expected) {
        robo_test_fail(__FILE__, __LINE__, "%s: expected %d, got %d", what, expected, actual);
    }
}

/** Assert a strength, printing both values' own decimal forms. */
static void opac_check_value(double actual, double expected, const char *what)
{
    if (!(actual == expected)) {
        robo_test_fail(__FILE__, __LINE__, "%s: expected %.17g, got %.17g", what, expected,
                       actual);
    }
}

static void opac_check_record(const robo_paint_opacity_t *record,
                              double expected,
                              const char *what)
{
    opac_check_value(robo_paint_opacity_value(record), expected, what);
}

/** A CPU surface for a paint pass: owned, RGBA8, zero-filled (so "untouched" is transparent and
 *  an unpainted pixel is distinguishable from a black one). */
static robo_gfx_surface_t *opac_surface(uint32_t width,
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

/**
 * Build a display list from a record list, asserting the build succeeds.
 *
 * The stats are **zeroed before the call**, so a refused build (which leaves the output struct
 * untouched) shows up as zeroes and the message above it names the refusal, rather than a case
 * reporting whatever the stack happened to hold.
 */
static void opac_build(const robo_paint_list_t *records,
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
    status = robo_paint_display_list_builder_build(builder, records, *out_list, out_stats, &diag);
    if (status != ROBO_OK) {
        robo_test_fail(__FILE__, __LINE__, "the display build refused the records: %s (line %d)",
                       diag.message ? diag.message : "(no message)", diag.line);
    }
    robo_paint_display_list_builder_destroy(builder);
}

/**
 * Build a display list, reporting the outcome rather than asserting it.
 *
 * The refusal cases need a build that is *expected* to fail, so the status and the message come
 * back to the caller -- and the list is handed back **emptied**, which is itself one of the
 * claims.
 */
static robo_result_t opac_build_expect_refusal(const robo_paint_list_t *records,
                                               robo_paint_display_list_t **out_list,
                                               robo_paint_diag_t *out_diag)
{
    robo_paint_display_list_builder_t *builder = NULL;
    robo_result_t status;

    ROBO_CHECK_EQ_INT(robo_paint_display_list_create(out_list, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_paint_display_list_builder_create(&builder, NULL), ROBO_OK);
    robo_paint_diag_reset(out_diag);
    status = robo_paint_display_list_builder_build(builder, records, *out_list, NULL, out_diag);
    robo_paint_display_list_builder_destroy(builder);
    return status;
}

/** Paint @p list into @p target, asserting the pass succeeds. */
static void opac_paint(const robo_paint_display_list_t *list,
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
static robo_gfx_color_rgba8_t opac_read(robo_gfx_surface_t *surface, int32_t x, int32_t y)
{
    robo_gfx_color_rgba8_t color;
    int32_t width = (int32_t)robo_gfx_surface_width(surface);
    int32_t height = (int32_t)robo_gfx_surface_height(surface);

    memset(&color, 0, sizeof(color));
    if (x < 0 || y < 0 || x >= width || y >= height) {
        robo_test_fail(__FILE__, __LINE__, "the sample %d,%d is outside the %dx%d surface",
                       (int)x, (int)y, (int)width, (int)height);
        return color;
    }
    ROBO_CHECK_EQ_INT(robo_gfx_surface_get_pixel(surface, (uint32_t)x, (uint32_t)y, &color),
                      ROBO_OK);
    return color;
}

static void opac_pixel_is(robo_gfx_surface_t *surface,
                          int32_t x,
                          int32_t y,
                          robo_gfx_color_rgba8_t expected,
                          const char *what)
{
    robo_gfx_color_rgba8_t actual = opac_read(surface, x, y);

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
static int opac_any_ink(robo_gfx_surface_t *surface,
                        int32_t x0,
                        int32_t y0,
                        int32_t x1,
                        int32_t y1)
{
    int32_t x;
    int32_t y;

    for (y = y0; y < y1; y++) {
        for (x = x0; x < x1; x++) {
            if (opac_read(surface, x, y).a != 0u) {
                return 1;
            }
        }
    }
    return 0;
}

/** The first inked pixel of the rectangle [x0,x1) x [y0,y1), or 0 when there is none. */
static int opac_first_ink(robo_gfx_surface_t *surface,
                          int32_t x0,
                          int32_t y0,
                          int32_t x1,
                          int32_t y1,
                          int32_t *out_x,
                          int32_t *out_y)
{
    int32_t x;
    int32_t y;

    for (y = y0; y < y1; y++) {
        for (x = x0; x < x1; x++) {
            if (opac_read(surface, x, y).a != 0u) {
                if (out_x) {
                    *out_x = x;
                }
                if (out_y) {
                    *out_y = y;
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
 * The oracle is `robo_gfx_composite_src_over_opacity` -- the **graphics module's own blend** --
 * rather than a number this suite computes, so a case asserts a *relationship* ("the faded pixel
 * is the composite of the opaque one") that cannot drift from the arithmetic it is testing. The
 * destination is passed in because a colour over white and the same colour over nothing are
 * different pictures, and it is the fixture's business which one it is.
 */
static robo_gfx_color_rgba8_t opac_expected(robo_gfx_color_rgba8_t opaque,
                                            robo_gfx_color_rgba8_t under,
                                            double strength)
{
    robo_paint_opacity_t record = opac_of(strength);

    return robo_gfx_composite_src_over_opacity(opaque, under,
                                               robo_paint_opacity_to_byte(&record));
}

/** The @p nth command of @p list whose kind is @p kind, or 0 when there is none. */
static int opac_command_of_kind(const robo_paint_display_list_t *list,
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
static size_t opac_count_of_kind(const robo_paint_display_list_t *list,
                                 robo_paint_display_command_kind_t kind)
{
    return robo_paint_display_list_count_of_kind(list, kind);
}

/** The index of the @p nth command of @p kind whose node is @p node, or `(size_t)-1`. */
static size_t opac_index_of_kind_for_node(const robo_paint_display_list_t *list,
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

/** The index of the @p nth record of @p kind whose node is @p node, or `(size_t)-1`. */
static size_t opac_op_index_for_node(const paint_result_t *result,
                                     robo_paint_op_kind_t kind,
                                     robo_layout_node_id_t node,
                                     size_t nth)
{
    size_t index;
    size_t seen = 0u;

    for (index = 0u; index < robo_paint_list_count(result->list); index++) {
        robo_paint_operation_t operation;

        if (robo_paint_list_at(result->list, index, &operation) != ROBO_OK) {
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
static robo_paint_operation_t opac_op_at(const paint_result_t *result, size_t index)
{
    robo_paint_operation_t operation;

    robo_paint_operation_init(&operation);
    if (index == (size_t)-1) {
        robo_test_fail(__FILE__, __LINE__, "the record index is not in the list");
        return operation;
    }
    ROBO_CHECK_EQ_INT((int)robo_paint_list_at(result->list, index, &operation), (int)ROBO_OK);
    return operation;
}

/** Assert the record at @p index in @p result, which is an lvalue a caller can address. */
static void opac_check_record_at(const paint_result_t *result,
                                 size_t index,
                                 double expected,
                                 const char *what)
{
    robo_paint_operation_t operation = opac_op_at(result, index);

    opac_check_record(&operation.opacity, expected, what);
}

/** The @p nth record of @p kind in @p result, asserted to exist. */
static robo_paint_operation_t opac_op_of_kind(const paint_result_t *result,
                                              robo_paint_op_kind_t kind,
                                              size_t nth)
{
    robo_paint_operation_t operation;

    robo_paint_operation_init(&operation);
    ROBO_CHECK_EQ_INT(paint_nth_of_kind(result, kind, nth, &operation), 1);
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
static void opac_node_is(const paint_result_t *result,
                         robo_layout_node_id_t node,
                         double expected,
                         const char *what)
{
    size_t index;
    int seen = 0;

    for (index = 0u; index < robo_paint_list_count(result->list); index++) {
        robo_paint_operation_t operation;

        if (robo_paint_list_at(result->list, index, &operation) != ROBO_OK) {
            break;
        }
        if (operation.node != node) {
            continue;
        }
        if (operation.kind == ROBO_PAINT_OP_BEGIN_BOX ||
            operation.kind == ROBO_PAINT_OP_END_BOX) {
            continue;
        }
        opac_check_record(&operation.opacity, expected, what);
        seen++;
    }
    ROBO_CHECK(seen > 0);
}

/** Every record of @p result, checked against @p expected -- the whole list. */
static void opac_list_is(const paint_result_t *result, double expected, const char *what)
{
    size_t index;

    for (index = 0u; index < robo_paint_list_count(result->list); index++) {
        robo_paint_operation_t operation;

        if (robo_paint_list_at(result->list, index, &operation) != ROBO_OK) {
            break;
        }
        opac_check_record(&operation.opacity, expected, what);
    }
}

/** In layout coordinates: a box with no insets, so its three rectangles are one. */
static robo_paint_boxes_t opac_boxes(int32_t x, int32_t y, int32_t w, int32_t h)
{
    robo_paint_boxes_t boxes;

    robo_paint_boxes_init(&boxes);
    boxes.border = opac_rect(x, y, w, h);
    boxes.padding = boxes.border;
    boxes.content = boxes.border;
    boxes.insets_measured = 0;
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
static void opac_append_record(robo_paint_list_t *list,
                               robo_paint_op_kind_t kind,
                               robo_layout_node_id_t node,
                               size_t depth,
                               robo_paint_boxes_t boxes,
                               robo_paint_opacity_t opacity)
{
    robo_paint_operation_t operation;
    robo_paint_diag_t diag;

    robo_paint_operation_init(&operation);
    operation.kind = kind;
    operation.node = node;
    operation.depth = depth;
    operation.boxes = boxes;
    operation.origin = robo_gfx_point(0, 0);
    operation.opacity = opacity;
    operation.color = robo_paint_op_kind_draws(kind)
                              ? robo_css_color_make(255u, 0u, 0u, 255u)
                              : robo_css_color_none();
    operation.style = robo_paint_op_kind_is_border(kind) ? ROBO_CSS_BORDER_STYLE_SOLID
                                                         : ROBO_CSS_BORDER_STYLE_NONE;
    /*
     * The refusal the list would give is one sentence about four different rules; this says which
     * of them a case has tripped, which is the difference between a failing case that names its own
     * bug and one that sends a reader to `robo_paint_operation_is_valid`.
     */
    if (!robo_paint_operation_is_valid(&operation)) {
        robo_test_fail(__FILE__, __LINE__,
                       "a hand-built record is not a record: kind %d node %d known=%d boxes=%d "
                       "clip=%d drawable=%d text-is-a-run=%d colour-none=%d style-none=%d "
                       "opacity-valid=%d opacity-opaque=%d",
                       (int)kind, (int)node, robo_paint_op_kind_is_known(kind),
                       robo_paint_boxes_is_valid(&operation.boxes),
                       robo_paint_clip_is_valid(&operation.clip),
                       robo_css_color_is_valid(operation.color),
                       robo_paint_text_run_is_valid(&operation.text),
                       (int)(operation.color.kind == ROBO_CSS_COLOR_NONE),
                       (int)(operation.style == ROBO_CSS_BORDER_STYLE_NONE),
                       robo_paint_opacity_is_valid(&operation.opacity),
                       robo_paint_opacity_is_opaque(&operation.opacity));
    }
    robo_paint_diag_reset(&diag);
    if (robo_paint_list_append(list, &operation, &diag) != ROBO_OK) {
        robo_test_fail(__FILE__, __LINE__, "a hand-built record was refused: kind %d node %d: %s",
                       (int)kind, (int)node,
                       diag.message ? diag.message : "(no message)");
    }
}

/** An empty record list, asserted to have been created. */
static robo_paint_list_t *opac_empty_box(void)
{
    robo_paint_list_t *records = NULL;

    ROBO_CHECK_EQ_INT(robo_paint_list_create(&records, NULL), ROBO_OK);
    return records;
}

/**
 * A fresh record list holding one open box with a **decoration already drawn**: the first of the
 * two states an opacity push may not follow.
 */
static robo_paint_list_t *opac_painted_box(robo_layout_node_id_t node)
{
    robo_paint_list_t *records = opac_empty_box();

    opac_append_record(records, ROBO_PAINT_OP_BEGIN_BOX, node, 0u, opac_boxes(0, 0, 10, 10),
                       robo_paint_opacity_opaque());
    opac_append_record(records, ROBO_PAINT_OP_BACKGROUND, node, 0u, opac_boxes(0, 0, 10, 10),
                       robo_paint_opacity_opaque());
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
static robo_paint_list_t *opac_open_box(robo_layout_node_id_t node)
{
    robo_paint_list_t *records = opac_painted_box(node);

    opac_append_record(records, ROBO_PAINT_OP_CONTENT, node, 0u, opac_boxes(0, 0, 10, 10),
                       robo_paint_opacity_opaque());
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
static robo_paint_list_t *opac_scoped_box(robo_layout_node_id_t node, double strength)
{
    robo_paint_list_t *records = opac_empty_box();
    robo_paint_opacity_t record = opac_of(strength);

    opac_append_record(records, ROBO_PAINT_OP_BEGIN_BOX, node, 0u, opac_boxes(0, 0, 10, 10),
                       robo_paint_opacity_opaque());
    opac_append_record(records, ROBO_PAINT_OP_PUSH_OPACITY, node, 0u, opac_boxes(0, 0, 10, 10),
                       record);
    opac_append_record(records, ROBO_PAINT_OP_BACKGROUND, node, 0u, opac_boxes(0, 0, 10, 10),
                       record);
    opac_append_record(records, ROBO_PAINT_OP_CONTENT, node, 0u, opac_boxes(0, 0, 10, 10),
                       record);
    return records;
}

/** The pop that closes @p node's scope at @p strength, appended to @p records. */
static void opac_pop_box(robo_paint_list_t *records,
                         robo_layout_node_id_t node,
                         double strength)
{
    opac_append_record(records, ROBO_PAINT_OP_POP_OPACITY, node, 0u, opac_boxes(0, 0, 10, 10),
                       opac_of(strength));
}

/** Close @p records' open box, so a case can then add something after it. */
static void opac_close_box(robo_paint_list_t *records, robo_layout_node_id_t node)
{
    opac_append_record(records, ROBO_PAINT_OP_END_BOX, node, 0u, opac_boxes(0, 0, 10, 10),
                       robo_paint_opacity_opaque());
}

/**
 * Walk every **drawing** command of @p list that belongs to @p node and check its strength.
 *
 * The display-list twin of `opac_node_is`, and the narrow one is what a nested document needs:
 * "every drawing in the list" is a different claim from "every drawing of this box", and in a
 * doubly faded subtree the outer box's own background is at the outer strength while its child's
 * is at the product -- so a helper that lumped them would have to assert a number that is not
 * any of the strengths in the document.
 */
static void opac_draws_of_are(const robo_paint_display_list_t *list,
                              robo_layout_node_id_t node,
                              double expected,
                              const char *what)
{
    size_t index;
    int seen = 0;

    for (index = 0u; index < robo_paint_display_list_count(list); index++) {
        robo_paint_display_command_t command;

        if (robo_paint_display_list_at(list, index, &command) != ROBO_OK) {
            break;
        }
        if (!robo_paint_display_command_kind_draws(command.kind) || command.node != node) {
            continue;
        }
        opac_check_record(&command.opacity, expected, what);
        seen++;
    }
    ROBO_CHECK(seen > 0);
}

/**
 * Walk every **drawing** command of @p list and check its strength is @p expected.
 *
 * Drawing only, because a structural command's strength is the state at *its* position rather
 * than at the box that owns it: a box's `end-node` stands after its own scope has closed and
 * therefore carries the enclosing strength, which is a fact about the pair's placement rather than
 * about the scope.
 */
static void opac_draws_are(const robo_paint_display_list_t *list,
                           double expected,
                           const char *what)
{
    size_t index;
    int seen = 0;

    for (index = 0u; index < robo_paint_display_list_count(list); index++) {
        robo_paint_display_command_t command;

        if (robo_paint_display_list_at(list, index, &command) != ROBO_OK) {
            break;
        }
        if (!robo_paint_display_command_kind_draws(command.kind)) {
            continue;
        }
        opac_check_record(&command.opacity, expected, what);
        seen++;
    }
    ROBO_CHECK(seen > 0);
}

/* ------------------------------------------------------------------ */
/* 1. The record                                                       */
/* ------------------------------------------------------------------ */

static void test_opacity_record(void)
{
    robo_paint_opacity_t record;
    volatile double zero = 0.0;

    paint_fixture();

    /*
     * The cleared state, and the direction that is the whole reason this type has a door for it: a
     * record nothing asked about is **fully opaque**, so a document that never mentions `opacity`
     * draws exactly as it did before the property existed. A zeroed record would mean invisible,
     * and every element of every such document would disappear.
     */
    memset(&record, 0x5a, sizeof(record));
    robo_paint_opacity_init(&record);
    opac_check_value(record.value, 1.0, "the cleared record's value");
    opac_check_int(robo_paint_opacity_is_valid(&record), 1, "a cleared record is valid");
    opac_check_int(robo_paint_opacity_is_opaque(&record), 1, "a cleared record is opaque");
    opac_check_int(robo_paint_opacity_is_transparent(&record), 0,
                   "a cleared record is not transparent");
    opac_check_int(robo_paint_opacity_equal(&record, &(robo_paint_opacity_t){ 1.0 }), 1,
                   "a cleared record equals `1`");
    {
        robo_paint_opacity_t by_value = robo_paint_opacity_opaque();

        opac_check_int(robo_paint_opacity_equal(&by_value, &record), 1,
                       "the by-value door agrees with init");
    }

    /* NULL is not a record, but every question about *the state* has an answer for it -- and two
     * of those answers point in opposite directions, which is the pair worth pinning: nothing in
     * force is full strength, and it is emphatically not a transparent scope. */
    opac_check_value(robo_paint_opacity_value(NULL), 1.0, "the value of a NULL");
    opac_check_int(robo_paint_opacity_is_valid(NULL), 0, "NULL is not a record");
    opac_check_int(robo_paint_opacity_is_opaque(NULL), 1, "nothing in force is opaque");
    opac_check_int(robo_paint_opacity_is_transparent(NULL), 0,
                   "nothing in force is not a transparent scope");

    /* The construction door clamps rather than refusing: a declaration that arrived out of range
     * was already clamped where it was computed, so what this protects is a caller's arithmetic
     * between boxes. */
    opac_check_value(opac_of(0.0).value, 0.0, "zero");
    opac_check_value(opac_of(0.25).value, 0.25, "a quarter");
    opac_check_value(opac_of(2.0).value, 1.0, "above one clamps to one");
    opac_check_value(opac_of(-3.0).value, 0.0, "below zero clamps to zero");
    {
        /* A NaN becomes the **opaque** record, which is the direction `RoboCSSOpacity.h` argues:
         * a bug in a caller's arithmetic must render the way "nothing was declared" does, which is
         * visible, rather than the way "invisible" does, which is not. */
        robo_paint_opacity_t nan_record = opac_of(zero / zero);

        opac_check_int(robo_paint_opacity_is_valid(&nan_record), 1, "a NaN makes a record");
        opac_check_int(robo_paint_opacity_is_opaque(&nan_record), 1, "a NaN makes an opaque one");
    }

    /* The predicates at their boundaries, which is where a table of them is worth having. */
    {
        robo_paint_opacity_t zero_record = opac_of(0.0);
        robo_paint_opacity_t half = opac_of(0.5);

        opac_check_int(robo_paint_opacity_is_valid(&zero_record), 1, "zero is a record");
        opac_check_int(robo_paint_opacity_is_transparent(&zero_record), 1, "zero is transparent");
        opac_check_int(robo_paint_opacity_is_opaque(&zero_record), 0, "zero is not opaque");
        opac_check_int(robo_paint_opacity_is_transparent(&half), 0, "a half is not transparent");
        opac_check_int(robo_paint_opacity_is_opaque(&half), 0, "a half is not opaque");
    }

    /* Equality is the number **exactly**, and this case says so: a tolerance here would turn a
     * determinism failure into a pass, which is the one thing a comparison in this module must not
     * do. `0.1 + 0.2` is not `0.3` in binary floating point, so two records built from them are not
     * equal -- and neither are two records a millionth apart. */
    opac_check_int(robo_paint_opacity_equal(&(robo_paint_opacity_t){ 0.5 },
                                            &(robo_paint_opacity_t){ 0.5 }),
                   1, "two halves are equal");
    opac_check_int(robo_paint_opacity_equal(&(robo_paint_opacity_t){ 0.5 },
                                            &(robo_paint_opacity_t){ 0.25 }),
                   0, "a half and a quarter are not");
    {
        robo_paint_opacity_t inexact = opac_of(0.1 + 0.2);
        robo_paint_opacity_t exact = opac_of(0.3);

        opac_check_int(robo_paint_opacity_equal(&inexact, &exact), 0,
                       "and there is no tolerance: 0.1 + 0.2 is not 0.3");
    }
    opac_check_int(robo_paint_opacity_equal(NULL, NULL), 1, "two NULLs are equal");
    opac_check_int(robo_paint_opacity_equal(NULL, &(robo_paint_opacity_t){ 1.0 }), 0,
                   "a NULL is not a record");
}

static void test_opacity_record_safety(void)
{
    size_t row;

    paint_fixture();

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
            robo_paint_opacity_t outer = opac_of(table[row][0]);
            robo_paint_opacity_t inner = opac_of(table[row][1]);
            robo_paint_opacity_t product = robo_paint_opacity_multiply(&outer, &inner);

            opac_check_value(product.value, table[row][2], "the nested product table");
        }
    }

    /* A NULL operand is read as **opaque** (nothing in force), so the call is safe on a
     * destructively cleared pointer -- and the answer is the other operand's strength, whichever
     * side went missing. */
    {
        robo_paint_opacity_t outer = opac_of(0.5);

        opac_check_value(robo_paint_opacity_multiply(NULL, &outer).value, 0.5,
                         "a NULL outer leaves the inner strength");
        opac_check_value(robo_paint_opacity_multiply(&outer, NULL).value, 0.5,
                         "a NULL inner leaves the outer strength");
        opac_check_value(robo_paint_opacity_multiply(NULL, NULL).value, 1.0,
                         "two NULLs are fully opaque");
    }

    /* And the product of two records is a record, always: the clamp cannot fire for two records
     * this build produced, and that is exactly why the *result* is worth asserting rather than the
     * arithmetic. */
    {
        robo_paint_opacity_t product = robo_paint_opacity_multiply(&(robo_paint_opacity_t){ 0.0 },
                                                                  &(robo_paint_opacity_t){ 0.0 });

        opac_check_int(robo_paint_opacity_is_valid(&product), 1, "the product is a record");
        opac_check_value(product.value, 0.0, "zero times zero is zero");
    }
}

static void test_opacity_byte_conversion(void)
{
    paint_fixture();

    /*
     * The one conversion between this module's `[0, 1]` and the raster's global opacity, and the
     * rounding is the decision: **to nearest**, so `0.5` becomes `128` rather than `127`. The
     * table states the arithmetic once; the pass's own cases then pin it at a pixel, and the
     * `255` case is what makes a fully opaque drawing one a raster nothing has changed produces.
     */
    opac_check_int(robo_paint_opacity_to_byte(&(robo_paint_opacity_t){ 0.0 }), 0,
                   "zero is invisible");
    opac_check_int(robo_paint_opacity_to_byte(&(robo_paint_opacity_t){ 0.25 }), 64,
                   "a quarter rounds to 64");
    opac_check_int(robo_paint_opacity_to_byte(&(robo_paint_opacity_t){ 0.5 }), OPAC_HALF_BYTE,
                   "a half rounds to 128");
    opac_check_int(robo_paint_opacity_to_byte(&(robo_paint_opacity_t){ 0.75 }), 191,
                   "three quarters round to 191");
    opac_check_int(robo_paint_opacity_to_byte(&(robo_paint_opacity_t){ 1.0 }), 255,
                   "one is exactly the raster's own default");

    /* The safe direction for a converter: a value nothing can read must not put ink on a surface,
     * and answering `255` would turn a caller's arithmetic bug into a fully drawn document. */
    opac_check_int(robo_paint_opacity_to_byte(NULL), 0, "a NULL converts to invisible");
    opac_check_int(robo_paint_opacity_to_byte(&(robo_paint_opacity_t){ 2.0 }), 0,
                   "a record outside the range converts to invisible");
}

/* ------------------------------------------------------------------ */
/* 2. The stack                                                        */
/* ------------------------------------------------------------------ */

static void test_opacity_stack(void)
{
    robo_paint_opacity_stack_t *stack = NULL;
    robo_paint_opacity_t current;
    robo_paint_opacity_t half = opac_of(0.5);
    robo_paint_opacity_t quarter = opac_of(0.25);
    size_t baseline = paint_live_blocks();

    paint_fixture();

    ROBO_CHECK_EQ_INT((int)robo_paint_opacity_stack_create(NULL, NULL),
                      (int)ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT((int)robo_paint_opacity_stack_create(&stack, NULL), (int)ROBO_OK);
    ROBO_CHECK(stack != NULL);
    ROBO_CHECK_EQ_INT((int)robo_paint_opacity_stack_depth(stack), 0);
    ROBO_CHECK_EQ_INT((int)robo_paint_opacity_stack_capacity(stack), 0);
    opac_check_int(robo_paint_opacity_stack_is_empty(stack), 1, "a fresh stack is empty");

    /* An empty stack answers the **opaque** record: "nothing is in force" is the ordinary state of
     * a document that declares no opacity, not a miss -- and a NULL stack answers it too, so a
     * caller may read a destructively cleared pointer without a branch. */
    memset(&current, 0x5a, sizeof(current));
    robo_paint_opacity_stack_current(stack, &current);
    opac_check_value(current.value, 1.0, "an empty stack answers full strength");
    memset(&current, 0x5a, sizeof(current));
    robo_paint_opacity_stack_current(NULL, &current);
    opac_check_value(current.value, 1.0, "a NULL stack answers full strength");
    robo_paint_opacity_stack_current(stack, NULL);

    /* A push, a read and a pop: the innermost scope is what is in force, and the pair returns the
     * stack to exactly where it was. */
    ROBO_CHECK_EQ_INT((int)robo_paint_opacity_stack_push(stack, &half, NULL), (int)ROBO_OK);
    ROBO_CHECK_EQ_INT((int)robo_paint_opacity_stack_depth(stack), 1);
    robo_paint_opacity_stack_current(stack, &current);
    opac_check_value(current.value, 0.5, "the pushed record is in force");
    ROBO_CHECK_EQ_INT((int)robo_paint_opacity_stack_push(stack, &quarter, NULL), (int)ROBO_OK);
    robo_paint_opacity_stack_current(stack, &current);
    opac_check_value(current.value, 0.25, "the innermost record is in force");
    ROBO_CHECK_EQ_INT((int)robo_paint_opacity_stack_pop(stack, NULL), (int)ROBO_OK);
    robo_paint_opacity_stack_current(stack, &current);
    opac_check_value(current.value, 0.5, "popping restores the enclosing strength");
    ROBO_CHECK_EQ_INT((int)robo_paint_opacity_stack_pop(stack, NULL), (int)ROBO_OK);
    opac_check_int(robo_paint_opacity_stack_is_empty(stack), 1, "both scopes closed");

    /* A pop with nothing to pop is a **refusal**, not a silent no-op: answering OK would let a
     * consumer that lost track of its own pushes go on drawing at a strength that is not in force.
     * The refusal is safe -- the stack is untouched. */
    ROBO_CHECK_EQ_INT((int)robo_paint_opacity_stack_pop(stack, NULL), (int)ROBO_NOT_FOUND);
    ROBO_CHECK_EQ_INT((int)robo_paint_opacity_stack_depth(stack), 0);
    ROBO_CHECK_EQ_INT((int)robo_paint_opacity_stack_pop(NULL, NULL), (int)ROBO_INVALID_ARGUMENT);

    /* The argument checks: a NULL stack and a NULL record are both argument errors, and neither
     * pushes anything. */
    ROBO_CHECK_EQ_INT((int)robo_paint_opacity_stack_push(NULL, &half, NULL),
                      (int)ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT((int)robo_paint_opacity_stack_push(stack, NULL, NULL),
                      (int)ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT((int)robo_paint_opacity_stack_depth(stack), 0);

    /* A transparent record is a legal push: a scope that draws nothing is a fact about a document,
     * and the container has no opinion about values. The *command* vocabulary refuses an opaque
     * scope, because a scope that establishes nothing is a command a consumer could only skip --
     * a different rule, at a different layer, and this pair of assertions is where they are
     * distinguished. */
    ROBO_CHECK_EQ_INT((int)robo_paint_opacity_stack_push(stack, &(robo_paint_opacity_t){ 0.0 },
                                                         NULL),
                      (int)ROBO_OK);
    robo_paint_opacity_stack_current(stack, &current);
    opac_check_int(robo_paint_opacity_is_transparent(&current), 1, "a zero scope is in force");
    {
        robo_paint_opacity_t opaque = robo_paint_opacity_opaque();

        ROBO_CHECK_EQ_INT((int)robo_paint_opacity_stack_push(stack, &opaque, NULL),
                          (int)ROBO_OK);
    }
    robo_paint_opacity_stack_current(stack, &current);
    opac_check_int(robo_paint_opacity_is_opaque(&current), 1, "and so is an opaque one");

    /* Reset empties it and keeps its capacity, which is what a caller reusing a stack per pass
     * relies on. */
    robo_paint_opacity_stack_reset(stack);
    ROBO_CHECK_EQ_INT((int)robo_paint_opacity_stack_depth(stack), 0);
    ROBO_CHECK_EQ_INT((int)robo_paint_opacity_stack_capacity(stack), 8);
    robo_paint_opacity_stack_reset(NULL);

    robo_paint_opacity_stack_destroy(stack);
    robo_paint_opacity_stack_destroy(NULL);
    ROBO_CHECK_EQ_INT((int)paint_live_blocks(), (int)baseline);
}

static void test_opacity_stack_growth(void)
{
    robo_paint_opacity_stack_t *stack = NULL;
    robo_paint_opacity_t current;
    size_t index;

    paint_fixture();

    ROBO_CHECK_EQ_INT((int)robo_paint_opacity_stack_create(&stack, NULL), (int)ROBO_OK);
    ROBO_CHECK_EQ_INT((int)robo_paint_opacity_stack_capacity(stack), 0);

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
        robo_paint_opacity_t pushed = opac_of(1.0 - ((double)index / 40.0));

        ROBO_CHECK_EQ_INT((int)robo_paint_opacity_stack_push(stack, &pushed, NULL), (int)ROBO_OK);
    }
    ROBO_CHECK_EQ_INT((int)robo_paint_opacity_stack_depth(stack), 20);
    ROBO_CHECK_EQ_INT((int)robo_paint_opacity_stack_capacity(stack), 32);
    robo_paint_opacity_stack_current(stack, &current);
    opac_check_value(current.value, 1.0 - (19.0 / 40.0), "the top of twenty scopes");

    for (index = 0u; index < 20u; index++) {
        ROBO_CHECK_EQ_INT((int)robo_paint_opacity_stack_pop(stack, NULL), (int)ROBO_OK);
    }
    opac_check_int(robo_paint_opacity_stack_is_empty(stack), 1, "twenty pushes, twenty pops");

    /* And a push after the pops reads the value it pushed, not a stale one. */
    ROBO_CHECK_EQ_INT((int)robo_paint_opacity_stack_push(stack, &(robo_paint_opacity_t){ 0.25 },
                                                         NULL),
                      (int)ROBO_OK);
    robo_paint_opacity_stack_current(stack, &current);
    opac_check_value(current.value, 0.25, "a push after the pops reads its own value");

    robo_paint_opacity_stack_destroy(stack);
}

/* ------------------------------------------------------------------ */
/* 3. The traversal                                                    */
/* ------------------------------------------------------------------ */

static void test_opacity_default(void)
{
    paint_fixture_doc_t fixture;
    paint_result_t result;
    size_t baseline = paint_live_blocks();

    paint_fixture();

    paint_begin_laid_out(&fixture,
                         "<html><body><div class=\"plain\"></div></body></html>",
                         OPAC_BASE_CSS " .plain { width: 20px; height: 20px;"
                         " background-color: #ff0000 }");
    paint_once(&fixture, &result);

    /*
     * A document that declares no opacity produces a list with **no opacity command in it**, and
     * every record in it carries full strength. That is what makes the phase additive rather than
     * a rewrite: every drawing this build produced before it draws at exactly the same strength it
     * did before, because full strength is the raster's own default and is never written.
     */
    ROBO_CHECK_EQ_INT((int)result.stats.opacity_declared, 0);
    ROBO_CHECK_EQ_INT((int)result.stats.opacity_scopes, 0);
    ROBO_CHECK_EQ_INT((int)paint_count_of(&result, ROBO_PAINT_OP_PUSH_OPACITY), 0);
    ROBO_CHECK_EQ_INT((int)paint_count_of(&result, ROBO_PAINT_OP_POP_OPACITY), 0);
    ROBO_CHECK(robo_paint_list_count(result.list) > 0u);
    opac_list_is(&result, 1.0, "every record of a document that declares no opacity");

    paint_result_dispose(&result);
    paint_end(&fixture);
    ROBO_CHECK_EQ_INT((int)paint_live_blocks(), (int)baseline);
}

static void test_opacity_explicit_one(void)
{
    paint_fixture_doc_t fixture;
    paint_result_t silent;
    paint_result_t declared;
    size_t baseline = paint_live_blocks();

    paint_fixture();

    /*
     * The **same** document twice, the only difference being a declaration that changes nothing.
     * `1` is what an element that says nothing already has, so the two lists must be
     * byte-identical -- which is a stronger statement than "the pixels match": it says the phase
     * emits no command at all for a declaration that establishes nothing, and that its two counters
     * stay at zero.
     */
    paint_begin_laid_out(&fixture,
                         "<html><body><div class=\"plain\"></div></body></html>",
                         OPAC_BASE_CSS " .plain { width: 20px; height: 20px;"
                         " background-color: #ff0000 }");
    paint_once(&fixture, &silent);
    paint_end(&fixture);

    paint_begin_laid_out(&fixture,
                         "<html><body><div class=\"plain\"></div></body></html>",
                         OPAC_BASE_CSS " .plain { width: 20px; height: 20px;"
                         " background-color: #ff0000; opacity: 1 }");
    paint_once(&fixture, &declared);
    ROBO_CHECK_EQ_INT((int)declared.stats.opacity_declared, 0);
    ROBO_CHECK_EQ_INT((int)declared.stats.opacity_scopes, 0);
    paint_end(&fixture);

    opac_check_int(robo_paint_list_equal(silent.list, declared.list), 1,
                   "`opacity: 1` draws the same list as saying nothing");
    opac_check_int((int)robo_paint_list_count(silent.list),
                   (int)robo_paint_list_count(declared.list), "and the same number of records");

    paint_result_dispose(&declared);
    paint_result_dispose(&silent);
    ROBO_CHECK_EQ_INT((int)paint_live_blocks(), (int)baseline);
}

static void test_opacity_declared(void)
{
    paint_fixture_doc_t fixture;
    paint_result_t result;
    robo_layout_node_id_t fade;
    robo_layout_node_id_t kid;
    robo_paint_display_list_t *list = NULL;
    robo_paint_display_list_stats_t stats;
    size_t baseline = paint_live_blocks();

    paint_fixture();

    paint_begin_laid_out(&fixture,
                         "<html><body><div class=\"fade\"><div class=\"kid\"></div></div>"
                         "</body></html>",
                         OPAC_BASE_CSS " .fade { width: 40px; height: 40px;"
                         " background-color: #ff0000; opacity: 0.5 }"
                         " .kid { width: 10px; height: 10px; background-color: #0000ff }");
    paint_once(&fixture, &result);
    fade = paint_box(&fixture, "fade");
    kid = paint_box(&fixture, "kid");

    /* One declaration, one scope, and the whole subtree carries the box's strength -- its own
     * background *and* its child's. `declared` and `scopes` agreeing at 1 is the ordinary case: the
     * declaration changed something. */
    ROBO_CHECK_EQ_INT((int)result.stats.opacity_declared, 1);
    ROBO_CHECK_EQ_INT((int)result.stats.opacity_scopes, 1);
    ROBO_CHECK_EQ_INT((int)paint_count_of(&result, ROBO_PAINT_OP_PUSH_OPACITY), 1);
    ROBO_CHECK_EQ_INT((int)paint_count_of(&result, ROBO_PAINT_OP_POP_OPACITY), 1);
    opac_node_is(&result, fade, 0.5, "every record of the faded box");
    opac_node_is(&result, kid, 0.5, "the child inherits the strength");

    /* The pair carries the strength it establishes, in both halves: that is what makes a pair
     * checkable from the stream. */
    {
        robo_paint_operation_t push = opac_op_of_kind(&result, ROBO_PAINT_OP_PUSH_OPACITY, 0u);
        robo_paint_operation_t pop = opac_op_of_kind(&result, ROBO_PAINT_OP_POP_OPACITY, 0u);

        opac_check_record(&push.opacity, 0.5, "the push carries what it establishes");
        opac_check_record(&pop.opacity, 0.5, "and so does the pop");
        ROBO_CHECK_EQ_INT((int)push.node, (int)fade);
        ROBO_CHECK_EQ_INT((int)pop.node, (int)fade);
        opac_check_int(robo_paint_opacity_equal(&push.opacity, &pop.opacity), 1,
                       "the pair is self-identifying");
    }

    /* The display list converts both, with the stats agreeing and every drawing under the scope
     * carrying the strength. */
    opac_build(result.list, &list, &stats);
    ROBO_CHECK_EQ_INT((int)stats.opacity_scopes, 1);
    ROBO_CHECK_EQ_INT((int)opac_count_of_kind(list, ROBO_PAINT_DISPLAY_CMD_PUSH_OPACITY), 1);
    ROBO_CHECK_EQ_INT((int)opac_count_of_kind(list, ROBO_PAINT_DISPLAY_CMD_POP_OPACITY), 1);
    opac_draws_are(list, 0.5, "every drawing under the scope");
    robo_paint_display_list_destroy(list);

    paint_result_dispose(&result);
    paint_end(&fixture);
    ROBO_CHECK_EQ_INT((int)paint_live_blocks(), (int)baseline);
}

static void test_opacity_scope_placement(void)
{
    paint_fixture_doc_t fixture;
    paint_result_t result;
    robo_layout_node_id_t fade;
    robo_layout_node_id_t kid;
    size_t begin;
    size_t push;
    size_t background;
    size_t content;
    size_t pop;
    size_t end;

    paint_fixture();

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
    paint_begin_laid_out(&fixture,
                         "<html><body><div class=\"fade\"><div class=\"kid\"></div></div>"
                         "</body></html>",
                         OPAC_BASE_CSS " .fade { width: 40px; height: 40px;"
                         " background-color: #ff0000; opacity: 0.5 }"
                         " .kid { width: 10px; height: 10px; background-color: #0000ff }");
    paint_once(&fixture, &result);
    fade = paint_box(&fixture, "fade");
    kid = paint_box(&fixture, "kid");

    begin = opac_op_index_for_node(&result, ROBO_PAINT_OP_BEGIN_BOX, fade, 0u);
    push = opac_op_index_for_node(&result, ROBO_PAINT_OP_PUSH_OPACITY, fade, 0u);
    background = opac_op_index_for_node(&result, ROBO_PAINT_OP_BACKGROUND, fade, 0u);
    content = opac_op_index_for_node(&result, ROBO_PAINT_OP_CONTENT, fade, 0u);
    pop = opac_op_index_for_node(&result, ROBO_PAINT_OP_POP_OPACITY, fade, 0u);
    end = opac_op_index_for_node(&result, ROBO_PAINT_OP_END_BOX, fade, 0u);
    ROBO_CHECK(begin != (size_t)-1 && push != (size_t)-1 && background != (size_t)-1 &&
               content != (size_t)-1 && pop != (size_t)-1 && end != (size_t)-1);

    opac_check_int((int)push, (int)(begin + 1u), "the scope opens immediately after the marker");
    opac_check_int(background > push, 1, "the box's own background is inside the scope");
    opac_check_int(content > background, 1, "and so is its content boundary");
    opac_check_int(pop > content, 1, "the scope closes after the content boundary");
    opac_check_int(end > pop, 1, "and before the box's end marker");
    opac_check_int((int)(pop + 1u), (int)end, "nothing stands between the pop and the end");
    opac_check_int((int)opac_op_at(&result, begin).depth, (int)opac_op_at(&result, push).depth,
                   "the pair and the marker agree about the depth");

    /*
     * Every record's strength is the strength in force **at its own position**: the marker and the
     * end marker stand outside the pair and therefore carry the enclosing (full) strength, while
     * everything between them carries the box's. That single rule is what makes the two scope kinds
     * readable the same way even though their pairs nest differently -- and it is the same rule
     * that makes `pop-clip` and `end-box` disagree about the clip in 11.8.
     */
    opac_check_record_at(&result, begin, 1.0, "the marker stands outside the scope");
    opac_check_record_at(&result, push, 0.5, "the push is the scope");
    opac_check_record_at(&result, background, 0.5, "the background is inside");
    opac_check_record_at(&result, content, 0.5, "the content boundary too");
    opac_check_record_at(&result, pop, 0.5, "the pop carries the scope");
    opac_check_record_at(&result, end, 1.0, "the end marker is back outside");

    /* And a child box's own marker is inside its parent's scope. */
    {
        size_t kid_begin = opac_op_index_for_node(&result, ROBO_PAINT_OP_BEGIN_BOX, kid, 0u);

        opac_check_record_at(&result, kid_begin, 0.5,
                             "a child's marker is inside its parent's scope");
    }

    paint_result_dispose(&result);
    paint_end(&fixture);
}

static void test_opacity_nested(void)
{
    paint_fixture_doc_t fixture;
    paint_result_t result;
    robo_layout_node_id_t outer;
    robo_layout_node_id_t inner;
    robo_paint_display_list_t *list = NULL;
    robo_paint_display_list_stats_t stats;

    paint_fixture();

    paint_begin_laid_out(&fixture,
                         "<html><body><div class=\"outer\"><div class=\"inner\"></div></div>"
                         "</body></html>",
                         OPAC_BASE_CSS " .outer { width: 60px; height: 60px;"
                         " background-color: #ff0000; opacity: 0.5 }"
                         " .inner { width: 20px; height: 20px; background-color: #0000ff;"
                         " opacity: 0.5 }");
    paint_once(&fixture, &result);
    outer = paint_box(&fixture, "outer");
    inner = paint_box(&fixture, "inner");

    /* Two declarations, two scopes, and the child drawn at a **quarter** -- the product, not the
     * child's value. "The child replaces the parent" is the mistake this case exists for, and it
     * would leave the inner box at `0.5`. */
    ROBO_CHECK_EQ_INT((int)result.stats.opacity_declared, 2);
    ROBO_CHECK_EQ_INT((int)result.stats.opacity_scopes, 2);
    ROBO_CHECK_EQ_INT((int)paint_count_of(&result, ROBO_PAINT_OP_PUSH_OPACITY), 2);
    opac_node_is(&result, outer, 0.5, "the outer box's own records");
    opac_node_is(&result, inner, 0.25, "the inner box's records");

    /* The two pushes are properly nested and carry the strengths they establish. */
    {
        robo_paint_operation_t first = opac_op_of_kind(&result, ROBO_PAINT_OP_PUSH_OPACITY, 0u);
        robo_paint_operation_t second = opac_op_of_kind(&result, ROBO_PAINT_OP_PUSH_OPACITY, 1u);

        opac_check_record(&first.opacity, 0.5, "the outer push");
        opac_check_record(&second.opacity, 0.25, "the inner push is the product");
        opac_check_int(second.depth > first.depth, 1, "and it nests deeper");
        ROBO_CHECK_EQ_INT((int)first.node, (int)outer);
        ROBO_CHECK_EQ_INT((int)second.node, (int)inner);
    }

    opac_build(result.list, &list, &stats);
    ROBO_CHECK_EQ_INT((int)stats.opacity_scopes, 2);
    opac_draws_of_are(list, inner, 0.25, "the inner box's drawings");
    opac_draws_of_are(list, outer, 0.5, "and the outer box's own, one scope out");
    robo_paint_display_list_destroy(list);

    paint_result_dispose(&result);
    paint_end(&fixture);
}

static void test_opacity_nesting_table(void)
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
        const char *outer_text;
        const char *inner_text;
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

    paint_fixture();

    for (row = 0u; row < 6u; row++) {
        paint_fixture_doc_t fixture;
        paint_result_t result;
        char css[512];

        (void)snprintf(css, sizeof(css),
                       OPAC_BASE_CSS
                       " .outer { width: 60px; height: 60px; background-color: #ff0000;"
                       " opacity: %s }"
                       " .inner { width: 20px; height: 20px; background-color: #0000ff;"
                       " opacity: %s }",
                       rows[row].outer_text, rows[row].inner_text);
        paint_begin_laid_out(&fixture,
                             "<html><body><div class=\"outer\"><div class=\"inner\"></div>"
                             "</div></body></html>",
                             css);
        paint_once(&fixture, &result);

        opac_check_int((int)result.stats.opacity_declared, rows[row].declared,
                       "declarations that are not opaque");
        opac_check_int((int)result.stats.opacity_scopes, rows[row].scopes,
                       "scopes that changed the strength in force");
        opac_node_is(&result, paint_box(&fixture, "outer"), rows[row].outer,
                     "the outer box's strength");
        opac_node_is(&result, paint_box(&fixture, "inner"), rows[row].outer * rows[row].inner,
                     "the inner box's product");

        paint_result_dispose(&result);
        paint_end(&fixture);
    }
}

static void test_opacity_sibling_isolation(void)
{
    paint_fixture_doc_t fixture;
    paint_result_t result;
    robo_layout_node_id_t fade;
    robo_layout_node_id_t plain;

    paint_fixture();

    /*
     * Two siblings, the second declaring nothing. The faded one's scope closes before its end
     * marker and therefore long before the next sibling begins, so the plain box's records must
     * carry full strength. A build that left the scope open -- or that accumulated the strength
     * into the traversal's own state instead of passing it down as an argument -- would draw the
     * second sibling faint, which is the leak this case exists for.
     */
    paint_begin_laid_out(&fixture,
                         "<html><body><div class=\"fade\"></div><div class=\"plain\"></div>"
                         "</body></html>",
                         OPAC_BASE_CSS " .fade { width: 20px; height: 20px;"
                         " background-color: #ff0000; opacity: 0.5 }"
                         " .plain { width: 20px; height: 20px; background-color: #00ff00 }");
    paint_once(&fixture, &result);
    fade = paint_box(&fixture, "fade");
    plain = paint_box(&fixture, "plain");

    ROBO_CHECK_EQ_INT((int)result.stats.opacity_scopes, 1);
    opac_node_is(&result, fade, 0.5, "the faded sibling");
    opac_node_is(&result, plain, 1.0, "the plain sibling is untouched by its sibling's scope");

    /* And the plain sibling's drawing sits **after** the pop, in the sequence. */
    ROBO_CHECK(opac_op_index_for_node(&result, ROBO_PAINT_OP_POP_OPACITY, fade, 0u) <
               opac_op_index_for_node(&result, ROBO_PAINT_OP_BEGIN_BOX, plain, 0u));

    paint_result_dispose(&result);
    paint_end(&fixture);
}

static void test_opacity_zero(void)
{
    paint_fixture_doc_t fixture;
    paint_result_t result;
    robo_layout_node_id_t invisible;
    robo_paint_display_list_t *list = NULL;

    paint_fixture();

    /*
     * A strength of `0` is a legal value and a meaningful one, and it is **not** the same state as
     * an empty stack: an ancestor opacity has reduced to nothing draws nothing, which is a fact
     * about the document rather than about the container. The traversal still emits the whole
     * skeleton -- the box is a box, its subtree is walked, and every record says `0` -- so a
     * consumer that suppressed the *work* rather than the *ink* would be the one that broke.
     */
    paint_begin_laid_out(&fixture,
                         "<html><body><div class=\"gone\"><div class=\"kid\"></div></div>"
                         "</body></html>",
                         OPAC_BASE_CSS " .gone { width: 20px; height: 20px;"
                         " background-color: #ff0000; opacity: 0 }"
                         " .kid { width: 10px; height: 10px; background-color: #0000ff }");
    paint_once(&fixture, &result);
    invisible = paint_box(&fixture, "gone");

    ROBO_CHECK_EQ_INT((int)result.stats.opacity_declared, 1);
    ROBO_CHECK_EQ_INT((int)result.stats.opacity_scopes, 1);
    ROBO_CHECK_EQ_INT((int)paint_count_of(&result, ROBO_PAINT_OP_BACKGROUND), 2);
    opac_node_is(&result, invisible, 0.0, "the invisible box's records are zero");
    opac_node_is(&result, paint_box(&fixture, "kid"), 0.0, "and so is its subtree's");
    {
        robo_paint_operation_t push = opac_op_of_kind(&result, ROBO_PAINT_OP_PUSH_OPACITY, 0u);

        opac_check_int(robo_paint_opacity_is_transparent(&push.opacity), 1,
                       "the push is a transparent scope");
        opac_check_int(robo_paint_opacity_is_opaque(&push.opacity), 0,
                       "and not an opaque one -- the two states are different");
    }

    /* The display list keeps it and the builder raises no objection: a scope that establishes a
     * strength *different* from what is in force is a real scope, whatever the strength is. */
    opac_build(result.list, &list, NULL);
    ROBO_CHECK_EQ_INT((int)opac_count_of_kind(list, ROBO_PAINT_DISPLAY_CMD_PUSH_OPACITY), 1);
    robo_paint_display_list_destroy(list);

    paint_result_dispose(&result);
    paint_end(&fixture);
}

static void test_opacity_text(void)
{
    paint_fixture_doc_t fixture;
    paint_result_t result;
    robo_layout_node_id_t faded;
    size_t index;
    int runs = 0;

    paint_fixture();

    /*
     * **The one place a style must not be read twice.** A text run borrows the style of the element
     * that owns the text node, and that element opened its own opacity scope around the run before
     * the run was reached. A painter that read the declaration again at the run would apply it
     * twice, so a `0.5` element's text would draw at `0.25` while its background drew at `0.5` --
     * a difference nothing else in the pipeline would notice, and one this case catches at the
     * record.
     */
    paint_begin_laid_out(&fixture,
                         "<html><body><div class=\"faded\">A</div></body></html>",
                         OPAC_BASE_CSS " .faded { font-size: 100px; color: #ff0000;"
                         " opacity: 0.5 }");
    paint_once(&fixture, &result);
    faded = paint_box(&fixture, "faded");

    ROBO_CHECK_EQ_INT((int)result.stats.text_runs, 1);
    ROBO_CHECK_EQ_INT((int)result.stats.texts, 1);
    ROBO_CHECK_EQ_INT((int)result.stats.opacity_scopes, 1);

    for (index = 0u; index < robo_paint_list_count(result.list); index++) {
        robo_paint_operation_t operation;

        ROBO_CHECK_EQ_INT((int)robo_paint_list_at(result.list, index, &operation), (int)ROBO_OK);
        if (operation.kind != ROBO_PAINT_OP_TEXT) {
            continue;
        }
        runs++;
        /* The run's strength is the element's, not the element's squared. */
        opac_check_record(&operation.opacity, 0.5, "the run's strength");
        ROBO_CHECK(operation.node != faded);
    }
    opac_check_int(runs, 1, "one run was painted");

    /* And the element's own records carry the same strength, so the text and the box it sits in
     * are drawn at one strength rather than at two. */
    opac_node_is(&result, faded, 0.5, "the element that owns the run");

    paint_result_dispose(&result);
    paint_end(&fixture);
}

static void test_opacity_determinism(void)
{
    paint_fixture_doc_t fixture;
    paint_result_t first;
    paint_result_t second;
    robo_paint_display_list_t *list_first = NULL;
    robo_paint_display_list_t *list_second = NULL;
    robo_paint_display_list_stats_t stats_first;
    robo_paint_display_list_stats_t stats_second;
    robo_paint_decoration_stats_t paint_first;
    robo_paint_decoration_stats_t paint_second;
    robo_gfx_surface_t *left = NULL;
    robo_gfx_surface_t *right = NULL;
    size_t baseline = paint_live_blocks();
    int32_t x;
    int32_t y;
    int differences = 0;

    paint_fixture();

    /*
     * The same document painted twice through two independently built pipelines: the record lists
     * must be equal, the display lists equal, the counters agree and the pixels identical. Nothing
     * here reads a pointer or an allocation order, which is what makes "the same DOM and CSS produce
     * the same picture" a statement about the phase rather than about the allocator.
     */
    paint_begin_laid_out(&fixture,
                         "<html><body><div class=\"outer\"><div class=\"inner\"></div></div>"
                         "<div class=\"plain\"></div></body></html>",
                         OPAC_BASE_CSS " .outer { width: 40px; height: 40px;"
                         " background-color: #ff0000; opacity: 0.5 }"
                         " .inner { width: 10px; height: 10px; background-color: #0000ff;"
                         " opacity: 0.5 }"
                         " .plain { width: 20px; height: 20px; background-color: #00ff00 }");
    paint_once(&fixture, &first);
    paint_once(&fixture, &second);

    opac_check_int(robo_paint_list_equal(first.list, second.list), 1,
                   "two passes over one tree produce one list");

    opac_build(first.list, &list_first, &stats_first);
    opac_build(second.list, &list_second, &stats_second);
    opac_check_int(robo_paint_display_list_equal(list_first, list_second), 1,
                   "two builds over one list produce one display list");
    ROBO_CHECK_EQ_INT((int)stats_first.opacity_scopes, (int)stats_second.opacity_scopes);
    ROBO_CHECK_EQ_INT((int)stats_first.commands, (int)stats_second.commands);

    left = opac_surface(800u, 600u, ROBO_GFX_SURFACE_ACCESS_READ_WRITE);
    right = opac_surface(800u, 600u, ROBO_GFX_SURFACE_ACCESS_READ_WRITE);
    opac_paint(list_first, left, NULL, &paint_first);
    opac_paint(list_second, right, NULL, &paint_second);
    ROBO_CHECK_EQ_INT((int)paint_first.faded, (int)paint_second.faded);
    opac_check_int((int)paint_first.opacity_scopes, (int)paint_second.opacity_scopes,
                   "the pass entered the same scopes");
    opac_check_int((int)paint_first.opacity_depth_max, (int)paint_second.opacity_depth_max,
                   "and stood the same depth deep");

    for (y = 0; y < 80; y++) {
        for (x = 0; x < 80; x++) {
            robo_gfx_color_rgba8_t a = opac_read(left, x, y);
            robo_gfx_color_rgba8_t b = opac_read(right, x, y);

            if (a.r != b.r || a.g != b.g || a.b != b.b || a.a != b.a) {
                differences++;
            }
        }
    }
    opac_check_int(differences, 0, "the two pictures are the same picture");

    robo_gfx_surface_release(right);
    robo_gfx_surface_release(left);
    robo_paint_display_list_destroy(list_second);
    robo_paint_display_list_destroy(list_first);
    paint_result_dispose(&second);
    paint_result_dispose(&first);
    paint_end(&fixture);
    ROBO_CHECK_EQ_INT((int)paint_live_blocks(), (int)baseline);
}

static void test_opacity_mutation_isolation(void)
{
    paint_fixture_doc_t fixture;
    paint_result_t result;
    robo_paint_display_list_t *before = NULL;
    robo_paint_display_list_t *after = NULL;
    robo_paint_display_list_stats_t after_stats;
    robo_paint_decoration_stats_t first;
    robo_paint_decoration_stats_t second;
    robo_paint_opacity_t stored;
    robo_gfx_surface_t *one = NULL;
    robo_gfx_surface_t *two = NULL;
    int32_t x;
    int32_t y;
    int differences = 0;

    paint_fixture();

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
    paint_begin_laid_out(&fixture,
                         "<html><body><div class=\"fade\"></div></body></html>",
                         OPAC_BASE_CSS " .fade { width: 40px; height: 40px;"
                         " background-color: #ff0000; opacity: 0.5 }");
    paint_once(&fixture, &result);
    opac_build(result.list, &before, NULL);
    {
        robo_paint_display_command_t snapshot;

        ROBO_CHECK_EQ_INT(opac_command_of_kind(before, ROBO_PAINT_DISPLAY_CMD_PUSH_OPACITY, 0u,
                                               &snapshot),
                          1);
        stored = snapshot.opacity;
        opac_check_record(&stored, 0.5, "the strength the first list was built with");
    }

    one = opac_surface(800u, 600u, ROBO_GFX_SURFACE_ACCESS_READ_WRITE);
    opac_paint(before, one, NULL, &first);

    /* The mutation. */
    robo_paint_list_clear(result.list);
    ROBO_CHECK_EQ_INT((int)robo_paint_list_count(result.list), 0);
    paint_layout(&fixture, 400, 600);
    {
        robo_paint_context_t *context = NULL;

        ROBO_CHECK_EQ_INT((int)robo_paint_context_create(&context, NULL), (int)ROBO_OK);
        paint_run(&fixture, context, result.list);
        robo_paint_context_destroy(context);
    }
    opac_build(result.list, &after, &after_stats);

    /* The mutation is **observable**: the new list differs, so the isolation claim is not
     * vacuous. */
    opac_check_int(robo_paint_display_list_equal(before, after), 0,
                   "the two display lists differ");
    ROBO_CHECK_EQ_INT((int)after_stats.opacity_scopes, 1);
    /* And the new list still carries the same *strength* -- the declaration never changed, because
     * the thing that changed was the geometry. */
    {
        robo_paint_display_command_t fresh;

        ROBO_CHECK_EQ_INT(opac_command_of_kind(after, ROBO_PAINT_DISPLAY_CMD_PUSH_OPACITY, 0u,
                                               &fresh),
                          1);
        opac_check_int(robo_paint_opacity_equal(&fresh.opacity, &stored), 1,
                       "the strength did not change, so a difference between the lists is a "
                       "difference in geometry");
    }

    /* And the first list still paints exactly as it did: a display command is a snapshot, so the
     * pixels it produces cannot follow a source that changed after it was built. */
    two = opac_surface(800u, 600u, ROBO_GFX_SURFACE_ACCESS_READ_WRITE);
    opac_paint(before, two, NULL, &second);
    opac_check_int((int)first.faded, (int)second.faded, "a paint after the sources changed");
    opac_check_int((int)first.draws, (int)second.draws, "and the same number of drawings");
    for (y = 0; y < 60; y++) {
        for (x = 0; x < 60; x++) {
            robo_gfx_color_rgba8_t a = opac_read(one, x, y);
            robo_gfx_color_rgba8_t b = opac_read(two, x, y);

            if (a.r != b.r || a.g != b.g || a.b != b.b || a.a != b.a) {
                differences++;
            }
        }
    }
    opac_check_int(differences, 0, "the first list paints the same picture as before");

    robo_gfx_surface_release(two);
    robo_gfx_surface_release(one);
    robo_paint_display_list_destroy(after);
    robo_paint_display_list_destroy(before);
    paint_result_dispose(&result);
    paint_end(&fixture);
}

/* ------------------------------------------------------------------ */
/* 4. The display list                                                 */
/* ------------------------------------------------------------------ */

static void test_opacity_display_commands(void)
{
    paint_fixture_doc_t fixture;
    paint_result_t result;
    robo_layout_node_id_t fade;
    robo_paint_display_list_t *list = NULL;
    robo_paint_display_list_stats_t stats;
    robo_paint_display_command_t push;
    robo_paint_display_command_t pop;
    robo_paint_display_command_t background;
    robo_gfx_surface_t *surface = NULL;
    robo_paint_decoration_stats_t paint_stats;
    size_t begin;
    size_t push_at;
    size_t pop_at;
    size_t end;
    size_t background_at;
    size_t baseline = paint_live_blocks();

    paint_fixture();

    paint_begin_laid_out(&fixture,
                         "<html><body><div class=\"fade\"></div></body></html>",
                         OPAC_BASE_CSS " .fade { width: 40px; height: 40px;"
                         " background-color: #ff0000; opacity: 0.5 }");
    paint_once(&fixture, &result);
    fade = paint_box(&fixture, "fade");
    opac_build(result.list, &list, &stats);

    ROBO_CHECK_EQ_INT((int)stats.opacity_scopes, 1);
    ROBO_CHECK_EQ_INT(opac_command_of_kind(list, ROBO_PAINT_DISPLAY_CMD_PUSH_OPACITY, 0u, &push),
                      1);
    ROBO_CHECK_EQ_INT(opac_command_of_kind(list, ROBO_PAINT_DISPLAY_CMD_POP_OPACITY, 0u, &pop),
                      1);
    ROBO_CHECK_EQ_INT(opac_command_of_kind(list, ROBO_PAINT_DISPLAY_CMD_DRAW_BACKGROUND, 0u,
                                           &background),
                      1);

    /* The scope's commands name the box and its depth, exactly as its marker does, and the
     * rectangle they act on is the **border box** -- the whole of what the scope tints. (The clip
     * pair's is the padding box, because that is the boundary the box's overflow resolves to; the
     * two pairs name two different rectangles of one box, each the true answer to its own
     * question.) */
    opac_check_int((int)push.node, (int)fade, "the push names the box");
    opac_check_int((int)pop.node, (int)fade, "and so does the pop");
    ROBO_CHECK_EQ_INT((int)push.depth, (int)pop.depth);
    opac_check_int((int)push.rect.x, 0, "the scope's rectangle is the border box");
    opac_check_int((int)push.rect.width, 40, "on the horizontal axis");
    opac_check_int((int)push.rect.height, 40, "and the vertical one");
    opac_check_int((int)background.rect.width, (int)push.rect.width,
                   "and it is the rectangle the background fills");

    /* The command carries the strength as a snapshot, and the pop carries the strength its push
     * carried, so the pair is checkable from the list alone. */
    opac_check_record(&push.opacity, 0.5, "the push command");
    opac_check_record(&pop.opacity, 0.5, "the pop command");
    opac_check_record(&background.opacity, 0.5, "the background inside the scope");

    /* The pair's ordering: the push after the node marker and before the drawing, the pop after
     * the drawing and before the end marker. */
    begin = opac_index_of_kind_for_node(list, ROBO_PAINT_DISPLAY_CMD_BEGIN_NODE, fade, 0u);
    push_at = opac_index_of_kind_for_node(list, ROBO_PAINT_DISPLAY_CMD_PUSH_OPACITY, fade, 0u);
    pop_at = opac_index_of_kind_for_node(list, ROBO_PAINT_DISPLAY_CMD_POP_OPACITY, fade, 0u);
    end = opac_index_of_kind_for_node(list, ROBO_PAINT_DISPLAY_CMD_END_NODE, fade, 0u);
    background_at = opac_index_of_kind_for_node(list, ROBO_PAINT_DISPLAY_CMD_DRAW_BACKGROUND,
                                                fade, 0u);
    ROBO_CHECK(begin != (size_t)-1 && push_at != (size_t)-1 && pop_at != (size_t)-1 &&
               end != (size_t)-1 && background_at != (size_t)-1);
    opac_check_int((int)push_at, (int)(begin + 1u), "the push follows the node marker");
    opac_check_int(background_at > push_at, 1, "the drawing is inside the scope");
    opac_check_int((int)(pop_at + 1u), (int)end, "and the pop precedes the end marker");

    /* The paint pass: the scope is entered, the raster's opacity is set for the drawing, and the
     * pass's own counters say one scope and one faded drawing. */
    surface = opac_surface(64u, 64u, ROBO_GFX_SURFACE_ACCESS_READ_WRITE);
    opac_paint(list, surface, NULL, &paint_stats);
    opac_check_int((int)paint_stats.opacity_scopes, 1, "the pass entered the scope");
    opac_check_int((int)paint_stats.opacity_depth_max, 1, "one deep");
    opac_check_int((int)paint_stats.faded, 1, "one drawing was drawn below full strength");
    robo_gfx_surface_release(surface);

    robo_paint_display_list_destroy(list);
    paint_result_dispose(&result);
    paint_end(&fixture);
    ROBO_CHECK_EQ_INT((int)paint_live_blocks(), (int)baseline);
}

static void test_opacity_hand_built_scope(void)
{
    robo_paint_list_t *records = NULL;
    robo_paint_display_list_t *list = NULL;
    robo_paint_display_list_stats_t stats;

    paint_fixture();

    /*
     * A **hand-built** record list that opens and closes one scope around one box. The builder has
     * no tree to consult, so this is the case that shows the checks are about the *sequence* rather
     * than about the traversal: a properly nested, narrowing, self-identifying pair is accepted
     * from a caller that has no document at all.
     */
    ROBO_CHECK_EQ_INT((int)robo_paint_list_create(&records, NULL), (int)ROBO_OK);
    opac_append_record(records, ROBO_PAINT_OP_BEGIN_BOX, 7u, 0u, opac_boxes(0, 0, 10, 10),
                       robo_paint_opacity_opaque());
    opac_append_record(records, ROBO_PAINT_OP_PUSH_OPACITY, 7u, 0u, opac_boxes(0, 0, 10, 10),
                       opac_of(0.5));
    opac_append_record(records, ROBO_PAINT_OP_BACKGROUND, 7u, 0u, opac_boxes(0, 0, 10, 10),
                       opac_of(0.5));
    opac_append_record(records, ROBO_PAINT_OP_CONTENT, 7u, 0u, opac_boxes(0, 0, 10, 10),
                       opac_of(0.5));
    opac_append_record(records, ROBO_PAINT_OP_POP_OPACITY, 7u, 0u, opac_boxes(0, 0, 10, 10),
                       opac_of(0.5));
    opac_append_record(records, ROBO_PAINT_OP_END_BOX, 7u, 0u, opac_boxes(0, 0, 10, 10),
                       robo_paint_opacity_opaque());

    opac_build(records, &list, &stats);
    ROBO_CHECK_EQ_INT((int)stats.opacity_scopes, 1);
    ROBO_CHECK_EQ_INT((int)stats.commands, 6);
    ROBO_CHECK_EQ_INT((int)opac_count_of_kind(list, ROBO_PAINT_DISPLAY_CMD_PUSH_OPACITY), 1);
    ROBO_CHECK_EQ_INT((int)opac_count_of_kind(list, ROBO_PAINT_DISPLAY_CMD_POP_OPACITY), 1);
    {
        robo_paint_display_command_t push;
        robo_paint_display_command_t background;

        ROBO_CHECK_EQ_INT(opac_command_of_kind(list, ROBO_PAINT_DISPLAY_CMD_PUSH_OPACITY, 0u,
                                               &push),
                          1);
        ROBO_CHECK_EQ_INT(opac_command_of_kind(list, ROBO_PAINT_DISPLAY_CMD_DRAW_BACKGROUND, 0u,
                                               &background),
                          1);
        opac_check_record(&push.opacity, 0.5, "the hand-built push");
        opac_check_record(&background.opacity, 0.5, "the hand-built drawing");
    }
    robo_paint_display_list_destroy(list);

    /* And a hand-built list with **no** scope commands keeps working exactly as it did before the
     * phase: its drawings' own strengths are the only statement of opacity it has. */
    robo_paint_list_clear(records);
    opac_append_record(records, ROBO_PAINT_OP_BEGIN_BOX, 7u, 0u, opac_boxes(0, 0, 10, 10),
                       robo_paint_opacity_opaque());
    opac_append_record(records, ROBO_PAINT_OP_BACKGROUND, 7u, 0u, opac_boxes(0, 0, 10, 10),
                       opac_of(0.5));
    opac_append_record(records, ROBO_PAINT_OP_CONTENT, 7u, 0u, opac_boxes(0, 0, 10, 10),
                       robo_paint_opacity_opaque());
    opac_append_record(records, ROBO_PAINT_OP_END_BOX, 7u, 0u, opac_boxes(0, 0, 10, 10),
                       robo_paint_opacity_opaque());
    opac_build(records, &list, &stats);
    ROBO_CHECK_EQ_INT((int)stats.opacity_scopes, 0);
    ROBO_CHECK_EQ_INT((int)stats.commands, 4);
    robo_paint_display_list_destroy(list);

    robo_paint_list_destroy(records);
}

/**
 * Build @p records, assert the build is **refused**, and give the caller the refusal.
 *
 * A helper rather than the three lines written out eleven times, and the two claims it makes
 * together are the ones a refused build owes its caller: an error code, a message, and a list that
 * is **empty** rather than the previous build's.
 */
static void opac_refuse(const robo_paint_list_t *records, const char *what)
{
    robo_paint_display_list_t *list = NULL;
    robo_paint_diag_t diag;
    robo_result_t status = opac_build_expect_refusal(records, &list, &diag);

    if (status != ROBO_ERROR) {
        robo_test_fail(__FILE__, __LINE__, "%s: expected a refusal, got %d", what, (int)status);
    }
    if (!diag.message) {
        robo_test_fail(__FILE__, __LINE__, "%s: the refusal carries no message", what);
    }
    opac_check_int((int)diag.domain, (int)ROBO_PAINT_DOMAIN_DISPLAY_LIST, what);
    opac_check_int((int)robo_paint_display_list_count(list), 0, "a refused build leaves no list");
    robo_paint_display_list_destroy(list);
}

static void test_opacity_builder_refusals(void)
{
    robo_paint_list_t *records = NULL;

    paint_fixture();

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
    ROBO_CHECK_EQ_INT((int)robo_paint_list_create(&records, NULL), (int)ROBO_OK);
    opac_append_record(records, ROBO_PAINT_OP_PUSH_OPACITY, 7u, 0u, opac_boxes(0, 0, 10, 10),
                       opac_of(0.5));
    opac_refuse(records, "a push with no open box");
    robo_paint_list_destroy(records);

    /* A push naming a box that is **not the innermost open one**. */
    records = opac_open_box(7u);
    opac_append_record(records, ROBO_PAINT_OP_PUSH_OPACITY, 8u, 0u, opac_boxes(0, 0, 10, 10),
                       opac_of(0.5));
    opac_refuse(records, "a push naming another box");
    robo_paint_list_destroy(records);

    /* A push at a **depth that does not match** the box's. */
    records = opac_open_box(7u);
    opac_append_record(records, ROBO_PAINT_OP_PUSH_OPACITY, 7u, 3u, opac_boxes(0, 0, 10, 10),
                       opac_of(0.5));
    opac_refuse(records, "a push at the wrong depth");
    robo_paint_list_destroy(records);

    /* A push **after the box's content boundary**. */
    records = opac_open_box(7u);
    opac_append_record(records, ROBO_PAINT_OP_PUSH_OPACITY, 7u, 0u, opac_boxes(0, 0, 10, 10),
                       opac_of(0.5));
    opac_refuse(records, "a push after the content boundary");
    robo_paint_list_destroy(records);

    /*
     * A push **after the box has begun drawing** -- the one rule that distinguishes this pair from
     * the clip's, and the one a build that placed the scope at the content boundary would break.
     * The list is the case above with the content boundary removed, so the *only* thing wrong with
     * it is that a decoration was painted before the scope opened.
     */
    records = opac_painted_box(7u);
    opac_append_record(records, ROBO_PAINT_OP_PUSH_OPACITY, 7u, 0u, opac_boxes(0, 0, 10, 10),
                       opac_of(0.5));
    opac_refuse(records, "a push after the box's first decoration");
    robo_paint_list_destroy(records);

    /*
     * A push of an **opaque** record: a scope that establishes nothing.
     *
     * Refused one layer **earlier** than the cases around it, and that is the interesting part:
     * `robo_paint_operation_is_valid` holds every record to the two closing rules, so such a record
     * never enters a list at all -- the assertion is therefore at the record, and the *list* is
     * asserted untouched, which is the same two-part claim the builder's refusals make one boundary
     * down. A builder reading such a record would be reading one that came from somewhere else.
     */
    records = opac_empty_box();
    {
        robo_paint_operation_t scope;
        robo_paint_diag_t diag;

        robo_paint_operation_init(&scope);
        scope.kind = ROBO_PAINT_OP_PUSH_OPACITY;
        scope.node = 7u;
        scope.boxes = opac_boxes(0, 0, 10, 10);
        scope.opacity = robo_paint_opacity_opaque();
        opac_check_int(robo_paint_operation_is_valid(&scope), 0,
                       "an opaque scope is not a record");
        robo_paint_diag_reset(&diag);
        opac_check_int((int)robo_paint_list_append(records, &scope, &diag),
                       (int)ROBO_INVALID_ARGUMENT, "and a list refuses it");
        ROBO_CHECK(diag.message != NULL);
        opac_check_int((int)robo_paint_list_count(records), 0,
                       "the refused record left the list empty");
    }
    robo_paint_list_destroy(records);

    /* A box that opens **two** opacity scopes. */
    records = opac_scoped_box(7u, 0.5);
    opac_append_record(records, ROBO_PAINT_OP_PUSH_OPACITY, 7u, 0u, opac_boxes(0, 0, 10, 10),
                       opac_of(0.25));
    opac_pop_box(records, 7u, 0.25);
    opac_close_box(records, 7u);
    opac_refuse(records, "a box that opens two scopes");
    robo_paint_list_destroy(records);

    /*
     * A push **stronger** than the strength it is inside. Opacity composes by multiplication, so a
     * scope an ancestor reduced can only reduce further -- a push that raised it would be a box
     * whose `opacity` cancelled an ancestor's, which CSS has no way to express.
     */
    records = opac_scoped_box(7u, 0.25);
    opac_append_record(records, ROBO_PAINT_OP_BEGIN_BOX, 9u, 1u, opac_boxes(0, 0, 10, 10),
                       opac_of(0.25));
    opac_append_record(records, ROBO_PAINT_OP_PUSH_OPACITY, 9u, 1u, opac_boxes(0, 0, 10, 10),
                       opac_of(0.5));
    opac_refuse(records, "a scope stronger than the one it is inside");
    robo_paint_list_destroy(records);

    /* A **pop with nothing to pop**: no scope was ever opened. */
    records = opac_open_box(7u);
    opac_pop_box(records, 7u, 0.5);
    opac_refuse(records, "a pop with no scope");
    robo_paint_list_destroy(records);

    /* A pop whose strength is **not the one its push carried**. The pair is compared, not
     * trusted. */
    records = opac_scoped_box(7u, 0.5);
    opac_pop_box(records, 7u, 0.25);
    opac_close_box(records, 7u);
    opac_refuse(records, "a pop that closes with another strength");
    robo_paint_list_destroy(records);

    /* A box that **closes with its scope still open**. The box stack alone would not catch it: the
     * frame is about to be discarded, and the scope it left open is a command with no matching pop
     * until the end of the build -- a different and later failure. */
    records = opac_scoped_box(7u, 0.5);
    opac_close_box(records, 7u);
    opac_refuse(records, "a box closing with a scope open");
    robo_paint_list_destroy(records);

    /* A **drawing inside a scope carrying a different strength** -- the rule that makes the scope
     * the authority for what is drawn inside it. */
    records = opac_scoped_box(7u, 0.5);
    opac_append_record(records, ROBO_PAINT_OP_BACKGROUND, 7u, 0u, opac_boxes(0, 0, 10, 10),
                       opac_of(0.25));
    opac_pop_box(records, 7u, 0.5);
    opac_close_box(records, 7u);
    opac_refuse(records, "a command inside a scope carrying another strength");
    robo_paint_list_destroy(records);

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
    records = opac_scoped_box(7u, 0.5);
    opac_refuse(records, "a list that ends with a scope still open");
    robo_paint_list_destroy(records);

    /* And the **valid** list, built last: the builder left no scope behind across the refusals,
     * which is the property a caller reusing one depends on. */
    records = opac_scoped_box(7u, 0.5);
    opac_pop_box(records, 7u, 0.5);
    opac_close_box(records, 7u);
    {
        robo_paint_display_list_t *list = NULL;
        robo_paint_display_list_stats_t stats;

        opac_build(records, &list, &stats);
        ROBO_CHECK_EQ_INT((int)stats.opacity_scopes, 1);
        ROBO_CHECK_EQ_INT((int)stats.commands, 6);
        robo_paint_display_list_destroy(list);
    }
    robo_paint_list_destroy(records);
}

static void test_opacity_pass_refusals(void)
{
    robo_paint_display_list_t *list = NULL;
    robo_gfx_surface_t *surface = NULL;
    robo_paint_diag_t diag;
    robo_result_t status;
    size_t baseline = paint_live_blocks();

    paint_fixture();

    /*
     * A pop with nothing to pop. The list's own validity rule cannot see it -- each command is a
     * record -- so this is the pass's contract, and it refuses rather than skipping: a consumer that
     * closed a strength that was never established would go on drawing at a strength that is not in
     * force.
     */
    ROBO_CHECK_EQ_INT((int)robo_paint_display_list_create(&list, NULL), (int)ROBO_OK);
    {
        robo_paint_display_command_t command;

        robo_paint_display_command_init(&command);
        command.kind = ROBO_PAINT_DISPLAY_CMD_POP_OPACITY;
        command.node = 1u;
        command.depth = 0u;
        command.rect = opac_rect(0, 0, 10, 10);
        command.opacity = opac_of(0.5);
        ROBO_CHECK_EQ_INT((int)robo_paint_display_list_append(list, &command, NULL),
                          (int)ROBO_OK);
    }
    surface = opac_surface(64u, 64u, ROBO_GFX_SURFACE_ACCESS_READ_WRITE);
    robo_paint_diag_reset(&diag);
    status = robo_paint_decoration_paint(list, surface, NULL, NULL, &diag);
    ROBO_CHECK_EQ_INT((int)status, (int)ROBO_NOT_FOUND);
    ROBO_CHECK_EQ_INT((int)diag.domain, (int)ROBO_PAINT_DOMAIN_OPACITY);
    ROBO_CHECK(diag.message != NULL);
    robo_gfx_surface_release(surface);

    /* A missing target: the pass's argument check, unchanged by the phase. */
    robo_paint_diag_reset(&diag);
    status = robo_paint_decoration_paint(list, NULL, NULL, NULL, &diag);
    ROBO_CHECK_EQ_INT((int)status, (int)ROBO_INVALID_ARGUMENT);

    /* An empty list is a legitimate pass that draws nothing, with no scopes and no fades. */
    robo_paint_display_list_clear(list);
    surface = opac_surface(16u, 16u, ROBO_GFX_SURFACE_ACCESS_READ_WRITE);
    {
        robo_paint_decoration_stats_t stats;

        opac_paint(list, surface, NULL, &stats);
        opac_check_int((int)stats.commands, 0, "an empty list has no commands");
        opac_check_int((int)stats.opacity_scopes, 0, "and no scopes");
        opac_check_int((int)stats.faded, 0, "and nothing was faded");
    }
    robo_gfx_surface_release(surface);

    robo_paint_display_list_destroy(list);
    ROBO_CHECK_EQ_INT((int)paint_live_blocks(), (int)baseline);
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
static robo_gfx_surface_t *opac_picture(const char *html,
                                        const char *css,
                                        robo_font_face_t *face,
                                        robo_paint_decoration_stats_t *out_stats)
{
    paint_fixture_doc_t fixture;
    paint_result_t result;
    robo_paint_display_list_t *list = NULL;
    robo_gfx_surface_t *surface = opac_surface(800u, 600u, ROBO_GFX_SURFACE_ACCESS_READ_WRITE);

    paint_begin_laid_out(&fixture, html, css);
    paint_once(&fixture, &result);
    opac_build(result.list, &list, NULL);
    opac_paint(list, surface, face, out_stats);
    robo_paint_display_list_destroy(list);
    paint_result_dispose(&result);
    paint_end(&fixture);
    return surface;
}

#define OPAC_BACK_CSS " .back { width: 100px; height: 100px; background-color: #ffffff }"

/* The text case's two sheets, macros rather than variables because the other sheets are
 * concatenated around them and a string literal is what a concatenation needs. */
#define OPAC_TEXT_CSS " .txt { font-size: 100px; color: #ff0000 }"
#define OPAC_TEXT_FADED_CSS " .txt { font-size: 100px; color: #ff0000; opacity: 0.5 }"

static void test_opacity_background_pixels(void)
{
    static const char *const html =
        "<html><body><div class=\"back\"><div class=\"front\"></div></div></body></html>";
    robo_gfx_surface_t *plain = NULL;
    robo_gfx_surface_t *faded = NULL;
    robo_paint_decoration_stats_t plain_stats;
    robo_paint_decoration_stats_t faded_stats;
    robo_gfx_color_rgba8_t expected;
    robo_gfx_color_rgba8_t actual;
    size_t baseline = paint_live_blocks();

    paint_fixture();

    /*
     * The brief's own document: a white parent with a red child inside it, painted twice -- once as
     * written and once with the child at half strength. The two documents differ in **one
     * declaration** and therefore in nothing else: `opacity` does not affect layout, so the geometry
     * is identical and every pixel's *position* is comparable.
     */
    plain = opac_picture(html,
                         OPAC_BASE_CSS OPAC_BACK_CSS
                         " .front { width: 50px; height: 50px; background-color: #ff0000 }",
                         NULL, &plain_stats);
    faded = opac_picture(html,
                         OPAC_BASE_CSS OPAC_BACK_CSS
                         " .front { width: 50px; height: 50px; background-color: #ff0000;"
                         " opacity: 0.5 }",
                         NULL, &faded_stats);

    /* The control: red inside the child, white inside the parent and outside the child, and nothing
     * at all outside the parent. */
    opac_pixel_is(plain, 10, 10, OPAC_RED, "the child's own fill");
    opac_pixel_is(plain, 80, 80, OPAC_WHITE, "the parent's fill outside the child");
    opac_pixel_is(plain, 200, 200, OPAC_UNTOUCHED, "outside the painted document");

    /* The parent is **not** in a scope, so it is drawn at full strength -- the mutation isolates one
     * declaration, not the whole document. */
    opac_pixel_is(faded, 80, 80, OPAC_WHITE, "the unscoped parent is untouched");
    opac_pixel_is(faded, 200, 200, OPAC_UNTOUCHED, "and nothing leaked outside it");

    /*
     * And the child is the composite of what it was. The exact answer is `(255, 127, 127, 255)` --
     * `mul_div255(255, 128)` is 128, so the blend removes 128 from both of the white channels --
     * but the assertion is written through the blend door, so what is pinned is the *relationship*
     * and the three inline checks below name the number.
     */
    expected = opac_expected(OPAC_RED, OPAC_WHITE, 0.5);
    actual = opac_read(faded, 10, 10);
    opac_pixel_is(faded, 10, 10, expected, "the faded child is the composite of the opaque one");
    opac_check_int((int)actual.r, 255, "and it is still red");
    opac_check_int((int)actual.g, 127, "at `255 - mul_div255(255, 128)`");
    opac_check_int((int)actual.b, 127, "on both of the white channels");
    opac_check_int((int)actual.a, 255, "and still opaque, because the parent is");

    /* And it is **drawn**, not skipped: every pixel of its 50x50 box still carries ink, which is
     * what distinguishes "faded" from "not painted at all" -- the same distinction the record
     * layer's `is_transparent` draws, checked here at the surface. */
    opac_check_int(opac_any_ink(faded, 0, 0, 50, 50), 1,
                   "the faded child's whole box carries ink");
    opac_check_int(opac_any_ink(faded, 150, 150, 170, 170), 0,
                   "and the space outside the parent carries none");

    /* The pass's counters: one faded drawing, and the scope entered once. */
    opac_check_int((int)plain_stats.faded, 0, "the control fades nothing");
    opac_check_int((int)faded_stats.faded, 1, "and the faded document fades exactly one drawing");

    robo_gfx_surface_release(faded);
    robo_gfx_surface_release(plain);
    ROBO_CHECK_EQ_INT((int)paint_live_blocks(), (int)baseline);
}

static void test_opacity_border_pixels(void)
{
    static const char *const html =
        "<html><body><div class=\"back\"><div class=\"box\"></div></div></body></html>";
    robo_gfx_surface_t *plain = NULL;
    robo_gfx_surface_t *faded = NULL;
    robo_gfx_color_rgba8_t expected;
    robo_gfx_color_rgba8_t actual;
    size_t baseline = paint_live_blocks();

    paint_fixture();

    /*
     * A **border band** over a white parent, at half strength. The band is the second drawing kind,
     * and the one that proves the scope's placement is right: if the pair opened at the box's
     * *content boundary* (the clip's placement) the box's own border would be drawn at full strength
     * and this pixel would be pure red.
     */
    plain = opac_picture(html,
                         OPAC_BASE_CSS OPAC_BACK_CSS
                         " .box { width: 40px; height: 40px; border: 4px solid #ff0000 }",
                         NULL, NULL);
    faded = opac_picture(html,
                         OPAC_BASE_CSS OPAC_BACK_CSS
                         " .box { width: 40px; height: 40px; border: 4px solid #ff0000;"
                         " opacity: 0.5 }",
                         NULL, NULL);

    /* The control's top band is the four rows above the box's content: a sample two pixels in. */
    opac_pixel_is(plain, 10, 2, OPAC_RED, "the control's top border band");
    /* Inside the box there is nothing: it declares no background, so the parent's white shows. */
    opac_pixel_is(plain, 10, 20, OPAC_WHITE, "inside the box, nothing is drawn");

    opac_pixel_is(faded, 10, 20, OPAC_WHITE, "and the faded version leaves the same interior");
    expected = opac_expected(OPAC_RED, OPAC_WHITE, 0.5);
    actual = opac_read(faded, 10, 2);
    opac_pixel_is(faded, 10, 2, expected, "the faded band is the composite of the opaque one");
    opac_check_int((int)actual.g, 127, "the band's green channel is halved");
    opac_check_int((int)actual.a, 255, "and it is still opaque over an opaque parent");

    robo_gfx_surface_release(faded);
    robo_gfx_surface_release(plain);
    ROBO_CHECK_EQ_INT((int)paint_live_blocks(), (int)baseline);
}

static void test_opacity_text_pixels(void)
{
    static const char *const html =
        "<html><body><div class=\"back\"><div class=\"txt\">A</div></div></body></html>";
    robo_gfx_surface_t *plain = NULL;
    robo_gfx_surface_t *faded = NULL;
    opac_face_t face;
    robo_paint_decoration_stats_t plain_stats;
    robo_paint_decoration_stats_t faded_stats;
    robo_gfx_color_rgba8_t opaque;
    robo_gfx_color_rgba8_t expected;
    robo_gfx_color_rgba8_t actual;
    int32_t ink_x = 0;
    int32_t ink_y = 0;
    size_t baseline = paint_live_blocks();

    paint_fixture();
    opac_face_create(&face);

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
    plain = opac_picture(html, OPAC_BASE_CSS OPAC_BACK_CSS OPAC_TEXT_CSS, face.face,
                         &plain_stats);
    opac_check_int((int)plain_stats.no_font, 0, "the control drew with the face");
    ROBO_CHECK_EQ_INT(opac_first_ink(plain, 0, 0, 100, 100, &ink_x, &ink_y), 1);
    opaque = opac_read(plain, ink_x, ink_y);
    opac_check_int((int)opaque.a, 255, "the control's glyph ink is opaque");

    faded = opac_picture(html, OPAC_BASE_CSS OPAC_BACK_CSS OPAC_TEXT_FADED_CSS, face.face,
                         &faded_stats);

    /* The claim: the faded run put the composite of that colour where the opaque one put the
     * colour. A run's ink overhangs its advance box, which is why the *pixel* is the sample rather
     * than the rectangle. */
    expected = opac_expected(opaque, OPAC_WHITE, 0.5);
    actual = opac_read(faded, ink_x, ink_y);
    if (actual.r != expected.r || actual.g != expected.g || actual.b != expected.b ||
        actual.a != expected.a) {
        robo_test_fail(__FILE__, __LINE__,
                       "the faded glyph at %d,%d: expected %d,%d,%d,%d, got %d,%d,%d,%d",
                       (int)ink_x, (int)ink_y, (int)expected.r, (int)expected.g,
                       (int)expected.b, (int)expected.a, (int)actual.r, (int)actual.g,
                       (int)actual.b, (int)actual.a);
    }

    /* And the counters: the run really was drawn, the scope was entered and the drawing was faded.
     * `glyphs` is what makes "text respects opacity" a statement about a glyph rather than about a
     * command. */
    opac_check_int((int)plain_stats.glyphs, 1, "the control drew one glyph");
    opac_check_int((int)faded_stats.glyphs, 1, "and so did the faded document");
    opac_check_int((int)faded_stats.opacity_scopes, 1, "the faded run stood in one scope");
    opac_check_int((int)faded_stats.faded, 1, "and its drawing was faded");

    opac_face_destroy(&face);
    robo_gfx_surface_release(faded);
    robo_gfx_surface_release(plain);
    ROBO_CHECK_EQ_INT((int)paint_live_blocks(), (int)baseline);
}

static void test_opacity_no_opacity_unchanged(void)
{
    robo_gfx_surface_t *surface = NULL;
    robo_paint_decoration_stats_t stats;
    size_t baseline = paint_live_blocks();

    paint_fixture();

    /*
     * A document that declares no opacity, painted: the raster's own opacity is never set to
     * anything but its default, so `faded` is **zero** and the pixels are the ones the build
     * produced before this phase. This is the case that would catch a pass that set the strength
     * from a scope that is not there -- an empty stack answers full strength, and a store of `255`
     * is what a build with no opacity in it performs, which is the raster's own default.
     */
    surface = opac_picture("<html><body><div class=\"plain\"></div></body></html>",
                           OPAC_BASE_CSS " .plain { width: 20px; height: 20px;"
                           " background-color: #ff0000 }",
                           NULL, &stats);
    opac_pixel_is(surface, 5, 5, OPAC_RED, "an unscoped fill is exactly its colour");
    opac_check_int((int)stats.faded, 0, "and nothing was faded");
    opac_check_int((int)stats.opacity_scopes, 0, "in no scope at all");
    robo_gfx_surface_release(surface);
    ROBO_CHECK_EQ_INT((int)paint_live_blocks(), (int)baseline);
}

static void test_opacity_group_deviation(void)
{
    static const char *const html =
        "<html><body><div class=\"group\"><div class=\"one\"></div><div class=\"two\"></div>"
        "</div></body></html>";
    robo_gfx_surface_t *surface = NULL;
    robo_paint_decoration_stats_t stats;
    robo_gfx_color_rgba8_t blue_half;
    robo_gfx_color_rgba8_t red_half;
    robo_gfx_color_rgba8_t overlap;
    robo_gfx_color_rgba8_t per_drawing;
    robo_gfx_color_rgba8_t group;
    size_t baseline = paint_live_blocks();

    paint_fixture();

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
    surface = opac_picture(html,
                           OPAC_BASE_CSS " .group { width: 60px; height: 60px; opacity: 0.5 }"
                           " .one { width: 40px; height: 40px; background-color: #0000ff }"
                           " .two { width: 40px; height: 40px; background-color: #ff0000;"
                           " position: relative; top: -20px; left: 20px }",
                           NULL, &stats);

    /*
     * The three regions the two inks make. `.one` is at (0,0)-(40,40); `.two` is drawn at
     * (20,20)-(60,60) after the relative offset and after `.one` in document order.
     */
    blue_half = opac_read(surface, 5, 5);    /* `.one` alone */
    red_half = opac_read(surface, 50, 30);   /* `.two` alone */
    overlap = opac_read(surface, 30, 30);    /* both */

    opac_check_int((int)blue_half.r, 0, "the blue child alone has no red");
    opac_check_int((int)blue_half.b, 255, "and keeps its blue");
    opac_check_int((int)blue_half.a, 128, "at half alpha");
    opac_check_int((int)red_half.r, 255, "the red child alone is red");
    opac_check_int((int)red_half.a, 128, "at half alpha");

    /* This build's answer, computed through the blend door from the two single-ink pixels: each
     * ink at its own half, composited in document order. */
    per_drawing = robo_gfx_composite_src_over_opacity(red_half, blue_half, 255u);
    if (overlap.r != per_drawing.r || overlap.g != per_drawing.g || overlap.b != per_drawing.b ||
        overlap.a != per_drawing.a) {
        robo_test_fail(__FILE__, __LINE__,
                       "the overlap: expected the per-drawing composite %d,%d,%d,%d, got "
                       "%d,%d,%d,%d",
                       (int)per_drawing.r, (int)per_drawing.g, (int)per_drawing.b,
                       (int)per_drawing.a, (int)overlap.r, (int)overlap.g, (int)overlap.b,
                       (int)overlap.a);
    }

    /* And the group answer, which is **not** what a surface here holds: opaque red flattened, then
     * halved once. The blue the overlap keeps -- `85`, where the group form leaves `0` -- is the
     * deviation, and it is the assertion below that makes it a fact rather than a claim. */
    group = robo_gfx_composite_src_over_opacity(OPAC_RED, OPAC_UNTOUCHED, OPAC_HALF_BYTE);
    opac_check_int((int)group.b, 0, "the group form leaves no blue in the overlap");
    opac_check_int((int)overlap.b, 85, "this build leaves the blue below the red, at its half");
    opac_check_int((int)(overlap.b == group.b), 0,
                   "and those two are the deviation, asserted as a disagreement");
    opac_check_int((int)stats.faded, 2, "two drawings, each faded once");

    robo_gfx_surface_release(surface);
    ROBO_CHECK_EQ_INT((int)paint_live_blocks(), (int)baseline);
}

static void test_opacity_end_to_end(void)
{
    static const char *const html =
        "<html><body><div class=\"outer\"><div class=\"inner\">A</div></div></body></html>";
    robo_gfx_surface_t *surface = NULL;
    opac_face_t face;
    robo_paint_decoration_stats_t paint_stats;
    robo_gfx_color_rgba8_t outer_pixel;
    robo_gfx_color_rgba8_t inner_pixel;

    paint_fixture();
    opac_face_create(&face);

    /*
     * Nested opacity, a background per box and a text run, through the whole pipeline -- the
     * capstone document, with all three drawing kinds of this release inside one another and a real
     * face to turn the run's characters into glyphs.
     */
    surface = opac_picture(html,
                           OPAC_BASE_CSS " .outer { width: 100px; height: 100px;"
                           " background-color: #ffffff; opacity: 0.5 }"
                           " .inner { width: 60px; height: 60px; background-color: #ff0000;"
                           " opacity: 0.5; font-size: 100px; color: #0000ff }",
                           face.face, &paint_stats);

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
    outer_pixel = opac_read(surface, 90, 90);
    inner_pixel = opac_read(surface, 55, 55);

    opac_check_int((int)paint_stats.faded, 3, "three drawings were drawn below full strength");
    opac_check_int((int)paint_stats.texts, 1, "one text run reached the surface");
    opac_check_int((int)paint_stats.glyphs, 1, "as one glyph");
    opac_check_int((int)paint_stats.no_font, 0, "with the face the caller supplied");
    opac_check_int((int)outer_pixel.r, 255, "the outer fill is white");
    opac_check_int((int)outer_pixel.g, 255, "on all three channels");
    opac_check_int((int)outer_pixel.a, 128, "at half alpha, over nothing");
    opac_check_int((int)inner_pixel.r, 255, "the inner fill is still red");
    opac_check_int((int)(inner_pixel.g < 255), 1, "with something of the white below it gone");
    opac_check_int((int)(inner_pixel.g > 127), 1, "and more of it left than a plain half would");
    opac_check_int((int)inner_pixel.g, (int)inner_pixel.b, "the two channels that are not red agree");

    /*
     * The faded surface is a **real image**, not just an assertion: RoboGraphics' own encoder --
     * the one Phase 6 shipped, not a second one -- turns it into a PNG, whose signature and width
     * are read back here. That is what makes the blend inspectable rather than merely asserted: a
     * reader who wants to *see* the half-white with the quarter-red inside it runs this case and
     * opens the buffer.
     */
    {
        robo_gfx_encode_options_t encode_opts;
        robo_gfx_encoded_buffer_t encoded;
        static const uint8_t png_signature[8] = { 0x89u, 'P', 'N', 'G', 0x0du, 0x0au, 0x1au, 0x0au };

        memset(&encoded, 0, sizeof(encoded));
        robo_gfx_encode_options_init(&encode_opts);
        ROBO_CHECK_EQ_INT(robo_gfx_surface_encode(surface, &encode_opts, &encoded), ROBO_OK);
        ROBO_CHECK(encoded.size > 8u);
        ROBO_CHECK_EQ_INT(memcmp(encoded.data, png_signature, sizeof(png_signature)), 0);
        ROBO_CHECK_EQ_INT((int)((uint32_t)encoded.data[16] << 24 | (uint32_t)encoded.data[17] << 16 |
                                (uint32_t)encoded.data[18] << 8 | (uint32_t)encoded.data[19]),
                          (int)robo_gfx_surface_width(surface));
        robo_gfx_encoded_buffer_release(&encoded);
    }

    opac_face_destroy(&face);
    robo_gfx_surface_release(surface);
}

/* ------------------------------------------------------------------ */
/* 6. The dumps                                                        */
/* ------------------------------------------------------------------ */

static void test_opacity_dump(void)
{
    paint_fixture_doc_t fixture;
    paint_result_t result;
    robo_paint_display_list_t *list = NULL;
    char line[ROBO_PAINT_DUMP_LINE_MAX + 1u];
    char display_line[ROBO_PAINT_DISPLAY_DUMP_LINE_MAX + 1u];
    size_t length = 0u;
    size_t baseline = paint_live_blocks();

    paint_fixture();

    paint_begin_laid_out(&fixture,
                         "<html><body><div class=\"fade\"></div></body></html>",
                         OPAC_BASE_CSS " .fade { width: 40px; height: 40px;"
                         " background-color: #ff0000; opacity: 0.5 }");
    paint_once(&fixture, &result);

    /*
     * The **record** line, with the strength printed for every kind exactly as the clip is: what is
     * in force is a question every record answers, so a field that appeared only on a scope would
     * leave a drawing's strength unreadable from the drawing a dump is being read for.
     */
    {
        robo_paint_operation_t push = opac_op_of_kind(&result, ROBO_PAINT_OP_PUSH_OPACITY, 0u);
        robo_paint_operation_t background = opac_op_of_kind(&result, ROBO_PAINT_OP_BACKGROUND, 0u);
        robo_paint_operation_t begin = opac_op_of_kind(&result, ROBO_PAINT_OP_BEGIN_BOX, 0u);

        memset(line, 0, sizeof(line));
        ROBO_CHECK_EQ_INT((int)robo_paint_operation_write(&push, line, sizeof(line), &length,
                                                          NULL),
                          (int)ROBO_OK);
        ROBO_CHECK(length > 0u);
        ROBO_CHECK(strstr(line, "push-opacity") != NULL);
        ROBO_CHECK(strstr(line, "opacity=0.50") != NULL);

        memset(line, 0, sizeof(line));
        ROBO_CHECK_EQ_INT((int)robo_paint_operation_write(&background, line, sizeof(line),
                                                          &length, NULL),
                          (int)ROBO_OK);
        ROBO_CHECK(strstr(line, "opacity=0.50") != NULL);

        /* And a fully opaque record prints its own value rather than nothing, so a reader never has
         * to ask whether a missing field means "one" or "not printed". */
        memset(line, 0, sizeof(line));
        ROBO_CHECK_EQ_INT((int)robo_paint_operation_write(&begin, line, sizeof(line), &length,
                                                          NULL),
                          (int)ROBO_OK);
        ROBO_CHECK(strstr(line, "opacity=1.00") != NULL);
    }

    /* The **display** line, with the commands naming the scope and its strength. */
    opac_build(result.list, &list, NULL);
    {
        robo_paint_display_command_t push;
        robo_paint_display_command_t pop;

        ROBO_CHECK_EQ_INT(opac_command_of_kind(list, ROBO_PAINT_DISPLAY_CMD_PUSH_OPACITY, 0u,
                                               &push),
                          1);
        ROBO_CHECK_EQ_INT(opac_command_of_kind(list, ROBO_PAINT_DISPLAY_CMD_POP_OPACITY, 0u,
                                               &pop),
                          1);
        memset(display_line, 0, sizeof(display_line));
        ROBO_CHECK_EQ_INT((int)robo_paint_display_command_write(&push, display_line,
                                                                sizeof(display_line), &length,
                                                                NULL),
                          (int)ROBO_OK);
        ROBO_CHECK(strstr(display_line, "push-opacity") != NULL);
        ROBO_CHECK(strstr(display_line, "opacity=0.50") != NULL);
        memset(display_line, 0, sizeof(display_line));
        ROBO_CHECK_EQ_INT((int)robo_paint_display_command_write(&pop, display_line,
                                                                sizeof(display_line), &length,
                                                                NULL),
                          (int)ROBO_OK);
        ROBO_CHECK(strstr(display_line, "pop-opacity") != NULL);
        ROBO_CHECK(strstr(display_line, "opacity=0.50") != NULL);
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
        robo_paint_operation_t wide_record;
        robo_paint_display_command_t wide_command;

        robo_paint_operation_init(&wide_record);
        wide_record.kind = ROBO_PAINT_OP_BORDER_BOTTOM;
        wide_record.node = (robo_layout_node_id_t)999999u;
        wide_record.depth = 99u;
        wide_record.boxes = opac_boxes(INT32_MIN, INT32_MIN, INT32_MAX, INT32_MAX);
        wide_record.origin = robo_gfx_point(0, 0);
        wide_record.opacity = opac_of(0.5);
        wide_record.color = robo_css_color_make(255u, 255u, 255u, 255u);
        wide_record.style = ROBO_CSS_BORDER_STYLE_SOLID;
        wide_record.clip.horizontal.mode = ROBO_CSS_OVERFLOW_HIDDEN;
        wide_record.clip.horizontal.start = INT32_MIN;
        wide_record.clip.horizontal.end = INT32_MAX;
        wide_record.clip.vertical.mode = ROBO_CSS_OVERFLOW_HIDDEN;
        wide_record.clip.vertical.start = INT32_MIN;
        wide_record.clip.vertical.end = INT32_MAX;

        memset(line, 0, sizeof(line));
        ROBO_CHECK_EQ_INT((int)robo_paint_operation_write(&wide_record, line, sizeof(line),
                                                          &length, NULL),
                          (int)ROBO_OK);
        ROBO_CHECK(length < (size_t)ROBO_PAINT_DUMP_LINE_MAX);

        robo_paint_display_command_init(&wide_command);
        wide_command.kind = ROBO_PAINT_DISPLAY_CMD_DRAW_BORDER;
        wide_command.node = wide_record.node;
        wide_command.depth = wide_record.depth;
        wide_command.rect = opac_rect(INT32_MIN, INT32_MIN, INT32_MAX, INT32_MAX);
        wide_command.clip = wide_record.clip;
        wide_command.opacity = wide_record.opacity;
        wide_command.color = wide_record.color;
        wide_command.style = wide_record.style;
        memset(display_line, 0, sizeof(display_line));
        ROBO_CHECK_EQ_INT((int)robo_paint_display_command_write(&wide_command, display_line,
                                                                sizeof(display_line), &length,
                                                                NULL),
                          (int)ROBO_OK);
        ROBO_CHECK(length < (size_t)ROBO_PAINT_DISPLAY_DUMP_LINE_MAX);
    }

    robo_paint_display_list_destroy(list);
    paint_result_dispose(&result);
    paint_end(&fixture);
    ROBO_CHECK_EQ_INT((int)paint_live_blocks(), (int)baseline);
}

/* ------------------------------------------------------------------ */
/* 7. Empty and malformed                                              */
/* ------------------------------------------------------------------ */

static void test_opacity_empty_and_malformed(void)
{
    robo_paint_list_t *records = NULL;
    robo_paint_display_list_t *list = NULL;
    robo_paint_display_list_stats_t stats;
    robo_paint_operation_t operation;
    robo_paint_display_command_t command;
    robo_paint_diag_t diag;
    volatile double zero = 0.0;
    size_t baseline = paint_live_blocks();

    paint_fixture();

    /* An empty record list is a legitimate build: a document that draws nothing has an empty
     * display list and no scopes. */
    ROBO_CHECK_EQ_INT((int)robo_paint_list_create(&records, NULL), (int)ROBO_OK);
    opac_build(records, &list, &stats);
    ROBO_CHECK_EQ_INT((int)stats.opacity_scopes, 0);
    ROBO_CHECK_EQ_INT((int)robo_paint_display_list_count(list), 0);
    robo_paint_display_list_destroy(list);
    list = NULL;
    robo_paint_list_destroy(records);

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

        ROBO_CHECK_EQ_INT((int)robo_paint_list_create(&records, NULL), (int)ROBO_OK);
        for (index = 0u; index < 4u; index++) {
            robo_paint_operation_init(&operation);
            operation.kind = ROBO_PAINT_OP_BACKGROUND;
            operation.node = 1u;
            operation.depth = 0u;
            operation.boxes = opac_boxes(0, 0, 10, 10);
            operation.color = robo_css_color_make(255u, 0u, 0u, 255u);
            /* The fourth case is a NaN, built from the volatile zero so no optimiser can fold it
             * into a constant. */
            operation.opacity.value = (index == 3u) ? (zero / zero) : bad[index];
            opac_check_int(robo_paint_operation_is_valid(&operation), 0,
                           "an out-of-range record is not a record");
            robo_paint_diag_reset(&diag);
            ROBO_CHECK_EQ_INT((int)robo_paint_list_append(records, &operation, &diag),
                              (int)ROBO_INVALID_ARGUMENT);
        }
        ROBO_CHECK_EQ_INT((int)robo_paint_list_count(records), 0);
        robo_paint_list_destroy(records);
        records = NULL;
    }

    /* The same rule one stage along, on a display command. */
    ROBO_CHECK_EQ_INT((int)robo_paint_display_list_create(&list, NULL), (int)ROBO_OK);
    robo_paint_display_command_init(&command);
    command.kind = ROBO_PAINT_DISPLAY_CMD_DRAW_BACKGROUND;
    command.node = 1u;
    command.rect = opac_rect(0, 0, 10, 10);
    command.color = robo_css_color_make(255u, 0u, 0u, 255u);
    command.opacity.value = 2.0;
    opac_check_int(robo_paint_display_command_is_valid(&command), 0,
                   "an out-of-range command is not a command");
    robo_paint_diag_reset(&diag);
    ROBO_CHECK_EQ_INT((int)robo_paint_display_list_append(list, &command, &diag),
                      (int)ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT((int)robo_paint_display_list_count(list), 0);

    /* A scope command whose strength is opaque is refused by the vocabulary: a scope that
     * establishes nothing is a command a consumer could only skip. */
    robo_paint_display_command_init(&command);
    command.kind = ROBO_PAINT_DISPLAY_CMD_PUSH_OPACITY;
    command.node = 1u;
    command.rect = opac_rect(0, 0, 10, 10);
    command.opacity = robo_paint_opacity_opaque();
    opac_check_int(robo_paint_display_command_is_valid(&command), 0,
                   "an opaque scope command is not a command");
    command.opacity = opac_of(0.5);
    opac_check_int(robo_paint_display_command_is_valid(&command), 1,
                   "a real scope command is a command");
    robo_paint_display_list_destroy(list);
    list = NULL;

    /* An empty document: the pass succeeds, nothing is declared and no scope opens. */
    {
        paint_fixture_doc_t fixture;
        paint_result_t result;

        paint_begin_laid_out(&fixture, "<html><body></body></html>", OPAC_BASE_CSS);
        paint_once(&fixture, &result);
        ROBO_CHECK_EQ_INT((int)result.stats.opacity_declared, 0);
        ROBO_CHECK_EQ_INT((int)result.stats.opacity_scopes, 0);
        opac_list_is(&result, 1.0, "an empty document declares nothing");
        paint_result_dispose(&result);
        paint_end(&fixture);
    }

    /* A **childless** faded box: the scope is a statement about the box rather than about its
     * subtree, exactly as its content boundary is. */
    {
        paint_fixture_doc_t fixture;
        paint_result_t result;

        paint_begin_laid_out(&fixture,
                             "<html><body><div class=\"solo\"></div></body></html>",
                             OPAC_BASE_CSS " .solo { width: 10px; height: 0px; opacity: 0 }");
        paint_once(&fixture, &result);
        ROBO_CHECK_EQ_INT((int)result.stats.opacity_scopes, 1);
        ROBO_CHECK_EQ_INT((int)paint_count_of(&result, ROBO_PAINT_OP_PUSH_OPACITY), 1);
        ROBO_CHECK_EQ_INT((int)paint_count_of(&result, ROBO_PAINT_OP_POP_OPACITY), 1);
        paint_result_dispose(&result);
        paint_end(&fixture);
    }

    /* A document with no computed styles at all declares nothing, and every record is opaque --
     * "missing style" answers the same way "no declaration" does. */
    {
        paint_fixture_doc_t fixture;
        paint_result_t result;

        paint_begin_laid_out(&fixture, "<html><body><p>x</p></body></html>", OPAC_BASE_CSS);
        paint_once(&fixture, &result);
        opac_list_is(&result, 1.0, "a document with no declarations is fully opaque");
        paint_result_dispose(&result);
        paint_end(&fixture);
    }

    ROBO_CHECK_EQ_INT((int)paint_live_blocks(), (int)baseline);
}

/* ------------------------------------------------------------------ */
/* Registration                                                        */
/* ------------------------------------------------------------------ */

void robo_test_register_paint_opacity(void)
{
    robo_test_register("paint.opacity.record", test_opacity_record);
    robo_test_register("paint.opacity.record_safety", test_opacity_record_safety);
    robo_test_register("paint.opacity.byte_conversion", test_opacity_byte_conversion);
    robo_test_register("paint.opacity.stack", test_opacity_stack);
    robo_test_register("paint.opacity.stack_growth", test_opacity_stack_growth);
    robo_test_register("paint.opacity.default", test_opacity_default);
    robo_test_register("paint.opacity.explicit_one", test_opacity_explicit_one);
    robo_test_register("paint.opacity.declared", test_opacity_declared);
    robo_test_register("paint.opacity.scope_placement", test_opacity_scope_placement);
    robo_test_register("paint.opacity.nested", test_opacity_nested);
    robo_test_register("paint.opacity.nesting_table", test_opacity_nesting_table);
    robo_test_register("paint.opacity.sibling_isolation", test_opacity_sibling_isolation);
    robo_test_register("paint.opacity.zero", test_opacity_zero);
    robo_test_register("paint.opacity.text", test_opacity_text);
    robo_test_register("paint.opacity.determinism", test_opacity_determinism);
    robo_test_register("paint.opacity.mutation_isolation", test_opacity_mutation_isolation);
    robo_test_register("paint.opacity.display_commands", test_opacity_display_commands);
    robo_test_register("paint.opacity.hand_built_scope", test_opacity_hand_built_scope);
    robo_test_register("paint.opacity.builder_refusals", test_opacity_builder_refusals);
    robo_test_register("paint.opacity.pass_refusals", test_opacity_pass_refusals);
    robo_test_register("paint.opacity.background_pixels", test_opacity_background_pixels);
    robo_test_register("paint.opacity.border_pixels", test_opacity_border_pixels);
    robo_test_register("paint.opacity.text_pixels", test_opacity_text_pixels);
    robo_test_register("paint.opacity.no_opacity_unchanged", test_opacity_no_opacity_unchanged);
    robo_test_register("paint.opacity.group_deviation", test_opacity_group_deviation);
    robo_test_register("paint.opacity.end_to_end", test_opacity_end_to_end);
    robo_test_register("paint.opacity.dump", test_opacity_dump);
    robo_test_register("paint.opacity.empty_and_malformed", test_opacity_empty_and_malformed);
}
