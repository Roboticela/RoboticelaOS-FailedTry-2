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
 *
 * Phase 9.4 added what parsing needed and nothing else:
 *
 *   names        a selector list owns one byte arena for the *resolved*
 *                spelling of every name in it. Escapes are resolved once, by
 *                the parser, through the tokenizer's own value rule (9.2) — so
 *                `.foo\:bar` carries the six bytes `foo:bar` and matching never
 *                has to re-resolve. A hand-built selector keeps whatever slice
 *                it was given, which is why the accessors borrow rather than
 *                interpret.
 *   nested       `:not(<selector list>)` is a real, owned selector list held by
 *                the *compound* that owns the pseudo-class, addressed by index
 *                from the simple selector. Keeping the index (not a pointer) on
 *                the simple selector is what preserves 9.1's one copy rule: a
 *                simple selector still owns nothing, so copying one is still
 *                safe. A copy's index is cleared by
 *                robo_css_compound_selector_append, because an index only means
 *                something inside the compound that issued it.
 *   nth          `:nth-child(2n+1)`'s An+B, parsed once. `a` and `b` are
 *                integers and `n` runs over the non-negative integers; the
 *                argument text stays on the selector as the "as written" form.
 *
 * What 9.4 still does not put here: specificity, and any notion of which
 * element a selector "won". Matching lives in RoboCSSMatcher.h and reads these
 * structures; it never fills them and never mutates them.
 */

#ifndef ROBO_CSS_SELECTOR_H
#define ROBO_CSS_SELECTOR_H

#include "RoboBase/RoboError.h"
#include "RoboCSS/RoboCSSDiagnostics.h"
#include "RoboCSS/RoboCSSTypes.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * How deep functional pseudo-class arguments may nest before a selector is
 * rejected: `:not(:not(:not(...)))` inside itself, up to this many levels.
 *
 * The bound exists for the same reason ROBO_CSS_MAX_NESTING does in the value
 * model — parsing a nested argument is a recursive call, and adversarial input
 * must cost bounded stack rather than a crash. It is deliberately much smaller
 * than the value-model bound: real stylesheets nest `:not()` one or two deep,
 * so eight is generous, and anything past it is malformed rather than merely
 * unusual.
 */
#define ROBO_CSS_MAX_SELECTOR_NESTING 8u

/**
 * "This simple selector has no parsed argument list" — the value
 * `nested_index` holds until robo_css_compound_selector_attach_nested sets it.
 */
#define ROBO_CSS_NESTED_NONE ((size_t)-1)

/** Forward declaration: a compound owns nested argument lists of this type. */
typedef struct robo_css_selector_list robo_css_selector_list_t;

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
    /**
     * PSEUDO_CLASS: index into the owning compound's nested argument lists of
     * this pseudo-class's parsed selector argument (`:not(...)`), or
     * ROBO_CSS_NESTED_NONE when its argument is not a selector list, was not
     * parsed, or the selector was built by hand.
     *
     * An index rather than a list, so that this record keeps owning nothing. It
     * is meaningful only against the compound it was attached to; a copy made by
     * robo_css_compound_selector_append has it cleared, and a reader must treat
     * an out-of-range index as "no argument" rather than trusting it.
     */
    size_t nested_index;
    /**
     * `:nth-child()` / `:nth-last-child()`: non-zero when the argument was
     * parsed as An+B and `nth_a`/`nth_b` are meaningful.
     */
    int nth_parsed;
    /** The `a` coefficient of An+B (`0` for a bare integer such as `3`). */
    int nth_a;
    /** The `b` offset of An+B. */
    int nth_b;
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

/**
 * The nested argument list index @p selector was given, or ROBO_CSS_NESTED_NONE
 * for NULL, for a selector that has none, and for a selector whose index has not
 * been resolved against a compound (see robo_css_compound_selector_nested_at).
 */
size_t robo_css_simple_selector_nested_index(const robo_css_simple_selector_t *selector);

