/*
 * The RoboJS lexer (see `RoboJSLexer.h`).
 *
 * One function per lexical family — identifiers and keywords, numbers, strings, punctuators — and one
 * driver that skips whitespace and comments and dispatches. The driver is the only place `line` and
 * `column` move, so a token's span cannot be off by a line because one family forgot to count.
 *
 * The `line` counter is advanced in two places and they are the same two places a person would name:
 * a newline consumed as whitespace, and a newline inside a block comment or a string's line
 * continuation. Nothing else can cross a line.
 */

#include "RoboJS/RoboJSLexer.h"

#include "RoboJSMath.h"
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/* Cursor                                                              */
/* ------------------------------------------------------------------ */

static int JsLexerAtEnd(const JsLexer *lexer)
{
    return lexer->offset >= lexer->length;
}

static char JsLexerChar(const JsLexer *lexer)
{
    return JsLexerAtEnd(lexer) ? '\0' : lexer->source[lexer->offset];
}

static char JsLexerCharAt(const JsLexer *lexer, int32_t lookahead)
{
    int32_t index = lexer->offset + lookahead;

    return index >= lexer->length ? '\0' : lexer->source[index];
}

static void JsLexerAdvance(JsLexer *lexer)
{
    if (JsLexerAtEnd(lexer)) {
        return;
    }
    if (lexer->source[lexer->offset] == '\n') {
        lexer->line += 1;
        lexer->column = 1;
    } else {
        lexer->column += 1;
    }
    lexer->offset += 1;
}

static JsSpan JsLexerMark(const JsLexer *lexer)
{
    JsSpan span;

    span.line = lexer->line;
    span.column = lexer->column;
    span.offset = lexer->offset;
    span.endLine = span.line;
    span.endColumn = span.column;
    span.endOffset = span.offset;
    return span;
}

static void JsLexerFinishSpan(const JsLexer *lexer, JsSpan *span)
{
    span->endLine = lexer->line;
    span->endColumn = lexer->column;
    span->endOffset = lexer->offset;
}

static void JsLexerFail(JsLexer *lexer, const JsSpan *span, const char *format, ...)
{
    va_list args;
    char message[200];

    va_start(args, format);
    vsnprintf(message, sizeof(message), format, args);
    va_end(args);
    lexer->failed = 1;
    JsErrorSet(&lexer->error, RoboJsErrorSyntax, span, "%s", message);
}

static int JsLexerEnsureScratch(JsLexer *lexer, int32_t needed)
{
    char *fresh;
    int32_t capacity;

    if (lexer->scratchCapacity >= needed) {
        return 1;
    }
    capacity = lexer->scratchCapacity > 0 ? lexer->scratchCapacity : 64;
    while (capacity < needed) {
        capacity *= 2;
    }
    if (capacity > JsHeapMaxStringLength(lexer->heap)) {
        capacity = needed;
    }
    fresh = (char *)JsHeapRealloc(lexer->heap,
                                  lexer->scratch,
                                  (size_t)lexer->scratchCapacity,
                                  (size_t)capacity);
    if (fresh == NULL) {
        return 0;
    }
    lexer->scratch = fresh;
    lexer->scratchCapacity = capacity;
    return 1;
}

/* ------------------------------------------------------------------ */
/* Whitespace and comments                                             */
/* ------------------------------------------------------------------ */

static int JsLexerIsSpace(char c)
{
    return c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\f' || c == '\v';
}

/**
 * Skip whitespace and comments.
 * @return RoboOk, or `RoboParseError` for an unterminated block comment.
 */
static Result JsLexerSkipTrivia(JsLexer *lexer)
{
    for (;;) {
        char c = JsLexerChar(lexer);

        if (JsLexerIsSpace(c)) {
            JsLexerAdvance(lexer);
            continue;
        }
        if (c == '/' && JsLexerCharAt(lexer, 1) == '/') {
            while (!JsLexerAtEnd(lexer) && JsLexerChar(lexer) != '\n') {
                JsLexerAdvance(lexer);
            }
            continue;
        }
        if (c == '/' && JsLexerCharAt(lexer, 1) == '*') {
            JsSpan start = JsLexerMark(lexer);

            JsLexerAdvance(lexer);
            JsLexerAdvance(lexer);
            for (;;) {
                if (JsLexerAtEnd(lexer)) {
                    JsLexerFail(lexer, &start, "unterminated block comment");
                    return RoboParseError;
                }
                if (JsLexerChar(lexer) == '*' && JsLexerCharAt(lexer, 1) == '/') {
                    JsLexerAdvance(lexer);
                    JsLexerAdvance(lexer);
                    break;
                }
                JsLexerAdvance(lexer);
            }
            continue;
        }
        return RoboOk;
    }
}

