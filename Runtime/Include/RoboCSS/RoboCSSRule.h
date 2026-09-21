/*
 * RoboCSS rule model (Phase 9.1).
 *
 * Two things live in a stylesheet:
 *
 *   p { color: red }
 *   └─ a style rule: a selector list and a declaration list
 *
 *   @media print { p { color: black } }
 *   └─ an at-rule: a name, a prelude, and — when it has a block — its contents
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
 * A block is two possible things, and that is a fact about CSS rather than
 * about a particular at-rule: `@media`'s block holds *rules* while
 * `@font-face`'s holds *descriptors*, and those are declarations in everything
 * but name. Both are therefore modelled — `children` for the former,
 * `declarations` for the latter — because an at-rule that could only keep one
 * of them would silently discard `@font-face { font-family: X }`, and the
 * phases that implement font faces would have to re-parse the source to get it
 * back. No legal stylesheet fills both; a block that does is invalid CSS and is
 * kept rather than resolved, because deciding which half was "right" would be
 * semantics.
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
#include "RoboCSS/RoboCSSValue.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct CssRule CssRule;
typedef struct CssRuleList CssRuleList;

/** What a rule is. */
typedef enum CssRuleKind {
    /** No rule: a cleared record. */
    RoboCssRuleKindNone = 0,
    /** A selector list plus a declaration list. */
    RoboCssRuleKindStyle = 1,
    /** `@name prelude { … }` or `@name prelude ;` */
    RoboCssRuleKindAt = 2
} CssRuleKind;

/** Number of defined rule kinds. */
#define RoboCssRuleKindCount 3u

/** Non-zero if @p kind is a defined rule kind enumerator. */
int CssRuleKindIsKnown(CssRuleKind kind);

/** Human-readable rule kind name; never NULL. */
const char *CssRuleKindName(CssRuleKind kind);

/** An at-rule: its name, its prelude, and its block's rules if it has one. */
typedef struct CssAtRule {
    /** The name after `@`, as written (`media`, `import`, `font-face`). */
    CssSlice name;
    /** Extent of the whole at-rule, including its block when it has one. */
    CssRange range;
    /** Extent of the prelude (everything between the name and the block/`;`). */
    CssRange preludeRange;
    /** The prelude's bytes, borrowed from the source; never interpreted here. */
    CssSlice prelude;
    /**
     * The prelude as *component values* (9.3), owned; a LIST, empty when the
     * at-rule has no prelude.
     *
     * `prelude` and `preludeValues` answer two different questions, which is
     * why both exist: the slice is how the author spelled it, and the value
     * tree is its structure. A caller that needs the bytes (a diagnostic, a
     * source-preserving rewrite) reads the slice; a caller that needs to know
     * that `screen and (min-width: 30em)` is an ident, an ident, a block and a
     * dimension reads the tree. Nothing here interprets either one.
     */
    CssValue preludeValues;
    /** Non-zero when the at-rule was written with a block. */
    int hasBlock;
    /**
     * Non-zero when that block's closing `}` was actually present. `@media {`
     * that runs to the end of input has a block and it was never closed, which
     * is a different fact from `@import x;` and from a closed block.
     */
    int blockClosed;
    /** Owned nested rules, or NULL when the at-rule has none. */
    CssRuleList *children;
    /**
     * Owned descriptor declarations from the block, empty when it held none.
     *
     * This is where `@font-face { font-family: X }` keeps its descriptors: the
     * block's contents are declarations, and a declaration is a declaration
     * whichever rule owns it. Held by value because a declaration list is not
     * recursive — the recursion break that makes `children` a pointer does not
     * apply here, and a caller holding a rule can read the descriptors without
     * a second indirection.
     */
    CssDeclarationList declarations;
} CssAtRule;

/** Clear @p atRule to empty. Safe on NULL. */
void CssAtRuleInit(CssAtRule *atRule);

/** Dispose nested rules and reset. Idempotent; safe on NULL. */
void CssAtRuleDispose(CssAtRule *atRule);

