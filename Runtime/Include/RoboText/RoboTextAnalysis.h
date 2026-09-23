/*
 * RoboText's analysis runs (Phase 12.5) — the object the canonical data flow ends at:
 *
 *     TextBuffer → decoder → code point → { properties, script, direction } → font selection
 *                                                                                    │
 *                                                                          RoboTextAnalysisRun
 *                                                                                    │
 *                                                                          RoboTextShapeRequest
 *
 * ## What an analysis run is, and what it is not
 *
 * An analysis run is the **co-occurrence** of three independent classifications over one stretch of text:
 * one script, one classified direction, and one selected font. It is the unit a shaper will consume, and it
 * explicitly contains **no glyphs**: substitution is Phase 12.10 and positioning 12.11, and a record that
 * carried glyph ids here would be a record those goals would have to change rather than fill.
 *
 * The three boundaries are not a merge of the three segmentations; they are the *product* of them, because a
 * run is where all three are constant. `"Hello 世界"` on a Latin-only primary face is therefore:
 *
 *     Run 0: [0, 6)  Latin  LTR  primary Inter
 *     Run 1: [6, 8)  Han    LTR  no covering face (the fallback chain did not have one)
 *
 * and with a Han-capable fallback the second run's source would be `RoboTextFontSourceFallback`. The ranges
 * are kept in scalars — not in glyphs and not in bytes — so a selection, a caret and a hit test can all
 * address the same text the runs came from.
 *
 * ## The font is chosen for a run, not for a scalar
 *
 * The font is selected **once per script run** and then shared by every scalar in it, so a run's font is
 * constant by construction. That is a 12.6 change and it subsumes 12.5's rule that a combining mark takes
 * the font of the scalar before it: a mark resolves into its base's script run (the resolver attaches an
 * Inherited scalar left), and the run is selected as one unit, so a base character and its accent cannot be
 * two faces. `"e" + U+0301` is therefore one run even when U+0301's own coverage answer, asked scalar by
 * scalar, would have been a different face — which is the input a later `mark` attachment needs.
 *
 * ## The shape request is fully populated, and it is a real value
 *
 * `RoboTextShapeRequest` is the 12.4 structure the phase asks to fill from real data: it carries the text
 * view, the scalar range, the selected font **as a `RoboTextFontContext`** (face plus setting), the script,
 * the classified direction, the declared paragraph direction, the language tag and the feature list.
 * `RoboTextShapeRequestToRun` turns one into the `RoboTextRun` the glyph generator already consumes, so the
 * analysis path reaches the existing generation pipeline with no second shaper in between.
 *
 * ## Language does not select script
 *
 * The language tag travels on the request and is echoed into every run; nothing in this file reads it. A
 * `"ur"` tagged string of Latin letters is Latin, because the script comes from the Unicode data and the
 * tag comes from the author. Phase 12.10's `locl` lookup and a language-aware fallback preference will
 * read it; both are later goals and neither may be anticipated by guessing here.
 */
#ifndef ROBO_TEXT_ANALYSIS_H
#define ROBO_TEXT_ANALYSIS_H

#include "RoboBase/RoboError.h"
#include "RoboText/RoboTextBidi.h"
#include "RoboText/RoboTextDiagnostics.h"
#include "RoboText/RoboTextFont.h"
#include "RoboText/RoboTextFontSelection.h"
#include "RoboText/RoboTextRuns.h"
#include "RoboText/RoboTextTypes.h"
#include "RoboText/RoboTextUnicode.h"

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------ */
/* An analysis run                                                     */
/* ------------------------------------------------------------------ */

/** One stretch of text sharing a script, a classified direction and a selected font. */
typedef struct RoboTextAnalysisRun {
    /** The scalars the run covers, in the text's own index space. */
    RoboTextRange range;
    /** The script every scalar in the run resolves to. */
    RoboTextScript script;
    /**
     * The direction that applies to the run — the **resolved** direction since 12.8.
     *
     * 12.7 classified this; UAX #9 resolves it, and a shaper is entitled to the latter (Phase 12.8 §"do
     * not use only `ScriptRun.direction` after 12.8"). The classification is not lost: the direction runs
     * beside these still carry 12.7's answer, `Unknown` included.
     */
    RoboTextDirection direction;
    /** The resolved bidi level of the run's first scalar (Phase 12.8). */
    RoboTextBidiLevel bidiLevel;
    /** The font selected for the run, and where that selection came from. */
    RoboTextResolvedFont font;
} RoboTextAnalysisRun;

