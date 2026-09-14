/*
 * RoboFontCoverage — cmap-backed Unicode coverage (Goal 5.14).
 */

#include "RoboFont/RoboFontCoverage.h"

#include "RoboFont/RoboFontFace.h"
#include "RoboFont/RoboFontOutline.h"
#include "RoboMem/RoboMem.h"

#include <string.h>

struct robo_font_coverage {
    uint32_t refs;
    robo_font_cmap_map_t *cmap; /* retained */
    robo_font_coverage_range_t *ranges;
    uint32_t range_count;
    uint32_t range_cap;
    robo_unicode_script_t *scripts;
    uint32_t script_count;
    uint8_t *script_seen; /* ROBO_U_SCRIPT_COUNT */
    robo_font_coverage_stats_t stats;
};

typedef struct {
    robo_font_coverage_t *cov;
    const robo_font_coverage_options_t *opt;
    uint32_t visited;
    uint32_t open_first;
    uint32_t open_last;
    int open;
} coverage_build_t;

void robo_font_coverage_options_init(robo_font_coverage_options_t *opt)
{
    if (!opt) {
        return;
    }
    memset(opt, 0, sizeof(*opt));
    opt->analyze_scripts = 1;
    opt->analyze_categories = 1;
    opt->analyze_emoji = 1;
    opt->max_ranges = ROBO_FONT_COVERAGE_MAX_RANGES;
    opt->max_visit = ROBO_FONT_COVERAGE_MAX_VISIT;
}

static uint32_t opt_max_ranges(const robo_font_coverage_options_t *opt)
{
    uint32_t m = opt && opt->max_ranges ? opt->max_ranges : ROBO_FONT_COVERAGE_MAX_RANGES;
    return m > ROBO_FONT_COVERAGE_MAX_RANGES ? ROBO_FONT_COVERAGE_MAX_RANGES : m;
}

static uint32_t opt_max_visit(const robo_font_coverage_options_t *opt)
{
    uint32_t m = opt && opt->max_visit ? opt->max_visit : ROBO_FONT_COVERAGE_MAX_VISIT;
    return m > ROBO_FONT_COVERAGE_MAX_VISIT ? ROBO_FONT_COVERAGE_MAX_VISIT : m;
}

static void coverage_clear(robo_font_coverage_t *cov)
{
    if (!cov) {
        return;
    }
    if (cov->cmap) {
        robo_font_cmap_map_release(cov->cmap);
        cov->cmap = NULL;
    }
    robo_free(cov->ranges);
    robo_free(cov->scripts);
    robo_free(cov->script_seen);
    memset(cov, 0, sizeof(*cov));
}

static robo_result_t flush_open_range(coverage_build_t *b)
{
    robo_font_coverage_t *cov;
    uint32_t max_r;
    if (!b || !b->open) {
        return ROBO_OK;
    }
    cov = b->cov;
    max_r = opt_max_ranges(b->opt);
    if (cov->range_count >= max_r) {
        cov->stats.truncated = 1;
        b->open = 0;
        return ROBO_OK;
    }
    if (cov->range_count >= cov->range_cap) {
        uint32_t ncap = cov->range_cap ? cov->range_cap * 2u : 16u;
        robo_font_coverage_range_t *nr;
        if (ncap > max_r) {
            ncap = max_r;
        }
        if (ncap <= cov->range_cap) {
            cov->stats.truncated = 1;
            b->open = 0;
            return ROBO_OK;
        }
        nr = (robo_font_coverage_range_t *)robo_realloc(
            cov->ranges, (size_t)ncap * sizeof(*nr));
        if (!nr) {
            return ROBO_OUT_OF_MEMORY;
        }
        cov->ranges = nr;
        cov->range_cap = ncap;
    }
    cov->ranges[cov->range_count].first = b->open_first;
    cov->ranges[cov->range_count].last = b->open_last;
    cov->range_count++;
    b->open = 0;
    return ROBO_OK;
}

