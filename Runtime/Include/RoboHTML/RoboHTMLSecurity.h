/*
 * RoboHTML DOM security (Phase 17.10) — capabilities, posture, lifetime.
 *
 * The brief for this phase lists what the DOM must *not* be able to reach:
 * the filesystem, the kernel, raw devices, processes, native memory, arbitrary
 * host APIs and the network. The honest answer for RoboHTML is that most of
 * that is not a promise this module is keeping but a fact about its shape, and
 * the useful thing to ship is a surface where the fact is **readable and
 * tested** rather than asserted in prose:
 *
 *   HtmlSecurityPosture          what the DOM can reach, field by field:
 *                                filesystem 0, network 0, nativePointers 0,
 *                                processes 0, hardware 0, hostEnvironment 0,
 *                                bytesOnlyThroughSeam 1, hostOwnedDocument 1
 *
 *   HtmlDocumentCapabilities     what *this document* is allowed to do, so a
 *                                host that embeds untrusted markup can take a
 *                                door away rather than trusting a convention
 *
 * ## The capability model, and why it is opt-out
 *
 * A document an application created is fully capable: it may load resources,
 * navigate, define custom elements, attach shadow roots and mutate itself.
 * That is the default because it is what every existing caller expects — an
 * app rendering its own markup is not attacking itself.
 *
 * `HtmlDocumentSetCapabilities` narrows that set, and the doors that exercise a
 * capability refuse with **RoboPermissionDenied** (not NotFound, not
 * NotSupported: the operation exists and this document was not granted it —
 * a caller needs to be able to tell "not allowed" from "not implemented").
 *
 * The gates are deliberately at the *doors a document can reach*, not inside
 * the primitives a parser uses:
 *
 *   - structural DOM mutation is `HtmlNodeAppendChild` and friends. The tree
 *     builder links nodes through the construction path
 *     (`HtmlNodeLinkChild`), so **parsing a document is unaffected by the
 *     mutation capability** — a host can parse untrusted markup and refuse
 *     every later mutation of it, which is the shape the capability is for.
 *   - the image fetch seam is consulted only through
 *     `HtmlElementImageLoad` / `HtmlElementImageReplaceSource`.
 *   - navigation is `HtmlDocumentActivateLink` /
 *     `HtmlDocumentNavigateToFragment`.
 *   - custom element definition and shadow attachment are their own single
 *     doors.
 *
 * Everything else this module can do is *data*: an attribute named `onload`,
 * a `href` of `javascript:...` and a `src` of `file:///...` are bytes the DOM
 * stores and hands back verbatim, and none of them reaches a subsystem. That
 * is why the posture table says what it says: there is no host API here for a
 * hostile string to reach.
 *
 * ## Lifetime and ownership
 *
 * A document owns its nodes, and destroying it retires them: every handle
 * becomes stale at once, and every door that resolves one answers NotFound
 * rather than following a recycled id (the node registry's generation field is
 * what makes that true). `HtmlDocumentGetSecurityReport` is the machine's own
 * accounting of what a document owns right now — reachable nodes, detached
 * roots, image attachments and custom element definitions — so a lifetime test
 * can assert "nothing was left behind" instead of counting by hand.
 *
 * ## What this is NOT
 *
 * No sandbox, no process isolation, no origin or same-origin policy (there is
 * no origin in this model), no CSP, no Trusted Types, no URL sanitizer that
 * rewrites a document's own bytes, no `Content-Security-Policy` parsing, no
 * audit log, and no per-node ACL. A capability is granted per document and
 * refuses a door; it is not a substitute for the module boundaries that keep
 * the DOM away from the filesystem in the first place.
 */

#ifndef ROBO_HTML_SECURITY_H
#define ROBO_HTML_SECURITY_H

#include "RoboBase/RoboError.h"
#include "RoboHTML/RoboHTMLDiagnostics.h"
#include "RoboHTML/RoboHTMLTypes.h"

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------ */
/* Capabilities                                                        */
/* ------------------------------------------------------------------ */

/**
 * The doors a document's capability set controls.
 *
 * The enumerator order is the field order of `HtmlDocumentCapabilities`, and
 * `RoboHtmlCapabilityCount` is the count — so a table can be read by index and
 * a new capability is a field, an enumerator and one gate.
 */
typedef enum HtmlSecurityCapability {
    /** Structural mutation and attribute writes: the DOM edit surface. */
    RoboHtmlCapabilityDomMutation = 0,
    /** The image fetch seam: asking a resolver for bytes. */
    RoboHtmlCapabilityResourceLoading = 1,
    /** Link activation and same-document fragment navigation. */
    RoboHtmlCapabilityNavigation = 2,
    /** `customElements.define` for this document. */
    RoboHtmlCapabilityCustomElements = 3,
    /** `attachShadow`. */
    RoboHtmlCapabilityShadowDom = 4,
    RoboHtmlCapabilityCount = 5
} HtmlSecurityCapability;

