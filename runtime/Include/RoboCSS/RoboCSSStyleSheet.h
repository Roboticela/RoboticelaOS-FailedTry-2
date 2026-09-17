/*
 * RoboCSS stylesheet model (Phase 9.1).
 *
 * A stylesheet is the object a caller keeps: it carries where it came from
 * (origin, name), what it was built from (source bytes), what it contains
 * (rules), how far it has got (state) and what went wrong (last error). It
 * owns its source when the caller asked for a copy and owns its rules always.
 *
 * Stylesheets are addressed by id, not by pointer, using the same discipline
 * RoboHTML uses for documents: a module-scoped slot table, ids from a
 * monotonic cursor, and ids that are never reused, so an id that outlives its
 * stylesheet resolves to nothing instead of to somebody else's sheet. The
 * module exposes how many are live, and shutdown tears down whatever is left.
 *
 * What a stylesheet is *not* in this phase:
 *
 *   - not parsed. robo_css_stylesheet_parse exists, validates its arguments,
 *     and reports ROBO_NOT_SUPPORTED, recording that in the stylesheet's last
 *     error and leaving the state CREATED (or FAILED, with a fatal error).
 *     Nothing here pretends to read CSS.
 *   - not loaded. There is no fetch, no `@import`, no `@charset`, no encoding
 *     detection, no media evaluation. A stylesheet is created empty and rules
 *     are appended to it by whoever built them.
 *   - not attached. Which sheets apply to which document is the style
 *     context's business (RoboCSSStyle.h); a stylesheet knows nothing about
 *     the origin it is used under, only the origin recorded on it.
 *
 * Rules can be appended, read by index, removed, and cleared. Order is the
 * order they were appended, which is the order a cascade needs to see.
 */

#ifndef ROBO_CSS_STYLESHEET_H
#define ROBO_CSS_STYLESHEET_H

#include "RoboBase/RoboError.h"
#include "RoboCSS/RoboCSSDiagnostics.h"
#include "RoboCSS/RoboCSSError.h"
#include "RoboCSS/RoboCSSRule.h"
#include "RoboCSS/RoboCSSSource.h"
#include "RoboCSS/RoboCSSTypes.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Opaque stylesheet identity (stable while the stylesheet lives; 0 = none). */
typedef uint64_t robo_css_stylesheet_id_t;

#define ROBO_CSS_STYLESHEET_ID_NONE ((robo_css_stylesheet_id_t)0u)

/**
 * Create an empty stylesheet in state CREATED, of @p origin.
 * @p origin ROBO_CSS_ORIGIN_NONE is accepted — a sheet whose provenance is not
 * decided yet — and can be set later with robo_css_stylesheet_set_origin.
 * @return ROBO_OK; ROBO_INVALID_ARGUMENT for a NULL output or an unknown
 *         origin; ROBO_INIT_ERROR when the module is not initialized;
 *         ROBO_OUT_OF_MEMORY when the slot table is full.
 */
robo_result_t robo_css_stylesheet_create(robo_css_stylesheet_id_t *out_id,
                                         robo_css_origin_t origin,
                                         robo_css_diag_t *diag);

/**
 * Destroy @p id: dispose every rule and any owned source copy.
 * @return ROBO_OK; ROBO_NOT_FOUND for an id that does not resolve (including
 *         ROBO_CSS_STYLESHEET_ID_NONE, and a second destroy of the same id);
 *         ROBO_INVALID_ARGUMENT if @p id was never issued by this module.
 */
robo_result_t robo_css_stylesheet_destroy(robo_css_stylesheet_id_t id,
                                          robo_css_diag_t *diag);

/** Non-zero when @p id resolves to a live stylesheet. */
int robo_css_stylesheet_is_live(robo_css_stylesheet_id_t id);

/** Borrowed id for the stylesheet at @p index in slot order; NONE when out of
 *  range. Slot order is creation order, which is what a deterministic test
 *  needs and what a caller must not depend on for anything else. */
robo_css_stylesheet_id_t robo_css_stylesheet_id_at(size_t index);

/** Set the recorded origin. */
robo_result_t robo_css_stylesheet_set_origin(robo_css_stylesheet_id_t id,
                                             robo_css_origin_t origin,
                                             robo_css_diag_t *diag);

/** Recorded origin; NONE for an id that does not resolve. */
robo_css_origin_t robo_css_stylesheet_origin(robo_css_stylesheet_id_t id);

/** Current state; NONE for an id that does not resolve. */
robo_css_stylesheet_state_t robo_css_stylesheet_state(robo_css_stylesheet_id_t id);

/**
 * Force the state to CREATED, PARSED or FAILED.
 *
 * This exists because the parse that would set it (9.3) is not here yet, and
 * because a caller that built a stylesheet by hand has a legitimate reason to
 * say so. NONE is refused: NONE means "no stylesheet", and an id that resolves
 * always has a real state.
 * @return ROBO_OK; ROBO_NOT_FOUND; ROBO_INVALID_ARGUMENT for an unknown state.
 */
robo_result_t robo_css_stylesheet_set_state(robo_css_stylesheet_id_t id,
                                            robo_css_stylesheet_state_t state,
                                            robo_css_diag_t *diag);

/**
 * Record a diagnostic label (copied, truncated when too long, never rejected
 * for length — a label must not fail a call that would otherwise succeed).
 */
robo_result_t robo_css_stylesheet_set_name(robo_css_stylesheet_id_t id,
                                           const char *name,
                                           robo_css_diag_t *diag);

