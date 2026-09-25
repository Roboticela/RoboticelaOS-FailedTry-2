/*
 * RoboJS AST (Phase 16.3) — syntax as a tree, with no runtime value in sight.
 *
 * ## One node struct, and the fields that apply per kind
 *
 * `JsNode` has three ordered children (`first`, `second`, `third`), a variable-length child list
 * (`list`), a number, a string, an operator kind and a span. Which of those a kind uses is stated in
 * one table in `RoboJSAst.c` and nowhere else, so a printer, a validator and the evaluator all read
 * the same description of the tree instead of three switch statements that can drift.
 *
 *   `a + b`            first = a, second = b, op = +
 *   `a.b` / `a[b]`     first = a, second = b (b is a string literal for a dot access)
 *   `f(x, y)`          first = f, list = (x, y)
 *   `if (c) s else t`  first = c, second = s, third = t
 *   `for (i; c; u) b`  first = init, second = c, third = u, body = list[0]
 *   `{ a: 1, b: 2 }`   list = Property nodes; a property's `first` is its key, `second` its value
 *   `try { } catch (e) { } finally { }`  first = the block, second = the CatchClause, third = the
 *                      finally block
 *
 * A function node carries its **parameter names as a string array** (`params`) and its body as
 * `first`, because that is what a `JsFunction` stores, and the parser filling them once means the
 * interpreter never re-walks a parameter list.
 *
 * ## An AST node is never freed individually
 *
 * Nodes live in the runtime's heap (`RoboJSHeap.h`) and are released with it. That is what makes it
 * safe for `JsFunction.body` to point at a node for the lifetime of a closure.
 *
 * ## The printer is the debugging door
 *
 * `JsAstPrint` writes a one-line, parenthesised form — `(+ 1 (* 2 3))`-ish but with JavaScript's own
 * syntax — because a test that asserts a tree should assert text a person can read, and a failing
 * test should print something a person can diff. `JsAstWrite` is the bounded form: both report the
 * length a complete line needs and leave the buffer empty when it does not fit (the module's
 * complete-or-absent rule for diagnostics).
 */

#ifndef ROBO_JS_AST_H
#define ROBO_JS_AST_H

#include "RoboJS/RoboJSHeap.h"
#include "RoboJS/RoboJSTypes.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** A node. See the header comment for which fields each kind uses. */
typedef struct JsNode {
    uint32_t kind; /* JsNodeKind */
    uint32_t flags;
    JsSpan span;
    JsTokenKind op;
    int32_t intValue;

    double number;
    /** Literal text / identifier name / string key: heap-owned and NUL-terminated. */
    const char *text;
    int32_t textLength;
    /** `true`/`false` for a BooleanLiteral, the declared kind for a VariableDeclaration. */
    int boolean;

    /** Function nodes: the parameter names, in the heap, NUL-terminated. */
    const char **params;
    int32_t paramCount;

    struct JsNode *first;
    struct JsNode *second;
    struct JsNode *third;

    /** A variable-length child list; `length` is how many entries are meaningful. */
    struct JsNode **list;
    int32_t count;
    int32_t capacity;
} JsNode;

/** Allocate a node of @p kind, spanning @p span. Never NULL unless the heap is out of memory. */
JsNode *JsNodeCreate(JsHeap *heap, JsNodeKind kind, const JsSpan *span);

/** Append @p child to @p parent's list (growing it). RoboInvalidArgument for NULL arguments. */
Result JsNodeAppend(JsHeap *heap, JsNode *parent, JsNode *child);

/** Describe a kind's field usage: how many children and whether the list is used. */
typedef struct JsNodeShape {
    const char *name;
    int childCount;
    int usesList;
} JsNodeShape;

/** The one table the printer, the validator and the interpreter read. */
const JsNodeShape *JsNodeShapeOf(JsNodeKind kind);

/**
 * Write a one-line form of @p node into @p buffer (NUL-terminated).
 * @return the bytes a complete line needs (which may be larger than @p capacity — a caller that
 *         wants the whole line calls again with a bigger buffer); 0 when the buffer was left empty
 *         because nothing fit.
 */
int32_t JsAstPrint(const JsNode *node, char *buffer, size_t capacity);

/** `JsAstPrint` with a caller-chosen indentation budget for nested lists (0 → one line). */
int32_t JsAstWrite(const JsNode *node, char *buffer, size_t capacity, int indent);

/** Total node count of a subtree, bounded by @p limit to keep a hostile tree cheap to measure. */
int32_t JsAstCount(const JsNode *node, int32_t limit);

/** 1 when every node in the subtree is a known kind with an evaluator arm. Used by a test that
 *  wants "the parser cannot build something the interpreter cannot run" to be a checked fact. */
int JsAstIsWellFormed(const JsNode *node);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_JS_AST_H */
