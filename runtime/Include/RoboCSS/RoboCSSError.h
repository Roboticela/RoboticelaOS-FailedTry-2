/*
 * RoboCSS error vocabulary (Phase 9.1).
 *
 * A CSS parser is a recovery machine: most malformed input is not a failed
 * call, it is a problem the parser steps over on the way to a usable
 * stylesheet. That distinction is the whole point of this header. A
 * robo_result_t answers "did the call succeed"; a robo_css_error_t answers
 * "what was wrong with the CSS, and is it something we can continue past".
 *
 *   recoverable   the parser can continue and still produce a stylesheet —
 *                 a syntax error it recovered from, an invalid token, a
 *                 malformed declaration or rule, an unexpected end of input
 *   fatal         parsing cannot proceed at all — allocation failure, or a
 *                 feature this build does not have
 *
 * Being recoverable never means "report and ignore": 9.3 records the *first*
 * problem in the parser and on the stylesheet, so a caller always sees why.
 *
 * Errors are values, never control flow. Nothing here allocates, nothing here
 * throws, and no RoboCSS entry point fails the process on malformed CSS.
 */

#ifndef ROBO_CSS_ERROR_H
#define ROBO_CSS_ERROR_H

#include "RoboBase/RoboError.h"
#include "RoboCSS/RoboCSSTypes.h"

#ifdef __cplusplus
extern "C" {
#endif

/** What kind of CSS problem a record describes. */
typedef enum robo_css_error_kind {
    /** Not an error: valid input, or nothing recorded yet. */
    ROBO_CSS_ERROR_KIND_NONE = 0,
    /** The required subsystem does not exist in this build yet. */
    ROBO_CSS_ERROR_KIND_UNSUPPORTED = 1,
    /** A syntax error the parser recovered from and continued past. */
    ROBO_CSS_ERROR_KIND_RECOVERABLE_SYNTAX = 2,
    /** A token that cannot appear where it was found. */
    ROBO_CSS_ERROR_KIND_INVALID_TOKEN = 3,
    /** A declaration that is not `name: value`, skipped by the parser. */
    ROBO_CSS_ERROR_KIND_MALFORMED_DECLARATION = 4,
    /** A rule that is neither a style rule nor a recognisable at-rule. */
    ROBO_CSS_ERROR_KIND_MALFORMED_RULE = 5,
    /** The source ended inside a construct that needed more input. */
    ROBO_CSS_ERROR_KIND_UNEXPECTED_EOF = 6,
    /** Allocation failed; the model could not be built. */
    ROBO_CSS_ERROR_KIND_OUT_OF_MEMORY = 7
} robo_css_error_kind_t;

/** Number of defined error kinds. */
#define ROBO_CSS_ERROR_KIND_COUNT 8u

/** Non-zero if @p kind is a defined error kind enumerator. */
int robo_css_error_kind_is_known(robo_css_error_kind_t kind);

/** Human-readable error kind name; never NULL; "unknown" when undefined. */
const char *robo_css_error_kind_name(robo_css_error_kind_t kind);

/** Non-zero for every kind except NONE — i.e. "this describes a problem". */
int robo_css_error_kind_is_error(robo_css_error_kind_t kind);

/**
 * Non-zero when a parser may continue past this problem and still produce a
 * stylesheet. True for RECOVERABLE_SYNTAX, INVALID_TOKEN,
 * MALFORMED_DECLARATION, MALFORMED_RULE and UNEXPECTED_EOF; false for NONE,
 * UNSUPPORTED and OUT_OF_MEMORY.
 */
int robo_css_error_kind_is_recoverable(robo_css_error_kind_t kind);

/**
 * Non-zero when parsing cannot proceed at all: UNSUPPORTED and
 * OUT_OF_MEMORY. Fatal here means "this attempt is over", never "the process
 * dies" — no RoboCSS path terminates the caller.
 */
int robo_css_error_kind_is_fatal(robo_css_error_kind_t kind);

/**
 * One CSS problem.
 *
 * `message` is a static or caller-lifetime string and is never freed by
 * RoboCSS. `position` carries the byte offset and, when the source was
 * consulted, the line and column; a hand-built model records offset 0 with
 * no line (see robo_css_position_t).
 */
typedef struct robo_css_error {
    robo_css_error_kind_t kind;
    robo_result_t code;
    robo_css_position_t position;
    const char *message;
} robo_css_error_t;

/** Clear @p error to "no problem". Safe on NULL (no-op). */
void robo_css_error_init(robo_css_error_t *error);

/**
 * Fill @p error. NULL @p error → ROBO_INVALID_ARGUMENT. A NULL @p message
 * clears the message field rather than dereferencing it; a NULL @p position
 * records offset 0 with no line.
 * @return @p code.
 */
robo_result_t robo_css_error_set(robo_css_error_t *error,
                                 robo_css_error_kind_t kind,
                                 robo_result_t code,
                                 const robo_css_position_t *position,
                                 const char *message);

/**
 * Non-zero when @p error describes a problem (kind != NONE). Non-zero for a
 * non-NULL record whose kind is undefined, because an unknown kind is not
 * "fine" — it is a record nobody should have written.
 */
int robo_css_error_is_error(const robo_css_error_t *error);

/** Borrowed message ("") when unset; NULL @p error → "". */
const char *robo_css_error_message(const robo_css_error_t *error);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_CSS_ERROR_H */
