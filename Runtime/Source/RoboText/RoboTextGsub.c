/*
 * RoboText's glyph substitution engine (Phase 12.10) — see `RoboTextGsub.h`.
 *
 * The file is four pieces, in the order a shaping call meets them:
 *
 *   **the buffer**   a growable array of @ref RoboTextShapingGlyph, which is what a lookup edits. It is
 *                    separate from the final glyph run for one reason: substitution changes the *number*
 *                    of glyphs, and an array that is being spliced cannot be the array a caller reads. Its
 *                    invariants are enforced by the mutators (`Append` refuses a glyph with no source,
 *                    `Replace` refuses an empty replacement for a non-empty span) rather than believed.
 *
 *   **the plan**     `RoboTextGsubPlanResolve` walks the font's ScriptList → LangSys → FeatureList and
 *                    answers, in the font's own terms, which lookups this script, language and feature set
 *                    turn on. Two properties are load-bearing. The plan never branches on a script: a
 *                    script is a *tag* (`RoboTextGsubScriptTag`) and the font decides the rest, so Arabic
 *                    joining and Indic reordering arrive through the same code path Latin ligatures do.
 *                    And the execution order is the **Lookup List's** order, not the features': lookups are
 *                    numbered so that a later one sees an earlier one's work, and applying them in feature
 *                    order would reverse that for every font that lists its features out of order.
 *
 *   **the apply loop** walks the plan's lookup order once, resolving each lookup's extension wrapper and
 *                    trying its subtables at each buffer position. A position that matched continues
 *                    *after* what the lookup produced — never re-matching its own output with the same
 *                    lookup, which is the recursion the phase warns about and which no OpenType lookup asks
 *                    for.
 *
 *   **the reporting** counters, an optional trace of substitution events, and four writers. All of them are
 *                    complete-or-absent, like every other dump in this module: a truncated table reads as a
 *                    different table, and a diagnostic that lies is worse than one that refuses.
 *
 * Nothing here allocates per glyph, per position or per probe. A call allocates only when the buffer grows
 * (doubling, bounded by `RoboTextGsubLimits::maxGlyphs`) and, inside the subtable readers, once per
 * coverage or class definition it uses — see `RoboTextGsubInternal.h` for why that is the right place for
 * that cost.
 */

#include "RoboText/RoboTextGsub.h"

#include "RoboTextGsubInternal.h"
#include "RoboMem/RoboMem.h"
#include "RoboFont/RoboFontOtCommon.h"
#include "RoboText/RoboTextBidi.h"
#include "RoboText/RoboTextUnicode.h"

#include <string.h>

/* ------------------------------------------------------------------ */
/* Tags                                                                */
/* ------------------------------------------------------------------ */

uint32_t RoboTextGsubTagOf(const char *tag)
{
    if (!tag || strlen(tag) != 4u) {
        return 0u;
    }
    return ((uint32_t)(uint8_t)tag[0] << 24) | ((uint32_t)(uint8_t)tag[1] << 16) |
           ((uint32_t)(uint8_t)tag[2] << 8) | (uint32_t)(uint8_t)tag[3];
}

void RoboTextGsubTagWrite(uint32_t tag, char *buffer)
{
    if (!buffer) {
        return;
    }
    buffer[0] = (char)((tag >> 24) & 0xFFu);
    buffer[1] = (char)((tag >> 16) & 0xFFu);
    buffer[2] = (char)((tag >> 8) & 0xFFu);
    buffer[3] = (char)(tag & 0xFFu);
    buffer[4] = '\0';
}

/* ------------------------------------------------------------------ */
/* Limits                                                              */
/* ------------------------------------------------------------------ */

/*
 * The numbers are a policy, and they are chosen against what a real font does rather than against what a
 * hostile one might: a hundred-lookup font applies each lookup once per run, a long word is a few dozen
 * glyphs, and nesting is an order of magnitude below the OpenType maximum of 64 (which a font can only reach
 * with `LRE`-style controls, which are not this engine's business). Every limit is reported in
 * `RoboTextGsubStats` when it fires, so a caller can tell "the font ended" from "we stopped".
 */
const RoboTextGsubLimits RoboTextGsubDefaultLimits = {
    4096u,     /* maxLookupApplications */
    1048576u,  /* maxGlyphProbes */
    65536u,    /* maxGlyphs */
    6u         /* maxNesting */
};

/* ------------------------------------------------------------------ */
/* One shaping glyph                                                   */
/* ------------------------------------------------------------------ */

void RoboTextShapingGlyphInit(RoboTextShapingGlyph *glyph)
{
    if (!glyph) {
        return;
    }
    memset(glyph, 0, sizeof(*glyph));
    glyph->glyphId = RoboGlyphIdNone;
    glyph->bidiLevel = RoboTextBidiLevelNone;
    glyph->source = RoboTextRangeMake(0u, 0u);
}

int RoboTextShapingGlyphIsValid(const RoboTextShapingGlyph *glyph)
{
    if (!glyph) {
        return 0;
    }
    if (RoboTextRangeIsInverted(glyph->source) || RoboTextRangeIsEmpty(glyph->source)) {
        /* A glyph exists because something produced it, and a glyph with no source cannot answer "where did
         * this come from" — which is the one question the record exists to answer. */
        return 0;
    }
    return 1;
}

int RoboTextShapingGlyphEqual(const RoboTextShapingGlyph *a, const RoboTextShapingGlyph *b)
{
    if (a == b) {
        return 1;
    }
    if (!a || !b) {
        return 0;
    }
    return a->glyphId == b->glyphId && a->cluster == b->cluster && a->bidiLevel == b->bidiLevel &&
           a->flags == b->flags && a->inputGlyphId == b->inputGlyphId &&
           a->ligatureComponents == b->ligatureComponents &&
           RoboTextRangeEqual(&a->source, &b->source);
}

/* ------------------------------------------------------------------ */
/* The buffer                                                          */
/* ------------------------------------------------------------------ */

struct RoboTextGsubBuffer {
    RoboTextShapingGlyph *glyphs;
    size_t count;
    size_t capacity;
};

