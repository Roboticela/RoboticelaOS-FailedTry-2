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

typedef struct robo_css_value robo_css_value_t;

/** An owning, ordered sequence of component values. */
typedef struct robo_css_value_list {
    robo_css_value_t *items; /* owned; may be NULL when count == 0 */
    size_t count;
    size_t capacity;
} robo_css_value_list_t;

/** What a component value is. */
typedef enum robo_css_value_kind {
    /** No value: a cleared record. */
    ROBO_CSS_VALUE_KIND_NONE = 0,
    /** A single token. */
    ROBO_CSS_VALUE_KIND_TOKEN = 1,
    /** `name( … )`. */
    ROBO_CSS_VALUE_KIND_FUNCTION = 2,
    /** `{ … }`, `[ … ]` or `( … )`. */
    ROBO_CSS_VALUE_KIND_BLOCK = 3,
    /** An ordered sequence of component values. */
    ROBO_CSS_VALUE_KIND_LIST = 4
} robo_css_value_kind_t;

/** Number of defined value kinds. */
#define ROBO_CSS_VALUE_KIND_COUNT 5u

/** Non-zero if @p kind is a defined value kind enumerator. */
int robo_css_value_kind_is_known(robo_css_value_kind_t kind);

/** Human-readable value kind name; never NULL. */
const char *robo_css_value_kind_name(robo_css_value_kind_t kind);

/** Non-zero when a value of @p kind can hold children (FUNCTION/BLOCK/LIST). */
int robo_css_value_kind_holds_children(robo_css_value_kind_t kind);

/** One component value. */
struct robo_css_value {
    robo_css_value_kind_t kind;
    /** The token, for KIND_TOKEN. Plain value; owns nothing. */
    robo_css_token_t token;
    /** FUNCTION/BLOCK: the function name / the opening bracket; absent for LIST. */
    robo_css_slice_t name;
    /** BLOCK: the opening token type (`(`, `[`, `{`); NONE otherwise. */
    robo_css_token_type_t open;
    /** BLOCK: the closing type the block was matched with; NONE when unmatched. */
    robo_css_token_type_t close;
    /** Source range this value covers; the empty range when hand-built. */
    robo_css_range_t range;
    /** Owned children for FUNCTION/BLOCK/LIST. */
    robo_css_value_list_t children;
};

/** Clear @p value to KIND_NONE with no children. Safe on NULL (no-op). */
void robo_css_value_init(robo_css_value_t *value);

/**
 * Dispose @p value: dispose every child recursively, free the child array,
 * then reset to KIND_NONE. Idempotent and safe on NULL.
 */
void robo_css_value_dispose(robo_css_value_t *value);

/**
 * Move @p value's contents into @p out_value, leaving @p value KIND_NONE.
 * Used to hand a value across an ownership boundary without copying children.
 * @return ROBO_OK; ROBO_INVALID_ARGUMENT for a NULL argument or for
 *         `value == out_value`.
 */
robo_result_t robo_css_value_move(robo_css_value_t *value,
                                 robo_css_value_t *out_value,
                                 robo_css_diag_t *diag);

/** Non-zero when @p value is NONE. NULL → 1. */
int robo_css_value_is_empty(const robo_css_value_t *value);

/** Copy @p token in as a TOKEN value (no allocation). */
robo_result_t robo_css_value_make_token(robo_css_value_t *value,
                                       const robo_css_token_t *token,
                                       robo_css_diag_t *diag);

/** Make @p value an empty LIST. */
robo_result_t robo_css_value_make_list(robo_css_value_t *value,
                                      robo_css_diag_t *diag);

/** Make @p value a FUNCTION with the borrowed @p name and @p range. */
robo_result_t robo_css_value_make_function(robo_css_value_t *value,
                                          robo_css_slice_t name,
                                          robo_css_range_t range,
                                          robo_css_diag_t *diag);

/**
 * Make @p value a BLOCK opened by @p open_type with @p range as its extent.
 * @p open_type must be a block-start token type.
 */
robo_result_t robo_css_value_make_block(robo_css_value_t *value,
                                       robo_css_token_type_t open_type,
                                       robo_css_range_t range,
                                       robo_css_diag_t *diag);

/**
 * Append @p child to @p value's children by moving it in; @p child is reset to
 * KIND_NONE on success. Only FUNCTION, BLOCK and LIST values hold children.
 *
 * @return ROBO_OK; ROBO_INVALID_ARGUMENT when @p value is not a container or
 *         either pointer is NULL; ROBO_OUT_OF_MEMORY when the child array
 *         cannot grow (in which case @p child is untouched and still owns its
 *         contents).
 */
robo_result_t robo_css_value_append(robo_css_value_t *value,
                                   robo_css_value_t *child,
                                   robo_css_diag_t *diag);

/** Number of direct children (0 for TOKEN/NONE and for NULL). */
size_t robo_css_value_child_count(const robo_css_value_t *value);

/** Borrowed child at @p index, or NULL when out of range / not a container. */
const robo_css_value_t *robo_css_value_child(const robo_css_value_t *value,
                                             size_t index);

/** Remove every child (freeing them) but keep the value's own kind and name. */
void robo_css_value_clear_children(robo_css_value_t *value);

/**
 * Depth of the tree rooted at @p value: 1 for a leaf (a token), 1 + the
 * deepest child otherwise, 0 for NONE/NULL. Used by tests and by anything that
 * needs a bound on the recursion disposal performs.
 */
size_t robo_css_value_depth(const robo_css_value_t *value);

/** Total number of values in the tree rooted at @p value (0 for NONE/NULL). */
size_t robo_css_value_tree_count(const robo_css_value_t *value);

/** Borrowed token of a TOKEN value; the NONE token otherwise. NULL → NONE. */
robo_css_token_t robo_css_value_token(const robo_css_value_t *value);

/** Borrowed raw text of a TOKEN value; absent otherwise. NULL → absent. */
robo_css_slice_t robo_css_value_token_text(const robo_css_value_t *value);

/** Borrowed name of a FUNCTION/BLOCK value; absent otherwise. */
robo_css_slice_t robo_css_value_name(const robo_css_value_t *value);

/** Source range of @p value; the empty range for NONE/NULL. */
robo_css_range_t robo_css_value_range(const robo_css_value_t *value);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_CSS_VALUE_H */
