/*
 * RoboText's glyphs (Phase 12.4) — one glyph, a run of them, and the mapping between a glyph and the
 * scalars it came from.
 *
 * ## A glyph is not a scalar, and the record says so
 *
 * The painting path this phase replaces did this, once per scalar:
 *
 *     code point -> FontFaceLookupGlyph -> GfxRasterDrawGlyph -> pen += advance
 *
 * Four facts collapsed into a loop, which is why a ligature, a mark, a kern or a `locl` could not be
 * added without touching the painter. The record below separates them:
 *
 *   glyphId        *what to draw*. An index into the face's glyph space, and after substitution the
 *                  number of these will not equal the number of scalars.
 *   source         *where it came from*. A **range** of scalars and not an index, because that is what
 *                  makes a ligature (`fi` → one glyph whose source is two scalars) and a mark (one
 *                  scalar that contributes no glyph of its own) expressible in the same type as the
 *                  one-to-one case.
 *   advance        *how far the pen moves after it*, already in device pixels.
 *   offset         *where the glyph sits relative to the pen*, in device pixels. Zero for every glyph
 *                  this build produces — nothing positions anything yet — and present because a
 *                  positioning pass that had to add a field to this record would be a positioning pass
 *                  that changed every consumer.
 *
 * `cluster` is the scalar index of the run's first scalar for this glyph's cluster. With no clustering
 * algorithm in this build it is the glyph's own source start, which is the identity mapping: correct,
 * useless for selection, and *stated* rather than implied.
 *
 * ## The advance is a device quantity, and that is the change that matters
 *
 * Today "advance" exists twice: the measurer sums `hmtx` values in font units and scales once, while
 * the painter accumulates `float` device advances glyph by glyph. Two arithmetic chains, one font, and
 * a comment in `RoboPaintDecoration.c` admitting that a run's last glyph may therefore sit a fraction
 * of a pixel from the width its line was justified to.
 *
 * After this phase there is one: a glyph carries its device advance, and the **measurement** is the sum
 * of those advances accumulated in `double` and rounded once at the boundary. The "one rounding per
 * run" property survives (it was never about font units — it was about not rounding per glyph), and the
 * pen the painter accumulates and the width the measurer reports are now sums of the *same numbers*.
 *
 * ## What is deliberately absent
 *
 * No substitution, no positioning, no kerning, no ligatures, no mark attachment, no cluster merging, no
 * cache. A glyph run is generated, consumed and released. `RoboTextCapabilities` reports each absence as
 * a flag, and `RoboTextGlyphGenerationMode` names what *is* implemented so that a caller cannot mistake
 * "one cmap lookup per scalar" for shaping.
 */
#ifndef ROBO_TEXT_GLYPHS_H
#define ROBO_TEXT_GLYPHS_H

#include "RoboBase/RoboError.h"
#include "RoboFont/RoboFontMetrics.h"
#include "RoboFont/RoboFontTypes.h"
#include "RoboText/RoboTextDiagnostics.h"
#include "RoboText/RoboTextFont.h"
#include "RoboText/RoboTextRuns.h"
#include "RoboText/RoboTextTypes.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * How a glyph run's ids were produced.
 *
 * One value today, and the enum exists so that the answer is a thing a caller can *read* rather than a
 * property of which file the code lives in. A substitution pass will add enumerators here; nothing else
 * in the API has to change when it does.
 */
typedef enum RoboTextGlyphGenerationMode {
    /** No mode: an uninitialised record. */
    RoboTextGlyphGenerationNone = 0,
    /** One `cmap` lookup per scalar, in logical order, with no substitution. **This build.** */
    RoboTextGlyphGenerationCmapPerScalar = 1
} RoboTextGlyphGenerationMode;

/** Non-zero if @p mode is a defined enumerator. */
int RoboTextGlyphGenerationModeIsKnown(RoboTextGlyphGenerationMode mode);

/** Name of @p mode (`"cmap-per-scalar"`); never NULL. */
const char *RoboTextGlyphGenerationModeName(RoboTextGlyphGenerationMode mode);

/**
 * One glyph: what to draw, where it came from, and the two vectors that place it.
 *
 * A value with no pointers, so a glyph may be copied, compared and held by a test without a lifetime
 * question.
 */
