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

typedef struct robo_font_face robo_font_face_t;

/* ---- fsSelection bits that influence classification ---- */

#define ROBO_FONT_FS_ITALIC   0x0001u
#define ROBO_FONT_FS_BOLD     0x0020u
#define ROBO_FONT_FS_REGULAR  0x0040u
#define ROBO_FONT_FS_WWS      0x0100u
#define ROBO_FONT_FS_OBLIQUE  0x0200u

/* Preserved metadata only (do not drive style/weight normalization): */
#define ROBO_FONT_FS_UNDERSCORE       0x0002u
#define ROBO_FONT_FS_NEGATIVE         0x0004u
#define ROBO_FONT_FS_OUTLINED         0x0008u
#define ROBO_FONT_FS_STRIKEOUT        0x0010u
#define ROBO_FONT_FS_USE_TYPO_METRICS 0x0080u

/* head.macStyle */
#define ROBO_FONT_MAC_BOLD   0x0001u
#define ROBO_FONT_MAC_ITALIC 0x0002u

typedef enum robo_font_class_source {
    ROBO_FONT_CLASS_SRC_NONE = 0,
    ROBO_FONT_CLASS_SRC_OS2 = 1,
    ROBO_FONT_CLASS_SRC_FS_SELECTION = 2,
    ROBO_FONT_CLASS_SRC_HEAD = 3,
    ROBO_FONT_CLASS_SRC_POST = 4,
    ROBO_FONT_CLASS_SRC_STAT = 5, /* reserved; unused for numeric override in 5.10 */
    ROBO_FONT_CLASS_SRC_NAME = 6, /* reserved; no string weight inference in 5.10 */
    ROBO_FONT_CLASS_SRC_DEFAULT = 7
} robo_font_class_source_t;

typedef enum robo_font_class_avail {
    ROBO_FONT_CLASS_PRESENT = 0,     /* from authoritative font field */
    ROBO_FONT_CLASS_FALLBACK = 1,    /* derived from secondary indicator */
    ROBO_FONT_CLASS_DEFAULT = 2,     /* safe default when missing */
    ROBO_FONT_CLASS_INVALID = 3      /* raw present but out of range */
} robo_font_class_avail_t;

/**
 * Font-authored raw classification inputs. Never overwritten by normalized
 * values. All fields are value copies (no dangling table pointers).
 */
typedef struct robo_font_class_raw {
    uint8_t has_os2;
    uint16_t os2_version;
    uint16_t us_weight_class;
    uint16_t us_width_class;
    uint16_t fs_selection;

    uint8_t has_head;
    uint16_t mac_style;

    uint8_t has_post;
    int32_t italic_angle_fixed; /* post Fixed 16.16 */

    uint8_t has_stat;
    uint16_t stat_axis_count;
    uint8_t stat_has_wght;
    uint8_t stat_has_wdth;
    uint8_t stat_has_ital;
    uint8_t stat_has_slnt;
    uint8_t stat_has_opsz;

    uint8_t has_name_meta;
    uint8_t name_has_legacy_family;       /* ID 1 */
    uint8_t name_has_legacy_subfamily;    /* ID 2 */
    uint8_t name_has_typo_family;         /* ID 16 */
    uint8_t name_has_typo_subfamily;      /* ID 17 */
    uint8_t name_has_wws_family;          /* ID 21 */
    uint8_t name_has_wws_subfamily;       /* ID 22 */
} robo_font_class_raw_t;

/**
 * Normalized Roboticela classification + provenance.
 * Immutable after create. Concurrent readers OK under face lifetime.
 */
typedef struct robo_font_classification {
    robo_font_class_raw_t raw;

    robo_font_weight_t weight;
    robo_font_weight_category_t weight_category;
    robo_font_class_source_t weight_source;
    robo_font_class_avail_t weight_avail;

    robo_font_stretch_t stretch;
    robo_font_width_class_t width_class;
    robo_font_class_source_t stretch_source;
    robo_font_class_avail_t stretch_avail;

    robo_font_style_t style;
    robo_font_class_source_t style_source;
    robo_font_class_avail_t style_avail;

    /** 1 if post.italicAngle available; value is Fixed 16.16 (raw preserved). */
    uint8_t has_italic_angle;
    int32_t italic_angle_fixed;
    /** Millidegrees ≈ Fixed*1000/65536; only set when has_italic_angle. */
    int32_t italic_angle_millidegrees;

    /** fsSelection WWS bit (naming model hint; not a style). */
    uint8_t wws_flag;

    /** Diagnostic: REGULAR bit set together with italic/oblique. */
    uint8_t conflict_regular_and_slope;
    /** Diagnostic: BOLD bit set but usWeightClass valid and not bold-range. */
    uint8_t conflict_bold_bit_vs_weight;
    /** Diagnostic: ITALIC and OBLIQUE both set (OBLIQUE wins for style). */
    uint8_t both_italic_and_oblique;

    /** Convenience mirror for face/instance style_desc consumers. */
    robo_font_style_desc_t style_desc;
} robo_font_classification_t;

typedef struct robo_font_classification_info robo_font_classification_info_t;

/**
 * Build classification from already-parsed TT (+ optional STAT + name meta).
 * Does not parse SFNT. Owns a value-copy classification snapshot.
 */
robo_result_t robo_font_classification_info_create(
    const robo_font_tt_t *tt,
    const robo_font_stat_t *stat_opt,
    const robo_font_metadata_t *meta_opt,
    robo_font_classification_info_t **out);

/** Classify from an SFNT (parses TT; optionally OT EXTRA for STAT). */
robo_result_t robo_font_classification_info_from_sfnt(
    robo_font_sfnt_t *sfnt,
    const robo_font_metadata_t *meta_opt,
    robo_font_classification_info_t **out);

void robo_font_classification_info_retain(robo_font_classification_info_t *info);
void robo_font_classification_info_release(robo_font_classification_info_t *info);

const robo_font_classification_t *robo_font_classification_info_get(
    const robo_font_classification_info_t *info);

/** Fill style_desc from classification (weight/stretch/style only). */
void robo_font_classification_to_style_desc(const robo_font_classification_t *cls,
                                            robo_font_style_desc_t *out);

/**
 * Pure helper: classify from raw field bag (unit tests / fuzz without fonts).
 * `raw` is copied; normalized fields filled in `out`.
 */
robo_result_t robo_font_classify(const robo_font_class_raw_t *raw,
                                 robo_font_classification_t *out);

/** Fuzz entry: arbitrary SFNT or OS/2-ish bytes. Never crashes. */
robo_result_t robo_font_classification_fuzz_parse(const uint8_t *data, size_t size);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_FONT_CLASSIFICATION_H */
