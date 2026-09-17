/*
 * RoboCSS parser boundary (Phase 9.1).
 *
 * The pipeline this phase fixes, and what each stage owns:
 *
 *   robo_css_source_t        bytes, borrowed or copied        (9.1, here)
 *         │
 *         ▼
 *   robo_css_token_t         the token vocabulary             (9.1, here)
 *         │                   the machine that fills it       (9.2)
 *         ▼
 *   robo_css_parser_t        the cursor and the first error   (9.1, here)
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
 * through robo_css_stylesheet_parse (RoboCSSStyleSheet.h), because a parse
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
 * and is always within `[0, length]`. `has_error`/`error` record the first
 * problem, if any; everything else the parser needs (a token lookahead, an
 * open-block stack, a nesting budget) belongs to 9.2/9.3 and is deliberately
 * not predicted here.
 */
typedef struct robo_css_parser {
    const robo_css_source_t *source;
    robo_css_origin_t origin;
    size_t offset;
    int has_error;
    robo_css_error_t error;
} robo_css_parser_t;

/**
 * Point @p parser at @p source, at offset 0, with no error recorded.
 *
 * @p source may be empty (a legal stylesheet is allowed to be empty) but must
 * not be NULL: a parser with no source has nothing to be a cursor over.
 * @p origin is recorded and never interpreted. An unknown origin is accepted as
 * NONE only; anything else unknown is rejected, because the origin vocabulary is
 * closed.
 *
 * @return ROBO_OK; ROBO_INVALID_ARGUMENT for a NULL parser/source or an unknown
 *         origin.
 */
robo_result_t robo_css_parser_init(robo_css_parser_t *parser,
                                  const robo_css_source_t *source,
                                  robo_css_origin_t origin,
                                  robo_css_diag_t *diag);

/**
 * Forget the source and reset the cursor. Safe on NULL; idempotent.
 */
void robo_css_parser_dispose(robo_css_parser_t *parser);

/**
 * Return the cursor to the start and clear the recorded error, keeping the
 * source and origin. This is how a caller re-parses the same bytes.
 * @return ROBO_OK; ROBO_INVALID_ARGUMENT for a NULL parser or one with no
 *         source (disposed, or never initialized).
 */
robo_result_t robo_css_parser_reset(robo_css_parser_t *parser,
                                   robo_css_diag_t *diag);

/** Borrowed source, or NULL when @p parser is NULL or has been disposed. */
const robo_css_source_t *robo_css_parser_source(const robo_css_parser_t *parser);

/** Recorded origin; ROBO_CSS_ORIGIN_NONE for NULL. */
robo_css_origin_t robo_css_parser_origin(const robo_css_parser_t *parser);

/** Current byte offset (0 for NULL). */
size_t robo_css_parser_offset(const robo_css_parser_t *parser);

/** Bytes left before the end (0 for NULL, or for a disposed parser). */
size_t robo_css_parser_remaining(const robo_css_parser_t *parser);

/** Non-zero when the cursor has reached the end. A NULL parser → 1. */
int robo_css_parser_at_end(const robo_css_parser_t *parser);

/**
 * Resolve the cursor to a position (offset, line, column).
 * @return ROBO_OK; ROBO_INVALID_ARGUMENT for a NULL parser/output or a
 *         disposed parser.
 */
robo_result_t robo_css_parser_position(const robo_css_parser_t *parser,
                                      robo_css_position_t *out_position,
                                      robo_css_diag_t *diag);

/**
 * Move the cursor @p count bytes forward.
 *
 * A count larger than what is left is a *caller* mistake, so it is reported —
 * but the cursor still lands at the end rather than nowhere, which keeps a
 * recovery path able to continue from a defined place.
 * @return ROBO_OK; ROBO_INVALID_ARGUMENT for a NULL/disposed parser, or for a
 *         count beyond the remaining bytes (the cursor is then at the end).
 */
robo_result_t robo_css_parser_advance(robo_css_parser_t *parser,
                                      size_t count,
                                      robo_css_diag_t *diag);

/**
 * Move the cursor to @p offset.
 * @return ROBO_OK; ROBO_INVALID_ARGUMENT for a NULL/disposed parser or an
 *         offset past the end (the cursor does not move in that case — this is
 *         an absolute seek, not a saturated one, so a caller that asks for the
 *         end gets the end and a caller that asks for past the end gets told).
 */
robo_result_t robo_css_parser_seek(robo_css_parser_t *parser,
                                  size_t offset,
                                  robo_css_diag_t *diag);

/**
 * The empty range at the cursor. Handy for building a token or an error at
 * "where we are", and the only range the cursor hands out.
 */
robo_css_range_t robo_css_parser_cursor_range(const robo_css_parser_t *parser);

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
int robo_css_parser_record_error(robo_css_parser_t *parser,
                                 const robo_css_error_t *error);

/**
 * Record a problem built from its parts, positioned at the cursor.
 * @return 1 when recorded, 0 when a previous error was kept or an argument is
 *         unusable.
 */
int robo_css_parser_fail(robo_css_parser_t *parser,
                         robo_css_error_kind_t kind,
                         robo_result_t code,
                         const char *message);

/** Non-zero when an error has been recorded. NULL parser → 0. */
int robo_css_parser_has_error(const robo_css_parser_t *parser);

/**
 * Borrowed error record, or NULL when there is none.
 * The pointer is owned by the parser and is valid until it is reset, disposed
 * or freed.
 */
const robo_css_error_t *robo_css_parser_error(const robo_css_parser_t *parser);

/** Copy the recorded error into @p out_error and clear the parser's record.
 *  @return 1 when there was one to take, 0 otherwise. */
int robo_css_parser_take_error(robo_css_parser_t *parser,
                               robo_css_error_t *out_error);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_CSS_PARSER_H */
