/*
 * RoboCSS selector model (Phase 9.1) — see RoboCSSSelector.h.
 *
 * Storage and vocabulary only: no matching, no specificity, and no parsing.
 * The three ownership levels (compound owns simples, complex owns compounds and
 * combinators, list owns complex selectors) each follow the subsystem's one
 * rule — children are moved in, never copied in — with the single exception of
 * simple selectors, which own nothing and are therefore copied.
 *
 * Phase 9.4 gave two records something of their own to own, and both are
 * disposed here rather than by their consumers: a compound owns its nested
 * argument lists (`:not(...)`), and a selector list owns the arena holding the
 * resolved spelling of its names. Nested lists are addressed by index from the
 * simple selectors that use them, which is what keeps the copy rule above
 * intact: resetting an index on copy costs one assignment, whereas owning the
 * list on the simple selector would have forced every copy to deep-copy or to
 * be banned.
 *
 * One deliberately defensive reader lives here too: nested_at and argument_of
 * treat an index as a *claim* and check it, because a simple selector copied
 * from one compound into another carries an index that means nothing in its new
 * home. That is a hole the type system cannot close, so it is closed by a bound
 * check at the one place that would otherwise dereference it.
 */

#include "RoboCSS/RoboCSSSelector.h"

#include "RoboCSSInternal.h"
#include "RoboMem/RoboMem.h"

#include <string.h>

/* ------------------------------------------------------------------ */
/* Vocabulary                                                          */
/* ------------------------------------------------------------------ */

int CssSimpleSelectorKindIsKnown(CssSimpleSelectorKind kind)
{
    return kind >= RoboCssSimpleSelectorNone &&
           kind < (CssSimpleSelectorKind)RoboCssSimpleSelectorKindCount;
}

const char *CssSimpleSelectorKindName(CssSimpleSelectorKind kind)
{
    switch (kind) {
    case RoboCssSimpleSelectorNone:
        return "none";
    case RoboCssSimpleSelectorType:
        return "type";
    case RoboCssSimpleSelectorUniversal:
        return "universal";
    case RoboCssSimpleSelectorClass:
        return "class";
    case RoboCssSimpleSelectorId:
        return "id";
    case RoboCssSimpleSelectorAttribute:
        return "attribute";
    case RoboCssSimpleSelectorPseudoClass:
        return "pseudo-class";
    case RoboCssSimpleSelectorPseudoElement:
        return "pseudo-element";
    default:
        return "unknown";
    }
}

int CssAttributeMatchIsKnown(CssAttributeMatch match)
{
    return match >= RoboCssAttributeMatchNone &&
           match < (CssAttributeMatch)RoboCssAttributeMatchCount;
}

const char *CssAttributeMatchName(CssAttributeMatch match)
{
    switch (match) {
    case RoboCssAttributeMatchNone:
        return "none";
    case RoboCssAttributeMatchEquals:
        return "equals";
    case RoboCssAttributeMatchIncludes:
        return "includes";
    case RoboCssAttributeMatchDash:
        return "dash";
    case RoboCssAttributeMatchPrefix:
        return "prefix";
    case RoboCssAttributeMatchSuffix:
        return "suffix";
    case RoboCssAttributeMatchSubstring:
        return "substring";
    default:
        return "unknown";
    }
}

int CssAttributeCaseIsKnown(CssAttributeCase flag)
{
    return flag >= RoboCssAttributeCaseDefault &&
           flag < (CssAttributeCase)RoboCssAttributeCaseCount;
}

const char *CssAttributeCaseName(CssAttributeCase flag)
{
    switch (flag) {
    case RoboCssAttributeCaseDefault:
        return "default";
    case RoboCssAttributeCaseInsensitive:
        return "insensitive";
    case RoboCssAttributeCaseSensitive:
        return "sensitive";
    default:
        return "unknown";
    }
}

int CssCombinatorIsKnown(CssCombinator combinator)
{
    return combinator >= RoboCssCombinatorNone &&
           combinator < (CssCombinator)RoboCssCombinatorCount;
}

const char *CssCombinatorName(CssCombinator combinator)
{
    switch (combinator) {
    case RoboCssCombinatorNone:
        return "none";
    case RoboCssCombinatorDescendant:
        return "descendant";
    case RoboCssCombinatorChild:
        return "child";
    case RoboCssCombinatorNextSibling:
        return "next-sibling";
    case RoboCssCombinatorSubsequentSibling:
        return "subsequent-sibling";
    case RoboCssCombinatorColumn:
        return "column";
    default:
        return "unknown";
    }
}

