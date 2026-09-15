/*
 * Tokenizer object layout (Phase 8.2) — private to RoboHTML sources.
 */
#ifndef ROBO_HTML_TOKENIZER_INTERNAL_H
#define ROBO_HTML_TOKENIZER_INTERNAL_H

#include "RoboHTMLInternal.h"
#include "RoboHTMLCharRef.h"

#include <stddef.h>
#include <stdint.h>

/** Tokenizer object (public type is an opaque handle). */
struct robo_html_tokenizer {
    /* Borrowed source (Phase 8.1 view; caller keeps bytes alive). */
    const char *bytes;
    size_t length;
    robo_html_source_kind_t source_kind;

    /* Cursor. */
    size_t pos;
    int at_eof;             /* EOF token emitted */
    robo_html_tokenizer_state_t state;
    robo_html_tokenizer_state_t return_state; /* after character refs */

    /* Position tracking (token starts and diagnostics). */
    size_t tok_start_offset;
    uint32_t tok_start_line;
    uint32_t tok_start_col;
    uint32_t line;
    uint32_t column;

    /* Pending token construction. */
    robo_html_token_t token;
    robo_html_token_buffer_t text;  /* name / data / comment / doctype */
    robo_html_token_buffer_t text2; /* doctype public id */
    robo_html_token_buffer_t text3; /* doctype system id */
    robo_html_token_pool_t attrs;
    robo_html_token_attr_entry_t *current_attr;

    /* Pending tag flavor/flags (read at emit; the token union is reset by
     * token_begin, so these live on the tokenizer). */
    int tag_is_end;       /* pending tag came from "</" */
    int tag_self_closing; /* pending tag had "/>" */

    /* Doctype scratch. */
    int doctype_force_quirks;
    int doctype_has_public;
    int doctype_has_system;

    /* Character-reference scratch. */
    char charref_out[ROBO_HTML_CHARREF_MAX_BYTES];
    size_t charref_len;
    size_t charref_replay;    /* bytes of charref_out left to emit */
    size_t charref_replay_at; /* index into charref_out */

    /* Raw-content (RAWTEXT/RCDATA/script) end-tag lookahead name. */
    robo_html_token_buffer_t raw_end_name;
    int pending_end_tag;

    /* Storage for the currently emitted CHARACTER token: the token borrows
     * these bytes, so they must outlive the state-handler call (data is
     * stolen from the handler's local run buffer at emit time and released
     * by the next character emission / reset / destroy). */
    robo_html_token_buffer_t chars;

    /* Name of the last emitted start tag; the controlled raw-content
     * switch hook (8.3 flow) derives the expected end tag from it. */
    robo_html_token_buffer_t last_start_tag;

    /* Per-emitted-tag attribute view (owned; freed at next token or
     * destroy — keeps the borrowed token contract without handing out
     * pool internals). */
    robo_html_token_attr_t *attr_view;
    size_t attr_view_len;

    /* Script double-escape bookkeeping (script data states). */
    int script_double_escape;

    /* Diagnostics (bounded). */
    robo_html_diag_list_t diags;
};

/** One-byte cursor helper: current byte or '\0' at/after EOF. */
char robo_html_tok_peek(const robo_html_tokenizer_t *t);

/** Peek at @p ahead bytes past the cursor (0 = current). */
char robo_html_tok_peek_ahead(const robo_html_tokenizer_t *t, size_t ahead);

/** Advance one byte, maintaining line/column. */
void robo_html_tok_advance(robo_html_tokenizer_t *t);

/** ASCII lower-case (no locale). */
char robo_html_tok_ascii_lower(char c);

/** ASCII case-insensitive match of NUL-terminated @p word at cursor. */
int robo_html_tok_word_at(const robo_html_tokenizer_t *t, const char *word);

#endif /* ROBO_HTML_TOKENIZER_INTERNAL_H */
