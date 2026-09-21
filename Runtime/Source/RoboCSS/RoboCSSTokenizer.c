/*
 * RoboCSS tokenizer (Phase 9.2) — see RoboCSSTokenizer.h.
 *
 * An independent implementation of the CSS Syntax tokenizer, written against
 * the specification and shaped by a reading of Blink's
 * `css_tokenizer.cc` / `css_tokenizer_input_stream.{h,cc}` — which is the
 * reference this runtime keeps under `chromium/`. Where the two differ, the
 * difference is deliberate and named in a comment; where they agree, this
 * implementation says why, because agreeing with a reference implementation for
 * the wrong reason is not a design.
 *
 * What Blink's reading settled, and why:
 *
 *   - Blink **skips** the specification's input-preprocessing step, and its
 *     `CSSTokenizer` constructor says exactly why: it uses HTML whitespace
 *     (which already accepts CR and FF), it does not count whitespace, and its
 *     input stream substitutes U+FFFD for NUL and lone surrogates at read time.
 *     We skip the same step for the same first two reasons, and honour the
 *     third by *classification*: U+0000 is an identifier code point here (see
 *     IsIdentifierStartByte). What we do not do is substitute the
 *     replacement character into a value, because a value whose bytes differ
 *     from the caller's bytes is a lie about the source — so a token that
 *     contains a NUL says so, and the caller can substitute it itself.
 *
 *   - Blink tokenizes comments into the token stream and then skips them, or
 *     hands them back when asked (`SkipComments`). Our model has COMMENT as a
 *     first-class token, and this tokenizer always produces it, so every byte of
 *     the source is covered by exactly one token. Tokens are composed in the
 *     order they appear and their ranges tile the source; that invariant is
 *     asserted by the suites, and it is what makes a source-preserving consumer
 *     (a formatter, a range-based diagnostic) possible at all.
 *
 *   - Blink collapses whitespace before a quoted `url(` argument rather than
 *     emitting it, with the comment "the spec is slightly different so as to
 *     avoid dropping whitespace tokens, but they wouldn't be used and this is
 *     easier". We follow the *spec* here and keep the whitespace as its own
 *     token, because dropping it would break the tiling invariant above. The
 *     consequence is named in the header and pinned by a test:
 *     `url( "x")` is function, whitespace, string, `)`.
 *
 *   - Blink's token vocabulary carries tokens this subsystem's frozen 9.1
 *     vocabulary does not: the two-character attribute-match operators (`*=`,
 *     `^=`, `$=`, `~=`, `|=`), the column token (`||`), and the unicode-range
 *     token (`U+0025`). Those are composed by later phases from DELIM and the
 *     numeric tokens — see each site below for what is produced instead.
 *
 * The structure is one `produce()` switch dispatching to one function per token
 * class, which is the shape of the specification's own "consume a token" and of
 * Blink's `NextToken()`. Unlike Blink there is no pushback buffer: the cursor
 * only ever moves forward and the one place the specification re-consumes (a
 * newline ending a string) is expressed by not consuming it in the first place,
 * which is what makes the invariant "the cursor advances or the stream ends"
 * checkable by inspection.
 */

#include "RoboCSS/RoboCSSTokenizer.h"

#include "RoboBase/RoboBase.h"
#include "RoboBase/RoboLog.h"
#include "RoboCSSInternal.h"

#include <string.h>

/* ------------------------------------------------------------------ */
/* Reading the source                                                  */
/* ------------------------------------------------------------------ */

static size_t TokLength(const CssTokenizer *t)
{
    return (t && t->source) ? t->source->length : 0u;
}

static const uint8_t *TokBytes(const CssTokenizer *t)
{
    return (t && t->source) ? t->source->bytes : NULL;
}

/** Non-zero when @p ahead bytes past the cursor are inside the source. */
static int TokHas(const CssTokenizer *t, size_t ahead)
{
    return t != NULL && t->offset + ahead < TokLength(t);
}

/** The byte @p ahead past the cursor. The caller checked TokHas first. */
static uint8_t TokAt(const CssTokenizer *t, size_t ahead)
{
    return TokBytes(t)[t->offset + ahead];
}

/** The byte at an absolute @p offset. The caller checked the bound first. */
static uint8_t TokByte(const CssTokenizer *t, size_t offset)
{
    return TokBytes(t)[offset];
}

static void TokAdvance(CssTokenizer *t, size_t count)
{
    size_t length = TokLength(t);

    t->offset = (t->offset + count > length) ? length : (t->offset + count);
}

/* ------------------------------------------------------------------ */
/* Classification                                                      */
/* ------------------------------------------------------------------ */

/**
 * Non-zero for a byte that starts an identifier on its own.
 *
 * U+0000 is here because the specification's preprocessing turns it into
 * U+FFFD, which is one. We classify it as one and keep the raw byte in the
 * value, which is the whole of our preprocessing story (see the file comment).
 */
static int IsIdentifierStartByte(uint8_t b)
{
    return CssAsciiIsAlpha(b) || b == (uint8_t)'_' || b == 0x00u;
}

/**
 * Length in bytes of the well-formed non-ASCII scalar at absolute @p offset, or
 * 0 when the byte there is ASCII or cannot start one. Ill-formed bytes are not
 * code points at all, and are reported as INVALID one byte at a time rather than
 * being folded into a name.
 */
static size_t TokScalarLength(const CssTokenizer *t, size_t offset)
{
    size_t length = TokLength(t);
    size_t scalar;

    if (offset >= length || TokByte(t, offset) < 0x80u) {
        return 0u;
    }
    scalar = CssUtf8ScalarLength(TokBytes(t) + offset, length - offset);
    return scalar > 1u ? scalar : 0u;
}

/** Non-zero when a valid CSS escape starts at absolute @p offset. */
static int TokIsEscape(const CssTokenizer *t, size_t offset)
{
    size_t length = TokLength(t);

    if (offset >= length) {
        return 0;
    }
    return CssEscapeScan(TokBytes(t) + offset, length - offset).valid;
}

