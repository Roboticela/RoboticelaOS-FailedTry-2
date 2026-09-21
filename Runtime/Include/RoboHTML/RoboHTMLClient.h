/*
 * RoboHTML client/render integration boundary (Phase 8.1) — architecture.
 *
 * Fixes how RoboHTML will connect to CSS, layout, paint, RoboGraphics,
 * RoboWindow, and the frame/render pipeline WITHOUT depending on them
 * (DEPENDENCIES.md: RoboHTML is a lower module; CSS/layout/paint sit above
 * it and consume the document through this boundary).
 *
 * Direction of calls (permanent):
 *
 *   CSS / Layout / Paint / RoboRender   (higher, future phases)
 *        │  pull document state via robo_html_client_* accessors
 *        │  push invalidation via hooks below
 *        ▼
 *   RoboHTML document
 *
 * RoboHTML never includes graphics/window headers and never draws. The
 * hooks below are the only surface higher layers get; each is optional
 * and application-installed.
 */

#ifndef ROBO_HTML_CLIENT_H
#define ROBO_HTML_CLIENT_H

#include "RoboBase/RoboError.h"
#include "RoboHTML/RoboHTMLDiagnostics.h"
#include "RoboHTML/RoboHTMLLifecycle.h"
#include "RoboHTML/RoboHTMLTypes.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Invalidation reasons a document can report to its client. Future
 * CSS/style/layout phases extend this list; RoboHTML core only emits
 * SOURCE / LIFECYCLE / PARSING today.
 */
typedef enum HtmlInvalidationReason {
    RoboHtmlInvalidationNone = 0,
    RoboHtmlInvalidationSource = 1,
    RoboHtmlInvalidationLifecycle = 2,
    RoboHtmlInvalidationParsing = 3,
    RoboHtmlInvalidationStyle = 4,     /* future: RoboCSS */
    RoboHtmlInvalidationLayout = 5,    /* future: RoboLayout */
    RoboHtmlInvalidationPaint = 6      /* future: RoboPaint */
} HtmlInvalidationReason;

/** Non-zero if @p reason is a defined invalidation reason enumerator. */
int HtmlInvalidationReasonIsKnown(
    HtmlInvalidationReason reason);

/** Human-readable invalidation reason name; never NULL. */
const char *HtmlInvalidationReasonName(
    HtmlInvalidationReason reason);

typedef struct HtmlInvalidation {
    HtmlInvalidationReason reason;
    HtmlDocumentId documentId;
} HtmlInvalidation;

/**
 * Render client hooks. All optional; uninstalled entries are NULL and
 * RoboHTML then skips the notification. Hooks run synchronously after the
 * triggering operation completes (document state consistent).
 *
 * NOTE: future phases will extend this struct with additional trailing
 * fields; existing fields keep position and meaning (append-only ABI).
 */
typedef struct HtmlClientHooks {
    /** Called when document lifecycle changes (see RoboHTMLLifecycle.h). */
    void (*OnDocumentLifecycle)(HtmlDocumentId documentId,
                                  HtmlDocumentLifecycle from,
                                  HtmlDocumentLifecycle to,
                                  void *userdata);

    /**
     * Called when the document needs re-processing (source attached,
     * parsing progressed, later: style/layout/paint dirty).
     * Roboticela document model: whole-document invalidation only in
     * early phases; region-based invalidation arrives with paint.
     */
    void (*OnInvalidate)(HtmlDocumentId documentId,
                          HtmlInvalidationReason reason,
                          void *userdata);
} HtmlClientHooks;

/**
 * Install client hooks for @p document (hooks copied by value).
 * @p hooks NULL clears existing hooks. Re-installing over existing
 * non-NULL hooks → RoboAlreadyExists (clear first).
 * Hook failures never roll back the triggering operation.
 */
Result HtmlDocumentSetClient(
    HtmlDocumentId documentId,
    const HtmlClientHooks *hooks,
    void *userdata,
    HtmlDiag *diag);

/** Non-zero when a client is installed on @p document. */
int HtmlDocumentHasClient(HtmlDocumentId documentId);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_HTML_CLIENT_H */