/** Room for @p wanted glyphs, doubling the capacity. Non-zero on success. */
static int GsubBufferReserve(RoboTextGsubBuffer *buffer, size_t wanted)
{
    size_t capacity;
    RoboTextShapingGlyph *grown;

    if (wanted <= buffer->capacity) {
        return 1;
    }
    capacity = (buffer->capacity != 0u) ? buffer->capacity : 8u;
    while (capacity < wanted) {
        if (capacity > ((size_t)-1) / 2u) {
            return 0;
        }
        capacity *= 2u;
    }
    grown = (RoboTextShapingGlyph *)Realloc(buffer->glyphs, capacity * sizeof(*grown));
    if (!grown) {
        return 0;
    }
    buffer->glyphs = grown;
    buffer->capacity = capacity;
    return 1;
}

RoboTextGsubBuffer *RoboTextGsubBufferCreate(size_t capacity)
{
    RoboTextGsubBuffer *buffer = (RoboTextGsubBuffer *)RoboCalloc(1u, sizeof(*buffer));

    if (!buffer) {
        return NULL;
    }
    if (capacity == 0u) {
        capacity = 8u;
    }
    buffer->glyphs = (RoboTextShapingGlyph *)RoboCalloc(capacity, sizeof(*buffer->glyphs));
    if (!buffer->glyphs) {
        RoboFree(buffer);
        return NULL;
    }
    buffer->capacity = capacity;
    return buffer;
}

void RoboTextGsubBufferDestroy(RoboTextGsubBuffer *buffer)
{
    if (!buffer) {
        return;
    }
    RoboFree(buffer->glyphs);
    RoboFree(buffer);
}

size_t RoboTextGsubBufferCount(const RoboTextGsubBuffer *buffer)
{
    return buffer ? buffer->count : 0u;
}

const RoboTextShapingGlyph *RoboTextGsubBufferAt(const RoboTextGsubBuffer *buffer, size_t index)
{
    return (buffer && index < buffer->count) ? &buffer->glyphs[index] : NULL;
}

RoboTextShapingGlyph *RoboTextGsubBufferAtMutable(RoboTextGsubBuffer *buffer, size_t index)
{
    return (buffer && index < buffer->count) ? &buffer->glyphs[index] : NULL;
}

Result RoboTextGsubBufferAppend(RoboTextGsubBuffer *buffer, const RoboTextShapingGlyph *glyph)
{
    if (!buffer || !glyph) {
        return RoboInvalidArgument;
    }
    if (!RoboTextShapingGlyphIsValid(glyph)) {
        return RoboInvalidArgument;
    }
    if (!GsubBufferReserve(buffer, buffer->count + 1u)) {
        return RoboOutOfMemory;
    }
    buffer->glyphs[buffer->count] = *glyph;
    buffer->count++;
    return RoboOk;
}

Result RoboTextGsubBufferSetGlyph(RoboTextGsubBuffer *buffer, size_t index, GlyphId glyphId)
{
    if (!buffer || index >= buffer->count) {
        return RoboInvalidArgument;
    }
    buffer->glyphs[index].glyphId = glyphId;
    return RoboOk;
}

Result RoboTextGsubBufferReplace(RoboTextGsubBuffer *buffer,
                                 size_t index,
                                 size_t count,
                                 const RoboTextShapingGlyph *replacement,
                                 size_t replacementCount)
{
    size_t after;

    if (!buffer || index > buffer->count) {
        return RoboInvalidArgument;
    }
    if (count > buffer->count - index) {
        return RoboInvalidArgument;
    }
    if (replacementCount != 0u && !replacement) {
        return RoboInvalidArgument;
    }
    /* A deletion is expressed as an empty replacement and not as "trust me, it is all empty records": a
     * caller that passed a count of glyphs it did not own would otherwise splice uninitialised memory into
     * the buffer. */
    for (size_t probe = 0u; probe < replacementCount; probe++) {
        if (!RoboTextShapingGlyphIsValid(&replacement[probe])) {
            return RoboInvalidArgument;
        }
    }
    after = buffer->count - index - count;
    if (replacementCount > count) {
        if (!GsubBufferReserve(buffer, buffer->count + (replacementCount - count))) {
            return RoboOutOfMemory;
        }
    }
    if (replacementCount != count && after != 0u) {
        memmove(&buffer->glyphs[index + replacementCount], &buffer->glyphs[index + count],
                after * sizeof(*buffer->glyphs));
    }
    for (size_t probe = 0u; probe < replacementCount; probe++) {
        buffer->glyphs[index + probe] = replacement[probe];
    }
    buffer->count = buffer->count - count + replacementCount;
    return RoboOk;
}

/* ------------------------------------------------------------------ */
/* The feature plan                                                    */
/* ------------------------------------------------------------------ */

void RoboTextGsubPlanInit(RoboTextGsubPlan *plan)
{
    if (!plan) {
        return;
    }
    memset(plan, 0, sizeof(*plan));
    plan->empty = 1;
}

int RoboTextGsubPlanIsValid(const RoboTextGsubPlan *plan)
{
    if (!plan) {
        return 0;
    }
    if (plan->featureCount > (size_t)RoboTextGsubMaxPlanFeatures) {
        return 0;
    }
    if (plan->lookupCount > (size_t)RoboFontOtMaxLookups) {
        return 0;
    }
    return 1;
}

/**
 * The four-byte OpenType script tag for @p script, or 0 when the script asks for the `DFLT` record.
 *
 * Derived from RoboUnicode rather than listed here, and that is the point: the OpenType script registry
 * *is* the ISO 15924 code in lower case (`UcdScriptShortName` is Phase 4's own answer for that code, so
 * this module reads the same table every other script decision in the tree reads), and a hand-written
 * switch over 171 scripts would be a second copy of a registry that can drift. The exceptions to the
 * lower-case rule are the whole remainder of this function, and each is the registry's:
 *
 *   `Hira` -> `kana`  Hiragana and Katakana share one OpenType record.
 *   `Laoo` -> `lao `  The four codes whose ISO form is longer than the tag pads with a space.
 *   `Nkoo` -> `nko `
 *   `Yiii` -> `yi  `
 *   `Vaii` -> `vai `
 *   `Zyyy`, `Zinh`, `Zzzz` -> 0  Common, Inherited and unassigned text has no script behaviour of its
 *                                own to look up, and 0 is how a caller asks for the font's `DFLT` record.
 *
 * **No shaping decision anywhere in this module branches on a script.** This function produces a tag and
 * the font decides everything else, which is what makes Arabic joining and Indic reordering arrive through
 * the same code path Latin ligatures do.
 */
