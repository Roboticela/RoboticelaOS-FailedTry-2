/*
 * RoboJS's vocabulary, defined once (see `RoboJSTypes.h`).
 *
 * Three tables live here and nowhere else: the token-kind names, the node-kind names, and the
 * **binary operator table** the parser's precedence climbing and the AST printer both read. A
 * diagnostic that says `"expected )"` and a printer that says `"a + b"` are both reading these rows
 * rather than carrying their own copy of the language.
 */

#include "RoboJS/RoboJSTypes.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/* Tokens                                                              */
/* ------------------------------------------------------------------ */

/*
 * Kind to name. A table of pairs, not an array indexed by kind: the enum's values are grouped with
 * gaps so the families read apart in a debugger, and an offset-indexed array would silently shift
 * every name onto the wrong kind the day one family grows.
 */
typedef struct JsTokenName {
    uint32_t kind;
    const char *name;
} JsTokenName;

static const JsTokenName JsTokenNames[] = {
    { RoboJsTokenEnd, "end" },
    { RoboJsTokenIdentifier, "identifier" },
    { RoboJsTokenNumber, "number" },
    { RoboJsTokenString, "string" },
    { RoboJsTokenVar, "var" },
    { RoboJsTokenLet, "let" },
    { RoboJsTokenConst, "const" },
    { RoboJsTokenFunction, "function" },
    { RoboJsTokenReturn, "return" },
    { RoboJsTokenIf, "if" },
    { RoboJsTokenElse, "else" },
    { RoboJsTokenWhile, "while" },
    { RoboJsTokenDo, "do" },
    { RoboJsTokenFor, "for" },
    { RoboJsTokenBreak, "break" },
    { RoboJsTokenContinue, "continue" },
    { RoboJsTokenThrow, "throw" },
    { RoboJsTokenTry, "try" },
    { RoboJsTokenCatch, "catch" },
    { RoboJsTokenFinally, "finally" },
    { RoboJsTokenSwitch, "switch" },
    { RoboJsTokenCase, "case" },
    { RoboJsTokenDefault, "default" },
    { RoboJsTokenNew, "new" },
    { RoboJsTokenThis, "this" },
    { RoboJsTokenTypeof, "typeof" },
    { RoboJsTokenDelete, "delete" },
    { RoboJsTokenVoid, "void" },
    { RoboJsTokenIn, "in" },
    { RoboJsTokenInstanceof, "instanceof" },
    { RoboJsTokenTrue, "true" },
    { RoboJsTokenFalse, "false" },
    { RoboJsTokenNull, "null" },
    { RoboJsTokenUndefined, "undefined" },
    { RoboJsTokenLParen, "(" },
    { RoboJsTokenRParen, ")" },
    { RoboJsTokenLBrace, "{" },
    { RoboJsTokenRBrace, "}" },
    { RoboJsTokenLBracket, "[" },
    { RoboJsTokenRBracket, "]" },
    { RoboJsTokenDot, "." },
    { RoboJsTokenComma, "," },
    { RoboJsTokenSemicolon, ";" },
    { RoboJsTokenColon, ":" },
    { RoboJsTokenQuestion, "?" },
    { RoboJsTokenArrow, "=>" },
    { RoboJsTokenAssign, "=" },
    { RoboJsTokenPlus, "+" },
    { RoboJsTokenMinus, "-" },
    { RoboJsTokenStar, "*" },
    { RoboJsTokenSlash, "/" },
    { RoboJsTokenPercent, "%" },
    { RoboJsTokenEqEq, "==" },
    { RoboJsTokenEqEqEq, "===" },
    { RoboJsTokenNotEq, "!=" },
    { RoboJsTokenNotEqEq, "!==" },
    { RoboJsTokenLt, "<" },
    { RoboJsTokenGt, ">" },
    { RoboJsTokenLtEq, "<=" },
    { RoboJsTokenGtEq, ">=" },
    { RoboJsTokenAndAnd, "&&" },
    { RoboJsTokenOrOr, "||" },
    { RoboJsTokenNot, "!" },
    { RoboJsTokenPlusPlus, "++" },
    { RoboJsTokenMinusMinus, "--" },
    { RoboJsTokenPlusAssign, "+=" },
    { RoboJsTokenMinusAssign, "-=" },
    { RoboJsTokenStarAssign, "*=" },
    { RoboJsTokenSlashAssign, "/=" },
    { RoboJsTokenPercentAssign, "%=" }
};

#define JsTokenNameCount ((int)(sizeof(JsTokenNames) / sizeof(JsTokenNames[0])))

int JsTokenKindIsKnown(JsTokenKind kind)
{
    return JsTokenKindName(kind) != NULL;
}

