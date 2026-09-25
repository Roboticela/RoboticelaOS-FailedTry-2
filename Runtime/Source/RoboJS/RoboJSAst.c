/*
 * The RoboJS AST (see `RoboJSAst.h`).
 *
 * The **shape table** is the file's centre: one row per node kind saying how many ordered children it
 * uses and whether its list is meaningful. The printer, `JsAstIsWellFormed` and the evaluator all read
 * it, so "the parser built a node the printer cannot print" and "the parser built a node the
 * interpreter cannot run" are both checkable rather than discovered.
 *
 * The **printer** writes a one-line, re-parseable form: `if (a) { b; } else { c; }`, `a + b * c`,
 * `f(x, y)`. Precedence is handled by parenthesising a child whose operator binds less tightly than
 * its parent's, which is the rule that makes `(a + b) * c` print with its parentheses and `a + b * c`
 * without.
 */

#include "RoboJS/RoboJSAst.h"

#include "RoboJS/RoboJSValue.h"

#include <stdio.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/* Creation                                                            */
/* ------------------------------------------------------------------ */

JsNode *JsNodeCreate(JsHeap *heap, JsNodeKind kind, const JsSpan *span)
{
    JsNode *node;

    if (heap == NULL) {
        return NULL;
    }
    node = (JsNode *)JsHeapAlloc(heap, sizeof(JsNode));
    if (node == NULL) {
        return NULL;
    }
    node->kind = (uint32_t)kind;
    node->op = RoboJsTokenEnd;
    if (span != NULL) {
        node->span = *span;
    } else {
        JsSpanInit(&node->span);
    }
    return node;
}

Result JsNodeAppend(JsHeap *heap, JsNode *parent, JsNode *child)
{
    JsNode **fresh;
    int32_t capacity;

    if (heap == NULL || parent == NULL || child == NULL) {
        return RoboInvalidArgument;
    }
    if (parent->count == parent->capacity) {
        capacity = parent->capacity > 0 ? parent->capacity * 2 : 4;
        fresh = (JsNode **)JsHeapRealloc(heap,
                                         parent->list,
                                         (size_t)parent->capacity * sizeof(JsNode *),
                                         (size_t)capacity * sizeof(JsNode *));
        if (fresh == NULL) {
            return RoboOutOfMemory;
        }
        parent->list = fresh;
        parent->capacity = capacity;
    }
    parent->list[parent->count] = child;
    parent->count += 1;
    return RoboOk;
}

/* ------------------------------------------------------------------ */
/* Shape                                                               */
/* ------------------------------------------------------------------ */

static const JsNodeShape JsNodeShapes[] = {
    { "Program", 0, 1 },
    { "Identifier", 0, 0 },
    { "NumberLiteral", 0, 0 },
    { "StringLiteral", 0, 0 },
    { "BooleanLiteral", 0, 0 },
    { "NullLiteral", 0, 0 },
    { "UndefinedLiteral", 0, 0 },
    { "ThisExpression", 0, 0 },
    { "ArrayLiteral", 0, 1 },
    { "ObjectLiteral", 0, 1 },
    { "Property", 2, 0 },
    { "FunctionExpression", 1, 0 },
    { "FunctionDeclaration", 1, 0 },
    { "ArrowFunction", 1, 0 },
    { "VariableDeclaration", 0, 1 },
    { "VariableDeclarator", 2, 0 },
    { "ExpressionStatement", 1, 0 },
    { "BlockStatement", 0, 1 },
    { "EmptyStatement", 0, 0 },
    { "IfStatement", 3, 0 },
    { "WhileStatement", 2, 0 },
    { "DoWhileStatement", 2, 0 },
    { "ForStatement", 3, 1 },
    { "BreakStatement", 0, 0 },
    { "ContinueStatement", 0, 0 },
    { "ReturnStatement", 1, 0 },
    { "ThrowStatement", 1, 0 },
    { "TryStatement", 3, 0 },
    { "CatchClause", 2, 0 },
    { "SwitchStatement", 1, 1 },
    { "SwitchCase", 1, 1 },
    { "UnaryExpression", 1, 0 },
    { "UpdateExpression", 1, 0 },
    { "BinaryExpression", 2, 0 },
    { "LogicalExpression", 2, 0 },
    { "AssignmentExpression", 2, 0 },
    { "ConditionalExpression", 3, 0 },
    { "CallExpression", 1, 1 },
    { "NewExpression", 1, 1 },
    { "MemberExpression", 2, 0 },
    { "SequenceExpression", 0, 1 }
};