uint32_t RoboTextGsubScriptTag(RoboTextScript script)
{
    const char *iso = UcdScriptShortName(script);
    char tag[4];

    if (!iso || strlen(iso) != 4u) {
        return 0u;
    }
    if (strcmp(iso, "Zyyy") == 0 || strcmp(iso, "Zinh") == 0 || strcmp(iso, "Zzzz") == 0) {
        return 0u;
    }
    if (strcmp(iso, "Hira") == 0) {
        return RoboFontTag('k', 'a', 'n', 'a');
    }
    if (strcmp(iso, "Laoo") == 0) {
        return RoboFontTag('l', 'a', 'o', ' ');
    }
    if (strcmp(iso, "Nkoo") == 0) {
        return RoboFontTag('n', 'k', 'o', ' ');
    }
    if (strcmp(iso, "Yiii") == 0) {
        return RoboFontTag('y', 'i', ' ', ' ');
    }
    if (strcmp(iso, "Vaii") == 0) {
        return RoboFontTag('v', 'a', 'i', ' ');
    }
    for (size_t index = 0u; index < 4u; index++) {
        char c = iso[index];

        tag[index] = (c >= 'A' && c <= 'Z') ? (char)(c - 'A' + 'a') : c;
    }
    return RoboFontTag(tag[0], tag[1], tag[2], tag[3]);
}

/** The complex-script feature set a script's text needs beyond the generic defaults. */
static const char *const *GsubComplexFeatures(RoboTextScript script, size_t *outCount)
{
    /* Joining forms: the features a cursive script's shapes come from. */
    static const char *const kJoining[] = {"isol", "fina", "medi", "init", "rlig", "calt", "mset",
                                           "cswh"};
    /* Indic reordering and presentation, in the order the OpenType Indic specification applies them. */
    static const char *const kIndic[] = {"nukt", "akhn", "rphf", "rkrf", "pref", "blwf", "half",
                                         "abvf", "pstf", "vatu", "cjct", "cfar", "init", "pres",
                                         "abvs", "blws", "psts", "haln", "abvm", "blwm", "dist"};
    /* Khmer and Myanmar share the Indic machinery with their own feature names. */
    static const char *const kKhmer[] = {"pref", "blwf", "abvf", "pstf", "pres", "abvs", "blws",
                                         "psts", "clig", "dist"};
    static const char *const kClusters[] = {"ccmp", "locl", "rlig", "calt", "clig", "liga", "pres",
                                            "abvs", "blws", "psts", "dist"};

    *outCount = 0u;
    switch (script) {
        case RoboUScriptArabic:
        case RoboUScriptSyriac:
        case RoboUScriptMongolian:
        case RoboUScriptThaana:
        case RoboUScriptAdlam:
        case RoboUScriptNko:
        case RoboUScriptMandaic:
            *outCount = sizeof(kJoining) / sizeof(kJoining[0]);
            return kJoining;
        case RoboUScriptDevanagari:
        case RoboUScriptBengali:
        case RoboUScriptGurmukhi:
        case RoboUScriptGujarati:
        case RoboUScriptOriya:
        case RoboUScriptTamil:
        case RoboUScriptTelugu:
        case RoboUScriptKannada:
        case RoboUScriptMalayalam:
        case RoboUScriptSinhala:
            *outCount = sizeof(kIndic) / sizeof(kIndic[0]);
            return kIndic;
        case RoboUScriptKhmer:
        case RoboUScriptMyanmar:
            *outCount = sizeof(kKhmer) / sizeof(kKhmer[0]);
            return kKhmer;
        case RoboUScriptThai:
        case RoboUScriptLao:
        case RoboUScriptTibetan:
            *outCount = sizeof(kClusters) / sizeof(kClusters[0]);
            return kClusters;
        default:
            *outCount = 0u;
            return NULL;
    }
}

void RoboTextGsubDefaultFeatureSet(RoboTextScript script, RoboTextFeatureSet *out)
{
    /* The generic set: what every shaper applies unless told otherwise. */
    static const char *const kGeneric[] = {"ccmp", "locl", "rlig", "liga", "clig", "calt", "rclt"};
    const char *const *complex = NULL;
    size_t complexCount = 0u;

    if (!out) {
        return;
    }
    RoboTextFeatureSetInit(out);
    for (size_t index = 0u; index < sizeof(kGeneric) / sizeof(kGeneric[0]); index++) {
        (void)RoboTextFeatureSetSet(out, kGeneric[index], RoboTextFeatureOn, 0u);
    }
    complex = GsubComplexFeatures(script, &complexCount);
    for (size_t index = 0u; index < complexCount; index++) {
        (void)RoboTextFeatureSetSet(out, complex[index], RoboTextFeatureOn, 0u);
    }
}

/** The state the caller asked of @p tag, or `RoboTextFeatureDefault` when the caller named none. */
static RoboTextFeatureState GsubCallerState(const RoboTextFeatureSet *features,
                                            const char *tag,
                                            uint32_t *outValue)
{
    const RoboTextFeatureSetting *setting = RoboTextFeatureSetFind(features, tag);

    if (outValue) {
        *outValue = 0u;
    }
    if (!setting) {
        return RoboTextFeatureDefault;
    }
    if (outValue) {
        *outValue = setting->value;
    }
    return setting->state;
}

/**
 * Non-zero when @p record (a layout tag, four bytes, space-padded) names @p wanted.
 *
 * ASCII case-insensitive and prefix-tolerant, because the two spellings are both real: a layout table
 * spells a language the way a font foundry did (`UR  `, `ENG `) while a caller states one the way the web
 * does (`ur`, `en-US`). No `strcasecmp` here — this module is compiled into the freestanding guest, and the
 * comparison is four bytes of ASCII rather than a locale question.
 */
static int GsubTagMatches(const char *record, const char *wanted)
{
    size_t length = strlen(wanted);

    if (length == 0u || length > 4u) {
        return 0;
    }
    for (size_t index = 0u; index < length; index++) {
        char left = record[index];
        char right = wanted[index];

        if (left >= 'A' && left <= 'Z') {
            left = (char)(left - 'A' + 'a');
        }
        if (right >= 'A' && right <= 'Z') {
            right = (char)(right - 'A' + 'a');
        }
        if (left != right) {
            return 0;
        }
    }
    /* A prefix match is a match: `en` against `ENG ` is the same language stated twice. */
    return 1;
}

