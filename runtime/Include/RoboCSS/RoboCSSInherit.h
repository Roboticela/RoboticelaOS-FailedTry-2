/*
 * RoboCSS inheritance (Phase 9.7).
 *
 * The cascade answers "which declaration wins for this element". Inheritance
 * answers the question underneath it: "and what about the properties this
 * element says nothing about", which for an inherited property means *the
 * parent's answer* and for a non-inherited one means *the property's own
 * initial value*. The result is a resolved style: one entry per property that
 * this element has an answer for, with where the answer came from.
 *
 * Where it sits
 *
 *   DOM element
 *       ↓  selector matching      (9.4)
 *       ↓  specificity            (9.6)
 *       ↓  cascade                (9.5)
 *   inheritance                   (9.7, here)
 *       ↓  resolved style
 *   computed values               (9.8)
 *
 * Inheritance runs **after** the cascade and never competes with it. A value
 * this element declares always wins over a value it could have inherited —
 * including an `!important` declaration on the parent, which has no way to
 * reach a child, because importance is a fact about a declaration on *this*
 * element and inheritance is not a declaration at all.
 *
 * One entry per property, and what the entry says
 *
 * A resolved style holds exactly the properties this element has an answer for:
 *
 *   - every property the element's own cascade declares, one entry each,
 *     resolved through the CSS-wide keywords;
 *   - every property that inherits by default and that the parent carries,
 *     because that is what inheriting *is*;
 *   - every property that inherits by default that neither of the above
 *     supplied, resolved to its initial value — which is where a root element's
 *     inherited properties come from, and what a caller who passes no parent
 *     style is telling this module: "there is no parent, so use the default".
 *
 * A property that does **not** inherit and that nothing declares has **no
 * entry**. That is not a gap to be filled: `width` is not on an element because
 * nobody said what it is, and inventing `auto` here would put a value in a
 * resolution that no input produced. The entry appears the moment something
 * asks for `width` — a declaration, `initial`, `unset`, or `inherit` — and the
 * value 9.8 goes looking for when nothing asked is the registry's, which is
 * where a property's initial value belongs.
 *
 * Custom properties are the one family with no initial value at all: an unset
 * custom property is *invalid*, not `initial`. So a custom property that
 * neither the element nor the parent declared simply has no entry, and stays
 * that way however deep the tree goes.
 *
 * The CSS-wide keywords
 *
 * `inherit`, `initial` and `unset` are not values like other values; they are
 * instructions about *where to get* a value, and this header is where they are
 * obeyed. They are recognised on a parsed value (`robo_css_wide_keyword_of`),
 * never on a string, and an entry records both where its value came from
 * (`source`) and which keyword produced it (`keyword`) — so a consumer can tell
 * "the parent's colour because `color` inherits" from "the parent's colour
 * because someone wrote `color: inherit`", which are the same value by
 * different routes and different facts.
 *
 * `revert` is recognised and **not obeyed**. Rolling it back means re-running
 * the cascade with one origin's declarations removed, which is a question about
 * the cascade's inputs rather than about inheritance, and answering it half-way
 * (treating it as `unset`) would be wrong in exactly the cases it exists for.
 * So a `revert` declaration is carried as a *deferred* entry: the value is the
 * keyword itself, marked unresolved, counted, and reported through
 * `unsupported` — never silently resolved into something that looks like an
 * answer. `revert-layer` is not recognised at all, because cascading layers do
 * not exist in this model.
 *
 * Explicit parent, not a walk
 *
 * The resolution door takes the parent's resolved style as an *argument*. This
 * module never walks up a tree looking for a parent of its own accord, and
 * never keeps a global "current style" — resolving a child without a parent
 * style is not an error, it is the caller saying there is none, and it produces
 * the same values a root element gets. That is what makes the whole thing a
 * pure function of its inputs, testable one element at a time, and free of the
 * caches that would otherwise need invalidating: nothing is stored between
 * calls, so nothing can go stale. When a parent's style or a stylesheet
 * changes, the caller re-resolves, and that *is* the invalidation boundary for
 * this phase. (9.8 and the layout phases will want more than that; they will
 * want it on top of this, not instead of it.)
 *
 * Ownership
 *
 * A resolved style owns everything it reports: each entry's name is a copy and
 * each entry's value is a deep copy of whatever it came from. The parent's
 * style is read and never modified, the cascade is read and never modified, and
 * a style can outlive every sheet and cascade it was resolved from. Copying
 * rather than borrowing is deliberate — an inherited value that aliased its
 * parent's would be freed through the parent's disposal, and the two styles
 * would stop being independent objects.
 *
 * One dependency this phase is careful about: a value carries slices into the
 * bytes that declared it, so an entry's *name* is copied (which frees it from
 * the sheet's lifetime) and its *value* is copied (which frees it from the
 * parent's), but the bytes inside that value — a number's text, an identifier —
 * still belong to the stylesheet or source the declaration came from. A
 * resolved style therefore stays valid as long as the sheets it was resolved
 * from, which is a lifetime the caller already owns because it owns them.
 */

