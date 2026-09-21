/*
 * RoboHTML tokenizer (Phase 8.2).
 *
 * An explicit state machine in the shape of the HTML tokenizer model
 * (independent implementation; no Chromium code or data structures).
 *
 * Structure: HtmlTokenizerNext() drives a byte loop; each state has
 * a handler function returning either CONTINUE (consume another byte in a
 * new state) or EMIT (a finished token is in t->token). Character-run
 * merging for the common data-state case is done by the data handler
 * scanning ahead so plain text yields one character token per run.
 */

#include "RoboHTML/RoboHTML.h"
#include "RoboHTML/RoboHTMLTokenizer.h"
#include "RoboHTMLCharRef.h"
#include "RoboHTMLTokenizerInternal.h"

#include "RoboBase/RoboLog.h"
#include "RoboMem/RoboMem.h"
#include "RoboUnicode/RoboUnicode.h"

#include <stddef.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/* Small helpers                                                       */
/* ------------------------------------------------------------------ */

char HtmlTokAsciiLower(char c)
{
    if (c >= 'A' && c <= 'Z') {
        return (char)(c - 'A' + 'a');
    }
    return c;
}

char HtmlTokPeek(const HtmlTokenizer *t)
{
    if (!t || t->pos >= t->length) {
        return '\0';
    }
    return t->bytes[t->pos];
}

char HtmlTokPeekAhead(const HtmlTokenizer *t, size_t ahead)
{
    if (!t || t->pos + ahead >= t->length) {
        return '\0';
    }
    return t->bytes[t->pos + ahead];
}

void HtmlTokAdvance(HtmlTokenizer *t)
{
    if (!t || t->pos >= t->length) {
        return;
    }
    if (t->bytes[t->pos] == '\n') {
        t->line++;
        t->column = 1u;
    } else {
        t->column++;
    }
    t->pos++;
}

int HtmlTokWordAt(const HtmlTokenizer *t, const char *word)
{
    size_t i;

    if (!t || !word) {
        return 0;
    }
    for (i = 0u; word[i] != '\0'; i++) {
        if (t->pos + i >= t->length ||
            HtmlTokAsciiLower(t->bytes[t->pos + i]) != word[i]) {
            return 0;
        }
    }
    return 1;
}

static int IsWs(char c)
{
    /*
     * HTML whitespace: TAB, LF, FF, CR, SPACE. (CR is normalized away by
     * the input preprocessor in the standard; Roboticela drops CR before
     * LF and treats a lone CR as LF-equivalent — see data-state handling.)
     */
    return c == '\t' || c == '\n' || c == '\f' || c == '\r' || c == ' ';
}

/** ASCII case-insensitive equality of two byte ranges (no locale). */
static int AsciiCasecmpBytes(const char *a, const char *b, size_t n)
{
    size_t i;
    for (i = 0u; i < n; i++) {
        if (HtmlTokAsciiLower(a[i]) != HtmlTokAsciiLower(b[i])) {
            return 0;
        }
    }
    return 1;
}

static void diag(HtmlTokenizer *t,
                 Result code,
                 const char *msg)
{
    HtmlDiagnosticsPush(&t->diags, code, RoboHtmlDomainParser, msg,
                               t->line, t->column);
}

static void SnapshotPos(HtmlTokenizer *t)
{
    t->tokStartOffset = t->pos;
    t->tokStartLine = t->line;
    t->tokStartCol = t->column;
}

static void TokenBegin(HtmlTokenizer *t, HtmlTokenKind kind)
{
    HtmlTokenInit(&t->token);
    t->token.kind = kind;
    t->token.pos.offset = t->tokStartOffset;
    t->token.pos.line = t->tokStartLine;
    t->token.pos.column = t->tokStartCol;
}

/* Mark a finished token's end position as its start (kept lightweight). */
static void TokenFinish(HtmlTokenizer *t)
{
    (void)t;
}

static void SetCurrentAttrNameByte(HtmlTokenizer *t, char c)
{
    if (t->currentAttr) {
        (void)HtmlTokenBufferAppendByte(&t->currentAttr->name, c);
    }
}

static void SetCurrentAttrValueByte(HtmlTokenizer *t, char c)
{
    if (t->currentAttr) {
        (void)HtmlTokenBufferAppendByte(&t->currentAttr->value, c);
    }
}

static void SetCurrentAttrValueCp(HtmlTokenizer *t, uint32_t cp)
{
    if (t->currentAttr) {
        (void)HtmlTokenBufferAppendUtf8(&t->currentAttr->value, cp);
    }
}

static int IsAsciiLetter(char c)
{
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z');
}

static int IsAsciiNameChar(char c)
{
    return IsAsciiLetter(c) || (c >= '0' && c <= '9');
}

/* ------------------------------------------------------------------ */
/* Character references                                                */
/* ------------------------------------------------------------------ */

/**
 * Expand a character reference at the cursor inside @p context.
 * On match: appends UTF-8 to @p buf (or the current attribute value when
 * @p buf is NULL) and advances the cursor. On no-match: emits '&' and
 * advances one byte. Diagnostics recorded per the HTML rules.
 */
static void ConsumeCharref(HtmlTokenizer *t,
                            HtmlTokenBuffer *buf,
                            int inAttribute)
{
    size_t offset = t->pos;
    HtmlCharrefError err = RoboHtmlCharrefOk;

    if (HtmlCharrefExpand(t->bytes, t->length, &offset, inAttribute,
                                 t->charrefOut, &t->charrefLen,
                                 &err)) {
        size_t i;
        for (i = 0u; i < t->charrefLen; i++) {
            if (buf) {
                (void)HtmlTokenBufferAppendByte(buf, t->charrefOut[i]);
            } else {
                SetCurrentAttrValueByte(t, t->charrefOut[i]);
            }
        }
        while (t->pos < offset) {
            HtmlTokAdvance(t);
        }
        if (err == RoboHtmlCharrefMissingSemicolon) {
            diag(t, RoboParseError, "character reference missing ';'");
        } else if (err == RoboHtmlCharrefOutOfRange) {
            diag(t, RoboParseError,
                 "character reference value out of range (mapped)");
        }
    } else {
        /* Literal '&' (the following bytes re-enter the normal scan). */
        if (buf) {
            (void)HtmlTokenBufferAppendByte(buf, '&');
        } else {
            SetCurrentAttrValueByte(t, '&');
        }
        HtmlTokAdvance(t);
        if (HtmlTokPeek(t) == '#') {
            diag(t, RoboParseError, "invalid numeric character reference");
        } else if (IsAsciiNameChar(HtmlTokPeek(t))) {
            diag(t, RoboParseError, "unknown character reference");
        }
    }
}

/* ------------------------------------------------------------------ */
/* Comment states                                                      */
/* ------------------------------------------------------------------ */

typedef enum tokResult {
    TOK_CONTINUE = 0,
    TOK_EMIT = 1,
    TOK_ERROR = 2
} TokResult;

static TokResult StData(HtmlTokenizer *t);
static TokResult StTagOpen(HtmlTokenizer *t);
static TokResult StTagName(HtmlTokenizer *t);
static TokResult StEndTagOpen(HtmlTokenizer *t);
static TokResult StBeforeAttrName(HtmlTokenizer *t);
static TokResult StAttrName(HtmlTokenizer *t);
static TokResult StAfterAttrName(HtmlTokenizer *t);
static TokResult StBeforeAttrValue(HtmlTokenizer *t);
static TokResult StAttrValueQuoted(HtmlTokenizer *t, char quote);
static TokResult StAttrValueUnquoted(HtmlTokenizer *t);
static TokResult StAfterAttrValueQuoted(HtmlTokenizer *t);
static TokResult StSelfClosingStartTag(HtmlTokenizer *t);
static TokResult StMarkupDeclarationOpen(HtmlTokenizer *t);
static TokResult StCommentBody(HtmlTokenizer *t);
static TokResult StDoctypeName(HtmlTokenizer *t);
static TokResult StDoctypeAfterName(HtmlTokenizer *t);
static TokResult StBeforeDoctypePublicId(HtmlTokenizer *t);
static TokResult StBeforeDoctypeSystemId(HtmlTokenizer *t);
static TokResult StBogusDoctype(HtmlTokenizer *t);
static TokResult StBogusComment(HtmlTokenizer *t);
static TokResult StRawtextLike(HtmlTokenizer *t);

/**
 * Emit a stable EOF token. Used both for clean end-of-input and as the
 * recovery when EOF is reached inside a markup state (the partially
 * accumulated tag/comment content is dropped with a diagnostic).
 */
static TokResult EmitEofToken(HtmlTokenizer *t)
{
    TokenBegin(t, RoboHtmlTokenEof);
    TokenFinish(t);
    t->atEof = 1;
    t->state = RoboHtmlTokenizerStateData;
    return TOK_EMIT;
}

static TokResult EmitCharToken(HtmlTokenizer *t,
                                    HtmlTokenBuffer *run)
{
    TokenBegin(t, RoboHtmlTokenCharacter);
    /* The token borrows the character bytes: move the run into the
     * tokenizer-owned chars buffer so the data stays valid until the next
     * character emission / reset / destroy (borrowed-token contract). */
    HtmlTokenBufferFree(&t->chars);
    t->chars = *run;
    run->data = NULL;
    run->length = 0u;
    run->capacity = 0u;
    t->token.u.characters.data = t->chars.data ? t->chars.data : "";
    t->token.u.characters.length = t->chars.length;
    TokenFinish(t);
    return TOK_EMIT;
}

/* ------------------------------------------------------------------ */
/* Data state                                                          */
/* ------------------------------------------------------------------ */

static TokResult StData(HtmlTokenizer *t)
{
    HtmlTokenBuffer run;
    char c;

    HtmlTokenBufferInit(&run);
    SnapshotPos(t);

    while (t->pos < t->length) {
        c = HtmlTokPeek(t);
        if (c == '&') {
            ConsumeCharref(t, &run, 0);
            continue;
        }
        if (c == '<') {
            /* Markup only when the next byte opens a construct: '!', '/',
             * '?', or a tag-name letter. Otherwise '<' is literal text
             * ("a < b" stays one character run). */
            char n = HtmlTokPeekAhead(t, 1);
            if (n == '!' || n == '/' || n == '?' || IsAsciiLetter(n)) {
                HtmlTokenizerState nextState =
                    (n == '/') ? RoboHtmlTokenizerStateEndTagOpen
                    : (n == '!')
                          ? RoboHtmlTokenizerStateMarkupDeclarationOpen
                          : RoboHtmlTokenizerStateTagOpen;
                TokResult r = TOK_CONTINUE;
                if (run.length > 0u) {
                    r = EmitCharToken(t, &run); /* steals run */
                } else {
                    HtmlTokenBufferFree(&run);
                }
                SnapshotPos(t); /* the '<' begins the markup token */
                /* Consume '<' and, for the two states that are defined
                 * *after* their introducer, that introducer too: the
                 * classic tag-open hand-off. END_TAG_OPEN expects the
                 * cursor at the tag name (past "</") and
                 * MARKUP_DECLARATION_OPEN past "<!" (it matches
                 * "doctype" / "--" / "[CDATA[" at the cursor). */
                HtmlTokAdvance(t); /* consume '<' */
                if (n == '/' || n == '!') {
                    HtmlTokAdvance(t); /* consume '/' or '!' */
                }
                t->state = nextState;
                return r;
            }
            (void)HtmlTokenBufferAppendByte(&run, '<');
            HtmlTokAdvance(t);
            continue;
        }
        if (c == '\r') {
            /* CR normalization: emit LF; swallow a following LF. */
            (void)HtmlTokenBufferAppendByte(&run, '\n');
            HtmlTokAdvance(t);
            if (HtmlTokPeek(t) == '\n') {
                HtmlTokAdvance(t);
            }
            continue;
        }
        if (c == '\0') {
            /* NUL in data: replacement character + parse error. */
            (void)HtmlTokenBufferAppendUtf8(&run, 0xFFFDu);
            diag(t, RoboParseError, "NUL character in data state");
            HtmlTokAdvance(t);
            continue;
        }
        /* Multi-byte UTF-8: decode with REPLACE (never out of bounds). */
        {
            uint32_t cp = 0u;
            size_t next = 0u;
            UnicodeStatus st = UnicodeUtf8Decode(
                t->bytes, t->length, t->pos, RoboUnicodeReplace, &cp,
                &next);
            if (st == RoboUnicodeOk || st == RoboUnicodeInvalid ||
                st == RoboUnicodeTruncated) {
                (void)HtmlTokenBufferAppendUtf8(&run, cp);
                if (st != RoboUnicodeOk) {
                    diag(t, RoboParseError,
                         "ill-formed UTF-8 in text (replacement used)");
                }
                while (t->pos < next) {
                    HtmlTokAdvance(t);
                }
                continue;
            }
        }
        (void)HtmlTokenBufferAppendByte(&run, c);
        HtmlTokAdvance(t);
    }

    /* EOF: flush the run if non-empty, else end the stream. */
    if (run.length > 0u) {
        /* EmitCharToken steals the run storage. */
        return EmitCharToken(t, &run);
    }
    HtmlTokenBufferFree(&run);
    return EmitEofToken(t);
}

