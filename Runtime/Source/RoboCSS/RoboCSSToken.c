/*
 * RoboCSS token model (Phase 9.1) — see RoboCSSToken.h.
 *
 * Note what is *not* here: no classification of bytes and no numeric
 * conversion. CssTokenMake binds a type to a range and copies the
 * range's bytes into the token's text view, which is all a token record needs;
 * deciding that those bytes *are* an ident, or what number they spell, is the
 * tokenizer's job in 9.2.
 *
 * What is here, added by 9.2, is the other half of a token: its *value*. The
 * value accessors at the bottom of this file resolve escapes and strip the
 * delimiters the token's own type carries, working from `text` alone — a token
 * has no source pointer and needs none, which is what keeps a token a
 * self-contained value that can be copied, stored and compared at any time.
 * They allocate nothing: the comparisons decode escapes as they walk and the
 * only writer is the explicit copy into caller storage.
 */

#include "RoboCSS/RoboCSSToken.h"

#include "RoboCSSInternal.h"

#include <string.h>

int CssTokenTypeIsKnown(CssTokenType type)
{
    return type >= RoboCssTokenNone &&
           type < (CssTokenType)RoboCssTokenTypeCount;
}

const char *CssTokenTypeName(CssTokenType type)
{
    switch (type) {
    case RoboCssTokenNone:
        return "none";
    case RoboCssTokenIdent:
        return "ident";
    case RoboCssTokenFunction:
        return "function";
    case RoboCssTokenAtKeyword:
        return "at-keyword";
    case RoboCssTokenHash:
        return "hash";
    case RoboCssTokenString:
        return "string";
    case RoboCssTokenBadString:
        return "bad-string";
    case RoboCssTokenUrl:
        return "url";
    case RoboCssTokenBadUrl:
        return "bad-url";
    case RoboCssTokenDelim:
        return "delim";
    case RoboCssTokenNumber:
        return "number";
    case RoboCssTokenPercentage:
        return "percentage";
    case RoboCssTokenDimension:
        return "dimension";
    case RoboCssTokenWhitespace:
        return "whitespace";
    case RoboCssTokenComment:
        return "comment";
    case RoboCssTokenCdo:
        return "cdo";
    case RoboCssTokenCdc:
        return "cdc";
    case RoboCssTokenColon:
        return "colon";
    case RoboCssTokenSemicolon:
        return "semicolon";
    case RoboCssTokenComma:
        return "comma";
    case RoboCssTokenLeftParen:
        return "left-paren";
    case RoboCssTokenRightParen:
        return "right-paren";
    case RoboCssTokenLeftSquare:
        return "left-square";
    case RoboCssTokenRightSquare:
        return "right-square";
    case RoboCssTokenLeftCurly:
        return "left-curly";
    case RoboCssTokenRightCurly:
        return "right-curly";
    case RoboCssTokenEof:
        return "eof";
    case RoboCssTokenInvalid:
        return "invalid";
    default:
        return "unknown";
    }
}

int CssTokenTypeIsNumeric(CssTokenType type)
{
    return type == RoboCssTokenNumber || type == RoboCssTokenPercentage ||
           type == RoboCssTokenDimension;
}

int CssTokenTypeIsIgnorable(CssTokenType type)
{
    return type == RoboCssTokenWhitespace || type == RoboCssTokenComment;
}

int CssTokenTypeIsBlockStart(CssTokenType type)
{
    return type == RoboCssTokenLeftParen || type == RoboCssTokenLeftSquare ||
           type == RoboCssTokenLeftCurly;
}

int CssTokenTypeIsBlockEnd(CssTokenType type)
{
    return type == RoboCssTokenRightParen || type == RoboCssTokenRightSquare ||
           type == RoboCssTokenRightCurly;
}

int CssTokenTypeMatchingClose(CssTokenType openType,
                                      CssTokenType *outClose)
{
    CssTokenType close = RoboCssTokenNone;

    switch (openType) {
    case RoboCssTokenLeftParen:
        close = RoboCssTokenRightParen;
        break;
    case RoboCssTokenLeftSquare:
        close = RoboCssTokenRightSquare;
        break;
    case RoboCssTokenLeftCurly:
        close = RoboCssTokenRightCurly;
        break;
    default:
        break;
    }
    if (outClose) {
        *outClose = close;
    }
    return close != RoboCssTokenNone;
}

