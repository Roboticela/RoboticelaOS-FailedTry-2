/*
 * RoboHTML node identity, tree shape, and payload access (Phase 8.1 + 8.3).
 *
 * Identity
 *   - Node identity is (owning document id, node id, generation).
 *   - The generation is the guard, not the id: node ids are dense and
 *     restart when a document's tree is cleared (see
 *     robo_html_document_clear_tree / reset), so a handle copied by an
 *     application from a discarded tree resolves as stale instead of
 *     aliasing whatever node now occupies that id.
 *   - Ids restart dense so that id == registry slot + 1 keeps holding; that
 *     correspondence is what makes registration and lookup linear however
 *     many times a document is re-parsed.
 *
 * Ownership
 *   - The owning document exclusively owns every registered node.
 *   - Nodes have NO reference counting and NO external owner.
 *   - Application code only ever holds robo_html_node_handle_t values,
 *     which are inert tokens (safe to copy, compare, store). A handle is
 *     meaningful only while its node is live in its owning document.
 *   - Node payloads (element names, attributes, text) live in the owning
 *     document's payload arena, never in a separately owned allocation.
 *
 * Lifetime
 *   - The tree builder (8.3) creates nodes through document-scoped
 *     registration and links them; there is still no application-facing
 *     "create node" or "append child" here (DOM mutation belongs to the
 *     DOM phase). This header exposes the read side.
 *   - Destroying the document retires all node handles and frees the
 *     payload arena at once.
 *
 * Borrowed-data contract (important)
 *   - The accessors below return pointers INTO the document's payload
 *     arena. They are valid until the arena grows — i.e. until further tree
 *     building on that document — or until the document is destroyed.
 *   - After tree construction finishes the arena stops growing, so views
 *     taken then are stable for the document's lifetime.
 *   - Callers that must keep data copy it out.
 *
 * Stale handling
 *   - robo_html_node_handle_resolve returns ROBO_NOT_FOUND for unknown ids
 *     and for handles whose generation no longer matches; callers that care
 *     distinguish the two via robo_html_node_handle_is_stale, which is true
 *     for both (and for a handle whose owning document is gone).
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

/**
 * Owning document of @p h (ROBO_HTML_DOCUMENT_ID_NONE for NULL / an
 * ill-formed handle). This is the identity companion of the handle: it
 * lets a caller ask "whose node is this?" without resolving it, and it is
 * what makes a cross-document mistake detectable up front — a handle from
 * document A can never be valid in document B, because the check compares
 * this field against B's id before anything else.
 *
 * Note: the field is what the *handle claims*, not proof of ownership. Use
 * robo_html_document_owns_node (RoboHTMLDocument.h) to check both the
 * claim and the live registry in one call.
 */
robo_html_document_id_t robo_html_node_handle_document(
    const robo_html_node_handle_t *h);

/** Non-zero when all three identity fields are non-zero. */
int robo_html_node_handle_is_wellformed(const robo_html_node_handle_t *h);

/** Non-zero when both handles carry identical identity fields. */
int robo_html_node_handle_equal(const robo_html_node_handle_t *a,
                                const robo_html_node_handle_t *b);

/**
 * Resolve @p h against the live registry.
 * @p out_kind is optional: passing NULL performs a pure validation.
 * @return ROBO_OK (and *out_kind when requested) for a live node;
 *         ROBO_INVALID_ARGUMENT for a NULL or ill-formed handle;
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
 *
 * Tree links are stored as node ids rather than pointers so that registry
 * storage may move and stale links are impossible to dereference.
 */
