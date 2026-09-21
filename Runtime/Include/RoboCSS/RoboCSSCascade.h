/*
 * RoboCSS cascade (Phase 9.5).
 *
 * A stylesheet is a set of claims about elements; the cascade is the part that
 * answers "so what applies here, and in what order". CSS sorts those claims by
 * three things, and all three are now complete:
 *
 *   1. origin and importance   — the *level*. A user-agent rule loses to a user
 *      rule, which loses to an author rule, and an `!important` declaration
 *      reverses that order. Phase 9.5 owns the whole table, and it is final:
 *      no later goal changes a level.
 *   2. specificity             — a property of the selector that matched. Phase
 *      9.6 owns it (RoboCSSSpecificity.h), and this header *consumes* it: a
 *      candidate carries the specificity of the selector that matched, computed
 *      once at collection time by the authoritative implementation. The cascade
 *      contains no specificity arithmetic of its own.
 *   3. document order          — later wins. Owned here, and it is the
 *      provenance a candidate carries.
 *
 * So the cascade is total: every property question has an answer.
 *
 *   the level table                      complete, and never revisited
 *   collection of what applies           complete: every declaration of every
 *                                        matched rule, from every origin, plus
 *                                        the element's own `style` attribute
 *   the order of those candidates        complete: by level, then document order
 *   the winner for a property            decided by level, then specificity, then
 *                                        document order
 *
 * Phase 9.5 shipped without the middle key and answered
 * ROBO_CSS_CASCADE_VERDICT_UNDECIDED_SPECIFICITY rather than guessing. That
 * verdict is gone: 9.6 is the middle key, so there is nothing left to be
 * undecided about. The rule that produced it survives in a sharper form — the
 * specificity a candidate carries is the specificity of the selector that
 * *matched*, never the first selector's in its list and never the list's
 * maximum — which is the same refusal to answer from the wrong input, expressed
 * where it cannot be got wrong.
 *
 * The level table
 *
 * Ascending precedence, with the two halves mirrored the way CSS mirrors them:
 *
 *     0  runtime          a user-agent rule
 *     1  user
 *     2  author
 *     3  inline           the element's own `style` attribute
 *     4  author    !important
 *     5  inline    !important
 *     6  user      !important
 *     7  runtime   !important
 *
 * Two facts about that table are worth stating because they are decisions
 * rather than transcriptions.
 *
 * `inline` sits *above* the origin it belongs to. In CSS a `style` attribute is
 * an author-origin declaration with a specificity no selector can reach, so
 * treating INLINE (RoboCSSTypes.h) as its own level above AUTHOR reproduces what
 * a browser does, in both halves.
 *
 * The important half is *not* the normal half reversed end to end. It reverses
 * the origin order — `runtime !important` is the strongest thing there is, then
 * user, then author — while inline keeps its position directly above author,
 * because inline is author-level. So an inline `!important` beats an author
 * `!important` (which happens because both are author-origin) and still loses to
 * a user `!important`. Reading the table as a plain reversal of levels 0–3 would
 * get that backwards.
 *
 * What is deliberately absent: `@layer`, animations and transitions. Cascading
 * layers do not exist in this model, and animation and transition origins are
 * *effects* (9.16's world) rather than declarations. Inventing rows for them
 * now would be inventing precedence nothing can produce.
 *
 * Where a cascade comes from
 *
 * Not from this header. A cascade is what a style context produces:
 * CssStyleContextMatchDeclarations (RoboCSSStyle.h) walks the sheets
 * the context holds, matches their rules through the element host, and fills a
 * CssCascade. This header is the *model* and the *questions*: what the
 * level of an origin and importance is, what a collected candidate is, what
 * order they are in, and who wins for one property.
 *
 * Ownership
 *
 * One rule, and it is the same one the rest of the subsystem uses:
 *
 *   the cascade owns its candidate array and, when the element had a `style`
 *   attribute, the declaration list parsed from it. Every candidate *borrows*
 *   the declaration it points at — from a stylesheet the context referenced, or
 *   from the cascade's own inline list — so a cascade is valid exactly as long
 *   as the sheets it was collected from, which is a fact the caller already
 *   owns because it owns those sheets.
 *
 * Nothing here takes a stylesheet, a DOM or a context: a result is inert
 * description, and a caller can keep one, ask it questions, and dispose it
 * without anything else changing under it.
 *
 * Errors
 *
 * Collecting a cascade is where malformed CSS and incomplete configuration meet
 * the caller, and RoboCSS's usual rule applies: the *call* and the *CSS* are
 * different things. Collection returns RoboOk whenever it produced a result;
 * `outError` (always filled) carries the first CSS problem — a malformed
 * `style` attribute, say — and a recovered problem is not a failed call.
 * Conditions that make the result *incomplete* rather than wrong are counted on
 * the result and set `unsupported`: a rule whose selector could not be decided,
 * an at-rule with a block that nothing evaluates, a sheet that was destroyed
 * after it was referenced. `unsupported == 1` never means "nothing matched"; it
 * means "do not read this as the whole truth".
 */

