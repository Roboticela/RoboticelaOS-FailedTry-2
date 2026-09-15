/*
 * RoboHTML parser boundary (Phase 8.1).
 * Exclusive lease + state machine only; no tokenization.
 */

#include "RoboHTML/RoboHTML.h"
#include "RoboHTML/RoboHTMLParser.h"
#include "RoboHTMLInternal.h"

#include "RoboBase/RoboLog.h"
#include "RoboMem/RoboMem.h"

#include <stddef.h>
#include <string.h>

int robo_html_parser_mode_is_known(robo_html_parser_mode_t mode)
{
    switch (mode) {
    case ROBO_HTML_PARSER_MODE_NONE:
    case ROBO_HTML_PARSER_MODE_COMPLETE:
    case ROBO_HTML_PARSER_MODE_INCREMENTAL:
        return 1;
    default:
        return 0;
    }
}

const char *robo_html_parser_mode_name(robo_html_parser_mode_t mode)
{
    switch (mode) {
    case ROBO_HTML_PARSER_MODE_NONE:
        return "none";
    case ROBO_HTML_PARSER_MODE_COMPLETE:
        return "complete";
    case ROBO_HTML_PARSER_MODE_INCREMENTAL:
        return "incremental";
    default:
        return "unknown";
    }
}

int robo_html_parser_state_is_known(robo_html_parser_state_t state)
{
    switch (state) {
    case ROBO_HTML_PARSER_STATE_IDLE:
    case ROBO_HTML_PARSER_STATE_FEEDING:
    case ROBO_HTML_PARSER_STATE_COMPLETE:
    case ROBO_HTML_PARSER_STATE_FAILED:
    case ROBO_HTML_PARSER_STATE_DETACHED:
        return 1;
    default:
        return 0;
    }
}

const char *robo_html_parser_state_name(robo_html_parser_state_t state)
{
    switch (state) {
    case ROBO_HTML_PARSER_STATE_IDLE:
        return "idle";
    case ROBO_HTML_PARSER_STATE_FEEDING:
        return "feeding";
    case ROBO_HTML_PARSER_STATE_COMPLETE:
        return "complete";
    case ROBO_HTML_PARSER_STATE_FAILED:
        return "failed";
    case ROBO_HTML_PARSER_STATE_DETACHED:
        return "detached";
    default:
        return "unknown";
    }
}

robo_result_t robo_html_parser_create(robo_html_document_id_t document_id,
                                      robo_html_parser_t **out_parser,
                                      robo_html_diag_t *diag)
{
    robo_html_document_slot_t *slot;
    robo_html_parser_t *p;

    if (!out_parser) {
        return ROBO_HTML_DIAG(diag, ROBO_HTML_DOMAIN_PARSER,
                              ROBO_INVALID_ARGUMENT, "out_parser is NULL");
    }
    *out_parser = NULL;
    if (!robo_html_is_initialized()) {
        return ROBO_HTML_DIAG(diag, ROBO_HTML_DOMAIN_PARSER, ROBO_INIT_ERROR,
                              "RoboHTML module not initialized");
    }
    slot = robo_html_document_slot_find(document_id);
    if (!slot) {
        return ROBO_HTML_DIAG(diag, ROBO_HTML_DOMAIN_PARSER, ROBO_NOT_FOUND,
                              "unknown document id");
    }
    if (slot->lifecycle != ROBO_HTML_DOCUMENT_LIFECYCLE_CREATED &&
        slot->lifecycle != ROBO_HTML_DOCUMENT_LIFECYCLE_READY) {
        return ROBO_HTML_DIAG(diag, ROBO_HTML_DOMAIN_PARSER, ROBO_ERROR,
                              "document lifecycle does not accept a parser");
    }
    if (slot->leased_parser) {
        return ROBO_HTML_DIAG(diag, ROBO_HTML_DOMAIN_PARSER, ROBO_BUSY,
                              "document already has a parser lease");
    }
    p = (robo_html_parser_t *)robo_calloc(1u, sizeof(*p));
    if (!p) {
        return ROBO_HTML_DIAG(diag, ROBO_HTML_DOMAIN_PARSER,
                              ROBO_OUT_OF_MEMORY, "parser allocation failed");
    }
    p->magic = ROBO_HTML_PARSER_MAGIC;
    p->document_id = document_id;
    p->state = ROBO_HTML_PARSER_STATE_IDLE;
    p->mode = ROBO_HTML_PARSER_MODE_NONE;
    p->source_kind = ROBO_HTML_SOURCE_KIND_NONE;
    p->source_length = 0u;
    slot->leased_parser = p;
    *out_parser = p;
    robo_log_debug("robo_html: parser leased doc=%llu",
                   (unsigned long long)document_id);
    return ROBO_OK;
}