const char *JsTokenKindName(JsTokenKind kind)
{
    int i;

    for (i = 0; i < JsTokenNameCount; ++i) {
        if (JsTokenNames[i].kind == (uint32_t)kind) {
            return JsTokenNames[i].name;
        }
    }
    return NULL;
}

const char *JsTokenKindText(JsTokenKind kind)
{
    switch (kind) {
    case RoboJsTokenLParen: return "(";
    case RoboJsTokenRParen: return ")";
    case RoboJsTokenLBrace: return "{";
    case RoboJsTokenRBrace: return "}";
    case RoboJsTokenLBracket: return "[";
    case RoboJsTokenRBracket: return "]";
    case RoboJsTokenDot: return ".";
    case RoboJsTokenComma: return ",";
    case RoboJsTokenSemicolon: return ";";
    case RoboJsTokenColon: return ":";
    case RoboJsTokenQuestion: return "?";
    case RoboJsTokenArrow: return "=>";
    case RoboJsTokenAssign: return "=";
    case RoboJsTokenPlus: return "+";
    case RoboJsTokenMinus: return "-";
    case RoboJsTokenStar: return "*";
    case RoboJsTokenSlash: return "/";
    case RoboJsTokenPercent: return "%";
    case RoboJsTokenEqEq: return "==";
    case RoboJsTokenEqEqEq: return "===";
    case RoboJsTokenNotEq: return "!=";
    case RoboJsTokenNotEqEq: return "!==";
    case RoboJsTokenLt: return "<";
    case RoboJsTokenGt: return ">";
    case RoboJsTokenLtEq: return "<=";
    case RoboJsTokenGtEq: return ">=";
    case RoboJsTokenAndAnd: return "&&";
    case RoboJsTokenOrOr: return "||";
    case RoboJsTokenNot: return "!";
    case RoboJsTokenPlusPlus: return "++";
    case RoboJsTokenMinusMinus: return "--";
    case RoboJsTokenPlusAssign: return "+=";
    case RoboJsTokenMinusAssign: return "-=";
    case RoboJsTokenStarAssign: return "*=";
    case RoboJsTokenSlashAssign: return "/=";
    case RoboJsTokenPercentAssign: return "%=";
    case RoboJsTokenTypeof: return "typeof";
    case RoboJsTokenDelete: return "delete";
    case RoboJsTokenVoid: return "void";
    case RoboJsTokenNew: return "new";
    case RoboJsTokenThis: return "this";
    case RoboJsTokenIn: return "in";
    case RoboJsTokenInstanceof: return "instanceof";
    default: return NULL;
    }
}

/*
 * Precedence, from the language: assignment lowest and right-associative, then `||`, `&&`, equality,
 * relational, additive, multiplicative. A number is only ever compared with another number from this
 * same table, so the exact values are private; a caller reading one should treat it as a rank.
 */
static const JsBinaryOpInfo JsBinaryOps[] = {
    { RoboJsTokenAssign, 1, 1, "=" },
    { RoboJsTokenPlusAssign, 1, 1, "+=" },
    { RoboJsTokenMinusAssign, 1, 1, "-=" },
    { RoboJsTokenStarAssign, 1, 1, "*=" },
    { RoboJsTokenSlashAssign, 1, 1, "/=" },
    { RoboJsTokenPercentAssign, 1, 1, "%=" },
    { RoboJsTokenOrOr, 2, 0, "||" },
    { RoboJsTokenAndAnd, 3, 0, "&&" },
    { RoboJsTokenEqEq, 4, 0, "==" },
    { RoboJsTokenEqEqEq, 4, 0, "===" },
    { RoboJsTokenNotEq, 4, 0, "!=" },
    { RoboJsTokenNotEqEq, 4, 0, "!==" },
    { RoboJsTokenLt, 5, 0, "<" },
    { RoboJsTokenGt, 5, 0, ">" },
    { RoboJsTokenLtEq, 5, 0, "<=" },
    { RoboJsTokenGtEq, 5, 0, ">=" },
    { RoboJsTokenIn, 5, 0, "in" },
    { RoboJsTokenInstanceof, 5, 0, "instanceof" },
    { RoboJsTokenPlus, 6, 0, "+" },
    { RoboJsTokenMinus, 6, 0, "-" },
    { RoboJsTokenStar, 7, 0, "*" },
    { RoboJsTokenSlash, 7, 0, "/" },
    { RoboJsTokenPercent, 7, 0, "%" }
};

static const int JsBinaryOpCount = (int)(sizeof(JsBinaryOps) / sizeof(JsBinaryOps[0]));

const JsBinaryOpInfo *JsBinaryOpInfoOf(JsTokenKind kind)
{
    int i;

    for (i = 0; i < JsBinaryOpCount; ++i) {
        if (JsBinaryOps[i].op == kind) {
            return &JsBinaryOps[i];
        }
    }
    return NULL;
}

