/*
 * Kerning (Phase 12.13) — the policy that decides *whether* a font's spacing corrections apply, and the
 * record of what they did.
 *
 * ## There is no second kerning engine here
 *
 * Phase 12.11 built positioning: the GPOS table, the pair-adjustment readers, the value records and the
 * extension wrapper. Kerning **is** GPOS pair adjustment, so this layer adds no per-glyph table, no `AV`
 * special case, no Unicode pair, and no accumulation of character widths. What it adds is the three things an
 * engine should not own:
 *
 *   - **the policy** — `kern` is an OpenType feature like any other, and `RoboTextKerningPolicy` is the
 *     caller's decision as data. It becomes entries in a `RoboTextFeatureSet`, which is the only thing the
 *     plan resolver reads, so `font-kerning: none` reaches the engine as `"kern" off` and never as an
 *     instruction to a paint layer. Nothing in this file branches on a script.
 *   - **the resolution** — whether kerning is on, whether the face has a `kern` feature for this run's script
 *     and language, whether a legacy `kern` table is even available, and which of the two sources answered.
 *     One face can carry kerning in both, and the rule is a choice rather than a sum (see
 *     @ref RoboTextKerningLegacy): a pair the GPOS table already moved must not be moved again by a table
 *     written for a font format that predates it.
 *   - **the report** — the adjustments the run actually received, read off the glyphs. `advanceAdjustX` is
 *     the font's own part of a glyph's advance, so a diagnostic can print "the pen moves 12.40px, of which
 *     -1.25 is the face's kerning" without re-running a lookup.
 *
 * ## Where kerning sits in the pipeline
 *
 * ```text
 * Unicode → scripts → direction → bidi → font selection → features → cmap → GSUB → GPOS (kerning) → glyph run
 * ```
 *
 * Positioning runs **after** substitution, always: a ligature is one glyph with one advance, and kerning the
 * characters that produced it would be kerning glyphs that no longer exist. The order is
 * `RoboTextGlyphs.c`'s, not a caller's choice, and `text.kern.after_ligature` inside the substitution suite is
 * what holds it.
 *
 * ## Precision
 *
 * A value record is font units scaled **once** through `RoboTextFontUnitsToDevice`, and a glyph's adjustment
 * stays a `float` device pixel through layout: a `-0.35px` pair is not rounded to `0px` on the way. Rounding
 * happens at the rasterizer, which is the module's policy in one sentence — precision through shaping and
 * layout, round once where the drawing is.
 *
 * ## What this layer does not do
 *
 * No optical kerning, no tracking or letter-spacing, no `kern`-table parser of its own (RobinFont owns the
 * face's tables, and Phase 5 does not parse the legacy `kern` table yet — @ref RoboTextKerningResolution says
 * so rather than pretending), and no CSS parsing: `font-kerning` maps onto
 * `RoboTextShapeOptionsSetFeature(options, "kern", …)` and nothing else.
 *
 * Boundary: this file may include `RoboBase`, `RoboMem`, `RoboText`'s own types, the font doors and the glyph
 * run. It parses no table, shards no text, and knows nothing of a layout or a paint layer.
 */
#ifndef ROBO_TEXT_KERNING_H
#define ROBO_TEXT_KERNING_H

#include "RoboBase/RoboError.h"
#include "RoboText/RoboTextDiagnostics.h"
#include "RoboText/RoboTextFont.h"
#include "RoboText/RoboTextGlyphs.h"
#include "RoboText/RoboTextTypes.h"

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/** The `kern` tag, as the one string this file spells. */
#define RoboTextKerningFeatureTag "kern"

/* ------------------------------------------------------------------ */
/* Policy                                                              */
/* ------------------------------------------------------------------ */

/** Nothing stated: `RoboTextFeatureDefault` for `kern`, @ref RoboTextKerningLegacyNever. */
void RoboTextKerningPolicyInit(RoboTextKerningPolicy *policy);

/**
 * The module's default policy: **kerning on**, legacy `kern` table never consulted.
 *
 * `kern` on is the phase's answer for ordinary document and UI text, and it is the same answer the GPOS plan's
 * own script defaults give — stated here so that it is a caller-visible decision
 * (`RoboTextShapeOptionsInit` installs it) rather than a table buried in an engine. A face that carries no
 * `kern` feature is simply a face that kerns nothing; that is the font's answer, not a policy question.
 */
void RoboTextKerningPolicyDefault(RoboTextKerningPolicy *policy);

/** Set what is asked of `kern`. RoboInvalidArgument for a NULL policy or an undefined feature state. */
Result RoboTextKerningPolicySet(RoboTextKerningPolicy *policy, RoboTextFeatureState kern);

/** What is asked of `kern`: `RoboTextFeatureDefault` for a NULL policy. */
RoboTextFeatureState RoboTextKerningPolicyState(const RoboTextKerningPolicy *policy);