#define JsNodeShapeCount ((int)(sizeof(JsNodeShapes) / sizeof(JsNodeShapes[0])))

const JsNodeShape *JsNodeShapeOf(JsNodeKind kind)
{
    if (!JsNodeKindIsKnown(kind)) {
        return NULL;
    }
    if ((int)kind >= JsNodeShapeCount) {
        return NULL;
    }
    return &JsNodeShapes[(int)kind];
}

/* ------------------------------------------------------------------ */
/* Printer                                                             */
/* ------------------------------------------------------------------ */

typedef struct JsWriter {
    char *buffer;
    size_t capacity;
    size_t written;
} JsWriter;

static void JsWriteChar(JsWriter *writer, char c)
{
    if (writer->buffer != NULL && writer->written + 1u < writer->capacity) {
        writer->buffer[writer->written] = c;
    }
    writer->written += 1;
}

static void JsWriteText(JsWriter *writer, const char *text)
{
    size_t i;

    if (text == NULL) {
        return;
    }
    for (i = 0; text[i] != '\0'; ++i) {
        JsWriteChar(writer, text[i]);
    }
}

static void JsWriteNumber(JsWriter *writer, double number)
{
    char scratch[40];

    scratch[0] = '\0';
    JsNumberToString(number, scratch, sizeof(scratch));
    JsWriteText(writer, scratch);
}

static int JsPrecedenceOf(JsTokenKind op)
{
    const JsBinaryOpInfo *info = JsBinaryOpInfoOf(op);

    if (info != NULL) {
        return info->precedence;
    }
    switch (op) {
    case RoboJsTokenQuestion: return 1;
    case RoboJsTokenPlusPlus:
    case RoboJsTokenMinusMinus:
        return 8;
    case RoboJsTokenNot:
    case RoboJsTokenTypeof:
    case RoboJsTokenDelete:
    case RoboJsTokenVoid:
    case RoboJsTokenMinus:
    case RoboJsTokenPlus:
        return 8;
    default:
        return 9;
    }
}

static void JsPrintNode(JsWriter *writer, const JsNode *node, int parentPrecedence);

static void JsPrintList(JsWriter *writer, const JsNode *node, const char *separator)
{
    int32_t i;

    for (i = 0; i < node->count; ++i) {
        if (i > 0) {
            JsWriteText(writer, separator);
        }
        JsPrintNode(writer, node->list[i], 0);
    }
}

static void JsPrintChild(JsWriter *writer, const JsNode *child, int parentPrecedence)
{
    int own;

    if (child == NULL) {
        JsWriteText(writer, "<null>");
        return;
    }
    own = JsPrecedenceOf(child->op);
    if (own < parentPrecedence) {
        JsWriteChar(writer, '(');
        JsPrintNode(writer, child, parentPrecedence);
        JsWriteChar(writer, ')');
        return;
    }
    JsPrintNode(writer, child, parentPrecedence);
}

static void JsPrintFunction(JsWriter *writer, const JsNode *node, int isDeclaration)
{
    int32_t i;

    JsWriteText(writer, isDeclaration ? "function " : "function ");
    if (node->text != NULL && node->text[0] != '\0') {
        JsWriteText(writer, node->text);
    }
    JsWriteChar(writer, '(');
    for (i = 0; i < node->paramCount; ++i) {
        if (i > 0) {
            JsWriteText(writer, ", ");
        }
        JsWriteText(writer, node->params[i] != NULL ? node->params[i] : "");
    }
    JsWriteText(writer, ") ");
    JsPrintNode(writer, node->first, 0);
}

