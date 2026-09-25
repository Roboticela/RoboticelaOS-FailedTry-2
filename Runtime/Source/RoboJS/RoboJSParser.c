/*
 * The RoboJS parser (see `RoboJSParser.h`).
 *
 * ## Shape
 *
 * One token of lookahead, one function per grammar family, and precedence climbing for the binary
 * operators — so all twenty-one of them share one loop that reads `JsBinaryOpInfoOf` from
 * `RoboJSTypes.c`. A parenthesised expression that might be an arrow function's parameter list is
 * decided by **speculation**: the lexer is copied (it is a plain struct), the arrow parse is
 * attempted, and a failure rewinds to the copy. The nodes a failed attempt allocated stay in the heap,
 * which is the whole reason the heap is released as a whole (`RoboJSHeap.h`).
 *
 * ## What the grammar covers, and what it refuses
 *
 * Declarations (`var`/`let`/`const`, `function`), every statement in 16.2's list, the whole expression
 * family, `new`, member access, calls, arity-checked arrow functions, object and array literals, and
 * automatic semicolon insertion at a newline, before `}` and at end of input. It refuses — with a
 * located `SyntaxError` naming the token — classes, destructuring, spread, templates, `async`,
 * generators, labels, `for...of`/`for...in`, and an object literal's shorthand property.
 */

#include "RoboJS/RoboJSParser.h"

#include <stdio.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/* Tokens                                                              */
/* ------------------------------------------------------------------ */

static Result JsParserNextToken(JsParser *parser)
{
    Result status;

    /*
     * No `parser->lexer.peeked = 0` here. A peeked token is *one already lexed and not yet
     * consumed*, so `JsLexerNext` hands it back and clears the flag itself; discarding the flag
     * instead would re-lex from a cursor already past that token and silently swallow it -- which
     * is how `a + b` parsed as `a`, and `console.log` as `console`. The speculation in
     * `JsParsePrimary` rewinds the whole lexer struct, flag and cached token included, so it stays
     * consistent with this door.
     */
    status = JsLexerNext(&parser->lexer, &parser->current);
    if (status != RoboOk) {
        const JsError *error = JsLexerError(&parser->lexer);

        parser->failed = 1;
        parser->parseError.error = *error;
        parser->parseError.hasToken = 0;
        return status;
    }
    return RoboOk;
}

static Result JsParserPeekToken(JsParser *parser, JsToken *out)
{
    Result status = JsLexerPeek(&parser->lexer, out);

    return status;
}

static Result JsParserFail(JsParser *parser, const JsToken *token, const char *expected)
{
    char message[200];

    if (parser->failed) {
        return RoboParseError;
    }
    snprintf(message,
             sizeof(message),
             "expected %s but found %s",
             expected != NULL ? expected : "something else",
             token != NULL ? JsTokenKindName((JsTokenKind)token->kind) : "the end of the input");
    parser->failed = 1;
    parser->parseError.hasToken = token != NULL;
    if (token != NULL) {
        parser->parseError.token = (JsTokenKind)token->kind;
    }
    JsErrorSet(&parser->parseError.error,
               RoboJsErrorSyntax,
               token != NULL ? &token->span : NULL,
               "%s",
               message);
    return RoboParseError;
}

static int JsParserAt(JsParser *parser, JsTokenKind kind)
{
    return (JsTokenKind)parser->current.kind == kind;
}

static Result JsParserExpect(JsParser *parser, JsTokenKind kind, JsToken *out);

static JsNode *JsParserNode(JsParser *parser, JsNodeKind kind, const JsSpan *span)
{
    JsNode *node;

    parser->nodeCount += 1;
    if (parser->maxNodes > 0 && parser->nodeCount > parser->maxNodes) {
        parser->failed = 1;
        parser->parseError.hasToken = 0;
        JsErrorSet(&parser->parseError.error,
                   RoboJsErrorLimit,
                   span,
                   "the source is too large to parse (limit %d nodes)",
                   parser->maxNodes);
        return NULL;
    }
    node = JsNodeCreate(parser->heap, kind, span);
    return node;
}

static int JsParserEnter(JsParser *parser, const JsSpan *span)
{
    parser->depth += 1;
    if (parser->maxDepth > 0 && parser->depth > parser->maxDepth) {
        parser->failed = 1;
        parser->parseError.hasToken = 0;
        JsErrorSet(&parser->parseError.error,
                   RoboJsErrorLimit,
                   span,
                   "the source nests deeper than the limit of %d",
                   parser->maxDepth);
        return 0;
    }
    return 1;
}

static void JsParserLeave(JsParser *parser)
{
    if (parser->depth > 0) {
        parser->depth -= 1;
    }
}

/* ------------------------------------------------------------------ */
/* Expressions                                                         */
/* ------------------------------------------------------------------ */

static JsNode *JsParseAssignment(JsParser *parser);
static JsNode *JsParseExpression(JsParser *parser);
static JsNode *JsParseStatement(JsParser *parser);

static int JsIsAssignOp(JsTokenKind kind)
{
    switch (kind) {
    case RoboJsTokenAssign:
    case RoboJsTokenPlusAssign:
    case RoboJsTokenMinusAssign:
    case RoboJsTokenStarAssign:
    case RoboJsTokenSlashAssign:
    case RoboJsTokenPercentAssign:
        return 1;
    default:
        return 0;
    }
}

static Result JsParserExpect(JsParser *parser, JsTokenKind kind, JsToken *out)
{
    if (!JsParserAt(parser, kind)) {
        return JsParserFail(parser, &parser->current, JsTokenKindName(kind));
    }
    if (out != NULL) {
        *out = parser->current;
    }
    return JsParserNextToken(parser);
}

static JsNode *JsParseIdentifierNode(JsParser *parser)
{
    JsToken token;
    JsNode *node;

    if (!JsParserAt(parser, RoboJsTokenIdentifier)) {
        JsParserFail(parser, &parser->current, "an identifier");
        return NULL;
    }
    token = parser->current;
    node = JsParserNode(parser, RoboJsNodeIdentifier, &token.span);
    if (node == NULL) {
        return NULL;
    }
    node->text = JsHeapString(parser->heap, token.text, token.length);
    node->textLength = token.length;
    if (node->text == NULL) {
        JsErrorSet(&parser->parseError.error, RoboJsErrorInternal, &token.span, "out of memory");
        parser->failed = 1;
        return NULL;
    }
    if (JsParserNextToken(parser) != RoboOk) {
        return NULL;
    }
    return node;
}

