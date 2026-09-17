/*
 * RoboCSS stylesheet model (Phase 9.1) — see RoboCSSStyleSheet.h.
 *
 * Every public call resolves its id to a slot first, and a call with an id that
 * does not resolve reports ROBO_NOT_FOUND rather than doing nothing quietly.
 * The one exception is asked for by name in the header: the clear-and-forget
 * calls (clear_rules) tolerate a dead id, because "make sure nothing is left"
 * has the same answer whether or not there was something.
 */

#include "RoboCSS/RoboCSSStyleSheet.h"

#include "RoboCSSInternal.h"
#include "RoboCSS/RoboCSS.h"

robo_result_t robo_css_stylesheet_create(robo_css_stylesheet_id_t *out_id,
                                         robo_css_origin_t origin,
                                         robo_css_diag_t *diag)
{
    robo_css_stylesheet_slot_t *slot;
    robo_css_stylesheet_id_t id;

    if (!out_id) {
        return ROBO_CSS_DIAG(diag, ROBO_CSS_DOMAIN_STYLESHEET, ROBO_INVALID_ARGUMENT,
                             "out_id is NULL");
    }
    *out_id = ROBO_CSS_STYLESHEET_ID_NONE;
    if (!robo_css_origin_is_known(origin)) {
        return ROBO_CSS_DIAG(diag, ROBO_CSS_DOMAIN_STYLESHEET, ROBO_INVALID_ARGUMENT,
                             "unknown stylesheet origin");
    }
    if (!robo_css_is_initialized()) {
        return ROBO_CSS_DIAG(diag, ROBO_CSS_DOMAIN_STYLESHEET, ROBO_INIT_ERROR,
                             "RoboCSS module is not initialized");
    }
    slot = robo_css_slot_alloc();
    if (!slot) {
        return ROBO_CSS_DIAG(diag, ROBO_CSS_DOMAIN_STYLESHEET, ROBO_OUT_OF_MEMORY,
                             "stylesheet table is full");
    }
    id = robo_css_slot_claim(slot, origin);
    *out_id = id;
    return ROBO_OK;
}

robo_result_t robo_css_stylesheet_destroy(robo_css_stylesheet_id_t id,
                                          robo_css_diag_t *diag)
{
    robo_css_stylesheet_slot_t *slot = robo_css_slot_find(id);

    if (id == ROBO_CSS_STYLESHEET_ID_NONE) {
        return ROBO_CSS_DIAG(diag, ROBO_CSS_DOMAIN_STYLESHEET, ROBO_NOT_FOUND,
                             "no stylesheet id given");
    }
    if (!slot) {
        return ROBO_CSS_DIAG(diag, ROBO_CSS_DOMAIN_STYLESHEET, ROBO_NOT_FOUND,
                             "stylesheet id does not resolve");
    }
    robo_css_slot_release(slot);
    return ROBO_OK;
}

int robo_css_stylesheet_is_live(robo_css_stylesheet_id_t id)
{
    return robo_css_slot_find(id) != NULL;
}

robo_css_stylesheet_id_t robo_css_stylesheet_id_at(size_t index)
{
    return robo_css_slot_id_at(index);
}

robo_result_t robo_css_stylesheet_set_origin(robo_css_stylesheet_id_t id,
                                             robo_css_origin_t origin,
                                             robo_css_diag_t *diag)
{
    robo_css_stylesheet_slot_t *slot = robo_css_slot_find(id);

    if (!slot) {
        return ROBO_CSS_DIAG(diag, ROBO_CSS_DOMAIN_STYLESHEET, ROBO_NOT_FOUND,
                             "stylesheet id does not resolve");
    }
    if (!robo_css_origin_is_known(origin)) {
        return ROBO_CSS_DIAG(diag, ROBO_CSS_DOMAIN_STYLESHEET, ROBO_INVALID_ARGUMENT,
                             "unknown stylesheet origin");
    }
    slot->origin = origin;
    return ROBO_OK;
}