#ifndef ROBO_CSS_INHERIT_H
#define ROBO_CSS_INHERIT_H

#include "RoboBase/RoboError.h"
#include "RoboCSS/RoboCSSCascade.h"
#include "RoboCSS/RoboCSSDeclaration.h"
#include "RoboCSS/RoboCSSDiagnostics.h"
#include "RoboCSS/RoboCSSProperty.h"
#include "RoboCSS/RoboCSSPropertyRegistry.h"
#include "RoboCSS/RoboCSSStyle.h"
#include "RoboCSS/RoboCSSTypes.h"
#include "RoboCSS/RoboCSSValue.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * How far the ancestor walk goes before it stops.
 *
 * The context door resolves an element by walking to its root, collecting a
 * cascade at each level. The bound is the cycle guard: a DOM that reports a
 * cycle among parents (`a`'s parent is `b`, `b`'s parent is `a`) would otherwise
 * walk forever, and this module cannot see enough of the tree to detect that —
 * it only ever asks for one element's parent. Reaching the bound is *reported*,
 * not hidden, because the top of a capped chain is not a root and pretending it
 * was would produce a wrong answer that looked right.
 *
 * 64 is well past any tree a document has, and the cost of being wrong in the
 * other direction is a bounded walk rather than a hang.
 */
#define ROBO_CSS_INHERIT_MAX_DEPTH 64u

/* ------------------------------------------------------------------ */
/* The CSS-wide keywords                                               */
/* ------------------------------------------------------------------ */

/**
 * A CSS-wide keyword: `inherit`, `initial`, `unset`, `revert` — or none.
 *
 * These are not properties and not values; they are the four words that can
 * appear where a value can and mean "not this value, that one instead". NONE is
 * the answer for every value that is not one of them, which includes every
 * property-specific keyword (`auto`, `normal`, `static`), because those are
 * ordinary values that happen to be single identifiers.
 */
typedef enum robo_css_wide_keyword {
    /** Not a CSS-wide keyword. */
    ROBO_CSS_WIDE_KEYWORD_NONE = 0,
    /** Take the parent's resolved value. */
    ROBO_CSS_WIDE_KEYWORD_INHERIT = 1,
    /** Take the property's initial value. */
    ROBO_CSS_WIDE_KEYWORD_INITIAL = 2,
    /** `inherit` for an inherited property, `initial` for any other. */
    ROBO_CSS_WIDE_KEYWORD_UNSET = 3,
    /** Roll back to the previous origin — recognised, deliberately not obeyed. */
    ROBO_CSS_WIDE_KEYWORD_REVERT = 4
} robo_css_wide_keyword_t;

/** Number of defined keywords (not counting NONE). */
#define ROBO_CSS_WIDE_KEYWORD_COUNT 5u

/** Non-zero when @p keyword is a defined enumerator. */
int robo_css_wide_keyword_is_known(robo_css_wide_keyword_t keyword);

/** Human-readable keyword name ("inherit", "revert"); never NULL. */
const char *robo_css_wide_keyword_name(robo_css_wide_keyword_t keyword);

/**
 * Classify a parsed value as a CSS-wide keyword.
 *
 * A keyword is a *single* identifier and nothing else: `inherit` is one,
 * `inherit inherit` is not, and `red inherit` is not. The comparison is made on
 * the decoded identifier (`robo_css_token_value_equals_ascii_ci`), so an escape
 * spelling like `\69 nherit` is the keyword, which is what CSS says — an escape
 * changes which bytes spell a name, never which name it is.
 *
 * A value that is not a single identifier — including one that is empty, a
 * function, a block, or several tokens — answers NONE, which is the only safe
 * answer: `calc(inherit)` is not the keyword, and reading it as one would
 * resolve a property from a value the author never wrote.
 *
 * @return the keyword, or NONE. A NULL value answers NONE.
 */
robo_css_wide_keyword_t robo_css_wide_keyword_of(const robo_css_value_t *value);

/* ------------------------------------------------------------------ */
/* Where a value came from                                             */
/* ------------------------------------------------------------------ */