static JsNode *JsParseObjectLiteral(JsParser *parser)
{
    JsSpan start = parser->current.span;
    JsNode *node = JsParserNode(parser, RoboJsNodeObjectLiteral, &start);

    if (node == NULL) {
        return NULL;
    }
    if (JsParserNextToken(parser) != RoboOk) {
        return NULL;
    }
    while (!JsParserAt(parser, RoboJsTokenRBrace)) {
        JsNode *property;
        JsToken keyToken;
        char keyText[128];
        int32_t keyLength;

        if (JsParserAt(parser, RoboJsTokenEnd)) {
            JsParserFail(parser, &parser->current, "a property or }");
            return NULL;
        }
        keyToken = parser->current;
        if (JsParserAt(parser, RoboJsTokenIdentifier) || JsParserAt(parser, RoboJsTokenString) ||
            JsParserAt(parser, RoboJsTokenNumber) || JsTokenKindIsKeyword((JsTokenKind)keyToken.kind)) {
            keyLength = JsTokenCopyText(&keyToken, keyText, sizeof(keyText));
            if (JsParserNextToken(parser) != RoboOk) {
                return NULL;
            }
        } else {
            JsParserFail(parser, &parser->current, "a property name");
            return NULL;
        }
        if (!JsParserAt(parser, RoboJsTokenColon)) {
            JsParserFail(parser, &parser->current, ":");
            return NULL;
        }
        if (JsParserNextToken(parser) != RoboOk) {
            return NULL;
        }
        {
            JsNode *value = JsParseAssignment(parser);

            if (value == NULL) {
                return NULL;
            }
            property = JsParserNode(parser, RoboJsNodeProperty, &keyToken.span);
            if (property == NULL) {
                return NULL;
            }
            property->first = JsParserNode(parser, RoboJsNodeStringLiteral, &keyToken.span);
            if (property->first == NULL) {
                return NULL;
            }
            property->first->text = JsHeapString(parser->heap, keyText, keyLength);
            if (property->first->text == NULL) {
                parser->failed = 1;
                return NULL;
            }
            property->second = value;
            if (JsNodeAppend(parser->heap, node, property) != RoboOk) {
                parser->failed = 1;
                return NULL;
            }
        }
        if (JsParserAt(parser, RoboJsTokenComma)) {
            if (JsParserNextToken(parser) != RoboOk) {
                return NULL;
            }
        } else if (!JsParserAt(parser, RoboJsTokenRBrace)) {
            JsParserFail(parser, &parser->current, ", or }");
            return NULL;
        }
    }
    JsSpanCover(&node->span, &start, &parser->current.span);
    return JsParserNextToken(parser) == RoboOk ? node : NULL;
}

static JsNode *JsParseArrayLiteral(JsParser *parser)
{
    JsSpan start = parser->current.span;
    JsNode *node = JsParserNode(parser, RoboJsNodeArrayLiteral, &start);

    if (node == NULL) {
        return NULL;
    }
    if (JsParserNextToken(parser) != RoboOk) {
        return NULL;
    }
    while (!JsParserAt(parser, RoboJsTokenRBracket)) {
        JsNode *element;

        if (JsParserAt(parser, RoboJsTokenEnd)) {
            JsParserFail(parser, &parser->current, "an element or ]");
            return NULL;
        }
        if (JsParserAt(parser, RoboJsTokenComma)) {
            /* A hole: `[1, , 3]`. The element is `undefined`, which is what the language says. */
            element = JsParserNode(parser, RoboJsNodeUndefinedLiteral, &parser->current.span);
        } else {
            element = JsParseAssignment(parser);
        }
        if (element == NULL) {
            return NULL;
        }
        if (JsNodeAppend(parser->heap, node, element) != RoboOk) {
            parser->failed = 1;
            return NULL;
        }
        if (JsParserAt(parser, RoboJsTokenComma)) {
            if (JsParserNextToken(parser) != RoboOk) {
                return NULL;
            }
        } else if (!JsParserAt(parser, RoboJsTokenRBracket)) {
            JsParserFail(parser, &parser->current, ", or ]");
            return NULL;
        }
    }
    JsSpanCover(&node->span, &start, &parser->current.span);
    return JsParserNextToken(parser) == RoboOk ? node : NULL;
}

static JsNode *JsParseFunctionRest(JsParser *parser, JsNodeKind kind, const JsSpan *start)
{
    JsNode *node;
    const char *params[32];
    int32_t paramCount = 0;

    /* The name is optional in the grammar and required for a declaration — the statement arm checks
     * that before calling here, so this door only reads what is there. */
    node = JsParserNode(parser, kind, start);
    if (node == NULL) {
        return NULL;
    }
    if (JsParserAt(parser, RoboJsTokenIdentifier)) {
        JsToken nameToken = parser->current;

        node->text = JsHeapString(parser->heap, nameToken.text, nameToken.length);
        if (node->text == NULL || JsParserNextToken(parser) != RoboOk) {
            return NULL;
        }
    } else {
        node->text = JsHeapString(parser->heap, "", -1);
        if (node->text == NULL) {
            return NULL;
        }
    }
    if (!JsParserAt(parser, RoboJsTokenLParen)) {
        JsParserFail(parser, &parser->current, "(");
        return NULL;
    }
    if (JsParserNextToken(parser) != RoboOk) {
        return NULL;
    }
    while (!JsParserAt(parser, RoboJsTokenRParen)) {
        JsToken param;

        if (!JsParserAt(parser, RoboJsTokenIdentifier) || paramCount >= 32) {
            JsParserFail(parser, &parser->current, "a parameter name");
            return NULL;
        }
        param = parser->current;
        {
            char name[128];

            JsTokenCopyText(&param, name, sizeof(name));
            params[paramCount] = JsHeapString(parser->heap, name, -1);
            if (params[paramCount] == NULL) {
                parser->failed = 1;
                return NULL;
            }
            paramCount += 1;
        }
        if (JsParserNextToken(parser) != RoboOk) {
            return NULL;
        }
        if (JsParserAt(parser, RoboJsTokenComma)) {
            if (JsParserNextToken(parser) != RoboOk) {
                return NULL;
            }
        } else if (!JsParserAt(parser, RoboJsTokenRParen)) {
            JsParserFail(parser, &parser->current, ", or )");
            return NULL;
        }
    }
    if (JsParserNextToken(parser) != RoboOk) {
        return NULL;
    }
    {
        const char **owned;

        node->params = NULL;
        node->paramCount = paramCount;
        if (paramCount > 0) {
            owned = (const char **)JsHeapAllocArray(parser->heap, (size_t)paramCount, sizeof(char *));
            int32_t i;

            if (owned == NULL) {
                parser->failed = 1;
                return NULL;
            }
            for (i = 0; i < paramCount; ++i) {
                owned[i] = params[i];
            }
            node->params = owned;
        }
    }
    {
        JsNode *body = JsParseStatement(parser);

        if (body == NULL) {
            return NULL;
        }
        node->first = body;
        JsSpanCover(&node->span, start, &body->span);
    }
    return node;
}

