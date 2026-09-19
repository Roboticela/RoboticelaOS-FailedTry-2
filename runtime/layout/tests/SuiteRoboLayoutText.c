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
typedef struct layout_text_script {
    int32_t per_code_point;
    int32_t height;
    int32_t ascent;
    int32_t descent;
    /** Calls the seam made. What makes "measured once per run" assertable. */
    size_t calls;
    /** When non-zero, the callback fails rather than answering. */
    int fail;
} layout_text_script_t;

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
static robo_result_t layout_text_script_measure(void *userdata,
                                               const robo_layout_text_request_t *request,
                                               robo_layout_text_run_t *out_run)
{
    layout_text_script_t *script = (layout_text_script_t *)userdata;

    if (!script || !request || !out_run || !request->style) {
        return ROBO_INVALID_ARGUMENT;
    }
    script->calls++;
    if (script->fail) {
        return ROBO_ERROR;
    }
    out_run->source = request->text_node;
    out_run->range = request->range;
    out_run->direction = request->style->direction;
    out_run->code_points = robo_layout_text_range_length(request->range);
    out_run->width = (int32_t)out_run->code_points * script->per_code_point;
    out_run->height = script->height;
    out_run->ascent = script->ascent;
    out_run->descent = script->descent;
    return ROBO_OK;
}

