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
 * No grapheme segmentation (the cluster rule is "a combining mark attaches left", and it is named as the
 * approximation it is), no shaping cache, no line breaking. A glyph run is generated, consumed and released.
 * `RoboTextCapabilities` reports each absence as a flag, and `RoboTextGlyphGenerationMode` names what *is*
 * implemented so that a caller cannot mistake "one cmap lookup per scalar" for shaping.
 *
 * ## Substitution (Phase 12.10)
 *
 * The cmap pass produces one glyph per scalar. When the face carries a GSUB table and the run's script,
 * language and features turn any of its lookups on, those lookups then run — through `RoboTextGsub.h`, on a
 * shaping buffer, and the run is **rebuilt** from the buffer's result. Rebuilding is what the glyph count
 * changing costs: a ligature is one glyph where the pass made three, a multiple substitution is three where
 * it made one, and a bidi control keeps its slot through both so that every source position still has one.
 *
 * The rebuild is why a glyph's device advance is computed from *its own* id: the substitute a font chose
 * carries the font's advance for that glyph, and reusing the input glyph's advance would put the pen a
 * millimetre out on a ligature — the one number substitution changes that a reader would notice.
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
    /** One `cmap` lookup per scalar, in logical order, with no substitution. */
    RoboTextGlyphGenerationCmapPerScalar = 1,
    /**
     * One `cmap` lookup per scalar, then the face's own GSUB lookups. **This build**, for a face that has
     * substitution to apply to this run.
     *
     * The two modes are not two code paths: `Gsub` means the map pass ran *and* a plan resolved against the
     * face was applied to its result. A run whose face has no GSUB, or none for its script, is
     * `CmapPerScalar` — which is a fact about the font rather than about the text, and the reason the mode is
     * a value a caller can read instead of an assumption.
     */
    RoboTextGlyphGenerationGsub = 2,
    /**
     * The same, then the face's own GPOS lookups. **This build**, for a face that has positioning to apply
     * to this run — which is most of them: a UI face kerns its capital pairs, and 12.11 is what turns that
     * into an advance the pen and the measurer both see.
     *
     * The mode is the *furthest* stage that ran, so a run of a face with GPOS and no GSUB is `Gpos` and a
     * run of a face with GSUB and no GPOS is `Gsub`. A face with neither, or none for this run's script, is
     * `CmapPerScalar` — which is a fact about the font rather than about the text.
     */
    RoboTextGlyphGenerationGpos = 3
} RoboTextGlyphGenerationMode;

/** Non-zero if @p mode is a defined enumerator. */
int RoboTextGlyphGenerationModeIsKnown(RoboTextGlyphGenerationMode mode);

/** Name of @p mode (`"cmap-per-scalar"`); never NULL. */
const char *RoboTextGlyphGenerationModeName(RoboTextGlyphGenerationMode mode);

/**
 * The facts about a glyph that a reader needs to *group* it with its neighbours (Phase 12.14.22).
 *
 * Six flags and not twenty. Each is a question a later stage asks and could not answer from the glyph's id
 * and source alone:
 *
 *   ClusterStart / ClusterContinuation  which glyph begins the cluster this one belongs to. A cluster is a
 *                                       source range and a glyph range, so "where do I split" is a question
 *                                       only the second glyph of a cluster can answer wrongly.
 *   Mark                                a glyph that is a combining mark: it attaches to the glyph before
 *                                       it, so it may not begin a line and may not be separated from its base.
 *   Ligature                            a glyph several scalars folded into. `ligatureComponents >= 2` says
 *                                       the same thing; the flag is here because the *group* of facts a reader
 *                                       wants is this one, and a reader that had to know which field to test
 *                                       for each of six facts would eventually test the wrong one.
 *   UnsafeToBreak                       do not let a line begin at this glyph (Phase 12.14.23).
 *   UnsafeToConcat                      do not shape across this boundary: concatenating another run here
 *                                       could change this run's shaping (Phase 12.14.23).
 *
 * What is deliberately **not** here: `control`, `mirrored`, `mapped`, `substituted` and `positioned` are
 * facts about a glyph's *id*, and each already has its own field below. Folding them into this word would
 * state five of them twice, and a record that states a fact twice is a record that can contradict itself.
 */