/** Non-zero when an ident-start code point is at absolute @p offset. */
static int TokIsIdentStart(const CssTokenizer *t, size_t offset)
{
    size_t length = TokLength(t);

    if (offset >= length) {
        return 0;
    }
    if (TokByte(t, offset) < 0x80u) {
        return IsIdentifierStartByte(TokByte(t, offset));
    }
    return TokScalarLength(t, offset) > 0u;
}

/** Non-zero when a code point that may continue a name is at @p offset. */
static int TokIsNameCodePoint(const CssTokenizer *t, size_t offset)
{
    size_t length = TokLength(t);

    if (offset >= length) {
        return 0;
    }
    if (TokByte(t, offset) == (uint8_t)'-' ||
        CssAsciiIsDigit(TokByte(t, offset))) {
        return 1;
    }
    return TokIsIdentStart(t, offset);
}

/**
 * Non-zero when an identifier starts at absolute @p offset — the
 * specification's "would start an identifier", used by the hash type decision
 * and by the `@`, `-`, `.` and `+` cases of the main switch.
 */
static int TokWouldStartIdent(const CssTokenizer *t, size_t offset)
{
    size_t length = TokLength(t);

    if (offset >= length) {
        return 0;
    }
    if (TokByte(t, offset) == (uint8_t)'-') {
        if (offset + 1u >= length) {
            return 0;
        }
        return TokIsIdentStart(t, offset + 1u) ||
               TokByte(t, offset + 1u) == (uint8_t)'-' ||
               TokIsEscape(t, offset + 1u);
    }
    if (TokByte(t, offset) == (uint8_t)'\\') {
        return TokIsEscape(t, offset);
    }
    return TokIsIdentStart(t, offset);
}

/** Non-zero when a number starts at absolute @p offset. */
static int TokWouldStartNumber(const CssTokenizer *t, size_t offset)
{
    size_t length = TokLength(t);
    uint8_t first;

    if (offset >= length) {
        return 0;
    }
    first = TokByte(t, offset);
    if (CssAsciiIsDigit(first)) {
        return 1;
    }
    if (first == (uint8_t)'+' || first == (uint8_t)'-') {
        if (offset + 1u >= length) {
            return 0;
        }
        if (CssAsciiIsDigit(TokByte(t, offset + 1u))) {
            return 1;
        }
        return TokByte(t, offset + 1u) == (uint8_t)'.' && offset + 2u < length &&
               CssAsciiIsDigit(TokByte(t, offset + 2u));
    }
    if (first == (uint8_t)'.') {
        return offset + 1u < length &&
               CssAsciiIsDigit(TokByte(t, offset + 1u));
    }
    return 0;
}

/**
 * End offset of the name starting at @p offset: name code points and escapes,
 * and nothing else. A `\` that is not a valid escape ends the name, which is
 * what leaves it to be tokenized as a delimiter on its own.
 */
static size_t TokNameEnd(const CssTokenizer *t, size_t offset)
{
    size_t length = TokLength(t);
    size_t pos = offset;

    while (pos < length) {
        size_t scalar;

        if (TokIsNameCodePoint(t, pos)) {
            scalar = TokScalarLength(t, pos);
            pos += (scalar > 0u) ? scalar : 1u;
            continue;
        }
        if (TokByte(t, pos) == (uint8_t)'\\') {
            CssEscape escape =
                CssEscapeScan(TokBytes(t) + pos, length - pos);

            if (!escape.valid) {
                break;
            }
            pos += escape.length;
            continue;
        }
        break;
    }
    return pos;
}

/* ------------------------------------------------------------------ */
/* Numbers                                                             */
/* ------------------------------------------------------------------ */

/**
 * Decimal to double, without the host locale and without libm.
 *
 * `strtod` is not used and must not be: it reads the current locale's decimal
 * point, which would make a style sheet tokenize differently depending on the
 * process's environment. This walks the digits itself.
 *
 * Precision: at most 19 significant digits are accumulated, which is more than
 * a double's 17 and enough for every integer a uint64 can hold; digits beyond
 * that are dropped and counted into the exponent, so the value is the truncation
 * rather than a rounding of it. Scaling is by exact powers of ten up to 1e22,
 * one rounding per step, so a value written with a handful of digits converts to
 * the nearest double — which is what the suites check by comparing against the
 * host compiler's own conversion of the same literal. Overflow saturates to
 * infinity and underflow to zero through ordinary IEEE arithmetic; nothing here
 * asserts a range, so `1e400` is infinite and `1e-400` is zero rather than an
 * error, exactly as CSS's conversion step leaves them.
 */
