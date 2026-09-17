/*
 * RoboCSS cascade (Phase 9.5) — see Include/RoboCSS/RoboCSSCascade.h.
 *
 * Two halves, and they are worth keeping apart.
 *
 * Collection walks a style context once: the sheets of every stylesheet origin,
 * their rules, the rules' declarations, and finally the element's own `style`
 * attribute. Everything it cannot use is counted rather than ignored, because a
 * cascade that quietly drops an undecided selector is a cascade that applies the
 * wrong rule and gives no sign that it did.
 *
 * Resolution is then a read of that collected array, ordered by the cascade's
 * three keys: level, then the specificity the selector that matched carried
 * (9.6), then document order. The cascade computes no specificity of its own —
 * robo_css_specificity_of_list_item is the one implementation, and this file
 * only ever calls robo_css_specificity_compare.
 *
 * The array is kept in cascade order as it is built — the walk visits origins in
 * ascending level order and, within an origin, sheets, rules and declarations in
 * document order — with one exception that the sort at the end repairs: two
 * declarations in the same rule can sit at different levels (one `!important`,
 * one not), so a rule's declarations do not arrive level-sorted. A counting sort
 * over eight levels puts them back in order, stably, in one pass, and it is
 * stable precisely because document order is the tie-break inside a level.
 */

#include "RoboCSS/RoboCSSCascade.h"

#include "RoboCSSInternal.h"
#include "RoboCSS/RoboCSSMatcher.h"
#include "RoboMem/RoboMem.h"

#include <string.h>

/* ------------------------------------------------------------------ */
/* The level table                                                     */
/* ------------------------------------------------------------------ */

/*
 * The table, written out rather than derived.
 *
 * A derived rule ("mirror the normal order for important, then patch inline in")
 * is exactly the kind of arithmetic that reads as obviously right and is not:
 * inline is author-level, so it must stay directly above author in *both*
 * halves, and a plain reversal would put it below every other origin's
 * important declarations. Spelled out, the two exceptions are visible at the
 * place they matter.
 */
static const robo_css_cascade_level_t cascade_level_table[ROBO_CSS_ORIGIN_COUNT][2] = {
    /* ORIGIN_NONE */ {ROBO_CSS_CASCADE_LEVEL_NONE, ROBO_CSS_CASCADE_LEVEL_NONE},
    /* RUNTIME     */ {ROBO_CSS_CASCADE_LEVEL_RUNTIME,
                       ROBO_CSS_CASCADE_LEVEL_RUNTIME_IMPORTANT},
    /* USER        */ {ROBO_CSS_CASCADE_LEVEL_USER,
                       ROBO_CSS_CASCADE_LEVEL_USER_IMPORTANT},
    /* AUTHOR      */ {ROBO_CSS_CASCADE_LEVEL_AUTHOR,
                       ROBO_CSS_CASCADE_LEVEL_AUTHOR_IMPORTANT},
    /* INLINE      */ {ROBO_CSS_CASCADE_LEVEL_INLINE,
                       ROBO_CSS_CASCADE_LEVEL_INLINE_IMPORTANT}
};

static const char *const cascade_level_names[ROBO_CSS_CASCADE_LEVEL_COUNT] = {
    "runtime",
    "user",
    "author",
    "inline",
    "author important",
    "inline important",
    "user important",
    "runtime important"
};

int robo_css_cascade_level_is_known(robo_css_cascade_level_t level)
{
    return level >= ROBO_CSS_CASCADE_LEVEL_RUNTIME &&
           level <= ROBO_CSS_CASCADE_LEVEL_RUNTIME_IMPORTANT;
}

const char *robo_css_cascade_level_name(robo_css_cascade_level_t level)
{
    if (level == ROBO_CSS_CASCADE_LEVEL_NONE) {
        return "none";
    }
    if (!robo_css_cascade_level_is_known(level)) {
        return "unknown";
    }
    return cascade_level_names[(size_t)level];
}

