/*
 * RoboText's Unicode integration (Phase 12.5) — see RoboTextUnicode.h.
 *
 * One file, four ideas, and each is a *wrapper* rather than an implementation: the code-point vocabulary,
 * the property doors, the decoder and the direction classifier. The point of the file is what is **not**
 * in it — no Unicode table, no script range, no case fold, no normalization — and the header argues why.
 *
 * The decoder's invalid-input policy is the one decision worth reading in code. `UnicodeUtf8Decode` in
 * `RoboUnicodeReplace` mode never fails: every ill-formed byte yields one U+FFFD and a `next` one byte on,
 * so an unterminated tail becomes one replacement per byte. The only thing this file adds is the
 * *distinction* between a genuine U+FFFD and a replacement produced by bad bytes, which it recovers by
 * re-running the decoder in STRICT mode **only for a decoded U+FFFD** — the common path pays nothing, and a
 * caller that wants to count malformed input gets an answer rather than a guess.
 */

#include "RoboText/RoboTextUnicode.h"

#include "RoboMem/RoboMem.h"

#include <string.h>

/* ------------------------------------------------------------------ */
/* Code points                                                         */
/* ------------------------------------------------------------------ */

int RoboTextCodePointIsValid(RoboTextCodePoint codePoint)
{
    return codePoint <= RoboTextCodePointMax;
}

int RoboTextCodePointIsHighSurrogate(RoboTextCodePoint codePoint)
{
    return codePoint >= RoboTextCodePointHighSurrogateFirst &&
           codePoint <= RoboTextCodePointHighSurrogateLast;
}

int RoboTextCodePointIsLowSurrogate(RoboTextCodePoint codePoint)
{
    return codePoint >= RoboTextCodePointLowSurrogateFirst &&
           codePoint <= RoboTextCodePointLowSurrogateLast;
}

int RoboTextCodePointIsSurrogate(RoboTextCodePoint codePoint)
{
    return RoboTextCodePointIsHighSurrogate(codePoint) || RoboTextCodePointIsLowSurrogate(codePoint);
}

int RoboTextCodePointIsScalar(RoboTextCodePoint codePoint)
{
    return RoboTextCodePointIsValid(codePoint) && !RoboTextCodePointIsSurrogate(codePoint);
}

int RoboTextCodePointIsNoncharacter(RoboTextCodePoint codePoint)
{
    if (!RoboTextCodePointIsValid(codePoint)) {
        return 0;
    }
    /*
     * The two permanent noncharacter ranges, spelled out rather than looked up: Unicode guarantees these
     * 66 code points are never assigned, so the rule is stable and does not need a table. `U+FFFE`/`U+FFFF`
     * at the end of *every* plane is the second range, which is what the `0xFFFE` mask tests.
     */
    if (codePoint >= 0xFDD0u && codePoint <= 0xFDEFu) {
        return 1;
    }
    return (codePoint & 0xFFFEu) == 0xFFFEu;
}

RoboTextCodePoint RoboTextCodePointToScalar(RoboTextCodePoint codePoint)
{
    return RoboTextCodePointIsScalar(codePoint) ? codePoint : RoboTextCodePointReplacement;
}

/* ------------------------------------------------------------------ */
/* General category                                                    */
/* ------------------------------------------------------------------ */

int RoboTextCategoryIsKnown(RoboTextCategory category)
{
    return (int)category >= 0 && (int)category < (int)RoboTextCategoryCount;
}

const char *RoboTextCategoryName(RoboTextCategory category)
{
    switch (category) {
        case RoboTextCategoryLetter:
            return "letter";
        case RoboTextCategoryMark:
            return "mark";
        case RoboTextCategoryNumber:
            return "number";
        case RoboTextCategoryPunctuation:
            return "punctuation";
        case RoboTextCategorySymbol:
            return "symbol";
        case RoboTextCategorySeparator:
            return "separator";
        case RoboTextCategoryControl:
            return "control";
        case RoboTextCategoryFormat:
            return "format";
        case RoboTextCategoryOther:
        default:
            return "other";
    }
}