/* ------------------------------------------------------------------ */
/* Simple selectors                                                    */
/* ------------------------------------------------------------------ */

void CssSimpleSelectorInit(CssSimpleSelector *selector)
{
    if (!selector) {
        return;
    }
    selector->kind = RoboCssSimpleSelectorNone;
    selector->range = CssRangeAt(0u);
    selector->name = CssSliceEmpty();
    selector->attribute = CssSliceEmpty();
    selector->attributeValue = CssSliceEmpty();
    selector->match = RoboCssAttributeMatchNone;
    selector->caseFlag = RoboCssAttributeCaseDefault;
    selector->valueWasString = 0;
    selector->argument = CssSliceEmpty();
    selector->nestedIndex = RoboCssNestedNone;
    selector->nthParsed = 0;
    selector->nthA = 0;
    selector->nthB = 0;
}

void CssSimpleSelectorDispose(CssSimpleSelector *selector)
{
    CssSimpleSelectorInit(selector);
}

/** Non-zero when a simple selector of @p kind must carry a name. */
static int SimpleKindNeedsName(CssSimpleSelectorKind kind)
{
    return kind == RoboCssSimpleSelectorType ||
           kind == RoboCssSimpleSelectorClass ||
           kind == RoboCssSimpleSelectorId ||
           kind == RoboCssSimpleSelectorPseudoClass ||
           kind == RoboCssSimpleSelectorPseudoElement;
}

Result CssSimpleSelectorMake(CssSimpleSelector *selector,
                                            CssSimpleSelectorKind kind,
                                            CssSlice name,
                                            CssDiag *diag)
{
    if (!selector) {
        return RoboCssDiag(diag, RoboCssDomainSelector, RoboInvalidArgument,
                             "selector is NULL");
    }
    if (!CssSimpleSelectorKindIsKnown(kind) ||
        kind == RoboCssSimpleSelectorNone) {
        return RoboCssDiag(diag, RoboCssDomainSelector, RoboInvalidArgument,
                             "unknown or absent simple selector kind");
    }
    /* ATTRIBUTE carries its name through set_attribute, which also has to record
     * the comparison; making one here without it would leave the record half
     * described, so a name is not required for it and is not rejected either. */
    if (SimpleKindNeedsName(kind) && CssSliceIsEmpty(name)) {
        return RoboCssDiag(diag, RoboCssDomainSelector, RoboInvalidArgument,
                             "this simple selector kind needs a name");
    }
    CssSimpleSelectorDispose(selector);
    selector->kind = kind;
    selector->name = name;
    return RoboOk;
}

Result CssSimpleSelectorSetAttribute(
    CssSimpleSelector *selector,
    CssSlice attribute,
    CssAttributeMatch match,
    CssSlice value,
    CssAttributeCase caseFlag,
    CssDiag *diag)
{
    if (!selector) {
        return RoboCssDiag(diag, RoboCssDomainSelector, RoboInvalidArgument,
                             "selector is NULL");
    }
    if (selector->kind != RoboCssSimpleSelectorAttribute) {
        return RoboCssDiag(diag, RoboCssDomainSelector, RoboInvalidArgument,
                             "not an attribute selector");
    }
    if (CssSliceIsEmpty(attribute)) {
        return RoboCssDiag(diag, RoboCssDomainSelector, RoboInvalidArgument,
                             "an attribute selector needs an attribute name");
    }
    if (!CssAttributeMatchIsKnown(match)) {
        return RoboCssDiag(diag, RoboCssDomainSelector, RoboInvalidArgument,
                             "unknown attribute match operator");
    }
    if (!CssAttributeCaseIsKnown(caseFlag)) {
        return RoboCssDiag(diag, RoboCssDomainSelector, RoboInvalidArgument,
                             "unknown attribute case flag");
    }
    /* Presence is the absence of a comparison, not a comparison against
     * nothing: `[a]` and `[a=""]` are different selectors and must stay
     * distinguishable, which is why a comparison needs a value. */
    if (match != RoboCssAttributeMatchNone && CssSliceIsAbsent(value)) {
        return RoboCssDiag(diag, RoboCssDomainSelector, RoboInvalidArgument,
                             "a comparison needs a value");
    }
    selector->attribute = attribute;
    selector->match = match;
    selector->attributeValue = value;
    selector->caseFlag = caseFlag;
    return RoboOk;
}

