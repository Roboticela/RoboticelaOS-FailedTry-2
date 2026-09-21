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
struct HtmlTokenizer {
    /* Borrowed source (Phase 8.1 view; caller keeps bytes alive). */
    const char *bytes;
    size_t length;
    HtmlSourceKind sourceKind;

    /* Cursor. */
    size_t pos;
    int atEof;             /* EOF token emitted */
    HtmlTokenizerState state;
    HtmlTokenizerState returnState; /* after character refs */

    /* Position tracking (token starts and diagnostics). */
    size_t tokStartOffset;
    uint32_t tokStartLine;
    uint32_t tokStartCol;
    uint32_t line;
    uint32_t column;

    /* Pending token construction. */
    HtmlToken token;
    HtmlTokenBuffer text;  /* name / data / comment / doctype */
    HtmlTokenBuffer text2; /* doctype public id */
    HtmlTokenBuffer text3; /* doctype system id */
    HtmlTokenPool attrs;
    HtmlTokenAttrEntry *currentAttr;

    /* Pending tag flavor/flags (read at emit; the token union is reset by
     * TokenBegin, so these live on the tokenizer). */
    int tagIsEnd;       /* pending tag came from "</" */
    int tagSelfClosing; /* pending tag had "/>" */

    /* Doctype scratch. */
    int doctypeForceQuirks;
    int doctypeHasPublic;
    int doctypeHasSystem;

    /* Character-reference scratch. */
    char charrefOut[RoboHtmlCharrefMaxBytes];
    size_t charrefLen;
    size_t charrefReplay;    /* bytes of charrefOut left to emit */
    size_t charrefReplayAt; /* index into charrefOut */

    /* Raw-content (RAWTEXT/RCDATA/script) end-tag lookahead name. */
    HtmlTokenBuffer rawEndName;
    int pendingEndTag;

    /* Storage for the currently emitted CHARACTER token: the token borrows
     * these bytes, so they must outlive the state-handler call (data is
     * stolen from the handler's local run buffer at emit time and released
     * by the next character emission / reset / destroy). */
    HtmlTokenBuffer chars;

    /* Name of the last emitted start tag; the controlled raw-content
     * switch hook (8.3 flow) derives the expected end tag from it. */
    HtmlTokenBuffer lastStartTag;

    /* Per-emitted-tag attribute view (owned; freed at next token or
     * destroy — keeps the borrowed token contract without handing out
     * pool internals). */
    HtmlTokenAttr *attrView;
    size_t attrViewLen;

    /* Script double-escape bookkeeping (script data states). */
    int scriptDoubleEscape;

    /* Diagnostics (bounded). */
    HtmlDiagList diags;
};

/** One-byte cursor helper: current byte or '\0' at/after EOF. */
char HtmlTokPeek(const HtmlTokenizer *t);

/** Peek at @p ahead bytes past the cursor (0 = current). */
char HtmlTokPeekAhead(const HtmlTokenizer *t, size_t ahead);

/** Advance one byte, maintaining line/column. */
void HtmlTokAdvance(HtmlTokenizer *t);

/** ASCII lower-case (no locale). */
char HtmlTokAsciiLower(char c);

/** ASCII case-insensitive match of NUL-terminated @p word at cursor. */
int HtmlTokWordAt(const HtmlTokenizer *t, const char *word);

#endif /* ROBO_HTML_TOKENIZER_INTERNAL_H */