static double CssDecimalToDouble(const uint8_t *bytes, size_t length)
{
    static const double kPow10[23] = {
        1e0,  1e1,  1e2,  1e3,  1e4,  1e5,  1e6,  1e7,  1e8,  1e9,  1e10, 1e11,
        1e12, 1e13, 1e14, 1e15, 1e16, 1e17, 1e18, 1e19, 1e20, 1e21, 1e22};
    uint64_t mantissa = 0u;
    size_t i = 0u;
    int digits = 0;
    int exponent = 0;
    int negative = 0;
    double value;

    if (length == 0u) {
        return 0.0;
    }
    if (bytes[0] == (uint8_t)'+' || bytes[0] == (uint8_t)'-') {
        negative = bytes[0] == (uint8_t)'-';
        i = 1u;
    }

    /* Integer part. Leading zeros are absorbed rather than accumulated, so a
     * long run of them cannot push the significant digits out of the mantissa. */
    while (i < length && CssAsciiIsDigit(bytes[i])) {
        uint8_t digit = (uint8_t)(bytes[i] - (uint8_t)'0');

        if (mantissa == 0u && digit == 0u && digits == 0) {
            i++;
            continue;
        }
        if (digits < 19) {
            mantissa = (mantissa * 10u) + digit;
            digits++;
        } else {
            exponent++;
        }
        i++;
    }

    /* Fractional part. Each digit shifts the exponent down, and a *significant*
     * digit also enters the mantissa. The leading zeros of `0.001` shift the
     * exponent without consuming one of the nineteen significant places, which
     * is what keeps `0.0000000000000000000000001` from being all budget and no
     * value. A digit dropped past the nineteenth shifts nothing. */
    if (i < length && bytes[i] == (uint8_t)'.') {
        i++;
        while (i < length && CssAsciiIsDigit(bytes[i])) {
            uint8_t digit = (uint8_t)(bytes[i] - (uint8_t)'0');

            if (mantissa == 0u && digit == 0u && digits == 0) {
                exponent--;
                i++;
                continue;
            }
            if (digits < 19) {
                mantissa = (mantissa * 10u) + digit;
                digits++;
                exponent--;
            }
            i++;
        }
    }

    /* Exponent part. The magnitude is clamped before it is applied: a value
     * cannot be rescued from an exponent of a billion, and clamping keeps the
     * arithmetic below from wrapping. */
    if (i < length && (bytes[i] == (uint8_t)'e' || bytes[i] == (uint8_t)'E')) {
        int expSign = 1;
        int expValue = 0;

        i++;
        if (i < length && (bytes[i] == (uint8_t)'+' || bytes[i] == (uint8_t)'-')) {
            expSign = (bytes[i] == (uint8_t)'-') ? -1 : 1;
            i++;
        }
        while (i < length && CssAsciiIsDigit(bytes[i])) {
            if (expValue < 100000) {
                expValue = (expValue * 10) + (int)(bytes[i] - (uint8_t)'0');
            }
            i++;
        }
        exponent += expSign * expValue;
    }

    value = (double)mantissa;
    if (value == 0.0) {
        return negative ? -0.0 : 0.0;
    }
    /* Scaling, in 10^22 steps at most.
     *
     * The exact-scaling range is the whole point of the table: 10^k is exactly
     * representable for every k up to 22, so a decimal exponent inside ±22 is
     * applied in one multiply or one divide of the exact power — one rounding,
     * and therefore the same double the compiler produces for the same literal.
     * Outside that range the steps compound their rounding, which is why the
     * precision the header documents is a promise about ±22 and a bound beyond
     * it, not a promise everywhere. */
    while (exponent >= 23) {
        value *= kPow10[22];
        exponent -= 22;
    }
    while (exponent <= -23) {
        value /= kPow10[22];
        exponent += 22;
    }
    if (exponent > 0) {
        value *= kPow10[exponent];
    } else if (exponent < 0) {
        value /= kPow10[-exponent];
    }
    return negative ? -value : value;
}

/** What the numeric part of a numeric token turned out to be. */
typedef struct cssNumberScan {
    /** End offset of the numeric part (excluding any unit or `%`). */
    size_t end;
    /** Its value. */
    double value;
    /** 1 when it was written as an integer: no `.` and no exponent. */
    int isInteger;
    /** 1 when a `+` or `-` was written. */
    int hasSign;
} CssNumberScan;

/**
 * Scan the numeric part at absolute @p offset: the specification's "consume a
 * number" and "convert a string to a number" in one pass. The caller checked
 * that a number starts there.
 */
static CssNumberScan ScanNumber(const CssTokenizer *t, size_t offset)
{
    CssNumberScan scan;
    size_t length = TokLength(t);
    size_t pos = offset;

    scan.isInteger = 1;
    scan.hasSign = 0;

    if (TokByte(t, pos) == (uint8_t)'+' || TokByte(t, pos) == (uint8_t)'-') {
        scan.hasSign = 1;
        pos++;
    }
    while (pos < length && CssAsciiIsDigit(TokByte(t, pos))) {
        pos++;
    }
    if (pos + 1u < length && TokByte(t, pos) == (uint8_t)'.' &&
        CssAsciiIsDigit(TokByte(t, pos + 1u))) {
        scan.isInteger = 0;
        pos++;
        while (pos < length && CssAsciiIsDigit(TokByte(t, pos))) {
            pos++;
        }
    }
    if (pos < length && (TokByte(t, pos) == (uint8_t)'e' || TokByte(t, pos) == (uint8_t)'E')) {
        if (pos + 1u < length && CssAsciiIsDigit(TokByte(t, pos + 1u))) {
            scan.isInteger = 0;
            pos += 2u;
            while (pos < length && CssAsciiIsDigit(TokByte(t, pos))) {
                pos++;
            }
        } else if (pos + 2u < length &&
                   (TokByte(t, pos + 1u) == (uint8_t)'+' ||
                    TokByte(t, pos + 1u) == (uint8_t)'-') &&
                   CssAsciiIsDigit(TokByte(t, pos + 2u))) {
            scan.isInteger = 0;
            pos += 3u;
            while (pos < length && CssAsciiIsDigit(TokByte(t, pos))) {
                pos++;
            }
        }
    }
    scan.end = pos;
    scan.value = CssDecimalToDouble(TokBytes(t) + offset, pos - offset);
    return scan;
}

/* ------------------------------------------------------------------ */
/* Errors                                                              */
/* ------------------------------------------------------------------ */

static void TokFailAt(CssTokenizer *t,
                        size_t offset,
                        CssErrorKind kind,
                        Result code,
                        const char *message)
{
    CssError error;
    CssPosition position;

    if (t->hasError) {
        return; /* the first problem is the one that explains the rest */
    }
    if (t->source &&
        CssSourcePosition(t->source, offset, &position) == RoboOk) {
        (void)CssErrorSet(&error, kind, code, &position, message);
    } else {
        (void)CssErrorSet(&error, kind, code, NULL, message);
    }
    if (CssTokenizerRecordError(t, &error)) {
        LogDebug("robo_css: tokenizer error kind=%s offset=%llu",
                       CssErrorKindName(kind), (unsigned long long)offset);
    }
}

