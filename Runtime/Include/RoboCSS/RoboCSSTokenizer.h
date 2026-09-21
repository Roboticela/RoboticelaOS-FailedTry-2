/*
 * RoboCSS tokenizer (Phase 9.2).
 *
 * The real machine, where 9.1 shipped the record it fills. This turns CSS
 * source bytes into the token vocabulary of RoboCSSToken.h: the CSS Syntax
 * state machine — identifiers, functions, at-keywords, hashes, strings, urls,
 * numbers, percentages, dimensions, whitespace, comments, CDO/CDC,
 * punctuation — written independently and with no Chromium code or tables.
 *
 *   CssSource        bytes                          (9.1)
 *        │
 *        ▼
 *   CssTokenizer     this file: the stream machine   (9.2)
 *        │
 *        ▼
 *   CssToken         one token, borrowed             (9.1 + 9.2 value)
 *        │
 *        ▼
 *   the stylesheet grammar                                  (9.3, not here)
 *
 * Three decisions shape everything below, and all three are deliberate.
 *
 * 1. The stream is a cursor the caller owns, not a heap object.
 *
 *    RoboHTML's tokenizer is created and destroyed because it retains a bounded
 *    diagnostic list; RoboCSS's is a plain by-value record with `init`,
 *    `dispose` and `reset`, exactly like CssParser — the thing it will
 *    be embedded in. There is no allocation anywhere in this subsystem, so
 *    `init` cannot fail, nothing leaks, and a tokenizer is as cheap to put on
 *    the stack as it is to keep in a parser.
 *
 * 2. The caller's bytes are never rewritten.
 *
 *    The specification's input preprocessing step maps CR, CRLF and FF to LF
 *    and NUL to U+FFFD. This tokenizer does not do that, because a token's
 *    range is a byte range into the caller's own buffer and rewriting would make
 *    that a lie. Instead the same effect is achieved by *classification*: CR,
 *    LF and FF are read as one newline (a CRLF pair as one), a hex escape's
 *    trailing whitespace swallows a whole CRLF pair, and a newline inside a
 *    string makes it a bad-string rather than being replaced. The one case where
 *    classification cannot substitute is NUL, which the specification would have
 *    turned into U+FFFD and therefore an identifier character and which this
 *    build reports as a delimiter — claiming it was U+FFFD would mean the
 *    token's value said something the caller's bytes do not.
 *
 *    Ill-formed UTF-8 is preserved and reported as an INVALID token one byte at
 *    a time (the same "one ill-formed position" rule the source model uses),
 *    never repaired and never silently folded into a neighbouring name.
 *
 * 3. A token is raw; its value is computed.
 *
 *    The tokenizer fills every field of the 9.1 record — including `number`,
 *    `repr`, `unit` and the hash classification — but leaves `text` as the raw
 *    bytes so that `text` and `range` can never disagree. The token's *value*
 *    (escapes resolved, delimiters stripped) is reached through the
 *    robo_css_token_value_* accessors, which work from `text` alone. So a
 *    tokenizer never hands out a token whose text points into scratch storage,
 *    no token is invalidated by asking for the next one, and a token can be
 *    copied, stored and compared for as long as its source lives.
 *
 * Errors follow the parser's rule exactly: the *first* problem is kept, later
 * ones are not recorded over it, and every problem is a value. Malformed CSS
 * never crashes, never terminates the caller, and always yields a defined token
 * stream.
 */

#ifndef ROBO_CSS_TOKENIZER_H
#define ROBO_CSS_TOKENIZER_H

#include "RoboBase/RoboError.h"
#include "RoboCSS/RoboCSSDiagnostics.h"
#include "RoboCSS/RoboCSSError.h"
#include "RoboCSS/RoboCSSSource.h"
#include "RoboCSS/RoboCSSToken.h"
#include "RoboCSS/RoboCSSTypes.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * A token stream over a CSS source.
 *
 * `source` is borrowed and must outlive the tokenizer; `offset` is always within
 * `[0, length]`. `peeked`/`hasPeeked` are the one-token lookahead that `peek`
 * and `skip` share, so calling `peek` twice reads the same token twice rather
 * than tokenizing it twice. `tokensProduced` counts the tokens handed out,
 * including EOF repeats, which is what makes "this style sheet needed N tokens"
 * answerable without a second pass.
 *
 * The struct is public because it is a value the caller owns, the same as
 * CssParser. Initializing it is `CssTokenizerInit`; there is no
 * valid way to use one that was not initialized.
 */
typedef struct CssTokenizer {
    const CssSource *source;
    size_t offset;
    CssToken peeked;
    int hasPeeked;
    size_t tokensProduced;
    int hasError;
    CssError error;
} CssTokenizer;

/**
 * Point @p tokenizer at @p source, at offset 0, with no peek and no error.
 *
 * @p source may be empty — an empty style sheet is a legal stream of one EOF —
 * but must not be NULL. Disposes any state the tokenizer already held, so
 * re-initializing one is safe.
 *
 * @return RoboOk; RoboInvalidArgument for a NULL tokenizer or source.
 */
Result CssTokenizerInit(CssTokenizer *tokenizer,
                                     const CssSource *source,
                                     CssDiag *diag);

/**
 * Forget the source and the cursor. Safe on NULL, idempotent, and frees nothing
 * because nothing was ever allocated.
 */
void CssTokenizerDispose(CssTokenizer *tokenizer);

/**
 * Return to offset 0, drop the peeked token, clear the recorded error and the
 * produced count, keeping the source. This is how one tokenizer lexes a second
 * style sheet of the same length, or the same one twice.
 * @return RoboOk; RoboInvalidArgument for a NULL tokenizer or one with no
 *         source (disposed, or never initialized).
 */