/** Borrowed label, always NUL-terminated ("" when unset); NULL when the id
 *  does not resolve. */
const char *robo_css_stylesheet_name(robo_css_stylesheet_id_t id);

/* ------------------------------------------------------------------ */
/* Source attachment                                                   */
/* ------------------------------------------------------------------ */

/**
 * Attach borrowed source bytes. The caller's buffer must stay valid for as
 * long as the stylesheet reads them (rules borrow their text from here).
 */
robo_result_t robo_css_stylesheet_set_source_memory(robo_css_stylesheet_id_t id,
                                                    const uint8_t *bytes,
                                                    size_t length,
                                                    robo_css_diag_t *diag);

/** Attach a borrowed NUL-terminated string as the source. */
robo_result_t robo_css_stylesheet_set_source_string(robo_css_stylesheet_id_t id,
                                                    const char *utf8,
                                                    robo_css_diag_t *diag);

/**
 * Attach an owned copy of @p bytes: the stylesheet allocates it and frees it
 * when the stylesheet is destroyed or the source is replaced. This is the mode
 * a caller wants when the bytes are temporary, which is the usual case for a
 * `<style>` element's text.
 */
robo_result_t robo_css_stylesheet_copy_source(robo_css_stylesheet_id_t id,
                                             const uint8_t *bytes,
                                             size_t length,
                                             robo_css_diag_t *diag);

/** Borrowed source, or NULL when the id does not resolve. */
const robo_css_source_t *robo_css_stylesheet_source(robo_css_stylesheet_id_t id);

/** Source length in bytes (0 when none). */
size_t robo_css_stylesheet_source_length(robo_css_stylesheet_id_t id);

/** Non-zero when the stylesheet owns its source bytes. */
int robo_css_stylesheet_owns_source(robo_css_stylesheet_id_t id);

/* ------------------------------------------------------------------ */
/* Rules                                                               */
/* ------------------------------------------------------------------ */

/**
 * Append @p rule by moving it in; @p rule is left KIND_NONE.
 * @return ROBO_OK; ROBO_NOT_FOUND for an id that does not resolve;
 *         ROBO_INVALID_ARGUMENT for a NULL rule or a rule kind of NONE;
 *         ROBO_OUT_OF_MEMORY when the rule list cannot grow.
 */
robo_result_t robo_css_stylesheet_append_rule(robo_css_stylesheet_id_t id,
                                              robo_css_rule_t *rule,
                                              robo_css_diag_t *diag);

/** Number of top-level rules (0 when the id does not resolve). */
size_t robo_css_stylesheet_rule_count(robo_css_stylesheet_id_t id);

/** Borrowed top-level rule at @p index, or NULL when out of range. */
const robo_css_rule_t *robo_css_stylesheet_rule(robo_css_stylesheet_id_t id,
                                                size_t index);

/** Total number of rules, at-rules' blocks included. */
size_t robo_css_stylesheet_tree_rule_count(robo_css_stylesheet_id_t id);

/** Number of style rules, at-rules' blocks included. */
size_t robo_css_stylesheet_style_rule_count(robo_css_stylesheet_id_t id);

/**
 * Remove the top-level rule at @p index, disposing it.
 * @return ROBO_OK; ROBO_NOT_FOUND for an id that does not resolve or an index
 *         out of range.
 */
robo_result_t robo_css_stylesheet_remove_rule(robo_css_stylesheet_id_t id,
                                              size_t index,
                                              robo_css_diag_t *diag);

/** Remove every rule, disposing them. Safe on an id that does not resolve. */
void robo_css_stylesheet_clear_rules(robo_css_stylesheet_id_t id);

/** Borrowed rule list, or NULL when the id does not resolve. */
const robo_css_rule_list_t *robo_css_stylesheet_rules(robo_css_stylesheet_id_t id);

/* ------------------------------------------------------------------ */
/* Parse boundary                                                      */
/* ------------------------------------------------------------------ */

/**
 * Parse the attached source into this stylesheet's rule list.
 *
 * Phase 9.1 has no parser. This validates its arguments, records
 * ROBO_CSS_ERROR_KIND_UNSUPPORTED in the stylesheet's last error (and in
 * @p out_error when non-NULL, which is filled whenever it is passed, error or
 * not), and returns ROBO_NOT_SUPPORTED. The rule list, the state and the
 * source are all left exactly as they were: a refused parse changes nothing.
 *
 * Filling @p out_error on a successful future call with kind NONE is what a
 * caller should expect from 9.3 onward, which is why the parameter is here now.
 *
 * @return ROBO_NOT_SUPPORTED in this build; ROBO_NOT_FOUND for an id that does
 *         not resolve.
 */
robo_result_t robo_css_stylesheet_parse(robo_css_stylesheet_id_t id,
                                        robo_css_error_t *out_error,
                                        robo_css_diag_t *diag);

/**
 * The error the last parse attempt recorded.
 * @return 1 and fills @p out_error when the stylesheet holds a recorded error;
 *         0 and clears @p out_error otherwise (including for an id that does
 *         not resolve).
 */
int robo_css_stylesheet_last_error(robo_css_stylesheet_id_t id,
                                   robo_css_error_t *out_error);

/** Record or clear the stylesheet's error slot. Used by the parse path; a
 *  caller may use it to attach its own explanation. */
void robo_css_stylesheet_set_last_error(robo_css_stylesheet_id_t id,
                                        const robo_css_error_t *error);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_CSS_STYLESHEET_H */
