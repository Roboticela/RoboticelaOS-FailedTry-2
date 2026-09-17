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
 * robo_css_style_context_match_declarations (RoboCSSStyle.h) walks the sheets
 * the context holds, matches their rules through the element host, and fills a
 * robo_css_cascade_t. This header is the *model* and the *questions*: what the
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
 * different things. Collection returns ROBO_OK whenever it produced a result;
 * `out_error` (always filled) carries the first CSS problem — a malformed
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
 * `sheet_index`, `rule_index` and `selector_index` hold for a declaration that
 * came from an element's `style` attribute, which has no stylesheet, no rule
 * and no selector.
 */
#define ROBO_CSS_CASCADE_NO_INDEX ((size_t)-1)

/* ------------------------------------------------------------------ */
/* The level table                                                     */
/* ------------------------------------------------------------------ */

/**
 * The cascade's precedence levels, ascending: a higher level beats a lower one.
 *
 * `ROBO_CSS_CASCADE_LEVEL_NONE` is not a level — it is what an origin and
 * importance that have no level answer with, and what a cleared record holds.
 */
typedef enum robo_css_cascade_level {
    /** No level: ROBO_CSS_ORIGIN_NONE, or an unknown origin. */
    ROBO_CSS_CASCADE_LEVEL_NONE = -1,
    /** A rule the runtime itself provides. */
    ROBO_CSS_CASCADE_LEVEL_RUNTIME = 0,
    /** A rule the user installed. */
    ROBO_CSS_CASCADE_LEVEL_USER = 1,
    /** A rule belonging to the document or application. */
    ROBO_CSS_CASCADE_LEVEL_AUTHOR = 2,
    /** The element's own `style` attribute. */
    ROBO_CSS_CASCADE_LEVEL_INLINE = 3,
    /** An author `!important` declaration. */
    ROBO_CSS_CASCADE_LEVEL_AUTHOR_IMPORTANT = 4,
    /** An inline `!important` declaration — author-level, so above the above. */
    ROBO_CSS_CASCADE_LEVEL_INLINE_IMPORTANT = 5,
    /** A user `!important` declaration. */
    ROBO_CSS_CASCADE_LEVEL_USER_IMPORTANT = 6,
    /** A runtime (user-agent) `!important` declaration: the strongest level. */
    ROBO_CSS_CASCADE_LEVEL_RUNTIME_IMPORTANT = 7
} robo_css_cascade_level_t;

/** Number of defined levels (not counting LEVEL_NONE). */
#define ROBO_CSS_CASCADE_LEVEL_COUNT 8u

/** Non-zero when @p level is one of the eight levels. */
int robo_css_cascade_level_is_known(robo_css_cascade_level_t level);

/** Human-readable level name ("author", "author important"); never NULL. */
const char *robo_css_cascade_level_name(robo_css_cascade_level_t level);

/**
 * The level @p origin's declarations occupy when their importance is
 * @p importance.
 * @return the level, or ROBO_CSS_CASCADE_LEVEL_NONE for ROBO_CSS_ORIGIN_NONE or
 *         an origin this build does not know.
 */
robo_css_cascade_level_t robo_css_cascade_level_of(robo_css_origin_t origin,
                                                   int important);

/**
 * Non-zero when @p level is exactly the level of (@p origin, @p importance).
 * The round-trip test a suite wants, spelled once: a level is not a number to
 * compare by hand, it is a claim about an origin and an importance.
 */
int robo_css_cascade_level_holds(robo_css_cascade_level_t level,
                                 robo_css_origin_t origin,
                                 int important);

/**
 * Compare two levels: -1 when @p a is weaker, 0 when equal, 1 when stronger.
 * An unknown level (LEVEL_NONE) is weaker than every real level, and two
 * unknown levels compare equal.
 */