/** The LangSys record for @p languageTag inside @p script, or NULL. */
static const FontOtLangSys *GsubLanguageSystem(const FontOtScript *script,
                                               const RoboTextLanguage *language,
                                               uint32_t *outTag)
{
    char tag[8];
    const char *text;

    if (outTag) {
        *outTag = 0u;
    }
    if (!script || !language || !RoboTextLanguageIsSet(language)) {
        return NULL;
    }
    text = RoboTextLanguageTag(language);
    if (strlen(text) == 0u || strlen(text) > 4u) {
        return NULL;
    }
    for (size_t index = 0u; index < script->langSysCount; index++) {
        memset(tag, 0, sizeof(tag));
        RoboTextGsubTagWrite(script->langSysRecords[index].langTag, tag);
        /* A layout table's language tag is four bytes, right-padded with spaces and conventionally upper
         * case ("UR  "), while a caller states a language the way the web does ("ur"). Both spellings are
         * tried, in both cases, because a font may use either — and a tag that matches neither leaves the
         * script's default LangSys to answer, which is deterministic rather than a guess. */
        if (GsubTagMatches(tag, text)) {
            if (outTag) {
                *outTag = script->langSysRecords[index].langTag;
            }
            return &script->langSysRecords[index].langSys;
        }
    }
    return NULL;
}

/** Where a lookup's index is first named by an enabled feature, for the trace's `feature` bytes. */
static const char *GsubFeatureForLookup(const RoboTextGsubPlan *plan, uint16_t lookupIndex)
{
    for (size_t feature = 0u; feature < plan->featureCount; feature++) {
        const RoboTextGsubFeature *entry = &plan->features[feature];

        for (uint16_t lookup = 0u; lookup < entry->lookupIndexCount; lookup++) {
            if (entry->lookupIndices[lookup] == lookupIndex) {
                return entry->tag;
            }
        }
    }
    return "    ";
}

Result RoboTextGsubPlanResolve(const FontOtLayout *gsub,
                               RoboTextScript script,
                               const RoboTextLanguage *language,
                               const RoboTextFeatureSet *features,
                               RoboTextGsubPlan *out,
                               RoboTextDiag *diag)
{
    uint32_t scriptTag;
    const FontOtScript *record = NULL;
    const FontOtLangSys *langSys = NULL;
    uint32_t languageTag = 0u;

    if (!out) {
        return RoboTextDiagMsg(diag, RoboTextDomainGsub, RoboInvalidArgument,
                               "an out plan is required");
    }
    RoboTextGsubPlanInit(out);
    if (!gsub) {
        /* No GSUB is not an error: the caller shapes with the glyphs the cmap gave it. */
        return RoboOk;
    }
    if (gsub->kind != RoboFontOtLayoutGsub) {
        return RoboTextDiagMsg(diag, RoboTextDomainGsub, RoboInvalidArgument,
                               "the layout handed to the substitution engine is not GSUB");
    }

    scriptTag = RoboTextGsubScriptTag(script);
    if (scriptTag != 0u) {
        for (size_t index = 0u; index < gsub->scripts.scriptCount; index++) {
            if (gsub->scripts.scripts[index].scriptTag == scriptTag) {
                record = &gsub->scripts.scripts[index];
                break;
            }
        }
    }
    if (!record) {
        /* `DFLT`: the record a font puts behaviour common to every script under, and the answer for a script
         * the font has no record for (or for Common/Inherited text, which has no tag of its own). */
        for (size_t index = 0u; index < gsub->scripts.scriptCount; index++) {
            if (gsub->scripts.scripts[index].scriptTag == RoboFontTag('D', 'F', 'L', 'T')) {
                record = &gsub->scripts.scripts[index];
                out->usedDefaultScript = 1;
                break;
            }
        }
    }
    if (!record) {
        /* A font with no record for this script runs no substitution — for this script. */
        return RoboOk;
    }
    out->scriptTag = record->scriptTag;

    langSys = GsubLanguageSystem(record, language, &languageTag);
    if (langSys) {
        out->languageTag = languageTag;
    } else {
        if (!record->hasDefaultLangSys) {
            /* A script record with neither the language nor a default: nothing this text may run. */
            return RoboOk;
        }
        langSys = &record->defaultLangSys;
        out->usedDefaultLangSys = 1;
    }
    out->empty = 0;

    /*
     * The features this LangSys lists, filtered by the caller's settings against the script's defaults. The
     * font's own order is kept for the *feature* list (a diagnostic shows it); the lookups are sorted below,
     * because that is what execution order has to follow.
     */
    for (size_t index = 0u; index < langSys->featureIndexCount; index++) {
        uint16_t featureIndex = langSys->featureIndices[index];
        const FontOtFeature *feature;
        RoboTextFeatureState state;
        uint32_t value = 0u;
        char tag[RoboTextFeatureTagBytes];

        if (featureIndex >= gsub->features.featureCount) {
            continue;
        }
        feature = &gsub->features.features[featureIndex];
        RoboTextGsubTagWrite(feature->featureTag, tag);
        state = GsubCallerState(features, tag, &value);
        if (state == RoboTextFeatureDefault) {
            /* The caller said nothing, so the script's defaults decide — which is what makes `liga` ligate
             * without anyone asking and `dlig` stay off without anyone saying so. */
            RoboTextFeatureSet defaults;

            RoboTextGsubDefaultFeatureSet(script, &defaults);
            state = RoboTextFeatureSetFind(&defaults, tag) ? RoboTextFeatureOn : RoboTextFeatureOff;
        }
        if (state != RoboTextFeatureOn) {
            continue;
        }
        if (out->featureCount >= (size_t)RoboTextGsubMaxPlanFeatures) {
            return RoboTextDiagMsg(diag, RoboTextDomainGsub, RoboNotSupported,
                                   "the plan cannot hold another enabled feature");
        }
        if (feature->lookupIndexCount > 0u && !feature->lookupIndices) {
            /* A record whose count promises lookups that are not there: skipped rather than followed. */
            continue;
        }
        memcpy(out->features[out->featureCount].tag, tag, RoboTextFeatureTagBytes);
        out->features[out->featureCount].tagValue = feature->featureTag;
        out->features[out->featureCount].featureIndex = featureIndex;
        out->features[out->featureCount].lookupIndices = feature->lookupIndices;
        out->features[out->featureCount].lookupIndexCount = feature->lookupIndexCount;
        out->features[out->featureCount].value = value;
        out->featureCount++;

        for (uint16_t lookup = 0u; lookup < feature->lookupIndexCount; lookup++) {
            uint16_t lookupIndex = feature->lookupIndices[lookup];
            int seen = 0;

            if (lookupIndex >= gsub->lookups.lookupCount) {
                continue;
            }
            for (size_t known = 0u; known < out->lookupCount; known++) {
                if (out->lookupOrder[known] == lookupIndex) {
                    seen = 1;
                    break;
                }
            }
            if (seen) {
                continue;
            }
            if (out->lookupCount >= (size_t)RoboFontOtMaxLookups) {
                break;
            }
            out->lookupOrder[out->lookupCount++] = lookupIndex;
        }
    }

    /*
     * The required feature, if the LangSys names one, is applied whether or not the caller asked for it:
     * "required" is the font's word for it, and a font that requires `ccmp` for a script means it. It is
     * looked up in the FeatureList directly because a required feature need not be listed among the
     * LangSys's own feature indices.
     */
    if (langSys->requiredFeatureIndex != 0xFFFFu &&
        langSys->requiredFeatureIndex < gsub->features.featureCount) {
        const FontOtFeature *feature = &gsub->features.features[langSys->requiredFeatureIndex];
        int known = 0;

        for (size_t index = 0u; index < out->featureCount; index++) {
            if (out->features[index].featureIndex == langSys->requiredFeatureIndex) {
                known = 1;
                break;
            }
        }
        if (!known && out->featureCount < (size_t)RoboTextGsubMaxPlanFeatures) {
            char tag[RoboTextFeatureTagBytes];

            RoboTextGsubTagWrite(feature->featureTag, tag);
            memcpy(out->features[out->featureCount].tag, tag, RoboTextFeatureTagBytes);
            out->features[out->featureCount].tagValue = feature->featureTag;
            out->features[out->featureCount].featureIndex = langSys->requiredFeatureIndex;
            out->features[out->featureCount].lookupIndices = feature->lookupIndices;
            out->features[out->featureCount].lookupIndexCount = feature->lookupIndexCount;
            out->features[out->featureCount].value = 0u;
            out->featureCount++;
            for (uint16_t lookup = 0u; lookup < feature->lookupIndexCount; lookup++) {
                uint16_t lookupIndex = feature->lookupIndices[lookup];
                int seen = 0;

                if (lookupIndex >= gsub->lookups.lookupCount) {
                    continue;
                }
                for (size_t index = 0u; index < out->lookupCount; index++) {
                    if (out->lookupOrder[index] == lookupIndex) {
                        seen = 1;
                        break;
                    }
                }
                if (!seen && out->lookupCount < (size_t)RoboFontOtMaxLookups) {
                    out->lookupOrder[out->lookupCount++] = lookupIndex;
                }
            }
        }
    }

    /* Insertion sort into the Lookup List's order: the font numbers lookups so that a later one sees an
     * earlier one's work, and that order is shaping behaviour rather than an implementation detail. */
    for (size_t index = 1u; index < out->lookupCount; index++) {
        uint16_t value = out->lookupOrder[index];
        size_t slot = index;

        while (slot > 0u && out->lookupOrder[slot - 1u] > value) {
            out->lookupOrder[slot] = out->lookupOrder[slot - 1u];
            slot--;
        }
        out->lookupOrder[slot] = value;
    }
    return RoboOk;
}

