/*
 * RoboText's glyph run, seen from inside the module (Phase 12.14).
 *
 * `RoboTextGlyphRun` is opaque to callers — `RoboTextGlyphs.h` hands out accessors and a pointer — and this
 * header is where the layout is written down, for the one reason an opaque type ever needs an internal
 * header: a second file in the same module has to *build* one. `RoboTextRunList.c` merges two adjacent runs
 * into one (`RoboTextGlyphRunListCoalesce`) and cannot do that through a public accessor, because the
 * operation is a concatenation of two arrays and a re-derivation of everything derived from them.
 *
 * It is not a public interface. Nothing outside `Runtime/Source/RoboText/` may include it, and the two doors
 * below are the whole of what the run list is allowed to do:
 *
 *   RoboTextGlyphRunFinalize  re-derive the cluster array, the glyph flags, the run's metrics and its own
 *                             totals from the glyph array. Generation calls it once, at the end; a merge
 *                             calls it once, after the concatenation. It is the *only* writer of those
 *                             fields, which is what keeps "the invariants hold" a property of one function.
 *   RoboTextGlyphRunConcat    one run whose glyphs are @p first's then @p second's.
 *
 * ## Why the derived fields live on the run rather than being computed per call
 *
 * A cluster, a flag word, an ascent and a total are each a function of the glyph array, so a reader could
 * start from the glyphs every time. It does not, for the reason measurement does not: a line-breaking pass
 * asks a run for its clusters once per candidate break, so recomputing them per call would put an O(glyphs)
 * walk behind an accessor that reads like a field. So the run carries them, and `RoboTextGlyphRunValidate`
 * is the door that checks they agree with the glyphs — in a test, not on the hot path.
 */
#ifndef ROBO_TEXT_GLYPHS_INTERNAL_H
#define ROBO_TEXT_GLYPHS_INTERNAL_H

#include "RoboBase/RoboError.h"
#include "RoboText/RoboTextDiagnostics.h"
#include "RoboText/RoboTextFont.h"
#include "RoboText/RoboTextGlyphs.h"
#include "RoboText/RoboTextRuns.h"
#include "RoboText/RoboTextTypes.h"

#include <stddef.h>
#include <stdint.h>

/*
 * The run. Every field is either an input copied from the text run it was generated for, or a function of
 * `glyphs` that `RoboTextGlyphRunFinalize` writes. The comment on each says which, because "is this field
 * derived or declared" is the question a reader of a merge has to answer.
 */
struct RoboTextGlyphRun {
    RoboTextGlyph *glyphs;
    size_t count;
    /**
     * Clusters, tiling the glyph array in logical source order (Phase 12.14.6). Derived from `glyphs`:
     * `RoboTextGlyphRunFinalize` groups them, and a merge re-groups the concatenation.
     */
    RoboTextGlyphCluster *clusters;
    size_t clusterCount;
    /** Declared: the text run's range. */
    RoboTextRange source;
    RoboTextScript script;
    RoboTextDirection direction;
    /** The resolved level the run was generated at (Phase 12.8). */
    RoboTextBidiLevel bidiLevel;
    RoboTextDirection baseDirection;
    RoboTextFontContext font;
    RoboTextLanguage language;
    /**
     * The caller's feature settings, copied from the text run (Phase 12.10).
     *
     * Copied rather than pointed at, and carried on the run rather than only on the run set, because the
     * settings are part of what makes two runs *different* — `RoboTextGlyphRunKey` compares them, and a key
     * that had to reach back into a run set would make the run list depend on a lifetime it does not own.
     */
    RoboTextFeatureSet features;
    RoboTextGlyphGenerationMode mode;
    /**
     * The face's metrics at the context's size (Phase 12.14.18), as the context reports them. Zeroed when the
     * face could not be asked — `haveMetrics` is the flag that tells that apart from a face with no metrics,
     * which is the same distinction `RoboTextGlyph::positioned` draws one level down.
     */
    RoboTextFontMetrics metrics;
    int haveMetrics;
    size_t missing;
    /** Glyphs whose id came from a mirrored code point (UAX #9 L4). */
    size_t mirrored;
    /** Glyphs that stand for a bidi formatting control: no id, no advance, nothing drawn. */
    size_t controls;
    /** Glyphs whose id a GSUB lookup produced rather than the `cmap` (Phase 12.10). */
    size_t substituted;
    /** Glyphs a ligature produced: several scalars folded into one glyph (Phase 12.12). */
    size_t ligatures;
    /** Glyphs the face's GPOS positioned (Phase 12.11). */
    size_t positioned;
    /** Glyphs that are a combining mark, by the flag word (Phase 12.14.22). */
    size_t marks;
    /** Glyphs that may not begin a new line (Phase 12.14.23). */
    size_t unsafeToBreak;
    /** Accumulated in `double` from the glyphs' own `float` advances, so a long run's total is the sum of
     *  what the painter accumulates rather than a second rounding of it. */
    double advanceTotal;
    /** See @ref advanceTotal: the vertical total, zero for horizontal text. */
    double advanceTotalY;
};

/**
 * Derive everything a run's glyph array implies: the cluster array, each glyph's flags and cluster index, the
 * run's metrics and its own totals.
 *
 * Idempotent — calling it twice yields the same record — and the only writer of those fields, which is what
 * makes them consistent by construction rather than by two call sites agreeing. O(glyphs).
 *
 * @return RoboOk; RoboInvalidArgument for a NULL run; RoboOutOfMemory when the cluster array cannot be
 *         allocated (in which case the run is left with no clusters rather than half of them);
 *         RoboParseError when the metrics door refuses the face, which is reported rather than answered with
 *         zeroes because a run whose line height is a guess is a run that lays out wrong.
 */
Result RoboTextGlyphRunFinalize(RoboTextGlyphRun *run, RoboTextDiag *diag);

/**
 * One run whose glyphs are @p first's then @p second's, for `RoboTextGlyphRunListCoalesce`.
 *
 * The caller is responsible for the question this cannot answer — whether the two *may* be merged — and asks it
 * with `RoboTextGlyphRunKeyEqual` plus a contiguity check on the source ranges. This door checks only what
 * makes a merge meaningless: that the two agree about everything a glyph cannot restate (font, script,
 * direction, level, language, features and base direction) and that @p first's source ends exactly where
 * @p second's begins. A merge of two runs that disagreed would produce a record whose own key is false.
 *
 * The result is finalized, so its clusters, flags and totals are the concatenation's rather than either
 * input's.
 *
 * @return RoboOk and @p out; RoboInvalidArgument for a NULL argument, a missing font context, disagreeing
 *         inputs or non-contiguous sources — all of which are *reported*, because a caller that merged
 *         regardless would be asking for the record to lie; RoboOutOfMemory.
 */
Result RoboTextGlyphRunConcat(const RoboTextGlyphRun *first,
                              const RoboTextGlyphRun *second,
                              RoboTextGlyphRun **out,
                              RoboTextDiag *diag);

#endif /* ROBO_TEXT_GLYPHS_INTERNAL_H */