/* ------------------------------------------------------------------ */
/* Token construction                                                  */
/* ------------------------------------------------------------------ */

static Result finish(CssTokenizer *t,
                            CssToken *out,
                            CssTokenType type,
                            size_t begin,
                            size_t end,
                            CssDiag *diag)
{
    CssRange range;

    range.begin = begin;
    range.end = end;
    return CssTokenMake(out, type, t->source, range, diag);
}

static Result EmitEof(CssTokenizer *t,
                              CssToken *out,
                              CssDiag *diag)
{
    return CssTokenMakeEof(out, t->source, t->offset, diag);
}

/* ------------------------------------------------------------------ */
/* Token classes                                                       */
/* ------------------------------------------------------------------ */

/**
 * A comment, brackets and all.
 *
 * An unterminated comment is not an error token in CSS: it runs to the end of
 * input and is a comment. The token says it was never closed through `closed`,
 * and the stream records the same fact as an `UNEXPECTED_EOF` — the input ended
 * inside a construct that wanted more — because a comment that never closes is
 * a real thing to tell a caller about even though the token is well-formed.
 */
static Result ProduceComment(CssTokenizer *t,
                                     CssToken *out,
                                     CssDiag *diag)
{
    size_t begin = t->offset;
    size_t length = TokLength(t);
    size_t pos = begin + 2u; /* past the two opening bytes */
    int closed = 0;

    while (pos < length) {
        if (TokByte(t, pos) == (uint8_t)'*' && pos + 1u < length &&
            TokByte(t, pos + 1u) == (uint8_t)'/') {
            pos += 2u;
            closed = 1;
            break;
        }
        pos++;
    }
    t->offset = pos;
    {
        Result result = finish(t, out, RoboCssTokenComment, begin, pos, diag);

        if (result != RoboOk) {
            return result;
        }
    }
    /* After `finish`: making a token clears it first. */
    out->closed = closed;
    if (!closed) {
        TokFailAt(t, begin, RoboCssErrorKindUnexpectedEof, RoboInvalidArgument,
                    "comment is never closed");
    }
    return RoboOk;
}

/** A run of whitespace, preserved rather than dropped. */
static Result ProduceWhitespace(CssTokenizer *t,
                                        CssToken *out,
                                        CssDiag *diag)
{
    size_t begin = t->offset;
    size_t length = TokLength(t);

    while (t->offset < length && CssAsciiIsSpace(TokByte(t, t->offset))) {
        t->offset++;
    }
    return finish(t, out, RoboCssTokenWhitespace, begin, t->offset, diag);
}

/**
 * A string.
 *
 * Three endings, and the specification is exact about all three:
 *
 *   - the matching quote closes it: STRING, `closed` set;
 *   - a newline ends it *in front of the newline* — the newline is not consumed
 *     and becomes an ordinary whitespace token next. The token is BAD_STRING,
 *     and it is the one case in this file where the specification re-consumes
 *     an input code point, which is why it is handled by not consuming rather
 *     than by a pushback;
 *   - end of input leaves a STRING that was never closed: a string running off
 *     the end is a string, not a bad one (`closed` reports the difference).
 *
 * A backslash before a newline swallows the newline and keeps the string going
 * (the specification's escaped-newline parse error), so `"a\<newline>b"` is one
 * string whose value is `ab`.
 */
static Result ProduceString(CssTokenizer *t,
                                    CssToken *out,
                                    CssDiag *diag)
{
    size_t begin = t->offset;
    size_t length = TokLength(t);
    uint8_t quote = TokAt(t, 0);
    size_t pos = begin + 1u;
    int closed = 0;
    int bad = 0;

    while (pos < length) {
        uint8_t b = TokByte(t, pos);

        if (b == quote) {
            pos++;
            closed = 1;
            break;
        }
        if (CssIsNewlineByte(b)) {
            bad = 1;
            break;
        }
        if (b == (uint8_t)'\\') {
            CssEscape escape =
                CssEscapeScan(TokBytes(t) + pos, length - pos);

            if (escape.valid) {
                pos += escape.length;
                continue;
            }
            /* A backslash at the very end of input is dropped; a backslash
             * before a newline swallows the newline and the string continues.
             * Both the backslash and the break are stepped over: advancing by
             * the break alone would leave the cursor *on* the newline, which is
             * the one thing a string may not contain. */
            if (pos + 1u >= length) {
                pos = length;
                break;
            }
            pos += 1u + CssNewlineLength(TokBytes(t) + pos + 1u, length - pos - 1u);
            continue;
        }
        pos++;
    }
    t->offset = pos;
    {
        Result result = finish(t, out,
                                      bad ? RoboCssTokenBadString : RoboCssTokenString,
                                      begin, pos, diag);

        if (result != RoboOk) {
            return result;
        }
    }
    out->closed = !bad && closed;
    if (bad) {
        TokFailAt(t, begin, RoboCssErrorKindRecoverableSyntax,
                    RoboInvalidArgument, "string is ended by a newline");
    } else if (!closed) {
        TokFailAt(t, begin, RoboCssErrorKindUnexpectedEof, RoboInvalidArgument,
                    "string is never closed");
    }
    return RoboOk;
}

/**
 * A hash: `#name`, classified by whether what follows would start an
 * identifier. `#-x` and `#\31 23` are id hashes; `#123` is unrestricted (a
 * colour, never an id selector). A `#` that starts neither is a delimiter.
 */