RoboTextCategory RoboTextCategoryGroup(UnicodeGeneralCategory generalCategory)
{
    switch (generalCategory) {
        case RoboUGcLu:
        case RoboUGcLl:
        case RoboUGcLt:
        case RoboUGcLm:
        case RoboUGcLo:
            return RoboTextCategoryLetter;
        case RoboUGcMn:
        case RoboUGcMc:
        case RoboUGcMe:
            return RoboTextCategoryMark;
        case RoboUGcNd:
        case RoboUGcNl:
        case RoboUGcNo:
            return RoboTextCategoryNumber;
        case RoboUGcPc:
        case RoboUGcPd:
        case RoboUGcPs:
        case RoboUGcPe:
        case RoboUGcPi:
        case RoboUGcPf:
        case RoboUGcPo:
            return RoboTextCategoryPunctuation;
        case RoboUGcSm:
        case RoboUGcSc:
        case RoboUGcSk:
        case RoboUGcSo:
            return RoboTextCategorySymbol;
        case RoboUGcZs:
        case RoboUGcZl:
        case RoboUGcZp:
            return RoboTextCategorySeparator;
        case RoboUGcCc:
            return RoboTextCategoryControl;
        case RoboUGcCf:
            return RoboTextCategoryFormat;
        case RoboUGcCs:
        case RoboUGcCo:
        case RoboUGcCn:
        default:
            return RoboTextCategoryOther;
    }
}

/* ------------------------------------------------------------------ */
/* Properties                                                          */
/* ------------------------------------------------------------------ */

UnicodeGeneralCategory RoboTextUnicodeGeneralCategory(RoboTextCodePoint codePoint)
{
    /* A code point that is not a scalar is not a character; `Cn` is the UCD's own answer for "no
     * character here", and it is the same answer the tables give for a surrogate. */
    if (!RoboTextCodePointIsScalar(codePoint)) {
        return RoboUGcCn;
    }
    return UnicodeCategory((Uchar32)codePoint);
}

RoboTextCategory RoboTextUnicodeCategory(RoboTextCodePoint codePoint)
{
    return RoboTextCategoryGroup(RoboTextUnicodeGeneralCategory(codePoint));
}

int RoboTextUnicodeAsciiScript(RoboTextCodePoint codePoint, RoboTextScript *outScript)
{
    if (!outScript || codePoint > 0x7Fu) {
        return 0;
    }
    /* The block is frozen: the ASCII letters are `Latn` and everything else below U+0080 is `Zyyy`. */
    *outScript = ((codePoint >= (RoboTextCodePoint)'A' && codePoint <= (RoboTextCodePoint)'Z') ||
                  (codePoint >= (RoboTextCodePoint)'a' && codePoint <= (RoboTextCodePoint)'z'))
                     ? RoboUScriptLatin
                     : RoboUScriptCommon;
    return 1;
}

UnicodeScript RoboTextUnicodeScript(RoboTextCodePoint codePoint)
{
    RoboTextScript ascii;

    if (RoboTextUnicodeAsciiScript(codePoint, &ascii)) {
        return ascii;
    }
    if (!RoboTextCodePointIsScalar(codePoint)) {
        return RoboUScriptUnknown;
    }
    return MakeUnicodeScript((Uchar32)codePoint);
}

UnicodeBidiClass RoboTextUnicodeBidiClass(RoboTextCodePoint codePoint)
{
    if (!RoboTextCodePointIsScalar(codePoint)) {
        return RoboUBcL;
    }
    return MakeUnicodeBidiClass((Uchar32)codePoint);
}