Result CssSimpleSelectorSetArgument(
    CssSimpleSelector *selector,
    CssSlice argument,
    CssDiag *diag)
{
    if (!selector) {
        return RoboCssDiag(diag, RoboCssDomainSelector, RoboInvalidArgument,
                             "selector is NULL");
    }
    if (selector->kind != RoboCssSimpleSelectorPseudoClass &&
        selector->kind != RoboCssSimpleSelectorPseudoElement) {
        return RoboCssDiag(diag, RoboCssDomainSelector, RoboInvalidArgument,
                             "only a pseudo selector takes an argument");
    }
    selector->argument = argument;
    return RoboOk;
}

size_t CssSimpleSelectorNestedIndex(const CssSimpleSelector *selector)
{
    return selector ? selector->nestedIndex : RoboCssNestedNone;
}

Result CssSimpleSelectorSetNth(CssSimpleSelector *selector,
                                               int a,
                                               int b,
                                               CssDiag *diag)
{
    if (!selector) {
        return RoboCssDiag(diag, RoboCssDomainSelector, RoboInvalidArgument,
                             "selector is NULL");
    }
    if (selector->kind != RoboCssSimpleSelectorPseudoClass) {
        return RoboCssDiag(diag, RoboCssDomainSelector, RoboInvalidArgument,
                             "An+B belongs to a pseudo-class");
    }
    selector->nthParsed = 1;
    selector->nthA = a;
    selector->nthB = b;
    return RoboOk;
}

int CssSimpleSelectorHasNth(const CssSimpleSelector *selector)
{
    return selector && selector->nthParsed;
}

int CssSimpleSelectorNthA(const CssSimpleSelector *selector)
{
    return selector ? selector->nthA : 0;
}

int CssSimpleSelectorNthB(const CssSimpleSelector *selector)
{
    return selector ? selector->nthB : 0;
}

int CssSimpleSelectorIsPseudoNamed(const CssSimpleSelector *selector,
                                            const char *ascii)
{
    if (!selector || !ascii || !selector->kind ||
        (selector->kind != RoboCssSimpleSelectorPseudoClass &&
         selector->kind != RoboCssSimpleSelectorPseudoElement)) {
        return 0;
    }
    return CssSliceEqualsAsciiCi(selector->name, ascii);
}

int CssSimpleSelectorIsSet(const CssSimpleSelector *selector)
{
    return selector && selector->kind != RoboCssSimpleSelectorNone;
}

/* ------------------------------------------------------------------ */
/* Compound selectors                                                  */
/* ------------------------------------------------------------------ */

void CssCompoundSelectorInit(CssCompoundSelector *compound)
{
    if (!compound) {
        return;
    }
    compound->items = NULL;
    compound->count = 0u;
    compound->capacity = 0u;
    compound->range = CssRangeAt(0u);
    compound->nested = NULL;
    compound->nestedCount = 0u;
    compound->nestedCapacity = 0u;
}

void CssCompoundSelectorDispose(CssCompoundSelector *compound)
{
    size_t i;

    if (!compound) {
        return;
    }
    /* Nested argument lists own their own compounds and name arenas, so they are
     * disposed through the same call a top-level list uses. */
    for (i = 0u; i < compound->nestedCount; i++) {
        CssSelectorListDispose(&compound->nested[i]);
    }
    if (compound->nested) {
        RoboFree(compound->nested);
    }
    /* Simple selectors own nothing, so there is nothing to dispose element by
     * element; the array itself is the only other allocation. */
    if (compound->items) {
        RoboFree(compound->items);
    }
    CssCompoundSelectorInit(compound);
}