/* ------------------------------------------------------------------ */
/* Identifiers and keywords                                            */
/* ------------------------------------------------------------------ */

typedef struct JsKeyword {
    const char *text;
    uint32_t kind;
} JsKeyword;

static const JsKeyword JsKeywords[] = {
    { "var", RoboJsTokenVar },
    { "let", RoboJsTokenLet },
    { "const", RoboJsTokenConst },
    { "function", RoboJsTokenFunction },
    { "return", RoboJsTokenReturn },
    { "if", RoboJsTokenIf },
    { "else", RoboJsTokenElse },
    { "while", RoboJsTokenWhile },
    { "do", RoboJsTokenDo },
    { "for", RoboJsTokenFor },
    { "break", RoboJsTokenBreak },
    { "continue", RoboJsTokenContinue },
    { "throw", RoboJsTokenThrow },
    { "try", RoboJsTokenTry },
    { "catch", RoboJsTokenCatch },
    { "finally", RoboJsTokenFinally },
    { "switch", RoboJsTokenSwitch },
    { "case", RoboJsTokenCase },
    { "default", RoboJsTokenDefault },
    { "new", RoboJsTokenNew },
    { "this", RoboJsTokenThis },
    { "typeof", RoboJsTokenTypeof },
    { "delete", RoboJsTokenDelete },
    { "void", RoboJsTokenVoid },
    { "in", RoboJsTokenIn },
    { "instanceof", RoboJsTokenInstanceof },
    { "true", RoboJsTokenTrue },
    { "false", RoboJsTokenFalse },
    { "null", RoboJsTokenNull },
    { "undefined", RoboJsTokenUndefined }
};

#define JsKeywordCount ((int)(sizeof(JsKeywords) / sizeof(JsKeywords[0])))

static int JsIdentifierStart(char c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_' || c == '$';
}

static int JsIdentifierPart(char c)
{
    return JsIdentifierStart(c) || (c >= '0' && c <= '9');
}

static Result JsLexerReadName(JsLexer *lexer, JsToken *out, JsSpan *start)
{
    int32_t begin = lexer->offset;
    int32_t length;
    int i;

    while (JsIdentifierPart(JsLexerChar(lexer))) {
        JsLexerAdvance(lexer);
    }
    length = lexer->offset - begin;
    out->text = lexer->source + begin;
    out->length = length;
    for (i = 0; i < JsKeywordCount; ++i) {
        size_t keywordLength = strlen(JsKeywords[i].text);

        if ((int32_t)keywordLength == length &&
            strncmp(lexer->source + begin, JsKeywords[i].text, keywordLength) == 0) {
            out->kind = JsKeywords[i].kind;
            JsLexerFinishSpan(lexer, start);
            out->span = *start;
            return RoboOk;
        }
    }
    out->kind = RoboJsTokenIdentifier;
    JsLexerFinishSpan(lexer, start);
    out->span = *start;
    return RoboOk;
}

/* ------------------------------------------------------------------ */
/* Numbers                                                             */
/* ------------------------------------------------------------------ */

