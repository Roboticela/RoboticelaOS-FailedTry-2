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
 *                                  run at, and the same `paint_local_boxes_match_layout` call
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
 *                                  `.notdef` and **counted** (`glyphs_missing`), the measurer's
 *                                  own rule one layer along;
 *
 *   **no font is a number**        a caller that supplies no face paints a textless document
 *                                  identically and is *told* about a document with text rather
 *                                  than handed a silently blank frame (`no_font`).
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
#define TEXT_RED robo_gfx_rgba8(0xff, 0x00, 0x00, 255)
#define TEXT_WHITE robo_gfx_rgba8(0xff, 0xff, 0xff, 255)

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
} text_synth_table_t;

static void text_put_u8(uint8_t **p, uint8_t v) { *(*p)++ = v; }
static void text_put_u16(uint8_t **p, uint16_t v)
{
    *(*p)++ = (uint8_t)(v >> 8);
    *(*p)++ = (uint8_t)v;
}
static void text_put_i16(uint8_t **p, int16_t v) { text_put_u16(p, (uint16_t)v); }
static void text_put_u32(uint8_t **p, uint32_t v)
{
    *(*p)++ = (uint8_t)(v >> 24);
    *(*p)++ = (uint8_t)(v >> 16);
    *(*p)++ = (uint8_t)(v >> 8);
    *(*p)++ = (uint8_t)v;
}
static void text_put_i64(uint8_t **p, int64_t v)
{
    int i;

    for (i = 7; i >= 0; i--) {
        text_put_u8(p, (uint8_t)((uint64_t)v >> (i * 8)));
    }
}

static void text_write_search_fields(uint8_t *hdr, uint16_t num_tables)
{
    uint16_t pow2 = 1u;
    uint16_t entry = 0u;
    uint8_t *p;

    while ((uint16_t)(pow2 * 2u) <= num_tables) {
        pow2 = (uint16_t)(pow2 * 2u);
        entry++;
    }
    p = hdr + 4;
    text_put_u16(&p, num_tables);
    text_put_u16(&p, (uint16_t)(pow2 * 16u));
    text_put_u16(&p, entry);
    text_put_u16(&p, (uint16_t)(num_tables * 16u - pow2 * 16u));
}

static size_t text_build_sfnt(uint8_t *out,
                              size_t out_cap,
                              uint32_t version,
                              const text_synth_table_t *tables,
                              uint32_t n)
{
    size_t dir_end = 12u + (size_t)n * 16u;
    size_t cursor;
    size_t need = dir_end;
    uint32_t i;

    for (i = 0u; i < n; i++) {
        need += ((size_t)tables[i].length + 3u) & ~(size_t)3u;
    }
    if (need > out_cap) {
        return 0u;
    }
    memset(out, 0, need);
    {
        uint8_t *p = out;

        text_put_u32(&p, version);
    }
    text_write_search_fields(out, (uint16_t)n);
    cursor = dir_end;
    for (i = 0u; i < n; i++) {
        uint32_t pad = (uint32_t)(((size_t)tables[i].length + 3u) & ~(size_t)3u);
        uint8_t *p = out + 12u + (size_t)i * 16u;

        text_put_u32(&p, tables[i].tag);
        text_put_u32(&p, 0u);
        text_put_u32(&p, (uint32_t)cursor);
        text_put_u32(&p, tables[i].length);
        memcpy(out + cursor, tables[i].data, tables[i].length);
        cursor += pad;
    }
    return cursor;
}

static size_t text_build_head(uint8_t *b, uint16_t upem)
{
    uint8_t *p = b;

    text_put_u16(&p, 1);
    text_put_u16(&p, 0);
    text_put_u32(&p, 0x00010000u);
    text_put_u32(&p, 0u);
    text_put_u32(&p, ROBO_FONT_HEAD_MAGIC);
    text_put_u16(&p, 0u);
    text_put_u16(&p, upem);
    text_put_i64(&p, 0);
    text_put_i64(&p, 0);
    text_put_i16(&p, -50);
    text_put_i16(&p, -200);
    text_put_i16(&p, 1000);
    text_put_i16(&p, 800);
    text_put_u16(&p, 0u);
    text_put_u16(&p, 8u);
    text_put_i16(&p, 2);
    text_put_i16(&p, 1); /* long loca, so loca entries are u32 */
    text_put_i16(&p, 0);
    return (size_t)(p - b);
}

static size_t text_build_hhea(uint8_t *b, uint16_t n_h_metrics)
{
    uint8_t *p = b;
    int i;

    text_put_u16(&p, 1);
    text_put_u16(&p, 0);
    text_put_i16(&p, 800);  /* ascender */
    text_put_i16(&p, -200); /* descender */
    text_put_i16(&p, 90);   /* line gap */
    text_put_u16(&p, 900);
    text_put_i16(&p, -10);
    text_put_i16(&p, -20);
    text_put_i16(&p, 880);
    text_put_i16(&p, 1);
    text_put_i16(&p, 0);
    text_put_i16(&p, 0);
    for (i = 0; i < 4; i++) {
        text_put_i16(&p, 0);
    }
    text_put_i16(&p, 0);
    text_put_u16(&p, n_h_metrics);
    return (size_t)(p - b);
}

static size_t text_build_maxp(uint8_t *b, uint16_t num_glyphs)
{
    uint8_t *p = b;
    int i;

    text_put_u32(&p, 0x00010000u);
    text_put_u16(&p, num_glyphs);
    text_put_u16(&p, 8);
    text_put_u16(&p, 2);
    for (i = 0; i < 12; i++) {
        text_put_u16(&p, 0);
    }
    return (size_t)(p - b);
}

static size_t text_build_hmtx(uint8_t *b, const uint16_t *advances, uint16_t n)
{
    uint8_t *p = b;
    uint16_t i;

    for (i = 0u; i < n; i++) {
        text_put_u16(&p, advances[i]);
        text_put_i16(&p, (int16_t)(i + 1));
    }
    return (size_t)(p - b);
}

/** `n + 1` long `loca` entries from @p offsets. */
static size_t text_build_loca(uint8_t *loca, const uint32_t *offsets, uint16_t n)
{
    uint8_t *p = loca;
    uint16_t i;

    for (i = 0u; i <= n; i++) {
        text_put_u32(&p, offsets[i]);
    }
    return (size_t)(p - loca);
}

/** One glyph: a square from (0, 0) to (100, 100) in FUnits. */
static size_t text_build_square_glyf(uint8_t *b)
{
    uint8_t *p = b;

    text_put_i16(&p, 1);                   /* numberOfContours */
    text_put_i16(&p, 0);                   /* xMin */
    text_put_i16(&p, 0);                   /* yMin */
    text_put_i16(&p, TEXT_FACE_SQUARE_UNITS); /* xMax */
    text_put_i16(&p, TEXT_FACE_SQUARE_UNITS); /* yMax */
    text_put_u16(&p, 3);                   /* endPtsOfContours[0] */
    text_put_u16(&p, 0);                   /* instructionLength */
    text_put_u8(&p, (uint8_t)(ROBO_FONT_GLYF_FLAG_ON_CURVE |
                              ROBO_FONT_GLYF_FLAG_X_SAME_OR_POSITIVE |
                              ROBO_FONT_GLYF_FLAG_Y_SAME_OR_POSITIVE));
    text_put_u8(&p, (uint8_t)(ROBO_FONT_GLYF_FLAG_ON_CURVE | ROBO_FONT_GLYF_FLAG_X_SHORT |
                              ROBO_FONT_GLYF_FLAG_X_SAME_OR_POSITIVE |
                              ROBO_FONT_GLYF_FLAG_Y_SAME_OR_POSITIVE));
    text_put_u8(&p, (uint8_t)(ROBO_FONT_GLYF_FLAG_ON_CURVE | ROBO_FONT_GLYF_FLAG_Y_SHORT |
                              ROBO_FONT_GLYF_FLAG_X_SAME_OR_POSITIVE |
                              ROBO_FONT_GLYF_FLAG_Y_SAME_OR_POSITIVE));
    text_put_u8(&p, (uint8_t)(ROBO_FONT_GLYF_FLAG_ON_CURVE | ROBO_FONT_GLYF_FLAG_X_SHORT |
                              ROBO_FONT_GLYF_FLAG_Y_SAME_OR_POSITIVE));
    text_put_u8(&p, TEXT_FACE_SQUARE_UNITS);
    text_put_u8(&p, TEXT_FACE_SQUARE_UNITS);
    text_put_u8(&p, TEXT_FACE_SQUARE_UNITS);
    return (size_t)(p - b);
}

