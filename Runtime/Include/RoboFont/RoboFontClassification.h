/*
 * RoboFont classification — style / weight / stretch model (Goal 5.10).
 *
 * Distinguishes font-authored raw metadata from Roboticela normalized
 * classification. Does not perform font selection or matching (Goal 5.11+).
 * Does not parse fvar/avar/gvar (Goal 5.18).
 *
 * Precedence (deterministic; see docs/Classification.md):
 *
 * Weight:
 *   1. OS/2.usWeightClass if 1..1000
 *   2. else OS/2 fsSelection BOLD → 700
 *   3. else head.macStyle bold → 700
 *   4. else default 400
 *   (valid usWeightClass is never overridden by bold bits)
 *
 * Stretch:
 *   1. OS/2.usWidthClass if 1..9
 *   2. else default width 5 / stretch 100%
 *
 * Style:
 *   1. OS/2 OBLIQUE (bit 9) → oblique
 *   2. else OS/2 ITALIC (bit 0) → italic
 *   3. else head.macStyle italic → italic
 *   4. else post.italicAngle != 0 → oblique (angle preserved)
 *   5. else normal
 *
 * STAT axis tags are retained for variable-font preparation only; they do not
 * override OS/2 numeric classification in Goal 5.10.
 */

#ifndef ROBO_FONT_CLASSIFICATION_H
#define ROBO_FONT_CLASSIFICATION_H

#include "RoboBase/RoboError.h"
#include "RoboFont/RoboFontName.h"
#include "RoboFont/RoboFontOt.h"
#include "RoboFont/RoboFontStyle.h"
#include "RoboFont/RoboFontTt.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct FontFace FontFace;

/* ---- fsSelection bits that influence classification ---- */

#define RoboFontFsItalic   0x0001u
#define RoboFontFsBold     0x0020u
#define RoboFontFsRegular  0x0040u
#define RoboFontFsWws      0x0100u
#define RoboFontFsOblique  0x0200u

/* Preserved metadata only (do not drive style/weight normalization): */
#define RoboFontFsUnderscore       0x0002u
#define RoboFontFsNegative         0x0004u
#define RoboFontFsOutlined         0x0008u
#define RoboFontFsStrikeout        0x0010u
#define RoboFontFsUseTypoMetrics 0x0080u

/* head.macStyle */
#define RoboFontMacBold   0x0001u
#define RoboFontMacItalic 0x0002u

typedef enum FontClassSource {
    RoboFontClassSrcNone = 0,
    RoboFontClassSrcOs2 = 1,
    RoboFontClassSrcFsSelection = 2,
    RoboFontClassSrcHead = 3,
    RoboFontClassSrcPost = 4,
    RoboFontClassSrcStat = 5, /* reserved; unused for numeric override in 5.10 */
    RoboFontClassSrcName = 6, /* reserved; no string weight inference in 5.10 */
    RoboFontClassSrcDefault = 7
} FontClassSource;

typedef enum FontClassAvail {
    RoboFontClassPresent = 0,     /* from authoritative font field */
    RoboFontClassFallback = 1,    /* derived from secondary indicator */
    RoboFontClassDefault = 2,     /* safe default when missing */
    RoboFontClassInvalid = 3      /* raw present but out of range */
} FontClassAvail;

/**
 * Font-authored raw classification inputs. Never overwritten by normalized
 * values. All fields are value copies (no dangling table pointers).
 */
typedef struct FontClassRaw {
    uint8_t hasOs2;
    uint16_t os2Version;
    uint16_t usWeightClass;
    uint16_t usWidthClass;
    uint16_t fsSelection;

    uint8_t hasHead;
    uint16_t macStyle;

    uint8_t hasPost;
    int32_t italicAngleFixed; /* post Fixed 16.16 */

    uint8_t hasStat;
    uint16_t statAxisCount;
    uint8_t statHasWght;
    uint8_t statHasWdth;
    uint8_t statHasItal;
    uint8_t statHasSlnt;
    uint8_t statHasOpsz;

    uint8_t hasNameMeta;
    uint8_t nameHasLegacyFamily;       /* ID 1 */
    uint8_t nameHasLegacySubfamily;    /* ID 2 */
    uint8_t nameHasTypoFamily;         /* ID 16 */
    uint8_t nameHasTypoSubfamily;      /* ID 17 */
    uint8_t nameHasWwsFamily;          /* ID 21 */
    uint8_t nameHasWwsSubfamily;       /* ID 22 */
} FontClassRaw;

/**
 * Normalized Roboticela classification + provenance.
 * Immutable after create. Concurrent readers OK under face lifetime.
 */
typedef struct FontClassification {
    FontClassRaw raw;

    FontWeight weight;
    FontWeightCategory weightCategory;
    FontClassSource weightSource;
    FontClassAvail weightAvail;

    FontStretch stretch;
    FontWidthClass widthClass;
    FontClassSource stretchSource;
    FontClassAvail stretchAvail;

    FontStyle style;
    FontClassSource styleSource;
    FontClassAvail styleAvail;

    /** 1 if post.italicAngle available; value is Fixed 16.16 (raw preserved). */
    uint8_t hasItalicAngle;
    int32_t italicAngleFixed;
    /** Millidegrees ≈ Fixed*1000/65536; only set when hasItalicAngle. */
    int32_t italicAngleMillidegrees;

    /** fsSelection WWS bit (naming model hint; not a style). */
    uint8_t wwsFlag;

    /** Diagnostic: REGULAR bit set together with italic/oblique. */
    uint8_t conflictRegularAndSlope;
    /** Diagnostic: BOLD bit set but usWeightClass valid and not bold-range. */
    uint8_t conflictBoldBitVsWeight;
    /** Diagnostic: ITALIC and OBLIQUE both set (OBLIQUE wins for style). */
    uint8_t bothItalicAndOblique;

    /** Convenience mirror for face/instance styleDesc consumers. */
    FontStyleDesc styleDesc;
} FontClassification;

typedef struct FontClassificationInfo FontClassificationInfo;

/**
 * Build classification from already-parsed TT (+ optional STAT + name meta).
 * Does not parse SFNT. Owns a value-copy classification snapshot.
 */
Result FontClassificationInfoCreate(
    const FontTt *tt,
    const FontStat *statOpt,
    const FontMetadata *metaOpt,
    FontClassificationInfo **out);

/** Classify from an SFNT (parses TT; optionally OT EXTRA for STAT). */
Result FontClassificationInfoFromSfnt(
    FontSfnt *sfnt,
    const FontMetadata *metaOpt,
    FontClassificationInfo **out);

void FontClassificationInfoRetain(FontClassificationInfo *info);
void FontClassificationInfoRelease(FontClassificationInfo *info);

const FontClassification *FontClassificationInfoGet(
    const FontClassificationInfo *info);

/** Fill styleDesc from classification (weight/stretch/style only). */
void FontClassificationToStyleDesc(const FontClassification *cls,
                                            FontStyleDesc *out);

/**
 * Pure helper: classify from raw field bag (unit tests / fuzz without fonts).
 * `raw` is copied; normalized fields filled in `out`.
 */
Result FontClassify(const FontClassRaw *raw,
                                 FontClassification *out);

/** Fuzz entry: arbitrary SFNT or OS/2-ish bytes. Never crashes. */
Result FontClassificationFuzzParse(const uint8_t *data, size_t size);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_FONT_CLASSIFICATION_H */
