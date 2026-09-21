/*
 * RoboCSS component value model (Phase 9.1).
 *
 * A declaration's value is not a string. CSS Syntax turns the token stream
 * between a `:` and a `;` into a *component value tree*, and every later stage
 * — matching, the cascade, inheritance, computed values, the property parsers
 * of 9.9 and beyond — reads that tree rather than re-scanning bytes. This
 * header is that tree.
 *
 * Four kinds, and only four:
 *
 *   TOKEN      one token. An identifier, a string, a number, a percentage, a
 *              dimension, a delim, a hash, a url — the token model already
 *              distinguishes them, so a value does not need a kind per token
 *              type. `10px` is a DIMENSION token; `red` is an IDENT token;
 *              `url(a.png)` arrives as a URL token or, when quoted and
 *              bracketed, as a function.
 *   FUNCTION   a name, its parentheses, and the component values inside.
 *   BLOCK      a simple block: `{ … }`, `[ … ]` or `( … )`, with the opening
 *              and closing token types recorded so a recovery path can reason
 *              about a block that never closed.
 *   LIST       an ordered sequence of component values. This is what a
 *              declaration's value is once trivia is handled, and what a
 *              function or block contains.
 *
 * FUNCTION, BLOCK and LIST all use the same `children` array rather than three
 * storage mechanisms, because they *are* sequences; the kind is what differs.
 *
 * Ownership is unambiguous and always the same: a value owns its `children`,
 * a child is appended by *moving* it in (the caller's value is reset to NONE),
 * and disposing a value disposes its children recursively. Tokens are plain
 * values with borrowed slices, so moving one is free and disposing one is a
 * reset — the only heap in this model is the child arrays.
 *
 * Nesting depth comes from the parser, which bounds it (9.2/9.3); disposal
 * therefore recurses only as deep as the parser allowed anything to be built.
 *
 * Colours and URLs are deliberately absent as first-class kinds: a colour is
 * an identifier, a hash or a function, and a URL is a url token or a function,
 * and 9.9/9.10 are where those become semantic. Inventing a `color` kind here
 * would be inventing a property family this phase does not implement.
 */

#ifndef ROBO_CSS_VALUE_H
#define ROBO_CSS_VALUE_H

#include "RoboBase/RoboError.h"
#include "RoboCSS/RoboCSSDiagnostics.h"
#include "RoboCSS/RoboCSSToken.h"
#include "RoboCSS/RoboCSSTypes.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct CssValue CssValue;

/** An owning, ordered sequence of component values. */
typedef struct CssValueList {
    CssValue *items; /* owned; may be NULL when count == 0 */
    size_t count;
    size_t capacity;
} CssValueList;

/** What a component value is. */
typedef enum CssValueKind {
    /** No value: a cleared record. */
    RoboCssValueKindNone = 0,
    /** A single token. */
    RoboCssValueKindToken = 1,
    /** `name( … )`. */
    RoboCssValueKindFunction = 2,
    /** `{ … }`, `[ … ]` or `( … )`. */
    RoboCssValueKindBlock = 3,
    /** An ordered sequence of component values. */
    RoboCssValueKindList = 4
} CssValueKind;

/** Number of defined value kinds. */
#define RoboCssValueKindCount 5u

/** Non-zero if @p kind is a defined value kind enumerator. */
int CssValueKindIsKnown(CssValueKind kind);

/** Human-readable value kind name; never NULL. */
const char *CssValueKindName(CssValueKind kind);

/** Non-zero when a value of @p kind can hold children (FUNCTION/BLOCK/LIST). */
int CssValueKindHoldsChildren(CssValueKind kind);

/** One component value. */
struct CssValue {
    CssValueKind kind;
    /** The token, for KIND_TOKEN. Plain value; owns nothing. */
    CssToken token;
    /** FUNCTION/BLOCK: the function name / the opening bracket; absent for LIST. */
    CssSlice name;
    /** BLOCK: the opening token type (`(`, `[`, `{`); NONE otherwise. */
    CssTokenType open;
    /** BLOCK: the closing type the block was matched with; NONE when unmatched. */
    CssTokenType close;
    /** Source range this value covers; the empty range when hand-built. */
    CssRange range;
    /** Owned children for FUNCTION/BLOCK/LIST. */
    CssValueList children;
};

/** Clear @p value to KIND_NONE with no children. Safe on NULL (no-op). */
void CssValueInit(CssValue *value);