robo_css_cascade_level_t robo_css_cascade_level_of(robo_css_origin_t origin,
                                                   int important)
{
    if (!robo_css_origin_is_known(origin)) {
        return ROBO_CSS_CASCADE_LEVEL_NONE;
    }
    return cascade_level_table[(size_t)origin][important ? 1 : 0];
}

int robo_css_cascade_level_holds(robo_css_cascade_level_t level,
                                 robo_css_origin_t origin,
                                 int important)
{
    if (!robo_css_cascade_level_is_known(level)) {
        return 0;
    }
    return robo_css_cascade_level_of(origin, important) == level;
}

int robo_css_cascade_level_compare(robo_css_cascade_level_t a,
                                   robo_css_cascade_level_t b)
{
    int a_known = robo_css_cascade_level_is_known(a);
    int b_known = robo_css_cascade_level_is_known(b);

    /* An unknown level is not a level, so it is weaker than any real one; two
     * unknowns have nothing to rank them by and compare equal. */
    if (!a_known || !b_known) {
        if (!a_known && !b_known) {
            return 0;
        }
        return a_known ? 1 : -1;
    }
    if (a < b) {
        return -1;
    }
    return a > b ? 1 : 0;
}

/* ------------------------------------------------------------------ */
/* Candidates                                                          */
/* ------------------------------------------------------------------ */

/* ------------------------------------------------------------------ */
/* The collected cascade                                               */
/* ------------------------------------------------------------------ */

void robo_css_cascade_init(robo_css_cascade_t *cascade)
{
    if (!cascade) {
        return;
    }
    memset(cascade, 0, sizeof(*cascade));
    robo_css_declaration_list_init(&cascade->inline_declarations);
    cascade->element = ROBO_CSS_ELEMENT_REF_NONE;
}

void robo_css_cascade_dispose(robo_css_cascade_t *cascade)
{
    if (!cascade) {
        return;
    }
    robo_css_declaration_list_dispose(&cascade->inline_declarations);
    if (cascade->items) {
        robo_free(cascade->items);
    }
    robo_css_cascade_init(cascade);
}

size_t robo_css_cascade_count(const robo_css_cascade_t *cascade)
{
    return cascade ? cascade->count : 0u;
}

const robo_css_cascade_candidate_t *robo_css_cascade_at(
    const robo_css_cascade_t *cascade,
    size_t index)
{
    if (!cascade || index >= cascade->count) {
        return NULL;
    }
    return &cascade->items[index];
}

int robo_css_cascade_is_complete(const robo_css_cascade_t *cascade)
{
    return cascade ? !cascade->unsupported : 0;
}

/* ------------------------------------------------------------------ */
/* Verdicts                                                            */
/* ------------------------------------------------------------------ */

int robo_css_cascade_verdict_is_known(robo_css_cascade_verdict_t verdict)
{
    return verdict >= ROBO_CSS_CASCADE_VERDICT_NONE &&
           verdict <= ROBO_CSS_CASCADE_VERDICT_DECIDED;
}

const char *robo_css_cascade_verdict_name(robo_css_cascade_verdict_t verdict)
{
    switch (verdict) {
    case ROBO_CSS_CASCADE_VERDICT_NONE:
        return "none";
    case ROBO_CSS_CASCADE_VERDICT_DECIDED:
        return "decided";
    default:
        return "unknown";
    }
}

/* ------------------------------------------------------------------ */
/* Resolution                                                          */
/* ------------------------------------------------------------------ */

/**
 * The shared core of the two resolve doors.
 *
 * `by_name` chooses which identity a candidate is tested against: the property
 * id the context resolved for it, or the name slice the author wrote. The two
 * are not interchangeable — a declaration whose name never resolved still has a
 * name — which is why both doors exist rather than one with a fallback.
 */
