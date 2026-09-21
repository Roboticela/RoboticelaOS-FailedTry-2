/*
 * RoboHTML node identity, tree shape, and payload access (Phase 8.1 + 8.3).
 *
 * Identity
 *   - Node identity is (owning document id, node id, generation).
 *   - The generation is the guard, not the id: node ids are dense and
 *     restart when a document's tree is cleared (see
 *     HtmlDocumentClearTree / reset), so a handle copied by an
 *     application from a discarded tree resolves as stale instead of
 *     aliasing whatever node now occupies that id.
 *   - Ids restart dense so that id == registry slot + 1 keeps holding; that
 *     correspondence is what makes registration and lookup linear however
 *     many times a document is re-parsed.
 *
 * Ownership
 *   - The owning document exclusively owns every registered node.
 *   - Nodes have NO reference counting and NO external owner.
 *   - Application code only ever holds HtmlNodeHandle values,
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
 *   - HtmlNodeHandleResolve returns RoboNotFound for unknown ids
 *     and for handles whose generation no longer matches; callers that care
 *     distinguish the two via HtmlNodeHandleIsStale, which is true
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
typedef struct HtmlNodeHandle {
    HtmlDocumentId documentId;
    HtmlNodeId nodeId;
    uint32_t generation;
} HtmlNodeHandle;

/** Clear @p h to the invalid handle. Safe on NULL. */
void HtmlNodeHandleClear(HtmlNodeHandle *h);

/**
 * Owning document of @p h (RoboHtmlDocumentIdNone for NULL / an
 * ill-formed handle). This is the identity companion of the handle: it
 * lets a caller ask "whose node is this?" without resolving it, and it is
 * what makes a cross-document mistake detectable up front — a handle from
 * document A can never be valid in document B, because the check compares
 * this field against B's id before anything else.
 *
 * Note: the field is what the *handle claims*, not proof of ownership. Use
 * HtmlDocumentOwnsNode (RoboHTMLDocument.h) to check both the
 * claim and the live registry in one call.
 */
HtmlDocumentId HtmlNodeHandleDocument(
    const HtmlNodeHandle *h);

/** Non-zero when all three identity fields are non-zero. */
int HtmlNodeHandleIsWellformed(const HtmlNodeHandle *h);

/** Non-zero when both handles carry identical identity fields. */
int HtmlNodeHandleEqual(const HtmlNodeHandle *a,
                                const HtmlNodeHandle *b);

/**
 * Resolve @p h against the live registry.
 * @p outKind is optional: passing NULL performs a pure validation.
 * @return RoboOk (and *outKind when requested) for a live node;
 *         RoboInvalidArgument for a NULL or ill-formed handle;
 *         RoboNotFound for unknown document, unknown node id,
 *         or a stale generation (check HtmlNodeHandleIsStale
 *         to distinguish stale from unknown).
 */
Result HtmlNodeHandleResolve(
    const HtmlNodeHandle *h,
    HtmlNodeKind *outKind);

/**
 * Non-zero when @p h is well-formed but does not resolve to a live node
 * (unknown document, retired document, unknown id, or stale generation).
 */
int HtmlNodeHandleIsStale(const HtmlNodeHandle *h);

/**
 * Registry entry (private layout, public for embedding in the document
 * struct). Treat fields as opaque outside RoboHTML sources.
 *
 * Tree links are stored as node ids rather than pointers so that registry
 * storage may move and stale links are impossible to dereference.
 */
typedef struct HtmlNodeEntry {
    int inUse;
    HtmlNodeId id;
    uint32_t generation;
    HtmlNodeKind kind;
    /**
     * Creation site of the node, stored as a HtmlNodeOrigin value
     * (RoboHTMLDom.h). Recorded so element state can distinguish a node the
     * parser built from one the future DOM created — the kind alone cannot.
     */
    uint32_t origin;

    /* Tree links (RoboHtmlNodeIdNone = absent). */
    HtmlNodeId parent;
    HtmlNodeId firstChild;
    HtmlNodeId lastChild;
    HtmlNodeId prevSibling;
    HtmlNodeId NextSibling;

    /** Byte range of this node's payload in the document payload arena. */
    uint32_t payloadOffset;
    uint32_t payloadLength;
    /** Number of direct children (kept in step with the links). */
    uint32_t childCount;
    /** Element-only flags (see RoboHTMLInternal.h payload header). */
    uint32_t flags;
} HtmlNodeEntry;

