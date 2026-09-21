/*
 * RoboFontConformance — golden serialization + diagnostics (Goal 5.21).
 */

#include "RoboFont/RoboFontConformance.h"

#include "RoboFont/RoboFontCmap.h"
#include "RoboFont/RoboFontValidate.h"

#include <stdio.h>
#include <string.h>

const char *FontConformCategoryString(FontConformCategory cat)
{
    switch (cat) {
    case RoboFontConformSfnt:
        return "sfnt";
    case RoboFontConformTruetype:
        return "truetype";
    case RoboFontConformOpentype:
        return "opentype";
    case RoboFontConformCff:
        return "cff";
    case RoboFontConformCmap:
        return "cmap";
    case RoboFontConformMetrics:
        return "metrics";
    case RoboFontConformNames:
        return "names";
    case RoboFontConformStyle:
        return "style";
    case RoboFontConformSelection:
        return "selection";
    case RoboFontConformDiscovery:
        return "discovery";
    case RoboFontConformLoading:
        return "loading";
    case RoboFontConformCoverage:
        return "coverage";
    case RoboFontConformFallback:
        return "fallback";
    case RoboFontConformGeneric:
        return "generic";
    case RoboFontConformEmoji:
        return "emoji";
    case RoboFontConformVariation:
        return "variation";
    case RoboFontConformColor:
        return "color";
    case RoboFontConformSecurity:
        return "security";
    case RoboFontConformLimits:
        return "limits";
    case RoboFontConformCrossTable:
        return "cross_table";
    case RoboFontConformGolden:
        return "golden";
    case RoboFontConformProperty:
        return "property";
    case RoboFontConformFuzz:
        return "fuzz";
    case RoboFontConformBench:
        return "bench";
    default:
        return "unknown";
    }
}

const char *FontConformKindString(FontConformKind kind)
{
    switch (kind) {
    case RoboFontConformKindUnit:
        return "unit";
    case RoboFontConformKindStructural:
        return "structural";
    case RoboFontConformKindBehavioral:
        return "behavioral";
    case RoboFontConformKindMalformed:
        return "malformed";
    case RoboFontConformKindSecurity:
        return "security";
    case RoboFontConformKindRegression:
        return "regression";
    case RoboFontConformKindGolden:
        return "golden";
    case RoboFontConformKindProperty:
        return "property";
    case RoboFontConformKindFuzz:
        return "fuzz";
    case RoboFontConformKindBench:
        return "bench";
    default:
        return "unknown";
    }
}

void FontConformResultInit(FontConformResult *r)
{
    if (!r) {
        return;
    }
    memset(r, 0, sizeof(*r));
    r->ok = 1;
}

static void CopyTrunc(char *dst, size_t cap, const char *src)
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

Result FontConformResultAdd(
    FontConformResult *r,
    const char *fixture,
    const char *tableTag,
    uint32_t glyphId,
    uint32_t codePoint,
    FontTag axisTag,
    const char *expected,
    const char *actual,
    const char *message)
{
    FontConformDiagnostic *d;

    if (!r) {
        return RoboInvalidArgument;
    }
    r->ok = 0;
    if (r->diagnosticCount >= RoboFontConformMaxDiagnostics) {
        return RoboError;
    }
    d = &r->diagnostics[r->diagnosticCount++];
    memset(d, 0, sizeof(*d));
    CopyTrunc(d->fixture, sizeof(d->fixture), fixture);
    CopyTrunc(d->tableTag, sizeof(d->tableTag), tableTag);
    d->glyphId = glyphId;
    d->codePoint = codePoint;
    d->axisTag = axisTag;
    CopyTrunc(d->expected, sizeof(d->expected), expected);
    CopyTrunc(d->actual, sizeof(d->actual), actual);
    CopyTrunc(d->message, sizeof(d->message), message);
    return RoboOk;
}

void FontGoldenInit(FontGoldenData *g)
{
    if (!g) {
        return;
    }
    g->bytes[0] = '\0';
    g->length = 0u;
}

Result FontGoldenAppend(FontGoldenData *g, const char *text)
{
    size_t n;
    size_t avail;

    if (!g || !text) {
        return RoboInvalidArgument;
    }
    n = strlen(text);
    avail = (g->length < RoboFontGoldenMax - 1u)
                ? (RoboFontGoldenMax - 1u - g->length)
                : 0u;
    if (n > avail) {
        return RoboError;
    }
    memcpy(g->bytes + g->length, text, n);
    g->length += n;
    g->bytes[g->length] = '\0';
    return RoboOk;
}

Result FontGoldenAppendU32(FontGoldenData *g, const char *key,
                                          uint32_t value)
{
    char line[96];
    int n;

    if (!g || !key) {
        return RoboInvalidArgument;
    }
    n = snprintf(line, sizeof(line), "%s=%u\n", key, (unsigned)value);
    if (n < 0 || (size_t)n >= sizeof(line)) {
        return RoboError;
    }
    return FontGoldenAppend(g, line);
}

Result FontGoldenAppendI32(FontGoldenData *g, const char *key,
                                          int32_t value)
{
    char line[96];
    int n;

    if (!g || !key) {
        return RoboInvalidArgument;
    }
    n = snprintf(line, sizeof(line), "%s=%d\n", key, (int)value);
    if (n < 0 || (size_t)n >= sizeof(line)) {
        return RoboError;
    }
    return FontGoldenAppend(g, line);
}

Result FontGoldenAppendF32(FontGoldenData *g, const char *key,
                                          float value)
{
    char line[96];
    int n;

    if (!g || !key) {
        return RoboInvalidArgument;
    }
    /* Fixed 6 decimal places — deterministic across platforms. */
    n = snprintf(line, sizeof(line), "%s=%.6f\n", key, (double)value);
    if (n < 0 || (size_t)n >= sizeof(line)) {
        return RoboError;
    }
    return FontGoldenAppend(g, line);
}

Result FontGoldenAppendStr(FontGoldenData *g, const char *key,
                                          const char *value)
{
    char line[160];
    int n;

    if (!g || !key) {
        return RoboInvalidArgument;
    }
    if (!value) {
        value = "";
    }
    n = snprintf(line, sizeof(line), "%s=%s\n", key, value);
    if (n < 0 || (size_t)n >= sizeof(line)) {
        return RoboError;
    }
    return FontGoldenAppend(g, line);
}

int FontGoldenEquals(const FontGoldenData *a,
                            const FontGoldenData *b)
{
    if (!a || !b) {
        return 0;
    }
    if (a->length != b->length) {
        return 0;
    }
    return memcmp(a->bytes, b->bytes, a->length) == 0;
}

int FontGoldenEqualsCstr(const FontGoldenData *g, const char *expected)
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

Result FontConformFuzzSmoke(const uint8_t *data, size_t size)
{
    /* Bound work; ignore result — must not crash / hang. */
    (void)FontValidateFuzzParse(data, size);
    (void)FontCmapFuzzParse(data, size);
    return RoboOk;
}
