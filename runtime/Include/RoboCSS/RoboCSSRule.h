/*
 * RoboCSS rule model (Phase 9.1).
 *
 * Two things live in a stylesheet:
 *
 *   p { color: red }
 *   └─ a style rule: a selector list and a declaration list
 *
 *   @media print { p { color: black } }
 *   └─ an at-rule: a name, a prelude, and — when it has a block — more rules
 *
 * Both are modelled here because a stylesheet that could hold only style rules
 * would have to be redesigned the first time it meets `@media`, and the whole
 * point of a fixed architecture is not to need that. What at-rules do *not*
 * get in this phase is semantics: `@media` is not evaluated, `@import` is not
 * followed, `@font-face` is not loaded, and no at-rule is given a fixed set of
 * recognised names. The model records the name and the prelude text and stops.
 * Which at-rules matter, and what their blocks mean, belongs to the phases that
 * implement those features, and inventing that table now would be inventing
 * behaviour.
 *
 * A style rule owns its selector list and its declaration list; disposal
 * disposes both. An at-rule owns its prelude *text* only as a borrowed slice
 * (it points into the stylesheet's source) and owns its child rule list, which
 * is allocated on first use — an at-rule with no block, or with an empty one,
 * holds no allocation at all.
 *
 * The rule list is the only recursive type in the subsystem: a rule list holds
 * rules, and an at-rule may hold a rule list. The recursion is broken with a
 * pointer (`children`), which is why an at-rule's children are never a value
 * the caller can copy.
 */

#ifndef ROBO_CSS_RULE_H
#define ROBO_CSS_RULE_H

#include "RoboBase/RoboError.h"
#include "RoboCSS/RoboCSSDeclaration.h"
#include "RoboCSS/RoboCSSDiagnostics.h"
#include "RoboCSS/RoboCSSSelector.h"
#include "RoboCSS/RoboCSSTypes.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct robo_css_rule robo_css_rule_t;
typedef struct robo_css_rule_list robo_css_rule_list_t;

/** What a rule is. */
typedef enum robo_css_rule_kind {
    /** No rule: a cleared record. */
    ROBO_CSS_RULE_KIND_NONE = 0,
    /** A selector list plus a declaration list. */
    ROBO_CSS_RULE_KIND_STYLE = 1,
    /** `@name prelude { … }` or `@name prelude ;` */
    ROBO_CSS_RULE_KIND_AT = 2
} robo_css_rule_kind_t;

/** Number of defined rule kinds. */
#define ROBO_CSS_RULE_KIND_COUNT 3u

/** Non-zero if @p kind is a defined rule kind enumerator. */
int robo_css_rule_kind_is_known(robo_css_rule_kind_t kind);

/** Human-readable rule kind name; never NULL. */
const char *robo_css_rule_kind_name(robo_css_rule_kind_t kind);

/** An at-rule: its name, its prelude, and its block's rules if it has one. */
typedef struct robo_css_at_rule {
    /** The name after `@`, as written (`media`, `import`, `font-face`). */
    robo_css_slice_t name;
    /** Extent of the whole at-rule, including its block when it has one. */
    robo_css_range_t range;
    /** Extent of the prelude (everything between the name and the block/`;`). */
    robo_css_range_t prelude_range;
    /** The prelude's bytes, borrowed from the source; never interpreted here. */
    robo_css_slice_t prelude;
    /** Non-zero when the at-rule was written with a block. */
    int has_block;
    /** Owned nested rules, or NULL when the at-rule has none. */
    robo_css_rule_list_t *children;
} robo_css_at_rule_t;

/** Clear @p at_rule to empty. Safe on NULL. */
void robo_css_at_rule_init(robo_css_at_rule_t *at_rule);

/** Dispose nested rules and reset. Idempotent; safe on NULL. */
void robo_css_at_rule_dispose(robo_css_at_rule_t *at_rule);

/**
 * Name @p at_rule after @p name (without the `@`).
 * @return ROBO_OK; ROBO_INVALID_ARGUMENT for a NULL at-rule or an empty name.
 */
robo_result_t robo_css_at_rule_make(robo_css_at_rule_t *at_rule,
                                    robo_css_slice_t name,
                                    robo_css_diag_t *diag);