int robo_css_cascade_level_compare(robo_css_cascade_level_t a,
                                   robo_css_cascade_level_t b);

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
typedef struct robo_css_cascade_candidate {
    /** Borrowed declaration. Never NULL in a collected candidate. */
    const robo_css_declaration_t *declaration;
    /** Provenance as the *context* placed it, not as the sheet recorded it. */
    robo_css_origin_t origin;
    /** `robo_css_cascade_level_of(origin, important)`. */
    robo_css_cascade_level_t level;
    /** The declaration's own `!important`. */
    int important;
    /**
     * The specificity of the selector that matched (9.6), computed once here by
     * robo_css_specificity_of_list_item with the index the matcher reported.
     *
     * For an inline declaration this is the zero specificity — not because a
     * `style` attribute is weak but because its strength is carried by
     * ROBO_CSS_CASCADE_LEVEL_INLINE, which no selector reaches. Inline therefore
     * never ties a selector on level, so the two never meet in a specificity
     * comparison, and the two fields cannot disagree.
     */
    robo_css_specificity_t specificity;
    /**
     * The declaration's property, resolved against the context's property table
     * at collection time; ROBO_CSS_PROPERTY_ID_NONE when no table was installed
     * or the table has never heard of the name.
     *
     * Resolved here rather than asked of the declaration, because the
     * declaration is borrowed from a sheet the parser built without a table:
     * which table a name means is the *context's* decision, and this is where
     * the context is.
     */
    robo_css_property_id_t property;
    /**
     * Document order: 0 for the first declaration the collection met, counting
     * up across origins, sheets, rules and declarations, with the element's own
     * `style` attribute last. Unique within a cascade, which is what makes the
     * candidate order a total order.
     */
    size_t order;
    /** The sheet this came from; NONE for an inline declaration. */
    robo_css_stylesheet_id_t sheet;
    /** Index of @p sheet in the context's list for @p origin. */
    size_t sheet_index;
    /** Index of the rule in @p sheet; NO_INDEX for inline. */
    size_t rule_index;
    /** Which complex selector of the rule's list matched; NO_INDEX for inline. */
    size_t selector_index;
    /**
     * Index of the declaration within the list that declared it — the rule's
     * declaration list, or the element's parsed `style` attribute — which is the
     * only thing that orders two declarations that came from the same selector.
     */
    size_t declaration_index;
} robo_css_cascade_candidate_t;

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
typedef struct robo_css_cascade {
    /** Owned candidates, in cascade order (ascending level, then document order). */
    robo_css_cascade_candidate_t *items;
    size_t count;
    size_t capacity;
    /** The element this was collected for. */
    robo_css_element_ref_t element;
    /**
     * Owned declarations parsed from the element's `style` attribute, empty when
     * it had none or the context installed no inline provider. The inline
     * candidates borrow from here.
     */
    robo_css_declaration_list_t inline_declarations;

    /* --- what was walked --- */
    size_t sheets_scanned;
    size_t rules_scanned;
    size_t rules_matched;

    /* --- what could not be used, each counted where it is noticed --- */
    /** A referenced stylesheet id no longer resolves. Makes the result incomplete. */
    size_t sheets_unavailable;
    /** A sheet the context listed under an origin other than the one it records. */
    size_t sheets_origin_mismatch;
    /** Rules whose selector list could not be decided (no host, or `:hover`…). */
    size_t rules_undecided;
    /** Rules with no parsed selectors: a prelude never parsed, or invalid. */
    size_t rules_without_selectors;
    /** At-rules skipped entirely; their blocks are not evaluated here. */
    size_t at_rules_skipped;
    /** The subset of those with a block, which is where hidden rules could be. */
    size_t at_rules_with_blocks;
    /** Declarations skipped as unusable, in matched rules and inline alike. */
    size_t declarations_ignored;

    /* --- the inline boundary (9.1.10) --- */
    /** Non-zero when the context had an inline provider to ask. */
    int inline_checked;
    /** Non-zero when the element actually had a `style` attribute. */
    int inline_present;
    /** Non-zero when the inline text raised a CSS problem (recovered or not). */
    int has_inline_error;
    /** That problem, the first one, always filled when `has_inline_error`. */
    robo_css_error_t inline_error;

    /**
     * 1 when at least one thing this build cannot decide was met, so `items` is
     * a lower bound rather than the whole answer. 0 does **not** mean "nothing
     * matched": an empty cascade over a clean context is a complete answer.
     */
    int unsupported;
} robo_css_cascade_t;

/** Clear @p cascade to empty. Safe on NULL. */
void robo_css_cascade_init(robo_css_cascade_t *cascade);

/**
 * Dispose every candidate, the inline declaration list and the array.
 * Idempotent and safe on NULL.
 */
void robo_css_cascade_dispose(robo_css_cascade_t *cascade);

/** Number of candidates (0 for NULL). */
size_t robo_css_cascade_count(const robo_css_cascade_t *cascade);

/** Borrowed candidate at @p index, or NULL when out of range. */
const robo_css_cascade_candidate_t *robo_css_cascade_at(
    const robo_css_cascade_t *cascade,
    size_t index);

/** Non-zero when @p cascade met something it could not decide (0 for NULL). */
int robo_css_cascade_is_complete(const robo_css_cascade_t *cascade);

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
typedef enum robo_css_cascade_verdict {
    /** No candidate for that property: nothing in any origin declares it. */
    ROBO_CSS_CASCADE_VERDICT_NONE = 0,
    /** A winner was found and written out. */
    ROBO_CSS_CASCADE_VERDICT_DECIDED = 1
} robo_css_cascade_verdict_t;

/** Number of defined verdicts. */
#define ROBO_CSS_CASCADE_VERDICT_COUNT 2u

/** Non-zero when @p verdict is a defined verdict enumerator. */
int robo_css_cascade_verdict_is_known(robo_css_cascade_verdict_t verdict);

/** Human-readable verdict name; never NULL. */
const char *robo_css_cascade_verdict_name(robo_css_cascade_verdict_t verdict);

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
 * Every comparison goes through robo_css_specificity_compare; the cascade holds
 * no specificity arithmetic of its own.
 *
 * @p out_winner is written only for `DECIDED`, and it borrows: it points into
 * @p cascade's own array. Passing NULL for it is legal and asks only the
 * question.
 *
 * @return the verdict. A NULL @p cascade answers NONE — there is nothing to
 *         resolve, which is a fact about the argument rather than a failure.
 */
robo_css_cascade_verdict_t robo_css_cascade_resolve_property(
    const robo_css_cascade_t *cascade,
    robo_css_property_id_t property,
    const robo_css_cascade_candidate_t **out_winner);

/**
 * Resolve the winning candidate for the property named @p name, comparing the
 * declaration's own name slice ASCII case-insensitively (CSS property names are
 * ASCII case-insensitive, and a declaration whose name never resolved to an id
 * still has the name the author wrote).
 *
 * Use this when no property table was installed, and use
 * robo_css_cascade_resolve_property when one was: the two answer the same
 * question about two different spellings of identity, and neither is a fallback
 * for the other.
 *
 * @return as above.
 */
robo_css_cascade_verdict_t robo_css_cascade_resolve_name(
    const robo_css_cascade_t *cascade,
    robo_css_slice_t name,
    const robo_css_cascade_candidate_t **out_winner);

/** Number of candidates declaring @p property (0 for NULL or NONE). */
size_t robo_css_cascade_count_of(const robo_css_cascade_t *cascade,
                                 robo_css_property_id_t property);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_CSS_CASCADE_H */