/* ------------------------------------------------------------------ */
/* Tag states                                                          */
/* ------------------------------------------------------------------ */

static TokResult StTagOpen(HtmlTokenizer *t)
{
    char c = HtmlTokPeek(t);

    if (c == '!') {
        HtmlTokAdvance(t);
        t->state = RoboHtmlTokenizerStateMarkupDeclarationOpen;
        return TOK_CONTINUE;
    }
    if (c == '/') {
        HtmlTokAdvance(t);
        t->state = RoboHtmlTokenizerStateEndTagOpen;
        return TOK_CONTINUE;
    }
    if (c >= 'A' && c <= 'Z') {
        t->tagIsEnd = 0;
        HtmlTokenBufferClear(&t->text);
        (void)HtmlTokenBufferAppendByte(&t->text,
                                                 HtmlTokAsciiLower(c));
        HtmlTokAdvance(t);
        t->state = RoboHtmlTokenizerStateTagName;
        return TOK_CONTINUE;
    }
    if (c >= 'a' && c <= 'z') {
        t->tagIsEnd = 0;
        HtmlTokenBufferClear(&t->text);
        (void)HtmlTokenBufferAppendByte(&t->text, c);
        HtmlTokAdvance(t);
        t->state = RoboHtmlTokenizerStateTagName;
        return TOK_CONTINUE;
    }
    if (c == '?') {
        /* Bogus comment per the standard (processing-instruction legacy). */
        diag(t, RoboParseError, "'<?' treated as bogus comment");
        HtmlTokenBufferClear(&t->text);
        HtmlTokAdvance(t);
        t->state = RoboHtmlTokenizerStateBogusComment;
        return TOK_CONTINUE;
    }
    if (c == '\0') {
        diag(t, RoboParseError, "NUL after '<'");
        HtmlTokenBufferClear(&t->text);
        (void)HtmlTokenBufferAppendUtf8(&t->text, 0xFFFDu);
        HtmlTokAdvance(t);
        t->state = RoboHtmlTokenizerStateTagName;
        return TOK_CONTINUE;
    }
    /* Anything else (including EOF): '<' is literal text. */
    if (t->pos >= t->length) {
        diag(t, RoboParseError, "'<' at end of input");
    } else {
        diag(t, RoboParseError, "'<' before invalid tag character");
    }
    {
        HtmlTokenBuffer lt;
        HtmlTokenBufferInit(&lt);
        (void)HtmlTokenBufferAppendByte(&lt, '<');
        TokResult r = EmitCharToken(t, &lt);
        HtmlTokenBufferFree(&lt);
        t->state = RoboHtmlTokenizerStateData;
        return r;
    }
}

static TokResult StEndTagOpen(HtmlTokenizer *t)
{
    char c = HtmlTokPeek(t);

    if (c >= 'A' && c <= 'Z') {
        t->tagIsEnd = 1;
        HtmlTokenBufferClear(&t->text);
        (void)HtmlTokenBufferAppendByte(&t->text,
                                                 HtmlTokAsciiLower(c));
        HtmlTokAdvance(t);
        t->state = RoboHtmlTokenizerStateTagName;
        return TOK_CONTINUE;
    }
    if (c >= 'a' && c <= 'z') {
        t->tagIsEnd = 1;
        HtmlTokenBufferClear(&t->text);
        (void)HtmlTokenBufferAppendByte(&t->text, c);
        HtmlTokAdvance(t);
        t->state = RoboHtmlTokenizerStateTagName;
        return TOK_CONTINUE;
    }
    if (c == '>') {
        diag(t, RoboParseError, "empty end tag '</>'");
        HtmlTokAdvance(t);
        t->state = RoboHtmlTokenizerStateData;
        return TOK_CONTINUE;
    }
    /* EOF or junk → bogus comment starting from the current position. */
    diag(t, RoboParseError, "malformed end tag; treated as bogus comment");
    HtmlTokenBufferClear(&t->text);
    t->state = RoboHtmlTokenizerStateBogusComment;
    return TOK_CONTINUE;
}

static TokResult EmitTagToken(HtmlTokenizer *t)
{
    TokenBegin(t, t->tagIsEnd ? RoboHtmlTokenEndTag
                                 : RoboHtmlTokenStartTag);
    t->token.u.tag.name = t->text.data ? t->text.data : "";
    t->token.u.tag.attrCount = t->attrs.count;
    t->token.u.tag.selfClosing = t->tagSelfClosing;
    /* Attributes stay in token order; duplicates remain in the token
     * (first-occurrence-wins is a tree-builder rule, Phase 8.3). */
    if (t->attrs.count > 0u) {
        size_t i;
        /* Build the borrowed view array in the pool's own storage order. */
        HtmlTokenAttr *view = (HtmlTokenAttr *)Malloc(
            t->attrs.count * sizeof(*view));
        if (!view) {
            return TOK_ERROR;
        }
        for (i = 0u; i < t->attrs.count; i++) {
            view[i].name = t->attrs.entries[i].name.data
                               ? t->attrs.entries[i].name.data
                               : "";
            view[i].value = t->attrs.entries[i].value.data
                                ? t->attrs.entries[i].value.data
                                : "";
        }
        t->token.u.tag.attrs = view;
        t->attrView = view; /* freed at next token / destroy */
    } else {
        t->token.u.tag.attrs = NULL;
    }
    if (!t->tagIsEnd) {
        /* Remember the start-tag name; the controlled raw-content switch
         * hook seeds the expected end tag from it. */
        size_t i;
        HtmlTokenBufferClear(&t->lastStartTag);
        for (i = 0u; i < t->text.length; i++) {
            (void)HtmlTokenBufferAppendByte(&t->lastStartTag,
                                                     t->text.data[i]);
        }
    }
    TokenFinish(t);
    t->tagSelfClosing = 0;
    return TOK_EMIT;
}

static TokResult StTagName(HtmlTokenizer *t)
{
    char c = HtmlTokPeek(t);

    if (t->pos >= t->length) {
        diag(t, RoboParseError, "EOF in tag");
        return EmitEofToken(t);
    }
    if (IsWs(c)) {
        HtmlTokAdvance(t);
        t->state = RoboHtmlTokenizerStateBeforeAttributeName;
        return TOK_CONTINUE;
    }
    if (c == '/') {
        HtmlTokAdvance(t);
        t->state = RoboHtmlTokenizerStateSelfClosingStartTag;
        return TOK_CONTINUE;
    }
    if (c == '>') {
        HtmlTokAdvance(t);
        {
            TokResult r = EmitTagToken(t);
            t->state = RoboHtmlTokenizerStateData;
            return r;
        }
    }
    if (c == '\0') {
        (void)HtmlTokenBufferAppendUtf8(&t->text, 0xFFFDu);
        diag(t, RoboParseError, "NUL in tag name");
        HtmlTokAdvance(t);
        return TOK_CONTINUE;
    }
    (void)HtmlTokenBufferAppendByte(&t->text,
                                             HtmlTokAsciiLower(c));
    HtmlTokAdvance(t);
    return TOK_CONTINUE;
}

static TokResult StBeforeAttrName(HtmlTokenizer *t)
{
    char c;

    if (t->pos >= t->length) {
        diag(t, RoboParseError, "EOF in tag");
        return EmitEofToken(t);
    }
    c = HtmlTokPeek(t);
    if (IsWs(c)) {
        HtmlTokAdvance(t);
        return TOK_CONTINUE;
    }
    if (c == '/') {
        HtmlTokAdvance(t);
        t->state = RoboHtmlTokenizerStateSelfClosingStartTag;
        return TOK_CONTINUE;
    }
    if (c == '>') {
        HtmlTokAdvance(t);
        {
            TokResult r = EmitTagToken(t);
            t->state = RoboHtmlTokenizerStateData;
            return r;
        }
    }
    if (c == '=') {
        diag(t, RoboParseError, "'=' before attribute name");
        t->currentAttr = NULL;
        {
            HtmlTokenAttrEntry *e = NULL;
            if (HtmlTokenPoolNext(&t->attrs, &e) != RoboOk) {
                return TOK_ERROR;
            }
            t->currentAttr = e;
            (void)HtmlTokenBufferAppendByte(&e->name, '=');
        }
        HtmlTokAdvance(t);
        t->state = RoboHtmlTokenizerStateAttributeName;
        return TOK_CONTINUE;
    }
    if (c == '\0') {
        diag(t, RoboParseError, "NUL in attribute name");
        {
            HtmlTokenAttrEntry *e = NULL;
            if (HtmlTokenPoolNext(&t->attrs, &e) != RoboOk) {
                return TOK_ERROR;
            }
            t->currentAttr = e;
            (void)HtmlTokenBufferAppendUtf8(&e->name, 0xFFFDu);
        }
        HtmlTokAdvance(t);
        t->state = RoboHtmlTokenizerStateAttributeName;
        return TOK_CONTINUE;
    }
    if (c == '"' || c == '\'' || c == '<') {
        diag(t, RoboParseError, "unexpected quote/'<' in attribute name");
    }
    {
        HtmlTokenAttrEntry *e = NULL;
        if (HtmlTokenPoolNext(&t->attrs, &e) != RoboOk) {
            return TOK_ERROR;
        }
        t->currentAttr = e;
        (void)HtmlTokenBufferAppendByte(&e->name,
                                                 HtmlTokAsciiLower(c));
    }
    HtmlTokAdvance(t);
    t->state = RoboHtmlTokenizerStateAttributeName;
    return TOK_CONTINUE;
}