robo_css_origin_t robo_css_stylesheet_origin(robo_css_stylesheet_id_t id)
{
    robo_css_stylesheet_slot_t *slot = robo_css_slot_find(id);

    return slot ? slot->origin : ROBO_CSS_ORIGIN_NONE;
}

robo_css_stylesheet_state_t robo_css_stylesheet_state(robo_css_stylesheet_id_t id)
{
    robo_css_stylesheet_slot_t *slot = robo_css_slot_find(id);

    return slot ? slot->state : ROBO_CSS_STYLESHEET_STATE_NONE;
}

robo_result_t robo_css_stylesheet_set_state(robo_css_stylesheet_id_t id,
                                            robo_css_stylesheet_state_t state,
                                            robo_css_diag_t *diag)
{
    robo_css_stylesheet_slot_t *slot = robo_css_slot_find(id);

    if (!slot) {
        return ROBO_CSS_DIAG(diag, ROBO_CSS_DOMAIN_STYLESHEET, ROBO_NOT_FOUND,
                             "stylesheet id does not resolve");
    }
    if (state != ROBO_CSS_STYLESHEET_STATE_CREATED &&
        state != ROBO_CSS_STYLESHEET_STATE_PARSED &&
        state != ROBO_CSS_STYLESHEET_STATE_FAILED) {
        return ROBO_CSS_DIAG(diag, ROBO_CSS_DOMAIN_STYLESHEET, ROBO_INVALID_ARGUMENT,
                             "a live stylesheet is created, parsed or failed");
    }
    slot->state = state;
    return ROBO_OK;
}

robo_result_t robo_css_stylesheet_set_name(robo_css_stylesheet_id_t id,
                                           const char *name,
                                           robo_css_diag_t *diag)
{
    robo_css_stylesheet_slot_t *slot = robo_css_slot_find(id);

    if (!slot) {
        return ROBO_CSS_DIAG(diag, ROBO_CSS_DOMAIN_STYLESHEET, ROBO_NOT_FOUND,
                             "stylesheet id does not resolve");
    }
    robo_css_copy_label(slot->name, sizeof(slot->name), name);
    return ROBO_OK;
}

const char *robo_css_stylesheet_name(robo_css_stylesheet_id_t id)
{
    robo_css_stylesheet_slot_t *slot = robo_css_slot_find(id);

    return slot ? slot->name : NULL;
}

/* ------------------------------------------------------------------ */
/* Source attachment                                                   */
/* ------------------------------------------------------------------ */

robo_result_t robo_css_stylesheet_set_source_memory(robo_css_stylesheet_id_t id,
                                                    const uint8_t *bytes,
                                                    size_t length,
                                                    robo_css_diag_t *diag)
{
    robo_css_stylesheet_slot_t *slot = robo_css_slot_find(id);

    if (!slot) {
        return ROBO_CSS_DIAG(diag, ROBO_CSS_DOMAIN_STYLESHEET, ROBO_NOT_FOUND,
                             "stylesheet id does not resolve");
    }
    return robo_css_source_set_memory(&slot->source, bytes, length, diag);
}

robo_result_t robo_css_stylesheet_set_source_string(robo_css_stylesheet_id_t id,
                                                    const char *utf8,
                                                    robo_css_diag_t *diag)
{
    robo_css_stylesheet_slot_t *slot = robo_css_slot_find(id);

    if (!slot) {
        return ROBO_CSS_DIAG(diag, ROBO_CSS_DOMAIN_STYLESHEET, ROBO_NOT_FOUND,
                             "stylesheet id does not resolve");
    }
    return robo_css_source_set_string(&slot->source, utf8, diag);
}