/**
 * A one-subtable cmap holding a format-4 segment table, mapping `first..last` contiguously to
 * glyph ids `1` upward (delta `1 - first`).
 *
 * The required `0xFFFF` segment is appended, because a format-4 table without it is not one.
 */
static size_t text_build_cmap(uint8_t *b, uint16_t first, uint16_t last)
{
    uint8_t *p = b;
    uint8_t *length_ptr;
    uint8_t *subtable = b + 12;

    text_put_u16(&p, 0);   /* version */
    text_put_u16(&p, 1);   /* numTables */
    text_put_u16(&p, 3);   /* Windows */
    text_put_u16(&p, 1);   /* Unicode BMP */
    text_put_u32(&p, 12u); /* subtable offset */

    text_put_u16(&p, 4);    /* format */
    length_ptr = p;
    text_put_u16(&p, 0);    /* length, patched below */
    text_put_u16(&p, 0);    /* language */
    text_put_u16(&p, 4);    /* segCountX2: two segments */
    text_put_u16(&p, 4);    /* searchRange */
    text_put_u16(&p, 1);    /* entrySelector */
    text_put_u16(&p, 0);    /* rangeShift */
    text_put_u16(&p, last);
    text_put_u16(&p, 0xFFFFu);
    text_put_u16(&p, 0); /* reservedPad */
    text_put_u16(&p, first);
    text_put_u16(&p, 0xFFFFu);
    text_put_i16(&p, (int16_t)(1 - (int32_t)first)); /* idDelta[0]: 'A' -> glyph 1 */
    text_put_i16(&p, 1);                             /* idDelta[1]: the 0xFFFF segment */
    text_put_u16(&p, 0);                             /* idRangeOffset[0] */
    text_put_u16(&p, 0);                             /* idRangeOffset[1] */

    {
        uint16_t length = (uint16_t)(p - subtable);

        length_ptr[0] = (uint8_t)(length >> 8);
        length_ptr[1] = (uint8_t)length;
    }
    return (size_t)(p - b);
}

/** What a face plus its source cost a caller, so a case can release both the way it built
 *  them. */
typedef struct text_face {
    robo_font_source_t *source;
    robo_font_face_t *face;
} text_face_t;

/**
 * Build the synthetic face, with 'A' -> glyph 1 and 'B' -> glyph 2.
 *
 * Both mapped glyphs carry the square outline (glyph 0, `.notdef`, is empty), so a mapped code
 * point draws pixels and an unmapped one draws none -- which is what makes the pixel and the
 * missing-glyph cases different claims rather than the same one twice.
 */
static void text_face_create(text_face_t *out)
{
    uint8_t head[64], hhea[64], maxp[64], hmtx[64], loca[64], glyf[128], cmap[64];
    uint8_t font[1024];
    static const uint16_t advances[TEXT_FACE_NUM_GLYPHS] = {
        (uint16_t)TEXT_FACE_ADVANCE_NOTDEF,
        (uint16_t)TEXT_FACE_ADVANCE_GLYPH,
        (uint16_t)TEXT_FACE_ADVANCE_GLYPH
    };
    text_synth_table_t tables[7];
    uint32_t nt = 0u;
    size_t glyph_size;
    size_t n;
    uint8_t *gp;

    out->source = NULL;
    out->face = NULL;

    glyph_size = text_build_square_glyf(glyf);
    /* Two copies: one for each mapped glyph, with glyph 0 left empty. */
    gp = glyf + glyph_size;
    (void)text_build_square_glyf(glyf); /* already written; keep the second copy explicit */
    memcpy(gp, glyf, glyph_size);

    {
        const uint32_t offsets[TEXT_FACE_NUM_GLYPHS + 1u] = {
            0u, 0u, (uint32_t)glyph_size, (uint32_t)(glyph_size * 2u)
        };

        (void)text_build_loca(loca, offsets, (uint16_t)TEXT_FACE_NUM_GLYPHS);
    }

    tables[nt].tag = ROBO_FONT_TAG_HEAD;
    tables[nt].data = head;
    tables[nt].length = (uint32_t)text_build_head(head, (uint16_t)TEXT_FACE_UPEM);
    nt++;
    tables[nt].tag = ROBO_FONT_TAG_HHEA;
    tables[nt].data = hhea;
    tables[nt].length = (uint32_t)text_build_hhea(hhea, (uint16_t)TEXT_FACE_NUM_GLYPHS);
    nt++;
    tables[nt].tag = ROBO_FONT_TAG_MAXP;
    tables[nt].data = maxp;
    tables[nt].length = (uint32_t)text_build_maxp(maxp, (uint16_t)TEXT_FACE_NUM_GLYPHS);
    nt++;
    tables[nt].tag = ROBO_FONT_TAG_HMTX;
    tables[nt].data = hmtx;
    tables[nt].length = (uint32_t)text_build_hmtx(hmtx, advances,
                                                 (uint16_t)TEXT_FACE_NUM_GLYPHS);
    nt++;
    tables[nt].tag = ROBO_FONT_TAG_CMAP;
    tables[nt].data = cmap;
    /* 'A' (0x41) -> glyph 1, 'B' (0x42) -> glyph 2. */
    tables[nt].length = (uint32_t)text_build_cmap(cmap, 0x41u, 0x42u);
    nt++;
    tables[nt].tag = ROBO_FONT_TAG_LOCA;
    tables[nt].data = loca;
    tables[nt].length = (uint32_t)((TEXT_FACE_NUM_GLYPHS + 1u) * 4u);
    nt++;
    tables[nt].tag = ROBO_FONT_TAG_GLYF;
    tables[nt].data = glyf;
    tables[nt].length = (uint32_t)(glyph_size * 2u);
    nt++;

    n = text_build_sfnt(font, sizeof(font), ROBO_FONT_SFNT_VERSION_TRUE, tables, nt);
    ROBO_CHECK(n > 0u);
    ROBO_CHECK_EQ_INT(robo_font_source_create_memory_copy(font, n, &out->source), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_font_face_create(out->source, 0, &out->face), ROBO_OK);
}

static void text_face_destroy(text_face_t *face)
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

/** A CPU surface for a decoration pass: owned, RGBA8, zero-filled (so "untouched" is
 *  transparent and an unpainted pixel is distinguishable from a black one). */
static robo_gfx_surface_t *text_surface(uint32_t width, uint32_t height)
{
    robo_gfx_surface_desc_t desc;
    robo_gfx_surface_t *surface = NULL;

    robo_gfx_surface_desc_init(&desc);
    desc.width = width;
    desc.height = height;
    desc.format = ROBO_GFX_FORMAT_RGBA8;
    desc.access = ROBO_GFX_SURFACE_ACCESS_READ_WRITE;
    ROBO_CHECK_EQ_INT(robo_gfx_surface_create(&desc, &surface), ROBO_OK);
    ROBO_CHECK(surface != NULL);
    return surface;
}