int CssTokenTypeMatchingOpen(CssTokenType closeType,
                                     CssTokenType *outOpen)
{
    CssTokenType open = RoboCssTokenNone;

    switch (closeType) {
    case RoboCssTokenRightParen:
        open = RoboCssTokenLeftParen;
        break;
    case RoboCssTokenRightSquare:
        open = RoboCssTokenLeftSquare;
        break;
    case RoboCssTokenRightCurly:
        open = RoboCssTokenLeftCurly;
        break;
    default:
        break;
    }
    if (outOpen) {
        *outOpen = open;
    }
    return open != RoboCssTokenNone;
}

int CssHashTypeIsKnown(CssHashType type)
{
    return type >= RoboCssHashTypeNone &&
           type < (CssHashType)RoboCssHashTypeCount;
}

const char *CssHashTypeName(CssHashType type)
{
    switch (type) {
    case RoboCssHashTypeNone:
        return "none";
    case RoboCssHashTypeId:
        return "id";
    case RoboCssHashTypeUnrestricted:
        return "unrestricted";
    default:
        return "unknown";
    }
}

/* ------------------------------------------------------------------ */
/* Tokens                                                             */
/* ------------------------------------------------------------------ */

void CssTokenInit(CssToken *token)
{
    if (!token) {
        return;
    }
    token->type = RoboCssTokenNone;
    token->range = CssRangeAt(0u);
    token->text = CssSliceEmpty();
    token->number = 0.0;
    token->isInteger = 0;
    token->hasSign = 0;
    token->repr = CssSliceEmpty();
    token->unit = CssSliceEmpty();
    token->hash = RoboCssHashTypeNone;
    token->closed = 0;
}

void CssTokenDispose(CssToken *token)
{
    CssTokenInit(token);
}

Result CssTokenMake(CssToken *token,
                                  CssTokenType type,
                                  const CssSource *source,
                                  CssRange range,
                                  CssDiag *diag)
{
    CssSlice text;

    if (!token || !source) {
        return RoboCssDiag(diag, RoboCssDomainToken, RoboInvalidArgument,
                             "token or source is NULL");
    }
    if (!CssTokenTypeIsKnown(type)) {
        return RoboCssDiag(diag, RoboCssDomainToken, RoboInvalidArgument,
                             "unknown token type");
    }
    if (!CssSourceRangeIsValid(source, range)) {
        return RoboCssDiag(diag, RoboCssDomainToken, RoboInvalidArgument,
                             "range is not valid in the source");
    }
    if (CssSourceRangeSlice(source, range, &text) != RoboOk) {
        return RoboCssDiag(diag, RoboCssDomainToken, RoboInvalidArgument,
                             "range is not valid in the source");
    }
    CssTokenInit(token);
    token->type = type;
    token->range = range;
    /* A slice with no bytes stays absent rather than becoming a NULL-plus-zero
     * view, so a caller can tell "this token has no text" from "this token's
     * text was empty", which for EOF is the difference that matters. */
    token->text = text;
    return RoboOk;
}

Result CssTokenMakeEof(CssToken *token,
                                      const CssSource *source,
                                      size_t offset,
                                      CssDiag *diag)
{
    Result result;

    if (!token || !source) {
        return RoboCssDiag(diag, RoboCssDomainToken, RoboInvalidArgument,
                             "token or source is NULL");
    }
    if (!CssSourceOffsetIsValid(source, offset)) {
        return RoboCssDiag(diag, RoboCssDomainToken, RoboInvalidArgument,
                             "offset is past the end of the source");
    }
    CssTokenInit(token);
    result = CssTokenMake(token, RoboCssTokenEof, source,
                                 CssRangeAt(offset), diag);
    if (result != RoboOk) {
        return result;
    }
    /* EOF carries no bytes by definition, whatever the empty range resolved to. */
    token->text = CssSliceEmpty();
    return RoboOk;
}

Result CssTokenCopy(const CssToken *token,
                                  CssToken *outToken,
                                  CssDiag *diag)
{
    if (!token || !outToken) {
        return RoboCssDiag(diag, RoboCssDomainToken, RoboInvalidArgument,
                             "token or output is NULL");
    }
    if (token == outToken) {
        return RoboOk;
    }
    *outToken = *token;
    return RoboOk;
}

int CssTokenIsNone(const CssToken *token)
{
    return !token || token->type == RoboCssTokenNone;
}

int CssTokenIsTerminator(const CssToken *token)
{
    if (!token) {
        return 1;
    }
    return token->type == RoboCssTokenNone || token->type == RoboCssTokenEof;
}

