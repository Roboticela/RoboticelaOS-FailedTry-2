/*
 * RoboDesktop diagnostics (Phase 11.13).
 *
 * The same shape every module in this runtime uses: a public call returns a plain `robo_result_t` and an
 * optional caller-owned record refines a failure with a domain, a message and the call site. What is
 * this module's own is the **vocabulary**, and it is organised by *which object failed* rather than by
 * which phase of a frame it failed in -- because a desktop's failures are answered by different owners:
 *
 *   a **shell** failure is the desktop itself: a size it will not accept, a frame that could not be
 *   made, a resize that would leave no work area. The caller fixes it by asking for something else;
 *   a **window** failure is a shell window: an unknown id, a size below the decoration minimum, a
 *   content list that could not be rasterised. The caller fixes it by changing the window;
 *   a **surface** failure is the destination of a presentation: a window that is not attached, a
 *   render surface the window could not give, a destination that does not fit. The caller fixes it by
 *   attaching a window or resizing;
 *   a **document** failure is an application document (`RoboDesktopDocument.h`): RoboHTML, RoboCSS,
 *   RoboLayout or RoboPaint refused the application's own markup. The caller fixes *the document* -- and
 *   this domain exists so that a reader is sent there rather than to the desktop, which is the same
 *   reasoning `RoboRenderDiagnostics.h` gives for keeping a document-layer failure out of its own
 *   record;
 *   a **dump** failure is the text a developer reads.
 *
 * The message a failure carries names the *stage*, so a caller holding one record can tell "my window
 * has no content" from "my document has no style rule", and those two go to different places.
 */
#ifndef ROBO_DESKTOP_DIAGNOSTICS_H
#define ROBO_DESKTOP_DIAGNOSTICS_H

#include "RoboBase/RoboError.h"

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Which object a desktop failure came from. */
typedef enum robo_desktop_diagnostic_domain {
    /** Nothing: a cleared record's value. */
    ROBO_DESKTOP_DOMAIN_NONE = 0,
    /** The shell: its size, its frame target, its geometry. */
    ROBO_DESKTOP_DOMAIN_SHELL = 1,
    /** A shell window: its id, its bounds, its content. */
    ROBO_DESKTOP_DOMAIN_WINDOW = 2,
    /** The presentation path: the attached window, its render surface, the destination. */
    ROBO_DESKTOP_DOMAIN_SURFACE = 3,
    /** An application document: markup, style, layout or paint refused it. */
    ROBO_DESKTOP_DOMAIN_DOCUMENT = 4,
    /** The shell dump a developer reads. */
    ROBO_DESKTOP_DOMAIN_DUMP = 5
} robo_desktop_diagnostic_domain_t;

/** Number of defined domains. Asserted positionally by `desktop.conformance.*`. */
#define ROBO_DESKTOP_DOMAIN_COUNT 6u

/** Non-zero if @p domain is a defined domain. */
int robo_desktop_domain_is_known(robo_desktop_diagnostic_domain_t domain);

/** Human-readable domain name; never NULL; `"unknown"` when undefined. */
const char *robo_desktop_domain_name(robo_desktop_diagnostic_domain_t domain);

/** Caller-owned record. `message` is static and never freed; `file`/`line` name the reporting site. */
typedef struct robo_desktop_diag {
    robo_result_t code;
    robo_desktop_diagnostic_domain_t domain;
    const char *message; /* static; do not free */
    const char *file;
    int line;
} robo_desktop_diag_t;

/** Clear @p diag to success / empty. Safe on NULL. */
void robo_desktop_diag_reset(robo_desktop_diag_t *diag);

/**
 * Fill @p diag. NULL @p diag -> ROBO_INVALID_ARGUMENT.
 * @p code == ROBO_OK clears, like reset.
 */
robo_result_t robo_desktop_diag_set(robo_desktop_diag_t *diag,
                                   robo_result_t code,
                                   robo_desktop_diagnostic_domain_t domain,
                                   const char *message,
                                   const char *file,
                                   int line);

/** Non-strict variant used by the macro: always returns @p code, filling a non-NULL record. */
robo_result_t robo_desktop_diag_record(robo_desktop_diag_t *diag,
                                      robo_result_t code,
                                      robo_desktop_diagnostic_domain_t domain,
                                      const char *message,
                                      const char *file,
                                      int line);

/** Implementation-site wrapper; evaluates to @p code whether or not @p diag is NULL. */
#define ROBO_DESKTOP_DIAG(diag, dom, code, msg)                                    \
    robo_desktop_diag_record((diag), (code), (dom), (msg), __FILE__, __LINE__)

#ifdef __cplusplus
}
#endif

#endif /* ROBO_DESKTOP_DIAGNOSTICS_H */