/* ------------------------------------------------------------------ */
/* The shape request                                                   */
/* ------------------------------------------------------------------ */

/**
 * Everything a shaper needs, and nothing it does not.
 *
 * A value with no ownership: `text` is borrowed and must outlive the request. `font` is a context (a
 * borrowed face plus the setting), so the request can be copied into a queue, compared and stored without a
 * lifetime question.
 */
typedef struct RoboTextShapeRequest {
    /** The text the range indexes. Borrowed. */
    RoboTextView text;
    /** The scalars to shape. */
    RoboTextRange range;
    /** The selected face and the setting it was selected at. */
    RoboTextFontContext font;
    /** The script classification for the range. */
    RoboTextScript script;
    /** The direction that applies to the range: the resolved direction of its level. */
    RoboTextDirection direction;
    /**
     * The resolved bidi level of the range's first scalar (Phase 12.8), or `RoboTextBidiLevelNone` when
     * the request was not built from a resolved run.
     *
     * The paragraph's *base* level is deliberately **not** here: it is a property of the paragraph rather
     * than of a run, and the paragraph this request came from is one call away (the run set and the
     * analysis both publish theirs).
     */
    RoboTextBidiLevel bidiLevel;
    /** The direction the caller declared for the whole text. */
    RoboTextDirection baseDirection;
    /** The declared language, carried and not interpreted. */
    RoboTextLanguage language;
    /**
     * The caller's OpenType feature settings (Phase 12.10).
     *
     * A shaper reads the *features*, not only the script, because the same tag means different lookups in
     * different fonts and only the font knows which: the set below is what the caller asked for, and the
     * resolution against the face's own ScriptList/FeatureList happens one layer down.
     */
    RoboTextFeatureSet features;
} RoboTextShapeRequest;

/** Clear @p request to "nothing asked": no text, no font, no features. Safe on NULL. */
void RoboTextShapeRequestInit(RoboTextShapeRequest *request);

/** Non-zero when @p request names a text, a range inside it and a face. Safe on NULL. */
int RoboTextShapeRequestIsUsable(const RoboTextShapeRequest *request);

/**
 * Build the request for one analysis run.
 *
 * `style` is the analysis's requested setting (the selector's primary style) and is adjusted field by
 * field to the run's own recorded weight/style/stretch, so the request agrees with the run's identity
 * rather than with what was asked for. `options` may be NULL; its feature list is copied when present.
 *
 * @return RoboOk; RoboInvalidArgument for a NULL @p run, @p style or @p out, or a range that is inverted.
 */
Result RoboTextShapeRequestFromRun(const RoboTextAnalysisRun *run,
                                   RoboTextView text,
                                   const RoboTextFontStyle *style,
                                   const RoboTextLanguage *language,
                                   RoboTextDirection baseDirection,
                                   const RoboTextShapeOptions *options,
                                   RoboTextShapeRequest *out);

/**
 * The `RoboTextRun` a request denotes.
 *
 * The bridge to the Phase 12.4 pipeline: the run this produces is exactly what `RoboTextGlyphRunGenerate`
 * consumes, which is what makes the analysis path reach real glyphs without a second generator.
 *
 * @return RoboOk; RoboInvalidArgument for a NULL argument.
 */
Result RoboTextShapeRequestToRun(const RoboTextShapeRequest *request, RoboTextRun *out);

/* ------------------------------------------------------------------ */
/* The analysis                                                        */
/* ------------------------------------------------------------------ */

typedef struct RoboTextAnalysis RoboTextAnalysis;