static robo_result_t add_script(robo_font_coverage_t *cov, robo_unicode_script_t sc)
{
    if (sc == ROBO_U_SCRIPT_COMMON) {
        cov->stats.has_common = 1;
        return ROBO_OK;
    }
    if (sc == ROBO_U_SCRIPT_INHERITED) {
        cov->stats.has_inherited = 1;
        return ROBO_OK;
    }
    if ((unsigned)sc >= (unsigned)ROBO_U_SCRIPT_COUNT) {
        return ROBO_OK;
    }
    if (!cov->script_seen) {
        return ROBO_OK;
    }
    if (cov->script_seen[sc]) {
        return ROBO_OK;
    }
    if (cov->script_count >= ROBO_FONT_COVERAGE_MAX_SCRIPTS) {
        cov->stats.truncated = 1;
        return ROBO_OK;
    }
    cov->script_seen[sc] = 1;
    cov->scripts[cov->script_count++] = sc;
    return ROBO_OK;
}

static robo_result_t analyze_cp(coverage_build_t *b, uint32_t cp)
{
    robo_font_coverage_t *cov = b->cov;
    const robo_font_coverage_options_t *opt = b->opt;
    robo_unicode_script_t scx[16];
    size_t n;
    size_t i;

    if (opt && opt->analyze_categories) {
        robo_unicode_general_category_t gc = robo_unicode_category((robo_uchar32_t)cp);
        if ((unsigned)gc < 32u) {
            cov->stats.category_bits |= (1u << (unsigned)gc);
        }
    }
    if (opt && opt->analyze_emoji && robo_unicode_is_emoji((robo_uchar32_t)cp)) {
        cov->stats.emoji_scalars++;
    }
    if (!opt || !opt->analyze_scripts) {
        return ROBO_OK;
    }
    n = robo_unicode_script_extensions((robo_uchar32_t)cp, scx,
                                       sizeof(scx) / sizeof(scx[0]));
    if (n == 0u) {
        return add_script(cov, robo_unicode_script((robo_uchar32_t)cp));
    }
    for (i = 0u; i < n && i < sizeof(scx) / sizeof(scx[0]); i++) {
        robo_result_t res = add_script(cov, scx[i]);
        if (res != ROBO_OK) {
            return res;
        }
    }
    return ROBO_OK;
}

static robo_result_t on_mapped(uint32_t cp, robo_glyph_id_t gid, void *user)
{
    coverage_build_t *b = (coverage_build_t *)user;
    robo_font_coverage_t *cov = b->cov;
    uint32_t max_v = opt_max_visit(b->opt);
    uint32_t num_glyphs;
    robo_result_t res;

    if (b->visited >= max_v) {
        cov->stats.truncated = 1;
        return ROBO_NOT_SUPPORTED; /* abort foreach early */
    }
    b->visited++;
    cov->stats.mapped_scalars++;

    num_glyphs = robo_font_cmap_map_num_glyphs(cov->cmap);
    if (gid >= num_glyphs) {
        cov->stats.invalid_mappings++;
        return ROBO_OK;
    }
    if (gid == 0u) {
        cov->stats.notdef_mappings++;
        return ROBO_OK;
    }

    cov->stats.usable_mappings++;
    if (cov->stats.min_code_point > cp) {
        cov->stats.min_code_point = cp;
    }
    if (cov->stats.max_code_point < cp) {
        cov->stats.max_code_point = cp;
    }

    res = analyze_cp(b, cp);
    if (res != ROBO_OK) {
        return res;
    }

    if (!b->open) {
        b->open = 1;
        b->open_first = cp;
        b->open_last = cp;
        return ROBO_OK;
    }
    if (cp == b->open_last + 1u) {
        b->open_last = cp;
        return ROBO_OK;
    }
    res = flush_open_range(b);
    if (res != ROBO_OK) {
        return res;
    }
    b->open = 1;
    b->open_first = cp;
    b->open_last = cp;
    return ROBO_OK;
}