static TokResult StAttrName(HtmlTokenizer *t)
{
    char c;

    if (t->pos >= t->length) {
        diag(t, RoboParseError, "EOF in tag");
        return EmitEofToken(t);
    }
    c = HtmlTokPeek(t);
    if (IsWs(c) || c == '/' || c == '>') {
        t->state = RoboHtmlTokenizerStateAfterAttributeName;
        return TOK_CONTINUE; /* do not consume: after-state re-dispatches */
    }
    if (c == '=') {
        HtmlTokAdvance(t);
        t->state = RoboHtmlTokenizerStateBeforeAttributeValue;
        return TOK_CONTINUE;
    }
    if (c == '\0') {
        (void)HtmlTokenBufferAppendUtf8(&t->currentAttr->name,
                                                 0xFFFDu);
        diag(t, RoboParseError, "NUL in attribute name");
        HtmlTokAdvance(t);
        return TOK_CONTINUE;
    }
    if (c == '"' || c == '\'' || c == '<') {
        diag(t, RoboParseError, "unexpected quote/'<' in attribute name");
    }
    SetCurrentAttrNameByte(t, HtmlTokAsciiLower(c));
    HtmlTokAdvance(t);
    return TOK_CONTINUE;
}

static TokResult StAfterAttrName(HtmlTokenizer *t)
{
    char c;

    if (t->pos >= t->length) {
        diag(t, RoboParseError, "EOF in tag");
        return EmitEofToken(t);
    }
    c = HtmlTokPeek(t);
    if (IsWs(c)) {
        HtmlTokAdvance(t);
        return TOK_CONTINUE;
    }
    if (c == '/') {
        HtmlTokAdvance(t);
        t->state = RoboHtmlTokenizerStateSelfClosingStartTag;
        return TOK_CONTINUE;
    }
    if (c == '=') {
        HtmlTokAdvance(t);
        t->state = RoboHtmlTokenizerStateBeforeAttributeValue;
        return TOK_CONTINUE;
    }
    if (c == '>') {
        HtmlTokAdvance(t);
        {
            TokResult r = EmitTagToken(t);
            t->state = RoboHtmlTokenizerStateData;
            return r;
        }
    }
    /* New attribute without value separator. */
    t->currentAttr = NULL;
    {
        HtmlTokenAttrEntry *e = NULL;
        if (HtmlTokenPoolNext(&t->attrs, &e) != RoboOk) {
            return TOK_ERROR;
        }
        t->currentAttr = e;
        (void)HtmlTokenBufferAppendByte(&e->name,
                                                 HtmlTokAsciiLower(c));
    }
    HtmlTokAdvance(t);
    t->state = RoboHtmlTokenizerStateAttributeName;
    return TOK_CONTINUE;
}

static TokResult StBeforeAttrValue(HtmlTokenizer *t)
{
    char c;

    if (t->pos >= t->length) {
        diag(t, RoboParseError, "EOF in tag");
        return EmitEofToken(t);
    }
    c = HtmlTokPeek(t);
    if (IsWs(c)) {
        HtmlTokAdvance(t);
        return TOK_CONTINUE;
    }
    if (c == '"') {
        HtmlTokAdvance(t);
        t->state = RoboHtmlTokenizerStateAttributeValueDoubleQuoted;
        return TOK_CONTINUE;
    }
    if (c == '\'') {
        HtmlTokAdvance(t);
        t->state = RoboHtmlTokenizerStateAttributeValueSingleQuoted;
        return TOK_CONTINUE;
    }
    if (c == '>') {
        diag(t, RoboParseError, "attribute value missing");
        HtmlTokAdvance(t);
        {
            TokResult r = EmitTagToken(t);
            t->state = RoboHtmlTokenizerStateData;
            return r;
        }
    }
    t->state = RoboHtmlTokenizerStateAttributeValueUnquoted;
    return TOK_CONTINUE; /* unquoted handler consumes */
}

static TokResult StAttrValueQuoted(HtmlTokenizer *t, char quote)
{
    char c;

    if (t->pos >= t->length) {
        diag(t, RoboParseError, "EOF in quoted attribute value");
        return EmitEofToken(t);
    }
    c = HtmlTokPeek(t);
    if (c == quote) {
        HtmlTokAdvance(t);
        t->state = RoboHtmlTokenizerStateAfterAttributeValueQuoted;
        return TOK_CONTINUE;
    }
    if (c == '&') {
        ConsumeCharref(t, NULL, 1);
        return TOK_CONTINUE;
    }
    if (c == '\0') {
        SetCurrentAttrValueCp(t, 0xFFFDu);
        diag(t, RoboParseError, "NUL in attribute value");
        HtmlTokAdvance(t);
        return TOK_CONTINUE;
    }
    if (c == '\r') {
        SetCurrentAttrValueByte(t, '\n');
        HtmlTokAdvance(t);
        if (HtmlTokPeek(t) == '\n') {
            HtmlTokAdvance(t);
        }
        return TOK_CONTINUE;
    }
    SetCurrentAttrValueByte(t, c);
    HtmlTokAdvance(t);
    return TOK_CONTINUE;
}

static TokResult StAttrValueUnquoted(HtmlTokenizer *t)
{
    char c;

    if (t->pos >= t->length) {
        diag(t, RoboParseError, "EOF in tag");
        return EmitEofToken(t);
    }
    c = HtmlTokPeek(t);
    if (IsWs(c)) {
        HtmlTokAdvance(t);
        t->state = RoboHtmlTokenizerStateBeforeAttributeName;
        return TOK_CONTINUE;
    }
    if (c == '>') {
        HtmlTokAdvance(t);
        {
            TokResult r = EmitTagToken(t);
            t->state = RoboHtmlTokenizerStateData;
            return r;
        }
    }
    if (c == '/' && HtmlTokPeekAhead(t, 1) == '>') {
        /* "/>" after the value: self-closing syntax, not value content. */
        t->state = RoboHtmlTokenizerStateSelfClosingStartTag;
        return TOK_CONTINUE; /* self-closing handler consumes */
    }
    if (c == '&') {
        ConsumeCharref(t, NULL, 1);
        return TOK_CONTINUE;
    }
    if (c == '\0') {
        SetCurrentAttrValueCp(t, 0xFFFDu);
        diag(t, RoboParseError, "NUL in attribute value");
        HtmlTokAdvance(t);
        return TOK_CONTINUE;
    }
    if (c == '"' || c == '\'' || c == '<' || c == '=' || c == '`') {
        diag(t, RoboParseError, "unexpected character in unquoted value");
    }
    SetCurrentAttrValueByte(t, c);
    HtmlTokAdvance(t);
    return TOK_CONTINUE;
}

static TokResult StAfterAttrValueQuoted(HtmlTokenizer *t)
{
    char c;

    if (t->pos >= t->length) {
        diag(t, RoboParseError, "EOF in tag");
        return EmitEofToken(t);
    }
    c = HtmlTokPeek(t);
    if (IsWs(c)) {
        HtmlTokAdvance(t);
        t->state = RoboHtmlTokenizerStateBeforeAttributeName;
        return TOK_CONTINUE;
    }
    if (c == '/') {
        HtmlTokAdvance(t);
        t->state = RoboHtmlTokenizerStateSelfClosingStartTag;
        return TOK_CONTINUE;
    }
    if (c == '>') {
        HtmlTokAdvance(t);
        {
            TokResult r = EmitTagToken(t);
            t->state = RoboHtmlTokenizerStateData;
            return r;
        }
    }
    diag(t, RoboParseError, "missing whitespace between attributes");
    t->state = RoboHtmlTokenizerStateBeforeAttributeName;
    return TOK_CONTINUE; /* do not consume */
}

static TokResult StSelfClosingStartTag(HtmlTokenizer *t)
{
    char c;

    if (t->pos >= t->length) {
        diag(t, RoboParseError, "EOF in tag");
        return EmitEofToken(t);
    }
    c = HtmlTokPeek(t);
    if (c == '>') {
        HtmlTokAdvance(t);
        t->tagSelfClosing = 1;
        {
            TokResult r = EmitTagToken(t);
            t->state = RoboHtmlTokenizerStateData;
            return r;
        }
    }
    diag(t, RoboParseError, "'/' not followed by '>'");
    t->state = RoboHtmlTokenizerStateBeforeAttributeName;
    return TOK_CONTINUE; /* do not consume */
}

/* ------------------------------------------------------------------ */
/* Markup declarations: comments + doctype                             */
/* ------------------------------------------------------------------ */

static TokResult StMarkupDeclarationOpen(HtmlTokenizer *t)
{
    if (HtmlTokWordAt(t, "doctype")) {
        size_t i;
        for (i = 0u; i < 7u; i++) {
            HtmlTokAdvance(t);
        }
        HtmlTokenBufferClear(&t->text);
        HtmlTokenBufferClear(&t->text2);
        HtmlTokenBufferClear(&t->text3);
        t->doctypeForceQuirks = 0;
        t->doctypeHasPublic = 0;
        t->doctypeHasSystem = 0;
        t->state = RoboHtmlTokenizerStateBeforeDoctypeName;
        return TOK_CONTINUE;
    }
    if (HtmlTokPeekAhead(t, 0) == '-' &&
        HtmlTokPeekAhead(t, 1) == '-') {
        HtmlTokAdvance(t);
        HtmlTokAdvance(t);
        HtmlTokenBufferClear(&t->text);
        t->state = RoboHtmlTokenizerStateCommentStart;
        return TOK_CONTINUE;
    }
    if (t->pos + 7u <= t->length &&
        memcmp(t->bytes + t->pos, "[CDATA[", 7u) == 0) {
        size_t i;
        /* In HTML content CDATA is a bogus comment with the payload text. */
        diag(t, RoboParseError,
             "'<![CDATA[' in HTML content: bogus comment");
        for (i = 0u; i < 7u; i++) {
            HtmlTokAdvance(t);
        }
        HtmlTokenBufferClear(&t->text);
        t->state = RoboHtmlTokenizerStateBogusComment;
        return TOK_CONTINUE;
    }
    diag(t, RoboParseError, "malformed markup declaration");
    HtmlTokenBufferClear(&t->text);
    t->state = RoboHtmlTokenizerStateBogusComment;
    return TOK_CONTINUE;
}

static TokResult EmitCommentToken(HtmlTokenizer *t)
{
    TokenBegin(t, RoboHtmlTokenComment);
    t->token.u.comment.data = t->text.data ? t->text.data : "";
    t->token.u.comment.length = t->text.length;
    TokenFinish(t);
    return TOK_EMIT;
}

static TokResult StCommentStart(HtmlTokenizer *t)
{
    char c;

    if (t->pos >= t->length) {
        diag(t, RoboParseError, "EOF in comment");
        {
            TokResult r = EmitCommentToken(t);
            t->state = RoboHtmlTokenizerStateData;
            return r;
        }
    }
    c = HtmlTokPeek(t);
    if (c == '-') {
        HtmlTokAdvance(t);
        t->state = RoboHtmlTokenizerStateCommentStartDash;
        return TOK_CONTINUE;
    }
    if (c == '>') {
        diag(t, RoboParseError, "abrupt closing of empty comment");
        HtmlTokAdvance(t);
        {
            TokResult r = EmitCommentToken(t);
            t->state = RoboHtmlTokenizerStateData;
            return r;
        }
    }
    t->state = RoboHtmlTokenizerStateComment;
    return TOK_CONTINUE;
}

static TokResult StCommentStartDash(HtmlTokenizer *t)
{
    char c;

    if (t->pos >= t->length) {
        diag(t, RoboParseError, "EOF in comment");
        {
            TokResult r = EmitCommentToken(t);
            t->state = RoboHtmlTokenizerStateData;
            return r;
        }
    }
    c = HtmlTokPeek(t);
    if (c == '-') {
        HtmlTokAdvance(t);
        t->state = RoboHtmlTokenizerStateCommentEnd;
        return TOK_CONTINUE;
    }
    if (c == '>') {
        diag(t, RoboParseError, "abrupt closing of empty comment");
        HtmlTokAdvance(t);
        {
            TokResult r = EmitCommentToken(t);
            t->state = RoboHtmlTokenizerStateData;
            return r;
        }
    }
    (void)HtmlTokenBufferAppendByte(&t->text, '-');
    t->state = RoboHtmlTokenizerStateComment;
    return TOK_CONTINUE;
}

