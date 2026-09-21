/*
 * RoboCSS rule model (Phase 9.1) — see RoboCSSRule.h.
 *
 * Disposal disposes both alternatives rather than switching on the kind. That
 * is not laziness: a rule is a tagged union whose inactive half is always
 * empty, and disposing an empty collection is defined to be a no-op, so
 * disposing both is the version that stays correct even for a record that was
 * built half way and then abandoned.
 */

#include "RoboCSS/RoboCSSRule.h"

#include "RoboCSSInternal.h"
#include "RoboMem/RoboMem.h"

int CssRuleKindIsKnown(CssRuleKind kind)
{
    return kind >= RoboCssRuleKindNone &&
           kind < (CssRuleKind)RoboCssRuleKindCount;
}

const char *CssRuleKindName(CssRuleKind kind)
{
    switch (kind) {
    case RoboCssRuleKindNone:
        return "none";
    case RoboCssRuleKindStyle:
        return "style";
    case RoboCssRuleKindAt:
        return "at";
    default:
        return "unknown";
    }
}

/* ------------------------------------------------------------------ */
/* At-rules                                                            */
/* ------------------------------------------------------------------ */

void CssAtRuleInit(CssAtRule *atRule)
{
    if (!atRule) {
        return;
    }
    atRule->name = CssSliceEmpty();
    atRule->range = CssRangeAt(0u);
    atRule->preludeRange = CssRangeAt(0u);
    atRule->prelude = CssSliceEmpty();
    CssValueInit(&atRule->preludeValues);
    atRule->hasBlock = 0;
    atRule->blockClosed = 0;
    atRule->children = NULL;
    CssDeclarationListInit(&atRule->declarations);
}

void CssAtRuleDispose(CssAtRule *atRule)
{
    if (!atRule) {
        return;
    }
    if (atRule->children) {
        CssRuleListDispose(atRule->children);
        RoboFree(atRule->children);
    }
    /* The prelude's component values are owned like any other value tree, and
     * the cleared record is the only state init produces, so releasing them
     * here is what keeps the cleared-record contract true. */
    CssValueDispose(&atRule->preludeValues);
    CssDeclarationListDispose(&atRule->declarations);
    CssAtRuleInit(atRule);
}

Result CssAtRuleMake(CssAtRule *atRule,
                                    CssSlice name,
                                    CssDiag *diag)
{
    if (!atRule) {
        return RoboCssDiag(diag, RoboCssDomainRule, RoboInvalidArgument,
                             "at-rule is NULL");
    }
    if (CssSliceIsEmpty(name)) {
        return RoboCssDiag(diag, RoboCssDomainRule, RoboInvalidArgument,
                             "an at-rule needs a name");
    }
    CssAtRuleDispose(atRule);
    atRule->name = name;
    return RoboOk;
}

Result CssAtRuleSetPrelude(CssAtRule *atRule,
                                          CssSlice prelude,
                                          int hasBlock,
                                          CssDiag *diag)
{
    if (!atRule) {
        return RoboCssDiag(diag, RoboCssDomainRule, RoboInvalidArgument,
                             "at-rule is NULL");
    }
    if (CssSliceIsEmpty(atRule->name)) {
        return RoboCssDiag(diag, RoboCssDomainRule, RoboInvalidArgument,
                             "at-rule has not been named");
    }
    /* An at-rule that had a block and no longer does must not keep what lived
     * in it: turning `@media { p { } }` into `@import url(x);` by clearing the
     * flag would otherwise leave orphaned children — or descriptors — behind. */
    if (atRule->hasBlock && !hasBlock) {
        if (atRule->children) {
            CssRuleListDispose(atRule->children);
            RoboFree(atRule->children);
            atRule->children = NULL;
        }
        CssDeclarationListClear(&atRule->declarations);
    }
    atRule->prelude = prelude;
    atRule->hasBlock = hasBlock ? 1 : 0;
    return RoboOk;
}