/**
 * Name @p atRule after @p name (without the `@`).
 * @return RoboOk; RoboInvalidArgument for a NULL at-rule or an empty name.
 */
Result CssAtRuleMake(CssAtRule *atRule,
                                    CssSlice name,
                                    CssDiag *diag);

/** Record the at-rule's prelude text and whether it has a block. */
Result CssAtRuleSetPrelude(CssAtRule *atRule,
                                          CssSlice prelude,
                                          int hasBlock,
                                          CssDiag *diag);

/**
 * Move @p preludeValues into @p atRule as its structured prelude.
 *
 * The value is moved, not copied: @p preludeValues is left NONE and the
 * at-rule owns whatever it held. This is the call the parser makes after
 * consuming a prelude's component values.
 * @return RoboOk; RoboInvalidArgument for NULL arguments or a value that is
 *         not a LIST.
 */
Result CssAtRuleSetPreludeValues(CssAtRule *atRule,
                                                 CssValue *preludeValues,
                                                 CssDiag *diag);

/** Borrowed structured prelude, or NULL for a NULL at-rule. */
const CssValue *CssAtRulePreludeValues(const CssAtRule *atRule);

/**
 * Record whether the at-rule's block was closed by `}`. Only meaningful for an
 * at-rule that has a block; setting it on one without a block is refused.
 */
Result CssAtRuleSetBlockClosed(CssAtRule *atRule,
                                               int closed,
                                               CssDiag *diag);

/** Non-zero when the at-rule's block was closed (0 for NULL, or no block). */
int CssAtRuleBlockClosed(const CssAtRule *atRule);

/** Record the whole at-rule's source extent. */
Result CssAtRuleSetRange(CssAtRule *atRule,
                                        CssRange range,
                                        CssDiag *diag);

/** Borrowed whole-rule extent. */
CssRange CssAtRuleRange(const CssAtRule *atRule);

/** Borrowed prelude extent. */
CssRange CssAtRulePreludeRange(const CssAtRule *atRule);

/**
 * Append @p rule to @p atRule's block by moving it in, allocating the child
 * list on first use. Appending to an at-rule without a block is refused rather
 * than silently turning it into one.
 * @return RoboOk; RoboInvalidArgument for NULL arguments, a rule kind of
 *         NONE, or an at-rule with no block; RoboOutOfMemory when the child
 *         list cannot grow.
 */
Result CssAtRuleAppendRule(CssAtRule *atRule,
                                          CssRule *rule,
                                          CssDiag *diag);

/** Number of nested rules (0 when the at-rule has no block or no children). */
size_t CssAtRuleRuleCount(const CssAtRule *atRule);

/** Borrowed nested rule at @p index, or NULL when out of range. */
const CssRule *CssAtRuleRuleAt(const CssAtRule *atRule,
                                                size_t index);

/**
 * Move @p declarations into @p atRule as its block's descriptor declarations.
 *
 * The list is moved, not copied: @p declarations is left empty and the at-rule
 * owns whatever it held. This is the call the parser makes after reading a
 * block whose contents turned out to be declarations — `@font-face`'s, for
 * instance. Refused for a non-empty target, because an at-rule has one block
 * and a second move into it would be appending to something the caller did not
 * append to.
 *
 * @return RoboOk; RoboInvalidArgument for NULL arguments, an at-rule with no
 *         block, or a target that is not empty.
 */
Result CssAtRuleTakeDeclarations(CssAtRule *atRule,
                                                CssDeclarationList *declarations,
                                                CssDiag *diag);

/** Number of descriptor declarations (0 when the at-rule has none). */
size_t CssAtRuleDeclarationCount(const CssAtRule *atRule);

/** Borrowed descriptor declaration at @p index, or NULL when out of range. */
const CssDeclaration *CssAtRuleDeclaration(const CssAtRule *atRule,
                                                           size_t index);

/** The at-rule's declaration list, or NULL for a NULL at-rule. */
const CssDeclarationList *CssAtRuleDeclarations(
    const CssAtRule *atRule);