static void JsPrintNode(JsWriter *writer, const JsNode *node, int parentPrecedence)
{
    int32_t i;

    if (node == NULL) {
        JsWriteText(writer, "<null>");
        return;
    }
    switch ((JsNodeKind)node->kind) {
    case RoboJsNodeProgram:
        JsPrintList(writer, node, "\n");
        break;
    case RoboJsNodeIdentifier:
        JsWriteText(writer, node->text != NULL ? node->text : "");
        break;
    case RoboJsNodeNumberLiteral:
        JsWriteNumber(writer, node->number);
        break;
    case RoboJsNodeStringLiteral:
        JsWriteChar(writer, '"');
        JsWriteText(writer, node->text != NULL ? node->text : "");
        JsWriteChar(writer, '"');
        break;
    case RoboJsNodeBooleanLiteral:
        JsWriteText(writer, node->boolean ? "true" : "false");
        break;
    case RoboJsNodeNullLiteral:
        JsWriteText(writer, "null");
        break;
    case RoboJsNodeUndefinedLiteral:
        JsWriteText(writer, "undefined");
        break;
    case RoboJsNodeThisExpression:
        JsWriteText(writer, "this");
        break;
    case RoboJsNodeArrayLiteral:
        JsWriteChar(writer, '[');
        JsPrintList(writer, node, ", ");
        JsWriteChar(writer, ']');
        break;
    case RoboJsNodeObjectLiteral:
        JsWriteChar(writer, '{');
        JsPrintList(writer, node, ", ");
        JsWriteChar(writer, '}');
        break;
    case RoboJsNodeProperty:
        JsWriteText(writer, node->first != NULL ? node->first->text : "");
        JsWriteText(writer, ": ");
        JsPrintNode(writer, node->second, 0);
        break;
    case RoboJsNodeFunctionExpression:
        JsPrintFunction(writer, node, 0);
        break;
    case RoboJsNodeFunctionDeclaration:
        JsPrintFunction(writer, node, 1);
        break;
    case RoboJsNodeArrowFunction:
        JsWriteChar(writer, '(');
        for (i = 0; i < node->paramCount; ++i) {
            if (i > 0) {
                JsWriteText(writer, ", ");
            }
            JsWriteText(writer, node->params[i] != NULL ? node->params[i] : "");
        }
        JsWriteText(writer, ") => ");
        JsPrintNode(writer, node->first, 0);
        break;
    case RoboJsNodeVariableDeclaration:
        JsWriteText(writer, JsTokenKindName((JsTokenKind)node->op));
        JsWriteChar(writer, ' ');
        JsPrintList(writer, node, ", ");
        break;
    case RoboJsNodeVariableDeclarator:
        JsPrintNode(writer, node->first, 0);
        if (node->second != NULL) {
            JsWriteText(writer, " = ");
            JsPrintNode(writer, node->second, 1);
        }
        break;
    case RoboJsNodeExpressionStatement:
        JsPrintNode(writer, node->first, 0);
        JsWriteChar(writer, ';');
        break;
    case RoboJsNodeBlockStatement:
        JsWriteText(writer, "{ ");
        JsPrintList(writer, node, " ");
        JsWriteText(writer, " }");
        break;
    case RoboJsNodeEmptyStatement:
        JsWriteChar(writer, ';');
        break;
    case RoboJsNodeIfStatement:
        JsWriteText(writer, "if (");
        JsPrintNode(writer, node->first, 0);
        JsWriteText(writer, ") ");
        JsPrintNode(writer, node->second, 0);
        if (node->third != NULL) {
            JsWriteText(writer, " else ");
            JsPrintNode(writer, node->third, 0);
        }
        break;
    case RoboJsNodeWhileStatement:
        JsWriteText(writer, "while (");
        JsPrintNode(writer, node->first, 0);
        JsWriteText(writer, ") ");
        JsPrintNode(writer, node->second, 0);
        break;
    case RoboJsNodeDoWhileStatement:
        JsWriteText(writer, "do ");
        JsPrintNode(writer, node->second, 0);
        JsWriteText(writer, " while (");
        JsPrintNode(writer, node->first, 0);
        JsWriteText(writer, ");");
        break;
    case RoboJsNodeForStatement:
        JsWriteText(writer, "for (");
        if (node->first != NULL) {
            JsPrintNode(writer, node->first, 0);
        }
        JsWriteText(writer, "; ");
        if (node->second != NULL) {
            JsPrintNode(writer, node->second, 0);
        }
        JsWriteText(writer, "; ");
        if (node->third != NULL) {
            JsPrintNode(writer, node->third, 0);
        }
        JsWriteText(writer, ") ");
        JsPrintNode(writer, node->count > 0 ? node->list[0] : NULL, 0);
        break;
    case RoboJsNodeBreakStatement:
        JsWriteText(writer, "break;");
        break;
    case RoboJsNodeContinueStatement:
        JsWriteText(writer, "continue;");
        break;
    case RoboJsNodeReturnStatement:
        JsWriteText(writer, "return");
        if (node->first != NULL) {
            JsWriteChar(writer, ' ');
            JsPrintNode(writer, node->first, 0);
        }
        JsWriteChar(writer, ';');
        break;
    case RoboJsNodeThrowStatement:
        JsWriteText(writer, "throw ");
        JsPrintNode(writer, node->first, 0);
        JsWriteChar(writer, ';');
        break;
    case RoboJsNodeTryStatement:
        JsWriteText(writer, "try ");
        JsPrintNode(writer, node->first, 0);
        if (node->second != NULL) {
            JsWriteChar(writer, ' ');
            JsPrintNode(writer, node->second, 0);
        }
        if (node->third != NULL) {
            JsWriteText(writer, " finally ");
            JsPrintNode(writer, node->third, 0);
        }
        break;
    case RoboJsNodeCatchClause:
        JsWriteText(writer, "catch (");
        JsPrintNode(writer, node->first, 0);
        JsWriteText(writer, ") ");
        JsPrintNode(writer, node->second, 0);
        break;
    case RoboJsNodeSwitchStatement:
        JsWriteText(writer, "switch (");
        JsPrintNode(writer, node->first, 0);
        JsWriteText(writer, ") { ");
        JsPrintList(writer, node, " ");
        JsWriteText(writer, " }");
        break;
    case RoboJsNodeSwitchCase:
        if (node->first != NULL) {
            JsWriteText(writer, "case ");
            JsPrintNode(writer, node->first, 0);
            JsWriteText(writer, ": ");
        } else {
            JsWriteText(writer, "default: ");
        }
        JsPrintList(writer, node, " ");
        break;
    case RoboJsNodeUnaryExpression:
        JsWriteText(writer, JsTokenKindText(node->op) != NULL ? JsTokenKindText(node->op) : "?");
        JsPrintChild(writer, node->first, 8);
        break;
    case RoboJsNodeUpdateExpression:
        if (node->boolean) {
            JsPrintChild(writer, node->first, 8);
            JsWriteText(writer, JsTokenKindText(node->op) != NULL ? JsTokenKindText(node->op) : "?");
        } else {
            JsWriteText(writer, JsTokenKindText(node->op) != NULL ? JsTokenKindText(node->op) : "?");
            JsPrintChild(writer, node->first, 8);
        }
        break;
    case RoboJsNodeBinaryExpression:
    case RoboJsNodeLogicalExpression:
    case RoboJsNodeAssignmentExpression: {
        int precedence = JsPrecedenceOf(node->op);

        JsPrintChild(writer, node->first, precedence);
        JsWriteChar(writer, ' ');
        JsWriteText(writer, JsTokenKindText(node->op) != NULL ? JsTokenKindText(node->op) : "?");
        JsWriteChar(writer, ' ');
        JsPrintChild(writer, node->second, precedence + ((JsBinaryOpInfoOf(node->op) != NULL &&
                                                          JsBinaryOpInfoOf(node->op)->rightAssociative)
                                                             ? 0
                                                             : 1));
        break;
    }
    case RoboJsNodeConditionalExpression:
        JsPrintChild(writer, node->first, 2);
        JsWriteText(writer, " ? ");
        JsPrintNode(writer, node->second, 0);
        JsWriteText(writer, " : ");
        JsPrintNode(writer, node->third, 0);
        break;
    case RoboJsNodeCallExpression:
    case RoboJsNodeNewExpression:
        if ((JsNodeKind)node->kind == RoboJsNodeNewExpression) {
            JsWriteText(writer, "new ");
        }
        JsPrintChild(writer, node->first, 9);
        JsWriteChar(writer, '(');
        JsPrintList(writer, node, ", ");
        JsWriteChar(writer, ')');
        break;
    case RoboJsNodeMemberExpression:
        JsPrintChild(writer, node->first, 9);
        if (node->intValue) {
            JsWriteChar(writer, '[');
            JsPrintNode(writer, node->second, 0);
            JsWriteChar(writer, ']');
        } else {
            JsWriteChar(writer, '.');
            JsPrintNode(writer, node->second, 0);
        }
        break;
    case RoboJsNodeSequenceExpression:
        JsPrintList(writer, node, ", ");
        break;
    default:
        JsWriteText(writer, "<unknown:");
        JsWriteText(writer, JsNodeKindName((JsNodeKind)node->kind));
        JsWriteChar(writer, '>');
        break;
    }
    (void)parentPrecedence;
    (void)i;
}

