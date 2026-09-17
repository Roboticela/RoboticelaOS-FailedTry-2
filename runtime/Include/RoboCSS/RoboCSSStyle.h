/*
 * RoboCSS style boundary — the DOM/CSS seam (Phase 9.1).
 *
 * RoboHTML and RoboCSS are peers: per DEPENDENCIES.md neither may depend on the
 * other, and both meet at the DOM. That rule has a concrete consequence here,
 * which is the entire design of this header:
 *
 *   RoboCSS never holds a DOM node, never includes a DOM header, and never
 *   copies a DOM tree. It is handed a *view* of one element at a time, by a
 *   callback its host installed, and it asks for the next one the same way.
 *
 * So the seam has two halves and no shared storage:
 *
 *   the host (the DOM side, or an application)        RoboCSS
 *   ──────────────────────────────────────────        ───────────────
 *   fills robo_css_element_view_t from a node         reads the view
 *   answers the element-lookup callback               walks selectors (9.4)
 *   answers the three relation callbacks              matches combinators (9.4)
 *   answers the inline-style callback with the        parses that text later (9.3)
 *     `style` attribute's *text*
 *   owns the attribute bytes                          owns the stylesheets
 *
 * Three boundaries from the phase specification are the same seam seen from
 * three directions:
 *
 *   9.1.9  DOM integration    the element view and the lookup callback — the
 *                             element is identified by an opaque token, and
 *                             every fact about it (tag, id, classes,
 *                             attributes) arrives in the view. 9.4 extended
 *                             this with the three *relation* callbacks
 *                             (parent, previous, next) that combinator and
 *                             structural-pseudo-class matching needs
 *   9.1.10 inline styles      robo_css_inline_style_fn returns the `style`
 *                             attribute's text; RoboCSS parses it into
 *                             declarations and stores none of it. There is no
 *                             second attribute store because there is no store
 *   9.1.11 default styles     the RUNTIME origin is a slot on the style
 *                             context, exactly like USER and AUTHOR. This
 *                             build ships no default stylesheet — the slot is
 *                             there and empty, which is what "the boundary
 *                             exists, the styles do not yet" looks like
 *
 * The style context is where a host assembles those pieces. It owns nothing
 * that belongs to somebody else: stylesheets are referenced by id and stay
 * owned by the module, the property table and the callbacks are borrowed, and
 * the sheets it references are the caller's to destroy. Destroying the context
 * therefore frees the context and nothing else.
 *
 * The resolution doors have arrived one phase at a time.
 * robo_css_style_context_match_declarations is **real as of 9.5**: it walks the
 * context's sheets, matches their rules through the host (9.4), parses the
 * element's `style` attribute through the same grammar as any other
 * declarations, and hands back a robo_css_cascade_t (RoboCSSCascade.h) in
 * cascade order. robo_css_style_context_resolve_inherited_style is **real as of
 * 9.7**: it collects a cascade per ancestor and resolves the chain top down,
 * handing back a robo_css_inherited_style_t (RoboCSSInherit.h).
 * robo_css_style_context_compute_style is still 9.8 and still answers
 * ROBO_NOT_SUPPORTED, because computing a value needs colours and units as well
 * as a cascade and an inheritance behind it.
 */

#ifndef ROBO_CSS_STYLE_H
#define ROBO_CSS_STYLE_H

#include "RoboBase/RoboError.h"
#include "RoboCSS/RoboCSSComputed.h"
#include "RoboCSS/RoboCSSDeclaration.h"
#include "RoboCSS/RoboCSSDiagnostics.h"
#include "RoboCSS/RoboCSSError.h"
#include "RoboCSS/RoboCSSProperty.h"
#include "RoboCSS/RoboCSSStyleSheet.h"
#include "RoboCSS/RoboCSSTypes.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * An opaque reference to one DOM element.
 *
 * RoboCSS never dereferences this and never learns what it points at; it only
 * carries it back to the host's callbacks. 0 means "no element".
 */
typedef uint64_t robo_css_element_ref_t;

#define ROBO_CSS_ELEMENT_REF_NONE ((robo_css_element_ref_t)0u)

/** One attribute of an element view. Both slices borrow from the host. */
typedef struct robo_css_attribute {
    robo_css_slice_t name;
    robo_css_slice_t value;
} robo_css_attribute_t;