/* ------------------------------------------------------------------ */
/* Stats, and the writers                                              */
/* ------------------------------------------------------------------ */

void RoboTextGsubStatsInit(RoboTextGsubStats *stats)
{
    if (stats) {
        memset(stats, 0, sizeof(*stats));
    }
}

/** The dump sink the three writers below share: measure with a NULL buffer, then write. */
typedef struct GsubDumpSink {
    char *buffer;
    size_t capacity;
    size_t length;
    int failed;
} GsubDumpSink;

static void GsubDumpSinkLine(GsubDumpSink *sink, const char *line, size_t lineLength)
{
    if (sink->failed) {
        return;
    }
    if (lineLength == RoboTextWriterOverflow) {
        sink->failed = 1;
        return;
    }
    if (sink->buffer) {
        if (sink->length + lineLength + 2u > sink->capacity + 1u) {
            sink->failed = 1;
            return;
        }
        memcpy(sink->buffer + sink->length, line, lineLength);
        sink->length += lineLength;
        sink->buffer[sink->length++] = '\n';
        sink->buffer[sink->length] = '\0';
    } else {
        sink->length += lineLength + 1u;
    }
}

/** A counter line: `label value`. */
static void GsubCounterLine(GsubDumpSink *sink, const char *label, size_t value)
{
    char line[96];
    size_t length = 0u;

    length = RoboTextWriterText(line, sizeof(line), length, label);
    length = RoboTextWriterUnsigned(line, sizeof(line), length, (uint32_t)value);
    GsubDumpSinkLine(sink, line, length);
}

Result RoboTextGsubStatsWrite(const RoboTextGsubStats *stats,
                              char *buffer,
                              size_t capacity,
                              size_t *outLength)
{
    GsubDumpSink measure;
    GsubDumpSink write;
    GsubDumpSink *sinks[2];

    if (outLength) {
        *outLength = 0u;
    }
    if (!stats || !buffer || capacity == 0u) {
        return RoboInvalidArgument;
    }
    buffer[0] = '\0';
    memset(&measure, 0, sizeof(measure));
    memset(&write, 0, sizeof(write));
    write.buffer = buffer;
    write.capacity = capacity;
    sinks[0] = &measure;
    sinks[1] = &write;
    for (int pass = 0; pass < 2; pass++) {
        GsubDumpSink *sink = sinks[pass];

        GsubCounterLine(sink, "glyphs before ", stats->glyphsBefore);
        GsubCounterLine(sink, "glyphs after ", stats->glyphsAfter);
        GsubCounterLine(sink, "lookups applied ", stats->lookupsApplied);
        GsubCounterLine(sink, "lookups skipped ", stats->lookupsSkipped);
        GsubCounterLine(sink, "subtables tried ", stats->subtablesTried);
        GsubCounterLine(sink, "single ", stats->singleSubstitutions);
        GsubCounterLine(sink, "multiple ", stats->multipleSubstitutions);
        GsubCounterLine(sink, "alternate ", stats->alternateSubstitutions);
        GsubCounterLine(sink, "ligature ", stats->ligatureSubstitutions);
        GsubCounterLine(sink, "contextual ", stats->contextualMatches);
        GsubCounterLine(sink, "reverse ", stats->reverseSubstitutions);
        GsubCounterLine(sink, "unmapped ", stats->unmappedGlyphs);
        GsubCounterLine(sink, "trace events ", stats->traceEvents);
        GsubCounterLine(sink, "probes ", (size_t)stats->probes);
        GsubCounterLine(sink, "limit reached ", stats->limitReached ? 1u : 0u);
        if (sink->failed) {
            return RoboNotSupported;
        }
        if (pass == 0 && measure.length + 1u > capacity) {
            if (outLength) {
                *outLength = measure.length;
            }
            /* Complete-or-absent: the caller's buffer is empty, not a prefix of the table. */
            return RoboNotSupported;
        }
    }
    if (outLength) {
        *outLength = write.length;
    }
    return RoboOk;
}