static robo_css_cascade_verdict_t cascade_resolve(
    const robo_css_cascade_t *cascade,
    robo_css_property_id_t property,
    int by_name,
    robo_css_slice_t name,
    const robo_css_cascade_candidate_t **out_winner)
{
    const robo_css_cascade_candidate_t *best = NULL;
    robo_css_cascade_verdict_t verdict = ROBO_CSS_CASCADE_VERDICT_NONE;
    size_t index;

    if (out_winner) {
        *out_winner = NULL;
    }
    if (!cascade) {
        return ROBO_CSS_CASCADE_VERDICT_NONE;
    }
    if (by_name) {
        if (robo_css_slice_is_absent(name)) {
            return ROBO_CSS_CASCADE_VERDICT_NONE;
        }
    } else if (property == ROBO_CSS_PROPERTY_ID_NONE) {
        /* NONE is not a property; every unresolved declaration would answer to
         * it, which would report a winner that declares nothing in particular. */
        return ROBO_CSS_CASCADE_VERDICT_NONE;
    }

    /* The array is in cascade order, so a candidate can only ever be at the
     * current level or above it; that is what makes one forward pass enough. */
    for (index = 0u; index < cascade->count; index++) {
        const robo_css_cascade_candidate_t *candidate = &cascade->items[index];
        int matches = by_name
                          ? robo_css_slice_equals_ci(
                                robo_css_declaration_name(candidate->declaration), name)
                          : candidate->property == property;

        if (!matches) {
            continue;
        }
        if (verdict == ROBO_CSS_CASCADE_VERDICT_NONE) {
            best = candidate;
            verdict = ROBO_CSS_CASCADE_VERDICT_DECIDED;
            continue;
        }
        {
            int level_order =
                robo_css_cascade_level_compare(candidate->level, best->level);

            if (level_order > 0) {
                /* A strictly stronger level: nothing the weaker level said can
                 * matter, so the incumbent is replaced outright. */
                best = candidate;
                continue;
            }
            if (level_order < 0) {
                /* Cannot happen in cascade order; refusing to read it as a win
                 * is cheaper than being wrong if the order is ever broken. */
                continue;
            }
        }
        /* Same level, so specificity decides — through the one implementation,
         * never by arithmetic here. A greater specificity displaces the
         * incumbent, and an *equal* one does too: the array is in document order
         * within a level, so the candidate being looked at is the later of the
         * two and the tie-break is exactly "later wins". */
        if (robo_css_specificity_compare(&candidate->specificity,
                                         &best->specificity) >= 0) {
            best = candidate;
        }
    }

    if (verdict == ROBO_CSS_CASCADE_VERDICT_DECIDED && out_winner) {
        *out_winner = best;
    }
    return verdict;
}

robo_css_cascade_verdict_t robo_css_cascade_resolve_property(
    const robo_css_cascade_t *cascade,
    robo_css_property_id_t property,
    const robo_css_cascade_candidate_t **out_winner)
{
    return cascade_resolve(cascade, property, 0, robo_css_slice_empty(), out_winner);
}

robo_css_cascade_verdict_t robo_css_cascade_resolve_name(
    const robo_css_cascade_t *cascade,
    robo_css_slice_t name,
    const robo_css_cascade_candidate_t **out_winner)
{
    return cascade_resolve(cascade, ROBO_CSS_PROPERTY_ID_NONE, 1, name, out_winner);
}

size_t robo_css_cascade_count_of(const robo_css_cascade_t *cascade,
                                 robo_css_property_id_t property)
{
    size_t index;
    size_t total = 0u;

    if (!cascade || property == ROBO_CSS_PROPERTY_ID_NONE) {
        return 0u;
    }
    for (index = 0u; index < cascade->count; index++) {
        if (cascade->items[index].property == property) {
            total++;
        }
    }
    return total;
}

/* ------------------------------------------------------------------ */
/* Collection                                                          */
/* ------------------------------------------------------------------ */