robo_result_t robo_font_coverage_create_from_cmap(
    const robo_font_cmap_map_t *map,
    const robo_font_coverage_options_t *opt,
    robo_font_coverage_t **out)
{
    robo_font_coverage_t *cov;
    robo_font_coverage_options_t local;
    coverage_build_t build;
    robo_result_t res;

    if (!out || !map) {
        return ROBO_INVALID_ARGUMENT;
    }
    *out = NULL;
    if (!opt) {
        robo_font_coverage_options_init(&local);
        opt = &local;
    }

    cov = (robo_font_coverage_t *)robo_calloc(1u, sizeof(*cov));
    if (!cov) {
        return ROBO_OUT_OF_MEMORY;
    }
    cov->refs = 1u;
    cov->stats.min_code_point = 0xFFFFFFFFu;
    cov->stats.max_code_point = 0u;
    cov->stats.has_uvs = robo_font_cmap_map_has_uvs(map);

    cov->script_seen = (uint8_t *)robo_calloc((size_t)ROBO_U_SCRIPT_COUNT, 1u);
    cov->scripts = (robo_unicode_script_t *)robo_calloc(ROBO_FONT_COVERAGE_MAX_SCRIPTS,
                                                        sizeof(robo_unicode_script_t));
    if (!cov->script_seen || !cov->scripts) {
        coverage_clear(cov);
        robo_free(cov);
        return ROBO_OUT_OF_MEMORY;
    }

    robo_font_cmap_map_retain((robo_font_cmap_map_t *)map);
    cov->cmap = (robo_font_cmap_map_t *)map;

    memset(&build, 0, sizeof(build));
    build.cov = cov;
    build.opt = opt;
    res = robo_font_cmap_foreach_mapped(map, on_mapped, &build);
    if (res == ROBO_NOT_SUPPORTED && cov->stats.truncated) {
        res = ROBO_OK; /* visit limit — keep partial coverage */
    }
    if (res == ROBO_OK) {
        res = flush_open_range(&build);
    }
    if (res != ROBO_OK) {
        coverage_clear(cov);
        robo_free(cov);
        return res;
    }

    if (cov->stats.usable_mappings == 0u) {
        cov->stats.min_code_point = 0u;
    }
    cov->stats.range_count = cov->range_count;
    cov->stats.script_count = cov->script_count;
    *out = cov;
    return ROBO_OK;
}

robo_result_t robo_font_coverage_create_from_face(
    robo_font_face_t *face,
    const robo_font_coverage_options_t *opt,
    robo_font_coverage_t **out)
{
    const robo_font_coverage_t *existing;
    robo_result_t res;

    if (!out || !face) {
        return ROBO_INVALID_ARGUMENT;
    }
    *out = NULL;
    (void)opt; /* face-attached coverage always uses default analysis options */

    existing = robo_font_face_coverage(face);
    if (existing) {
        robo_font_coverage_retain((robo_font_coverage_t *)existing);
        *out = (robo_font_coverage_t *)existing;
        return ROBO_OK;
    }
    res = robo_font_face_parse_coverage(face);
    if (res != ROBO_OK) {
        return res;
    }
    existing = robo_font_face_coverage(face);
    if (!existing) {
        return ROBO_NOT_FOUND;
    }
    robo_font_coverage_retain((robo_font_coverage_t *)existing);
    *out = (robo_font_coverage_t *)existing;
    return ROBO_OK;
}

void robo_font_coverage_retain(robo_font_coverage_t *cov)
{
    if (cov) {
        cov->refs++;
    }
}

void robo_font_coverage_release(robo_font_coverage_t *cov)
{
    if (!cov) {
        return;
    }
    if (cov->refs > 1u) {
        cov->refs--;
        return;
    }
    coverage_clear(cov);
    robo_free(cov);
}

const robo_font_cmap_map_t *robo_font_coverage_cmap(const robo_font_coverage_t *cov)
{
    return cov ? cov->cmap : NULL;
}

const robo_font_coverage_stats_t *robo_font_coverage_stats(
    const robo_font_coverage_t *cov)
{
    return cov ? &cov->stats : NULL;
}

uint32_t robo_font_coverage_range_count(const robo_font_coverage_t *cov)
{
    return cov ? cov->range_count : 0u;
}

const robo_font_coverage_range_t *robo_font_coverage_range(
    const robo_font_coverage_t *cov, uint32_t index)
{
    if (!cov || index >= cov->range_count) {
        return NULL;
    }
    return &cov->ranges[index];
}

uint32_t robo_font_coverage_script_count(const robo_font_coverage_t *cov)
{
    return cov ? cov->script_count : 0u;
}

