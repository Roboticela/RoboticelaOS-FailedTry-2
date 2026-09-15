/*
 * Internal RoboHTML glue (Phase 8.1) — not a public API.
 * Mirrors RoboWindowInternal.h conventions: shared internal layout for
 * the document table, parser leases, and hook slots.
 */
#ifndef ROBO_HTML_INTERNAL_H
#define ROBO_HTML_INTERNAL_H

#include "RoboHTML/RoboHTMLClient.h"
#include "RoboHTML/RoboHTMLDocument.h"
#include "RoboHTML/RoboHTMLDom.h"
#include "RoboHTML/RoboHTMLEncoding.h"
#include "RoboHTML/RoboHTMLLifecycle.h"
#include "RoboHTML/RoboHTMLNode.h"
#include "RoboHTML/RoboHTMLParser.h"
#include "RoboHTML/RoboHTMLResource.h"
#include "RoboHTML/RoboHTMLSource.h"
#include "RoboHTML/RoboHTMLTypes.h"

#include <stddef.h>
#include <stdint.h>

/** Per-document state slot (fixed table, module-owned). */
typedef struct robo_html_document_slot {
    int in_use;
    robo_html_document_id_t id;

    robo_html_document_lifecycle_t lifecycle;
    robo_html_document_lifecycle_t previous_lifecycle;

    /** Failure record for FAILED documents (diagnostics, no heap). */
    robo_html_diagnostic_domain_t fail_domain;
    const char *fail_message;

    /** Attached source metadata (kind; bytes stay application-owned). */
    robo_html_source_kind_t source_kind;
    size_t source_length;
    char source_name[ROBO_HTML_URL_MAX];

    robo_html_encoding_state_t encoding;

    robo_html_node_registry_t nodes;
    robo_html_node_handle_t root;

    /** Parser lease (exclusive; NULL when unleased). */
    struct robo_html_parser *leased_parser;

    /** Client/render hooks (8.1: lifecycle + invalidation). */
    robo_html_client_hooks_t client;
    void *client_userdata;

    /** Mutation hook foundation (8.1: node registration + lifecycle). */
    robo_html_mutation_hook_fn mutation_hook;
    void *mutation_userdata;

    /** Resource boundary slot (acquisition delegated; see RoboHTMLResource.h). */
    robo_html_load_phase_t load_phase;
    robo_html_loader_t *loader;
} robo_html_document_slot_t;

/** Parser object (heap-allocated; layout private to RoboHTML sources). */
struct robo_html_parser {
    uint32_t magic; /* ROBO_HTML_PARSER_MAGIC; guards use-after-destroy */
    robo_html_document_id_t document_id;
    robo_html_parser_state_t state;
    robo_html_parser_mode_t mode;
    robo_html_source_kind_t source_kind;
    size_t source_length;
};

#define ROBO_HTML_PARSER_MAGIC 0x524F4255u /* "ROBU" */

/* ------------------------------------------------------------------ *
 * Tokenizer internals (Phase 8.2)                                    *
 * ------------------------------------------------------------------ */

#include "RoboHTML/RoboHTMLToken.h"
#include "RoboHTML/RoboHTMLTokenizer.h"
#include "RoboHTML/RoboHTMLTokenizerStates.h"

/** Growable byte buffer for token text (tag names, data, comments, ...). */
typedef struct robo_html_token_buffer {
    char *data;
    size_t length;
    size_t capacity; /* includes the NUL slot */
} robo_html_token_buffer_t;

void robo_html_token_buffer_init(robo_html_token_buffer_t *buf);
void robo_html_token_buffer_free(robo_html_token_buffer_t *buf);
robo_result_t robo_html_token_buffer_reserve(robo_html_token_buffer_t *buf,
                                             size_t extra);
void robo_html_token_buffer_clear(robo_html_token_buffer_t *buf);
robo_result_t robo_html_token_buffer_append_byte(
    robo_html_token_buffer_t *buf, char byte);
robo_result_t robo_html_token_buffer_append_utf8(
    robo_html_token_buffer_t *buf, uint32_t cp);

/** One pending attribute (name + value scratch buffers). */
typedef struct robo_html_token_attr_entry {
    robo_html_token_buffer_t name;
    robo_html_token_buffer_t value;
} robo_html_token_attr_entry_t;

/** Attribute pool reused across tokens (entries reset per token). */
typedef struct robo_html_token_pool {
    robo_html_token_attr_entry_t *entries;
    size_t count;
    size_t capacity;
} robo_html_token_pool_t;

void robo_html_token_pool_init(robo_html_token_pool_t *pool);
void robo_html_token_pool_free(robo_html_token_pool_t *pool);
void robo_html_token_pool_clear(robo_html_token_pool_t *pool);
robo_result_t robo_html_token_pool_next(robo_html_token_pool_t *pool,
                                        robo_html_token_attr_entry_t **out);

/** Bounded in-tokenizer diagnostic list (survives until reset). */
typedef struct robo_html_diag_list {
    robo_html_diag_t items[ROBO_HTML_TOKENIZER_MAX_DIAGNOSTICS];
    size_t count;
} robo_html_diag_list_t;

void robo_html_diagnostics_init(robo_html_diag_list_t *list);
void robo_html_diagnostics_push(robo_html_diag_list_t *list,
                                robo_result_t code,
                                robo_html_diagnostic_domain_t domain,
                                const char *message,
                                uint32_t line,
                                uint32_t column);
void robo_html_diagnostics_clear(robo_html_diag_list_t *list);

/** Document table access (RoboHTMLDocument.c). */
robo_html_document_slot_t *robo_html_document_slot_find(
    robo_html_document_id_t id);
robo_html_document_slot_t *robo_html_document_slot_alloc(void);

/** Mutation hook emission (RoboHTMLDom.c). */
void robo_html_emit_mutation(const robo_html_mutation_event_t *event,
                             void *userdata);

/** Client hook emission (RoboHTMLClient.c). */
void robo_html_emit_lifecycle(robo_html_document_slot_t *slot,
                              robo_html_document_lifecycle_t from,
                              robo_html_document_lifecycle_t to);
void robo_html_emit_invalidate(robo_html_document_slot_t *slot,
                               robo_html_invalidation_reason_t reason);

/** Lifecycle transition worker shared by document/parser/resource paths. */
robo_result_t robo_html_document_apply_transition(
    robo_html_document_slot_t *slot,
    robo_html_document_lifecycle_t to);

#endif /* ROBO_HTML_INTERNAL_H */
