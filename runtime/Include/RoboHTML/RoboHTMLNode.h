/*
 * RoboHTML node identity and lifetime model (Phase 8.1).
 *
 * Architecture only — the full DOM node system arrives in the DOM phase.
 * What 8.1 fixes permanently:
 *
 * Identity
 *   - Node identity is (owning document id, node id, generation).
 *   - Node ids are never reused within a document; generations guard the
 *     registry slot table so a stale handle copied by an application is
 *     detectable instead of aliasing a newer node.
 *
 * Ownership
 *   - The owning document exclusively owns every registered node.
 *   - Nodes have NO reference counting and NO external owner in 8.1.
 *   - Application code only ever holds robo_html_node_handle_t values,
 *     which are inert tokens (safe to copy, compare, store). A handle is
 *     meaningful only while its node is live in its owning document.
 *
 * Lifetime
 *   - Registration goes through the document (see RoboHTMLDocument.h);
 *     there is no public "create node" in 8.1 — the tree builder (8.3)
 *     will use the document-scoped registration entry points.
 *   - Destruction of the document retires all node handles at once.
 *     Per-node release arrives with the DOM phase, behind the document.
 *
 * Stale handling
 *   - robo_html_node_handle_resolve returns ROBO_NOT_FOUND for unknown ids
 *     and ROBO_NOT_FOUND + NODE domain diagnostics for stale generations —
 *     callers that care distinguish via robo_html_node_handle_is_stale.
 */

#ifndef ROBO_HTML_NODE_H
#define ROBO_HTML_NODE_H

#include "RoboBase/RoboError.h"
#include "RoboHTML/RoboHTMLDiagnostics.h"
#include "RoboHTML/RoboHTMLTypes.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Inert node handle (safe to copy/compare/store; no ownership).
 * A zeroed handle is always invalid. Generation must match the owning
 * document's registry entry for the handle to resolve.
 */
typedef struct robo_html_node_handle {
    robo_html_document_id_t document_id;
    robo_html_node_id_t node_id;
    uint32_t generation;
} robo_html_node_handle_t;

/** Clear @p h to the invalid handle. Safe on NULL. */
void robo_html_node_handle_clear(robo_html_node_handle_t *h);

/** Non-zero when all three identity fields are non-zero. */
int robo_html_node_handle_is_wellformed(const robo_html_node_handle_t *h);

/** Non-zero when both handles carry identical identity fields. */
int robo_html_node_handle_equal(const robo_html_node_handle_t *a,
                                const robo_html_node_handle_t *b);

/**
 * Resolve @p h against the live registry.
 * @return ROBO_OK and *out_kind when the node is live;
 *         ROBO_INVALID_ARGUMENT for NULL/ill-formed inputs;
 *         ROBO_NOT_FOUND for unknown document, unknown node id,
 *         or a stale generation (check robo_html_node_handle_is_stale
 *         to distinguish stale from unknown).
 */
robo_result_t robo_html_node_handle_resolve(
    const robo_html_node_handle_t *h,
    robo_html_node_kind_t *out_kind);

/**
 * Non-zero when @p h is well-formed but does not resolve to a live node
 * (unknown document, retired document, unknown id, or stale generation).
 */
int robo_html_node_handle_is_stale(const robo_html_node_handle_t *h);

/**
 * Registry entry (private layout, public for embedding in the document
 * struct). Treat fields as opaque outside RoboHTML sources.
 */
typedef struct robo_html_node_entry {
    int in_use;
    robo_html_node_id_t id;
    uint32_t generation;
    robo_html_node_kind_t kind;
} robo_html_node_entry_t;

/**
 * Node registry (fixed-capacity, document-owned). Layout is public so the
 * document struct can embed it; all mutation is internal to RoboHTML.
 */
typedef struct robo_html_node_registry {
    robo_html_node_entry_t slots[ROBO_HTML_MAX_NODES_PER_DOCUMENT];
    size_t used;
    robo_html_node_id_t next_id;
    uint32_t next_generation;
} robo_html_node_registry_t;

/** Reset registry to empty (no live nodes, fresh id/generation cursor). */
void robo_html_node_registry_init(robo_html_node_registry_t *reg);

/** Number of live nodes; 0 on NULL. */
size_t robo_html_node_registry_count(const robo_html_node_registry_t *reg);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_HTML_NODE_H */