/* ------------------------------------------------------------------ */
/* Primary and friends                                                 */
/* ------------------------------------------------------------------ */

static JsNode *JsParsePrimary(JsParser *parser)
{
    JsToken token = parser->current;

    switch ((JsTokenKind)token.kind) {
    case RoboJsTokenNumber: {
        JsNode *node = JsParserNode(parser, RoboJsNodeNumberLiteral, &token.span);

        if (node == NULL) {
            return NULL;
        }
        node->number = token.number;
        return JsParserNextToken(parser) == RoboOk ? node : NULL;
    }
    case RoboJsTokenString: {
        JsNode *node = JsParserNode(parser, RoboJsNodeStringLiteral, &token.span);

        if (node == NULL) {
            return NULL;
        }
        node->text = JsHeapString(parser->heap, token.text, token.length);
        node->textLength = token.length;
        if (node->text == NULL) {
            parser->failed = 1;
            return NULL;
        }
        return JsParserNextToken(parser) == RoboOk ? node : NULL;
    }
    case RoboJsTokenTrue:
    case RoboJsTokenFalse: {
        JsNode *node = JsParserNode(parser, RoboJsNodeBooleanLiteral, &token.span);

        if (node == NULL) {
            return NULL;
        }
        node->boolean = (JsTokenKind)token.kind == RoboJsTokenTrue;
        return JsParserNextToken(parser) == RoboOk ? node : NULL;
    }
    case RoboJsTokenNull: {
        JsNode *node = JsParserNode(parser, RoboJsNodeNullLiteral, &token.span);

        return node != NULL && JsParserNextToken(parser) != RoboOk ? NULL : node;
    }
    case RoboJsTokenUndefined: {
        JsNode *node = JsParserNode(parser, RoboJsNodeUndefinedLiteral, &token.span);

        return node != NULL && JsParserNextToken(parser) != RoboOk ? NULL : node;
    }
    case RoboJsTokenThis: {
        JsNode *node = JsParserNode(parser, RoboJsNodeThisExpression, &token.span);

        return node != NULL && JsParserNextToken(parser) != RoboOk ? NULL : node;
    }
    case RoboJsTokenIdentifier: {
        JsToken lookahead;

        if (JsParserPeekToken(parser, &lookahead) != RoboOk) {
            return NULL;
        }
        if ((JsTokenKind)lookahead.kind == RoboJsTokenArrow) {
            /* `x => ...`: a single-parameter arrow with no parentheses. */
            JsNode *node = JsParserNode(parser, RoboJsNodeArrowFunction, &token.span);
            const char **owned;

            if (node == NULL) {
                return NULL;
            }
            owned = (const char **)JsHeapAllocArray(parser->heap, 1u, sizeof(char *));
            if (owned == NULL) {
                parser->failed = 1;
                return NULL;
            }
            owned[0] = JsHeapString(parser->heap, token.text, token.length);
            node->params = owned;
            node->paramCount = 1;
            if (JsParserNextToken(parser) != RoboOk) { /* identifier */
                return NULL;
            }
            if (JsParserNextToken(parser) != RoboOk) { /* => */
                return NULL;
            }
            node->first = JsParseAssignment(parser);
            if (node->first == NULL) {
                return NULL;
            }
            JsSpanCover(&node->span, &token.span, &node->first->span);
            return node;
        }
        return JsParseIdentifierNode(parser);
    }
    case RoboJsTokenLParen: {
        /* Speculation: the lexer is a plain struct, so a copy is a snapshot. */
        JsLexer savedLexer = parser->lexer;
        JsToken savedCurrent = parser->current;
        JsNode *arrow = NULL;

        {
            int ok = 1;
            const char *names[32];
            int32_t count = 0;
            JsSpan start = token.span;

            if (JsParserNextToken(parser) != RoboOk) {
                return NULL;
            }
            while (!JsParserAt(parser, RoboJsTokenRParen)) {
                JsToken nameToken;

                if (!JsParserAt(parser, RoboJsTokenIdentifier) || count >= 32) {
                    ok = 0;
                    break;
                }
                nameToken = parser->current;
                {
                    char name[128];

                    JsTokenCopyText(&nameToken, name, sizeof(name));
                    names[count] = JsHeapString(parser->heap, name, -1);
                    count += 1;
                }
                if (JsParserNextToken(parser) != RoboOk) {
                    ok = 0;
                    break;
                }
                if (JsParserAt(parser, RoboJsTokenComma)) {
                    if (JsParserNextToken(parser) != RoboOk) {
                        ok = 0;
                        break;
                    }
                } else if (!JsParserAt(parser, RoboJsTokenRParen)) {
                    ok = 0;
                    break;
                }
            }
            if (ok && JsParserAt(parser, RoboJsTokenRParen)) {
                if (JsParserNextToken(parser) != RoboOk) {
                    ok = 0;
                }
            } else {
                ok = 0;
            }
            if (ok && JsParserAt(parser, RoboJsTokenArrow)) {
                int32_t i;

                if (JsParserNextToken(parser) != RoboOk) {
                    return NULL;
                }
                arrow = JsParserNode(parser, RoboJsNodeArrowFunction, &start);
                if (arrow == NULL) {
                    return NULL;
                }
                arrow->paramCount = count;
                if (count > 0) {
                    const char **owned =
                        (const char **)JsHeapAllocArray(parser->heap, (size_t)count, sizeof(char *));

                    if (owned == NULL) {
                        parser->failed = 1;
                        return NULL;
                    }
                    for (i = 0; i < count; ++i) {
                        owned[i] = names[i];
                    }
                    arrow->params = owned;
                }
                arrow->first = JsParseAssignment(parser);
                if (arrow->first == NULL) {
                    return NULL;
                }
                JsSpanCover(&arrow->span, &start, &arrow->first->span);
                return arrow;
            }
        }
        /* Not an arrow: rewind and read a grouped expression. */
        parser->lexer = savedLexer;
        parser->current = savedCurrent;
        {
            JsNode *inner;

            if (JsParserNextToken(parser) != RoboOk) {
                return NULL;
            }
            inner = JsParseExpression(parser);
            if (inner == NULL) {
                return NULL;
            }
            if (JsParserExpect(parser, RoboJsTokenRParen, NULL) != RoboOk) {
                return NULL;
            }
            return inner;
        }
    }
    case RoboJsTokenLBracket:
        return JsParseArrayLiteral(parser);
    case RoboJsTokenLBrace:
        return JsParseObjectLiteral(parser);
    case RoboJsTokenFunction:
        /* `JsParseFunctionRest` starts at the *name*, so the keyword is consumed here -- for an
         * expression it is optional and the rest reads an empty name for an anonymous function. */
        if (JsParserNextToken(parser) != RoboOk) {
            return NULL;
        }
        return JsParseFunctionRest(parser, RoboJsNodeFunctionExpression, &token.span);
    case RoboJsTokenNew: {
        JsNode *node = JsParserNode(parser, RoboJsNodeNewExpression, &token.span);
        JsNode *callee;

        if (node == NULL) {
            return NULL;
        }
        if (JsParserNextToken(parser) != RoboOk) {
            return NULL;
        }
        callee = JsParsePrimary(parser);
        if (callee == NULL) {
            return NULL;
        }
        /* `new f` with no argument list is legal; `new f()` reads its arguments here. */
        while (JsParserAt(parser, RoboJsTokenDot) || JsParserAt(parser, RoboJsTokenLBracket)) {
            JsNode *member = JsParserNode(parser,
                                          RoboJsNodeMemberExpression,
                                          &callee->span);

            if (member == NULL) {
                return NULL;
            }
            member->first = callee;
            if (JsParserAt(parser, RoboJsTokenDot)) {
                JsToken name;

                if (JsParserNextToken(parser) != RoboOk) {
                    return NULL;
                }
                if (!JsParserAt(parser, RoboJsTokenIdentifier)) {
                    JsParserFail(parser, &parser->current, "a property name");
                    return NULL;
                }
                name = parser->current;
                member->second = JsParserNode(parser, RoboJsNodeIdentifier, &name.span);
                if (member->second == NULL) {
                    return NULL;
                }
                member->second->text = JsHeapString(parser->heap, name.text, name.length);
                if (JsParserNextToken(parser) != RoboOk) {
                    return NULL;
                }
            } else {
                if (JsParserNextToken(parser) != RoboOk) {
                    return NULL;
                }
                member->intValue = 1;
                member->second = JsParseExpression(parser);
                if (member->second == NULL) {
                    return NULL;
                }
                if (JsParserExpect(parser, RoboJsTokenRBracket, NULL) != RoboOk) {
                    return NULL;
                }
            }
            callee = member;
        }
        node->first = callee;
        if (JsParserAt(parser, RoboJsTokenLParen)) {
            if (JsParserNextToken(parser) != RoboOk) {
                return NULL;
            }
            while (!JsParserAt(parser, RoboJsTokenRParen)) {
                JsNode *argument = JsParseAssignment(parser);

                if (argument == NULL) {
                    return NULL;
                }
                if (JsNodeAppend(parser->heap, node, argument) != RoboOk) {
                    parser->failed = 1;
                    return NULL;
                }
                if (JsParserAt(parser, RoboJsTokenComma)) {
                    if (JsParserNextToken(parser) != RoboOk) {
                        return NULL;
                    }
                } else if (!JsParserAt(parser, RoboJsTokenRParen)) {
                    JsParserFail(parser, &parser->current, ", or )");
                    return NULL;
                }
            }
            JsSpanCover(&node->span, &token.span, &parser->current.span);
            if (JsParserNextToken(parser) != RoboOk) {
                return NULL;
            }
        }
        return node;
    }
    default:
        JsParserFail(parser, &token, "an expression");
        return NULL;
    }
}