static Result ProduceHash(CssTokenizer *t,
                                  CssToken *out,
                                  CssDiag *diag)
{
    size_t begin = t->offset;
    size_t pos = begin + 1u;

    if (pos >= TokLength(t) ||
        (!TokIsNameCodePoint(t, pos) && !TokIsEscape(t, pos))) {
        t->offset = pos;
        return finish(t, out, RoboCssTokenDelim, begin, pos, diag);
    }
    {
        /* The classification is decided on the code points *after* the `#`,
         * which is why it is asked of an offset rather than of the name. */
        CssHashType hash = TokWouldStartIdent(t, pos)
                                        ? RoboCssHashTypeId
                                        : RoboCssHashTypeUnrestricted;
        Result result;

        t->offset = TokNameEnd(t, pos);
        result = finish(t, out, RoboCssTokenHash, begin, t->offset, diag);
        if (result != RoboOk) {
            return result;
        }
        /* After `finish`: making a token clears it first. */
        out->hash = hash;
    }
    return RoboOk;
}

/** An at-keyword, or `@` as a delimiter when no name follows. */
static Result ProduceAtKeyword(CssTokenizer *t,
                                        CssToken *out,
                                        CssDiag *diag)
{
    size_t begin = t->offset;
    size_t pos = begin + 1u;

    if (!TokWouldStartIdent(t, pos)) {
        t->offset = pos;
        return finish(t, out, RoboCssTokenDelim, begin, pos, diag);
    }
    t->offset = TokNameEnd(t, pos);
    return finish(t, out, RoboCssTokenAtKeyword, begin, t->offset, diag);
}

/**
 * The remnants of a `url(` that failed: everything up to the `)` that closes it
 * or the end of input, escapes included. Consuming them is what stops a bad url
 * from spilling the rest of the style sheet into the next token.
 */
static void ConsumeBadUrlRemnants(CssTokenizer *t)
{
    size_t length = TokLength(t);

    while (t->offset < length) {
        uint8_t b = TokByte(t, t->offset);

        if (b == (uint8_t)')') {
            t->offset++;
            return;
        }
        if (b == (uint8_t)'\\') {
            CssEscape escape =
                CssEscapeScan(TokBytes(t) + t->offset, length - t->offset);

            if (escape.valid) {
                TokAdvance(t, escape.length);
                continue;
            }
        }
        t->offset++;
    }
}

/**
 * An unquoted `url(...)`, which the caller has already passed `url(` for.
 *
 * Leading whitespace is skipped and trailing whitespace before the `)` is left
 * inside the token's range but outside its value (the value accessors trim it),
 * so the ranges still tile the source. Four endings:
 *
 *   - `)` closes it: URL;
 *   - end of input: URL, unclosed — the specification is explicit that running
 *     off the end of input yields a url token, not a bad one;
 *   - whitespace that is not followed by `)` or end of input: BAD_URL;
 *   - `"`, `'`, `(`, a non-printable code point, or an invalid escape: BAD_URL.
 */
static Result ProduceUnquotedUrl(CssTokenizer *t,
                                          CssToken *out,
                                          size_t begin,
                                          CssDiag *diag)
{
    size_t length = TokLength(t);
    int closed = 0;
    int bad = 0;

    while (t->offset < length && CssAsciiIsSpace(TokByte(t, t->offset))) {
        t->offset++;
    }
    while (t->offset < length) {
        uint8_t b = TokByte(t, t->offset);

        if (b == (uint8_t)')') {
            t->offset++;
            closed = 1;
            break;
        }
        if (CssAsciiIsSpace(b)) {
            size_t lookahead = t->offset;

            while (lookahead < length && CssAsciiIsSpace(TokByte(t, lookahead))) {
                lookahead++;
            }
            if (lookahead >= length) {
                t->offset = length; /* end of input: a url, not a bad one */
                break;
            }
            if (TokByte(t, lookahead) == (uint8_t)')') {
                t->offset = lookahead + 1u;
                closed = 1;
                break;
            }
            bad = 1;
            break;
        }
        if (b == (uint8_t)'"' || b == (uint8_t)'\'' || b == (uint8_t)'(' ||
            b == 0x7Fu || (b < 0x20u && !CssIsNewlineByte(b)) || b == 0x00u) {
            bad = 1;
            break;
        }
        /* A well-formed non-ASCII scalar is content and is stepped over whole:
         * a continuation byte is only a byte, and walking a scalar one byte at
         * a time would call its tail ill-formed. An ill-formed byte is not a
         * code point at all, and a url cannot silently contain one. */
        if (b >= 0x80u) {
            size_t scalar = TokScalarLength(t, t->offset);

            if (scalar == 0u) {
                bad = 1;
                break;
            }
            t->offset += scalar;
            continue;
        }
        if (b == (uint8_t)'\\') {
            CssEscape escape =
                CssEscapeScan(TokBytes(t) + t->offset, length - t->offset);

            if (!escape.valid) {
                bad = 1;
                break;
            }
            TokAdvance(t, escape.length);
            continue;
        }
        t->offset++;
    }

    if (bad) {
        ConsumeBadUrlRemnants(t);
        {
            Result result = finish(t, out, RoboCssTokenBadUrl, begin, t->offset, diag);

            if (result != RoboOk) {
                return result;
            }
        }
        TokFailAt(t, begin, RoboCssErrorKindRecoverableSyntax,
                    RoboInvalidArgument, "url contains an illegal character");
        return RoboOk;
    }

    {
        Result result = finish(t, out, RoboCssTokenUrl, begin, t->offset, diag);

        if (result != RoboOk) {
            return result;
        }
    }
    /* After `finish`: making a token clears it first, so a flag written before
     * it would be wiped. The range covers the whole `url(...)` — the value
     * accessors are what take the `url(` and the `)` back off. */
    out->closed = closed;
    if (!closed) {
        TokFailAt(t, begin, RoboCssErrorKindUnexpectedEof, RoboInvalidArgument,
                    "url is never closed");
    }
    return RoboOk;
}

/**
 * An identifier or a function — and the one place the two-argument `url(` form
 * is decided.
 *
 * `url(` whose first non-whitespace argument character is a quote is a
 * *function* token followed by a string, which is what makes
 * `url("x.png")` a function call in the specification's grammar. The whitespace
 * between the two stays in the stream as its own token; see the file comment.
 */
