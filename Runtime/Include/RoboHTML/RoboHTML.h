/*
 * RoboHTML module lifecycle (Phase 8.1).
 *
 * Module state machine (mirrors RoboWindow discipline):
 *
 *   uninitialized → initialized → uninitialized
 *
 *   - HtmlInit validates its foundation (RoboBase, RoboMem) is up and
 *     refuses double initialization with RoboAlreadyExists.
 *   - HtmlShutdown tears down all live documents (deterministic order),
 *     releases module registries, and is idempotent (no-op when down).
 *   - All document/node APIs require the module to be initialized and return
 *     RoboInitError otherwise (deterministic, never crash).
 */

#ifndef ROBO_HTML_H
#define ROBO_HTML_H

#include "RoboBase/RoboError.h"

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Null-terminated RoboHTML version string (static; do not free). */
const char *HtmlVersionString(void);

/** Numeric RoboHTML version components; NULL out → RoboInvalidArgument. */
Result HtmlGetVersion(I32 *outMajor,
                                    I32 *outMinor,
                                    I32 *outPatch);

/**
 * Initialize the RoboHTML module.
 * @return RoboOk; RoboAlreadyExists if already initialized;
 *         RoboInitError if foundation modules (RoboBase/RoboMem) are down.
 */
Result HtmlInit(void);

/**
 * Shut down RoboHTML: destroys all live documents, releases registries.
 * Idempotent — no-op when not initialized.
 */
void HtmlShutdown(void);

/** @return 1 if the module is initialized, else 0. */
int HtmlIsInitialized(void);

/** Number of live documents (0 when uninitialized). */
size_t HtmlLiveDocumentCount(void);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_HTML_H */