static Result JsLexerReadNumber(JsLexer *lexer, JsToken *out, JsSpan *start)
{
    double value = 0.0;
    double fraction = 0.0;
    double divisor = 1.0;
    int seenDigit = 0;
    int seenDot = 0;
    int exponent = 0;
    int exponentNegative = 0;
    int seenExponentDigit = 0;
    int32_t begin = lexer->offset;
    int32_t length;

    while (!JsLexerAtEnd(lexer)) {
        char c = JsLexerChar(lexer);

        if (c >= '0' && c <= '9') {
            if (seenDot) {
                divisor *= 10.0;
                fraction += (double)(c - '0') / divisor;
            } else {
                value = value * 10.0 + (double)(c - '0');
            }
            seenDigit = 1;
            JsLexerAdvance(lexer);
        } else if (c == '.' && !seenDot) {
            seenDot = 1;
            JsLexerAdvance(lexer);
        } else {
            break;
        }
    }
    if (!seenDigit) {
        JsLexerFail(lexer, start, "a number needs at least one digit");
        return RoboParseError;
    }
    if (JsLexerChar(lexer) == 'e' || JsLexerChar(lexer) == 'E') {
        JsLexerAdvance(lexer);
        if (JsLexerChar(lexer) == '+' || JsLexerChar(lexer) == '-') {
            exponentNegative = JsLexerChar(lexer) == '-';
            JsLexerAdvance(lexer);
        }
        while (JsLexerChar(lexer) >= '0' && JsLexerChar(lexer) <= '9') {
            exponent = exponent * 10 + (JsLexerChar(lexer) - '0');
            if (exponent > 308) {
                exponent = 308;
            }
            seenExponentDigit = 1;
            JsLexerAdvance(lexer);
        }
        if (!seenExponentDigit) {
            JsLexerFail(lexer, start, "an exponent needs at least one digit");
            return RoboParseError;
        }
    }
    value += fraction;
    if (exponent != 0) {
        value *= JsMathPow(10.0, exponentNegative ? -(double)exponent : (double)exponent);
    }
    length = lexer->offset - begin;
    if (!JsLexerEnsureScratch(lexer, length + 1)) {
        JsLexerFail(lexer, start, "out of memory reading a number");
        return RoboOutOfMemory;
    }
    memcpy(lexer->scratch, lexer->source + begin, (size_t)length);
    lexer->scratch[length] = '\0';
    out->kind = RoboJsTokenNumber;
    out->number = value;
    out->text = lexer->scratch;
    out->length = length;
    JsLexerFinishSpan(lexer, (JsSpan *)start);
    out->span = *start;
    return RoboOk;
}

/* ------------------------------------------------------------------ */
/* Strings                                                             */
/* ------------------------------------------------------------------ */

static void JsLexerEmitUtf8(JsLexer *lexer, int32_t *written, uint32_t code)
{
    if (code < 0x80u) {
        lexer->scratch[*written] = (char)code;
        *written += 1;
    } else if (code < 0x800u) {
        lexer->scratch[*written] = (char)(0xC0u | (code >> 6));
        lexer->scratch[*written + 1] = (char)(0x80u | (code & 0x3Fu));
        *written += 2;
    } else {
        lexer->scratch[*written] = (char)(0xE0u | (code >> 12));
        lexer->scratch[*written + 1] = (char)(0x80u | ((code >> 6) & 0x3Fu));
        lexer->scratch[*written + 2] = (char)(0x80u | (code & 0x3Fu));
        *written += 3;
    }
}

static int JsHexDigit(char c)
{
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    if (c >= 'a' && c <= 'f') {
        return c - 'a' + 10;
    }
    if (c >= 'A' && c <= 'F') {
        return c - 'A' + 10;
    }
    return -1;
}

