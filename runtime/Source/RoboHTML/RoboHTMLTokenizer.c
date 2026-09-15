/*
 * RoboHTML tokenizer (Phase 8.2).
 *
 * An explicit state machine in the shape of the HTML tokenizer model
 * (independent implementation; no Chromium code or data structures).
 *
 * Structure: robo_html_tokenizer_next() drives a byte loop; each state has
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

char robo_html_tok_ascii_lower(char c)
{
    if (c >= 'A' && c <= 'Z') {
        return (char)(c - 'A' + 'a');
    }
    return c;
}

char robo_html_tok_peek(const robo_html_tokenizer_t *t)
{
    if (!t || t->pos >= t->length) {
        return '\0';
    }
    return t->bytes[t->pos];
}

char robo_html_tok_peek_ahead(const robo_html_tokenizer_t *t, size_t ahead)
{
    if (!t || t->pos + ahead >= t->length) {
        return '\0';
    }
    return t->bytes[t->pos + ahead];
}

void robo_html_tok_advance(robo_html_tokenizer_t *t)
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

int robo_html_tok_word_at(const robo_html_tokenizer_t *t, const char *word)
{
    size_t i;

    if (!t || !word) {
        return 0;
    }
    for (i = 0u; word[i] != '\0'; i++) {
        if (t->pos + i >= t->length ||
            robo_html_tok_ascii_lower(t->bytes[t->pos + i]) != word[i]) {
            return 0;
        }
    }
    return 1;
}

static int is_ws(char c)
{
    /*
     * HTML whitespace: TAB, LF, FF, CR, SPACE. (CR is normalized away by
     * the input preprocessor in the standard; Roboticela drops CR before
     * LF and treats a lone CR as LF-equivalent — see data-state handling.)
     */
    return c == '\t' || c == '\n' || c == '\f' || c == '\r' || c == ' ';
}

/** ASCII case-insensitive equality of two byte ranges (no locale). */
static int robo_ascii_casecmp_bytes(const char *a, const char *b, size_t n)
{
    size_t i;
    for (i = 0u; i < n; i++) {
        if (robo_html_tok_ascii_lower(a[i]) != robo_html_tok_ascii_lower(b[i])) {
            return 0;
        }
    }
    return 1;
}

static void diag(robo_html_tokenizer_t *t,
                 robo_result_t code,
                 const char *msg)
{
    robo_html_diagnostics_push(&t->diags, code, ROBO_HTML_DOMAIN_PARSER, msg,
                               t->line, t->column);
}

static void snapshot_pos(robo_html_tokenizer_t *t)
{
    t->tok_start_offset = t->pos;
    t->tok_start_line = t->line;
    t->tok_start_col = t->column;
}

static void token_begin(robo_html_tokenizer_t *t, robo_html_token_kind_t kind)
{
    robo_html_token_init(&t->token);
    t->token.kind = kind;
    t->token.pos.offset = t->tok_start_offset;
    t->token.pos.line = t->tok_start_line;
    t->token.pos.column = t->tok_start_col;
}

/* Mark a finished token's end position as its start (kept lightweight). */
static void token_finish(robo_html_tokenizer_t *t)
{
    (void)t;
}

static void set_current_attr_name_byte(robo_html_tokenizer_t *t, char c)
{
    if (t->current_attr) {
        (void)robo_html_token_buffer_append_byte(&t->current_attr->name, c);
    }
}

static void set_current_attr_value_byte(robo_html_tokenizer_t *t, char c)
{
    if (t->current_attr) {
        (void)robo_html_token_buffer_append_byte(&t->current_attr->value, c);
    }
}

static void set_current_attr_value_cp(robo_html_tokenizer_t *t, uint32_t cp)
{
    if (t->current_attr) {
        (void)robo_html_token_buffer_append_utf8(&t->current_attr->value, cp);
    }
}

static int is_ascii_letter(char c)
{
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z');
}