Result CssCompoundSelectorAppend(
    CssCompoundSelector *compound,
    const CssSimpleSelector *selector,
    CssDiag *diag)
{
    if (!compound || !selector) {
        return RoboCssDiag(diag, RoboCssDomainSelector, RoboInvalidArgument,
                             "compound or selector is NULL");
    }
    if (!CssSimpleSelectorIsSet(selector)) {
        return RoboCssDiag(diag, RoboCssDomainSelector, RoboInvalidArgument,
                             "selector is not set");
    }
    if (!CssGrow((void **)&compound->items, &compound->capacity, compound->count,
                       sizeof(CssSimpleSelector))) {
        return RoboCssDiag(diag, RoboCssDomainSelector, RoboOutOfMemory,
                             "could not grow the simple selector array");
    }
    compound->items[compound->count] = *selector;
    /* An index names a list inside the compound that issued it, so a copy can
     * never inherit one — it would address this compound's unrelated slot. The
     * parser re-attaches the copy's own list with attach_nested. */
    compound->items[compound->count].nestedIndex = RoboCssNestedNone;
    compound->count++;
    return RoboOk;
}

Result CssCompoundSelectorAttachNested(
    CssCompoundSelector *compound,
    size_t simpleIndex,
    CssSelectorList *list,
    CssDiag *diag)
{
    size_t index;

    if (!compound || !list) {
        return RoboCssDiag(diag, RoboCssDomainSelector, RoboInvalidArgument,
                             "compound or list is NULL");
    }
    if (simpleIndex >= compound->count) {
        return RoboCssDiag(diag, RoboCssDomainSelector, RoboInvalidArgument,
                             "no such simple selector in this compound");
    }
    if (!CssGrow((void **)&compound->nested, &compound->nestedCapacity,
                       compound->nestedCount, sizeof(CssSelectorList))) {
        return RoboCssDiag(diag, RoboCssDomainSelector, RoboOutOfMemory,
                             "could not grow the nested argument list array");
    }
    index = compound->nestedCount;
    compound->nested[index] = *list;
    compound->nestedCount++;
    /* Moving the list in is why the caller's copy is cleared here: the list
     * holds the only pointer to its compounds and its arena. */
    CssSelectorListInit(list);
    compound->items[simpleIndex].nestedIndex = index;
    return RoboOk;
}

size_t CssCompoundSelectorNestedCount(const CssCompoundSelector *compound)
{
    return compound ? compound->nestedCount : 0u;
}

const CssSelectorList *CssCompoundSelectorNestedAt(
    const CssCompoundSelector *compound,
    size_t index)
{
    if (!compound || index >= compound->nestedCount) {
        return NULL;
    }
    return &compound->nested[index];
}

const CssSelectorList *CssCompoundSelectorArgumentOf(
    const CssCompoundSelector *compound,
    size_t simpleIndex)
{
    const CssSimpleSelector *simple;

    if (!compound || simpleIndex >= compound->count) {
        return NULL;
    }
    simple = &compound->items[simpleIndex];
    if (simple->nestedIndex == RoboCssNestedNone) {
        return NULL;
    }
    return CssCompoundSelectorNestedAt(compound, simple->nestedIndex);
}

size_t CssCompoundSelectorCount(const CssCompoundSelector *compound)
{
    return compound ? compound->count : 0u;
}

const CssSimpleSelector *CssCompoundSelectorAt(
    const CssCompoundSelector *compound,
    size_t index)
{
    if (!compound || index >= compound->count) {
        return NULL;
    }
    return &compound->items[index];
}

int CssCompoundSelectorIsSet(const CssCompoundSelector *compound)
{
    return compound && compound->count > 0u;
}

void CssCompoundSelectorSealRange(CssCompoundSelector *compound)
{
    size_t i;

    if (!compound || compound->count == 0u) {
        if (compound) {
            compound->range = CssRangeAt(0u);
        }
        return;
    }
    compound->range = compound->items[0].range;
    for (i = 1u; i < compound->count; i++) {
        if (compound->items[i].range.begin < compound->range.begin) {
            compound->range.begin = compound->items[i].range.begin;
        }
        if (compound->items[i].range.end > compound->range.end) {
            compound->range.end = compound->items[i].range.end;
        }
    }
}

/* ------------------------------------------------------------------ */
/* Complex selectors                                                   */
/* ------------------------------------------------------------------ */

void CssComplexSelectorInit(CssComplexSelector *selector)
{
    if (!selector) {
        return;
    }
    selector->compounds = NULL;
    selector->compoundCount = 0u;
    selector->compoundCapacity = 0u;
    selector->combinators = NULL;
    selector->combinatorCount = 0u;
    selector->combinatorCapacity = 0u;
    selector->range = CssRangeAt(0u);
}