typedef struct cascade_walk {
    robo_css_cascade_t *cascade;
    const robo_css_element_host_t *host;
    const robo_css_property_table_t *table;
    robo_css_error_t *out_error;
    /** Non-zero once the call's first CSS problem has been recorded. */
    int error_recorded;
    /** Document-order cursor; written to each candidate as it is appended. */
    size_t order;
} cascade_walk_t;

/* The first problem is the one that explains the rest (the 9.3 rule). */
static void cascade_note_error(cascade_walk_t *walk, const robo_css_error_t *error)
{
    if (walk->error_recorded || !robo_css_error_is_error(error)) {
        return;
    }
    if (walk->out_error) {
        *walk->out_error = *error;
    }
    walk->error_recorded = 1;
}

/**
 * Append one candidate, growing the array. Returns 0 on allocation failure,
 * leaving the cascade exactly as it was.
 */
static int cascade_append(cascade_walk_t *walk,
                         const robo_css_declaration_t *declaration,
                         robo_css_origin_t origin,
                         robo_css_specificity_t specificity,
                         robo_css_stylesheet_id_t sheet,
                         size_t sheet_index,
                         size_t rule_index,
                         size_t selector_index,
                         size_t declaration_index)
{
    robo_css_cascade_t *cascade = walk->cascade;
    robo_css_cascade_candidate_t *slot;

    if (!robo_css_grow((void **)&cascade->items, &cascade->capacity, cascade->count,
                       sizeof(*cascade->items))) {
        return 0;
    }
    slot = &cascade->items[cascade->count];
    memset(slot, 0, sizeof(*slot));
    slot->declaration = declaration;
    slot->origin = origin;
    slot->important = declaration->important ? 1 : 0;
    slot->level = robo_css_cascade_level_of(origin, slot->important);
    /* Carried, not recomputed: the specificity belongs to the selector that
     * matched, and only the matching step knows which one that was. */
    slot->specificity = specificity;
    slot->property = walk->table
                         ? robo_css_property_table_lookup_slice(walk->table,
                                                                declaration->name)
                         : ROBO_CSS_PROPERTY_ID_NONE;
    slot->order = walk->order++;
    slot->sheet = sheet;
    slot->sheet_index = sheet_index;
    slot->rule_index = rule_index;
    slot->selector_index = selector_index;
    slot->declaration_index = declaration_index;
    cascade->count++;
    return 1;
}

/** Every usable declaration of one matched rule, in source order. */
static int cascade_append_rule_declarations(cascade_walk_t *walk,
                                           const robo_css_style_rule_t *rule,
                                           robo_css_origin_t origin,
                                           robo_css_specificity_t specificity,
                                           robo_css_stylesheet_id_t sheet,
                                           size_t sheet_index,
                                           size_t rule_index,
                                           size_t selector_index)
{
    size_t index;

    for (index = 0u; index < robo_css_style_rule_declaration_count(rule); index++) {
        const robo_css_declaration_t *declaration =
            robo_css_style_rule_declaration(rule, index);

        if (!robo_css_declaration_is_usable(declaration)) {
            /* A malformed declaration is a *record of a problem* the parse kept
             * (9.3), not a declaration: it declares nothing on any element. */
            walk->cascade->declarations_ignored++;
            continue;
        }
        if (!cascade_append(walk, declaration, origin, specificity, sheet, sheet_index,
                            rule_index, selector_index, index)) {
            return 0;
        }
    }
    return 1;
}

/**
 * Match a rule's selector list against the element and report the specificity of
 * the selector that matched.
 *
 * Three answers, and the third is why this is a loop rather than one call to
 * robo_css_selector_list_matches.
 *
 * A selector *list* does not have a specificity. Each of its members does, and
 * CSS says the value in force for a match is that of the most specific member
 * that matches the element — not the first one to match, and not the maximum of
 * the whole list, which could be a member that does not apply. So the walk
 * cannot stop at the first match: it has to see every member that matches and
 * keep the greatest. `div, #main` on `<div id="main">` is (1,0,0), and a cascade
 * that took `div`'s (0,0,1) because it came first would sort the rule in the
 * wrong place on every element the two share.
 *
 * The matching itself is the 9.4 matcher's — one call per complex selector — so
 * nothing about *how* a selector matches is restated here. What is here is only
 * the list-level rule the per-selector door does not implement.
 *
 * @return 1 matched, with @p out_specificity and @p out_selector_index filled
 *         (the index the matcher would report, i.e. a member of the winning
 *         specificity); 0 when no member matched; -1 when no member matched and
 *         at least one could not be evaluated, which is not a negative.
 */