/**
 * Record the An+B form of a `:nth-child()`-family argument.
 * `a` and `b` are signed, which is what makes `-n+3` and `n-1` expressible;
 * `a == 0` is the bare-integer form (`3` means `0n+3`).
 * @return ROBO_OK; ROBO_INVALID_ARGUMENT for a NULL selector or a selector
 *         whose kind is not PSEUDO_CLASS.
 */
robo_result_t robo_css_simple_selector_set_nth(robo_css_simple_selector_t *selector,
                                               int a,
                                               int b,
                                               robo_css_diag_t *diag);

/** Non-zero when @p selector carries a parsed An+B. */
int robo_css_simple_selector_has_nth(const robo_css_simple_selector_t *selector);

/** The parsed `a` coefficient (0 when there is none). */
int robo_css_simple_selector_nth_a(const robo_css_simple_selector_t *selector);

/** The parsed `b` offset (0 when there is none). */
int robo_css_simple_selector_nth_b(const robo_css_simple_selector_t *selector);

/**
 * Non-zero when @p selector's pseudo-class or pseudo-element is named @p ascii,
 * compared ASCII case-insensitively (CSS pseudo names are ASCII
 * case-insensitive). False for a NULL selector, a selector whose kind is not a
 * pseudo kind, and an unset name.
 */
int robo_css_simple_selector_is_pseudo_named(const robo_css_simple_selector_t *selector,
                                            const char *ascii);

/** Non-zero when @p selector is a usable condition (kind is not NONE). */
int robo_css_simple_selector_is_set(const robo_css_simple_selector_t *selector);

/** The compound selector that owns the simple selectors it holds. */
typedef struct robo_css_compound_selector {
    robo_css_simple_selector_t *items; /* owned; NULL when count == 0 */
    size_t count;
    size_t capacity;
    /** Extent of the whole compound selector. */
    robo_css_range_t range;
    /**
     * Owned nested argument lists, addressed by index from the simple selectors
     * above: `:not(...)`'s argument, and later any other functional form whose
     * argument is itself a selector list.
     *
     * They live here rather than on the simple selector for the ownership
     * reason stated at the top of this header — a simple selector owns nothing,
     * so it is still copied rather than moved. Each entry is a complete
     * `robo_css_selector_list_t` with its own name arena, so a nested list is
     * disposed exactly as a top-level one is.
     */
    robo_css_selector_list_t *nested; /* owned; NULL when nested_count == 0 */
    size_t nested_count;
    size_t nested_capacity;
} robo_css_compound_selector_t;

/** Clear @p compound to empty. Safe on NULL. */
void robo_css_compound_selector_init(robo_css_compound_selector_t *compound);

/**
 * Dispose every nested argument list, then free the simple selector array and
 * the nested array. The simple selectors themselves own nothing, so there is
 * nothing to release element by element. Idempotent; safe on NULL.
 */
void robo_css_compound_selector_dispose(robo_css_compound_selector_t *compound);

/**
 * Append a *copy* of @p selector (simple selectors own nothing, so a copy is
 * safe and cheap). The first simple selector in a compound determines nothing;
 * order is preserved as written.
 *
 * The copy's `nested_index` is cleared: an index names a list inside the
 * compound that issued it, so carrying one into a different compound could only
 * ever be wrong. The parser attaches a list to the copy afterwards with
 * robo_css_compound_selector_attach_nested.
 *
 * @return ROBO_OK; ROBO_INVALID_ARGUMENT for a NULL argument or an unset
 *         selector; ROBO_OUT_OF_MEMORY when the array cannot grow.
 */
robo_result_t robo_css_compound_selector_append(
    robo_css_compound_selector_t *compound,
    const robo_css_simple_selector_t *selector,
    robo_css_diag_t *diag);

/**
 * Move @p list into @p compound's nested lists and point simple selector
 * @p simple_index at it.
 *
 * The two-step shape (append the simple selector, then attach its argument) is
 * what keeps the index and the list in step: the index is written only after the
 * list exists, so no window exists in which a simple selector addresses a hole.
 *
 * @return ROBO_OK; ROBO_INVALID_ARGUMENT for NULL arguments or an out-of-range
 *         @p simple_index; ROBO_OUT_OF_MEMORY when the array cannot grow (the
 *         list is then untouched, and the caller still owns it).
 */
