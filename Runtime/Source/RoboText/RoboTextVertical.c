/*
 * Vertical writing (Phase 12.20) — see `RoboTextVertical.h` for the contract.
 *
 * Three pieces, none of them a second shaping engine:
 *
 *   **the writing mode**   a value, its two axes, and the two questions a consumer asks of it ("is the
 *                          pen on y?", "which way do the lines stack?"). Pure data: no text is involved.
 *   **the orientation**    a character becomes upright or sideways. Ideographic scripts stand up (one row
 *                          per script), CJK punctuation stands up by code point (a table of ranges), and
 *                          whitespace takes the neighbour it sits after. The contextual door is what
 *                          segmentation cuts runs on, so a Japanese column's spaces do not open one-scalar
 *                          sideways runs.
 *   **the OpenType hook**  `vert` and `vrt2` written into a feature set. That is the entire integration
 *                          with 12.10: a vertical alternate is a GSUB substitution like any other, and
 *                          this file's only job is to name the two features that ask for one.
 *
 * What is deliberately absent: any idea of where a glyph is *drawn*, any rotation of a bitmap, any
 * vertical line breaking. The glyph's placement is 12.20's positioning work in `RoboTextGlyphs.c` and the
 * rotation is a number (`RoboTextGlyphOrientationRotation`) the painter composes into its own transform —
 * this file states the decision and nothing consumes pixels.
 */
#include "RoboText/RoboTextVertical.h"

#include "RoboTextInternal.h"

#include <string.h>

/* ------------------------------------------------------------------ */
/* Writing mode                                                        */
/* ------------------------------------------------------------------ */

int RoboTextWritingModeIsVertical(RoboTextWritingMode mode)
{
    return mode == RoboTextWritingVerticalRightToLeft || mode == RoboTextWritingVerticalLeftToRight;
}

int RoboTextWritingModeBlockIsLeftward(RoboTextWritingMode mode)
{
    return mode == RoboTextWritingVerticalRightToLeft;
}

RoboTextWritingAxes RoboTextWritingModeAxes(RoboTextWritingMode mode)
{
    RoboTextWritingAxes axes;

    /*
     * The table the header draws, written once. The default arm answers the horizontal pair rather than a
     * zero vector: a zero axis would make every coordinate a line's layout produced collapse to the origin,
     * which reads as a layout bug rather than as a bad mode — and "the mode was not known" is already a
     * question `RoboTextWritingModeIsKnown` answers.
     */
    switch (mode) {
    case RoboTextWritingVerticalRightToLeft:
        axes.inlineX = 0.0f;
        axes.inlineY = 1.0f;
        axes.blockX = -1.0f;
        axes.blockY = 0.0f;
        break;
    case RoboTextWritingVerticalLeftToRight:
        axes.inlineX = 0.0f;
        axes.inlineY = 1.0f;
        axes.blockX = 1.0f;
        axes.blockY = 0.0f;
        break;
    case RoboTextWritingHorizontal:
    default:
        axes.inlineX = 1.0f;
        axes.inlineY = 0.0f;
        axes.blockX = 0.0f;
        axes.blockY = 1.0f;
        break;
    }
    return axes;
}

int RoboTextWritingAxesIsHorizontal(const RoboTextWritingAxes *axes)
{
    if (!axes) {
        return 1;
    }
    return axes->inlineX != 0.0f;
}

Result RoboTextWritingAxesInline(const RoboTextWritingAxes *axes, float value, float *outX, float *outY)
{
    if (!axes || !outX || !outY) {
        return RoboInvalidArgument;
    }
    *outX = axes->inlineX * value;
    *outY = axes->inlineY * value;
    return RoboOk;
}

Result RoboTextWritingAxesBlock(const RoboTextWritingAxes *axes, float value, float *outX, float *outY)
{
    if (!axes || !outX || !outY) {
        return RoboInvalidArgument;
    }
    *outX = axes->blockX * value;
    *outY = axes->blockY * value;
    return RoboOk;
}

/* ------------------------------------------------------------------ */
/* Orientation                                                         */
/* ------------------------------------------------------------------ */

float RoboTextGlyphOrientationRotation(RoboTextGlyphOrientation orientation)
{
    /*
     * A quarter turn clockwise, and `π/2` spelled out rather than pulled from `math.h`: this file must
     * compile in the freestanding build, and a literal here is one fewer libm dependency for a constant
     * that has no precision question. The value is the `float` nearest `π/2`, which is what a transform
     * composed in `float` wants anyway.
     */
    return RoboTextGlyphOrientationIsUpright(orientation) ? 0.0f : 1.57079632679f;
}

/*
 * The scripts that stand up in a vertical column. One row each, and the row is a name from RoboUnicode's
 * own table — this module keeps no script vocabulary of its own (Phase 12.5), and the list is short
 * because UAX #50's own rule is "ideographic scripts upright, everything else rotated".
 */
