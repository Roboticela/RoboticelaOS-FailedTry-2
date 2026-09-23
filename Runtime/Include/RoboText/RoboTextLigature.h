/*
 * Ligatures (Phase 12.12) — the policy that decides *whether* a font's ligatures run, and the record of what
 * they produced.
 *
 * ## What is here, and what is deliberately not
 *
 * Phase 12.10 built GSUB: the tables, the lookups, the ligature substitution itself. This layer adds no
 * second engine and no `ffi` table of its own. It answers two questions 12.10 should not have to:
 *
 *   - **Which ligature features does a caller want?** `liga`, `clig`, `rlig`, `dlig`, `hlig` are five
 *     independent tags, and typography — not the engine — decides their defaults. `RoboTextLigaturePolicy`
 *     is that decision as data, `RoboTextLigatureVariant` is the same decision in the vocabulary a stylesheet
 *     uses, and a policy reaches the engine by becoming entries in a `RoboTextFeatureSet`, which is the only
 *     thing GSUB reads. There is no `if (tag == "liga") ApplyLigatures()` anywhere: the font says which
 *     lookups a tag drives.
 *   - **What did the font actually do?** A substituted glyph already carries its source range and a
 *     component count, so selection, caret placement and hit testing can still see the characters a ligature
 *     swallowed (`RoboTextGlyphRunLigatureAt`), and `RoboTextLigatureReport` is the one-number-per-question
 *     summary a diagnostic or a test asserts on.
 *
 * ## Required ligatures are not optional typography
 *
 * `rlig` exists because some scripts cannot be written without it: Arabic joins, Indic reorders, and a font
 * that lost its required substitutions would produce the wrong *letters*, not merely a less pretty `fi`. So
 * the policy keeps the kinds apart rather than treating them as a scale — `RoboTextLigatureVariantNone`
 * turns the optional kinds off and leaves `rlig` alone — and `RoboTextLigaturePolicyApply` never folds one
 * tag's state into another's.
 *
 * ## Drawing a ligature is the painter's business
 *
 * Everything here is about glyph *identity* and provenance. A ligature's advance is its own glyph's `hmtx`
 * advance, not the sum of the characters it replaced, and measurement therefore already follows the shaped
 * result without this file doing anything. No code-point paint path exists to bypass: the glyph run the
 * engine produced is what a display list receives.
 *
 * Boundary: this file may include `RoboBase`, `RoboMem`, `RoboText`'s own types and the glyph run. It
 * resolves no font, walks no UTF-8, and knows nothing of a stylesheet beyond the variant names.
 */
#ifndef ROBO_TEXT_LIGATURE_H
#define ROBO_TEXT_LIGATURE_H

#include "RoboBase/RoboError.h"
#include "RoboText/RoboTextDiagnostics.h"
#include "RoboText/RoboTextGlyphs.h"
#include "RoboText/RoboTextTypes.h"

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------ */
/* Kinds and tags                                                      */
/* ------------------------------------------------------------------ */

/** Non-zero when @p kind is one of the five. Safe on NULL-like values (0 is a kind). */
int RoboTextLigatureKindIsKnown(RoboTextLigatureKind kind);

/** The kind's name: `"standard"`, `"contextual"`, `"required"`, `"discretionary"`, `"historical"`. */
const char *RoboTextLigatureKindName(RoboTextLigatureKind kind);

/** The kind's OpenType tag: `"liga"`, `"clig"`, `"rlig"`, `"dlig"`, `"hlig"`. Empty for an unknown kind. */
const char *RoboTextLigatureKindTag(RoboTextLigatureKind kind);

/**
 * The kind @p tag names.
 *
 * @param outKind receives it; untouched when the answer is 0.
 * @return non-zero when @p tag is one of the five, 0 otherwise (a NULL argument included).
 */
int RoboTextLigatureKindForTag(const char *tag, RoboTextLigatureKind *outKind);

/* ------------------------------------------------------------------ */
/* Variants — the stylesheet's vocabulary                              */
/* ------------------------------------------------------------------ */

/** Non-zero when @p variant is one of the eight. */
int RoboTextLigatureVariantIsKnown(RoboTextLigatureVariant variant);

/** The variant's CSS spelling: `"normal"`, `"none"`, `"common-ligatures"`, `"no-common-ligatures"`,
 *  `"contextual"`, `"no-contextual"`, `"discretionary-ligatures"`, `"historical-ligatures"`. */
const char *RoboTextLigatureVariantName(RoboTextLigatureVariant variant);

/**
 * The policy @p variant asks for, written into @p out.
 *
 * The one place CSS lands. `RoboTextLigatureVariantNormal` states nothing (the font's own record decides,
 * as it does for a caller that never mentions ligatures); `…None` states `Off` for the optional kinds and
 * leaves `RoboTextLigatureRequired` at `RoboTextFeatureDefault`; the rest state one kind and leave the others
 * as they were in @p out, so two variants applied in order compose the way two CSS declarations would.
 */
void RoboTextLigaturePolicyFromVariant(RoboTextLigatureVariant variant, RoboTextLigaturePolicy *out);

/* ------------------------------------------------------------------ */
/* Policy                                                              */
/* ------------------------------------------------------------------ */

