/*
 * RoboJS parser (Phase 16.2) — tokens into an AST.
 *
 * Recursive descent with precedence climbing for the binary operators, which is the shape the
 * operator table in `RoboJSTypes.h` exists for: `JsBinaryOpInfoOf` says a precedence and an
 * associativity for every operator kind, and one loop uses it for all twenty-one of them rather
 * than a ladder of twenty-one functions.
 *
 * ## Errors are structured and located, and the parser recovers to the end
 *
 * A bad token answers `RoboParseError` with `JsParserError` filled — a `SyntaxError` with the token
 * it could not accept, where it was, and what it expected instead. Nothing is printed and no
 * JavaScript value is thrown: a parse failure happens before any script ran. The parser stops at the
 * first error; a caller that wants to keep going can call again, but this build makes no promise
 * about the tree after a failure and the caller should discard it.
 *
 * ## Depth is bounded
 *
 * `maxDepth` (from the runtime's configuration) stops `((((((...` at a stated limit with a
 * `RangeError`, rather than by exhausting the host's C stack. A parse that recurses once per nesting
 * level is exactly the parser a hostile input attacks.
 *
 * ## What the grammar covers
 *
 * The subset in 16.2: declarations, expressions with precedence, assignment, calls, member access,
 * conditional expressions, function declarations and expressions, arrow functions with a
 * parenthesised parameter list, blocks, `if`/`else`, `while`, `do`/`while`, `for` with an optional
 * three-part header, `return`, `break`, `continue`, `throw`, `try`/`catch`/`finally`, `switch`,
 * object and array literals. It does not cover classes, `let [a, b] = ...` destructuring, spread,
 * `async`/`await`, generators, modules or labels.
 */

#ifndef ROBO_JS_PARSER_H
#define ROBO_JS_PARSER_H

#include "RoboJS/RoboJSAst.h"
#include "RoboJS/RoboJSHeap.h"
#include "RoboJS/RoboJSLexer.h"
#include "RoboJS/RoboJSTypes.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** A parse failure: kind, message and the token it happened at. */
typedef struct JsParserError {
    JsError error;
    JsTokenKind token;
    int hasToken;
} JsParserError;

/**
 * The parser. Public layout for stack use; only `RoboJSParser.c` writes a field.
 */
typedef struct JsParser {
    JsLexer lexer;
    /**
     * The lookahead token: the one the parser has read and not yet consumed. One token of lookahead is
     * the whole of this parser's buffering, and a call that must not consume copies the *lexer* (a
     * plain struct) rather than the token, which is what makes the arrow-function speculation cheap.
     */
    JsToken current;
    JsHeap *heap;
    int32_t maxDepth;
    int32_t depth;
    int32_t nodeCount;
    int32_t maxNodes;
    /** Non-zero once a failure was reported; every later door answers `RoboParseError`. */
    int failed;
    JsParserError parseError;
} JsParser;

/**
 * Prepare @p parser over @p source (length < 0 → strlen).
 * @return RoboOk; RoboInvalidArgument on a NULL parser/heap; RoboOutOfMemory from the lexer or an
 *         internal allocation.
 */
Result JsParserInit(JsParser *parser,
                    JsHeap *heap,
                    const char *source,
                    int32_t length,
                    int32_t maxDepth);

/**
 * Parse the whole source as a program.
 *
 * @return RoboOk with @p out set to a `RoboJsNodeProgram` node whose list is the statements;
 *         RoboParseError with `JsParserError` filled (a `SyntaxError`) when the source does not
 *         parse; RoboOutOfMemory when the heap refuses a node; `RoboJsErrorLimit` when the node or
 *         depth budget is exhausted.
 *
 * @p out is set to NULL on every failure, so a caller that forgets to check the result still cannot
 * hand the interpreter a half-built tree.
 */
Result JsParserParseProgram(JsParser *parser, JsNode **out);

/** Parse one expression (the door the tests use to keep an expectation small). */
Result JsParserParseExpression(JsParser *parser, JsNode **out);

/**
 * The parser's structured error; never NULL. `kind` is `RoboJsErrorNone` before a failure.
 *
 * Named `…Of` rather than `JsParserError` because the failure *record* already owns that name: one is
 * a struct a caller embeds and the other a door a caller asks, and a header that used one identifier
 * for both would not compile (`RoboJSParser.c` is where that was found).
 */
const JsError *JsParserErrorOf(const JsParser *parser);

/** The full parse failure record, or NULL when there was none. */
const JsParserError *JsParserFailure(const JsParser *parser);

/** Nodes the last successful parse built. Reported so a test can assert growth, not to be a metric. */
int32_t JsParserNodeCount(const JsParser *parser);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_JS_PARSER_H */