int RoboTextVerticalScriptIsUpright(RoboTextScript script)
{
    switch (script) {
    case RoboUScriptHan:
    case RoboUScriptHiragana:
    case RoboUScriptKatakana:
    case RoboUScriptBopomofo:
    case RoboUScriptHangul:
    case RoboUScriptYi:
        return 1;
    default:
        break;
    }
    return 0;
}

int RoboTextVerticalCodePointIsUpright(RoboTextCodePoint codePoint)
{
    /*
     * The code-point half of the rule, for the characters whose script is `Common`: CJK punctuation and
     * the blocks that carry vertical forms. Each range is a row and the comment names what it is, because
     * a reader checking this against UAX #50 needs to know which table a bound came from.
     */
    if (codePoint >= 0x3000u && codePoint <= 0x303Fu) {
        return 1; /* CJK Symbols and Punctuation, ideographic space included */
    }
    if (codePoint >= 0x3100u && codePoint <= 0x312Fu) {
        return 1; /* Bopomofo */
    }
    if (codePoint >= 0x31C0u && codePoint <= 0x31EFu) {
        return 1; /* CJK Strokes + Katakana Phonetic Extensions */
    }
    if (codePoint >= 0x3200u && codePoint <= 0x9FFFu) {
        return 1; /* Enclosed CJK, CJK Compatibility, CJK Unified Ideographs + ext A */
    }
    if (codePoint >= 0xA000u && codePoint <= 0xA4CFu) {
        return 1; /* Yi Syllables + Yi Radicals */
    }
    if (codePoint >= 0xAC00u && codePoint <= 0xD7AFu) {
        return 1; /* Hangul Syllables */
    }
    if (codePoint >= 0xF900u && codePoint <= 0xFAFFu) {
        return 1; /* CJK Compatibility Ideographs */
    }
    if (codePoint >= 0xFE10u && codePoint <= 0xFE1Fu) {
        return 1; /* Vertical Forms: the vertical punctuation this phase exists for */
    }
    if (codePoint >= 0xFE30u && codePoint <= 0xFE4Fu) {
        return 1; /* CJK Compatibility Forms */
    }
    if (codePoint >= 0xFF01u && codePoint <= 0xFF60u) {
        return 1; /* Fullwidth Forms (halfwidth Katakana begins at FF61 and is *not* upright) */
    }
    if (codePoint >= 0xFFE0u && codePoint <= 0xFFE6u) {
        return 1; /* Fullwidth signs: cent, pound, not, macron, broken bar, yen, won */
    }
    if (codePoint >= 0x1F200u && codePoint <= 0x1F2FFu) {
        return 1; /* Enclosed Ideographic Supplement */
    }
    if (codePoint >= 0x20000u && codePoint <= 0x3FFFFu) {
        return 1; /* CJK Ideograph Extension B and above */
    }
    return 0;
}

int RoboTextVerticalCodePointIsRotated(RoboTextCodePoint codePoint)
{
    /*
     * The characters UAX #50 gives a **rotated** form although their script is one that normally stands up:
     * halfwidth Katakana (U+FF61–U+FFDC) and the halfwidth Hangul and marks beside it (U+FFE8–U+FFEE).
     *
     * This table has to be consulted *before* the script, and that is the whole reason it exists as a door of
     * its own: halfwidth Katakana's script is Katakana, and a rule that asked the script first would answer
     * "upright" for a character the property rotates — which is what a Japanese column of `ｶ` would then get
     * wrong. `text.vertical.orientation` proves the two tables against each other for exactly this reason.
     */
    if (codePoint >= 0xFF61u && codePoint <= 0xFFDCu) {
        return 1;
    }
    if (codePoint >= 0xFFE8u && codePoint <= 0xFFEEu) {
        return 1;
    }
    return 0;
}

int RoboTextVerticalCodePointIsWhitespace(RoboTextCodePoint codePoint)
{
    return codePoint == 0x0020u || codePoint == 0x0009u || codePoint == 0x000Au || codePoint == 0x000Du ||
           codePoint == 0x00A0u || codePoint == 0x1680u || (codePoint >= 0x2000u && codePoint <= 0x200Au) ||
           codePoint == 0x2028u || codePoint == 0x2029u || codePoint == 0x202Fu || codePoint == 0x205Fu ||
           codePoint == 0x3000u || codePoint == 0xFEFFu;
}