typedef enum RoboTextGlyphFlag {
    /** No flags: an uninitialised or flagless glyph. */
    RoboTextGlyphFlagNone = 0,
    /** The first glyph of its cluster. */
    RoboTextGlyphFlagClusterStart = 1u << 0,
    /** A later glyph of its cluster: the same source, the next glyph. */
    RoboTextGlyphFlagClusterContinuation = 1u << 1,
    /** A combining mark, by the source scalar's `General_Category` being `Mn`, `Mc` or `Me`. */
    RoboTextGlyphFlagMark = 1u << 2,
    /** A glyph several scalars were folded into: see @ref RoboTextGlyph::ligatureComponents. */
    RoboTextGlyphFlagLigature = 1u << 3,
    /** A line may not begin at this glyph. See @ref RoboTextGlyphRunUnsafeToBreak. */
    RoboTextGlyphFlagUnsafeToBreak = 1u << 4,
    /** Another run's glyphs may not be shaped against this glyph's boundary. */
    RoboTextGlyphFlagUnsafeToConcat = 1u << 5
} RoboTextGlyphFlag;

/** A glyph's flags, as a bit word of @ref RoboTextGlyphFlag. */
typedef uint32_t RoboTextGlyphFlags;

/** Non-zero when @p flag is a defined single flag. */
int RoboTextGlyphFlagIsKnown(RoboTextGlyphFlag flag);

/** Name of a single @p flag (`"cluster-start"`); `"none"` for @ref RoboTextGlyphFlagNone. Never NULL. */
const char *RoboTextGlyphFlagName(RoboTextGlyphFlag flag);

/**
 * @p flags as `"cluster-start|mark"`, into @p buffer.
 *
 * Complete or absent, like every dump in this module, and `@` for a word with no flags — a spelling that
 * cannot be confused with a flag's name. @p outLength receives the length a complete word needs either way.
 *
 * @return RoboOk; RoboInvalidArgument for a NULL argument; RoboNotSupported when @p buffer cannot hold the
 *         word.
 */
Result RoboTextGlyphFlagsWrite(RoboTextGlyphFlags flags,
                                char *buffer,
                                size_t capacity,
                                size_t *outLength);

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
    /**
     * The character the glyph came from, or 0 when its source covers more than one scalar (Phase 12.14).
     *
     * @ref source is the scalar *index*; this is the code point at it. A glyph's properties are properties of
     * the character — the one this build reads is `Mark`, from `General_Category` — and after substitution the
     * text is no longer at hand to decode, so the character travels with the glyph. See
     * @ref RoboTextShapingGlyph::codePoint for the two meanings of 0 and how they are told apart.
     */
    RoboTextCodePoint codePoint;
    /** The **cluster** index of this glyph (Phase 12.14.6): which cluster of the run it belongs to. */
    RoboTextClusterIndex cluster;
    /** The face's own `hmtx` advance, in font units. Kept so a dump can show the raw number. */
    uint16_t advanceUnits;
    /** The advance the pen moves after this glyph, in device pixels. */
    float advanceX;
    /** The vertical advance. Zero for horizontal text, and present for vertical text (12.20). */
    float advanceY;
    /** Where the glyph sits relative to the pen, in device pixels. */
    float offsetX;
    /** See @ref offsetX. */
    float offsetY;
    /**
     * The **font's own** part of @ref advanceX: what its GPOS value records added to the `hmtx` advance
     * (Phase 12.11/12.13). `advanceX` is this plus the face's advance, and keeping the two apart is what lets
     * a diagnostic say "the pen moves 12.4px, of which -1.25 is the font's kerning" instead of guessing.
     *
     * Zero for a glyph the font positioned with a placement adjustment only, and for one it said nothing
     * about — @ref positioned is the flag that tells those two apart, never this number.
     */
    float advanceAdjustX;
    /** See @ref advanceAdjustX. */
    float advanceAdjustY;
    /** The font's own part of @ref offsetX: a placement adjustment, in device pixels. */
    float offsetAdjustX;
    /** See @ref offsetAdjustX. */
    float offsetAdjustY;
    /**
     * Non-zero when the face's GPOS positioned this glyph (Phase 12.11): it carries a placement offset, an
     * advance adjustment, or a value record the font wrote for it.
     *
     * Distinct from "`offsetX` is non-zero", because the two questions are different: a kerning pair moves
     * the *advance* and leaves the placement at zero, and a dump that inferred "the font said nothing" from
     * an offset of zero would report a kerned pair as unpositioned.
     */
    int positioned;
    /**
     * Non-zero when a GSUB lookup chose this id rather than the `cmap` (Phase 12.10).
     *
     * The reason this is a field rather than something a caller infers from
     * @ref RoboTextGlyphRunGenerationMode: a ligature's product and the glyphs around it are one run, and
     * only *some* of them were substituted. A diagnostic that wanted to know which would otherwise have to
     * re-run the lookups.
     */
    int substituted;
    /**
     * Glyphs this one was substituted **from**, when the substitution was a ligature (Phase 12.12).
     *
     * Zero or one for everything else: a single substitution replaces one glyph, a multiple substitution
     * replaces one, and the cmap's own mapping replaces none. So a count of two or more is exactly "this glyph
     * is a ligature", and it is the number that tells a `ffi` apart from a decomposition — both have a
     * multi-scalar @ref source, and only one of them folded characters *together*.
     */
    uint16_t ligatureComponents;
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
    /**
     * The grouping facts about this glyph (Phase 12.14.22). See @ref RoboTextGlyphFlag for each flag's
     * meaning and for why the id facts are fields rather than flags.
     *
     * A glyph that a caller built by hand rather than one this module produced carries
     * @ref RoboTextGlyphFlagNone, which is what an unset flag word should say.
     */
    RoboTextGlyphFlags flags;
} RoboTextGlyph;

