/*
 * RoboJS vocabulary (Phase 16.1) — the tokens, the AST node kinds, the completion kinds, the error
 * kinds, the source span and the resource limits.
 *
 * This header is *names and numbers*, deliberately: every other header in the module is about
 * behaviour, and putting the vocabulary in one place is what keeps the lexer, the parser, the
 * interpreter and the diagnostics agreeing about what a `+` is called.
 *
 * Three rules the rest of the module follows from here:
 *
 *   **A token kind is a token kind, not a character.** `RoboJsTokenPlus` is the punctuator `+`
 *   however it was spelled, and the parser switches on the kind rather than on text. Only
 *   identifiers carry text that the parser reads.
 *
 *   **A span is four numbers, not a string.** `JsSpan` is where a node or token starts and where it
 *   ends, in line/column/offset. A diagnostic that says "line 3, column 7" is a diagnostic a person
 *   can act on, and nothing in this module formats a span into prose except the printer.
 *
 *   **Every limit has a name and a default.** The runtime's configuration is these constants; a
 *   script that loops forever is stopped by `RoboJsDefaultMaxSteps` rather than by the operating
 *   system noticing the machine has stopped responding.
 */

#ifndef ROBO_JS_TYPES_H
#define ROBO_JS_TYPES_H

#include "RoboBase/RoboError.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * The module's four reference types, forward-declared once so every header in RoboJS can name them
 * without including another. `JsObject` is the only garbage-collected-shaped thing in the runtime;
 * `JsEnv` is a scope, `JsNode` an AST node and `JsRuntime` the owner of all of them.
 */
typedef struct JsObject JsObject;
typedef struct JsEnv JsEnv;
typedef struct JsNode JsNode;
typedef struct JsRuntime JsRuntime;

/* ------------------------------------------------------------------ */
/* Tokens                                                              */
/* ------------------------------------------------------------------ */

/**
 * Lexer token kind. Keywords and punctuators are separate values rather than one `Keyword` kind
 * carrying a name, because the parser is a switch and a switch on an enum is the whole parser's
 * shape. The list is the language subset of 16.2 and nothing more.
 */
typedef enum JsTokenKind {
    RoboJsTokenEnd = 0,

    /* Names and literals. */
    RoboJsTokenIdentifier = 1,
    RoboJsTokenNumber = 2,
    RoboJsTokenString = 3,

    /* Declarations and statements. */
    RoboJsTokenVar = 10,
    RoboJsTokenLet = 11,
    RoboJsTokenConst = 12,
    RoboJsTokenFunction = 13,
    RoboJsTokenReturn = 14,
    RoboJsTokenIf = 15,
    RoboJsTokenElse = 16,
    RoboJsTokenWhile = 17,
    RoboJsTokenDo = 18,
    RoboJsTokenFor = 19,
    RoboJsTokenBreak = 20,
    RoboJsTokenContinue = 21,
    RoboJsTokenThrow = 22,
    RoboJsTokenTry = 23,
    RoboJsTokenCatch = 24,
    RoboJsTokenFinally = 25,
    RoboJsTokenSwitch = 26,
    RoboJsTokenCase = 27,
    RoboJsTokenDefault = 28,

    /* Operators spelled as words. */
    RoboJsTokenNew = 30,
    RoboJsTokenThis = 31,
    RoboJsTokenTypeof = 32,
    RoboJsTokenDelete = 33,
    RoboJsTokenVoid = 34,
    RoboJsTokenIn = 35,
    RoboJsTokenInstanceof = 36,

    /* Literals spelled as words. */
    RoboJsTokenTrue = 40,
    RoboJsTokenFalse = 41,
    RoboJsTokenNull = 42,
    RoboJsTokenUndefined = 43,

    /* Punctuation. */
    RoboJsTokenLParen = 50,
    RoboJsTokenRParen = 51,
    RoboJsTokenLBrace = 52,
    RoboJsTokenRBrace = 53,
    RoboJsTokenLBracket = 54,
    RoboJsTokenRBracket = 55,
    RoboJsTokenDot = 56,
    RoboJsTokenComma = 57,
    RoboJsTokenSemicolon = 58,
    RoboJsTokenColon = 59,
    RoboJsTokenQuestion = 60,
    RoboJsTokenArrow = 61,

    /* Operators. */
    RoboJsTokenAssign = 70,
    RoboJsTokenPlus = 71,
    RoboJsTokenMinus = 72,
    RoboJsTokenStar = 73,
    RoboJsTokenSlash = 74,
    RoboJsTokenPercent = 75,
    RoboJsTokenEqEq = 76,
    RoboJsTokenEqEqEq = 77,
    RoboJsTokenNotEq = 78,
    RoboJsTokenNotEqEq = 79,
    RoboJsTokenLt = 80,
    RoboJsTokenGt = 81,
    RoboJsTokenLtEq = 82,
    RoboJsTokenGtEq = 83,
    RoboJsTokenAndAnd = 84,
    RoboJsTokenOrOr = 85,
    RoboJsTokenNot = 86,
    RoboJsTokenPlusPlus = 87,
    RoboJsTokenMinusMinus = 88,
    RoboJsTokenPlusAssign = 89,
    RoboJsTokenMinusAssign = 90,
    RoboJsTokenStarAssign = 91,
    RoboJsTokenSlashAssign = 92,
    RoboJsTokenPercentAssign = 93
} JsTokenKind;