/**
 * Classify @p view and select a font for every script run.
 *
 * The one call that turns text plus a selector into runs. It decodes once, resolves the scripts once, works
 * the paragraph direction out once, selects a font per **script run** (through the selector's memo) and then
 * emits the maximal spans over which script, classified direction and font all agree.
 *
 * @param view     the text. Borrowed; it must outlive the analysis.
 * @param selector the font policy. Borrowed; it must outlive the analysis and its fallback chain must too.
 * @param options  declared direction, script hint and language. NULL means the defaults.
 * @param out      receives a new analysis the caller destroys.
 *
 * @return RoboOk; RoboInvalidArgument for a NULL @p view-as-bytes, @p selector or @p out; RoboError when a
 *         candidate's `cmap` will not parse; RoboOutOfMemory. An **empty** view produces an empty analysis
 *         rather than a failure, exactly as an empty run set does.
 */
Result RoboTextAnalysisCreate(RoboTextView view,
                              RoboTextFontSelector *selector,
                              const RoboTextShapeOptions *options,
                              RoboTextAnalysis **out,
                              RoboTextDiag *diag);

/** Release @p analysis. Safe on NULL. */
void RoboTextAnalysisDestroy(RoboTextAnalysis *analysis);

/** Analysis runs, in ascending text order. */
size_t RoboTextAnalysisRunCount(const RoboTextAnalysis *analysis);

/** Run @p index, or NULL when out of range. */
const RoboTextAnalysisRun *RoboTextAnalysisRunAt(const RoboTextAnalysis *analysis, size_t index);

/** The run covering scalar @p index, or NULL at the end-of-text position. */
const RoboTextAnalysisRun *RoboTextAnalysisRunAtScalar(const RoboTextAnalysis *analysis,
                                                       RoboTextScalarIndex index);

/** Script runs, from the shared resolver. */
size_t RoboTextAnalysisScriptRunCount(const RoboTextAnalysis *analysis);

/** Script run @p index, or NULL when out of range. */
const RoboTextScriptRun *RoboTextAnalysisScriptRun(const RoboTextAnalysis *analysis, size_t index);

/**
 * Direction runs, from the direction resolver (Phase 12.7) — the **classification**.
 *
 * Since 12.8 the analysis runs are cut against the *resolution* (the levels) rather than against this
 * array, so the two are no longer required to agree boundary for boundary — and they do not, in exactly
 * the cases where the algorithm adds information. What this array is, unchanged, is 12.7's answer: the
 * class projection, the paragraph context and the neutral rules, with `RoboTextDirectionUnknown` a real
 * state. It stays public because a caller debugging a mixed-direction line wants to see both readings.
 */
size_t RoboTextAnalysisDirectionRunCount(const RoboTextAnalysis *analysis);

/** Direction run @p index, or NULL when out of range. */
const RoboTextDirectionRun *RoboTextAnalysisDirectionRun(const RoboTextAnalysis *analysis, size_t index);

/* --- Phase 12.8: the resolution ---------------------------------------------------------- */

/**
 * The bidi paragraph the analysis was resolved with: levels, logical runs and L2's visual order.
 *
 * Borrowed and owned by the analysis. Never NULL for an analysis this module produced, including an empty
 * one, so `RoboTextBidiParagraphBaseLevel` always answers.
 */
const RoboTextBidiParagraph *RoboTextAnalysisBidiParagraph(const RoboTextAnalysis *analysis);

/**
 * The **level runs**: the algorithm's own segmentation, beside the script and direction runs.
 *
 * An analysis run is cut where the script or the resolved *direction* changes; these are cut where the
 * *level* changes. The two differ only where a level moves without changing the parity — so the analysis
 * run is the shaping unit and the level run is the algorithm's own boundary. A consumer that needs the
 * latter — a caret, a mirrored glyph, a diagnostic — reads these.
 */
size_t RoboTextAnalysisBidiRunCount(const RoboTextAnalysis *analysis);

/** Level run @p index, or NULL when out of range. */
const RoboTextBidiRun *RoboTextAnalysisBidiRun(const RoboTextAnalysis *analysis, size_t index);