Result CssAtRuleSetPreludeValues(CssAtRule *atRule,
                                                 CssValue *preludeValues,
                                                 CssDiag *diag)
{
    if (!atRule || !preludeValues) {
        return RoboCssDiag(diag, RoboCssDomainRule, RoboInvalidArgument,
                             "at-rule or prelude values is NULL");
    }
    if (preludeValues->kind != RoboCssValueKindList) {
        return RoboCssDiag(diag, RoboCssDomainRule, RoboInvalidArgument,
                             "a prelude is a list of component values");
    }
    CssValueDispose(&atRule->preludeValues);
    return CssValueMove(preludeValues, &atRule->preludeValues, diag);
}

const CssValue *CssAtRulePreludeValues(const CssAtRule *atRule)
{
    if (!atRule) {
        return NULL;
    }
    return &atRule->preludeValues;
}

Result CssAtRuleSetBlockClosed(CssAtRule *atRule,
                                               int closed,
                                               CssDiag *diag)
{
    if (!atRule) {
        return RoboCssDiag(diag, RoboCssDomainRule, RoboInvalidArgument,
                             "at-rule is NULL");
    }
    if (!atRule->hasBlock) {
        return RoboCssDiag(diag, RoboCssDomainRule, RoboInvalidArgument,
                             "at-rule has no block to close");
    }
    atRule->blockClosed = closed ? 1 : 0;
    return RoboOk;
}

int CssAtRuleBlockClosed(const CssAtRule *atRule)
{
    if (!atRule || !atRule->hasBlock) {
        return 0;
    }
    return atRule->blockClosed;
}

Result CssAtRuleSetRange(CssAtRule *atRule,
                                        CssRange range,
                                        CssDiag *diag)
{
    if (!atRule) {
        return RoboCssDiag(diag, RoboCssDomainRule, RoboInvalidArgument,
                             "at-rule is NULL");
    }
    atRule->range = range;
    return RoboOk;
}

CssRange CssAtRuleRange(const CssAtRule *atRule)
{
    if (!atRule) {
        return CssRangeAt(0u);
    }
    return atRule->range;
}

CssRange CssAtRulePreludeRange(const CssAtRule *atRule)
{
    if (!atRule) {
        return CssRangeAt(0u);
    }
    return atRule->preludeRange;
}

Result CssAtRuleAppendRule(CssAtRule *atRule,
                                          CssRule *rule,
                                          CssDiag *diag)
{
    if (!atRule || !rule) {
        return RoboCssDiag(diag, RoboCssDomainRule, RoboInvalidArgument,
                             "at-rule or rule is NULL");
    }
    if (rule->kind == RoboCssRuleKindNone) {
        return RoboCssDiag(diag, RoboCssDomainRule, RoboInvalidArgument,
                             "rule has not been made");
    }
    if (!atRule->hasBlock) {
        return RoboCssDiag(diag, RoboCssDomainRule, RoboInvalidArgument,
                             "at-rule has no block to add rules to");
    }
    if (!atRule->children) {
        atRule->children = (CssRuleList *)RoboCalloc(1u,
                                                               sizeof(CssRuleList));
        if (!atRule->children) {
            return RoboCssDiag(diag, RoboCssDomainRule, RoboOutOfMemory,
                                 "could not allocate the at-rule's rule list");
        }
        CssRuleListInit(atRule->children);
    }
    return CssRuleListAppend(atRule->children, rule, diag);
}

size_t CssAtRuleRuleCount(const CssAtRule *atRule)
{
    if (!atRule || !atRule->children) {
        return 0u;
    }
    return CssRuleListCount(atRule->children);
}

const CssRule *CssAtRuleRuleAt(const CssAtRule *atRule,
                                                size_t index)
{
    if (!atRule || !atRule->children) {
        return NULL;
    }
    return CssRuleListAt(atRule->children, index);
}

Result CssAtRuleTakeDeclarations(CssAtRule *atRule,
                                                CssDeclarationList *declarations,
                                                CssDiag *diag)
{
    if (!atRule || !declarations) {
        return RoboCssDiag(diag, RoboCssDomainRule, RoboInvalidArgument,
                             "at-rule or declarations is NULL");
    }
    if (!atRule->hasBlock) {
        return RoboCssDiag(diag, RoboCssDomainRule, RoboInvalidArgument,
                             "at-rule has no block to take declarations from");
    }
    if (!CssDeclarationListIsEmpty(&atRule->declarations)) {
        return RoboCssDiag(diag, RoboCssDomainRule, RoboInvalidArgument,
                             "at-rule already has descriptor declarations");
    }
    /* Move, don't copy: the source list's buffer becomes the at-rule's, and the
     * source is left as the cleared record its own contract promises. */
    atRule->declarations = *declarations;
    CssDeclarationListInit(declarations);
    return RoboOk;
}

