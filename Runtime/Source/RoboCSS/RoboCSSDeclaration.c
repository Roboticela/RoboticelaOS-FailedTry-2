/*
 * RoboCSS declaration model (Phase 9.1) — see RoboCSSDeclaration.h.
 *
 * Ownership is one-directional and never shared: a declaration owns exactly one
 * value tree, a list owns its declarations, and both hand ownership over on
 * append (the appended object is left NONE). The only borrowed part of a
 * declaration is its name slice, which points into the source the caller
 * attached to the stylesheet.
 */

#include "RoboCSS/RoboCSSDeclaration.h"

#include "RoboCSSInternal.h"
#include "RoboMem/RoboMem.h"

int CssDeclarationStateIsKnown(CssDeclarationState state)
{
    return state >= RoboCssDeclarationStateNone &&
           state < (CssDeclarationState)RoboCssDeclarationStateCount;
}

const char *CssDeclarationStateName(CssDeclarationState state)
{
    switch (state) {
    case RoboCssDeclarationStateNone:
        return "none";
    case RoboCssDeclarationStateParsed:
        return "parsed";
    case RoboCssDeclarationStateInvalid:
        return "invalid";
    default:
        return "unknown";
    }
}

void CssDeclarationInit(CssDeclaration *declaration)
{
    if (!declaration) {
        return;
    }
    declaration->state = RoboCssDeclarationStateNone;
    declaration->name = CssSliceEmpty();
    declaration->property = RoboCssPropertyIdNone;
    declaration->important = 0;
    declaration->range = CssRangeAt(0u);
    declaration->nameRange = CssRangeAt(0u);
    declaration->valueRange = CssRangeAt(0u);
    CssValueInit(&declaration->value);
}

void CssDeclarationDispose(CssDeclaration *declaration)
{
    if (!declaration) {
        return;
    }
    CssValueDispose(&declaration->value);
    CssDeclarationInit(declaration);
}

Result CssDeclarationMake(CssDeclaration *declaration,
                                       CssSlice name,
                                       CssDeclarationState state,
                                       CssDiag *diag)
{
    if (!declaration) {
        return RoboCssDiag(diag, RoboCssDomainDeclaration, RoboInvalidArgument,
                             "declaration is NULL");
    }
    if (state != RoboCssDeclarationStateParsed &&
        state != RoboCssDeclarationStateInvalid) {
        return RoboCssDiag(diag, RoboCssDomainDeclaration, RoboInvalidArgument,
                             "a declaration is made in the parsed or invalid state");
    }
    if (state == RoboCssDeclarationStateParsed && CssSliceIsEmpty(name)) {
        return RoboCssDiag(diag, RoboCssDomainDeclaration, RoboInvalidArgument,
                             "a parsed declaration needs a name");
    }
    CssDeclarationDispose(declaration);
    declaration->state = state;
    declaration->name = name;
    return RoboOk;
}

Result CssDeclarationSetImportant(CssDeclaration *declaration,
                                                int important,
                                                CssDiag *diag)
{
    if (!declaration) {
        return RoboCssDiag(diag, RoboCssDomainDeclaration, RoboInvalidArgument,
                             "declaration is NULL");
    }
    declaration->important = important ? 1 : 0;
    return RoboOk;
}

Result CssDeclarationSetValue(CssDeclaration *declaration,
                                            CssValue *value,
                                            CssDiag *diag)
{
    if (!declaration || !value) {
        return RoboCssDiag(diag, RoboCssDomainDeclaration, RoboInvalidArgument,
                             "declaration or value is NULL");
    }
    if (declaration->state == RoboCssDeclarationStateNone) {
        return RoboCssDiag(diag, RoboCssDomainDeclaration, RoboInvalidArgument,
                             "declaration has not been made");
    }
    return CssValueMove(value, &declaration->value, diag);
}

Result CssDeclarationSetRanges(CssDeclaration *declaration,
                                             CssRange range,
                                             CssRange nameRange,
                                             CssRange valueRange,
                                             CssDiag *diag)
{
    if (!declaration) {
        return RoboCssDiag(diag, RoboCssDomainDeclaration, RoboInvalidArgument,
                             "declaration is NULL");
    }
    declaration->range = range;
    declaration->nameRange = nameRange;
    declaration->valueRange = valueRange;
    return RoboOk;
}

CssPropertyId CssDeclarationResolveProperty(
    CssDeclaration *declaration,
    const CssPropertyTable *table)
{
    if (!declaration || !table) {
        return RoboCssPropertyIdNone;
    }
    declaration->property = CssPropertyTableLookupSlice(table,
                                                                 declaration->name);
    return declaration->property;
}

int CssDeclarationHasValue(const CssDeclaration *declaration)
{
    return declaration && !CssValueIsEmpty(&declaration->value);
}

int CssDeclarationIsUsable(const CssDeclaration *declaration)
{
    if (!declaration) {
        return 0;
    }
    if (declaration->state != RoboCssDeclarationStateParsed) {
        return 0;
    }
    return !CssSliceIsEmpty(declaration->name);
}