Result RoboTextGsubBufferWrite(const RoboTextGsubBuffer *buffer,
                               char *bufferOut,
                               size_t capacity,
                               size_t *outLength)
{
    GsubDumpSink measure;
    GsubDumpSink write;
    GsubDumpSink *sinks[2];

    if (outLength) {
        *outLength = 0u;
    }
    if (!buffer || !bufferOut || capacity == 0u) {
        return RoboInvalidArgument;
    }
    bufferOut[0] = '\0';
    memset(&measure, 0, sizeof(measure));
    memset(&write, 0, sizeof(write));
    write.buffer = bufferOut;
    write.capacity = capacity;
    sinks[0] = &measure;
    sinks[1] = &write;
    for (int pass = 0; pass < 2; pass++) {
        GsubDumpSink *sink = sinks[pass];

        GsubCounterLine(sink, "glyphs ", buffer->count);
        for (size_t index = 0u; index < buffer->count; index++) {
            const RoboTextShapingGlyph *glyph = &buffer->glyphs[index];
            char line[192];
            size_t length = 0u;

            length = RoboTextWriterText(line, sizeof(line), length, "#");
            length = RoboTextWriterUnsigned(line, sizeof(line), length, (uint32_t)index);
            length = RoboTextWriterText(line, sizeof(line), length, " gid=");
            length = RoboTextWriterUnsigned(line, sizeof(line), length, (uint32_t)glyph->glyphId);
            length = RoboTextWriterText(line, sizeof(line), length, " src=[");
            length = RoboTextWriterUnsigned(line, sizeof(line), length, glyph->source.start);
            length = RoboTextWriterText(line, sizeof(line), length, ",");
            length = RoboTextWriterUnsigned(line, sizeof(line), length, glyph->source.end);
            length = RoboTextWriterText(line, sizeof(line), length, ") cluster=");
            length = RoboTextWriterUnsigned(line, sizeof(line), length, glyph->cluster);
            length = RoboTextWriterText(line, sizeof(line), length, " lvl=");
            if (RoboTextBidiLevelIsResolved(glyph->bidiLevel)) {
                length = RoboTextWriterUnsigned(line, sizeof(line), length, glyph->bidiLevel);
            } else {
                length = RoboTextWriterText(line, sizeof(line), length, "x");
            }
            length = RoboTextWriterText(line, sizeof(line), length, " flags=");
            length = RoboTextWriterUnsigned(line, sizeof(line), length, glyph->flags);
            if (glyph->inputGlyphId != RoboGlyphIdNone) {
                length = RoboTextWriterText(line, sizeof(line), length, " from=");
                length = RoboTextWriterUnsigned(line, sizeof(line), length, glyph->inputGlyphId);
            }
            if (glyph->ligatureComponents != 0u) {
                length = RoboTextWriterText(line, sizeof(line), length, " components=");
                length = RoboTextWriterUnsigned(line, sizeof(line), length, glyph->ligatureComponents);
            }
            GsubDumpSinkLine(sink, line, length);
        }
        if (sink->failed) {
            return RoboNotSupported;
        }
        if (pass == 0 && measure.length + 1u > capacity) {
            if (outLength) {
                *outLength = measure.length;
            }
            return RoboNotSupported;
        }
    }
    if (outLength) {
        *outLength = write.length;
    }
    return RoboOk;
}

Result RoboTextGsubEventsWrite(const RoboTextGsubEvent *events,
                               size_t count,
                               char *buffer,
                               size_t capacity,
                               size_t *outLength)
{
    GsubDumpSink measure;
    GsubDumpSink write;
    GsubDumpSink *sinks[2];

    if (outLength) {
        *outLength = 0u;
    }
    if ((count != 0u && !events) || !buffer || capacity == 0u) {
        return RoboInvalidArgument;
    }
    buffer[0] = '\0';
    memset(&measure, 0, sizeof(measure));
    memset(&write, 0, sizeof(write));
    write.buffer = buffer;
    write.capacity = capacity;
    sinks[0] = &measure;
    sinks[1] = &write;
    for (int pass = 0; pass < 2; pass++) {
        GsubDumpSink *sink = sinks[pass];

        GsubCounterLine(sink, "events ", count);
        for (size_t index = 0u; index < count; index++) {
            const RoboTextGsubEvent *event = &events[index];
            char line[288];
            char tag[RoboTextFeatureTagBytes];
            size_t length = 0u;

            memcpy(tag, event->feature, 4u);
            tag[4] = '\0';
            length = RoboTextWriterText(line, sizeof(line), length, tag);
            length = RoboTextWriterText(line, sizeof(line), length, " lookup ");
            length = RoboTextWriterUnsigned(line, sizeof(line), length, event->lookupIndex);
            length = RoboTextWriterText(line, sizeof(line), length, " (type ");
            length = RoboTextWriterUnsigned(line, sizeof(line), length, event->lookupType);
            length = RoboTextWriterText(line, sizeof(line), length, ") at ");
            length = RoboTextWriterUnsigned(line, sizeof(line), length, (uint32_t)event->position);
            length = RoboTextWriterText(line, sizeof(line), length, ":");
            for (size_t glyph = 0u; glyph < event->fromCount; glyph++) {
                length = RoboTextWriterText(line, sizeof(line), length, " ");
                length = RoboTextWriterUnsigned(line, sizeof(line), length, event->from[glyph]);
            }
            length = RoboTextWriterText(line, sizeof(line), length, " ->");
            for (size_t glyph = 0u; glyph < event->toCount; glyph++) {
                length = RoboTextWriterText(line, sizeof(line), length, " ");
                length = RoboTextWriterUnsigned(line, sizeof(line), length, event->to[glyph]);
            }
            length = RoboTextWriterText(line, sizeof(line), length, " src=[");
            length = RoboTextWriterUnsigned(line, sizeof(line), length, event->source.start);
            length = RoboTextWriterText(line, sizeof(line), length, ",");
            length = RoboTextWriterUnsigned(line, sizeof(line), length, event->source.end);
            length = RoboTextWriterText(line, sizeof(line), length, ")");
            GsubDumpSinkLine(sink, line, length);
        }
        if (sink->failed) {
            return RoboNotSupported;
        }
        if (pass == 0 && measure.length + 1u > capacity) {
            if (outLength) {
                *outLength = measure.length;
            }
            return RoboNotSupported;
        }
    }
    if (outLength) {
        *outLength = write.length;
    }
    return RoboOk;
}

