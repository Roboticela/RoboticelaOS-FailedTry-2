/*
 * RoboText's Unicode integration (Phase 12.5) — the module's one decoder, one code-point vocabulary and
 * one property interface, each of them a *window onto RoboUnicode* rather than a second copy of it.
 *
 * ## Why every door here is a wrapper
 *
 * Phase 4 owns the Unicode Character Database: the tables, the encoding rules and the normalization data.
 * This phase's whole risk is that a text layer, needing "just" a script lookup and "just" a UTF-8
 * increment, grows its own three-hundred-line decoder and its own script range table — and then the two
 * copies disagree about U+00AD, or about how many scalars a five-byte sequence is. So nothing in this
 * module reads a `RoboUcdData.h` table directly. Every property below is one call into RoboUnicode:
 *
 *     RoboTextUnicodeGeneralCategory(cp)   →  UnicodeCategory(cp)
 *     RoboTextUnicodeScript(cp)            →  MakeUnicodeScript(cp)
 *     RoboTextUnicodeBidiClass(cp)         →  MakeUnicodeBidiClass(cp)
 *     RoboTextUnicodeIsWhitespace(cp)      →  UnicodeIsWhitespace(cp)
 *
 * and the decoder is `UnicodeUtf8Decode` in `RoboUnicodeReplace` mode, wrapped in an iterator so a
 * caller walks a buffer instead of re-deriving `next` at every use site. The *policy* is this module's
 * (see below); the *rules* are Phase 4's.
 *
 * ## The invalid-UTF-8 policy, stated once
 *
 * **Every ill-formed byte decodes to exactly one U+FFFD REPLACEMENT CHARACTER and the decoder advances
 * one byte and continues.** An unterminated multi-byte sequence at the end of a buffer therefore becomes
 * one replacement *per byte* rather than one for the whole tail, and a lead byte with a bad continuation
 * byte becomes one replacement followed by whatever the next byte turns out to be. That is
 * `RoboUnicodeReplace`'s policy, spelled out at its implementation, and it is the same one
 * `RoboTextBuffer` already applies and the same one the HTML tokenizer upstream applied before the text
 * reached the DOM. The point of naming it here is that it must be *identical* in measurement, rendering,
 * selection and any future editing path: a buffer that replaced a lone surrogate while a measurer refused
 * it would produce a caret position the other two cannot express.
 *
 * `RoboTextUnicodeDecoderLastWasReplaced` is the one addition: a scalar of U+FFFD that came from a
 * *genuine* U+FFFD and one that came from malformed bytes are the same character and different facts, and
 * a caller that wants to count the latter (a validator, a dump) can.
 *
 * ## Code points and scalars
 *
 * Unicode distinguishes a **code point** (any integer 0..U+10FFFF, surrogates included) from a **scalar
 * value** (a code point that is not a surrogate). UTF-8 can only ever carry scalars, so a text layer's
 * `Next()` always returns one; but the *validation* doors exist because a caller that built a code point
 * by hand — from a UTF-16 unit, from an escape, from a decoder of its own — must be able to ask which it
 * has. `RoboTextCodePointIsValid` is the code-point test and `RoboTextCodePointIsScalar` the scalar one,
 * and the surrogate predicates are the reason they are not the same test.
 *
 * ## Script resolution is classification, and it is here rather than in the shaper
 *
 * `RoboTextScriptRunsResolve` is the module's one implementation of the neutrality rule: a scalar whose
 * script is **Common** or **Inherited** does not get a run of its own — it takes the previous resolved
 * script, and a scalar *before* the first resolved script takes the next one. That single forward pass is
 * what makes `"abc 123 def"` one script run rather than three, and it lives here (rather than inside the
 * shaping code) because Phase 12.6 makes script segmentation a stage of its own and must not have to
 * re-derive the rule to do it.
 *
 * ## What is deliberately not here
 *
 * No normalization, no case mapping, no grapheme/word/line segmentation, no reordering and no bidi
 * resolution. Phase 4 owns all of them; Phase 12.8 owns the bidirectional algorithm. What this header
 * produces is the *input* to those algorithms — a per-scalar script and a per-scalar strong direction —
 * and it says so.
 */