/** A style rule: one selector list and one declaration list. */
typedef struct CssStyleRule {
    /** Owned selector list. */
    CssSelectorList selectors;
    /** Owned declaration list. */
    CssDeclarationList declarations;
    /** Extent of the whole rule, braces included. */
    CssRange range;
    /** Extent of the selector text (the prelude before `{`). */
    CssRange preludeRange;
    /** Extent of the declaration block, braces included. */
    CssRange blockRange;
    /**
     * The prelude as component values (9.3), owned; a LIST, empty when the
     * parser recorded none.
     *
     * This is what Phase 9.4 turns into selectors, and it is deliberately a
     * value tree rather than a string: a descendant combinator is *whitespace*,
     * so a representation that dropped whitespace could not tell `a b` from
     * `ab`. `selectors` stays empty until 9.4 fills it, which makes "was this
     * rule's prelude understood as selectors yet" answerable by asking.
     */
    CssValue preludeValues;
    /**
     * Non-zero when the block's closing `}` was present. `p { color: red;` is a
     * rule with a block that was never closed, and this is how a caller tells
     * that from the malformed-declaration case inside a closed block.
     */
    int blockClosed;
} CssStyleRule;

/** Clear @p rule to empty. Safe on NULL. */
void CssStyleRuleInit(CssStyleRule *rule);

/** Dispose the selector and declaration lists. Idempotent; safe on NULL. */
void CssStyleRuleDispose(CssStyleRule *rule);

/**
 * Append @p selector by moving it into @p rule's selector list.
 * @return RoboOk; RoboInvalidArgument for NULL arguments or an empty
 *         selector; RoboOutOfMemory when the list cannot grow.
 */
Result CssStyleRuleAppendSelector(CssStyleRule *rule,
                                                 CssComplexSelector *selector,
                                                 CssDiag *diag);

/**
 * Append @p declaration by moving it into @p rule's declaration list.
 * Declarations are stored in source order, duplicates included: dropping a
 * duplicate here would be implementing the cascade's last-one-wins rule.
 */
Result CssStyleRuleAppendDeclaration(
    CssStyleRule *rule,
    CssDeclaration *declaration,
    CssDiag *diag);

/**
 * Move @p preludeValues into @p rule as its structured prelude.
 * @return RoboOk; RoboInvalidArgument for NULL arguments or a value that is
 *         not a LIST.
 */
Result CssStyleRuleSetPreludeValues(CssStyleRule *rule,
                                                    CssValue *preludeValues,
                                                    CssDiag *diag);

/** Borrowed structured prelude, or NULL for a NULL rule. */
const CssValue *CssStyleRulePreludeValues(
    const CssStyleRule *rule);

/** Record the rule's three source extents at once. */
Result CssStyleRuleSetRanges(CssStyleRule *rule,
                                            CssRange range,
                                            CssRange preludeRange,
                                            CssRange blockRange,
                                            CssDiag *diag);

/** Record whether the rule's block was closed by `}`. */
Result CssStyleRuleSetBlockClosed(CssStyleRule *rule,
                                                  int closed,
                                                  CssDiag *diag);

/** Non-zero when the rule's block was closed (0 for NULL). */
int CssStyleRuleBlockClosed(const CssStyleRule *rule);

/** Borrowed whole-rule extent. */
CssRange CssStyleRuleRange(const CssStyleRule *rule);

/** Borrowed prelude extent. */
CssRange CssStyleRulePreludeRange(const CssStyleRule *rule);

/** Borrowed block extent. */
CssRange CssStyleRuleBlockRange(const CssStyleRule *rule);

/** Number of complex selectors (0 for NULL). */
size_t CssStyleRuleSelectorCount(const CssStyleRule *rule);

/** Borrowed selector list; NULL for a NULL rule. */
const CssSelectorList *CssStyleRuleSelectors(
    const CssStyleRule *rule);

/** Number of declarations (0 for NULL). */
size_t CssStyleRuleDeclarationCount(const CssStyleRule *rule);

/** Borrowed declaration at @p index, or NULL when out of range. */
const CssDeclaration *CssStyleRuleDeclaration(
    const CssStyleRule *rule,
    size_t index);