int RoboTextUnicodeIsCombiningMark(RoboTextCodePoint codePoint)
{
    RoboTextCategory category;

    if (codePoint < 0x0300u) {
        /* Unicode's first mark is U+0300, so every code point below it — ASCII, Latin-1, controls — is a
         * base character. The bound is the block's, not a table's, and a test proves it against the
         * tables. */
        return 0;
    }
    category = RoboTextUnicodeCategory(codePoint);
    return category == RoboTextCategoryMark;
}

int RoboTextUnicodeIsWhitespace(RoboTextCodePoint codePoint)
{
    if (!RoboTextCodePointIsScalar(codePoint)) {
        return 0;
    }
    return UnicodeIsWhitespace((Uchar32)codePoint);
}

int RoboTextUnicodeIsControl(RoboTextCodePoint codePoint)
{
    return RoboTextUnicodeCategory(codePoint) == RoboTextCategoryControl;
}

int RoboTextUnicodeIsFormat(RoboTextCodePoint codePoint)
{
    return RoboTextUnicodeCategory(codePoint) == RoboTextCategoryFormat;
}

int RoboTextUnicodeIsAssigned(RoboTextCodePoint codePoint)
{
    return RoboTextUnicodeGeneralCategory(codePoint) != RoboUGcCn;
}

unsigned RoboTextUnicodeCombiningClass(RoboTextCodePoint codePoint)
{
    if (!RoboTextCodePointIsScalar(codePoint)) {
        return 0u;
    }
    return UnicodeCombiningClass((Uchar32)codePoint);
}

/* ------------------------------------------------------------------ */
/* Script extensions                                                   */
/* ------------------------------------------------------------------ */

Result RoboTextUnicodeScriptInfoOf(RoboTextCodePoint codePoint,
                                   RoboTextUnicodeScriptInfo *out,
                                   RoboTextDiag *diag)
{
    size_t total;

    if (!out) {
        return RoboTextDiagMsg(diag, RoboTextDomainFont, RoboInvalidArgument,
                               "an out record is required");
    }
    memset(out, 0, sizeof(*out));
    out->primaryScript = RoboTextUnicodeScript(codePoint);
    if (!RoboTextCodePointIsScalar(codePoint)) {
        /* Not a character: `Unknown` with the singleton set, which is the same shape a scalar with no
         * explicit extension gets. */
        out->extensions[0] = out->primaryScript;
        out->extensionCount = 1u;
        return RoboOk;
    }
    total = UnicodeScriptExtensions((Uchar32)codePoint, out->extensions, RoboTextScriptExtensionsMax);
    if (total == 0u) {
        /* Guaranteed >= 1 for an in-range scalar, but a table gap would otherwise leave an empty set, and
         * an empty set would make resolution treat the scalar as linking to nothing. */
        out->extensions[0] = out->primaryScript;
        total = 1u;
    }
    out->extensionsTruncated = (total > (size_t)RoboTextScriptExtensionsMax) ? 1 : 0;
    out->extensionCount = (total > (size_t)RoboTextScriptExtensionsMax) ? (size_t)RoboTextScriptExtensionsMax
                                                                        : total;
    /* Explicit when the set says something the primary script does not: a single entry equal to the
     * primary is the defaulted property, not a listed set. */
    out->extensionsExplicit =
        (out->extensionCount == 1u && out->extensions[0] == out->primaryScript) ? 0 : 1;
    return RoboOk;
}

size_t RoboTextUnicodeScriptExtensions(RoboTextCodePoint codePoint,
                                       RoboTextScript *out,
                                       size_t capacity)
{
    if (!RoboTextCodePointIsScalar(codePoint)) {
        return 0u;
    }
    return UnicodeScriptExtensions((Uchar32)codePoint, out, capacity);
}

int RoboTextUnicodeScriptInfoHas(const RoboTextUnicodeScriptInfo *info, RoboTextScript script)
{
    if (!info) {
        return 0;
    }
    for (size_t index = 0u; index < info->extensionCount; index++) {
        if (info->extensions[index] == script) {
            return 1;
        }
    }
    return 0;
}

