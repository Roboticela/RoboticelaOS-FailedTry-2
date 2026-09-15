/*
 * RoboHTML module lifecycle (Phase 8.1).
 *
 * Module state machine (mirrors RoboWindow discipline):
 *
 *   uninitialized → initialized → uninitialized
 *
 *   - robo_html_init validates its foundation (RoboBase, RoboMem) is up and
 *     refuses double initialization with ROBO_ALREADY_EXISTS.
 *   - robo_html_shutdown tears down all live documents (deterministic order),
 *     releases module registries, and is idempotent (no-op when down).
 *   - All document/node APIs require the module to be initialized and return
 *     ROBO_INIT_ERROR otherwise (deterministic, never crash).
 */

#ifndef ROBO_HTML_H
#define ROBO_HTML_H

#include "RoboBase/RoboError.h"

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Null-terminated RoboHTML version string (static; do not free). */
const char *robo_html_version_string(void);

/** Numeric RoboHTML version components; NULL out → ROBO_INVALID_ARGUMENT. */
robo_result_t robo_html_get_version(robo_i32 *out_major,
                                    robo_i32 *out_minor,
                                    robo_i32 *out_patch);

/**
 * Initialize the RoboHTML module.
 * @return ROBO_OK; ROBO_ALREADY_EXISTS if already initialized;
 *         ROBO_INIT_ERROR if foundation modules (RoboBase/RoboMem) are down.
 */
robo_result_t robo_html_init(void);

/**
 * Shut down RoboHTML: destroys all live documents, releases registries.
 * Idempotent — no-op when not initialized.
 */
void robo_html_shutdown(void);

/** @return 1 if the module is initialized, else 0. */
int robo_html_is_initialized(void);

/** Number of live documents (0 when uninitialized). */
size_t robo_html_live_document_count(void);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_HTML_H */
