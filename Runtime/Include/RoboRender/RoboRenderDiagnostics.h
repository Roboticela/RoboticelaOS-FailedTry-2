/*
 * RoboRender diagnostics (Phase 11.12).
 *
 * The same shape every module in this runtime uses: a public call returns a plain `Result`
 * and an optional caller-owned record refines a failure with a domain, a message and the call site.
 *
 * **Its own record rather than RoboPaint's**, and the reason is the reason RoboPaint has one of its
 * own: a diagnostic's job is to send a reader to the right stage. The three failures a frame has
 * that nothing below it can have are
 *
 *   a **target** this build cannot make or read       -- a size, a stride, a frame this process
 *                                                       cannot hold;
 *   a **list** this rasteriser cannot execute         -- a command kind it does not know, a scope
 *                                                       pair that does not balance;
 *   a **frame** that could not be run                 -- the pass below it refused, or the record
 *                                                       could not be produced.
 *
 * and the two failures that are *not* this module's are deliberately not folded in: a document
 * whose layout is stale is RoboLayout's answer and a list that could not be built is RoboPaint's,
 * and a caller told "RoboRender failed" for either would go looking in the wrong module. The paint
 * diagnostics a run does produce travel beside this one -- a frame's own door hands the paint
 * module *its* record and turns a refusal into this one's, so a caller holds one record and the
 * message names the stage that actually failed.
 */
#ifndef ROBO_RENDER_DIAGNOSTICS_H
#define ROBO_RENDER_DIAGNOSTICS_H

#include "RoboBase/RoboError.h"

/* `size_t` and `NULL` for the record's own fields, stated rather than inherited from whatever the
 * error header happens to pull in -- this module's headers must compile on their own. */
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Which part of RoboRender a failure came from.
 *
 * Four rows, in the order a frame is attempted, and each is a stage a caller can act on
 * differently: a target failure is fixed by asking for a different frame, a list failure by
 * rebuilding the list, and a frame failure by reading the message. A reader sent to the wrong
 * stage is a reader sent to the wrong phase.
 */
typedef enum RenderDiagnosticDomain {
    /** Nothing: a cleared record's value. */
    RoboRenderDomainNone = 0,
    /** The target: its size, its stride, its allocation, its bytes. */
    RoboRenderDomainTarget = 1,
    /** The display list: a kind this rasteriser does not execute, an unbalanced scope pair. */
    RoboRenderDomainList = 2,
    /** The run: the pass below, the record, the region arithmetic. */
    RoboRenderDomainFrame = 3,
    /** The frame dump a developer reads. */
    RoboRenderDomainDump = 4
} RenderDiagnosticDomain;

/** Number of defined domains. Asserted positionally by `render.conformance.*`. */
#define RoboRenderDomainCount 5u

/** Non-zero if @p domain is a defined domain. */
int RenderDomainIsKnown(RenderDiagnosticDomain domain);

/** Human-readable domain name; never NULL; "unknown" when undefined. */
const char *RenderDomainName(RenderDiagnosticDomain domain);

/**
 * Caller-owned RoboRender diagnostic record. `message` is a static string and is never freed by
 * RoboRender; `file`/`line` name the reporting call site.
 */
typedef struct RenderDiag {
    Result code;
    RenderDiagnosticDomain domain;
    const char *message; /* static; do not free */
    const char *file;    /* __FILE__ of the reporting call site */
    int line;
} RenderDiag;

/** Clear @p diag to success / empty. Safe on NULL (no-op). */
void RenderDiagReset(RenderDiag *diag);

/**
 * Fill @p diag. NULL @p diag -> RoboInvalidArgument.
 * @p code should normally be a failure; RoboOk clears like reset.
 * RoboRenderDomainNone is accepted (a generic RoboRender failure).
 */
Result RenderDiagSet(RenderDiag *diag,
                                   Result code,
                                   RenderDiagnosticDomain domain,
                                   const char *message,
                                   const char *file,
                                   int line);

/**
 * Non-strict variant used by the RoboRenderDiag macro: always returns @p code, filling @p diag
 * only when it is non-NULL. Diagnostics are optional; the result code is authoritative.
 */
Result RenderDiagRecord(RenderDiag *diag,
                                      Result code,
                                      RenderDiagnosticDomain domain,
                                      const char *message,
                                      const char *file,
                                      int line);

/**
 * Convenience wrapper used by RoboRender implementation call sites:
 *   RoboRenderDiag(&diag, RoboRenderDomainTarget, RoboOutOfMemory, "...");
 * Evaluates to @p code whether or not @p diag is NULL.
 */
#define RoboRenderDiag(diag, dom, code, msg)                                     \
    RenderDiagRecord((diag), (code), (dom), (msg), __FILE__, __LINE__)

#ifdef __cplusplus
}
#endif

#endif /* ROBO_RENDER_DIAGNOSTICS_H */