Result RoboTextGsubPlanWrite(const RoboTextGsubPlan *plan,
                             char *buffer,
                             size_t capacity,
                             size_t *outLength)
{
    char line[512];
    size_t length = 0u;

    if (outLength) {
        *outLength = 0u;
    }
    if (!plan || !buffer || capacity == 0u) {
        return RoboInvalidArgument;
    }
    buffer[0] = '\0';
    if (plan->empty) {
        length = RoboTextWriterText(line, sizeof(line), length, "(empty plan)");
    } else {
        char scriptTag[RoboTextFeatureTagBytes];
        char languageTag[RoboTextFeatureTagBytes];

        RoboTextGsubTagWrite(plan->scriptTag, scriptTag);
        length = RoboTextWriterText(line, sizeof(line), length, scriptTag);
        length = RoboTextWriterText(line, sizeof(line), length, "/");
        if (plan->languageTag != 0u) {
            RoboTextGsubTagWrite(plan->languageTag, languageTag);
            length = RoboTextWriterText(line, sizeof(line), length, languageTag);
        } else {
            length = RoboTextWriterText(line, sizeof(line), length, "dflt");
        }
        length = RoboTextWriterText(line, sizeof(line), length, "  features:");
        for (size_t index = 0u; index < plan->featureCount; index++) {
            length = RoboTextWriterText(line, sizeof(line), length, " ");
            length = RoboTextWriterText(line, sizeof(line), length, plan->features[index].tag);
        }
        length = RoboTextWriterText(line, sizeof(line), length, "  lookups:");
        for (size_t index = 0u; index < plan->lookupCount; index++) {
            length = RoboTextWriterText(line, sizeof(line), length, " ");
            length = RoboTextWriterUnsigned(line, sizeof(line), length, plan->lookupOrder[index]);
        }
        if (plan->usedDefaultScript) {
            length = RoboTextWriterText(line, sizeof(line), length, "  (DFLT)");
        }
        if (plan->usedDefaultLangSys) {
            length = RoboTextWriterText(line, sizeof(line), length, "  (default LangSys)");
        }
    }
    if (length == RoboTextWriterOverflow || length + 1u > capacity) {
        if (outLength) {
            *outLength = (length == RoboTextWriterOverflow) ? 0u : length;
        }
        return RoboNotSupported;
    }
    memcpy(buffer, line, length + 1u);
    if (outLength) {
        *outLength = length;
    }
    return RoboOk;
}

/* ------------------------------------------------------------------ */
/* Application                                                         */
/* ------------------------------------------------------------------ */

/** The lookup at @p index in @p gsub, or NULL when the index is outside the Lookup List. */
static const FontOtLookup *GsubLookupAt(const FontOtLayout *gsub, uint16_t index)
{
    if (!gsub || index >= gsub->lookups.lookupCount) {
        return NULL;
    }
    return &gsub->lookups.lookups[index];
}

void RoboTextGsubTrace(RoboTextGsubCall *apply,
                       size_t position,
                       const GlyphId *from,
                       size_t fromCount,
                       const GlyphId *to,
                       size_t toCount,
                       RoboTextRange source)
{
    RoboTextGsubEvent *event;

    if (!apply || !apply->trace || apply->traceCount >= apply->traceCapacity) {
        return;
    }
    event = &apply->trace[apply->traceCount++];
    memset(event, 0, sizeof(*event));
    memcpy(event->feature, apply->featureTag, 4u);
    event->lookupIndex = apply->lookupIndex;
    event->lookupType = apply->lookupType;
    event->position = position;
    event->source = source;
    for (size_t index = 0u; index < fromCount && index < RoboTextGsubEventGlyphs; index++) {
        event->from[index] = from[index];
    }
    event->fromCount = fromCount;
    for (size_t index = 0u; index < toCount && index < RoboTextGsubEventGlyphs; index++) {
        event->to[index] = to[index];
    }
    event->toCount = toCount;
}

int RoboTextGsubLookupAppliesToDirection(const RoboTextGsubCall *apply)
{
    if (!apply || !apply->lookup) {
        return 0;
    }
    if ((apply->lookup->lookupFlag & RoboFontOtLookupFlagRtl) == 0u) {
        /* Most lookups say nothing about direction: they apply to either. */
        return 1;
    }
    /* `RightToLeft` is the flag a font sets on a lookup that must not run on left-to-right text — an Arabic
     * `fina` whose forms are meaningless for Latin. It is a condition on the run's *resolved* direction. */
    return RoboTextDirectionIsRightToLeft(apply->direction) ? 1 : 0;
}

/**
 * Apply one lookup, scanning every position. @return 1 when it substituted anywhere.
 *
 * The one lookup type whose scan runs the other way is reverse chaining (type 8), and the reason is in its
 * format rather than in the text: its substitutes are listed by the input glyph's coverage index, and what
 * makes the lookup applicable is its context on *both* sides — so "reverse" is the traversal order the
 * format's own name describes. Nothing about the run's direction follows from it, and the buffer's length
 * cannot change under this scan because that format substitutes one glyph for one.
 */
static int GsubApplyLookupScan(RoboTextGsubCall *apply)
{
    size_t index;
    int applied = 0;

    if (apply->lookupType == RoboTextGsubLookupReverseChaining) {
        size_t reverse = RoboTextGsubBufferCount(apply->buffer);

        while (reverse != 0u) {
            size_t resolved = 0u;

            reverse--;
            for (uint16_t subtable = 0u; subtable < apply->lookup->subtableCount; subtable++) {
                const FontOtLookupSubtable *entry = &apply->lookup->subtables[subtable];

                if (entry->offset == 0u) {
                    continue;
                }
                apply->stats.subtablesTried++;
                if (RoboTextGsubApplySubtable(apply, apply->lookupType, entry->offset, reverse,
                                              &resolved)) {
                    applied = 1;
                    break;
                }
                if (apply->failed) {
                    return applied;
                }
            }
        }
        return applied;
    }

    index = 0u;
    while (index < RoboTextGsubBufferCount(apply->buffer)) {
        size_t next = index + 1u;
        int substituted = 0;

        for (uint16_t subtable = 0u; subtable < apply->lookup->subtableCount; subtable++) {
            const FontOtLookupSubtable *entry = &apply->lookup->subtables[subtable];
            size_t resolved = index;

            if (entry->offset == 0u) {
                continue;
            }
            apply->stats.subtablesTried++;
            if (RoboTextGsubApplySubtable(apply, apply->lookupType, entry->offset, index, &resolved)) {
                substituted = 1;
                next = resolved;
                break;
            }
            if (apply->failed) {
                return applied;
            }
        }
        if (!substituted) {
            index++;
            continue;
        }
        applied = 1;
        index = (next > index) ? next : index + 1u;
    }
    return applied;
}