static JsNode *JsParseCallAndMember(JsParser *parser)
{
    JsNode *current = JsParsePrimary(parser);

    if (current == NULL) {
        return NULL;
    }
    for (;;) {
        if (JsParserAt(parser, RoboJsTokenDot)) {
            JsNode *member = JsParserNode(parser, RoboJsNodeMemberExpression, &current->span);
            JsToken name;

            if (member == NULL) {
                return NULL;
            }
            if (JsParserNextToken(parser) != RoboOk) {
                return NULL;
            }
            name = parser->current;
            if (!JsParserAt(parser, RoboJsTokenIdentifier) &&
                !JsTokenKindIsKeyword((JsTokenKind)name.kind)) {
                JsParserFail(parser, &parser->current, "a property name");
                return NULL;
            }
            member->first = current;
            member->second = JsParserNode(parser, RoboJsNodeStringLiteral, &name.span);
            if (member->second == NULL) {
                return NULL;
            }
            member->second->text = JsHeapString(parser->heap, name.text, name.length);
            member->second->textLength = name.length;
            if (member->second->text == NULL) {
                parser->failed = 1;
                return NULL;
            }
            JsSpanCover(&member->span, &current->span, &name.span);
            if (JsParserNextToken(parser) != RoboOk) {
                return NULL;
            }
            current = member;
            continue;
        }
        if (JsParserAt(parser, RoboJsTokenLBracket)) {
            JsNode *member = JsParserNode(parser, RoboJsNodeMemberExpression, &current->span);

            if (member == NULL) {
                return NULL;
            }
            if (JsParserNextToken(parser) != RoboOk) {
                return NULL;
            }
            member->first = current;
            member->intValue = 1;
            member->second = JsParseExpression(parser);
            if (member->second == NULL) {
                return NULL;
            }
            JsSpanCover(&member->span, &current->span, &parser->current.span);
            if (JsParserExpect(parser, RoboJsTokenRBracket, NULL) != RoboOk) {
                return NULL;
            }
            current = member;
            continue;
        }
        if (JsParserAt(parser, RoboJsTokenLParen)) {
            JsNode *call = JsParserNode(parser, RoboJsNodeCallExpression, &current->span);

            if (call == NULL) {
                return NULL;
            }
            call->first = current;
            if (JsParserNextToken(parser) != RoboOk) {
                return NULL;
            }
            while (!JsParserAt(parser, RoboJsTokenRParen)) {
                JsNode *argument;

                if (JsParserAt(parser, RoboJsTokenEnd)) {
                    JsParserFail(parser, &parser->current, "an argument or )");
                    return NULL;
                }
                argument = JsParseAssignment(parser);
                if (argument == NULL) {
                    return NULL;
                }
                if (JsNodeAppend(parser->heap, call, argument) != RoboOk) {
                    parser->failed = 1;
                    return NULL;
                }
                if (JsParserAt(parser, RoboJsTokenComma)) {
                    if (JsParserNextToken(parser) != RoboOk) {
                        return NULL;
                    }
                } else if (!JsParserAt(parser, RoboJsTokenRParen)) {
                    JsParserFail(parser, &parser->current, ", or )");
                    return NULL;
                }
            }
            JsSpanCover(&call->span, &current->span, &parser->current.span);
            if (JsParserNextToken(parser) != RoboOk) {
                return NULL;
            }
            current = call;
            continue;
        }
        break;
    }
    return current;
}

static JsNode *JsParseUpdate(JsParser *parser)
{
    JsNode *node = JsParseCallAndMember(parser);

    if (node == NULL) {
        return NULL;
    }
    if (JsParserAt(parser, RoboJsTokenPlusPlus) || JsParserAt(parser, RoboJsTokenMinusMinus)) {
        JsNode *update = JsParserNode(parser, RoboJsNodeUpdateExpression, &node->span);

        if (update == NULL) {
            return NULL;
        }
        update->op = (JsTokenKind)parser->current.kind;
        update->boolean = 1; /* postfix */
        update->first = node;
        JsSpanCover(&update->span, &node->span, &parser->current.span);
        if (JsParserNextToken(parser) != RoboOk) {
            return NULL;
        }
        return update;
    }
    return node;
}

