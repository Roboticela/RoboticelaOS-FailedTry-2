/*
 * RoboHTML fundamental identities and limits (Phase 8.1).
 *
 * Document and node identities are runtime-owned 64-bit tokens — never
 * pointers handed to applications, never OS handles, never Chromium types.
 *
 * Identity rules (mirrors RoboWindow/RoboDisplay token discipline):
 *   - 0 is always "none" and never a live identity.
 *   - Identity is generation-guarded: a handle for a recycled node slot is
 *     detectable through the document's registry, because the node's
 *     generation no longer matches.
 *   - Document ids are stable while the document exists and are never
 *     reused, so a destroyed document id stays dead.
 *   - Node ids are stable while the node exists. They are drawn densely
 *     from a per-document cursor that restarts when the tree is cleared,
 *     which is why the generation — not the id — is what makes a stale
 *     node handle detectable.
 *
 * RoboHTML may depend only on RoboBase, RoboMem, RoboContainers, and the
 * future RoboDOM boundary stubbed here (see DEPENDENCIES.md). It must never
 * depend on RoboCSS / layout / paint / JS / networking modules.
 */

#ifndef ROBO_HTML_TYPES_H
#define ROBO_HTML_TYPES_H

#include "RoboBase/RoboError.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Opaque document identity (stable while the document exists; 0 = none). */
typedef uint64_t robo_html_document_id_t;

#define ROBO_HTML_DOCUMENT_ID_NONE ((robo_html_document_id_t)0u)

/**
 * Opaque node identity (stable while the node lives in its owning
 * document; 0 = none). Valid only together with its owning document.
 */
typedef uint64_t robo_html_node_id_t;

#define ROBO_HTML_NODE_ID_NONE ((robo_html_node_id_t)0u)

/** Soft ceiling for simultaneous live documents in Phase 8.1. */
#define ROBO_HTML_MAX_DOCUMENTS 8u

/**
 * Soft ceiling on tracked nodes per document in Phase 8.1.
 * This bounds the identity registry only — it is not a tree limit; the
 * tree itself is constructed by later phases (8.2 / 8.3).
 */
#define ROBO_HTML_MAX_NODES_PER_DOCUMENT 4096u

/** Maximum length of a document URL / name label (copied). */
#define ROBO_HTML_URL_MAX 512u

/** Maximum length of an encoding label (copied; e.g. "UTF-8"). */
#define ROBO_HTML_ENCODING_LABEL_MAX 64u

/**
 * Node kind taxonomy (architecture stub — full DOM semantics arrive with
 * the DOM phase). Kind is recorded at node registration time so the
 * document can reason about lifetime before element/text behavior exists.
 */
typedef enum robo_html_node_kind {
    ROBO_HTML_NODE_KIND_DOCUMENT = 0,
    ROBO_HTML_NODE_KIND_ELEMENT = 1,
    ROBO_HTML_NODE_KIND_TEXT = 2,
    ROBO_HTML_NODE_KIND_COMMENT = 3,
    ROBO_HTML_NODE_KIND_DOCUMENT_TYPE = 4,
    ROBO_HTML_NODE_KIND_FRAGMENT = 5
} robo_html_node_kind_t;

/** Number of defined node kinds; the length of a per-kind count array. */
#define ROBO_HTML_NODE_KIND_COUNT 6u

/** Non-zero if @p kind is a defined node kind enumerator. */
int robo_html_node_kind_is_known(robo_html_node_kind_t kind);

/** Human-readable node kind name; never NULL; "unknown" when undefined. */
const char *robo_html_node_kind_name(robo_html_node_kind_t kind);

/* ------------------------------------------------------------------ */
/* Element namespaces (Phase 8.5)                                      */
/* ------------------------------------------------------------------ */

/**
 * Element platform namespace.
 *
 * HTML is not the only namespace the HTML parser has to reach: <svg> and
 * <math> are switches into foreign content, so the enumerator exists now and
 * the element model stores one — a later foreign-content phase then adds
 * behaviour rather than a type. Only @c HTML is produced in this phase; the
 * tree builder never switches namespace yet.
 */