/** Build a display list from @p records, asserting the build succeeds. */
static void text_build(const paint_result_t *records,
                       robo_paint_display_list_t **out_list,
                       robo_paint_display_list_stats_t *out_stats)
{
    robo_paint_display_list_builder_t *builder = NULL;
    robo_paint_diag_t diag;
    robo_result_t status;

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

/** Paint @p list into @p target with @p face (may be NULL), asserting the pass succeeds. */
static void text_paint(const robo_paint_display_list_t *list,
                       robo_gfx_surface_t *target,
                       robo_font_face_t *face,
                       robo_paint_decoration_stats_t *out_stats)
{
    robo_paint_diag_t diag;
    robo_result_t status;

    robo_paint_diag_reset(&diag);
    status = robo_paint_decoration_paint(list, target, face, out_stats, &diag);
    if (status != ROBO_OK) {
        robo_test_fail(__FILE__, __LINE__, "the decoration pass refused the list: %s (%s:%d)",
                       diag.message ? diag.message : "(no message)",
                       diag.file ? diag.file : "?", diag.line);
    }
}

/** The @p nth command of kind @p kind, or 0 when there is none. */
static int text_command_of_kind(const robo_paint_display_list_t *list,
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

/** The index of the @p nth command of kind @p kind, or `(size_t)-1`. */
static size_t text_index_of_kind(const robo_paint_display_list_t *list,
                                 robo_paint_display_command_kind_t kind,
                                 size_t nth)
{
    size_t index;
    size_t seen = 0u;

    for (index = 0u; index < robo_paint_display_list_count(list); index++) {
        robo_paint_display_command_t command;

        if (robo_paint_display_list_at(list, index, &command) != ROBO_OK) {
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
static void text_read_span(const robo_paint_text_pool_t *pool,
                           robo_paint_text_span_t span,
                           uint32_t *out,
                           size_t count,
                           const char *what)
{
    size_t got = 0u;

    memset(out, 0, count * sizeof(*out));
    if (robo_paint_text_pool_read(pool, span, out, count, &got, NULL) != ROBO_OK) {
        robo_test_fail(__FILE__, __LINE__, "%s: the span could not be read", what);
        return;
    }
    if (got != count) {
        robo_test_fail(__FILE__, __LINE__, "%s: expected %d code points, read %d", what,
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
static void text_expect_points(const robo_paint_text_pool_t *pool,
                               robo_paint_text_span_t span,
                               const uint32_t *expected,
                               size_t count,
                               const char *what)
{
    uint32_t buffer[64];
    size_t index;

    if (count > 64u) {
        robo_test_fail(__FILE__, __LINE__, "%s: the expectation is longer than the buffer",
                       what);
        return;
    }
    if (span.count != (uint32_t)count) {
        robo_test_fail(__FILE__, __LINE__, "%s: expected %d code points, span holds %d", what,
                       (int)count, (int)span.count);
        return;
    }
    text_read_span(pool, span, buffer, count, what);
    for (index = 0u; index < count; index++) {
        if (buffer[index] != expected[index]) {
            robo_test_fail(__FILE__, __LINE__, "%s: code point %d is U+%04X, expected U+%04X",
                           what, (int)index, (unsigned)buffer[index],
                           (unsigned)expected[index]);
            return;
        }
    }
}

/** Assert the @p span of @p pool holds exactly the ASCII text @p expected. */
static void text_expect_chars(const robo_paint_text_pool_t *pool,
                              robo_paint_text_span_t span,
                              const char *expected,
                              const char *what)
{
    uint32_t buffer[64];
    size_t length = strlen(expected);
    size_t index;

    if (length > 64u) {
        robo_test_fail(__FILE__, __LINE__, "%s: the expectation is longer than the buffer",
                       what);
        return;
    }
    for (index = 0u; index < length; index++) {
        buffer[index] = (uint32_t)(unsigned char)expected[index];
    }
    text_expect_points(pool, span, buffer, length, what);
}

/** Non-zero when @p haystack contains @p needle. */
static int text_contains(const char *haystack, const char *needle)
{
    return haystack != NULL && needle != NULL && strstr(haystack, needle) != NULL;
}

/* ------------------------------------------------------------------ */
/* 1. The record                                                       */
/* ------------------------------------------------------------------ */

static void test_text_record_shape(void)
{
    paint_fixture_doc_t fixture;
    paint_result_t result;
    robo_paint_operation_t operation;
    robo_paint_text_run_t run;
    robo_layout_baseline_metrics_t baseline;
    robo_layout_text_style_t style;
    robo_paint_text_pool_t *pool;
    const robo_css_computed_style_t *computed;
    int32_t expected_baseline = 0;

    paint_begin_laid_out(&fixture, "<html><body><div class=\"box\">Hello</div></body></html>",
                         TEXT_BASE_CSS " .box { width: 300px; height: 60px }");
    paint_once(&fixture, &result);

    /* Exactly one run was reached and it produced one record. */
    ROBO_CHECK_EQ_INT((int)result.stats.text_runs, 1);
    ROBO_CHECK_EQ_INT((int)result.stats.texts, 1);
    ROBO_CHECK_EQ_INT((int)result.stats.text_skipped, 0);
    ROBO_CHECK_EQ_INT((int)paint_count_of(&result, ROBO_PAINT_OP_TEXT), 1);

    operation = paint_require(&result, ROBO_PAINT_OP_TEXT);
    ROBO_CHECK_EQ_INT((int)robo_paint_operation_kind(&operation),
                      (int)ROBO_PAINT_OP_TEXT);
    ROBO_CHECK_EQ_INT(robo_paint_operation_is_drawing(&operation), 1);
    ROBO_CHECK_EQ_INT(robo_paint_operation_is_structural(&operation), 0);
    ROBO_CHECK_EQ_INT(robo_paint_operation_is_valid(&operation), 1);

    /* The record's local boxes are what layout wrote -- the same door the box suites use. */
    paint_local_boxes_match_layout(&fixture, operation);

    /* The characters are the document's, read through the list's own pool. */
    run = robo_paint_operation_text(&operation);
    ROBO_CHECK_EQ_INT(robo_paint_text_run_is_valid(&run), 1);
    pool = robo_paint_list_text_pool(result.list);
    ROBO_CHECK(pool != NULL);
    ROBO_CHECK_EQ_INT(robo_paint_text_pool_contains(pool, run.span), 1);
    ROBO_CHECK_EQ_INT((int)robo_paint_text_pool_count(pool), 5);
    text_expect_chars(pool, run.span, "Hello", "the run's characters");

    /* The baseline is layout's, through its own door. */
    ROBO_CHECK_EQ_INT(robo_layout_object_baseline(fixture.tree, operation.node, &baseline, NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_layout_baseline_offset(&baseline, &expected_baseline), ROBO_OK);
    ROBO_CHECK_EQ_INT(run.baseline_offset, expected_baseline);
    ROBO_CHECK(robo_paint_text_run_baseline_offset(&run) == expected_baseline);

    /* The font inputs are the measurer's, by value. */
    computed = robo_layout_object_style(fixture.tree, operation.node);
    ROBO_CHECK(computed != NULL);
    robo_layout_text_style_init(&style);
    ROBO_CHECK_EQ_INT(robo_layout_text_style_resolve(computed, &style), ROBO_OK);
    ROBO_CHECK(robo_paint_text_run_font(&run).font_size_px == style.font_size_px);
    ROBO_CHECK_EQ_INT(robo_paint_text_run_font(&run).font_weight, style.font_weight);
    ROBO_CHECK_EQ_INT(robo_paint_text_run_font(&run).italic, style.italic);
    ROBO_CHECK_EQ_INT((int)robo_paint_text_run_font(&run).direction, (int)style.direction);

    /* And every other kind carries a cleared run -- the "one record, one meaning" rule. */
    {
        robo_paint_operation_t background;

        if (paint_nth_of_kind(&result, ROBO_PAINT_OP_BACKGROUND, 0u, &background)) {
            ROBO_CHECK_EQ_INT(robo_paint_operation_text(&background).span.count, 0);
        }
    }

    paint_result_dispose(&result);
    paint_end(&fixture);
}

static void test_text_characters_are_code_points(void)
{
    paint_fixture_doc_t fixture;
    paint_result_t result;
    robo_paint_operation_t operation;
    robo_paint_text_run_t run;
    robo_paint_text_pool_t *pool;
    /* 'cafe' with an accented e: four code points, five UTF-8 bytes, so the last of them is
     * U+00E9 rather than two bytes that happen to spell it. */
    static const uint32_t expected[4] = { 0x0063u, 0x0061u, 0x0066u, 0x00E9u };

    paint_begin_laid_out(&fixture,
                         "<html><body><div class=\"box\">caf\xc3\xa9</div></body></html>",
                         TEXT_BASE_CSS " .box { width: 300px; height: 60px }");
    paint_once(&fixture, &result);

    operation = paint_require(&result, ROBO_PAINT_OP_TEXT);
    run = robo_paint_operation_text(&operation);
    pool = robo_paint_list_text_pool(result.list);

    /* A span counts **code points**, so five bytes and four characters are four. */
    ROBO_CHECK_EQ_INT((int)run.span.count, 4);
    ROBO_CHECK_EQ_INT((int)robo_paint_text_pool_count(pool), 4);
    text_expect_points(pool, run.span, expected, 4, "the accented run");

    paint_result_dispose(&result);
    paint_end(&fixture);
}

/* ------------------------------------------------------------------ */
/* 2. The command                                                      */
/* ------------------------------------------------------------------ */

static void test_text_command_and_span(void)
{
    paint_fixture_doc_t fixture;
    paint_result_t result;
    robo_paint_display_list_t *list = NULL;
    robo_paint_display_list_stats_t stats;
    robo_paint_display_command_t command;
    robo_paint_operation_t operation;
    robo_paint_text_run_t record_run;
    robo_paint_text_run_t command_run;
    robo_paint_text_pool_t *record_pool;
    robo_paint_text_pool_t *command_pool;
    uint32_t characters[16];
    size_t got = 0u;

    paint_begin_laid_out(&fixture, "<html><body><div class=\"box\">AB</div></body></html>",
                         TEXT_BASE_CSS " .box { width: 300px; height: 60px }");
    paint_once(&fixture, &result);
    memset(&stats, 0, sizeof(stats));
    text_build(&result, &list, &stats);

    ROBO_CHECK_EQ_INT((int)stats.texts, 1);
    ROBO_CHECK_EQ_INT((int)robo_paint_display_list_count_of_kind(
                          list, ROBO_PAINT_DISPLAY_CMD_DRAW_TEXT),
                      1);
    ROBO_CHECK_EQ_INT(text_command_of_kind(list, ROBO_PAINT_DISPLAY_CMD_DRAW_TEXT, 0u,
                                           &command),
                      1);
    ROBO_CHECK_EQ_INT((int)robo_paint_display_command_kind(&command),
                      (int)ROBO_PAINT_DISPLAY_CMD_DRAW_TEXT);
    ROBO_CHECK_EQ_INT(robo_paint_display_command_is_drawing(&command), 1);
    ROBO_CHECK_EQ_INT(robo_paint_display_command_is_valid(&command), 1);
    ROBO_CHECK_EQ_INT(robo_paint_display_command_kind_draws(
                          ROBO_PAINT_DISPLAY_CMD_DRAW_TEXT),
                      1);

    /* The rectangle the command acts on is the placement layout produced. */
    operation = paint_require(&result, ROBO_PAINT_OP_TEXT);
    paint_rect_eq(robo_paint_display_command_rect(&command), operation.boxes.border,
                  "the text command's rectangle");

    /* The run crossed the boundary: same offset, same font, same length. */
    record_run = robo_paint_operation_text(&operation);
    command_run = robo_paint_display_command_text(&command);
    ROBO_CHECK(robo_paint_text_run_equal(&record_run, &command_run));
    ROBO_CHECK_EQ_INT(command_run.baseline_offset, record_run.baseline_offset);
    ROBO_CHECK_EQ_INT((int)command_run.span.count, (int)record_run.span.count);

    /* Two pools, and the display list's is its own: the characters read back from **its**
     * pool through **its** span, without consulting the record list at all. */
    record_pool = robo_paint_list_text_pool(result.list);
    command_pool = robo_paint_display_list_text_pool(list);
    ROBO_CHECK(command_pool != NULL);
    /* Two different containers: the display list carries its own characters. */
    ROBO_CHECK(record_pool != command_pool);
    ROBO_CHECK_EQ_INT(robo_paint_text_pool_contains(command_pool, command_run.span), 1);
    text_expect_chars(command_pool, command_run.span, "AB",
                      "the command's characters");
    ROBO_CHECK_EQ_INT(robo_paint_text_pool_count(command_pool), 2);
    ROBO_CHECK_EQ_INT(robo_paint_display_command_text(&command).span.first,
                      command_run.span.first);

    /* And the same span read out of the record list's pool says the same thing, which is the
     * copy being a copy rather than a renumbering that changed the characters. */
    ROBO_CHECK_EQ_INT(robo_paint_text_pool_read(record_pool, record_run.span, characters,
                                               16u, &got, NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT((int)got, 2);

    robo_paint_display_list_destroy(list);
    paint_result_dispose(&result);
    paint_end(&fixture);
}

static void test_text_order_and_depth(void)
{
    paint_fixture_doc_t fixture;
    paint_result_t result;
    robo_paint_display_list_t *list = NULL;
    robo_paint_display_list_stats_t stats;
    robo_paint_display_command_t command;
    robo_paint_operation_t operation;
    robo_layout_node_id_t box;
    size_t content_index;
    size_t text_index;
    size_t end_index;

    paint_begin_laid_out(&fixture, "<html><body><div class=\"box\">AB</div></body></html>",
                         TEXT_BASE_CSS " .box { width: 300px; height: 60px }");
    paint_once(&fixture, &result);
    memset(&stats, 0, sizeof(stats));
    text_build(&result, &list, &stats);

    operation = paint_require(&result, ROBO_PAINT_OP_TEXT);
    box = ROBO_LAYOUT_NODE_ID_NONE;
    /* The run's owner is the element whose text node this is -- the box that placed it. */
    ROBO_CHECK_EQ_INT(robo_layout_object_parent(fixture.tree, operation.node, &box), ROBO_OK);
    ROBO_CHECK(box != ROBO_LAYOUT_NODE_ID_NONE);

    /*
     * The `DRAW_TEXT` sits **inside** the owning box: after its `CONTENT` boundary and before
     * its `END_NODE`. That is what puts a run's painting in the right place relative to the
     * box's own background and border without a second ordering rule.
     */
    content_index = (size_t)-1;
    end_index = (size_t)-1;
    {
        size_t index;

        for (index = 0u; index < robo_paint_display_list_count(list); index++) {
            robo_paint_display_command_t each;

            ROBO_CHECK_EQ_INT(robo_paint_display_list_at(list, index, &each), ROBO_OK);
            if (each.node != box) {
                continue;
            }
            if (each.kind == ROBO_PAINT_DISPLAY_CMD_CONTENT) {
                content_index = index;
            }
            if (each.kind == ROBO_PAINT_DISPLAY_CMD_END_NODE) {
                end_index = index;
            }
        }
    }
    text_index = text_index_of_kind(list, ROBO_PAINT_DISPLAY_CMD_DRAW_TEXT, 0u);
    ROBO_CHECK(content_index != (size_t)-1);
    ROBO_CHECK(end_index != (size_t)-1);
    ROBO_CHECK(text_index != (size_t)-1);
    ROBO_CHECK(content_index < text_index);
    ROBO_CHECK(text_index < end_index);

    /* And the run is one level deeper than the box that owns it, in both spaces. */
    ROBO_CHECK_EQ_INT(text_command_of_kind(list, ROBO_PAINT_DISPLAY_CMD_DRAW_TEXT, 0u,
                                           &command),
                      1);
    {
        size_t index;
        int compared = 0;

        for (index = 0u; index < robo_paint_list_count(result.list); index++) {
            robo_paint_operation_t each;

            ROBO_CHECK_EQ_INT(robo_paint_list_at(result.list, index, &each), ROBO_OK);
            if (each.kind == ROBO_PAINT_OP_BEGIN_BOX && each.node == box) {
                ROBO_CHECK_EQ_INT((int)robo_paint_operation_depth(&operation),
                                  (int)robo_paint_operation_depth(&each) + 1);
                ROBO_CHECK_EQ_INT((int)robo_paint_operation_depth(&operation),
                                  (int)robo_paint_display_command_depth(&command));
                compared = 1;
                break;
            }
        }
        ROBO_CHECK_EQ_INT(compared, 1);
    }

    robo_paint_display_list_destroy(list);
    paint_result_dispose(&result);
    paint_end(&fixture);
}

static void test_text_multiple_runs(void)
{
    paint_fixture_doc_t fixture;
    paint_result_t result;
    robo_paint_text_pool_t *pool;
    robo_paint_operation_t first;
    robo_paint_operation_t second;
    size_t index;

    paint_begin_laid_out(&fixture,
                         "<html><body><div class=\"box\">"
                         "<span class=\"a\">One</span>"
                         "<span class=\"b\">Two</span>"
                         "</div></body></html>",
                         TEXT_BASE_CSS
                         " .box { width: 300px; height: 80px } .a, .b { height: 30px }");
    paint_once(&fixture, &result);

    ROBO_CHECK_EQ_INT((int)result.stats.text_runs, 2);
    ROBO_CHECK_EQ_INT((int)result.stats.texts, 2);
    ROBO_CHECK_EQ_INT((int)paint_count_of(&result, ROBO_PAINT_OP_TEXT), 2);

    ROBO_CHECK_EQ_INT(paint_nth_of_kind(&result, ROBO_PAINT_OP_TEXT, 0u, &first), 1);
    ROBO_CHECK_EQ_INT(paint_nth_of_kind(&result, ROBO_PAINT_OP_TEXT, 1u, &second), 1);
    pool = robo_paint_list_text_pool(result.list);
    text_expect_chars(pool, robo_paint_operation_text(&first).span, "One",
                      "the first run");
    text_expect_chars(pool, robo_paint_operation_text(&second).span, "Two",
                      "the second run");

    /* Document order is list order, and no pair of runs shares a span. */
    ROBO_CHECK(robo_paint_operation_text(&first).span.first <
               robo_paint_operation_text(&second).span.first);
    ROBO_CHECK(robo_paint_operation_text(&first).span.first +
                   robo_paint_operation_text(&first).span.count <=
               robo_paint_operation_text(&second).span.first);

    /* The two runs are two different objects, so each is deeper than a different box. */
    ROBO_CHECK(robo_paint_operation_node(&first) != robo_paint_operation_node(&second));
    for (index = 0u; index < robo_paint_list_count(result.list); index++) {
        robo_paint_operation_t each;

        ROBO_CHECK_EQ_INT(robo_paint_list_at(result.list, index, &each), ROBO_OK);
        if (each.kind == ROBO_PAINT_OP_TEXT) {
            paint_local_boxes_match_layout(&fixture, each);
        }
    }

    paint_result_dispose(&result);
    paint_end(&fixture);
}

static void test_text_wrapped_lines(void)
{
    paint_fixture_doc_t fixture;
    paint_result_t result;
    robo_paint_operation_t first;
    robo_paint_operation_t second;

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
    paint_begin_laid_out(&fixture,
                         "<html><body><div class=\"box\">AB<span class=\"in\">AB</span>"
                         "</div></body></html>",
                         TEXT_BASE_CSS
                         " .box { width: 24px; height: 100px } .box .in { display: inline }");
    paint_once(&fixture, &result);

    ROBO_CHECK_EQ_INT((int)result.stats.text_runs, 2);
    ROBO_CHECK_EQ_INT((int)result.stats.texts, (int)result.stats.text_runs);
    ROBO_CHECK_EQ_INT((int)paint_count_of(&result, ROBO_PAINT_OP_TEXT), 2);
    ROBO_CHECK(paint_nth_of_kind(&result, ROBO_PAINT_OP_TEXT, 0u, &first) == 1);
    ROBO_CHECK(paint_nth_of_kind(&result, ROBO_PAINT_OP_TEXT, 1u, &second) == 1);
    /* Two lines, so two different vertical placements -- and neither was invented here. */
    ROBO_CHECK(first.boxes.border.y != second.boxes.border.y);
    ROBO_CHECK(second.boxes.border.y > first.boxes.border.y);
    {
        robo_paint_text_pool_t *pool = robo_paint_list_text_pool(result.list);

        text_expect_chars(pool, robo_paint_operation_text(&first).span, "AB",
                          "the first line's run");
        text_expect_chars(pool, robo_paint_operation_text(&second).span, "AB",
                          "the second line's run");
    }

    paint_result_dispose(&result);
    paint_end(&fixture);
}

/* ------------------------------------------------------------------ */
/* 3. Colour, baseline, pixels                                         */
/* ------------------------------------------------------------------ */

static void test_text_color(void)
{
    paint_fixture_doc_t fixture;
    paint_result_t result;
    robo_paint_display_list_t *list = NULL;
    robo_paint_display_list_stats_t stats;
    robo_paint_display_command_t command;
    robo_paint_operation_t operation;

    paint_begin_laid_out(&fixture, "<html><body><div class=\"box\">AB</div></body></html>",
                         TEXT_BASE_CSS
                         " .box { width: 300px; height: 60px; color: #ff0000 }");
    paint_once(&fixture, &result);

    operation = paint_require(&result, ROBO_PAINT_OP_TEXT);
    paint_color_eq(robo_paint_operation_color(&operation), robo_css_color_make(0xff, 0x00, 0x00, 255),
                   "the text record's colour");

    memset(&stats, 0, sizeof(stats));
    text_build(&result, &list, &stats);
    ROBO_CHECK_EQ_INT(text_command_of_kind(list, ROBO_PAINT_DISPLAY_CMD_DRAW_TEXT, 0u,
                                           &command),
                      1);
    paint_color_eq(robo_paint_display_command_color(&command),
                   robo_css_color_make(0xff, 0x00, 0x00, 255), "the text command's colour");

    robo_paint_display_list_destroy(list);
    paint_result_dispose(&result);
    paint_end(&fixture);
}

static void test_text_pixels(void)
{
    paint_fixture_doc_t fixture;
    paint_result_t result;
    robo_paint_display_list_t *list = NULL;
    robo_paint_display_list_stats_t stats;
    robo_paint_decoration_stats_t decoration;
    text_face_t face;
    robo_gfx_surface_t *surface;
    robo_gfx_color_rgba8_t color;
    uint32_t marked = 0u;
    uint32_t x;
    uint32_t y;

    text_face_create(&face);
    text_face_create(&face);
    paint_begin_laid_out(&fixture, "<html><body><div class=\"box\">AB</div></body></html>",
                         TEXT_BASE_CSS
                         " .box { width: 300px; height: 60px; background-color: #ffffff;"
                         " color: #ff0000; font-size: 100px }");
    paint_once(&fixture, &result);
    memset(&stats, 0, sizeof(stats));
    text_build(&result, &list, &stats);

    surface = text_surface(300u, 100u);
    memset(&decoration, 0, sizeof(decoration));
    text_paint(list, surface, face.face, &decoration);

    ROBO_CHECK_EQ_INT((int)decoration.texts, 1);
    ROBO_CHECK_EQ_INT((int)decoration.no_font, 0);
    ROBO_CHECK_EQ_INT((int)decoration.glyphs, 2);
    ROBO_CHECK_EQ_INT((int)decoration.glyphs_missing, 0);
    ROBO_CHECK_EQ_INT((int)decoration.commands,
                      (int)(decoration.draws + decoration.skipped));
    ROBO_CHECK_EQ_INT((int)decoration.draws,
                      (int)(decoration.backgrounds + decoration.borders + decoration.texts));
    ROBO_CHECK_EQ_INT((int)decoration.draws,
                      (int)(decoration.filled + decoration.contextual + decoration.transparent +
                            decoration.empty + decoration.no_font));

    /* The glyphs reach the surface: a mapped code point strokes its outline in the run's own
     * colour, so those pixels exist and nothing else on the page is that colour. */
    for (y = 0u; y < robo_gfx_surface_height(surface); y++) {
        for (x = 0u; x < robo_gfx_surface_width(surface); x++) {
            ROBO_CHECK_EQ_INT(robo_gfx_surface_get_pixel(surface, x, y, &color), ROBO_OK);
            if (color.r == TEXT_RED.r && color.g == TEXT_RED.g && color.b == TEXT_RED.b) {
                marked++;
            }
        }
    }
    ROBO_CHECK(marked > 0u);

    robo_gfx_surface_release(surface);
    robo_paint_display_list_destroy(list);
    paint_result_dispose(&result);
    paint_end(&fixture);
    text_face_destroy(&face);
}

static void test_text_no_font(void)
{
    paint_fixture_doc_t fixture;
    paint_result_t result;
    robo_paint_display_list_t *list = NULL;
    robo_paint_display_list_stats_t stats;
    robo_paint_decoration_stats_t decoration;
    robo_gfx_surface_t *surface;
    robo_gfx_color_rgba8_t color;
    uint32_t marked = 0u;
    uint32_t x;
    uint32_t y;

    paint_begin_laid_out(&fixture, "<html><body><div class=\"box\">AB</div></body></html>",
                         TEXT_BASE_CSS
                         " .box { width: 300px; height: 60px; background-color: #ffffff;"
                         " color: #ff0000; font-size: 100px }");
    paint_once(&fixture, &result);
    memset(&stats, 0, sizeof(stats));
    text_build(&result, &list, &stats);

    surface = text_surface(300u, 100u);
    memset(&decoration, 0, sizeof(decoration));
    text_paint(list, surface, NULL, &decoration);

    /* The text command is seen and **not** drawn, and the reason is the call rather than the
     * document -- which is exactly what `no_font` is for. */
    ROBO_CHECK_EQ_INT((int)decoration.texts, 1);
    ROBO_CHECK_EQ_INT((int)decoration.no_font, 1);
    ROBO_CHECK_EQ_INT((int)decoration.glyphs, 0);
    ROBO_CHECK_EQ_INT((int)decoration.draws,
                      (int)(decoration.backgrounds + decoration.borders + decoration.texts));
    ROBO_CHECK_EQ_INT((int)decoration.draws,
                      (int)(decoration.filled + decoration.contextual + decoration.transparent +
                            decoration.empty + decoration.no_font));

    for (y = 0u; y < robo_gfx_surface_height(surface); y++) {
        for (x = 0u; x < robo_gfx_surface_width(surface); x++) {
            ROBO_CHECK_EQ_INT(robo_gfx_surface_get_pixel(surface, x, y, &color), ROBO_OK);
            if (color.r == TEXT_RED.r && color.g == TEXT_RED.g && color.b == TEXT_RED.b) {
                marked++;
            }
        }
    }
    ROBO_CHECK_EQ_INT((int)marked, 0);

    robo_gfx_surface_release(surface);
    robo_paint_display_list_destroy(list);
    paint_result_dispose(&result);
    paint_end(&fixture);
}

static void test_text_missing_glyph(void)
{
    paint_fixture_doc_t fixture;
    paint_result_t result;
    robo_paint_display_list_t *list = NULL;
    robo_paint_display_list_stats_t stats;
    robo_paint_decoration_stats_t decoration;
    text_face_t face;
    robo_gfx_surface_t *surface;

    text_face_create(&face);
    /* 'Z' and 'z' are not in the face's cmap, so both draw as `.notdef`. */
    paint_begin_laid_out(&fixture, "<html><body><div class=\"box\">Zz</div></body></html>",
                         TEXT_BASE_CSS
                         " .box { width: 300px; height: 60px; color: #ff0000;"
                         " font-size: 100px }");
    paint_once(&fixture, &result);
    memset(&stats, 0, sizeof(stats));
    text_build(&result, &list, &stats);

    surface = text_surface(300u, 100u);
    memset(&decoration, 0, sizeof(decoration));
    text_paint(list, surface, face.face, &decoration);

    ROBO_CHECK_EQ_INT((int)decoration.texts, 1);
    ROBO_CHECK_EQ_INT((int)decoration.glyphs, 2);
    /* Counted, not skipped: the measurer's own number one layer along. */
    ROBO_CHECK_EQ_INT((int)decoration.glyphs_missing, 2);

    robo_gfx_surface_release(surface);
    robo_paint_display_list_destroy(list);
    paint_result_dispose(&result);
    paint_end(&fixture);
    text_face_destroy(&face);
}

static void test_text_rtl(void)
{
    paint_fixture_doc_t fixture;
    paint_result_t result;
    robo_paint_display_list_t *list = NULL;
    robo_paint_display_list_stats_t stats;
    robo_paint_decoration_stats_t decoration;
    robo_paint_operation_t operation;
    robo_paint_text_run_t run;
    text_face_t face;
    robo_gfx_surface_t *surface;

    text_face_create(&face);
    paint_begin_laid_out(&fixture, "<html><body><div class=\"box\">AB</div></body></html>",
                         TEXT_BASE_CSS
                         " .box { width: 300px; height: 60px; color: #ff0000;"
                         " font-size: 100px; direction: rtl }");
    paint_once(&fixture, &result);

    operation = paint_require(&result, ROBO_PAINT_OP_TEXT);
    run = robo_paint_operation_text(&operation);
    ROBO_CHECK_EQ_INT((int)run.font.direction, (int)ROBO_LAYOUT_TEXT_DIRECTION_RTL);

    memset(&stats, 0, sizeof(stats));
    text_build(&result, &list, &stats);
    surface = text_surface(300u, 100u);
    memset(&decoration, 0, sizeof(decoration));
    text_paint(list, surface, face.face, &decoration);

    /* Drawn in logical order and **named**: this build has no bidi algorithm, so the fact that
     * a simplification happened is a number a reader sees. */
    ROBO_CHECK_EQ_INT((int)decoration.texts, 1);
    ROBO_CHECK_EQ_INT((int)decoration.rtl, 1);

    robo_gfx_surface_release(surface);
    robo_paint_display_list_destroy(list);
    paint_result_dispose(&result);
    paint_end(&fixture);
    text_face_destroy(&face);
}

/* ------------------------------------------------------------------ */
/* 4. Determinism, isolation, empties                                  */
/* ------------------------------------------------------------------ */

static void test_text_determinism(void)
{
    paint_fixture_doc_t first;
    paint_fixture_doc_t second;
    paint_result_t first_result;
    paint_result_t second_result;
    robo_paint_display_list_t *first_list = NULL;
    robo_paint_display_list_t *second_list = NULL;
    robo_paint_display_list_stats_t first_stats;
    robo_paint_display_list_stats_t second_stats;
    char first_dump[4096];
    char second_dump[4096];
    size_t first_length = 0u;
    size_t second_length = 0u;

    paint_begin_laid_out(&first, "<html><body><div class=\"box\">Hello</div></body></html>",
                         TEXT_BASE_CSS " .box { width: 300px; height: 60px }");
    paint_once(&first, &first_result);
    memset(&first_stats, 0, sizeof(first_stats));
    text_build(&first_result, &first_list, &first_stats);

    paint_begin_laid_out(&second, "<html><body><div class=\"box\">Hello</div></body></html>",
                         TEXT_BASE_CSS " .box { width: 300px; height: 60px }");
    paint_once(&second, &second_result);
    memset(&second_stats, 0, sizeof(second_stats));
    text_build(&second_result, &second_list, &second_stats);

    /* Identical input, identical output -- spans, characters, geometry, order and counts. */
    ROBO_CHECK_EQ_INT(robo_paint_display_list_equal(first_list, second_list), 1);
    ROBO_CHECK_EQ_INT((int)robo_paint_list_count(first_result.list),
                      (int)robo_paint_list_count(second_result.list));
    ROBO_CHECK_EQ_INT((int)robo_paint_text_pool_count(
                          robo_paint_list_text_pool(first_result.list)),
                      (int)robo_paint_text_pool_count(
                          robo_paint_list_text_pool(second_result.list)));
    ROBO_CHECK((int)first_result.stats.texts == (int)second_result.stats.texts);
    ROBO_CHECK((int)first_stats.texts == (int)second_stats.texts);

    /* And the debug dump is deterministic too -- no addresses, no allocation order. */
    ROBO_CHECK_EQ_INT(robo_paint_display_list_write(first_list, first_dump, sizeof(first_dump),
                                                    &first_length, NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_paint_display_list_write(second_list, second_dump,
                                                    sizeof(second_dump), &second_length, NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT((int)first_length, (int)second_length);
    ROBO_CHECK(strcmp(first_dump, second_dump) == 0);

    robo_paint_display_list_destroy(first_list);
    robo_paint_display_list_destroy(second_list);
    paint_result_dispose(&first_result);
    paint_result_dispose(&second_result);
    paint_end(&first);
    paint_end(&second);
}

static void test_text_mutation_isolation(void)
{
    paint_fixture_doc_t fixture;
    paint_result_t result;
    robo_paint_display_list_t *list = NULL;
    robo_paint_display_list_stats_t stats;
    robo_paint_display_command_t command;
    robo_paint_text_pool_t *display_pool;
    text_face_t face;
    robo_gfx_surface_t *surface;
    robo_gfx_color_rgba8_t color;
    char before[4096];
    char after[4096];
    size_t before_length = 0u;
    size_t after_length = 0u;
    uint32_t marked = 0u;
    uint32_t x;
    uint32_t y;

    text_face_create(&face);
    paint_begin_laid_out(&fixture, "<html><body><div class=\"box\">AB</div></body></html>",
                         TEXT_BASE_CSS
                         " .box { width: 300px; height: 60px; color: #ff0000;"
                         " font-size: 100px }");
    paint_once(&fixture, &result);
    memset(&stats, 0, sizeof(stats));
    text_build(&result, &list, &stats);

    ROBO_CHECK_EQ_INT(robo_paint_display_list_write(list, before, sizeof(before), &before_length,
                                                    NULL),
                      ROBO_OK);

    /*
     * The records are gone -- every operation and every character they held. The display list
     * still carries its own copy, which is the whole reason it has a pool of its own.
     */
    paint_result_dispose(&result);

    display_pool = robo_paint_display_list_text_pool(list);
    ROBO_CHECK(display_pool != NULL);
    ROBO_CHECK_EQ_INT((int)robo_paint_text_pool_count(display_pool), 2);
    ROBO_CHECK_EQ_INT(text_command_of_kind(list, ROBO_PAINT_DISPLAY_CMD_DRAW_TEXT, 0u,
                                           &command),
                      1);
    text_expect_chars(display_pool, robo_paint_display_command_text(&command).span, "AB",
                      "the characters after the records were freed");
    ROBO_CHECK_EQ_INT(robo_paint_display_list_write(list, after, sizeof(after), &after_length,
                                                    NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT((int)before_length, (int)after_length);
    ROBO_CHECK(strcmp(before, after) == 0);

    /* And it still paints, so the isolation is about the snapshot and not about a dead list. */
    surface = text_surface(300u, 100u);
    {
        robo_paint_decoration_stats_t decoration;

        memset(&decoration, 0, sizeof(decoration));
        text_paint(list, surface, face.face, &decoration);
        ROBO_CHECK_EQ_INT((int)decoration.glyphs, 2);
    }
    for (y = 0u; y < robo_gfx_surface_height(surface); y++) {
        for (x = 0u; x < robo_gfx_surface_width(surface); x++) {
            ROBO_CHECK_EQ_INT(robo_gfx_surface_get_pixel(surface, x, y, &color), ROBO_OK);
            if (color.r == TEXT_RED.r && color.g == TEXT_RED.g && color.b == TEXT_RED.b) {
                marked++;
            }
        }
    }
    ROBO_CHECK(marked > 0u);

    robo_gfx_surface_release(surface);
    robo_paint_display_list_destroy(list);
    paint_end(&fixture);
    text_face_destroy(&face);
}

static void test_text_empty_cases(void)
{
    paint_fixture_doc_t fixture;
    paint_result_t result;
    robo_paint_display_list_t *list = NULL;
    robo_paint_display_list_stats_t stats;
    robo_paint_decoration_stats_t decoration;
    robo_gfx_surface_t *surface;

    /* A document with no text at all: no run reached, no record, and a call with no face is
     * told nothing about a font it did not need. */
    paint_begin_laid_out(&fixture, "<html><body><div class=\"box\"></div></body></html>",
                         TEXT_BASE_CSS
                         " .box { width: 40px; height: 20px; background-color: #ffffff }");
    paint_once(&fixture, &result);

    ROBO_CHECK_EQ_INT((int)result.stats.text_runs, 0);
    ROBO_CHECK_EQ_INT((int)result.stats.texts, 0);
    ROBO_CHECK_EQ_INT((int)result.stats.text_skipped, 0);
    ROBO_CHECK_EQ_INT((int)paint_count_of(&result, ROBO_PAINT_OP_TEXT), 0);
    ROBO_CHECK_EQ_INT((int)result.stats.text_runs,
                      (int)(result.stats.texts + result.stats.text_skipped));

    memset(&stats, 0, sizeof(stats));
    text_build(&result, &list, &stats);
    ROBO_CHECK_EQ_INT((int)stats.texts, 0);
    ROBO_CHECK_EQ_INT((int)robo_paint_display_list_count_of_kind(
                          list, ROBO_PAINT_DISPLAY_CMD_DRAW_TEXT),
                      0);
    ROBO_CHECK_EQ_INT((int)robo_paint_text_pool_count(
                          robo_paint_display_list_text_pool(list)),
                      0);

    surface = text_surface(80u, 40u);
    memset(&decoration, 0, sizeof(decoration));
    text_paint(list, surface, NULL, &decoration);
    ROBO_CHECK_EQ_INT((int)decoration.texts, 0);
    ROBO_CHECK_EQ_INT((int)decoration.no_font, 0);
    ROBO_CHECK_EQ_INT((int)decoration.commands,
                      (int)(decoration.draws + decoration.skipped));
    ROBO_CHECK_EQ_INT((int)decoration.draws,
                      (int)(decoration.backgrounds + decoration.borders + decoration.texts));

    robo_gfx_surface_release(surface);
    robo_paint_display_list_destroy(list);
    paint_result_dispose(&result);
    paint_end(&fixture);
}

static void test_text_hand_built_rules(void)
{
    robo_paint_operation_t operation;
    robo_paint_display_command_t command;
    robo_paint_text_run_t run;
    robo_paint_list_t *list = NULL;
    robo_paint_text_span_t span;
    robo_paint_diag_t diag;
    robo_paint_boxes_t boxes;
    robo_layout_rect_t rect;

    paint_fixture();

    /* A box with no inset layers: one rectangle, three times, and the flag clear. */
    robo_paint_boxes_init(&boxes);
    rect = robo_gfx_rect(0, 0, 10, 10);
    boxes.border = rect;
    boxes.padding = rect;
    boxes.content = rect;
    ROBO_CHECK_EQ_INT(robo_paint_boxes_is_valid(&boxes), 1);

    /* A run with no characters is not a run, and an operation carrying one is refused. */
    robo_paint_text_run_init(&run);
    ROBO_CHECK_EQ_INT(robo_paint_text_run_is_valid(&run), 0);
    run.span = robo_paint_text_span_at(0u);
    run.baseline_offset = 8;
    ROBO_CHECK_EQ_INT(robo_paint_text_run_is_valid(&run), 0);

    run.span.count = 1u;
    run.baseline_offset = -1;
    ROBO_CHECK_EQ_INT(robo_paint_text_run_is_valid(&run), 0);

    run.baseline_offset = 8;
    run.span.count = 0u;
    robo_paint_operation_init(&operation);
    operation.kind = ROBO_PAINT_OP_TEXT;
    operation.node = 1;
    operation.boxes = boxes;
    operation.color = robo_css_color_make(0x00, 0x00, 0x00, 255);
    operation.text = run;
    ROBO_CHECK_EQ_INT(robo_paint_operation_is_valid(&operation), 0);

    /* The same record with one character is a record -- and the list checks its span against
     * its own pool, so a span naming characters the pool does not hold is refused there. */
    run.span.count = 1u;
    operation.text = run;
    ROBO_CHECK_EQ_INT(robo_paint_operation_is_valid(&operation), 1);
    ROBO_CHECK_EQ_INT(robo_paint_list_create(&list, NULL), ROBO_OK);
    robo_paint_diag_reset(&diag);
    ROBO_CHECK_EQ_INT(robo_paint_list_append(list, &operation, &diag), ROBO_INVALID_ARGUMENT);

    span = robo_paint_text_span_at(0u);
    ROBO_CHECK_EQ_INT(robo_paint_list_add_text(list, (const uint32_t[]){ 'A' }, 1u, &span,
                                               &diag),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT((int)span.first, 0);
    ROBO_CHECK_EQ_INT((int)span.count, 1);
    operation.text.span = span;
    ROBO_CHECK_EQ_INT(robo_paint_list_append(list, &operation, &diag), ROBO_OK);

    /* And the command's rule is the record's, one field of a kind along. */
    memset(&command, 0, sizeof(command));
    command.kind = ROBO_PAINT_DISPLAY_CMD_DRAW_TEXT;
    ROBO_CHECK_EQ_INT(robo_paint_display_command_is_valid(&command), 0);

    robo_paint_list_destroy(list);
}

static void test_text_pool_doors(void)
{
    robo_paint_text_pool_t *pool = NULL;
    robo_paint_text_pool_t *other = NULL;
    robo_paint_text_span_t span;
    robo_paint_text_span_t copied;
    robo_paint_text_span_t bogus;
    robo_paint_diag_t diag;
    uint32_t buffer[8];
    size_t got = 0u;
    size_t capacity;
    static const uint32_t hello[5] = { 'H', 'e', 'l', 'l', 'o' };

    paint_fixture();

    ROBO_CHECK_EQ_INT(robo_paint_text_pool_create(&pool, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_paint_text_pool_create(&other, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT((int)robo_paint_text_pool_count(pool), 0);
    ROBO_CHECK(robo_paint_text_pool_code_points(pool) == NULL);
    ROBO_CHECK_EQ_INT(robo_paint_text_pool_contains(pool, robo_paint_text_span_at(0u)), 1);

    robo_paint_diag_reset(&diag);
    /* A run of zero characters is refused: it would name a position, not a run. */
    ROBO_CHECK_EQ_INT(robo_paint_text_pool_append(pool, hello, 0u, &span, &diag),
                      ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_paint_text_pool_append(pool, NULL, 1u, &span, &diag),
                      ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_paint_text_pool_append(pool, hello, 5u, &span, &diag), ROBO_OK);
    ROBO_CHECK_EQ_INT((int)span.first, 0);
    ROBO_CHECK_EQ_INT((int)span.count, 5);
    ROBO_CHECK_EQ_INT((int)robo_paint_text_pool_count(pool), 5);
    capacity = robo_paint_text_pool_capacity(pool);
    ROBO_CHECK(capacity >= 5u);

    /* The read door validates the span, then copies. */
    ROBO_CHECK_EQ_INT(robo_paint_text_pool_read(pool, span, buffer, 8u, &got, &diag), ROBO_OK);
    ROBO_CHECK_EQ_INT((int)got, 5);
    ROBO_CHECK(memcmp(buffer, hello, sizeof(hello)) == 0);

    bogus = robo_paint_text_span_at(3u);
    bogus.count = 5u;
    ROBO_CHECK_EQ_INT(robo_paint_text_pool_contains(pool, bogus), 0);
    ROBO_CHECK_EQ_INT(robo_paint_text_pool_read(pool, bogus, buffer, 8u, &got, &diag),
                      ROBO_INVALID_ARGUMENT);

    /* A buffer that cannot hold the run reports how much it would have needed. */
    got = 0u;
    ROBO_CHECK_EQ_INT(robo_paint_text_pool_read(pool, span, buffer, 2u, &got, &diag),
                      ROBO_NOT_SUPPORTED);
    ROBO_CHECK_EQ_INT((int)got, 5);

    /* The copy door is the one the builder uses: the characters move and the span renumbers. */
    ROBO_CHECK_EQ_INT(robo_paint_text_span_at(4u).count, 0);
    ROBO_CHECK_EQ_INT(robo_paint_text_pool_copy(other, pool, span, &copied, &diag), ROBO_OK);
    ROBO_CHECK_EQ_INT((int)robo_paint_text_pool_count(other), 5);
    ROBO_CHECK_EQ_INT((int)copied.count, 5);
    ROBO_CHECK_EQ_INT(robo_paint_text_pool_contains(other, copied), 1);
    memset(buffer, 0, sizeof(buffer));
    ROBO_CHECK_EQ_INT(robo_paint_text_pool_read(other, copied, buffer, 8u, &got, &diag),
                      ROBO_OK);
    ROBO_CHECK(memcmp(buffer, hello, sizeof(hello)) == 0);

    /* An empty span and a span outside the source are both refused by the copy door. */
    ROBO_CHECK_EQ_INT(robo_paint_text_pool_copy(other, pool, robo_paint_text_span_at(0u),
                                                &copied, &diag),
                      ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_paint_text_pool_copy(other, pool, bogus, &copied, &diag),
                      ROBO_INVALID_ARGUMENT);

    /* Clearing keeps the storage -- a renderer that repaints reuses it -- and drops every
     * character with it. */
    robo_paint_text_pool_clear(pool);
    ROBO_CHECK_EQ_INT((int)robo_paint_text_pool_count(pool), 0);
    ROBO_CHECK(robo_paint_text_pool_capacity(pool) >= capacity);
    ROBO_CHECK_EQ_INT(robo_paint_text_pool_contains(pool, span), 0);

    /* NULL is a defined answer for every door. */
    ROBO_CHECK_EQ_INT((int)robo_paint_text_pool_count(NULL), 0);
    ROBO_CHECK_EQ_INT((int)robo_paint_text_pool_capacity(NULL), 0);
    ROBO_CHECK_EQ_INT(robo_paint_text_pool_contains(NULL, span), 0);
    ROBO_CHECK(robo_paint_text_pool_code_points(NULL) == NULL);

    robo_paint_text_pool_destroy(pool);
    robo_paint_text_pool_destroy(other);
    robo_paint_text_pool_destroy(NULL); /* documented as safe */
}

static void test_text_dump(void)
{
    paint_fixture_doc_t fixture;
    paint_result_t result;
    robo_paint_display_list_t *list = NULL;
    robo_paint_display_list_stats_t stats;
    char records[8192];
    char commands[8192];
    size_t records_length = 0u;
    size_t commands_length = 0u;

    paint_begin_laid_out(&fixture, "<html><body><div class=\"box\">Hi</div></body></html>",
                         TEXT_BASE_CSS
                         " .box { width: 300px; height: 60px; color: #ff0000 }");
    paint_once(&fixture, &result);

    ROBO_CHECK_EQ_INT(robo_paint_list_write(result.list, records, sizeof(records),
                                            &records_length, NULL),
                      ROBO_OK);
    /* The dump names the run's characters, so a reader of a record dump can see what a text
     * operation is about without opening the pool. */
    ROBO_CHECK(text_contains(records, "text=\"Hi\""));

    memset(&stats, 0, sizeof(stats));
    text_build(&result, &list, &stats);
    ROBO_CHECK_EQ_INT(robo_paint_display_list_write(list, commands, sizeof(commands),
                                                    &commands_length, NULL),
                      ROBO_OK);
    ROBO_CHECK(text_contains(commands, "draw-text"));
    ROBO_CHECK(text_contains(commands, "text=\"Hi\""));

    robo_paint_display_list_destroy(list);
    paint_result_dispose(&result);
    paint_end(&fixture);
}

/* ------------------------------------------------------------------ */
/* Registration                                                        */
/* ------------------------------------------------------------------ */

void robo_test_register_paint_text(void)
{
    robo_test_register("paint.text.record_shape", test_text_record_shape);
    robo_test_register("paint.text.characters_are_code_points",
                       test_text_characters_are_code_points);
    robo_test_register("paint.text.command_and_span", test_text_command_and_span);
    robo_test_register("paint.text.order_and_depth", test_text_order_and_depth);
    robo_test_register("paint.text.multiple_runs", test_text_multiple_runs);
    robo_test_register("paint.text.wrapped_lines", test_text_wrapped_lines);
    robo_test_register("paint.text.color", test_text_color);
    robo_test_register("paint.text.pixels", test_text_pixels);
    robo_test_register("paint.text.no_font", test_text_no_font);
    robo_test_register("paint.text.missing_glyph", test_text_missing_glyph);
    robo_test_register("paint.text.rtl", test_text_rtl);
    robo_test_register("paint.text.determinism", test_text_determinism);
    robo_test_register("paint.text.mutation_isolation", test_text_mutation_isolation);
    robo_test_register("paint.text.empty_cases", test_text_empty_cases);
    robo_test_register("paint.text.hand_built_rules", test_text_hand_built_rules);
    robo_test_register("paint.text.pool_doors", test_text_pool_doors);
    robo_test_register("paint.text.dump", test_text_dump);
}