static TokResult StCommentBody(HtmlTokenizer *t)
{
    char c;

    if (t->pos >= t->length) {
        diag(t, RoboParseError, "EOF in comment");
        {
            TokResult r = EmitCommentToken(t);
            t->state = RoboHtmlTokenizerStateData;
            return r;
        }
    }
    c = HtmlTokPeek(t);
    if (c == '<') {
        (void)HtmlTokenBufferAppendByte(&t->text, '<');
        HtmlTokAdvance(t);
        t->state = RoboHtmlTokenizerStateCommentLessThan;
        return TOK_CONTINUE;
    }
    if (c == '-') {
        HtmlTokAdvance(t);
        t->state = RoboHtmlTokenizerStateCommentEndDash;
        return TOK_CONTINUE;
    }
    if (c == '\0') {
        (void)HtmlTokenBufferAppendUtf8(&t->text, 0xFFFDu);
        diag(t, RoboParseError, "NUL in comment");
        HtmlTokAdvance(t);
        return TOK_CONTINUE;
    }
    (void)HtmlTokenBufferAppendByte(&t->text, c);
    HtmlTokAdvance(t);
    return TOK_CONTINUE;
}

static TokResult StCommentLessThan(HtmlTokenizer *t)
{
    char c = HtmlTokPeek(t);

    if (c == '!' || c == '/' || c == '<') {
        (void)HtmlTokenBufferAppendByte(&t->text, c);
        HtmlTokAdvance(t);
        if (c == '!') {
            t->state = RoboHtmlTokenizerStateCommentLessThanBang;
        }
        return TOK_CONTINUE;
    }
    t->state = RoboHtmlTokenizerStateComment;
    return TOK_CONTINUE;
}

static TokResult StCommentLessThanBang(HtmlTokenizer *t)
{
    if (HtmlTokPeek(t) == '-' && HtmlTokPeekAhead(t, 1) == '-') {
        HtmlTokAdvance(t);
        HtmlTokAdvance(t);
        t->state = RoboHtmlTokenizerStateCommentLessThanBangDashes;
        return TOK_CONTINUE;
    }
    t->state = RoboHtmlTokenizerStateComment;
    return TOK_CONTINUE;
}

static TokResult StCommentLessThanBangDashes(HtmlTokenizer *t)
{
    char c = HtmlTokPeek(t);

    if (c == '-' || c == '>') {
        t->state = RoboHtmlTokenizerStateCommentEnd;
        return TOK_CONTINUE;
    }
    t->state = RoboHtmlTokenizerStateComment;
    return TOK_CONTINUE;
}

static TokResult StCommentEndDash(HtmlTokenizer *t)
{
    char c;

    if (t->pos >= t->length) {
        diag(t, RoboParseError, "EOF in comment");
        {
            TokResult r = EmitCommentToken(t);
            t->state = RoboHtmlTokenizerStateData;
            return r;
        }
    }
    c = HtmlTokPeek(t);
    if (c == '-') {
        HtmlTokAdvance(t);
        t->state = RoboHtmlTokenizerStateCommentEnd;
        return TOK_CONTINUE;
    }
    (void)HtmlTokenBufferAppendByte(&t->text, '-');
    t->state = RoboHtmlTokenizerStateComment;
    return TOK_CONTINUE;
}

static TokResult StCommentEnd(HtmlTokenizer *t)
{
    char c;

    if (t->pos >= t->length) {
        diag(t, RoboParseError, "EOF in comment");
        {
            TokResult r = EmitCommentToken(t);
            t->state = RoboHtmlTokenizerStateData;
            return r;
        }
    }
    c = HtmlTokPeek(t);
    if (c == '>') {
        HtmlTokAdvance(t);
        {
            TokResult r = EmitCommentToken(t);
            t->state = RoboHtmlTokenizerStateData;
            return r;
        }
    }
    if (c == '!') {
        HtmlTokAdvance(t);
        t->state = RoboHtmlTokenizerStateCommentEndBang;
        return TOK_CONTINUE;
    }
    if (c == '-') {
        (void)HtmlTokenBufferAppendByte(&t->text, '-');
        HtmlTokAdvance(t);
        return TOK_CONTINUE;
    }
    (void)HtmlTokenBufferAppendByte(&t->text, '-');
    (void)HtmlTokenBufferAppendByte(&t->text, '-');
    t->state = RoboHtmlTokenizerStateComment;
    return TOK_CONTINUE;
}

static TokResult StCommentEndBang(HtmlTokenizer *t)
{
    char c;

    if (t->pos >= t->length) {
        diag(t, RoboParseError, "EOF in comment");
        {
            TokResult r = EmitCommentToken(t);
            t->state = RoboHtmlTokenizerStateData;
            return r;
        }
    }
    c = HtmlTokPeek(t);
    if (c == '-') {
        (void)HtmlTokenBufferAppendByte(&t->text, '-');
        (void)HtmlTokenBufferAppendByte(&t->text, '-');
        (void)HtmlTokenBufferAppendByte(&t->text, '!');
        HtmlTokAdvance(t);
        t->state = RoboHtmlTokenizerStateCommentEndDash;
        return TOK_CONTINUE;
    }
    if (c == '>') {
        diag(t, RoboParseError, "incorrectly closed comment");
        HtmlTokAdvance(t);
        {
            TokResult r = EmitCommentToken(t);
            t->state = RoboHtmlTokenizerStateData;
            return r;
        }
    }
    (void)HtmlTokenBufferAppendByte(&t->text, '-');
    (void)HtmlTokenBufferAppendByte(&t->text, '-');
    (void)HtmlTokenBufferAppendByte(&t->text, '!');
    t->state = RoboHtmlTokenizerStateComment;
    return TOK_CONTINUE;
}

static TokResult StBogusComment(HtmlTokenizer *t)
{
    char c;

    if (t->pos >= t->length) {
        {
            TokResult r = EmitCommentToken(t);
            t->state = RoboHtmlTokenizerStateData;
            return r;
        }
    }
    c = HtmlTokPeek(t);
    if (c == '>') {
        HtmlTokAdvance(t);
        {
            TokResult r = EmitCommentToken(t);
            t->state = RoboHtmlTokenizerStateData;
            return r;
        }
    }
    if (c == '\0') {
        (void)HtmlTokenBufferAppendUtf8(&t->text, 0xFFFDu);
        HtmlTokAdvance(t);
        return TOK_CONTINUE;
    }
    (void)HtmlTokenBufferAppendByte(&t->text, c);
    HtmlTokAdvance(t);
    return TOK_CONTINUE;
}

/* ------------------------------------------------------------------ */
/* Doctype states                                                      */
/* ------------------------------------------------------------------ */

static TokResult EmitDoctypeToken(HtmlTokenizer *t)
{
    TokenBegin(t, RoboHtmlTokenDoctype);
    t->token.u.doctype.name = t->text.data ? t->text.data : "";
    t->token.u.doctype.publicId =
        t->doctypeHasPublic ? (t->text2.data ? t->text2.data : "") : NULL;
    t->token.u.doctype.systemId =
        t->doctypeHasSystem ? (t->text3.data ? t->text3.data : "") : NULL;
    t->token.u.doctype.forceQuirks = t->doctypeForceQuirks;
    TokenFinish(t);
    return TOK_EMIT;
}

static TokResult StBeforeDoctypeName(HtmlTokenizer *t)
{
    char c;

    if (t->pos >= t->length) {
        diag(t, RoboParseError, "EOF in DOCTYPE");
        t->doctypeForceQuirks = 1;
        {
            TokResult r = EmitDoctypeToken(t);
            t->state = RoboHtmlTokenizerStateData;
            return r;
        }
    }
    c = HtmlTokPeek(t);
    if (IsWs(c)) {
        HtmlTokAdvance(t);
        return TOK_CONTINUE;
    }
    if (c == '>') {
        diag(t, RoboParseError, "DOCTYPE without name");
        HtmlTokAdvance(t);
        t->doctypeForceQuirks = 1;
        {
            TokResult r = EmitDoctypeToken(t);
            t->state = RoboHtmlTokenizerStateData;
            return r;
        }
    }
    t->state = RoboHtmlTokenizerStateDoctypeName;
    return TOK_CONTINUE; /* name handler consumes */
}

static TokResult StDoctypeName(HtmlTokenizer *t)
{
    char c;

    if (t->pos >= t->length) {
        diag(t, RoboParseError, "EOF in DOCTYPE");
        t->doctypeForceQuirks = 1;
        {
            TokResult r = EmitDoctypeToken(t);
            t->state = RoboHtmlTokenizerStateData;
            return r;
        }
    }
    c = HtmlTokPeek(t);
    if (IsWs(c)) {
        HtmlTokAdvance(t);
        t->state = RoboHtmlTokenizerStateAfterDoctypeName;
        return TOK_CONTINUE;
    }
    if (c == '>') {
        HtmlTokAdvance(t);
        {
            TokResult r = EmitDoctypeToken(t);
            t->state = RoboHtmlTokenizerStateData;
            return r;
        }
    }
    if (c == '\0') {
        (void)HtmlTokenBufferAppendUtf8(&t->text, 0xFFFDu);
        diag(t, RoboParseError, "NUL in DOCTYPE name");
        HtmlTokAdvance(t);
        return TOK_CONTINUE;
    }
    (void)HtmlTokenBufferAppendByte(&t->text,
                                             HtmlTokAsciiLower(c));
    HtmlTokAdvance(t);
    return TOK_CONTINUE;
}

static TokResult StDoctypeAfterName(HtmlTokenizer *t)
{
    char c;

    if (t->pos >= t->length) {
        diag(t, RoboParseError, "EOF in DOCTYPE");
        t->doctypeForceQuirks = 1;
        {
            TokResult r = EmitDoctypeToken(t);
            t->state = RoboHtmlTokenizerStateData;
            return r;
        }
    }
    c = HtmlTokPeek(t);
    if (IsWs(c)) {
        HtmlTokAdvance(t);
        return TOK_CONTINUE;
    }
    if (c == '>') {
        HtmlTokAdvance(t);
        {
            TokResult r = EmitDoctypeToken(t);
            t->state = RoboHtmlTokenizerStateData;
            return r;
        }
    }
    if (HtmlTokWordAt(t, "public")) {
        size_t i;
        for (i = 0u; i < 6u; i++) {
            HtmlTokAdvance(t);
        }
        t->state = RoboHtmlTokenizerStateAfterDoctypePublicKeyword;
        return TOK_CONTINUE;
    }
    if (HtmlTokWordAt(t, "system")) {
        size_t i;
        for (i = 0u; i < 6u; i++) {
            HtmlTokAdvance(t);
        }
        t->state = RoboHtmlTokenizerStateAfterDoctypeSystemKeyword;
        return TOK_CONTINUE;
    }
    diag(t, RoboParseError, "invalid DOCTYPE content; bogus doctype");
    t->doctypeForceQuirks = 1;
    t->state = RoboHtmlTokenizerStateBogusDoctype;
    return TOK_CONTINUE;
}

