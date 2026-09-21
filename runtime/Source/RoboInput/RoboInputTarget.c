/*
 * RoboInput targets (Phase 12.1) — see RoboInputTarget.h.
 *
 * Every door here fills a *record of identity* and checks that the record does not say two things at once.
 * The validation is the substance of the file: a target that carried both a window and an application, or a
 * document tip with no window to have hit it in, would be a record whose reader has to guess which field
 * the producer meant -- and a reader that guesses is a dispatcher that routes to the wrong place.
 *
 * Nothing in this file resolves anything. A handle is copied, checked for well-formedness (which is a
 * question about the *handle*, answerable by RoboHTML itself) and never resolved into a node; a layout id is
 * copied and never looked up. That is what makes a target safe to carry on an event that outlives the tree
 * it was concluded in.
 */

#include "RoboInput/RoboInputTarget.h"

#include "RoboInputInternal.h"

#include "RoboHTML/RoboHTMLNode.h"

#include <stdint.h>

int robo_input_target_kind_is_known(robo_input_target_kind_t kind)
{
    return kind >= ROBO_INPUT_TARGET_NONE && kind < (robo_input_target_kind_t)ROBO_INPUT_TARGET_KIND_COUNT;
}

const char *robo_input_target_kind_name(robo_input_target_kind_t kind)
{
    switch (kind) {
    case ROBO_INPUT_TARGET_NONE:
        return "none";
    case ROBO_INPUT_TARGET_WINDOW:
        return "window";
    case ROBO_INPUT_TARGET_APPLICATION:
        return "application";
    case ROBO_INPUT_TARGET_DOCUMENT:
        return "document";
    default:
        break;
    }
    return "unknown";
}

void robo_input_target_init(robo_input_target_t *target)
{
    if (!target) {
        return;
    }
    target->kind = ROBO_INPUT_TARGET_NONE;
    target->window = ROBO_WINDOW_ID_NONE;
    target->application = ROBO_INPUT_APPLICATION_ID_NONE;
    target->display = ROBO_DISPLAY_ID_NONE;
    robo_html_node_handle_clear(&target->dom_node);
    target->layout_node = ROBO_LAYOUT_NODE_ID_NONE;
    target->flags = 0u;
    target->space = ROBO_INPUT_SPACE_UNKNOWN;
}

/** Non-zero when the record carries no document identity at all. */
static int target_document_identity_is_empty(const robo_input_target_t *target)
{
    return !robo_html_node_handle_is_wellformed(&target->dom_node) &&
           target->layout_node == ROBO_LAYOUT_NODE_ID_NONE;
}

/** Non-zero when the record carries no *routing* identity: no application and no document tip. */
static int target_payload_is_empty(const robo_input_target_t *target)
{
    return target->application == ROBO_INPUT_APPLICATION_ID_NONE && target->flags == 0u &&
           target_document_identity_is_empty(target);
}

int robo_input_target_is_valid(const robo_input_target_t *target)
{
    if (!target) {
        return 0;
    }
    if (!robo_input_target_kind_is_known(target->kind)) {
        return 0;
    }
    /* A target names the plane its routing was concluded in, and `UNKNOWN` is not a plane: a target whose
     * plane is unset could not be compared against the event it belongs to, which is the one check that
     * makes a target trustworthy. */
    if (target->space == ROBO_INPUT_SPACE_UNKNOWN || !robo_input_space_is_known(target->space)) {
        return 0;
    }
    if ((target->flags & ~ROBO_INPUT_TARGET_FLAG_ALL) != 0u) {
        return 0;
    }

    switch (target->kind) {
    case ROBO_INPUT_TARGET_NONE:
        /* About nothing: nothing may be filled in. */
        return target->window == ROBO_WINDOW_ID_NONE && target_payload_is_empty(target);
    case ROBO_INPUT_TARGET_WINDOW:
        return target->window != ROBO_WINDOW_ID_NONE && target_payload_is_empty(target);
    case ROBO_INPUT_TARGET_APPLICATION:
        return target->application != ROBO_INPUT_APPLICATION_ID_NONE &&
               target->window == ROBO_WINDOW_ID_NONE && target->flags == 0u &&
               target_document_identity_is_empty(target);
    case ROBO_INPUT_TARGET_DOCUMENT:
        /* A window, and something under the point: at least one of the two identities, because both absent
         * would be a `WINDOW` target wearing a document's kind. */
        return target->window != ROBO_WINDOW_ID_NONE && target->application == ROBO_INPUT_APPLICATION_ID_NONE &&
               !target_document_identity_is_empty(target);
    default:
        break;
    }
    return 0;
}

int robo_input_target_has_document(const robo_input_target_t *target)
{
    return target != NULL && target->kind == ROBO_INPUT_TARGET_DOCUMENT;
}

int robo_input_target_is_window(const robo_input_target_t *target, robo_window_id_t window)
{
    return target != NULL && window != ROBO_WINDOW_ID_NONE && target->window == window;
}

/**
 * Clear @p target and give it a plane: the first thing every `set_*` door below does.
 *
 * Clearing first is what makes a refusal *safe*: a caller that reuses one target record for event after
 * event and gets a refusal is left with an empty record rather than the previous event's routing, so a
 * mistake cannot silently re-deliver the last target.
 */
