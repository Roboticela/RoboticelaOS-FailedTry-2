/*
 * RoboLayout's RoboFont-backed text measurer (Phase 10.5).
 *
 * The real supplier behind RoboLayoutText.h's seam: a RoboFont face asked through
 * RoboFont's own public API, answering the advance width and line extent of a code
 * point span.
 *
 * It is a *measurer*, not a font manager
 *
 * It discovers nothing, opens no file, chooses no font and caches no table. The
 * face is handed to it by the caller and borrowed for as long as it is attached --
 * the same lifetime rule RoboLayoutStyle.h states for a computed style, and for
 * the same reason: RoboLayout cannot extend the life of something it does not own,
 * and a font manager inside the layout module would be the second one in the tree.
 * Which face a document's text should use is a *selection* question (RoboFont's
 * own Goal 5.11/5.12/5.15) and a `font-family` question (CSS's), and both have
 * owners that are not this module.
 *
 * What it computes, and the one decision that matters
 *
 *   width   the sum of every glyph's `hmtx` advance, in **font units**, scaled to
 *           pixels **once** at the end.
 *
 * Summing in font units and converting once is the whole design of the width. The
 * obvious alternative -- scale each advance, round, add -- introduces one rounding
 * per glyph, and a hundred-glyph run would drift by up to fifty pixels from the
 * width its glyphs actually occupy. RoboFont's own `FontScaleFontUnits` is
 * used for the single conversion rather than a local multiply, because it already
 * carries the overflow check and the stated rounding policy
 * (RoboFontRoundNearest, ties away from zero) that this module would otherwise
 * have to restate.
 *
 *   height  ascent + descent + line gap, from the face's canonical layout metrics,
 *           scaled the same way.
 *
 * The *baseline* is not computed here. The record carries ascent and descent so
 * that 10.7 can place a baseline without re-deriving the font's metrics, and the
 * `height` this build reports is the run's line extent -- which is what CSS calls
 * the *normal* line height, and which 10.6 replaces with a real line box.
 *
 * Unshaped, and it says so
 *
 * No GlyphSubstitution, no GlyphPositioning, no ligatures, no kerning, no bidi, no
 * fallback. The glyphs are the cmap's, in logical order, and their advances are
 * their own. That is exactly CSS's *advance width* of a run before layout reorders
 * it, and naming the limit is what keeps a later phase from having to guess which
 * of the two numbers this one produced.
 *
 * A code point the face has no glyph for is measured at `.notdef`'s advance and
 * **counted** (`glyphsMissing`) rather than skipped: a build that reported a
 * plausible width for a run the font cannot draw would be the single failure no
 * later phase could detect. Font fallback is a later phase's; noticing the miss is
 * this one's.
 */

#ifndef ROBO_LAYOUT_TEXT_FONT_H
#define ROBO_LAYOUT_TEXT_FONT_H

#include "RoboBase/RoboError.h"
#include "RoboFont/RoboFontFace.h"

#include "RoboLayout/RoboLayoutDiagnostics.h"
#include "RoboLayout/RoboLayoutText.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * A measurer over one borrowed font face.
 *
 * An opaque-ish handle by value, and it owns nothing: `attach` borrows, `detach`
 * releases the borrow and touches nothing RoboFont owns. The struct is public so
 * a caller can hold one on its stack, which is where it belongs -- a measurer's
 * life is a pass's life.
 */
typedef struct LayoutTextFont {
    /** Borrowed face; NULL when detached. */
    FontFace *face;
    /**
     * Non-zero when the face's cmap and metrics have been parsed.
     *
     * A cache of a *three-state* RoboFont question rather than of a value: RoboFont
     * parses lazily and idempotently, and the flag exists so the measurer asks once
     * per pass instead of once per glyph run. It is reset by `detach`, because the
     * parses belong to the face that was attached and not to this struct.
     */
    int parsed;
    /** Measurements this measurer has answered since it was attached. */
    size_t runsMeasured;
    /** Code points it has measured with no glyph in the face. */
    size_t glyphsMissing;
} LayoutTextFont;

/** Clear @p font to detached. Safe on NULL. */
void LayoutTextFontInit(LayoutTextFont *font);

/**
 * Borrow @p face for measuring. The caller keeps ownership and must not release it
 * while a metrics source built from this measurer is in use.
 *
 * @return RoboOk; RoboInvalidArgument for a NULL @p font or @p face.
 */
Result LayoutTextFontAttach(LayoutTextFont *font,
                                          FontFace *face);

/** Release the borrow. Safe on NULL and on an already-detached measurer. */
void LayoutTextFontDetach(LayoutTextFont *font);

/** Non-zero when a face is attached. */
int LayoutTextFontIsAttached(const LayoutTextFont *font);

/**
 * A metrics source over @p font, for `LayoutContextSetTextMetrics`.
 *
 * The source borrows @p font, so @p font must outlive every pass run with it --
 * the same rule the style source states, and for the same reason. The source is
 * also *live*: a `runsMeasured` counter incremented after this call is visible
 * through the source, which is what lets a caller read what its measurer did
 * without the pass reporting it twice.
 */
LayoutTextMetrics LayoutTextFontMetrics(LayoutTextFont *font);

/**
 * Measure one request through a measurer, without the seam's indirection.
 *
 * Exposed because it is the interesting half to test directly: the seam
 * (RoboLayoutText.h) is about "no source" and "a source that failed", while this
 * is about the arithmetic. A caller that wants the seam's guarantees goes through
 * `LayoutTextMeasure`.
 *
 * @return RoboOk; RoboInvalidArgument for NULL arguments, an ill-formed node or
 *         a NULL style; RoboNotFound when the text node does not resolve;
 *         RoboInvalidArgument when the range does not lie inside it; RoboError when
 *         the face is detached or its tables will not parse.
 */
Result LayoutTextFontMeasure(LayoutTextFont *font,
                                           const LayoutTextRequest *request,
                                           LayoutTextRun *outRun,
                                           LayoutDiag *diag);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_LAYOUT_TEXT_FONT_H */