static TokResult StDoctypePublicKeyword(HtmlTokenizer *t)
{
    char c;

    if (t->pos >= t->length) {
        diag(t, RoboParseError, "EOF in DOCTYPE");
        t->doctypeForceQuirks = 1;
        {
            TokResult r = EmitDoctypeToken(t);
            t->state = RoboHtmlTokenizerStateData;
            return r;
        }
    }
    c = HtmlTokPeek(t);
    if (IsWs(c)) {
        HtmlTokAdvance(t);
        t->state = RoboHtmlTokenizerStateBeforeDoctypePublicIdentifier;
        return TOK_CONTINUE;
    }
    if (c == '"') {
        diag(t, RoboParseError, "unquoted DOCTYPE public identifier");
        HtmlTokAdvance(t);
        t->doctypeHasPublic = 1;
        HtmlTokenBufferClear(&t->text2);
        t->state = RoboHtmlTokenizerStateDoctypePublicIdentifierDoubleQuoted;
        return TOK_CONTINUE;
    }
    if (c == '\'') {
        diag(t, RoboParseError, "unquoted DOCTYPE public identifier");
        HtmlTokAdvance(t);
        t->doctypeHasPublic = 1;
        HtmlTokenBufferClear(&t->text2);
        t->state = RoboHtmlTokenizerStateDoctypePublicIdentifierSingleQuoted;
        return TOK_CONTINUE;
    }
    diag(t, RoboParseError, "missing quote after PUBLIC; bogus doctype");
    t->doctypeForceQuirks = 1;
    t->state = RoboHtmlTokenizerStateBogusDoctype;
    return TOK_CONTINUE;
}

/**
 * Before-DOCTYPE-public-identifier / before-DOCTYPE-system-identifier.
 *
 * These states exist to consume the opening quote; the quoted states that
 * follow copy only the identifier payload. Keeping them distinct is what
 * makes `doctypeHasPublic` / `doctypeHasSystem` true (and the buffers
 * empty at the quote) instead of swallowing the quote into the value.
 */
static TokResult StBeforeDoctypePublicId(HtmlTokenizer *t)
{
    char c;

    if (t->pos >= t->length) {
        diag(t, RoboParseError, "EOF in DOCTYPE");
        t->doctypeForceQuirks = 1;
        {
            TokResult r = EmitDoctypeToken(t);
            t->state = RoboHtmlTokenizerStateData;
            return r;
        }
    }
    c = HtmlTokPeek(t);
    if (IsWs(c)) {
        HtmlTokAdvance(t);
        return TOK_CONTINUE;
    }
    if (c == '"' || c == '\'') {
        HtmlTokAdvance(t);
        t->doctypeHasPublic = 1;
        t->doctypeForceQuirks = 0;
        HtmlTokenBufferClear(&t->text2);
        t->state = (c == '"')
                       ? RoboHtmlTokenizerStateDoctypePublicIdentifierDoubleQuoted
                       : RoboHtmlTokenizerStateDoctypePublicIdentifierSingleQuoted;
        return TOK_CONTINUE;
    }
    if (c == '>') {
        diag(t, RoboParseError, "'>' before DOCTYPE public identifier");
        t->doctypeForceQuirks = 1;
        HtmlTokAdvance(t);
        {
            TokResult r = EmitDoctypeToken(t);
            t->state = RoboHtmlTokenizerStateData;
            return r;
        }
    }
    diag(t, RoboParseError, "invalid DOCTYPE public identifier; bogus doctype");
    t->doctypeForceQuirks = 1;
    t->state = RoboHtmlTokenizerStateBogusDoctype;
    return TOK_CONTINUE;
}

static TokResult StBeforeDoctypeSystemId(HtmlTokenizer *t)
{
    char c;

    if (t->pos >= t->length) {
        diag(t, RoboParseError, "EOF in DOCTYPE");
        t->doctypeForceQuirks = 1;
        {
            TokResult r = EmitDoctypeToken(t);
            t->state = RoboHtmlTokenizerStateData;
            return r;
        }
    }
    c = HtmlTokPeek(t);
    if (IsWs(c)) {
        HtmlTokAdvance(t);
        return TOK_CONTINUE;
    }
    if (c == '"' || c == '\'') {
        HtmlTokAdvance(t);
        t->doctypeHasSystem = 1;
        t->doctypeForceQuirks = 0;
        HtmlTokenBufferClear(&t->text3);
        t->state = (c == '"')
                       ? RoboHtmlTokenizerStateDoctypeSystemIdentifierDoubleQuoted
                       : RoboHtmlTokenizerStateDoctypeSystemIdentifierSingleQuoted;
        return TOK_CONTINUE;
    }
    if (c == '>') {
        diag(t, RoboParseError, "'>' before DOCTYPE system identifier");
        t->doctypeForceQuirks = 1;
        HtmlTokAdvance(t);
        {
            TokResult r = EmitDoctypeToken(t);
            t->state = RoboHtmlTokenizerStateData;
            return r;
        }
    }
    diag(t, RoboParseError, "invalid DOCTYPE system identifier; bogus doctype");
    t->state = RoboHtmlTokenizerStateBogusDoctype;
    return TOK_CONTINUE;
}

static TokResult StDoctypePublicIdQuoted(HtmlTokenizer *t,
                                                char quote)
{
    char c;

    if (t->pos >= t->length) {
        diag(t, RoboParseError, "EOF in DOCTYPE public identifier");
        t->doctypeForceQuirks = 1;
        {
            TokResult r = EmitDoctypeToken(t);
            t->state = RoboHtmlTokenizerStateData;
            return r;
        }
    }
    c = HtmlTokPeek(t);
    if (c == quote) {
        HtmlTokAdvance(t);
        t->state =
            RoboHtmlTokenizerStateBetweenDoctypePublicAndSystemIdentifiers;
        return TOK_CONTINUE;
    }
    if (c == '\0') {
        (void)HtmlTokenBufferAppendUtf8(&t->text2, 0xFFFDu);
        diag(t, RoboParseError, "NUL in DOCTYPE public identifier");
        HtmlTokAdvance(t);
        return TOK_CONTINUE;
    }
    if (c == '>') {
        diag(t, RoboParseError, "'>' in DOCTYPE public identifier");
        t->doctypeForceQuirks = 1;
        HtmlTokAdvance(t);
        {
            TokResult r = EmitDoctypeToken(t);
            t->state = RoboHtmlTokenizerStateData;
            return r;
        }
    }
    (void)HtmlTokenBufferAppendByte(&t->text2, c);
    HtmlTokAdvance(t);
    return TOK_CONTINUE;
}

static TokResult StBetweenPublicSystem(HtmlTokenizer *t)
{
    char c;

    if (t->pos >= t->length) {
        diag(t, RoboParseError, "EOF in DOCTYPE");
        t->doctypeForceQuirks = 1;
        {
            TokResult r = EmitDoctypeToken(t);
            t->state = RoboHtmlTokenizerStateData;
            return r;
        }
    }
    c = HtmlTokPeek(t);
    if (IsWs(c)) {
        HtmlTokAdvance(t);
        t->state = RoboHtmlTokenizerStateBeforeDoctypeSystemIdentifier;
        return TOK_CONTINUE;
    }
    if (c == '>') {
        HtmlTokAdvance(t);
        {
            TokResult r = EmitDoctypeToken(t);
            t->state = RoboHtmlTokenizerStateData;
            return r;
        }
    }
    if (c == '"') {
        HtmlTokAdvance(t);
        t->doctypeHasSystem = 1;
        t->state = RoboHtmlTokenizerStateDoctypeSystemIdentifierDoubleQuoted;
        return TOK_CONTINUE;
    }
    if (c == '\'') {
        HtmlTokAdvance(t);
        t->doctypeHasSystem = 1;
        t->state = RoboHtmlTokenizerStateDoctypeSystemIdentifierSingleQuoted;
        return TOK_CONTINUE;
    }
    diag(t, RoboParseError, "missing quote after DOCTYPE public id");
    t->doctypeForceQuirks = 1;
    t->state = RoboHtmlTokenizerStateBogusDoctype;
    return TOK_CONTINUE;
}

static TokResult StDoctypeSystemKeyword(HtmlTokenizer *t)
{
    char c;

    if (t->pos >= t->length) {
        diag(t, RoboParseError, "EOF in DOCTYPE");
        t->doctypeForceQuirks = 1;
        {
            TokResult r = EmitDoctypeToken(t);
            t->state = RoboHtmlTokenizerStateData;
            return r;
        }
    }
    c = HtmlTokPeek(t);
    if (IsWs(c)) {
        HtmlTokAdvance(t);
        t->state = RoboHtmlTokenizerStateBeforeDoctypeSystemIdentifier;
        return TOK_CONTINUE;
    }
    if (c == '"') {
        HtmlTokAdvance(t);
        t->doctypeHasSystem = 1;
        t->state = RoboHtmlTokenizerStateDoctypeSystemIdentifierDoubleQuoted;
        return TOK_CONTINUE;
    }
    if (c == '\'') {
        HtmlTokAdvance(t);
        t->doctypeHasSystem = 1;
        t->state = RoboHtmlTokenizerStateDoctypeSystemIdentifierSingleQuoted;
        return TOK_CONTINUE;
    }
    diag(t, RoboParseError, "missing quote after SYSTEM; bogus doctype");
    t->doctypeForceQuirks = 1;
    t->state = RoboHtmlTokenizerStateBogusDoctype;
    return TOK_CONTINUE;
}

static TokResult StDoctypeSystemIdQuoted(HtmlTokenizer *t,
                                                char quote)
{
    char c;

    if (t->pos >= t->length) {
        diag(t, RoboParseError, "EOF in DOCTYPE system identifier");
        t->doctypeForceQuirks = 1;
        {
            TokResult r = EmitDoctypeToken(t);
            t->state = RoboHtmlTokenizerStateData;
            return r;
        }
    }
    c = HtmlTokPeek(t);
    if (c == quote) {
        HtmlTokAdvance(t);
        t->state = RoboHtmlTokenizerStateAfterDoctypeSystemIdentifier;
        return TOK_CONTINUE;
    }
    if (c == '\0') {
        (void)HtmlTokenBufferAppendUtf8(&t->text3, 0xFFFDu);
        diag(t, RoboParseError, "NUL in DOCTYPE system identifier");
        HtmlTokAdvance(t);
        return TOK_CONTINUE;
    }
    if (c == '>') {
        diag(t, RoboParseError, "'>' in DOCTYPE system identifier");
        t->doctypeForceQuirks = 1;
        HtmlTokAdvance(t);
        {
            TokResult r = EmitDoctypeToken(t);
            t->state = RoboHtmlTokenizerStateData;
            return r;
        }
    }
    (void)HtmlTokenBufferAppendByte(&t->text3, c);
    HtmlTokAdvance(t);
    return TOK_CONTINUE;
}

static TokResult StAfterDoctypeSystemId(HtmlTokenizer *t)
{
    char c;

    if (t->pos >= t->length) {
        diag(t, RoboParseError, "EOF in DOCTYPE");
        t->doctypeForceQuirks = 1;
        {
            TokResult r = EmitDoctypeToken(t);
            t->state = RoboHtmlTokenizerStateData;
            return r;
        }
    }
    c = HtmlTokPeek(t);
    if (IsWs(c)) {
        HtmlTokAdvance(t);
        return TOK_CONTINUE;
    }
    if (c == '>') {
        HtmlTokAdvance(t);
        {
            TokResult r = EmitDoctypeToken(t);
            t->state = RoboHtmlTokenizerStateData;
            return r;
        }
    }
    diag(t, RoboParseError, "unexpected content after DOCTYPE system id");
    t->state = RoboHtmlTokenizerStateBogusDoctype;
    return TOK_CONTINUE;
}

