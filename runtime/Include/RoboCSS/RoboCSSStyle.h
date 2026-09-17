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
 *   answers the element-lookup callback               walks selectors later (9.4)
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
 *                             attributes) arrives in the view
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
 * Resolution itself is *declared and not implemented*: matching declarations
 * (9.4/9.5) and computing a style (9.8) both answer ROBO_NOT_SUPPORTED and say
 * so in their error record. They exist now because the contract a host has to
 * build against — what it must be able to answer, what comes back, who owns it
 * — is the thing Phase 9.1 is for.
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

/* ------------------------------------------------------------------ */
/* Style context                                                       */
/* ------------------------------------------------------------------ */

/** An opaque, caller-owned assembly of everything a resolution needs. */
typedef struct robo_css_style_context robo_css_style_context_t;

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

/** Install the element-lookup callback (NULL clears it). */
robo_result_t robo_css_style_context_set_element_lookup(
    robo_css_style_context_t *context,
    robo_css_element_lookup_fn lookup,
    void *userdata,
    robo_css_diag_t *diag);

/** Borrowed element-lookup callback, or NULL when none was installed. */
robo_css_element_lookup_fn robo_css_style_context_element_lookup(
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

/** Non-zero when @p context has a RUNTIME-origin stylesheet — i.e. this build
 *  is using a runtime default stylesheet. Always 0 in Phase 9.1, which ships
 *  none. */
int robo_css_style_context_has_runtime_sheet(
    const robo_css_style_context_t *context);

/* ------------------------------------------------------------------ */
/* Resolution boundaries (declared; not implemented in 9.1)            */
/* ------------------------------------------------------------------ */

/**
 * Collect the declarations that apply to @p element.
 *
 * This is where 9.4 (matching) and 9.5 (the cascade) arrive, and where the
 * context's origin lists, element lookup and inline callback are consumed.
 *
 * Phase 9.1 does not match anything. This validates its arguments, records
 * ROBO_CSS_ERROR_KIND_UNSUPPORTED (when @p out_error is non-NULL, which it
 * fills either way), and returns ROBO_NOT_SUPPORTED; @p out is initialised and
 * left empty.
 *
 * @return ROBO_NOT_SUPPORTED in this build; ROBO_INVALID_ARGUMENT for a NULL
 *         context or output.
 */
robo_result_t robo_css_style_context_match_declarations(
    const robo_css_style_context_t *context,
    robo_css_element_ref_t element,
    robo_css_declaration_list_t *out,
    robo_css_error_t *out_error,
    robo_css_diag_t *diag);

/**
 * Compute @p element's style.
 *
 * This is 9.8, plus everything it depends on: 9.5's cascade, 9.6's specificity,
 * 9.7's inheritance, 9.9's colours and 9.10's units.
 *
 * Phase 9.1 computes nothing. This validates its arguments, records
 * ROBO_CSS_ERROR_KIND_UNSUPPORTED (@p out_error is filled either way), and
 * returns ROBO_NOT_SUPPORTED; @p out_style is left empty.
 *
 * @return ROBO_NOT_SUPPORTED in this build; ROBO_INVALID_ARGUMENT for a NULL
 *         context or output.
 */
robo_result_t robo_css_style_context_compute_style(
    const robo_css_style_context_t *context,
    robo_css_element_ref_t element,
    robo_css_computed_style_t *out_style,
    robo_css_error_t *out_error,
    robo_css_diag_t *diag);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_CSS_STYLE_H */