/**
 * The **visual** order of the level runs (L2), as logical level-run indices — what a painter walks.
 *
 * @return RoboOk; RoboInvalidArgument for a NULL argument or @p visual past the last run.
 */
Result RoboTextAnalysisVisualBidiRun(const RoboTextAnalysis *analysis,
                                     size_t visual,
                                     size_t *outLogicalRunIndex,
                                     RoboTextDiag *diag);

/** The resolved bidi level of @p scalar, or @ref RoboTextBidiLevelRemoved when it has none. */
RoboTextBidiLevel RoboTextAnalysisBidiLevelAt(const RoboTextAnalysis *analysis,
                                              RoboTextScalarIndex scalar);

/**
 * The context the analysis was classified in, and the paragraph direction it resolved to.
 *
 * Fills @p out (which may be NULL) with the context the runs were resolved against and returns the
 * resolved paragraph direction — the value @ref RoboTextAnalysisBaseDirection also answers, so a caller
 * that wants both reads them in one call. A NULL @p analysis answers `Unknown` and leaves @p out at the
 * default context.
 */
RoboTextDirection RoboTextAnalysisDirectionContext(const RoboTextAnalysis *analysis,
                                                   RoboTextDirectionContext *out);

/** Scalars analysed. */
RoboTextScalarIndex RoboTextAnalysisScalarCount(const RoboTextAnalysis *analysis);

/** The paragraph direction the analysis was classified against. */
RoboTextDirection RoboTextAnalysisBaseDirection(const RoboTextAnalysis *analysis);

/** The declared script hint, or `RoboTextScriptCommon`. */
RoboTextScript RoboTextAnalysisScriptHint(const RoboTextAnalysis *analysis);

/** The declared language. Never NULL. */
const RoboTextLanguage *RoboTextAnalysisLanguage(const RoboTextAnalysis *analysis);

/** The requested setting every run was selected at. Never NULL. */
const RoboTextFontStyle *RoboTextAnalysisStyle(const RoboTextAnalysis *analysis);

/** Counters for a dump or an assertion. RoboInvalidArgument for NULL arguments. */
typedef struct RoboTextAnalysisStats {
    size_t runs;
    /** Runs whose font came from the requested primary. */
    size_t primaryRuns;
    /** Runs whose font came from the fallback chain. */
    size_t fallbackRuns;
    /** Runs no candidate covered. */
    size_t unresolvedRuns;
    /** Script runs the resolver produced. */
    size_t scriptRuns;
    /** Direction runs the resolver produced (Phase 12.7) — the classification. */
    size_t directionRuns;
    /** Level runs the bidi stage produced (Phase 12.8) — the resolution. */
    size_t bidiRuns;
    /** The resolved paragraph level: 0 or 1. */
    RoboTextBidiLevel baseLevel;
    /** Scalars X9 removed from reordering (format controls and `BN`). */
    size_t removedScalars;
    /** Scalars whose resolved level is odd. */
    size_t rtlScalars;
    /** Scalars that are right-to-left and have a mirroring glyph (L4 applies to them). */
    size_t mirroredScalars;
    /** Runs whose resolved direction is RTL. */
    size_t rtlRuns;
    /** Combining marks seen (and folded into their base's font). */
    size_t combiningMarks;
    /** Scalars analysed. */
    RoboTextScalarIndex scalars;
} RoboTextAnalysisStats;

Result RoboTextAnalysisGetStats(const RoboTextAnalysis *analysis, RoboTextAnalysisStats *out);

/**
 * The shape request for run @p index.
 *
 * @return RoboOk; RoboInvalidArgument for a NULL argument or an index past the last run; RoboOutOfMemory.
 */
Result RoboTextAnalysisShapeRequest(const RoboTextAnalysis *analysis,
                                    size_t index,
                                    RoboTextShapeRequest *out);

/** Fuzz entry: analyse arbitrary bytes with a selector that names no face. Never crashes. */
Result RoboTextFuzzAnalysis(const uint8_t *data, size_t size);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_TEXT_ANALYSIS_H */