size_t CssAtRuleDeclarationCount(const CssAtRule *atRule)
{
    if (!atRule) {
        return 0u;
    }
    return CssDeclarationListCount(&atRule->declarations);
}

const CssDeclaration *CssAtRuleDeclaration(const CssAtRule *atRule,
                                                           size_t index)
{
    if (!atRule) {
        return NULL;
    }
    return CssDeclarationListAt(&atRule->declarations, index);
}

const CssDeclarationList *CssAtRuleDeclarations(
    const CssAtRule *atRule)
{
    if (!atRule) {
        return NULL;
    }
    return &atRule->declarations;
}

/* ------------------------------------------------------------------ */
/* Style rules                                                         */
/* ------------------------------------------------------------------ */

void CssStyleRuleInit(CssStyleRule *rule)
{
    if (!rule) {
        return;
    }
    CssSelectorListInit(&rule->selectors);
    CssDeclarationListInit(&rule->declarations);
    rule->range = CssRangeAt(0u);
    rule->preludeRange = CssRangeAt(0u);
    rule->blockRange = CssRangeAt(0u);
    CssValueInit(&rule->preludeValues);
    rule->blockClosed = 0;
}

void CssStyleRuleDispose(CssStyleRule *rule)
{
    if (!rule) {
        return;
    }
    CssSelectorListDispose(&rule->selectors);
    CssDeclarationListDispose(&rule->declarations);
    CssValueDispose(&rule->preludeValues);
    CssStyleRuleInit(rule);
}

Result CssStyleRuleSetPreludeValues(CssStyleRule *rule,
                                                    CssValue *preludeValues,
                                                    CssDiag *diag)
{
    if (!rule || !preludeValues) {
        return RoboCssDiag(diag, RoboCssDomainRule, RoboInvalidArgument,
                             "rule or prelude values is NULL");
    }
    if (preludeValues->kind != RoboCssValueKindList) {
        return RoboCssDiag(diag, RoboCssDomainRule, RoboInvalidArgument,
                             "a prelude is a list of component values");
    }
    CssValueDispose(&rule->preludeValues);
    return CssValueMove(preludeValues, &rule->preludeValues, diag);
}

const CssValue *CssStyleRulePreludeValues(
    const CssStyleRule *rule)
{
    if (!rule) {
        return NULL;
    }
    return &rule->preludeValues;
}

Result CssStyleRuleSetRanges(CssStyleRule *rule,
                                            CssRange range,
                                            CssRange preludeRange,
                                            CssRange blockRange,
                                            CssDiag *diag)
{
    if (!rule) {
        return RoboCssDiag(diag, RoboCssDomainRule, RoboInvalidArgument,
                             "rule is NULL");
    }
    rule->range = range;
    rule->preludeRange = preludeRange;
    rule->blockRange = blockRange;
    return RoboOk;
}

Result CssStyleRuleSetBlockClosed(CssStyleRule *rule,
                                                  int closed,
                                                  CssDiag *diag)
{
    if (!rule) {
        return RoboCssDiag(diag, RoboCssDomainRule, RoboInvalidArgument,
                             "rule is NULL");
    }
    rule->blockClosed = closed ? 1 : 0;
    return RoboOk;
}

int CssStyleRuleBlockClosed(const CssStyleRule *rule)
{
    if (!rule) {
        return 0;
    }
    return rule->blockClosed;
}

CssRange CssStyleRuleRange(const CssStyleRule *rule)
{
    if (!rule) {
        return CssRangeAt(0u);
    }
    return rule->range;
}

CssRange CssStyleRulePreludeRange(const CssStyleRule *rule)
{
    if (!rule) {
        return CssRangeAt(0u);
    }
    return rule->preludeRange;
}

CssRange CssStyleRuleBlockRange(const CssStyleRule *rule)
{
    if (!rule) {
        return CssRangeAt(0u);
    }
    return rule->blockRange;
}