int RoboTextGsubApplyLookupAt(RoboTextGsubCall *apply,
                              uint16_t lookupIndex,
                              size_t position,
                              size_t *outNext)
{
    const FontOtLookup *lookup = GsubLookupAt(apply->gsub, lookupIndex);
    const FontOtLookup *outer = apply->lookup;
    uint16_t outerType = apply->lookupType;
    char outerTag[4];
    int substituted = 0;

    if (outNext) {
        *outNext = position;
    }
    if (!lookup || !apply->limits) {
        return 0;
    }
    if (apply->depth + 1u > apply->limits->maxNesting) {
        apply->limitReached = 1;
        return 0;
    }
    if (apply->stats.lookupsApplied >= (size_t)apply->limits->maxLookupApplications) {
        apply->limitReached = 1;
        return 0;
    }
    memcpy(outerTag, apply->featureTag, 4u);
    apply->lookup = lookup;
    apply->lookupIndex = lookupIndex;
    apply->lookupType = lookup->lookupType;
    apply->depth++;
    apply->stats.lookupsApplied++;

    for (uint16_t subtable = 0u; subtable < lookup->subtableCount; subtable++) {
        size_t resolved = position;

        if (lookup->subtables[subtable].offset == 0u) {
            continue;
        }
        apply->stats.subtablesTried++;
        if (RoboTextGsubApplySubtable(apply, apply->lookupType, lookup->subtables[subtable].offset,
                                      position, &resolved)) {
            if (outNext) {
                *outNext = resolved;
            }
            substituted = 1;
            break;
        }
        if (apply->failed) {
            break;
        }
    }

    apply->depth--;
    apply->lookup = outer;
    apply->lookupType = outerType;
    memcpy(apply->featureTag, outerTag, 4u);
    return substituted;
}

Result RoboTextGsubApply(const FontOtLayout *gsub,
                         const FontGdef *gdef,
                         const RoboTextGsubPlan *plan,
                         const RoboTextGsubOptions *options,
                         RoboTextGsubBuffer *buffer,
                         RoboTextGsubStats *outStats,
                         RoboTextDiag *diag)
{
    RoboTextGsubCall apply;
    RoboTextGsubOptions defaults;

    if (!buffer || !outStats) {
        return RoboTextDiagMsg(diag, RoboTextDomainGsub, RoboInvalidArgument,
                               "a buffer and an out stats record are required");
    }
    RoboTextGsubStatsInit(outStats);
    memset(&apply, 0, sizeof(apply));
    memset(&defaults, 0, sizeof(defaults));
    apply.gsub = gsub;
    apply.gdef = gdef;
    apply.plan = plan;
    apply.buffer = buffer;
    apply.limits = (options && options->limits) ? options->limits : &RoboTextGsubDefaultLimits;
    apply.direction = options ? options->direction : RoboTextDirectionUnknown;
    apply.bidiLevel = options ? options->bidiLevel : RoboTextBidiLevelNone;
    if (options) {
        apply.trace = options->trace;
        apply.traceCapacity = options->traceCapacity;
    }
    apply.stats.glyphsBefore = RoboTextGsubBufferCount(buffer);
    for (size_t index = 0u; index < apply.stats.glyphsBefore; index++) {
        const RoboTextShapingGlyph *glyph = RoboTextGsubBufferAt(buffer, index);

        if (glyph && glyph->glyphId == RoboGlyphIdNone) {
            apply.stats.unmappedGlyphs++;
        }
    }

    if (gsub && plan && !plan->empty) {
        if (gsub->kind != RoboFontOtLayoutGsub) {
            return RoboTextDiagMsg(diag, RoboTextDomainGsub, RoboInvalidArgument,
                                   "the layout handed to the substitution engine is not GSUB");
        }
        for (size_t index = 0u; index < plan->lookupCount; index++) {
            const FontOtLookup *lookup = GsubLookupAt(gsub, plan->lookupOrder[index]);
            uint16_t type;

            if (!lookup) {
                apply.stats.lookupsSkipped++;
                continue;
            }
            type = lookup->lookupType;
            if (type == 0u || !FontGsubLookupTypeIsKnown(type)) {
                /* A lookup type this build does not execute. Skipped, counted, and shaping continues: the
                 * glyphs from the lookups before it are already the caller's. */
                apply.stats.lookupsSkipped++;
                continue;
            }
            apply.lookup = lookup;
            apply.lookupIndex = plan->lookupOrder[index];
            apply.lookupType = type;
            memcpy(apply.featureTag, GsubFeatureForLookup(plan, apply.lookupIndex), 4u);
            if (!RoboTextGsubLookupAppliesToDirection(&apply)) {
                /* The font's own condition, not a failure: a right-to-left-only lookup on left-to-right
                 * text simply does not apply. */
                continue;
            }
            if (apply.stats.lookupsApplied >= (size_t)apply.limits->maxLookupApplications) {
                apply.limitReached = 1;
                break;
            }
            apply.depth = 0u;
            apply.stats.lookupsApplied++;
            (void)GsubApplyLookupScan(&apply);
            if (apply.failed) {
                break;
            }
        }
    }

    apply.stats.glyphsAfter = RoboTextGsubBufferCount(buffer);
    apply.stats.traceEvents = apply.traceCount;
    apply.stats.limitReached = (apply.limitReached || apply.failed) ? 1 : 0;
    *outStats = apply.stats;
    RoboTextGsubCacheClear(&apply);
    if (apply.failed) {
        return RoboTextDiagMsg(diag, RoboTextDomainGsub, RoboOutOfMemory,
                               "the shaping buffer could not grow");
    }
    return RoboOk;
}
