/*
 * RoboHTML client/render integration boundary (Phase 8.1).
 * Hook installation + synchronous emission; RoboHTML never draws.
 */

#include "RoboHTML/RoboHTMLClient.h"
#include "RoboHTMLInternal.h"

#include <stddef.h>
#include <string.h>

int robo_html_invalidation_reason_is_known(
    robo_html_invalidation_reason_t reason)
{
    switch (reason) {
    case ROBO_HTML_INVALIDATION_NONE:
    case ROBO_HTML_INVALIDATION_SOURCE:
    case ROBO_HTML_INVALIDATION_LIFECYCLE:
    case ROBO_HTML_INVALIDATION_PARSING:
    case ROBO_HTML_INVALIDATION_STYLE:
    case ROBO_HTML_INVALIDATION_LAYOUT:
    case ROBO_HTML_INVALIDATION_PAINT:
        return 1;
    default:
        return 0;
    }
}

const char *robo_html_invalidation_reason_name(
    robo_html_invalidation_reason_t reason)
{
    switch (reason) {
    case ROBO_HTML_INVALIDATION_NONE:
        return "none";
    case ROBO_HTML_INVALIDATION_SOURCE:
        return "source";
    case ROBO_HTML_INVALIDATION_LIFECYCLE:
        return "lifecycle";
    case ROBO_HTML_INVALIDATION_PARSING:
        return "parsing";
    case ROBO_HTML_INVALIDATION_STYLE:
        return "style";
    case ROBO_HTML_INVALIDATION_LAYOUT:
        return "layout";
    case ROBO_HTML_INVALIDATION_PAINT:
        return "paint";
    default:
        return "unknown";
    }
}

robo_result_t robo_html_document_set_client(
    robo_html_document_id_t document_id,
    const robo_html_client_hooks_t *hooks,
    void *userdata,
    robo_html_diag_t *diag)
{
    robo_html_document_slot_t *slot = robo_html_document_slot_find(document_id);

    if (!slot) {
        return ROBO_HTML_DIAG(diag, ROBO_HTML_DOMAIN_DOCUMENT,
                              ROBO_NOT_FOUND, "unknown document id");
    }
    if (hooks && slot->client.on_document_lifecycle) {
        return ROBO_HTML_DIAG(diag, ROBO_HTML_DOMAIN_CLIENT,
                              ROBO_ALREADY_EXISTS,
                              "client already installed; clear first");
    }
    if (hooks) {
        slot->client = *hooks;
        slot->client_userdata = userdata;
    } else {
        memset(&slot->client, 0, sizeof(slot->client));
        slot->client_userdata = NULL;
    }
    return ROBO_OK;
}

int robo_html_document_has_client(robo_html_document_id_t document_id)
{
    const robo_html_document_slot_t *slot =
        robo_html_document_slot_find(document_id);

    if (!slot) {
        return 0;
    }
    return slot->client.on_document_lifecycle != NULL ||
           slot->client.on_invalidate != NULL;
}

void robo_html_emit_lifecycle(robo_html_document_slot_t *slot,
                              robo_html_document_lifecycle_t from,
                              robo_html_document_lifecycle_t to)
{
    if (!slot || !slot->client.on_document_lifecycle) {
        return;
    }
    slot->client.on_document_lifecycle(slot->id, from, to,
                                       slot->client_userdata);
}

void robo_html_emit_invalidate(robo_html_document_slot_t *slot,
                               robo_html_invalidation_reason_t reason)
{
    if (!slot || !slot->client.on_invalidate) {
        return;
    }
    slot->client.on_invalidate(slot->id, reason, slot->client_userdata);
}