typedef struct RoboTextGlyph {
    /** The face's glyph id, or `RoboGlyphIdNone` when the face had no glyph for the source scalars. */
    GlyphId glyphId;
    /** The scalars this glyph was generated from; a half-open range, never inverted. */
    RoboTextRange source;
    /** The scalar index of this glyph's cluster. Identity mapping in this build (see the header). */
    RoboTextClusterIndex cluster;
    /** The face's own `hmtx` advance, in font units. Kept so a dump can show the raw number. */
    uint16_t advanceUnits;
    /** The advance the pen moves after this glyph, in device pixels. */
    float advanceX;
    /** The vertical advance. Zero for horizontal text, and present for vertical text (12.20). */
    float advanceY;
    /** Where the glyph sits relative to the pen, in device pixels. Zero in this build. */
    float offsetX;
    /** See @ref offsetX. */
    float offsetY;
    /** Non-zero when the face's cmap claimed the source scalars. */
    int mapped;
    /**
     * Non-zero when the glyph stands for a bidi **formatting control** rather than for a character.
     *
     * The nine controls (`LRE`/`RLE`/`LRO`/`RLO`/`PDF` and `LRI`/`RLI`/`FSI`/`PDI`) do not paint: Phase
     * 12.8 §"formatting characters" says they are logical controls and not visible glyphs, and a control
     * that reached this layer as an ordinary scalar would draw `.notdef` — a box the reader would see and
     * the pen would advance over. So it becomes a glyph with no id, no advance and this flag set, which
     * keeps the array one-per-scalar (and therefore the source mapping exact) while telling a painter to
     * draw nothing. Such a glyph is **not** counted in @ref RoboTextGlyphStats::missing — it is not a
     * character the face failed to cover.
     */
    int control;
    /**
     * Non-zero when the id was looked up from the **mirrored** code point rather than from the scalar.
     *
     * UAX #9 L4: a scalar whose resolved level is odd is depicted by its `Bidi_Mirroring_Glyph`, so an
     * `(` in right-to-left text draws as `)`. The mirroring is data (`RoboUnicode/RoboBidiMirror.h`) and
     * this flag is the record of it — the *source* is untouched, which is what makes selection, copy and
     * accessibility still see `(` (Phase 12.8 §"mirroring and glyph mapping"). Phase 12.10's shaping will
     * reach the same result through a font's alternate glyphs; until then this is how L4 is honoured.
     */
    int mirrored;
    /** The metrics door's status: three ways a glyph can have no advance, none of them zero. */
    FontGlyphMetricsStatus metricsStatus;
} RoboTextGlyph;

/** Clear @p glyph to a null glyph: no id, empty source, zero vectors. Safe on NULL. */
void RoboTextGlyphInit(RoboTextGlyph *glyph);

/** Non-zero when @p glyph names a drawable id and a well-formed source range. Safe on NULL. */
int RoboTextGlyphIsValid(const RoboTextGlyph *glyph);

/** Field-for-field equality. Safe on NULL (NULL == NULL). */
int RoboTextGlyphEqual(const RoboTextGlyph *a, const RoboTextGlyph *b);

/* ------------------------------------------------------------------ */
/* A glyph run                                                         */
/* ------------------------------------------------------------------ */

typedef struct RoboTextGlyphRun RoboTextGlyphRun;

/**
 * Generate the glyph run for one text run.
 *
 * The generator is the placeholder this phase names: one cmap lookup per scalar, in logical order, with
 * no substitution and no positioning. Its output is nonetheless *correct* for the text it claims to
 * handle, and every number in it comes from the face.
 *
 * `run->direction` is carried into the glyph run and does **not** reorder anything: the glyphs are in
 * logical order, and painting them in that order from the run's origin is right-to-left rendering only
 * in the sense that the caller places the run's origin. Reordering is Phase 12.8's.
 *
 * @return RoboOk and @p out; RoboInvalidArgument for a NULL argument or a run whose range does not lie
 *         in @p view; RoboNotSupported when the run's context has no face — which is reported rather
 *         than answered with an empty run, because "no glyphs" and "no font" are different states and a
 *         caller that conflated them would draw a blank where it should have said so; RoboOutOfMemory.
 */
Result RoboTextGlyphRunGenerate(RoboTextView view,
                                const RoboTextRun *run,
                                RoboTextGlyphRun **out,
                                RoboTextDiag *diag);

