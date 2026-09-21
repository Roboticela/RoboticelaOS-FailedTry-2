/*
 * RoboFontEmoji — emoji font awareness & selection foundations (Goal 5.17).
 *
 * Identifies emoji-capable fonts, exposes color/monochrome/bitmap/vector
 * capability metadata, variation-selector queries, and sequence-request
 * foundations. Integrates with Coverage, Selector, Fallback, and the
 * generic `emoji` family (Goal 5.16).
 *
 * Does NOT implement:
 *   emoji / ZWJ shaping, presentation resolution, COLR paint execution,
 *   bitmap/SVG rasterization, or platform emoji font discovery.
 */

#ifndef ROBO_FONT_EMOJI_H
#define ROBO_FONT_EMOJI_H

#include "RoboBase/RoboError.h"
#include "RoboFont/RoboFontCoverage.h"
#include "RoboFont/RoboFontFace.h"
#include "RoboFont/RoboFontFallback.h"
#include "RoboFont/RoboFontGenericFamily.h"
#include "RoboFont/RoboFontRegistry.h"
#include "RoboFont/RoboFontSelection.h"
#include "RoboFont/RoboFontSfnt.h"
#include "RoboFont/RoboFontTypes.h"
#include "RoboUnicode/RoboUnicodeEmoji.h"
#include "RoboUnicode/RoboUnicodeProps.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define RoboFontEmojiVsText 0xFE0Eu
#define RoboFontEmojiVsEmoji 0xFE0Fu
#define RoboFontEmojiZwj 0x200Du
#define RoboFontEmojiKeycap 0x20E3u
#define RoboFontEmojiMaxSequence 64u
#define RoboFontEmojiMaxCandidates 256u

/** Text vs emoji variation selectors (U+FE0E / U+FE0F). */
#define RoboFontEmojiHasVsText 1u
#define RoboFontEmojiHasVsEmoji 2u

/* ---- Kind / capabilities ---- */

/**
 * Classification of how a font relates to emoji.
 * Name hints alone never produce COLOR / MONOCHROME / CONFIGURED.
 */
typedef enum FontEmojiKind {
    RoboFontEmojiKindNone = 0,
    /** A few emoji scalars exist; not treated as an emoji font. */
    RoboFontEmojiKindIncidental = 1,
    /** Meaningful emoji coverage without color glyph tables. */
    RoboFontEmojiKindMonochrome = 2,
    /** Color glyph tables present with emoji-related coverage/signals. */
    RoboFontEmojiKindColor = 3,
    /** Caller/config explicitly marked this family/face as emoji. */
    RoboFontEmojiKindConfigured = 4
} FontEmojiKind;

typedef enum FontEmojiCapability {
    RoboFontEmojiCapNone = 0,
    RoboFontEmojiCapCoverage = 1u << 0,
    RoboFontEmojiCapMonochrome = 1u << 1,
    RoboFontEmojiCapColor = 1u << 2,
    RoboFontEmojiCapVectorColor = 1u << 3, /* COLR/CPAL */
    RoboFontEmojiCapBitmap = 1u << 4,       /* sbix / CBDT+CBLC */
    RoboFontEmojiCapSvg = 1u << 5,
    RoboFontEmojiCapOutline = 1u << 6,      /* glyf/CFF present */
    RoboFontEmojiCapUvs = 1u << 7,
    RoboFontEmojiCapVsText = 1u << 8,      /* FE0E record exists */
    RoboFontEmojiCapVsEmoji = 1u << 9,     /* FE0F record exists */
    RoboFontEmojiCapNameHint = 1u << 10,   /* weak; never authoritative */
    RoboFontEmojiCapConfigured = 1u << 11
} FontEmojiCapability;

typedef enum FontEmojiColorPreference {
    RoboFontEmojiColorAny = 0,
    RoboFontEmojiColorPreferColor = 1,
    RoboFontEmojiColorPreferMono = 2,
    RoboFontEmojiColorRequireColor = 3,
    RoboFontEmojiColorRequireMono = 4
} FontEmojiColorPreference;

/**
 * Conservative sequence-support answer.
 * Never claims true ZWJ/shaping support in Goal 5.17.
 */