Result CssTokenizerReset(CssTokenizer *tokenizer,
                                      CssDiag *diag);

/* ------------------------------------------------------------------ */
/* State                                                               */
/* ------------------------------------------------------------------ */

/** Borrowed source, or NULL when @p tokenizer is NULL or disposed. */
const CssSource *CssTokenizerSource(const CssTokenizer *tokenizer);

/** Current byte offset (0 for NULL). Advances only as tokens are consumed. */
size_t CssTokenizerOffset(const CssTokenizer *tokenizer);

/** Bytes left before the end (0 for NULL or a disposed tokenizer). */
size_t CssTokenizerRemaining(const CssTokenizer *tokenizer);

/**
 * Non-zero when the cursor has reached the end, i.e. the next token will be
 * EOF. A NULL tokenizer → 1.
 *
 * This is a question about the *cursor*, not about what is left to read: after
 * CssTokenizerSkipTrivia it can be 1 while a real token is still
 * peeked. Use the peeked token's type for that question.
 */
int CssTokenizerAtEnd(const CssTokenizer *tokenizer);

/** How many tokens `next` has handed out so far, EOF repeats included. */
size_t CssTokenizerTokensProduced(const CssTokenizer *tokenizer);

/** Non-zero when a token is sitting in the lookahead from `peek` or `skip_trivia`. */
int CssTokenizerHasPeek(const CssTokenizer *tokenizer);

/**
 * Resolve the cursor to a position (offset, line, column).
 * @return RoboOk; RoboInvalidArgument for a NULL tokenizer/output or a
 *         disposed tokenizer.
 */
Result CssTokenizerPosition(const CssTokenizer *tokenizer,
                                         CssPosition *outPosition,
                                         CssDiag *diag);

/* ------------------------------------------------------------------ */
/* The token stream                                                    */
/* ------------------------------------------------------------------ */

/**
 * Produce the next token and consume it.
 *
 * The token borrows @p tokenizer's source: `text` and every other byte field
 * point into the caller's bytes and stay valid as long as the source does. The
 * returned token is a plain value and may be copied, stored and kept.
 *
 * An EOF token is a real token carrying an empty range at the end offset.
 * Asking again after EOF keeps returning that same EOF token and never advances
 * the cursor and never touches the source, so a loop that only terminates on
 * EOF terminates on a value.
 *
 * @return RoboOk with a token in @p outToken; RoboInvalidArgument for a
 *         NULL tokenizer/token or one with no source. A malformed style sheet
 *         is not a failed call: it is a token plus a recorded error (see
 *         CssTokenizerError).
 */
Result CssTokenizerNext(CssTokenizer *tokenizer,
                                      CssToken *outToken,
                                      CssDiag *diag);

/**
 * Produce the next token without consuming it; the following `next` returns the
 * same token. Repeated peeks read the same token, so peeking never costs more
 * than the first call.
 *
 * @return as CssTokenizerNext.
 */
Result CssTokenizerPeek(CssTokenizer *tokenizer,
                                      CssToken *outToken,
                                      CssDiag *diag);

/**
 * Discard the next token without producing it — the "consume" of a
 * peek/consume pair. A token already peeked is dropped without re-reading it.
 *
 * @return RoboOk once a token was consumed, including an EOF token;
 *         RoboInvalidArgument for a NULL or source-less tokenizer.
 */
Result CssTokenizerSkip(CssTokenizer *tokenizer,
                                     CssDiag *diag);

/**
 * Consume whitespace and comment tokens until the next token is neither, then
 * leave that token peeked.
 *
 * Trivia preservation is the tokenizer's default — the specification keeps
 * whitespace and comments as tokens, and both are needed to reconstruct
 * anything — but virtually every consumer steps over them, and doing that by
 * hand is where an off-by-one lookahead comes from. After this call, `peek`
 * yields either a real token or EOF, and a consumer that *does* want the trivia
 * (a comment scanner, a source-preserving rewrite) simply never calls it.
 *
 * @return RoboOk; RoboInvalidArgument for a NULL or source-less tokenizer.
 */
Result CssTokenizerSkipTrivia(CssTokenizer *tokenizer,
                                            CssDiag *diag);

/* ------------------------------------------------------------------ */
/* The first error                                                     */
/* ------------------------------------------------------------------ */

/**
 * Record @p error as the tokenizer's error.
 *
 * The first error wins: one that has already been recorded is kept and this
 * returns 0, because the earliest problem is the one that explains the rest.
 *
 * @return 1 when recorded; 0 when a previous error was kept, @p error is NULL,
 *         @p tokenizer is NULL, or @p error's kind is NONE.
 */
int CssTokenizerRecordError(CssTokenizer *tokenizer,
                                    const CssError *error);

/**
 * Record a problem built from its parts, positioned at the cursor. The message
 * is a static string and is never freed.
 * @return 1 when recorded, 0 when a previous error was kept.
 */
int CssTokenizerFail(CssTokenizer *tokenizer,
                            CssErrorKind kind,
                            Result code,
                            const char *message);

/** Non-zero when an error has been recorded. NULL tokenizer → 0. */
int CssTokenizerHasError(const CssTokenizer *tokenizer);

/**
 * Borrowed error record, or NULL when there is none. Owned by the tokenizer and
 * valid until it is reset or disposed.
 */
const CssError *CssTokenizerError(const CssTokenizer *tokenizer);

/** Copy the recorded error into @p outError and clear the record.
 *  @return 1 when there was one to take, 0 otherwise. */
int CssTokenizerTakeError(CssTokenizer *tokenizer,
                                  CssError *outError);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_CSS_TOKENIZER_H */
