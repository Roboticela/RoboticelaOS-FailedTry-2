/*
 * RoboFont selection — match a request to loaded faces (Goal 5.11).
 *
 * Operates on already-loaded RoboFontFace objects with metadata +
 * classification (Goals 5.9–5.10). Does not discover, install, or fall back
 * fonts. Does not reshape or scale glyphs.
 *
 * Matching order within a family match set (CSS Fonts–inspired):
 *   1. stretch distance
 *   2. style distance
 *   3. weight distance
 *   4. deterministic tie-break (exactness, WWS flag, faceId)
 *
 * Family identity is a gate: faces that do not match the requested family
 * (or aliases) are never selected.
 *
 * See docs/Selection.md for full rules.
 */

#ifndef ROBO_FONT_SELECTION_H
#define ROBO_FONT_SELECTION_H

#include "RoboBase/RoboError.h"
#include "RoboFont/RoboFontClassification.h"
#include "RoboFont/RoboFontFace.h"
#include "RoboFont/RoboFontStyle.h"
#include "RoboFont/RoboFontTypes.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define RoboFontSelectMaxCandidates 4096u
#define RoboFontSelectMaxAliases 32u
#define RoboFontSelectMaxFamilyBytes 1024u

/** Which name field produced the family identity match (lower rank = better). */
typedef enum FontSelectFamilyKind {
    RoboFontSelectFamilyNone = 0,
    RoboFontSelectFamilyTypographic = 1, /* name ID 16 */
    RoboFontSelectFamilyWws = 2,         /* name ID 21 */
    RoboFontSelectFamilyLegacy = 3,      /* name ID 1 */
    RoboFontSelectFamilyFull = 4,        /* name ID 4 */
    RoboFontSelectFamilyPostscript = 5,  /* name ID 6 */
    RoboFontSelectFamilyAlias = 6        /* matched via request alias */
} FontSelectFamilyKind;

typedef enum FontSelectFlags {
    RoboFontSelectFlagsNone = 0,
    /**
     * Prefer WWS family (21) over typographic (16) when both match.
     * Default: typographic > WWS > legacy > full > PostScript.
     */
    RoboFontSelectFlagPreferWws = 1,
    /** Allow full name (ID 4) as a family identity match. */
    RoboFontSelectFlagAllowFullName = 2,
    /** Allow PostScript name (ID 6) as a family identity match. */
    RoboFontSelectFlagAllowPostscript = 4
} FontSelectFlags;

/**
 * Selection request. Not a CSS type — reusable by RoboText / future CSS.
 * Strings are borrowed UTF-8 (not owned). Optional axes (Goal 5.18) allow
 * explicit variation settings and named-instance / optical-size hints.
 */
typedef struct FontSelectRequest {
    const char *family;
    size_t familyLen; /* 0 → strlen(family) */

    const char *const *aliases; /* optional; each NUL-terminated */
    uint32_t aliasCount;       /* ≤ RoboFontSelectMaxAliases */

    FontWeight weight;
    FontStretch stretch;
    FontStyle style;

    /** Optional requested oblique angle (millidegrees). Used when style=OBLIQUE. */
    uint8_t hasObliqueAngle;
    int32_t obliqueAngleMillidegrees;

    uint32_t flags; /* FontSelectFlags */

    /** Explicit variation axis settings (tagged user coords). */
    FontAxisSetting axes[RoboFontStyleMaxAxes];
    uint8_t axisCount;

    /** Named instance index when hasNamedInstance≠0. */
    uint8_t hasNamedInstance;
    uint32_t namedInstanceIndex;

    /** Optical size (opsz) preference. */
    uint8_t hasOpticalSize;
    float opticalSize;
    uint8_t opticalSizeAuto;
} FontSelectRequest;

/**
 * Per-candidate score. Lower distance keys are better.
 * `eligible` is 0 when the family gate fails (face never selected).
 */
typedef struct FontMatchScore {
    uint8_t eligible;
    FontSelectFamilyKind familyKind;
    uint32_t familyRank;   /* 0 = best family kind among policy */
    uint32_t stretchDist;  /* stretch distance key */
    uint32_t styleDist;    /* style distance key */
    uint32_t weightDist;   /* weight distance key */
    uint32_t exactBits;    /* bit0 stretch exact, bit1 style, bit2 weight */
    uint8_t wwsFlag;       /* candidate fsSelection WWS */
    FontFaceId faceId;
} FontMatchScore;

typedef struct FontSelection {
    uint32_t cookie; /* RoboFontSelectionCookie when initialized */
    uint8_t found;
    FontFace *face; /* retained when found; caller must release via
                               FontSelectionClear or face_release */
    FontFaceId faceId;
    FontMatchScore score;
    FontStyleDesc requested;
    FontStyleDesc selected;
    FontSelectFamilyKind familyKind;
} FontSelection;

/** Cookie written by init/clear; select accepts zeroed or cookied out. */
#define RoboFontSelectionCookie 0x53454C31u /* 'SEL1' */

void FontSelectRequestInit(FontSelectRequest *req);
Result FontSelectRequestValidate(const FontSelectRequest *req);

void FontSelectionInit(FontSelection *out);
void FontSelectionClear(FontSelection *sel);

/**
 * Compare two scores. Returns <0 if `a` is better, >0 if `b` is better, 0 if
 * identical keys (should not happen when faceId differs).
 */
int FontMatchScoreCompare(const FontMatchScore *a,
                                  const FontMatchScore *b);

/** Stretch / style / weight distance keys (lower better). Public for tests. */
uint32_t FontSelectStretchDist(FontStretch requested,
                                       FontStretch candidate);
uint32_t FontSelectWeightDist(FontWeight requested,
                                      FontWeight candidate);
uint32_t FontSelectStyleDist(FontStyle requested,
                                     FontStyle candidate,
                                     uint8_t reqHasAngle,
                                     int32_t reqAngleMdeg,
                                     uint8_t candHasAngle,
                                     int32_t candAngleMdeg);

/**
 * Score one face against a request. Ensures metadata + classification are
 * parsed. Sets score->eligible=0 when family does not match.
 */
Result FontSelectScoreFace(const FontSelectRequest *req,
                                          FontFace *face,
                                          FontMatchScore *outScore);

/**
 * Select the best face among candidates.
 *
 * - `out` may be zeroed or previously initialized; a prior retained face is
 *   released when `out` carries RoboFontSelectionCookie.
 * - Skips NULL entries.
 * - Does not select a face that fails the family gate.
 * - Same request + same candidate set (by faceId multiset) → same winner
 *   regardless of input order.
 * - On success with no eligible face: RoboOk and out->found == 0.
 */
Result FontSelect(const FontSelectRequest *req,
                               FontFace *const *candidates,
                               uint32_t candidateCount,
                               FontSelection *out);

/** Fuzz entry: arbitrary request-ish bytes. Never crashes. */
Result FontSelectionFuzzParse(const uint8_t *data, size_t size);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_FONT_SELECTION_H */