/** Borrowed declaration list; NULL for a NULL rule. */
const CssDeclarationList *CssStyleRuleDeclarations(
    const CssStyleRule *rule);

/**
 * Non-zero when @p rule has at least one selector and at least one
 * declaration. A rule with selectors and no declarations is legal CSS, so this
 * is a "worth keeping" test and not a validity test.
 */
int CssStyleRuleIsPopulated(const CssStyleRule *rule);

/** One rule: either a style rule or an at-rule, never both. */
struct CssRule {
    CssRuleKind kind;
    CssStyleRule style;
    CssAtRule at;
};

/** Clear @p rule to KIND_NONE. Safe on NULL. */
void CssRuleInit(CssRule *rule);

/** Dispose whichever alternative @p rule holds. Idempotent; safe on NULL. */
void CssRuleDispose(CssRule *rule);

/** Make @p rule a STYLE rule with empty selector and declaration lists. */
Result CssRuleMakeStyle(CssRule *rule, CssDiag *diag);

/**
 * Make @p rule an AT rule named @p name.
 * @return RoboOk; RoboInvalidArgument for NULL arguments or an empty name.
 */
Result CssRuleMakeAt(CssRule *rule,
                                    CssSlice name,
                                    CssDiag *diag);

/** Borrowed style-rule alternative, or NULL when @p rule is not a STYLE rule. */
const CssStyleRule *CssRuleStyle(const CssRule *rule);

/** Borrowed at-rule alternative, or NULL when @p rule is not an AT rule. */
const CssAtRule *CssRuleAt(const CssRule *rule);

/** The rule's declared range (whatever the parser recorded). */
CssRange CssRuleRange(const CssRule *rule);

/**
 * Number of rules in the tree rooted at @p rule: 1 for a style rule, and
 * 1 + every nested rule for an at-rule. 0 for NONE/NULL. The same measure a
 * test wants when it asks "how big is this sheet", without flattening anything.
 */
size_t CssRuleTreeCount(const CssRule *rule);

/** An owning, ordered list of rules. */
struct CssRuleList {
    CssRule *items; /* owned; NULL when count == 0 */
    size_t count;
    size_t capacity;
};

/** Clear @p list to empty. Safe on NULL. */
void CssRuleListInit(CssRuleList *list);

/** Dispose every rule and free the array. Idempotent; safe on NULL. */
void CssRuleListDispose(CssRuleList *list);

/**
 * Append @p rule by moving it in; @p rule is left KIND_NONE.
 * @return RoboOk; RoboInvalidArgument for NULL arguments or a rule kind of
 *         NONE; RoboOutOfMemory when the array cannot grow.
 */
Result CssRuleListAppend(CssRuleList *list,
                                       CssRule *rule,
                                       CssDiag *diag);

/** Number of rules (0 for NULL). */
size_t CssRuleListCount(const CssRuleList *list);

/** Borrowed rule at @p index, or NULL when out of range. */
const CssRule *CssRuleListAt(const CssRuleList *list,
                                             size_t index);

/** Non-zero when @p list holds no rules (also for NULL). */
int CssRuleListIsEmpty(const CssRuleList *list);

/** Remove every rule. Safe on NULL. */
void CssRuleListClear(CssRuleList *list);

/**
 * Number of rules in @p list and, recursively, in the blocks of its at-rules.
 * This is the count a stylesheet reports as "how much CSS is in here".
 */
size_t CssRuleListTreeCount(const CssRuleList *list);

/** Number of style rules in @p list, at-rules' blocks included. */
size_t CssRuleListStyleRuleCount(const CssRuleList *list);

/**
 * Flatten @p list, at-rules' blocks included, into @p outRules, writing how
 * many entries were written (the count is min(total, capacity)).
 * A NULL output with a zero capacity is legal and just measures.
 * @return the total number of rules found, which may exceed @p capacity.
 */
size_t CssRuleListFlatten(const CssRuleList *list,
                                  const CssRule **outRules,
                                  size_t capacity);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_CSS_RULE_H */
