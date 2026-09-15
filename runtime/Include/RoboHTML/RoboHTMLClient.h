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
typedef enum robo_html_invalidation_reason {
    ROBO_HTML_INVALIDATION_NONE = 0,
    ROBO_HTML_INVALIDATION_SOURCE = 1,
    ROBO_HTML_INVALIDATION_LIFECYCLE = 2,
    ROBO_HTML_INVALIDATION_PARSING = 3,
    ROBO_HTML_INVALIDATION_STYLE = 4,     /* future: RoboCSS */
    ROBO_HTML_INVALIDATION_LAYOUT = 5,    /* future: RoboLayout */
    ROBO_HTML_INVALIDATION_PAINT = 6      /* future: RoboPaint */
} robo_html_invalidation_reason_t;

/** Non-zero if @p reason is a defined invalidation reason enumerator. */
int robo_html_invalidation_reason_is_known(
    robo_html_invalidation_reason_t reason);

/** Human-readable invalidation reason name; never NULL. */
const char *robo_html_invalidation_reason_name(
    robo_html_invalidation_reason_t reason);

typedef struct robo_html_invalidation {
    robo_html_invalidation_reason_t reason;
    robo_html_document_id_t document_id;
} robo_html_invalidation_t;

/**
 * Render client hooks. All optional; uninstalled entries are NULL and
 * RoboHTML then skips the notification. Hooks run synchronously after the
 * triggering operation completes (document state consistent).
 *
 * NOTE: future phases will extend this struct with additional trailing
 * fields; existing fields keep position and meaning (append-only ABI).
 */
typedef struct robo_html_client_hooks {
    /** Called when document lifecycle changes (see RoboHTMLLifecycle.h). */
    void (*on_document_lifecycle)(robo_html_document_id_t document_id,
                                  robo_html_document_lifecycle_t from,
                                  robo_html_document_lifecycle_t to,
                                  void *userdata);

    /**
     * Called when the document needs re-processing (source attached,
     * parsing progressed, later: style/layout/paint dirty).
     * Roboticela document model: whole-document invalidation only in
     * early phases; region-based invalidation arrives with paint.
     */
    void (*on_invalidate)(robo_html_document_id_t document_id,
                          robo_html_invalidation_reason_t reason,
                          void *userdata);
} robo_html_client_hooks_t;

/**
 * Install client hooks for @p document (hooks copied by value).
 * @p hooks NULL clears existing hooks. Re-installing over existing
 * non-NULL hooks → ROBO_ALREADY_EXISTS (clear first).
 * Hook failures never roll back the triggering operation.
 */
robo_result_t robo_html_document_set_client(
    robo_html_document_id_t document_id,
    const robo_html_client_hooks_t *hooks,
    void *userdata,
    robo_html_diag_t *diag);

/** Non-zero when a client is installed on @p document. */
int robo_html_document_has_client(robo_html_document_id_t document_id);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_HTML_CLIENT_H */