CssSlice CssDeclarationName(const CssDeclaration *declaration)
{
    if (!declaration) {
        return CssSliceEmpty();
    }
    return declaration->name;
}

const CssValue *CssDeclarationValue(
    const CssDeclaration *declaration)
{
    if (!declaration) {
        return NULL;
    }
    return &declaration->value;
}

/* ------------------------------------------------------------------ */
/* Declaration lists                                                   */
/* ------------------------------------------------------------------ */

void CssDeclarationListInit(CssDeclarationList *list)
{
    if (!list) {
        return;
    }
    list->items = NULL;
    list->count = 0u;
    list->capacity = 0u;
}

void CssDeclarationListClear(CssDeclarationList *list)
{
    size_t i;

    if (!list) {
        return;
    }
    for (i = 0u; i < list->count; i++) {
        CssDeclarationDispose(&list->items[i]);
    }
    list->count = 0u;
}

void CssDeclarationListDispose(CssDeclarationList *list)
{
    if (!list) {
        return;
    }
    CssDeclarationListClear(list);
    if (list->items) {
        RoboFree(list->items);
    }
    CssDeclarationListInit(list);
}

Result CssDeclarationListAppend(CssDeclarationList *list,
                                               CssDeclaration *declaration,
                                               CssDiag *diag)
{
    if (!list || !declaration) {
        return RoboCssDiag(diag, RoboCssDomainDeclaration, RoboInvalidArgument,
                             "list or declaration is NULL");
    }
    if (declaration->state == RoboCssDeclarationStateNone) {
        return RoboCssDiag(diag, RoboCssDomainDeclaration, RoboInvalidArgument,
                             "declaration has not been made");
    }
    if (!CssGrow((void **)&list->items, &list->capacity, list->count,
                       sizeof(CssDeclaration))) {
        return RoboCssDiag(diag, RoboCssDomainDeclaration, RoboOutOfMemory,
                             "could not grow the declaration list");
    }
    list->items[list->count] = *declaration;
    list->count++;
    CssDeclarationInit(declaration);
    return RoboOk;
}

size_t CssDeclarationListCount(const CssDeclarationList *list)
{
    return list ? list->count : 0u;
}

const CssDeclaration *CssDeclarationListAt(
    const CssDeclarationList *list,
    size_t index)
{
    if (!list || index >= list->count) {
        return NULL;
    }
    return &list->items[index];
}

int CssDeclarationListIsEmpty(const CssDeclarationList *list)
{
    return !list || list->count == 0u;
}

int CssDeclarationListFindFirst(const CssDeclarationList *list,
                                        CssPropertyId property,
                                        size_t *outIndex)
{
    size_t i;

    if (!list || property == RoboCssPropertyIdNone) {
        return 0;
    }
    for (i = 0u; i < list->count; i++) {
        if (list->items[i].property == property) {
            if (outIndex) {
                *outIndex = i;
            }
            return 1;
        }
    }
    return 0;
}

int CssDeclarationListFindLast(const CssDeclarationList *list,
                                       CssPropertyId property,
                                       size_t *outIndex)
{
    size_t i;

    if (!list || property == RoboCssPropertyIdNone) {
        return 0;
    }
    i = list->count;
    while (i > 0u) {
        i--;
        if (list->items[i].property == property) {
            if (outIndex) {
                *outIndex = i;
            }
            return 1;
        }
    }
    return 0;
}

size_t CssDeclarationListCountOf(const CssDeclarationList *list,
                                         CssPropertyId property)
{
    size_t i;
    size_t count = 0u;

    if (!list || property == RoboCssPropertyIdNone) {
        return 0u;
    }
    for (i = 0u; i < list->count; i++) {
        if (list->items[i].property == property) {
            count++;
        }
    }
    return count;
}

/* ------------------------------------------------------------------ */
/* Inline-style boundary                                               */
/* ------------------------------------------------------------------ */

Result CssDeclarationListParseText(const char *text,
                                                   size_t length,
                                                   CssOrigin origin,
                                                   CssDeclarationList *outList,
                                                   CssError *outError)
{
    CssSource source = RoboCssCleared;

    if (!outList) {
        return RoboInvalidArgument;
    }
    if (!text && length > 0u) {
        return RoboInvalidArgument;
    }
    /* The list is initialised before anything can fail, so a caller can dispose
     * it unconditionally and never has to guess. */
    CssDeclarationListInit(outList);

    /* The caller's bytes, borrowed and never copied. That is the ownership
     * contract of this boundary: the declarations that come back borrow their
     * name and value slices from @p text, so @p text must outlive @p outList.
     * A DOM that parses its own `style` attribute keeps the attribute alive for
     * as long as it keeps the list, which is exactly the lifetime a caller
     * already has. */
    if (CssSourceSetMemory(&source, (const uint8_t *)text, length, NULL) != RoboOk) {
        CssErrorSet(outError, RoboCssErrorKindNone, RoboInvalidArgument,
                           NULL, "the declaration text could not be attached as a CSS source");
        return RoboInvalidArgument;
    }
    /* The source is borrowed, so there is nothing to release afterwards. */
    return CssGrammarParseDeclarations(outList, &source, origin, outError);
}