#ifndef ROBO_TEXT_UNICODE_H
#define ROBO_TEXT_UNICODE_H

#include "RoboBase/RoboError.h"
#include "RoboText/RoboTextBuffer.h"
#include "RoboText/RoboTextDiagnostics.h"
#include "RoboText/RoboTextTypes.h"
#include "RoboUnicode/RoboBidiData.h"
#include "RoboUnicode/RoboUcdData.h"
#include "RoboUnicode/RoboUnicode.h"
#include "RoboUnicode/RoboUnicodeBidi.h"
#include "RoboUnicode/RoboUnicodeProps.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------ */
/* Code points                                                         */
/* ------------------------------------------------------------------ */

/** A Unicode code point: an integer in `0 .. 0x10FFFF`, surrogates included. */
typedef uint32_t RoboTextCodePoint;

/** The largest code point Unicode defines. */
#define RoboTextCodePointMax ((RoboTextCodePoint)0x10FFFFu)

/** The replacement character every invalid byte sequence decodes to. */
#define RoboTextCodePointReplacement ((RoboTextCodePoint)0xFFFDu)

/** First UTF-16 high surrogate. */
#define RoboTextCodePointHighSurrogateFirst ((RoboTextCodePoint)0xD800u)
/** Last UTF-16 high surrogate. */
#define RoboTextCodePointHighSurrogateLast ((RoboTextCodePoint)0xDBFFu)
/** First UTF-16 low surrogate. */
#define RoboTextCodePointLowSurrogateFirst ((RoboTextCodePoint)0xDC00u)
/** Last UTF-16 low surrogate. */
#define RoboTextCodePointLowSurrogateLast ((RoboTextCodePoint)0xDFFFu)

/** Non-zero when @p codePoint is in `0 .. U+10FFFF` — the code-point test, surrogates allowed. */
int RoboTextCodePointIsValid(RoboTextCodePoint codePoint);

/** Non-zero when @p codePoint is a Unicode **scalar value**: valid and not a surrogate. */
int RoboTextCodePointIsScalar(RoboTextCodePoint codePoint);

/** Non-zero when @p codePoint is a UTF-16 high surrogate. Always non-scalar. */
int RoboTextCodePointIsHighSurrogate(RoboTextCodePoint codePoint);

/** Non-zero when @p codePoint is a UTF-16 low surrogate. Always non-scalar. */
int RoboTextCodePointIsLowSurrogate(RoboTextCodePoint codePoint);

/** Non-zero when @p codePoint is either surrogate. */
int RoboTextCodePointIsSurrogate(RoboTextCodePoint codePoint);

/** Non-zero when @p codePoint is a Unicode noncharacter (permanent non-scalar-for-text range). */
int RoboTextCodePointIsNoncharacter(RoboTextCodePoint codePoint);

/** The code point as a scalar: @p codePoint when it is one, else U+FFFD. */
RoboTextCodePoint RoboTextCodePointToScalar(RoboTextCodePoint codePoint);

/* ------------------------------------------------------------------ */
/* General category                                                    */
/* ------------------------------------------------------------------ */

/**
 * The general-category *group* a scalar belongs to.
 *
 * The nine groups the text layer reasons about, and deliberately a **grouping** rather than a replacement
 * for `UnicodeGeneralCategory`: `RoboTextUnicodeGeneralCategory` still answers the detailed category, so
 * collapsing to a group is a caller's choice at the use site rather than information lost on the way in.
 */