static int cascade_match_rule_selectors(const cascade_walk_t *walk,
                                        const robo_css_selector_list_t *selectors,
                                        robo_css_specificity_t *out_specificity,
                                        size_t *out_selector_index)
{
    size_t count = robo_css_selector_list_count(selectors);
    robo_css_specificity_t best;
    size_t best_index = 0u;
    int matched = 0;
    int undecided = 0;
    size_t i;

    robo_css_specificity_init(&best);
    for (i = 0u; i < count; i++) {
        const robo_css_complex_selector_t *selector =
            robo_css_selector_list_at(selectors, i);
        robo_css_selector_match_t attempt;
        int verdict;

        if (!robo_css_complex_selector_is_well_formed(selector)) {
            /* A combinator with no compound on one side is a broken record, not
             * a selector that failed to match — the same distinction the list
             * door draws. */
            continue;
        }
        robo_css_selector_match_init(&attempt);
        verdict = robo_css_complex_selector_matches(selector, walk->host,
                                                    walk->cascade->element, &attempt,
                                                    NULL);
        if (verdict < 0) {
            continue;
        }
        if (verdict == 0) {
            if (attempt.unsupported) {
                undecided = 1;
            }
            continue;
        }
        if (!matched) {
            matched = 1;
            best = robo_css_specificity_of_list_item(selectors, i);
            best_index = i;
            continue;
        }
        {
            robo_css_specificity_t candidate =
                robo_css_specificity_of_list_item(selectors, i);

            /* Strictly greater, so two members of equal specificity leave the
             * earlier index reported; either is the same answer to the question
             * this function exists to answer. */
            if (robo_css_specificity_compare(&candidate, &best) > 0) {
                best = candidate;
                best_index = i;
            }
        }
    }
    if (matched) {
        *out_specificity = best;
        *out_selector_index = best_index;
        return 1;
    }
    return undecided ? -1 : 0;
}

/**
 * One stylesheet: its top-level rules, matched and collected.
 *
 * At-rules contribute nothing. `@media`'s block is not evaluated, `@import`'s
 * target is not fetched, and the model has no at-rule table to consult (9.3's
 * decision, and still the right one) — so the only honest thing to do with a
 * block is to say that it is there. That is why an at-rule with a block marks
 * the cascade incomplete while one without does not: `@charset "utf-8";` hides
 * nothing, and a block may hold rules that would have matched.
 */