static JsNode *JsParseUnary(JsParser *parser)
{
    JsTokenKind kind = (JsTokenKind)parser->current.kind;

    switch (kind) {
    case RoboJsTokenNot:
    case RoboJsTokenMinus:
    case RoboJsTokenPlus:
    case RoboJsTokenTypeof:
    case RoboJsTokenDelete:
    case RoboJsTokenVoid: {
        JsSpan span = parser->current.span;
        JsNode *node;

        if (!JsParserEnter(parser, &span)) {
            return NULL;
        }
        if (JsParserNextToken(parser) != RoboOk) {
            JsParserLeave(parser);
            return NULL;
        }
        node = JsParserNode(parser, RoboJsNodeUnaryExpression, &span);
        if (node == NULL) {
            JsParserLeave(parser);
            return NULL;
        }
        node->op = kind;
        node->first = JsParseUnary(parser);
        JsParserLeave(parser);
        if (node->first == NULL) {
            return NULL;
        }
        JsSpanCover(&node->span, &span, &node->first->span);
        return node;
    }
    case RoboJsTokenPlusPlus:
    case RoboJsTokenMinusMinus: {
        JsSpan span = parser->current.span;
        JsNode *node;

        if (JsParserNextToken(parser) != RoboOk) {
            return NULL;
        }
        node = JsParserNode(parser, RoboJsNodeUpdateExpression, &span);
        if (node == NULL) {
            return NULL;
        }
        node->op = kind;
        node->boolean = 0; /* prefix */
        node->first = JsParseUnary(parser);
        if (node->first == NULL) {
            return NULL;
        }
        JsSpanCover(&node->span, &span, &node->first->span);
        return node;
    }
    default:
        return JsParseUpdate(parser);
    }
}

static JsNode *JsParseBinary(JsParser *parser, int minPrecedence)
{
    JsNode *left = JsParseUnary(parser);

    if (left == NULL) {
        return NULL;
    }
    for (;;) {
        const JsBinaryOpInfo *info = JsBinaryOpInfoOf((JsTokenKind)parser->current.kind);
        JsNode *node;
        int nextMinimum;

        if (info == NULL || info->precedence < minPrecedence || info->precedence <= 1) {
            break;
        }
        if (!JsParserEnter(parser, &parser->current.span)) {
            return NULL;
        }
        nextMinimum = info->precedence + (info->rightAssociative ? 0 : 1);
        if (JsParserNextToken(parser) != RoboOk) {
            JsParserLeave(parser);
            return NULL;
        }
        node = JsParserNode(parser,
                            (info->op == RoboJsTokenAndAnd || info->op == RoboJsTokenOrOr)
                                ? RoboJsNodeLogicalExpression
                                : RoboJsNodeBinaryExpression,
                            &left->span);
        if (node == NULL) {
            JsParserLeave(parser);
            return NULL;
        }
        node->op = info->op;
        node->first = left;
        node->second = JsParseBinary(parser, nextMinimum);
        JsParserLeave(parser);
        if (node->second == NULL) {
            return NULL;
        }
        JsSpanCover(&node->span, &left->span, &node->second->span);
        left = node;
    }
    return left;
}

static JsNode *JsParseConditional(JsParser *parser)
{
    JsNode *test = JsParseBinary(parser, 2);

    if (test == NULL) {
        return NULL;
    }
    if (!JsParserAt(parser, RoboJsTokenQuestion)) {
        return test;
    }
    {
        JsNode *node;

        if (JsParserNextToken(parser) != RoboOk) {
            return NULL;
        }
        node = JsParserNode(parser, RoboJsNodeConditionalExpression, &test->span);
        if (node == NULL) {
            return NULL;
        }
        node->first = test;
        node->second = JsParseAssignment(parser);
        if (node->second == NULL) {
            return NULL;
        }
        if (JsParserExpect(parser, RoboJsTokenColon, NULL) != RoboOk) {
            return NULL;
        }
        node->third = JsParseAssignment(parser);
        if (node->third == NULL) {
            return NULL;
        }
        JsSpanCover(&node->span, &test->span, &node->third->span);
        return node;
    }
}

static JsNode *JsParseAssignment(JsParser *parser)
{
    JsNode *left;

    if (!JsParserEnter(parser, &parser->current.span)) {
        return NULL;
    }
    left = JsParseConditional(parser);
    if (left == NULL) {
        JsParserLeave(parser);
        return NULL;
    }
    if (JsIsAssignOp((JsTokenKind)parser->current.kind)) {
        JsNode *node;
        JsTokenKind op = (JsTokenKind)parser->current.kind;

        if ((JsNodeKind)left->kind != RoboJsNodeIdentifier &&
            (JsNodeKind)left->kind != RoboJsNodeMemberExpression) {
            JsParserFail(parser, &parser->current, "an assignable target before the operator");
            JsParserLeave(parser);
            return NULL;
        }
        if (JsParserNextToken(parser) != RoboOk) {
            JsParserLeave(parser);
            return NULL;
        }
        node = JsParserNode(parser, RoboJsNodeAssignmentExpression, &left->span);
        if (node == NULL) {
            JsParserLeave(parser);
            return NULL;
        }
        node->op = op;
        node->first = left;
        node->second = JsParseAssignment(parser);
        JsParserLeave(parser);
        if (node->second == NULL) {
            return NULL;
        }
        JsSpanCover(&node->span, &left->span, &node->second->span);
        return node;
    }
    JsParserLeave(parser);
    return left;
}

static JsNode *JsParseExpression(JsParser *parser)
{
    JsNode *first = JsParseAssignment(parser);

    if (first == NULL) {
        return NULL;
    }
    if (!JsParserAt(parser, RoboJsTokenComma)) {
        return first;
    }
    {
        JsNode *sequence = JsParserNode(parser, RoboJsNodeSequenceExpression, &first->span);

        if (sequence == NULL) {
            return NULL;
        }
        if (JsNodeAppend(parser->heap, sequence, first) != RoboOk) {
            parser->failed = 1;
            return NULL;
        }
        while (JsParserAt(parser, RoboJsTokenComma)) {
            JsNode *next;

            if (JsParserNextToken(parser) != RoboOk) {
                return NULL;
            }
            next = JsParseAssignment(parser);
            if (next == NULL) {
                return NULL;
            }
            if (JsNodeAppend(parser->heap, sequence, next) != RoboOk) {
                parser->failed = 1;
                return NULL;
            }
        }
        return sequence;
    }
}