/** @return 1 if @p kind is a defined token kind, else 0. */
int JsTokenKindIsKnown(JsTokenKind kind);

/** Human-readable token kind name; never NULL. */
const char *JsTokenKindName(JsTokenKind kind);

/**
 * The canonical spelling of a punctuator or operator kind — `"==="`, `"+="`, `"("`. NULL for a kind
 * that is not punctuation. Used by the AST printer so a tree prints as the source it came from.
 */
const char *JsTokenKindText(JsTokenKind kind);

/**
 * Reflection for a binary operator: its lexical kind, its precedence (higher binds tighter) and
 * whether it is right-associative. NULL when @p kind is not a binary operator. The parser's
 * precedence climbing and the printer both read this one table.
 */
typedef struct JsBinaryOpInfo {
    JsTokenKind op;
    int precedence;
    int rightAssociative;
    const char *text;
} JsBinaryOpInfo;

const JsBinaryOpInfo *JsBinaryOpInfoOf(JsTokenKind kind);

/* ------------------------------------------------------------------ */
/* Source locations                                                    */
/* ------------------------------------------------------------------ */

/** A half-open range of source, in three coordinate systems so no caller has to convert. */
typedef struct JsSpan {
    int32_t line;    /* 1-based */
    int32_t column;  /* 1-based, in bytes */
    int32_t offset;  /* 0-based byte offset */
    int32_t endLine;
    int32_t endColumn;
    int32_t endOffset;
} JsSpan;

void JsSpanInit(JsSpan *span);
/** A span covering both @p first and @p last (so a binary node spans its operands). */
void JsSpanCover(JsSpan *out, const JsSpan *first, const JsSpan *last);

/* ------------------------------------------------------------------ */
/* AST node kinds                                                      */
/* ------------------------------------------------------------------ */

/**
 * AST node kind. The set is exactly what 16.9's evaluator switches on and nothing more; a kind
 * with no evaluator arm would be a node the parser can build and the interpreter cannot run.
 */
typedef enum JsNodeKind {
    RoboJsNodeProgram = 0,
    RoboJsNodeIdentifier = 1,
    RoboJsNodeNumberLiteral = 2,
    RoboJsNodeStringLiteral = 3,
    RoboJsNodeBooleanLiteral = 4,
    RoboJsNodeNullLiteral = 5,
    RoboJsNodeUndefinedLiteral = 6,
    RoboJsNodeThisExpression = 7,
    RoboJsNodeArrayLiteral = 8,
    RoboJsNodeObjectLiteral = 9,
    RoboJsNodeProperty = 10,
    RoboJsNodeFunctionExpression = 11,
    RoboJsNodeFunctionDeclaration = 12,
    RoboJsNodeArrowFunction = 13,
    RoboJsNodeVariableDeclaration = 14,
    RoboJsNodeVariableDeclarator = 15,
    RoboJsNodeExpressionStatement = 16,
    RoboJsNodeBlockStatement = 17,
    RoboJsNodeEmptyStatement = 18,
    RoboJsNodeIfStatement = 19,
    RoboJsNodeWhileStatement = 20,
    RoboJsNodeDoWhileStatement = 21,
    RoboJsNodeForStatement = 22,
    RoboJsNodeBreakStatement = 23,
    RoboJsNodeContinueStatement = 24,
    RoboJsNodeReturnStatement = 25,
    RoboJsNodeThrowStatement = 26,
    RoboJsNodeTryStatement = 27,
    RoboJsNodeCatchClause = 28,
    RoboJsNodeSwitchStatement = 29,
    RoboJsNodeSwitchCase = 30,
    RoboJsNodeUnaryExpression = 31,
    RoboJsNodeUpdateExpression = 32,
    RoboJsNodeBinaryExpression = 33,
    RoboJsNodeLogicalExpression = 34,
    RoboJsNodeAssignmentExpression = 35,
    RoboJsNodeConditionalExpression = 36,
    RoboJsNodeCallExpression = 37,
    RoboJsNodeNewExpression = 38,
    RoboJsNodeMemberExpression = 39,
    RoboJsNodeSequenceExpression = 40
} JsNodeKind;