static int cascade_walk_sheet(cascade_walk_t *walk,
                             robo_css_origin_t origin,
                             robo_css_stylesheet_id_t sheet,
                             size_t sheet_index)
{
    robo_css_cascade_t *cascade = walk->cascade;
    size_t rule_count;
    size_t index;

    if (robo_css_stylesheet_origin(sheet) != origin) {
        /* The context's list is what places a sheet — it *is* the cascade
         * order — but a sheet that records a different origin than the one it
         * is listed under is a caller mistake worth reporting rather than
         * smoothing over. */
        cascade->sheets_origin_mismatch++;
    }
    cascade->sheets_scanned++;
    rule_count = robo_css_stylesheet_rule_count(sheet);

    for (index = 0u; index < rule_count; index++) {
        const robo_css_rule_t *rule = robo_css_stylesheet_rule(sheet, index);
        const robo_css_style_rule_t *style = robo_css_rule_style(rule);
        const robo_css_selector_list_t *selectors;
        robo_css_specificity_t specificity;
        size_t matched_selector = 0u;
        int verdict;

        if (!style) {
            const robo_css_at_rule_t *at = robo_css_rule_at(rule);

            cascade->at_rules_skipped++;
            if (at && at->has_block) {
                cascade->at_rules_with_blocks++;
                cascade->unsupported = 1;
            }
            continue;
        }
        cascade->rules_scanned++;
        selectors = robo_css_style_rule_selectors(style);
        if (robo_css_selector_list_count(selectors) == 0u) {
            /* Either the prelude was never parsed as selectors or it was not a
             * valid list, and this build cannot tell those apart. Both mean the
             * rule's reach is unknown, so the answer is incomplete — which is
             * also what tells a caller to call
             * robo_css_style_rule_parse_selectors first. */
            cascade->rules_without_selectors++;
            cascade->unsupported = 1;
            continue;
        }
        if (!robo_css_element_host_is_usable(walk->host)) {
            cascade->rules_undecided++;
            cascade->unsupported = 1;
            continue;
        }
        robo_css_specificity_init(&specificity);
        verdict = cascade_match_rule_selectors(walk, selectors, &specificity,
                                               &matched_selector);
        if (verdict < 0) {
            /* No member matched and at least one could not be evaluated: a rule
             * whose reach is unknown, which is an incomplete answer rather than
             * a negative. */
            cascade->rules_undecided++;
            cascade->unsupported = 1;
            continue;
        }
        if (verdict == 0) {
            continue;
        }
        cascade->rules_matched++;
        if (!cascade_append_rule_declarations(walk, style, origin, specificity, sheet,
                                              sheet_index, index, matched_selector)) {
            return 0;
        }
    }
    return 1;
}

/** The element's own `style` attribute: asked for, parsed, collected. */
static robo_result_t cascade_walk_inline(cascade_walk_t *walk,
                                        robo_css_inline_style_fn provider,
                                        void *userdata)
{
    robo_css_cascade_t *cascade = walk->cascade;
    robo_css_slice_t text = robo_css_slice_empty();
    robo_css_error_t error = ROBO_CSS_CLEARED;
    robo_css_specificity_t specificity;
    robo_result_t rc;
    size_t index;

    /* Every inline declaration has the *same* specificity, and it is zero: a
     * `style` attribute declares through no selector at all. Its strength is the
     * INLINE level, which no selector reaches, so it never meets a selector in a
     * specificity comparison and this value is never the reason anything wins. */
    robo_css_specificity_init(&specificity);
    cascade->inline_checked = 1;
    rc = provider(userdata, cascade->element, &text);
    if (rc == ROBO_NOT_FOUND) {
        /* No `style` attribute at all, which is not the same as `style=""`:
         * the first declares nothing because there is nothing, the second
         * because its text is empty. Both end up declaring nothing here. */
        return ROBO_OK;
    }
    if (rc != ROBO_OK) {
        /* The host could not answer. That is not the cascade failing, but it is
         * the cascade being incomplete, and saying so is the difference between
         * a lower bound and a wrong answer. */
        robo_css_error_set(&cascade->inline_error, ROBO_CSS_ERROR_KIND_UNSUPPORTED, rc,
                           NULL, "the host did not provide the element's style attribute");
        cascade->has_inline_error = 1;
        cascade->unsupported = 1;
        cascade_note_error(walk, &cascade->inline_error);
        return ROBO_OK;
    }
    if (robo_css_slice_is_absent(text)) {
        if (text.length != 0u) {
            /* No bytes but a length: nothing can be parsed from that. */
            robo_css_error_set(&cascade->inline_error,
                               ROBO_CSS_ERROR_KIND_UNSUPPORTED, ROBO_INVALID_ARGUMENT,
                               NULL,
                               "the host returned a style attribute with no bytes");
            cascade->has_inline_error = 1;
            cascade->unsupported = 1;
            cascade_note_error(walk, &cascade->inline_error);
        }
        return ROBO_OK;
    }
    cascade->inline_present = 1;
    rc = robo_css_declaration_list_parse_text((const char *)text.bytes, text.length,
                                              ROBO_CSS_ORIGIN_INLINE,
                                              &cascade->inline_declarations, &error);
    if (rc == ROBO_OUT_OF_MEMORY) {
        return ROBO_OUT_OF_MEMORY;
    }
    if (rc == ROBO_INVALID_ARGUMENT) {
        /* The parse refused the input itself: record it and stay incomplete
         * rather than pretending the element had no inline declarations. */
        robo_css_error_set(&cascade->inline_error, ROBO_CSS_ERROR_KIND_UNSUPPORTED, rc,
                           NULL, "the element's style attribute could not be parsed");
        cascade->has_inline_error = 1;
        cascade->unsupported = 1;
        cascade_note_error(walk, &cascade->inline_error);
        return ROBO_OK;
    }
    if (robo_css_error_is_error(&error)) {
        /* Malformed inline CSS is recovered from exactly as a malformed rule is:
         * the valid declarations are collected and the problem is reported. */
        cascade->inline_error = error;
        cascade->has_inline_error = 1;
        cascade_note_error(walk, &error);
    }
    for (index = 0u; index < robo_css_declaration_list_count(&cascade->inline_declarations);
         index++) {
        const robo_css_declaration_t *declaration =
            robo_css_declaration_list_at(&cascade->inline_declarations, index);

        if (!robo_css_declaration_is_usable(declaration)) {
            cascade->declarations_ignored++;
            continue;
        }
        if (!cascade_append(walk, declaration, ROBO_CSS_ORIGIN_INLINE, specificity,
                            ROBO_CSS_STYLESHEET_ID_NONE, ROBO_CSS_CASCADE_NO_INDEX,
                            ROBO_CSS_CASCADE_NO_INDEX, ROBO_CSS_CASCADE_NO_INDEX, index)) {
            return ROBO_OUT_OF_MEMORY;
        }
    }
    return ROBO_OK;
}

