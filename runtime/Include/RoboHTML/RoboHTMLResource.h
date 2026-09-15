/*
 * RoboHTML resource boundary (Phase 8.1) — architecture only.
 *
 * Separation of concerns frozen by this boundary:
 *
 *   1. Source acquisition — application/RoboNetwork fetches bytes.
 *   2. Encoding            — bytes become decoded text (encoding phase).
 *   3. Parsing             — decoded text becomes tree (8.2 / 8.3).
 *   4. Document lifecycle  — RoboHTML tracks READY / FAILED states.
 *
 * RoboHTML NEVER performs network I/O. A future loader (RoboNetwork or an
 * application) acquires bytes, wraps them in a robo_html_source_t, and
 * attaches them to a document. The loader receives lifecycle/mutation
 * notifications through the hooks already defined (RoboHTMLDom.h) and the
 * client boundary (RoboHTMLClient.h).
 *
 * This header defines only the request/cancel vocabulary the future
 * loader integration will call, plus the document-side loader slot, so
 * later phases do not redesign document ownership.
 *
 * No HTTP, DNS, TLS, fetch, caching, cookies, or CORS here — Roboticela
 * applications acquire bytes through their own capability model.
 */

#ifndef ROBO_HTML_RESOURCE_H
#define ROBO_HTML_RESOURCE_H

#include "RoboBase/RoboError.h"
#include "RoboHTML/RoboHTMLDiagnostics.h"
#include "RoboHTML/RoboHTMLTypes.h"

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Document-side loader slot (opaque). Installed by the future loader to
 * mark a document as "acquisition delegated" so the runtime can refuse
 * conflicting direct source attachment while a load is outstanding.
 * Phase 8.1 defines the slot; the network phase implements installers.
 */
typedef struct robo_html_loader robo_html_loader_t;

typedef enum robo_html_load_phase {
    ROBO_HTML_LOAD_PHASE_NONE = 0,
    /** Loader delegated for acquisition; no source attached yet. */
    ROBO_HTML_LOAD_PHASE_ACQUIRING = 1,
    /** Source attached; parsing boundary owns progress. */
    ROBO_HTML_LOAD_PHASE_PARSING = 2,
    /** Document reached READY with the loaded source. */
    ROBO_HTML_LOAD_PHASE_DONE = 3,
    /** Load failed at acquisition or boundary; document FAILED. */
    ROBO_HTML_LOAD_PHASE_FAILED = 4
} robo_html_load_phase_t;

/** Non-zero if @p phase is a defined load phase enumerator. */
int robo_html_load_phase_is_known(robo_html_load_phase_t phase);

/** Human-readable load phase name; never NULL. */
const char *robo_html_load_phase_name(robo_html_load_phase_t phase);

/**
 * Begin a delegated acquisition for @p document.
 * Phase 8.1 boundary: records ACQUIRING on the document so that direct
 * source attachment is refused with ROBO_BUSY until robo_html_resource_end
 * or robo_html_resource_cancel is called. @p loader is an opaque token
 * owned by the future loader; it is stored only as an identity (never
 * dereferenced by RoboHTML).
 */
robo_result_t robo_html_resource_begin(robo_html_document_id_t document_id,
                                       robo_html_loader_t *loader,
                                       robo_html_diag_t *diag);

/**
 * Finish delegated acquisition.
 * @p ok non-zero → document returns to CREATED (loader will attach a
 * source itself); 0 → document lifecycle FAILED with RESOURCE domain.
 * Idempotent when no acquisition is outstanding (ROBO_OK).
 */
robo_result_t robo_html_resource_end(robo_html_document_id_t document_id,
                                     int ok,
                                     robo_html_diag_t *diag);

/**
 * Cancel delegated acquisition. Document returns to CREATED; diagnostics
 * record the cancellation when @p diag is provided.
 * Idempotent when no acquisition is outstanding (ROBO_OK).
 */
robo_result_t robo_html_resource_cancel(robo_html_document_id_t document_id,
                                        robo_html_diag_t *diag);

/** Copy the current load phase; NULL/unknown document → ROBO_NOT_FOUND. */
robo_result_t robo_html_resource_get_phase(robo_html_document_id_t document_id,
                                           robo_html_load_phase_t *out_phase);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_HTML_RESOURCE_H */
