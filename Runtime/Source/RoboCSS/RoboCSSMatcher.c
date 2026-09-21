/*
 * RoboCSS selector matching (Phase 9.4) — see Include/RoboCSS/RoboCSSMatcher.h.
 *
 * The whole implementation is one walk and one predicate:
 *
 *   MatchFrom(selector, index, element)
 *       match compound[index] against element
 *       if index == 0, matched
 *       otherwise the combinator before it says how to find a candidate for
 *       compound[index-1]: the parent, some ancestor, the previous element
 *       sibling, or some earlier element sibling
 *
 * and a compound is a loop over its simple selectors against one view.
 *
 * Right-to-left is what makes this cheap in practice: the caller has an element
 * and wants to know whether it is the *subject* of the selector, which is the
 * right-most compound, so the first test is the one that rejects almost
 * everything. The recursion depth is the number of compounds, never the depth of
 * the tree, so no stylesheet can exhaust the stack however deep the document is.
 *
 * Two walks are bounded on purpose. Walking ancestors and walking siblings both
 * follow a host's links, and a host whose links describe a cycle rather than a
 * tree would otherwise spin forever. The bound is far above any possible
 * document (the DOM caps a document's nodes well below it), and exceeding it is
 * reported as a failure rather than as a silent "no match", because a truncated
 * walk and a real negative are different facts.
 */

#include "RoboCSS/RoboCSSMatcher.h"

#include "RoboCSSInternal.h"
/* For the parsed An+B form and its membership test, which the parser header owns
 * — the matcher consumes them and interprets nothing else of that header. */
#include "RoboCSS/RoboCSSSelectorParser.h"

/* ------------------------------------------------------------------ */
/* Bounds and state                                                    */
/* ------------------------------------------------------------------ */

/*
 * How many links one walk may follow before the host is judged to describe
 * something that is not a tree. A document cannot hold this many nodes, so a
 * walk that reaches it is a cycle, not a deep tree.
 */
#define MATCH_WALK_LIMIT 65536u

typedef enum RoboMatchRelation {
    MATCH_RELATION_PARENT = 0,
    MATCH_RELATION_PREVIOUS = 1,
    MATCH_RELATION_NEXT = 2
} MatchRelation;

typedef struct matchContext {
    const CssElementHost *host;
    CssSelectorMatch *out;
    int unusable; /* the matcher could not run at all */
} MatchContext;

void CssSelectorMatchInit(CssSelectorMatch *out)
{
    if (!out) {
        return;
    }
    out->matched = 0;
    out->unsupported = 0;
    out->condition = CssSliceEmpty();
    out->selectorIndex = 0u;
    out->range = CssRangeAt(0u);
}

/* ------------------------------------------------------------------ */
/* Relations                                                           */
/* ------------------------------------------------------------------ */

static int MatchRelationAvailable(const CssElementHost *host,
                                    MatchRelation relation)
{
    if (!host) {
        return 0;
    }
    switch (relation) {
    case MATCH_RELATION_PARENT:
        return host->parent != NULL;
    case MATCH_RELATION_PREVIOUS:
        return host->PreviousSibling != NULL;
    case MATCH_RELATION_NEXT:
        return host->NextSibling != NULL;
    default:
        return 0;
    }
}

/*
 * Ask the host for one relation.
 *
 * An unavailable callback and a host that answers "there is none" both yield
 * RoboCssElementRefNone, and the two are distinguished by `unsupported`:
 * the first is a condition this build cannot evaluate, the second is a fact
 * about the element (`:first-child` *is* "no previous sibling"). Getting that
 * wrong in either direction would be a wrong stylesheet, so every caller that
 * would read the answer as a *fact* goes through MatchRelationAnswer below,
 * which refuses the answer when the relation was unavailable.
 */
static CssElementRef GetRoboMatchRelation(MatchContext *context,
                                             CssElementRef element,
                                             MatchRelation relation)
{
    if (!MatchRelationAvailable(context->host, relation)) {
        context->out->unsupported = 1;
        return RoboCssElementRefNone;
    }
    switch (relation) {
    case MATCH_RELATION_PARENT:
        return context->host->parent(context->host->userdata, element);
    case MATCH_RELATION_PREVIOUS:
        return context->host->PreviousSibling(context->host->userdata, element);
    case MATCH_RELATION_NEXT:
        return context->host->NextSibling(context->host->userdata, element);
    default:
        return RoboCssElementRefNone;
    }
}

