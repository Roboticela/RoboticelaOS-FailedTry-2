/*
 * RoboCSS stylesheet model (Phase 9.1).
 *
 * A stylesheet is the object a caller keeps: it carries where it came from
 * (origin, name), what it was built from (source bytes), what it contains
 * (rules), how far it has got (state) and what went wrong (last error). It
 * owns its source when the caller asked for a copy and owns its rules always.
 *
 * Stylesheets are addressed by id, not by pointer, using the same discipline
 * RoboHTML uses for documents: a module-scoped slot table, ids from a
 * monotonic cursor, and ids that are never reused, so an id that outlives its
 * stylesheet resolves to nothing instead of to somebody else's sheet. The
 * module exposes how many are live, and shutdown tears down whatever is left.
 *
 * What a stylesheet is *not*:
 *
 *   - not *interpreted* by its parse. CssStylesheetParse is a real parse
 *     as of 9.3 (see its own documentation), and what it produces is syntax:
 *     rules, declarations and component values. No selector is matched, no
 *     at-rule is evaluated, and no declaration is given a meaning. Whether a
 *     parse replaced the rules or left them alone is stated there.
 *   - not loaded. There is no fetch, no `@import`, no `@charset`, no encoding
 *     detection, no media evaluation. Parsing `@import "x";` records a name, a
 *     prelude and a `;` — and fetches nothing. A stylesheet can also still be
 *     built by hand and rules appended to it.
 *   - not attached. Which sheets apply to which document is the style
 *     context's business (RoboCSSStyle.h); a stylesheet knows nothing about
 *     the origin it is used under, only the origin recorded on it.
 *
 * Rules can be appended, read by index, removed, and cleared. Order is the
 * order they were appended, which is the order a cascade needs to see.
 */

#ifndef ROBO_CSS_STYLESHEET_H
#define ROBO_CSS_STYLESHEET_H

#include "RoboBase/RoboError.h"
#include "RoboCSS/RoboCSSDiagnostics.h"
#include "RoboCSS/RoboCSSError.h"
#include "RoboCSS/RoboCSSRule.h"
#include "RoboCSS/RoboCSSSource.h"
#include "RoboCSS/RoboCSSTypes.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Opaque stylesheet identity (stable while the stylesheet lives; 0 = none). */
typedef uint64_t CssStylesheetId;

#define RoboCssStylesheetIdNone ((CssStylesheetId)0u)

/**
 * Create an empty stylesheet in state CREATED, of @p origin.
 * @p origin RoboCssOriginNone is accepted — a sheet whose provenance is not
 * decided yet — and can be set later with CssStylesheetSetOrigin.
 * @return RoboOk; RoboInvalidArgument for a NULL output or an unknown
 *         origin; RoboInitError when the module is not initialized;
 *         RoboOutOfMemory when the slot table is full.
 */
Result CssStylesheetCreate(CssStylesheetId *outId,
                                         CssOrigin origin,
                                         CssDiag *diag);

/**
 * Destroy @p id: dispose every rule and any owned source copy.
 * @return RoboOk; RoboNotFound for an id that does not resolve (including
 *         RoboCssStylesheetIdNone, and a second destroy of the same id);
 *         RoboInvalidArgument if @p id was never issued by this module.
 */
Result CssStylesheetDestroy(CssStylesheetId id,
                                          CssDiag *diag);

/** Non-zero when @p id resolves to a live stylesheet. */
int CssStylesheetIsLive(CssStylesheetId id);

/** Borrowed id for the stylesheet at @p index in slot order; NONE when out of
 *  range. Slot order is creation order, which is what a deterministic test
 *  needs and what a caller must not depend on for anything else. */
CssStylesheetId CssStylesheetIdAt(size_t index);

/** Set the recorded origin. */
Result CssStylesheetSetOrigin(CssStylesheetId id,
                                             CssOrigin origin,
                                             CssDiag *diag);

/** Recorded origin; NONE for an id that does not resolve. */
CssOrigin CssStylesheetOrigin(CssStylesheetId id);

/** Current state; NONE for an id that does not resolve. */
CssStylesheetState MakeCssStylesheetState(CssStylesheetId id);

/**
 * Force the state to CREATED, PARSED or FAILED.
 *
 * This exists because the parse that would set it (9.3) is not here yet, and
 * because a caller that built a stylesheet by hand has a legitimate reason to
 * say so. NONE is refused: NONE means "no stylesheet", and an id that resolves
 * always has a real state.
 * @return RoboOk; RoboNotFound; RoboInvalidArgument for an unknown state.
 */
Result CssStylesheetSetState(CssStylesheetId id,
                                            CssStylesheetState state,
                                            CssDiag *diag);

/**
 * Record a diagnostic label (copied, truncated when too long, never rejected
 * for length — a label must not fail a call that would otherwise succeed).
 */
Result CssStylesheetSetName(CssStylesheetId id,
                                           const char *name,
                                           CssDiag *diag);

/** Borrowed label, always NUL-terminated ("" when unset); NULL when the id
 *  does not resolve. */
const char *CssStylesheetName(CssStylesheetId id);

/* ------------------------------------------------------------------ */
/* Source attachment                                                   */
/* ------------------------------------------------------------------ */

/**
 * Attach borrowed source bytes. The caller's buffer must stay valid for as
 * long as the stylesheet reads them (rules borrow their text from here).
 */
Result CssStylesheetSetSourceMemory(CssStylesheetId id,
                                                    const uint8_t *bytes,
                                                    size_t length,
                                                    CssDiag *diag);