static Result ProduceIdentLike(CssTokenizer *t,
                                        CssToken *out,
                                        CssDiag *diag)
{
    size_t begin = t->offset;
    size_t length = TokLength(t);
    size_t end = TokNameEnd(t, begin);
    int isFunction = 0;

    t->offset = end;

    if (end < length && TokByte(t, end) == (uint8_t)'(') {
        t->offset = end + 1u;
        isFunction = 1;
    }
    if (!isFunction) {
        return finish(t, out, RoboCssTokenIdent, begin, end, diag);
    }

    {
        Result result =
            finish(t, out, RoboCssTokenFunction, begin, end + 1u, diag);

        if (result != RoboOk) {
            return result;
        }
    }

    /* The name is compared through the token's own value, so an escaped
     * spelling of `url` is recognised the way the specification requires. */
    if (CssTokenValueEqualsAsciiCi(out, "url")) {
        size_t lookahead = t->offset;

        while (lookahead < length && CssAsciiIsSpace(TokByte(t, lookahead))) {
            lookahead++;
        }
        if (lookahead < length &&
            (TokByte(t, lookahead) == (uint8_t)'"' ||
             TokByte(t, lookahead) == (uint8_t)'\'')) {
            return RoboOk; /* a function token: the string comes next */
        }
        return ProduceUnquotedUrl(t, out, begin, diag);
    }
    return RoboOk;
}

/**
 * A number, a percentage or a dimension.
 *
 * The order matters and is the specification's: the numeric part is consumed
 * first, then a unit if a name follows it, and only then `%`. So `10px%` is a
 * dimension then a `%`, never a percentage.
 */
static Result ProduceNumeric(CssTokenizer *t,
                                     CssToken *out,
                                     CssDiag *diag)
{
    size_t begin = t->offset;
    size_t length = TokLength(t);
    CssNumberScan scan = ScanNumber(t, begin);
    CssTokenType type = RoboCssTokenNumber;
    size_t unitBegin = scan.end;
    size_t unitEnd = scan.end;

    t->offset = scan.end;

    if (TokWouldStartIdent(t, scan.end)) {
        type = RoboCssTokenDimension;
        unitEnd = TokNameEnd(t, scan.end);
        if (unitEnd == scan.end) {
            type = RoboCssTokenNumber;
        } else {
            t->offset = unitEnd;
        }
    } else if (scan.end < length && TokByte(t, scan.end) == (uint8_t)'%') {
        type = RoboCssTokenPercentage;
        t->offset = scan.end + 1u;
    }

    {
        Result result = finish(t, out, type, begin, t->offset, diag);

        if (result != RoboOk) {
            return result;
        }
    }
    out->number = scan.value;
    out->isInteger = scan.isInteger;
    out->hasSign = scan.hasSign;
    out->repr = CssSliceFromBytes(TokBytes(t) + begin, scan.end - begin);
    if (type == RoboCssTokenDimension) {
        out->unit = CssSliceFromBytes(TokBytes(t) + unitBegin, unitEnd - unitBegin);
    }
    return RoboOk;
}