/**
 * Ask for one relation and say whether the answer means anything.
 *
 * @return 1 and writes the relation to @p outRef when the host answered; 0 when
 *         the relation is unavailable, having set `unsupported` — in which case
 *         the caller must abandon the condition instead of reading
 *         RoboCssElementRefNone as "there is none". `:root` and
 *         `:first-child` are exactly the conditions that turn that confusion into
 *         a matched element.
 */
static int MatchRelationAnswer(MatchContext *context,
                                 CssElementRef element,
                                 MatchRelation relation,
                                 CssElementRef *outRef)
{
    CssElementRef ref = GetRoboMatchRelation(context, element, relation);

    if (context->out->unsupported) {
        return 0;
    }
    *outRef = ref;
    return 1;
}

/* ------------------------------------------------------------------ */
/* Byte predicates                                                     */
/* ------------------------------------------------------------------ */

static int MatchBytesEqual(CssSlice a, CssSlice b, int foldAsciiCase)
{
    size_t i;

    if (a.length != b.length) {
        return 0;
    }
    if (a.length == 0u) {
        return 1;
    }
    if (!a.bytes || !b.bytes) {
        return 0;
    }
    for (i = 0u; i < a.length; i++) {
        uint8_t left = a.bytes[i];
        uint8_t right = b.bytes[i];

        if (foldAsciiCase) {
            left = CssAsciiLower(left);
            right = CssAsciiLower(right);
        }
        if (left != right) {
            return 0;
        }
    }
    return 1;
}

static int MatchBytesStartsWith(CssSlice value,
                                   CssSlice prefix,
                                   int foldAsciiCase)
{
    if (prefix.length > value.length || !value.bytes || !prefix.bytes) {
        return 0;
    }
    return MatchBytesEqual(CssSliceFromBytes(value.bytes, prefix.length),
                             prefix, foldAsciiCase);
}

static int MatchBytesEndsWith(CssSlice value,
                                 CssSlice suffix,
                                 int foldAsciiCase)
{
    if (suffix.length > value.length || !value.bytes || !suffix.bytes) {
        return 0;
    }
    return MatchBytesEqual(CssSliceFromBytes(value.bytes + value.length -
                                                          suffix.length,
                                                      suffix.length),
                             suffix, foldAsciiCase);
}

static int MatchBytesContains(CssSlice value,
                                CssSlice needle,
                                int foldAsciiCase)
{
    size_t offset;

    if (needle.length == 0u) {
        return 0;
    }
    if (needle.length > value.length || !value.bytes || !needle.bytes) {
        return 0;
    }
    for (offset = 0u; offset + needle.length <= value.length; offset++) {
        if (MatchBytesEqual(CssSliceFromBytes(value.bytes + offset,
                                                        needle.length),
                              needle, foldAsciiCase)) {
            return 1;
        }
    }
    return 0;
}

