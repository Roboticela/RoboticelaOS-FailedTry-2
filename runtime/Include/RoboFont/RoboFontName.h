/*
 * RoboFont naming & metadata (Goal 5.9).
 *
 * Builds on the structural SFNT `name` table parse (Goal 5.3) to provide
 * Unicode decoding, locale association, deterministic localized lookup, and
 * a normalized RoboFontMetadata snapshot.
 *
 *   SFNT `name` → records → decode → RoboFontNameInfo → RoboFontMetadata
 *
 * Metadata is inert Unicode text. No URL fetch, HTML, or script execution.
 */

#ifndef ROBO_FONT_NAME_H
#define ROBO_FONT_NAME_H

#include "RoboBase/RoboError.h"
#include "RoboFont/RoboFontSfnt.h"
#include "RoboFont/RoboFontTt.h"
#include "RoboI18N/RoboI18NLocale.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct robo_font_face robo_font_face_t;

/* ---- Limits ---- */

#define ROBO_FONT_NAME_MAX_RECORDS ROBO_FONT_TT_MAX_NAME_RECORDS
#define ROBO_FONT_NAME_MAX_STRING ROBO_FONT_TT_MAX_NAME_STRING
#define ROBO_FONT_NAME_MAX_TOTAL_DECODED (512u * 1024u)
#define ROBO_FONT_NAME_MAX_UNKNOWN_IDS 256u
#define ROBO_FONT_NAME_MAX_LANG_TAGS 256u

/* ---- Platforms / encodings ---- */

#define ROBO_FONT_NAME_PLATFORM_UNICODE 0u
#define ROBO_FONT_NAME_PLATFORM_MACINTOSH 1u
#define ROBO_FONT_NAME_PLATFORM_ISO 2u
#define ROBO_FONT_NAME_PLATFORM_WINDOWS 3u

#define ROBO_FONT_NAME_WIN_ENC_SYMBOL 0u
#define ROBO_FONT_NAME_WIN_ENC_UNICODE_BMP 1u
#define ROBO_FONT_NAME_WIN_ENC_UNICODE_FULL 10u

#define ROBO_FONT_NAME_MAC_ENC_ROMAN 0u

/* ---- Name IDs (OpenType) ---- */

#define ROBO_FONT_NAME_ID_COPYRIGHT 0u
/* 1–6, 16–17 already in RoboFontTt.h */
#define ROBO_FONT_NAME_ID_TRADEMARK 7u
#define ROBO_FONT_NAME_ID_MANUFACTURER 8u
#define ROBO_FONT_NAME_ID_DESIGNER 9u
#define ROBO_FONT_NAME_ID_DESCRIPTION 10u
#define ROBO_FONT_NAME_ID_VENDOR_URL 11u
#define ROBO_FONT_NAME_ID_DESIGNER_URL 12u
#define ROBO_FONT_NAME_ID_LICENSE 13u
#define ROBO_FONT_NAME_ID_LICENSE_URL 14u
#define ROBO_FONT_NAME_ID_COMPAT_FULL 18u
#define ROBO_FONT_NAME_ID_SAMPLE_TEXT 19u
#define ROBO_FONT_NAME_ID_CID_FINDFONT 20u
#define ROBO_FONT_NAME_ID_WWS_FAMILY 21u
#define ROBO_FONT_NAME_ID_WWS_SUBFAMILY 22u
#define ROBO_FONT_NAME_ID_LIGHT_BG_PALETTE 23u
#define ROBO_FONT_NAME_ID_DARK_BG_PALETTE 24u
#define ROBO_FONT_NAME_ID_VARIATIONS_PS_PREFIX 25u

/* ---- Availability (authored vs derived) ---- */

typedef enum robo_font_name_avail {
    ROBO_FONT_NAME_PRESENT = 0,     /* font-authored decoded value */
    ROBO_FONT_NAME_UNAVAILABLE = 1, /* no record / not present */
    ROBO_FONT_NAME_FALLBACK = 2,    /* derived by lookup policy from another ID */
    ROBO_FONT_NAME_UNSUPPORTED = 3, /* encoding/platform not decodable */
    ROBO_FONT_NAME_MALFORMED = 4    /* present but invalid decode/structure */
} robo_font_name_avail_t;

typedef enum robo_font_name_lookup_kind {
    ROBO_FONT_NAME_LOOKUP_EXACT_LOCALE = 0,
    ROBO_FONT_NAME_LOOKUP_LANGUAGE = 1,
    ROBO_FONT_NAME_LOOKUP_PLATFORM_PREF = 2,
    ROBO_FONT_NAME_LOOKUP_DEFAULT = 3,
    ROBO_FONT_NAME_LOOKUP_NONE = 4
} robo_font_name_lookup_kind_t;

/**
 * One decoded name record. `utf8` is owned by the parent name_info.
 * Raw bytes are not exposed; decoding happened at construction.
 */