static TokResult StBogusDoctype(HtmlTokenizer *t)
{
    char c;

    if (t->pos >= t->length) {
        {
            TokResult r = EmitDoctypeToken(t);
            t->state = RoboHtmlTokenizerStateData;
            return r;
        }
    }
    c = HtmlTokPeek(t);
    if (c == '>') {
        HtmlTokAdvance(t);
        {
            TokResult r = EmitDoctypeToken(t);
            t->state = RoboHtmlTokenizerStateData;
            return r;
        }
    }
    if (c == '\0') {
        diag(t, RoboParseError, "NUL in bogus DOCTYPE");
    }
    HtmlTokAdvance(t);
    return TOK_CONTINUE;
}

/* ------------------------------------------------------------------ */
/* Raw content families (RAWTEXT / RCDATA / script data)               */
/* ------------------------------------------------------------------ */

/**
 * Shared raw-content engine (RAWTEXT / RCDATA / script data / plaintext).
 *
 * Accumulates text until the matching end tag appears, then hands the
 * `</...` construct back to the normal tag machinery by switching to the
 * DATA state (end-tag attributes keep parsing uniformly).
 *
 * The expected end-tag name comes from rawEndName (seeded by
 * switch_state from the last emitted start tag). When no name is known:
 *   - a leading `<name>` start tag is consumed to learn the name;
 *   - otherwise the region closes at the LAST `</name`-shaped construct
 *     terminated by ws, '/', or '>' — earlier look-alikes stay literal
 *     text (context-less switch contract; `</x` at EOF never closes).
 * PLAINTEXT never matches end tags. RCDATA decodes character references;
 * the other raw families keep bytes literal.
 */
static TokResult StRawtextLike(HtmlTokenizer *t)
{
    HtmlTokenizerState base = t->state;
    int isRcdata = (base == RoboHtmlTokenizerStateRcdata);
    int isPlaintext = (base == RoboHtmlTokenizerStatePlaintext);
    const char *endName = t->rawEndName.data ? t->rawEndName.data : "";
    size_t nameLen = t->rawEndName.length;
    HtmlTokenBuffer run;
    size_t candSrc = 0u;
    size_t candRunMark = 0u;
    int haveCandidate = 0;

    HtmlTokenBufferInit(&run);
    SnapshotPos(t);

    /* Context-less raw region: learn the end name from a leading tag. */
    if (!isPlaintext && nameLen == 0u && t->pos + 1u < t->length &&
        t->bytes[t->pos] == '<' && IsAsciiLetter(t->bytes[t->pos + 1u])) {
        size_t p = t->pos + 2u;

        while (p < t->length && t->bytes[p] != '>') {
            p++;
        }
        if (p < t->length) { /* '>' found: consume the leading start tag */
            size_t i;

            /* The name starts at pos + 1 (the first byte after '<');
             * pos + 2 would drop it and leave the region context-less. */
            for (i = t->pos + 1u; i < p; i++) {
                char ch = t->bytes[i];

                if (IsWs(ch)) {
                    break; /* the name ends at the first separator */
                }
                (void)HtmlTokenBufferAppendByte(
                    &t->rawEndName, HtmlTokAsciiLower(ch));
            }
            while (t->pos <= p) {
                HtmlTokAdvance(t);
            }
            endName = t->rawEndName.data ? t->rawEndName.data : "";
            nameLen = t->rawEndName.length;
        }
    }

    while (t->pos < t->length) {
        char c = HtmlTokPeek(t);

        if (c == '<' && !isPlaintext) {
            /* Candidate end tag: "</" + letters + (ws, '/', or '>'). */
            size_t afterLt = t->pos + 1u;

            if (afterLt < t->length && t->bytes[afterLt] == '/') {
                size_t p = afterLt + 1u;
                size_t nlen = 0u;

                while (p + nlen < t->length &&
                       IsAsciiLetter(t->bytes[p + nlen])) {
                    nlen++;
                }
                if (nlen > 0u && p + nlen < t->length) {
                    char term = t->bytes[p + nlen];

                    if (term == '>' || term == '/' || IsWs(term)) {
                        if (nameLen > 0u) {
                            /* Known context: exact (case-insensitive) name. */
                            if (nlen == nameLen &&
                                AsciiCasecmpBytes(t->bytes + p,
                                                         endName,
                                                         nameLen)) {
                                break; /* close here; tag machinery follows */
                            }
                        } else {
                            /* No known context: close at the LAST
                             * candidate; remember this one and keep the
                             * bytes as literal text for now. */
                            haveCandidate = 1;
                            candSrc = t->pos;
                            candRunMark = run.length;
                        }
                    }
                }
            }
        }
        if (c == '&') {
            if (isRcdata) {
                ConsumeCharref(t, &run, 0);
            } else {
                (void)HtmlTokenBufferAppendByte(&run, '&');
                HtmlTokAdvance(t);
            }
            continue;
        }
        if (c == '\r') {
            (void)HtmlTokenBufferAppendByte(&run, '\n');
            HtmlTokAdvance(t);
            if (HtmlTokPeek(t) == '\n') {
                HtmlTokAdvance(t);
            }
            continue;
        }
        if (c == '\0') {
            (void)HtmlTokenBufferAppendUtf8(&run, 0xFFFDu);
            HtmlTokAdvance(t);
            continue;
        }
        {
            uint32_t cp = 0u;
            size_t next = 0u;
            UnicodeStatus st = UnicodeUtf8Decode(
                t->bytes, t->length, t->pos, RoboUnicodeReplace, &cp,
                &next);
            if (st == RoboUnicodeOk || st == RoboUnicodeInvalid ||
                st == RoboUnicodeTruncated) {
                (void)HtmlTokenBufferAppendUtf8(&run, cp);
                while (t->pos < next) {
                    HtmlTokAdvance(t);
                }
                continue;
            }
        }
        (void)HtmlTokenBufferAppendByte(&run, c);
        HtmlTokAdvance(t);
    }

    /* End of region without a known-context match. */
    if (haveCandidate) {
        /* No known context: close at the last candidate seen. */
        t->pos = candSrc;
        SnapshotPos(t);
        run.length = candRunMark;
        if (run.data) {
            run.data[run.length] = '\0';
        }
    }
    if (run.length > 0u) {
        TokResult r = EmitCharToken(t, &run); /* steals the run */

        t->state = RoboHtmlTokenizerStateData;
        return r;
    }
    HtmlTokenBufferFree(&run);
    if (t->pos < t->length) {
        /* Positioned on the matching end tag: let the normal tag
         * machinery parse and emit it. */
        t->state = RoboHtmlTokenizerStateData;
        return TOK_CONTINUE;
    }
    return EmitEofToken(t);
}

/* ------------------------------------------------------------------ */
/* Dispatch + public API                                               */
/* ------------------------------------------------------------------ */

static TokResult step(HtmlTokenizer *t)
{
    switch (t->state) {
    case RoboHtmlTokenizerStateData:
        return StData(t);
    case RoboHtmlTokenizerStateTagOpen:
        return StTagOpen(t);
    case RoboHtmlTokenizerStateEndTagOpen:
        return StEndTagOpen(t);
    case RoboHtmlTokenizerStateTagName:
        return StTagName(t);
    case RoboHtmlTokenizerStateBeforeAttributeName:
        return StBeforeAttrName(t);
    case RoboHtmlTokenizerStateAttributeName:
        return StAttrName(t);
    case RoboHtmlTokenizerStateAfterAttributeName:
        return StAfterAttrName(t);
    case RoboHtmlTokenizerStateBeforeAttributeValue:
        return StBeforeAttrValue(t);
    case RoboHtmlTokenizerStateAttributeValueDoubleQuoted:
        return StAttrValueQuoted(t, '"');
    case RoboHtmlTokenizerStateAttributeValueSingleQuoted:
        return StAttrValueQuoted(t, '\'');
    case RoboHtmlTokenizerStateAttributeValueUnquoted:
        return StAttrValueUnquoted(t);
    case RoboHtmlTokenizerStateAfterAttributeValueQuoted:
        return StAfterAttrValueQuoted(t);
    case RoboHtmlTokenizerStateSelfClosingStartTag:
        return StSelfClosingStartTag(t);
    case RoboHtmlTokenizerStateMarkupDeclarationOpen:
        return StMarkupDeclarationOpen(t);
    case RoboHtmlTokenizerStateBogusComment:
        return StBogusComment(t);
    case RoboHtmlTokenizerStateCommentStart:
        return StCommentStart(t);
    case RoboHtmlTokenizerStateCommentStartDash:
        return StCommentStartDash(t);
    case RoboHtmlTokenizerStateComment:
    case RoboHtmlTokenizerStateCommentLessThan:
    case RoboHtmlTokenizerStateCommentLessThanBang:
    case RoboHtmlTokenizerStateCommentLessThanBangDashes:
    case RoboHtmlTokenizerStateCommentEndDash:
    case RoboHtmlTokenizerStateCommentEnd:
    case RoboHtmlTokenizerStateCommentEndBang:
        /* Comment states dispatch to the shared body engine with their
         * individual transitions handled inside the comment functions
         * (states are kept explicit for inspection/testing). */
        if (t->state == RoboHtmlTokenizerStateComment) {
            return StCommentBody(t);
        }
        if (t->state == RoboHtmlTokenizerStateCommentLessThan) {
            return StCommentLessThan(t);
        }
        if (t->state == RoboHtmlTokenizerStateCommentLessThanBang) {
            return StCommentLessThanBang(t);
        }
        if (t->state ==
            RoboHtmlTokenizerStateCommentLessThanBangDashes) {
            return StCommentLessThanBangDashes(t);
        }
        if (t->state == RoboHtmlTokenizerStateCommentEndDash) {
            return StCommentEndDash(t);
        }
        if (t->state == RoboHtmlTokenizerStateCommentEnd) {
            return StCommentEnd(t);
        }
        return StCommentEndBang(t);
    case RoboHtmlTokenizerStateDoctype:
    case RoboHtmlTokenizerStateBeforeDoctypeName:
        return StBeforeDoctypeName(t);
    case RoboHtmlTokenizerStateDoctypeName:
        return StDoctypeName(t);
    case RoboHtmlTokenizerStateAfterDoctypeName:
        return StDoctypeAfterName(t);
    case RoboHtmlTokenizerStateBogusDoctype:
    case RoboHtmlTokenizerStateBogusDoctypeQuirks:
        return StBogusDoctype(t);
    case RoboHtmlTokenizerStateAfterDoctypePublicKeyword:
        return StDoctypePublicKeyword(t);
    case RoboHtmlTokenizerStateBeforeDoctypePublicIdentifier:
        return StBeforeDoctypePublicId(t);
    case RoboHtmlTokenizerStateDoctypePublicIdentifierDoubleQuoted:
        return StDoctypePublicIdQuoted(t, '"');
    case RoboHtmlTokenizerStateDoctypePublicIdentifierSingleQuoted:
        return StDoctypePublicIdQuoted(t, '\'');
    case RoboHtmlTokenizerStateAfterDoctypePublicIdentifier:
    case RoboHtmlTokenizerStateBetweenDoctypePublicAndSystemIdentifiers:
        return StBetweenPublicSystem(t);
    case RoboHtmlTokenizerStateAfterDoctypeSystemKeyword:
        return StDoctypeSystemKeyword(t);
    case RoboHtmlTokenizerStateBeforeDoctypeSystemIdentifier:
        return StBeforeDoctypeSystemId(t);
    case RoboHtmlTokenizerStateDoctypeSystemIdentifierDoubleQuoted:
        return StDoctypeSystemIdQuoted(t, '"');
    case RoboHtmlTokenizerStateDoctypeSystemIdentifierSingleQuoted:
        return StDoctypeSystemIdQuoted(t, '\'');
    case RoboHtmlTokenizerStateAfterDoctypeSystemIdentifier:
        return StAfterDoctypeSystemId(t);

    case RoboHtmlTokenizerStateCharacterReference:
    case RoboHtmlTokenizerStateCharacterReferenceNamed:
    case RoboHtmlTokenizerStateCharacterReferenceNumeric:
    case RoboHtmlTokenizerStateCharacterReferenceNumericHex:
    case RoboHtmlTokenizerStateCharacterReferenceNumericEnd:
        /* Handled inline by ConsumeCharref(); reaching the dispatcher
         * here means a state invariant broke — recover to DATA. */
        diag(t, RoboError, "character-reference state leak; reset to DATA");
        t->state = RoboHtmlTokenizerStateData;
        return TOK_CONTINUE;

    case RoboHtmlTokenizerStateRawtext:
    case RoboHtmlTokenizerStateRcdata:
    case RoboHtmlTokenizerStateScriptData:
        return StRawtextLike(t);
    default:
        /* PLAINTEXT and raw sub-states map onto the shared engine. */
        return StRawtextLike(t);
    }
}