typedef enum robo_html_namespace {
    ROBO_HTML_NAMESPACE_NONE = 0,
    ROBO_HTML_NAMESPACE_HTML = 1,
    ROBO_HTML_NAMESPACE_SVG = 2,
    ROBO_HTML_NAMESPACE_MATHML = 3
} robo_html_namespace_t;

/** Number of defined namespaces. */
#define ROBO_HTML_NAMESPACE_COUNT 4u

/** Non-zero if @p ns is a defined namespace enumerator. */
int robo_html_namespace_is_known(robo_html_namespace_t ns);

/** Human-readable namespace name ("html", "svg", "mathml"); never NULL. */
const char *robo_html_namespace_name(robo_html_namespace_t ns);

/**
 * Namespace URI of @p ns; "" for NONE and for unknown values.
 *
 * The URI is the identity the future DOM/CSS/selector layers compare
 * against. It is deliberately a constant per namespace rather than stored
 * bytes on every element: constants here, comparisons later, no per-node
 * copy of a 28-byte string.
 */
const char *robo_html_namespace_url(robo_html_namespace_t ns);

/**
 * HTML-specific diagnostic domains. These refine the shared robo_result_t
 * codes for RoboHTML callers without creating a parallel error framework:
 * callers keep receiving plain robo_result_t; the domain records which
 * RoboHTML subsystem rejected the operation (see RoboHTMLDiagnostics.h).
 */
typedef enum robo_html_diagnostic_domain {
    ROBO_HTML_DOMAIN_NONE = 0,
    ROBO_HTML_DOMAIN_DOCUMENT = 1,
    ROBO_HTML_DOMAIN_LIFECYCLE = 2,
    ROBO_HTML_DOMAIN_NODE = 3,
    ROBO_HTML_DOMAIN_SOURCE = 4,
    ROBO_HTML_DOMAIN_ENCODING = 5,
    ROBO_HTML_DOMAIN_PARSER = 6,
    ROBO_HTML_DOMAIN_RESOURCE = 7,
    ROBO_HTML_DOMAIN_CLIENT = 8,
    ROBO_HTML_DOMAIN_MUTATION = 9,
    ROBO_HTML_DOMAIN_RENDER = 10,
    /**
     * Document-model/tree-invariant domain (Phase 8.4): invalid node
     * ownership, invalid root, illegal construction after finalization,
     * and broken parent/child/sibling links reported by
     * robo_html_document_validate_tree.
     */
    ROBO_HTML_DOMAIN_TREE = 11,
    /**
     * Element-model domain (Phase 8.5): element creation rejection (empty
     * or unusable local name, unknown namespace), a handle that is not an
     * element, an invalid parent for creation, and element access on a node
     * whose kind is wrong.
     */
    ROBO_HTML_DOMAIN_ELEMENT = 12,
    /**
     * Text-node domain (Phase 8.7): text creation rejection (invalid
     * parent for creation, unknown origin), a handle that is not text, and
     * text content/mutation requested on a non-text node.
     */
    ROBO_HTML_DOMAIN_TEXT = 13,
    /**
     * Traversal domain (Phase 8.8): a walk or search started from a handle
     * that does not resolve, an unusable visitor/predicate/output, and a
     * link graph that is not a tree (the node ceiling was reached during a
     * walk, which means a cycle or a corrupted link).
     */
    ROBO_HTML_DOMAIN_TRAVERSAL = 14
} robo_html_diagnostic_domain_t;

/** Non-zero if @p domain is a defined diagnostic domain enumerator. */
int robo_html_diagnostic_domain_is_known(robo_html_diagnostic_domain_t domain);

/** Human-readable domain name; never NULL; "unknown" when undefined. */
const char *robo_html_diagnostic_domain_name(robo_html_diagnostic_domain_t domain);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_HTML_TYPES_H */