typedef enum RoboTextCategory {
    RoboTextCategoryLetter = 0,
    RoboTextCategoryMark = 1,
    RoboTextCategoryNumber = 2,
    RoboTextCategoryPunctuation = 3,
    RoboTextCategorySymbol = 4,
    RoboTextCategorySeparator = 5,
    RoboTextCategoryControl = 6,
    RoboTextCategoryFormat = 7,
    RoboTextCategoryOther = 8
} RoboTextCategory;

/** Number of defined category groups. */
#define RoboTextCategoryCount 9u

/** Non-zero if @p category is a defined group. */
int RoboTextCategoryIsKnown(RoboTextCategory category);

/** Lower-case name of @p category (`"letter"`, …); never NULL. */
const char *RoboTextCategoryName(RoboTextCategory category);

/** The group @p generalCategory belongs to. `RoboUGcCn` (unassigned) is `Other`. */
RoboTextCategory RoboTextCategoryGroup(UnicodeGeneralCategory generalCategory);

/* ------------------------------------------------------------------ */
/* Properties (the UnicodeProperties interface)                        */
/* ------------------------------------------------------------------ */

/** The general category of @p codePoint, from RoboUnicode's tables. */
UnicodeGeneralCategory RoboTextUnicodeGeneralCategory(RoboTextCodePoint codePoint);

/** The category *group* of @p codePoint. */
RoboTextCategory RoboTextUnicodeCategory(RoboTextCodePoint codePoint);

/** The `Script` property of @p codePoint (not Script_Extensions; see UAX #24). */
UnicodeScript RoboTextUnicodeScript(RoboTextCodePoint codePoint);

/** The `Bidi_Class` of @p codePoint. */
UnicodeBidiClass RoboTextUnicodeBidiClass(RoboTextCodePoint codePoint);

/**
 * Non-zero when @p codePoint is a combining mark.
 *
 * A mark is a scalar whose general category is `Mn` (non-spacing), `Mc` (spacing combining) or `Me`
 * (enclosing). This is the classification the fallback pass keys on: a mark **must not** pick a fallback
 * face of its own, because a base glyph from one face and its accent from another is the exact failure
 * that makes a later shaping pass unable to attach them.
 */
int RoboTextUnicodeIsCombiningMark(RoboTextCodePoint codePoint);

/** Non-zero when @p codePoint is whitespace (`Zs`, tab, newline, form feed, carriage return, NEL). */
int RoboTextUnicodeIsWhitespace(RoboTextCodePoint codePoint);

/** Non-zero when @p codePoint's category is `Cc` — a control character. */
int RoboTextUnicodeIsControl(RoboTextCodePoint codePoint);

/** Non-zero when @p codePoint's category is `Cf` — a format character (ZWJ, BOM, bidi controls). */
int RoboTextUnicodeIsFormat(RoboTextCodePoint codePoint);

/** Non-zero when @p codePoint is assigned (its category is not `Cn`). */
int RoboTextUnicodeIsAssigned(RoboTextCodePoint codePoint);

/** The canonical combining class of @p codePoint (0 for a base character). */
unsigned RoboTextUnicodeCombiningClass(RoboTextCodePoint codePoint);

/**
 * The Unicode data version the runtime's tables were generated from.
 *
 * The **same** string RoboUnicode reports, deliberately: this build claims no version its tables do not
 * have, and a text layer that pinned its own would be a second version to keep in step. Never NULL.
 */
const char *RoboTextUnicodeDataVersion(void);

/* ------------------------------------------------------------------ */
/* Decoder                                                             */
/* ------------------------------------------------------------------ */

/**
 * A forward UTF-8 decoder over a borrowed view.
 *
 * A **value** with no allocation: `RoboTextUnicodeDecoderInit` points it at bytes and every door below
 * walks them. That matters because the ring of consumers — measurement, segmentation, a diagnostic —
 * each decode the *same* text, and a decoder that allocated would make each of them a copy.
 *
 * The decoder holds two positions: a byte offset into the view, and the **scalar index** of the next
 * scalar it will produce (equal to the number of scalars already produced). `Index()` is therefore the
 * caret-style answer — the count so far — and it is total at the end of the text, exactly as the buffer's
 * `ScalarToByte` is total one past the last scalar.
 */