/** Attach a borrowed NUL-terminated string as the source. */
Result CssStylesheetSetSourceString(CssStylesheetId id,
                                                    const char *utf8,
                                                    CssDiag *diag);

/**
 * Attach an owned copy of @p bytes: the stylesheet allocates it and frees it
 * when the stylesheet is destroyed or the source is replaced. This is the mode
 * a caller wants when the bytes are temporary, which is the usual case for a
 * `<style>` element's text.
 */
Result CssStylesheetCopySource(CssStylesheetId id,
                                             const uint8_t *bytes,
                                             size_t length,
                                             CssDiag *diag);

/** Borrowed source, or NULL when the id does not resolve. */
const CssSource *CssStylesheetSource(CssStylesheetId id);

/** Source length in bytes (0 when none). */
size_t CssStylesheetSourceLength(CssStylesheetId id);

/** Non-zero when the stylesheet owns its source bytes. */
int CssStylesheetOwnsSource(CssStylesheetId id);

/* ------------------------------------------------------------------ */
/* Rules                                                               */
/* ------------------------------------------------------------------ */

/**
 * Append @p rule by moving it in; @p rule is left KIND_NONE.
 * @return RoboOk; RoboNotFound for an id that does not resolve;
 *         RoboInvalidArgument for a NULL rule or a rule kind of NONE;
 *         RoboOutOfMemory when the rule list cannot grow.
 */
Result CssStylesheetAppendRule(CssStylesheetId id,
                                              CssRule *rule,
                                              CssDiag *diag);

/** Number of top-level rules (0 when the id does not resolve). */
size_t CssStylesheetRuleCount(CssStylesheetId id);

/** Borrowed top-level rule at @p index, or NULL when out of range. */
const CssRule *CssStylesheetRule(CssStylesheetId id,
                                                size_t index);

/** Total number of rules, at-rules' blocks included. */
size_t CssStylesheetTreeRuleCount(CssStylesheetId id);

/** Number of style rules, at-rules' blocks included. */
size_t CssStylesheetStyleRuleCount(CssStylesheetId id);

/**
 * Remove the top-level rule at @p index, disposing it.
 * @return RoboOk; RoboNotFound for an id that does not resolve or an index
 *         out of range.
 */
Result CssStylesheetRemoveRule(CssStylesheetId id,
                                              size_t index,
                                              CssDiag *diag);

/** Remove every rule, disposing them. Safe on an id that does not resolve. */
void CssStylesheetClearRules(CssStylesheetId id);

/** Borrowed rule list, or NULL when the id does not resolve. */
const CssRuleList *CssStylesheetRules(CssStylesheetId id);

/* ------------------------------------------------------------------ */
/* Parse boundary                                                      */
/* ------------------------------------------------------------------ */

/**
 * Parse the attached source into this stylesheet's rule list.
 *
 * A real parse as of 9.3: the source is tokenized (9.2) and the token stream is
 * consumed by the CSS Syntax grammar, which builds style rules, at-rules,
 * declaration blocks and component values, and interprets none of them.
 *
 * **Transactional.** The parse builds into its own rule list and only a parse
 * that returned RoboOk replaces this stylesheet's rules — so parsing twice is
 * idempotent rather than additive, and an attempt that could not run leaves the
 * rules and the source exactly as they were.
 *
 * **The call's outcome and the CSS's problems are different things**, and each
 * has its own place:
 *
 *   - the *return value* is the call: RoboOk whenever a rule list was built,
 *     including one built by recovering from malformed CSS, because CSS error
 *     recovery is the normal case and the rules that survived are real rules.
 *     RoboNotFound for an id that does not resolve; RoboOutOfMemory when
 *     the model could not be built at all.
 *   - `outError` — always filled, never left untouched — is the *first* CSS
 *     problem recovered, or a record of kind NONE when the input was clean. The
 *     first problem wins and later ones do not overwrite it, because the first
 *     is the one that explains the rest.
 *
 * The same record is kept in the stylesheet's last-error slot, so a caller that
 * did not ask for it still sees why (see CssStylesheetLastError). The
 * state becomes PARSED on any completed attempt, *including* a recovered one,
 * and FAILED when no usable stylesheet could be produced.
 *
 * At-rules are parsed as syntax only: nothing is loaded, nothing is fetched, no
 * `@media` is evaluated and no at-rule name is looked up in a table, because
 * there is no table. Selectors are not parsed either — a style rule's prelude is
 * kept as component values for 9.4.
 *
 * A stylesheet with no attached source parses to no rules, exactly like an
 * attached empty source: a parse reads bytes, and there are none.
 *
 * @return RoboOk; RoboNotFound; RoboOutOfMemory; RoboInvalidArgument
 *         for an unknown origin.
 */
Result CssStylesheetParse(CssStylesheetId id,
                                        CssError *outError,
                                        CssDiag *diag);

/**
 * The error the last parse attempt recorded.
 * @return 1 and fills @p outError when the stylesheet holds a recorded error;
 *         0 and clears @p outError otherwise (including for an id that does
 *         not resolve).
 */
int CssStylesheetLastError(CssStylesheetId id,
                                   CssError *outError);

/** Record or clear the stylesheet's error slot. Used by the parse path; a
 *  caller may use it to attach its own explanation. */
void CssStylesheetSetLastError(CssStylesheetId id,
                                        const CssError *error);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_CSS_STYLESHEET_H */