/**
 * Which level bucket a candidate belongs in.
 *
 * A candidate's level always comes from the table above, so an out-of-range one
 * cannot happen; the clamp exists so that the counting pass and the placement
 * pass cannot disagree about where such a candidate goes, which is the only way
 * a counting sort can lose an element.
 */
static size_t cascade_bucket(const robo_css_cascade_candidate_t *candidate)
{
    int level = (int)candidate->level;

    if (level < 0 || level >= (int)ROBO_CSS_CASCADE_LEVEL_COUNT) {
        return 0u;
    }
    return (size_t)level;
}

/**
 * Put the candidates in cascade order: ascending level, and within a level the
 * document order they were met in.
 *
 * One pass to count, one to place, and the placement is stable because it walks
 * the array in its original (document) order — which is exactly the tie-break
 * inside a level, so no comparison function is needed and no sort can disagree
 * with the specification about ties.
 */
static int cascade_order_by_level(robo_css_cascade_t *cascade)
{
    size_t counts[ROBO_CSS_CASCADE_LEVEL_COUNT];
    size_t offsets[ROBO_CSS_CASCADE_LEVEL_COUNT];
    robo_css_cascade_candidate_t *sorted;
    size_t level;
    size_t index;

    if (cascade->count < 2u) {
        return 1;
    }
    if (cascade->count > (size_t)-1 / sizeof(*sorted)) {
        return 0;
    }
    for (level = 0u; level < ROBO_CSS_CASCADE_LEVEL_COUNT; level++) {
        counts[level] = 0u;
    }
    for (index = 0u; index < cascade->count; index++) {
        counts[cascade_bucket(&cascade->items[index])]++;
    }
    offsets[0] = 0u;
    for (level = 1u; level < ROBO_CSS_CASCADE_LEVEL_COUNT; level++) {
        offsets[level] = offsets[level - 1u] + counts[level - 1u];
    }
    sorted = (robo_css_cascade_candidate_t *)robo_malloc(
        cascade->count * sizeof(*sorted));
    if (!sorted) {
        return 0;
    }
    for (index = 0u; index < cascade->count; index++) {
        size_t bucket = cascade_bucket(&cascade->items[index]);

        sorted[offsets[bucket]] = cascade->items[index];
        offsets[bucket]++;
    }
    robo_free(cascade->items);
    cascade->items = sorted;
    cascade->capacity = cascade->count;
    return 1;
}