robo_result_t robo_css_compound_selector_attach_nested(
    robo_css_compound_selector_t *compound,
    size_t simple_index,
    robo_css_selector_list_t *list,
    robo_css_diag_t *diag);

/** Number of nested argument lists @p compound owns (0 for NULL). */
size_t robo_css_compound_selector_nested_count(const robo_css_compound_selector_t *compound);

/**
 * Borrowed nested argument list @p index, or NULL when out of range.
 *
 * Prefer this over reading `nested_index` directly: a simple selector copied
 * between compounds, or one whose index was never attached, addresses nothing,
 * and this returns NULL for that instead of dereferencing a hole.
 */
const robo_css_selector_list_t *robo_css_compound_selector_nested_at(
    const robo_css_compound_selector_t *compound,
    size_t index);

/**
 * The nested list simple selector @p simple_index actually resolves to, or NULL
 * when it has none or its index does not resolve. The one call a matcher needs.
 */
const robo_css_selector_list_t *robo_css_compound_selector_argument_of(
    const robo_css_compound_selector_t *compound,
    size_t simple_index);

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
struct robo_css_selector_list {
    robo_css_complex_selector_t *items; /* owned */
    size_t count;
    size_t capacity;
    /** Extent of the whole list, including the commas. */
    robo_css_range_t range;
    /**
     * Owned arena holding the resolved spelling of every name in this list, its
     * nested argument lists included. NULL until something is interned.
     *
     * Escapes are resolved once, here, so that matching compares the bytes CSS
     * meant rather than the bytes the author typed. The arena never moves after
     * the parser reserves it (9.4 reserves it up front from the prelude's
     * length), which is what lets slices into it stay valid for the list's life.
     */
    char *names;
    size_t names_length;
    size_t names_capacity;
};

/**
 * Reserve @p bytes of name arena up front.
 *
 * The parser calls this before interning anything so that interning can never
 * reallocate and invalidate a name it already handed out. Reserving twice keeps
 * the larger reservation. A caller that does not reserve still works: interning
 * grows the arena itself, it just cannot guarantee its earlier slices survive.
 * @return ROBO_OK; ROBO_INVALID_ARGUMENT for NULL; ROBO_OUT_OF_MEMORY.
 */
robo_result_t robo_css_selector_list_reserve_names(robo_css_selector_list_t *list,
                                                  size_t bytes,
                                                  robo_css_diag_t *diag);

/**
 * Copy @p length bytes at @p bytes into @p list's name arena and return a slice
 * over the copy.
 *
 * A zero-length name interns as an *empty but present* slice, not as the absent
 * one: `[a=""]` is an attribute selector comparing against an empty value, and
 * `[a]` is a different selector that asks only whether the attribute is there.
 * Conflating the two here would make them the same selector one layer up.
 *
 * The absent slice means failure — a NULL list, a non-zero length with no bytes,
 * or an arena that cannot grow — and that is the one case a caller must check.
 * The parser does, and fails the parse rather than recording a name that points
 * nowhere.
 */
robo_css_slice_t robo_css_selector_list_intern(robo_css_selector_list_t *list,
                                               const uint8_t *bytes,
                                               size_t length);

/** Bytes used in @p list's name arena (0 for NULL). */
size_t robo_css_selector_list_names_length(const robo_css_selector_list_t *list);

/** Borrowed arena contents, or NULL when nothing was interned. */
const char *robo_css_selector_list_names(const robo_css_selector_list_t *list);

/** Clear @p list to empty. Safe on NULL. */
void robo_css_selector_list_init(robo_css_selector_list_t *list);

/**
 * Dispose every complex selector — and, through them, their compounds' nested
 * argument lists — then free the array and the name arena. Idempotent; safe on
 * NULL.
 */
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