Result CssStyleRuleAppendSelector(CssStyleRule *rule,
                                                 CssComplexSelector *selector,
                                                 CssDiag *diag)
{
    if (!rule) {
        return RoboCssDiag(diag, RoboCssDomainRule, RoboInvalidArgument,
                             "rule is NULL");
    }
    return CssSelectorListAppend(&rule->selectors, selector, diag);
}

Result CssStyleRuleAppendDeclaration(
    CssStyleRule *rule,
    CssDeclaration *declaration,
    CssDiag *diag)
{
    if (!rule) {
        return RoboCssDiag(diag, RoboCssDomainRule, RoboInvalidArgument,
                             "rule is NULL");
    }
    return CssDeclarationListAppend(&rule->declarations, declaration, diag);
}

size_t CssStyleRuleSelectorCount(const CssStyleRule *rule)
{
    if (!rule) {
        return 0u;
    }
    return CssSelectorListCount(&rule->selectors);
}

const CssSelectorList *CssStyleRuleSelectors(
    const CssStyleRule *rule)
{
    if (!rule) {
        return NULL;
    }
    return &rule->selectors;
}

size_t CssStyleRuleDeclarationCount(const CssStyleRule *rule)
{
    if (!rule) {
        return 0u;
    }
    return CssDeclarationListCount(&rule->declarations);
}

const CssDeclaration *CssStyleRuleDeclaration(
    const CssStyleRule *rule,
    size_t index)
{
    if (!rule) {
        return NULL;
    }
    return CssDeclarationListAt(&rule->declarations, index);
}

const CssDeclarationList *CssStyleRuleDeclarations(
    const CssStyleRule *rule)
{
    if (!rule) {
        return NULL;
    }
    return &rule->declarations;
}

int CssStyleRuleIsPopulated(const CssStyleRule *rule)
{
    if (!rule) {
        return 0;
    }
    return CssSelectorListIsSet(&rule->selectors) &&
           !CssDeclarationListIsEmpty(&rule->declarations);
}

/* ------------------------------------------------------------------ */
/* Rules                                                               */
/* ------------------------------------------------------------------ */

void CssRuleInit(CssRule *rule)
{
    if (!rule) {
        return;
    }
    rule->kind = RoboCssRuleKindNone;
    CssStyleRuleInit(&rule->style);
    CssAtRuleInit(&rule->at);
}

void CssRuleDispose(CssRule *rule)
{
    if (!rule) {
        return;
    }
    CssStyleRuleDispose(&rule->style);
    CssAtRuleDispose(&rule->at);
    rule->kind = RoboCssRuleKindNone;
}

Result CssRuleMakeStyle(CssRule *rule, CssDiag *diag)
{
    if (!rule) {
        return RoboCssDiag(diag, RoboCssDomainRule, RoboInvalidArgument,
                             "rule is NULL");
    }
    CssRuleDispose(rule);
    rule->kind = RoboCssRuleKindStyle;
    return RoboOk;
}

Result CssRuleMakeAt(CssRule *rule,
                                    CssSlice name,
                                    CssDiag *diag)
{
    Result result;

    if (!rule) {
        return RoboCssDiag(diag, RoboCssDomainRule, RoboInvalidArgument,
                             "rule is NULL");
    }
    CssRuleDispose(rule);
    result = CssAtRuleMake(&rule->at, name, diag);
    if (result != RoboOk) {
        return result;
    }
    rule->kind = RoboCssRuleKindAt;
    return RoboOk;
}

const CssStyleRule *CssRuleStyle(const CssRule *rule)
{
    if (!rule || rule->kind != RoboCssRuleKindStyle) {
        return NULL;
    }
    return &rule->style;
}

const CssAtRule *CssRuleAt(const CssRule *rule)
{
    if (!rule || rule->kind != RoboCssRuleKindAt) {
        return NULL;
    }
    return &rule->at;
}

CssRange CssRuleRange(const CssRule *rule)
{
    if (!rule) {
        return CssRangeAt(0u);
    }
    if (rule->kind == RoboCssRuleKindStyle) {
        return rule->style.range;
    }
    if (rule->kind == RoboCssRuleKindAt) {
        return rule->at.range;
    }
    return CssRangeAt(0u);
}