/**
 * What RoboCSS is allowed to know about one element.
 *
 * The host fills this in; every field is borrowed and must stay valid for the
 * duration of the call it was filled for. `classes` points at an array of
 * slices the host owns — robo_css_split_ascii_whitespace is the helper for
 * turning a `class` attribute into one, but a host that keeps a class list
 * already can hand that over instead.
 *
 * Everything selector matching needs is here and nothing else is: no child
 * list, no parent pointer, no document — relationships are the host's answer
 * through the lookup callback, which keeps RoboCSS from ever needing the tree.
 */
typedef struct robo_css_element_view {
    /** The element this view describes. Must not be 0 for a usable view. */
    robo_css_element_ref_t ref;
    /** Local (tag) name, as the DOM stores it. */
    robo_css_slice_t tag_name;
    /** The `id` attribute's value; absent when the element has none. */
    robo_css_slice_t id;
    /** Borrowed array of class slices; may be NULL when class_count is 0. */
    const robo_css_slice_t *classes;
    size_t class_count;
    /** Borrowed array of attributes; may be NULL when attribute_count is 0. */
    const robo_css_attribute_t *attributes;
    size_t attribute_count;
} robo_css_element_view_t;

/** Clear @p view to "no element, nothing known". Safe on NULL. */
void robo_css_element_view_init(robo_css_element_view_t *view);

/** Describe @p view as @p ref with a borrowed @p tag_name. */
robo_result_t robo_css_element_view_set_element(robo_css_element_view_t *view,
                                                robo_css_element_ref_t ref,
                                                robo_css_slice_t tag_name,
                                                robo_css_diag_t *diag);

/** Record the element's `id` value (may be an empty slice for `id=""`). */
robo_result_t robo_css_element_view_set_id(robo_css_element_view_t *view,
                                           robo_css_slice_t id,
                                           robo_css_diag_t *diag);

/** Record the borrowed class array. Both arguments are borrowed, never copied. */
robo_result_t robo_css_element_view_set_classes(robo_css_element_view_t *view,
                                                const robo_css_slice_t *classes,
                                                size_t class_count,
                                                robo_css_diag_t *diag);

/** Record the borrowed attribute array. Borrowed, never copied. */
robo_result_t robo_css_element_view_set_attributes(
    robo_css_element_view_t *view,
    const robo_css_attribute_t *attributes,
    size_t attribute_count,
    robo_css_diag_t *diag);

/** Non-zero when @p view names an element (ref is not 0). */
int robo_css_element_view_is_set(const robo_css_element_view_t *view);

/**
 * Find @p name among the view's attributes, comparing names ASCII
 * case-insensitively (HTML attribute names are case-insensitive, and this is
 * the only comparison the model can make without knowing the document
 * language).
 * @return 1 and writes the value (which may be empty) to @p out_value when
 *         found; 0 otherwise, leaving @p out_value alone.
 */
int robo_css_element_view_find_attribute(const robo_css_element_view_t *view,
                                        robo_css_slice_t name,
                                        robo_css_slice_t *out_value);

/** Non-zero when @p view has exactly the class @p name (byte-exact). */
int robo_css_element_view_has_class(const robo_css_element_view_t *view,
                                    robo_css_slice_t name);

/** Non-zero when @p view's id equals @p id (byte-exact). */
int robo_css_element_view_id_equals(const robo_css_element_view_t *view,
                                    robo_css_slice_t id);

/* ------------------------------------------------------------------ */
/* Host callbacks                                                      */
/* ------------------------------------------------------------------ */

/**
 * Answer "give me the view of this element".
 *
 * The host fills @p out_view for @p element and returns ROBO_OK. Returning
 * ROBO_NOT_FOUND is the honest answer for an element that is gone (the DOM
 * mutated under the resolution), and is how a future matcher learns to stop
 * rather than to guess.
 *
 * @return ROBO_OK / ROBO_NOT_FOUND / any failure the host wants to report.
 */
typedef robo_result_t (*robo_css_element_lookup_fn)(
    void *userdata,
    robo_css_element_ref_t element,
    robo_css_element_view_t *out_view);

/**
 * Answer "give me this element's `style` attribute text".
 *
 * This is the whole inline-style boundary: the DOM keeps the attribute, and the
 * host hands RoboCSS its *text* when asked. Returning ROBO_NOT_FOUND means the
 * element has no `style` attribute — which is different from returning ROBO_OK
 * with an empty slice, and the difference matters because an empty `style=""`
 * is a declaration list with no declarations.
 */