/**
 * Dispose @p value: dispose every child recursively, free the child array,
 * then reset to KIND_NONE. Idempotent and safe on NULL.
 */
void CssValueDispose(CssValue *value);

/**
 * Move @p value's contents into @p outValue, leaving @p value KIND_NONE.
 * Used to hand a value across an ownership boundary without copying children.
 * @return RoboOk; RoboInvalidArgument for a NULL argument or for
 *         `value == outValue`.
 */
Result CssValueMove(CssValue *value,
                                 CssValue *outValue,
                                 CssDiag *diag);

/**
 * Deep-copy @p value into @p outValue, which is disposed first. (9.7)
 *
 * Moving is how 9.1 hands a value across an ownership boundary; *copying* is
 * what a resolved style needs, because inheritance carries a value from a
 * parent's style into a child's and the two must not alias — a child that
 * shared its parent's child array would be mutated (or freed) through the
 * parent, which is the exact ownership hazard the rest of this model avoids by
 * always moving.
 *
 * Every slice inside is borrowed and stays borrowed: a copy shares the source
 * bytes and the token text with the original, which is correct, because slices
 * are never owned by a value in the first place. Only the child arrays are new.
 *
 * Transactional in the same way @p CssValueAppend is: on
 * RoboOutOfMemory, @p outValue is left KIND_NONE rather than half-copied,
 * so a caller that ignores the result still owns something consistent.
 *
 * @return RoboOk; RoboInvalidArgument for a NULL argument or for
 *         `value == outValue`; RoboOutOfMemory when a child array could not
 *         be allocated.
 */
Result CssValueCopy(CssValue *outValue,
                                  const CssValue *value,
                                  CssDiag *diag);

/** Non-zero when @p value is NONE. NULL → 1. */
int CssValueIsEmpty(const CssValue *value);

/** Copy @p token in as a TOKEN value (no allocation). */
Result CssValueMakeToken(CssValue *value,
                                       const CssToken *token,
                                       CssDiag *diag);

/** Make @p value an empty LIST. */
Result CssValueMakeList(CssValue *value,
                                      CssDiag *diag);

/** Make @p value a FUNCTION with the borrowed @p name and @p range. */
Result CssValueMakeFunction(CssValue *value,
                                          CssSlice name,
                                          CssRange range,
                                          CssDiag *diag);

/**
 * Make @p value a BLOCK opened by @p openType with @p range as its extent.
 * @p openType must be a block-start token type.
 */
Result CssValueMakeBlock(CssValue *value,
                                       CssTokenType openType,
                                       CssRange range,
                                       CssDiag *diag);

/**
 * Append @p child to @p value's children by moving it in; @p child is reset to
 * KIND_NONE on success. Only FUNCTION, BLOCK and LIST values hold children.
 *
 * @return RoboOk; RoboInvalidArgument when @p value is not a container or
 *         either pointer is NULL; RoboOutOfMemory when the child array
 *         cannot grow (in which case @p child is untouched and still owns its
 *         contents).
 */
Result CssValueAppend(CssValue *value,
                                   CssValue *child,
                                   CssDiag *diag);

/** Number of direct children (0 for TOKEN/NONE and for NULL). */
size_t CssValueChildCount(const CssValue *value);

/** Borrowed child at @p index, or NULL when out of range / not a container. */
const CssValue *CssValueChild(const CssValue *value,
                                             size_t index);

/** Remove every child (freeing them) but keep the value's own kind and name. */
void CssValueClearChildren(CssValue *value);

/**
 * Depth of the tree rooted at @p value: 1 for a leaf (a token), 1 + the
 * deepest child otherwise, 0 for NONE/NULL. Used by tests and by anything that
 * needs a bound on the recursion disposal performs.
 */
size_t CssValueDepth(const CssValue *value);

/** Total number of values in the tree rooted at @p value (0 for NONE/NULL). */
size_t CssValueTreeCount(const CssValue *value);

/** Borrowed token of a TOKEN value; the NONE token otherwise. NULL → NONE. */
CssToken CssValueToken(const CssValue *value);

/** Borrowed raw text of a TOKEN value; absent otherwise. NULL → absent. */
CssSlice CssValueTokenText(const CssValue *value);

/** Borrowed name of a FUNCTION/BLOCK value; absent otherwise. */
CssSlice CssValueName(const CssValue *value);

/** Source range of @p value; the empty range for NONE/NULL. */
CssRange CssValueRange(const CssValue *value);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_CSS_VALUE_H */
