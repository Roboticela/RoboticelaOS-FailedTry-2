/*
 * RoboLayout baselines (Phase 10.7).
 *
 * The line the letters sit on. 10.5 measured a run and recorded its ascent and
 * descent; 10.6 made a line a record with a rectangle; this phase is the one that
 * puts a participant *on* the line, and the whole of it is one question answered
 * three places:
 *
 *   where, inside a line's rectangle, is the baseline?
 *
 * Everything else follows. A participant is not aligned to the top of its line any
 * more; it is aligned so that its own baseline *is* the line's. Two runs at
 * different `font-size`s therefore share a floor rather than a ceiling, which is
 * the single visible difference between this phase and 10.6 -- and the reason
 * 10.6's `size.height = the tallest member` was always a placeholder: the line's
 * height is `ascent + descent`, and "the tallest member" happens to equal that
 * only when every member shares one font.
 *
 * Why a *record* and not two numbers on the object
 *
 * A baseline is not a property of an object; it is a *relationship* between an
 * object and the line that holds it. So the metrics travel as a small value type
 * (this header), the object stores the ones it was placed with (RoboLayoutObject.h),
 * and the line stores the ones it concluded (RoboLayoutLineBox.h). Three copies of
 * one number would be a mistake; three *different* numbers that happen to start from
 * the same measurement are the model:
 *
 *   a run's own metrics       ascent + descent + line gap, from the font
 *   a participant's metrics   the same, with the line gap split as half-leading
 *   a line's metrics          the maximum ascent and the maximum descent, summed
 *
 * The three kinds, and why there is no fourth
 *
 *   NONE     no baseline. Not a value a placed object has: it is what a cleared
 *            record reads as, and what an object that has never been placed
 *            answers. It is also what a *line* would have if nothing anchored one,
 *            which this build never stores -- a line exists only because something
 *            was placed on it (RoboLayoutLineBox.h).
 *   TEXT     the baseline came from **font line metrics**: `hhea`/`OS2`'s ascent
 *            and descent, scaled to the computed `font-size`.
 *   BOX      the baseline is a **fallback**: the object's own bottom edge. This is
 *            the rule for everything that is not a text run -- an `inline-block`,
 *            an empty inline box -- and the header states it where the door is
 *            declared rather than leaving it to be inferred.
 *
 * `TOP`, `MIDDLE`, `CENTRAL` and the rest of `vertical-align`'s keywords are
 * deliberately **not** enumerators here, for the reason 10.2 refused a `FLEX`
 * context kind and 9.12 refused `display: flex`: an enumerator with no rule behind
 * it is a name the module can spell and cannot honour. They arrive as rows when
 * `vertical-align` does.
 *
 * The two conventions this header fixes
 *
 * Sign: **ascent and descent are magnitudes and both non-negative.** `ascent` is the
 * distance from the baseline *up* to the top of the object's extent, `descent` the
 * distance *down* to its bottom. A caller never subtracts a descent -- which is
 * what makes `extent == ascent + descent` readable at a glance and what makes a
 * font that authored its descent with the other sign convention somebody else's
 * problem (RoboLayoutTextFont.c normalises it once).
 *
 * Space: metrics are stated **relative to the baseline**, so they carry no
 * coordinate and belong to no particular space. That is the point of the type:
 * the same numbers describe a run anywhere, and the phase's job is entirely about
 * *where the baseline goes*, which is a fact about a line and lives on the line.
 *
 * What this module is not
 *
 * Not a CSS parser: no `vertical-align` value is read, spelled or stored. Not
 * shaping: a glyph's own extents are absent and a run's ascent is the *font's*, not
 * the ink's -- which is what CSS specifies for `line-height: normal` and is the
 * reason a line can be laid out before any glyph exists. Not a renderer: a baseline
 * is a coordinate, and nothing here draws on it.
 */

#ifndef ROBO_LAYOUT_BASELINE_H
#define ROBO_LAYOUT_BASELINE_H

#include <stddef.h>
#include <stdint.h>

#include "RoboBase/RoboError.h"

#include "RoboLayout/RoboLayoutText.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------ */
/* Kind                                                                */
/* ------------------------------------------------------------------ */

/** What an object's or a line's baseline is anchored on. */
typedef enum robo_layout_baseline_kind {
    /** No baseline at all: the absence, not a kind. */
    ROBO_LAYOUT_BASELINE_NONE = 0,
    /** From font line metrics (ascent/descent scaled to the computed size). */
    ROBO_LAYOUT_BASELINE_TEXT = 1,
    /** The fallback rule: the object's own bottom edge. */
    ROBO_LAYOUT_BASELINE_BOX = 2
} robo_layout_baseline_kind_t;

/** Number of defined baseline kinds. NONE is one of them. */
#define ROBO_LAYOUT_BASELINE_KIND_COUNT 3u

/** Non-zero when @p kind is a defined kind. NONE is defined. */
int robo_layout_baseline_kind_is_known(robo_layout_baseline_kind_t kind);

/** Human-readable kind name ("none", "text", "box"); never NULL, "unknown" for an
 *  undefined enumerator. */
const char *robo_layout_baseline_kind_name(robo_layout_baseline_kind_t kind);

/**
 * Whether a baseline of @p kind is *usable* for vertical placement.
 *
 * NONE answers 0; TEXT and BOX answer 1. Not the same question as
 * @ref robo_layout_baseline_kind_is_known, and the difference is the reason it is
 * here: NONE is a perfectly well-formed kind -- it is what a cleared record holds --
 * and it is precisely the one a placement pass must not treat as a baseline, because
 * an object with no baseline has no position relative to a line.
 *
 * A *zero* ascent and descent are usable, and that is deliberate: an unmeasured run
 * is anchored at the baseline with no extent, which is a statement about a document
 * nothing measured rather than a missing answer.
 */