static int is_ascii_name_char(char c)
{
    return is_ascii_letter(c) || (c >= '0' && c <= '9');
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
static void consume_charref(robo_html_tokenizer_t *t,
                            robo_html_token_buffer_t *buf,
                            int in_attribute)
{
    size_t offset = t->pos;
    robo_html_charref_error_t err = ROBO_HTML_CHARREF_OK;

    if (robo_html_charref_expand(t->bytes, t->length, &offset, in_attribute,
                                 t->charref_out, &t->charref_len,
                                 &err)) {
        size_t i;
        for (i = 0u; i < t->charref_len; i++) {
            if (buf) {
                (void)robo_html_token_buffer_append_byte(buf, t->charref_out[i]);
            } else {
                set_current_attr_value_byte(t, t->charref_out[i]);
            }
        }
        while (t->pos < offset) {
            robo_html_tok_advance(t);
        }
        if (err == ROBO_HTML_CHARREF_MISSING_SEMICOLON) {
            diag(t, ROBO_PARSE_ERROR, "character reference missing ';'");
        } else if (err == ROBO_HTML_CHARREF_OUT_OF_RANGE) {
            diag(t, ROBO_PARSE_ERROR,
                 "character reference value out of range (mapped)");
        }
    } else {
        /* Literal '&' (the following bytes re-enter the normal scan). */
        if (buf) {
            (void)robo_html_token_buffer_append_byte(buf, '&');
        } else {
            set_current_attr_value_byte(t, '&');
        }
        robo_html_tok_advance(t);
        if (robo_html_tok_peek(t) == '#') {
            diag(t, ROBO_PARSE_ERROR, "invalid numeric character reference");
        } else if (is_ascii_name_char(robo_html_tok_peek(t))) {
            diag(t, ROBO_PARSE_ERROR, "unknown character reference");
        }
    }
}

/* ------------------------------------------------------------------ */
/* Comment states                                                      */
/* ------------------------------------------------------------------ */

typedef enum tok_result {
    TOK_CONTINUE = 0,
    TOK_EMIT = 1,
    TOK_ERROR = 2
} tok_result_t;

static tok_result_t st_data(robo_html_tokenizer_t *t);
static tok_result_t st_tag_open(robo_html_tokenizer_t *t);
static tok_result_t st_tag_name(robo_html_tokenizer_t *t);
static tok_result_t st_end_tag_open(robo_html_tokenizer_t *t);
static tok_result_t st_before_attr_name(robo_html_tokenizer_t *t);
static tok_result_t st_attr_name(robo_html_tokenizer_t *t);
static tok_result_t st_after_attr_name(robo_html_tokenizer_t *t);
static tok_result_t st_before_attr_value(robo_html_tokenizer_t *t);
static tok_result_t st_attr_value_quoted(robo_html_tokenizer_t *t, char quote);
static tok_result_t st_attr_value_unquoted(robo_html_tokenizer_t *t);
static tok_result_t st_after_attr_value_quoted(robo_html_tokenizer_t *t);
static tok_result_t st_self_closing_start_tag(robo_html_tokenizer_t *t);
static tok_result_t st_markup_declaration_open(robo_html_tokenizer_t *t);
static tok_result_t st_comment_body(robo_html_tokenizer_t *t);
static tok_result_t st_doctype_name(robo_html_tokenizer_t *t);
static tok_result_t st_doctype_after_name(robo_html_tokenizer_t *t);
static tok_result_t st_before_doctype_public_id(robo_html_tokenizer_t *t);
static tok_result_t st_before_doctype_system_id(robo_html_tokenizer_t *t);
static tok_result_t st_bogus_doctype(robo_html_tokenizer_t *t);
static tok_result_t st_bogus_comment(robo_html_tokenizer_t *t);
static tok_result_t st_rawtext_like(robo_html_tokenizer_t *t);

/**
 * Emit a stable EOF token. Used both for clean end-of-input and as the
 * recovery when EOF is reached inside a markup state (the partially
 * accumulated tag/comment content is dropped with a diagnostic).
 */
static tok_result_t emit_eof_token(robo_html_tokenizer_t *t)
{
    token_begin(t, ROBO_HTML_TOKEN_EOF);
    token_finish(t);
    t->at_eof = 1;
    t->state = ROBO_HTML_TOKENIZER_STATE_DATA;
    return TOK_EMIT;
}

static tok_result_t emit_char_token(robo_html_tokenizer_t *t,
                                    robo_html_token_buffer_t *run)
{
    token_begin(t, ROBO_HTML_TOKEN_CHARACTER);
    /* The token borrows the character bytes: move the run into the
     * tokenizer-owned chars buffer so the data stays valid until the next
     * character emission / reset / destroy (borrowed-token contract). */
    robo_html_token_buffer_free(&t->chars);
    t->chars = *run;
    run->data = NULL;
    run->length = 0u;
    run->capacity = 0u;
    t->token.u.characters.data = t->chars.data ? t->chars.data : "";
    t->token.u.characters.length = t->chars.length;
    token_finish(t);
    return TOK_EMIT;
}

/* ------------------------------------------------------------------ */
/* Data state                                                          */
/* ------------------------------------------------------------------ */

static tok_result_t st_data(robo_html_tokenizer_t *t)
{
    robo_html_token_buffer_t run;
    char c;

    robo_html_token_buffer_init(&run);
    snapshot_pos(t);

    while (t->pos < t->length) {
        c = robo_html_tok_peek(t);
        if (c == '&') {
            consume_charref(t, &run, 0);
            continue;
        }
        if (c == '<') {
            /* Markup only when the next byte opens a construct: '!', '/',
             * '?', or a tag-name letter. Otherwise '<' is literal text
             * ("a < b" stays one character run). */
            char n = robo_html_tok_peek_ahead(t, 1);
            if (n == '!' || n == '/' || n == '?' || is_ascii_letter(n)) {
                robo_html_tokenizer_state_t next_state =
                    (n == '/') ? ROBO_HTML_TOKENIZER_STATE_END_TAG_OPEN
                    : (n == '!')
                          ? ROBO_HTML_TOKENIZER_STATE_MARKUP_DECLARATION_OPEN
                          : ROBO_HTML_TOKENIZER_STATE_TAG_OPEN;
                tok_result_t r = TOK_CONTINUE;
                if (run.length > 0u) {
                    r = emit_char_token(t, &run); /* steals run */
                } else {
                    robo_html_token_buffer_free(&run);
                }
                snapshot_pos(t); /* the '<' begins the markup token */
                /* Consume '<' and, for the two states that are defined
                 * *after* their introducer, that introducer too: the
                 * classic tag-open hand-off. END_TAG_OPEN expects the
                 * cursor at the tag name (past "</") and
                 * MARKUP_DECLARATION_OPEN past "<!" (it matches
                 * "doctype" / "--" / "[CDATA[" at the cursor). */
                robo_html_tok_advance(t); /* consume '<' */
                if (n == '/' || n == '!') {
                    robo_html_tok_advance(t); /* consume '/' or '!' */
                }
                t->state = next_state;
                return r;
            }
            (void)robo_html_token_buffer_append_byte(&run, '<');
            robo_html_tok_advance(t);
            continue;
        }
        if (c == '\r') {
            /* CR normalization: emit LF; swallow a following LF. */
            (void)robo_html_token_buffer_append_byte(&run, '\n');
            robo_html_tok_advance(t);
            if (robo_html_tok_peek(t) == '\n') {
                robo_html_tok_advance(t);
            }
            continue;
        }
        if (c == '\0') {
            /* NUL in data: replacement character + parse error. */
            (void)robo_html_token_buffer_append_utf8(&run, 0xFFFDu);
            diag(t, ROBO_PARSE_ERROR, "NUL character in data state");
            robo_html_tok_advance(t);
            continue;
        }
        /* Multi-byte UTF-8: decode with REPLACE (never out of bounds). */
        {
            uint32_t cp = 0u;
            size_t next = 0u;
            robo_unicode_status_t st = robo_unicode_utf8_decode(
                t->bytes, t->length, t->pos, ROBO_UNICODE_REPLACE, &cp,
                &next);
            if (st == ROBO_UNICODE_OK || st == ROBO_UNICODE_INVALID ||
                st == ROBO_UNICODE_TRUNCATED) {
                (void)robo_html_token_buffer_append_utf8(&run, cp);
                if (st != ROBO_UNICODE_OK) {
                    diag(t, ROBO_PARSE_ERROR,
                         "ill-formed UTF-8 in text (replacement used)");
                }
                while (t->pos < next) {
                    robo_html_tok_advance(t);
                }
                continue;
            }
        }
        (void)robo_html_token_buffer_append_byte(&run, c);
        robo_html_tok_advance(t);
    }

    /* EOF: flush the run if non-empty, else end the stream. */
    if (run.length > 0u) {
        /* emit_char_token steals the run storage. */
        return emit_char_token(t, &run);
    }
    robo_html_token_buffer_free(&run);
    return emit_eof_token(t);
}

/* ------------------------------------------------------------------ */
/* Tag states                                                          */
/* ------------------------------------------------------------------ */

static tok_result_t st_tag_open(robo_html_tokenizer_t *t)
{
    char c = robo_html_tok_peek(t);

    if (c == '!') {
        robo_html_tok_advance(t);
        t->state = ROBO_HTML_TOKENIZER_STATE_MARKUP_DECLARATION_OPEN;
        return TOK_CONTINUE;
    }
    if (c == '/') {
        robo_html_tok_advance(t);
        t->state = ROBO_HTML_TOKENIZER_STATE_END_TAG_OPEN;
        return TOK_CONTINUE;
    }
    if (c >= 'A' && c <= 'Z') {
        t->tag_is_end = 0;
        robo_html_token_buffer_clear(&t->text);
        (void)robo_html_token_buffer_append_byte(&t->text,
                                                 robo_html_tok_ascii_lower(c));
        robo_html_tok_advance(t);
        t->state = ROBO_HTML_TOKENIZER_STATE_TAG_NAME;
        return TOK_CONTINUE;
    }
    if (c >= 'a' && c <= 'z') {
        t->tag_is_end = 0;
        robo_html_token_buffer_clear(&t->text);
        (void)robo_html_token_buffer_append_byte(&t->text, c);
        robo_html_tok_advance(t);
        t->state = ROBO_HTML_TOKENIZER_STATE_TAG_NAME;
        return TOK_CONTINUE;
    }
    if (c == '?') {
        /* Bogus comment per the standard (processing-instruction legacy). */
        diag(t, ROBO_PARSE_ERROR, "'<?' treated as bogus comment");
        robo_html_token_buffer_clear(&t->text);
        robo_html_tok_advance(t);
        t->state = ROBO_HTML_TOKENIZER_STATE_BOGUS_COMMENT;
        return TOK_CONTINUE;
    }
    if (c == '\0') {
        diag(t, ROBO_PARSE_ERROR, "NUL after '<'");
        robo_html_token_buffer_clear(&t->text);
        (void)robo_html_token_buffer_append_utf8(&t->text, 0xFFFDu);
        robo_html_tok_advance(t);
        t->state = ROBO_HTML_TOKENIZER_STATE_TAG_NAME;
        return TOK_CONTINUE;
    }
    /* Anything else (including EOF): '<' is literal text. */
    if (t->pos >= t->length) {
        diag(t, ROBO_PARSE_ERROR, "'<' at end of input");
    } else {
        diag(t, ROBO_PARSE_ERROR, "'<' before invalid tag character");
    }
    {
        robo_html_token_buffer_t lt;
        robo_html_token_buffer_init(&lt);
        (void)robo_html_token_buffer_append_byte(&lt, '<');
        tok_result_t r = emit_char_token(t, &lt);
        robo_html_token_buffer_free(&lt);
        t->state = ROBO_HTML_TOKENIZER_STATE_DATA;
        return r;
    }
}

static tok_result_t st_end_tag_open(robo_html_tokenizer_t *t)
{
    char c = robo_html_tok_peek(t);

    if (c >= 'A' && c <= 'Z') {
        t->tag_is_end = 1;
        robo_html_token_buffer_clear(&t->text);
        (void)robo_html_token_buffer_append_byte(&t->text,
                                                 robo_html_tok_ascii_lower(c));
        robo_html_tok_advance(t);
        t->state = ROBO_HTML_TOKENIZER_STATE_TAG_NAME;
        return TOK_CONTINUE;
    }
    if (c >= 'a' && c <= 'z') {
        t->tag_is_end = 1;
        robo_html_token_buffer_clear(&t->text);
        (void)robo_html_token_buffer_append_byte(&t->text, c);
        robo_html_tok_advance(t);
        t->state = ROBO_HTML_TOKENIZER_STATE_TAG_NAME;
        return TOK_CONTINUE;
    }
    if (c == '>') {
        diag(t, ROBO_PARSE_ERROR, "empty end tag '</>'");
        robo_html_tok_advance(t);
        t->state = ROBO_HTML_TOKENIZER_STATE_DATA;
        return TOK_CONTINUE;
    }
    /* EOF or junk → bogus comment starting from the current position. */
    diag(t, ROBO_PARSE_ERROR, "malformed end tag; treated as bogus comment");
    robo_html_token_buffer_clear(&t->text);
    t->state = ROBO_HTML_TOKENIZER_STATE_BOGUS_COMMENT;
    return TOK_CONTINUE;
}

static tok_result_t emit_tag_token(robo_html_tokenizer_t *t)
{
    token_begin(t, t->tag_is_end ? ROBO_HTML_TOKEN_END_TAG
                                 : ROBO_HTML_TOKEN_START_TAG);
    t->token.u.tag.name = t->text.data ? t->text.data : "";
    t->token.u.tag.attr_count = t->attrs.count;
    t->token.u.tag.self_closing = t->tag_self_closing;
    /* Attributes stay in token order; duplicates remain in the token
     * (first-occurrence-wins is a tree-builder rule, Phase 8.3). */
    if (t->attrs.count > 0u) {
        size_t i;
        /* Build the borrowed view array in the pool's own storage order. */
        robo_html_token_attr_t *view = (robo_html_token_attr_t *)robo_malloc(
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
        t->attr_view = view; /* freed at next token / destroy */
    } else {
        t->token.u.tag.attrs = NULL;
    }
    if (!t->tag_is_end) {
        /* Remember the start-tag name; the controlled raw-content switch
         * hook seeds the expected end tag from it. */
        size_t i;
        robo_html_token_buffer_clear(&t->last_start_tag);
        for (i = 0u; i < t->text.length; i++) {
            (void)robo_html_token_buffer_append_byte(&t->last_start_tag,
                                                     t->text.data[i]);
        }
    }
    token_finish(t);
    t->tag_self_closing = 0;
    return TOK_EMIT;
}

static tok_result_t st_tag_name(robo_html_tokenizer_t *t)
{
    char c = robo_html_tok_peek(t);

    if (t->pos >= t->length) {
        diag(t, ROBO_PARSE_ERROR, "EOF in tag");
        return emit_eof_token(t);
    }
    if (is_ws(c)) {
        robo_html_tok_advance(t);
        t->state = ROBO_HTML_TOKENIZER_STATE_BEFORE_ATTRIBUTE_NAME;
        return TOK_CONTINUE;
    }
    if (c == '/') {
        robo_html_tok_advance(t);
        t->state = ROBO_HTML_TOKENIZER_STATE_SELF_CLOSING_START_TAG;
        return TOK_CONTINUE;
    }
    if (c == '>') {
        robo_html_tok_advance(t);
        {
            tok_result_t r = emit_tag_token(t);
            t->state = ROBO_HTML_TOKENIZER_STATE_DATA;
            return r;
        }
    }
    if (c == '\0') {
        (void)robo_html_token_buffer_append_utf8(&t->text, 0xFFFDu);
        diag(t, ROBO_PARSE_ERROR, "NUL in tag name");
        robo_html_tok_advance(t);
        return TOK_CONTINUE;
    }
    (void)robo_html_token_buffer_append_byte(&t->text,
                                             robo_html_tok_ascii_lower(c));
    robo_html_tok_advance(t);
    return TOK_CONTINUE;
}

static tok_result_t st_before_attr_name(robo_html_tokenizer_t *t)
{
    char c;

    if (t->pos >= t->length) {
        diag(t, ROBO_PARSE_ERROR, "EOF in tag");
        return emit_eof_token(t);
    }
    c = robo_html_tok_peek(t);
    if (is_ws(c)) {
        robo_html_tok_advance(t);
        return TOK_CONTINUE;
    }
    if (c == '/') {
        robo_html_tok_advance(t);
        t->state = ROBO_HTML_TOKENIZER_STATE_SELF_CLOSING_START_TAG;
        return TOK_CONTINUE;
    }
    if (c == '>') {
        robo_html_tok_advance(t);
        {
            tok_result_t r = emit_tag_token(t);
            t->state = ROBO_HTML_TOKENIZER_STATE_DATA;
            return r;
        }
    }
    if (c == '=') {
        diag(t, ROBO_PARSE_ERROR, "'=' before attribute name");
        t->current_attr = NULL;
        {
            robo_html_token_attr_entry_t *e = NULL;
            if (robo_html_token_pool_next(&t->attrs, &e) != ROBO_OK) {
                return TOK_ERROR;
            }
            t->current_attr = e;
            (void)robo_html_token_buffer_append_byte(&e->name, '=');
        }
        robo_html_tok_advance(t);
        t->state = ROBO_HTML_TOKENIZER_STATE_ATTRIBUTE_NAME;
        return TOK_CONTINUE;
    }
    if (c == '\0') {
        diag(t, ROBO_PARSE_ERROR, "NUL in attribute name");
        {
            robo_html_token_attr_entry_t *e = NULL;
            if (robo_html_token_pool_next(&t->attrs, &e) != ROBO_OK) {
                return TOK_ERROR;
            }
            t->current_attr = e;
            (void)robo_html_token_buffer_append_utf8(&e->name, 0xFFFDu);
        }
        robo_html_tok_advance(t);
        t->state = ROBO_HTML_TOKENIZER_STATE_ATTRIBUTE_NAME;
        return TOK_CONTINUE;
    }
    if (c == '"' || c == '\'' || c == '<') {
        diag(t, ROBO_PARSE_ERROR, "unexpected quote/'<' in attribute name");
    }
    {
        robo_html_token_attr_entry_t *e = NULL;
        if (robo_html_token_pool_next(&t->attrs, &e) != ROBO_OK) {
            return TOK_ERROR;
        }
        t->current_attr = e;
        (void)robo_html_token_buffer_append_byte(&e->name,
                                                 robo_html_tok_ascii_lower(c));
    }
    robo_html_tok_advance(t);
    t->state = ROBO_HTML_TOKENIZER_STATE_ATTRIBUTE_NAME;
    return TOK_CONTINUE;
}

static tok_result_t st_attr_name(robo_html_tokenizer_t *t)
{
    char c;

    if (t->pos >= t->length) {
        diag(t, ROBO_PARSE_ERROR, "EOF in tag");
        return emit_eof_token(t);
    }
    c = robo_html_tok_peek(t);
    if (is_ws(c) || c == '/' || c == '>') {
        t->state = ROBO_HTML_TOKENIZER_STATE_AFTER_ATTRIBUTE_NAME;
        return TOK_CONTINUE; /* do not consume: after-state re-dispatches */
    }
    if (c == '=') {
        robo_html_tok_advance(t);
        t->state = ROBO_HTML_TOKENIZER_STATE_BEFORE_ATTRIBUTE_VALUE;
        return TOK_CONTINUE;
    }
    if (c == '\0') {
        (void)robo_html_token_buffer_append_utf8(&t->current_attr->name,
                                                 0xFFFDu);
        diag(t, ROBO_PARSE_ERROR, "NUL in attribute name");
        robo_html_tok_advance(t);
        return TOK_CONTINUE;
    }
    if (c == '"' || c == '\'' || c == '<') {
        diag(t, ROBO_PARSE_ERROR, "unexpected quote/'<' in attribute name");
    }
    set_current_attr_name_byte(t, robo_html_tok_ascii_lower(c));
    robo_html_tok_advance(t);
    return TOK_CONTINUE;
}

static tok_result_t st_after_attr_name(robo_html_tokenizer_t *t)
{
    char c;

    if (t->pos >= t->length) {
        diag(t, ROBO_PARSE_ERROR, "EOF in tag");
        return emit_eof_token(t);
    }
    c = robo_html_tok_peek(t);
    if (is_ws(c)) {
        robo_html_tok_advance(t);
        return TOK_CONTINUE;
    }
    if (c == '/') {
        robo_html_tok_advance(t);
        t->state = ROBO_HTML_TOKENIZER_STATE_SELF_CLOSING_START_TAG;
        return TOK_CONTINUE;
    }
    if (c == '=') {
        robo_html_tok_advance(t);
        t->state = ROBO_HTML_TOKENIZER_STATE_BEFORE_ATTRIBUTE_VALUE;
        return TOK_CONTINUE;
    }
    if (c == '>') {
        robo_html_tok_advance(t);
        {
            tok_result_t r = emit_tag_token(t);
            t->state = ROBO_HTML_TOKENIZER_STATE_DATA;
            return r;
        }
    }
    /* New attribute without value separator. */
    t->current_attr = NULL;
    {
        robo_html_token_attr_entry_t *e = NULL;
        if (robo_html_token_pool_next(&t->attrs, &e) != ROBO_OK) {
            return TOK_ERROR;
        }
        t->current_attr = e;
        (void)robo_html_token_buffer_append_byte(&e->name,
                                                 robo_html_tok_ascii_lower(c));
    }
    robo_html_tok_advance(t);
    t->state = ROBO_HTML_TOKENIZER_STATE_ATTRIBUTE_NAME;
    return TOK_CONTINUE;
}

static tok_result_t st_before_attr_value(robo_html_tokenizer_t *t)
{
    char c;

    if (t->pos >= t->length) {
        diag(t, ROBO_PARSE_ERROR, "EOF in tag");
        return emit_eof_token(t);
    }
    c = robo_html_tok_peek(t);
    if (is_ws(c)) {
        robo_html_tok_advance(t);
        return TOK_CONTINUE;
    }
    if (c == '"') {
        robo_html_tok_advance(t);
        t->state = ROBO_HTML_TOKENIZER_STATE_ATTRIBUTE_VALUE_DOUBLE_QUOTED;
        return TOK_CONTINUE;
    }
    if (c == '\'') {
        robo_html_tok_advance(t);
        t->state = ROBO_HTML_TOKENIZER_STATE_ATTRIBUTE_VALUE_SINGLE_QUOTED;
        return TOK_CONTINUE;
    }
    if (c == '>') {
        diag(t, ROBO_PARSE_ERROR, "attribute value missing");
        robo_html_tok_advance(t);
        {
            tok_result_t r = emit_tag_token(t);
            t->state = ROBO_HTML_TOKENIZER_STATE_DATA;
            return r;
        }
    }
    t->state = ROBO_HTML_TOKENIZER_STATE_ATTRIBUTE_VALUE_UNQUOTED;
    return TOK_CONTINUE; /* unquoted handler consumes */
}

static tok_result_t st_attr_value_quoted(robo_html_tokenizer_t *t, char quote)
{
    char c;

    if (t->pos >= t->length) {
        diag(t, ROBO_PARSE_ERROR, "EOF in quoted attribute value");
        return emit_eof_token(t);
    }
    c = robo_html_tok_peek(t);
    if (c == quote) {
        robo_html_tok_advance(t);
        t->state = ROBO_HTML_TOKENIZER_STATE_AFTER_ATTRIBUTE_VALUE_QUOTED;
        return TOK_CONTINUE;
    }
    if (c == '&') {
        consume_charref(t, NULL, 1);
        return TOK_CONTINUE;
    }
    if (c == '\0') {
        set_current_attr_value_cp(t, 0xFFFDu);
        diag(t, ROBO_PARSE_ERROR, "NUL in attribute value");
        robo_html_tok_advance(t);
        return TOK_CONTINUE;
    }
    if (c == '\r') {
        set_current_attr_value_byte(t, '\n');
        robo_html_tok_advance(t);
        if (robo_html_tok_peek(t) == '\n') {
            robo_html_tok_advance(t);
        }
        return TOK_CONTINUE;
    }
    set_current_attr_value_byte(t, c);
    robo_html_tok_advance(t);
    return TOK_CONTINUE;
}

static tok_result_t st_attr_value_unquoted(robo_html_tokenizer_t *t)
{
    char c;

    if (t->pos >= t->length) {
        diag(t, ROBO_PARSE_ERROR, "EOF in tag");
        return emit_eof_token(t);
    }
    c = robo_html_tok_peek(t);
    if (is_ws(c)) {
        robo_html_tok_advance(t);
        t->state = ROBO_HTML_TOKENIZER_STATE_BEFORE_ATTRIBUTE_NAME;
        return TOK_CONTINUE;
    }
    if (c == '>') {
        robo_html_tok_advance(t);
        {
            tok_result_t r = emit_tag_token(t);
            t->state = ROBO_HTML_TOKENIZER_STATE_DATA;
            return r;
        }
    }
    if (c == '/' && robo_html_tok_peek_ahead(t, 1) == '>') {
        /* "/>" after the value: self-closing syntax, not value content. */
        t->state = ROBO_HTML_TOKENIZER_STATE_SELF_CLOSING_START_TAG;
        return TOK_CONTINUE; /* self-closing handler consumes */
    }
    if (c == '&') {
        consume_charref(t, NULL, 1);
        return TOK_CONTINUE;
    }
    if (c == '\0') {
        set_current_attr_value_cp(t, 0xFFFDu);
        diag(t, ROBO_PARSE_ERROR, "NUL in attribute value");
        robo_html_tok_advance(t);
        return TOK_CONTINUE;
    }
    if (c == '"' || c == '\'' || c == '<' || c == '=' || c == '`') {
        diag(t, ROBO_PARSE_ERROR, "unexpected character in unquoted value");
    }
    set_current_attr_value_byte(t, c);
    robo_html_tok_advance(t);
    return TOK_CONTINUE;
}

static tok_result_t st_after_attr_value_quoted(robo_html_tokenizer_t *t)
{
    char c;

    if (t->pos >= t->length) {
        diag(t, ROBO_PARSE_ERROR, "EOF in tag");
        return emit_eof_token(t);
    }
    c = robo_html_tok_peek(t);
    if (is_ws(c)) {
        robo_html_tok_advance(t);
        t->state = ROBO_HTML_TOKENIZER_STATE_BEFORE_ATTRIBUTE_NAME;
        return TOK_CONTINUE;
    }
    if (c == '/') {
        robo_html_tok_advance(t);
        t->state = ROBO_HTML_TOKENIZER_STATE_SELF_CLOSING_START_TAG;
        return TOK_CONTINUE;
    }
    if (c == '>') {
        robo_html_tok_advance(t);
        {
            tok_result_t r = emit_tag_token(t);
            t->state = ROBO_HTML_TOKENIZER_STATE_DATA;
            return r;
        }
    }
    diag(t, ROBO_PARSE_ERROR, "missing whitespace between attributes");
    t->state = ROBO_HTML_TOKENIZER_STATE_BEFORE_ATTRIBUTE_NAME;
    return TOK_CONTINUE; /* do not consume */
}

static tok_result_t st_self_closing_start_tag(robo_html_tokenizer_t *t)
{
    char c;

    if (t->pos >= t->length) {
        diag(t, ROBO_PARSE_ERROR, "EOF in tag");
        return emit_eof_token(t);
    }
    c = robo_html_tok_peek(t);
    if (c == '>') {
        robo_html_tok_advance(t);
        t->tag_self_closing = 1;
        {
            tok_result_t r = emit_tag_token(t);
            t->state = ROBO_HTML_TOKENIZER_STATE_DATA;
            return r;
        }
    }
    diag(t, ROBO_PARSE_ERROR, "'/' not followed by '>'");
    t->state = ROBO_HTML_TOKENIZER_STATE_BEFORE_ATTRIBUTE_NAME;
    return TOK_CONTINUE; /* do not consume */
}

/* ------------------------------------------------------------------ */
/* Markup declarations: comments + doctype                             */
/* ------------------------------------------------------------------ */

static tok_result_t st_markup_declaration_open(robo_html_tokenizer_t *t)
{
    if (robo_html_tok_word_at(t, "doctype")) {
        size_t i;
        for (i = 0u; i < 7u; i++) {
            robo_html_tok_advance(t);
        }
        robo_html_token_buffer_clear(&t->text);
        robo_html_token_buffer_clear(&t->text2);
        robo_html_token_buffer_clear(&t->text3);
        t->doctype_force_quirks = 0;
        t->doctype_has_public = 0;
        t->doctype_has_system = 0;
        t->state = ROBO_HTML_TOKENIZER_STATE_BEFORE_DOCTYPE_NAME;
        return TOK_CONTINUE;
    }
    if (robo_html_tok_peek_ahead(t, 0) == '-' &&
        robo_html_tok_peek_ahead(t, 1) == '-') {
        robo_html_tok_advance(t);
        robo_html_tok_advance(t);
        robo_html_token_buffer_clear(&t->text);
        t->state = ROBO_HTML_TOKENIZER_STATE_COMMENT_START;
        return TOK_CONTINUE;
    }
    if (t->pos + 7u <= t->length &&
        memcmp(t->bytes + t->pos, "[CDATA[", 7u) == 0) {
        size_t i;
        /* In HTML content CDATA is a bogus comment with the payload text. */
        diag(t, ROBO_PARSE_ERROR,
             "'<![CDATA[' in HTML content: bogus comment");
        for (i = 0u; i < 7u; i++) {
            robo_html_tok_advance(t);
        }
        robo_html_token_buffer_clear(&t->text);
        t->state = ROBO_HTML_TOKENIZER_STATE_BOGUS_COMMENT;
        return TOK_CONTINUE;
    }
    diag(t, ROBO_PARSE_ERROR, "malformed markup declaration");
    robo_html_token_buffer_clear(&t->text);
    t->state = ROBO_HTML_TOKENIZER_STATE_BOGUS_COMMENT;
    return TOK_CONTINUE;
}

static tok_result_t emit_comment_token(robo_html_tokenizer_t *t)
{
    token_begin(t, ROBO_HTML_TOKEN_COMMENT);
    t->token.u.comment.data = t->text.data ? t->text.data : "";
    t->token.u.comment.length = t->text.length;
    token_finish(t);
    return TOK_EMIT;
}

static tok_result_t st_comment_start(robo_html_tokenizer_t *t)
{
    char c;

    if (t->pos >= t->length) {
        diag(t, ROBO_PARSE_ERROR, "EOF in comment");
        {
            tok_result_t r = emit_comment_token(t);
            t->state = ROBO_HTML_TOKENIZER_STATE_DATA;
            return r;
        }
    }
    c = robo_html_tok_peek(t);
    if (c == '-') {
        robo_html_tok_advance(t);
        t->state = ROBO_HTML_TOKENIZER_STATE_COMMENT_START_DASH;
        return TOK_CONTINUE;
    }
    if (c == '>') {
        diag(t, ROBO_PARSE_ERROR, "abrupt closing of empty comment");
        robo_html_tok_advance(t);
        {
            tok_result_t r = emit_comment_token(t);
            t->state = ROBO_HTML_TOKENIZER_STATE_DATA;
            return r;
        }
    }
    t->state = ROBO_HTML_TOKENIZER_STATE_COMMENT;
    return TOK_CONTINUE;
}

static tok_result_t st_comment_start_dash(robo_html_tokenizer_t *t)
{
    char c;

    if (t->pos >= t->length) {
        diag(t, ROBO_PARSE_ERROR, "EOF in comment");
        {
            tok_result_t r = emit_comment_token(t);
            t->state = ROBO_HTML_TOKENIZER_STATE_DATA;
            return r;
        }
    }
    c = robo_html_tok_peek(t);
    if (c == '-') {
        robo_html_tok_advance(t);
        t->state = ROBO_HTML_TOKENIZER_STATE_COMMENT_END;
        return TOK_CONTINUE;
    }
    if (c == '>') {
        diag(t, ROBO_PARSE_ERROR, "abrupt closing of empty comment");
        robo_html_tok_advance(t);
        {
            tok_result_t r = emit_comment_token(t);
            t->state = ROBO_HTML_TOKENIZER_STATE_DATA;
            return r;
        }
    }
    (void)robo_html_token_buffer_append_byte(&t->text, '-');
    t->state = ROBO_HTML_TOKENIZER_STATE_COMMENT;
    return TOK_CONTINUE;
}

static tok_result_t st_comment_body(robo_html_tokenizer_t *t)
{
    char c;

    if (t->pos >= t->length) {
        diag(t, ROBO_PARSE_ERROR, "EOF in comment");
        {
            tok_result_t r = emit_comment_token(t);
            t->state = ROBO_HTML_TOKENIZER_STATE_DATA;
            return r;
        }
    }
    c = robo_html_tok_peek(t);
    if (c == '<') {
        (void)robo_html_token_buffer_append_byte(&t->text, '<');
        robo_html_tok_advance(t);
        t->state = ROBO_HTML_TOKENIZER_STATE_COMMENT_LESS_THAN;
        return TOK_CONTINUE;
    }
    if (c == '-') {
        robo_html_tok_advance(t);
        t->state = ROBO_HTML_TOKENIZER_STATE_COMMENT_END_DASH;
        return TOK_CONTINUE;
    }
    if (c == '\0') {
        (void)robo_html_token_buffer_append_utf8(&t->text, 0xFFFDu);
        diag(t, ROBO_PARSE_ERROR, "NUL in comment");
        robo_html_tok_advance(t);
        return TOK_CONTINUE;
    }
    (void)robo_html_token_buffer_append_byte(&t->text, c);
    robo_html_tok_advance(t);
    return TOK_CONTINUE;
}

static tok_result_t st_comment_less_than(robo_html_tokenizer_t *t)
{
    char c = robo_html_tok_peek(t);

    if (c == '!' || c == '/' || c == '<') {
        (void)robo_html_token_buffer_append_byte(&t->text, c);
        robo_html_tok_advance(t);
        if (c == '!') {
            t->state = ROBO_HTML_TOKENIZER_STATE_COMMENT_LESS_THAN_BANG;
        }
        return TOK_CONTINUE;
    }
    t->state = ROBO_HTML_TOKENIZER_STATE_COMMENT;
    return TOK_CONTINUE;
}

static tok_result_t st_comment_less_than_bang(robo_html_tokenizer_t *t)
{
    if (robo_html_tok_peek(t) == '-' && robo_html_tok_peek_ahead(t, 1) == '-') {
        robo_html_tok_advance(t);
        robo_html_tok_advance(t);
        t->state = ROBO_HTML_TOKENIZER_STATE_COMMENT_LESS_THAN_BANG_DASHES;
        return TOK_CONTINUE;
    }
    t->state = ROBO_HTML_TOKENIZER_STATE_COMMENT;
    return TOK_CONTINUE;
}

static tok_result_t st_comment_less_than_bang_dashes(robo_html_tokenizer_t *t)
{
    char c = robo_html_tok_peek(t);

    if (c == '-' || c == '>') {
        t->state = ROBO_HTML_TOKENIZER_STATE_COMMENT_END;
        return TOK_CONTINUE;
    }
    t->state = ROBO_HTML_TOKENIZER_STATE_COMMENT;
    return TOK_CONTINUE;
}

static tok_result_t st_comment_end_dash(robo_html_tokenizer_t *t)
{
    char c;

    if (t->pos >= t->length) {
        diag(t, ROBO_PARSE_ERROR, "EOF in comment");
        {
            tok_result_t r = emit_comment_token(t);
            t->state = ROBO_HTML_TOKENIZER_STATE_DATA;
            return r;
        }
    }
    c = robo_html_tok_peek(t);
    if (c == '-') {
        robo_html_tok_advance(t);
        t->state = ROBO_HTML_TOKENIZER_STATE_COMMENT_END;
        return TOK_CONTINUE;
    }
    (void)robo_html_token_buffer_append_byte(&t->text, '-');
    t->state = ROBO_HTML_TOKENIZER_STATE_COMMENT;
    return TOK_CONTINUE;
}

static tok_result_t st_comment_end(robo_html_tokenizer_t *t)
{
    char c;

    if (t->pos >= t->length) {
        diag(t, ROBO_PARSE_ERROR, "EOF in comment");
        {
            tok_result_t r = emit_comment_token(t);
            t->state = ROBO_HTML_TOKENIZER_STATE_DATA;
            return r;
        }
    }
    c = robo_html_tok_peek(t);
    if (c == '>') {
        robo_html_tok_advance(t);
        {
            tok_result_t r = emit_comment_token(t);
            t->state = ROBO_HTML_TOKENIZER_STATE_DATA;
            return r;
        }
    }
    if (c == '!') {
        robo_html_tok_advance(t);
        t->state = ROBO_HTML_TOKENIZER_STATE_COMMENT_END_BANG;
        return TOK_CONTINUE;
    }
    if (c == '-') {
        (void)robo_html_token_buffer_append_byte(&t->text, '-');
        robo_html_tok_advance(t);
        return TOK_CONTINUE;
    }
    (void)robo_html_token_buffer_append_byte(&t->text, '-');
    (void)robo_html_token_buffer_append_byte(&t->text, '-');
    t->state = ROBO_HTML_TOKENIZER_STATE_COMMENT;
    return TOK_CONTINUE;
}

static tok_result_t st_comment_end_bang(robo_html_tokenizer_t *t)
{
    char c;

    if (t->pos >= t->length) {
        diag(t, ROBO_PARSE_ERROR, "EOF in comment");
        {
            tok_result_t r = emit_comment_token(t);
            t->state = ROBO_HTML_TOKENIZER_STATE_DATA;
            return r;
        }
    }
    c = robo_html_tok_peek(t);
    if (c == '-') {
        (void)robo_html_token_buffer_append_byte(&t->text, '-');
        (void)robo_html_token_buffer_append_byte(&t->text, '-');
        (void)robo_html_token_buffer_append_byte(&t->text, '!');
        robo_html_tok_advance(t);
        t->state = ROBO_HTML_TOKENIZER_STATE_COMMENT_END_DASH;
        return TOK_CONTINUE;
    }
    if (c == '>') {
        diag(t, ROBO_PARSE_ERROR, "incorrectly closed comment");
        robo_html_tok_advance(t);
        {
            tok_result_t r = emit_comment_token(t);
            t->state = ROBO_HTML_TOKENIZER_STATE_DATA;
            return r;
        }
    }
    (void)robo_html_token_buffer_append_byte(&t->text, '-');
    (void)robo_html_token_buffer_append_byte(&t->text, '-');
    (void)robo_html_token_buffer_append_byte(&t->text, '!');
    t->state = ROBO_HTML_TOKENIZER_STATE_COMMENT;
    return TOK_CONTINUE;
}

static tok_result_t st_bogus_comment(robo_html_tokenizer_t *t)
{
    char c;

    if (t->pos >= t->length) {
        {
            tok_result_t r = emit_comment_token(t);
            t->state = ROBO_HTML_TOKENIZER_STATE_DATA;
            return r;
        }
    }
    c = robo_html_tok_peek(t);
    if (c == '>') {
        robo_html_tok_advance(t);
        {
            tok_result_t r = emit_comment_token(t);
            t->state = ROBO_HTML_TOKENIZER_STATE_DATA;
            return r;
        }
    }
    if (c == '\0') {
        (void)robo_html_token_buffer_append_utf8(&t->text, 0xFFFDu);
        robo_html_tok_advance(t);
        return TOK_CONTINUE;
    }
    (void)robo_html_token_buffer_append_byte(&t->text, c);
    robo_html_tok_advance(t);
    return TOK_CONTINUE;
}

/* ------------------------------------------------------------------ */
/* Doctype states                                                      */
/* ------------------------------------------------------------------ */

static tok_result_t emit_doctype_token(robo_html_tokenizer_t *t)
{
    token_begin(t, ROBO_HTML_TOKEN_DOCTYPE);
    t->token.u.doctype.name = t->text.data ? t->text.data : "";
    t->token.u.doctype.public_id =
        t->doctype_has_public ? (t->text2.data ? t->text2.data : "") : NULL;
    t->token.u.doctype.system_id =
        t->doctype_has_system ? (t->text3.data ? t->text3.data : "") : NULL;
    t->token.u.doctype.force_quirks = t->doctype_force_quirks;
    token_finish(t);
    return TOK_EMIT;
}

static tok_result_t st_before_doctype_name(robo_html_tokenizer_t *t)
{
    char c;

    if (t->pos >= t->length) {
        diag(t, ROBO_PARSE_ERROR, "EOF in DOCTYPE");
        t->doctype_force_quirks = 1;
        {
            tok_result_t r = emit_doctype_token(t);
            t->state = ROBO_HTML_TOKENIZER_STATE_DATA;
            return r;
        }
    }
    c = robo_html_tok_peek(t);
    if (is_ws(c)) {
        robo_html_tok_advance(t);
        return TOK_CONTINUE;
    }
    if (c == '>') {
        diag(t, ROBO_PARSE_ERROR, "DOCTYPE without name");
        robo_html_tok_advance(t);
        t->doctype_force_quirks = 1;
        {
            tok_result_t r = emit_doctype_token(t);
            t->state = ROBO_HTML_TOKENIZER_STATE_DATA;
            return r;
        }
    }
    t->state = ROBO_HTML_TOKENIZER_STATE_DOCTYPE_NAME;
    return TOK_CONTINUE; /* name handler consumes */
}

static tok_result_t st_doctype_name(robo_html_tokenizer_t *t)
{
    char c;

    if (t->pos >= t->length) {
        diag(t, ROBO_PARSE_ERROR, "EOF in DOCTYPE");
        t->doctype_force_quirks = 1;
        {
            tok_result_t r = emit_doctype_token(t);
            t->state = ROBO_HTML_TOKENIZER_STATE_DATA;
            return r;
        }
    }
    c = robo_html_tok_peek(t);
    if (is_ws(c)) {
        robo_html_tok_advance(t);
        t->state = ROBO_HTML_TOKENIZER_STATE_AFTER_DOCTYPE_NAME;
        return TOK_CONTINUE;
    }
    if (c == '>') {
        robo_html_tok_advance(t);
        {
            tok_result_t r = emit_doctype_token(t);
            t->state = ROBO_HTML_TOKENIZER_STATE_DATA;
            return r;
        }
    }
    if (c == '\0') {
        (void)robo_html_token_buffer_append_utf8(&t->text, 0xFFFDu);
        diag(t, ROBO_PARSE_ERROR, "NUL in DOCTYPE name");
        robo_html_tok_advance(t);
        return TOK_CONTINUE;
    }
    (void)robo_html_token_buffer_append_byte(&t->text,
                                             robo_html_tok_ascii_lower(c));
    robo_html_tok_advance(t);
    return TOK_CONTINUE;
}

static tok_result_t st_doctype_after_name(robo_html_tokenizer_t *t)
{
    char c;

    if (t->pos >= t->length) {
        diag(t, ROBO_PARSE_ERROR, "EOF in DOCTYPE");
        t->doctype_force_quirks = 1;
        {
            tok_result_t r = emit_doctype_token(t);
            t->state = ROBO_HTML_TOKENIZER_STATE_DATA;
            return r;
        }
    }
    c = robo_html_tok_peek(t);
    if (is_ws(c)) {
        robo_html_tok_advance(t);
        return TOK_CONTINUE;
    }
    if (c == '>') {
        robo_html_tok_advance(t);
        {
            tok_result_t r = emit_doctype_token(t);
            t->state = ROBO_HTML_TOKENIZER_STATE_DATA;
            return r;
        }
    }
    if (robo_html_tok_word_at(t, "public")) {
        size_t i;
        for (i = 0u; i < 6u; i++) {
            robo_html_tok_advance(t);
        }
        t->state = ROBO_HTML_TOKENIZER_STATE_AFTER_DOCTYPE_PUBLIC_KEYWORD;
        return TOK_CONTINUE;
    }
    if (robo_html_tok_word_at(t, "system")) {
        size_t i;
        for (i = 0u; i < 6u; i++) {
            robo_html_tok_advance(t);
        }
        t->state = ROBO_HTML_TOKENIZER_STATE_AFTER_DOCTYPE_SYSTEM_KEYWORD;
        return TOK_CONTINUE;
    }
    diag(t, ROBO_PARSE_ERROR, "invalid DOCTYPE content; bogus doctype");
    t->doctype_force_quirks = 1;
    t->state = ROBO_HTML_TOKENIZER_STATE_BOGUS_DOCTYPE;
    return TOK_CONTINUE;
}

static tok_result_t st_doctype_public_keyword(robo_html_tokenizer_t *t)
{
    char c;

    if (t->pos >= t->length) {
        diag(t, ROBO_PARSE_ERROR, "EOF in DOCTYPE");
        t->doctype_force_quirks = 1;
        {
            tok_result_t r = emit_doctype_token(t);
            t->state = ROBO_HTML_TOKENIZER_STATE_DATA;
            return r;
        }
    }
    c = robo_html_tok_peek(t);
    if (is_ws(c)) {
        robo_html_tok_advance(t);
        t->state = ROBO_HTML_TOKENIZER_STATE_BEFORE_DOCTYPE_PUBLIC_IDENTIFIER;
        return TOK_CONTINUE;
    }
    if (c == '"') {
        diag(t, ROBO_PARSE_ERROR, "unquoted DOCTYPE public identifier");
        robo_html_tok_advance(t);
        t->doctype_has_public = 1;
        robo_html_token_buffer_clear(&t->text2);
        t->state = ROBO_HTML_TOKENIZER_STATE_DOCTYPE_PUBLIC_IDENTIFIER_DOUBLE_QUOTED;
        return TOK_CONTINUE;
    }
    if (c == '\'') {
        diag(t, ROBO_PARSE_ERROR, "unquoted DOCTYPE public identifier");
        robo_html_tok_advance(t);
        t->doctype_has_public = 1;
        robo_html_token_buffer_clear(&t->text2);
        t->state = ROBO_HTML_TOKENIZER_STATE_DOCTYPE_PUBLIC_IDENTIFIER_SINGLE_QUOTED;
        return TOK_CONTINUE;
    }
    diag(t, ROBO_PARSE_ERROR, "missing quote after PUBLIC; bogus doctype");
    t->doctype_force_quirks = 1;
    t->state = ROBO_HTML_TOKENIZER_STATE_BOGUS_DOCTYPE;
    return TOK_CONTINUE;
}

/**
 * Before-DOCTYPE-public-identifier / before-DOCTYPE-system-identifier.
 *
 * These states exist to consume the opening quote; the quoted states that
 * follow copy only the identifier payload. Keeping them distinct is what
 * makes `doctype_has_public` / `doctype_has_system` true (and the buffers
 * empty at the quote) instead of swallowing the quote into the value.
 */
static tok_result_t st_before_doctype_public_id(robo_html_tokenizer_t *t)
{
    char c;

    if (t->pos >= t->length) {
        diag(t, ROBO_PARSE_ERROR, "EOF in DOCTYPE");
        t->doctype_force_quirks = 1;
        {
            tok_result_t r = emit_doctype_token(t);
            t->state = ROBO_HTML_TOKENIZER_STATE_DATA;
            return r;
        }
    }
    c = robo_html_tok_peek(t);
    if (is_ws(c)) {
        robo_html_tok_advance(t);
        return TOK_CONTINUE;
    }
    if (c == '"' || c == '\'') {
        robo_html_tok_advance(t);
        t->doctype_has_public = 1;
        t->doctype_force_quirks = 0;
        robo_html_token_buffer_clear(&t->text2);
        t->state = (c == '"')
                       ? ROBO_HTML_TOKENIZER_STATE_DOCTYPE_PUBLIC_IDENTIFIER_DOUBLE_QUOTED
                       : ROBO_HTML_TOKENIZER_STATE_DOCTYPE_PUBLIC_IDENTIFIER_SINGLE_QUOTED;
        return TOK_CONTINUE;
    }
    if (c == '>') {
        diag(t, ROBO_PARSE_ERROR, "'>' before DOCTYPE public identifier");
        t->doctype_force_quirks = 1;
        robo_html_tok_advance(t);
        {
            tok_result_t r = emit_doctype_token(t);
            t->state = ROBO_HTML_TOKENIZER_STATE_DATA;
            return r;
        }
    }
    diag(t, ROBO_PARSE_ERROR, "invalid DOCTYPE public identifier; bogus doctype");
    t->doctype_force_quirks = 1;
    t->state = ROBO_HTML_TOKENIZER_STATE_BOGUS_DOCTYPE;
    return TOK_CONTINUE;
}

static tok_result_t st_before_doctype_system_id(robo_html_tokenizer_t *t)
{
    char c;

    if (t->pos >= t->length) {
        diag(t, ROBO_PARSE_ERROR, "EOF in DOCTYPE");
        t->doctype_force_quirks = 1;
        {
            tok_result_t r = emit_doctype_token(t);
            t->state = ROBO_HTML_TOKENIZER_STATE_DATA;
            return r;
        }
    }
    c = robo_html_tok_peek(t);
    if (is_ws(c)) {
        robo_html_tok_advance(t);
        return TOK_CONTINUE;
    }
    if (c == '"' || c == '\'') {
        robo_html_tok_advance(t);
        t->doctype_has_system = 1;
        t->doctype_force_quirks = 0;
        robo_html_token_buffer_clear(&t->text3);
        t->state = (c == '"')
                       ? ROBO_HTML_TOKENIZER_STATE_DOCTYPE_SYSTEM_IDENTIFIER_DOUBLE_QUOTED
                       : ROBO_HTML_TOKENIZER_STATE_DOCTYPE_SYSTEM_IDENTIFIER_SINGLE_QUOTED;
        return TOK_CONTINUE;
    }
    if (c == '>') {
        diag(t, ROBO_PARSE_ERROR, "'>' before DOCTYPE system identifier");
        t->doctype_force_quirks = 1;
        robo_html_tok_advance(t);
        {
            tok_result_t r = emit_doctype_token(t);
            t->state = ROBO_HTML_TOKENIZER_STATE_DATA;
            return r;
        }
    }
    diag(t, ROBO_PARSE_ERROR, "invalid DOCTYPE system identifier; bogus doctype");
    t->state = ROBO_HTML_TOKENIZER_STATE_BOGUS_DOCTYPE;
    return TOK_CONTINUE;
}

static tok_result_t st_doctype_public_id_quoted(robo_html_tokenizer_t *t,
                                                char quote)
{
    char c;

    if (t->pos >= t->length) {
        diag(t, ROBO_PARSE_ERROR, "EOF in DOCTYPE public identifier");
        t->doctype_force_quirks = 1;
        {
            tok_result_t r = emit_doctype_token(t);
            t->state = ROBO_HTML_TOKENIZER_STATE_DATA;
            return r;
        }
    }
    c = robo_html_tok_peek(t);
    if (c == quote) {
        robo_html_tok_advance(t);
        t->state =
            ROBO_HTML_TOKENIZER_STATE_BETWEEN_DOCTYPE_PUBLIC_AND_SYSTEM_IDENTIFIERS;
        return TOK_CONTINUE;
    }
    if (c == '\0') {
        (void)robo_html_token_buffer_append_utf8(&t->text2, 0xFFFDu);
        diag(t, ROBO_PARSE_ERROR, "NUL in DOCTYPE public identifier");
        robo_html_tok_advance(t);
        return TOK_CONTINUE;
    }
    if (c == '>') {
        diag(t, ROBO_PARSE_ERROR, "'>' in DOCTYPE public identifier");
        t->doctype_force_quirks = 1;
        robo_html_tok_advance(t);
        {
            tok_result_t r = emit_doctype_token(t);
            t->state = ROBO_HTML_TOKENIZER_STATE_DATA;
            return r;
        }
    }
    (void)robo_html_token_buffer_append_byte(&t->text2, c);
    robo_html_tok_advance(t);
    return TOK_CONTINUE;
}

static tok_result_t st_between_public_system(robo_html_tokenizer_t *t)
{
    char c;

    if (t->pos >= t->length) {
        diag(t, ROBO_PARSE_ERROR, "EOF in DOCTYPE");
        t->doctype_force_quirks = 1;
        {
            tok_result_t r = emit_doctype_token(t);
            t->state = ROBO_HTML_TOKENIZER_STATE_DATA;
            return r;
        }
    }
    c = robo_html_tok_peek(t);
    if (is_ws(c)) {
        robo_html_tok_advance(t);
        t->state = ROBO_HTML_TOKENIZER_STATE_BEFORE_DOCTYPE_SYSTEM_IDENTIFIER;
        return TOK_CONTINUE;
    }
    if (c == '>') {
        robo_html_tok_advance(t);
        {
            tok_result_t r = emit_doctype_token(t);
            t->state = ROBO_HTML_TOKENIZER_STATE_DATA;
            return r;
        }
    }
    if (c == '"') {
        robo_html_tok_advance(t);
        t->doctype_has_system = 1;
        t->state = ROBO_HTML_TOKENIZER_STATE_DOCTYPE_SYSTEM_IDENTIFIER_DOUBLE_QUOTED;
        return TOK_CONTINUE;
    }
    if (c == '\'') {
        robo_html_tok_advance(t);
        t->doctype_has_system = 1;
        t->state = ROBO_HTML_TOKENIZER_STATE_DOCTYPE_SYSTEM_IDENTIFIER_SINGLE_QUOTED;
        return TOK_CONTINUE;
    }
    diag(t, ROBO_PARSE_ERROR, "missing quote after DOCTYPE public id");
    t->doctype_force_quirks = 1;
    t->state = ROBO_HTML_TOKENIZER_STATE_BOGUS_DOCTYPE;
    return TOK_CONTINUE;
}

static tok_result_t st_doctype_system_keyword(robo_html_tokenizer_t *t)
{
    char c;

    if (t->pos >= t->length) {
        diag(t, ROBO_PARSE_ERROR, "EOF in DOCTYPE");
        t->doctype_force_quirks = 1;
        {
            tok_result_t r = emit_doctype_token(t);
            t->state = ROBO_HTML_TOKENIZER_STATE_DATA;
            return r;
        }
    }
    c = robo_html_tok_peek(t);
    if (is_ws(c)) {
        robo_html_tok_advance(t);
        t->state = ROBO_HTML_TOKENIZER_STATE_BEFORE_DOCTYPE_SYSTEM_IDENTIFIER;
        return TOK_CONTINUE;
    }
    if (c == '"') {
        robo_html_tok_advance(t);
        t->doctype_has_system = 1;
        t->state = ROBO_HTML_TOKENIZER_STATE_DOCTYPE_SYSTEM_IDENTIFIER_DOUBLE_QUOTED;
        return TOK_CONTINUE;
    }
    if (c == '\'') {
        robo_html_tok_advance(t);
        t->doctype_has_system = 1;
        t->state = ROBO_HTML_TOKENIZER_STATE_DOCTYPE_SYSTEM_IDENTIFIER_SINGLE_QUOTED;
        return TOK_CONTINUE;
    }
    diag(t, ROBO_PARSE_ERROR, "missing quote after SYSTEM; bogus doctype");
    t->doctype_force_quirks = 1;
    t->state = ROBO_HTML_TOKENIZER_STATE_BOGUS_DOCTYPE;
    return TOK_CONTINUE;
}

static tok_result_t st_doctype_system_id_quoted(robo_html_tokenizer_t *t,
                                                char quote)
{
    char c;

    if (t->pos >= t->length) {
        diag(t, ROBO_PARSE_ERROR, "EOF in DOCTYPE system identifier");
        t->doctype_force_quirks = 1;
        {
            tok_result_t r = emit_doctype_token(t);
            t->state = ROBO_HTML_TOKENIZER_STATE_DATA;
            return r;
        }
    }
    c = robo_html_tok_peek(t);
    if (c == quote) {
        robo_html_tok_advance(t);
        t->state = ROBO_HTML_TOKENIZER_STATE_AFTER_DOCTYPE_SYSTEM_IDENTIFIER;
        return TOK_CONTINUE;
    }
    if (c == '\0') {
        (void)robo_html_token_buffer_append_utf8(&t->text3, 0xFFFDu);
        diag(t, ROBO_PARSE_ERROR, "NUL in DOCTYPE system identifier");
        robo_html_tok_advance(t);
        return TOK_CONTINUE;
    }
    if (c == '>') {
        diag(t, ROBO_PARSE_ERROR, "'>' in DOCTYPE system identifier");
        t->doctype_force_quirks = 1;
        robo_html_tok_advance(t);
        {
            tok_result_t r = emit_doctype_token(t);
            t->state = ROBO_HTML_TOKENIZER_STATE_DATA;
            return r;
        }
    }
    (void)robo_html_token_buffer_append_byte(&t->text3, c);
    robo_html_tok_advance(t);
    return TOK_CONTINUE;
}

static tok_result_t st_after_doctype_system_id(robo_html_tokenizer_t *t)
{
    char c;

    if (t->pos >= t->length) {
        diag(t, ROBO_PARSE_ERROR, "EOF in DOCTYPE");
        t->doctype_force_quirks = 1;
        {
            tok_result_t r = emit_doctype_token(t);
            t->state = ROBO_HTML_TOKENIZER_STATE_DATA;
            return r;
        }
    }
    c = robo_html_tok_peek(t);
    if (is_ws(c)) {
        robo_html_tok_advance(t);
        return TOK_CONTINUE;
    }
    if (c == '>') {
        robo_html_tok_advance(t);
        {
            tok_result_t r = emit_doctype_token(t);
            t->state = ROBO_HTML_TOKENIZER_STATE_DATA;
            return r;
        }
    }
    diag(t, ROBO_PARSE_ERROR, "unexpected content after DOCTYPE system id");
    t->state = ROBO_HTML_TOKENIZER_STATE_BOGUS_DOCTYPE;
    return TOK_CONTINUE;
}

static tok_result_t st_bogus_doctype(robo_html_tokenizer_t *t)
{
    char c;

    if (t->pos >= t->length) {
        {
            tok_result_t r = emit_doctype_token(t);
            t->state = ROBO_HTML_TOKENIZER_STATE_DATA;
            return r;
        }
    }
    c = robo_html_tok_peek(t);
    if (c == '>') {
        robo_html_tok_advance(t);
        {
            tok_result_t r = emit_doctype_token(t);
            t->state = ROBO_HTML_TOKENIZER_STATE_DATA;
            return r;
        }
    }
    if (c == '\0') {
        diag(t, ROBO_PARSE_ERROR, "NUL in bogus DOCTYPE");
    }
    robo_html_tok_advance(t);
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
 * The expected end-tag name comes from raw_end_name (seeded by
 * switch_state from the last emitted start tag). When no name is known:
 *   - a leading `<name>` start tag is consumed to learn the name;
 *   - otherwise the region closes at the LAST `</name`-shaped construct
 *     terminated by ws, '/', or '>' — earlier look-alikes stay literal
 *     text (context-less switch contract; `</x` at EOF never closes).
 * PLAINTEXT never matches end tags. RCDATA decodes character references;
 * the other raw families keep bytes literal.
 */
static tok_result_t st_rawtext_like(robo_html_tokenizer_t *t)
{
    robo_html_tokenizer_state_t base = t->state;
    int is_rcdata = (base == ROBO_HTML_TOKENIZER_STATE_RCDATA);
    int is_plaintext = (base == ROBO_HTML_TOKENIZER_STATE_PLAINTEXT);
    const char *end_name = t->raw_end_name.data ? t->raw_end_name.data : "";
    size_t name_len = t->raw_end_name.length;
    robo_html_token_buffer_t run;
    size_t cand_src = 0u;
    size_t cand_run_mark = 0u;
    int have_candidate = 0;

    robo_html_token_buffer_init(&run);
    snapshot_pos(t);

    /* Context-less raw region: learn the end name from a leading tag. */
    if (!is_plaintext && name_len == 0u && t->pos + 1u < t->length &&
        t->bytes[t->pos] == '<' && is_ascii_letter(t->bytes[t->pos + 1u])) {
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

                if (is_ws(ch)) {
                    break; /* the name ends at the first separator */
                }
                (void)robo_html_token_buffer_append_byte(
                    &t->raw_end_name, robo_html_tok_ascii_lower(ch));
            }
            while (t->pos <= p) {
                robo_html_tok_advance(t);
            }
            end_name = t->raw_end_name.data ? t->raw_end_name.data : "";
            name_len = t->raw_end_name.length;
        }
    }

    while (t->pos < t->length) {
        char c = robo_html_tok_peek(t);

        if (c == '<' && !is_plaintext) {
            /* Candidate end tag: "</" + letters + (ws, '/', or '>'). */
            size_t after_lt = t->pos + 1u;

            if (after_lt < t->length && t->bytes[after_lt] == '/') {
                size_t p = after_lt + 1u;
                size_t nlen = 0u;

                while (p + nlen < t->length &&
                       is_ascii_letter(t->bytes[p + nlen])) {
                    nlen++;
                }
                if (nlen > 0u && p + nlen < t->length) {
                    char term = t->bytes[p + nlen];

                    if (term == '>' || term == '/' || is_ws(term)) {
                        if (name_len > 0u) {
                            /* Known context: exact (case-insensitive) name. */
                            if (nlen == name_len &&
                                robo_ascii_casecmp_bytes(t->bytes + p,
                                                         end_name,
                                                         name_len)) {
                                break; /* close here; tag machinery follows */
                            }
                        } else {
                            /* No known context: close at the LAST
                             * candidate; remember this one and keep the
                             * bytes as literal text for now. */
                            have_candidate = 1;
                            cand_src = t->pos;
                            cand_run_mark = run.length;
                        }
                    }
                }
            }
        }
        if (c == '&') {
            if (is_rcdata) {
                consume_charref(t, &run, 0);
            } else {
                (void)robo_html_token_buffer_append_byte(&run, '&');
                robo_html_tok_advance(t);
            }
            continue;
        }
        if (c == '\r') {
            (void)robo_html_token_buffer_append_byte(&run, '\n');
            robo_html_tok_advance(t);
            if (robo_html_tok_peek(t) == '\n') {
                robo_html_tok_advance(t);
            }
            continue;
        }
        if (c == '\0') {
            (void)robo_html_token_buffer_append_utf8(&run, 0xFFFDu);
            robo_html_tok_advance(t);
            continue;
        }
        {
            uint32_t cp = 0u;
            size_t next = 0u;
            robo_unicode_status_t st = robo_unicode_utf8_decode(
                t->bytes, t->length, t->pos, ROBO_UNICODE_REPLACE, &cp,
                &next);
            if (st == ROBO_UNICODE_OK || st == ROBO_UNICODE_INVALID ||
                st == ROBO_UNICODE_TRUNCATED) {
                (void)robo_html_token_buffer_append_utf8(&run, cp);
                while (t->pos < next) {
                    robo_html_tok_advance(t);
                }
                continue;
            }
        }
        (void)robo_html_token_buffer_append_byte(&run, c);
        robo_html_tok_advance(t);
    }

    /* End of region without a known-context match. */
    if (have_candidate) {
        /* No known context: close at the last candidate seen. */
        t->pos = cand_src;
        snapshot_pos(t);
        run.length = cand_run_mark;
        if (run.data) {
            run.data[run.length] = '\0';
        }
    }
    if (run.length > 0u) {
        tok_result_t r = emit_char_token(t, &run); /* steals the run */

        t->state = ROBO_HTML_TOKENIZER_STATE_DATA;
        return r;
    }
    robo_html_token_buffer_free(&run);
    if (t->pos < t->length) {
        /* Positioned on the matching end tag: let the normal tag
         * machinery parse and emit it. */
        t->state = ROBO_HTML_TOKENIZER_STATE_DATA;
        return TOK_CONTINUE;
    }
    return emit_eof_token(t);
}

/* ------------------------------------------------------------------ */
/* Dispatch + public API                                               */
/* ------------------------------------------------------------------ */

static tok_result_t step(robo_html_tokenizer_t *t)
{
    switch (t->state) {
    case ROBO_HTML_TOKENIZER_STATE_DATA:
        return st_data(t);
    case ROBO_HTML_TOKENIZER_STATE_TAG_OPEN:
        return st_tag_open(t);
    case ROBO_HTML_TOKENIZER_STATE_END_TAG_OPEN:
        return st_end_tag_open(t);
    case ROBO_HTML_TOKENIZER_STATE_TAG_NAME:
        return st_tag_name(t);
    case ROBO_HTML_TOKENIZER_STATE_BEFORE_ATTRIBUTE_NAME:
        return st_before_attr_name(t);
    case ROBO_HTML_TOKENIZER_STATE_ATTRIBUTE_NAME:
        return st_attr_name(t);
    case ROBO_HTML_TOKENIZER_STATE_AFTER_ATTRIBUTE_NAME:
        return st_after_attr_name(t);
    case ROBO_HTML_TOKENIZER_STATE_BEFORE_ATTRIBUTE_VALUE:
        return st_before_attr_value(t);
    case ROBO_HTML_TOKENIZER_STATE_ATTRIBUTE_VALUE_DOUBLE_QUOTED:
        return st_attr_value_quoted(t, '"');
    case ROBO_HTML_TOKENIZER_STATE_ATTRIBUTE_VALUE_SINGLE_QUOTED:
        return st_attr_value_quoted(t, '\'');
    case ROBO_HTML_TOKENIZER_STATE_ATTRIBUTE_VALUE_UNQUOTED:
        return st_attr_value_unquoted(t);
    case ROBO_HTML_TOKENIZER_STATE_AFTER_ATTRIBUTE_VALUE_QUOTED:
        return st_after_attr_value_quoted(t);
    case ROBO_HTML_TOKENIZER_STATE_SELF_CLOSING_START_TAG:
        return st_self_closing_start_tag(t);
    case ROBO_HTML_TOKENIZER_STATE_MARKUP_DECLARATION_OPEN:
        return st_markup_declaration_open(t);
    case ROBO_HTML_TOKENIZER_STATE_BOGUS_COMMENT:
        return st_bogus_comment(t);
    case ROBO_HTML_TOKENIZER_STATE_COMMENT_START:
        return st_comment_start(t);
    case ROBO_HTML_TOKENIZER_STATE_COMMENT_START_DASH:
        return st_comment_start_dash(t);
    case ROBO_HTML_TOKENIZER_STATE_COMMENT:
    case ROBO_HTML_TOKENIZER_STATE_COMMENT_LESS_THAN:
    case ROBO_HTML_TOKENIZER_STATE_COMMENT_LESS_THAN_BANG:
    case ROBO_HTML_TOKENIZER_STATE_COMMENT_LESS_THAN_BANG_DASHES:
    case ROBO_HTML_TOKENIZER_STATE_COMMENT_END_DASH:
    case ROBO_HTML_TOKENIZER_STATE_COMMENT_END:
    case ROBO_HTML_TOKENIZER_STATE_COMMENT_END_BANG:
        /* Comment states dispatch to the shared body engine with their
         * individual transitions handled inside the comment functions
         * (states are kept explicit for inspection/testing). */
        if (t->state == ROBO_HTML_TOKENIZER_STATE_COMMENT) {
            return st_comment_body(t);
        }
        if (t->state == ROBO_HTML_TOKENIZER_STATE_COMMENT_LESS_THAN) {
            return st_comment_less_than(t);
        }
        if (t->state == ROBO_HTML_TOKENIZER_STATE_COMMENT_LESS_THAN_BANG) {
            return st_comment_less_than_bang(t);
        }
        if (t->state ==
            ROBO_HTML_TOKENIZER_STATE_COMMENT_LESS_THAN_BANG_DASHES) {
            return st_comment_less_than_bang_dashes(t);
        }
        if (t->state == ROBO_HTML_TOKENIZER_STATE_COMMENT_END_DASH) {
            return st_comment_end_dash(t);
        }
        if (t->state == ROBO_HTML_TOKENIZER_STATE_COMMENT_END) {
            return st_comment_end(t);
        }
        return st_comment_end_bang(t);
    case ROBO_HTML_TOKENIZER_STATE_DOCTYPE:
    case ROBO_HTML_TOKENIZER_STATE_BEFORE_DOCTYPE_NAME:
        return st_before_doctype_name(t);
    case ROBO_HTML_TOKENIZER_STATE_DOCTYPE_NAME:
        return st_doctype_name(t);
    case ROBO_HTML_TOKENIZER_STATE_AFTER_DOCTYPE_NAME:
        return st_doctype_after_name(t);
    case ROBO_HTML_TOKENIZER_STATE_BOGUS_DOCTYPE:
    case ROBO_HTML_TOKENIZER_STATE_BOGUS_DOCTYPE_QUIRKS:
        return st_bogus_doctype(t);
    case ROBO_HTML_TOKENIZER_STATE_AFTER_DOCTYPE_PUBLIC_KEYWORD:
        return st_doctype_public_keyword(t);
    case ROBO_HTML_TOKENIZER_STATE_BEFORE_DOCTYPE_PUBLIC_IDENTIFIER:
        return st_before_doctype_public_id(t);
    case ROBO_HTML_TOKENIZER_STATE_DOCTYPE_PUBLIC_IDENTIFIER_DOUBLE_QUOTED:
        return st_doctype_public_id_quoted(t, '"');
    case ROBO_HTML_TOKENIZER_STATE_DOCTYPE_PUBLIC_IDENTIFIER_SINGLE_QUOTED:
        return st_doctype_public_id_quoted(t, '\'');
    case ROBO_HTML_TOKENIZER_STATE_AFTER_DOCTYPE_PUBLIC_IDENTIFIER:
    case ROBO_HTML_TOKENIZER_STATE_BETWEEN_DOCTYPE_PUBLIC_AND_SYSTEM_IDENTIFIERS:
        return st_between_public_system(t);
    case ROBO_HTML_TOKENIZER_STATE_AFTER_DOCTYPE_SYSTEM_KEYWORD:
        return st_doctype_system_keyword(t);
    case ROBO_HTML_TOKENIZER_STATE_BEFORE_DOCTYPE_SYSTEM_IDENTIFIER:
        return st_before_doctype_system_id(t);
    case ROBO_HTML_TOKENIZER_STATE_DOCTYPE_SYSTEM_IDENTIFIER_DOUBLE_QUOTED:
        return st_doctype_system_id_quoted(t, '"');
    case ROBO_HTML_TOKENIZER_STATE_DOCTYPE_SYSTEM_IDENTIFIER_SINGLE_QUOTED:
        return st_doctype_system_id_quoted(t, '\'');
    case ROBO_HTML_TOKENIZER_STATE_AFTER_DOCTYPE_SYSTEM_IDENTIFIER:
        return st_after_doctype_system_id(t);

    case ROBO_HTML_TOKENIZER_STATE_CHARACTER_REFERENCE:
    case ROBO_HTML_TOKENIZER_STATE_CHARACTER_REFERENCE_NAMED:
    case ROBO_HTML_TOKENIZER_STATE_CHARACTER_REFERENCE_NUMERIC:
    case ROBO_HTML_TOKENIZER_STATE_CHARACTER_REFERENCE_NUMERIC_HEX:
    case ROBO_HTML_TOKENIZER_STATE_CHARACTER_REFERENCE_NUMERIC_END:
        /* Handled inline by consume_charref(); reaching the dispatcher
         * here means a state invariant broke — recover to DATA. */
        diag(t, ROBO_ERROR, "character-reference state leak; reset to DATA");
        t->state = ROBO_HTML_TOKENIZER_STATE_DATA;
        return TOK_CONTINUE;

    case ROBO_HTML_TOKENIZER_STATE_RAWTEXT:
    case ROBO_HTML_TOKENIZER_STATE_RCDATA:
    case ROBO_HTML_TOKENIZER_STATE_SCRIPT_DATA:
        return st_rawtext_like(t);
    default:
        /* PLAINTEXT and raw sub-states map onto the shared engine. */
        return st_rawtext_like(t);
    }
}

robo_result_t robo_html_tokenizer_create(const robo_html_source_t *src,
                                         robo_html_tokenizer_t **out,
                                         robo_html_diag_t *diag)
{
    robo_html_tokenizer_t *t;

    if (!out) {
        return ROBO_HTML_DIAG(diag, ROBO_HTML_DOMAIN_PARSER,
                              ROBO_INVALID_ARGUMENT, "out is NULL");
    }
    *out = NULL;
    if (!robo_html_is_initialized()) {
        return ROBO_HTML_DIAG(diag, ROBO_HTML_DOMAIN_PARSER, ROBO_INIT_ERROR,
                              "RoboHTML module not initialized");
    }
    if (!src || src->kind == ROBO_HTML_SOURCE_KIND_NONE) {
        return ROBO_HTML_DIAG(diag, ROBO_HTML_DOMAIN_SOURCE,
                              ROBO_INVALID_ARGUMENT, "source is NULL/none");
    }
    t = (robo_html_tokenizer_t *)robo_calloc(1u, sizeof(*t));
    if (!t) {
        return ROBO_HTML_DIAG(diag, ROBO_HTML_DOMAIN_PARSER,
                              ROBO_OUT_OF_MEMORY, "tokenizer allocation");
    }
    t->bytes = (const char *)(src->bytes ? (const void *)src->bytes
                                         : (const void *)"");
    t->length = robo_html_source_length(src);
    t->source_kind = src->kind;
    t->state = ROBO_HTML_TOKENIZER_STATE_DATA;
    t->return_state = ROBO_HTML_TOKENIZER_STATE_DATA;
    t->line = 1u;
    t->column = 1u;
    robo_html_token_buffer_init(&t->text);
    robo_html_token_buffer_init(&t->text2);
    robo_html_token_buffer_init(&t->text3);
    robo_html_token_buffer_init(&t->chars);
    robo_html_token_buffer_init(&t->last_start_tag);
    robo_html_token_buffer_init(&t->raw_end_name);
    robo_html_token_pool_init(&t->attrs);
    robo_html_diagnostics_init(&t->diags);
    *out = t;
    robo_log_debug("robo_html: tokenizer created len=%llu",
                   (unsigned long long)t->length);
    return ROBO_OK;
}

robo_result_t robo_html_tokenizer_next(robo_html_tokenizer_t *tok,
                                       robo_html_token_t *out_tok,
                                       robo_html_diag_t *diag_out)
{
    tok_result_t r;
    unsigned guard;

    if (!tok || !out_tok) {
        return ROBO_HTML_DIAG(diag_out, ROBO_HTML_DOMAIN_PARSER,
                              ROBO_INVALID_ARGUMENT, "NULL argument");
    }
    /* Free the previous token's borrowed attribute view (if any). */
    if (tok->attr_view) {
        robo_free(tok->attr_view);
        tok->attr_view = NULL;
    }
    if (tok->at_eof) {
        /* Stable repeated EOF. */
        token_begin(tok, ROBO_HTML_TOKEN_EOF);
        *out_tok = tok->token;
        return ROBO_OK;
    }
    robo_html_token_pool_clear(&tok->attrs);
    tok->current_attr = NULL;
    robo_html_token_buffer_clear(&tok->text);
    robo_html_token_buffer_clear(&tok->text2);
    robo_html_token_buffer_clear(&tok->text3);
    tok->tag_is_end = 0;
    tok->tag_self_closing = 0;
    /* Token start positions are recorded by the state handler that begins
     * the token, not here: st_data/st_rawtext_like snapshot the byte that
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
            diag(tok, ROBO_OUT_OF_MEMORY, "tokenizer allocation failure");
            return ROBO_HTML_DIAG(diag_out, ROBO_HTML_DOMAIN_PARSER,
                                  ROBO_OUT_OF_MEMORY,
                                  "tokenizer allocation failure");
        }
        guard++;
    } while (r == TOK_CONTINUE && guard < 0xFFFFFFu);

    if (guard >= 0xFFFFFFu) {
        diag(tok, ROBO_ERROR, "tokenizer state loop; forcing EOF");
        token_begin(tok, ROBO_HTML_TOKEN_EOF);
        tok->at_eof = 1;
        r = TOK_EMIT;
    }
    *out_tok = tok->token;
    (void)robo_html_diag_set(diag_out, ROBO_OK, ROBO_HTML_DOMAIN_PARSER,
                             "token", __FILE__, __LINE__);
    return ROBO_OK;
}

robo_result_t robo_html_token_copy_tag_name(const robo_html_token_t *tok,
                                            char *out,
                                            size_t out_cap)
{
    const char *name;
    size_t len;

    if (!tok || !out || out_cap == 0u) {
        return ROBO_INVALID_ARGUMENT;
    }
    out[0] = '\0';
    if (tok->kind != ROBO_HTML_TOKEN_START_TAG &&
        tok->kind != ROBO_HTML_TOKEN_END_TAG &&
        tok->kind != ROBO_HTML_TOKEN_DOCTYPE) {
        return ROBO_OK;
    }
    name = (tok->kind == ROBO_HTML_TOKEN_DOCTYPE) ? tok->u.doctype.name
                                                  : tok->u.tag.name;
    if (!name) {
        return ROBO_OK;
    }
    len = 0u;
    while (name[len] != '\0') {
        len++;
    }
    if (len + 1u > out_cap) {
        return ROBO_NOT_SUPPORTED; /* refuse, never truncate */
    }
    memcpy(out, name, len + 1u);
    return ROBO_OK;
}

