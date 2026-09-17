/*
 * RoboCSS declaration model (Phase 9.1).
 *
 * A declaration is the atom CSS is made of:
 *
 *   padding-left : 12px !important ;
 *   └──── name ─┘ └─ value ─┘ └─ flag ┘
 *
 *   name         the property name as written, borrowed from the source
 *   property     its identity in a table, when the table recognised it
 *   value        the component-value tree (RoboCSSValue.h)
 *   important    the author's request, which the cascade weighs in 9.5
 *   range        where it came from, for diagnostics
 *   state        parsed, or recognised as malformed and skipped
 *
 * The name stays a borrowed slice *and* gains an id rather than being replaced
 * by one, because the two answers are different questions: the slice is what
 * the author wrote (needed for diagnostics, for custom properties, and for
 * anything the table has never heard of), and the id is what the rest of the
 * pipeline switches on.
 *
 * `important` is recorded and never interpreted here. Which declaration beats
 * which is the cascade's decision (9.5), and a model that "helpfully" ordered
 * declarations would be implementing 9.5 by accident.
 *
 * Nothing in this phase decides whether a *value* is valid for a property: a
 * declaration whose name and syntax are fine is PARSED even if the property is
 * unknown or the value is nonsense for it. Semantic validity is 9.3's and
 * 9.9's, and conflating it with syntax here would make the two impossible to
 * tell apart later.
 */

#ifndef ROBO_CSS_DECLARATION_H
#define ROBO_CSS_DECLARATION_H

#include "RoboBase/RoboError.h"
#include "RoboCSS/RoboCSSDiagnostics.h"
#include "RoboCSS/RoboCSSError.h"
#include "RoboCSS/RoboCSSProperty.h"
#include "RoboCSS/RoboCSSValue.h"

