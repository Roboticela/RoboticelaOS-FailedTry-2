/*
 * RoboCSS parser boundary (Phase 9.1).
 *
 * The pipeline this phase fixes, and what each stage owns:
 *
 *   CssSource        bytes, borrowed or copied        (9.1, here)
 *         │
 *         ▼
 *   CssToken         the token vocabulary             (9.1, here)
 *         │                   the machine that fills it       (9.2)
 *         ▼
 *   CssParser        the cursor and the first error   (9.1, here)
 *         │                   the grammar that consumes tokens (9.3)
 *         ▼
 *   robo_css_stylesheet_t    rules, declarations, state       (9.1, here)
 *
 * Phase 9.1 contributes the cursor: a source, a byte offset, an origin, and the
 * record of the *first* thing that went wrong. That is not the parser — the
 * cursor does not classify a byte, consume a token, or know what a declaration
 * is — but it is the state the parser will be written on top of, and the
 * lifetime and error rules here are the ones 9.2 and 9.3 must keep:
 *
 *   - a parser borrows its source; the source must outlive it
 *   - the cursor never runs backwards past zero and never past the end
 *   - the first error is kept, later ones are not recorded over it, because
 *     the first problem is the one that explains the rest
 *   - every problem is a value, never a jump: no longjmp, no abort, no panic
 *
 * There is intentionally no "parse" entry point here. Parsing is reached
 * through CssStylesheetParse (RoboCSSStyleSheet.h), because a parse
 * produces a stylesheet and that is the object that owns the result; a second
 * entry point here would be a second way to produce the same thing.
 */

#ifndef ROBO_CSS_PARSER_H
#define ROBO_CSS_PARSER_H

#include "RoboBase/RoboError.h"
#include "RoboCSS/RoboCSSDiagnostics.h"
#include "RoboCSS/RoboCSSError.h"
#include "RoboCSS/RoboCSSSource.h"
#include "RoboCSS/RoboCSSTypes.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * A cursor over a CSS source.
 *
 * `source` is borrowed and must outlive the parser. `offset` is a byte offset
 * and is always within `[0, length]`. `hasError`/`error` record the first
 * problem, if any; everything else the parser needs (a token lookahead, an
 * open-block stack, a nesting budget) belongs to 9.2/9.3 and is deliberately
 * not predicted here.
 */
typedef struct CssParser {
    const CssSource *source;
    CssOrigin origin;
    size_t offset;
    int hasError;
    CssError error;
} CssParser;

/**
 * Point @p parser at @p source, at offset 0, with no error recorded.
 *
 * @p source may be empty (a legal stylesheet is allowed to be empty) but must
 * not be NULL: a parser with no source has nothing to be a cursor over.
 * @p origin is recorded and never interpreted. An unknown origin is accepted as
 * NONE only; anything else unknown is rejected, because the origin vocabulary is
 * closed.
 *
 * @return RoboOk; RoboInvalidArgument for a NULL parser/source or an unknown
 *         origin.
 */
Result CssParserInit(CssParser *parser,
                                  const CssSource *source,
                                  CssOrigin origin,
                                  CssDiag *diag);

/**
 * Forget the source and reset the cursor. Safe on NULL; idempotent.
 */
void CssParserDispose(CssParser *parser);

/**
 * Return the cursor to the start and clear the recorded error, keeping the
 * source and origin. This is how a caller re-parses the same bytes.
 * @return RoboOk; RoboInvalidArgument for a NULL parser or one with no
 *         source (disposed, or never initialized).
 */
Result CssParserReset(CssParser *parser,
                                   CssDiag *diag);

/** Borrowed source, or NULL when @p parser is NULL or has been disposed. */
const CssSource *CssParserSource(const CssParser *parser);

/** Recorded origin; RoboCssOriginNone for NULL. */
CssOrigin CssParserOrigin(const CssParser *parser);

/** Current byte offset (0 for NULL). */
size_t CssParserOffset(const CssParser *parser);

/** Bytes left before the end (0 for NULL, or for a disposed parser). */
size_t CssParserRemaining(const CssParser *parser);

/** Non-zero when the cursor has reached the end. A NULL parser → 1. */
int CssParserAtEnd(const CssParser *parser);

/**
 * Resolve the cursor to a position (offset, line, column).
 * @return RoboOk; RoboInvalidArgument for a NULL parser/output or a
 *         disposed parser.
 */
Result CssParserPosition(const CssParser *parser,
                                      CssPosition *outPosition,
                                      CssDiag *diag);

/**
 * Move the cursor @p count bytes forward.
 *
 * A count larger than what is left is a *caller* mistake, so it is reported —
 * but the cursor still lands at the end rather than nowhere, which keeps a
 * recovery path able to continue from a defined place.
 * @return RoboOk; RoboInvalidArgument for a NULL/disposed parser, or for a
 *         count beyond the remaining bytes (the cursor is then at the end).
 */
Result CssParserAdvance(CssParser *parser,
                                      size_t count,
                                      CssDiag *diag);

/**
 * Move the cursor to @p offset.
 * @return RoboOk; RoboInvalidArgument for a NULL/disposed parser or an
 *         offset past the end (the cursor does not move in that case — this is
 *         an absolute seek, not a saturated one, so a caller that asks for the
 *         end gets the end and a caller that asks for past the end gets told).
 */
Result CssParserSeek(CssParser *parser,
                                  size_t offset,
                                  CssDiag *diag);

/**
 * The empty range at the cursor. Handy for building a token or an error at
 * "where we are", and the only range the cursor hands out.
 */
CssRange CssParserCursorRange(const CssParser *parser);

/* ------------------------------------------------------------------ */
/* The first error                                                     */
/* ------------------------------------------------------------------ */

/**
 * Record @p error as the parser's error.
 *
 * The *first* error wins: a parser that has already recorded one keeps it, and
 * this returns 0. That is deliberate — by the time a recovery path has skipped
 * three constructs, the useful report is still the first problem.
 *
 * @return 1 when the error was recorded, 0 when a previous one was kept (or
 *         @p error is NULL / @p parser is NULL / @p error's kind is NONE).
 */
int CssParserRecordError(CssParser *parser,
                                 const CssError *error);

/**
 * Record a problem built from its parts, positioned at the cursor.
 * @return 1 when recorded, 0 when a previous error was kept or an argument is
 *         unusable.
 */
int CssParserFail(CssParser *parser,
                         CssErrorKind kind,
                         Result code,
                         const char *message);

/** Non-zero when an error has been recorded. NULL parser → 0. */
int CssParserHasError(const CssParser *parser);

/**
 * Borrowed error record, or NULL when there is none.
 * The pointer is owned by the parser and is valid until it is reset, disposed
 * or freed.
 */
const CssError *CssParserError(const CssParser *parser);

/** Copy the recorded error into @p outError and clear the parser's record.
 *  @return 1 when there was one to take, 0 otherwise. */
int CssParserTakeError(CssParser *parser,
                               CssError *outError);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_CSS_PARSER_H */