int RoboTextUnicodeScriptExtensionsInclude(RoboTextCodePoint codePoint, RoboTextScript script)
{
    RoboTextScript ascii;

    if (RoboTextUnicodeAsciiScript(codePoint, &ascii)) {
        /* No ASCII code point lists an explicit set, so `Script_Extensions` defaults to `{Script}` and
         * membership is equality. The suite proves the assumption over the whole block. */
        return ascii == script;
    }
    if (!RoboTextCodePointIsScalar(codePoint)) {
        return 0;
    }
    return UnicodeHasScript((Uchar32)codePoint, script);
}

const char *RoboTextUnicodeDataVersion(void)
{
    /*
     * RoboUnicode's own pin (Unicode 16.0.0), read through its function rather than its macro so that a
     * later build whose tables move to a new UCD version reports the new one here without a second edit.
     */
    return UnicodeUcdVersionString();
}

/* ------------------------------------------------------------------ */
/* Decoder                                                             */
/* ------------------------------------------------------------------ */

/** Non-zero when a view can be read at all: bytes, or a length of zero. */
static int UnicodeViewIsSound(RoboTextView view)
{
    return view.bytes != NULL || view.length == 0u;
}

/**
 * Decode one scalar at @p offset under the module's policy, reporting whether it was replaced.
 *
 * `RoboUnicodeReplace` cannot fail, so the only error is the decoder's own refusal to advance — which the
 * `next <= offset` guard turns into a failure rather than a loop that never ends. The STRICT re-run happens
 * only when the replacement character came back, and it is what separates "the text says U+FFFD" from "the
 * text was malformed".
 */
static Result UnicodeDecodeAt(RoboTextView view,
                              size_t offset,
                              RoboTextCodePoint *outCodePoint,
                              size_t *outNext,
                              int *outReplaced)
{
    Uchar32 codePoint = 0;
    size_t next = offset;

    if (offset >= view.length) {
        return RoboInvalidArgument;
    }
    if (UnicodeUtf8Decode(view.bytes, view.length, offset, RoboUnicodeReplace, &codePoint, &next) !=
            RoboUnicodeOk ||
        next <= offset) {
        return RoboParseError;
    }
    if (outReplaced) {
        if ((RoboTextCodePoint)codePoint == RoboTextCodePointReplacement) {
            Uchar32 strict = 0;
            size_t strictNext = offset;

            *outReplaced =
                UnicodeUtf8Decode(view.bytes, view.length, offset, RoboUnicodeStrict, &strict,
                                  &strictNext) != RoboUnicodeOk;
        } else {
            *outReplaced = 0;
        }
    }
    if (outCodePoint) {
        *outCodePoint = (RoboTextCodePoint)codePoint;
    }
    if (outNext) {
        *outNext = next;
    }
    return RoboOk;
}

Result RoboTextUnicodeDecoderInit(RoboTextUnicodeDecoder *decoder, RoboTextView view, RoboTextDiag *diag)
{
    if (!decoder) {
        return RoboTextDiagMsg(diag, RoboTextDomainBuffer, RoboInvalidArgument,
                               "a decoder is required");
    }
    memset(decoder, 0, sizeof(*decoder));
    if (!UnicodeViewIsSound(view)) {
        return RoboTextDiagMsg(diag, RoboTextDomainBuffer, RoboInvalidArgument,
                               "a view with a length needs bytes");
    }
    decoder->view = view;
    return RoboOk;
}

Result RoboTextUnicodeDecoderReset(RoboTextUnicodeDecoder *decoder, RoboTextView view, RoboTextDiag *diag)
{
    return RoboTextUnicodeDecoderInit(decoder, view, diag);
}

int RoboTextUnicodeDecoderHasNext(const RoboTextUnicodeDecoder *decoder)
{
    return decoder != NULL && decoder->offset < decoder->view.length;
}