RoboTextGlyphOrientation RoboTextVerticalOrientationOf(RoboTextCodePoint codePoint,
                                                     RoboTextScript script,
                                                     RoboTextTextOrientation requested)
{
    if (requested == RoboTextTextOrientationUpright) {
        return RoboTextGlyphOrientationUpright;
    }
    if (requested == RoboTextTextOrientationSideways) {
        return RoboTextGlyphOrientationSideways;
    }
    /*
     * `Mixed`, and the order is three questions rather than two. **A character that must rotate is asked
     * first**, because its script would otherwise claim it: halfwidth `ｶ` is Katakana, and a rule that went
     * straight to the script table would stand it up. Then the script, which answers for the ideographs (`漢`
     * is upright *because it is Han*), then the code-point table, which answers for the characters whose
     * script is Common (`、` is upright *because it is a CJK comma*).
     */
    if (RoboTextVerticalCodePointIsRotated(codePoint)) {
        return RoboTextGlyphOrientationSideways;
    }
    if (RoboTextVerticalScriptIsUpright(script)) {
        return RoboTextGlyphOrientationUpright;
    }
    if (RoboTextVerticalCodePointIsUpright(codePoint)) {
        return RoboTextGlyphOrientationUpright;
    }
    return RoboTextGlyphOrientationSideways;
}

RoboTextGlyphOrientation RoboTextVerticalOrientationInContext(RoboTextCodePoint codePoint,
                                                             RoboTextScript script,
                                                             RoboTextGlyphOrientation preceding,
                                                             RoboTextTextOrientation requested)
{
    /*
     * Whitespace takes the character before it — the one contextual rule in this file, and the reason the
     * door exists beside the per-character one. A **forced** orientation is still forced: a caller that
     * asked for `sideways` gets a sideways space, because the space is not what the request was about.
     */
    if (RoboTextVerticalCodePointIsWhitespace(codePoint) && requested == RoboTextTextOrientationMixed) {
        return RoboTextGlyphOrientationIsKnown(preceding) ? preceding : RoboTextGlyphOrientationUpright;
    }
    return RoboTextVerticalOrientationOf(codePoint, script, requested);
}

/* ------------------------------------------------------------------ */
/* The OpenType hook                                                   */
/* ------------------------------------------------------------------ */

Result RoboTextWritingModeApplyFeatures(RoboTextWritingMode mode, RoboTextFeatureSet *features)
{
    Result status;

    if (!features) {
        return RoboInvalidArgument;
    }
    if (!RoboTextWritingModeIsKnown(mode)) {
        return RoboInvalidArgument;
    }
    if (!RoboTextWritingModeIsVertical(mode)) {
        /*
         * Horizontal text writes nothing. Not "writes `vert` off": a caller's own `vert off` is the
         * caller's, and a horizontal run's features are exactly the ones it was handed.
         */
        return RoboOk;
    }
    /*
     * Both tags, in the order the header explains. `RoboTextFeatureSetSet` is a *set* rather than a list
     * push, so a tag the caller already named keeps its own state: a caller that wrote `"vrt2" off` gets
     * `vrt2` off with `vert` on, which is a request this build can honour and the reason the two are
     * separate settings rather than one.
     */
    if (!RoboTextFeatureSetFind(features, "vert")) {
        status = RoboTextFeatureSetSet(features, "vert", RoboTextFeatureOn, 0u);
        if (status != RoboOk) {
            return status;
        }
    }
    if (!RoboTextFeatureSetFind(features, "vrt2")) {
        status = RoboTextFeatureSetSet(features, "vrt2", RoboTextFeatureOn, 0u);
        if (status != RoboOk) {
            return status;
        }
    }
    return RoboOk;
}

/* ------------------------------------------------------------------ */
/* Diagnostics                                                         */
/* ------------------------------------------------------------------ */

Result RoboTextWritingModeWrite(RoboTextWritingMode mode,
                                RoboTextTextOrientation orientation,
                                char *buffer,
                                size_t capacity,
                                size_t *outLength)
{
    const char *modeName;
    const char *orientName;
    size_t modeLen;
    size_t orientLen;
    size_t need;

    if (!buffer || !outLength) {
        return RoboInvalidArgument;
    }
    *outLength = 0u;
    buffer[0] = '\0';
    modeName = RoboTextWritingModeName(mode);
    orientName = RoboTextTextOrientationName(orientation);
    modeLen = strlen(modeName);
    orientLen = strlen(orientName);
    /* `mode SP orient NUL`. */
    need = modeLen + 1u + orientLen + 1u;
    if (capacity < need) {
        *outLength = need;
        return RoboNotSupported;
    }
    memcpy(buffer, modeName, modeLen);
    buffer[modeLen] = ' ';
    memcpy(buffer + modeLen + 1u, orientName, orientLen);
    buffer[need - 1u] = '\0';
    *outLength = need - 1u;
    return RoboOk;
}

int RoboTextWritingModeIsValid(RoboTextWritingMode mode, RoboTextTextOrientation orientation)
{
    return RoboTextWritingModeIsKnown(mode) && RoboTextTextOrientationIsKnown(orientation);
}