typedef struct robo_html_node_entry {
    int in_use;
    robo_html_node_id_t id;
    uint32_t generation;
    robo_html_node_kind_t kind;
    /**
     * Creation site of the node, stored as a robo_html_node_origin_t value
     * (RoboHTMLDom.h). Recorded so element state can distinguish a node the
     * parser built from one the future DOM created — the kind alone cannot.
     */
    uint32_t origin;

    /* Tree links (ROBO_HTML_NODE_ID_NONE = absent). */
    robo_html_node_id_t parent;
    robo_html_node_id_t first_child;
    robo_html_node_id_t last_child;
    robo_html_node_id_t prev_sibling;
    robo_html_node_id_t next_sibling;

    /** Byte range of this node's payload in the document payload arena. */
    uint32_t payload_offset;
    uint32_t payload_length;
    /** Number of direct children (kept in step with the links). */
    uint32_t child_count;
    /** Element-only flags (see RoboHTMLInternal.h payload header). */
    uint32_t flags;
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

/* ------------------------------------------------------------------ */
/* Read-side tree access (Phase 8.3)                                   */
/* ------------------------------------------------------------------ */

/**
 * Snapshot of one node's shape and borrowed payload.
 *
 * @c name is the element/doctype name ("") for other kinds.
 * @c text is the text/comment data ("") for other kinds.
 * Attribute storage is element-specific — use RoboHTMLElement.h.
 */
typedef struct robo_html_node_view {
    robo_html_node_handle_t handle;
    robo_html_node_kind_t kind;

    robo_html_node_handle_t parent;       /* invalid when detached */
    robo_html_node_handle_t first_child;  /* invalid when childless */
    robo_html_node_handle_t last_child;
    robo_html_node_handle_t prev_sibling;
    robo_html_node_handle_t next_sibling;
    size_t child_count;

    const char *name;      /* borrowed; "" when not applicable */
    const char *text;      /* borrowed; "" when not applicable */
    size_t text_length;    /* bytes, excluding the NUL */

    /** Element-only: non-zero when the source had "/>". */
    int self_closing;
} robo_html_node_view_t;

/**
 * Fill @p out with the node's shape and borrowed payload pointers.
 * @return ROBO_OK; ROBO_INVALID_ARGUMENT (NULL args / ill-formed handle);
 *         ROBO_NOT_FOUND (unknown document, unknown id, stale generation).
 */
robo_result_t robo_html_node_get_view(const robo_html_node_handle_t *h,
                                      robo_html_node_view_t *out,
                                      robo_html_diag_t *diag);

/** Number of direct children; unknown node → ROBO_NOT_FOUND. */
robo_result_t robo_html_node_child_count(const robo_html_node_handle_t *h,
                                         size_t *out_count,
                                         robo_html_diag_t *diag);

/**
 * Handle of child @p index (0-based, document order).
 * Out of range → ROBO_NOT_FOUND (never a zeroed-but-successful handle).
 */
robo_result_t robo_html_node_child_at(const robo_html_node_handle_t *h,
                                      size_t index,
                                      robo_html_node_handle_t *out,
                                      robo_html_diag_t *diag);

/** Handle of the first child whose name matches @p name (lowercase ASCII). */
robo_result_t robo_html_node_find_child_by_name(
    const robo_html_node_handle_t *h,
    const char *name,
    robo_html_node_handle_t *out,
    robo_html_diag_t *diag);

/**
 * Depth-first search over @p h's subtree (excluding @p h itself) for the
 * first element whose name matches @p name. Bounded by the document's node
 * count, so a malformed tree cannot loop forever.
 */
robo_result_t robo_html_node_find_element(const robo_html_node_handle_t *h,
                                          const char *name,
                                          robo_html_node_handle_t *out,
                                          robo_html_diag_t *diag);

/**
 * Concatenated text of a subtree in document order (text nodes only).
 *
 * @p out is NUL-terminated and truncated to @p out_cap - 1 bytes;
 * @p out_length receives the *untruncated* byte length so callers can
 * detect truncation (out_length >= out_cap means content was dropped).
 * @return ROBO_OK; ROBO_INVALID_ARGUMENT for NULL args / zero capacity;
 *         ROBO_NOT_FOUND for an unknown node.
 */
robo_result_t robo_html_node_subtree_text(const robo_html_node_handle_t *h,
                                          char *out,
                                          size_t out_cap,
                                          size_t *out_length,
                                          robo_html_diag_t *diag);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_HTML_NODE_H */