/** Clear @p glyph to a null glyph: no id, empty source, zero vectors. Safe on NULL. */
void RoboTextGlyphInit(RoboTextGlyph *glyph);

/** Non-zero when @p glyph names a drawable id and a well-formed source range. Safe on NULL. */
int RoboTextGlyphIsValid(const RoboTextGlyph *glyph);

/** Field-for-field equality. Safe on NULL (NULL == NULL). */
int RoboTextGlyphEqual(const RoboTextGlyph *a, const RoboTextGlyph *b);

/* ------------------------------------------------------------------ */
/* Clusters                                                            */
/* ------------------------------------------------------------------ */

/**
 * A cluster: a source range and the glyphs it produced (Phase 12.14.6).
 *
 * The mapping between the two index spaces, and the *only* place the many-to-many relation between scalars
 * and glyphs is written down. All four shapes live in one type:
 *
 *   1 source → 1 glyph        one scalar, one glyph, the ordinary case.
 *   1 source → many glyphs    a decomposition, or a scalar a font draws with several glyphs.
 *   many source → 1 glyph     a ligature: `ffi` is one glyph and one cluster covering three scalars.
 *   many source → many glyphs a cluster whose glyphs each cover part of the source — a base and a mark that
 *                             a font composed into one glyph each.
 *
 * **Logical order, not visual order** (Phase 12.14.7): the clusters of a right-to-left run are in source
 * order, and it is the run's *placement* that makes them right-to-left. Reversing this array for an RTL run
 * would destroy the source mapping that selection and hit testing need, and is the single most common way a
 * bidi implementation gets a caret wrong.
 */
typedef struct RoboTextGlyphCluster {
    /** The scalars this cluster covers: half-open, never inverted, never empty. */
    RoboTextRange source;
    /** The first glyph of the cluster, as an index into the run's own glyph array. */
    RoboTextGlyphIndex glyphStart;
    /** One past the cluster's last glyph. `glyphStart < glyphEnd` always. */
    RoboTextGlyphIndex glyphEnd;
} RoboTextGlyphCluster;

typedef struct RoboTextGlyphRun RoboTextGlyphRun;

/**
 * The run's clusters, in logical source order.
 *
 * They tile the glyph array: the first starts at glyph 0, each begins where the last ended, and the last ends
 * at the run's glyph count. They tile the run's source the same way, and they never overlap.
 */
size_t RoboTextGlyphRunClusterCount(const RoboTextGlyphRun *run);

/** Cluster @p index, or NULL when out of range. */
const RoboTextGlyphCluster *RoboTextGlyphRunCluster(const RoboTextGlyphRun *run, RoboTextClusterIndex index);

/**
 * The cluster covering scalar @p scalar of the run's own source.
 *
 * The door a caret and a hit test use, and the reason a cluster is an object: `RoboTextGlyph::cluster` answers
 * "which cluster does this glyph belong to" for a glyph the caller already has, and this answers the inverse
 * question for a *source position*, which is the one a click produces.
 *
 * @return RoboOk and @p outCluster; RoboInvalidArgument for a NULL argument or a scalar outside the run;
 *         RoboNotFound when no cluster covers it, which cannot happen for a run this module finalized and is
 *         reported rather than answered with cluster 0.
 */