/** One token, from wherever the cursor is. */
static Result produce(CssTokenizer *t,
                             CssToken *out,
                             CssDiag *diag)
{
    uint8_t b;
    size_t begin;

    if (t->offset >= TokLength(t)) {
        return EmitEof(t, out, diag);
    }

    begin = t->offset;
    b = TokByte(t, begin);
    CssTokenInit(out);

    switch (b) {
    case 0x09u: /* tab */
    case 0x0Au: /* LF */
    case 0x0Cu: /* FF */
    case 0x0Du: /* CR */
    case 0x20u: /* space */
        return ProduceWhitespace(t, out, diag);

    case '"':
    case '\'':
        return ProduceString(t, out, diag);

    case '0': case '1': case '2': case '3': case '4':
    case '5': case '6': case '7': case '8': case '9':
        return ProduceNumeric(t, out, diag);

    case '(':
        t->offset = begin + 1u;
        return finish(t, out, RoboCssTokenLeftParen, begin, begin + 1u, diag);
    case ')':
        t->offset = begin + 1u;
        return finish(t, out, RoboCssTokenRightParen, begin, begin + 1u, diag);
    case '[':
        t->offset = begin + 1u;
        return finish(t, out, RoboCssTokenLeftSquare, begin, begin + 1u, diag);
    case ']':
        t->offset = begin + 1u;
        return finish(t, out, RoboCssTokenRightSquare, begin, begin + 1u, diag);
    case '{':
        t->offset = begin + 1u;
        return finish(t, out, RoboCssTokenLeftCurly, begin, begin + 1u, diag);
    case '}':
        t->offset = begin + 1u;
        return finish(t, out, RoboCssTokenRightCurly, begin, begin + 1u, diag);
    case ',':
        t->offset = begin + 1u;
        return finish(t, out, RoboCssTokenComma, begin, begin + 1u, diag);
    case ':':
        t->offset = begin + 1u;
        return finish(t, out, RoboCssTokenColon, begin, begin + 1u, diag);
    case ';':
        t->offset = begin + 1u;
        return finish(t, out, RoboCssTokenSemicolon, begin, begin + 1u, diag);

    case '+':
    case '.':
        if (TokWouldStartNumber(t, begin)) {
            return ProduceNumeric(t, out, diag);
        }
        t->offset = begin + 1u;
        return finish(t, out, RoboCssTokenDelim, begin, begin + 1u, diag);

    case '-':
        if (TokWouldStartNumber(t, begin)) {
            return ProduceNumeric(t, out, diag);
        }
        if (TokHas(t, 1u) && TokAt(t, 1u) == '-' && TokHas(t, 2u) &&
            TokAt(t, 2u) == '>') {
            t->offset = begin + 3u;
            return finish(t, out, RoboCssTokenCdc, begin, begin + 3u, diag);
        }
        if (TokWouldStartIdent(t, begin)) {
            return ProduceIdentLike(t, out, diag);
        }
        t->offset = begin + 1u;
        return finish(t, out, RoboCssTokenDelim, begin, begin + 1u, diag);

    case '<':
        /* The one CDO spelling, and only at the start of a token. There is no
         * `<=` and no column token in this vocabulary: `||` is two delimiters,
         * which is what the frozen 9.1 record offers, and 9.4 composes the
         * match operators from them. */
        if (TokHas(t, 3u) && TokAt(t, 1u) == '!' && TokAt(t, 2u) == '-' &&
            TokAt(t, 3u) == '-') {
            t->offset = begin + 4u;
            return finish(t, out, RoboCssTokenCdo, begin, begin + 4u, diag);
        }
        t->offset = begin + 1u;
        return finish(t, out, RoboCssTokenDelim, begin, begin + 1u, diag);

    case '/':
        if (TokHas(t, 1u) && TokAt(t, 1u) == '*') {
            return ProduceComment(t, out, diag);
        }
        t->offset = begin + 1u;
        return finish(t, out, RoboCssTokenDelim, begin, begin + 1u, diag);

    case '\\':
        if (TokIsEscape(t, begin)) {
            return ProduceIdentLike(t, out, diag);
        }
        /* A backslash before a newline, or at the end of input, is a parse
         * error in the specification and simply a delimiter here; the newline
         * is left for the next token. The token is well-formed either way, so
         * the note is recorded rather than turned into an error token. */
        t->offset = begin + 1u;
        {
            Result result =
                finish(t, out, RoboCssTokenDelim, begin, begin + 1u, diag);

            if (result != RoboOk) {
                return result;
            }
        }
        TokFailAt(t, begin, RoboCssErrorKindRecoverableSyntax,
                    RoboInvalidArgument,
                    "backslash is not the start of a valid escape");
        return RoboOk;

    case '#':
        return ProduceHash(t, out, diag);

    case '@':
        return ProduceAtKeyword(t, out, diag);

    case 'u':
    case 'U':
        /* No unicode-range token exists in this vocabulary, so `U+0025` lexes
         * as the identifier `U`, a `+` delimiter and a dimension. A later phase
         * composes the range; inventing a token here would be a second model. */
        return ProduceIdentLike(t, out, diag);

    case 0x01u: case 0x02u: case 0x03u: case 0x04u: case 0x05u: case 0x06u:
    case 0x07u: case 0x08u: case 0x0Bu: case 0x0Eu: case 0x0Fu: case 0x10u:
    case 0x11u: case 0x12u: case 0x13u: case 0x14u: case 0x15u: case 0x16u:
    case 0x17u: case 0x18u: case 0x19u: case 0x1Au: case 0x1Bu: case 0x1Cu:
    case 0x1Du: case 0x1Eu: case 0x1Fu:
    case '!': case '%': case '&': case '*': case '=': case '>': case '?':
    case '^': case '$': case '|': case '~': case '`': case 0x7Fu:
        t->offset = begin + 1u;
        return finish(t, out, RoboCssTokenDelim, begin, begin + 1u, diag);

    default:
        break;
    }

    /* Everything left is either an identifier — NUL, which preprocessing would
     * have replaced with U+FFFD, and any well-formed non-ASCII code point — or
     * a byte that cannot begin a token at all. */
    if (TokIsIdentStart(t, begin)) {
        return ProduceIdentLike(t, out, diag);
    }

    t->offset = begin + 1u;
    {
        Result result =
            finish(t, out, RoboCssTokenInvalid, begin, begin + 1u, diag);

        if (result != RoboOk) {
            return result;
        }
    }
    TokFailAt(t, begin, RoboCssErrorKindInvalidToken, RoboInvalidArgument,
                "byte is not well-formed UTF-8 and cannot start a token");
    return RoboOk;
}

/* ------------------------------------------------------------------ */
/* Public API                                                          */
/* ------------------------------------------------------------------ */

Result CssTokenizerInit(CssTokenizer *tokenizer,
                                     const CssSource *source,
                                     CssDiag *diag)
{
    if (!tokenizer || !source) {
        return RoboCssDiag(diag, RoboCssDomainToken, RoboInvalidArgument,
                             "tokenizer or source is NULL");
    }
    CssTokenizerDispose(tokenizer);
    tokenizer->source = source;
    LogDebug("robo_css: tokenizer attached source name=%s len=%llu kind=%s",
                   CssSourceName(source),
                   (unsigned long long)source->length,
                   CssSourceKindName(source->kind));
    return RoboOk;
}

void CssTokenizerDispose(CssTokenizer *tokenizer)
{
    if (!tokenizer) {
        return;
    }
    if (tokenizer->source) {
        LogDebug("robo_css: tokenizer released (tokens=%llu, error=%s)",
                       (unsigned long long)tokenizer->tokensProduced,
                       CssErrorKindName(tokenizer->hasError
                                                    ? tokenizer->error.kind
                                                    : RoboCssErrorKindNone));
    }
    (void)memset(tokenizer, 0, sizeof(*tokenizer));
    CssTokenInit(&tokenizer->peeked);
    CssErrorInit(&tokenizer->error);
}

Result CssTokenizerReset(CssTokenizer *tokenizer,
                                      CssDiag *diag)
{
    if (!tokenizer || !tokenizer->source) {
        return RoboCssDiag(diag, RoboCssDomainToken, RoboInvalidArgument,
                             "tokenizer has no source");
    }
    tokenizer->offset = 0u;
    tokenizer->hasPeeked = 0;
    tokenizer->tokensProduced = 0u;
    tokenizer->hasError = 0;
    CssTokenInit(&tokenizer->peeked);
    CssErrorInit(&tokenizer->error);
    return RoboOk;
}

const CssSource *CssTokenizerSource(const CssTokenizer *tokenizer)
{
    return tokenizer ? tokenizer->source : NULL;
}