/** Release @p run. Safe on NULL. */
void RoboTextGlyphRunDestroy(RoboTextGlyphRun *run);

/** Glyphs in the run. */
size_t RoboTextGlyphRunCount(const RoboTextGlyphRun *run);

/** Glyph @p index, or NULL when out of range. */
const RoboTextGlyph *RoboTextGlyphRunGlyph(const RoboTextGlyphRun *run, RoboTextGlyphIndex index);

/**
 * The scalars the run covers.
 *
 * Mirroring does not appear here: a glyph generated from a mirrored code point still names the scalar it
 * came from, so this is the logical source range in every case (Phase 12.8 §"mirroring and glyph
 * mapping").
 */
RoboTextRange RoboTextGlyphRunSourceRange(const RoboTextGlyphRun *run);

/** The script the run was generated for. */
RoboTextScript RoboTextGlyphRunScript(const RoboTextGlyphRun *run);

/** The direction that applies to the run — the resolved direction of its level (Phase 12.8). */
RoboTextDirection RoboTextGlyphRunDirection(const RoboTextGlyphRun *run);

/**
 * The resolved bidi level the run was generated at (Phase 12.8), or `RoboTextBidiLevelNone`.
 *
 * Carried for diagnostics and for the cursor/selection work that follows: a caret needs to know which
 * level's direction it is moving *within*, which a single direction per run cannot say when a line holds
 * two levels of the same parity.
 */
RoboTextBidiLevel RoboTextGlyphRunBidiLevel(const RoboTextGlyphRun *run);

/**
 * Glyphs whose id came from a mirrored code point (UAX #9 L4) rather than from the scalar.
 *
 * Not a defect count: it is the L4 record, and the glyphs' **sources are unaffected** by it — mapping a
 * glyph back to the text answers `(` for a glyph that drew `)`.
 */
size_t RoboTextGlyphRunMirrored(const RoboTextGlyphRun *run);

/**
 * Glyphs that stand for a bidi formatting control — the ones a painter draws nothing for.
 *
 * Read @ref RoboTextGlyph::control per glyph rather than searching for a zero id: `.notdef` is id 0 as
 * well, and an unmapped *character* is drawn and advanced over deliberately (`RoboTextGlyphs.c` explains
 * why). The two facts are different and only one of them means "do not draw".
 */
size_t RoboTextGlyphRunControls(const RoboTextGlyphRun *run);

/** The direction the caller declared for the whole text. */
RoboTextDirection RoboTextGlyphRunBaseDirection(const RoboTextGlyphRun *run);

/** The font the run is set in; borrowed. Never NULL for a run that was generated. */
const RoboTextFontContext *RoboTextGlyphRunFont(const RoboTextGlyphRun *run);

/** The language the run carries. Never NULL. */
const RoboTextLanguage *RoboTextGlyphRunLanguage(const RoboTextGlyphRun *run);

/** The mode the ids were produced in. */
RoboTextGlyphGenerationMode RoboTextGlyphRunGenerationMode(const RoboTextGlyphRun *run);

/** Scalars the run covered, which is its source range's length. */
RoboTextScalarIndex RoboTextGlyphRunScalarCount(const RoboTextGlyphRun *run);

/** Scalars the face had no glyph for. Never zero-by-accident: `.notdef` is a drawable id. */
size_t RoboTextGlyphRunMissingGlyphs(const RoboTextGlyphRun *run);

/**
 * The run's total advance, in device pixels.
 *
 * Summed in `double` from the glyphs' own `advanceX` and returned as the width the run occupies — the
 * one quantity a measurer and a painter must agree about, and the reason it is computed here rather
 * than in each of them.
 */
float RoboTextGlyphRunAdvance(const RoboTextGlyphRun *run);

/**
 * The advance before glyph @p index: the pen position that glyph is drawn at, relative to the run's
 * origin.
 *
 * The prefix sum a caret needs and a painter could accumulate — exposed because a caret is the next
 * thing that will ask, and because a prefix sum computed by the *run* cannot disagree with the pen the
 * painter's own accumulation produces.
 *
 * @return RoboOk and @p outPx; RoboInvalidArgument for a NULL argument or an index past the glyph
 *         count (the count itself is legal and yields the whole advance).
 */
Result RoboTextGlyphRunAdvanceUpTo(const RoboTextGlyphRun *run,
                                   RoboTextGlyphIndex index,
                                   float *outPx,
                                   RoboTextDiag *diag);

