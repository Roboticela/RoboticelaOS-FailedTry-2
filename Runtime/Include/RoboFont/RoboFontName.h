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

typedef struct FontFace FontFace;

/* ---- Limits ---- */

#define RoboFontNameMaxRecords RoboFontTtMaxNameRecords
#define RoboFontNameMaxString RoboFontTtMaxNameString
#define RoboFontNameMaxTotalDecoded (512u * 1024u)
#define RoboFontNameMaxUnknownIds 256u
#define RoboFontNameMaxLangTags 256u

/* ---- Platforms / encodings ---- */

#define RoboFontNamePlatformUnicode 0u
#define RoboFontNamePlatformMacintosh 1u
#define RoboFontNamePlatformIso 2u
#define RoboFontNamePlatformWindows 3u

#define RoboFontNameWinEncSymbol 0u
#define RoboFontNameWinEncUnicodeBmp 1u
#define RoboFontNameWinEncUnicodeFull 10u

#define RoboFontNameMacEncRoman 0u

/* ---- Name IDs (OpenType) ---- */

#define RoboFontNameIdCopyright 0u
/* 1–6, 16–17 already in RoboFontTt.h */
#define RoboFontNameIdTrademark 7u
#define RoboFontNameIdManufacturer 8u
#define RoboFontNameIdDesigner 9u
#define RoboFontNameIdDescription 10u
#define RoboFontNameIdVendorUrl 11u
#define RoboFontNameIdDesignerUrl 12u
#define RoboFontNameIdLicense 13u
#define RoboFontNameIdLicenseUrl 14u
#define RoboFontNameIdCompatFull 18u
#define RoboFontNameIdSampleText 19u
#define RoboFontNameIdCidFindfont 20u
#define RoboFontNameIdWwsFamily 21u
#define RoboFontNameIdWwsSubfamily 22u
#define RoboFontNameIdLightBgPalette 23u
#define RoboFontNameIdDarkBgPalette 24u
#define RoboFontNameIdVariationsPsPrefix 25u

/* ---- Availability (authored vs derived) ---- */

typedef enum FontNameAvail {
    RoboFontNamePresent = 0,     /* font-authored decoded value */
    RoboFontNameUnavailable = 1, /* no record / not present */
    RoboFontNameFallback = 2,    /* derived by lookup policy from another ID */
    RoboFontNameUnsupported = 3, /* encoding/platform not decodable */
    RoboFontNameMalformed = 4    /* present but invalid decode/structure */
} FontNameAvail;

typedef enum FontNameLookupKind {
    RoboFontNameLookupExactLocale = 0,
    RoboFontNameLookupLanguage = 1,
    RoboFontNameLookupPlatformPref = 2,
    RoboFontNameLookupDefault = 3,
    RoboFontNameLookupNone = 4
} FontNameLookupKind;

/**
 * One decoded name record. `utf8` is owned by the parent nameInfo.
 * Raw bytes are not exposed; decoding happened at construction.
 */
typedef struct FontNameEntry {
    uint16_t platformId;
    uint16_t encodingId;
    uint16_t languageId;
    uint16_t nameId;
    FontNameAvail decodeStatus;
    uint8_t hasLocale; /* 1 if localeTag is usable BCP 47 */
    char localeTag[RoboI18nTagMax];
    const char *utf8; /* NUL-terminated; empty if not PRESENT */
    size_t utf8Len;
} FontNameEntry;

typedef struct FontMetaField {
    const char *utf8;
    size_t utf8Len;
    FontNameAvail avail;
    uint16_t sourceNameId; /* 0xFFFF if unavailable */
} FontMetaField;

typedef struct FontVersionParsed {
    uint8_t valid;
    uint16_t major;
    uint16_t minor;
    uint16_t revision;
} FontVersionParsed;

/**
 * Normalized metadata snapshot. Strings point into nameInfo storage.
 * Immutable after create. Concurrent readers OK under face lifetime.
 */
typedef struct FontMetadata {
    FontMetaField familyName;              /* name ID 1 */
    FontMetaField subfamilyName;           /* name ID 2 */
    FontMetaField fullName;                /* name ID 4 */
    FontMetaField postscriptName;          /* name ID 6 */
    FontMetaField version;                  /* name ID 5 */
    FontMetaField typographicFamilyName;  /* 16 */
    FontMetaField typographicSubfamilyName; /* 17 */
    FontMetaField wwsFamilyName;          /* 21 */
    FontMetaField wwsSubfamilyName;       /* 22 */
    FontMetaField manufacturer;             /* 8 */
    FontMetaField designer;                 /* 9 */
    FontMetaField description;              /* 10 */
    FontMetaField copyright;                /* 0 */
    FontMetaField trademark;                /* 7 */
    FontMetaField licenseDescription;      /* 13 */
    FontMetaField vendorUrl;               /* 11 */
    FontMetaField designerUrl;             /* 12 */
    FontMetaField licenseUrl;              /* 14 */
    FontVersionParsed versionParsed;
    uint8_t postscriptNameStructurallyOk; /* 1 if PS name passes basic checks */
} FontMetadata;

typedef struct FontNameInfo FontNameInfo;

/**
 * Create name info + metadata from an SFNT (uses existing TT name parse when
 * a TT bundle is available, otherwise reads the `name` table directly).
 */
Result FontNameInfoCreate(FontSfnt *sfnt,
                                         FontTt *ttOpt,
                                         const I18nLocale *preferredOpt,
                                         FontNameInfo **out);

/** Parse arbitrary `name` table bytes (fuzz / standalone). */
Result FontNameInfoFromSpan(FontSpan nameSpan,
                                            const I18nLocale *preferredOpt,
                                            FontNameInfo **out);

void FontNameInfoRetain(FontNameInfo *info);
void FontNameInfoRelease(FontNameInfo *info);

uint32_t FontNameInfoEntryCount(const FontNameInfo *info);
const FontNameEntry *FontNameInfoEntry(const FontNameInfo *info,
                                                        uint32_t index);

/**
 * Localized lookup for a name ID.
 *
 * Fallback order (documented):
 *   1. exact locale match (when preferred set and entry has locale)
 *   2. language-only match
 *   3. platform-preferred (Windows Unicode en-US, then any Win Unicode,
 *      Unicode platform, Mac Roman)
 *   4. first remaining PRESENT entry for nameId (stable lowest index)
 *
 * Duplicate identical keys: lowest table index wins among equal score.
 */
Result FontNameInfoLookup(const FontNameInfo *info,
                                         uint16_t nameId,
                                         const I18nLocale *preferredOpt,
                                         const FontNameEntry **outEntry,
                                         FontNameLookupKind *outKind);

const FontMetadata *FontNameInfoMetadata(const FontNameInfo *info);

/**
 * Preferred family for later selection (5.10/5.11):
 * typographic (16) → WWS (21) → legacy (1).
 * avail is PRESENT if ID 16 used, else FALLBACK when using 21/1.
 */
Result FontMetadataPreferredFamily(const FontMetadata *meta,
                                                  FontMetaField *out);

Result FontMetadataPreferredSubfamily(const FontMetadata *meta,
                                                     FontMetaField *out);

/** Fuzz entry: arbitrary SFNT or raw name-table-ish bytes. Never crashes. */
Result FontNameFuzzParse(const uint8_t *data, size_t size);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_FONT_NAME_H */