typedef struct robo_font_name_entry {
    uint16_t platform_id;
    uint16_t encoding_id;
    uint16_t language_id;
    uint16_t name_id;
    robo_font_name_avail_t decode_status;
    uint8_t has_locale; /* 1 if locale_tag is usable BCP 47 */
    char locale_tag[ROBO_I18N_TAG_MAX];
    const char *utf8; /* NUL-terminated; empty if not PRESENT */
    size_t utf8_len;
} robo_font_name_entry_t;

typedef struct robo_font_meta_field {
    const char *utf8;
    size_t utf8_len;
    robo_font_name_avail_t avail;
    uint16_t source_name_id; /* 0xFFFF if unavailable */
} robo_font_meta_field_t;

typedef struct robo_font_version_parsed {
    uint8_t valid;
    uint16_t major;
    uint16_t minor;
    uint16_t revision;
} robo_font_version_parsed_t;

/**
 * Normalized metadata snapshot. Strings point into name_info storage.
 * Immutable after create. Concurrent readers OK under face lifetime.
 */
typedef struct robo_font_metadata {
    robo_font_meta_field_t family_name;              /* name ID 1 */
    robo_font_meta_field_t subfamily_name;           /* name ID 2 */
    robo_font_meta_field_t full_name;                /* name ID 4 */
    robo_font_meta_field_t postscript_name;          /* name ID 6 */
    robo_font_meta_field_t version;                  /* name ID 5 */
    robo_font_meta_field_t typographic_family_name;  /* 16 */
    robo_font_meta_field_t typographic_subfamily_name; /* 17 */
    robo_font_meta_field_t wws_family_name;          /* 21 */
    robo_font_meta_field_t wws_subfamily_name;       /* 22 */
    robo_font_meta_field_t manufacturer;             /* 8 */
    robo_font_meta_field_t designer;                 /* 9 */
    robo_font_meta_field_t description;              /* 10 */
    robo_font_meta_field_t copyright;                /* 0 */
    robo_font_meta_field_t trademark;                /* 7 */
    robo_font_meta_field_t license_description;      /* 13 */
    robo_font_meta_field_t vendor_url;               /* 11 */
    robo_font_meta_field_t designer_url;             /* 12 */
    robo_font_meta_field_t license_url;              /* 14 */
    robo_font_version_parsed_t version_parsed;
    uint8_t postscript_name_structurally_ok; /* 1 if PS name passes basic checks */
} robo_font_metadata_t;

typedef struct robo_font_name_info robo_font_name_info_t;

/**
 * Create name info + metadata from an SFNT (uses existing TT name parse when
 * a TT bundle is available, otherwise reads the `name` table directly).
 */
robo_result_t robo_font_name_info_create(robo_font_sfnt_t *sfnt,
                                         robo_font_tt_t *tt_opt,
                                         const robo_i18n_locale_t *preferred_opt,
                                         robo_font_name_info_t **out);

/** Parse arbitrary `name` table bytes (fuzz / standalone). */
robo_result_t robo_font_name_info_from_span(robo_font_span_t name_span,
                                            const robo_i18n_locale_t *preferred_opt,
                                            robo_font_name_info_t **out);

void robo_font_name_info_retain(robo_font_name_info_t *info);
void robo_font_name_info_release(robo_font_name_info_t *info);

uint32_t robo_font_name_info_entry_count(const robo_font_name_info_t *info);
const robo_font_name_entry_t *robo_font_name_info_entry(const robo_font_name_info_t *info,
                                                        uint32_t index);

/**
 * Localized lookup for a name ID.
 *
 * Fallback order (documented):
 *   1. exact locale match (when preferred set and entry has locale)
 *   2. language-only match
 *   3. platform-preferred (Windows Unicode en-US, then any Win Unicode,
 *      Unicode platform, Mac Roman)
 *   4. first remaining PRESENT entry for name_id (stable lowest index)
 *
 * Duplicate identical keys: lowest table index wins among equal score.
 */
robo_result_t robo_font_name_info_lookup(const robo_font_name_info_t *info,
                                         uint16_t name_id,
                                         const robo_i18n_locale_t *preferred_opt,
                                         const robo_font_name_entry_t **out_entry,
                                         robo_font_name_lookup_kind_t *out_kind);

const robo_font_metadata_t *robo_font_name_info_metadata(const robo_font_name_info_t *info);

/**
 * Preferred family for later selection (5.10/5.11):
 * typographic (16) → WWS (21) → legacy (1).
 * avail is PRESENT if ID 16 used, else FALLBACK when using 21/1.
 */
robo_result_t robo_font_metadata_preferred_family(const robo_font_metadata_t *meta,
                                                  robo_font_meta_field_t *out);

robo_result_t robo_font_metadata_preferred_subfamily(const robo_font_metadata_t *meta,
                                                     robo_font_meta_field_t *out);

/** Fuzz entry: arbitrary SFNT or raw name-table-ish bytes. Never crashes. */
robo_result_t robo_font_name_fuzz_parse(const uint8_t *data, size_t size);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_FONT_NAME_H */