Result HtmlTokenizerCreate(const HtmlSource *src,
                                         HtmlTokenizer **out,
                                         HtmlDiag *diag)
{
    HtmlTokenizer *t;

    if (!out) {
        return RoboHtmlDiag(diag, RoboHtmlDomainParser,
                              RoboInvalidArgument, "out is NULL");
    }
    *out = NULL;
    if (!HtmlIsInitialized()) {
        return RoboHtmlDiag(diag, RoboHtmlDomainParser, RoboInitError,
                              "RoboHTML module not initialized");
    }
    if (!src || src->kind == RoboHtmlSourceKindNone) {
        return RoboHtmlDiag(diag, RoboHtmlDomainSource,
                              RoboInvalidArgument, "source is NULL/none");
    }
    t = (HtmlTokenizer *)RoboCalloc(1u, sizeof(*t));
    if (!t) {
        return RoboHtmlDiag(diag, RoboHtmlDomainParser,
                              RoboOutOfMemory, "tokenizer allocation");
    }
    t->bytes = (const char *)(src->bytes ? (const void *)src->bytes
                                         : (const void *)"");
    t->length = HtmlSourceLength(src);
    t->sourceKind = src->kind;
    t->state = RoboHtmlTokenizerStateData;
    t->returnState = RoboHtmlTokenizerStateData;
    t->line = 1u;
    t->column = 1u;
    HtmlTokenBufferInit(&t->text);
    HtmlTokenBufferInit(&t->text2);
    HtmlTokenBufferInit(&t->text3);
    HtmlTokenBufferInit(&t->chars);
    HtmlTokenBufferInit(&t->lastStartTag);
    HtmlTokenBufferInit(&t->rawEndName);
    HtmlTokenPoolInit(&t->attrs);
    HtmlDiagnosticsInit(&t->diags);
    *out = t;
    LogDebug("robo_html: tokenizer created len=%llu",
                   (unsigned long long)t->length);
    return RoboOk;
}

Result HtmlTokenizerNext(HtmlTokenizer *tok,
                                       HtmlToken *outTok,
                                       HtmlDiag *diagOut)
{
    TokResult r;
    unsigned guard;

    if (!tok || !outTok) {
        return RoboHtmlDiag(diagOut, RoboHtmlDomainParser,
                              RoboInvalidArgument, "NULL argument");
    }
    /* Free the previous token's borrowed attribute view (if any). */
    if (tok->attrView) {
        RoboFree(tok->attrView);
        tok->attrView = NULL;
    }
    if (tok->atEof) {
        /* Stable repeated EOF. */
        TokenBegin(tok, RoboHtmlTokenEof);
        *outTok = tok->token;
        return RoboOk;
    }
    HtmlTokenPoolClear(&tok->attrs);
    tok->currentAttr = NULL;
    HtmlTokenBufferClear(&tok->text);
    HtmlTokenBufferClear(&tok->text2);
    HtmlTokenBufferClear(&tok->text3);
    tok->tagIsEnd = 0;
    tok->tagSelfClosing = 0;
    /* Token start positions are recorded by the state handler that begins
     * the token, not here: StData/StRawtextLike snapshot the byte that
     * opens the construct (the '<'), and markup states resume a token whose
     * '<' was already recorded. Snapshotting again at the top of next()
     * would move the position past the introducer (e.g. "</b>" reporting
     * the offset of 'b'). */

    /* Hard bound per token: each state step consumes >= 1 byte or emits;
     * guard against internal loops (invalid state invariant). */
    guard = 0u;
    do {
        r = step(tok);
        if (r == TOK_ERROR) {
            diag(tok, RoboOutOfMemory, "tokenizer allocation failure");
            return RoboHtmlDiag(diagOut, RoboHtmlDomainParser,
                                  RoboOutOfMemory,
                                  "tokenizer allocation failure");
        }
        guard++;
    } while (r == TOK_CONTINUE && guard < 0xFFFFFFu);

    if (guard >= 0xFFFFFFu) {
        diag(tok, RoboError, "tokenizer state loop; forcing EOF");
        TokenBegin(tok, RoboHtmlTokenEof);
        tok->atEof = 1;
        r = TOK_EMIT;
    }
    *outTok = tok->token;
    (void)HtmlDiagSet(diagOut, RoboOk, RoboHtmlDomainParser,
                             "token", __FILE__, __LINE__);
    return RoboOk;
}

Result HtmlTokenCopyTagName(const HtmlToken *tok,
                                            char *out,
                                            size_t outCap)
{
    const char *name;
    size_t len;

    if (!tok || !out || outCap == 0u) {
        return RoboInvalidArgument;
    }
    out[0] = '\0';
    if (tok->kind != RoboHtmlTokenStartTag &&
        tok->kind != RoboHtmlTokenEndTag &&
        tok->kind != RoboHtmlTokenDoctype) {
        return RoboOk;
    }
    name = (tok->kind == RoboHtmlTokenDoctype) ? tok->u.doctype.name
                                                  : tok->u.tag.name;
    if (!name) {
        return RoboOk;
    }
    len = 0u;
    while (name[len] != '\0') {
        len++;
    }
    if (len + 1u > outCap) {
        return RoboNotSupported; /* refuse, never truncate */
    }
    memcpy(out, name, len + 1u);
    return RoboOk;
}

Result HtmlTokenCopyAttr(const HtmlToken *tok,
                                        const char *name,
                                        char *out,
                                        size_t outCap)
{
    size_t i;

    if (!tok || !name || !out || outCap == 0u) {
        return RoboInvalidArgument;
    }
    out[0] = '\0';
    if (tok->kind != RoboHtmlTokenStartTag &&
        tok->kind != RoboHtmlTokenEndTag) {
        return RoboNotFound;
    }
    for (i = 0u; i < tok->u.tag.attrCount; i++) {
        const char *candidate = tok->u.tag.attrs[i].name;
        size_t j = 0u;
        int equal = 1;

        /* ASCII case-insensitive match (names are stored lowercased). */
        while (candidate[j] != '\0' || name[j] != '\0') {
            if (HtmlTokAsciiLower(candidate[j]) !=
                HtmlTokAsciiLower(name[j])) {
                equal = 0;
                break;
            }
            j++;
        }
        if (!equal) {
            continue;
        }
        {
            const char *value = tok->u.tag.attrs[i].value;
            size_t len = 0u;

            while (value[len] != '\0') {
                len++;
            }
            if (len + 1u > outCap) {
                return RoboNotSupported; /* refuse, never truncate */
            }
            memcpy(out, value, len + 1u);
            return RoboOk;
        }
    }
    return RoboNotFound;
}

Result HtmlTokenizerReset(HtmlTokenizer *tok,
                                        HtmlDiag *diag)
{
    if (!tok) {
        return RoboHtmlDiag(diag, RoboHtmlDomainParser,
                              RoboInvalidArgument, "tokenizer is NULL");
    }
    tok->pos = 0u;
    tok->atEof = 0;
    tok->state = RoboHtmlTokenizerStateData;
    tok->returnState = RoboHtmlTokenizerStateData;
    tok->line = 1u;
    tok->column = 1u;
    tok->tokStartOffset = 0u;
    tok->tokStartLine = 1u;
    tok->tokStartCol = 1u;
    tok->tagIsEnd = 0;
    tok->tagSelfClosing = 0;
    tok->scriptDoubleEscape = 0;
    HtmlTokenBufferClear(&tok->text);
    HtmlTokenBufferClear(&tok->text2);
    HtmlTokenBufferClear(&tok->text3);
    HtmlTokenBufferFree(&tok->chars);
    HtmlTokenBufferClear(&tok->rawEndName);
    HtmlTokenBufferClear(&tok->lastStartTag);
    HtmlTokenPoolClear(&tok->attrs);
    tok->currentAttr = NULL;
    HtmlDiagnosticsClear(&tok->diags);
    return RoboOk;
}

int HtmlTokenizerAtEof(const HtmlTokenizer *tok)
{
    return tok ? tok->atEof : 1;
}

Result HtmlTokenizerGetState(
    const HtmlTokenizer *tok, HtmlTokenizerState *out)
{
    if (!tok || !out) {
        return RoboInvalidArgument;
    }
    *out = tok->state;
    return RoboOk;
}

size_t HtmlTokenizerDiagnosticCount(const HtmlTokenizer *tok)
{
    return tok ? tok->diags.count : 0u;
}

Result HtmlTokenizerGetDiagnostic(
    const HtmlTokenizer *tok, size_t index, HtmlDiag *outDiag)
{
    if (!tok || !outDiag) {
        return RoboInvalidArgument;
    }
    if (index >= tok->diags.count) {
        return RoboNotFound;
    }
    *outDiag = tok->diags.items[index];
    return RoboOk;
}

Result HtmlTokenizerSwitchState(
    HtmlTokenizer *tok, HtmlTokenizerState state,
    HtmlDiag *diag)
{
    if (!tok) {
        return RoboHtmlDiag(diag, RoboHtmlDomainParser,
                              RoboInvalidArgument, "tokenizer is NULL");
    }
    if (HtmlTokenizerStateSwitchValidate(state) != RoboOk) {
        return RoboHtmlDiag(diag, RoboHtmlDomainParser,
                              RoboInvalidArgument, "invalid state");
    }
    switch (state) {
    case RoboHtmlTokenizerStateRcdata:
    case RoboHtmlTokenizerStateRawtext:
    case RoboHtmlTokenizerStateScriptData:
        tok->state = state;
        /* Seed the expected end tag from the last emitted start tag
         * (8.3 integration shape: <textarea> ... switch ... </textarea>). */
        HtmlTokenBufferClear(&tok->rawEndName);
        if (tok->lastStartTag.length > 0u) {
            size_t i;
            for (i = 0u; i < tok->lastStartTag.length; i++) {
                (void)HtmlTokenBufferAppendByte(
                    &tok->rawEndName, tok->lastStartTag.data[i]);
            }
        }
        return RoboOk;
    case RoboHtmlTokenizerStatePlaintext:
    case RoboHtmlTokenizerStateData:
        tok->state = state;
        HtmlTokenBufferClear(&tok->rawEndName);
        return RoboOk;
    default:
        return RoboHtmlDiag(diag, RoboHtmlDomainParser,
                              RoboInvalidArgument,
                              "state not a switch entry point");
    }
}