static Result JsLexerReadString(JsLexer *lexer, JsToken *out, JsSpan *start)
{
    char quote = JsLexerChar(lexer);
    int32_t written = 0;
    int32_t needed;

    JsLexerAdvance(lexer);
    needed = (lexer->length - lexer->offset) + 4;
    if (needed < 32) {
        needed = 32;
    }
    if (!JsLexerEnsureScratch(lexer, needed)) {
        JsLexerFail(lexer, start, "out of memory reading a string");
        return RoboOutOfMemory;
    }
    for (;;) {
        char c = JsLexerChar(lexer);

        if (JsLexerAtEnd(lexer)) {
            JsLexerFail(lexer, start, "unterminated string literal");
            return RoboParseError;
        }
        if (c == quote) {
            JsLexerAdvance(lexer);
            break;
        }
        if (c == '\n') {
            JsLexerFail(lexer, start, "a string literal may not span a line");
            return RoboParseError;
        }
        if (written + 4 >= lexer->scratchCapacity) {
            if (!JsLexerEnsureScratch(lexer, lexer->scratchCapacity * 2)) {
                JsLexerFail(lexer, start, "out of memory reading a string");
                return RoboOutOfMemory;
            }
        }
        if (c == '\\') {
            JsLexerAdvance(lexer);
            c = JsLexerChar(lexer);
            switch (c) {
            case 'n': lexer->scratch[written++] = '\n'; JsLexerAdvance(lexer); break;
            case 't': lexer->scratch[written++] = '\t'; JsLexerAdvance(lexer); break;
            case 'r': lexer->scratch[written++] = '\r'; JsLexerAdvance(lexer); break;
            case 'b': lexer->scratch[written++] = '\b'; JsLexerAdvance(lexer); break;
            case 'f': lexer->scratch[written++] = '\f'; JsLexerAdvance(lexer); break;
            case 'v': lexer->scratch[written++] = '\v'; JsLexerAdvance(lexer); break;
            case '0': lexer->scratch[written++] = '\0'; JsLexerAdvance(lexer); break;
            case '\\': lexer->scratch[written++] = '\\'; JsLexerAdvance(lexer); break;
            case '\'': lexer->scratch[written++] = '\''; JsLexerAdvance(lexer); break;
            case '"': lexer->scratch[written++] = '"'; JsLexerAdvance(lexer); break;
            case 'x': {
                int hi;
                int lo;

                JsLexerAdvance(lexer);
                hi = JsHexDigit(JsLexerChar(lexer));
                JsLexerAdvance(lexer);
                lo = JsHexDigit(JsLexerChar(lexer));
                JsLexerAdvance(lexer);
                if (hi < 0 || lo < 0) {
                    JsLexerFail(lexer, start, "\\x needs two hexadecimal digits");
                    return RoboParseError;
                }
                lexer->scratch[written++] = (char)((hi << 4) | lo);
                break;
            }
            case 'u': {
                uint32_t code = 0;
                int i;

                JsLexerAdvance(lexer);
                for (i = 0; i < 4; ++i) {
                    int digit = JsHexDigit(JsLexerChar(lexer));

                    if (digit < 0) {
                        JsLexerFail(lexer, start, "\\u needs four hexadecimal digits");
                        return RoboParseError;
                    }
                    code = (code << 4) | (uint32_t)digit;
                    JsLexerAdvance(lexer);
                }
                if (written + 4 >= lexer->scratchCapacity) {
                    if (!JsLexerEnsureScratch(lexer, lexer->scratchCapacity * 2)) {
                        JsLexerFail(lexer, start, "out of memory reading a string");
                        return RoboOutOfMemory;
                    }
                }
                JsLexerEmitUtf8(lexer, &written, code);
                break;
            }
            default:
                JsLexerFail(lexer, start, "unsupported escape \\%c", c != '\0' ? c : '?');
                return RoboParseError;
            }
            continue;
        }
        lexer->scratch[written++] = c;
        JsLexerAdvance(lexer);
    }
    lexer->scratch[written] = '\0';
    out->kind = RoboJsTokenString;
    out->text = lexer->scratch;
    out->length = written;
    JsLexerFinishSpan(lexer, start);
    out->span = *start;
    return RoboOk;
}

/* ------------------------------------------------------------------ */
/* Punctuators                                                         */
/* ------------------------------------------------------------------ */

