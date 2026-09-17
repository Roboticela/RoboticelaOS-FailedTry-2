/*
 * RoboCSS selector model (Phase 9.1).
 *
 * Selectors are the largest thing Phase 9.1 must not implement and the one it
 * must model most carefully, because the shape chosen here decides what 9.4
 * (matching) and 9.6 (specificity) can be written against without redesign.
 *
 * The model is the CSS Selectors structure, bottom-up:
 *
 *   simple selector     one condition: `div`, `*`, `.card`, `#main`,
 *                       `[type="text"]`, `:hover`, `::before`
 *   compound selector   simple selectors that all apply to one element:
 *                       `input[type="text"]:disabled`
 *   complex selector    compound selectors joined by combinators:
 *                       `form > input + label`
 *   selector list       complex selectors joined by commas: `h1, h2, h3`
 *
 * A compound selector owns its simple selectors; a complex selector owns its
 * compound selectors and its combinators; a selector list owns its complex
 * selectors. Appending *moves* the child in, exactly as component values do,
 * so there is one ownership rule in the whole subsystem rather than one per
 * model. Simple selectors are plain values holding borrowed slices, so they are
 * copied rather than moved.
 *
 * Two things are deliberately absent:
 *
 *   - Matching. `robo_css_simple_selector_kind` says what a condition *is*;
 *     nothing here decides whether an element satisfies it. That is 9.4, and it
 *     needs the DOM view in RoboCSSStyle.h rather than a DOM copy here.
 *   - Specificity. A specificity is a property of a *specific* selector list
 *     under a specific spec revision; computing one is 9.6. A placeholder field
 *     now would be a number nobody computed, which is worse than no field.
 *
 * What *is* here beyond storage is the vocabulary an implementer needs to be
 * unambiguous: the attribute operators (`=`, `~=`, `|=`, `^=`, `$=`, `*=`) as
 * an enum rather than as magic strings, the `i`/`s` case-sensitivity flags, and
 * combinators including the column combinator that modern CSS added.
 *
 * A functional pseudo-class's argument (`:not(...)`, `:nth-child(...)`) is kept
 * as one borrowed slice of raw text: whether it contains a selector list, an
 * An+B expression or a language range depends on which pseudo-class it is, and
 * splitting it here would be parsing.
 */

#ifndef ROBO_CSS_SELECTOR_H
#define ROBO_CSS_SELECTOR_H

#include "RoboBase/RoboError.h"
#include "RoboCSS/RoboCSSDiagnostics.h"
#include "RoboCSS/RoboCSSTypes.h"