robo_result_t robo_css_cascade_collect(const robo_css_style_context_t *context,
                                       robo_css_element_ref_t element,
                                       robo_css_cascade_t *out,
                                       robo_css_error_t *out_error,
                                       robo_css_diag_t *diag)
{
    static const robo_css_origin_t origins_in_level_order[] = {
        ROBO_CSS_ORIGIN_RUNTIME, ROBO_CSS_ORIGIN_USER, ROBO_CSS_ORIGIN_AUTHOR
    };
    cascade_walk_t walk;
    robo_css_element_host_t host;
    robo_css_inline_style_fn inline_provider;
    void *inline_userdata;
    robo_result_t rc = ROBO_OK;
    size_t origin_index;

    robo_css_cascade_init(out);
    out->element = element;
    memset(&walk, 0, sizeof(walk));
    walk.cascade = out;
    walk.out_error = out_error;
    walk.table = robo_css_style_context_property_table(context);
    host = robo_css_style_context_element_host(context);
    walk.host = &host;

    for (origin_index = 0u;
         origin_index < sizeof(origins_in_level_order) / sizeof(origins_in_level_order[0]);
         origin_index++) {
        robo_css_origin_t origin = origins_in_level_order[origin_index];
        size_t sheet_count = robo_css_style_context_sheet_count(context, origin);
        size_t sheet_index;

        for (sheet_index = 0u; sheet_index < sheet_count; sheet_index++) {
            robo_css_stylesheet_id_t sheet =
                robo_css_style_context_sheet(context, origin, sheet_index);

            if (!robo_css_stylesheet_is_live(sheet)) {
                /* A context refuses a dead id when it is added, so this is a
                 * sheet that was destroyed while it was still referenced. */
                out->sheets_unavailable++;
                out->unsupported = 1;
                continue;
            }
            if (!cascade_walk_sheet(&walk, origin, sheet, sheet_index)) {
                rc = ROBO_OUT_OF_MEMORY;
                break;
            }
        }
        if (rc != ROBO_OK) {
            break;
        }
    }

    inline_provider = robo_css_style_context_inline_style(context);
    inline_userdata = robo_css_style_context_inline_userdata(context);
    if (rc == ROBO_OK && inline_provider) {
        rc = cascade_walk_inline(&walk, inline_provider, inline_userdata);
    }

    if (rc == ROBO_OK && !cascade_order_by_level(out)) {
        rc = ROBO_OUT_OF_MEMORY;
    }

    if (rc != ROBO_OK) {
        /* Out of memory means no answer was produced, so the caller gets an
         * empty cascade rather than a partial one it might mistake for a
         * result. */
        robo_css_cascade_dispose(out);
        out->element = element;
        robo_css_error_set(out_error, ROBO_CSS_ERROR_KIND_OUT_OF_MEMORY, rc, NULL,
                           "the cascade could not be collected");
        return ROBO_CSS_DIAG(diag, ROBO_CSS_DOMAIN_STYLE, rc,
                             "the cascade could not be collected");
    }
    if (!walk.error_recorded && out->has_inline_error) {
        cascade_note_error(&walk, &out->inline_error);
    }
    if (!walk.error_recorded) {
        /* Always filled, so a caller that never checks a result code cannot be
         * handed a record describing the previous call. */
        robo_css_error_init(out_error);
    }
    return ROBO_OK;
}