typedef enum FontEmojiSequenceSupport {
    RoboFontEmojiSeqSupportNone = 0,
    /** Every scalar has a usable cmap mapping (components only). */
    RoboFontEmojiSeqSupportComponents = 1,
    /** Components + relevant VS mapping when the sequence includes VS. */
    RoboFontEmojiSeqSupportComponentsAndVs = 2,
    /**
     * True single-glyph sequence support cannot be determined without
     * shaping — returned when RGI kind is ZWJ/FLAG/KEYCAP/etc.
     */
    RoboFontEmojiSeqSupportUnknown = 3
} FontEmojiSequenceSupport;

typedef struct FontEmojiInfo FontEmojiInfo;

typedef struct FontEmojiAnalyzeOptions {
    /**
     * Treat the face as explicitly configured emoji (e.g. listed under
     * generic family `emoji`). Always sets CAP_CONFIGURED. Structural
     * COLOR/MONOCHROME kind is preserved when capabilities warrant it;
     * otherwise kind becomes KIND_CONFIGURED.
     */
    int configuredAsEmoji;
    /** Minimum usable emoji scalars for MONOCHROME without color tables. */
    uint32_t monoEmojiScalarThreshold; /* 0 → default 8 */
    /** Minimum usable emoji scalars to count as INCIDENTAL vs NONE. */
    uint32_t incidentalEmojiScalarThreshold; /* 0 → default 1 */
} FontEmojiAnalyzeOptions;

void FontEmojiAnalyzeOptionsInit(FontEmojiAnalyzeOptions *opt);

/**
 * Build immutable emoji capability info.
 * @p sfnt may be NULL (no color-table detection).
 * @p cov may be NULL (no coverage/UVS stats).
 * @p familyName / @p fullName are optional borrowed UTF-8 (name hint only).
 */
Result FontEmojiInfoCreate(
    const FontSfnt *sfnt,
    const FontCoverage *cov,
    const char *familyName,
    const char *fullName,
    const FontEmojiAnalyzeOptions *opt,
    FontEmojiInfo **out);

/** Analyze a face (ensures coverage; parses SFNT tables via face helpers). */
Result FontEmojiInfoCreateFromFace(
    FontFace *face,
    const FontEmojiAnalyzeOptions *opt,
    FontEmojiInfo **out);

void FontEmojiInfoRetain(FontEmojiInfo *info);
void FontEmojiInfoRelease(FontEmojiInfo *info);

FontEmojiKind FontEmojiInfoKind(const FontEmojiInfo *info);
uint32_t FontEmojiInfoCapabilities(const FontEmojiInfo *info);
uint32_t FontEmojiInfoEmojiScalars(const FontEmojiInfo *info);

int FontEmojiInfoSupportsEmoji(const FontEmojiInfo *info);
int FontEmojiInfoSupportsColorEmoji(const FontEmojiInfo *info);
int FontEmojiInfoSupportsMonochromeEmoji(const FontEmojiInfo *info);
int FontEmojiInfoSupportsVariationSelectors(
    const FontEmojiInfo *info);
int FontEmojiInfoIsClassifiedEmoji(const FontEmojiInfo *info);

const char *FontEmojiKindName(FontEmojiKind kind);

/* ---- Coverage / VS queries ---- */

int FontEmojiCoversCodepoint(const FontEmojiInfo *info,
                                     uint32_t codePoint);

/**
 * Query UVS for (@p base, @p vs). Requires coverage with a cmap map.
 * Returns RoboNotSupported if UVS data unavailable.
 */
Result FontEmojiQueryUvs(const FontEmojiInfo *info,
                                        uint32_t baseCodePoint,
                                        uint32_t variationSelector,
                                        FontCmapUvsResult *out);

/**
 * Presentation probe for a base emoji scalar (does not synthesize glyphs).
 * Checks coverage and optional FE0E/FE0F UVS records.
 */
typedef struct FontEmojiPresentationProbe {
    int baseCovered;
    int hasVsTextMapping;  /* FE0E DEFAULT or NON_DEFAULT */
    int hasVsEmojiMapping; /* FE0F DEFAULT or NON_DEFAULT */
    FontCmapUvsStatus vsTextStatus;
    FontCmapUvsStatus vsEmojiStatus;
} FontEmojiPresentationProbe;

Result FontEmojiProbePresentation(const FontEmojiInfo *info,
                                                 uint32_t baseCodePoint,
                                                 FontEmojiPresentationProbe *out);

/* ---- Sequence foundation (no shaping) ---- */