/** Which source may answer: @ref RoboTextKerningLegacyNever for a NULL policy. */
RoboTextKerningLegacy RoboTextKerningPolicyLegacy(const RoboTextKerningPolicy *policy);

/** Set which source may answer. RoboInvalidArgument for a NULL policy or an unknown enum value. */
Result RoboTextKerningPolicySetLegacy(RoboTextKerningPolicy *policy, RoboTextKerningLegacy legacy);

/** Non-zero when both fields are defined. Safe on NULL (NULL is not valid). */
int RoboTextKerningPolicyIsValid(const RoboTextKerningPolicy *policy);

/** Field-for-field equality. Safe on NULL (NULL == NULL). */
int RoboTextKerningPolicyEqual(const RoboTextKerningPolicy *a, const RoboTextKerningPolicy *b);

/** `"kern=on legacy=never"`, or `"kern=default legacy=never"`. */
Result RoboTextKerningPolicyWrite(const RoboTextKerningPolicy *policy,
                                  char *buffer,
                                  size_t capacity,
                                  size_t *outLength);

/**
 * Write the policy's `kern` state into @p set, **overwriting** what is there.
 *
 * The door for a layer that owns the whole answer. A caller layering a policy *under* explicit settings wants
 * @ref RoboTextKerningPolicyApplyIfUnset.
 *
 * @return RoboOk; RoboInvalidArgument for a NULL argument; RoboNotSupported when @p set is full of other tags.
 */
Result RoboTextKerningPolicyApply(const RoboTextKerningPolicy *policy, RoboTextFeatureSet *set);

/**
 * Write `kern` into @p set **only when @p set does not name that tag**.
 *
 * What the run set does with `RoboTextShapeOptions::kerning`: a policy is a default about one feature and an
 * explicit setting is a decision about that one, so the explicit one wins — `"kern" off` survives a policy
 * that asks for kerning. Never fails, and a set with no room for the tag simply keeps the font's own answer.
 */
void RoboTextKerningPolicyApplyIfUnset(const RoboTextKerningPolicy *policy, RoboTextFeatureSet *set);

/* ------------------------------------------------------------------ */
/* Resolution                                                          */
/* ------------------------------------------------------------------ */

/** Which source actually answered for a run. */
typedef enum RoboTextKerningSource {
    /** Nothing did: kerning is off, or the face has nothing to say for this script and language. */
    RoboTextKerningSourceNone = 0,
    /** The face's GPOS `kern` feature. The primary path. */
    RoboTextKerningSourceGpos = 1,
    /** The face's legacy `kern` table, used as the fallback it is. */
    RoboTextKerningSourceLegacyKern = 2,
    RoboTextKerningSourceCount = 3
} RoboTextKerningSource;

/** Name of @p source (`"none"`, `"gpos"`, `"kern-table"`); never NULL. */
const char *RoboTextKerningSourceName(RoboTextKerningSource source);

/** Non-zero when @p source is a defined enumerator. */
int RoboTextKerningSourceIsKnown(RoboTextKerningSource source);

/**
 * What a resolution is asked about: one run's face, script, language, direction and features.
 *
 * The phase's `KerningRequest`. `direction` and `language` are carried because the answer depends on them: the
 * same face carries different `kern` features for different scripts, and a lookup may be stated for one
 * direction only. Nothing here is defaulted silently — a caller with no language passes NULL, which means
 * "the script's default LangSys", exactly as the plan resolver reads it.
 */
typedef struct RoboTextKerningRequest {
    /** The face and size the answer is about. Required. */
    const RoboTextFontContext *font;
    /** The run's script. `Common` resolves the face's `DFLT` record, as every plan in this module does. */
    RoboTextScript script;
    /** The run's language, or NULL for the script's default. */
    const RoboTextLanguage *language;
    /** The run's resolved direction. */
    RoboTextDirection direction;
    /** The caller's feature settings, or NULL for "none stated". */
    const RoboTextFeatureSet *features;
} RoboTextKerningRequest;

/**
 * The answer: whether kerning applies, what the face offers, and which source won.
 *
 * Every field is a *fact about this face for this request*, not a guess: `fontHasKernFeature` is read from the
 * face's own GPOS ScriptList/LangSys/FeatureList, `legacyTableAvailable` is read from the face's table
 * directory, and `source` is the one that actually applies. A face with no GPOS at all reports
 * `RoboTextKerningSourceNone` with `fontHasKernFeature == 0`, which is a different situation from a caller
 * that turned kerning off (`enabled == 0`) and both are visible here.
 */