/**
 * Node registry (fixed-capacity, document-owned). Layout is public so the
 * document struct can embed it; all mutation is internal to RoboHTML.
 */
typedef struct HtmlNodeRegistry {
    HtmlNodeEntry slots[RoboHtmlMaxNodesPerDocument];
    size_t used;
    HtmlNodeId nextId;
    uint32_t nextGeneration;
} HtmlNodeRegistry;

/** Reset registry to empty (no live nodes, fresh id/generation cursor). */
void HtmlNodeRegistryInit(HtmlNodeRegistry *reg);

/** Number of live nodes; 0 on NULL. */
size_t HtmlNodeRegistryCount(const HtmlNodeRegistry *reg);

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
typedef struct HtmlNodeView {
    HtmlNodeHandle handle;
    HtmlNodeKind kind;

    HtmlNodeHandle parent;       /* invalid when detached */
    HtmlNodeHandle firstChild;  /* invalid when childless */
    HtmlNodeHandle lastChild;
    HtmlNodeHandle prevSibling;
    HtmlNodeHandle NextSibling;
    size_t childCount;

    const char *name;      /* borrowed; "" when not applicable */
    const char *text;      /* borrowed; "" when not applicable */
    size_t textLength;    /* bytes, excluding the NUL */

    /** Element-only: non-zero when the source had "/>". */
    int selfClosing;
} HtmlNodeView;

/**
 * Fill @p out with the node's shape and borrowed payload pointers.
 * @return RoboOk; RoboInvalidArgument (NULL args / ill-formed handle);
 *         RoboNotFound (unknown document, unknown id, stale generation).
 */
Result HtmlNodeGetView(const HtmlNodeHandle *h,
                                      HtmlNodeView *out,
                                      HtmlDiag *diag);

/** Number of direct children; unknown node → RoboNotFound. */
Result HtmlNodeChildCount(const HtmlNodeHandle *h,
                                         size_t *outCount,
                                         HtmlDiag *diag);

/**
 * Handle of child @p index (0-based, document order).
 * Out of range → RoboNotFound (never a zeroed-but-successful handle).
 */
Result HtmlNodeChildAt(const HtmlNodeHandle *h,
                                      size_t index,
                                      HtmlNodeHandle *out,
                                      HtmlDiag *diag);

/** Handle of the first child whose name matches @p name (lowercase ASCII). */
Result HtmlNodeFindChildByName(
    const HtmlNodeHandle *h,
    const char *name,
    HtmlNodeHandle *out,
    HtmlDiag *diag);

/**
 * Depth-first search over @p h's subtree (excluding @p h itself) for the
 * first element whose name matches @p name. Bounded by the document's node
 * count, so a malformed tree cannot loop forever.
 */
Result HtmlNodeFindElement(const HtmlNodeHandle *h,
                                          const char *name,
                                          HtmlNodeHandle *out,
                                          HtmlDiag *diag);

/**
 * Concatenated text of a subtree in document order (text nodes only).
 *
 * @p out is NUL-terminated and truncated to @p outCap - 1 bytes;
 * @p outLength receives the *untruncated* byte length so callers can
 * detect truncation (outLength >= outCap means content was dropped).
 * @return RoboOk; RoboInvalidArgument for NULL args / zero capacity;
 *         RoboNotFound for an unknown node.
 */
Result HtmlNodeSubtreeText(const HtmlNodeHandle *h,
                                          char *out,
                                          size_t outCap,
                                          size_t *outLength,
                                          HtmlDiag *diag);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_HTML_NODE_H */