size_t CssTokenizerOffset(const CssTokenizer *tokenizer)
{
    return tokenizer ? tokenizer->offset : 0u;
}

size_t CssTokenizerRemaining(const CssTokenizer *tokenizer)
{
    size_t length;

    if (!tokenizer || !tokenizer->source) {
        return 0u;
    }
    length = tokenizer->source->length;
    return (tokenizer->offset >= length) ? 0u : (length - tokenizer->offset);
}

int CssTokenizerAtEnd(const CssTokenizer *tokenizer)
{
    if (!tokenizer || !tokenizer->source) {
        return 1;
    }
    return tokenizer->offset >= tokenizer->source->length;
}

size_t CssTokenizerTokensProduced(const CssTokenizer *tokenizer)
{
    return tokenizer ? tokenizer->tokensProduced : 0u;
}

int CssTokenizerHasPeek(const CssTokenizer *tokenizer)
{
    return (tokenizer != NULL && tokenizer->hasPeeked) ? 1 : 0;
}

Result CssTokenizerPosition(const CssTokenizer *tokenizer,
                                         CssPosition *outPosition,
                                         CssDiag *diag)
{
    if (!tokenizer || !outPosition || !tokenizer->source) {
        return RoboCssDiag(diag, RoboCssDomainToken, RoboInvalidArgument,
                             "tokenizer, output or source is NULL");
    }
    return CssSourcePosition(tokenizer->source, tokenizer->offset,
                                    outPosition);
}

Result CssTokenizerNext(CssTokenizer *tokenizer,
                                      CssToken *outToken,
                                      CssDiag *diag)
{
    Result result;

    if (!tokenizer || !outToken) {
        return RoboCssDiag(diag, RoboCssDomainToken, RoboInvalidArgument,
                             "tokenizer or token is NULL");
    }
    if (!tokenizer->source) {
        return RoboCssDiag(diag, RoboCssDomainToken, RoboInvalidArgument,
                             "tokenizer has no source");
    }
    if (tokenizer->hasPeeked) {
        *outToken = tokenizer->peeked;
        tokenizer->hasPeeked = 0;
        tokenizer->tokensProduced++;
        return RoboOk;
    }
    result = produce(tokenizer, outToken, diag);
    if (result != RoboOk) {
        return result;
    }
    tokenizer->tokensProduced++;
    return RoboOk;
}

Result CssTokenizerPeek(CssTokenizer *tokenizer,
                                      CssToken *outToken,
                                      CssDiag *diag)
{
    Result result;

    if (!tokenizer || !outToken) {
        return RoboCssDiag(diag, RoboCssDomainToken, RoboInvalidArgument,
                             "tokenizer or token is NULL");
    }
    if (!tokenizer->source) {
        return RoboCssDiag(diag, RoboCssDomainToken, RoboInvalidArgument,
                             "tokenizer has no source");
    }
    if (!tokenizer->hasPeeked) {
        result = produce(tokenizer, &tokenizer->peeked, diag);
        if (result != RoboOk) {
            return result;
        }
        tokenizer->hasPeeked = 1;
    }
    *outToken = tokenizer->peeked;
    return RoboOk;
}

Result CssTokenizerSkip(CssTokenizer *tokenizer,
                                     CssDiag *diag)
{
    CssToken discarded;
    Result result;

    if (!tokenizer || !tokenizer->source) {
        return RoboCssDiag(diag, RoboCssDomainToken, RoboInvalidArgument,
                             "tokenizer has no source");
    }
    if (tokenizer->hasPeeked) {
        tokenizer->hasPeeked = 0;
        tokenizer->tokensProduced++;
        return RoboOk;
    }
    result = produce(tokenizer, &discarded, diag);
    if (result != RoboOk) {
        return result;
    }
    tokenizer->tokensProduced++;
    return RoboOk;
}

Result CssTokenizerSkipTrivia(CssTokenizer *tokenizer,
                                            CssDiag *diag)
{
    for (;;) {
        CssToken token;
        Result result = CssTokenizerPeek(tokenizer, &token, diag);

        if (result != RoboOk) {
            return result;
        }
        if (!CssTokenTypeIsIgnorable(token.type)) {
            return RoboOk;
        }
        tokenizer->hasPeeked = 0;
        tokenizer->tokensProduced++;
    }
}

int CssTokenizerRecordError(CssTokenizer *tokenizer,
                                    const CssError *error)
{
    if (!tokenizer || !error || !CssErrorIsError(error) || tokenizer->hasError) {
        return 0;
    }
    tokenizer->error = *error;
    tokenizer->hasError = 1;
    return 1;
}

int CssTokenizerFail(CssTokenizer *tokenizer,
                            CssErrorKind kind,
                            Result code,
                            const char *message)
{
    CssError error;
    CssPosition position;
    CssPosition *positionPtr = NULL;

    if (!tokenizer || tokenizer->hasError) {
        return 0;
    }
    if (tokenizer->source &&
        CssSourcePosition(tokenizer->source, tokenizer->offset, &position) ==
            RoboOk) {
        positionPtr = &position;
    }
    (void)CssErrorSet(&error, kind, code, positionPtr, message);
    return CssTokenizerRecordError(tokenizer, &error);
}

int CssTokenizerHasError(const CssTokenizer *tokenizer)
{
    return (tokenizer != NULL && tokenizer->hasError) ? 1 : 0;
}

const CssError *CssTokenizerError(const CssTokenizer *tokenizer)
{
    if (!tokenizer || !tokenizer->hasError) {
        return NULL;
    }
    return &tokenizer->error;
}

int CssTokenizerTakeError(CssTokenizer *tokenizer,
                                  CssError *outError)
{
    if (!tokenizer || !tokenizer->hasError || !outError) {
        return 0;
    }
    *outError = tokenizer->error;
    tokenizer->hasError = 0;
    CssErrorInit(&tokenizer->error);
    return 1;
}
