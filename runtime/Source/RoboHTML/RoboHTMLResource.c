/*
 * RoboHTML resource/loading boundary (Phase 8.1).
 * Delegated-acquisition slot only; no I/O of any kind in RoboHTML.
 */

#include "RoboHTML/RoboHTMLResource.h"
#include "RoboHTMLInternal.h"

#include "RoboBase/RoboLog.h"

int robo_html_load_phase_is_known(robo_html_load_phase_t phase)
{
    switch (phase) {
    case ROBO_HTML_LOAD_PHASE_NONE:
    case ROBO_HTML_LOAD_PHASE_ACQUIRING:
    case ROBO_HTML_LOAD_PHASE_PARSING:
    case ROBO_HTML_LOAD_PHASE_DONE:
    case ROBO_HTML_LOAD_PHASE_FAILED:
        return 1;
    default:
        return 0;
    }
}

const char *robo_html_load_phase_name(robo_html_load_phase_t phase)
{
    switch (phase) {
    case ROBO_HTML_LOAD_PHASE_NONE:
        return "none";
    case ROBO_HTML_LOAD_PHASE_ACQUIRING:
        return "acquiring";
    case ROBO_HTML_LOAD_PHASE_PARSING:
        return "parsing";
    case ROBO_HTML_LOAD_PHASE_DONE:
        return "done";
    case ROBO_HTML_LOAD_PHASE_FAILED:
        return "failed";
    default:
        return "unknown";
    }
}

robo_result_t robo_html_resource_begin(robo_html_document_id_t document_id,
                                       robo_html_loader_t *loader,
                                       robo_html_diag_t *diag)
{
    robo_html_document_slot_t *slot = robo_html_document_slot_find(document_id);

    if (!slot) {
        return ROBO_HTML_DIAG(diag, ROBO_HTML_DOMAIN_RESOURCE,
                              ROBO_NOT_FOUND, "unknown document id");
    }
    if (slot->load_phase == ROBO_HTML_LOAD_PHASE_ACQUIRING) {
        return ROBO_HTML_DIAG(diag, ROBO_HTML_DOMAIN_RESOURCE, ROBO_BUSY,
                              "acquisition already outstanding");
    }
    if (!robo_html_document_lifecycle_allows_source_attach(slot->lifecycle)) {
        return ROBO_HTML_DIAG(diag, ROBO_HTML_DOMAIN_RESOURCE, ROBO_ERROR,
                              "lifecycle does not allow acquisition");
    }
    slot->load_phase = ROBO_HTML_LOAD_PHASE_ACQUIRING;
    slot->loader = loader;
    robo_log_debug("robo_html: acquisition begin doc=%llu loader=%p",
                   (unsigned long long)document_id, (void *)loader);
    return ROBO_OK;
}

robo_result_t robo_html_resource_end(robo_html_document_id_t document_id,
                                     int ok,
                                     robo_html_diag_t *diag)
{
    robo_html_document_slot_t *slot = robo_html_document_slot_find(document_id);

    if (!slot) {
        return ROBO_HTML_DIAG(diag, ROBO_HTML_DOMAIN_RESOURCE,
                              ROBO_NOT_FOUND, "unknown document id");
    }
    if (slot->load_phase != ROBO_HTML_LOAD_PHASE_ACQUIRING) {
        /* Idempotent no-op when nothing is outstanding. */
        return ROBO_OK;
    }
    slot->loader = NULL;
    if (ok) {
        slot->load_phase = ROBO_HTML_LOAD_PHASE_NONE;
    } else {
        slot->load_phase = ROBO_HTML_LOAD_PHASE_FAILED;
        (void)robo_html_document_mark_failed(document_id,
                                             ROBO_HTML_DOMAIN_RESOURCE,
                                             "acquisition failed", diag);
    }
    robo_log_debug("robo_html: acquisition end doc=%llu ok=%d",
                   (unsigned long long)document_id, ok);
    return ROBO_OK;
}

robo_result_t robo_html_resource_cancel(robo_html_document_id_t document_id,
                                        robo_html_diag_t *diag)
{
    robo_html_document_slot_t *slot = robo_html_document_slot_find(document_id);

    if (!slot) {
        return ROBO_HTML_DIAG(diag, ROBO_HTML_DOMAIN_RESOURCE,
                              ROBO_NOT_FOUND, "unknown document id");
    }
    if (slot->load_phase != ROBO_HTML_LOAD_PHASE_ACQUIRING) {
        return ROBO_OK;
    }
    slot->load_phase = ROBO_HTML_LOAD_PHASE_NONE;
    slot->loader = NULL;
    (void)robo_html_diag_set(diag, ROBO_OK, ROBO_HTML_DOMAIN_RESOURCE,
                             "acquisition cancelled", __FILE__, __LINE__);
    robo_log_debug("robo_html: acquisition cancelled doc=%llu",
                   (unsigned long long)document_id);
    return ROBO_OK;
}

robo_result_t robo_html_resource_get_phase(robo_html_document_id_t document_id,
                                           robo_html_load_phase_t *out_phase)
{
    const robo_html_document_slot_t *slot =
        robo_html_document_slot_find(document_id);

    if (!slot || !out_phase) {
        return ROBO_NOT_FOUND;
    }
    *out_phase = slot->load_phase;
    return ROBO_OK;
}
