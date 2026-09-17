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
 *     is_identifier_start_byte). What we do not do is substitute the
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

static size_t tok_length(const robo_css_tokenizer_t *t)
{
    return (t && t->source) ? t->source->length : 0u;
}

static const uint8_t *tok_bytes(const robo_css_tokenizer_t *t)
{
    return (t && t->source) ? t->source->bytes : NULL;
}

/** Non-zero when @p ahead bytes past the cursor are inside the source. */
static int tok_has(const robo_css_tokenizer_t *t, size_t ahead)
{
    return t != NULL && t->offset + ahead < tok_length(t);
}

/** The byte @p ahead past the cursor. The caller checked tok_has first. */
static uint8_t tok_at(const robo_css_tokenizer_t *t, size_t ahead)
{
    return tok_bytes(t)[t->offset + ahead];
}

/** The byte at an absolute @p offset. The caller checked the bound first. */
static uint8_t tok_byte(const robo_css_tokenizer_t *t, size_t offset)
{
    return tok_bytes(t)[offset];
}

static void tok_advance(robo_css_tokenizer_t *t, size_t count)
{
    size_t length = tok_length(t);

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
static int is_identifier_start_byte(uint8_t b)
{
    return robo_css_ascii_is_alpha(b) || b == (uint8_t)'_' || b == 0x00u;
}

/**
 * Length in bytes of the well-formed non-ASCII scalar at absolute @p offset, or
 * 0 when the byte there is ASCII or cannot start one. Ill-formed bytes are not
 * code points at all, and are reported as INVALID one byte at a time rather than
 * being folded into a name.
 */
static size_t tok_scalar_length(const robo_css_tokenizer_t *t, size_t offset)
{
    size_t length = tok_length(t);
    size_t scalar;

    if (offset >= length || tok_byte(t, offset) < 0x80u) {
        return 0u;
    }
    scalar = robo_css_utf8_scalar_length(tok_bytes(t) + offset, length - offset);
    return scalar > 1u ? scalar : 0u;
}

/** Non-zero when a valid CSS escape starts at absolute @p offset. */
static int tok_is_escape(const robo_css_tokenizer_t *t, size_t offset)
{
    size_t length = tok_length(t);

    if (offset >= length) {
        return 0;
    }
    return robo_css_escape_scan(tok_bytes(t) + offset, length - offset).valid;
}

/** Non-zero when an ident-start code point is at absolute @p offset. */
static int tok_is_ident_start(const robo_css_tokenizer_t *t, size_t offset)
{
    size_t length = tok_length(t);

    if (offset >= length) {
        return 0;
    }
    if (tok_byte(t, offset) < 0x80u) {
        return is_identifier_start_byte(tok_byte(t, offset));
    }
    return tok_scalar_length(t, offset) > 0u;
}

/** Non-zero when a code point that may continue a name is at @p offset. */
static int tok_is_name_code_point(const robo_css_tokenizer_t *t, size_t offset)
{
    size_t length = tok_length(t);

    if (offset >= length) {
        return 0;
    }
    if (tok_byte(t, offset) == (uint8_t)'-' ||
        robo_css_ascii_is_digit(tok_byte(t, offset))) {
        return 1;
    }
    return tok_is_ident_start(t, offset);
}

/**
 * Non-zero when an identifier starts at absolute @p offset — the
 * specification's "would start an identifier", used by the hash type decision
 * and by the `@`, `-`, `.` and `+` cases of the main switch.
 */
static int tok_would_start_ident(const robo_css_tokenizer_t *t, size_t offset)
{
    size_t length = tok_length(t);

    if (offset >= length) {
        return 0;
    }
    if (tok_byte(t, offset) == (uint8_t)'-') {
        if (offset + 1u >= length) {
            return 0;
        }
        return tok_is_ident_start(t, offset + 1u) ||
               tok_byte(t, offset + 1u) == (uint8_t)'-' ||
               tok_is_escape(t, offset + 1u);
    }
    if (tok_byte(t, offset) == (uint8_t)'\\') {
        return tok_is_escape(t, offset);
    }
    return tok_is_ident_start(t, offset);
}

/** Non-zero when a number starts at absolute @p offset. */
static int tok_would_start_number(const robo_css_tokenizer_t *t, size_t offset)
{
    size_t length = tok_length(t);
    uint8_t first;

    if (offset >= length) {
        return 0;
    }
    first = tok_byte(t, offset);
    if (robo_css_ascii_is_digit(first)) {
        return 1;
    }
    if (first == (uint8_t)'+' || first == (uint8_t)'-') {
        if (offset + 1u >= length) {
            return 0;
        }
        if (robo_css_ascii_is_digit(tok_byte(t, offset + 1u))) {
            return 1;
        }
        return tok_byte(t, offset + 1u) == (uint8_t)'.' && offset + 2u < length &&
               robo_css_ascii_is_digit(tok_byte(t, offset + 2u));
    }
    if (first == (uint8_t)'.') {
        return offset + 1u < length &&
               robo_css_ascii_is_digit(tok_byte(t, offset + 1u));
    }
    return 0;
}

/**
 * End offset of the name starting at @p offset: name code points and escapes,
 * and nothing else. A `\` that is not a valid escape ends the name, which is
 * what leaves it to be tokenized as a delimiter on its own.
 */
static size_t tok_name_end(const robo_css_tokenizer_t *t, size_t offset)
{
    size_t length = tok_length(t);
    size_t pos = offset;

    while (pos < length) {
        size_t scalar;

        if (tok_is_name_code_point(t, pos)) {
            scalar = tok_scalar_length(t, pos);
            pos += (scalar > 0u) ? scalar : 1u;
            continue;
        }
        if (tok_byte(t, pos) == (uint8_t)'\\') {
            robo_css_escape_t escape =
                robo_css_escape_scan(tok_bytes(t) + pos, length - pos);

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
static double css_decimal_to_double(const uint8_t *bytes, size_t length)
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
    while (i < length && robo_css_ascii_is_digit(bytes[i])) {
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
        while (i < length && robo_css_ascii_is_digit(bytes[i])) {
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
        int exp_sign = 1;
        int exp_value = 0;

        i++;
        if (i < length && (bytes[i] == (uint8_t)'+' || bytes[i] == (uint8_t)'-')) {
            exp_sign = (bytes[i] == (uint8_t)'-') ? -1 : 1;
            i++;
        }
        while (i < length && robo_css_ascii_is_digit(bytes[i])) {
            if (exp_value < 100000) {
                exp_value = (exp_value * 10) + (int)(bytes[i] - (uint8_t)'0');
            }
            i++;
        }
        exponent += exp_sign * exp_value;
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
typedef struct css_number_scan {
    /** End offset of the numeric part (excluding any unit or `%`). */
    size_t end;
    /** Its value. */
    double value;
    /** 1 when it was written as an integer: no `.` and no exponent. */
    int is_integer;
    /** 1 when a `+` or `-` was written. */
    int has_sign;
} css_number_scan_t;

/**
 * Scan the numeric part at absolute @p offset: the specification's "consume a
 * number" and "convert a string to a number" in one pass. The caller checked
 * that a number starts there.
 */
static css_number_scan_t scan_number(const robo_css_tokenizer_t *t, size_t offset)
{
    css_number_scan_t scan;
    size_t length = tok_length(t);
    size_t pos = offset;

    scan.is_integer = 1;
    scan.has_sign = 0;

    if (tok_byte(t, pos) == (uint8_t)'+' || tok_byte(t, pos) == (uint8_t)'-') {
        scan.has_sign = 1;
        pos++;
    }
    while (pos < length && robo_css_ascii_is_digit(tok_byte(t, pos))) {
        pos++;
    }
    if (pos + 1u < length && tok_byte(t, pos) == (uint8_t)'.' &&
        robo_css_ascii_is_digit(tok_byte(t, pos + 1u))) {
        scan.is_integer = 0;
        pos++;
        while (pos < length && robo_css_ascii_is_digit(tok_byte(t, pos))) {
            pos++;
        }
    }
    if (pos < length && (tok_byte(t, pos) == (uint8_t)'e' || tok_byte(t, pos) == (uint8_t)'E')) {
        if (pos + 1u < length && robo_css_ascii_is_digit(tok_byte(t, pos + 1u))) {
            scan.is_integer = 0;
            pos += 2u;
            while (pos < length && robo_css_ascii_is_digit(tok_byte(t, pos))) {
                pos++;
            }
        } else if (pos + 2u < length &&
                   (tok_byte(t, pos + 1u) == (uint8_t)'+' ||
                    tok_byte(t, pos + 1u) == (uint8_t)'-') &&
                   robo_css_ascii_is_digit(tok_byte(t, pos + 2u))) {
            scan.is_integer = 0;
            pos += 3u;
            while (pos < length && robo_css_ascii_is_digit(tok_byte(t, pos))) {
                pos++;
            }
        }
    }
    scan.end = pos;
    scan.value = css_decimal_to_double(tok_bytes(t) + offset, pos - offset);
    return scan;
}

/* ------------------------------------------------------------------ */
/* Errors                                                              */
/* ------------------------------------------------------------------ */

static void tok_fail_at(robo_css_tokenizer_t *t,
                        size_t offset,
                        robo_css_error_kind_t kind,
                        robo_result_t code,
                        const char *message)
{
    robo_css_error_t error;
    robo_css_position_t position;

    if (t->has_error) {
        return; /* the first problem is the one that explains the rest */
    }
    if (t->source &&
        robo_css_source_position(t->source, offset, &position) == ROBO_OK) {
        (void)robo_css_error_set(&error, kind, code, &position, message);
    } else {
        (void)robo_css_error_set(&error, kind, code, NULL, message);
    }
    if (robo_css_tokenizer_record_error(t, &error)) {
        robo_log_debug("robo_css: tokenizer error kind=%s offset=%llu",
                       robo_css_error_kind_name(kind), (unsigned long long)offset);
    }
}

/* ------------------------------------------------------------------ */
/* Token construction                                                  */
/* ------------------------------------------------------------------ */

static robo_result_t finish(robo_css_tokenizer_t *t,
                            robo_css_token_t *out,
                            robo_css_token_type_t type,
                            size_t begin,
                            size_t end,
                            robo_css_diag_t *diag)
{
    robo_css_range_t range;

    range.begin = begin;
    range.end = end;
    return robo_css_token_make(out, type, t->source, range, diag);
}

static robo_result_t emit_eof(robo_css_tokenizer_t *t,
                              robo_css_token_t *out,
                              robo_css_diag_t *diag)
{
    return robo_css_token_make_eof(out, t->source, t->offset, diag);
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
static robo_result_t produce_comment(robo_css_tokenizer_t *t,
                                     robo_css_token_t *out,
                                     robo_css_diag_t *diag)
{
    size_t begin = t->offset;
    size_t length = tok_length(t);
    size_t pos = begin + 2u; /* past the two opening bytes */
    int closed = 0;

    while (pos < length) {
        if (tok_byte(t, pos) == (uint8_t)'*' && pos + 1u < length &&
            tok_byte(t, pos + 1u) == (uint8_t)'/') {
            pos += 2u;
            closed = 1;
            break;
        }
        pos++;
    }
    t->offset = pos;
    {
        robo_result_t result = finish(t, out, ROBO_CSS_TOKEN_COMMENT, begin, pos, diag);

        if (result != ROBO_OK) {
            return result;
        }
    }
    /* After `finish`: making a token clears it first. */
    out->closed = closed;
    if (!closed) {
        tok_fail_at(t, begin, ROBO_CSS_ERROR_KIND_UNEXPECTED_EOF, ROBO_INVALID_ARGUMENT,
                    "comment is never closed");
    }
    return ROBO_OK;
}

/** A run of whitespace, preserved rather than dropped. */
static robo_result_t produce_whitespace(robo_css_tokenizer_t *t,
                                        robo_css_token_t *out,
                                        robo_css_diag_t *diag)
{
    size_t begin = t->offset;
    size_t length = tok_length(t);

    while (t->offset < length && robo_css_ascii_is_space(tok_byte(t, t->offset))) {
        t->offset++;
    }
    return finish(t, out, ROBO_CSS_TOKEN_WHITESPACE, begin, t->offset, diag);
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
static robo_result_t produce_string(robo_css_tokenizer_t *t,
                                    robo_css_token_t *out,
                                    robo_css_diag_t *diag)
{
    size_t begin = t->offset;
    size_t length = tok_length(t);
    uint8_t quote = tok_at(t, 0);
    size_t pos = begin + 1u;
    int closed = 0;
    int bad = 0;

    while (pos < length) {
        uint8_t b = tok_byte(t, pos);

        if (b == quote) {
            pos++;
            closed = 1;
            break;
        }
        if (robo_css_is_newline_byte(b)) {
            bad = 1;
            break;
        }
        if (b == (uint8_t)'\\') {
            robo_css_escape_t escape =
                robo_css_escape_scan(tok_bytes(t) + pos, length - pos);

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
            pos += 1u + robo_css_newline_length(tok_bytes(t) + pos + 1u, length - pos - 1u);
            continue;
        }
        pos++;
    }
    t->offset = pos;
    {
        robo_result_t result = finish(t, out,
                                      bad ? ROBO_CSS_TOKEN_BAD_STRING : ROBO_CSS_TOKEN_STRING,
                                      begin, pos, diag);

        if (result != ROBO_OK) {
            return result;
        }
    }
    out->closed = !bad && closed;
    if (bad) {
        tok_fail_at(t, begin, ROBO_CSS_ERROR_KIND_RECOVERABLE_SYNTAX,
                    ROBO_INVALID_ARGUMENT, "string is ended by a newline");
    } else if (!closed) {
        tok_fail_at(t, begin, ROBO_CSS_ERROR_KIND_UNEXPECTED_EOF, ROBO_INVALID_ARGUMENT,
                    "string is never closed");
    }
    return ROBO_OK;
}

/**
 * A hash: `#name`, classified by whether what follows would start an
 * identifier. `#-x` and `#\31 23` are id hashes; `#123` is unrestricted (a
 * colour, never an id selector). A `#` that starts neither is a delimiter.
 */
static robo_result_t produce_hash(robo_css_tokenizer_t *t,
                                  robo_css_token_t *out,
                                  robo_css_diag_t *diag)
{
    size_t begin = t->offset;
    size_t pos = begin + 1u;

    if (pos >= tok_length(t) ||
        (!tok_is_name_code_point(t, pos) && !tok_is_escape(t, pos))) {
        t->offset = pos;
        return finish(t, out, ROBO_CSS_TOKEN_DELIM, begin, pos, diag);
    }
    {
        /* The classification is decided on the code points *after* the `#`,
         * which is why it is asked of an offset rather than of the name. */
        robo_css_hash_type_t hash = tok_would_start_ident(t, pos)
                                        ? ROBO_CSS_HASH_TYPE_ID
                                        : ROBO_CSS_HASH_TYPE_UNRESTRICTED;
        robo_result_t result;

        t->offset = tok_name_end(t, pos);
        result = finish(t, out, ROBO_CSS_TOKEN_HASH, begin, t->offset, diag);
        if (result != ROBO_OK) {
            return result;
        }
        /* After `finish`: making a token clears it first. */
        out->hash = hash;
    }
    return ROBO_OK;
}

/** An at-keyword, or `@` as a delimiter when no name follows. */
static robo_result_t produce_at_keyword(robo_css_tokenizer_t *t,
                                        robo_css_token_t *out,
                                        robo_css_diag_t *diag)
{
    size_t begin = t->offset;
    size_t pos = begin + 1u;

    if (!tok_would_start_ident(t, pos)) {
        t->offset = pos;
        return finish(t, out, ROBO_CSS_TOKEN_DELIM, begin, pos, diag);
    }
    t->offset = tok_name_end(t, pos);
    return finish(t, out, ROBO_CSS_TOKEN_AT_KEYWORD, begin, t->offset, diag);
}

/**
 * The remnants of a `url(` that failed: everything up to the `)` that closes it
 * or the end of input, escapes included. Consuming them is what stops a bad url
 * from spilling the rest of the style sheet into the next token.
 */
static void consume_bad_url_remnants(robo_css_tokenizer_t *t)
{
    size_t length = tok_length(t);

    while (t->offset < length) {
        uint8_t b = tok_byte(t, t->offset);

        if (b == (uint8_t)')') {
            t->offset++;
            return;
        }
        if (b == (uint8_t)'\\') {
            robo_css_escape_t escape =
                robo_css_escape_scan(tok_bytes(t) + t->offset, length - t->offset);

            if (escape.valid) {
                tok_advance(t, escape.length);
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
static robo_result_t produce_unquoted_url(robo_css_tokenizer_t *t,
                                          robo_css_token_t *out,
                                          size_t begin,
                                          robo_css_diag_t *diag)
{
    size_t length = tok_length(t);
    int closed = 0;
    int bad = 0;

    while (t->offset < length && robo_css_ascii_is_space(tok_byte(t, t->offset))) {
        t->offset++;
    }
    while (t->offset < length) {
        uint8_t b = tok_byte(t, t->offset);

        if (b == (uint8_t)')') {
            t->offset++;
            closed = 1;
            break;
        }
        if (robo_css_ascii_is_space(b)) {
            size_t lookahead = t->offset;

            while (lookahead < length && robo_css_ascii_is_space(tok_byte(t, lookahead))) {
                lookahead++;
            }
            if (lookahead >= length) {
                t->offset = length; /* end of input: a url, not a bad one */
                break;
            }
            if (tok_byte(t, lookahead) == (uint8_t)')') {
                t->offset = lookahead + 1u;
                closed = 1;
                break;
            }
            bad = 1;
            break;
        }
        if (b == (uint8_t)'"' || b == (uint8_t)'\'' || b == (uint8_t)'(' ||
            b == 0x7Fu || (b < 0x20u && !robo_css_is_newline_byte(b)) || b == 0x00u) {
            bad = 1;
            break;
        }
        /* A well-formed non-ASCII scalar is content and is stepped over whole:
         * a continuation byte is only a byte, and walking a scalar one byte at
         * a time would call its tail ill-formed. An ill-formed byte is not a
         * code point at all, and a url cannot silently contain one. */
        if (b >= 0x80u) {
            size_t scalar = tok_scalar_length(t, t->offset);

            if (scalar == 0u) {
                bad = 1;
                break;
            }
            t->offset += scalar;
            continue;
        }
        if (b == (uint8_t)'\\') {
            robo_css_escape_t escape =
                robo_css_escape_scan(tok_bytes(t) + t->offset, length - t->offset);

            if (!escape.valid) {
                bad = 1;
                break;
            }
            tok_advance(t, escape.length);
            continue;
        }
        t->offset++;
    }

    if (bad) {
        consume_bad_url_remnants(t);
        {
            robo_result_t result = finish(t, out, ROBO_CSS_TOKEN_BAD_URL, begin, t->offset, diag);

            if (result != ROBO_OK) {
                return result;
            }
        }
        tok_fail_at(t, begin, ROBO_CSS_ERROR_KIND_RECOVERABLE_SYNTAX,
                    ROBO_INVALID_ARGUMENT, "url contains an illegal character");
        return ROBO_OK;
    }

    {
        robo_result_t result = finish(t, out, ROBO_CSS_TOKEN_URL, begin, t->offset, diag);

        if (result != ROBO_OK) {
            return result;
        }
    }
    /* After `finish`: making a token clears it first, so a flag written before
     * it would be wiped. The range covers the whole `url(...)` — the value
     * accessors are what take the `url(` and the `)` back off. */
    out->closed = closed;
    if (!closed) {
        tok_fail_at(t, begin, ROBO_CSS_ERROR_KIND_UNEXPECTED_EOF, ROBO_INVALID_ARGUMENT,
                    "url is never closed");
    }
    return ROBO_OK;
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
static robo_result_t produce_ident_like(robo_css_tokenizer_t *t,
                                        robo_css_token_t *out,
                                        robo_css_diag_t *diag)
{
    size_t begin = t->offset;
    size_t length = tok_length(t);
    size_t end = tok_name_end(t, begin);
    int is_function = 0;

    t->offset = end;

    if (end < length && tok_byte(t, end) == (uint8_t)'(') {
        t->offset = end + 1u;
        is_function = 1;
    }
    if (!is_function) {
        return finish(t, out, ROBO_CSS_TOKEN_IDENT, begin, end, diag);
    }

    {
        robo_result_t result =
            finish(t, out, ROBO_CSS_TOKEN_FUNCTION, begin, end + 1u, diag);

        if (result != ROBO_OK) {
            return result;
        }
    }

    /* The name is compared through the token's own value, so an escaped
     * spelling of `url` is recognised the way the specification requires. */
    if (robo_css_token_value_equals_ascii_ci(out, "url")) {
        size_t lookahead = t->offset;

        while (lookahead < length && robo_css_ascii_is_space(tok_byte(t, lookahead))) {
            lookahead++;
        }
        if (lookahead < length &&
            (tok_byte(t, lookahead) == (uint8_t)'"' ||
             tok_byte(t, lookahead) == (uint8_t)'\'')) {
            return ROBO_OK; /* a function token: the string comes next */
        }
        return produce_unquoted_url(t, out, begin, diag);
    }
    return ROBO_OK;
}

/**
 * A number, a percentage or a dimension.
 *
 * The order matters and is the specification's: the numeric part is consumed
 * first, then a unit if a name follows it, and only then `%`. So `10px%` is a
 * dimension then a `%`, never a percentage.
 */
static robo_result_t produce_numeric(robo_css_tokenizer_t *t,
                                     robo_css_token_t *out,
                                     robo_css_diag_t *diag)
{
    size_t begin = t->offset;
    size_t length = tok_length(t);
    css_number_scan_t scan = scan_number(t, begin);
    robo_css_token_type_t type = ROBO_CSS_TOKEN_NUMBER;
    size_t unit_begin = scan.end;
    size_t unit_end = scan.end;

    t->offset = scan.end;

    if (tok_would_start_ident(t, scan.end)) {
        type = ROBO_CSS_TOKEN_DIMENSION;
        unit_end = tok_name_end(t, scan.end);
        if (unit_end == scan.end) {
            type = ROBO_CSS_TOKEN_NUMBER;
        } else {
            t->offset = unit_end;
        }
    } else if (scan.end < length && tok_byte(t, scan.end) == (uint8_t)'%') {
        type = ROBO_CSS_TOKEN_PERCENTAGE;
        t->offset = scan.end + 1u;
    }

    {
        robo_result_t result = finish(t, out, type, begin, t->offset, diag);

        if (result != ROBO_OK) {
            return result;
        }
    }
    out->number = scan.value;
    out->is_integer = scan.is_integer;
    out->has_sign = scan.has_sign;
    out->repr = robo_css_slice_from_bytes(tok_bytes(t) + begin, scan.end - begin);
    if (type == ROBO_CSS_TOKEN_DIMENSION) {
        out->unit = robo_css_slice_from_bytes(tok_bytes(t) + unit_begin, unit_end - unit_begin);
    }
    return ROBO_OK;
}

/** One token, from wherever the cursor is. */
static robo_result_t produce(robo_css_tokenizer_t *t,
                             robo_css_token_t *out,
                             robo_css_diag_t *diag)
{
    uint8_t b;
    size_t begin;

    if (t->offset >= tok_length(t)) {
        return emit_eof(t, out, diag);
    }

    begin = t->offset;
    b = tok_byte(t, begin);
    robo_css_token_init(out);

    switch (b) {
    case 0x09u: /* tab */
    case 0x0Au: /* LF */
    case 0x0Cu: /* FF */
    case 0x0Du: /* CR */
    case 0x20u: /* space */
        return produce_whitespace(t, out, diag);

    case '"':
    case '\'':
        return produce_string(t, out, diag);

    case '0': case '1': case '2': case '3': case '4':
    case '5': case '6': case '7': case '8': case '9':
        return produce_numeric(t, out, diag);

    case '(':
        t->offset = begin + 1u;
        return finish(t, out, ROBO_CSS_TOKEN_LEFT_PAREN, begin, begin + 1u, diag);
    case ')':
        t->offset = begin + 1u;
        return finish(t, out, ROBO_CSS_TOKEN_RIGHT_PAREN, begin, begin + 1u, diag);
    case '[':
        t->offset = begin + 1u;
        return finish(t, out, ROBO_CSS_TOKEN_LEFT_SQUARE, begin, begin + 1u, diag);
    case ']':
        t->offset = begin + 1u;
        return finish(t, out, ROBO_CSS_TOKEN_RIGHT_SQUARE, begin, begin + 1u, diag);
    case '{':
        t->offset = begin + 1u;
        return finish(t, out, ROBO_CSS_TOKEN_LEFT_CURLY, begin, begin + 1u, diag);
    case '}':
        t->offset = begin + 1u;
        return finish(t, out, ROBO_CSS_TOKEN_RIGHT_CURLY, begin, begin + 1u, diag);
    case ',':
        t->offset = begin + 1u;
        return finish(t, out, ROBO_CSS_TOKEN_COMMA, begin, begin + 1u, diag);
    case ':':
        t->offset = begin + 1u;
        return finish(t, out, ROBO_CSS_TOKEN_COLON, begin, begin + 1u, diag);
    case ';':
        t->offset = begin + 1u;
        return finish(t, out, ROBO_CSS_TOKEN_SEMICOLON, begin, begin + 1u, diag);

    case '+':
    case '.':
        if (tok_would_start_number(t, begin)) {
            return produce_numeric(t, out, diag);
        }
        t->offset = begin + 1u;
        return finish(t, out, ROBO_CSS_TOKEN_DELIM, begin, begin + 1u, diag);

    case '-':
        if (tok_would_start_number(t, begin)) {
            return produce_numeric(t, out, diag);
        }
        if (tok_has(t, 1u) && tok_at(t, 1u) == '-' && tok_has(t, 2u) &&
            tok_at(t, 2u) == '>') {
            t->offset = begin + 3u;
            return finish(t, out, ROBO_CSS_TOKEN_CDC, begin, begin + 3u, diag);
        }
        if (tok_would_start_ident(t, begin)) {
            return produce_ident_like(t, out, diag);
        }
        t->offset = begin + 1u;
        return finish(t, out, ROBO_CSS_TOKEN_DELIM, begin, begin + 1u, diag);

    case '<':
        /* The one CDO spelling, and only at the start of a token. There is no
         * `<=` and no column token in this vocabulary: `||` is two delimiters,
         * which is what the frozen 9.1 record offers, and 9.4 composes the
         * match operators from them. */
        if (tok_has(t, 3u) && tok_at(t, 1u) == '!' && tok_at(t, 2u) == '-' &&
            tok_at(t, 3u) == '-') {
            t->offset = begin + 4u;
            return finish(t, out, ROBO_CSS_TOKEN_CDO, begin, begin + 4u, diag);
        }
        t->offset = begin + 1u;
        return finish(t, out, ROBO_CSS_TOKEN_DELIM, begin, begin + 1u, diag);

    case '/':
        if (tok_has(t, 1u) && tok_at(t, 1u) == '*') {
            return produce_comment(t, out, diag);
        }
        t->offset = begin + 1u;
        return finish(t, out, ROBO_CSS_TOKEN_DELIM, begin, begin + 1u, diag);

    case '\\':
        if (tok_is_escape(t, begin)) {
            return produce_ident_like(t, out, diag);
        }
        /* A backslash before a newline, or at the end of input, is a parse
         * error in the specification and simply a delimiter here; the newline
         * is left for the next token. The token is well-formed either way, so
         * the note is recorded rather than turned into an error token. */
        t->offset = begin + 1u;
        {
            robo_result_t result =
                finish(t, out, ROBO_CSS_TOKEN_DELIM, begin, begin + 1u, diag);

            if (result != ROBO_OK) {
                return result;
            }
        }
        tok_fail_at(t, begin, ROBO_CSS_ERROR_KIND_RECOVERABLE_SYNTAX,
                    ROBO_INVALID_ARGUMENT,
                    "backslash is not the start of a valid escape");
        return ROBO_OK;

    case '#':
        return produce_hash(t, out, diag);

    case '@':
        return produce_at_keyword(t, out, diag);

    case 'u':
    case 'U':
        /* No unicode-range token exists in this vocabulary, so `U+0025` lexes
         * as the identifier `U`, a `+` delimiter and a dimension. A later phase
         * composes the range; inventing a token here would be a second model. */
        return produce_ident_like(t, out, diag);

    case 0x01u: case 0x02u: case 0x03u: case 0x04u: case 0x05u: case 0x06u:
    case 0x07u: case 0x08u: case 0x0Bu: case 0x0Eu: case 0x0Fu: case 0x10u:
    case 0x11u: case 0x12u: case 0x13u: case 0x14u: case 0x15u: case 0x16u:
    case 0x17u: case 0x18u: case 0x19u: case 0x1Au: case 0x1Bu: case 0x1Cu:
    case 0x1Du: case 0x1Eu: case 0x1Fu:
    case '!': case '%': case '&': case '*': case '=': case '>': case '?':
    case '^': case '$': case '|': case '~': case '`': case 0x7Fu:
        t->offset = begin + 1u;
        return finish(t, out, ROBO_CSS_TOKEN_DELIM, begin, begin + 1u, diag);

    default:
        break;
    }

    /* Everything left is either an identifier — NUL, which preprocessing would
     * have replaced with U+FFFD, and any well-formed non-ASCII code point — or
     * a byte that cannot begin a token at all. */
    if (tok_is_ident_start(t, begin)) {
        return produce_ident_like(t, out, diag);
    }

    t->offset = begin + 1u;
    {
        robo_result_t result =
            finish(t, out, ROBO_CSS_TOKEN_INVALID, begin, begin + 1u, diag);

        if (result != ROBO_OK) {
            return result;
        }
    }
    tok_fail_at(t, begin, ROBO_CSS_ERROR_KIND_INVALID_TOKEN, ROBO_INVALID_ARGUMENT,
                "byte is not well-formed UTF-8 and cannot start a token");
    return ROBO_OK;
}

/* ------------------------------------------------------------------ */
/* Public API                                                          */
/* ------------------------------------------------------------------ */

robo_result_t robo_css_tokenizer_init(robo_css_tokenizer_t *tokenizer,
                                     const robo_css_source_t *source,
                                     robo_css_diag_t *diag)
{
    if (!tokenizer || !source) {
        return ROBO_CSS_DIAG(diag, ROBO_CSS_DOMAIN_TOKEN, ROBO_INVALID_ARGUMENT,
                             "tokenizer or source is NULL");
    }
    robo_css_tokenizer_dispose(tokenizer);
    tokenizer->source = source;
    robo_log_debug("robo_css: tokenizer attached source name=%s len=%llu kind=%s",
                   robo_css_source_name(source),
                   (unsigned long long)source->length,
                   robo_css_source_kind_name(source->kind));
    return ROBO_OK;
}

void robo_css_tokenizer_dispose(robo_css_tokenizer_t *tokenizer)
{
    if (!tokenizer) {
        return;
    }
    if (tokenizer->source) {
        robo_log_debug("robo_css: tokenizer released (tokens=%llu, error=%s)",
                       (unsigned long long)tokenizer->tokens_produced,
                       robo_css_error_kind_name(tokenizer->has_error
                                                    ? tokenizer->error.kind
                                                    : ROBO_CSS_ERROR_KIND_NONE));
    }
    (void)memset(tokenizer, 0, sizeof(*tokenizer));
    robo_css_token_init(&tokenizer->peeked);
    robo_css_error_init(&tokenizer->error);
}

robo_result_t robo_css_tokenizer_reset(robo_css_tokenizer_t *tokenizer,
                                      robo_css_diag_t *diag)
{
    if (!tokenizer || !tokenizer->source) {
        return ROBO_CSS_DIAG(diag, ROBO_CSS_DOMAIN_TOKEN, ROBO_INVALID_ARGUMENT,
                             "tokenizer has no source");
    }
    tokenizer->offset = 0u;
    tokenizer->has_peeked = 0;
    tokenizer->tokens_produced = 0u;
    tokenizer->has_error = 0;
    robo_css_token_init(&tokenizer->peeked);
    robo_css_error_init(&tokenizer->error);
    return ROBO_OK;
}

const robo_css_source_t *robo_css_tokenizer_source(const robo_css_tokenizer_t *tokenizer)
{
    return tokenizer ? tokenizer->source : NULL;
}

size_t robo_css_tokenizer_offset(const robo_css_tokenizer_t *tokenizer)
{
    return tokenizer ? tokenizer->offset : 0u;
}

size_t robo_css_tokenizer_remaining(const robo_css_tokenizer_t *tokenizer)
{
    size_t length;

    if (!tokenizer || !tokenizer->source) {
        return 0u;
    }
    length = tokenizer->source->length;
    return (tokenizer->offset >= length) ? 0u : (length - tokenizer->offset);
}

int robo_css_tokenizer_at_end(const robo_css_tokenizer_t *tokenizer)
{
    if (!tokenizer || !tokenizer->source) {
        return 1;
    }
    return tokenizer->offset >= tokenizer->source->length;
}

size_t robo_css_tokenizer_tokens_produced(const robo_css_tokenizer_t *tokenizer)
{
    return tokenizer ? tokenizer->tokens_produced : 0u;
}

int robo_css_tokenizer_has_peek(const robo_css_tokenizer_t *tokenizer)
{
    return (tokenizer != NULL && tokenizer->has_peeked) ? 1 : 0;
}

robo_result_t robo_css_tokenizer_position(const robo_css_tokenizer_t *tokenizer,
                                         robo_css_position_t *out_position,
                                         robo_css_diag_t *diag)
{
    if (!tokenizer || !out_position || !tokenizer->source) {
        return ROBO_CSS_DIAG(diag, ROBO_CSS_DOMAIN_TOKEN, ROBO_INVALID_ARGUMENT,
                             "tokenizer, output or source is NULL");
    }
    return robo_css_source_position(tokenizer->source, tokenizer->offset,
                                    out_position);
}

robo_result_t robo_css_tokenizer_next(robo_css_tokenizer_t *tokenizer,
                                      robo_css_token_t *out_token,
                                      robo_css_diag_t *diag)
{
    robo_result_t result;

    if (!tokenizer || !out_token) {
        return ROBO_CSS_DIAG(diag, ROBO_CSS_DOMAIN_TOKEN, ROBO_INVALID_ARGUMENT,
                             "tokenizer or token is NULL");
    }
    if (!tokenizer->source) {
        return ROBO_CSS_DIAG(diag, ROBO_CSS_DOMAIN_TOKEN, ROBO_INVALID_ARGUMENT,
                             "tokenizer has no source");
    }
    if (tokenizer->has_peeked) {
        *out_token = tokenizer->peeked;
        tokenizer->has_peeked = 0;
        tokenizer->tokens_produced++;
        return ROBO_OK;
    }
    result = produce(tokenizer, out_token, diag);
    if (result != ROBO_OK) {
        return result;
    }
    tokenizer->tokens_produced++;
    return ROBO_OK;
}

robo_result_t robo_css_tokenizer_peek(robo_css_tokenizer_t *tokenizer,
                                      robo_css_token_t *out_token,
                                      robo_css_diag_t *diag)
{
    robo_result_t result;

    if (!tokenizer || !out_token) {
        return ROBO_CSS_DIAG(diag, ROBO_CSS_DOMAIN_TOKEN, ROBO_INVALID_ARGUMENT,
                             "tokenizer or token is NULL");
    }
    if (!tokenizer->source) {
        return ROBO_CSS_DIAG(diag, ROBO_CSS_DOMAIN_TOKEN, ROBO_INVALID_ARGUMENT,
                             "tokenizer has no source");
    }
    if (!tokenizer->has_peeked) {
        result = produce(tokenizer, &tokenizer->peeked, diag);
        if (result != ROBO_OK) {
            return result;
        }
        tokenizer->has_peeked = 1;
    }
    *out_token = tokenizer->peeked;
    return ROBO_OK;
}

robo_result_t robo_css_tokenizer_skip(robo_css_tokenizer_t *tokenizer,
                                     robo_css_diag_t *diag)
{
    robo_css_token_t discarded;
    robo_result_t result;

    if (!tokenizer || !tokenizer->source) {
        return ROBO_CSS_DIAG(diag, ROBO_CSS_DOMAIN_TOKEN, ROBO_INVALID_ARGUMENT,
                             "tokenizer has no source");
    }
    if (tokenizer->has_peeked) {
        tokenizer->has_peeked = 0;
        tokenizer->tokens_produced++;
        return ROBO_OK;
    }
    result = produce(tokenizer, &discarded, diag);
    if (result != ROBO_OK) {
        return result;
    }
    tokenizer->tokens_produced++;
    return ROBO_OK;
}

robo_result_t robo_css_tokenizer_skip_trivia(robo_css_tokenizer_t *tokenizer,
                                            robo_css_diag_t *diag)
{
    for (;;) {
        robo_css_token_t token;
        robo_result_t result = robo_css_tokenizer_peek(tokenizer, &token, diag);

        if (result != ROBO_OK) {
            return result;
        }
        if (!robo_css_token_type_is_ignorable(token.type)) {
            return ROBO_OK;
        }
        tokenizer->has_peeked = 0;
        tokenizer->tokens_produced++;
    }
}

int robo_css_tokenizer_record_error(robo_css_tokenizer_t *tokenizer,
                                    const robo_css_error_t *error)
{
    if (!tokenizer || !error || !robo_css_error_is_error(error) || tokenizer->has_error) {
        return 0;
    }
    tokenizer->error = *error;
    tokenizer->has_error = 1;
    return 1;
}

int robo_css_tokenizer_fail(robo_css_tokenizer_t *tokenizer,
                            robo_css_error_kind_t kind,
                            robo_result_t code,
                            const char *message)
{
    robo_css_error_t error;
    robo_css_position_t position;
    robo_css_position_t *position_ptr = NULL;

    if (!tokenizer || tokenizer->has_error) {
        return 0;
    }
    if (tokenizer->source &&
        robo_css_source_position(tokenizer->source, tokenizer->offset, &position) ==
            ROBO_OK) {
        position_ptr = &position;
    }
    (void)robo_css_error_set(&error, kind, code, position_ptr, message);
    return robo_css_tokenizer_record_error(tokenizer, &error);
}

int robo_css_tokenizer_has_error(const robo_css_tokenizer_t *tokenizer)
{
    return (tokenizer != NULL && tokenizer->has_error) ? 1 : 0;
}

const robo_css_error_t *robo_css_tokenizer_error(const robo_css_tokenizer_t *tokenizer)
{
    if (!tokenizer || !tokenizer->has_error) {
        return NULL;
    }
    return &tokenizer->error;
}

int robo_css_tokenizer_take_error(robo_css_tokenizer_t *tokenizer,
                                  robo_css_error_t *out_error)
{
    if (!tokenizer || !tokenizer->has_error || !out_error) {
        return 0;
    }
    *out_error = tokenizer->error;
    tokenizer->has_error = 0;
    robo_css_error_init(&tokenizer->error);
    return 1;
}