/* ------------------------------------------------------------------ */
/* Source mapping                                                      */
/* ------------------------------------------------------------------ */

/**
 * The scalars glyph @p index was generated from.
 *
 * The glyph record carries its own `source`, so this is the door a caller uses when it has an index and
 * not a pointer — a hit test, a selection end, a diagnostic.
 *
 * @return RoboOk and @p out; RoboInvalidArgument for a NULL argument or an index past the run.
 */
Result RoboTextGlyphRunSourceOf(const RoboTextGlyphRun *run,
                                RoboTextGlyphIndex index,
                                RoboTextRange *out,
                                RoboTextDiag *diag);

/**
 * The glyphs generated from scalar @p scalar.
 *
 * One glyph in this build, always — a later substitution pass may answer with none (a mark that a
 * combining lookup consumed into a base) or with several (a decomposition), which is why the answer is a
 * count and a first index rather than one index.
 *
 * @return RoboOk with @p outFirstIndex and @p outCount set (@p outCount may be 0, which is a legal
 *         answer from a later generator and is reported rather than treated as a miss);
 *         RoboInvalidArgument for a NULL argument or a scalar outside the run's source range.
 */
Result RoboTextGlyphRunGlyphsForScalar(const RoboTextGlyphRun *run,
                                       RoboTextScalarIndex scalar,
                                       RoboTextGlyphIndex *outFirstIndex,
                                       RoboTextGlyphIndex *outCount,
                                       RoboTextDiag *diag);

/**
 * The glyph covering @p index of the run's own source: the inverse of the door above, for one scalar.
 *
 * A convenience over `GlyphsForScalar` that answers the *first* glyph and its index, because that is
 * what "which glyph is the caret next to" resolves to, and doing it here keeps three callers from each
 * picking a different end of a multi-glyph answer.
 *
 * @return RoboOk and @p outGlyphIndex; RoboInvalidArgument for a NULL argument or an index outside the
 *         run; RoboNotFound when no glyph covers it.
 */
Result RoboTextGlyphRunGlyphForScalarIndex(const RoboTextGlyphRun *run,
                                           RoboTextScalarIndex index,
                                           RoboTextGlyphIndex *outGlyphIndex,
                                           RoboTextDiag *diag);

/** Counter record for a run or a shape result, for a dump or an assertion. */
typedef struct RoboTextGlyphStats {
    size_t glyphRuns;
    size_t glyphs;
    /** Glyphs whose id is `.notdef` or whose metrics would not parse. */
    size_t missing;
    /** Glyphs whose source range covers more than one scalar. Zero in this build (no ligatures). */
    size_t multiScalarSources;
    /** Glyphs that carry a non-zero offset. Zero in this build (no positioning). */
    size_t positioned;
    /** Glyphs whose id came from a mirrored code point (UAX #9 L4 applies to their scalar). */
    size_t mirrored;
    /** Glyphs that stand for a bidi formatting control: no id, no advance, nothing drawn. */
    size_t controls;
    /** Total advance across every run, in device pixels. */
    double advancePx;
} RoboTextGlyphStats;

/** Fill @p out with @p run's counters. RoboInvalidArgument for NULL arguments. */
Result RoboTextGlyphRunGetStats(const RoboTextGlyphRun *run, RoboTextGlyphStats *out);

/**
 * Human-readable one-line dump of a glyph, for the visual test surface the phase asks for.
 *
 * Writes `id=<id> scalar=<n>..<n> advance=<f> units=<n> offset=<f>,<f> mapped=<0|1> mirrored=<0|1>`.
 * The glyph's index
 * in its run is deliberately **not** part of the line: the record does not carry one (a glyph does not
 * know where it sits), and a caller dumping a run is already walking the indices.
 *
 * RoboNotSupported when the line does not fit @p capacity, with @p outLength receiving the length it
 * needed — a truncated dump line reads as a different glyph.
 */
Result RoboTextGlyphWrite(const RoboTextGlyph *glyph,
                          char *buffer,
                          size_t capacity,
                          size_t *outLength);

/** Fuzz entry: generate glyph runs over arbitrary bytes with no face. Never crashes. */
Result RoboTextFuzzGlyphs(const uint8_t *data, size_t size);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_TEXT_GLYPHS_H */