typedef struct RoboTextUnicodeDecoder {
    RoboTextView view;
    /** Byte offset of the next scalar. */
    size_t offset;
    /** Scalars produced so far; also the index `Next()` will return. */
    RoboTextScalarIndex index;
    /** Byte offset of the scalar `Peek()` has decoded, when `hasPeek`. */
    size_t peekOffset;
    RoboTextCodePoint peek;
    int hasPeek;
    /** Byte range of the most recent `Next()`; empty before the first. */
    size_t lastStart;
    size_t lastEnd;
    /** Non-zero when the most recent `Next()` replaced an ill-formed sequence. */
    int lastReplaced;
} RoboTextUnicodeDecoder;

/**
 * Point @p decoder at @p view.
 *
 * @return RoboOk; RoboInvalidArgument for a NULL @p decoder or an unsound view (bytes absent with a
 *         non-zero length). The bytes are borrowed and must outlive the decoder.
 */
Result RoboTextUnicodeDecoderInit(RoboTextUnicodeDecoder *decoder, RoboTextView view, RoboTextDiag *diag);

/** Non-zero when another scalar can be produced. Safe on NULL (0). */
int RoboTextUnicodeDecoderHasNext(const RoboTextUnicodeDecoder *decoder);

/**
 * The next scalar without consuming it.
 *
 * The three-exit door: U+FFFD when exhausted or on a NULL decoder, and the decoded scalar otherwise. A
 * caller that needs to tell exhaustion from a genuine replacement character uses
 * @ref RoboTextUnicodeDecoderHasNext first, which is why it exists.
 */
RoboTextCodePoint RoboTextUnicodeDecoderPeek(RoboTextUnicodeDecoder *decoder);

/** Consume and return the next scalar; U+FFFD and no advance when exhausted. */
RoboTextCodePoint RoboTextUnicodeDecoderNext(RoboTextUnicodeDecoder *decoder);

/** The scalar index the next `Next()` will return (the count so far). Safe on NULL (0). */
RoboTextScalarIndex RoboTextUnicodeDecoderIndex(const RoboTextUnicodeDecoder *decoder);

/** Re-point @p decoder at the whole of @p view, discarding any position. */
Result RoboTextUnicodeDecoderReset(RoboTextUnicodeDecoder *decoder, RoboTextView view, RoboTextDiag *diag);

/**
 * The byte span the most recent `Next()` consumed, as `[start, end)`.
 *
 * Empty before the first `Next()`, which is a *position* — the same convention the range algebra uses.
 */
Result RoboTextUnicodeDecoderLastRange(const RoboTextUnicodeDecoder *decoder,
                                       RoboTextByteRange *out,
                                       RoboTextDiag *diag);

/** Non-zero when the most recent `Next()` produced U+FFFD **from ill-formed bytes**. Safe on NULL (0). */
int RoboTextUnicodeDecoderLastWasReplaced(const RoboTextUnicodeDecoder *decoder);

/**
 * Decode the whole of @p view into a newly allocated scalar array.
 *
 * The module's **one** "decode everything" door, shared by script resolution, direction classification and
 * the run set, so that three callers cannot decode the same bytes three slightly different ways. Two walks
 * and one allocation: the first counts (so the second can allocate exactly), which is what keeps a
 * document's text from being a quadratic decode.
 *
 * @param outScalars receives a `RoboCalloc` array the caller frees with @ref RoboTextUnicodeScalarsFree;
 *                   NULL for an empty view.
 * @param outCount   receives the scalar count.
 *
 * @return RoboOk; RoboInvalidArgument for a NULL out or an unsound view; RoboParseError only for the
 *         decoder's own failure to advance (which an ill-formed sequence, by policy, does not cause);
 *         RoboOutOfMemory.
 */