typedef struct FontEmojiSequence {
    const uint32_t *codepoints; /* borrowed */
    uint32_t count;
    /** Filled by classify helpers; RoboEmojiSeqNone if unclassified. */
    UnicodeEmojiSeqKind rgiKind;
    uint8_t hasVsText;
    uint8_t hasVsEmoji;
    uint8_t hasZwj;
    uint8_t hasModifier;
    uint8_t hasRegionalIndicator;
    uint8_t hasKeycap;
} FontEmojiSequence;

void FontEmojiSequenceInit(FontEmojiSequence *seq);
Result FontEmojiSequenceSet(FontEmojiSequence *seq,
                                           const uint32_t *codepoints,
                                           uint32_t count);
/** Classify via RoboUnicode RGI data + scan VS/ZWJ/modifiers. */
Result FontEmojiSequenceAnalyze(FontEmojiSequence *seq);

FontEmojiSequenceSupport FontEmojiInfoSupportsSequence(
    const FontEmojiInfo *info,
    const FontEmojiSequence *seq);

int FontEmojiContentIsEmojiRelated(const uint32_t *codepoints,
                                             uint32_t count);

/* ---- Selection ---- */

typedef struct FontEmojiSelectRequest {
    /**
     * Sequence / code points to cover. Optional (count 0) → rank by
     * emoji capability alone.
     */
    const uint32_t *codepoints;
    uint32_t codepointCount;

    FontEmojiColorPreference colorPreference;

    /** Style request for RoboFontSelector (family field ignored). */
    FontWeight weight;
    FontStretch stretch;
    FontStyle style;

    /**
     * Optional explicit preferred families (UTF-8), ahead of generic emoji
     * mapping. Borrowed.
     */
    const char *const *preferredFamilies;
    uint32_t preferredFamilyCount;

    /** Optional color-only preferred families. */
    const char *const *colorFamilies;
    uint32_t colorFamilyCount;

    /** Optional monochrome preferred families. */
    const char *const *monoFamilies;
    uint32_t monoFamilyCount;

    /**
     * When 1, resolve RoboFontGenericEmoji via @p generics and append
     * those families (after explicit preferred lists).
     */
    int useGenericEmoji;
} FontEmojiSelectRequest;

typedef struct FontEmojiSelection {
    uint32_t cookie;
    uint8_t found;
    FontFace *face; /* retained when found */
    FontFaceId faceId;
    FontEmojiKind kind;
    uint32_t capabilities;
    uint32_t score;
    char family[128];
} FontEmojiSelection;

#define RoboFontEmojiSelectionCookie 0x454D4F31u /* 'EMO1' */

void FontEmojiSelectRequestInit(FontEmojiSelectRequest *req);
void FontEmojiSelectionInit(FontEmojiSelection *out);
void FontEmojiSelectionClear(FontEmojiSelection *out);

/**
 * Select best emoji-capable face from the registry.
 * Ranking (deterministic):
 *   configured/classified → coverage → VS → color preference →
 *   family priority → faceId.
 * Does not invent fonts when nothing matches.
 */
Result FontEmojiSelect(
    FontRegistry *registry,
    FontGenericResolver *generics, /* optional; for useGenericEmoji */
    const FontEmojiSelectRequest *req,
    FontEmojiSelection *out);

/**
 * When primary lacks emoji-related coverage, resolve via emoji select then
 * RoboFontFallback with emoji families as preferred.
 */
Result FontEmojiResolveFallback(
    FontRegistry *registry,
    FontGenericResolver *generics,
    FontFallbackResolver *fallback,
    FontFace *primary,
    const FontEmojiSelectRequest *req,
    FontEmojiSelection *emojiOut, /* optional */
    FontFallbackResult *fallbackOut);

/* ---- Face lazy cache ---- */

/** Lazy, idempotent; immutable after success. */
Result FontFaceParseEmoji(FontFace *face);
const FontEmojiInfo *FontFaceEmoji(const FontFace *face);

/**
 * Ensure SFNT is parsed on the face (needed for color-table detection).
 * Public so emoji analysis can share the face cache.
 */
Result FontFaceParseSfnt(FontFace *face);
const FontSfnt *FontFaceSfnt(const FontFace *face);

/* ---- Fuzz ---- */

Result FontEmojiFuzzParse(const uint8_t *data, size_t size);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_FONT_EMOJI_H */
