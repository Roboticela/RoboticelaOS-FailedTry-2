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
typedef uint64_t HtmlDocumentId;

#define RoboHtmlDocumentIdNone ((HtmlDocumentId)0u)

/**
 * Opaque node identity (stable while the node lives in its owning
 * document; 0 = none). Valid only together with its owning document.
 */
typedef uint64_t HtmlNodeId;

#define RoboHtmlNodeIdNone ((HtmlNodeId)0u)

/** Soft ceiling for simultaneous live documents in Phase 8.1. */
#define RoboHtmlMaxDocuments 8u

/**
 * Soft ceiling on tracked nodes per document in Phase 8.1.
 * This bounds the identity registry only — it is not a tree limit; the
 * tree itself is constructed by later phases (8.2 / 8.3).
 */
#define RoboHtmlMaxNodesPerDocument 4096u

/** Maximum length of a document URL / name label (copied). */
#define RoboHtmlUrlMax 512u

/** Maximum length of an encoding label (copied; e.g. "UTF-8"). */
#define RoboHtmlEncodingLabelMax 64u

/**
 * Node kind taxonomy (architecture stub — full DOM semantics arrive with
 * the DOM phase). Kind is recorded at node registration time so the
 * document can reason about lifetime before element/text behavior exists.
 */
typedef enum HtmlNodeKind {
    RoboHtmlNodeKindDocument = 0,
    RoboHtmlNodeKindElement = 1,
    RoboHtmlNodeKindText = 2,
    RoboHtmlNodeKindComment = 3,
    RoboHtmlNodeKindDocumentType = 4,
    RoboHtmlNodeKindFragment = 5
} HtmlNodeKind;

/** Number of defined node kinds; the length of a per-kind count array. */
#define RoboHtmlNodeKindCount 6u

/** Non-zero if @p kind is a defined node kind enumerator. */
int HtmlNodeKindIsKnown(HtmlNodeKind kind);

/** Human-readable node kind name; never NULL; "unknown" when undefined. */
const char *HtmlNodeKindName(HtmlNodeKind kind);

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
typedef enum HtmlNamespace {
    RoboHtmlNamespaceNone = 0,
    RoboHtmlNamespaceHtml = 1,
    RoboHtmlNamespaceSvg = 2,
    RoboHtmlNamespaceMathml = 3
} HtmlNamespace;

/** Number of defined namespaces. */
#define RoboHtmlNamespaceCount 4u

/** Non-zero if @p ns is a defined namespace enumerator. */
int HtmlNamespaceIsKnown(HtmlNamespace ns);

/** Human-readable namespace name ("html", "svg", "mathml"); never NULL. */
const char *HtmlNamespaceName(HtmlNamespace ns);

/**
 * Namespace URI of @p ns; "" for NONE and for unknown values.
 *
 * The URI is the identity the future DOM/CSS/selector layers compare
 * against. It is deliberately a constant per namespace rather than stored
 * bytes on every element: constants here, comparisons later, no per-node
 * copy of a 28-byte string.
 */
const char *HtmlNamespaceUrl(HtmlNamespace ns);

/* ------------------------------------------------------------------ */
/* Element role vocabulary (Phase 8.5; moved here in Phase 8.11)        */
/* ------------------------------------------------------------------ */

/**
 * Coarse role an element plays.
 *
 * This is deliberately NOT the HTML content-category specification: it is
 * the small vocabulary a renderer or a later phase needs to make one
 * decision without a string table of its own. It is a pure function of the
 * element's identity, so classifying costs no storage and no lookup, and an
 * unrecognised element is @c GENERIC rather than an error — which is what
 * makes custom elements, SVG and future elements safe to classify.
 *
 * Since Phase 8.11 the role is a *column of the tag vocabulary* rather than
 * a table of its own: @c HtmlElementClassify and
 * @c HtmlTagClass are the same answer, so a role and a tag can no
 * longer disagree about what an element is. Full element identity — which
 * tag, is it a heading, is it void — is @c HtmlTag in RoboHTMLTag.h.
 *
 * The vocabulary lives in this header, next to the node kinds and the
 * namespaces, because an identity vocabulary is what this header is for: it
 * is included wherever an element is named, and it must be visible to the
 * tag layer (which carries a role per tag) without either header needing the
 * other.
 */