static Result JsLexerReadPunctuator(JsLexer *lexer, JsToken *out, JsSpan *start)
{
    char c = JsLexerChar(lexer);
    char next = JsLexerCharAt(lexer, 1);
    uint32_t kind = 0;

    switch (c) {
    case '(': kind = RoboJsTokenLParen; break;
    case ')': kind = RoboJsTokenRParen; break;
    case '{': kind = RoboJsTokenLBrace; break;
    case '}': kind = RoboJsTokenRBrace; break;
    case '[': kind = RoboJsTokenLBracket; break;
    case ']': kind = RoboJsTokenRBracket; break;
    case '.': kind = RoboJsTokenDot; break;
    case ',': kind = RoboJsTokenComma; break;
    case ';': kind = RoboJsTokenSemicolon; break;
    case ':': kind = RoboJsTokenColon; break;
    case '?': kind = RoboJsTokenQuestion; break;
    case '~': /* `~` is bitwise-not, which this build does not have; refuse by name below. */
        JsLexerFail(lexer, start, "'~' is not supported by this JavaScript subset");
        return RoboParseError;
    case '+':
        if (next == '+') {
            kind = RoboJsTokenPlusPlus;
        } else if (next == '=') {
            kind = RoboJsTokenPlusAssign;
        } else {
            kind = RoboJsTokenPlus;
        }
        break;
    case '-':
        if (next == '-') {
            kind = RoboJsTokenMinusMinus;
        } else if (next == '=') {
            kind = RoboJsTokenMinusAssign;
        } else {
            kind = RoboJsTokenMinus;
        }
        break;
    case '*':
        kind = next == '=' ? RoboJsTokenStarAssign : RoboJsTokenStar;
        break;
    case '/':
        kind = next == '=' ? RoboJsTokenSlashAssign : RoboJsTokenSlash;
        break;
    case '%':
        kind = next == '=' ? RoboJsTokenPercentAssign : RoboJsTokenPercent;
        break;
    case '=':
        if (next == '=' && JsLexerCharAt(lexer, 2) == '=') {
            kind = RoboJsTokenEqEqEq;
        } else if (next == '=') {
            kind = RoboJsTokenEqEq;
        } else if (next == '>') {
            kind = RoboJsTokenArrow;
        } else {
            kind = RoboJsTokenAssign;
        }
        break;
    case '!':
        if (next == '=' && JsLexerCharAt(lexer, 2) == '=') {
            kind = RoboJsTokenNotEqEq;
        } else if (next == '=') {
            kind = RoboJsTokenNotEq;
        } else {
            kind = RoboJsTokenNot;
        }
        break;
    case '<':
        kind = next == '=' ? RoboJsTokenLtEq : RoboJsTokenLt;
        break;
    case '>':
        kind = next == '=' ? RoboJsTokenGtEq : RoboJsTokenGt;
        break;
    case '&':
        if (next != '&') {
            JsLexerFail(lexer, start, "'&' alone is a bitwise operator this subset does not have");
            return RoboParseError;
        }
        kind = RoboJsTokenAndAnd;
        break;
    case '|':
        if (next != '|') {
            JsLexerFail(lexer, start, "'|' alone is a bitwise operator this subset does not have");
            return RoboParseError;
        }
        kind = RoboJsTokenOrOr;
        break;
    default:
        JsLexerFail(lexer, start,
                    "unexpected character '%c' (0x%02x)",
                    c >= 32 && c < 127 ? c : '?',
                    (unsigned)(unsigned char)c);
        return RoboParseError;
    }
    /* Every punctuator whose text is longer than one byte has its own entry in the twin table in
     * `RoboJSTypes.c`; the length is taken from that spelling so the two cannot disagree. */
    {
        const char *spelling = JsTokenKindText((JsTokenKind)kind);
        int32_t length = spelling != NULL ? (int32_t)strlen(spelling) : 1;
        int32_t i;

        for (i = 0; i < length; ++i) {
            JsLexerAdvance(lexer);
        }
    }
    out->kind = kind;
    out->text = JsTokenKindText((JsTokenKind)kind);
    out->length = out->text != NULL ? (int32_t)strlen(out->text) : 0;
    JsLexerFinishSpan(lexer, start);
    out->span = *start;
    return RoboOk;
}

/* ------------------------------------------------------------------ */
/* Driver                                                              */
/* ------------------------------------------------------------------ */

Result JsLexerInit(JsLexer *lexer, JsHeap *heap, const char *source, int32_t length)
{
    if (lexer == NULL || heap == NULL || source == NULL) {
        return RoboInvalidArgument;
    }
    memset(lexer, 0, sizeof(*lexer));
    lexer->heap = heap;
    lexer->source = source;
    lexer->length = length >= 0 ? length : (int32_t)strlen(source);
    lexer->line = 1;
    lexer->column = 1;
    JsErrorInit(&lexer->error);
    if (lexer->length < 0) {
        return RoboInvalidArgument;
    }
    return RoboOk;
}