robo_unicode_script_t robo_font_coverage_script(const robo_font_coverage_t *cov,
                                                uint32_t index)
{
    if (!cov || index >= cov->script_count) {
        return ROBO_U_SCRIPT_UNKNOWN;
    }
    return cov->scripts[index];
}

int robo_font_coverage_has_script(const robo_font_coverage_t *cov,
                                  robo_unicode_script_t script)
{
    uint32_t i;
    if (!cov) {
        return 0;
    }
    if (script == ROBO_U_SCRIPT_COMMON) {
        return cov->stats.has_common;
    }
    if (script == ROBO_U_SCRIPT_INHERITED) {
        return cov->stats.has_inherited;
    }
    for (i = 0u; i < cov->script_count; i++) {
        if (cov->scripts[i] == script) {
            return 1;
        }
    }
    return 0;
}

static int range_contains(const robo_font_coverage_t *cov, uint32_t cp)
{
    uint32_t lo = 0u;
    uint32_t hi;
    if (!cov || cov->range_count == 0u) {
        return 0;
    }
    hi = cov->range_count;
    while (lo < hi) {
        uint32_t mid = lo + (hi - lo) / 2u;
        if (cp > cov->ranges[mid].last) {
            lo = mid + 1u;
        } else {
            hi = mid;
        }
    }
    if (lo >= cov->range_count) {
        return 0;
    }
    return cp >= cov->ranges[lo].first && cp <= cov->ranges[lo].last;
}

int robo_font_coverage_contains(const robo_font_coverage_t *cov, uint32_t code_point)
{
    if (!cov || !robo_font_cmap_is_unicode_scalar(code_point)) {
        return 0;
    }
    return range_contains(cov, code_point);
}

robo_result_t robo_font_coverage_query(const robo_font_coverage_t *cov,
                                       uint32_t code_point,
                                       robo_font_coverage_query_result_t *out)
{
    robo_font_cmap_lookup_result_t lr;
    robo_result_t res;

    if (!out) {
        return ROBO_INVALID_ARGUMENT;
    }
    memset(out, 0, sizeof(*out));
    out->outline_hint = ROBO_FONT_COVERAGE_OUTLINE_UNKNOWN;
    if (!cov || !cov->cmap) {
        out->status = ROBO_FONT_COVERAGE_NO_CMAP;
        return ROBO_OK;
    }
    if (!robo_font_cmap_is_unicode_scalar(code_point)) {
        out->status = ROBO_FONT_COVERAGE_INVALID_CODE_POINT;
        return ROBO_OK;
    }
    res = robo_font_cmap_lookup(cov->cmap, code_point, &lr);
    if (res != ROBO_OK) {
        return res;
    }
    if (lr.status == ROBO_FONT_CMAP_INVALID_CODE_POINT) {
        out->status = ROBO_FONT_COVERAGE_INVALID_CODE_POINT;
        return ROBO_OK;
    }
    if (lr.status == ROBO_FONT_CMAP_INVALID_FONT) {
        out->status = ROBO_FONT_COVERAGE_INVALID_MAPPING;
        return ROBO_OK;
    }
    if (lr.status == ROBO_FONT_CMAP_UNMAPPED) {
        out->status = ROBO_FONT_COVERAGE_UNSUPPORTED;
        return ROBO_OK;
    }
    /* MAPPED */
    out->glyph_id = lr.glyph_id;
    if (lr.glyph_id == 0u) {
        out->status = ROBO_FONT_COVERAGE_MAPPED_NOTDEF;
    } else if (lr.glyph_id >= robo_font_cmap_map_num_glyphs(cov->cmap)) {
        out->status = ROBO_FONT_COVERAGE_INVALID_MAPPING;
    } else {
        out->status = ROBO_FONT_COVERAGE_MAPPED_USABLE;
    }
    return ROBO_OK;
}