void CssComplexSelectorDispose(CssComplexSelector *selector)
{
    size_t i;

    if (!selector) {
        return;
    }
    for (i = 0u; i < selector->compoundCount; i++) {
        CssCompoundSelectorDispose(&selector->compounds[i]);
    }
    if (selector->compounds) {
        RoboFree(selector->compounds);
    }
    if (selector->combinators) {
        RoboFree(selector->combinators);
    }
    CssComplexSelectorInit(selector);
}

Result CssComplexSelectorAppendCompound(
    CssComplexSelector *selector,
    CssCompoundSelector *compound,
    CssDiag *diag)
{
    if (!selector || !compound) {
        return RoboCssDiag(diag, RoboCssDomainSelector, RoboInvalidArgument,
                             "selector or compound is NULL");
    }
    if (!CssCompoundSelectorIsSet(compound)) {
        return RoboCssDiag(diag, RoboCssDomainSelector, RoboInvalidArgument,
                             "compound selector is empty");
    }
    if (!CssGrow((void **)&selector->compounds, &selector->compoundCapacity,
                       selector->compoundCount, sizeof(CssCompoundSelector))) {
        return RoboCssDiag(diag, RoboCssDomainSelector, RoboOutOfMemory,
                             "could not grow the compound selector array");
    }
    selector->compounds[selector->compoundCount] = *compound;
    selector->compoundCount++;
    CssCompoundSelectorInit(compound);
    return RoboOk;
}

Result CssComplexSelectorAppendCombinator(
    CssComplexSelector *selector,
    CssCombinator combinator,
    CssDiag *diag)
{
    if (!selector) {
        return RoboCssDiag(diag, RoboCssDomainSelector, RoboInvalidArgument,
                             "selector is NULL");
    }
    if (!CssCombinatorIsKnown(combinator) ||
        combinator == RoboCssCombinatorNone) {
        return RoboCssDiag(diag, RoboCssDomainSelector, RoboInvalidArgument,
                             "a combinator must be one of the defined kinds");
    }
    if (!CssGrow((void **)&selector->combinators, &selector->combinatorCapacity,
                       selector->combinatorCount, sizeof(CssCombinator))) {
        return RoboCssDiag(diag, RoboCssDomainSelector, RoboOutOfMemory,
                             "could not grow the combinator array");
    }
    selector->combinators[selector->combinatorCount] = combinator;
    selector->combinatorCount++;
    return RoboOk;
}

size_t CssComplexSelectorCompoundCount(
    const CssComplexSelector *selector)
{
    return selector ? selector->compoundCount : 0u;
}

const CssCompoundSelector *CssComplexSelectorCompoundAt(
    const CssComplexSelector *selector,
    size_t index)
{
    if (!selector || index >= selector->compoundCount) {
        return NULL;
    }
    return &selector->compounds[index];
}

size_t CssComplexSelectorCombinatorCount(
    const CssComplexSelector *selector)
{
    return selector ? selector->combinatorCount : 0u;
}

CssCombinator CssComplexSelectorCombinatorAt(
    const CssComplexSelector *selector,
    size_t index)
{
    if (!selector || index >= selector->combinatorCount) {
        return RoboCssCombinatorNone;
    }
    return selector->combinators[index];
}

int CssComplexSelectorIsWellFormed(
    const CssComplexSelector *selector)
{
    if (!selector || selector->compoundCount == 0u) {
        return 0;
    }
    return selector->combinatorCount == selector->compoundCount - 1u;
}

/* ------------------------------------------------------------------ */
/* Selector lists                                                      */
/* ------------------------------------------------------------------ */

void CssSelectorListInit(CssSelectorList *list)
{
    if (!list) {
        return;
    }
    list->items = NULL;
    list->count = 0u;
    list->capacity = 0u;
    list->range = CssRangeAt(0u);
    list->names = NULL;
    list->namesLength = 0u;
    list->namesCapacity = 0u;
}

void CssSelectorListDispose(CssSelectorList *list)
{
    size_t i;

    if (!list) {
        return;
    }
    for (i = 0u; i < list->count; i++) {
        CssComplexSelectorDispose(&list->items[i]);
    }
    if (list->items) {
        RoboFree(list->items);
    }
    if (list->names) {
        RoboFree(list->names);
    }
    CssSelectorListInit(list);
}

