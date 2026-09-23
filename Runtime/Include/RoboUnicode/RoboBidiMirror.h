/*
 * Unicode mirrored glyphs — the `Bidi_Mirroring_Glyph` property (UCD 16.0.0).
 *
 * ## What this is, and what it is not
 *
 * UAX #9's rule L4 says that a character whose *resolved* direction is right-to-left is
 * depicted by a mirrored glyph when it has `Bidi_Mirrored=Yes`, and the UCD's
 * `BidiMirroring.txt` lists `Bidi_Mirroring_Glyph` — the code point whose glyph is the
 * mirror image. `(` draws as `)` in an RTL context; `<` draws as `>`; `«` draws as `»`.
 *
 * Two facts follow, and both are this file's contract:
 *
 *   **Mirroring is data, and the UCD owns it.** Nothing here is derived from a script, a
 *   direction or a font. A text layer that kept its own pairing table would be a second
 *   Unicode database to keep in step with the first (Phase 12.8 §"no duplicated Unicode
 *   database"), so the table lives in RoboUnicode and the bidi and glyph layers *ask*.
 *
 *   **Mirroring does not change the text.** The answer is the code point whose glyph to
 *   *draw*, never a substitution in the buffer: `source` stays the original scalar, so a
 *   selection, a copy and an accessibility read all still see `(`. Phase 12.8 §"mirroring
 *   and glyph mapping" is the rule, and `RoboTextGlyph` records both facts side by side.
 *
 * The table is the explicit pairing list, which is what a renderer needs to pick a glyph.
 * A handful of `Bidi_Mirrored=Yes` characters have **no** pairing row — the UCD gives them
 * no `Bidi_Mirroring_Glyph` because no single code point is their mirror image, and their
 * mirroring is a font/italic concern rather than a glyph selection — so they answer "no
 * mirror" here. That is the file's own position rather than a simplification of it.
 *
 * The data is generated: `Tools/ucd/source/BidiMirroring.txt` is vendored, and
 * `Tools/ucd/generate_mirror.py` writes `Source/RoboUnicode/RoboUnicodeBidiMirror.c`
 * from it. Nothing in a normal build runs that script.
 */
#ifndef ROBO_BIDI_MIRROR_H
#define ROBO_BIDI_MIRROR_H

#include "RoboUnicode/RoboUnicode.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** The Unicode data version the pairing table was generated from. Never NULL. */
#define RoboBidiMirrorUcdVersion "16.0.0"

/** One `Bidi_Mirroring_Glyph` row. */
typedef struct BidiMirrorPair {
    /** The code point being mirrored. */
    uint32_t codePoint;
    /** The code point whose glyph depicts it in a right-to-left context. */
    uint32_t mirror;
} BidiMirrorPair;

/**
 * The mirroring code point of @p codePoint, when the UCD lists one.
 *
 * @param outMirror receives the mirroring code point on success and @p codePoint on failure, so a
 *                  caller that ignores the return value still has a usable code point.
 * @return 1 when the UCD lists a distinct mirroring glyph for @p codePoint; 0 otherwise (including a
 *         code point outside Unicode's range, which has no row by construction).
 */
int BidiMirrorLookup(Uchar32 codePoint, Uchar32 *outMirror);

/** Rows in the table. The count a dump or a test reports, not a capacity. */
size_t BidiMirrorPairCountValue(void);

/**
 * Pair @p index in ascending code-point order — what a test walks to check the table is sorted and
 * involutive, and what a dump would print.
 *
 * @return 1 when @p index is a row and the two out pointers were written; 0 when it is past the end.
 */
int BidiMirrorPairAt(size_t index, Uchar32 *outCodePoint, Uchar32 *outMirror);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_BIDI_MIRROR_H */