robo_result_t robo_html_token_copy_attr(const robo_html_token_t *tok,
                                        const char *name,
                                        char *out,
                                        size_t out_cap)
{
    size_t i;

    if (!tok || !name || !out || out_cap == 0u) {
        return ROBO_INVALID_ARGUMENT;
    }
    out[0] = '\0';
    if (tok->kind != ROBO_HTML_TOKEN_START_TAG &&
        tok->kind != ROBO_HTML_TOKEN_END_TAG) {
        return ROBO_NOT_FOUND;
    }
    for (i = 0u; i < tok->u.tag.attr_count; i++) {
        const char *candidate = tok->u.tag.attrs[i].name;
        size_t j = 0u;
        int equal = 1;

        /* ASCII case-insensitive match (names are stored lowercased). */
        while (candidate[j] != '\0' || name[j] != '\0') {
            if (robo_html_tok_ascii_lower(candidate[j]) !=
                robo_html_tok_ascii_lower(name[j])) {
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
            if (len + 1u > out_cap) {
                return ROBO_NOT_SUPPORTED; /* refuse, never truncate */
            }
            memcpy(out, value, len + 1u);
            return ROBO_OK;
        }
    }
    return ROBO_NOT_FOUND;
}

robo_result_t robo_html_tokenizer_reset(robo_html_tokenizer_t *tok,
                                        robo_html_diag_t *diag)
{
    if (!tok) {
        return ROBO_HTML_DIAG(diag, ROBO_HTML_DOMAIN_PARSER,
                              ROBO_INVALID_ARGUMENT, "tokenizer is NULL");
    }
    tok->pos = 0u;
    tok->at_eof = 0;
    tok->state = ROBO_HTML_TOKENIZER_STATE_DATA;
    tok->return_state = ROBO_HTML_TOKENIZER_STATE_DATA;
    tok->line = 1u;
    tok->column = 1u;
    tok->tok_start_offset = 0u;
    tok->tok_start_line = 1u;
    tok->tok_start_col = 1u;
    tok->tag_is_end = 0;
    tok->tag_self_closing = 0;
    tok->script_double_escape = 0;
    robo_html_token_buffer_clear(&tok->text);
    robo_html_token_buffer_clear(&tok->text2);
    robo_html_token_buffer_clear(&tok->text3);
    robo_html_token_buffer_free(&tok->chars);
    robo_html_token_buffer_clear(&tok->raw_end_name);
    robo_html_token_buffer_clear(&tok->last_start_tag);
    robo_html_token_pool_clear(&tok->attrs);
    tok->current_attr = NULL;
    robo_html_diagnostics_clear(&tok->diags);
    return ROBO_OK;
}

int robo_html_tokenizer_at_eof(const robo_html_tokenizer_t *tok)
{
    return tok ? tok->at_eof : 1;
}

robo_result_t robo_html_tokenizer_get_state(
    const robo_html_tokenizer_t *tok, robo_html_tokenizer_state_t *out)
{
    if (!tok || !out) {
        return ROBO_INVALID_ARGUMENT;
    }
    *out = tok->state;
    return ROBO_OK;
}

size_t robo_html_tokenizer_diagnostic_count(const robo_html_tokenizer_t *tok)
{
    return tok ? tok->diags.count : 0u;
}

robo_result_t robo_html_tokenizer_get_diagnostic(
    const robo_html_tokenizer_t *tok, size_t index, robo_html_diag_t *out_diag)
{
    if (!tok || !out_diag) {
        return ROBO_INVALID_ARGUMENT;
    }
    if (index >= tok->diags.count) {
        return ROBO_NOT_FOUND;
    }
    *out_diag = tok->diags.items[index];
    return ROBO_OK;
}

robo_result_t robo_html_tokenizer_switch_state(
    robo_html_tokenizer_t *tok, robo_html_tokenizer_state_t state,
    robo_html_diag_t *diag)
{
    if (!tok) {
        return ROBO_HTML_DIAG(diag, ROBO_HTML_DOMAIN_PARSER,
                              ROBO_INVALID_ARGUMENT, "tokenizer is NULL");
    }
    if (robo_html_tokenizer_state_switch_validate(state) != ROBO_OK) {
        return ROBO_HTML_DIAG(diag, ROBO_HTML_DOMAIN_PARSER,
                              ROBO_INVALID_ARGUMENT, "invalid state");
    }
    switch (state) {
    case ROBO_HTML_TOKENIZER_STATE_RCDATA:
    case ROBO_HTML_TOKENIZER_STATE_RAWTEXT:
    case ROBO_HTML_TOKENIZER_STATE_SCRIPT_DATA:
        tok->state = state;
        /* Seed the expected end tag from the last emitted start tag
         * (8.3 integration shape: <textarea> ... switch ... </textarea>). */
        robo_html_token_buffer_clear(&tok->raw_end_name);
        if (tok->last_start_tag.length > 0u) {
            size_t i;
            for (i = 0u; i < tok->last_start_tag.length; i++) {
                (void)robo_html_token_buffer_append_byte(
                    &tok->raw_end_name, tok->last_start_tag.data[i]);
            }
        }
        return ROBO_OK;
    case ROBO_HTML_TOKENIZER_STATE_PLAINTEXT:
    case ROBO_HTML_TOKENIZER_STATE_DATA:
        tok->state = state;
        robo_html_token_buffer_clear(&tok->raw_end_name);
        return ROBO_OK;
    default:
        return ROBO_HTML_DIAG(diag, ROBO_HTML_DOMAIN_PARSER,
                              ROBO_INVALID_ARGUMENT,
                              "state not a switch entry point");
    }
}

void robo_html_tokenizer_destroy(robo_html_tokenizer_t *tok)
{
    if (!tok) {
        return;
    }
    if (tok->attr_view) {
        robo_free(tok->attr_view);
    }
    robo_html_token_buffer_free(&tok->text);
    robo_html_token_buffer_free(&tok->text2);
    robo_html_token_buffer_free(&tok->text3);
    robo_html_token_buffer_free(&tok->chars);
    robo_html_token_buffer_free(&tok->last_start_tag);
    robo_html_token_buffer_free(&tok->raw_end_name);
    robo_html_token_pool_free(&tok->attrs);
    robo_free(tok);
}

/* ------------------------------------------------------------------ */
/* State helpers (public test surface)                                 */
/* ------------------------------------------------------------------ */

int robo_html_tokenizer_state_is_known(robo_html_tokenizer_state_t state)
{
    return state >= ROBO_HTML_TOKENIZER_STATE_DATA &&
           state < ROBO_HTML_TOKENIZER_STATE_COUNT_;
}

const char *robo_html_tokenizer_state_name(robo_html_tokenizer_state_t state)
{
    switch (state) {
    case ROBO_HTML_TOKENIZER_STATE_DATA:
        return "data";
    case ROBO_HTML_TOKENIZER_STATE_PLAINTEXT:
        return "plaintext";
    case ROBO_HTML_TOKENIZER_STATE_TAG_OPEN:
        return "tag_open";
    case ROBO_HTML_TOKENIZER_STATE_END_TAG_OPEN:
        return "end_tag_open";
    case ROBO_HTML_TOKENIZER_STATE_TAG_NAME:
        return "tag_name";
    case ROBO_HTML_TOKENIZER_STATE_BEFORE_ATTRIBUTE_NAME:
        return "before_attribute_name";
    case ROBO_HTML_TOKENIZER_STATE_ATTRIBUTE_NAME:
        return "attribute_name";
    case ROBO_HTML_TOKENIZER_STATE_AFTER_ATTRIBUTE_NAME:
        return "after_attribute_name";
    case ROBO_HTML_TOKENIZER_STATE_BEFORE_ATTRIBUTE_VALUE:
        return "before_attribute_value";
    case ROBO_HTML_TOKENIZER_STATE_ATTRIBUTE_VALUE_DOUBLE_QUOTED:
        return "attribute_value_double_quoted";
    case ROBO_HTML_TOKENIZER_STATE_ATTRIBUTE_VALUE_SINGLE_QUOTED:
        return "attribute_value_single_quoted";
    case ROBO_HTML_TOKENIZER_STATE_ATTRIBUTE_VALUE_UNQUOTED:
        return "attribute_value_unquoted";
    case ROBO_HTML_TOKENIZER_STATE_AFTER_ATTRIBUTE_VALUE_QUOTED:
        return "after_attribute_value_quoted";
    case ROBO_HTML_TOKENIZER_STATE_SELF_CLOSING_START_TAG:
        return "self_closing_start_tag";
    case ROBO_HTML_TOKENIZER_STATE_MARKUP_DECLARATION_OPEN:
        return "markup_declaration_open";
    case ROBO_HTML_TOKENIZER_STATE_BOGUS_COMMENT:
        return "bogus_comment";
    case ROBO_HTML_TOKENIZER_STATE_COMMENT_START:
        return "comment_start";
    case ROBO_HTML_TOKENIZER_STATE_COMMENT_START_DASH:
        return "comment_start_dash";
    case ROBO_HTML_TOKENIZER_STATE_COMMENT:
        return "comment";
    case ROBO_HTML_TOKENIZER_STATE_COMMENT_LESS_THAN:
        return "comment_less_than";
    case ROBO_HTML_TOKENIZER_STATE_COMMENT_LESS_THAN_BANG:
        return "comment_less_than_bang";
    case ROBO_HTML_TOKENIZER_STATE_COMMENT_LESS_THAN_BANG_DASHES:
        return "comment_less_than_bang_dashes";
    case ROBO_HTML_TOKENIZER_STATE_COMMENT_END_DASH:
        return "comment_end_dash";
    case ROBO_HTML_TOKENIZER_STATE_COMMENT_END:
        return "comment_end";
    case ROBO_HTML_TOKENIZER_STATE_COMMENT_END_BANG:
        return "comment_end_bang";
    case ROBO_HTML_TOKENIZER_STATE_DOCTYPE:
        return "doctype";
    case ROBO_HTML_TOKENIZER_STATE_BEFORE_DOCTYPE_NAME:
        return "before_doctype_name";
    case ROBO_HTML_TOKENIZER_STATE_DOCTYPE_NAME:
        return "doctype_name";
    case ROBO_HTML_TOKENIZER_STATE_AFTER_DOCTYPE_NAME:
        return "after_doctype_name";
    case ROBO_HTML_TOKENIZER_STATE_BOGUS_DOCTYPE:
        return "bogus_doctype";
    case ROBO_HTML_TOKENIZER_STATE_AFTER_DOCTYPE_PUBLIC_KEYWORD:
        return "after_doctype_public_keyword";
    case ROBO_HTML_TOKENIZER_STATE_BEFORE_DOCTYPE_PUBLIC_IDENTIFIER:
        return "before_doctype_public_identifier";
    case ROBO_HTML_TOKENIZER_STATE_DOCTYPE_PUBLIC_IDENTIFIER_DOUBLE_QUOTED:
        return "doctype_public_identifier_double_quoted";
    case ROBO_HTML_TOKENIZER_STATE_DOCTYPE_PUBLIC_IDENTIFIER_SINGLE_QUOTED:
        return "doctype_public_identifier_single_quoted";
    case ROBO_HTML_TOKENIZER_STATE_AFTER_DOCTYPE_PUBLIC_IDENTIFIER:
        return "after_doctype_public_identifier";
    case ROBO_HTML_TOKENIZER_STATE_BETWEEN_DOCTYPE_PUBLIC_AND_SYSTEM_IDENTIFIERS:
        return "between_doctype_public_and_system_identifiers";
    case ROBO_HTML_TOKENIZER_STATE_AFTER_DOCTYPE_SYSTEM_KEYWORD:
        return "after_doctype_system_keyword";
    case ROBO_HTML_TOKENIZER_STATE_BEFORE_DOCTYPE_SYSTEM_IDENTIFIER:
        return "before_doctype_system_identifier";
    case ROBO_HTML_TOKENIZER_STATE_DOCTYPE_SYSTEM_IDENTIFIER_DOUBLE_QUOTED:
        return "doctype_system_identifier_double_quoted";
    case ROBO_HTML_TOKENIZER_STATE_DOCTYPE_SYSTEM_IDENTIFIER_SINGLE_QUOTED:
        return "doctype_system_identifier_single_quoted";
    case ROBO_HTML_TOKENIZER_STATE_AFTER_DOCTYPE_SYSTEM_IDENTIFIER:
        return "after_doctype_system_identifier";
    case ROBO_HTML_TOKENIZER_STATE_BOGUS_DOCTYPE_QUIRKS:
        return "bogus_doctype_quirks";
    case ROBO_HTML_TOKENIZER_STATE_CHARACTER_REFERENCE:
        return "character_reference";
    case ROBO_HTML_TOKENIZER_STATE_CHARACTER_REFERENCE_NAMED:
        return "character_reference_named";
    case ROBO_HTML_TOKENIZER_STATE_CHARACTER_REFERENCE_NUMERIC:
        return "character_reference_numeric";
    case ROBO_HTML_TOKENIZER_STATE_CHARACTER_REFERENCE_NUMERIC_HEX:
        return "character_reference_numeric_hex";
    case ROBO_HTML_TOKENIZER_STATE_CHARACTER_REFERENCE_NUMERIC_END:
        return "character_reference_numeric_end";
    case ROBO_HTML_TOKENIZER_STATE_RAWTEXT:
        return "rawtext";
    case ROBO_HTML_TOKENIZER_STATE_RAWTEXT_LESS_THAN_SIGN:
        return "rawtext_less_than_sign";
    case ROBO_HTML_TOKENIZER_STATE_RAWTEXT_END_TAG_OPEN:
        return "rawtext_end_tag_open";
    case ROBO_HTML_TOKENIZER_STATE_RAWTEXT_END_TAG_NAME:
        return "rawtext_end_tag_name";
    case ROBO_HTML_TOKENIZER_STATE_RCDATA:
        return "rcdata";
    case ROBO_HTML_TOKENIZER_STATE_RCDATA_LESS_THAN_SIGN:
        return "rcdata_less_than_sign";
    case ROBO_HTML_TOKENIZER_STATE_RCDATA_END_TAG_OPEN:
        return "rcdata_end_tag_open";
    case ROBO_HTML_TOKENIZER_STATE_RCDATA_END_TAG_NAME:
        return "rcdata_end_tag_name";
    case ROBO_HTML_TOKENIZER_STATE_SCRIPT_DATA:
        return "script_data";
    case ROBO_HTML_TOKENIZER_STATE_SCRIPT_DATA_LESS_THAN_SIGN:
        return "script_data_less_than_sign";
    case ROBO_HTML_TOKENIZER_STATE_SCRIPT_DATA_END_TAG_OPEN:
        return "script_data_end_tag_open";
    case ROBO_HTML_TOKENIZER_STATE_SCRIPT_DATA_END_TAG_NAME:
        return "script_data_end_tag_name";
    case ROBO_HTML_TOKENIZER_STATE_SCRIPT_DATA_ESCAPE_START:
        return "script_data_escape_start";
    case ROBO_HTML_TOKENIZER_STATE_SCRIPT_DATA_ESCAPE_START_DASH:
        return "script_data_escape_start_dash";
    case ROBO_HTML_TOKENIZER_STATE_SCRIPT_DATA_ESCAPED:
        return "script_data_escaped";
    case ROBO_HTML_TOKENIZER_STATE_SCRIPT_DATA_ESCAPED_DASH:
        return "script_data_escaped_dash";
    case ROBO_HTML_TOKENIZER_STATE_SCRIPT_DATA_ESCAPED_DASH_DASH:
        return "script_data_escaped_dash_dash";
    case ROBO_HTML_TOKENIZER_STATE_SCRIPT_DATA_ESCAPED_LESS_THAN_SIGN:
        return "script_data_escaped_less_than_sign";
    case ROBO_HTML_TOKENIZER_STATE_SCRIPT_DATA_ESCAPED_END_TAG_OPEN:
        return "script_data_escaped_end_tag_open";
    case ROBO_HTML_TOKENIZER_STATE_SCRIPT_DATA_ESCAPED_END_TAG_NAME:
        return "script_data_escaped_end_tag_name";
    case ROBO_HTML_TOKENIZER_STATE_SCRIPT_DATA_DOUBLE_ESCAPE_START:
        return "script_data_double_escape_start";
    case ROBO_HTML_TOKENIZER_STATE_SCRIPT_DATA_DOUBLE_ESCAPED:
        return "script_data_double_escaped";
    case ROBO_HTML_TOKENIZER_STATE_SCRIPT_DATA_DOUBLE_ESCAPED_DASH:
        return "script_data_double_escaped_dash";
    case ROBO_HTML_TOKENIZER_STATE_SCRIPT_DATA_DOUBLE_ESCAPED_DASH_DASH:
        return "script_data_double_escaped_dash_dash";
    case ROBO_HTML_TOKENIZER_STATE_SCRIPT_DATA_DOUBLE_ESCAPED_LESS_THAN_SIGN:
        return "script_data_double_escaped_less_than_sign";
    case ROBO_HTML_TOKENIZER_STATE_SCRIPT_DATA_DOUBLE_ESCAPED_END_TAG_OPEN:
        return "script_data_double_escaped_end_tag_open";
    case ROBO_HTML_TOKENIZER_STATE_SCRIPT_DATA_DOUBLE_ESCAPED_END_TAG_NAME:
        return "script_data_double_escaped_end_tag_name";
    default:
        return "unknown";
    }
}

int robo_html_tokenizer_state_is_raw_content(robo_html_tokenizer_state_t state)
{
    switch (state) {
    case ROBO_HTML_TOKENIZER_STATE_PLAINTEXT:
    case ROBO_HTML_TOKENIZER_STATE_RAWTEXT:
    case ROBO_HTML_TOKENIZER_STATE_RAWTEXT_LESS_THAN_SIGN:
    case ROBO_HTML_TOKENIZER_STATE_RAWTEXT_END_TAG_OPEN:
    case ROBO_HTML_TOKENIZER_STATE_RAWTEXT_END_TAG_NAME:
    case ROBO_HTML_TOKENIZER_STATE_RCDATA:
    case ROBO_HTML_TOKENIZER_STATE_RCDATA_LESS_THAN_SIGN:
    case ROBO_HTML_TOKENIZER_STATE_RCDATA_END_TAG_OPEN:
    case ROBO_HTML_TOKENIZER_STATE_RCDATA_END_TAG_NAME:
    case ROBO_HTML_TOKENIZER_STATE_SCRIPT_DATA:
    case ROBO_HTML_TOKENIZER_STATE_SCRIPT_DATA_LESS_THAN_SIGN:
    case ROBO_HTML_TOKENIZER_STATE_SCRIPT_DATA_END_TAG_OPEN:
    case ROBO_HTML_TOKENIZER_STATE_SCRIPT_DATA_END_TAG_NAME:
    case ROBO_HTML_TOKENIZER_STATE_SCRIPT_DATA_ESCAPE_START:
    case ROBO_HTML_TOKENIZER_STATE_SCRIPT_DATA_ESCAPE_START_DASH:
    case ROBO_HTML_TOKENIZER_STATE_SCRIPT_DATA_ESCAPED:
    case ROBO_HTML_TOKENIZER_STATE_SCRIPT_DATA_ESCAPED_DASH:
    case ROBO_HTML_TOKENIZER_STATE_SCRIPT_DATA_ESCAPED_DASH_DASH:
    case ROBO_HTML_TOKENIZER_STATE_SCRIPT_DATA_ESCAPED_LESS_THAN_SIGN:
    case ROBO_HTML_TOKENIZER_STATE_SCRIPT_DATA_ESCAPED_END_TAG_OPEN:
    case ROBO_HTML_TOKENIZER_STATE_SCRIPT_DATA_ESCAPED_END_TAG_NAME:
    case ROBO_HTML_TOKENIZER_STATE_SCRIPT_DATA_DOUBLE_ESCAPE_START:
    case ROBO_HTML_TOKENIZER_STATE_SCRIPT_DATA_DOUBLE_ESCAPED:
    case ROBO_HTML_TOKENIZER_STATE_SCRIPT_DATA_DOUBLE_ESCAPED_DASH:
    case ROBO_HTML_TOKENIZER_STATE_SCRIPT_DATA_DOUBLE_ESCAPED_DASH_DASH:
    case ROBO_HTML_TOKENIZER_STATE_SCRIPT_DATA_DOUBLE_ESCAPED_LESS_THAN_SIGN:
    case ROBO_HTML_TOKENIZER_STATE_SCRIPT_DATA_DOUBLE_ESCAPED_END_TAG_OPEN:
    case ROBO_HTML_TOKENIZER_STATE_SCRIPT_DATA_DOUBLE_ESCAPED_END_TAG_NAME:
        return 1;
    default:
        return 0;
    }
}

robo_result_t robo_html_tokenizer_state_switch_validate(
    robo_html_tokenizer_state_t state)
{
    /* Only the five content-family entry points may be switched into from
     * outside; every sub-state is reached by the machine itself. */
    switch (state) {
    case ROBO_HTML_TOKENIZER_STATE_DATA:
    case ROBO_HTML_TOKENIZER_STATE_PLAINTEXT:
    case ROBO_HTML_TOKENIZER_STATE_RAWTEXT:
    case ROBO_HTML_TOKENIZER_STATE_RCDATA:
    case ROBO_HTML_TOKENIZER_STATE_SCRIPT_DATA:
        return ROBO_OK;
    default:
        return ROBO_INVALID_ARGUMENT;
    }
}