/* ------------------------------------------------------------------ */
/* Statements                                                          */
/* ------------------------------------------------------------------ */

static Result JsParseSemicolon(JsParser *parser)
{
    if (JsParserAt(parser, RoboJsTokenSemicolon)) {
        return JsParserNextToken(parser);
    }
    if (JsParserAt(parser, RoboJsTokenRBrace) || JsParserAt(parser, RoboJsTokenEnd)) {
        return RoboOk;
    }
    return JsParserFail(parser, &parser->current, ";");
}

static JsNode *JsParseBlock(JsParser *parser)
{
    JsSpan start = parser->current.span;
    JsNode *block = JsParserNode(parser, RoboJsNodeBlockStatement, &start);

    if (block == NULL) {
        return NULL;
    }
    if (JsParserNextToken(parser) != RoboOk) {
        return NULL;
    }
    while (!JsParserAt(parser, RoboJsTokenRBrace)) {
        JsNode *statement;

        if (JsParserAt(parser, RoboJsTokenEnd)) {
            JsParserFail(parser, &parser->current, "a statement or }");
            return NULL;
        }
        statement = JsParseStatement(parser);
        if (statement == NULL) {
            return NULL;
        }
        if (JsNodeAppend(parser->heap, block, statement) != RoboOk) {
            parser->failed = 1;
            return NULL;
        }
    }
    JsSpanCover(&block->span, &start, &parser->current.span);
    return JsParserNextToken(parser) == RoboOk ? block : NULL;
}

static JsNode *JsParseVariableDeclaration(JsParser *parser)
{
    JsSpan start = parser->current.span;
    JsTokenKind kind = (JsTokenKind)parser->current.kind;
    JsNode *declaration = JsParserNode(parser, RoboJsNodeVariableDeclaration, &start);

    if (declaration == NULL) {
        return NULL;
    }
    declaration->op = kind;
    if (JsParserNextToken(parser) != RoboOk) {
        return NULL;
    }
    for (;;) {
        JsNode *declarator;
        JsNode *target;

        if (!JsParserAt(parser, RoboJsTokenIdentifier)) {
            JsParserFail(parser, &parser->current, "a variable name");
            return NULL;
        }
        declarator = JsParserNode(parser, RoboJsNodeVariableDeclarator, &parser->current.span);
        if (declarator == NULL) {
            return NULL;
        }
        target = JsParseIdentifierNode(parser);
        if (target == NULL) {
            return NULL;
        }
        declarator->first = target;
        if (JsParserAt(parser, RoboJsTokenAssign)) {
            if (JsParserNextToken(parser) != RoboOk) {
                return NULL;
            }
            declarator->second = JsParseAssignment(parser);
            if (declarator->second == NULL) {
                return NULL;
            }
        }
        JsSpanCover(&declarator->span, &target->span,
                    declarator->second != NULL ? &declarator->second->span : &target->span);
        if (JsNodeAppend(parser->heap, declaration, declarator) != RoboOk) {
            parser->failed = 1;
            return NULL;
        }
        if (JsParserAt(parser, RoboJsTokenComma)) {
            if (JsParserNextToken(parser) != RoboOk) {
                return NULL;
            }
            continue;
        }
        break;
    }
    if (JsParseSemicolon(parser) != RoboOk) {
        return NULL;
    }
    return declaration;
}