Result RoboTextGlyphRunClusterForScalar(const RoboTextGlyphRun *run,
                                        RoboTextScalarIndex scalar,
                                        RoboTextClusterIndex *outCluster,
                                        RoboTextDiag *diag);

/**
 * Glyphs a combining mark (Phase 12.14.22): `General_Category` `Mn`, `Mc` or `Me`.
 *
 * Counted from the flags, so it is the same answer a caller gets by walking them.
 */
size_t RoboTextGlyphRunMarks(const RoboTextGlyphRun *run);

/**
 * Glyphs a line may not begin at (Phase 12.14.23).
 *
 * Not a line-breaking implementation — that is Phase 19's — but the information a line breaker will need and
 * cannot recover once the shaping buffer is gone: a glyph that is a combining mark, or the product of a
 * ligature, or one a font placed relative to its neighbour through GPOS, is a glyph whose removal from its
 * neighbours' context would change what was drawn.
 */
size_t RoboTextGlyphRunUnsafeToBreak(const RoboTextGlyphRun *run);

/**
 * The face's metrics at the run's own context (Phase 12.14.18), in device pixels.
 *
 * The record line layout needs — ascent, descent, the leading a line box uses, and the optional cap and
 * x-heights — so that a consumer never asks the font parser again. The values are the face's, scaled through
 * the module's one scaling door, and `fromFace` is zero when a fallback estimate was used.
 *
 * @return RoboOk and @p out; RoboInvalidArgument for a NULL argument. A run whose face could not be asked
 *         answers with a zeroed record and `fromFace == 0`, which is the same "not available" a context
 *         reports.
 */
Result RoboTextGlyphRunMetrics(const RoboTextGlyphRun *run, RoboTextFontMetrics *out);

/** The run's ascent in device pixels, or 0 when the face could not be asked. */
float RoboTextGlyphRunAscent(const RoboTextGlyphRun *run);

/** See @ref RoboTextGlyphRunAscent: the descent, as a positive magnitude. */
float RoboTextGlyphRunDescent(const RoboTextGlyphRun *run);

/** See @ref RoboTextGlyphRunAscent: the face's line gap. */
float RoboTextGlyphRunLineGap(const RoboTextGlyphRun *run);

/** See @ref RoboTextGlyphRunAscent: the leading a line box uses. */
float RoboTextGlyphRunLineHeight(const RoboTextGlyphRun *run);

/** The feature settings the run was shaped with (Phase 12.10), copied from its text run. */
const RoboTextFeatureSet *RoboTextGlyphRunFeatures(const RoboTextGlyphRun *run);

/**
 * The writing mode the run was generated at (Phase 12.20). Horizontal for a NULL run.
 *
 * The field a painter reads to know **which vector its pen accumulates** — `advanceX` for a horizontal run,
 * `advanceY` for a vertical one — and the field a layout pass reads to know which axis a line progresses
 * along. Reading `advanceY` without asking this is not wrong (it is zero for a horizontal run), but reading
 * *this* is what makes the two axes one code path rather than two.
 */
RoboTextWritingMode RoboTextGlyphRunWritingMode(const RoboTextGlyphRun *run);

/** The requested `text-orientation` (Phase 12.20). Mixed for a NULL run. */
RoboTextTextOrientation RoboTextGlyphRunTextOrientation(const RoboTextGlyphRun *run);

/**
 * The **resolved** orientation of every glyph in the run (Phase 12.20): upright or sideways.
 *
 * One answer for the whole run, because the run was cut where the orientation changed — so a painter asks
 * this once rather than per glyph, and it never inspects a script or a code point to decide how to draw a
 * glyph. `RoboTextGlyphOrientationRotation` is the angle it composes into its transform, and
 * `RoboTextGlyphOrientationUpright` is the default for a NULL run (which is also what a horizontal run's
 * glyphs are).
 */
RoboTextGlyphOrientation RoboTextGlyphRunOrientation(const RoboTextGlyphRun *run);

/** Non-zero when @p run advances along +y. A spelling of the question, not a second field. */
int RoboTextGlyphRunIsVertical(const RoboTextGlyphRun *run);

/**
 * Glyphs whose vertical advance was **synthesised** because the face carried no `vmtx` (Phase 12.20).
 *
 * Zero for a horizontal run and for a vertical run over a face with vertical metrics. A non-zero count is not
 * a defect — a face without `vmtx` is shaped at its em box's height, which is the specification's own answer
 * for it — but it is the one number that tells a caller whether the vertical advances it is holding were
 * measured or derived.
 */