CssSlice CssTokenText(const CssToken *token)
{
    if (!token) {
        return CssSliceEmpty();
    }
    return token->text;
}

int CssTokenEqualsAscii(const CssToken *token, const char *ascii)
{
    if (!token) {
        return 0;
    }
    return CssBytesEqualAscii(token->text.bytes, token->text.length, ascii);
}

int CssTokenEqualsAsciiCi(const CssToken *token, const char *ascii)
{
    if (!token) {
        return 0;
    }
    return CssBytesEqualAsciiCi(token->text.bytes, token->text.length, ascii);
}

/* ------------------------------------------------------------------ */
/* The token's value                                                   */
/* ------------------------------------------------------------------ */

/**
 * Non-zero for the token types CSS resolves escapes in.
 *
 * A COMMENT is deliberately not one: a backslash in a comment is a character,
 * not an escape, and the specification says so. Neither is a DIMENSION, whose
 * value is its numeric part — the unit is a name and is a separate slice.
 */
static int TokenResolvesEscapes(CssTokenType type)
{
    return type == RoboCssTokenIdent || type == RoboCssTokenFunction ||
           type == RoboCssTokenAtKeyword || type == RoboCssTokenHash ||
           type == RoboCssTokenString || type == RoboCssTokenBadString ||
           type == RoboCssTokenUrl || type == RoboCssTokenBadUrl;
}

static int BytesAreQuote(uint8_t b)
{
    return b == (uint8_t)'"' || b == (uint8_t)'\'';
}

/**
 * The span of `text` whose escapes resolve to the token's value: the delimiters
 * the token's own type contributes are removed, and nothing else is.
 *
 * Every removal is guarded, so a token built by hand (rather than produced by
 * the tokenizer) yields its whole text rather than a window outside it.
 */
static void TokenValueWindow(const CssToken *token,
                               const uint8_t **outBytes,
                               size_t *outLength)
{
    const uint8_t *bytes = token->text.bytes;
    size_t length = token->text.length;
    size_t begin = 0u;
    size_t end = length;

    switch (token->type) {
    case RoboCssTokenFunction:
        /* `name(` → `name`: the opening paren is part of the token, not of the
         * name, which is what a consumer comparing against "rgb" wants. */
        if (length > 0u && bytes[length - 1u] == (uint8_t)'(') {
            end = length - 1u;
        }
        break;
    case RoboCssTokenAtKeyword:
        if (length > 0u && bytes[0] == (uint8_t)'@') {
            begin = 1u;
        }
        break;
    case RoboCssTokenHash:
        if (length > 0u && bytes[0] == (uint8_t)'#') {
            begin = 1u;
        }
        break;
    case RoboCssTokenDimension:
        /* `10px` → `10`. The unit is raw and stays raw: reading it is 9.10. */
        if (token->unit.length > 0u && token->unit.length < length) {
            end = length - token->unit.length;
        }
        break;
    case RoboCssTokenString:
    case RoboCssTokenBadString:
        if (length > 0u && BytesAreQuote(bytes[0])) {
            begin = 1u;
        }
        if (token->type == RoboCssTokenString && token->closed && end > begin &&
            BytesAreQuote(bytes[end - 1u])) {
            end -= 1u;
        }
        /* A string that never closed, and whose input ended on a backslash,
         * drops that backslash: it is the one byte the specification's
         * consume-a-string discards rather than keeping as content. The walk
         * steps over whole escapes, so a backslash that escaped the next byte
         * (`"a\\` , which the string did carry) is never the one dropped. */
        if (token->type == RoboCssTokenString && !token->closed) {
            size_t pos = begin;
            size_t last = begin;

            while (pos < end) {
                if (bytes[pos] == (uint8_t)'\\') {
                    CssEscape escape =
                        CssEscapeScan(bytes + pos, end - pos);

                    if (escape.valid) {
                        pos += escape.length;
                        last = pos;
                        continue;
                    }
                    last = (pos + 1u < end) ? (pos + 1u) : pos;
                    pos += 1u;
                    continue;
                }
                pos++;
                last = pos;
            }
            end = last;
        }
        break;
    case RoboCssTokenUrl:
    case RoboCssTokenBadUrl:
        if (length >= 4u && CssBytesEqualAsciiCi(bytes, 4u, "url(")) {
            begin = 4u;
        }
        /* Only a *closed* url loses its `)`. A bad-url keeps its remnants —
         * there is no value to give it, and inventing one would be worse than
         * handing back what is actually there. */
        if (token->type == RoboCssTokenUrl && token->closed && end > begin &&
            bytes[end - 1u] == (uint8_t)')') {
            end -= 1u;
        }
        /* `url(   foo.png   )` has a value of `foo.png`: the padding is inside
         * the token's range, because the range has to tile the source, and
         * outside its value, because it is not part of the url. The walk is
         * escape-aware so `url(a\ )` keeps the space it spelled — an escaped
         * space is content, not padding. */
        if (token->type == RoboCssTokenUrl) {
            size_t pos = begin;
            size_t last = begin;

            while (pos < end) {
                if (bytes[pos] == (uint8_t)'\\') {
                    CssEscape escape =
                        CssEscapeScan(bytes + pos, end - pos);

                    if (escape.valid) {
                        pos += escape.length;
                        last = pos;
                        continue;
                    }
                }
                if (!CssAsciiIsSpace(bytes[pos])) {
                    last = pos + 1u;
                }
                pos++;
            }
            while (begin < last && CssAsciiIsSpace(bytes[begin])) {
                begin++;
            }
            end = last;
        }
        break;
    default:
        break;
    }
    if (end < begin) {
        end = begin;
    }
    *outBytes = bytes ? bytes + begin : NULL;
    *outLength = end - begin;
}