/**
 * The provenance of one resolved value.
 *
 * This is the "why" a debugging tool or a later phase needs, and it is recorded
 * rather than inferred because once a value is a copy, the copy does not
 * remember. Five answers, and each is a different fact:
 */
typedef enum robo_css_inherited_source {
    /** No entry: a cleared record. */
    ROBO_CSS_INHERITED_SOURCE_NONE = 0,
    /** The element's own cascade declared it (a normal value). */
    ROBO_CSS_INHERITED_SOURCE_DECLARED = 1,
    /** The parent's resolved style supplied it — by default inheritance, or by a keyword. */
    ROBO_CSS_INHERITED_SOURCE_PARENT = 2,
    /** The property's initial/default value supplied it. */
    ROBO_CSS_INHERITED_SOURCE_DEFAULT = 3,
    /** The `revert` keyword, carried unresolved for a later phase. */
    ROBO_CSS_INHERITED_SOURCE_DEFERRED = 4
} robo_css_inherited_source_t;

/** Number of defined sources (not counting NONE). */
#define ROBO_CSS_INHERITED_SOURCE_COUNT 5u

/** Non-zero when @p source is a defined enumerator. */
int robo_css_inherited_source_is_known(robo_css_inherited_source_t source);

/** Human-readable source name ("declared", "parent", "default", "deferred"); never NULL. */
const char *robo_css_inherited_source_name(robo_css_inherited_source_t source);

/* ------------------------------------------------------------------ */
/* One resolved property                                               */
/* ------------------------------------------------------------------ */

/**
 * One property this element has an answer for.
 *
 * `name` is owned by the style; `value` is owned by the style. `property` is
 * the identity the *cascade* resolved against the context's property table, and
 * is ROBO_CSS_PROPERTY_ID_NONE for a custom property (`--x`) or a name no table
 * knows — in which case `name` is the identity, and it is compared the way CSS
 * compares that kind of name (see robo_css_is_custom_property_name).
 */
typedef struct robo_css_inherited_property {
    /** Identity from the context's table, or NONE when the name has none. */
    robo_css_property_id_t property;
    /** Owned, NUL-terminated name. */
    char *name;
    /** Length of @p name in bytes, excluding its NUL. */
    size_t name_length;
    robo_css_inherited_source_t source;
    /**
     * The CSS-wide keyword this value came from, or NONE for an ordinary
     * declaration. Recorded because the value alone cannot say it: a parent's
     * `color` and `color: inherit` are the same colour by different routes.
     */
    robo_css_wide_keyword_t keyword;
    /** Owned value; KIND_NONE exactly when @p unresolved is set. */
    robo_css_value_t value;
    /**
     * Non-zero when the property's default value is not known to this build.
     *
     * The entry exists and is typed, which is the point: "nothing declares this
     * property and I do not know what it starts as" is a fact worth carrying,
     * and it is not the same fact as "this property has no entry". It happens
     * when `initial`, `unset`, or a root's inherited default applies to a
     * property absent from the registry — which is why the value is empty
     * rather than invented.
     */
    int unresolved;
} robo_css_inherited_property_t;

/** Borrowed name slice of @p property (empty for NULL). */
robo_css_slice_t robo_css_inherited_property_name(
    const robo_css_inherited_property_t *property);

/** Borrowed value of @p property, or NULL when it has none. */
const robo_css_value_t *robo_css_inherited_property_value(
    const robo_css_inherited_property_t *property);

/* ------------------------------------------------------------------ */
/* The resolved style                                                  */
/* ------------------------------------------------------------------ */

/**
 * Everything one element resolved to, plus what the resolution could not settle.
 *
 * The entries are a set, not a sequence: order is registration order (the
 * cascade's candidate order first, then the parent's, then the registry's) and
 * is stable for a given input, but no meaning is attached to it. Lookup is by
 * name or by property id.
 *
 * The counters are what keep the result from being read as more complete than
 * it is, exactly as the cascade's counters do.
 */
typedef struct robo_css_inherited_style {
    /** Owned entries. */
    robo_css_inherited_property_t *items;
    size_t count;
    size_t capacity;
    /** The element this was resolved for; NONE when the caller had no cascade. */
    robo_css_element_ref_t element;
    /** Non-zero when a parent style was supplied. */
    int has_parent;

    /* --- how each entry was decided --- */
    /** Entries whose value the element's own cascade declared (source DECLARED). */
    size_t declared_count;
    /** Entries whose value came from the parent (source PARENT). */
    size_t inherited_count;
    /** Entries resolved to a property default (source DEFAULT). */
    size_t default_count;
    /** Entries carrying the `revert` keyword unresolved (source DEFERRED). */
    size_t deferred_count;
    /**
     * Entries whose default value this build does not know. A subset of
     * `default_count`, and the reason `unsupported` is set.
     */
    size_t unresolved_count;
    /**
     * Non-zero when this resolution met something it could not settle: an
     * unknown property default, a `revert` carried unresolved, an ancestor walk
     * that hit its depth bound, or a cascade that was itself incomplete.
     *
     * 0 means the answer is complete — **not** that the element declared
     * anything. An element with no declarations at all resolves to a complete
     * style: defaults are answers.
     */
    int unsupported;
} robo_css_inherited_style_t;