/** Record the at-rule's prelude text and whether it has a block. */
robo_result_t robo_css_at_rule_set_prelude(robo_css_at_rule_t *at_rule,
                                          robo_css_slice_t prelude,
                                          int has_block,
                                          robo_css_diag_t *diag);

/**
 * Append @p rule to @p at_rule's block by moving it in, allocating the child
 * list on first use. Appending to an at-rule without a block is refused rather
 * than silently turning it into one.
 * @return ROBO_OK; ROBO_INVALID_ARGUMENT for NULL arguments, a rule kind of
 *         NONE, or an at-rule with no block; ROBO_OUT_OF_MEMORY when the child
 *         list cannot grow.
 */
robo_result_t robo_css_at_rule_append_rule(robo_css_at_rule_t *at_rule,
                                          robo_css_rule_t *rule,
                                          robo_css_diag_t *diag);

/** Number of nested rules (0 when the at-rule has no block or no children). */
size_t robo_css_at_rule_rule_count(const robo_css_at_rule_t *at_rule);

/** Borrowed nested rule at @p index, or NULL when out of range. */
const robo_css_rule_t *robo_css_at_rule_rule_at(const robo_css_at_rule_t *at_rule,
                                                size_t index);

/** A style rule: one selector list and one declaration list. */
typedef struct robo_css_style_rule {
    /** Owned selector list. */
    robo_css_selector_list_t selectors;
    /** Owned declaration list. */
    robo_css_declaration_list_t declarations;
    /** Extent of the whole rule, braces included. */
    robo_css_range_t range;
    /** Extent of the selector text (the prelude before `{`). */
    robo_css_range_t prelude_range;
    /** Extent of the declaration block, braces included. */
    robo_css_range_t block_range;
} robo_css_style_rule_t;

/** Clear @p rule to empty. Safe on NULL. */
void robo_css_style_rule_init(robo_css_style_rule_t *rule);

/** Dispose the selector and declaration lists. Idempotent; safe on NULL. */
void robo_css_style_rule_dispose(robo_css_style_rule_t *rule);

/**
 * Append @p selector by moving it into @p rule's selector list.
 * @return ROBO_OK; ROBO_INVALID_ARGUMENT for NULL arguments or an empty
 *         selector; ROBO_OUT_OF_MEMORY when the list cannot grow.
 */
robo_result_t robo_css_style_rule_append_selector(robo_css_style_rule_t *rule,
                                                 robo_css_complex_selector_t *selector,
                                                 robo_css_diag_t *diag);

/**
 * Append @p declaration by moving it into @p rule's declaration list.
 * Declarations are stored in source order, duplicates included: dropping a
 * duplicate here would be implementing the cascade's last-one-wins rule.
 */
robo_result_t robo_css_style_rule_append_declaration(
    robo_css_style_rule_t *rule,
    robo_css_declaration_t *declaration,
    robo_css_diag_t *diag);

/** Number of complex selectors (0 for NULL). */
size_t robo_css_style_rule_selector_count(const robo_css_style_rule_t *rule);

/** Borrowed selector list; NULL for a NULL rule. */
const robo_css_selector_list_t *robo_css_style_rule_selectors(
    const robo_css_style_rule_t *rule);

/** Number of declarations (0 for NULL). */
size_t robo_css_style_rule_declaration_count(const robo_css_style_rule_t *rule);

/** Borrowed declaration at @p index, or NULL when out of range. */
const robo_css_declaration_t *robo_css_style_rule_declaration(
    const robo_css_style_rule_t *rule,
    size_t index);

/** Borrowed declaration list; NULL for a NULL rule. */
const robo_css_declaration_list_t *robo_css_style_rule_declarations(
    const robo_css_style_rule_t *rule);

/**
 * Non-zero when @p rule has at least one selector and at least one
 * declaration. A rule with selectors and no declarations is legal CSS, so this
 * is a "worth keeping" test and not a validity test.
 */
int robo_css_style_rule_is_populated(const robo_css_style_rule_t *rule);