static robo_result_t set_space(robo_input_target_t *target,
                               robo_input_space_t space,
                               robo_input_diag_t *diag)
{
    if (!target) {
        return ROBO_INPUT_DIAG(diag, ROBO_INPUT_DOMAIN_TARGET, ROBO_INVALID_ARGUMENT,
                               "a target needs a record to fill");
    }
    if (!robo_input_space_is_known(space)) {
        return ROBO_INPUT_DIAG(diag, ROBO_INPUT_DOMAIN_TARGET, ROBO_INVALID_ARGUMENT,
                               "a target needs a defined plane");
    }
    robo_input_target_init(target);
    target->space = space;
    return ROBO_OK;
}

robo_result_t robo_input_target_set_none(robo_input_target_t *target,
                                         robo_input_space_t space,
                                         robo_input_diag_t *diag)
{
    return set_space(target, space, diag);
}

robo_result_t robo_input_target_set_window(robo_input_target_t *target,
                                           robo_window_id_t window,
                                           robo_input_space_t space,
                                           robo_input_diag_t *diag)
{
    robo_result_t r = set_space(target, space, diag);

    if (r != ROBO_OK) {
        return r;
    }
    if (window == ROBO_WINDOW_ID_NONE) {
        return ROBO_INPUT_DIAG(diag, ROBO_INPUT_DOMAIN_TARGET, ROBO_INVALID_ARGUMENT,
                               "a window target needs a window");
    }
    target->kind = ROBO_INPUT_TARGET_WINDOW;
    target->window = window;
    return ROBO_OK;
}

robo_result_t robo_input_target_set_application(robo_input_target_t *target,
                                                robo_input_application_id_t application,
                                                robo_input_space_t space,
                                                robo_input_diag_t *diag)
{
    robo_result_t r = set_space(target, space, diag);

    if (r != ROBO_OK) {
        return r;
    }
    if (application == ROBO_INPUT_APPLICATION_ID_NONE) {
        return ROBO_INPUT_DIAG(diag, ROBO_INPUT_DOMAIN_TARGET, ROBO_INVALID_ARGUMENT,
                               "an application target needs an application");
    }
    target->kind = ROBO_INPUT_TARGET_APPLICATION;
    target->application = application;
    return ROBO_OK;
}

robo_result_t robo_input_target_set_document(robo_input_target_t *target,
                                             robo_window_id_t window,
                                             const robo_html_node_handle_t *dom_node,
                                             robo_layout_node_id_t layout_node,
                                             uint32_t flags,
                                             robo_input_space_t space,
                                             robo_input_diag_t *diag)
{
    robo_result_t r = set_space(target, space, diag);

    if (r != ROBO_OK) {
        return r;
    }
    if (window == ROBO_WINDOW_ID_NONE) {
        return ROBO_INPUT_DIAG(diag, ROBO_INPUT_DOMAIN_TARGET, ROBO_INVALID_ARGUMENT,
                               "a document target needs the window it was hit in");
    }
    if ((flags & ~ROBO_INPUT_TARGET_FLAG_ALL) != 0u) {
        return ROBO_INPUT_DIAG(diag, ROBO_INPUT_DOMAIN_TARGET, ROBO_INVALID_ARGUMENT,
                               "a document target was given a flag this build does not define");
    }
    if (dom_node) {
        target->dom_node = *dom_node;
    }
    target->layout_node = layout_node;
    if (target_document_identity_is_empty(target)) {
        return ROBO_INPUT_DIAG(diag, ROBO_INPUT_DOMAIN_TARGET, ROBO_INVALID_ARGUMENT,
                               "a document target needs the node under the point");
    }
    target->kind = ROBO_INPUT_TARGET_DOCUMENT;
    target->window = window;
    target->flags = flags;
    return ROBO_OK;
}

int robo_input_target_document(const robo_input_target_t *target,
                               robo_html_node_handle_t *out_dom_node,
                               robo_layout_node_id_t *out_node)
{
    if (out_dom_node) {
        robo_html_node_handle_clear(out_dom_node);
    }
    if (out_node) {
        *out_node = ROBO_LAYOUT_NODE_ID_NONE;
    }
    if (!target || target->kind != ROBO_INPUT_TARGET_DOCUMENT) {
        return 0;
    }
    if (out_dom_node) {
        *out_dom_node = target->dom_node;
    }
    if (out_node) {
        *out_node = target->layout_node;
    }
    return 1;
}

int robo_input_target_has_flag(const robo_input_target_t *target, uint32_t flag)
{
    return target != NULL && (target->flags & flag) != 0u;
}

int robo_input_target_equal(const robo_input_target_t *a, const robo_input_target_t *b)
{
    if (a == b) {
        return 1;
    }
    if (!a || !b) {
        return 0;
    }
    return a->kind == b->kind && a->window == b->window && a->application == b->application &&
           a->display == b->display && a->layout_node == b->layout_node && a->flags == b->flags &&
           a->space == b->space && a->dom_node.document_id == b->dom_node.document_id &&
           a->dom_node.node_id == b->dom_node.node_id && a->dom_node.generation == b->dom_node.generation;
}