RoboTextCodePoint RoboTextUnicodeDecoderPeek(RoboTextUnicodeDecoder *decoder)
{
    RoboTextCodePoint codePoint = RoboTextCodePointReplacement;
    size_t next = 0u;

    if (!decoder || !RoboTextUnicodeDecoderHasNext(decoder)) {
        return RoboTextCodePointReplacement;
    }
    if (decoder->hasPeek) {
        return decoder->peek;
    }
    if (UnicodeDecodeAt(decoder->view, decoder->offset, &codePoint, &next, NULL) != RoboOk) {
        return RoboTextCodePointReplacement;
    }
    decoder->peekOffset = decoder->offset;
    decoder->peek = codePoint;
    decoder->hasPeek = 1;
    return codePoint;
}

RoboTextCodePoint RoboTextUnicodeDecoderNext(RoboTextUnicodeDecoder *decoder)
{
    RoboTextCodePoint codePoint = RoboTextCodePointReplacement;
    size_t next = 0u;
    int replaced = 0;

    if (!decoder) {
        return RoboTextCodePointReplacement;
    }
    if (!RoboTextUnicodeDecoderHasNext(decoder)) {
        /* Exhaustion is not an error and not a character: no advance, no consumed range, no replacement
         * flag, so a caller that ignores `HasNext` cannot spiral and cannot mis-count. */
        decoder->lastStart = decoder->view.length;
        decoder->lastEnd = decoder->view.length;
        decoder->lastReplaced = 0;
        return RoboTextCodePointReplacement;
    }
    if (decoder->hasPeek && decoder->peekOffset == decoder->offset) {
        codePoint = decoder->peek;
        (void)UnicodeDecodeAt(decoder->view, decoder->offset, NULL, &next, &replaced);
    } else if (UnicodeDecodeAt(decoder->view, decoder->offset, &codePoint, &next, &replaced) != RoboOk) {
        /* The decoder refused to advance — the one failure `RoboUnicodeReplace` cannot produce for
         * ill-formed bytes, so it is reported as exhausted rather than as a character. */
        decoder->lastStart = decoder->offset;
        decoder->lastEnd = decoder->offset;
        decoder->lastReplaced = 0;
        return RoboTextCodePointReplacement;
    }
    decoder->lastStart = decoder->offset;
    decoder->lastEnd = next;
    decoder->lastReplaced = replaced;
    decoder->offset = next;
    decoder->index++;
    decoder->hasPeek = 0;
    return codePoint;
}

RoboTextScalarIndex RoboTextUnicodeDecoderIndex(const RoboTextUnicodeDecoder *decoder)
{
    return decoder ? decoder->index : 0u;
}

Result RoboTextUnicodeDecoderLastRange(const RoboTextUnicodeDecoder *decoder,
                                       RoboTextByteRange *out,
                                       RoboTextDiag *diag)
{
    if (!decoder || !out) {
        return RoboTextDiagMsg(diag, RoboTextDomainRange, RoboInvalidArgument,
                               "a decoder and an out range are required");
    }
    out->start = (RoboTextByteIndex)decoder->lastStart;
    out->end = (RoboTextByteIndex)decoder->lastEnd;
    return RoboOk;
}

int RoboTextUnicodeDecoderLastWasReplaced(const RoboTextUnicodeDecoder *decoder)
{
    return decoder ? decoder->lastReplaced : 0;
}