/** One rule: either a style rule or an at-rule, never both. */
struct robo_css_rule {
    robo_css_rule_kind_t kind;
    robo_css_style_rule_t style;
    robo_css_at_rule_t at;
};

/** Clear @p rule to KIND_NONE. Safe on NULL. */
void robo_css_rule_init(robo_css_rule_t *rule);

/** Dispose whichever alternative @p rule holds. Idempotent; safe on NULL. */
void robo_css_rule_dispose(robo_css_rule_t *rule);

/** Make @p rule a STYLE rule with empty selector and declaration lists. */
robo_result_t robo_css_rule_make_style(robo_css_rule_t *rule, robo_css_diag_t *diag);

/**
 * Make @p rule an AT rule named @p name.
 * @return ROBO_OK; ROBO_INVALID_ARGUMENT for NULL arguments or an empty name.
 */
robo_result_t robo_css_rule_make_at(robo_css_rule_t *rule,
                                    robo_css_slice_t name,
                                    robo_css_diag_t *diag);

/** Borrowed style-rule alternative, or NULL when @p rule is not a STYLE rule. */
const robo_css_style_rule_t *robo_css_rule_style(const robo_css_rule_t *rule);

/** Borrowed at-rule alternative, or NULL when @p rule is not an AT rule. */
const robo_css_at_rule_t *robo_css_rule_at(const robo_css_rule_t *rule);

/** The rule's declared range (whatever the parser recorded). */
robo_css_range_t robo_css_rule_range(const robo_css_rule_t *rule);

/**
 * Number of rules in the tree rooted at @p rule: 1 for a style rule, and
 * 1 + every nested rule for an at-rule. 0 for NONE/NULL. The same measure a
 * test wants when it asks "how big is this sheet", without flattening anything.
 */
size_t robo_css_rule_tree_count(const robo_css_rule_t *rule);

/** An owning, ordered list of rules. */
struct robo_css_rule_list {
    robo_css_rule_t *items; /* owned; NULL when count == 0 */
    size_t count;
    size_t capacity;
};

/** Clear @p list to empty. Safe on NULL. */
void robo_css_rule_list_init(robo_css_rule_list_t *list);

/** Dispose every rule and free the array. Idempotent; safe on NULL. */
void robo_css_rule_list_dispose(robo_css_rule_list_t *list);

/**
 * Append @p rule by moving it in; @p rule is left KIND_NONE.
 * @return ROBO_OK; ROBO_INVALID_ARGUMENT for NULL arguments or a rule kind of
 *         NONE; ROBO_OUT_OF_MEMORY when the array cannot grow.
 */
robo_result_t robo_css_rule_list_append(robo_css_rule_list_t *list,
                                       robo_css_rule_t *rule,
                                       robo_css_diag_t *diag);

/** Number of rules (0 for NULL). */
size_t robo_css_rule_list_count(const robo_css_rule_list_t *list);

/** Borrowed rule at @p index, or NULL when out of range. */
const robo_css_rule_t *robo_css_rule_list_at(const robo_css_rule_list_t *list,
                                             size_t index);

/** Non-zero when @p list holds no rules (also for NULL). */
int robo_css_rule_list_is_empty(const robo_css_rule_list_t *list);

/** Remove every rule. Safe on NULL. */
void robo_css_rule_list_clear(robo_css_rule_list_t *list);

/**
 * Number of rules in @p list and, recursively, in the blocks of its at-rules.
 * This is the count a stylesheet reports as "how much CSS is in here".
 */
size_t robo_css_rule_list_tree_count(const robo_css_rule_list_t *list);

/** Number of style rules in @p list, at-rules' blocks included. */
size_t robo_css_rule_list_style_rule_count(const robo_css_rule_list_t *list);

/**
 * Flatten @p list, at-rules' blocks included, into @p out_rules, writing how
 * many entries were written (the count is min(total, capacity)).
 * A NULL output with a zero capacity is legal and just measures.
 * @return the total number of rules found, which may exceed @p capacity.
 */
size_t robo_css_rule_list_flatten(const robo_css_rule_list_t *list,
                                  const robo_css_rule_t **out_rules,
                                  size_t capacity);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_CSS_RULE_H */