/**
 * A cursor over a value window that emits the value one piece at a time, each
 * piece either a run of source bytes or the encoding of a hexadecimal escape.
 */
typedef struct tokenValueCursor {
    const uint8_t *bytes;
    size_t length;
    size_t offset;
    int resolve;
    /**
     * 1 when a backslash before a newline is a *line continuation* rather than
     * two characters. It is the string rule: a name or a url never reaches this
     * state, because their scanners refuse such a backslash and end the token
     * before it, so the byte is never in the window being walked.
     */
    int lineContinuation;
} TokenValueCursor;

static void TokenValueCursorInit(TokenValueCursor *cursor,
                                    const CssToken *token)
{
    TokenValueWindow(token, &cursor->bytes, &cursor->length);
    cursor->offset = 0u;
    cursor->resolve = TokenResolvesEscapes(token->type);
    cursor->lineContinuation = token->type == RoboCssTokenString;
}

/**
 * Emit the next piece. @p scratch holds the encoding of a hexadecimal escape,
 * which is the one piece whose bytes are not already in the source.
 * @return 1 when a piece was written, 0 at the end of the window.
 */
static int TokenValueNext(TokenValueCursor *cursor,
                            uint8_t scratch[4],
                            CssSlice *out)
{
    for (;;) {
        size_t start;

        if (cursor->offset >= cursor->length) {
            return 0;
        }
        start = cursor->offset;

        if (cursor->resolve && cursor->bytes[start] == (uint8_t)'\\') {
            CssEscape escape =
                CssEscapeScan(cursor->bytes + start, cursor->length - start);

            if (escape.valid) {
                cursor->offset = start + escape.length;
                if (escape.isHex) {
                    size_t written = 0u;

                    if (CssUtf8Append(scratch, 4u, &written, escape.codePoint)) {
                        *out = CssSliceFromBytes(scratch, written);
                        return 1;
                    }
                    /* Cannot happen for a scalar (four bytes is the maximum), but
                     * a refusal must still emit something rather than lose the
                     * byte. */
                    scratch[0] = 0xEFu;
                    scratch[1] = 0xBFu;
                    scratch[2] = 0xBDu;
                    *out = CssSliceFromBytes(scratch, 3u);
                    return 1;
                }
                /* A literal escape stands for a character the source already
                 * spells, so its bytes are handed straight through — which is
                 * why an escaped multi-byte character is never re-encoded. */
                *out = CssSliceFromBytes(cursor->bytes + start + 1u,
                                                 escape.length - 1u);
                return 1;
            }
            /* A backslash before a newline inside a string is the line
             * continuation the specification defines: both bytes are consumed
             * and *nothing* is appended in their place, which is what lets a
             * long string be wrapped. Skipping rather than emitting is the whole
             * of the rule, so the walk continues to the next real piece. */
            if (cursor->lineContinuation && start + 1u < cursor->length &&
                CssIsNewlineByte(cursor->bytes[start + 1u])) {
                cursor->offset = start + 1u +
                                 CssNewlineLength(cursor->bytes + start + 1u,
                                                         cursor->length - start - 1u);
                continue;
            }
            /* Otherwise an invalid escape is a backslash like any other byte:
             * CSS only treats it as one when a valid escape starts there, and
             * the tokenizer is what decides whether that can happen in this
             * token type. */
        }

        cursor->offset = start + 1u;
        while (cursor->offset < cursor->length) {
            if (cursor->resolve && cursor->bytes[cursor->offset] == (uint8_t)'\\') {
                break;
            }
            cursor->offset++;
        }
        *out = CssSliceFromBytes(cursor->bytes + start, cursor->offset - start);
        return 1;
    }
}