#ifndef ROBO_CSS_CASCADE_H
#define ROBO_CSS_CASCADE_H

#include "RoboBase/RoboError.h"
#include "RoboCSS/RoboCSSDeclaration.h"
#include "RoboCSS/RoboCSSDiagnostics.h"
#include "RoboCSS/RoboCSSError.h"
#include "RoboCSS/RoboCSSProperty.h"
#include "RoboCSS/RoboCSSSpecificity.h"
#include "RoboCSS/RoboCSSStyle.h"
#include "RoboCSS/RoboCSSStyleSheet.h"
#include "RoboCSS/RoboCSSTypes.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * "This candidate has no document position of this kind" — the value
 * `sheetIndex`, `ruleIndex` and `selectorIndex` hold for a declaration that
 * came from an element's `style` attribute, which has no stylesheet, no rule
 * and no selector.
 */
#define RoboCssCascadeNoIndex ((size_t)-1)

/* ------------------------------------------------------------------ */
/* The level table                                                     */
/* ------------------------------------------------------------------ */

/**
 * The cascade's precedence levels, ascending: a higher level beats a lower one.
 *
 * `RoboCssCascadeLevelNone` is not a level — it is what an origin and
 * importance that have no level answer with, and what a cleared record holds.
 */
typedef enum CssCascadeLevel {
    /** No level: RoboCssOriginNone, or an unknown origin. */
    RoboCssCascadeLevelNone = -1,
    /** A rule the runtime itself provides. */
    RoboCssCascadeLevelRuntime = 0,
    /** A rule the user installed. */
    RoboCssCascadeLevelUser = 1,
    /** A rule belonging to the document or application. */
    RoboCssCascadeLevelAuthor = 2,
    /** The element's own `style` attribute. */
    RoboCssCascadeLevelInline = 3,
    /** An author `!important` declaration. */
    RoboCssCascadeLevelAuthorImportant = 4,
    /** An inline `!important` declaration — author-level, so above the above. */
    RoboCssCascadeLevelInlineImportant = 5,
    /** A user `!important` declaration. */
    RoboCssCascadeLevelUserImportant = 6,
    /** A runtime (user-agent) `!important` declaration: the strongest level. */
    RoboCssCascadeLevelRuntimeImportant = 7
} CssCascadeLevel;

/** Number of defined levels (not counting LEVEL_NONE). */
#define RoboCssCascadeLevelCount 8u

/** Non-zero when @p level is one of the eight levels. */
int CssCascadeLevelIsKnown(CssCascadeLevel level);

/** Human-readable level name ("author", "author important"); never NULL. */
const char *CssCascadeLevelName(CssCascadeLevel level);

/**
 * The level @p origin's declarations occupy when their importance is
 * @p importance.
 * @return the level, or RoboCssCascadeLevelNone for RoboCssOriginNone or
 *         an origin this build does not know.
 */
CssCascadeLevel CssCascadeLevelOf(CssOrigin origin,
                                                   int important);

/**
 * Non-zero when @p level is exactly the level of (@p origin, @p importance).
 * The round-trip test a suite wants, spelled once: a level is not a number to
 * compare by hand, it is a claim about an origin and an importance.
 */
int CssCascadeLevelHolds(CssCascadeLevel level,
                                 CssOrigin origin,
                                 int important);

/**
 * Compare two levels: -1 when @p a is weaker, 0 when equal, 1 when stronger.
 * An unknown level (LEVEL_NONE) is weaker than every real level, and two
 * unknown levels compare equal.
 */
int CssCascadeLevelCompare(CssCascadeLevel a,
                                   CssCascadeLevel b);

/* ------------------------------------------------------------------ */
/* Candidates                                                          */
/* ------------------------------------------------------------------ */

/**
 * One declaration that applies to the element, with everything the cascade
 * needs to place it: where it came from, which level that is, and how early the
 * browser met it.
 *
 * `declaration` is **borrowed**. It belongs to the stylesheet the context
 * referenced, or to the cascade's own inline list — see the header comment.
 */