/** @return 1 if @p kind is a defined node kind, else 0. */
int JsNodeKindIsKnown(JsNodeKind kind);

/** Human-readable node kind name; never NULL. */
const char *JsNodeKindName(JsNodeKind kind);

/* ------------------------------------------------------------------ */
/* Completions                                                         */
/* ------------------------------------------------------------------ */

/**
 * How a statement or expression finished. `Normal` is the ordinary case; the other four are the
 * control-flow signals the interpreter passes up as values rather than as `longjmp`, which is what
 * keeps `finally` and partial unwinding correct.
 */
typedef enum JsCompletionKind {
    RoboJsCompleteNormal = 0,
    RoboJsCompleteReturn = 1,
    RoboJsCompleteBreak = 2,
    RoboJsCompleteContinue = 3,
    RoboJsCompleteThrow = 4
} JsCompletionKind;

const char *JsCompletionKindName(JsCompletionKind kind);

/* ------------------------------------------------------------------ */
/* Errors                                                              */
/* ------------------------------------------------------------------ */

/**
 * Error kind, used for both the host's structured error record and the JavaScript error object's
 * `name`. One vocabulary, two readers: a `catch` compares the object's name and a diagnostic
 * compares the record's kind.
 */
typedef enum JsErrorKind {
    RoboJsErrorNone = 0,
    RoboJsErrorGeneral = 1,      /* Error */
    RoboJsErrorType = 2,         /* TypeError */
    RoboJsErrorReference = 3,    /* ReferenceError */
    RoboJsErrorRange = 4,        /* RangeError */
    RoboJsErrorSyntax = 5,       /* SyntaxError */
    RoboJsErrorInternal = 6,     /* the engine's own failure, never a script's */
    RoboJsErrorLimit = 7         /* a resource limit was reached (RangeError in script terms) */
} JsErrorKind;

int JsErrorKindIsKnown(JsErrorKind kind);

/** Human-readable error kind name; never NULL. `RoboJsErrorLimit` answers `"RangeError"`. */
const char *JsErrorKindName(JsErrorKind kind);

/** Host-side structured error: a kind, a message, and where it happened. */
typedef struct JsError {
    JsErrorKind kind;
    char message[256];
    JsSpan span;
    int hasSpan;
} JsError;

void JsErrorInit(JsError *error);
/**
 * Fill @p error from a `printf`-style format. Truncates rather than failing: a diagnostic that is
 * too long to store is still a diagnostic, and this door never allocates.
 */
void JsErrorSet(JsError *error,
                JsErrorKind kind,
                const JsSpan *span,
                const char *format,
                ...);

/* ------------------------------------------------------------------ */
/* Resource limits                                                     */
/* ------------------------------------------------------------------ */

/** Execution steps per top-level evaluation. `0` in a config means "use the default". */
#define RoboJsDefaultMaxSteps 500000
/** Nested call depth. */
#define RoboJsDefaultMaxCallDepth 64
/** Microtasks drained before one task boundary refuses further microtasks. */
#define RoboJsDefaultMaxMicrotasks 4096
/** Timer callbacks per event-loop cycle. */
#define RoboJsDefaultMaxTimersPerCycle 256
/** Tasks executed by one `JsLoopRunUntilIdle` before it refuses and reports. */
#define RoboJsDefaultMaxTasksPerCycle 4096
/** Tasks a runtime will hold queued at once. */
#define RoboJsDefaultMaxQueueLength 4096
/** Timer slots per runtime. */
#define RoboJsDefaultMaxTimerCount 1024
/** Properties (named and element) one object may hold. */
#define RoboJsDefaultMaxProperties 8192
/** Bytes one string may hold. */
#define RoboJsDefaultMaxStringLength (1 << 20)
/** Nesting the parser will accept before refusing. */
#define RoboJsDefaultMaxParseDepth 64
/** The first heap chunk; later chunks follow the allocation that needed them. */
#define RoboJsDefaultHeapChunkBytes (64 * 1024)

/** One millisecond, so a caller writing `timeout` math has a name for the unit. */
#define RoboJsMsPerSecond 1000u

#ifdef __cplusplus
}
#endif

#endif /* ROBO_JS_TYPES_H */
