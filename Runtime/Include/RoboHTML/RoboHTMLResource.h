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
 * application) acquires bytes, wraps them in a HtmlSource, and
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
typedef struct HtmlLoader HtmlLoader;

typedef enum HtmlLoadPhase {
    RoboHtmlLoadPhaseNone = 0,
    /** Loader delegated for acquisition; no source attached yet. */
    RoboHtmlLoadPhaseAcquiring = 1,
    /** Source attached; parsing boundary owns progress. */
    RoboHtmlLoadPhaseParsing = 2,
    /** Document reached READY with the loaded source. */
    RoboHtmlLoadPhaseDone = 3,
    /** Load failed at acquisition or boundary; document FAILED. */
    RoboHtmlLoadPhaseFailed = 4
} HtmlLoadPhase;

/** Non-zero if @p phase is a defined load phase enumerator. */
int HtmlLoadPhaseIsKnown(HtmlLoadPhase phase);

/** Human-readable load phase name; never NULL. */
const char *HtmlLoadPhaseName(HtmlLoadPhase phase);

/**
 * Begin a delegated acquisition for @p document.
 * Phase 8.1 boundary: records ACQUIRING on the document so that direct
 * source attachment is refused with RoboBusy until HtmlResourceEnd
 * or HtmlResourceCancel is called. @p loader is an opaque token
 * owned by the future loader; it is stored only as an identity (never
 * dereferenced by RoboHTML).
 */
Result HtmlResourceBegin(HtmlDocumentId documentId,
                                       HtmlLoader *loader,
                                       HtmlDiag *diag);

/**
 * Finish delegated acquisition.
 * @p ok non-zero → document returns to CREATED (loader will attach a
 * source itself); 0 → document lifecycle FAILED with RESOURCE domain.
 * Idempotent when no acquisition is outstanding (RoboOk).
 */
Result HtmlResourceEnd(HtmlDocumentId documentId,
                                     int ok,
                                     HtmlDiag *diag);

/**
 * Cancel delegated acquisition. Document returns to CREATED; diagnostics
 * record the cancellation when @p diag is provided.
 * Idempotent when no acquisition is outstanding (RoboOk).
 */
Result HtmlResourceCancel(HtmlDocumentId documentId,
                                        HtmlDiag *diag);

/** Copy the current load phase; NULL/unknown document → RoboNotFound. */
Result HtmlResourceGetPhase(HtmlDocumentId documentId,
                                           HtmlLoadPhase *outPhase);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_HTML_RESOURCE_H */