robo_result_t robo_html_parser_attach_source(robo_html_parser_t *parser,
                                             const robo_html_source_t *src,
                                             robo_html_diag_t *diag)
{
    robo_html_document_slot_t *slot;
    robo_html_parser_mode_t mode;

    if (!parser || parser->magic != ROBO_HTML_PARSER_MAGIC) {
        return ROBO_HTML_DIAG(diag, ROBO_HTML_DOMAIN_PARSER,
                              ROBO_INVALID_ARGUMENT, "parser is invalid");
    }
    if (!src) {
        return ROBO_HTML_DIAG(diag, ROBO_HTML_DOMAIN_PARSER,
                              ROBO_INVALID_ARGUMENT, "source is NULL");
    }
    slot = robo_html_document_slot_find(parser->document_id);
    if (!slot) {
        parser->state = ROBO_HTML_PARSER_STATE_DETACHED;
        return ROBO_HTML_DIAG(diag, ROBO_HTML_DOMAIN_PARSER, ROBO_NOT_FOUND,
                              "owning document no longer exists");
    }
    if (parser->state == ROBO_HTML_PARSER_STATE_FEEDING) {
        return ROBO_HTML_DIAG(diag, ROBO_HTML_DOMAIN_PARSER, ROBO_BUSY,
                              "source already attached");
    }
    if (parser->state == ROBO_HTML_PARSER_STATE_FAILED ||
        parser->state == ROBO_HTML_PARSER_STATE_DETACHED ||
        parser->state == ROBO_HTML_PARSER_STATE_COMPLETE) {
        return ROBO_HTML_DIAG(diag, ROBO_HTML_DOMAIN_PARSER, ROBO_ERROR,
                              "parser state does not accept a source");
    }
    if (!robo_html_document_lifecycle_allows_content(slot->lifecycle)) {
        return ROBO_HTML_DIAG(diag, ROBO_HTML_DOMAIN_PARSER, ROBO_ERROR,
                              "document lifecycle does not allow parsing");
    }
    if (slot->load_phase == ROBO_HTML_LOAD_PHASE_ACQUIRING) {
        return ROBO_HTML_DIAG(diag, ROBO_HTML_DOMAIN_PARSER, ROBO_BUSY,
                              "resource acquisition outstanding");
    }
    if (src->kind == ROBO_HTML_SOURCE_KIND_STREAM) {
        mode = ROBO_HTML_PARSER_MODE_INCREMENTAL;
    } else {
        mode = ROBO_HTML_PARSER_MODE_COMPLETE;
    }
    /* Record declared encoding metadata through the encoding boundary. */
    if (src->encoding[0] != '\0') {
        (void)robo_html_document_input_encoding_declared(parser->document_id,
                                                         src->encoding, diag);
    }
    parser->mode = mode;
    parser->source_kind = src->kind;
    parser->source_length = robo_html_source_length(src);
    parser->state = ROBO_HTML_PARSER_STATE_FEEDING;
    slot->source_kind = src->kind;
    slot->source_length = parser->source_length;
    if (src->name[0] != '\0') {
        memcpy(slot->source_name, src->name, sizeof(slot->source_name) - 1u);
        slot->source_name[sizeof(slot->source_name) - 1u] = '\0';
    }
    (void)robo_html_document_apply_transition(
        slot, ROBO_HTML_DOCUMENT_LIFECYCLE_LOADING);
    (void)robo_html_diag_set(diag, ROBO_OK, ROBO_HTML_DOMAIN_PARSER,
                             "source attached", __FILE__, __LINE__);
    robo_log_debug("robo_html: parser source attached doc=%llu mode=%s len=%llu",
                   (unsigned long long)parser->document_id,
                   robo_html_parser_mode_name(mode),
                   (unsigned long long)parser->source_length);
    return ROBO_OK;
}

robo_result_t robo_html_parser_detach(robo_html_parser_t *parser,
                                      robo_html_diag_t *diag)
{
    robo_html_document_slot_t *slot;

    if (!parser || parser->magic != ROBO_HTML_PARSER_MAGIC) {
        return ROBO_HTML_DIAG(diag, ROBO_HTML_DOMAIN_PARSER,
                              ROBO_INVALID_ARGUMENT, "parser is invalid");
    }
    if (parser->state == ROBO_HTML_PARSER_STATE_DETACHED) {
        return ROBO_OK;
    }
    slot = robo_html_document_slot_find(parser->document_id);
    if (slot) {
        if (slot->leased_parser == parser) {
            slot->leased_parser = NULL;
        }
        if (slot->lifecycle == ROBO_HTML_DOCUMENT_LIFECYCLE_LOADING) {
            (void)robo_html_document_apply_transition(
                slot, ROBO_HTML_DOCUMENT_LIFECYCLE_READY);
        }
    }
    parser->document_id = ROBO_HTML_DOCUMENT_ID_NONE;
    parser->state = ROBO_HTML_PARSER_STATE_DETACHED;
    parser->mode = ROBO_HTML_PARSER_MODE_NONE;
    parser->source_kind = ROBO_HTML_SOURCE_KIND_NONE;
    parser->source_length = 0u;
    return ROBO_OK;
}

robo_result_t robo_html_parser_get_state(const robo_html_parser_t *parser,
                                         robo_html_parser_state_t *out_state)
{
    if (!parser || parser->magic != ROBO_HTML_PARSER_MAGIC || !out_state) {
        return ROBO_INVALID_ARGUMENT;
    }
    *out_state = parser->state;
    return ROBO_OK;
}

robo_result_t robo_html_parser_get_mode(const robo_html_parser_t *parser,
                                        robo_html_parser_mode_t *out_mode)
{
    if (!parser || parser->magic != ROBO_HTML_PARSER_MAGIC || !out_mode) {
        return ROBO_INVALID_ARGUMENT;
    }
    *out_mode = parser->mode;
    return ROBO_OK;
}

robo_html_document_id_t robo_html_parser_document(
    const robo_html_parser_t *parser)
{
    if (!parser || parser->magic != ROBO_HTML_PARSER_MAGIC) {
        return ROBO_HTML_DOCUMENT_ID_NONE;
    }
    return parser->document_id;
}

void robo_html_parser_destroy(robo_html_parser_t *parser)
{
    if (!parser || parser->magic != ROBO_HTML_PARSER_MAGIC) {
        return;
    }
    (void)robo_html_parser_detach(parser, NULL);
    parser->magic = 0u;
    robo_free(parser);
}
