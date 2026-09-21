/*
 * RoboCSS error vocabulary (Phase 9.1).
 *
 * A CSS parser is a recovery machine: most malformed input is not a failed
 * call, it is a problem the parser steps over on the way to a usable
 * stylesheet. That distinction is the whole point of this header. A
 * Result answers "did the call succeed"; a CssError answers
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
typedef enum CssErrorKind {
    /** Not an error: valid input, or nothing recorded yet. */
    RoboCssErrorKindNone = 0,
    /** The required subsystem does not exist in this build yet. */
    RoboCssErrorKindUnsupported = 1,
    /** A syntax error the parser recovered from and continued past. */
    RoboCssErrorKindRecoverableSyntax = 2,
    /** A token that cannot appear where it was found. */
    RoboCssErrorKindInvalidToken = 3,
    /** A declaration that is not `name: value`, skipped by the parser. */
    RoboCssErrorKindMalformedDeclaration = 4,
    /** A rule that is neither a style rule nor a recognisable at-rule. */
    RoboCssErrorKindMalformedRule = 5,
    /** The source ended inside a construct that needed more input. */
    RoboCssErrorKindUnexpectedEof = 6,
    /** Allocation failed; the model could not be built. */
    RoboCssErrorKindOutOfMemory = 7
} CssErrorKind;

/** Number of defined error kinds. */
#define RoboCssErrorKindCount 8u

/** Non-zero if @p kind is a defined error kind enumerator. */
int CssErrorKindIsKnown(CssErrorKind kind);

/** Human-readable error kind name; never NULL; "unknown" when undefined. */
const char *CssErrorKindName(CssErrorKind kind);

/** Non-zero for every kind except NONE — i.e. "this describes a problem". */
int CssErrorKindIsError(CssErrorKind kind);

/**
 * Non-zero when a parser may continue past this problem and still produce a
 * stylesheet. True for RECOVERABLE_SYNTAX, INVALID_TOKEN,
 * MALFORMED_DECLARATION, MALFORMED_RULE and UNEXPECTED_EOF; false for NONE,
 * UNSUPPORTED and OUT_OF_MEMORY.
 */
int CssErrorKindIsRecoverable(CssErrorKind kind);

/**
 * Non-zero when parsing cannot proceed at all: UNSUPPORTED and
 * OUT_OF_MEMORY. Fatal here means "this attempt is over", never "the process
 * dies" — no RoboCSS path terminates the caller.
 */
int CssErrorKindIsFatal(CssErrorKind kind);

/**
 * One CSS problem.
 *
 * `message` is a static or caller-lifetime string and is never freed by
 * RoboCSS. `position` carries the byte offset and, when the source was
 * consulted, the line and column; a hand-built model records offset 0 with
 * no line (see CssPosition).
 */
typedef struct CssError {
    CssErrorKind kind;
    Result code;
    CssPosition position;
    const char *message;
} CssError;

/** Clear @p error to "no problem". Safe on NULL (no-op). */
void CssErrorInit(CssError *error);

/**
 * Fill @p error. NULL @p error → RoboInvalidArgument. A NULL @p message
 * clears the message field rather than dereferencing it; a NULL @p position
 * records offset 0 with no line.
 * @return @p code.
 */
Result CssErrorSet(CssError *error,
                                 CssErrorKind kind,
                                 Result code,
                                 const CssPosition *position,
                                 const char *message);

/**
 * Non-zero when @p error describes a problem (kind != NONE). Non-zero for a
 * non-NULL record whose kind is undefined, because an unknown kind is not
 * "fine" — it is a record nobody should have written.
 */
int CssErrorIsError(const CssError *error);

/** Borrowed message ("") when unset; NULL @p error → "". */
const char *CssErrorMessage(const CssError *error);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_CSS_ERROR_H */