typedef robo_result_t (*robo_css_inline_style_fn)(
    void *userdata,
    robo_css_element_ref_t element,
    robo_css_slice_t *out_text);

/*
 * The relation half of the seam (9.4).
 *
 * A view answers "what is this element"; a relation answers "which element is
 * next to it". Matching a combinator — `div > p`, `.a + .b`, `#x .y` — is the
 * only thing that needs the second question, and it needs exactly three
 * answers: the parent, the element before, and the element after.
 *
 * Returning ROBO_CSS_ELEMENT_REF_NONE is how a host says "there is none", and
 * that answer is load-bearing rather than an edge case: it is what `:root`, the
 * document element, makes true; it is what `:first-child` and `:last-child`
 * are; and it is how a walk up the ancestor chain terminates. A host therefore
 * must not invent a sentinel element for "the top" — the absence *is* the fact.
 *
 * Element siblings, not node siblings: a host whose tree stores text and
 * comments beside elements answers with the nearest *element*, because
 * `:first-child` and `+` count elements. Filtering is the host's job precisely
 * because RoboCSS cannot see the nodes it would have to filter.
 *
 * All three are optional. A host that supplies only a view can still answer
 * type, universal, class, id and attribute selectors — those need one element —
 * and the matcher reports the combinators and structural pseudo-classes it
 * cannot evaluate rather than guessing at them.
 */
typedef robo_css_element_ref_t (*robo_css_element_parent_fn)(
    void *userdata,
    robo_css_element_ref_t element);

typedef robo_css_element_ref_t (*robo_css_element_previous_sibling_fn)(
    void *userdata,
    robo_css_element_ref_t element);

typedef robo_css_element_ref_t (*robo_css_element_next_sibling_fn)(
    void *userdata,
    robo_css_element_ref_t element);

/**
 * Everything RoboCSS may ask a host about one element.
 *
 * One `userdata` for all four callbacks, because they come from one host: a
 * host is a document, a document is one object, and four contexts to keep in
 * step would be four chances to get it wrong. `view` is the only required
 * member.
 */
typedef struct robo_css_element_host {
    void *userdata;
    robo_css_element_lookup_fn view;
    robo_css_element_parent_fn parent;
    robo_css_element_previous_sibling_fn previous_sibling;
    robo_css_element_next_sibling_fn next_sibling;
} robo_css_element_host_t;

/** Clear @p host to "no callbacks". Safe on NULL. */
void robo_css_element_host_init(robo_css_element_host_t *host);

/**
 * Non-zero when @p host can answer for an element (its `view` is set).
 * Says nothing about the relations: a usable host may still be unable to answer
 * a combinator, and the matcher reports that per selector rather than here.
 */
int robo_css_element_host_is_usable(const robo_css_element_host_t *host);

/* ------------------------------------------------------------------ */
/* Style context                                                       */
/* ------------------------------------------------------------------ */

/** An opaque, caller-owned assembly of everything a resolution needs. */
typedef struct robo_css_style_context robo_css_style_context_t;

/**
 * Forward declaration of what a collection produces (RoboCSSCascade.h).
 *
 * The definition lives in that header, and this one does not include it, so the
 * dependency between the two runs one way: the cascade model knows about style
 * contexts (it has to match through a host), and the style context only knows
 * that a cascade *type* exists. A caller that collects one includes both.
 */
typedef struct robo_css_cascade robo_css_cascade_t;

/**
 * Forward declaration of the property classification registry (9.7).
 *
 * Opaque here for the same reason: the inheritance phase owns what is in one,
 * and a context only borrows it so that a resolution can ask whether a property
 * inherits by default and what its initial value is.
 */
typedef struct robo_css_property_registry robo_css_property_registry_t;

/**
 * Forward declaration of what inheritance produces (RoboCSSInherit.h).
 *
 * The same one-way dependency as the cascade above, for the same reason: this
 * header owns the *context* and the doors reached through it, and the model a
 * door produces lives in the header named after it. A caller that resolves an
 * inherited style includes both.
 */
typedef struct robo_css_inherited_style robo_css_inherited_style_t;

/**
 * Create an empty context: no sheets, no property table, no callbacks.
 * @return ROBO_OK; ROBO_INVALID_ARGUMENT for a NULL output; ROBO_OUT_OF_MEMORY.
 */
robo_result_t robo_css_style_context_create(robo_css_style_context_t **out_context,
                                            robo_css_diag_t *diag);