robo_result_t robo_font_coverage_glyph(const robo_font_coverage_t *cov,
                                       uint32_t code_point,
                                       robo_glyph_id_t *out_gid)
{
    robo_font_coverage_query_result_t q;
    robo_result_t res;

    if (!out_gid) {
        return ROBO_INVALID_ARGUMENT;
    }
    *out_gid = 0u;
    res = robo_font_coverage_query(cov, code_point, &q);
    if (res != ROBO_OK) {
        return res;
    }
    if (q.status != ROBO_FONT_COVERAGE_MAPPED_USABLE) {
        return ROBO_NOT_FOUND;
    }
    *out_gid = q.glyph_id;
    return ROBO_OK;
}

robo_result_t robo_font_coverage_query_uvs(const robo_font_coverage_t *cov,
                                           uint32_t base_code_point,
                                           uint32_t variation_selector,
                                           robo_font_cmap_uvs_result_t *out)
{
    if (!cov || !cov->cmap) {
        if (out) {
            memset(out, 0, sizeof(*out));
            out->status = ROBO_FONT_CMAP_UVS_NONE;
            out->base_code_point = base_code_point;
            out->variation_selector = variation_selector;
        }
        return cov ? ROBO_OK : ROBO_INVALID_ARGUMENT;
    }
    return robo_font_cmap_lookup_uvs(cov->cmap, base_code_point, variation_selector,
                                     out);
}

robo_result_t robo_font_coverage_probe_outline(robo_font_face_t *face,
                                               robo_glyph_id_t glyph_id,
                                               robo_font_coverage_outline_hint_t *out)
{
    robo_font_outline_t *ol = NULL;
    robo_result_t res;
    robo_font_outline_status_t st;

    if (!out) {
        return ROBO_INVALID_ARGUMENT;
    }
    *out = ROBO_FONT_COVERAGE_OUTLINE_UNKNOWN;
    if (!face) {
        return ROBO_INVALID_ARGUMENT;
    }
    res = robo_font_face_glyph_outline(face, glyph_id, &ol);
    if (res != ROBO_OK) {
        return res;
    }
    if (!ol) {
        return ROBO_OK;
    }
    st = robo_font_outline_status(ol);
    if (st == ROBO_FONT_OUTLINE_STATUS_EMPTY) {
        *out = ROBO_FONT_COVERAGE_OUTLINE_EMPTY;
    } else if (st == ROBO_FONT_OUTLINE_STATUS_OK) {
        *out = ROBO_FONT_COVERAGE_OUTLINE_NONEMPTY;
    }
    robo_font_outline_release(ol);
    return ROBO_OK;
}

uint64_t robo_font_coverage_approx_bytes(const robo_font_coverage_t *cov)
{
    if (!cov) {
        return 0u;
    }
    return (uint64_t)sizeof(*cov) +
           (uint64_t)cov->range_cap * (uint64_t)sizeof(robo_font_coverage_range_t) +
           (uint64_t)ROBO_FONT_COVERAGE_MAX_SCRIPTS * (uint64_t)sizeof(robo_unicode_script_t) +
           (uint64_t)ROBO_U_SCRIPT_COUNT;
}

robo_result_t robo_font_coverage_fuzz_parse(const uint8_t *data, size_t size)
{
    robo_font_cmap_map_t *map = NULL;
    robo_font_coverage_t *cov = NULL;
    robo_font_coverage_options_t opt;
    robo_result_t res;
    robo_font_span_t span;

    if (!data && size > 0u) {
        return ROBO_INVALID_ARGUMENT;
    }
    (void)robo_font_cmap_fuzz_parse(data, size);
    span.data = data;
    span.size = size;
    res = robo_font_cmap_map_create_from_span(span, 256u, &map);
    if (res != ROBO_OK || !map) {
        return res == ROBO_OUT_OF_MEMORY ? res : ROBO_OK;
    }
    robo_font_coverage_options_init(&opt);
    opt.max_visit = 4096u;
    opt.max_ranges = 256u;
    res = robo_font_coverage_create_from_cmap(map, &opt, &cov);
    if (cov) {
        robo_font_coverage_query_result_t q;
        (void)robo_font_coverage_query(cov, 0x41u, &q);
        (void)robo_font_coverage_contains(cov, 0x41u);
        robo_font_coverage_release(cov);
    }
    robo_font_cmap_map_release(map);
    return res == ROBO_OUT_OF_MEMORY ? res : ROBO_OK;
}