typedef struct CssCascadeCandidate {
    /** Borrowed declaration. Never NULL in a collected candidate. */
    const CssDeclaration *declaration;
    /** Provenance as the *context* placed it, not as the sheet recorded it. */
    CssOrigin origin;
    /** `CssCascadeLevelOf(origin, important)`. */
    CssCascadeLevel level;
    /** The declaration's own `!important`. */
    int important;
    /**
     * The specificity of the selector that matched (9.6), computed once here by
     * CssSpecificityOfListItem with the index the matcher reported.
     *
     * For an inline declaration this is the zero specificity — not because a
     * `style` attribute is weak but because its strength is carried by
     * RoboCssCascadeLevelInline, which no selector reaches. Inline therefore
     * never ties a selector on level, so the two never meet in a specificity
     * comparison, and the two fields cannot disagree.
     */
    CssSpecificity specificity;
    /**
     * The declaration's property, resolved against the context's property table
     * at collection time; RoboCssPropertyIdNone when no table was installed
     * or the table has never heard of the name.
     *
     * Resolved here rather than asked of the declaration, because the
     * declaration is borrowed from a sheet the parser built without a table:
     * which table a name means is the *context's* decision, and this is where
     * the context is.
     */
    CssPropertyId property;
    /**
     * Document order: 0 for the first declaration the collection met, counting
     * up across origins, sheets, rules and declarations, with the element's own
     * `style` attribute last. Unique within a cascade, which is what makes the
     * candidate order a total order.
     */
    size_t order;
    /** The sheet this came from; NONE for an inline declaration. */
    CssStylesheetId sheet;
    /** Index of @p sheet in the context's list for @p origin. */
    size_t sheetIndex;
    /** Index of the rule in @p sheet; NO_INDEX for inline. */
    size_t ruleIndex;
    /** Which complex selector of the rule's list matched; NO_INDEX for inline. */
    size_t selectorIndex;
    /**
     * Index of the declaration within the list that declared it — the rule's
     * declaration list, or the element's parsed `style` attribute — which is the
     * only thing that orders two declarations that came from the same selector.
     */
    size_t declarationIndex;
} CssCascadeCandidate;

/* ------------------------------------------------------------------ */
/* The collected cascade                                               */
/* ------------------------------------------------------------------ */

/**
 * What applies to one element, in cascade order, plus what could not be decided.
 *
 * `items` is sorted by level and then by document order — weakest first — so the
 * last candidate for a property is the one that beat the others wherever the
 * ordering is decided, and reading the array front to back walks the cascade
 * the way the specification defines it.
 *
 * The counters are not decoration. A cascade is only as complete as its inputs,
 * and every way this build can be incomplete has a field: an undecided selector,
 * an at-rule nothing evaluates, a sheet that is gone, a declaration that is
 * malformed. `unsupported` is the one-line version of "do not read `items` as
 * the whole truth".
 */
typedef struct CssCascade {
    /** Owned candidates, in cascade order (ascending level, then document order). */
    CssCascadeCandidate *items;
    size_t count;
    size_t capacity;
    /** The element this was collected for. */
    CssElementRef element;
    /**
     * Owned declarations parsed from the element's `style` attribute, empty when
     * it had none or the context installed no inline provider. The inline
     * candidates borrow from here.
     */
    CssDeclarationList inlineDeclarations;

    /* --- what was walked --- */
    size_t sheetsScanned;
    size_t rulesScanned;
    size_t rulesMatched;

    /* --- what could not be used, each counted where it is noticed --- */
    /** A referenced stylesheet id no longer resolves. Makes the result incomplete. */
    size_t sheetsUnavailable;
    /** A sheet the context listed under an origin other than the one it records. */
    size_t sheetsOriginMismatch;
    /** Rules whose selector list could not be decided (no host, or `:hover`…). */
    size_t rulesUndecided;
    /** Rules with no parsed selectors: a prelude never parsed, or invalid. */
    size_t rulesWithoutSelectors;
    /** At-rules skipped entirely; their blocks are not evaluated here. */
    size_t atRulesSkipped;
    /** The subset of those with a block, which is where hidden rules could be. */
    size_t atRulesWithBlocks;
    /** Declarations skipped as unusable, in matched rules and inline alike. */
    size_t declarationsIgnored;

    /* --- the inline boundary (9.1.10) --- */
    /** Non-zero when the context had an inline provider to ask. */
    int inlineChecked;
    /** Non-zero when the element actually had a `style` attribute. */
    int inlinePresent;
    /** Non-zero when the inline text raised a CSS problem (recovered or not). */
    int hasInlineError;
    /** That problem, the first one, always filled when `hasInlineError`. */
    CssError inlineError;

    /**
     * 1 when at least one thing this build cannot decide was met, so `items` is
     * a lower bound rather than the whole answer. 0 does **not** mean "nothing
     * matched": an empty cascade over a clean context is a complete answer.
     */
    int unsupported;
} CssCascade;