/**
 * Destroy @p context. Referenced stylesheets, the property table and every
 * callback userdata belong to the caller and are not touched. Safe on NULL.
 */
void robo_css_style_context_destroy(robo_css_style_context_t *context);

/**
 * Add a stylesheet to @p origin's ordered list.
 *
 * The context borrows the id; it never destroys the stylesheet. Insertion order
 * is preserved because a cascade needs the cascade order, and nothing here
 * reorders.
 * @return ROBO_OK; ROBO_INVALID_ARGUMENT for a NULL context, a non-stylesheet
 *         origin (NONE or INLINE) or a NONE id; ROBO_NOT_FOUND for an id that
 *         does not resolve to a live stylesheet (a context that accepted a dead
 *         id would fail later, during a resolution, where the reason is no
 *         longer visible); ROBO_OUT_OF_MEMORY.
 */
robo_result_t robo_css_style_context_add_sheet(robo_css_style_context_t *context,
                                              robo_css_origin_t origin,
                                              robo_css_stylesheet_id_t sheet,
                                              robo_css_diag_t *diag);

/** Number of stylesheets in @p origin's list (0 for a NULL context or an
 *  origin no sheet can be added to). */
size_t robo_css_style_context_sheet_count(const robo_css_style_context_t *context,
                                         robo_css_origin_t origin);

/** Borrowed id at @p index in @p origin's list; NONE when out of range. */
robo_css_stylesheet_id_t robo_css_style_context_sheet(
    const robo_css_style_context_t *context,
    robo_css_origin_t origin,
    size_t index);

/** Remove every sheet from @p origin's list. Safe on NULL. */
void robo_css_style_context_clear_origin(robo_css_style_context_t *context,
                                        robo_css_origin_t origin);

/** Remove every sheet from every origin. Safe on NULL. */
void robo_css_style_context_clear(robo_css_style_context_t *context);

/** Number of stylesheets across every origin (0 for NULL). */
size_t robo_css_style_context_total_sheet_count(
    const robo_css_style_context_t *context);

/**
 * Borrow a property table. The table must outlive the context's use of it;
 * NULL is accepted and means "no property identities are available", which
 * makes every declaration's property resolve to NONE.
 */
robo_result_t robo_css_style_context_set_property_table(
    robo_css_style_context_t *context,
    const robo_css_property_table_t *table,
    robo_css_diag_t *diag);

/** Borrowed property table, or NULL when none was set. */
const robo_css_property_table_t *robo_css_style_context_property_table(
    const robo_css_style_context_t *context);

/**
 * Borrow a property classification registry (9.7).
 *
 * The registry answers the two questions inheritance asks about a property it
 * has already identified — does it inherit by default, and what is its initial
 * value — and it is borrowed here for the same reason the property table is:
 * which properties exist is *policy*, and policy belongs to the caller. NULL is
 * accepted and means "no property is classified", which makes every property
 * fall back to the table's flags and then to the default of "does not inherit".
 *
 * The registry must outlive the context's use of it.
 */
robo_result_t robo_css_style_context_set_property_registry(
    robo_css_style_context_t *context,
    const robo_css_property_registry_t *registry,
    robo_css_diag_t *diag);

/** Borrowed property registry, or NULL when none was set. */
const robo_css_property_registry_t *robo_css_style_context_property_registry(
    const robo_css_style_context_t *context);

/**
 * Install the element-lookup callback (NULL clears it).
 *
 * A convenience over robo_css_style_context_set_element_host for the host that
 * has only a view to give: it sets the view callback and the shared userdata
 * and leaves the relations cleared, exactly as if a host with only `view` filled
 * in had been installed. The two setters write the same storage, so whichever is
 * called last decides.
 */
robo_result_t robo_css_style_context_set_element_lookup(
    robo_css_style_context_t *context,
    robo_css_element_lookup_fn lookup,
    void *userdata,
    robo_css_diag_t *diag);

/** Borrowed element-lookup callback, or NULL when none was installed. */
robo_css_element_lookup_fn robo_css_style_context_element_lookup(
    const robo_css_style_context_t *context);

/**
 * Install the whole element host. NULL clears every callback.
 * The host is copied; its userdata stays owned by the caller.
 * @return ROBO_OK; ROBO_INVALID_ARGUMENT for a NULL context, or for a host whose
 *         `view` is NULL but which supplies a relation (a host that can say
 *         where an element sits but not what it is cannot be used for
 *         anything).
 */