typedef struct RoboTextKerningResolution {
    /** Non-zero when the policy and the caller's features leave `kern` on. */
    int enabled;
    /** Non-zero when the face's GPOS carries a `kern` feature for this script and language. */
    int fontHasKernFeature;
    /**
     * Non-zero when the face ships a legacy `kern` table **that this build can read**.
     *
     * **Always 0 in this build**: Phase 5 parses the OpenType layout tables, and the legacy `kern` table is
     * not one of them. The field exists so that a caller asking for
     * @ref RoboTextKerningLegacyIfNoGposKern is told the fallback is not there rather than being handed a
     * silently kerning-free run, and so that the day the parser lands the answer changes in one place.
     */
    int legacyTableAvailable;
    /** Which source answered. @ref RoboTextKerningSourceNone when nothing does. */
    RoboTextKerningSource source;
    /** Pair lookups the `kern` feature enabled for this request. Zero when the feature is off or absent. */
    size_t lookupCount;
    /** Non-zero when the feature is on and the face has at least one lookup for it. */
    int applies;
} RoboTextKerningResolution;

/** `"kerning=on source=gpos lookups=3 features=1"`. */
Result RoboTextKerningResolutionWrite(const RoboTextKerningResolution *resolution,
                                      char *buffer,
                                      size_t capacity,
                                      size_t *outLength);

/**
 * Resolve @p request against @p policy.
 *
 * Reads the face's GPOS plan for @p request's script and language (`RoboTextGposPlanResolve`, so the
 * ScriptList/LangSys/FeatureList selection is the *same* one the engine will execute — there is no second
 * feature lookup here) and answers what applies and from where. Pure: no table is parsed twice, nothing is
 * cached globally, and the same request answers the same resolution.
 *
 * @return RoboOk; RoboInvalidArgument for a NULL @p out or a request with no face.
 */
Result RoboTextKerningResolve(const RoboTextKerningRequest *request,
                              const RoboTextKerningPolicy *policy,
                              RoboTextKerningResolution *out,
                              RoboTextDiag *diag);

/* ------------------------------------------------------------------ */
/* What the font did                                                   */
/* ------------------------------------------------------------------ */

/** One glyph the font moved, with the pair that moved it. */
typedef struct RoboTextKerningAdjustment {
    /** The glyph's index in the run. */
    RoboTextGlyphIndex glyphIndex;
    /** The glyph the font kerns it against, by position — `glyphIndex + 1` for a pair, and never 0xFFFF. */
    RoboTextGlyphIndex pairIndex;
    /** The glyph's id, and the neighbour's. */
    uint16_t glyphId;
    uint16_t pairGlyphId;
    /** The scalars the glyph stands for — a ligature's whole span, not its first scalar. */
    RoboTextRange source;
    /** The placement the font asked for. */
    float xPlacement;
    /** See @ref xPlacement. Zero for horizontal text in every face that has no vertical positioning. */
    float yPlacement;
    /** The advance the font added, in device pixels. Negative for the usual pair of square capitals. */
    float xAdvance;
    /** See @ref xAdvance. Zero for horizontal text. */
    float yAdvance;
} RoboTextKerningAdjustment;

/** One number per kerning question, for a dump or an assertion. */
typedef struct RoboTextKerningReport {
    /** Glyphs in the run. */
    size_t glyphs;
    /** Glyphs the face positioned at all: the run's `positioned` count, restated where a kerning reader looks. */
    size_t positionedGlyphs;
    /** Glyphs whose **advance** the face moved, which is kerning's own effect. */
    size_t adjustedAdvances;
    /** Adjacent pairs of positioned glyphs: `A V` counts, `A B` does not. */
    size_t adjustedPairs;
    /** The sum of every advance adjustment, in device pixels. Zero means "nothing to move the pen by". */
    float advanceSum;
    /** The largest negative adjustment: how far the tightest pair pulls in. Zero when none is negative. */
    float advanceMin;
    /** The run's total advance. */
    double advance;
} RoboTextKerningReport;

/** Fill @p out from @p run. RoboInvalidArgument for NULL arguments. Safe on an empty or unpositioned run. */
Result RoboTextKerningReportForRun(const RoboTextGlyphRun *run, RoboTextKerningReport *out);

/**
 * `"positioned=6 advances=3 pairs=2 sum=-2.50 min=-1.25 glyphs=8 advance=101.42"`.
 *
 * The number a test asserts on, and the line a reader looks at when spacing looks wrong.
 */
Result RoboTextKerningReportWrite(const RoboTextKerningReport *report,
                                  char *buffer,
                                  size_t capacity,
                                  size_t *outLength);

/**
 * The @p index'th glyph the face moved, by position among the run's positioned glyphs.
 *
 * @param outAdjustment receives the record.
 * @return RoboOk; RoboInvalidArgument for NULL arguments; RoboNotFound when @p index is at or past the
 *         positioned count — a run the font did not position answers `RoboNotFound` for index 0 rather than
 *         reporting a glyph that carries nothing.
 */
Result RoboTextKerningAdjustmentAt(const RoboTextGlyphRun *run,
                                   size_t index,
                                   RoboTextKerningAdjustment *outAdjustment,
                                   RoboTextDiag *diag);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_TEXT_KERNING_H */