/** Every kind `RoboTextFeatureDefault`: nothing stated. */
void RoboTextLigaturePolicyInit(RoboTextLigaturePolicy *policy);

/**
 * The module's default policy: standard, contextual and required **on**, discretionary and historical
 * **off**.
 *
 * The same answer the GSUB plan's script defaults give for `liga`/`clig`/`rlig`, stated here so that it is
 * a caller-visible decision (`RoboTextShapeOptionsInit` installs it) rather than a table inside an engine.
 */
void RoboTextLigaturePolicyDefault(RoboTextLigaturePolicy *policy);

/** Set one kind's state. RoboInvalidArgument for a NULL policy or an unknown kind. */
Result RoboTextLigaturePolicySet(RoboTextLigaturePolicy *policy,
                                 RoboTextLigatureKind kind,
                                 RoboTextFeatureState state);

/** One kind's state, or `RoboTextFeatureDefault` for an unknown kind or a NULL policy. */
RoboTextFeatureState RoboTextLigaturePolicyState(const RoboTextLigaturePolicy *policy,
                                                 RoboTextLigatureKind kind);

/** Non-zero when every entry is a known feature state. Safe on NULL (NULL is not valid). */
int RoboTextLigaturePolicyIsValid(const RoboTextLigaturePolicy *policy);

/** Field-for-field equality. Safe on NULL (NULL == NULL). */
int RoboTextLigaturePolicyEqual(const RoboTextLigaturePolicy *a, const RoboTextLigaturePolicy *b);

/** `"liga=on clig=on rlig=on dlig=off hlig=off"`, or `"(nothing stated)"`. */
Result RoboTextLigaturePolicyWrite(const RoboTextLigaturePolicy *policy,
                                   char *buffer,
                                   size_t capacity,
                                   size_t *outLength);

/**
 * Write every kind the policy states into @p set, **overwriting** what is there.
 *
 * The door for a layer that owns the whole answer — a stylesheet whose `font-variant-ligatures` is the last
 * word. A caller layering a policy *under* explicit settings wants @ref RoboTextLigaturePolicyApplyIfUnset.
 *
 * @return RoboOk; RoboInvalidArgument for a NULL argument; RoboNotSupported when @p set has no room left.
 */
Result RoboTextLigaturePolicyApply(const RoboTextLigaturePolicy *policy, RoboTextFeatureSet *set);

/**
 * Write each stated kind into @p set **only when @p set does not name that tag**.
 *
 * What the run set does with `RoboTextShapeOptions::ligatures`: a policy is a default about five tags and an
 * explicit setting is a decision about one, so the explicit one wins. Never fails: a set with no room for a
 * tag the caller never named is not an error a caller can act on, and the tags that did fit are written.
 */
void RoboTextLigaturePolicyApplyIfUnset(const RoboTextLigaturePolicy *policy, RoboTextFeatureSet *set);

/* ------------------------------------------------------------------ */
/* What the font did                                                   */
/* ------------------------------------------------------------------ */

/**
 * The ligature glyph @p index of @p run, by position among the run's ligature glyphs.
 *
 * @param outGlyphIndex receives the glyph's index in @p run.
 * @return RoboOk; RoboInvalidArgument for a NULL argument; RoboNotFound when @p index is at or past
 *         `RoboTextLigatureReport`'s count — a ligature-free run answers `RoboNotFound` for index 0 rather
 *         than reporting a glyph that is not one.
 *
 * The glyph itself is read with `RoboTextGlyphRunGlyph`: its `source` is every scalar it stands for (three,
 * for `ffi`) and its `ligatureComponents` is how many participated, which is the difference between "this
 * glyph covers three scalars because the font ligated them" and a decomposition's one-scalar-many-glyphs.
 */
Result RoboTextGlyphRunLigatureAt(const RoboTextGlyphRun *run,
                                  size_t index,
                                  RoboTextGlyphIndex *outGlyphIndex,
                                  RoboTextDiag *diag);

/** One number per ligature question, for a dump or an assertion. */
typedef struct RoboTextLigatureReport {
    /** Glyphs the font chose for several scalars at once. Zero for a text with no ligature. */
    size_t ligatureGlyphs;
    /** Scalars those glyphs stand for. `3` for one `ffi`. */
    size_t absorbedScalars;
    /** The largest component count in the run: 3 for `ffi`, 0 when nothing ligated. */
    size_t maxComponents;
    /** Substituted glyphs of any kind, ligatures included. */
    size_t substitutedGlyphs;
    /** Glyphs in the run. */
    size_t glyphs;
    /** Scalars the run covers. A ligature makes these two disagree, which is the point. */
    RoboTextScalarIndex scalars;
} RoboTextLigatureReport;

/** Fill @p out from @p run. RoboInvalidArgument for NULL arguments. Safe on an empty run. */
Result RoboTextLigatureReportForRun(const RoboTextGlyphRun *run, RoboTextLigatureReport *out);

/** `"ligatures=2 absorbed=6 max=3 glyphs=5 scalars=8 substituted=2"`. */
Result RoboTextLigatureReportWrite(const RoboTextLigatureReport *report,
                                   char *buffer,
                                   size_t capacity,
                                   size_t *outLength);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_TEXT_LIGATURE_H */