robo_result_t robo_css_style_context_set_element_host(
    robo_css_style_context_t *context,
    const robo_css_element_host_t *host,
    robo_css_diag_t *diag);

/**
 * The installed host, by value; every callback NULL when none was installed.
 * Returned by value rather than by pointer so that a caller can hand it straight
 * to the matcher without a lifetime question.
 */
robo_css_element_host_t robo_css_style_context_element_host(
    const robo_css_style_context_t *context);

/** Install the inline-style callback (NULL clears it). */
robo_result_t robo_css_style_context_set_inline_style(
    robo_css_style_context_t *context,
    robo_css_inline_style_fn provider,
    void *userdata,
    robo_css_diag_t *diag);

/** Borrowed inline-style callback, or NULL when none was installed. */
robo_css_inline_style_fn robo_css_style_context_inline_style(
    const robo_css_style_context_t *context);

/**
 * The userdata that goes with the inline-style callback, or NULL when none was
 * installed.
 *
 * A callback without its userdata is not callable, and the cascade has to call
 * this one itself — asking for the `style` attribute's text, parsing it and
 * collecting it are all RoboCSS's job (9.1.10) rather than the host's. The two
 * setters write the two fields together, so this cannot disagree with
 * robo_css_style_context_inline_style.
 */
void *robo_css_style_context_inline_userdata(const robo_css_style_context_t *context);

/** Non-zero when @p context has a RUNTIME-origin stylesheet — i.e. this build
 *  is using a runtime default stylesheet. Always 0 in Phase 9.1, which ships
 *  none. */
int robo_css_style_context_has_runtime_sheet(
    const robo_css_style_context_t *context);

/* ------------------------------------------------------------------ */
/* Resolution                                                          */
/* ------------------------------------------------------------------ */

/**
 * Collect, in cascade order, everything that applies to @p element.
 *
 * A real collection as of 9.5, and the door through which the whole cascade
 * arrives. It consumes every seam above: the origin lists decide the order the
 * sheets are walked in, the element host answers what the element is and how it
 * relates (9.4), the property table turns declaration names into identities, and
 * the inline callback hands over the `style` attribute's text, which is then
 * parsed by the same grammar a rule's block uses.
 *
 * What comes back is a robo_css_cascade_t (RoboCSSCascade.h): the candidates in
 * cascade order, plus a count of everything the build could not use — an
 * undecided selector, an at-rule block nothing evaluates, a sheet that was
 * destroyed, a rule whose prelude was never parsed as selectors. Those counts
 * are the whole reason a collection is allowed to succeed while being
 * incomplete: a cascade that dropped an undecided selector silently would apply
 * the wrong rule and look like it had answered.
 *
 * @p out is initialised first, so a caller that ignores the result code still
 * owns something disposable. @p out_error is always filled: the first CSS
 * problem met (a malformed `style` attribute, say), or a record of kind NONE
 * when there was none. A recovered problem is not a failed call — that is the
 * 9.3 rule and it holds here too.
 *
 * @return ROBO_OK whenever a cascade was produced, including an incomplete one;
 *         ROBO_INVALID_ARGUMENT for a NULL context, output or element;
 *         ROBO_OUT_OF_MEMORY when no answer could be produced at all, in which
 *         case @p out is left empty rather than half-filled.
 */
robo_result_t robo_css_style_context_match_declarations(
    const robo_css_style_context_t *context,
    robo_css_element_ref_t element,
    robo_css_cascade_t *out,
    robo_css_error_t *out_error,
    robo_css_diag_t *diag);

/**
 * Resolve @p element's inherited style — the whole chain, from the root down.
 *
 * The door 9.7 adds, and the one a caller normally wants. It walks from
 * @p element up through the host's `parent` callback to the root, then resolves
 * **top down**: each level gets its own cascade collected and its own style
 * resolved, with the level above supplied as the parent. Only the element's own
 * style comes back; the intermediate ones are disposed as the walk descends.
 *
 * Walking up first and resolving down is what makes "the immediate resolved
 * parent" literal rather than approximate: `div { color: red } section { color:
 * blue }` gives a `<p>` inside `<section>` blue, because `<section>`'s resolved
 * style is the parent, not the nearest ancestor that happened to declare
 * something. A child never reaches past the element directly above it.
 *
 * The walk is bounded by ROBO_CSS_INHERIT_MAX_DEPTH, and hitting the bound is
 * *reported*: an ancestor chain longer than that, or a DOM that reports a cycle
 * among parents, produces a style whose `unsupported` is set rather than a
 * silent guess about where the top was.
 *
 * A context with no element host, or one whose `parent` callback is NULL, is
 * still usable: the walk simply finds no ancestor and the element is resolved
 * as a root. That is not a failure — a host that cannot answer "what is your
 * parent" has told this module the one thing it needs to know.
 *
 * @return ROBO_OK whenever a style was produced, including an incomplete one;
 *         ROBO_INVALID_ARGUMENT for a NULL context, output or element;
 *         ROBO_OUT_OF_MEMORY when no answer could be produced at all, in which
 *         case @p out is left empty rather than half-filled.
 */