size_t RoboTextGlyphRunVerticalSynthesised(const RoboTextGlyphRun *run);

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
 * A malformed GSUB table cannot fail a run of text: the engine skips what it cannot read and the glyphs the
 * cmap produced are what the caller gets, which is the phase's §"malformed fonts" applied at this level.
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

/**
 * Glyphs whose id came from a GSUB lookup rather than from the `cmap` (Phase 12.10).
 *
 * Not a count of *substitutions*: a ligature is one substituted glyph covering several scalars, and a
 * multiple substitution is several covering one, so this counts glyphs. It is zero exactly when the run's
 * mode is @ref RoboTextGlyphGenerationCmapPerScalar.
 */
size_t RoboTextGlyphRunSubstituted(const RoboTextGlyphRun *run);

/**
 * Glyphs a ligature produced: the run's `ffi` count (Phase 12.12).
 *
 * The count `RoboTextGlyphRunLigatureAt` indexes into, and the number a caller watching a `liga` toggle
 * compares between two runs.
 */
size_t RoboTextGlyphRunLigatures(const RoboTextGlyphRun *run);

/** Glyphs the face's GPOS positioned in @p run (Phase 12.11). See `RoboTextGlyphRunPositioned`. */
size_t RoboTextGlyphRunPositioned(const RoboTextGlyphRun *run);

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
 * The run's total **vertical** advance, in device pixels (Phase 12.14.19; non-zero since 12.20).
 *
 * Zero for a horizontal run, and the pen's step for a vertical one — where each glyph's `advanceY` is the
 * face's `vmtx` entry (or the em-box fallback, counted by @ref RoboTextGlyphRunVerticalSynthesised) plus
 * whatever vertical GPOS added. `RoboTextGlyphRunWritingMode` is what says which of the two totals a caller
 * accumulating a line's progress should read; asking it is why this field exists beside the horizontal one
 * instead of replacing it.
 */
float RoboTextGlyphRunAdvanceY(const RoboTextGlyphRun *run);

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
/**
 * Why a run is not a valid record (Phase 12.14.37).
 *
 * One enumerator per invariant `RoboTextGlyphRunValidate` checks, rather than one `Invalid` answer, because a
 * validation door that says only "no" is a door a failing test cannot use: the enumerator names the invariant
 * that broke, which is the whole value of validating in a test rather than asserting.
 */
typedef enum RoboTextGlyphRunProblem {
    /** Every invariant holds. */
    RoboTextGlyphRunProblemNone = 0,
    /** The source range is inverted. */
    RoboTextGlyphRunProblemRange,
    /** The run has a non-empty source and no glyphs, or glyphs and an empty source. */
    RoboTextGlyphRunProblemGlyphCount,
    /** No face, or a context this build cannot use. */
    RoboTextGlyphRunProblemFont,
    /** A glyph's source does not lie inside the run's. */
    RoboTextGlyphRunProblemGlyphSource,
    /** An advance or an offset is `NaN` or infinite. */
    RoboTextGlyphRunProblemVector,
    /** The clusters do not tile the glyph array. */
    RoboTextGlyphRunProblemClusterTiling,
    /** A cluster's source is empty or does not lie inside the run's. */
    RoboTextGlyphRunProblemClusterSource,
    /** The clusters are not in ascending logical source order. */
    RoboTextGlyphRunProblemClusterOrder,
    /** A glyph's cluster index names no cluster, or the wrong one for its source. */
    RoboTextGlyphRunProblemClusterIndex,
    /** The stored total disagrees with the sum of the glyphs' own advances. */
    RoboTextGlyphRunProblemTotal,
    /** A counter (`missing`, `ligatures`, `marks`, …) disagrees with the glyphs. */
    RoboTextGlyphRunProblemCounter,
    /** The direction disagrees with the resolved level's parity. */
    RoboTextGlyphRunProblemDirection
} RoboTextGlyphRunProblem;

/** Non-zero when @p problem is a defined enumerator. */
int RoboTextGlyphRunProblemIsKnown(RoboTextGlyphRunProblem problem);

/** Name of @p problem (`"cluster-tiling"`); `"none"` for the valid answer. Never NULL. */
const char *RoboTextGlyphRunProblemName(RoboTextGlyphRunProblem problem);