int robo_layout_baseline_kind_is_usable(robo_layout_baseline_kind_t kind);

/* ------------------------------------------------------------------ */
/* The record                                                          */
/* ------------------------------------------------------------------ */

/**
 * One object's or one line's baseline metrics.
 *
 * A record by value and owned by nobody: no allocation, so reading one out of an
 * object or a line copies it and neither is affected. The same type describes both,
 * because the same two numbers describe both -- what a caller does with them differs,
 * and that difference is the coordinate space the caller is in, not the type.
 */
typedef struct robo_layout_baseline_metrics {
    /** What the baseline is anchored on. NONE means "there is no baseline". */
    robo_layout_baseline_kind_t kind;
    /** Baseline up to the top of the extent, in px. Never negative. */
    int32_t ascent;
    /** Baseline down to the bottom of the extent, in px. Never negative. */
    int32_t descent;
} robo_layout_baseline_metrics_t;

/** Clear @p metrics to NONE with a zero extent. Safe on NULL. */
void robo_layout_baseline_metrics_init(robo_layout_baseline_metrics_t *metrics);

/** Field-by-field equality, including the kind. Safe on NULL. */
int robo_layout_baseline_metrics_equal(const robo_layout_baseline_metrics_t *a,
                                       const robo_layout_baseline_metrics_t *b);

/**
 * Whether @p metrics can describe anything: a usable kind with both terms
 * non-negative.
 *
 * A NONE kind is *invalid* here rather than merely empty, and the distinction is the
 * one @ref robo_layout_baseline_kind_is_usable draws -- a record with no baseline is
 * not a record with a zero baseline.
 */
int robo_layout_baseline_metrics_is_valid(const robo_layout_baseline_metrics_t *metrics);

/** The extent the metrics span: `ascent + descent`, in px. 0 for NULL. */
int32_t robo_layout_baseline_metrics_extent(const robo_layout_baseline_metrics_t *metrics);

/**
 * The baseline's distance from the **top** of the extent, in px.
 *
 * Equal to `ascent` for every value this build produces, and it is a door rather than
 * a note because the equality is a *convention* and not arithmetic: the extent is
 * defined as "ascent above the baseline, descent below it", so the top of the extent
 * is the baseline minus the ascent by construction. A caller that thinks in
 * "offset from the top" (a renderer measuring downward from a box's edge) gets the
 * number without having to know which sign convention this module chose.
 *
 * @return ROBO_OK and the offset; ROBO_INVALID_ARGUMENT for NULL arguments;
 *         ROBO_NOT_FOUND when the metrics carry no usable baseline.
 */
robo_result_t robo_layout_baseline_offset(const robo_layout_baseline_metrics_t *metrics,
                                          int32_t *out_offset);

/**
 * The baseline metrics of a **measured text run**.
 *
 * This is where the line gap becomes half-leading, and the arithmetic is the whole
 * of CSS's `line-height: normal` model:
 *
 *   a run's metrics        ascent, descent, and a gap between the run's extent and
 *                          the next line's -- the font's `hhea.lineGap`
 *   the participant's      the same ascent and descent, each *grown* by half the gap
 *
 * Why grow them rather than drop the gap: `run.height` is `ascent + descent + gap`,
 * and 10.6's line height was "the tallest member", so a phase that switched to
 * `ascent + descent` without the gap would have shortened every line in the build by
 * the font's leading. Splitting it is also what CSS does, and the half split is
 * exact -- `ascent + half` and `descent + gap - half` sum to the run's height to the
 * pixel, whatever the gap's parity.
 *
 * An unmeasured run yields kind TEXT with a zero extent, which is the documented
 * degradation and not an error: it is a text baseline with nothing measured on it.
 *
 * @return ROBO_OK; ROBO_INVALID_ARGUMENT for NULL arguments. A run whose recorded
 *         height is smaller than its ascent plus descent (which a third-party
 *         measurer can produce) is treated as having no gap rather than as a
 *         negative one.
 */
robo_result_t robo_layout_baseline_metrics_from_run(const robo_layout_text_run_t *run,
                                                   robo_layout_baseline_metrics_t *out);

/**
 * The **fallback** metrics: an object's bottom edge is its baseline.
 *
 * The rule for everything that is not a text run, stated once here so that the pass
 * and a later phase cannot disagree about it. An `inline-block`, an empty inline box
 * and anything else this build places without font metrics get
 * `ascent = @p extent`, `descent = 0`, kind BOX -- which is to say their baseline is
 * their bottom edge, so a row of them sits on the line's baseline by its feet.
 *
 * CSS's own rule for an inline-block is richer (the baseline of its last in-flow line
 * box, or its bottom *margin* edge when it has none or overflows) and both halves of
 * that are later phases': the line box inside it is 10.8's and the margin edge is
 * 10.9's. What this rule guarantees is the part those phases need from this one -- a
 * deterministic, documented, non-negative answer that does not depend on order.
 *
 * A negative @p extent is clamped to 0: a box cannot extend above its own baseline by
 * a negative amount, and the clamp keeps the invariant "ascent + descent == extent"
 * true for every value this door produces.
 *
 * @return ROBO_OK; ROBO_INVALID_ARGUMENT for a NULL @p out.
 */
robo_result_t robo_layout_baseline_metrics_from_extent(int32_t extent,
                                                     robo_layout_baseline_metrics_t *out);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_LAYOUT_BASELINE_H */