robo_result_t robo_css_style_context_resolve_inherited_style(
    const robo_css_style_context_t *context,
    robo_css_element_ref_t element,
    robo_css_inherited_style_t *out,
    robo_css_error_t *out_error,
    robo_css_diag_t *diag);

/**
 * Compute @p element's style.
 *
 * The door 9.8 adds, and the last one in the chain: selector matching (9.4),
 * specificity (9.6), the cascade (9.5) and inheritance (9.7) all feed it, and it
 * hands back a robo_css_computed_style_t (RoboCSSComputed.h) whose entries carry
 * both the specified value and a typed computed value.
 *
 * It computes the whole ancestor chain, not just the element. That is what makes
 * an inherited value *the parent's computed value* rather than a recomputation
 * of something that looked like it: each level is resolved and then computed
 * top-down, so the level below inherits a value that has already been through
 * the same normalisation. The intermediate styles are the walk's own and are
 * disposed as it descends; only @p element's comes back.
 *
 * A value that cannot be computed *yet* is not a failure and not a guess. A
 * `calc()` needs a containing block, a `var()` needs a custom property the
 * author may never have declared, and `currentColor` needs the element's own
 * `color`; each is carried with its tokens intact and counted through
 * robo_css_computed_style_incomplete_count. A value that means nothing for its
 * property at all — `color: nonsense` — is marked invalid rather than dropped,
 * so the caller can see that the declaration was there and was wrong.
 *
 * @p out_style is cleared first and is left empty when no answer could be
 * produced at all. @p out_error is always filled: the first CSS problem met on
 * the chain, or a record of kind NONE when there was none.
 *
 * @return ROBO_OK whenever a style was produced, including an incomplete one;
 *         ROBO_INVALID_ARGUMENT for a NULL context or output, or an element of
 *         NONE; ROBO_OUT_OF_MEMORY when no answer could be produced at all.
 */
robo_result_t robo_css_style_context_compute_style(
    const robo_css_style_context_t *context,
    robo_css_element_ref_t element,
    robo_css_computed_style_t *out_style,
    robo_css_error_t *out_error,
    robo_css_diag_t *diag);

/**
 * The computed value of one property, with the property's own initial value as
 * the fallback.
 *
 * A computed style holds the properties *this element has an answer for*, which
 * for a non-inherited property nothing declared means: no entry. That is the
 * right shape for the container and a poor one for a question, because
 * "what is this element's `width`" has an answer even when the element never
 * mentions width — the property's initial value. This door supplies it.
 *
 * @p style may be NULL, meaning "nothing was computed for this element", and
 * every property then answers with its initial value; that is the difference
 * between "the element declares nothing" and "there is no element".
 *
 * @param out              receives the computed value (a copy / a fresh
 *                         computation); untouched when the call fails.
 * @param out_from_initial non-NULL; receives 1 when the answer is the
 *                         property's initial value and 0 when it came from
 *                         @p style. This is the fact the caller usually wants:
 *                         a non-inherited property nothing declared is *at* its
 *                         initial value, and it did not get there from a parent.
 * @return ROBO_OK; ROBO_INVALID_ARGUMENT for a NULL context or output, or an
 *         absent name; ROBO_NOT_FOUND when neither @p style nor this build has
 *         an answer — an unknown property, or a custom property (which CSS gives
 *         no initial value at all).
 */
robo_result_t robo_css_style_context_query_computed_value(
    const robo_css_style_context_t *context,
    const robo_css_computed_style_t *style,
    robo_css_slice_t name,
    robo_css_computed_value_t *out,
    int *out_from_initial,
    robo_css_diag_t *diag);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_CSS_STYLE_H */