Result RoboTextUnicodeDecodeAll(RoboTextView view,
                                Uchar32 **outScalars,
                                RoboTextScalarIndex *outCount,
                                RoboTextDiag *diag);

/** Release an array from @ref RoboTextUnicodeDecodeAll. Safe on NULL. */
void RoboTextUnicodeScalarsFree(Uchar32 *scalars);

/* ------------------------------------------------------------------ */
/* Direction classification                                            */
/* ------------------------------------------------------------------ */

/**
 * The direction a scalar's own `Bidi_Class` classifies as, **before** neutrals are resolved.
 *
 * `R` and `AL` classify right-to-left, `L` left-to-right, and everything else — digits, punctuation,
 * spaces, format controls — is `RoboTextDirectionUnknown`, which is not a failure: it is the honest answer
 * for a scalar that carries no strong direction, and the caller resolves it against the paragraph base.
 */
RoboTextDirection RoboTextUnicodeStrongDirection(RoboTextCodePoint codePoint);

/**
 * The paragraph direction @p scalars is classified against.
 *
 * The declared base wins; otherwise the text's own first strong scalar; otherwise `Unknown`. The third case
 * is why `UnicodeBidiParagraphDirection` alone is not the answer — it reports `RoboLtr` when the text has no
 * strong scalar, and taking that at face value would call `"123 456"` a left-to-right paragraph, which
 * UAX #9's P2/P3 deliberately does not do.
 */
RoboTextDirection RoboTextUnicodeBaseDirection(const Uchar32 *scalars,
                                              size_t count,
                                              RoboTextDirection declared);

/**
 * The direction a scalar is classified as given the paragraph @p base: strong scalars keep their own
 * direction and every other takes @p base.
 */
RoboTextDirection RoboTextUnicodeClassifyDirection(RoboTextCodePoint codePoint, RoboTextDirection base);

/* ------------------------------------------------------------------ */
/* Script resolution                                                   */
/* ------------------------------------------------------------------ */

/**
 * Resolve the script of every scalar and emit the resulting script runs.
 *
 * @param scalars      the decoded scalars.
 * @param count        how many.
 * @param hint         a caller's script hint, or `RoboTextScriptCommon` for "classify from the scalars".
 * @param outScripts   receives @p count resolved scripts, or NULL to ask only for the runs.
 * @param outRuns      receives the runs, or NULL to ask only for the count.
 * @param runCapacity  slots in @p outRuns.
 * @param outRunCount  receives the runs @p scalars produce (whether or not they fitted).
 *
 * @return RoboOk; RoboInvalidArgument for a NULL argument or a count without scalars; RoboNotSupported
 *         when @p outRuns cannot hold the runs, with @p outRunCount set to the requirement.
 *
 * The neutrality rule (see the header): a Common or Inherited scalar takes the **previous** resolved
 * script, and a scalar before the first resolved script takes the **next** one. One forward pass, and the
 * answer never depends on where the reader is standing.
 */
Result RoboTextScriptRunsResolve(const Uchar32 *scalars,
                                 size_t count,
                                 RoboTextScript hint,
                                 RoboTextScript *outScripts,
                                 RoboTextScriptRun *outRuns,
                                 size_t runCapacity,
                                 size_t *outRunCount,
                                 RoboTextDiag *diag);

/**
 * @ref RoboTextScriptRunsResolve over a view: the convenience a caller with bytes and no scalar array uses.
 *
 * Decodes the view once through @ref RoboTextUnicodeDecodeAll, so it allocates; a caller that already holds
 * the scalars should call the array door above.
 */
Result RoboTextScriptRunsResolveView(RoboTextView view,
                                     RoboTextScript hint,
                                     RoboTextScriptRun *outRuns,
                                     size_t runCapacity,
                                     size_t *outRunCount,
                                     RoboTextDiag *diag);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_TEXT_UNICODE_H */
