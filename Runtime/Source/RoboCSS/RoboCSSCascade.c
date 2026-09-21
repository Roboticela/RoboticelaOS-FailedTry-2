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
 * CssSpecificityOfListItem is the one implementation, and this file
 * only ever calls CssSpecificityCompare.
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
static const CssCascadeLevel cascadeLevelTable[RoboCssOriginCount][2] = {
    /* ORIGIN_NONE */ {RoboCssCascadeLevelNone, RoboCssCascadeLevelNone},
    /* RUNTIME     */ {RoboCssCascadeLevelRuntime,
                       RoboCssCascadeLevelRuntimeImportant},
    /* USER        */ {RoboCssCascadeLevelUser,
                       RoboCssCascadeLevelUserImportant},
    /* AUTHOR      */ {RoboCssCascadeLevelAuthor,
                       RoboCssCascadeLevelAuthorImportant},
    /* INLINE      */ {RoboCssCascadeLevelInline,
                       RoboCssCascadeLevelInlineImportant}
};

static const char *const cascadeLevelNames[RoboCssCascadeLevelCount] = {
    "runtime",
    "user",
    "author",
    "inline",
    "author important",
    "inline important",
    "user important",
    "runtime important"
};

int CssCascadeLevelIsKnown(CssCascadeLevel level)
{
    return level >= RoboCssCascadeLevelRuntime &&
           level <= RoboCssCascadeLevelRuntimeImportant;
}

const char *CssCascadeLevelName(CssCascadeLevel level)
{
    if (level == RoboCssCascadeLevelNone) {
        return "none";
    }
    if (!CssCascadeLevelIsKnown(level)) {
        return "unknown";
    }
    return cascadeLevelNames[(size_t)level];
}

CssCascadeLevel CssCascadeLevelOf(CssOrigin origin,
                                                   int important)
{
    if (!CssOriginIsKnown(origin)) {
        return RoboCssCascadeLevelNone;
    }
    return cascadeLevelTable[(size_t)origin][important ? 1 : 0];
}

int CssCascadeLevelHolds(CssCascadeLevel level,
                                 CssOrigin origin,
                                 int important)
{
    if (!CssCascadeLevelIsKnown(level)) {
        return 0;
    }
    return CssCascadeLevelOf(origin, important) == level;
}

