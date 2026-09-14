/*
 * RoboFontConformance — golden serialization + diagnostics (Goal 5.21).
 */

#include "RoboFont/RoboFontConformance.h"

#include "RoboFont/RoboFontCmap.h"
#include "RoboFont/RoboFontValidate.h"

#include <stdio.h>
#include <string.h>

const char *robo_font_conform_category_string(robo_font_conform_category_t cat)
{
    switch (cat) {
    case ROBO_FONT_CONFORM_SFNT:
        return "sfnt";
    case ROBO_FONT_CONFORM_TRUETYPE:
        return "truetype";
    case ROBO_FONT_CONFORM_OPENTYPE:
        return "opentype";
    case ROBO_FONT_CONFORM_CFF:
        return "cff";
    case ROBO_FONT_CONFORM_CMAP:
        return "cmap";
    case ROBO_FONT_CONFORM_METRICS:
        return "metrics";
    case ROBO_FONT_CONFORM_NAMES:
        return "names";
    case ROBO_FONT_CONFORM_STYLE:
        return "style";
    case ROBO_FONT_CONFORM_SELECTION:
        return "selection";
    case ROBO_FONT_CONFORM_DISCOVERY:
        return "discovery";
    case ROBO_FONT_CONFORM_LOADING:
        return "loading";
    case ROBO_FONT_CONFORM_COVERAGE:
        return "coverage";
    case ROBO_FONT_CONFORM_FALLBACK:
        return "fallback";
    case ROBO_FONT_CONFORM_GENERIC:
        return "generic";
    case ROBO_FONT_CONFORM_EMOJI:
        return "emoji";
    case ROBO_FONT_CONFORM_VARIATION:
        return "variation";
    case ROBO_FONT_CONFORM_COLOR:
        return "color";
    case ROBO_FONT_CONFORM_SECURITY:
        return "security";
    case ROBO_FONT_CONFORM_LIMITS:
        return "limits";
    case ROBO_FONT_CONFORM_CROSS_TABLE:
        return "cross_table";
    case ROBO_FONT_CONFORM_GOLDEN:
        return "golden";
    case ROBO_FONT_CONFORM_PROPERTY:
        return "property";
    case ROBO_FONT_CONFORM_FUZZ:
        return "fuzz";
    case ROBO_FONT_CONFORM_BENCH:
        return "bench";
    default:
        return "unknown";
    }
}

const char *robo_font_conform_kind_string(robo_font_conform_kind_t kind)
{
    switch (kind) {
    case ROBO_FONT_CONFORM_KIND_UNIT:
        return "unit";
    case ROBO_FONT_CONFORM_KIND_STRUCTURAL:
        return "structural";
    case ROBO_FONT_CONFORM_KIND_BEHAVIORAL:
        return "behavioral";
    case ROBO_FONT_CONFORM_KIND_MALFORMED:
        return "malformed";
    case ROBO_FONT_CONFORM_KIND_SECURITY:
        return "security";
    case ROBO_FONT_CONFORM_KIND_REGRESSION:
        return "regression";
    case ROBO_FONT_CONFORM_KIND_GOLDEN:
        return "golden";
    case ROBO_FONT_CONFORM_KIND_PROPERTY:
        return "property";
    case ROBO_FONT_CONFORM_KIND_FUZZ:
        return "fuzz";
    case ROBO_FONT_CONFORM_KIND_BENCH:
        return "bench";
    default:
        return "unknown";
    }
}

void robo_font_conform_result_init(robo_font_conform_result_t *r)
{
    if (!r) {
        return;
    }
    memset(r, 0, sizeof(*r));
    r->ok = 1;
}

static void copy_trunc(char *dst, size_t cap, const char *src)
{
    size_t n;
    if (!dst || cap == 0u) {
        return;
    }
    if (!src) {
        dst[0] = '\0';
        return;
    }
    n = strlen(src);
    if (n >= cap) {
        n = cap - 1u;
    }
    memcpy(dst, src, n);
    dst[n] = '\0';
}