size_t CssTokenValueLength(const CssToken *token)
{
    TokenValueCursor cursor;
    uint8_t scratch[4];
    CssSlice piece;
    size_t total = 0u;

    if (!token) {
        return 0u;
    }
    TokenValueCursorInit(&cursor, token);
    while (TokenValueNext(&cursor, scratch, &piece)) {
        total += piece.length;
    }
    return total;
}

/**
 * The shared comparison behind both equality accessors: walk the value and the
 * ASCII needle together, a byte at a time.
 *
 * A piece may be a run of several bytes, so it is compared byte by byte rather
 * than assumed to be one character: an escape decodes to a piece that is one
 * character, but an escape-free stretch of the value comes back as a run of
 * many, and a run reading `url` matches the needle `url`. A piece holding a
 * multi-byte scalar cannot match an ASCII needle, and the byte comparison
 * settles that on its own — every continuation byte is above 0x7F.
 */
static int TokenValueEqualsAsciiImpl(const CssToken *token,
                                         const char *ascii,
                                         int ignoreCase)
{
    TokenValueCursor cursor;
    uint8_t scratch[4];
    CssSlice piece;
    size_t index = 0u;

    if (!token || !ascii) {
        return 0;
    }
    TokenValueCursorInit(&cursor, token);
    while (TokenValueNext(&cursor, scratch, &piece)) {
        size_t i;

        for (i = 0u; i < piece.length; i++) {
            uint8_t actual;
            uint8_t expected;

            if (ascii[index] == '\0') {
                return 0; /* the value is longer than the needle */
            }
            actual = piece.bytes[i];
            expected = (uint8_t)ascii[index];
            if (ignoreCase) {
                actual = CssAsciiLower(actual);
                expected = CssAsciiLower(expected);
            }
            if (actual != expected) {
                return 0;
            }
            index++;
        }
    }
    return ascii[index] == '\0';
}

int CssTokenValueEqualsAscii(const CssToken *token,
                                      const char *ascii)
{
    return TokenValueEqualsAsciiImpl(token, ascii, 0);
}

int CssTokenValueEqualsAsciiCi(const CssToken *token,
                                         const char *ascii)
{
    return TokenValueEqualsAsciiImpl(token, ascii, 1);
}

Result CssTokenValueCopy(const CssToken *token,
                                        uint8_t *out,
                                        size_t outCapacity,
                                        size_t *outLength,
                                        CssDiag *diag)
{
    TokenValueCursor cursor;
    uint8_t scratch[4];
    CssSlice piece;
    size_t written = 0u;

    if (!token || !out || !outLength) {
        return RoboCssDiag(diag, RoboCssDomainToken, RoboInvalidArgument,
                             "token, output or length is NULL");
    }
    TokenValueCursorInit(&cursor, token);
    while (TokenValueNext(&cursor, scratch, &piece)) {
        if (written + piece.length > outCapacity) {
            /* Report what is needed as well as what went wrong: a caller sizing
             * a second attempt has no other way to learn the size. */
            *outLength = CssTokenValueLength(token);
            return RoboCssDiag(diag, RoboCssDomainToken, RoboInvalidArgument,
                                 "output capacity is smaller than the token value");
        }
        memcpy(out + written, piece.bytes, piece.length);
        written += piece.length;
    }
    *outLength = written;
    return RoboOk;
}

int CssTokenHasEscapes(const CssToken *token)
{
    const uint8_t *bytes = NULL;
    size_t length = 0u;
    size_t i;

    if (!token || !TokenResolvesEscapes(token->type)) {
        return 0;
    }
    TokenValueWindow(token, &bytes, &length);
    for (i = 0u; i < length; i++) {
        if (bytes[i] == (uint8_t)'\\') {
            return 1;
        }
    }
    return 0;
}
