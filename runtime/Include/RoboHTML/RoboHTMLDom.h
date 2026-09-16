/*
 * RoboHTML future-DOM boundary (Phase 8.1) — architecture stubs only.
 *
 * Roboticela's dependency plan places the full DOM (nodes, elements,
 * attributes, events, traversal, mutation) in its own module ("RoboDOM"
 * in the planned hierarchy). Until that module exists, this header fixes
 * the boundary RoboHTML exposes toward it, so the tokenizer (8.2) and
 * tree builder (8.3) can be written against a stable seam:
 *
 *   tokenizer/tree-builder  →  RoboHTML DOM-facing operations  →  future RoboDOM
 *
 * What is deliberately NOT here yet: element behavior, attribute storage,
 * live trees, event dispatch, MutationObserver. Those belong to later
 * phases. Only the stub vocabulary needed for compilation and testing
 * of the 8.1 architecture is defined.
 */

#ifndef ROBO_HTML_DOM_H
#define ROBO_HTML_DOM_H

#include "RoboBase/RoboError.h"
#include "RoboHTML/RoboHTMLDiagnostics.h"
#include "RoboHTML/RoboHTMLLifecycle.h"
#include "RoboHTML/RoboHTMLNode.h"
#include "RoboHTML/RoboHTMLTypes.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Creation site of a node — records which subsystem asked the document to
 * register a node. The tree builder (8.3) will use the DOCUMENT_BUILDER
 * origin; tests use TEST origin. Origins are diagnostic metadata only;
 * they never change ownership rules (the owning document always owns).
 */
typedef enum robo_html_node_origin {
    ROBO_HTML_NODE_ORIGIN_NONE = 0,
    ROBO_HTML_NODE_ORIGIN_DOCUMENT_ROOT = 1,
    ROBO_HTML_NODE_ORIGIN_PARSER = 2,
    ROBO_HTML_NODE_ORIGIN_DOM = 3,
    ROBO_HTML_NODE_ORIGIN_TEST = 4
} robo_html_node_origin_t;

/** Non-zero if @p origin is a defined node origin enumerator. */
int robo_html_node_origin_is_known(robo_html_node_origin_t origin);

/** Human-readable node origin name; never NULL. */
const char *robo_html_node_origin_name(robo_html_node_origin_t origin);

/**
 * Future-DOM capability flags exposed by the 8.1 architecture. All are 0
 * in this phase; each flips to 1 only in its own phase. Callers must not
 * assume any capability is available yet.
 */
typedef struct robo_html_dom_capabilities {
    /** Full node/element/attribute DOM available (DOM phase). */
    int dom_nodes;
    /**
     * The element attribute model is available (Phase 8.6): lookup,
     * presence, deterministic order, and add/set/remove on an element.
     * This is not a claim of a NamedNodeMap-compatible DOM surface.
     */
    int attributes;
    /**
     * The text-node model is available (Phase 8.7): real text nodes with
     * content access and set/append, created through one seam. This is not a
     * claim of a CharacterData-compatible DOM surface (no splitText, no
     * Range/Selection, no mutation observers).
     */
    int text_nodes;
    /** Traversal/mutation APIs available (DOM phase). */
    int mutation;
    /** Event model available (DOM events phase). */
    int events;
    /** Tree construction from parser callbacks (Phase 8.3). */
    int tree_building;
} robo_html_dom_capabilities_t;

/** Zero all capability flags (architecture: nothing available in 8.1). */
void robo_html_dom_capabilities_init(robo_html_dom_capabilities_t *caps);

/**
 * Query the DOM-facing capabilities of this runtime build.
 * @return ROBO_OK; ROBO_INVALID_ARGUMENT when @p out is NULL.
 */
robo_result_t robo_html_dom_query_capabilities(
    robo_html_dom_capabilities_t *out);

/**
 * Mutation/event hook foundation (architecture only).
 *
 * The future DOM mutation layer will call these hooks so applications and
 * later subsystems (style/layout invalidation) observe node registration
 * and document lifecycle changes. Phase 8.1 invokes the registered hook
 * for node registration only; dispatch semantics, targeting, and
 * propagation are later-phase work.
 *
 * Contract:
 *   - Exactly one hook may be installed document-wide at a time;
 *     installing over an existing hook returns ROBO_ALREADY_EXISTS.
 *   - The hook runs synchronously, after the node is registered and the
 *     document state is consistent.
 *   - Hook failures are recorded as diagnostics and do NOT roll back the
 *     triggering operation (registration remains authoritative).
 */
typedef enum robo_html_mutation_kind {
    ROBO_HTML_MUTATION_NONE = 0,
    ROBO_HTML_MUTATION_NODE_REGISTERED = 1,
    ROBO_HTML_MUTATION_DOCUMENT_LIFECYCLE = 2
} robo_html_mutation_kind_t;

/** Non-zero if @p kind is a defined mutation hook kind. */
int robo_html_mutation_kind_is_known(robo_html_mutation_kind_t kind);

typedef struct robo_html_mutation_event {
    robo_html_mutation_kind_t kind;
    robo_html_document_id_t document_id;
    robo_html_node_handle_t node; /* invalid for lifecycle events */
    robo_html_document_lifecycle_t lifecycle; /* for lifecycle events */
} robo_html_mutation_event_t;

typedef void (*robo_html_mutation_hook_fn)(const robo_html_mutation_event_t *event,
                                           void *userdata);

/**
 * Install the document-wide mutation hook (see contract above).
 * @p hook NULL clears. Over-install → ROBO_ALREADY_EXISTS (clear first).
 * Unknown document → ROBO_NOT_FOUND.
 */
robo_result_t robo_html_document_set_mutation_hook(
    robo_html_document_id_t document_id,
    robo_html_mutation_hook_fn hook,
    void *userdata,
    robo_html_diag_t *diag);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_HTML_DOM_H */
