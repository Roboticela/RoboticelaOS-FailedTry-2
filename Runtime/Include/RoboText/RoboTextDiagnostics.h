/*
 * RoboText diagnostics (Phase 12.4).
 *
 * The shape every module in this tree uses: a public call returns a plain `Result` and an optional
 * caller-owned record refines a failure with a domain, a message and the call site.
 *
 * The domains are organised by **which object refused**, because they send a reader to five
 * different owners:
 *
 *   buffer     the text itself: bytes that are not UTF-8, an index past the end, a failed growth;
 *   range       a range that is inverted, or that does not lie inside the text it indexes;
 *   font        the font context: no face bound, a face with no cmap, a size this build cannot use;
 *   run          segmentation: a run set that would not allocate, or a run that does not lie in the
 *                text it was segmented from;
 *   glyph        glyph generation and mapping: a glyph id the face does not have, a glyph index
 *                past the end of a run.
 */
#ifndef ROBO_TEXT_DIAGNOSTICS_H
#define ROBO_TEXT_DIAGNOSTICS_H

#include "RoboBase/RoboError.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Which object a text failure came from. */
typedef enum RoboTextDiagnosticDomain {
    /** Nothing: a cleared record's value. */
    RoboTextDomainNone = 0,
    /** The text buffer. */
    RoboTextDomainBuffer = 1,
    /** A range or an index into a buffer. */
    RoboTextDomainRange = 2,
    /** The font context. */
    RoboTextDomainFont = 3,
    /** Script/direction/text runs. */
    RoboTextDomainRun = 4,
    /** Glyphs, glyph runs and source mapping. */
    RoboTextDomainGlyph = 5
} RoboTextDiagnosticDomain;

/**
 * One past the last real domain. @ref RoboTextDomainNone is **not** one of them, so the count is the five
 * real domains plus the sentinel; `text.conformance.*` asserts the two facts together.
 */
#define RoboTextDomainCount 6u

/** Non-zero if @p domain names an object. False for @ref RoboTextDomainNone, which is the *absence* of a
 *  domain rather than one of them. */
int RoboTextDomainIsKnown(RoboTextDiagnosticDomain domain);

/** Human-readable domain name; never NULL; `"unknown"` for @ref RoboTextDomainNone and for a value that is
 *  not a domain at all. */
const char *RoboTextDomainName(RoboTextDiagnosticDomain domain);

/** Caller-owned record. `message` is static and never freed; `file`/`line` name the reporting site. */
typedef struct RoboTextDiag {
    Result code;
    RoboTextDiagnosticDomain domain;
    const char *message; /* static; do not free */
    const char *file;
    int line;
} RoboTextDiag;

/** Clear @p diag to success / empty. Safe on NULL. */
void RoboTextDiagReset(RoboTextDiag *diag);

/** Record @p code. @p code == RoboOk clears. NULL @p diag -> RoboInvalidArgument. */
Result RoboTextDiagSet(RoboTextDiag *diag,
                       Result code,
                       RoboTextDiagnosticDomain domain,
                       const char *message,
                       const char *file,
                       int line);

/** Non-strict variant used by the macro: always returns @p code. */
Result RoboTextDiagRecord(RoboTextDiag *diag,
                          Result code,
                          RoboTextDiagnosticDomain domain,
                          const char *message,
                          const char *file,
                          int line);

/** Implementation-site wrapper; evaluates to @p code whether or not @p diag is NULL. */
#define RoboTextDiagMsg(diag, dom, code, msg) \
    RoboTextDiagRecord((diag), (code), (dom), (msg), __FILE__, __LINE__)

#ifdef __cplusplus
}
#endif

#endif /* ROBO_TEXT_DIAGNOSTICS_H */