size_t CssRuleTreeCount(const CssRule *rule)
{
    size_t total = 1u;
    size_t i;

    if (!rule || rule->kind == RoboCssRuleKindNone) {
        return 0u;
    }
    if (rule->kind == RoboCssRuleKindAt) {
        for (i = 0u; i < CssAtRuleRuleCount(&rule->at); i++) {
            total += CssRuleTreeCount(CssAtRuleRuleAt(&rule->at, i));
        }
    }
    return total;
}

/* ------------------------------------------------------------------ */
/* Rule lists                                                          */
/* ------------------------------------------------------------------ */

void CssRuleListInit(CssRuleList *list)
{
    if (!list) {
        return;
    }
    list->items = NULL;
    list->count = 0u;
    list->capacity = 0u;
}

void CssRuleListClear(CssRuleList *list)
{
    size_t i;

    if (!list) {
        return;
    }
    for (i = 0u; i < list->count; i++) {
        CssRuleDispose(&list->items[i]);
    }
    list->count = 0u;
}

void CssRuleListDispose(CssRuleList *list)
{
    if (!list) {
        return;
    }
    CssRuleListClear(list);
    if (list->items) {
        RoboFree(list->items);
    }
    CssRuleListInit(list);
}

Result CssRuleListAppend(CssRuleList *list,
                                       CssRule *rule,
                                       CssDiag *diag)
{
    if (!list || !rule) {
        return RoboCssDiag(diag, RoboCssDomainRule, RoboInvalidArgument,
                             "list or rule is NULL");
    }
    if (rule->kind == RoboCssRuleKindNone) {
        return RoboCssDiag(diag, RoboCssDomainRule, RoboInvalidArgument,
                             "rule has not been made");
    }
    if (!CssGrow((void **)&list->items, &list->capacity, list->count,
                       sizeof(CssRule))) {
        return RoboCssDiag(diag, RoboCssDomainRule, RoboOutOfMemory,
                             "could not grow the rule list");
    }
    list->items[list->count] = *rule;
    list->count++;
    CssRuleInit(rule);
    return RoboOk;
}

size_t CssRuleListCount(const CssRuleList *list)
{
    return list ? list->count : 0u;
}

const CssRule *CssRuleListAt(const CssRuleList *list,
                                             size_t index)
{
    if (!list || index >= list->count) {
        return NULL;
    }
    return &list->items[index];
}

int CssRuleListIsEmpty(const CssRuleList *list)
{
    return !list || list->count == 0u;
}

size_t CssRuleListTreeCount(const CssRuleList *list)
{
    size_t i;
    size_t total = 0u;

    if (!list) {
        return 0u;
    }
    for (i = 0u; i < list->count; i++) {
        total += CssRuleTreeCount(&list->items[i]);
    }
    return total;
}

size_t CssRuleListStyleRuleCount(const CssRuleList *list)
{
    size_t i;
    size_t total = 0u;

    if (!list) {
        return 0u;
    }
    for (i = 0u; i < list->count; i++) {
        const CssRule *rule = &list->items[i];

        if (rule->kind == RoboCssRuleKindStyle) {
            total++;
        } else if (rule->kind == RoboCssRuleKindAt && rule->at.children) {
            total += CssRuleListStyleRuleCount(rule->at.children);
        }
    }
    return total;
}

/** Depth-first flattening; returns the number of rules found at and below @p list. */
static size_t FlattenRules(const CssRuleList *list,
                            const CssRule **outRules,
                            size_t capacity,
                            size_t *written)
{
    size_t i;
    size_t total = 0u;

    for (i = 0u; i < list->count; i++) {
        const CssRule *rule = &list->items[i];

        total++;
        if (outRules && *written < capacity) {
            outRules[*written] = rule;
            (*written)++;
        }
        if (rule->kind == RoboCssRuleKindAt && rule->at.children) {
            total += FlattenRules(rule->at.children, outRules, capacity, written);
        }
    }
    return total;
}

size_t CssRuleListFlatten(const CssRuleList *list,
                                  const CssRule **outRules,
                                  size_t capacity)
{
    size_t written = 0u;

    if (!list) {
        return 0u;
    }
    if (!outRules) {
        capacity = 0u;
    }
    return FlattenRules(list, outRules, capacity, &written);
}
