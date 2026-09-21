/*
 * RoboFontConformance — conformance architecture helpers (Goal 5.21).
 *
 * Provides category IDs, golden-data serialization, and diagnostic formatting
 * for the RoboFont regression boundary. Does not replace per-goal suites;
 * orchestrates deterministic expected-value checks.
 *
 * Chromium/OTS/Skia are reference-only — no external font-test dependency.
 */

#ifndef ROBO_FONT_CONFORMANCE_H
#define ROBO_FONT_CONFORMANCE_H

#include "RoboBase/RoboError.h"
#include "RoboFont/RoboFontSfnt.h"
#include "RoboFont/RoboFontTypes.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---- Categories (map to --filter font.conform.<name>) ---- */

typedef enum FontConformCategory {
    RoboFontConformSfnt = 1,
    RoboFontConformTruetype = 2,
    RoboFontConformOpentype = 3,
    RoboFontConformCff = 4,
    RoboFontConformCmap = 5,
    RoboFontConformMetrics = 6,
    RoboFontConformNames = 7,
    RoboFontConformStyle = 8,
    RoboFontConformSelection = 9,
    RoboFontConformDiscovery = 10,
    RoboFontConformLoading = 11,
    RoboFontConformCoverage = 12,
    RoboFontConformFallback = 13,
    RoboFontConformGeneric = 14,
    RoboFontConformEmoji = 15,
    RoboFontConformVariation = 16,
    RoboFontConformColor = 17,
    RoboFontConformSecurity = 18,
    RoboFontConformLimits = 19,
    RoboFontConformCrossTable = 20,
    RoboFontConformGolden = 21,
    RoboFontConformProperty = 22,
    RoboFontConformFuzz = 23,
    RoboFontConformBench = 24
} FontConformCategory;

typedef enum FontConformKind {
    RoboFontConformKindUnit = 0,
    RoboFontConformKindStructural = 1,
    RoboFontConformKindBehavioral = 2,
    RoboFontConformKindMalformed = 3,
    RoboFontConformKindSecurity = 4,
    RoboFontConformKindRegression = 5,
    RoboFontConformKindGolden = 6,
    RoboFontConformKindProperty = 7,
    RoboFontConformKindFuzz = 8,
    RoboFontConformKindBench = 9
} FontConformKind;

#define RoboFontConformMaxDiagnostics 16u
#define RoboFontGoldenMax 512u

typedef struct FontConformCase {
    const char *id;          /* e.g. "font.conform.cmap" */
    FontConformCategory category;
    FontConformKind kind;
    const char *fixture;     /* corpus relative path or "synthetic:<name>" */
    const char *purpose;
} FontConformCase;

typedef struct FontConformDiagnostic {
    char fixture[64];
    char tableTag[8]; /* four-char + NUL, or empty */
    uint32_t glyphId; /* UINT32_MAX if N/A */
    uint32_t codePoint; /* UINT32_MAX if N/A */
    FontTag axisTag; /* 0 if N/A */
    char expected[96];
    char actual[96];
    char message[128];
} FontConformDiagnostic;

typedef struct FontConformResult {
    int ok; /* 1 = pass */
    uint32_t diagnosticCount;
    FontConformDiagnostic diagnostics[RoboFontConformMaxDiagnostics];
} FontConformResult;

typedef struct FontGoldenData {
    char bytes[RoboFontGoldenMax];
    size_t length;
} FontGoldenData;

const char *FontConformCategoryString(FontConformCategory cat);
const char *FontConformKindString(FontConformKind kind);

void FontConformResultInit(FontConformResult *r);

/** Append a mismatch diagnostic (truncated safely). Returns RoboOk or RoboError if full. */
Result FontConformResultAdd(
    FontConformResult *r,
    const char *fixture,
    const char *tableTag,
    uint32_t glyphId,
    uint32_t codePoint,
    FontTag axisTag,
    const char *expected,
    const char *actual,
    const char *message);

void FontGoldenInit(FontGoldenData *g);
Result FontGoldenAppend(FontGoldenData *g, const char *text);
Result FontGoldenAppendU32(FontGoldenData *g, const char *key,
                                          uint32_t value);
Result FontGoldenAppendI32(FontGoldenData *g, const char *key,
                                          int32_t value);
Result FontGoldenAppendF32(FontGoldenData *g, const char *key,
                                          float value);
Result FontGoldenAppendStr(FontGoldenData *g, const char *key,
                                          const char *value);

/** Exact string compare of golden payloads (deterministic). */
int FontGoldenEquals(const FontGoldenData *a,
                            const FontGoldenData *b);
int FontGoldenEqualsCstr(const FontGoldenData *g, const char *expected);

/**
 * Fuzz smoke for conformance harness — exercises validate + cmap parse
 * entry points with a bounded budget. Never crashes on malformed input.
 */
Result FontConformFuzzSmoke(const uint8_t *data, size_t size);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_FONT_CONFORMANCE_H */