Result JsLexerNext(JsLexer *lexer, JsToken *out)
{
    JsSpan start;
    Result status;

    if (lexer == NULL || out == NULL) {
        return RoboInvalidArgument;
    }
    memset(out, 0, sizeof(*out));
    if (lexer->failed) {
        return RoboParseError;
    }
    if (lexer->peeked) {
        lexer->peeked = 0;
        *out = lexer->peek;
        return RoboOk;
    }
    status = JsLexerSkipTrivia(lexer);
    if (status != RoboOk) {
        return status;
    }
    start = JsLexerMark(lexer);
    if (JsLexerAtEnd(lexer)) {
        out->kind = RoboJsTokenEnd;
        out->span = start;
        JsLexerFinishSpan(lexer, &out->span);
        return RoboOk;
    }
    {
        char c = JsLexerChar(lexer);

        if (JsIdentifierStart(c)) {
            return JsLexerReadName(lexer, out, &start);
        }
        if (c >= '0' && c <= '9') {
            return JsLexerReadNumber(lexer, out, &start);
        }
        if (c == '.' && JsLexerCharAt(lexer, 1) >= '0' && JsLexerCharAt(lexer, 1) <= '9') {
            return JsLexerReadNumber(lexer, out, &start);
        }
        if (c == '"' || c == '\'') {
            return JsLexerReadString(lexer, out, &start);
        }
        if (c == '`') {
            JsLexerFail(lexer, &start, "template literals are not supported by this subset");
            return RoboParseError;
        }
        return JsLexerReadPunctuator(lexer, out, &start);
    }
}

Result JsLexerPeek(JsLexer *lexer, JsToken *out)
{
    Result status;

    if (lexer == NULL || out == NULL) {
        return RoboInvalidArgument;
    }
    if (!lexer->peeked) {
        JsToken token;

        memset(&token, 0, sizeof(token));
        status = JsLexerNext(lexer, &token);
        if (status != RoboOk) {
            return status;
        }
        lexer->peek = token;
        lexer->peeked = 1;
    }
    *out = lexer->peek;
    return RoboOk;
}

const JsError *JsLexerError(const JsLexer *lexer)
{
    static const JsError none = { RoboJsErrorNone, { 0 }, { 1, 1, 0, 1, 1, 0 }, 0 };

    return lexer != NULL ? &lexer->error : &none;
}

int32_t JsTokenCopyText(const JsToken *token, char *buffer, size_t capacity)
{
    int32_t length;

    if (token == NULL) {
        return 0;
    }
    length = token->length;
    if (buffer != NULL && capacity > 0) {
        size_t copy = (size_t)length < capacity - 1u ? (size_t)length : capacity - 1u;

        if (token->text != NULL && copy > 0) {
            memcpy(buffer, token->text, copy);
        }
        buffer[copy] = '\0';
    }
    return length;
}

int JsTokenKindIsKeyword(JsTokenKind kind)
{
    return (int)kind >= (int)RoboJsTokenVar && (int)kind <= (int)RoboJsTokenUndefined;
}

int JsTokenKindStartsExpression(JsTokenKind kind)
{
    switch (kind) {
    case RoboJsTokenIdentifier:
    case RoboJsTokenNumber:
    case RoboJsTokenString:
    case RoboJsTokenTrue:
    case RoboJsTokenFalse:
    case RoboJsTokenNull:
    case RoboJsTokenUndefined:
    case RoboJsTokenThis:
    case RoboJsTokenNew:
    case RoboJsTokenTypeof:
    case RoboJsTokenDelete:
    case RoboJsTokenVoid:
    case RoboJsTokenFunction:
    case RoboJsTokenLParen:
    case RoboJsTokenLBracket:
    case RoboJsTokenLBrace:
    case RoboJsTokenNot:
    case RoboJsTokenMinus:
    case RoboJsTokenPlus:
    case RoboJsTokenPlusPlus:
    case RoboJsTokenMinusMinus:
    case RoboJsTokenArrow:
        return 1;
    default:
        return 0;
    }
}