/** Clear @p style to empty. Safe on NULL. */
void robo_css_inherited_style_init(robo_css_inherited_style_t *style);

/** Dispose every entry's name and value, and the array. Idempotent; safe on NULL. */
void robo_css_inherited_style_dispose(robo_css_inherited_style_t *style);

/** Number of entries (0 for NULL). */
size_t robo_css_inherited_style_count(const robo_css_inherited_style_t *style);

/** Borrowed entry at @p index, or NULL when out of range. */
const robo_css_inherited_property_t *robo_css_inherited_style_at(
    const robo_css_inherited_style_t *style,
    size_t index);

/**
 * Find the entry for @p name, comparing the way CSS compares that kind of name
 * (ASCII case-insensitive, or byte-exact for a custom property).
 * @return a borrowed entry, or NULL. Valid until @p style is disposed.
 */
const robo_css_inherited_property_t *robo_css_inherited_style_find(
    const robo_css_inherited_style_t *style,
    robo_css_slice_t name);

/**
 * Find the entry for a property identity.
 *
 * Property ids are per table, so this answers a question about *this* style's
 * ids and is only meaningful for a caller that resolved through the same table
 * the cascade used. Prefer the name overload when in doubt; this one exists
 * because that is the spelling a caller with an id already in hand wants.
 */
const robo_css_inherited_property_t *robo_css_inherited_style_find_property(
    const robo_css_inherited_style_t *style,
    robo_css_property_id_t property);

/** Non-zero when @p style has an entry for @p name. */
int robo_css_inherited_style_has(const robo_css_inherited_style_t *style,
                                 robo_css_slice_t name);

/** Non-zero when @p style met nothing it could not settle (0 for NULL). */
int robo_css_inherited_style_is_complete(const robo_css_inherited_style_t *style);

/** The element @p style was resolved for; NONE for NULL. */
robo_css_element_ref_t robo_css_inherited_style_element(
    const robo_css_inherited_style_t *style);

/** Non-zero when a parent style was supplied to the resolution (0 for NULL). */
int robo_css_inherited_style_has_parent(const robo_css_inherited_style_t *style);

/* ------------------------------------------------------------------ */
/* Resolution                                                          */
/* ------------------------------------------------------------------ */

/**
 * Resolve @p cascade into @p out, taking inherited values from @p parent.
 *
 * The primitive, and the only door that does any work: every other way into
 * this module is a walk that calls it once per level. It takes its parent
 * **explicitly**, so the same call with the same inputs always produces the
 * same result, and a caller that has a parent style (because it resolved the
 * parent a moment ago) pays nothing for it.
 *
 * @p parent may be NULL, which means "no parent": a root element, or a caller
 * that did not resolve one. Inherited properties then take their initial value,
 * which is exactly what a root does, and `has_parent` on the result records the
 * difference so a caller can tell the two apart.
 *
 * @p cascade may be NULL, which means "nothing is declared for this element" —
 * a legitimate state (an element whose document has no stylesheets) and not an
 * error. Its `element` and its completeness are what the result reports.
 *
 * An incomplete cascade makes the result incomplete: if the collection could
 * not decide a selector, then a property that *should* have been declared here
 * might be inherited from the parent instead, and a consumer cannot tell which.
 * `unsupported` is set, and that is the whole point of the flag.
 *
 * @p diag may be NULL. The output is initialised before any work, so a caller
 * that ignores the result code still owns a disposable record.
 *
 * @return ROBO_OK whenever a resolution was produced; ROBO_INVALID_ARGUMENT for
 *         a NULL context or output; ROBO_OUT_OF_MEMORY when no answer could be
 *         produced, in which case @p out is left empty rather than half-filled.
 */
robo_result_t robo_css_inherit_resolve(const robo_css_style_context_t *context,
                                       const robo_css_cascade_t *cascade,
                                       const robo_css_inherited_style_t *parent,
                                       robo_css_inherited_style_t *out,
                                       robo_css_diag_t *diag);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_CSS_INHERIT_H */