typedef enum HtmlElementClass {
    /** Not a known element name in this namespace. */
    RoboHtmlElementClassGeneric = 0,
    /** The document element, <html>. */
    RoboHtmlElementClassRoot = 1,
    /** Document metadata: <head>, <title>, <meta>, <link>, <script>, ... */
    RoboHtmlElementClassMetadata = 2,
    /** Block containers: <body>, <div>, <section>, <li>, ... */
    RoboHtmlElementClassSection = 3,
    /** <h1> … <h6>. */
    RoboHtmlElementClassHeading = 4,
    /** <p>. */
    RoboHtmlElementClassParagraph = 5,
    /** Text-level elements: <span>, <a>, <em>, <code>, <br>, ... */
    RoboHtmlElementClassText = 6,
    /** Replaced/embedded content: <img>, <video>, <canvas>, <iframe>, ... */
    RoboHtmlElementClassReplaced = 7,
    /** Form controls: <button>, <input>, <select>, <form>, ... */
    RoboHtmlElementClassControl = 8,
    /** Number of defined classes. */
    RoboHtmlElementClassCount = 9
} HtmlElementClass;

/** Non-zero if @p cls is a defined element class enumerator. */
int HtmlElementClassIsKnown(HtmlElementClass cls);

/** Human-readable class name; never NULL; "unknown" when undefined. */
const char *HtmlElementClassName(HtmlElementClass cls);

/**
 * Classify @p localName in @p ns. @p localName must already be lowercase
 * (stored names are), but the comparison is ASCII case-insensitive so a
 * caller holding a name from elsewhere still gets the right answer.
 * Anything unrecognised — including every name in a non-HTML namespace —
 * is @c GENERIC, never an error.
 */
HtmlElementClass HtmlElementClassify(
    HtmlNamespace ns,
    const char *localName);

/**
 * HTML-specific diagnostic domains. These refine the shared Result
 * codes for RoboHTML callers without creating a parallel error framework:
 * callers keep receiving plain Result; the domain records which
 * RoboHTML subsystem rejected the operation (see RoboHTMLDiagnostics.h).
 */
typedef enum HtmlDiagnosticDomain {
    RoboHtmlDomainNone = 0,
    RoboHtmlDomainDocument = 1,
    RoboHtmlDomainLifecycle = 2,
    RoboHtmlDomainNode = 3,
    RoboHtmlDomainSource = 4,
    RoboHtmlDomainEncoding = 5,
    RoboHtmlDomainParser = 6,
    RoboHtmlDomainResource = 7,
    RoboHtmlDomainClient = 8,
    RoboHtmlDomainMutation = 9,
    RoboHtmlDomainRender = 10,
    /**
     * Document-model/tree-invariant domain (Phase 8.4): invalid node
     * ownership, invalid root, illegal construction after finalization,
     * and broken parent/child/sibling links reported by
     * HtmlDocumentValidateTree.
     */
    RoboHtmlDomainTree = 11,
    /**
     * Element-model domain (Phase 8.5): element creation rejection (empty
     * or unusable local name, unknown namespace), a handle that is not an
     * element, an invalid parent for creation, and element access on a node
     * whose kind is wrong.
     */
    RoboHtmlDomainElement = 12,
    /**
     * Text-node domain (Phase 8.7): text creation rejection (invalid
     * parent for creation, unknown origin), a handle that is not text, and
     * text content/mutation requested on a non-text node.
     */
    RoboHtmlDomainText = 13,
    /**
     * Traversal domain (Phase 8.8): a walk or search started from a handle
     * that does not resolve, an unusable visitor/predicate/output, and a
     * link graph that is not a tree (the node ceiling was reached during a
     * walk, which means a cycle or a corrupted link).
     */
    RoboHtmlDomainTraversal = 14,
    /**
     * Image domain (Phase 8.12): reading an `<img>`'s attributes, refusing a
     * reference this runtime will not fetch, and reporting bytes that are not
     * a decodable image. A resource *outcome* — the image that failed to
     * load — is not a diagnostic; this domain carries the reason attached to
     * one.
     */
    RoboHtmlDomainImage = 15,
    /**
     * Anchor domain (Phase 8.13): reading an `<a>`'s attributes, refusing a
     * reference this runtime will not resolve, and refusing a scheme it will
     * not hand to a navigation handler. A navigation *outcome* — no target,
     * a missing fragment, a refused scheme — is not a diagnostic; this domain
     * carries the reason attached to a call that could not be attempted at
     * all.
     */
    RoboHtmlDomainAnchor = 16,
    /**
     * Forms domain (Phase 8.14): reading a form/control's attributes,
     * refusing a control operation on an element that is not a control,
     * and reporting a form-owner or label-target reference that names
     * nothing usable. "This input has no form owner" is *not* a diagnostic —
     * a control outside any form is an ordinary, valid document — so this
     * domain carries the reason attached to a call that could not be made at
     * all.
     */
    RoboHtmlDomainForms = 17
} HtmlDiagnosticDomain;

/** Non-zero if @p domain is a defined diagnostic domain enumerator. */
int HtmlDiagnosticDomainIsKnown(HtmlDiagnosticDomain domain);

/** Human-readable domain name; never NULL; "unknown" when undefined. */
const char *HtmlDiagnosticDomainName(HtmlDiagnosticDomain domain);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_HTML_TYPES_H */