robo_result_t robo_css_stylesheet_copy_source(robo_css_stylesheet_id_t id,
                                             const uint8_t *bytes,
                                             size_t length,
                                             robo_css_diag_t *diag)
{
    robo_css_stylesheet_slot_t *slot = robo_css_slot_find(id);

    if (!slot) {
        return ROBO_CSS_DIAG(diag, ROBO_CSS_DOMAIN_STYLESHEET, ROBO_NOT_FOUND,
                             "stylesheet id does not resolve");
    }
    return robo_css_source_copy(&slot->source, bytes, length, diag);
}

const robo_css_source_t *robo_css_stylesheet_source(robo_css_stylesheet_id_t id)
{
    robo_css_stylesheet_slot_t *slot = robo_css_slot_find(id);

    if (!slot) {
        return NULL;
    }
    /* A stylesheet created a moment ago has no source configured; reporting the
     * slot's source object would hand back a KIND_NONE view that a caller would
     * have to know how to read. "No source" is the honest answer. */
    if (!robo_css_source_is_configured(&slot->source)) {
        return NULL;
    }
    return &slot->source;
}

size_t robo_css_stylesheet_source_length(robo_css_stylesheet_id_t id)
{
    robo_css_stylesheet_slot_t *slot = robo_css_slot_find(id);

    return slot ? robo_css_source_length(&slot->source) : 0u;
}

int robo_css_stylesheet_owns_source(robo_css_stylesheet_id_t id)
{
    robo_css_stylesheet_slot_t *slot = robo_css_slot_find(id);

    return slot && robo_css_source_is_owned(&slot->source);
}

/* ------------------------------------------------------------------ */
/* Rules                                                               */
/* ------------------------------------------------------------------ */

robo_result_t robo_css_stylesheet_append_rule(robo_css_stylesheet_id_t id,
                                              robo_css_rule_t *rule,
                                              robo_css_diag_t *diag)
{
    robo_css_stylesheet_slot_t *slot = robo_css_slot_find(id);

    if (!slot) {
        return ROBO_CSS_DIAG(diag, ROBO_CSS_DOMAIN_STYLESHEET, ROBO_NOT_FOUND,
                             "stylesheet id does not resolve");
    }
    return robo_css_rule_list_append(&slot->rules, rule, diag);
}

size_t robo_css_stylesheet_rule_count(robo_css_stylesheet_id_t id)
{
    robo_css_stylesheet_slot_t *slot = robo_css_slot_find(id);

    return slot ? robo_css_rule_list_count(&slot->rules) : 0u;
}

const robo_css_rule_t *robo_css_stylesheet_rule(robo_css_stylesheet_id_t id,
                                                size_t index)
{
    robo_css_stylesheet_slot_t *slot = robo_css_slot_find(id);

    if (!slot) {
        return NULL;
    }
    return robo_css_rule_list_at(&slot->rules, index);
}

size_t robo_css_stylesheet_tree_rule_count(robo_css_stylesheet_id_t id)
{
    robo_css_stylesheet_slot_t *slot = robo_css_slot_find(id);

    return slot ? robo_css_rule_list_tree_count(&slot->rules) : 0u;
}

size_t robo_css_stylesheet_style_rule_count(robo_css_stylesheet_id_t id)
{
    robo_css_stylesheet_slot_t *slot = robo_css_slot_find(id);

    return slot ? robo_css_rule_list_style_rule_count(&slot->rules) : 0u;
}

robo_result_t robo_css_stylesheet_remove_rule(robo_css_stylesheet_id_t id,
                                              size_t index,
                                              robo_css_diag_t *diag)
{
    robo_css_stylesheet_slot_t *slot = robo_css_slot_find(id);
    size_t i;

    if (!slot) {
        return ROBO_CSS_DIAG(diag, ROBO_CSS_DOMAIN_STYLESHEET, ROBO_NOT_FOUND,
                             "stylesheet id does not resolve");
    }
    if (index >= slot->rules.count) {
        return ROBO_CSS_DIAG(diag, ROBO_CSS_DOMAIN_STYLESHEET, ROBO_NOT_FOUND,
                             "rule index is past the end of the stylesheet");
    }
    robo_css_rule_dispose(&slot->rules.items[index]);
    /* Close the gap by moving the tail down, which is the same operation as
     * removing from the middle of any ordered list and keeps the remaining
     * rules in source order. */
    for (i = index + 1u; i < slot->rules.count; i++) {
        slot->rules.items[i - 1u] = slot->rules.items[i];
        robo_css_rule_init(&slot->rules.items[i]);
    }
    slot->rules.count--;
    return ROBO_OK;
}