#ifdef __cplusplus
extern "C" {
#endif

/** The kinds of simple selector. */
typedef enum robo_css_simple_selector_kind {
    /** No selector: a cleared record. */
    ROBO_CSS_SIMPLE_SELECTOR_NONE = 0,
    /** `div`, `input` — a type (tag name) condition. */
    ROBO_CSS_SIMPLE_SELECTOR_TYPE = 1,
    /** `*` — matches any element. */
    ROBO_CSS_SIMPLE_SELECTOR_UNIVERSAL = 2,
    /** `.card` */
    ROBO_CSS_SIMPLE_SELECTOR_CLASS = 3,
    /** `#main` */
    ROBO_CSS_SIMPLE_SELECTOR_ID = 4,
    /** `[type="text"]` */
    ROBO_CSS_SIMPLE_SELECTOR_ATTRIBUTE = 5,
    /** `:hover`, `:not(...)`, `:nth-child(2n+1)` */
    ROBO_CSS_SIMPLE_SELECTOR_PSEUDO_CLASS = 6,
    /** `::before`, `::marker` */
    ROBO_CSS_SIMPLE_SELECTOR_PSEUDO_ELEMENT = 7
} robo_css_simple_selector_kind_t;

/** Number of defined simple selector kinds. */
#define ROBO_CSS_SIMPLE_SELECTOR_KIND_COUNT 8u

/** Non-zero if @p kind is a defined simple selector kind. */
int robo_css_simple_selector_kind_is_known(robo_css_simple_selector_kind_t kind);

/** Human-readable simple selector kind name; never NULL. */
const char *robo_css_simple_selector_kind_name(robo_css_simple_selector_kind_t kind);

/** How an attribute selector compares. */
typedef enum robo_css_attribute_match {
    /** No comparison: `[disabled]` — presence only. */
    ROBO_CSS_ATTRIBUTE_MATCH_NONE = 0,
    /** `[a=b]` */
    ROBO_CSS_ATTRIBUTE_MATCH_EQUALS = 1,
    /** `[a~=b]` — one whitespace-separated word equals b. */
    ROBO_CSS_ATTRIBUTE_MATCH_INCLUDES = 2,
    /** `[a|=b]` — equals b or begins with `b-`. */
    ROBO_CSS_ATTRIBUTE_MATCH_DASH = 3,
    /** `[a^=b]` */
    ROBO_CSS_ATTRIBUTE_MATCH_PREFIX = 4,
    /** `[a$=b]` */
    ROBO_CSS_ATTRIBUTE_MATCH_SUFFIX = 5,
    /** `[a*=b]` */
    ROBO_CSS_ATTRIBUTE_MATCH_SUBSTRING = 6
} robo_css_attribute_match_t;

/** Number of defined attribute match operators. */
#define ROBO_CSS_ATTRIBUTE_MATCH_COUNT 7u

/** Non-zero if @p match is a defined attribute match enumerator. */
int robo_css_attribute_match_is_known(robo_css_attribute_match_t match);

/** Human-readable attribute match name; never NULL (e.g. "equals", "none"). */
const char *robo_css_attribute_match_name(robo_css_attribute_match_t match);

/** How an attribute selector treats ASCII case. */
typedef enum robo_css_attribute_case {
    /** The document language decides (HTML: some attributes are case-insensitive). */
    ROBO_CSS_ATTRIBUTE_CASE_DEFAULT = 0,
    /** `[a=b i]` — the value compares ASCII case-insensitively. */
    ROBO_CSS_ATTRIBUTE_CASE_INSENSITIVE = 1,
    /** `[a=b s]` — the value compares case-sensitively. */
    ROBO_CSS_ATTRIBUTE_CASE_SENSITIVE = 2
} robo_css_attribute_case_t;

/** Number of defined attribute case flags. */
#define ROBO_CSS_ATTRIBUTE_CASE_COUNT 3u

/** Non-zero if @p flag is a defined attribute case enumerator. */
int robo_css_attribute_case_is_known(robo_css_attribute_case_t flag);

/** Human-readable attribute case name; never NULL. */
const char *robo_css_attribute_case_name(robo_css_attribute_case_t flag);

/** A combinator that joins two compound selectors. */
typedef enum robo_css_combinator {
    /** No combinator (the start of a complex selector). */
    ROBO_CSS_COMBINATOR_NONE = 0,
    /** Whitespace: any ancestor. */
    ROBO_CSS_COMBINATOR_DESCENDANT = 1,
    /** `>` — the parent. */
    ROBO_CSS_COMBINATOR_CHILD = 2,
    /** `+` — the immediately preceding sibling. */
    ROBO_CSS_COMBINATOR_NEXT_SIBLING = 3,
    /** `~` — any preceding sibling. */
    ROBO_CSS_COMBINATOR_SUBSEQUENT_SIBLING = 4,
    /** `||` — the column combinator. */
    ROBO_CSS_COMBINATOR_COLUMN = 5
} robo_css_combinator_t;

/** Number of defined combinators. */
#define ROBO_CSS_COMBINATOR_COUNT 6u

/** Non-zero if @p combinator is a defined combinator enumerator. */
int robo_css_combinator_is_known(robo_css_combinator_t combinator);

/** Human-readable combinator name; never NULL (e.g. "child", "descendant"). */
const char *robo_css_combinator_name(robo_css_combinator_t combinator);

/** One simple selector. All slices borrow from the source. */
typedef struct robo_css_simple_selector {
    robo_css_simple_selector_kind_t kind;
    /** Extent of this simple selector in the source. */
    robo_css_range_t range;
    /** Type name / class / id / pseudo name, as written. */
    robo_css_slice_t name;
    /** ATTRIBUTE: the attribute's name, as written inside the brackets. */
    robo_css_slice_t attribute;
    /** ATTRIBUTE: the comparison value; absent for a presence-only selector. */
    robo_css_slice_t attribute_value;
    /** ATTRIBUTE: how that value compares. */
    robo_css_attribute_match_t match;
    /** ATTRIBUTE: how that comparison treats ASCII case. */
    robo_css_attribute_case_t case_flag;
    /** ATTRIBUTE: non-zero when the value was written as a quoted string. */
    int value_was_string;
    /** PSEUDO_CLASS/PSEUDO_ELEMENT: the raw text between the parentheses. */
    robo_css_slice_t argument;
} robo_css_simple_selector_t;

/** Clear @p selector to KIND NONE. Safe on NULL. */
void robo_css_simple_selector_init(robo_css_simple_selector_t *selector);

/** Disposing a simple selector clears it; it owns nothing. Safe on NULL. */
void robo_css_simple_selector_dispose(robo_css_simple_selector_t *selector);

/**
 * Begin a simple selector of @p kind with @p name.
 * UNIVERSAL carries no name, TYPE/CLASS/ID/PSEUDO_* do, and ATTRIBUTE carries
 * its attribute name instead (pass it through @p name and copy it to
 * `attribute`, or set it with robo_css_simple_selector_set_attribute).
 * @return ROBO_OK; ROBO_INVALID_ARGUMENT for a NULL selector, a NONE or unknown
 *         kind, or a kind that requires a name given an empty one.
 */
robo_result_t robo_css_simple_selector_make(robo_css_simple_selector_t *selector,
                                            robo_css_simple_selector_kind_t kind,
                                            robo_css_slice_t name,
                                            robo_css_diag_t *diag);

/**
 * Configure an ATTRIBUTE simple selector's comparison.
 * @return ROBO_OK; ROBO_INVALID_ARGUMENT for a NULL selector, a selector that
 *         is not ATTRIBUTE, an empty attribute name, an unknown match or case
 *         flag, or a comparison without a value.
 */
robo_result_t robo_css_simple_selector_set_attribute(
    robo_css_simple_selector_t *selector,
    robo_css_slice_t attribute,
    robo_css_attribute_match_t match,
    robo_css_slice_t value,
    robo_css_attribute_case_t case_flag,
    robo_css_diag_t *diag);

/** Record a pseudo-class/element's raw parenthesised argument (may be empty). */
robo_result_t robo_css_simple_selector_set_argument(
    robo_css_simple_selector_t *selector,
    robo_css_slice_t argument,
    robo_css_diag_t *diag);

/** Non-zero when @p selector is a usable condition (kind is not NONE). */
int robo_css_simple_selector_is_set(const robo_css_simple_selector_t *selector);

/** The compound selector that owns the simple selectors it holds. */
typedef struct robo_css_compound_selector {
    robo_css_simple_selector_t *items; /* owned; NULL when count == 0 */
    size_t count;
    size_t capacity;
    /** Extent of the whole compound selector. */
    robo_css_range_t range;
} robo_css_compound_selector_t;

/** Clear @p compound to empty. Safe on NULL. */
void robo_css_compound_selector_init(robo_css_compound_selector_t *compound);

/** Dispose every simple selector and free the array. Idempotent; safe on NULL. */
void robo_css_compound_selector_dispose(robo_css_compound_selector_t *compound);

/**
 * Append a *copy* of @p selector (simple selectors own nothing, so a copy is
 * safe and cheap). The first simple selector in a compound determines nothing;
 * order is preserved as written.
 * @return ROBO_OK; ROBO_INVALID_ARGUMENT for a NULL argument or an unset
 *         selector; ROBO_OUT_OF_MEMORY when the array cannot grow.
 */
robo_result_t robo_css_compound_selector_append(
    robo_css_compound_selector_t *compound,
    const robo_css_simple_selector_t *selector,
    robo_css_diag_t *diag);

/** Number of simple selectors (0 for NULL). */
size_t robo_css_compound_selector_count(const robo_css_compound_selector_t *compound);

/** Borrowed simple selector at @p index, or NULL when out of range. */
const robo_css_simple_selector_t *robo_css_compound_selector_at(
    const robo_css_compound_selector_t *compound,
    size_t index);

/** Non-zero when @p compound holds at least one simple selector. */
int robo_css_compound_selector_is_set(const robo_css_compound_selector_t *compound);

/**
 * Tighten the compound's range to span its simple selectors, or to the empty
 * range at 0 when it has none. The parser sets this after filling a compound;
 * it is never computed automatically, so an explicit range is never overwritten.
 */
void robo_css_compound_selector_seal_range(robo_css_compound_selector_t *compound);

/** A complex selector: N compounds joined by N-1 combinators. */
typedef struct robo_css_complex_selector {
    robo_css_compound_selector_t *compounds; /* owned */
    size_t compound_count;
    size_t compound_capacity;
    robo_css_combinator_t *combinators; /* owned; one fewer than the compounds */
    size_t combinator_count;
    size_t combinator_capacity;
    /** Extent of the whole complex selector. */
    robo_css_range_t range;
} robo_css_complex_selector_t;

/** Clear @p selector to empty. Safe on NULL. */
void robo_css_complex_selector_init(robo_css_complex_selector_t *selector);

/** Dispose every compound and combinator. Idempotent; safe on NULL. */
void robo_css_complex_selector_dispose(robo_css_complex_selector_t *selector);

/**
 * Append @p compound by moving it in; @p compound is left empty.
 * @return ROBO_OK; ROBO_INVALID_ARGUMENT for NULL arguments or an empty
 *         compound; ROBO_OUT_OF_MEMORY when the array cannot grow (the
 *         compound is then untouched).
 */
robo_result_t robo_css_complex_selector_append_compound(
    robo_css_complex_selector_t *selector,
    robo_css_compound_selector_t *compound,
    robo_css_diag_t *diag);

/** Append a combinator trailing the compound last appended. */
robo_result_t robo_css_complex_selector_append_combinator(
    robo_css_complex_selector_t *selector,
    robo_css_combinator_t combinator,
    robo_css_diag_t *diag);

/** Number of compounds (0 for NULL). */
size_t robo_css_complex_selector_compound_count(
    const robo_css_complex_selector_t *selector);

/** Borrowed compound at @p index, or NULL when out of range. */
const robo_css_compound_selector_t *robo_css_complex_selector_compound_at(
    const robo_css_complex_selector_t *selector,
    size_t index);

/** Number of combinators (0 for NULL). */
size_t robo_css_complex_selector_combinator_count(
    const robo_css_complex_selector_t *selector);

/**
 * The combinator between compound @p index and compound `index + 1`.
 * @return NONE when out of range. A NULL output is tolerated.
 */
robo_css_combinator_t robo_css_complex_selector_combinator_at(
    const robo_css_complex_selector_t *selector,
    size_t index);

/**
 * Non-zero when @p selector is structurally coherent: at least one compound,
 * and exactly one fewer combinator than compounds. The parser builds in the
 * order compound → combinator → compound, so this cannot be enforced append by
 * append; it is checked here, once, by whoever wants to know.
 */
int robo_css_complex_selector_is_well_formed(
    const robo_css_complex_selector_t *selector);

/** A selector list: the comma-separated complex selectors of one rule. */
typedef struct robo_css_selector_list {
    robo_css_complex_selector_t *items; /* owned */
    size_t count;
    size_t capacity;
    /** Extent of the whole list, including the commas. */
    robo_css_range_t range;
} robo_css_selector_list_t;

/** Clear @p list to empty. Safe on NULL. */
void robo_css_selector_list_init(robo_css_selector_list_t *list);

/** Dispose every complex selector and free the array. Idempotent; safe on NULL. */
void robo_css_selector_list_dispose(robo_css_selector_list_t *list);

/**
 * Append @p selector by moving it in; @p selector is left empty.
 * @return ROBO_OK; ROBO_INVALID_ARGUMENT for NULL arguments or an empty
 *         selector; ROBO_OUT_OF_MEMORY when the array cannot grow.
 */
robo_result_t robo_css_selector_list_append(robo_css_selector_list_t *list,
                                            robo_css_complex_selector_t *selector,
                                            robo_css_diag_t *diag);

/** Number of complex selectors (0 for NULL). */
size_t robo_css_selector_list_count(const robo_css_selector_list_t *list);

/** Borrowed complex selector at @p index, or NULL when out of range. */
const robo_css_complex_selector_t *robo_css_selector_list_at(
    const robo_css_selector_list_t *list,
    size_t index);

/** Non-zero when @p list holds at least one complex selector. */
int robo_css_selector_list_is_set(const robo_css_selector_list_t *list);

/** Non-zero when every complex selector in @p list is well formed. */
int robo_css_selector_list_is_well_formed(const robo_css_selector_list_t *list);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_CSS_SELECTOR_H */