/** Non-zero when @p capability is a defined enumerator. */
int HtmlSecurityCapabilityIsKnown(HtmlSecurityCapability capability);

/** Stable lowercase name of @p capability; never NULL, never allocated. */
const char *HtmlSecurityCapabilityName(HtmlSecurityCapability capability);

/** A document's capability set, one field per enumerator. */
typedef struct HtmlDocumentCapabilities {
    int domMutation;
    int resourceLoading;
    int navigation;
    int customElements;
    int shadowDom;
} HtmlDocumentCapabilities;

/** Every capability granted — the default for a document an app created. */
void HtmlDocumentCapabilitiesDefault(HtmlDocumentCapabilities *out);

/**
 * Narrow @p documentId's capability set.
 *
 * Every field must be 0 or 1; anything else is RoboInvalidArgument rather than
 * being read as truthy, because a caller that wrote `2` meant something and
 * guessing which is how a security switch becomes a no-op.
 *
 * @return RoboOk; RoboInvalidArgument (NULL out, or a field that is not 0/1);
 *         RoboNotFound (no such live document).
 */
Result HtmlDocumentSetCapabilities(HtmlDocumentId documentId,
                                   const HtmlDocumentCapabilities *caps,
                                   HtmlDiag *diag);

/**
 * Read @p documentId's capability set — the default when it was never set.
 *
 * @return RoboOk; RoboInvalidArgument (NULL out); RoboNotFound.
 */
Result HtmlDocumentGetCapabilities(HtmlDocumentId documentId,
                                   HtmlDocumentCapabilities *out,
                                   HtmlDiag *diag);

/**
 * Non-zero when @p documentId has granted @p capability.
 *
 * Answers the default (granted) for a document that never called
 * `HtmlDocumentSetCapabilities`, and 0 for a document that does not exist —
 * so an unknown document grants nothing, which is the safe direction.
 */
int HtmlDocumentCapabilityAllowed(HtmlDocumentId documentId,
                                  HtmlSecurityCapability capability);

/* ------------------------------------------------------------------ */
/* Posture                                                             */
/* ------------------------------------------------------------------ */

/**
 * What the DOM can reach, as a table.
 *
 * The first six are zero in this runtime and are the statement the phase
 * exists to make; the last two are one and say *how* the DOM gets what it
 * needs without any of the first six — bytes come through a caller-installed
 * resolver, and the document belongs to the caller's application.
 */
typedef struct HtmlSecurityPosture {
    int filesystem;        /* 0: RoboHTML opens no file */
    int network;           /* 0: no socket, no resolver of its own */
    int nativePointers;    /* 0: no door takes or returns a raw address */
    int processes;         /* 0: no spawn, no shell, no environment */
    int hardware;          /* 0: no device, no display in this module */
    int hostEnvironment;   /* 0: no host API surface exposed as data */
    int scriptExecution;   /* 0: RoboHTML evaluates no script at all */
    int eventListeners;    /* 0: no listener registry exists to abuse */
    int bytesOnlyThroughSeam; /* 1: an image's bytes come from a resolver */
    int hostOwnedDocument;    /* 1: a document is the app's, not the DOM's */
    const char *version;      /* RoboHTML's version string */
} HtmlSecurityPosture;

/** Fill @p out. RoboInvalidArgument on NULL. */
Result HtmlSecurityGetPosture(HtmlSecurityPosture *out);

/* ------------------------------------------------------------------ */
/* Lifetime report                                                     */
/* ------------------------------------------------------------------ */

/**
 * What a document owns right now, counted by the model itself.
 *
 * Every field is read from the door that owns it — `HtmlDocumentValidateTree`
 * for the node counts, `HtmlDocumentImageCount` for attachments, the custom
 * element registry for definitions — so a lifetime test asserts against the
 * runtime's own accounting rather than re-walking and counting by hand.
 */
typedef struct HtmlSecurityReport {
    /** Document id the report describes. */
    HtmlDocumentId documentId;
    /** Nodes the audit reached from the document root. */
    size_t reachableNodes;
    /** Detached roots (a subtree with no parent) the audit found. */
    size_t detachedRoots;
    /** Tree-version at the time of the report. */
    uint32_t treeVersion;
    /** Image resources attached to elements. */
    size_t imageAttachments;
    /** Custom element definitions. */
    size_t customElementDefinitions;
    /** The capability set in force. */
    HtmlDocumentCapabilities capabilities;
    /** Non-zero when the whole-tree invariant audit was clean. */
    int treeIntact;
} HtmlSecurityReport;

/**
 * Build the report for @p documentId.
 *
 * @return RoboOk (including for a document whose tree audit found detached
 *         subtrees, which are legal); RoboInvalidArgument (NULL @p out);
 *         RoboNotFound (no such live document).
 */
Result HtmlDocumentGetSecurityReport(HtmlDocumentId documentId,
                                     HtmlSecurityReport *out,
                                     HtmlDiag *diag);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_HTML_SECURITY_H */