#ifdef __cplusplus
extern "C" {
#endif

/** What the model knows about a declaration. */
typedef enum robo_css_declaration_state {
    /** No declaration: a cleared record. */
    ROBO_CSS_DECLARATION_STATE_NONE = 0,
    /** Syntactically a declaration: a name, a value, however the value is spelled. */
    ROBO_CSS_DECLARATION_STATE_PARSED = 1,
    /** Recognisably malformed (no colon, empty name); skipped, and recorded. */
    ROBO_CSS_DECLARATION_STATE_INVALID = 2
} robo_css_declaration_state_t;

/** Number of defined declaration states. */
#define ROBO_CSS_DECLARATION_STATE_COUNT 3u

/** Non-zero if @p state is a defined declaration state enumerator. */
int robo_css_declaration_state_is_known(robo_css_declaration_state_t state);

/** Human-readable declaration state name; never NULL. */
const char *robo_css_declaration_state_name(robo_css_declaration_state_t state);

/** One declaration. Owns `value`; every slice borrows from the source. */
typedef struct robo_css_declaration {
    robo_css_declaration_state_t state;
    /** Property name as written, borrowed from the source. */
    robo_css_slice_t name;
    /** Property identity, or ROBO_CSS_PROPERTY_ID_NONE when unrecognised. */
    robo_css_property_id_t property;
    /** Non-zero when the declaration carried `!important`. */
    int important;
    /** Extent of the whole declaration, excluding the terminating `;`. */
    robo_css_range_t range;
    /** Extent of the name alone. */
    robo_css_range_t name_range;
    /** Extent of the value alone. */
    robo_css_range_t value_range;
    /** Owns its children; disposable. */
    robo_css_value_t value;
} robo_css_declaration_t;

/** Clear @p declaration to STATE_NONE. Safe on NULL. */
void robo_css_declaration_init(robo_css_declaration_t *declaration);

/**
 * Dispose @p declaration: dispose its value tree and reset to STATE_NONE.
 * Idempotent and safe on NULL.
 */
void robo_css_declaration_dispose(robo_css_declaration_t *declaration);

/**
 * Begin a declaration named @p name. @p state must be PARSED or INVALID;
 * a NULL declaration or a state of NONE is rejected.
 * @p name may be empty only when @p state is INVALID, because an empty name is
 * exactly what "malformed" looks like.
 * @return ROBO_OK; ROBO_INVALID_ARGUMENT otherwise. The declaration is disposed
 *         first, so a reused record never leaks.
 */
robo_result_t robo_css_declaration_make(robo_css_declaration_t *declaration,
                                       robo_css_slice_t name,
                                       robo_css_declaration_state_t state,
                                       robo_css_diag_t *diag);

/** Set the importance flag. NULL @p declaration → ROBO_INVALID_ARGUMENT. */
robo_result_t robo_css_declaration_set_important(robo_css_declaration_t *declaration,
                                                int important,
                                                robo_css_diag_t *diag);

/**
 * Move @p value into @p declaration, disposing any value it held. @p value is
 * reset to NONE. A declaration in state NONE is rejected.
 */
robo_result_t robo_css_declaration_set_value(robo_css_declaration_t *declaration,
                                            robo_css_value_t *value,
                                            robo_css_diag_t *diag);

/** Record the source range of @p declaration and its name/value parts. */
robo_result_t robo_css_declaration_set_ranges(robo_css_declaration_t *declaration,
                                             robo_css_range_t range,
                                             robo_css_range_t name_range,
                                             robo_css_range_t value_range,
                                             robo_css_diag_t *diag);

/**
 * Resolve @p declaration's name against @p table and store the identity.
 * Returns the resolved id, or ROBO_CSS_PROPERTY_ID_NONE when the table has no
 * such property — which is a *result*, not a failure, and leaves the
 * declaration untouched. A NULL declaration or table → NONE.
 */
robo_css_property_id_t robo_css_declaration_resolve_property(
    robo_css_declaration_t *declaration,
    const robo_css_property_table_t *table);

/** Non-zero when @p declaration holds a value tree of any kind. NULL → 0. */
int robo_css_declaration_has_value(const robo_css_declaration_t *declaration);

/**
 * Non-zero when @p declaration is usable: PARSED, with a non-empty name.
 * An INVALID declaration is a record of a problem, not a usable declaration.
 */
int robo_css_declaration_is_usable(const robo_css_declaration_t *declaration);

/** Borrowed name slice; absent for NULL. */
robo_css_slice_t robo_css_declaration_name(const robo_css_declaration_t *declaration);

/** Borrowed value tree; NULL pointer for NULL declaration (see the header). */
const robo_css_value_t *robo_css_declaration_value(
    const robo_css_declaration_t *declaration);

/* ------------------------------------------------------------------ */
/* Declaration lists                                                   */
/* ------------------------------------------------------------------ */

/** An owning, ordered list of declarations. */
typedef struct robo_css_declaration_list {
    robo_css_declaration_t *items; /* owned; NULL when count == 0 */
    size_t count;
    size_t capacity;
} robo_css_declaration_list_t;

/** Clear @p list to empty. Safe on NULL. */
void robo_css_declaration_list_init(robo_css_declaration_list_t *list);

/** Dispose every declaration and free the array. Idempotent; safe on NULL. */
void robo_css_declaration_list_dispose(robo_css_declaration_list_t *list);

/**
 * Append @p declaration by moving it in; @p declaration is left STATE_NONE.
 * @return ROBO_OK; ROBO_INVALID_ARGUMENT for a NULL argument;
 *         ROBO_OUT_OF_MEMORY when the array cannot grow (the declaration is
 *         then untouched and still owns its value).
 */
robo_result_t robo_css_declaration_list_append(robo_css_declaration_list_t *list,
                                               robo_css_declaration_t *declaration,
                                               robo_css_diag_t *diag);

/** Number of declarations (0 for NULL). */
size_t robo_css_declaration_list_count(const robo_css_declaration_list_t *list);

/** Borrowed declaration at @p index, or NULL when out of range. */
const robo_css_declaration_t *robo_css_declaration_list_at(
    const robo_css_declaration_list_t *list,
    size_t index);

/** Non-zero when @p list holds no declarations (also for NULL). */
int robo_css_declaration_list_is_empty(const robo_css_declaration_list_t *list);

/** Remove every declaration. Safe on NULL. */
void robo_css_declaration_list_clear(robo_css_declaration_list_t *list);

/**
 * Find the first declaration whose property is @p property.
 * @return 1 and writes @p out_index when found; 0 otherwise (a NULL list,
 *         ROBO_CSS_PROPERTY_ID_NONE, or no match).
 */
int robo_css_declaration_list_find_first(const robo_css_declaration_list_t *list,
                                        robo_css_property_id_t property,
                                        size_t *out_index);

/**
 * Find the *last* declaration whose property is @p property. Together with
 * find_first this lets a caller see both ends of a repeated property; which
 * one wins is the cascade's decision (9.5), not this module's.
 * @return 1 and writes @p out_index when found; 0 otherwise.
 */
int robo_css_declaration_list_find_last(const robo_css_declaration_list_t *list,
                                       robo_css_property_id_t property,
                                       size_t *out_index);

/** Number of declarations whose property is @p property. */
size_t robo_css_declaration_list_count_of(const robo_css_declaration_list_t *list,
                                         robo_css_property_id_t property);

/* ------------------------------------------------------------------ */
/* Inline-style boundary (9.1.10)                                      */
/* ------------------------------------------------------------------ */

/**
 * Parse the declarations in @p text into @p out_list.
 *
 * This is the *whole* inline-style boundary: the DOM keeps the `style`
 * attribute, RoboCSS is handed its text, and a declaration list comes back.
 * RoboCSS never stores the attribute, never reads it back, and owns no second
 * attribute store — the caller passes the bytes, and the caller's buffer stays
 * its own.
 *
 * Phase 9.1 has no parser: this returns ROBO_NOT_SUPPORTED, records
 * ROBO_CSS_ERROR_KIND_UNSUPPORTED in @p out_error, and leaves @p out_list
 * empty (it is initialised, never partially populated). It exists now so the
 * boundary, its ownership and its error contract are fixed before the parser
 * arrives in 9.3.
 *
 * @p origin is recorded by the caller later; passing INLINE here is what the
 * call site does, and any origin is accepted (an author-provided fragment is
 * the same text with different provenance).
 *
 * @return ROBO_NOT_SUPPORTED in this build; ROBO_INVALID_ARGUMENT for a NULL
 *         out_list or a NULL text with a non-zero length.
 */
robo_result_t robo_css_declaration_list_parse_text(const char *text,
                                                   size_t length,
                                                   robo_css_origin_t origin,
                                                   robo_css_declaration_list_t *out_list,
                                                   robo_css_error_t *out_error);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_CSS_DECLARATION_H */