static JsNode *JsParseStatement(JsParser *parser)
{
    JsSpan start = parser->current.span;
    JsNode *node = NULL;

    if (!JsParserEnter(parser, &start)) {
        return NULL;
    }
    switch ((JsTokenKind)parser->current.kind) {
    case RoboJsTokenSemicolon:
        node = JsParserNode(parser, RoboJsNodeEmptyStatement, &start);
        if (node != NULL && JsParserNextToken(parser) != RoboOk) {
            node = NULL;
        }
        break;
    case RoboJsTokenLBrace:
        node = JsParseBlock(parser);
        break;
    case RoboJsTokenVar:
    case RoboJsTokenLet:
    case RoboJsTokenConst:
        node = JsParseVariableDeclaration(parser);
        break;
    case RoboJsTokenFunction: {
        JsToken lookahead;

        if (JsParserPeekToken(parser, &lookahead) != RoboOk) {
            node = NULL;
            break;
        }
        if ((JsTokenKind)lookahead.kind != RoboJsTokenIdentifier) {
            JsParserFail(parser, &lookahead, "a function name");
            node = NULL;
            break;
        }
        /* The name check above is the declaration's own rule; `JsParseFunctionRest` reads the name
         * itself, so only the keyword is consumed here. */
        if (JsParserNextToken(parser) != RoboOk) {
            node = NULL;
            break;
        }
        node = JsParseFunctionRest(parser, RoboJsNodeFunctionDeclaration, &start);
        break;
    }
    case RoboJsTokenIf: {
        if (JsParserNextToken(parser) != RoboOk) {
            break;
        }
        node = JsParserNode(parser, RoboJsNodeIfStatement, &start);
        if (node == NULL) {
            break;
        }
        if (JsParserExpect(parser, RoboJsTokenLParen, NULL) != RoboOk) {
            node = NULL;
            break;
        }
        node->first = JsParseExpression(parser);
        if (node->first == NULL) {
            node = NULL;
            break;
        }
        if (JsParserExpect(parser, RoboJsTokenRParen, NULL) != RoboOk) {
            node = NULL;
            break;
        }
        node->second = JsParseStatement(parser);
        if (node->second == NULL) {
            node = NULL;
            break;
        }
        if (JsParserAt(parser, RoboJsTokenElse)) {
            if (JsParserNextToken(parser) != RoboOk) {
                node = NULL;
                break;
            }
            node->third = JsParseStatement(parser);
            if (node->third == NULL) {
                node = NULL;
            }
        }
        break;
    }
    case RoboJsTokenWhile: {
        if (JsParserNextToken(parser) != RoboOk) {
            break;
        }
        node = JsParserNode(parser, RoboJsNodeWhileStatement, &start);
        if (node == NULL) {
            break;
        }
        if (JsParserExpect(parser, RoboJsTokenLParen, NULL) != RoboOk) {
            node = NULL;
            break;
        }
        node->first = JsParseExpression(parser);
        if (node->first == NULL ||
            JsParserExpect(parser, RoboJsTokenRParen, NULL) != RoboOk) {
            node = NULL;
            break;
        }
        node->second = JsParseStatement(parser);
        if (node->second == NULL) {
            node = NULL;
        }
        break;
    }
    case RoboJsTokenDo: {
        if (JsParserNextToken(parser) != RoboOk) {
            break;
        }
        node = JsParserNode(parser, RoboJsNodeDoWhileStatement, &start);
        if (node == NULL) {
            break;
        }
        node->second = JsParseStatement(parser);
        if (node->second == NULL) {
            node = NULL;
            break;
        }
        if (!JsParserAt(parser, RoboJsTokenWhile)) {
            JsParserFail(parser, &parser->current, "while");
            node = NULL;
            break;
        }
        if (JsParserNextToken(parser) != RoboOk) {
            node = NULL;
            break;
        }
        if (JsParserExpect(parser, RoboJsTokenLParen, NULL) != RoboOk) {
            node = NULL;
            break;
        }
        node->first = JsParseExpression(parser);
        if (node->first == NULL ||
            JsParserExpect(parser, RoboJsTokenRParen, NULL) != RoboOk) {
            node = NULL;
            break;
        }
        if (JsParseSemicolon(parser) != RoboOk) {
            node = NULL;
        }
        break;
    }
    case RoboJsTokenFor: {
        if (JsParserNextToken(parser) != RoboOk) {
            break;
        }
        node = JsParserNode(parser, RoboJsNodeForStatement, &start);
        if (node == NULL) {
            break;
        }
        if (JsParserExpect(parser, RoboJsTokenLParen, NULL) != RoboOk) {
            node = NULL;
            break;
        }
        if (JsParserAt(parser, RoboJsTokenSemicolon)) {
            if (JsParserNextToken(parser) != RoboOk) {
                node = NULL;
                break;
            }
        } else if (JsParserAt(parser, RoboJsTokenVar) || JsParserAt(parser, RoboJsTokenLet) ||
                   JsParserAt(parser, RoboJsTokenConst)) {
            node->first = JsParseVariableDeclaration(parser);
            if (node->first == NULL) {
                node = NULL;
                break;
            }
        } else {
            node->first = JsParseExpression(parser);
            if (node->first == NULL ||
                JsParserExpect(parser, RoboJsTokenSemicolon, NULL) != RoboOk) {
                node = NULL;
                break;
            }
        }
        if (!JsParserAt(parser, RoboJsTokenSemicolon)) {
            node->second = JsParseExpression(parser);
            if (node->second == NULL) {
                node = NULL;
                break;
            }
        }
        if (JsParserExpect(parser, RoboJsTokenSemicolon, NULL) != RoboOk) {
            node = NULL;
            break;
        }
        if (!JsParserAt(parser, RoboJsTokenRParen)) {
            node->third = JsParseExpression(parser);
            if (node->third == NULL) {
                node = NULL;
                break;
            }
        }
        if (JsParserExpect(parser, RoboJsTokenRParen, NULL) != RoboOk) {
            node = NULL;
            break;
        }
        {
            JsNode *body = JsParseStatement(parser);

            if (body == NULL || JsNodeAppend(parser->heap, node, body) != RoboOk) {
                node = NULL;
            }
        }
        break;
    }
    case RoboJsTokenBreak:
    case RoboJsTokenContinue: {
        JsNodeKind kind = (JsTokenKind)parser->current.kind == RoboJsTokenBreak
                              ? RoboJsNodeBreakStatement
                              : RoboJsNodeContinueStatement;

        node = JsParserNode(parser, kind, &start);
        if (node != NULL && JsParserNextToken(parser) != RoboOk) {
            node = NULL;
            break;
        }
        if (node != NULL && JsParseSemicolon(parser) != RoboOk) {
            node = NULL;
        }
        break;
    }
    case RoboJsTokenReturn: {
        if (JsParserNextToken(parser) != RoboOk) {
            break;
        }
        node = JsParserNode(parser, RoboJsNodeReturnStatement, &start);
        if (node == NULL) {
            break;
        }
        if (!JsParserAt(parser, RoboJsTokenSemicolon) && !JsParserAt(parser, RoboJsTokenRBrace) &&
            !JsParserAt(parser, RoboJsTokenEnd)) {
            node->first = JsParseExpression(parser);
            if (node->first == NULL) {
                node = NULL;
                break;
            }
        }
        if (JsParseSemicolon(parser) != RoboOk) {
            node = NULL;
        }
        break;
    }
    case RoboJsTokenThrow: {
        if (JsParserNextToken(parser) != RoboOk) {
            break;
        }
        node = JsParserNode(parser, RoboJsNodeThrowStatement, &start);
        if (node == NULL) {
            break;
        }
        node->first = JsParseExpression(parser);
        if (node->first == NULL || JsParseSemicolon(parser) != RoboOk) {
            node = NULL;
        }
        break;
    }
    case RoboJsTokenTry: {
        if (JsParserNextToken(parser) != RoboOk) {
            break;
        }
        node = JsParserNode(parser, RoboJsNodeTryStatement, &start);
        if (node == NULL) {
            break;
        }
        if (!JsParserAt(parser, RoboJsTokenLBrace)) {
            JsParserFail(parser, &parser->current, "{");
            node = NULL;
            break;
        }
        node->first = JsParseBlock(parser);
        if (node->first == NULL) {
            node = NULL;
            break;
        }
        if (JsParserAt(parser, RoboJsTokenCatch)) {
            JsNode *clause;
            JsSpan catchStart = parser->current.span;

            if (JsParserNextToken(parser) != RoboOk) {
                node = NULL;
                break;
            }
            clause = JsParserNode(parser, RoboJsNodeCatchClause, &catchStart);
            if (clause == NULL) {
                node = NULL;
                break;
            }
            if (JsParserExpect(parser, RoboJsTokenLParen, NULL) != RoboOk) {
                node = NULL;
                break;
            }
            clause->first = JsParseIdentifierNode(parser);
            if (clause->first == NULL ||
                JsParserExpect(parser, RoboJsTokenRParen, NULL) != RoboOk) {
                node = NULL;
                break;
            }
            if (!JsParserAt(parser, RoboJsTokenLBrace)) {
                JsParserFail(parser, &parser->current, "{");
                node = NULL;
                break;
            }
            clause->second = JsParseBlock(parser);
            if (clause->second == NULL) {
                node = NULL;
                break;
            }
            node->second = clause;
        }
        if (JsParserAt(parser, RoboJsTokenFinally)) {
            if (JsParserNextToken(parser) != RoboOk) {
                node = NULL;
                break;
            }
            if (!JsParserAt(parser, RoboJsTokenLBrace)) {
                JsParserFail(parser, &parser->current, "{");
                node = NULL;
                break;
            }
            node->third = JsParseBlock(parser);
            if (node->third == NULL) {
                node = NULL;
                break;
            }
        }
        if (node->second == NULL && node->third == NULL) {
            JsParserFail(parser, &parser->current, "catch or finally");
            node = NULL;
        }
        break;
    }
    case RoboJsTokenSwitch: {
        if (JsParserNextToken(parser) != RoboOk) {
            break;
        }
        node = JsParserNode(parser, RoboJsNodeSwitchStatement, &start);
        if (node == NULL) {
            break;
        }
        if (JsParserExpect(parser, RoboJsTokenLParen, NULL) != RoboOk) {
            node = NULL;
            break;
        }
        node->first = JsParseExpression(parser);
        if (node->first == NULL ||
            JsParserExpect(parser, RoboJsTokenRParen, NULL) != RoboOk) {
            node = NULL;
            break;
        }
        if (JsParserExpect(parser, RoboJsTokenLBrace, NULL) != RoboOk) {
            node = NULL;
            break;
        }
        while (!JsParserAt(parser, RoboJsTokenRBrace)) {
            JsNode *clause;

            if (JsParserAt(parser, RoboJsTokenEnd)) {
                JsParserFail(parser, &parser->current, "a case, default or }");
                node = NULL;
                break;
            }
            if (JsParserAt(parser, RoboJsTokenCase)) {
                JsSpan caseSpan = parser->current.span;

                if (JsParserNextToken(parser) != RoboOk) {
                    node = NULL;
                    break;
                }
                clause = JsParserNode(parser, RoboJsNodeSwitchCase, &caseSpan);
                if (clause == NULL) {
                    node = NULL;
                    break;
                }
                clause->first = JsParseExpression(parser);
                if (clause->first == NULL) {
                    node = NULL;
                    break;
                }
            } else if (JsParserAt(parser, RoboJsTokenDefault)) {
                clause = JsParserNode(parser, RoboJsNodeSwitchCase, &parser->current.span);
                if (clause == NULL) {
                    node = NULL;
                    break;
                }
                if (JsParserNextToken(parser) != RoboOk) {
                    node = NULL;
                    break;
                }
            } else {
                JsParserFail(parser, &parser->current, "case or default");
                node = NULL;
                break;
            }
            if (JsParserExpect(parser, RoboJsTokenColon, NULL) != RoboOk) {
                node = NULL;
                break;
            }
            while (!JsParserAt(parser, RoboJsTokenCase) && !JsParserAt(parser, RoboJsTokenDefault) &&
                   !JsParserAt(parser, RoboJsTokenRBrace) && !JsParserAt(parser, RoboJsTokenEnd)) {
                JsNode *statement = JsParseStatement(parser);

                if (statement == NULL) {
                    node = NULL;
                    break;
                }
                if (JsNodeAppend(parser->heap, clause, statement) != RoboOk) {
                    parser->failed = 1;
                    node = NULL;
                    break;
                }
            }
            if (node == NULL) {
                break;
            }
            if (JsNodeAppend(parser->heap, node, clause) != RoboOk) {
                parser->failed = 1;
                node = NULL;
                break;
            }
        }
        if (node != NULL) {
            if (JsParserExpect(parser, RoboJsTokenRBrace, NULL) != RoboOk) {
                node = NULL;
            }
        }
        break;
    }
    default: {
        JsNode *expression;

        if (!JsTokenKindStartsExpression((JsTokenKind)parser->current.kind)) {
            JsParserFail(parser, &parser->current, "a statement");
            JsParserLeave(parser);
            return NULL;
        }
        expression = JsParseExpression(parser);
        if (expression == NULL) {
            JsParserLeave(parser);
            return NULL;
        }
        node = JsParserNode(parser, RoboJsNodeExpressionStatement, &expression->span);
        if (node != NULL) {
            node->first = expression;
            JsSpanCover(&node->span, &expression->span, &expression->span);
            if (JsParseSemicolon(parser) != RoboOk) {
                node = NULL;
            }
        }
        break;
    }
    }
    if (node != NULL) {
        JsSpanCover(&node->span, &start, &parser->current.span);
    }
    JsParserLeave(parser);
    return node;
}