Result CssSelectorListReserveNames(CssSelectorList *list,
                                                  size_t bytes,
                                                  CssDiag *diag)
{
    if (!list) {
        return RoboCssDiag(diag, RoboCssDomainSelector, RoboInvalidArgument,
                             "list is NULL");
    }
    if (bytes <= list->namesCapacity) {
        return RoboOk;
    }
    {
        char *grown = (char *)Realloc(list->names, bytes);

        if (!grown) {
            return RoboCssDiag(diag, RoboCssDomainSelector, RoboOutOfMemory,
                                 "could not reserve the name arena");
        }
        list->names = grown;
        list->namesCapacity = bytes;
    }
    return RoboOk;
}

CssSlice CssSelectorListIntern(CssSelectorList *list,
                                               const uint8_t *bytes,
                                               size_t length)
{
    size_t wanted;
    CssSlice slice;

    if (!list || (length > 0u && !bytes)) {
        return CssSliceEmpty();
    }
    if (length == 0u) {
        /* A zero-length name is a real record, not a failure: `[a=""]` is an
         * attribute selector comparing against an empty value, which is a
         * different selector from the presence-only `[a]`. The answer is empty
         * but *present*, so it needs a byte address — the arena's end will do,
         * and reserving one byte gives it one even for an otherwise empty list.
         */
        if (!list->names &&
            CssSelectorListReserveNames(list, 1u, NULL) != RoboOk) {
            return CssSliceEmpty();
        }
        return CssSliceFromBytes(
            (const uint8_t *)(list->names + list->namesLength), 0u);
    }
    wanted = list->namesLength + length;
    if (wanted > list->namesCapacity) {
        size_t capacity = list->namesCapacity;
        char *grown;

        if (capacity == 0u) {
            capacity = RoboCssCollectionMinCapacity;
        }
        while (capacity < wanted) {
            if (capacity > ((size_t)-1) / 2u) {
                capacity = wanted;
                break;
            }
            capacity *= 2u;
        }
        grown = (char *)Realloc(list->names, capacity);
        if (!grown) {
            /* Absent, not empty: a caller that recorded this slice would
             * otherwise compare names against no bytes and silently never
             * match, which is worse than failing the parse. */
            return CssSliceEmpty();
        }
        list->names = grown;
        list->namesCapacity = capacity;
    }
    memcpy(list->names + list->namesLength, bytes, length);
    slice = CssSliceFromBytes(
        (const uint8_t *)(list->names + list->namesLength), length);
    list->namesLength = wanted;
    return slice;
}

size_t CssSelectorListNamesLength(const CssSelectorList *list)
{
    return list ? list->namesLength : 0u;
}

const char *CssSelectorListNames(const CssSelectorList *list)
{
    return list ? list->names : NULL;
}

Result CssSelectorListAppend(CssSelectorList *list,
                                            CssComplexSelector *selector,
                                            CssDiag *diag)
{
    if (!list || !selector) {
        return RoboCssDiag(diag, RoboCssDomainSelector, RoboInvalidArgument,
                             "list or selector is NULL");
    }
    if (selector->compoundCount == 0u) {
        return RoboCssDiag(diag, RoboCssDomainSelector, RoboInvalidArgument,
                             "complex selector has no compounds");
    }
    if (!CssGrow((void **)&list->items, &list->capacity, list->count,
                       sizeof(CssComplexSelector))) {
        return RoboCssDiag(diag, RoboCssDomainSelector, RoboOutOfMemory,
                             "could not grow the selector list");
    }
    list->items[list->count] = *selector;
    list->count++;
    CssComplexSelectorInit(selector);
    return RoboOk;
}

size_t CssSelectorListCount(const CssSelectorList *list)
{
    return list ? list->count : 0u;
}

const CssComplexSelector *CssSelectorListAt(
    const CssSelectorList *list,
    size_t index)
{
    if (!list || index >= list->count) {
        return NULL;
    }
    return &list->items[index];
}

int CssSelectorListIsSet(const CssSelectorList *list)
{
    return list && list->count > 0u;
}

int CssSelectorListIsWellFormed(const CssSelectorList *list)
{
    size_t i;

    if (!list || list->count == 0u) {
        return 0;
    }
    for (i = 0u; i < list->count; i++) {
        if (!CssComplexSelectorIsWellFormed(&list->items[i])) {
            return 0;
        }
    }
    return 1;
}