int CssCascadeLevelCompare(CssCascadeLevel a,
                                   CssCascadeLevel b)
{
    int aKnown = CssCascadeLevelIsKnown(a);
    int bKnown = CssCascadeLevelIsKnown(b);

    /* An unknown level is not a level, so it is weaker than any real one; two
     * unknowns have nothing to rank them by and compare equal. */
    if (!aKnown || !bKnown) {
        if (!aKnown && !bKnown) {
            return 0;
        }
        return aKnown ? 1 : -1;
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

void CssCascadeInit(CssCascade *cascade)
{
    if (!cascade) {
        return;
    }
    memset(cascade, 0, sizeof(*cascade));
    CssDeclarationListInit(&cascade->inlineDeclarations);
    cascade->element = RoboCssElementRefNone;
}

void CssCascadeDispose(CssCascade *cascade)
{
    if (!cascade) {
        return;
    }
    CssDeclarationListDispose(&cascade->inlineDeclarations);
    if (cascade->items) {
        RoboFree(cascade->items);
    }
    CssCascadeInit(cascade);
}

size_t CssCascadeCount(const CssCascade *cascade)
{
    return cascade ? cascade->count : 0u;
}

const CssCascadeCandidate *CssCascadeAt(
    const CssCascade *cascade,
    size_t index)
{
    if (!cascade || index >= cascade->count) {
        return NULL;
    }
    return &cascade->items[index];
}

int CssCascadeIsComplete(const CssCascade *cascade)
{
    return cascade ? !cascade->unsupported : 0;
}

/* ------------------------------------------------------------------ */
/* Verdicts                                                            */
/* ------------------------------------------------------------------ */

int CssCascadeVerdictIsKnown(CssCascadeVerdict verdict)
{
    return verdict >= RoboCssCascadeVerdictNone &&
           verdict <= RoboCssCascadeVerdictDecided;
}

const char *CssCascadeVerdictName(CssCascadeVerdict verdict)
{
    switch (verdict) {
    case RoboCssCascadeVerdictNone:
        return "none";
    case RoboCssCascadeVerdictDecided:
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
 * `byName` chooses which identity a candidate is tested against: the property
 * id the context resolved for it, or the name slice the author wrote. The two
 * are not interchangeable — a declaration whose name never resolved still has a
 * name — which is why both doors exist rather than one with a fallback.
 */
static CssCascadeVerdict CascadeResolve(
    const CssCascade *cascade,
    CssPropertyId property,
    int byName,
    CssSlice name,
    const CssCascadeCandidate **outWinner)
{
    const CssCascadeCandidate *best = NULL;
    CssCascadeVerdict verdict = RoboCssCascadeVerdictNone;
    size_t index;

    if (outWinner) {
        *outWinner = NULL;
    }
    if (!cascade) {
        return RoboCssCascadeVerdictNone;
    }
    if (byName) {
        if (CssSliceIsAbsent(name)) {
            return RoboCssCascadeVerdictNone;
        }
    } else if (property == RoboCssPropertyIdNone) {
        /* NONE is not a property; every unresolved declaration would answer to
         * it, which would report a winner that declares nothing in particular. */
        return RoboCssCascadeVerdictNone;
    }

    /* The array is in cascade order, so a candidate can only ever be at the
     * current level or above it; that is what makes one forward pass enough. */
    for (index = 0u; index < cascade->count; index++) {
        const CssCascadeCandidate *candidate = &cascade->items[index];
        int matches = byName
                          ? CssSliceEqualsCi(
                                CssDeclarationName(candidate->declaration), name)
                          : candidate->property == property;

        if (!matches) {
            continue;
        }
        if (verdict == RoboCssCascadeVerdictNone) {
            best = candidate;
            verdict = RoboCssCascadeVerdictDecided;
            continue;
        }
        {
            int levelOrder =
                CssCascadeLevelCompare(candidate->level, best->level);

            if (levelOrder > 0) {
                /* A strictly stronger level: nothing the weaker level said can
                 * matter, so the incumbent is replaced outright. */
                best = candidate;
                continue;
            }
            if (levelOrder < 0) {
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
        if (CssSpecificityCompare(&candidate->specificity,
                                         &best->specificity) >= 0) {
            best = candidate;
        }
    }

    if (verdict == RoboCssCascadeVerdictDecided && outWinner) {
        *outWinner = best;
    }
    return verdict;
}

int CssCascadeCandidateCompare(const CssCascade *cascade,
                                       const CssCascadeCandidate *a,
                                       const CssCascadeCandidate *b)
{
    int levelOrder;
    int specificityOrder;
    size_t index;
    size_t aIndex = (size_t)-1;
    size_t bIndex = (size_t)-1;

    if (a == b) {
        return 0;
    }
    if (!a || !b) {
        /* A missing candidate is not a weaker one; there is nothing to order. */
        return 0;
    }
    levelOrder = CssCascadeLevelCompare(a->level, b->level);
    if (levelOrder != 0) {
        return levelOrder > 0 ? -1 : 1;
    }
    /* Same level, so specificity decides — through the one implementation. */
    specificityOrder = CssSpecificityCompare(&b->specificity, &a->specificity);
    if (specificityOrder != 0) {
        return specificityOrder > 0 ? 1 : -1;
    }
    if (!cascade || !cascade->items) {
        return 0;
    }
    for (index = 0u; index < cascade->count; index++) {
        if (&cascade->items[index] == a) {
            aIndex = index;
        }
        if (&cascade->items[index] == b) {
            bIndex = index;
        }
    }
    if (aIndex == (size_t)-1 || bIndex == (size_t)-1) {
        return 0;
    }
    /* Equal level and equal specificity: the array is in document order within
     * a level, so the later position is the later declaration and it wins — the
     * same tie-break the resolution pass applies by walking forward. */
    return aIndex > bIndex ? -1 : 1;
}

CssCascadeVerdict CssCascadeResolveProperty(
    const CssCascade *cascade,
    CssPropertyId property,
    const CssCascadeCandidate **outWinner)
{
    return CascadeResolve(cascade, property, 0, CssSliceEmpty(), outWinner);
}

CssCascadeVerdict CssCascadeResolveName(
    const CssCascade *cascade,
    CssSlice name,
    const CssCascadeCandidate **outWinner)
{
    return CascadeResolve(cascade, RoboCssPropertyIdNone, 1, name, outWinner);
}

size_t CssCascadeCountOf(const CssCascade *cascade,
                                 CssPropertyId property)
{
    size_t index;
    size_t total = 0u;

    if (!cascade || property == RoboCssPropertyIdNone) {
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

typedef struct cascadeWalk {
    CssCascade *cascade;
    const CssElementHost *host;
    const CssPropertyTable *table;
    CssError *outError;
    /** Non-zero once the call's first CSS problem has been recorded. */
    int errorRecorded;
    /** Document-order cursor; written to each candidate as it is appended. */
    size_t order;
} CascadeWalk;

/* The first problem is the one that explains the rest (the 9.3 rule). */
static void CascadeNoteError(CascadeWalk *walk, const CssError *error)
{
    if (walk->errorRecorded || !CssErrorIsError(error)) {
        return;
    }
    if (walk->outError) {
        *walk->outError = *error;
    }
    walk->errorRecorded = 1;
}

/**
 * Append one candidate, growing the array. Returns 0 on allocation failure,
 * leaving the cascade exactly as it was.
 */
static int CascadeAppend(CascadeWalk *walk,
                         const CssDeclaration *declaration,
                         CssOrigin origin,
                         CssSpecificity specificity,
                         CssStylesheetId sheet,
                         size_t sheetIndex,
                         size_t ruleIndex,
                         size_t selectorIndex,
                         size_t declarationIndex)
{
    CssCascade *cascade = walk->cascade;
    CssCascadeCandidate *slot;

    if (!CssGrow((void **)&cascade->items, &cascade->capacity, cascade->count,
                       sizeof(*cascade->items))) {
        return 0;
    }
    slot = &cascade->items[cascade->count];
    memset(slot, 0, sizeof(*slot));
    slot->declaration = declaration;
    slot->origin = origin;
    slot->important = declaration->important ? 1 : 0;
    slot->level = CssCascadeLevelOf(origin, slot->important);
    /* Carried, not recomputed: the specificity belongs to the selector that
     * matched, and only the matching step knows which one that was. */
    slot->specificity = specificity;
    slot->property = walk->table
                         ? CssPropertyTableLookupSlice(walk->table,
                                                                declaration->name)
                         : RoboCssPropertyIdNone;
    slot->order = walk->order++;
    slot->sheet = sheet;
    slot->sheetIndex = sheetIndex;
    slot->ruleIndex = ruleIndex;
    slot->selectorIndex = selectorIndex;
    slot->declarationIndex = declarationIndex;
    cascade->count++;
    return 1;
}

/** Every usable declaration of one matched rule, in source order. */
static int CascadeAppendRuleDeclarations(CascadeWalk *walk,
                                           const CssStyleRule *rule,
                                           CssOrigin origin,
                                           CssSpecificity specificity,
                                           CssStylesheetId sheet,
                                           size_t sheetIndex,
                                           size_t ruleIndex,
                                           size_t selectorIndex)
{
    size_t index;

    for (index = 0u; index < CssStyleRuleDeclarationCount(rule); index++) {
        const CssDeclaration *declaration =
            CssStyleRuleDeclaration(rule, index);

        if (!CssDeclarationIsUsable(declaration)) {
            /* A malformed declaration is a *record of a problem* the parse kept
             * (9.3), not a declaration: it declares nothing on any element. */
            walk->cascade->declarationsIgnored++;
            continue;
        }
        if (!CascadeAppend(walk, declaration, origin, specificity, sheet, sheetIndex,
                            ruleIndex, selectorIndex, index)) {
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
 * CssSelectorListMatches.
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
 * @return 1 matched, with @p outSpecificity and @p outSelectorIndex filled
 *         (the index the matcher would report, i.e. a member of the winning
 *         specificity); 0 when no member matched; -1 when no member matched and
 *         at least one could not be evaluated, which is not a negative.
 */
static int CascadeMatchRuleSelectors(const CascadeWalk *walk,
                                        const CssSelectorList *selectors,
                                        CssSpecificity *outSpecificity,
                                        size_t *outSelectorIndex)
{
    size_t count = CssSelectorListCount(selectors);
    CssSpecificity best;
    size_t bestIndex = 0u;
    int matched = 0;
    int undecided = 0;
    size_t i;

    CssSpecificityInit(&best);
    for (i = 0u; i < count; i++) {
        const CssComplexSelector *selector =
            CssSelectorListAt(selectors, i);
        CssSelectorMatch attempt;
        int verdict;

        if (!CssComplexSelectorIsWellFormed(selector)) {
            /* A combinator with no compound on one side is a broken record, not
             * a selector that failed to match — the same distinction the list
             * door draws. */
            continue;
        }
        CssSelectorMatchInit(&attempt);
        verdict = CssComplexSelectorMatches(selector, walk->host,
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
            best = CssSpecificityOfListItem(selectors, i);
            bestIndex = i;
            continue;
        }
        {
            CssSpecificity candidate =
                CssSpecificityOfListItem(selectors, i);

            /* Strictly greater, so two members of equal specificity leave the
             * earlier index reported; either is the same answer to the question
             * this function exists to answer. */
            if (CssSpecificityCompare(&candidate, &best) > 0) {
                best = candidate;
                bestIndex = i;
            }
        }
    }
    if (matched) {
        *outSpecificity = best;
        *outSelectorIndex = bestIndex;
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
static int CascadeWalkSheet(CascadeWalk *walk,
                             CssOrigin origin,
                             CssStylesheetId sheet,
                             size_t sheetIndex)
{
    CssCascade *cascade = walk->cascade;
    size_t ruleCount;
    size_t index;

    if (CssStylesheetOrigin(sheet) != origin) {
        /* The context's list is what places a sheet — it *is* the cascade
         * order — but a sheet that records a different origin than the one it
         * is listed under is a caller mistake worth reporting rather than
         * smoothing over. */
        cascade->sheetsOriginMismatch++;
    }
    cascade->sheetsScanned++;
    ruleCount = CssStylesheetRuleCount(sheet);

    for (index = 0u; index < ruleCount; index++) {
        const CssRule *rule = CssStylesheetRule(sheet, index);
        const CssStyleRule *style = CssRuleStyle(rule);
        const CssSelectorList *selectors;
        CssSpecificity specificity;
        size_t matchedSelector = 0u;
        int verdict;

        if (!style) {
            const CssAtRule *at = CssRuleAt(rule);

            cascade->atRulesSkipped++;
            if (at && at->hasBlock) {
                cascade->atRulesWithBlocks++;
                cascade->unsupported = 1;
            }
            continue;
        }
        cascade->rulesScanned++;
        selectors = CssStyleRuleSelectors(style);
        if (CssSelectorListCount(selectors) == 0u) {
            /* Either the prelude was never parsed as selectors or it was not a
             * valid list, and this build cannot tell those apart. Both mean the
             * rule's reach is unknown, so the answer is incomplete — which is
             * also what tells a caller to call
             * CssStyleRuleParseSelectors first. */
            cascade->rulesWithoutSelectors++;
            cascade->unsupported = 1;
            continue;
        }
        if (!CssElementHostIsUsable(walk->host)) {
            cascade->rulesUndecided++;
            cascade->unsupported = 1;
            continue;
        }
        CssSpecificityInit(&specificity);
        verdict = CascadeMatchRuleSelectors(walk, selectors, &specificity,
                                               &matchedSelector);
        if (verdict < 0) {
            /* No member matched and at least one could not be evaluated: a rule
             * whose reach is unknown, which is an incomplete answer rather than
             * a negative. */
            cascade->rulesUndecided++;
            cascade->unsupported = 1;
            continue;
        }
        if (verdict == 0) {
            continue;
        }
        cascade->rulesMatched++;
        if (!CascadeAppendRuleDeclarations(walk, style, origin, specificity, sheet,
                                              sheetIndex, index, matchedSelector)) {
            return 0;
        }
    }
    return 1;
}

/** The element's own `style` attribute: asked for, parsed, collected. */
static Result CascadeWalkInline(CascadeWalk *walk,
                                        CssInlineStyleFn provider,
                                        void *userdata)
{
    CssCascade *cascade = walk->cascade;
    CssSlice text = CssSliceEmpty();
    CssError error = RoboCssCleared;
    CssSpecificity specificity;
    Result rc;
    size_t index;

    /* Every inline declaration has the *same* specificity, and it is zero: a
     * `style` attribute declares through no selector at all. Its strength is the
     * INLINE level, which no selector reaches, so it never meets a selector in a
     * specificity comparison and this value is never the reason anything wins. */
    CssSpecificityInit(&specificity);
    cascade->inlineChecked = 1;
    rc = provider(userdata, cascade->element, &text);
    if (rc == RoboNotFound) {
        /* No `style` attribute at all, which is not the same as `style=""`:
         * the first declares nothing because there is nothing, the second
         * because its text is empty. Both end up declaring nothing here. */
        return RoboOk;
    }
    if (rc != RoboOk) {
        /* The host could not answer. That is not the cascade failing, but it is
         * the cascade being incomplete, and saying so is the difference between
         * a lower bound and a wrong answer. */
        CssErrorSet(&cascade->inlineError, RoboCssErrorKindUnsupported, rc,
                           NULL, "the host did not provide the element's style attribute");
        cascade->hasInlineError = 1;
        cascade->unsupported = 1;
        CascadeNoteError(walk, &cascade->inlineError);
        return RoboOk;
    }
    if (CssSliceIsAbsent(text)) {
        if (text.length != 0u) {
            /* No bytes but a length: nothing can be parsed from that. */
            CssErrorSet(&cascade->inlineError,
                               RoboCssErrorKindUnsupported, RoboInvalidArgument,
                               NULL,
                               "the host returned a style attribute with no bytes");
            cascade->hasInlineError = 1;
            cascade->unsupported = 1;
            CascadeNoteError(walk, &cascade->inlineError);
        }
        return RoboOk;
    }
    cascade->inlinePresent = 1;
    rc = CssDeclarationListParseText((const char *)text.bytes, text.length,
                                              RoboCssOriginInline,
                                              &cascade->inlineDeclarations, &error);
    if (rc == RoboOutOfMemory) {
        return RoboOutOfMemory;
    }
    if (rc == RoboInvalidArgument) {
        /* The parse refused the input itself: record it and stay incomplete
         * rather than pretending the element had no inline declarations. */
        CssErrorSet(&cascade->inlineError, RoboCssErrorKindUnsupported, rc,
                           NULL, "the element's style attribute could not be parsed");
        cascade->hasInlineError = 1;
        cascade->unsupported = 1;
        CascadeNoteError(walk, &cascade->inlineError);
        return RoboOk;
    }
    if (CssErrorIsError(&error)) {
        /* Malformed inline CSS is recovered from exactly as a malformed rule is:
         * the valid declarations are collected and the problem is reported. */
        cascade->inlineError = error;
        cascade->hasInlineError = 1;
        CascadeNoteError(walk, &error);
    }
    for (index = 0u; index < CssDeclarationListCount(&cascade->inlineDeclarations);
         index++) {
        const CssDeclaration *declaration =
            CssDeclarationListAt(&cascade->inlineDeclarations, index);

        if (!CssDeclarationIsUsable(declaration)) {
            cascade->declarationsIgnored++;
            continue;
        }
        if (!CascadeAppend(walk, declaration, RoboCssOriginInline, specificity,
                            RoboCssStylesheetIdNone, RoboCssCascadeNoIndex,
                            RoboCssCascadeNoIndex, RoboCssCascadeNoIndex, index)) {
            return RoboOutOfMemory;
        }
    }
    return RoboOk;
}

/**
 * Which level bucket a candidate belongs in.
 *
 * A candidate's level always comes from the table above, so an out-of-range one
 * cannot happen; the clamp exists so that the counting pass and the placement
 * pass cannot disagree about where such a candidate goes, which is the only way
 * a counting sort can lose an element.
 */
static size_t CascadeBucket(const CssCascadeCandidate *candidate)
{
    int level = (int)candidate->level;

    if (level < 0 || level >= (int)RoboCssCascadeLevelCount) {
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
static int CascadeOrderByLevel(CssCascade *cascade)
{
    size_t counts[RoboCssCascadeLevelCount];
    size_t offsets[RoboCssCascadeLevelCount];
    CssCascadeCandidate *sorted;
    size_t level;
    size_t index;

    if (cascade->count < 2u) {
        return 1;
    }
    if (cascade->count > (size_t)-1 / sizeof(*sorted)) {
        return 0;
    }
    for (level = 0u; level < RoboCssCascadeLevelCount; level++) {
        counts[level] = 0u;
    }
    for (index = 0u; index < cascade->count; index++) {
        counts[CascadeBucket(&cascade->items[index])]++;
    }
    offsets[0] = 0u;
    for (level = 1u; level < RoboCssCascadeLevelCount; level++) {
        offsets[level] = offsets[level - 1u] + counts[level - 1u];
    }
    sorted = (CssCascadeCandidate *)Malloc(
        cascade->count * sizeof(*sorted));
    if (!sorted) {
        return 0;
    }
    for (index = 0u; index < cascade->count; index++) {
        size_t bucket = CascadeBucket(&cascade->items[index]);

        sorted[offsets[bucket]] = cascade->items[index];
        offsets[bucket]++;
    }
    RoboFree(cascade->items);
    cascade->items = sorted;
    cascade->capacity = cascade->count;
    return 1;
}

Result RoboCssCascadeCollect(const CssStyleContext *context,
                                       CssElementRef element,
                                       CssCascade *out,
                                       CssError *outError,
                                       CssDiag *diag)
{
    static const CssOrigin originsInLevelOrder[] = {
        RoboCssOriginRuntime, RoboCssOriginUser, RoboCssOriginAuthor
    };
    CascadeWalk walk;
    CssElementHost host;
    CssInlineStyleFn inlineProvider;
    void *inlineUserdata;
    Result rc = RoboOk;
    size_t originIndex;

    CssCascadeInit(out);
    out->element = element;
    memset(&walk, 0, sizeof(walk));
    walk.cascade = out;
    walk.outError = outError;
    walk.table = CssStyleContextPropertyTable(context);
    host = CssStyleContextElementHost(context);
    walk.host = &host;

    for (originIndex = 0u;
         originIndex < sizeof(originsInLevelOrder) / sizeof(originsInLevelOrder[0]);
         originIndex++) {
        CssOrigin origin = originsInLevelOrder[originIndex];
        size_t sheetCount = CssStyleContextSheetCount(context, origin);
        size_t sheetIndex;

        for (sheetIndex = 0u; sheetIndex < sheetCount; sheetIndex++) {
            CssStylesheetId sheet =
                CssStyleContextSheet(context, origin, sheetIndex);

            if (!CssStylesheetIsLive(sheet)) {
                /* A context refuses a dead id when it is added, so this is a
                 * sheet that was destroyed while it was still referenced. */
                out->sheetsUnavailable++;
                out->unsupported = 1;
                continue;
            }
            if (!CascadeWalkSheet(&walk, origin, sheet, sheetIndex)) {
                rc = RoboOutOfMemory;
                break;
            }
        }
        if (rc != RoboOk) {
            break;
        }
    }

    inlineProvider = CssStyleContextInlineStyle(context);
    inlineUserdata = CssStyleContextInlineUserdata(context);
    if (rc == RoboOk && inlineProvider) {
        rc = CascadeWalkInline(&walk, inlineProvider, inlineUserdata);
    }

    if (rc == RoboOk && !CascadeOrderByLevel(out)) {
        rc = RoboOutOfMemory;
    }

    if (rc != RoboOk) {
        /* Out of memory means no answer was produced, so the caller gets an
         * empty cascade rather than a partial one it might mistake for a
         * result. */
        CssCascadeDispose(out);
        out->element = element;
        CssErrorSet(outError, RoboCssErrorKindOutOfMemory, rc, NULL,
                           "the cascade could not be collected");
        return RoboCssDiag(diag, RoboCssDomainStyle, rc,
                             "the cascade could not be collected");
    }
    if (!walk.errorRecorded && out->hasInlineError) {
        CascadeNoteError(&walk, &out->inlineError);
    }
    if (!walk.errorRecorded) {
        /* Always filled, so a caller that never checks a result code cannot be
         * handed a record describing the previous call. */
        CssErrorInit(outError);
    }
    return RoboOk;
}