/**
 * Check every invariant a valid run holds (Phase 12.14.37). O(glyphs + clusters).
 *
 * The door the phase asks for as "validation enabled in debug and test builds", written as a function a
 * caller invokes rather than as an `#ifdef` inside generation, because a check a test cannot call is a check
 * no test can prove fires. Generation does not call it: a run it produced is valid by construction, and this
 * exists to *prove* that in a suite rather than to be believed.
 *
 * The invariants, all of them:
 *
 *   glyphs and source agree in emptiness; every glyph's source lies inside the run's; every advance and
 *   offset is finite; clusters tile the glyph array and their sources tile the run's in ascending order with
 *   no overlap; every glyph's `cluster` names the cluster that contains it; the stored advance total is the
 *   sum of the glyphs' own; every counter equals what the glyphs say; and the direction agrees with the
 *   parity of the resolved level.
 *
 * @param outProblem receives the first broken invariant, or @ref RoboTextGlyphRunProblemNone.
 *
 * @return RoboOk when the run is valid; RoboInvalidArgument for a NULL argument or an invalid run — with
 *         @p outProblem naming the invariant; RoboNotSupported when the run has no font context.
 */
Result RoboTextGlyphRunValidate(const RoboTextGlyphRun *run,
                                RoboTextGlyphRunProblem *outProblem,
                                RoboTextDiag *diag);

/**
 * The run as a multi-line diagnostic block (Phase 12.14.35).
 *
 * The artifact a typography bug is diagnosed from, and the reason it is a door rather than a test helper:
 * `Build/Text-GlyphTable.txt` and a failing suite both want the same text, and a second spelling of it would
 * drift from this one.
 *
 * ```
 * GlyphRun {
 *   source [0..6)  scalars 6
 *   script latin  direction ltr  bidiLevel 0  base ltr
 *   mode cmap+gsub+gpos  features kern=on
 *   metrics ascent 15.10  descent 3.70  lineGap 0.00  lineHeight 18.80
 *   advance 61.20, 0.00  glyphs 6  clusters 6
 *   glyphs [0] id=42 ...
 *   clusters [0] source [0..1) glyphs [0..1)
 * }
 * ```
 *
 * Complete or absent, like every dump here: a run that does not fit @p capacity leaves the buffer as the
 * empty string, and `Build/Text-GlyphTable.txt`'s "absent" is a byte a reader can trust.
 *
 * @return RoboOk and @p outLength; RoboInvalidArgument for a NULL argument; RoboNotSupported when @p buffer
 *         cannot hold the block.
 */
Result RoboTextGlyphRunWrite(const RoboTextGlyphRun *run,
                             char *buffer,
                             size_t capacity,
                             size_t *outLength);

typedef struct RoboTextGlyphStats {
    size_t glyphRuns;
    size_t glyphs;
    /** Glyphs whose id is `.notdef` or whose metrics would not parse. */
    size_t missing;
    /** Glyphs whose source range covers more than one scalar: Phase 12.12's ligatures, and nothing else so
     *  far. A ligature is the one thing that makes a glyph stand for several scalars at once. */
    size_t multiScalarSources;
    /** Glyphs a ligature produced: `ligatureComponents` of two or more. */
    size_t ligatureGlyphs;
    /** Scalars those glyphs stand for: three for one `ffi`, zero for a text with no ligature. */
    size_t ligatureScalars;
    /** Glyphs the face's GPOS positioned (Phase 12.11). Zero for a face with no GPOS. */
    size_t positioned;
    /** Glyphs whose id came from a mirrored code point (UAX #9 L4 applies to their scalar). */
    size_t mirrored;
    /** Glyphs that stand for a bidi formatting control: no id, no advance, nothing drawn. */
    size_t controls;
    /** Glyphs that are a combining mark (Phase 12.14.22). */
    size_t marks;
    /** Clusters across every run (Phase 12.14.6). */
    size_t clusters;
    /** Total advance across every run, in device pixels. */
    double advancePx;
} RoboTextGlyphStats;

/** Fill @p out with @p run's counters. RoboInvalidArgument for NULL arguments. */
Result RoboTextGlyphRunGetStats(const RoboTextGlyphRun *run, RoboTextGlyphStats *out);

/**
 * Human-readable one-line dump of a glyph, for the visual test surface the phase asks for.
 *
 * Writes `id=<id> scalar=<n>..<n> advance=<f> units=<n> offset=<f>,<f> mapped=<0|1> mirrored=<0|1>
 * flags=<word>` — the flags word since 12.14, because "is this the start of its cluster" is the question a
 * reader of a glyph dump asks next and the one thing the old line could not answer.
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