/** Clear @p cascade to empty. Safe on NULL. */
void CssCascadeInit(CssCascade *cascade);

/**
 * Dispose every candidate, the inline declaration list and the array.
 * Idempotent and safe on NULL.
 */
void CssCascadeDispose(CssCascade *cascade);

/** Number of candidates (0 for NULL). */
size_t CssCascadeCount(const CssCascade *cascade);

/** Borrowed candidate at @p index, or NULL when out of range. */
const CssCascadeCandidate *CssCascadeAt(
    const CssCascade *cascade,
    size_t index);

/** Non-zero when @p cascade met something it could not decide (0 for NULL). */
int CssCascadeIsComplete(const CssCascade *cascade);

/* ------------------------------------------------------------------ */
/* Resolution                                                          */
/* ------------------------------------------------------------------ */

/**
 * What a resolution concluded.
 *
 * Two outcomes, because there are two facts: nothing declares the property, or
 * one declaration beat the others. Phase 9.5 had a third — "only specificity
 * could separate these" — and 9.6 removed it by supplying the specificity, so a
 * caller that used to branch on an undecided answer now gets a decision.
 */
typedef enum CssCascadeVerdict {
    /** No candidate for that property: nothing in any origin declares it. */
    RoboCssCascadeVerdictNone = 0,
    /** A winner was found and written out. */
    RoboCssCascadeVerdictDecided = 1
} CssCascadeVerdict;

/** Number of defined verdicts. */
#define RoboCssCascadeVerdictCount 2u

/** Non-zero when @p verdict is a defined verdict enumerator. */
int CssCascadeVerdictIsKnown(CssCascadeVerdict verdict);

/** Human-readable verdict name; never NULL. */
const char *CssCascadeVerdictName(CssCascadeVerdict verdict);

/**
 * Resolve the winning candidate for a property, by property id.
 *
 * The winner is the candidate at the strongest level; among candidates at that
 * level, the greatest specificity; among those, the latest in document order.
 * The candidate array is already in (level, document order) order, so one
 * forward pass suffices: a candidate always arrives at the current level or
 * above it, a strictly stronger level displaces the incumbent, and within one
 * level a greater specificity displaces it while an equal specificity leaves the
 * *later* candidate in place — which is the document-order tie-break.
 *
 * Every comparison goes through CssSpecificityCompare; the cascade holds
 * no specificity arithmetic of its own.
 *
 * @p outWinner is written only for `DECIDED`, and it borrows: it points into
 * @p cascade's own array. Passing NULL for it is legal and asks only the
 * question.
 *
 * @return the verdict. A NULL @p cascade answers NONE — there is nothing to
 *         resolve, which is a fact about the argument rather than a failure.
 */
CssCascadeVerdict CssCascadeResolveProperty(
    const CssCascade *cascade,
    CssPropertyId property,
    const CssCascadeCandidate **outWinner);

/**
 * Resolve the winning candidate for the property named @p name, comparing the
 * declaration's own name slice ASCII case-insensitively (CSS property names are
 * ASCII case-insensitive, and a declaration whose name never resolved to an id
 * still has the name the author wrote).
 *
 * Use this when no property table was installed, and use
 * CssCascadeResolveProperty when one was: the two answer the same
 * question about two different spellings of identity, and neither is a fallback
 * for the other.
 *
 * @return as above.
 */
CssCascadeVerdict CssCascadeResolveName(
    const CssCascade *cascade,
    CssSlice name,
    const CssCascadeCandidate **outWinner);

/**
 * Compare two candidates *from one cascade* by the cascade's own order.
 *
 * The two resolve doors above answer "who wins for this property", which is one
 * identity at a time. A shorthand is where one declaration speaks for several
 * identities at once: `margin: 10px` and `margin-left: 20px` are two candidates
 * for the same edge, and which of them the edge is comes down to the same three
 * keys — level, specificity, document order — applied across two property names
 * rather than within one. That is a fact about the cascade, so it is answered
 * here, by the same level table and the same specificity comparison the
 * resolution uses, rather than by arithmetic at the call site.
 *
 * @return -1 when @p a wins, 1 when @p b wins, 0 when they are the same
 *         candidate. Two candidates the cascade does not hold are unorderable
 *         and answer 0: there is no document order to compare, and guessing one
 *         would be inventing a tie-break.
 */
int CssCascadeCandidateCompare(const CssCascade *cascade,
                                       const CssCascadeCandidate *a,
                                       const CssCascadeCandidate *b);

/** Number of candidates declaring @p property (0 for NULL or NONE). */
size_t CssCascadeCountOf(const CssCascade *cascade,
                                 CssPropertyId property);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_CSS_CASCADE_H */