/* ------------------------------------------------------------------ */
/* Entry points                                                        */
/* ------------------------------------------------------------------ */

Result JsParserInit(JsParser *parser,
                    JsHeap *heap,
                    const char *source,
                    int32_t length,
                    int32_t maxDepth)
{
    Result status;

    if (parser == NULL || heap == NULL) {
        return RoboInvalidArgument;
    }
    memset(parser, 0, sizeof(*parser));
    parser->heap = heap;
    parser->maxDepth = maxDepth > 0 ? maxDepth : (int32_t)RoboJsDefaultMaxParseDepth;
    parser->maxNodes = 200000;
    JsErrorInit(&parser->parseError.error);
    status = JsLexerInit(&parser->lexer, heap, source, length);
    if (status != RoboOk) {
        return status;
    }
    return JsParserNextToken(parser);
}

Result JsParserParseProgram(JsParser *parser, JsNode **out)
{
    JsNode *program;
    JsSpan span;

    if (parser == NULL || out == NULL) {
        return RoboInvalidArgument;
    }
    *out = NULL;
    if (parser->failed) {
        return RoboParseError;
    }
    span = parser->current.span;
    program = JsParserNode(parser, RoboJsNodeProgram, &span);
    if (program == NULL) {
        return parser->parseError.error.kind == RoboJsErrorLimit ? RoboParseError : RoboOutOfMemory;
    }
    while (!JsParserAt(parser, RoboJsTokenEnd)) {
        JsNode *statement = JsParseStatement(parser);

        if (statement == NULL) {
            *out = NULL;
            return RoboParseError;
        }
        if (JsNodeAppend(parser->heap, program, statement) != RoboOk) {
            parser->failed = 1;
            JsErrorSet(&parser->parseError.error,
                       RoboJsErrorInternal,
                       &statement->span,
                       "out of memory building the program");
            return RoboOutOfMemory;
        }
    }
    JsSpanCover(&program->span, &span, &parser->current.span);
    *out = program;
    return RoboOk;
}

Result JsParserParseExpression(JsParser *parser, JsNode **out)
{
    JsNode *expression;

    if (parser == NULL || out == NULL) {
        return RoboInvalidArgument;
    }
    *out = NULL;
    if (parser->failed) {
        return RoboParseError;
    }
    expression = JsParseExpression(parser);
    if (expression == NULL) {
        return RoboParseError;
    }
    if (!JsParserAt(parser, RoboJsTokenEnd)) {
        JsParserFail(parser, &parser->current, "the end of the expression");
        return RoboParseError;
    }
    *out = expression;
    return RoboOk;
}

const JsError *JsParserErrorOf(const JsParser *parser)
{
    static const JsError none = { RoboJsErrorNone, { 0 }, { 1, 1, 0, 1, 1, 0 }, 0 };

    return parser != NULL ? &parser->parseError.error : &none;
}

const JsParserError *JsParserFailure(const JsParser *parser)
{
    if (parser == NULL || parser->parseError.error.kind == RoboJsErrorNone) {
        return NULL;
    }
    return &parser->parseError;
}

int32_t JsParserNodeCount(const JsParser *parser)
{
    return parser != NULL ? parser->nodeCount : 0;
}