void robo_css_stylesheet_clear_rules(robo_css_stylesheet_id_t id)
{
    robo_css_stylesheet_slot_t *slot = robo_css_slot_find(id);

    if (!slot) {
        return;
    }
    robo_css_rule_list_clear(&slot->rules);
}

const robo_css_rule_list_t *robo_css_stylesheet_rules(robo_css_stylesheet_id_t id)
{
    robo_css_stylesheet_slot_t *slot = robo_css_slot_find(id);

    return slot ? &slot->rules : NULL;
}

/* ------------------------------------------------------------------ */
/* Parse boundary                                                      */
/* ------------------------------------------------------------------ */

robo_result_t robo_css_stylesheet_parse(robo_css_stylesheet_id_t id,
                                        robo_css_error_t *out_error,
                                        robo_css_diag_t *diag)
{
    robo_css_stylesheet_slot_t *slot = robo_css_slot_find(id);
    robo_css_position_t position = robo_css_position_none();
    robo_css_error_t reason = ROBO_CSS_CLEARED;
    robo_result_t result;

    if (!slot) {
        robo_css_error_set(out_error, ROBO_CSS_ERROR_KIND_NONE, ROBO_NOT_FOUND, NULL,
                           "stylesheet id does not resolve");
        return ROBO_CSS_DIAG(diag, ROBO_CSS_DOMAIN_STYLESHEET, ROBO_NOT_FOUND,
                             "stylesheet id does not resolve");
    }
    /* The parser is 9.3. Reporting that is the only honest answer: no rules are
     * touched, the state is not advanced, and the source is not read. */
    robo_css_error_set(&reason, ROBO_CSS_ERROR_KIND_UNSUPPORTED, ROBO_NOT_SUPPORTED,
                       &position, "the CSS parser arrives in Goal 9.3");
    /* The reason is kept on the stylesheet whether or not the caller asked for
     * a copy, which is what makes it survive a call site that did not want it.
     * The caller's record is optional and must stay optional: robo_css_error_set
     * tolerates a NULL destination, so this function has to as well. */
    slot->has_error = 1;
    slot->error = reason;
    if (out_error) {
        *out_error = reason;
    }
    result = ROBO_CSS_DIAG(diag, ROBO_CSS_DOMAIN_PARSER, ROBO_NOT_SUPPORTED,
                           "the CSS parser arrives in Goal 9.3");
    return result;
}

int robo_css_stylesheet_last_error(robo_css_stylesheet_id_t id,
                                   robo_css_error_t *out_error)
{
    robo_css_stylesheet_slot_t *slot = robo_css_slot_find(id);

    if (!slot || !slot->has_error) {
        robo_css_error_init(out_error);
        return 0;
    }
    if (out_error) {
        *out_error = slot->error;
    }
    return 1;
}

void robo_css_stylesheet_set_last_error(robo_css_stylesheet_id_t id,
                                        const robo_css_error_t *error)
{
    robo_css_stylesheet_slot_t *slot = robo_css_slot_find(id);

    if (!slot) {
        return;
    }
    if (!error || error->kind == ROBO_CSS_ERROR_KIND_NONE) {
        slot->has_error = 0;
        robo_css_error_init(&slot->error);
        return;
    }
    slot->has_error = 1;
    slot->error = *error;
}