/** Non-zero when @p needle is one CSS-whitespace-separated word of @p value. */
static int MatchBytesHasWord(CssSlice value,
                                CssSlice needle,
                                int foldAsciiCase)
{
    size_t at = 0u;

    if (needle.length == 0u || !value.bytes) {
        return 0;
    }
    /*
     * The value is split here rather than through a helper because only the
     * split matters: a run of whitespace is one separator, leading and trailing
     * whitespace produce no empty word, and `class="a  b"` therefore has the
     * two words CSS says it has.
     */
    while (at < value.length) {
        size_t begin;
        size_t end;

        while (at < value.length && CssAsciiIsSpace(value.bytes[at])) {
            at++;
        }
        begin = at;
        while (at < value.length && !CssAsciiIsSpace(value.bytes[at])) {
            at++;
        }
        end = at;
        if (end > begin &&
            MatchBytesEqual(CssSliceFromBytes(value.bytes + begin,
                                                        end - begin),
                              needle, foldAsciiCase)) {
            return 1;
        }
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/* Attribute selectors                                                 */
/* ------------------------------------------------------------------ */

static int MatchAttribute(const MatchContext *context,
                           const CssElementView *view,
                           const CssSimpleSelector *simple)
{
    CssSlice value;
    int fold;

    (void)context;
    if (!CssElementViewFindAttribute(view, simple->attribute, &value)) {
        /* Absent is "no match" for every operator, presence included: `[a]` asks
         * whether a record exists, and none does. Nothing is inferred from an
         * empty value, which is why `[a=""]` and `[a]` stay different questions. */
        return 0;
    }
    fold = simple->caseFlag == RoboCssAttributeCaseInsensitive;

    switch (simple->match) {
    case RoboCssAttributeMatchNone:
        return 1;
    case RoboCssAttributeMatchEquals:
        return MatchBytesEqual(value, simple->attributeValue, fold);
    case RoboCssAttributeMatchIncludes:
        return MatchBytesHasWord(value, simple->attributeValue, fold);
    case RoboCssAttributeMatchDash:
        /* `[lang|=en]` is `en` or `en-…`: the dash is part of the match, so an
         * empty operand cannot match anything (every value starts with `-`
         * against an empty prefix, which the specification forbids). */
        if (CssSliceIsEmpty(simple->attributeValue)) {
            return 0;
        }
        if (MatchBytesEqual(value, simple->attributeValue, fold)) {
            return 1;
        }
        if (!MatchBytesStartsWith(value, simple->attributeValue, fold)) {
            return 0;
        }
        return value.length > simple->attributeValue.length &&
               value.bytes[simple->attributeValue.length] == (uint8_t)'-';
    case RoboCssAttributeMatchPrefix:
        return !CssSliceIsEmpty(simple->attributeValue) &&
               MatchBytesStartsWith(value, simple->attributeValue, fold);
    case RoboCssAttributeMatchSuffix:
        return !CssSliceIsEmpty(simple->attributeValue) &&
               MatchBytesEndsWith(value, simple->attributeValue, fold);
    case RoboCssAttributeMatchSubstring:
        return MatchBytesContains(value, simple->attributeValue, fold);
    default:
        /* An unknown operator on a hand-built selector: refused rather than
         * guessed at, and not reported as unsupported because the selector is
         * broken rather than ahead of this build. */
        return 0;
    }
}

/* ------------------------------------------------------------------ */
/* Structural pseudo-classes                                           */
/* ------------------------------------------------------------------ */

/** 1-based position of @p element among its element siblings, or 0 on failure. */
static int MatchElementIndex(MatchContext *context,
                               CssElementRef element,
                               MatchRelation direction,
                               int *outIndex)
{
    CssElementRef cursor = element;
    size_t steps = 0u;
    int index = 1;

    for (;;) {
        CssElementRef next = GetRoboMatchRelation(context, cursor, direction);

        if (context->out->unsupported) {
            return 0;
        }
        if (next == RoboCssElementRefNone) {
            break;
        }
        if (++steps > MATCH_WALK_LIMIT) {
            /* Not a tree. The count is meaningless, so the condition is reported
             * as unevaluatable rather than answered with whatever index the walk
             * had reached. */
            context->out->unsupported = 1;
            return 0;
        }
        cursor = next;
        index++;
    }
    *outIndex = index;
    return 1;
}

static int MatchStructuralPseudo(MatchContext *context,
                                   CssElementRef element,
                                   const CssSimpleSelector *simple,
                                   const CssSelectorList *argument,
                                   int *outHandled)
{
    /*
     * Every one of these is a comparison against "there is no such element", so
     * each has to establish that the host can answer at all: a host with no
     * parent callback returns NONE for the same reason a root element does, and
     * reading the two the same way would report `:root` as matched on every
     * element of such a host.
     */
    CssElementRef relation;

    *outHandled = 1;

    if (CssSimpleSelectorIsPseudoNamed(simple, "root")) {
        /* The document element is the one element with no parent element. The
         * seam says "no parent", so this needs no document and no special
         * "html" test. */
        if (!MatchRelationAnswer(context, element, MATCH_RELATION_PARENT, &relation)) {
            return 0;
        }
        return relation == RoboCssElementRefNone;
    }
    if (CssSimpleSelectorIsPseudoNamed(simple, "first-child")) {
        if (!MatchRelationAnswer(context, element, MATCH_RELATION_PREVIOUS, &relation)) {
            return 0;
        }
        return relation == RoboCssElementRefNone;
    }
    if (CssSimpleSelectorIsPseudoNamed(simple, "last-child")) {
        if (!MatchRelationAnswer(context, element, MATCH_RELATION_NEXT, &relation)) {
            return 0;
        }
        return relation == RoboCssElementRefNone;
    }
    if (CssSimpleSelectorIsPseudoNamed(simple, "only-child")) {
        if (!MatchRelationAnswer(context, element, MATCH_RELATION_PREVIOUS, &relation) ||
            relation != RoboCssElementRefNone) {
            return 0;
        }
        if (!MatchRelationAnswer(context, element, MATCH_RELATION_NEXT, &relation)) {
            return 0;
        }
        return relation == RoboCssElementRefNone;
    }
    if (CssSimpleSelectorIsPseudoNamed(simple, "nth-child") ||
        CssSimpleSelectorIsPseudoNamed(simple, "nth-last-child")) {
        CssAnPlusB form;
        int fromEnd = CssSimpleSelectorIsPseudoNamed(simple, "nth-last-child");
        int index = 0;

        if (!CssSimpleSelectorHasNth(simple)) {
            /* A hand-built `:nth-child()` with no parsed argument: the condition
             * exists but this build cannot evaluate it. */
            context->out->unsupported = 1;
            context->out->condition = simple->name;
            return 0;
        }
        form.a = CssSimpleSelectorNthA(simple);
        form.b = CssSimpleSelectorNthB(simple);
        if (!MatchElementIndex(context, element,
                                 fromEnd ? MATCH_RELATION_NEXT
                                          : MATCH_RELATION_PREVIOUS,
                                 &index)) {
            return 0;
        }
        return CssAnPlusBMatches(&form, index);
    }
    if (CssSimpleSelectorIsPseudoNamed(simple, "not")) {
        int inner;

        if (!argument) {
            /* `:not()` whose argument was never parsed: unevaluatable, and it
             * must not be read as "matches", which would apply rules the author
             * negated away. */
            context->out->unsupported = 1;
            context->out->condition = simple->name;
            return 0;
        }
        /* A record of its own: the inner attempt must not clear the outer one,
         * and its verdict is merged in explicitly afterwards. */
        CssSelectorMatch innerMatch;

        inner = CssSelectorListMatches(argument, context->host, element,
                                              &innerMatch, NULL);
        if (inner < 0) {
            context->unusable = 1;
            return 0;
        }
        if (innerMatch.unsupported) {
            /* The argument could not be evaluated, so neither can its negation,
             * and it must not be read as "does not match" — that would apply the
             * very rules the author negated away. */
            context->out->unsupported = 1;
            if (CssSliceIsAbsent(context->out->condition)) {
                context->out->condition = innerMatch.condition;
            }
            return 0;
        }
        return !inner;
    }

    *outHandled = 0;
    return 0;
}

/* ------------------------------------------------------------------ */
/* Simple and compound selectors                                       */
/* ------------------------------------------------------------------ */

/* Declared before use: a complex selector's walk needs a compound, and a
 * compound's `:not()` needs a list. Both directions are real. */
static int MatchFrom(MatchContext *context,
                      const CssComplexSelector *selector,
                      size_t index,
                      CssElementRef element);

static int MatchSimpleAgainst(MatchContext *context,
                                const CssElementView *view,
                                CssElementRef element,
                                const CssCompoundSelector *compound,
                                size_t simpleIndex,
                                const CssSimpleSelector *simple)
{
    switch (simple->kind) {
    case RoboCssSimpleSelectorType:
        /* HTML element names are ASCII case-insensitive, and the DOM stores
         * lowercase, so `DIV` and `div` name the same element. The author's
         * spelling is what `name` holds; the comparison is what folds. */
        return MatchBytesEqual(view->tagName, simple->name, 1);
    case RoboCssSimpleSelectorUniversal:
        return 1;
    case RoboCssSimpleSelectorClass:
        /* Class names are case-sensitive in HTML, and a class attribute is a
         * whitespace-separated list — never a substring. The host hands over the
         * list, so this is a word comparison rather than a search. */
        return CssElementViewHasClass(view, simple->name);
    case RoboCssSimpleSelectorId:
        return CssElementViewIdEquals(view, simple->name);
    case RoboCssSimpleSelectorAttribute:
        return MatchAttribute(context, view, simple);
    case RoboCssSimpleSelectorPseudoElement:
        /*
         * An element is not its own `::before`. `div::before` styles generated
         * content, which this build does not generate, so it matches no element
         * — and saying "no" is right rather than unsupported, because the
         * question was answered: this element is the div, not the content
         * before it. A later phase that generates content matches pseudo-elements
         * by giving the matcher a subject, and this case is where that lands.
         */
        return 0;
    case RoboCssSimpleSelectorPseudoClass: {
        const CssSelectorList *argument =
            compound ? CssCompoundSelectorArgumentOf(compound, simpleIndex)
                     : NULL;
        int handled = 0;
        int result = MatchStructuralPseudo(context, element, simple, argument,
                                            &handled);

        if (!handled) {
            /* `:hover`, `:focus`, `:lang(en)`, `:nth-of-type(2)`, … — real
             * pseudo-classes whose state or subject this build does not have.
             * Reported, never assumed true. */
            context->out->unsupported = 1;
            context->out->condition = simple->name;
            return 0;
        }
        return result;
    }
    default:
        return 0;
    }
}

static int MatchCompound(MatchContext *context,
                          CssElementRef element,
                          const CssCompoundSelector *compound)
{
    CssElementView view;
    size_t count;
    size_t i;

    if (element == RoboCssElementRefNone || !compound ||
        CssCompoundSelectorCount(compound) == 0u) {
        return 0;
    }
    if (context->host->view(context->host->userdata, element, &view) != RoboOk) {
        /* The element is gone, or the host cannot describe it. Either way this
         * candidate matches nothing; the caller's walk continues with the next
         * one rather than failing the whole match. */
        return 0;
    }
    count = CssCompoundSelectorCount(compound);
    for (i = 0u; i < count; i++) {
        const CssSimpleSelector *simple =
            CssCompoundSelectorAt(compound, i);

        if (!simple) {
            return 0;
        }
        if (!MatchSimpleAgainst(context, &view, element, compound, i, simple)) {
            return 0;
        }
    }
    return 1;
}

/* ------------------------------------------------------------------ */
/* Complex selectors                                                   */
/* ------------------------------------------------------------------ */

static int MatchWalk(MatchContext *context,
                      const CssComplexSelector *selector,
                      size_t index,
                      CssElementRef element,
                      MatchRelation relation)
{
    CssElementRef candidate = element;
    size_t steps = 0u;

    for (;;) {
        candidate = GetRoboMatchRelation(context, candidate, relation);
        if (context->out->unsupported) {
            return 0;
        }
        if (candidate == RoboCssElementRefNone) {
            return 0;
        }
        if (++steps > MATCH_WALK_LIMIT) {
            /* The same: a host whose links cycle is not a document, and a
             * truncated walk must not be reported as `no match`. */
            context->out->unsupported = 1;
            return 0;
        }
        if (MatchFrom(context, selector, index, candidate)) {
            return 1;
        }
        if (context->unusable) {
            return 0;
        }
    }
}

static int MatchFrom(MatchContext *context,
                      const CssComplexSelector *selector,
                      size_t index,
                      CssElementRef element)
{
    const CssCompoundSelector *compound =
        CssComplexSelectorCompoundAt(selector, index);

    if (!MatchCompound(context, element, compound)) {
        return 0;
    }
    if (index == 0u) {
        return 1;
    }

    {
        CssCombinator combinator =
            CssComplexSelectorCombinatorAt(selector, index - 1u);

        switch (combinator) {
        case RoboCssCombinatorChild: {
            CssElementRef parent;

            if (!MatchRelationAnswer(context, element, MATCH_RELATION_PARENT,
                                       &parent) ||
                parent == RoboCssElementRefNone) {
                return 0;
            }
            return MatchFrom(context, selector, index - 1u, parent);
        }
        case RoboCssCombinatorDescendant:
            return MatchWalk(context, selector, index - 1u, element,
                              MATCH_RELATION_PARENT);
        case RoboCssCombinatorNextSibling: {
            CssElementRef previous;

            if (!MatchRelationAnswer(context, element, MATCH_RELATION_PREVIOUS,
                                       &previous) ||
                previous == RoboCssElementRefNone) {
                return 0;
            }
            return MatchFrom(context, selector, index - 1u, previous);
        }
        case RoboCssCombinatorSubsequentSibling:
            return MatchWalk(context, selector, index - 1u, element,
                              MATCH_RELATION_PREVIOUS);
        case RoboCssCombinatorColumn:
            /* `||` selects table columns, which need a table model the DOM does
             * not have yet. The combinator parses and is represented; matching it
             * is reported rather than invented. */
            context->out->unsupported = 1;
            return 0;
        default:
            /* NONE between two compounds is a structural hole; the
             * well-formedness check rejects it before the walk starts. */
            return 0;
        }
    }
}

/* ------------------------------------------------------------------ */
/* Public entry points                                                 */
/* ------------------------------------------------------------------ */

static int MatchStart(const CssElementHost *host,
                       CssElementRef element,
                       CssSelectorMatch *out,
                       MatchContext *context)
{
    if (!out || !host || !CssElementHostIsUsable(host)) {
        return -1;
    }
    CssSelectorMatchInit(out);
    if (element == RoboCssElementRefNone) {
        return 0;
    }
    context->host = host;
    context->out = out;
    context->unusable = 0;
    return 1;
}

int CssSimpleSelectorMatches(const CssSimpleSelector *simple,
                                     const CssCompoundSelector *compound,
                                     size_t simpleIndex,
                                     const CssElementHost *host,
                                     CssElementRef element,
                                     CssSelectorMatch *out,
                                     CssDiag *diag)
{
    MatchContext context;
    CssElementView view;
    int started;

    (void)diag;
    if (!simple) {
        return -1;
    }
    started = MatchStart(host, element, out, &context);
    if (started <= 0) {
        return started;
    }
    if (host->view(host->userdata, element, &view) != RoboOk) {
        return 0;
    }
    if (MatchSimpleAgainst(&context, &view, element, compound, simpleIndex,
                             simple)) {
        out->matched = 1;
    }
    return out->matched;
}

int CssCompoundSelectorMatches(const CssCompoundSelector *compound,
                                       const CssElementHost *host,
                                       CssElementRef element,
                                       CssSelectorMatch *out,
                                       CssDiag *diag)
{
    MatchContext context;
    int started;

    (void)diag;
    if (!compound) {
        return -1;
    }
    started = MatchStart(host, element, out, &context);
    if (started <= 0) {
        return started;
    }
    if (MatchCompound(&context, element, compound)) {
        out->matched = 1;
    }
    return out->matched;
}

int CssComplexSelectorMatches(const CssComplexSelector *selector,
                                      const CssElementHost *host,
                                      CssElementRef element,
                                      CssSelectorMatch *out,
                                      CssDiag *diag)
{
    MatchContext context;
    int started;
    size_t last;

    (void)diag;
    if (!selector) {
        return -1;
    }
    started = MatchStart(host, element, out, &context);
    if (started <= 0) {
        return started;
    }
    if (!CssComplexSelectorIsWellFormed(selector)) {
        /* A combinator count that does not fit the compounds is a broken record,
         * not a selector that fails to match. */
        return -1;
    }
    last = CssComplexSelectorCompoundCount(selector) - 1u;
    if (MatchFrom(&context, selector, last, element)) {
        out->matched = 1;
        out->range = selector->range;
    }
    return out->matched;
}

int CssSelectorListMatches(const CssSelectorList *list,
                                   const CssElementHost *host,
                                   CssElementRef element,
                                   CssSelectorMatch *out,
                                   CssDiag *diag)
{
    size_t count;
    size_t i;

    (void)diag;
    if (!list) {
        return -1;
    }
    if (!out || !host || !CssElementHostIsUsable(host)) {
        return -1;
    }
    CssSelectorMatchInit(out);
    if (element == RoboCssElementRefNone) {
        return 0;
    }

    count = CssSelectorListCount(list);
    for (i = 0u; i < count; i++) {
        const CssComplexSelector *selector =
            CssSelectorListAt(list, i);
        CssSelectorMatch attempt;
        MatchContext context;

        CssSelectorMatchInit(&attempt);
        context.host = host;
        context.out = &attempt;
        context.unusable = 0;

        if (!CssComplexSelectorIsWellFormed(selector)) {
            continue;
        }
        if (MatchFrom(&context, selector,
                       CssComplexSelectorCompoundCount(selector) - 1u,
                       element)) {
            out->matched = 1;
            out->selectorIndex = i;
            out->range = selector->range;
            /* A list that matched is not re-evaluated, but everything the
             * attempt found is still reported: an unsupported condition in a
             * selector that was tried before the matching one explains a
             * negative a caller might reach by asking about that selector
             * directly. */
            out->unsupported = attempt.unsupported || out->unsupported;
            if (CssSliceIsAbsent(out->condition)) {
                out->condition = attempt.condition;
            }
            return 1;
        }
        if (context.unusable) {
            return -1;
        }
        if (attempt.unsupported && !out->unsupported) {
            out->unsupported = 1;
            out->condition = attempt.condition;
        }
    }
    return 0;
}