void HtmlTokenizerDestroy(HtmlTokenizer *tok)
{
    if (!tok) {
        return;
    }
    if (tok->attrView) {
        RoboFree(tok->attrView);
    }
    HtmlTokenBufferFree(&tok->text);
    HtmlTokenBufferFree(&tok->text2);
    HtmlTokenBufferFree(&tok->text3);
    HtmlTokenBufferFree(&tok->chars);
    HtmlTokenBufferFree(&tok->lastStartTag);
    HtmlTokenBufferFree(&tok->rawEndName);
    HtmlTokenPoolFree(&tok->attrs);
    RoboFree(tok);
}

/* ------------------------------------------------------------------ */
/* State helpers (public test surface)                                 */
/* ------------------------------------------------------------------ */

int HtmlTokenizerStateIsKnown(HtmlTokenizerState state)
{
    return state >= RoboHtmlTokenizerStateData &&
           state < RoboHtmlTokenizerStateCount;
}

const char *HtmlTokenizerStateName(HtmlTokenizerState state)
{
    switch (state) {
    case RoboHtmlTokenizerStateData:
        return "data";
    case RoboHtmlTokenizerStatePlaintext:
        return "plaintext";
    case RoboHtmlTokenizerStateTagOpen:
        return "tag_open";
    case RoboHtmlTokenizerStateEndTagOpen:
        return "end_tag_open";
    case RoboHtmlTokenizerStateTagName:
        return "tag_name";
    case RoboHtmlTokenizerStateBeforeAttributeName:
        return "before_attribute_name";
    case RoboHtmlTokenizerStateAttributeName:
        return "attribute_name";
    case RoboHtmlTokenizerStateAfterAttributeName:
        return "after_attribute_name";
    case RoboHtmlTokenizerStateBeforeAttributeValue:
        return "before_attribute_value";
    case RoboHtmlTokenizerStateAttributeValueDoubleQuoted:
        return "attribute_value_double_quoted";
    case RoboHtmlTokenizerStateAttributeValueSingleQuoted:
        return "attribute_value_single_quoted";
    case RoboHtmlTokenizerStateAttributeValueUnquoted:
        return "attribute_value_unquoted";
    case RoboHtmlTokenizerStateAfterAttributeValueQuoted:
        return "after_attribute_value_quoted";
    case RoboHtmlTokenizerStateSelfClosingStartTag:
        return "self_closing_start_tag";
    case RoboHtmlTokenizerStateMarkupDeclarationOpen:
        return "markup_declaration_open";
    case RoboHtmlTokenizerStateBogusComment:
        return "bogus_comment";
    case RoboHtmlTokenizerStateCommentStart:
        return "comment_start";
    case RoboHtmlTokenizerStateCommentStartDash:
        return "comment_start_dash";
    case RoboHtmlTokenizerStateComment:
        return "comment";
    case RoboHtmlTokenizerStateCommentLessThan:
        return "comment_less_than";
    case RoboHtmlTokenizerStateCommentLessThanBang:
        return "comment_less_than_bang";
    case RoboHtmlTokenizerStateCommentLessThanBangDashes:
        return "comment_less_than_bang_dashes";
    case RoboHtmlTokenizerStateCommentEndDash:
        return "comment_end_dash";
    case RoboHtmlTokenizerStateCommentEnd:
        return "comment_end";
    case RoboHtmlTokenizerStateCommentEndBang:
        return "comment_end_bang";
    case RoboHtmlTokenizerStateDoctype:
        return "doctype";
    case RoboHtmlTokenizerStateBeforeDoctypeName:
        return "before_doctype_name";
    case RoboHtmlTokenizerStateDoctypeName:
        return "doctype_name";
    case RoboHtmlTokenizerStateAfterDoctypeName:
        return "after_doctype_name";
    case RoboHtmlTokenizerStateBogusDoctype:
        return "bogus_doctype";
    case RoboHtmlTokenizerStateAfterDoctypePublicKeyword:
        return "after_doctype_public_keyword";
    case RoboHtmlTokenizerStateBeforeDoctypePublicIdentifier:
        return "before_doctype_public_identifier";
    case RoboHtmlTokenizerStateDoctypePublicIdentifierDoubleQuoted:
        return "doctype_public_identifier_double_quoted";
    case RoboHtmlTokenizerStateDoctypePublicIdentifierSingleQuoted:
        return "doctype_public_identifier_single_quoted";
    case RoboHtmlTokenizerStateAfterDoctypePublicIdentifier:
        return "after_doctype_public_identifier";
    case RoboHtmlTokenizerStateBetweenDoctypePublicAndSystemIdentifiers:
        return "between_doctype_public_and_system_identifiers";
    case RoboHtmlTokenizerStateAfterDoctypeSystemKeyword:
        return "after_doctype_system_keyword";
    case RoboHtmlTokenizerStateBeforeDoctypeSystemIdentifier:
        return "before_doctype_system_identifier";
    case RoboHtmlTokenizerStateDoctypeSystemIdentifierDoubleQuoted:
        return "doctype_system_identifier_double_quoted";
    case RoboHtmlTokenizerStateDoctypeSystemIdentifierSingleQuoted:
        return "doctype_system_identifier_single_quoted";
    case RoboHtmlTokenizerStateAfterDoctypeSystemIdentifier:
        return "after_doctype_system_identifier";
    case RoboHtmlTokenizerStateBogusDoctypeQuirks:
        return "bogus_doctype_quirks";
    case RoboHtmlTokenizerStateCharacterReference:
        return "character_reference";
    case RoboHtmlTokenizerStateCharacterReferenceNamed:
        return "character_reference_named";
    case RoboHtmlTokenizerStateCharacterReferenceNumeric:
        return "character_reference_numeric";
    case RoboHtmlTokenizerStateCharacterReferenceNumericHex:
        return "character_reference_numeric_hex";
    case RoboHtmlTokenizerStateCharacterReferenceNumericEnd:
        return "character_reference_numeric_end";
    case RoboHtmlTokenizerStateRawtext:
        return "rawtext";
    case RoboHtmlTokenizerStateRawtextLessThanSign:
        return "rawtext_less_than_sign";
    case RoboHtmlTokenizerStateRawtextEndTagOpen:
        return "rawtext_end_tag_open";
    case RoboHtmlTokenizerStateRawtextEndTagName:
        return "rawtext_end_tag_name";
    case RoboHtmlTokenizerStateRcdata:
        return "rcdata";
    case RoboHtmlTokenizerStateRcdataLessThanSign:
        return "rcdata_less_than_sign";
    case RoboHtmlTokenizerStateRcdataEndTagOpen:
        return "rcdata_end_tag_open";
    case RoboHtmlTokenizerStateRcdataEndTagName:
        return "rcdata_end_tag_name";
    case RoboHtmlTokenizerStateScriptData:
        return "script_data";
    case RoboHtmlTokenizerStateScriptDataLessThanSign:
        return "script_data_less_than_sign";
    case RoboHtmlTokenizerStateScriptDataEndTagOpen:
        return "script_data_end_tag_open";
    case RoboHtmlTokenizerStateScriptDataEndTagName:
        return "script_data_end_tag_name";
    case RoboHtmlTokenizerStateScriptDataEscapeStart:
        return "script_data_escape_start";
    case RoboHtmlTokenizerStateScriptDataEscapeStartDash:
        return "script_data_escape_start_dash";
    case RoboHtmlTokenizerStateScriptDataEscaped:
        return "script_data_escaped";
    case RoboHtmlTokenizerStateScriptDataEscapedDash:
        return "script_data_escaped_dash";
    case RoboHtmlTokenizerStateScriptDataEscapedDashDash:
        return "script_data_escaped_dash_dash";
    case RoboHtmlTokenizerStateScriptDataEscapedLessThanSign:
        return "script_data_escaped_less_than_sign";
    case RoboHtmlTokenizerStateScriptDataEscapedEndTagOpen:
        return "script_data_escaped_end_tag_open";
    case RoboHtmlTokenizerStateScriptDataEscapedEndTagName:
        return "script_data_escaped_end_tag_name";
    case RoboHtmlTokenizerStateScriptDataDoubleEscapeStart:
        return "script_data_double_escape_start";
    case RoboHtmlTokenizerStateScriptDataDoubleEscaped:
        return "script_data_double_escaped";
    case RoboHtmlTokenizerStateScriptDataDoubleEscapedDash:
        return "script_data_double_escaped_dash";
    case RoboHtmlTokenizerStateScriptDataDoubleEscapedDashDash:
        return "script_data_double_escaped_dash_dash";
    case RoboHtmlTokenizerStateScriptDataDoubleEscapedLessThanSign:
        return "script_data_double_escaped_less_than_sign";
    case RoboHtmlTokenizerStateScriptDataDoubleEscapedEndTagOpen:
        return "script_data_double_escaped_end_tag_open";
    case RoboHtmlTokenizerStateScriptDataDoubleEscapedEndTagName:
        return "script_data_double_escaped_end_tag_name";
    default:
        return "unknown";
    }
}

int HtmlTokenizerStateIsRawContent(HtmlTokenizerState state)
{
    switch (state) {
    case RoboHtmlTokenizerStatePlaintext:
    case RoboHtmlTokenizerStateRawtext:
    case RoboHtmlTokenizerStateRawtextLessThanSign:
    case RoboHtmlTokenizerStateRawtextEndTagOpen:
    case RoboHtmlTokenizerStateRawtextEndTagName:
    case RoboHtmlTokenizerStateRcdata:
    case RoboHtmlTokenizerStateRcdataLessThanSign:
    case RoboHtmlTokenizerStateRcdataEndTagOpen:
    case RoboHtmlTokenizerStateRcdataEndTagName:
    case RoboHtmlTokenizerStateScriptData:
    case RoboHtmlTokenizerStateScriptDataLessThanSign:
    case RoboHtmlTokenizerStateScriptDataEndTagOpen:
    case RoboHtmlTokenizerStateScriptDataEndTagName:
    case RoboHtmlTokenizerStateScriptDataEscapeStart:
    case RoboHtmlTokenizerStateScriptDataEscapeStartDash:
    case RoboHtmlTokenizerStateScriptDataEscaped:
    case RoboHtmlTokenizerStateScriptDataEscapedDash:
    case RoboHtmlTokenizerStateScriptDataEscapedDashDash:
    case RoboHtmlTokenizerStateScriptDataEscapedLessThanSign:
    case RoboHtmlTokenizerStateScriptDataEscapedEndTagOpen:
    case RoboHtmlTokenizerStateScriptDataEscapedEndTagName:
    case RoboHtmlTokenizerStateScriptDataDoubleEscapeStart:
    case RoboHtmlTokenizerStateScriptDataDoubleEscaped:
    case RoboHtmlTokenizerStateScriptDataDoubleEscapedDash:
    case RoboHtmlTokenizerStateScriptDataDoubleEscapedDashDash:
    case RoboHtmlTokenizerStateScriptDataDoubleEscapedLessThanSign:
    case RoboHtmlTokenizerStateScriptDataDoubleEscapedEndTagOpen:
    case RoboHtmlTokenizerStateScriptDataDoubleEscapedEndTagName:
        return 1;
    default:
        return 0;
    }
}

Result HtmlTokenizerStateSwitchValidate(
    HtmlTokenizerState state)
{
    /* Only the five content-family entry points may be switched into from
     * outside; every sub-state is reached by the machine itself. */
    switch (state) {
    case RoboHtmlTokenizerStateData:
    case RoboHtmlTokenizerStatePlaintext:
    case RoboHtmlTokenizerStateRawtext:
    case RoboHtmlTokenizerStateRcdata:
    case RoboHtmlTokenizerStateScriptData:
        return RoboOk;
    default:
        return RoboInvalidArgument;
    }
}
