/*
 * RoboCSS property identity (Phase 9.1).
 *
 * The whole point of this module is to make property names stop being strings
 * by the time anything hot touches them:
 *
 *   declaration text        "padding-left"
 *         │
 *         ▼  robo_css_property_table_lookup
 *   property identity        robo_css_property_id_t   (one integer)
 *         │
 *         ▼  9.3 parses, 9.8 computes, 9.9+ own the families
 *   parsed value             robo_css_value_t
 *         │
 *         ▼
 *   computed value
 *
 * Phase 9.1 defines the identity mechanism and nothing else. It registers no
 * built-in properties: the property *database* is 9.9 onward, and a table with
 * zero entries is the honest state of a build that has not implemented any
 * property semantics yet. A caller can register what it needs (tests do), and
 * a declaration whose name is not registered keeps
 * ROBO_CSS_PROPERTY_ID_NONE — "unrecognised", which is exactly what
 * `robo_css_property_table_lookup` returning NONE says.
 *
 * The table is an explicit object, not module state. Which properties exist is
 * *policy*, and policy is what the "no hidden global state" rule in
 * DESIGN_RULES.md is about: a caller creates a table, fills it, and hands it to
 * whatever needs to resolve names. Two tables can disagree, and that is a
 * feature — a test's table is not the application's.
 *
 * A property name is copied into the table (a table outlives the strings a
 * caller registered it with) and compared ASCII case-insensitively, because
 * CSS compares property names that way. Nothing is lowercased on the way in:
 * the name is stored as given, so a caller reading it back sees what it wrote,
 * and normalising case stays where CSS puts it — in the tokenizer.
 *
 * Ids are per table and are never reused within a table's lifetime, so an id
 * that outlives a `reset` cannot silently name a different property. Ids from
 * one table must never be looked up in another; nothing can detect that
 * mistake, which is why the rule is stated here rather than enforced.
 *
 * Custom properties (`--x`) are not registered and not special-cased in this
 * phase; that mechanism arrives with the property families.
 */

#ifndef ROBO_CSS_PROPERTY_H
#define ROBO_CSS_PROPERTY_H

#include "RoboBase/RoboError.h"
#include "RoboCSS/RoboCSSDiagnostics.h"
#include "RoboCSS/RoboCSSTypes.h"

#ifdef __cplusplus
extern "C" {
#endif

/** A property's identity within one table. 0 means "no such property". */
typedef uint32_t robo_css_property_id_t;

#define ROBO_CSS_PROPERTY_ID_NONE ((robo_css_property_id_t)0u)

/** Properties of a property, for the phases that need them. */
typedef enum robo_css_property_flag {
    ROBO_CSS_PROPERTY_FLAG_NONE = 0,
    /** The property inherits by default (9.7 reads this). */
    ROBO_CSS_PROPERTY_FLAG_INHERITED = 1 << 0,
    /** The property is a shorthand for others (9.5/9.9 expand it). */
    ROBO_CSS_PROPERTY_FLAG_SHORTHAND = 1 << 1
} robo_css_property_flag_t;

/** One registered property, as read back from the table. */
typedef struct robo_css_property_info {
    robo_css_property_id_t id;
    /** Borrowed, NUL-terminated name owned by the table. */
    const char *name;
    /** Length of @p name in bytes. */
    size_t name_length;
    /** Bitmask of robo_css_property_flag_t. */
    uint32_t flags;
} robo_css_property_info_t;

/** Non-zero when @p info describes a registered property. NULL → 0. */
int robo_css_property_info_is_set(const robo_css_property_info_t *info);

/** Non-zero when @p flags carries ROBO_CSS_PROPERTY_FLAG_INHERITED. */
int robo_css_property_flags_inherited(uint32_t flags);

/** Non-zero when @p flags carries ROBO_CSS_PROPERTY_FLAG_SHORTHAND. */
int robo_css_property_flags_shorthand(uint32_t flags);

/** An opaque, caller-owned table of property identities. */
typedef struct robo_css_property_table robo_css_property_table_t;

/**
 * Create an empty table.
 * @return ROBO_OK; ROBO_INVALID_ARGUMENT for a NULL output; ROBO_OUT_OF_MEMORY
 *         when the table cannot be allocated.
 * The caller owns the table until robo_css_property_table_destroy.
 */
robo_result_t robo_css_property_table_create(robo_css_property_table_t **out_table,
                                            robo_css_diag_t *diag);

/** Destroy @p table and every name it copied. Safe on NULL. */
void robo_css_property_table_destroy(robo_css_property_table_t *table);

/**
 * Register @p name with @p flags.
 *
 * @p name must be non-empty, at most ROBO_CSS_PROPERTY_NAME_MAX bytes, and must
 * not contain a NUL, a whitespace character, `:` or `.` — a name that cannot be
 * a CSS property name is rejected rather than stored. Registering a name that
 * is already present (compared ASCII case-insensitively) is not an error in
 * the caller's world: it returns ROBO_ALREADY_EXISTS and writes the *existing*
 * id, leaving the stored flags untouched.
 *
 * @return ROBO_OK; ROBO_ALREADY_EXISTS (with @p out_id filled);
 *         ROBO_INVALID_ARGUMENT for a NULL table/output or an unusable name;
 *         ROBO_OUT_OF_MEMORY when the table cannot grow or the name cannot be
 *         copied (nothing is registered in that case).
 */
robo_result_t robo_css_property_table_register(robo_css_property_table_t *table,
                                               const char *name,
                                               uint32_t flags,
                                               robo_css_property_id_t *out_id,
                                               robo_css_diag_t *diag);

/**
 * Look up @p name (ASCII case-insensitive, NUL-terminated).
 * @return the property id, or ROBO_CSS_PROPERTY_ID_NONE when the table has no
 *         such property (including for a NULL table or a NULL/empty name).
 */
robo_css_property_id_t robo_css_property_table_lookup(
    const robo_css_property_table_t *table,
    const char *name);

/**
 * Look up a borrowed slice as a property name.
 * @return the property id, or NONE. A slice holding an embedded NUL can never
 *         match, because a property name cannot contain one.
 */
robo_css_property_id_t robo_css_property_table_lookup_slice(
    const robo_css_property_table_t *table,
    robo_css_slice_t name);

/** Non-zero when @p id names a property in @p table. */
int robo_css_property_table_is_registered(const robo_css_property_table_t *table,
                                          robo_css_property_id_t id);

/**
 * Read @p id back out of @p table.
 * @return ROBO_OK and fills @p out_info; ROBO_NOT_FOUND when the id is not in
 *         the table; ROBO_INVALID_ARGUMENT for a NULL table/output.
 */
robo_result_t robo_css_property_table_info(const robo_css_property_table_t *table,
                                           robo_css_property_id_t id,
                                           robo_css_property_info_t *out_info,
                                           robo_css_diag_t *diag);

/**
 * Read the property registered at @p index in table order.
 * @return ROBO_OK; ROBO_NOT_FOUND when @p index is out of range;
 *         ROBO_INVALID_ARGUMENT for a NULL table/output.
 */
robo_result_t robo_css_property_table_at(const robo_css_property_table_t *table,
                                         size_t index,
                                         robo_css_property_info_t *out_info,
                                         robo_css_diag_t *diag);

/** Number of registered properties (0 for NULL). */
size_t robo_css_property_table_count(const robo_css_property_table_t *table);

/**
 * Remove every registration and free the names. The id cursor is *not* reset,
 * so ids issued before the clear can never name a property registered after it.
 * Safe on NULL.
 */
void robo_css_property_table_reset(robo_css_property_table_t *table);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_CSS_PROPERTY_H */