robo_result_t robo_font_conform_result_add(
    robo_font_conform_result_t *r,
    const char *fixture,
    const char *table_tag,
    uint32_t glyph_id,
    uint32_t code_point,
    robo_font_tag_t axis_tag,
    const char *expected,
    const char *actual,
    const char *message)
{
    robo_font_conform_diagnostic_t *d;

    if (!r) {
        return ROBO_INVALID_ARGUMENT;
    }
    r->ok = 0;
    if (r->diagnostic_count >= ROBO_FONT_CONFORM_MAX_DIAGNOSTICS) {
        return ROBO_ERROR;
    }
    d = &r->diagnostics[r->diagnostic_count++];
    memset(d, 0, sizeof(*d));
    copy_trunc(d->fixture, sizeof(d->fixture), fixture);
    copy_trunc(d->table_tag, sizeof(d->table_tag), table_tag);
    d->glyph_id = glyph_id;
    d->code_point = code_point;
    d->axis_tag = axis_tag;
    copy_trunc(d->expected, sizeof(d->expected), expected);
    copy_trunc(d->actual, sizeof(d->actual), actual);
    copy_trunc(d->message, sizeof(d->message), message);
    return ROBO_OK;
}

void robo_font_golden_init(robo_font_golden_data_t *g)
{
    if (!g) {
        return;
    }
    g->bytes[0] = '\0';
    g->length = 0u;
}

robo_result_t robo_font_golden_append(robo_font_golden_data_t *g, const char *text)
{
    size_t n;
    size_t avail;

    if (!g || !text) {
        return ROBO_INVALID_ARGUMENT;
    }
    n = strlen(text);
    avail = (g->length < ROBO_FONT_GOLDEN_MAX - 1u)
                ? (ROBO_FONT_GOLDEN_MAX - 1u - g->length)
                : 0u;
    if (n > avail) {
        return ROBO_ERROR;
    }
    memcpy(g->bytes + g->length, text, n);
    g->length += n;
    g->bytes[g->length] = '\0';
    return ROBO_OK;
}

robo_result_t robo_font_golden_append_u32(robo_font_golden_data_t *g, const char *key,
                                          uint32_t value)
{
    char line[96];
    int n;

    if (!g || !key) {
        return ROBO_INVALID_ARGUMENT;
    }
    n = snprintf(line, sizeof(line), "%s=%u\n", key, (unsigned)value);
    if (n < 0 || (size_t)n >= sizeof(line)) {
        return ROBO_ERROR;
    }
    return robo_font_golden_append(g, line);
}

robo_result_t robo_font_golden_append_i32(robo_font_golden_data_t *g, const char *key,
                                          int32_t value)
{
    char line[96];
    int n;

    if (!g || !key) {
        return ROBO_INVALID_ARGUMENT;
    }
    n = snprintf(line, sizeof(line), "%s=%d\n", key, (int)value);
    if (n < 0 || (size_t)n >= sizeof(line)) {
        return ROBO_ERROR;
    }
    return robo_font_golden_append(g, line);
}

robo_result_t robo_font_golden_append_f32(robo_font_golden_data_t *g, const char *key,
                                          float value)
{
    char line[96];
    int n;

    if (!g || !key) {
        return ROBO_INVALID_ARGUMENT;
    }
    /* Fixed 6 decimal places — deterministic across platforms. */
    n = snprintf(line, sizeof(line), "%s=%.6f\n", key, (double)value);
    if (n < 0 || (size_t)n >= sizeof(line)) {
        return ROBO_ERROR;
    }
    return robo_font_golden_append(g, line);
}

robo_result_t robo_font_golden_append_str(robo_font_golden_data_t *g, const char *key,
                                          const char *value)
{
    char line[160];
    int n;

    if (!g || !key) {
        return ROBO_INVALID_ARGUMENT;
    }
    if (!value) {
        value = "";
    }
    n = snprintf(line, sizeof(line), "%s=%s\n", key, value);
    if (n < 0 || (size_t)n >= sizeof(line)) {
        return ROBO_ERROR;
    }
    return robo_font_golden_append(g, line);
}

int robo_font_golden_equals(const robo_font_golden_data_t *a,
                            const robo_font_golden_data_t *b)
{
    if (!a || !b) {
        return 0;
    }
    if (a->length != b->length) {
        return 0;
    }
    return memcmp(a->bytes, b->bytes, a->length) == 0;
}

int robo_font_golden_equals_cstr(const robo_font_golden_data_t *g, const char *expected)
{
    size_t n;

    if (!g || !expected) {
        return 0;
    }
    n = strlen(expected);
    if (n != g->length) {
        return 0;
    }
    return memcmp(g->bytes, expected, n) == 0;
}

robo_result_t robo_font_conform_fuzz_smoke(const uint8_t *data, size_t size)
{
    /* Bound work; ignore result — must not crash / hang. */
    (void)robo_font_validate_fuzz_parse(data, size);
    (void)robo_font_cmap_fuzz_parse(data, size);
    return ROBO_OK;
}