static int32_t JsAstWriteCommon(const JsNode *node, char *buffer, size_t capacity)
{
    JsWriter writer;

    if (buffer != NULL && capacity > 0) {
        buffer[0] = '\0';
    }
    if (buffer == NULL || capacity == 0) {
        return 0;
    }
    writer.buffer = buffer;
    writer.capacity = capacity;
    writer.written = 0;
    JsPrintNode(&writer, node, 0);
    if (writer.written + 1u > capacity) {
        /* Complete or absent: a half-line is worse than none, and the caller can size a retry. */
        buffer[0] = '\0';
        return (int32_t)writer.written;
    }
    buffer[writer.written] = '\0';
    return (int32_t)writer.written;
}

int32_t JsAstPrint(const JsNode *node, char *buffer, size_t capacity)
{
    if (node == NULL) {
        if (buffer != NULL && capacity > 0) {
            buffer[0] = '\0';
        }
        return 0;
    }
    return JsAstWriteCommon(node, buffer, capacity);
}

int32_t JsAstWrite(const JsNode *node, char *buffer, size_t capacity, int indent)
{
    (void)indent;
    return JsAstPrint(node, buffer, capacity);
}

/* ------------------------------------------------------------------ */
/* Validation                                                          */
/* ------------------------------------------------------------------ */