Result RoboTextUnicodeDecodeAll(RoboTextView view,
                                Uchar32 **outScalars,
                                RoboTextScalarIndex *outCount,
                                RoboTextDiag *diag)
{
    RoboTextUnicodeDecoder decoder;
    size_t count = 0u;
    Uchar32 *scalars;
    Result status;

    if (!outScalars || !outCount) {
        return RoboTextDiagMsg(diag, RoboTextDomainBuffer, RoboInvalidArgument,
                               "two out pointers are required");
    }
    *outScalars = NULL;
    *outCount = 0u;
    status = RoboTextUnicodeDecoderInit(&decoder, view, diag);
    if (status != RoboOk) {
        return status;
    }
    if (view.length == 0u) {
        return RoboOk;
    }

    /* First walk: count, so the second can allocate exactly once. */
    while (RoboTextUnicodeDecoderHasNext(&decoder)) {
        if (RoboTextUnicodeDecoderNext(&decoder) == RoboTextCodePointReplacement &&
            decoder.lastEnd <= decoder.lastStart) {
            return RoboTextDiagMsg(diag, RoboTextDomainBuffer, RoboParseError,
                                   "the text could not be decoded");
        }
        count++;
    }
    if (count > (size_t)RoboTextIndexMax) {
        return RoboTextDiagMsg(diag, RoboTextDomainBuffer, RoboInvalidArgument,
                               "the text is longer than this build can index");
    }

    scalars = (Uchar32 *)RoboCalloc(count, sizeof(*scalars));
    if (!scalars) {
        return RoboTextDiagMsg(diag, RoboTextDomainBuffer, RoboOutOfMemory,
                               "the decoded text could not be allocated");
    }
    RoboTextUnicodeDecoderInit(&decoder, view, NULL);
    for (size_t index = 0u; index < count; index++) {
        scalars[index] = (Uchar32)RoboTextUnicodeDecoderNext(&decoder);
    }
    *outScalars = scalars;
    *outCount = (RoboTextScalarIndex)count;
    return RoboOk;
}

void RoboTextUnicodeScalarsFree(Uchar32 *scalars)
{
    RoboFree(scalars);
}

/* ------------------------------------------------------------------ */
/* Direction classification                                            */
/* ------------------------------------------------------------------ */

RoboTextDirection RoboTextUnicodeStrongDirection(RoboTextCodePoint codePoint)
{
    switch (RoboTextUnicodeBidiClass(codePoint)) {
        case RoboUBcL:
            return RoboTextDirectionLtr;
        case RoboUBcR:
        case RoboUBcAl:
            return RoboTextDirectionRtl;
        default:
            return RoboTextDirectionUnknown;
    }
}

RoboTextDirection RoboTextUnicodeBaseDirection(const Uchar32 *scalars,
                                              size_t count,
                                              RoboTextDirection declared)
{
    if (declared != RoboTextDirectionUnknown) {
        return declared;
    }
    if (!scalars && count != 0u) {
        return RoboTextDirectionUnknown;
    }
    for (size_t index = 0u; index < count; index++) {
        RoboTextDirection strong = RoboTextUnicodeStrongDirection((RoboTextCodePoint)scalars[index]);

        if (strong != RoboTextDirectionUnknown) {
            return strong;
        }
        switch (RoboTextUnicodeBidiClass((RoboTextCodePoint)scalars[index])) {
            case RoboUBcLri:
            case RoboUBcRli:
            case RoboUBcFsi:
                /*
                 * An isolate initiator before any strong scalar means P2 has contents to **skip**, and
                 * skipping them needs the algorithm's own state — so that door exists in RoboUnicode and this
                 * one defers to it. The deferral is the only path here that allocates, and it is the rare one:
                 * an ordinary paragraph is answered by the scan above, which is why a text layer can resolve a
                 * paragraph direction per frame without touching the heap.
                 */
                return (UnicodeBidiParagraphDirection(scalars, count) == RoboBidiRtl)
                           ? RoboTextDirectionRtl
                           : RoboTextDirectionLtr;
            default:
                break;
        }
    }
    /* Nothing strong and nothing declared: P3's default is the *algorithm's*, and this door reports the
     * honest "not decided" instead, which the caller resolves against its own fallback. */
    return RoboTextDirectionUnknown;
}

RoboTextDirection RoboTextUnicodeClassifyDirection(RoboTextCodePoint codePoint, RoboTextDirection base)
{
    RoboTextDirection strong = RoboTextUnicodeStrongDirection(codePoint);

    return (strong == RoboTextDirectionUnknown) ? base : strong;
}