/* ------------------------------------------------------------------ */
/* Spans                                                               */
/* ------------------------------------------------------------------ */

void JsSpanInit(JsSpan *span)
{
    if (span == NULL) {
        return;
    }
    span->line = 1;
    span->column = 1;
    span->offset = 0;
    span->endLine = 1;
    span->endColumn = 1;
    span->endOffset = 0;
}

void JsSpanCover(JsSpan *out, const JsSpan *first, const JsSpan *last)
{
    if (out == NULL || first == NULL || last == NULL) {
        return;
    }
    *out = *first;
    out->endLine = last->endLine;
    out->endColumn = last->endColumn;
    out->endOffset = last->endOffset;
}

/* ------------------------------------------------------------------ */
/* Node kinds                                                          */
/* ------------------------------------------------------------------ */

/* Order matches `JsNodeKind`; `JsNodeKindName` indexes it directly, so a new kind is one row. */
static const char *const JsNodeKindNames[] = {
    "Program",
    "Identifier",
    "NumberLiteral",
    "StringLiteral",
    "BooleanLiteral",
    "NullLiteral",
    "UndefinedLiteral",
    "ThisExpression",
    "ArrayLiteral",
    "ObjectLiteral",
    "Property",
    "FunctionExpression",
    "FunctionDeclaration",
    "ArrowFunction",
    "VariableDeclaration",
    "VariableDeclarator",
    "ExpressionStatement",
    "BlockStatement",
    "EmptyStatement",
    "IfStatement",
    "WhileStatement",
    "DoWhileStatement",
    "ForStatement",
    "BreakStatement",
    "ContinueStatement",
    "ReturnStatement",
    "ThrowStatement",
    "TryStatement",
    "CatchClause",
    "SwitchStatement",
    "SwitchCase",
    "UnaryExpression",
    "UpdateExpression",
    "BinaryExpression",
    "LogicalExpression",
    "AssignmentExpression",
    "ConditionalExpression",
    "CallExpression",
    "NewExpression",
    "MemberExpression",
    "SequenceExpression"
};

#define JsNodeKindCount ((int)(sizeof(JsNodeKindNames) / sizeof(JsNodeKindNames[0])))

int JsNodeKindIsKnown(JsNodeKind kind)
{
    return (int)kind >= 0 && (int)kind < JsNodeKindCount;
}

const char *JsNodeKindName(JsNodeKind kind)
{
    if (!JsNodeKindIsKnown(kind)) {
        return "Unknown";
    }
    return JsNodeKindNames[(int)kind];
}

/* ------------------------------------------------------------------ */
/* Completions                                                         */
/* ------------------------------------------------------------------ */

const char *JsCompletionKindName(JsCompletionKind kind)
{
    switch (kind) {
    case RoboJsCompleteNormal: return "Normal";
    case RoboJsCompleteReturn: return "Return";
    case RoboJsCompleteBreak: return "Break";
    case RoboJsCompleteContinue: return "Continue";
    case RoboJsCompleteThrow: return "Throw";
    default: return "Unknown";
    }
}

/* ------------------------------------------------------------------ */
/* Errors                                                              */
/* ------------------------------------------------------------------ */

int JsErrorKindIsKnown(JsErrorKind kind)
{
    return (int)kind >= 0 && (int)kind <= (int)RoboJsErrorLimit;
}

const char *JsErrorKindName(JsErrorKind kind)
{
    switch (kind) {
    case RoboJsErrorNone: return "";
    case RoboJsErrorGeneral: return "Error";
    case RoboJsErrorType: return "TypeError";
    case RoboJsErrorReference: return "ReferenceError";
    case RoboJsErrorRange: return "RangeError";
    case RoboJsErrorSyntax: return "SyntaxError";
    case RoboJsErrorInternal: return "InternalError";
    case RoboJsErrorLimit: return "RangeError";
    default: return "Error";
    }
}

void JsErrorInit(JsError *error)
{
    if (error == NULL) {
        return;
    }
    memset(error, 0, sizeof(*error));
    error->kind = RoboJsErrorNone;
    JsSpanInit(&error->span);
}

void JsErrorSet(JsError *error,
                JsErrorKind kind,
                const JsSpan *span,
                const char *format,
                ...)
{
    va_list args;

    if (error == NULL) {
        return;
    }
    error->kind = kind;
    error->hasSpan = span != NULL ? 1 : 0;
    if (span != NULL) {
        error->span = *span;
    }
    if (format == NULL) {
        error->message[0] = '\0';
        return;
    }
    va_start(args, format);
    vsnprintf(error->message, sizeof(error->message), format, args);
    va_end(args);
}