int32_t JsAstCount(const JsNode *node, int32_t limit)
{
    int32_t total = 1;
    int32_t i;

    if (node == NULL || limit <= 0) {
        return 0;
    }
    for (i = 0; i < node->count && total < limit; ++i) {
        total += JsAstCount(node->list[i], limit - total);
    }
    if (node->first != NULL && total < limit) {
        total += JsAstCount(node->first, limit - total);
    }
    if (node->second != NULL && total < limit) {
        total += JsAstCount(node->second, limit - total);
    }
    if (node->third != NULL && total < limit) {
        total += JsAstCount(node->third, limit - total);
    }
    return total;
}

static int JsNodeIsWellFormed(const JsNode *node, int depth)
{
    const JsNodeShape *shape;
    int32_t i;

    if (node == NULL) {
        return 1;
    }
    if (depth > 200) {
        return 0;
    }
    if (!JsNodeKindIsKnown((JsNodeKind)node->kind)) {
        return 0;
    }
    shape = JsNodeShapeOf((JsNodeKind)node->kind);
    if (shape == NULL) {
        return 0;
    }
    if (!shape->usesList && node->count > 0) {
        /* A list on a node whose shape says it has none is a node no evaluator arm will read: it
         * would be silently ignored, which is worse than a refusal. */
        return 0;
    }
    for (i = 0; i < node->count; ++i) {
        if (node->list[i] == NULL) {
            return 0;
        }
        if (!JsNodeIsWellFormed(node->list[i], depth + 1)) {
            return 0;
        }
    }
    return JsNodeIsWellFormed(node->first, depth + 1) &&
           JsNodeIsWellFormed(node->second, depth + 1) &&
           JsNodeIsWellFormed(node->third, depth + 1);
}

int JsAstIsWellFormed(const JsNode *node)
{
    return JsNodeIsWellFormed(node, 0);
}