static robo_layout_text_metrics_t layout_text_script_metrics(layout_text_script_t *script)
{
    robo_layout_text_metrics_t metrics;

    robo_layout_text_metrics_init(&metrics);
    metrics.userdata = script;
    metrics.measure = layout_text_script_measure;
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
static robo_result_t layout_text_style_measure(void *userdata,
                                              const robo_layout_text_request_t *request,
                                              robo_layout_text_run_t *out_run)
{
    layout_text_script_t *script = (layout_text_script_t *)userdata;
    int32_t size;

    if (!script || !request || !out_run || !request->style) {
        return ROBO_INVALID_ARGUMENT;
    }
    script->calls++;
    size = (int32_t)(request->style->font_size_px + 0.5);
    out_run->source = request->text_node;
    out_run->range = request->range;
    out_run->direction = request->style->direction;
    out_run->code_points = robo_layout_text_range_length(request->range);
    out_run->width = (int32_t)out_run->code_points * size;
    out_run->height = size;
    out_run->ascent = size;
    out_run->descent = 0;
    return ROBO_OK;
}

static robo_layout_text_metrics_t layout_text_style_metrics(layout_text_script_t *script)
{
    robo_layout_text_metrics_t metrics;

    robo_layout_text_metrics_init(&metrics);
    metrics.userdata = script;
    metrics.measure = layout_text_style_measure;
    return metrics;
}

/* ------------------------------------------------------------------ */
/* A synthetic face, built here                                        */
/* ------------------------------------------------------------------ */

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

typedef struct {
    uint32_t tag;
    const uint8_t *data;
    uint32_t length;
} text_synth_table_t;

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

static size_t text_build_hhea(uint8_t *b, uint16_t n_h_metrics, int16_t asc, int16_t desc,
                              int16_t gap)
{
    uint8_t *p = b;
    int i;

    text_put_u16(&p, 1);
    text_put_u16(&p, 0);
    text_put_i16(&p, asc);
    text_put_i16(&p, desc);
    text_put_i16(&p, gap);
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

/** `n` full horizontal metrics: every glyph has its own advance. */
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

static size_t text_build_loca(uint8_t *loca, uint16_t n)
{
    uint8_t *p = loca;
    uint16_t i;

    for (i = 0u; i <= n; i++) {
        text_put_u32(&p, 0u);
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
static size_t text_build_cmap(uint8_t *b, uint16_t first, uint16_t last, int16_t delta)
{
    uint8_t *p = b;
    uint8_t *length_ptr;
    uint8_t *subtable = b + 12;

    text_put_u16(&p, 0);  /* version */
    text_put_u16(&p, 1);  /* numTables */
    text_put_u16(&p, 3);  /* Windows */
    text_put_u16(&p, 1);  /* Unicode BMP */
    text_put_u32(&p, 12u); /* subtable offset */

    text_put_u16(&p, 4);  /* format */
    length_ptr = p;
    text_put_u16(&p, 0);  /* length, patched below */
    text_put_u16(&p, 0);  /* language */
    text_put_u16(&p, 4);  /* segCountX2: two segments */
    text_put_u16(&p, 4);  /* searchRange */
    text_put_u16(&p, 1);  /* entrySelector */
    text_put_u16(&p, 0);  /* rangeShift */
    text_put_u16(&p, last);
    text_put_u16(&p, 0xFFFFu);
    text_put_u16(&p, 0);  /* reservedPad */
    text_put_u16(&p, first);
    text_put_u16(&p, 0xFFFFu);
    text_put_i16(&p, delta);
    text_put_i16(&p, 1);
    text_put_u16(&p, 0);
    text_put_u16(&p, 0);

    {
        uint16_t length = (uint16_t)(p - subtable);

        length_ptr[0] = (uint8_t)(length >> 8);
        length_ptr[1] = (uint8_t)length;
    }
    return (size_t)(p - b);
}

static size_t text_build_post(uint8_t *b)
{
    uint8_t *p = b;

    text_put_u32(&p, 0x00030000u);
    text_put_u32(&p, 0u);
    text_put_i16(&p, -125);
    text_put_i16(&p, 50);
    text_put_u32(&p, 0u);
    text_put_u32(&p, 0u);
    text_put_u32(&p, 0u);
    text_put_u32(&p, 0u);
    text_put_u32(&p, 0u);
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
typedef struct layout_text_face {
    robo_font_source_t *source;
    robo_font_face_t *face;
} layout_text_face_t;

static void layout_text_face_create(layout_text_face_t *out)
{
    uint8_t head[64], hhea[64], maxp[64], hmtx[64], loca[64], glyf[8], post[64];
    uint8_t cmap[64];
    uint8_t font[1024];
    static const uint16_t advances[LAYOUT_TEXT_FACE_NUM_GLYPHS] = {
        (uint16_t)LAYOUT_TEXT_GLYPH_NOTDEF_ADVANCE,
        (uint16_t)LAYOUT_TEXT_GLYPH_A_ADVANCE,
        (uint16_t)LAYOUT_TEXT_GLYPH_B_ADVANCE
    };
    text_synth_table_t tables[8];
    uint32_t nt = 0u;
    size_t n;

    out->source = NULL;
    out->face = NULL;

    tables[nt].tag = ROBO_FONT_TAG_HEAD;
    tables[nt].data = head;
    tables[nt].length = (uint32_t)text_build_head(head, (uint16_t)LAYOUT_TEXT_FACE_UPEM);
    nt++;
    tables[nt].tag = ROBO_FONT_TAG_HHEA;
    tables[nt].data = hhea;
    tables[nt].length = (uint32_t)text_build_hhea(hhea, LAYOUT_TEXT_FACE_NUM_GLYPHS, 800, -200,
                                                  90);
    nt++;
    tables[nt].tag = ROBO_FONT_TAG_MAXP;
    tables[nt].data = maxp;
    tables[nt].length = (uint32_t)text_build_maxp(maxp, LAYOUT_TEXT_FACE_NUM_GLYPHS);
    nt++;
    tables[nt].tag = ROBO_FONT_TAG_HMTX;
    tables[nt].data = hmtx;
    tables[nt].length = (uint32_t)text_build_hmtx(hmtx, advances, LAYOUT_TEXT_FACE_NUM_GLYPHS);
    nt++;
    tables[nt].tag = ROBO_FONT_TAG_CMAP;
    tables[nt].data = cmap;
    /* 'A' (0x41) -> glyph 1, 'B' (0x42) -> glyph 2, one segment, delta 1 - 0x41. */
    tables[nt].length = (uint32_t)text_build_cmap(cmap, 0x41u, 0x42u, (int16_t)(1 - 0x41));
    nt++;
    tables[nt].tag = ROBO_FONT_TAG_LOCA;
    tables[nt].data = loca;
    tables[nt].length = (uint32_t)text_build_loca(loca, LAYOUT_TEXT_FACE_NUM_GLYPHS);
    nt++;
    tables[nt].tag = ROBO_FONT_TAG_GLYF;
    tables[nt].data = glyf;
    tables[nt].length = 0u; /* outlines are not what a measurement reads */
    nt++;
    tables[nt].tag = ROBO_FONT_TAG_POST;
    tables[nt].data = post;
    tables[nt].length = (uint32_t)text_build_post(post);
    nt++;

    n = text_build_sfnt(font, sizeof(font), ROBO_FONT_SFNT_VERSION_TRUE, tables, nt);
    ROBO_CHECK(n > 0u);
    ROBO_CHECK_EQ_INT(robo_font_source_create_memory_copy(font, n, &out->source), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_font_face_create(out->source, 0, &out->face), ROBO_OK);
}

static void layout_text_face_destroy(layout_text_face_t *face)
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
/* The fixture                                                         */
/* ------------------------------------------------------------------ */

typedef struct layout_text_rule {
    const char *tag;
    robo_css_display_t display;
} layout_text_rule_t;

#define LAYOUT_TEXT_MAX_RULES 8u

typedef struct layout_text_style {
    char tag[16];
    robo_css_computed_style_t *style; /* owned */
} layout_text_style_t;

typedef struct layout_text_fixture {
    layout_document_t doc;
    layout_text_style_t rules[LAYOUT_TEXT_MAX_RULES];
    size_t rule_count;
    robo_layout_style_source_t styles;
    robo_layout_tree_t *tree;
} layout_text_fixture_t;

static const robo_css_computed_style_t *layout_text_style_lookup(
    void *userdata,
    const robo_html_node_handle_t *element)
{
    layout_text_fixture_t *fixture = (layout_text_fixture_t *)userdata;
    const char *name;
    size_t i;

    if (!fixture || !element) {
        return NULL;
    }
    name = robo_html_element_name(element, NULL);
    if (!name || name[0] == '\0') {
        return NULL;
    }
    for (i = 0u; i < fixture->rule_count; i++) {
        if (strcmp(fixture->rules[i].tag, name) == 0) {
            return fixture->rules[i].style;
        }
    }
    /* Nothing declared: the *initial* display, which is `inline`. Same rule the other
     * suites state -- a fixture's geometry depends on what the test wrote. */
    return NULL;
}

static void layout_text_style_add(layout_text_fixture_t *fixture,
                                 const layout_text_rule_t *rule)
{
    robo_css_computed_style_t *style = NULL;
    robo_css_computed_value_t value = ROBO_CSS_CLEARED;
    robo_css_value_t specified = ROBO_CSS_CLEARED;
    size_t existing;

    layout_fixture();
    ROBO_CHECK_EQ_INT(fixture->rule_count < LAYOUT_TEXT_MAX_RULES, 1);
    for (existing = 0u; existing < fixture->rule_count; existing++) {
        ROBO_CHECK(strcmp(fixture->rules[existing].tag, rule->tag) != 0);
    }
    ROBO_CHECK_EQ_INT(robo_css_computed_style_create(&style, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_css_computed_value_make_display(&value, rule->display, NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_css_computed_style_set_entry(style, ROBO_CSS_PROPERTY_ID_NONE,
                                                       robo_css_slice_from_text("display"),
                                                       0u, &specified, &value, NULL),
                      ROBO_OK);
    strncpy(fixture->rules[fixture->rule_count].tag, rule->tag,
            sizeof(fixture->rules[fixture->rule_count].tag) - 1u);
    fixture->rules[fixture->rule_count].tag[sizeof(fixture->rules[fixture->rule_count].tag) -
                                            1u] = '\0';
    fixture->rules[fixture->rule_count].style = style;
    fixture->rule_count++;
}

static void layout_text_begin(layout_text_fixture_t *fixture,
                             const char *html,
                             const layout_text_rule_t *rules,
                             size_t count)
{
    robo_layout_build_options_t options;
    size_t index;

    memset(fixture, 0, sizeof(*fixture));
    layout_document_build(&fixture->doc, html);
    for (index = 0u; index < count; index++) {
        layout_text_style_add(fixture, &rules[index]);
    }
    robo_layout_style_source_init(&fixture->styles);
    fixture->styles.userdata = fixture;
    fixture->styles.computed_style_for = layout_text_style_lookup;
    ROBO_CHECK_EQ_INT(robo_layout_tree_create(&fixture->tree, NULL), ROBO_OK);
    robo_layout_build_options_init(&options);
    options.styles = &fixture->styles;
    ROBO_CHECK_EQ_INT(robo_layout_tree_build_from_document(fixture->tree, fixture->doc.id,
                                                          &options, NULL, NULL),
                      ROBO_OK);
}

static void layout_text_end(layout_text_fixture_t *fixture)
{
    size_t index;

    robo_layout_tree_destroy(fixture->tree);
    fixture->tree = NULL;
    for (index = 0u; index < fixture->rule_count; index++) {
        robo_css_computed_style_destroy(fixture->rules[index].style);
        fixture->rules[index].style = NULL;
    }
    fixture->rule_count = 0u;
    layout_document_release(&fixture->doc);
}

/** One pass, with a viewport and optionally a measurer installed on the context. */
static void layout_text_pass(layout_text_fixture_t *fixture,
                            int32_t viewport_width,
                            const robo_layout_text_metrics_t *metrics,
                            robo_layout_stats_t *out_stats,
                            robo_result_t *out_status)
{
    robo_layout_context_t *context = NULL;
    robo_layout_size_t viewport;
    robo_layout_stats_t stats;
    robo_result_t status;

    viewport.width = viewport_width;
    viewport.height = 600;
    ROBO_CHECK_EQ_INT(robo_layout_context_create(&context, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_layout_context_set_viewport(context, viewport, NULL), ROBO_OK);
    if (metrics) {
        ROBO_CHECK_EQ_INT(robo_layout_context_set_text_metrics(context, metrics), ROBO_OK);
    }
    status = robo_layout_context_run(context, fixture->tree, NULL);
    robo_layout_context_stats(context, &stats);
    robo_layout_context_destroy(context);
    if (out_stats) {
        *out_stats = stats;
    }
    if (out_status) {
        *out_status = status;
    }
}

/** A viewport wider than any fixture's content, so a case only wraps on purpose. */
#define LAYOUT_TEXT_WIDE_VIEWPORT 800

/** The block container every fixture below shares: `<html><body><p>`. */
#define LAYOUT_TEXT_BLOCK_RULES                         \
    { "html", ROBO_CSS_DISPLAY_BLOCK },                 \
    { "body", ROBO_CSS_DISPLAY_BLOCK },                 \
    { "p", ROBO_CSS_DISPLAY_BLOCK }

/** The box built for the first element named @p tag. */
static robo_layout_node_id_t layout_text_box_of(layout_text_fixture_t *fixture,
                                               const char *tag)
{
    robo_html_node_handle_t element = layout_document_find(&fixture->doc, tag);

    ROBO_CHECK_EQ_INT(robo_html_node_handle_is_wellformed(&element), 1);
    return robo_layout_object_id_for_dom_node(fixture->tree, &element);
}

/**
 * The first **text node** inside the first element named @p tag.
 *
 * The DOM's own walk rather than a target format, because the question a case asks is
 * "which text node is under this element", and the parser decides that.
 */
static robo_html_node_handle_t layout_text_first_text_child(
    const robo_html_node_handle_t *element)
{
    robo_html_node_view_t view;
    robo_html_node_handle_t child;

    robo_html_node_handle_clear(&child);
    if (!element || !robo_html_node_handle_is_wellformed(element)) {
        return child;
    }
    if (robo_html_node_get_view(element, &view, NULL) != ROBO_OK) {
        return child;
    }
    child = view.first_child;
    while (robo_html_node_handle_is_wellformed(&child)) {
        robo_html_node_view_t child_view;

        if (robo_html_node_get_view(&child, &child_view, NULL) != ROBO_OK) {
            break;
        }
        if (child_view.kind == ROBO_HTML_NODE_KIND_TEXT) {
            return child;
        }
        child = child_view.next_sibling;
    }
    robo_html_node_handle_clear(&child);
    return child;
}

static robo_html_node_handle_t layout_text_node_of(layout_text_fixture_t *fixture,
                                                   const char *tag)
{
    robo_html_node_handle_t element = layout_document_find(&fixture->doc, tag);

    return layout_text_first_text_child(&element);
}

/** The layout object a text node became. */
static robo_layout_node_id_t layout_text_object_of(layout_text_fixture_t *fixture,
                                                   const char *tag)
{
    robo_html_node_handle_t node = layout_text_node_of(fixture, tag);

    ROBO_CHECK_EQ_INT(robo_html_node_handle_is_wellformed(&node), 1);
    return robo_layout_object_id_for_dom_node(fixture->tree, &node);
}

/** Assert one object's geometry, and that the geometry is *known*. */
static void layout_text_expect_in(const robo_layout_tree_t *tree,
                                  robo_layout_node_id_t id,
                                  int32_t x,
                                  int32_t y,
                                  int32_t width,
                                  int32_t height)
{
    robo_layout_result_t result;
    robo_layout_point_t position;
    robo_layout_size_t size;

    ROBO_CHECK_EQ_INT(robo_layout_object_result(tree, id, &result), ROBO_OK);
    ROBO_CHECK_EQ_INT((int)result.state, (int)ROBO_LAYOUT_RESULT_COMPUTED);
    ROBO_CHECK_EQ_INT(robo_layout_result_position(&result, &position), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_layout_result_size(&result, &size), ROBO_OK);
    ROBO_CHECK_EQ_INT(position.x, x);
    ROBO_CHECK_EQ_INT(position.y, y);
    ROBO_CHECK_EQ_INT(size.width, width);
    ROBO_CHECK_EQ_INT(size.height, height);
}

static void layout_text_expect(layout_text_fixture_t *fixture,
                              robo_layout_node_id_t id,
                              int32_t x,
                              int32_t y,
                              int32_t width,
                              int32_t height)
{
    layout_text_expect_in(fixture->tree, id, x, y, width, height);
}

static void layout_text_expect_unresolved(layout_text_fixture_t *fixture,
                                          robo_layout_node_id_t id)
{
    robo_layout_result_t result;

    ROBO_CHECK_EQ_INT(robo_layout_object_result(fixture->tree, id, &result), ROBO_OK);
    ROBO_CHECK_EQ_INT((int)result.state, (int)ROBO_LAYOUT_RESULT_UNRESOLVED);
}

/** A request built by hand, for the cases that drive the seam directly. */
static robo_layout_text_request_t layout_text_request(const robo_html_node_handle_t *node,
                                                      robo_layout_text_range_t range,
                                                      const robo_layout_text_style_t *style)
{
    robo_layout_text_request_t request;

    memset(&request, 0, sizeof(request));
    request.text_node = *node;
    request.range = range;
    request.style = style;
    return request;
}

/* ------------------------------------------------------------------ */
/* The model                                                           */
/* ------------------------------------------------------------------ */

static void test_text_range(void)
{
    robo_layout_text_range_t range;

    range = robo_layout_text_range_at(3u);
    ROBO_CHECK_EQ_INT(range.start, 3u);
    ROBO_CHECK_EQ_INT(range.end, 3u);
    ROBO_CHECK_EQ_INT(robo_layout_text_range_is_empty(range), 1);
    ROBO_CHECK_EQ_INT(robo_layout_text_range_length(range), 0u);
    /* An empty range is a *position*, and a position is not inverted: the distinction
     * is what makes a caret addressable. */
    ROBO_CHECK_EQ_INT(robo_layout_text_range_is_inverted(range), 0);
    ROBO_CHECK_EQ_INT(robo_layout_text_range_is_valid(range, 3u), 1);

    range = robo_layout_text_range_full(4u);
    ROBO_CHECK_EQ_INT(range.start, 0u);
    ROBO_CHECK_EQ_INT(range.end, 4u);
    ROBO_CHECK_EQ_INT(robo_layout_text_range_length(range), 4u);
    ROBO_CHECK_EQ_INT(robo_layout_text_range_is_empty(range), 0);
    ROBO_CHECK_EQ_INT(robo_layout_text_range_is_valid(range, 4u), 1);
    /* One past the last code point is not inside the text. */
    ROBO_CHECK_EQ_INT(robo_layout_text_range_is_valid(range, 3u), 0);
    ROBO_CHECK_EQ_INT(robo_layout_text_range_contains(range, 0u), 1);
    ROBO_CHECK_EQ_INT(robo_layout_text_range_contains(range, 3u), 1);
    ROBO_CHECK_EQ_INT(robo_layout_text_range_contains(range, 4u), 0);
    ROBO_CHECK_EQ_INT(robo_layout_text_range_contains(range, 99u), 0);

    /* An inverted range is not a span of anything, however long the text is. */
    range.start = 5u;
    range.end = 2u;
    ROBO_CHECK_EQ_INT(robo_layout_text_range_is_inverted(range), 1);
    ROBO_CHECK_EQ_INT(robo_layout_text_range_is_empty(range), 1);
    ROBO_CHECK_EQ_INT(robo_layout_text_range_length(range), 0u);
    ROBO_CHECK_EQ_INT(robo_layout_text_range_is_valid(range, 100u), 0);
}

static void test_text_direction(void)
{
    ROBO_CHECK_EQ_INT(robo_layout_text_direction_is_known(ROBO_LAYOUT_TEXT_DIRECTION_LTR), 1);
    ROBO_CHECK_EQ_INT(robo_layout_text_direction_is_known(ROBO_LAYOUT_TEXT_DIRECTION_RTL), 1);
    ROBO_CHECK_EQ_INT(robo_layout_text_direction_is_known((robo_layout_text_direction_t)7), 0);
    ROBO_CHECK(strcmp(robo_layout_text_direction_name(ROBO_LAYOUT_TEXT_DIRECTION_LTR), "ltr") ==
               0);
    ROBO_CHECK(strcmp(robo_layout_text_direction_name(ROBO_LAYOUT_TEXT_DIRECTION_RTL), "rtl") ==
               0);
    ROBO_CHECK(strcmp(robo_layout_text_direction_name((robo_layout_text_direction_t)7),
                      "unknown") == 0);
    ROBO_CHECK_EQ_INT((int)ROBO_LAYOUT_TEXT_DIRECTION_COUNT, 2);
}

static void test_text_style_initial(void)
{
    robo_layout_text_style_t style;

    robo_layout_text_style_init(NULL); /* documented as safe */
    robo_layout_text_style_init(&style);
    ROBO_CHECK(style.font_size_px == ROBO_LAYOUT_TEXT_INITIAL_FONT_SIZE_PX);
    ROBO_CHECK_EQ_INT(style.font_weight, ROBO_LAYOUT_TEXT_WEIGHT_NORMAL);
    ROBO_CHECK_EQ_INT(style.italic, 0);
    ROBO_CHECK_EQ_INT((int)style.direction, (int)ROBO_LAYOUT_TEXT_DIRECTION_LTR);

    /* Nothing computed at all is not a failure: it is the property's own initial
     * value, exactly as a style-less element computes `display: inline`. */
    ROBO_CHECK_EQ_INT(robo_layout_text_style_resolve(NULL, &style), ROBO_OK);
    ROBO_CHECK(style.font_size_px == ROBO_LAYOUT_TEXT_INITIAL_FONT_SIZE_PX);
    ROBO_CHECK_EQ_INT(style.font_weight, ROBO_LAYOUT_TEXT_WEIGHT_NORMAL);
    ROBO_CHECK_EQ_INT(style.italic, 0);
    ROBO_CHECK_EQ_INT((int)style.direction, (int)ROBO_LAYOUT_TEXT_DIRECTION_LTR);
    ROBO_CHECK_EQ_INT(robo_layout_text_style_resolve(NULL, NULL), ROBO_INVALID_ARGUMENT);
}

/** One style built from real computed values, through 9.8's own makers. */
static void layout_text_style_set_length(robo_css_computed_style_t *style,
                                         const char *property,
                                         double number,
                                         robo_css_unit_t unit)
{
    robo_css_computed_value_t value = ROBO_CSS_CLEARED;
    robo_css_value_t specified = ROBO_CSS_CLEARED;

    ROBO_CHECK_EQ_INT(robo_css_computed_value_make_length(&value, number, unit, 0, NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_css_computed_style_set_entry(style, ROBO_CSS_PROPERTY_ID_NONE,
                                                       robo_css_slice_from_text(property), 0u,
                                                       &specified, &value, NULL),
                      ROBO_OK);
}

static void layout_text_style_set_keyword(robo_css_computed_style_t *style,
                                          const char *property,
                                          const char *keyword)
{
    robo_css_computed_value_t value = ROBO_CSS_CLEARED;
    robo_css_value_t specified = ROBO_CSS_CLEARED;

    ROBO_CHECK_EQ_INT(robo_css_computed_value_make_keyword(&value,
                                                          robo_css_slice_from_text(keyword),
                                                          NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_css_computed_style_set_entry(style, ROBO_CSS_PROPERTY_ID_NONE,
                                                       robo_css_slice_from_text(property), 0u,
                                                       &specified, &value, NULL),
                      ROBO_OK);
}

static void layout_text_style_set_number(robo_css_computed_style_t *style,
                                         const char *property,
                                         double number)
{
    robo_css_computed_value_t value = ROBO_CSS_CLEARED;
    robo_css_value_t specified = ROBO_CSS_CLEARED;

    ROBO_CHECK_EQ_INT(robo_css_computed_value_make_number(&value, number, 1, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_css_computed_style_set_entry(style, ROBO_CSS_PROPERTY_ID_NONE,
                                                       robo_css_slice_from_text(property), 0u,
                                                       &specified, &value, NULL),
                      ROBO_OK);
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
static void test_text_style_resolve(void)
{
    robo_css_computed_style_t *style = NULL;
    robo_layout_text_style_t resolved;

    layout_fixture();
    ROBO_CHECK_EQ_INT(robo_css_computed_style_create(&style, NULL), ROBO_OK);

    /* Defaults for an empty style: every property's initial value. */
    ROBO_CHECK_EQ_INT(robo_layout_text_style_resolve(style, &resolved), ROBO_OK);
    ROBO_CHECK(style && resolved.font_size_px == ROBO_LAYOUT_TEXT_INITIAL_FONT_SIZE_PX);
    ROBO_CHECK_EQ_INT(resolved.font_weight, ROBO_LAYOUT_TEXT_WEIGHT_NORMAL);
    ROBO_CHECK_EQ_INT(resolved.italic, 0);

    layout_text_style_set_length(style, "font-size", 24.0, ROBO_CSS_UNIT_PX);
    layout_text_style_set_number(style, "font-weight", 600.0);
    layout_text_style_set_keyword(style, "font-style", "italic");
    layout_text_style_set_keyword(style, "direction", "rtl");
    ROBO_CHECK_EQ_INT(robo_layout_text_style_resolve(style, &resolved), ROBO_OK);
    ROBO_CHECK(resolved.font_size_px == 24.0);
    ROBO_CHECK_EQ_INT(resolved.font_weight, 600);
    ROBO_CHECK_EQ_INT(resolved.italic, 1);
    ROBO_CHECK_EQ_INT((int)resolved.direction, (int)ROBO_LAYOUT_TEXT_DIRECTION_RTL);

    /* `oblique` is the same flag: CSS has two ways to say "not upright". */
    layout_text_style_set_keyword(style, "font-style", "oblique");
    ROBO_CHECK_EQ_INT(robo_layout_text_style_resolve(style, &resolved), ROBO_OK);
    ROBO_CHECK_EQ_INT(resolved.italic, 1);
    layout_text_style_set_keyword(style, "font-style", "normal");
    ROBO_CHECK_EQ_INT(robo_layout_text_style_resolve(style, &resolved), ROBO_OK);
    ROBO_CHECK_EQ_INT(resolved.italic, 0);

    /* The two keyword weights CSS defines, and the two relative ones resolved against
     * the initial weight because this record has no parent to be relative to. */
    layout_text_style_set_keyword(style, "font-weight", "bold");
    ROBO_CHECK_EQ_INT(robo_layout_text_style_resolve(style, &resolved), ROBO_OK);
    ROBO_CHECK_EQ_INT(resolved.font_weight, ROBO_LAYOUT_TEXT_WEIGHT_BOLD);
    layout_text_style_set_keyword(style, "font-weight", "normal");
    ROBO_CHECK_EQ_INT(robo_layout_text_style_resolve(style, &resolved), ROBO_OK);
    ROBO_CHECK_EQ_INT(resolved.font_weight, ROBO_LAYOUT_TEXT_WEIGHT_NORMAL);

    /* A weight the font could not have is clamped into CSS's own range rather than
     * passed through: `font-weight: 5000` must not ask a face for a 5000 face. */
    layout_text_style_set_number(style, "font-weight", 5000.0);
    ROBO_CHECK_EQ_INT(robo_layout_text_style_resolve(style, &resolved), ROBO_OK);
    ROBO_CHECK_EQ_INT(resolved.font_weight, 1000);
    layout_text_style_set_number(style, "font-weight", 0.0);
    ROBO_CHECK_EQ_INT(robo_layout_text_style_resolve(style, &resolved), ROBO_OK);
    ROBO_CHECK_EQ_INT(resolved.font_weight, 1);

    /* A relative unit and a percentage are not resolvable here, and take the initial
     * value -- which is a CSS fact and not a fallback. */
    layout_text_style_set_length(style, "font-size", 2.0, ROBO_CSS_UNIT_EM);
    ROBO_CHECK_EQ_INT(robo_layout_text_style_resolve(style, &resolved), ROBO_OK);
    ROBO_CHECK(resolved.font_size_px == ROBO_LAYOUT_TEXT_INITIAL_FONT_SIZE_PX);
    {
        robo_css_computed_value_t value = ROBO_CSS_CLEARED;
        robo_css_value_t specified = ROBO_CSS_CLEARED;

        ROBO_CHECK_EQ_INT(robo_css_computed_value_make_percentage(&value, 50.0, 0, NULL),
                          ROBO_OK);
        ROBO_CHECK_EQ_INT(robo_css_computed_style_set_entry(style, ROBO_CSS_PROPERTY_ID_NONE,
                                                           robo_css_slice_from_text("font-size"),
                                                           0u, &specified, &value, NULL),
                          ROBO_OK);
    }
    ROBO_CHECK_EQ_INT(robo_layout_text_style_resolve(style, &resolved), ROBO_OK);
    ROBO_CHECK(resolved.font_size_px == ROBO_LAYOUT_TEXT_INITIAL_FONT_SIZE_PX);

    /* An absolute unit that RoboCss normalises to px *is* usable, which is 9.8's rule
     * showing up in a font size. */
    layout_text_style_set_length(style, "font-size", 1.0, ROBO_CSS_UNIT_IN);
    ROBO_CHECK_EQ_INT(robo_layout_text_style_resolve(style, &resolved), ROBO_OK);
    ROBO_CHECK(resolved.font_size_px > 0.0);

    robo_css_computed_style_destroy(style);
}

static void test_text_run_and_metrics_record(void)
{
    robo_layout_text_run_t run;
    robo_layout_text_metrics_t metrics;

    robo_layout_text_run_init(&run);
    ROBO_CHECK_EQ_INT(robo_html_node_handle_is_wellformed(&run.source), 0);
    ROBO_CHECK_EQ_INT(robo_layout_text_range_is_empty(run.range), 1);
    ROBO_CHECK_EQ_INT((int)run.direction, (int)ROBO_LAYOUT_TEXT_DIRECTION_LTR);
    ROBO_CHECK_EQ_INT(run.width, 0);
    ROBO_CHECK_EQ_INT(run.height, 0);
    ROBO_CHECK_EQ_INT(run.ascent, 0);
    ROBO_CHECK_EQ_INT(run.descent, 0);
    ROBO_CHECK_EQ_INT(run.code_points, 0u);
    ROBO_CHECK_EQ_INT(run.glyphs_missing, 0u);
    robo_layout_text_run_init(NULL); /* documented as safe */

    robo_layout_text_metrics_init(&metrics);
    ROBO_CHECK_EQ_INT(robo_layout_text_metrics_is_usable(&metrics), 0);
    ROBO_CHECK_EQ_INT(robo_layout_text_metrics_is_usable(NULL), 0);
    metrics.userdata = NULL;
    metrics.measure = layout_text_script_measure;
    ROBO_CHECK_EQ_INT(robo_layout_text_metrics_is_usable(&metrics), 1);
    robo_layout_text_metrics_init(NULL); /* documented as safe */
}

/** The text-side stats record, which is where "did anything measure?" lives. */
static void test_text_stats_record(void)
{
    robo_layout_text_stats_t a, b;

    robo_layout_text_stats_init(&a);
    robo_layout_text_stats_init(&b);
    ROBO_CHECK_EQ_INT(robo_layout_text_stats_equal(&a, &b), 1);
    ROBO_CHECK_EQ_INT(a.runs_placed, 0u);
    ROBO_CHECK_EQ_INT(a.measured, 0u);
    ROBO_CHECK_EQ_INT(a.unmeasured, 0u);
    ROBO_CHECK_EQ_INT(a.empty, 0u);
    ROBO_CHECK_EQ_INT(a.code_points, 0u);
    ROBO_CHECK_EQ_INT(a.glyphs_missing, 0u);
    ROBO_CHECK_EQ_INT(a.rtl, 0u);
    ROBO_CHECK_EQ_INT(a.width_max, 0);
    ROBO_CHECK_EQ_INT(a.height_max, 0);
    ROBO_CHECK_EQ_INT(a.depth_max, 0u);

    a.width_max = 10;
    ROBO_CHECK_EQ_INT(robo_layout_text_stats_equal(&a, &b), 0);
    b.width_max = 10;
    ROBO_CHECK_EQ_INT(robo_layout_text_stats_equal(&a, &b), 1);
    /* Every field counts, and the two that are easy to forget are the two that carry
     * the phase's honesty: an unmeasured run's count and a glyph the font lacked. */
    b.unmeasured = 1u;
    ROBO_CHECK_EQ_INT(robo_layout_text_stats_equal(&a, &b), 0);
    b.unmeasured = 0u;
    b.glyphs_missing = 1u;
    ROBO_CHECK_EQ_INT(robo_layout_text_stats_equal(&a, &b), 0);
    b.glyphs_missing = 0u;
    b.rtl = 1u;
    ROBO_CHECK_EQ_INT(robo_layout_text_stats_equal(&a, &b), 0);
    b.rtl = 0u;
    b.depth_max = 3u;
    ROBO_CHECK_EQ_INT(robo_layout_text_stats_equal(&a, &b), 0);

    ROBO_CHECK_EQ_INT(robo_layout_text_stats_equal(NULL, NULL), 1);
    ROBO_CHECK_EQ_INT(robo_layout_text_stats_equal(&a, NULL), 0);
    ROBO_CHECK_EQ_INT(robo_layout_text_stats_equal(NULL, &a), 0);
    robo_layout_text_stats_init(NULL); /* documented as safe */
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
static void test_text_code_points(void)
{
    layout_text_fixture_t fixture;
    static const layout_text_rule_t rules[] = { LAYOUT_TEXT_BLOCK_RULES };
    robo_html_node_handle_t node;
    uint32_t count = 0u;

    layout_fixture();
    layout_text_begin(&fixture, "<html><body><p>h\xC3\xA9llo \xD8\xB3\xD9\x84\xD8\xA7\xD9\x85"
                                " \xF0\x9F\x91\x8B</p></body></html>",
                      rules, 3u);

    node = layout_text_node_of(&fixture, "p");
    ROBO_CHECK_EQ_INT(robo_html_node_handle_is_wellformed(&node), 1);
    /* "héllo سلام 👋" is 5 + 1 + 4 + 1 + 1 code points. */
    ROBO_CHECK_EQ_INT(robo_layout_text_node_code_points(&node, &count, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(count, 12u);

    /* A one-byte text node answers one, so the count is not accidentally constant. */
    layout_text_end(&fixture);
    layout_text_begin(&fixture, "<html><body><p>A</p></body></html>", rules, 3u);
    node = layout_text_node_of(&fixture, "p");
    ROBO_CHECK_EQ_INT(robo_layout_text_node_code_points(&node, &count, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(count, 1u);

    /* A node handle that names an element is refused rather than read: an element has
     * no text payload, and answering zero would look like an empty text node. */
    {
        robo_html_node_handle_t element = layout_document_find(&fixture.doc, "p");

        ROBO_CHECK_EQ_INT(robo_layout_text_node_code_points(&element, &count, NULL),
                          ROBO_INVALID_ARGUMENT);
    }
    ROBO_CHECK_EQ_INT(robo_layout_text_node_code_points(&node, NULL, NULL),
                      ROBO_INVALID_ARGUMENT);
    node = layout_text_node_of(&fixture, "nosuch");
    ROBO_CHECK_EQ_INT(robo_html_node_handle_is_wellformed(&node), 0);
    ROBO_CHECK_EQ_INT(robo_layout_text_node_code_points(&node, &count, NULL),
                      ROBO_INVALID_ARGUMENT);

    layout_text_end(&fixture);
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
static void test_text_range_bytes(void)
{
    layout_text_fixture_t fixture;
    static const layout_text_rule_t rules[] = { LAYOUT_TEXT_BLOCK_RULES };
    robo_html_node_handle_t node;
    size_t start = 0u, length = 0u;

    layout_fixture();
    layout_text_begin(&fixture, "<html><body><p>h\xC3\xA9llo</p></body></html>", rules, 3u);
    node = layout_text_node_of(&fixture, "p");
    ROBO_CHECK_EQ_INT(robo_layout_text_node_code_points(&node, NULL, NULL),
                      ROBO_INVALID_ARGUMENT);

    /* The whole run: 5 code points, 6 bytes. */
    ROBO_CHECK_EQ_INT(robo_layout_text_range_bytes(&node, robo_layout_text_range_full(5u),
                                                  &start, &length, NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT((int)start, 0);
    ROBO_CHECK_EQ_INT((int)length, 6);

    /* The `é` alone. */
    {
        robo_layout_text_range_t range;

        range.start = 1u;
        range.end = 2u;
        ROBO_CHECK_EQ_INT(robo_layout_text_range_bytes(&node, range, &start, &length, NULL),
                          ROBO_OK);
        ROBO_CHECK_EQ_INT((int)start, 1);
        ROBO_CHECK_EQ_INT((int)length, 2);
    }

    /* `llo` -- two bytes of accented letter skipped, then three plain ones. */
    {
        robo_layout_text_range_t range;

        range.start = 2u;
        range.end = 5u;
        ROBO_CHECK_EQ_INT(robo_layout_text_range_bytes(&node, range, &start, &length, NULL),
                          ROBO_OK);
        ROBO_CHECK_EQ_INT((int)start, 3);
        ROBO_CHECK_EQ_INT((int)length, 3);
    }

    /* An empty range is a caret: a zero-length span at the right byte offset. */
    {
        robo_layout_text_range_t caret = robo_layout_text_range_at(2u);

        ROBO_CHECK_EQ_INT(robo_layout_text_range_bytes(&node, caret, &start, &length, NULL),
                          ROBO_OK);
        ROBO_CHECK_EQ_INT((int)start, 3);
        ROBO_CHECK_EQ_INT((int)length, 0);
    }

    /* Past the end, beyond what the node holds, and inverted: all three are refused --
     * and the two outs are cleared by the refusal rather than left holding the last
     * successful answer. */
    ROBO_CHECK_EQ_INT(robo_layout_text_range_bytes(&node, robo_layout_text_range_full(6u),
                                                  &start, &length, NULL),
                      ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT((int)start, 0);
    ROBO_CHECK_EQ_INT((int)length, 0);
    {
        robo_layout_text_range_t range;

        range.start = 4u;
        range.end = 2u;
        ROBO_CHECK_EQ_INT(robo_layout_text_range_bytes(&node, range, &start, &length, NULL),
                          ROBO_INVALID_ARGUMENT);
    }
    ROBO_CHECK_EQ_INT(robo_layout_text_range_bytes(&node, robo_layout_text_range_full(5u),
                                                  NULL, &length, NULL),
                      ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_layout_text_range_bytes(&node, robo_layout_text_range_full(5u),
                                                  &start, NULL, NULL),
                      ROBO_INVALID_ARGUMENT);

    layout_text_end(&fixture);

    /* A four-byte emoji, so the bridge is asserted at every UTF-8 width and not only at
     * the two-byte one. */
    layout_text_begin(&fixture, "<html><body><p>\xF0\x9F\x91\x8B</p></body></html>", rules,
                      3u);
    node = layout_text_node_of(&fixture, "p");
    ROBO_CHECK_EQ_INT(robo_layout_text_range_bytes(&node, robo_layout_text_range_full(1u),
                                                  &start, &length, NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT((int)start, 0);
    ROBO_CHECK_EQ_INT((int)length, 4);
    layout_text_end(&fixture);
}

static void test_text_whitespace(void)
{
    ROBO_CHECK_EQ_INT(robo_layout_text_is_collapsible_whitespace("", 0u), 1);
    ROBO_CHECK_EQ_INT(robo_layout_text_is_collapsible_whitespace(NULL, 0u), 1);
    ROBO_CHECK_EQ_INT(robo_layout_text_is_collapsible_whitespace(" \t\n\f\r", 5u), 1);
    ROBO_CHECK_EQ_INT(robo_layout_text_is_collapsible_whitespace(" \tA ", 4u), 0);
    /* CSS's own five, and *only* those five: a non-breaking space is content. */
    ROBO_CHECK_EQ_INT(robo_layout_text_is_collapsible_whitespace("\xC2\xA0", 2u), 0);
    /* A prefix that is whitespace is not the whole of it. */
    ROBO_CHECK_EQ_INT(robo_layout_text_is_collapsible_whitespace("   x", 4u), 0);
}

/* ------------------------------------------------------------------ */
/* The seam                                                            */
/* ------------------------------------------------------------------ */

/**
 * The seam's four answers, which is the whole of its contract.
 *
 *   no source         a zero run with the source and range echoed, and ROBO_OK --
 *                     the documented degradation, and what keeps a build with no face
 *                     laying a document out exactly as 10.4 did;
 *   a source that     the same, because "no callback" is not a failure either;
 *     answers nothing
 *   a refusal         ROBO_INVALID_ARGUMENT, always *before* the source is consulted,
 *                     so a caller's bug is not reported as the measurer's;
 *   a failure         whatever the callback returned, with the run cleared first so a
 *                     failure never leaves a half-filled record.
 */
static void test_text_seam(void)
{
    layout_text_fixture_t fixture;
    static const layout_text_rule_t rules[] = { LAYOUT_TEXT_BLOCK_RULES };
    robo_html_node_handle_t node;
    robo_layout_text_request_t request;
    robo_layout_text_style_t style;
    robo_layout_text_run_t run;
    layout_text_script_t script;
    robo_layout_text_metrics_t metrics;
    robo_layout_text_metrics_t empty;

    layout_fixture();
    layout_text_begin(&fixture, "<html><body><p>AB</p></body></html>", rules, 3u);
    node = layout_text_node_of(&fixture, "p");
    robo_layout_text_style_init(&style);
    request = layout_text_request(&node, robo_layout_text_range_full(2u), &style);

    memset(&script, 0, sizeof(script));
    metrics = layout_text_script_metrics(&script);
    robo_layout_text_metrics_init(&empty);

    /* No source at all. */
    ROBO_CHECK_EQ_INT(robo_layout_text_measure(NULL, &request, &run, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(run.width, 0);
    ROBO_CHECK_EQ_INT(run.height, 0);
    ROBO_CHECK_EQ_INT(run.code_points, 0u);
    ROBO_CHECK_EQ_INT(robo_layout_text_range_length(run.range), 2u);
    ROBO_CHECK_EQ_INT(robo_html_node_handle_equal(&run.source, &node), 1);
    ROBO_CHECK_EQ_INT(script.calls, 0u);

    /* A source whose callback is NULL: the same answer, and the callback still not
     * called -- which is the distinction the pass counts as *unmeasured*. */
    ROBO_CHECK_EQ_INT(robo_layout_text_measure(&empty, &request, &run, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(run.width, 0);
    ROBO_CHECK_EQ_INT(script.calls, 0u);

    /* A source that answers. */
    script.per_code_point = 7;
    script.height = 11;
    script.ascent = 9;
    script.descent = 2;
    ROBO_CHECK_EQ_INT(robo_layout_text_measure(&metrics, &request, &run, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(script.calls, 1u);
    ROBO_CHECK_EQ_INT(run.width, 14);
    ROBO_CHECK_EQ_INT(run.height, 11);
    ROBO_CHECK_EQ_INT(run.ascent, 9);
    ROBO_CHECK_EQ_INT(run.descent, 2);
    ROBO_CHECK_EQ_INT(run.code_points, 2u);

    /* Refusals, each of which must leave the run cleared rather than half-filled. */
    ROBO_CHECK_EQ_INT(robo_layout_text_measure(&metrics, NULL, &run, NULL),
                      ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(run.width, 0);
    ROBO_CHECK_EQ_INT(robo_layout_text_measure(&metrics, &request, NULL, NULL),
                      ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_layout_text_measure(NULL, NULL, &run, NULL),
                      ROBO_INVALID_ARGUMENT);
    {
        robo_html_node_handle_t unformed;
        robo_layout_text_request_t bad;

        robo_html_node_handle_clear(&unformed);
        bad = layout_text_request(&unformed, robo_layout_text_range_full(0u), &style);
        ROBO_CHECK_EQ_INT(robo_layout_text_measure(&metrics, &bad, &run, NULL),
                          ROBO_INVALID_ARGUMENT);
        bad = layout_text_request(&node, robo_layout_text_range_full(2u), NULL);
        ROBO_CHECK_EQ_INT(robo_layout_text_measure(&metrics, &bad, &run, NULL),
                          ROBO_INVALID_ARGUMENT);
        ROBO_CHECK_EQ_INT(script.calls, 1u);
    }

    /* A failure from the source travels, and the run is cleared. */
    script.fail = 1;
    ROBO_CHECK_EQ_INT(robo_layout_text_measure(&metrics, &request, &run, NULL), ROBO_ERROR);
    ROBO_CHECK_EQ_INT(run.width, 0);
    ROBO_CHECK_EQ_INT(script.calls, 2u);

    layout_text_end(&fixture);
}

/** `measure_node`: the whole node, the owner's style, the initial values when there is
 *  none. */
static void test_text_measure_node(void)
{
    layout_text_fixture_t fixture;
    static const layout_text_rule_t rules[] = { LAYOUT_TEXT_BLOCK_RULES };
    robo_html_node_handle_t node;
    robo_layout_text_run_t run;
    layout_text_script_t script;
    robo_layout_text_metrics_t metrics;

    layout_fixture();
    layout_text_begin(&fixture, "<html><body><p>AB</p></body></html>", rules, 3u);
    node = layout_text_node_of(&fixture, "p");

    memset(&script, 0, sizeof(script));
    script.per_code_point = 5;
    script.height = 20;
    metrics = layout_text_script_metrics(&script);

    /* The *range* is built from the node's code points, so it is the whole node. */
    ROBO_CHECK_EQ_INT(robo_layout_text_measure_node(&node, NULL, &metrics, &run, NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(run.range.start, 0u);
    ROBO_CHECK_EQ_INT(run.range.end, 2u);
    ROBO_CHECK_EQ_INT(run.width, 10);
    ROBO_CHECK_EQ_INT(run.code_points, 2u);
    /* A NULL owner style is the initial font inputs, not a failure -- and this is
     * where a case can assert the measurer saw them. */
    ROBO_CHECK_EQ_INT((int)script.calls, 1);

    ROBO_CHECK_EQ_INT(robo_layout_text_measure_node(&node, NULL, NULL, &run, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(run.width, 0);
    ROBO_CHECK_EQ_INT(run.range.end, 2u);
    ROBO_CHECK_EQ_INT(run.code_points, 0u);

    ROBO_CHECK_EQ_INT(robo_layout_text_measure_node(NULL, NULL, &metrics, &run, NULL),
                      ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_layout_text_measure_node(&node, NULL, &metrics, NULL, NULL),
                      ROBO_INVALID_ARGUMENT);
    {
        robo_html_node_handle_t element = layout_document_find(&fixture.doc, "p");

        ROBO_CHECK_EQ_INT(robo_layout_text_measure_node(&element, NULL, &metrics, &run, NULL),
                          ROBO_INVALID_ARGUMENT);
    }

    layout_text_end(&fixture);
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
static void test_text_build(void)
{
    layout_text_fixture_t fixture;
    static const layout_text_rule_t rules[] = { LAYOUT_TEXT_BLOCK_RULES };
    robo_layout_build_options_t options;
    robo_layout_build_report_t report;
    robo_layout_tree_t *tree = NULL;
    robo_layout_node_id_t text_box;
    robo_layout_node_id_t p_box;
    robo_html_node_handle_t text_node;
    robo_html_node_handle_t p_element;
    robo_html_node_handle_t reported;
    robo_layout_object_kind_t kind;

    layout_fixture();
    /*
     * This case builds through the *report*, so it drives the builder itself rather
     * than `layout_text_begin` -- which means it also owns the fixture's zeroing.
     */
    memset(&fixture, 0, sizeof(fixture));
    /* One text node with content, and the whitespace the indentation produces. */
    layout_document_build(&fixture.doc,
                          "<html>\n"
                          "  <body>\n"
                          "    <p>Hello</p>\n"
                          "  </body>\n"
                          "</html>\n");
    layout_text_style_add(&fixture, &rules[0]);
    layout_text_style_add(&fixture, &rules[1]);
    layout_text_style_add(&fixture, &rules[2]);
    robo_layout_style_source_init(&fixture.styles);
    fixture.styles.userdata = &fixture;
    fixture.styles.computed_style_for = layout_text_style_lookup;
    ROBO_CHECK_EQ_INT(robo_layout_tree_create(&tree, NULL), ROBO_OK);
    robo_layout_build_options_init(&options);
    options.styles = &fixture.styles;
    ROBO_CHECK_EQ_INT(robo_layout_tree_build_from_document(tree, fixture.doc.id, &options,
                                                          &report, NULL),
                      ROBO_OK);

    ROBO_CHECK_EQ_INT(report.texts_created, 1u);
    /* The document's own newlines and indentation: one text node between `<html>` and
     * `<body>`, one around `<p>`, one before `</body>` and one after `</html>`. */
    ROBO_CHECK_EQ_INT(report.texts_skipped_whitespace, 3u);
    ROBO_CHECK_EQ_INT(report.elements_seen, 4u); /* html, head, body, p */
    ROBO_CHECK_EQ_INT(report.boxes_created, 4u);
    ROBO_CHECK_EQ_INT(robo_layout_tree_object_count(tree), 5u); /* + one text run */

    text_node = layout_text_node_of(&fixture, "p");
    text_box = robo_layout_object_id_for_dom_node(tree, &text_node);
    ROBO_CHECK(text_box != ROBO_LAYOUT_NODE_ID_NONE);

    kind = robo_layout_object_kind(tree, text_box);
    ROBO_CHECK_EQ_INT((int)kind, (int)ROBO_LAYOUT_OBJECT_TEXT);
    ROBO_CHECK_EQ_INT(robo_layout_object_has_children(tree, text_box), 0);
    ROBO_CHECK_EQ_INT((int)robo_layout_object_display(tree, text_box),
                      (int)ROBO_CSS_DISPLAY_INLINE);

    /* The node it came from is the *text node*, and the accessor is the same door a
     * box uses -- a handle is a node handle. */
    ROBO_CHECK_EQ_INT(robo_layout_object_dom_node(tree, text_box, &reported), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_html_node_handle_equal(&reported, &text_node), 1);

    /* The borrowed style is the parent element's, pointer-identical. */
    p_element = layout_document_find(&fixture.doc, "p");
    p_box = robo_layout_object_id_for_dom_node(tree, &p_element);
    ROBO_CHECK(robo_layout_object_style(tree, text_box) ==
               robo_layout_object_style(tree, p_box));
    ROBO_CHECK(robo_layout_object_style(tree, text_box) != NULL);

    /* Its parent is the `<p>`'s box, and it is the last of its children. */
    {
        robo_layout_node_id_t parent = ROBO_LAYOUT_NODE_ID_NONE;

        ROBO_CHECK_EQ_INT(robo_layout_object_parent(tree, text_box, &parent), ROBO_OK);
        ROBO_CHECK_EQ_INT(parent, p_box);
    }

    robo_layout_tree_destroy(tree);
    layout_text_end(&fixture);

    /* And the whitespace rule from the other side: a `<p>` holding only spaces
     * generates nothing at all. */
    layout_text_begin(&fixture, "<html><body><p>   </p></body></html>", rules, 3u);
    ROBO_CHECK_EQ_INT(layout_text_object_of(&fixture, "p"), ROBO_LAYOUT_NODE_ID_NONE);
    layout_text_end(&fixture);
}

/**
 * The text-node accessors: what an object's `dom_node` is for a text object, and what
 * reading a run before any pass says.
 */
static void test_text_object_model(void)
{
    layout_text_fixture_t fixture;
    static const layout_text_rule_t rules[] = { LAYOUT_TEXT_BLOCK_RULES };
    robo_layout_node_id_t text_box;
    robo_layout_node_id_t p_box;
    robo_layout_text_run_t run;
    robo_layout_stats_t stats;

    layout_fixture();
    layout_text_begin(&fixture, "<html><body><p>AB</p></body></html>", rules, 3u);
    text_box = layout_text_object_of(&fixture, "p");
    p_box = layout_text_box_of(&fixture, "p");
    ROBO_CHECK(text_box != ROBO_LAYOUT_NODE_ID_NONE);

    /* Before any pass, there is no measurement to read -- and the refusal is a miss
     * rather than a zeroed run, because a zeroed run *is* a shape a caller could read
     * a width out of. */
    ROBO_CHECK_EQ_INT(robo_layout_object_text_run(fixture.tree, text_box, &run, NULL),
                      ROBO_NOT_FOUND);
    ROBO_CHECK_EQ_INT(robo_layout_object_text_run(fixture.tree, text_box, NULL, NULL),
                      ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_layout_object_text_run(fixture.tree, ROBO_LAYOUT_NODE_ID_NONE,
                                                &run, NULL),
                      ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_layout_object_text_run(NULL, text_box, &run, NULL),
                      ROBO_INVALID_ARGUMENT);
    /* A box is not a text run, whatever its geometry. */
    ROBO_CHECK_EQ_INT(robo_layout_object_text_run(fixture.tree, p_box, &run, NULL),
                      ROBO_NOT_FOUND);

    layout_text_pass(&fixture, LAYOUT_TEXT_WIDE_VIEWPORT, NULL, &stats, NULL);
    /* After a pass with no measurer the run is zero -- but it *was* measured, so the
     * record is readable and says zero, which is the distinction the accessor's
     * refusal was drawing. */
    ROBO_CHECK_EQ_INT(robo_layout_object_text_run(fixture.tree, text_box, &run, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(run.width, 0);
    ROBO_CHECK_EQ_INT(run.range.end, 2u);
    ROBO_CHECK_EQ_INT(run.code_points, 0u);

    layout_text_end(&fixture);
}

/* ------------------------------------------------------------------ */
/* The real measurer                                                   */
/* ------------------------------------------------------------------ */

static robo_layout_text_style_t layout_text_face_style(double font_size_px)
{
    robo_layout_text_style_t style;

    robo_layout_text_style_init(&style);
    style.font_size_px = font_size_px;
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
static void test_text_font_measure(void)
{
    layout_text_fixture_t fixture;
    static const layout_text_rule_t rules[] = { LAYOUT_TEXT_BLOCK_RULES };
    layout_text_face_t face;
    robo_layout_text_font_t measurer;
    robo_layout_text_run_t run;
    robo_layout_text_style_t style;
    robo_html_node_handle_t node;
    robo_html_node_handle_t element;
    robo_font_metrics_t line;
    float ascent_px = 0.0f;
    float descent_px = 0.0f;
    float gap_px = 0.0f;
    robo_layout_text_request_t request;

    layout_fixture();
    layout_text_begin(&fixture, "<html><body><p>A</p></body></html>", rules, 3u);
    node = layout_text_node_of(&fixture, "p");

    layout_text_face_create(&face);
    robo_layout_text_font_init(&measurer);
    ROBO_CHECK_EQ_INT(robo_layout_text_font_is_attached(&measurer), 0);
    ROBO_CHECK_EQ_INT(robo_layout_text_font_attach(&measurer, face.face), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_layout_text_font_is_attached(&measurer), 1);
    ROBO_CHECK_EQ_INT(robo_layout_text_font_attach(NULL, face.face), ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_layout_text_font_attach(&measurer, NULL), ROBO_INVALID_ARGUMENT);

    /* What the face itself says, which is what the run below has to agree with.
     * RoboFont's table parses are *lazy*, so the metrics are asked for once here
     * before they are read -- the same call the measurer makes on attach, made
     * explicitly so that this case reads the face rather than a measurer's copy of
     * it. */
    ROBO_CHECK_EQ_INT(robo_font_face_parse_metrics(face.face), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_font_face_layout_metrics(face.face, &line), ROBO_OK);
    ROBO_CHECK_EQ_INT((int)line.units_per_em, (int)LAYOUT_TEXT_FACE_UPEM);
    ROBO_CHECK_EQ_INT((int)line.ascent, 800);
    ROBO_CHECK_EQ_INT((int)line.descent, 200); /* a magnitude, and RoboFont says so */
    ROBO_CHECK_EQ_INT((int)line.line_gap, 90);
    ROBO_CHECK_EQ_INT(robo_font_scale_font_units(line.ascent, line.units_per_em,
                                                (robo_font_pixel_size_t)LAYOUT_TEXT_FACE_FONT_SIZE,
                                                ROBO_FONT_ROUND_NEAREST, &ascent_px),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_font_scale_font_units(line.descent, line.units_per_em,
                                                (robo_font_pixel_size_t)LAYOUT_TEXT_FACE_FONT_SIZE,
                                                ROBO_FONT_ROUND_NEAREST, &descent_px),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_font_scale_font_units(line.line_gap, line.units_per_em,
                                                (robo_font_pixel_size_t)LAYOUT_TEXT_FACE_FONT_SIZE,
                                                ROBO_FONT_ROUND_NEAREST, &gap_px),
                      ROBO_OK);

    style = layout_text_face_style(LAYOUT_TEXT_FACE_FONT_SIZE);

    /* `A` -> glyph 1, advance 600 units -> 60px. */
    request = layout_text_request(&node, robo_layout_text_range_full(1u), &style);
    ROBO_CHECK_EQ_INT(robo_layout_text_font_measure(&measurer, &request, &run, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(run.width, 60);
    ROBO_CHECK_EQ_INT(run.code_points, 1u);
    ROBO_CHECK_EQ_INT(run.glyphs_missing, 0u);
    ROBO_CHECK_EQ_INT(run.ascent, (int32_t)(ascent_px + 0.5f));
    ROBO_CHECK_EQ_INT(run.descent, (int32_t)(descent_px + 0.5f));
    ROBO_CHECK_EQ_INT(run.height, run.ascent + run.descent + (int32_t)(gap_px + 0.5f));
    ROBO_CHECK_EQ_INT(run.height, 109);
    ROBO_CHECK_EQ_INT((int)run.direction, (int)ROBO_LAYOUT_TEXT_DIRECTION_LTR);
    ROBO_CHECK_EQ_INT(robo_html_node_handle_equal(&run.source, &node), 1);

    /* An empty range is a real range: a zero-size run, not a failure. */
    request = layout_text_request(&node, robo_layout_text_range_at(0u), &style);
    ROBO_CHECK_EQ_INT(robo_layout_text_font_measure(&measurer, &request, &run, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(run.width, 0);
    ROBO_CHECK_EQ_INT(run.code_points, 0u);
    /* The line extent is the *face's*, not the run's: a line has a height whether or
     * not anything is on it. */
    ROBO_CHECK_EQ_INT(run.height, 109);

    /* Two glyphs of *different* advances, so a single-glyph face would not pass. */
    layout_text_end(&fixture);
    layout_text_begin(&fixture, "<html><body><p>AB</p></body></html>", rules, 3u);
    node = layout_text_node_of(&fixture, "p");
    request = layout_text_request(&node, robo_layout_text_range_full(2u), &style);
    ROBO_CHECK_EQ_INT(robo_layout_text_font_measure(&measurer, &request, &run, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(run.width, 90);

    style.font_size_px = LAYOUT_TEXT_FACE_FONT_SIZE / 2.0;
    ROBO_CHECK_EQ_INT(robo_layout_text_font_measure(&measurer, &request, &run, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(run.width, 45);
    style.font_size_px = LAYOUT_TEXT_FACE_FONT_SIZE * 2.0;
    ROBO_CHECK_EQ_INT(robo_layout_text_font_measure(&measurer, &request, &run, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(run.width, 180);

    /* A code point the face has no glyph for: measured at `.notdef` (500 units -> 50px)
     * and *counted*, which is the only way a caller can learn the width it read is for
     * text the font cannot draw. `H` is not in this face's cmap; `h` is not either. */
    {
        layout_text_fixture_t other;
        robo_html_node_handle_t missing;

        layout_text_begin(&other, "<html><body><p>Z</p></body></html>", rules, 3u);
        missing = layout_text_node_of(&other, "p");
        style.font_size_px = LAYOUT_TEXT_FACE_FONT_SIZE;
        request = layout_text_request(&missing, robo_layout_text_range_full(1u), &style);
        ROBO_CHECK_EQ_INT(robo_layout_text_font_measure(&measurer, &request, &run, NULL),
                          ROBO_OK);
        ROBO_CHECK_EQ_INT(run.glyphs_missing, 1u);
        ROBO_CHECK_EQ_INT(run.width,
                          (int32_t)(LAYOUT_TEXT_GLYPH_NOTDEF_ADVANCE / 10u)); /* 50px */
        layout_text_end(&other);
    }

    /* The range is refused when it does not lie inside the node, and a node that does
     * not resolve is a miss -- both before the face is asked. */
    request = layout_text_request(&node, robo_layout_text_range_full(3u), &style);
    ROBO_CHECK_EQ_INT(robo_layout_text_font_measure(&measurer, &request, &run, NULL),
                      ROBO_INVALID_ARGUMENT);
    request = layout_text_request(&node, robo_layout_text_range_full(2u), NULL);
    ROBO_CHECK_EQ_INT(robo_layout_text_font_measure(&measurer, &request, &run, NULL),
                      ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_layout_text_font_measure(&measurer, &request, NULL, NULL),
                      ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_layout_text_font_measure(NULL, &request, &run, NULL),
                      ROBO_INVALID_ARGUMENT);

    /* Detached is a refusal, not a zero: a measurer with no face cannot answer. */
    robo_layout_text_font_detach(&measurer);
    ROBO_CHECK_EQ_INT(robo_layout_text_font_is_attached(&measurer), 0);
    request = layout_text_request(&node, robo_layout_text_range_full(2u), &style);
    ROBO_CHECK_EQ_INT(robo_layout_text_font_measure(&measurer, &request, &run, NULL),
                      ROBO_ERROR);
    robo_layout_text_font_detach(&measurer); /* idempotent */
    robo_layout_text_font_detach(NULL);      /* and safe on NULL */

    /* An element handle is refused too: a run can only live in a text node. */
    element = layout_document_find(&fixture.doc, "p");
    request = layout_text_request(&element, robo_layout_text_range_full(1u), &style);
    ROBO_CHECK_EQ_INT(robo_layout_text_font_measure(&measurer, &request, &run, NULL),
                      ROBO_INVALID_ARGUMENT);

    layout_text_end(&fixture);
    layout_text_face_destroy(&face);
}

/**
 * The measurer as a *source*: the adapter the pass installs, and its own counters.
 *
 * `runs_measured` and `glyphs_missing` live on the measurer, so a caller can read what
 * its font did without the pass reporting it twice -- and they are asserted here rather
 * than only through the run, because the measurer's numbers survive the run being
 * discarded and the pass's survive the measurer being detached.
 */
static void test_text_font_source(void)
{
    layout_text_fixture_t fixture;
    static const layout_text_rule_t rules[] = { LAYOUT_TEXT_BLOCK_RULES };
    layout_text_face_t face;
    robo_layout_text_font_t measurer;
    robo_layout_text_metrics_t metrics;
    robo_layout_text_run_t run;
    robo_layout_text_style_t style;
    robo_html_node_handle_t node;
    robo_layout_text_request_t request;

    layout_fixture();
    layout_text_begin(&fixture, "<html><body><p>AB</p></body></html>", rules, 3u);
    node = layout_text_node_of(&fixture, "p");
    layout_text_face_create(&face);
    robo_layout_text_font_init(&measurer);
    ROBO_CHECK_EQ_INT(measurer.runs_measured, 0u);
    ROBO_CHECK_EQ_INT(measurer.glyphs_missing, 0u);
    ROBO_CHECK_EQ_INT(robo_layout_text_font_attach(&measurer, face.face), ROBO_OK);

    metrics = robo_layout_text_font_metrics(&measurer);
    ROBO_CHECK_EQ_INT(robo_layout_text_metrics_is_usable(&metrics), 1);
    style = layout_text_face_style(LAYOUT_TEXT_FACE_FONT_SIZE);
    request = layout_text_request(&node, robo_layout_text_range_full(2u), &style);

    /* Through the *seam*, not through the measurer directly: this is the call the pass
     * makes, and the run it produces has to be the same one. */
    ROBO_CHECK_EQ_INT(robo_layout_text_measure(&metrics, &request, &run, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(run.width, 90);
    ROBO_CHECK_EQ_INT(measurer.runs_measured, 1u);
    ROBO_CHECK_EQ_INT((int)measurer.glyphs_missing, 0);

    /* A run the face cannot draw increments the measurer's own counter, which is the
     * fact a caller reads when it wants to know whether the font is adequate for the
     * document rather than whether one run was. */
    {
        layout_text_fixture_t other;
        robo_html_node_handle_t missing;

        layout_text_begin(&other, "<html><body><p>ZZ</p></body></html>", rules, 3u);
        missing = layout_text_node_of(&other, "p");
        request = layout_text_request(&missing, robo_layout_text_range_full(2u), &style);
        ROBO_CHECK_EQ_INT(robo_layout_text_measure(&metrics, &request, &run, NULL), ROBO_OK);
        ROBO_CHECK_EQ_INT(run.glyphs_missing, 2u);
        ROBO_CHECK_EQ_INT(measurer.glyphs_missing, 2u);
        ROBO_CHECK_EQ_INT(measurer.runs_measured, 2u);
        layout_text_end(&other);
    }

    /* A detached measurer still yields a *record*; the record simply has no callback,
     * which is exactly what the seam reads as "measures nothing". That is the whole
     * reason the source is a record and not a bare function pointer. */
    robo_layout_text_font_detach(&measurer);
    metrics = robo_layout_text_font_metrics(&measurer);
    ROBO_CHECK_EQ_INT(robo_layout_text_metrics_is_usable(&metrics), 0);
    request = layout_text_request(&node, robo_layout_text_range_full(2u), &style);
    ROBO_CHECK_EQ_INT(robo_layout_text_measure(&metrics, &request, &run, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(run.width, 0);
    /* And an empty record from no font at all is unusable rather than a crash. */
    metrics = robo_layout_text_font_metrics(NULL);
    ROBO_CHECK_EQ_INT(robo_layout_text_metrics_is_usable(&metrics), 0);

    layout_text_face_destroy(&face);
    layout_text_end(&fixture);
}

/* ------------------------------------------------------------------ */
/* The pass                                                            */
/* ------------------------------------------------------------------ */

/** The text object inside the fixture's `<p>`, asserted to exist. */
static robo_layout_node_id_t layout_text_require_object(layout_text_fixture_t *fixture)
{
    robo_layout_node_id_t id = layout_text_object_of(fixture, "p");

    ROBO_CHECK(id != ROBO_LAYOUT_NODE_ID_NONE);
    ROBO_CHECK_EQ_INT((int)robo_layout_object_kind(fixture->tree, id),
                      (int)ROBO_LAYOUT_OBJECT_TEXT);
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
static void test_text_pass_measured(void)
{
    layout_text_fixture_t fixture;
    static const layout_text_rule_t rules[] = { LAYOUT_TEXT_BLOCK_RULES };
    robo_layout_node_id_t text_box;
    robo_layout_node_id_t p_box;
    robo_html_node_handle_t text_node;
    layout_text_script_t script;
    robo_layout_text_metrics_t metrics;
    robo_layout_stats_t stats;
    robo_layout_text_run_t run;

    layout_fixture();
    layout_text_begin(&fixture, "<html><body><p>AB</p></body></html>", rules, 3u);
    memset(&script, 0, sizeof(script));
    script.per_code_point = 10;
    script.height = 20;
    script.ascent = 16;
    script.descent = 4;
    metrics = layout_text_script_metrics(&script);

    layout_text_pass(&fixture, LAYOUT_TEXT_WIDE_VIEWPORT, &metrics, &stats, NULL);
    text_box = layout_text_require_object(&fixture);
    p_box = layout_text_box_of(&fixture, "p");
    text_node = layout_text_node_of(&fixture, "p");

    /* The cursor began at zero, so the first participant is at the origin. */
    layout_text_expect(&fixture, text_box, 0, 0, 20, 20);
    /* And the block extent of the box that owns the inline context is the line's. */
    layout_text_expect(&fixture, p_box, 0, 0, LAYOUT_TEXT_WIDE_VIEWPORT, 20);
    ROBO_CHECK_EQ_INT(script.calls, 1u);

    ROBO_CHECK_EQ_INT(stats.text.runs_placed, 1u);
    ROBO_CHECK_EQ_INT(stats.text.measured, 1u);
    ROBO_CHECK_EQ_INT(stats.text.unmeasured, 0u);
    ROBO_CHECK_EQ_INT(stats.text.empty, 0u);
    ROBO_CHECK_EQ_INT(stats.text.code_points, 2u);
    ROBO_CHECK_EQ_INT(stats.text.glyphs_missing, 0u);
    ROBO_CHECK_EQ_INT(stats.text.rtl, 0u);
    ROBO_CHECK_EQ_INT(stats.text.width_max, 20);
    ROBO_CHECK_EQ_INT(stats.text.height_max, 20);
    ROBO_CHECK_EQ_INT(stats.text.depth_max, 3u); /* html 0, head/body 1, p 2, text 3 */

    /* A text run is a participant of the line, and it is *not* one of the two box
     * counters: it is not a box, and counting it as one would make those two describe
     * a set of things no formatting context has a rule about. */
    ROBO_CHECK_EQ_INT(stats.inline_flow.participants_placed, 1u);
    ROBO_CHECK_EQ_INT(stats.inline_flow.atomic_placed, 0u);
    ROBO_CHECK_EQ_INT(stats.inline_flow.non_atomic_placed, 0u);
    ROBO_CHECK_EQ_INT(stats.inline_flow.lines, 1u);
    ROBO_CHECK_EQ_INT(stats.inline_flow.wraps, 0u);
    ROBO_CHECK_EQ_INT(stats.inline_flow.empty_contexts, 0u);
    ROBO_CHECK_EQ_INT(stats.boxes_computed,
                      stats.block.blocks_placed + stats.inline_flow.participants_placed);

    /* The run, read back out of the tree: what was measured, and the extents a line
     * box and a baseline will need without re-measuring. */
    ROBO_CHECK_EQ_INT(robo_layout_object_text_run(fixture.tree, text_box, &run, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(run.width, 20);
    ROBO_CHECK_EQ_INT(run.height, 20);
    ROBO_CHECK_EQ_INT(run.ascent, 16);
    ROBO_CHECK_EQ_INT(run.descent, 4);
    ROBO_CHECK_EQ_INT(run.code_points, 2u);
    ROBO_CHECK_EQ_INT(run.glyphs_missing, 0u);
    ROBO_CHECK_EQ_INT(run.range.start, 0u);
    ROBO_CHECK_EQ_INT(run.range.end, 2u);
    ROBO_CHECK_EQ_INT((int)run.direction, (int)ROBO_LAYOUT_TEXT_DIRECTION_LTR);
    ROBO_CHECK_EQ_INT(robo_html_node_handle_equal(&run.source, &text_node), 1);

    layout_text_end(&fixture);
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
static void test_text_pass_unmeasured(void)
{
    layout_text_fixture_t fixture;
    static const layout_text_rule_t rules[] = { LAYOUT_TEXT_BLOCK_RULES };
    robo_layout_node_id_t text_box;
    robo_layout_node_id_t p_box;
    robo_layout_stats_t stats;
    robo_layout_text_run_t run;

    layout_fixture();
    layout_text_begin(&fixture, "<html><body><p>AB</p></body></html>", rules, 3u);
    layout_text_pass(&fixture, LAYOUT_TEXT_WIDE_VIEWPORT, NULL, &stats, NULL);
    text_box = layout_text_require_object(&fixture);
    p_box = layout_text_box_of(&fixture, "p");

    /* Placed, at the origin, with a measured width of zero -- which is 10.4's answer
     * for a `<span>` holding only text, now with a *reason* attached. */
    layout_text_expect(&fixture, text_box, 0, 0, 0, 0);
    layout_text_expect(&fixture, p_box, 0, 0, LAYOUT_TEXT_WIDE_VIEWPORT, 0);
    ROBO_CHECK_EQ_INT(stats.text.runs_placed, 1u);
    ROBO_CHECK_EQ_INT(stats.text.measured, 0u);
    ROBO_CHECK_EQ_INT(stats.text.unmeasured, 1u);
    ROBO_CHECK_EQ_INT(stats.text.code_points, 0u);
    ROBO_CHECK_EQ_INT(stats.text.width_max, 0);
    ROBO_CHECK_EQ_INT(stats.text.height_max, 0);

    /* Still a participant, and still a line: "nobody measured this" is not "this is
     * not on the line". */
    ROBO_CHECK_EQ_INT(stats.inline_flow.participants_placed, 1u);
    ROBO_CHECK_EQ_INT(stats.inline_flow.lines, 1u);
    ROBO_CHECK_EQ_INT(stats.inline_flow.empty_contexts, 0u);

    /* And the record is readable -- zero, and *not* the same statement as the
     * accessor's refusal before any pass ran. */
    ROBO_CHECK_EQ_INT(robo_layout_object_text_run(fixture.tree, text_box, &run, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(run.width, 0);
    ROBO_CHECK_EQ_INT(run.range.end, 2u);
    ROBO_CHECK_EQ_INT(run.code_points, 0u);

    layout_text_end(&fixture);
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
static void test_text_two_runs_and_rebase(void)
{
    layout_text_fixture_t fixture;
    static const layout_text_rule_t rules[] = {
        LAYOUT_TEXT_BLOCK_RULES,
        { "span", ROBO_CSS_DISPLAY_INLINE }
    };
    robo_layout_node_id_t span_box;
    robo_layout_node_id_t p_box;
    robo_layout_node_id_t inner_text;
    robo_html_node_handle_t span_node;
    robo_html_node_view_t span_view;
    layout_text_script_t script;
    robo_layout_text_metrics_t metrics;
    robo_layout_stats_t stats;

    layout_fixture();
    layout_text_begin(&fixture, "<html><body><p>AB<span>CD</span></p></body></html>", rules,
                      4u);
    memset(&script, 0, sizeof(script));
    script.per_code_point = 10;
    script.height = 20;
    metrics = layout_text_script_metrics(&script);
    layout_text_pass(&fixture, LAYOUT_TEXT_WIDE_VIEWPORT, &metrics, &stats, NULL);

    p_box = layout_text_box_of(&fixture, "p");
    span_box = layout_text_box_of(&fixture, "span");
    layout_text_expect(&fixture, layout_text_require_object(&fixture), 0, 0, 20, 20);
    /* The span is the union of what its contents reached: x 20..40, the line's height. */
    layout_text_expect(&fixture, span_box, 20, 0, 20, 20);
    layout_text_expect(&fixture, p_box, 0, 0, LAYOUT_TEXT_WIDE_VIEWPORT, 20);

    /* The run *inside* the span: the span's own origin subtracted, so it sits at its
     * own containing block's origin rather than at the line's. */
    span_node = layout_document_find(&fixture.doc, "span");
    ROBO_CHECK_EQ_INT(robo_html_node_get_view(&span_node, &span_view, NULL), ROBO_OK);
    inner_text = robo_layout_object_id_for_dom_node(fixture.tree, &span_view.first_child);
    ROBO_CHECK(inner_text != ROBO_LAYOUT_NODE_ID_NONE);
    layout_text_expect(&fixture, inner_text, 0, 0, 20, 20);

    ROBO_CHECK_EQ_INT(stats.text.runs_placed, 2u);
    ROBO_CHECK_EQ_INT(stats.text.measured, 2u);
    ROBO_CHECK_EQ_INT(stats.text.code_points, 4u);
    ROBO_CHECK_EQ_INT(stats.inline_flow.participants_placed, 3u); /* two runs + the span */
    ROBO_CHECK_EQ_INT(stats.inline_flow.non_atomic_placed, 1u);   /* the span, and only it */
    ROBO_CHECK_EQ_INT(stats.inline_flow.atomic_placed, 0u);
    ROBO_CHECK_EQ_INT(stats.inline_flow.lines, 1u);
    ROBO_CHECK_EQ_INT(script.calls, 2u);

    layout_text_end(&fixture);
}

/**
 * Wrapping, with a text run as the thing that does not fit.
 *
 * A measured run is one of the two participants whose width is known before it is
 * placed (the other is an atomic inline), so it is one of the two the wrap rule can act
 * on. The first half asserts the rule's other edge: a run wider than its *empty* line is
 * placed and overflows rather than starting lines forever.
 */
static void test_text_wrapping(void)
{
    layout_text_fixture_t fixture;
    static const layout_text_rule_t rules[] = {
        LAYOUT_TEXT_BLOCK_RULES,
        { "i", ROBO_CSS_DISPLAY_INLINE }
    };
    layout_text_script_t script;
    robo_layout_text_metrics_t metrics;
    robo_layout_stats_t stats;
    robo_layout_node_id_t p_box;
    robo_layout_node_id_t first;
    robo_layout_node_id_t second;
    robo_html_node_handle_t i_node;
    robo_html_node_view_t i_view;

    layout_fixture();
    memset(&script, 0, sizeof(script));
    script.per_code_point = 30;
    script.height = 20;
    metrics = layout_text_script_metrics(&script);

    /* One run of 60px in a 50px line: placed anyway, and the line count stays 1. */
    layout_text_begin(&fixture, "<html><body><p>AB</p></body></html>", rules, 4u);
    layout_text_pass(&fixture, 50, &metrics, &stats, NULL);
    first = layout_text_require_object(&fixture);
    layout_text_expect(&fixture, first, 0, 0, 60, 20);
    ROBO_CHECK_EQ_INT(stats.inline_flow.lines, 1u);
    ROBO_CHECK_EQ_INT(stats.inline_flow.wraps, 0u);
    layout_text_end(&fixture);

    /* Two runs of 60px in a 50px line: the second cannot fit after the cursor, so the
     * line moves down and the cursor returns to the start. */
    layout_text_begin(&fixture, "<html><body><p>AB<i>CD</i></p></body></html>", rules, 4u);
    layout_text_pass(&fixture, 50, &metrics, &stats, NULL);
    p_box = layout_text_box_of(&fixture, "p");
    first = layout_text_require_object(&fixture);
    layout_text_expect(&fixture, first, 0, 0, 60, 20);

    i_node = layout_document_find(&fixture.doc, "i");
    ROBO_CHECK_EQ_INT(robo_html_node_get_view(&i_node, &i_view, NULL), ROBO_OK);
    second = robo_layout_object_id_for_dom_node(fixture.tree, &i_view.first_child);
    ROBO_CHECK(second != ROBO_LAYOUT_NODE_ID_NONE);
    /* Placed on the second line, in the line's space... */
    {
        robo_layout_result_t result;
        robo_layout_point_t position;

        ROBO_CHECK_EQ_INT(robo_layout_object_result(fixture.tree, second, &result), ROBO_OK);
        ROBO_CHECK_EQ_INT(robo_layout_result_position(&result, &position), ROBO_OK);
        /* ...which makes its position inside the transparent box's own space
         * (0, 0) -- the rebase again, now over a wrapped line. */
        ROBO_CHECK_EQ_INT(position.x, 0);
        ROBO_CHECK_EQ_INT(position.y, 0);
    }
    ROBO_CHECK_EQ_INT(stats.inline_flow.lines, 2u);
    ROBO_CHECK_EQ_INT(stats.inline_flow.wraps, 1u);
    /* The `<p>`'s height is both lines: the cursor's extent, not the tallest one. */
    layout_text_expect(&fixture, p_box, 0, 0, 50, 40);
    ROBO_CHECK_EQ_INT(stats.text.runs_placed, 2u);
    ROBO_CHECK_EQ_INT(stats.text.height_max, 20);

    layout_text_end(&fixture);
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
static void test_text_direction_in_pass(void)
{
    layout_text_fixture_t fixture;
    static const layout_text_rule_t rules[] = { LAYOUT_TEXT_BLOCK_RULES };
    layout_text_script_t script;
    robo_layout_text_metrics_t metrics;
    robo_layout_stats_t stats;
    robo_layout_text_run_t run;
    robo_layout_node_id_t text_box;

    layout_fixture();
    layout_text_begin(&fixture, "<html><body><p>AB</p></body></html>", rules, 3u);
    /* The `<p>`'s own style carries the declaration, and a text object borrows its
     * parent's style -- so this is the same path a real cascade takes. */
    layout_text_style_set_keyword(fixture.rules[2].style, "direction", "rtl");
    memset(&script, 0, sizeof(script));
    script.per_code_point = 10;
    script.height = 20;
    metrics = layout_text_script_metrics(&script);
    layout_text_pass(&fixture, LAYOUT_TEXT_WIDE_VIEWPORT, &metrics, &stats, NULL);

    text_box = layout_text_require_object(&fixture);
    ROBO_CHECK_EQ_INT(robo_layout_object_text_run(fixture.tree, text_box, &run, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT((int)run.direction, (int)ROBO_LAYOUT_TEXT_DIRECTION_RTL);
    ROBO_CHECK_EQ_INT(stats.text.rtl, 1u);
    ROBO_CHECK_EQ_INT(stats.text.measured, 1u);

    layout_text_end(&fixture);
}

/**
 * A text run in a **block-direction** context is declined, not badly placed.
 *
 * `<div>` holding both text and a `<p>` establishes a block context (10.2's rule sees a
 * block-level child), so the inline run in it has no line to be on: CSS wraps it in an
 * anonymous block box first and this build makes none. The run is counted
 * (`inline_level_skipped`), left UNRESOLVED, and its run record is refused by the
 * accessor -- because a zeroed run read as an answer is exactly the plausible number
 * this module refuses to hand out.
 */
static void test_text_block_context(void)
{
    layout_text_fixture_t fixture;
    static const layout_text_rule_t rules[] = {
        LAYOUT_TEXT_BLOCK_RULES,
        { "div", ROBO_CSS_DISPLAY_BLOCK }
    };
    robo_html_node_handle_t div_node;
    robo_html_node_view_t div_view;
    robo_html_node_handle_t child;
    robo_layout_node_id_t run_box = ROBO_LAYOUT_NODE_ID_NONE;
    layout_text_script_t script;
    robo_layout_text_metrics_t metrics;
    robo_layout_stats_t stats;
    robo_layout_text_run_t run;
    size_t objects;

    layout_fixture();
    layout_text_begin(&fixture, "<html><body><div>AB<p>CD</p></div></body></html>", rules,
                      4u);
    memset(&script, 0, sizeof(script));
    script.per_code_point = 10;
    script.height = 20;
    metrics = layout_text_script_metrics(&script);
    layout_text_pass(&fixture, LAYOUT_TEXT_WIDE_VIEWPORT, &metrics, &stats, NULL);

    div_node = layout_document_find(&fixture.doc, "div");
    ROBO_CHECK_EQ_INT(robo_html_node_get_view(&div_node, &div_view, NULL), ROBO_OK);
    child = div_view.first_child;
    while (robo_html_node_handle_is_wellformed(&child)) {
        robo_html_node_view_t view;

        if (robo_html_node_get_view(&child, &view, NULL) != ROBO_OK) {
            break;
        }
        if (view.kind == ROBO_HTML_NODE_KIND_TEXT) {
            run_box = robo_layout_object_id_for_dom_node(fixture.tree, &child);
            break;
        }
        child = view.next_sibling;
    }
    ROBO_CHECK(run_box != ROBO_LAYOUT_NODE_ID_NONE);
    layout_text_expect_unresolved(&fixture, run_box);
    ROBO_CHECK_EQ_INT(robo_layout_object_text_run(fixture.tree, run_box, &run, NULL),
                      ROBO_NOT_FOUND);

    /* Declined *instead of* measured: a measurement with nowhere to go is not a result,
     * and the measurer was never asked about this node. */
    ROBO_CHECK_EQ_INT(stats.text.runs_placed, 1u); /* the run inside the `<p>` */
    ROBO_CHECK_EQ_INT(script.calls, 1u);
    ROBO_CHECK_EQ_INT(stats.block.inline_level_skipped >= 1u, 1);

    /* And the identity still closes over both kinds of object. */
    objects = robo_layout_tree_object_count(fixture.tree);
    ROBO_CHECK_EQ_INT(stats.block.blocks_placed + stats.block.inline_level_skipped +
                          stats.inline_flow.participants_placed + stats.block.unreached,
                      objects);

    layout_text_end(&fixture);
}

/**
 * Determinism, and what changes when the viewport does.
 *
 * Twice at one width is the same numbers and the same geometry; a narrower width adds a
 * line and moves the wrapped run down. The case uses the two-run fixture because a
 * single run never wraps (it would be alone on its line), so a viewport change could
 * not be told from a build that ignored it.
 */
static void test_text_relayout(void)
{
    layout_text_fixture_t fixture;
    static const layout_text_rule_t rules[] = {
        LAYOUT_TEXT_BLOCK_RULES,
        { "i", ROBO_CSS_DISPLAY_INLINE }
    };
    layout_text_script_t script;
    robo_layout_text_metrics_t metrics;
    robo_layout_stats_t first, second, narrow;
    robo_layout_node_id_t p_box;

    layout_fixture();
    layout_text_begin(&fixture, "<html><body><p>AB<i>CD</i></p></body></html>", rules, 4u);
    memset(&script, 0, sizeof(script));
    script.per_code_point = 10;
    script.height = 20;
    metrics = layout_text_script_metrics(&script);
    p_box = layout_text_box_of(&fixture, "p");

    layout_text_pass(&fixture, LAYOUT_TEXT_WIDE_VIEWPORT, &metrics, &first, NULL);
    layout_text_expect(&fixture, p_box, 0, 0, LAYOUT_TEXT_WIDE_VIEWPORT, 20);
    layout_text_pass(&fixture, LAYOUT_TEXT_WIDE_VIEWPORT, &metrics, &second, NULL);
    ROBO_CHECK_EQ_INT(robo_layout_text_stats_equal(&first.text, &second.text), 1);
    ROBO_CHECK_EQ_INT(robo_layout_inline_stats_equal(&first.inline_flow, &second.inline_flow),
                      1);
    ROBO_CHECK_EQ_INT(robo_layout_block_stats_equal(&first.block, &second.block), 1);
    layout_text_expect(&fixture, p_box, 0, 0, LAYOUT_TEXT_WIDE_VIEWPORT, 20);

    /* 10px + 10px in a 15px line: one participant per line. */
    layout_text_pass(&fixture, 15, &metrics, &narrow, NULL);
    ROBO_CHECK_EQ_INT(narrow.inline_flow.lines, 2u);
    ROBO_CHECK_EQ_INT(narrow.inline_flow.wraps, 1u);
    ROBO_CHECK_EQ_INT(narrow.text.runs_placed, 2u);
    ROBO_CHECK_EQ_INT(narrow.text.code_points, 4u);
    layout_text_expect(&fixture, p_box, 0, 0, 15, 40);

    layout_text_end(&fixture);
}

/* ------------------------------------------------------------------ */
/* The pipeline                                                        */
/* ------------------------------------------------------------------ */

/** Everything the real CSS path needs, and nothing it owns. */
typedef struct layout_text_pipeline {
    layout_document_t doc;
    layout_css_fixture_t css;
    robo_layout_dom_t *dom;
    robo_layout_tree_t *tree;
    robo_layout_style_source_t source;
} layout_text_pipeline_t;

static void layout_text_pipeline_begin(layout_text_pipeline_t *pipeline,
                                       const char *html,
                                       const char *css)
{
    layout_fixture();
    memset(pipeline, 0, sizeof(*pipeline));
    layout_document_build(&pipeline->doc, html);
    layout_css_begin(&pipeline->css);
    layout_css_sheet(&pipeline->css, css);
    ROBO_CHECK_EQ_INT(robo_layout_dom_create(pipeline->css.context, pipeline->doc.id,
                                            &pipeline->dom, NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_layout_dom_refresh(pipeline->dom, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_layout_dom_style_source(pipeline->dom, &pipeline->source, NULL),
                      ROBO_OK);
}

static robo_layout_tree_t *layout_text_pipeline_build(layout_text_pipeline_t *pipeline)
{
    robo_layout_build_options_t options;

    ROBO_CHECK_EQ_INT(robo_layout_tree_create(&pipeline->tree, NULL), ROBO_OK);
    robo_layout_build_options_init(&options);
    options.styles = &pipeline->source;
    ROBO_CHECK_EQ_INT(robo_layout_tree_build_from_document(pipeline->tree, pipeline->doc.id,
                                                          &options, NULL, NULL),
                      ROBO_OK);
    return pipeline->tree;
}

static void layout_text_pipeline_end(layout_text_pipeline_t *pipeline)
{
    if (pipeline->tree) {
        robo_layout_tree_destroy(pipeline->tree);
        pipeline->tree = NULL;
    }
    if (pipeline->dom) {
        robo_layout_dom_destroy(pipeline->dom);
        pipeline->dom = NULL;
    }
    layout_css_end(&pipeline->css);
    layout_document_release(&pipeline->doc);
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
static void test_text_pipeline(void)
{
    layout_text_pipeline_t pipeline;
    layout_text_face_t face;
    robo_layout_text_font_t measurer;
    robo_layout_text_metrics_t metrics;
    robo_layout_context_t *context = NULL;
    robo_layout_size_t viewport;
    robo_layout_stats_t stats;
    robo_layout_text_run_t run;
    robo_layout_node_id_t text_box;
    robo_layout_node_id_t p_box;
    robo_html_node_handle_t text_node;
    robo_html_node_handle_t element;
    robo_font_metrics_t line;

    layout_text_face_create(&face);
    robo_layout_text_font_init(&measurer);
    ROBO_CHECK_EQ_INT(robo_layout_text_font_attach(&measurer, face.face), ROBO_OK);
    metrics = robo_layout_text_font_metrics(&measurer);
    ROBO_CHECK_EQ_INT(robo_font_face_parse_metrics(face.face), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_font_face_layout_metrics(face.face, &line), ROBO_OK);

    layout_text_pipeline_begin(&pipeline,
                               "<html><body><p>AB</p></body></html>",
                               "html, body, p { display: block }\n"
                               "p { font-size: 100px }\n");
    layout_text_pipeline_build(&pipeline);
    /* html, head, body, p, and one text run -- the five objects the parser's document
     * generates, with the text node counted because it carries something. */
    ROBO_CHECK_EQ_INT(robo_layout_tree_object_count(pipeline.tree), 5u);

    viewport.width = LAYOUT_TEXT_WIDE_VIEWPORT;
    viewport.height = 600;
    ROBO_CHECK_EQ_INT(robo_layout_context_create(&context, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_layout_context_set_viewport(context, viewport, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_layout_context_set_text_metrics(context, &metrics), ROBO_OK);
    {
        robo_layout_text_metrics_t read_back;

        ROBO_CHECK_EQ_INT(robo_layout_context_text_metrics(context, &read_back), ROBO_OK);
        ROBO_CHECK_EQ_INT(robo_layout_text_metrics_is_usable(&read_back), 1);
        ROBO_CHECK_EQ_INT(read_back.measure == metrics.measure, 1);
    }
    ROBO_CHECK_EQ_INT(robo_layout_context_run(context, pipeline.tree, NULL), ROBO_OK);
    robo_layout_context_stats(context, &stats);
    robo_layout_context_destroy(context);

    element = layout_document_find(&pipeline.doc, "p");
    p_box = robo_layout_object_id_for_dom_node(pipeline.tree, &element);
    text_node = layout_text_first_text_child(&element);
    text_box = robo_layout_object_id_for_dom_node(pipeline.tree, &text_node);
    ROBO_CHECK(text_box != ROBO_LAYOUT_NODE_ID_NONE);

    ROBO_CHECK_EQ_INT(robo_layout_object_text_run(pipeline.tree, text_box, &run, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(run.width, 90);
    ROBO_CHECK_EQ_INT(run.code_points, 2u);
    ROBO_CHECK_EQ_INT(run.glyphs_missing, 0u);
    /*
     * The line extent, checked against the *face*: ascent + descent + gap, each scaled
     * once. Deriving the expectation from RoboFont's own metrics rather than writing
     * 109 keeps the case about the measurer's arithmetic rather than about which table
     * this synthetic face's line metrics came from -- and the assertion that the three
     * components add up to the height is the structural half.
     */
    {
        float ascent_px = 0.0f;
        float descent_px = 0.0f;
        float gap_px = 0.0f;

        ROBO_CHECK_EQ_INT(robo_font_scale_font_units(line.ascent, line.units_per_em,
                                                    (robo_font_pixel_size_t)100.0,
                                                    ROBO_FONT_ROUND_NEAREST, &ascent_px),
                          ROBO_OK);
        ROBO_CHECK_EQ_INT(robo_font_scale_font_units(line.descent, line.units_per_em,
                                                    (robo_font_pixel_size_t)100.0,
                                                    ROBO_FONT_ROUND_NEAREST, &descent_px),
                          ROBO_OK);
        ROBO_CHECK_EQ_INT(robo_font_scale_font_units(line.line_gap, line.units_per_em,
                                                    (robo_font_pixel_size_t)100.0,
                                                    ROBO_FONT_ROUND_NEAREST, &gap_px),
                          ROBO_OK);
        ROBO_CHECK_EQ_INT(run.ascent, (int32_t)(ascent_px + 0.5f));
        ROBO_CHECK_EQ_INT(run.descent, (int32_t)(descent_px + 0.5f));
        ROBO_CHECK_EQ_INT(run.height, run.ascent + run.descent + (int32_t)(gap_px + 0.5f));
        ROBO_CHECK_EQ_INT(run.height > 0, 1);
    }
    layout_text_expect_in(pipeline.tree, text_box, 0, 0, 90, run.height);
    layout_text_expect_in(pipeline.tree, p_box, 0, 0, LAYOUT_TEXT_WIDE_VIEWPORT, run.height);

    ROBO_CHECK_EQ_INT(stats.text.runs_placed, 1u);
    ROBO_CHECK_EQ_INT(stats.text.measured, 1u);
    ROBO_CHECK_EQ_INT(stats.text.unmeasured, 0u);
    ROBO_CHECK_EQ_INT(stats.text.code_points, 2u);
    ROBO_CHECK_EQ_INT(stats.text.width_max, 90);
    ROBO_CHECK_EQ_INT(measurer.runs_measured, 1u);
    ROBO_CHECK_EQ_INT(stats.inline_flow.participants_placed, 1u);

    layout_text_pipeline_end(&pipeline);

    /* Half the declaration, half the width. */
    layout_text_pipeline_begin(&pipeline,
                               "<html><body><p>AB</p></body></html>",
                               "html, body, p { display: block }\n"
                               "p { font-size: 50px }\n");
    layout_text_pipeline_build(&pipeline);
    ROBO_CHECK_EQ_INT(robo_layout_context_create(&context, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_layout_context_set_viewport(context, viewport, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_layout_context_set_text_metrics(context, &metrics), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_layout_context_run(context, pipeline.tree, NULL), ROBO_OK);
    robo_layout_context_destroy(context);
    element = layout_document_find(&pipeline.doc, "p");
    text_node = layout_text_first_text_child(&element);
    text_box = robo_layout_object_id_for_dom_node(pipeline.tree, &text_node);
    ROBO_CHECK_EQ_INT(robo_layout_object_text_run(pipeline.tree, text_box, &run, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(run.width, 45);
    layout_text_pipeline_end(&pipeline);

    layout_text_face_destroy(&face);
}

/**
 * `direction: rtl` **through the cascade**, and a `direction` the cascade never saw.
 *
 * The same vertical slice as above with one more declaration in it, because the
 * direction is the one property whose whole assertion is "it arrived": nothing reorders
 * anything yet, so the only way to test that a declaration reached the measurer is to
 * find it on the run that came back.
 */
static void test_text_pipeline_direction(void)
{
    layout_text_pipeline_t pipeline;
    layout_text_script_t script;
    robo_layout_text_metrics_t metrics;
    robo_layout_context_t *context = NULL;
    robo_layout_size_t viewport;
    robo_layout_stats_t stats;
    robo_layout_text_run_t run;
    robo_layout_node_id_t text_box;
    robo_html_node_handle_t element;
    robo_html_node_handle_t text_node;

    memset(&script, 0, sizeof(script));
    script.per_code_point = 4;
    script.height = 12;
    metrics = layout_text_style_metrics(&script);

    layout_text_pipeline_begin(&pipeline,
                               "<html><body><p>AB</p></body></html>",
                               "html, body, p { display: block }\n"
                               "p { direction: rtl; font-size: 12px }\n");
    layout_text_pipeline_build(&pipeline);

    viewport.width = LAYOUT_TEXT_WIDE_VIEWPORT;
    viewport.height = 600;
    ROBO_CHECK_EQ_INT(robo_layout_context_create(&context, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_layout_context_set_viewport(context, viewport, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_layout_context_set_text_metrics(context, &metrics), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_layout_context_run(context, pipeline.tree, NULL), ROBO_OK);
    robo_layout_context_stats(context, &stats);
    robo_layout_context_destroy(context);

    element = layout_document_find(&pipeline.doc, "p");
    text_node = layout_text_first_text_child(&element);
    text_box = robo_layout_object_id_for_dom_node(pipeline.tree, &text_node);
    ROBO_CHECK_EQ_INT(robo_layout_object_text_run(pipeline.tree, text_box, &run, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT((int)run.direction, (int)ROBO_LAYOUT_TEXT_DIRECTION_RTL);
    ROBO_CHECK_EQ_INT(stats.text.rtl, 1u);

    /* The style-driven measurer answers one px per code point per px of `font-size`, so
     * the *cascade's* 12px is visible in the geometry. */
    ROBO_CHECK_EQ_INT(run.width, 24);
    ROBO_CHECK_EQ_INT(run.height, 12);

    layout_text_pipeline_end(&pipeline);
}

/**
 * The accounting identity, over a document that has text in it.
 *
 * `blocks_placed + inline_level_skipped + participants_placed + unreached == objects`
 * is the property that says every object has a story -- and since 10.5 the objects
 * include text runs, which are placed as participants, declined as inline-level, or
 * unreached exactly as boxes are. A build that created a text object and then forgot
 * about it would have four right-looking counts and a document that did not add up.
 */
static void test_text_accounting(void)
{
    layout_text_fixture_t fixture;
    static const layout_text_rule_t rules[] = {
        LAYOUT_TEXT_BLOCK_RULES,
        { "div", ROBO_CSS_DISPLAY_BLOCK },
        { "span", ROBO_CSS_DISPLAY_INLINE }
    };
    layout_text_script_t script;
    robo_layout_text_metrics_t metrics;
    robo_layout_stats_t stats;
    size_t objects;

    layout_fixture();
    layout_text_begin(&fixture,
                      "<html><body>"
                      "<p>one</p>"
                      "<div>two<span>three</span></div>"
                      "<p>four</p>"
                      "</body></html>",
                      rules, 5u);
    memset(&script, 0, sizeof(script));
    script.per_code_point = 6;
    script.height = 18;
    metrics = layout_text_script_metrics(&script);
    layout_text_pass(&fixture, LAYOUT_TEXT_WIDE_VIEWPORT, &metrics, &stats, NULL);

    objects = robo_layout_tree_object_count(fixture.tree);
    ROBO_CHECK_EQ_INT(stats.block.blocks_placed + stats.block.inline_level_skipped +
                          stats.inline_flow.participants_placed + stats.block.unreached,
                      objects);
    ROBO_CHECK_EQ_INT(stats.boxes_computed,
                      stats.block.blocks_placed + stats.inline_flow.participants_placed);
    ROBO_CHECK_EQ_INT(stats.boxes_unresolved,
                      objects - stats.block.blocks_placed -
                          stats.inline_flow.participants_placed);

    /*
     * Four text nodes in this document carry content (`one`, `two`, `three`, `four`), and
     * all four are on lines: the `<p>` boxes and the `<div>` each establish an inline
     * context, and the `<span>` is transparent. So the *runs* equal the content nodes,
     * and the two sums below are asserted together because a build that created a text
     * object and never placed it would break the first without breaking the second.
     */
    ROBO_CHECK_EQ_INT(stats.text.runs_placed, 4u);
    ROBO_CHECK_EQ_INT(stats.text.measured, 4u);
    ROBO_CHECK_EQ_INT(stats.text.code_points, 3u + 3u + 5u + 4u);

    layout_text_end(&fixture);
}

/** A fixture that includes a real face, and everything a pass needs to measure with it. */
static void test_text_allocator_balance(void)
{
    layout_text_fixture_t fixture;
    static const layout_text_rule_t rules[] = {
        LAYOUT_TEXT_BLOCK_RULES,
        { "span", ROBO_CSS_DISPLAY_INLINE }
    };
    layout_text_face_t face;
    robo_layout_text_font_t measurer;
    robo_layout_text_metrics_t metrics;
    size_t before;
    int pass;

    layout_fixture();
    before = layout_live_blocks();
    layout_text_face_create(&face);
    robo_layout_text_font_init(&measurer);
    ROBO_CHECK_EQ_INT(robo_layout_text_font_attach(&measurer, face.face), ROBO_OK);
    metrics = robo_layout_text_font_metrics(&measurer);

    for (pass = 0; pass < 8; pass++) {
        robo_layout_stats_t stats;

        layout_text_begin(&fixture,
                          "<html><body><p>hello<span>world</span></p></body></html>", rules,
                          4u);
        layout_text_pass(&fixture, LAYOUT_TEXT_WIDE_VIEWPORT, &metrics, &stats, NULL);
        layout_text_pass(&fixture, LAYOUT_TEXT_WIDE_VIEWPORT, &metrics, &stats, NULL);
        layout_text_end(&fixture);
    }

    robo_layout_text_font_detach(&measurer);
    layout_text_face_destroy(&face);
    ROBO_CHECK_EQ_INT(layout_live_blocks(), before);
}

/* ------------------------------------------------------------------ */

void robo_test_register_layout_text(void);

void robo_test_register_layout_text(void)
{
    robo_test_register("layout.text.range", test_text_range);
    robo_test_register("layout.text.direction", test_text_direction);
    robo_test_register("layout.text.style_initial", test_text_style_initial);
    robo_test_register("layout.text.style_resolve", test_text_style_resolve);
    robo_test_register("layout.text.run_record", test_text_run_and_metrics_record);
    robo_test_register("layout.text.stats_record", test_text_stats_record);
    robo_test_register("layout.text.code_points", test_text_code_points);
    robo_test_register("layout.text.range_bytes", test_text_range_bytes);
    robo_test_register("layout.text.whitespace", test_text_whitespace);
    robo_test_register("layout.text.seam", test_text_seam);
    robo_test_register("layout.text.measure_node", test_text_measure_node);
    robo_test_register("layout.text.build", test_text_build);
    robo_test_register("layout.text.object_model", test_text_object_model);
    robo_test_register("layout.text.font_measure", test_text_font_measure);
    robo_test_register("layout.text.font_source", test_text_font_source);
    robo_test_register("layout.text.pass_measured", test_text_pass_measured);
    robo_test_register("layout.text.pass_unmeasured", test_text_pass_unmeasured);
    robo_test_register("layout.text.two_runs", test_text_two_runs_and_rebase);
    robo_test_register("layout.text.wrapping", test_text_wrapping);
    robo_test_register("layout.text.direction_in_pass", test_text_direction_in_pass);
    robo_test_register("layout.text.block_context", test_text_block_context);
    robo_test_register("layout.text.relayout", test_text_relayout);
    robo_test_register("layout.text.pipeline", test_text_pipeline);
    robo_test_register("layout.text.pipeline_direction", test_text_pipeline_direction);
    robo_test_register("layout.text.accounting", test_text_accounting);
    robo_test_register("layout.text.allocator_balance", test_text_allocator_balance);
}
