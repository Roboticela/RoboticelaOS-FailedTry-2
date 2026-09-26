/*
 * RoboHTML navigation controller (Phase 17.4).
 *
 * Phase 8.13 gave one anchor an activation with a caller-supplied handler.
 * This header is the *document-level* half the brief asks for: the controller
 * that owns the handler for a document, the current location (route and
 * fragment), the `href`/`target` readers on an element, and the document-load
 * step a route navigation ends in.
 *
 * ## The pipeline, and where each part lives
 *
 *   link activation   HtmlDocumentActivateLink
 *       ↓             (or HtmlDocumentNavigateToFragment for `#id`)
 *   target resolution Phase 8.13's HtmlNavigationTarget
 *       ↓
 *   controller        the document's installed HtmlNavigationHandler
 *       ↓
 *   document load     HtmlDocumentLoadSource — parses new source into the
 *                     *same* document through the real tree builder
 *
 * Nothing fetches. A route the handler accepts updates the controller's
 * location; a caller that wants new content parses it with
 * `HtmlDocumentLoadSource`, which is the one place a navigation's bytes reach
 * a tree. `javascript:`, `data:` and `file:` are still refused by Phase 8.13's
 * scheme policy before the handler is consulted, so this header adds no policy
 * of its own and no bypass.
 *
 * ## `preventDefault` has no event here yet
 *
 * This build has no DOM event system, so a `click` event cannot be cancelled.
 * The equivalent, and what a caller uses today, is the handler **declining**:
 * a handler that returns non-RoboOk downgrades the outcome to UNSUPPORTED and
 * performs no navigation, so "the document said no" is expressible and tested.
 * A future event phase routes its preventDefault through the same seam rather
 * than a second one.
 */

#ifndef ROBO_HTML_NAVIGATION_H
#define ROBO_HTML_NAVIGATION_H

#include "RoboBase/RoboError.h"
#include "RoboHTML/RoboHTMLAnchor.h"
#include "RoboHTML/RoboHTMLDiagnostics.h"
#include "RoboHTML/RoboHTMLNode.h"
#include "RoboHTML/RoboHTMLSource.h"
#include "RoboHTML/RoboHTMLTypes.h"

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Maximum fragment bytes this controller remembers. */
#define RoboHtmlNavigationFragmentMax 256u

/**
 * The document's current location.
 *
 * `route` is the resolved internal route or an accepted external URI;
 * `fragment` is the current `#id` when the last navigation was same-document.
 * Both are owned copies, so a caller can read them after the navigation's
 * outcome is cleared.
 */
typedef struct HtmlNavigationLocation {
    char route[RoboHtmlUrlMax];
    size_t routeLength;
    char fragment[RoboHtmlNavigationFragmentMax];
    size_t fragmentLength;
    /** Completed navigations (fragment or routed) since the document began. */
    size_t navigations;
} HtmlNavigationLocation;

/** `element.href` — the declared `href` (not resolved). */
Result HtmlElementHref(
    const HtmlNodeHandle *element,
    char *out,
    size_t outCap,
    size_t *outLength,
    HtmlDiag *diag);

/** `element.target` — the declared `target` ("" when absent). */
Result HtmlElementTarget(
    const HtmlNodeHandle *element,
    char *out,
    size_t outCap,
    size_t *outLength,
    HtmlDiag *diag);

/**
 * Install (or clear, with NULL) the document's navigation handler. The
 * controller copies the two fields; the handler owns nothing here.
 */
Result HtmlDocumentSetNavigationHandler(
    HtmlDocumentId documentId,
    const HtmlNavigationHandler *handler,
    HtmlDiag *diag);

/** Non-zero when a usable handler is installed for @p documentId. */
int HtmlDocumentHasNavigationHandler(HtmlDocumentId documentId);

/**
 * Activate @p anchor using the document's installed handler, and update the
 * controller's location for the outcomes that represent navigation.
 *
 * @p out owns its target; clear it with HtmlNavigationOutcomeClear.
 *
 * @return as HtmlElementActivate.
 */
Result HtmlDocumentActivateLink(
    const HtmlNodeHandle *anchor,
    HtmlNavigationOutcome *out,
    HtmlDiag *diag);

/**
 * Same-document navigation: resolve @p fragment to an element in this
 * document and record it as the current location.
 *
 * @return RoboOk with an outcome whose kind is FRAGMENT or
 *         FRAGMENT_MISSING (the latter is still a recorded attempt);
 *         RoboInvalidArgument; RoboNotFound (no such document).
 */
Result HtmlDocumentNavigateToFragment(
    HtmlDocumentId documentId,
    const char *fragment,
    size_t fragmentLength,
    HtmlNavigationOutcome *out,
    HtmlDiag *diag);

/** Read the document's current location. RoboNotFound for a dead document. */
Result HtmlDocumentCurrentLocation(
    HtmlDocumentId documentId,
    HtmlNavigationLocation *out,
    HtmlDiag *diag);

/**
 * The document-load step: parse @p source into @p documentId through the real
 * tokenizer and tree builder, replacing the document's tree.
 *
 * This is where a navigation's bytes become a tree. It performs no I/O — the
 * caller owns the bytes — and it is deliberately the *only* door here that
 * touches content, so "navigation loaded something" can always be pointed at
 * one call.
 *
 * @return as HtmlBuildTree.
 */
Result HtmlDocumentLoadSource(
    HtmlDocumentId documentId,
    const HtmlSource *source,
    HtmlDiag *diag);

/** Capability table for this surface. */
typedef struct HtmlNavigationCapabilities {
    int elementHref;        /* href / target readers */
    int controller;         /* document-scoped handler */
    int fragmentNavigation; /* same-document `#id` */
    int locationState;      /* current route/fragment */
    int documentLoad;       /* source → tree */
    int preventDefault;     /* 0: no event system yet; a handler declines */
    int networking;         /* 0: this module never fetches */
} HtmlNavigationCapabilities;

/** Fill @p out. RoboInvalidArgument on NULL. */
Result HtmlNavigationGetCapabilities(HtmlNavigationCapabilities *out);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_HTML_NAVIGATION_H */
