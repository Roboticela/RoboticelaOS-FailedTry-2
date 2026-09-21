/*
 * RoboCSS style boundary (Phase 9.1) — see RoboCSSStyle.h.
 *
 * The context borrows everything it is given: stylesheet ids stay owned by the
 * module (and the caller), the property table and the callback userdata stay
 * owned by the caller. Destroying a context therefore frees the context's own
 * per-origin id arrays and nothing else — no stylesheet is destroyed by
 * dropping the thing that referred to it, which is the difference between a
 * reference and an owner.
 */

#include "RoboCSS/RoboCSSStyle.h"

#include "RoboCSSInternal.h"
/* For CssCascadeInit and RoboCssCascadeCollect: this translation unit
 * owns the door, the cascade owns what comes out of it. */
#include "RoboCSS/RoboCSSCascade.h"
/* For RoboCssInheritResolve and the resolved-style accessors: this
 * translation unit owns the door, inheritance owns what comes out of it. */
#include "RoboCSS/RoboCSSInherit.h"
#include "RoboMem/RoboMem.h"

/** One origin's ordered list of borrowed stylesheet ids. */
typedef struct CssOriginSheets {
    CssStylesheetId *items; /* owned */
    size_t count;
    size_t capacity;
} CssOriginSheets;

struct CssStyleContext {
    CssOriginSheets origins[RoboCssOriginCount];
    const CssPropertyTable *propertyTable;
    /* Borrowed, like the table: which properties are classified is the caller's
     * policy, and the registry is where that policy lives (9.7). */
    const CssPropertyRegistry *propertyRegistry;
    /* The one place a host is stored. set_element_lookup writes this too, so
     * there is a single answer to "which callbacks are installed" rather than a
     * lookup slot and a host that could disagree. */
    CssElementHost host;
    CssInlineStyleFn inlineStyle;
    void *inlineUserdata;
};

/* ------------------------------------------------------------------ */
/* Element host                                                        */
/* ------------------------------------------------------------------ */

void CssElementHostInit(CssElementHost *host)
{
    if (!host) {
        return;
    }
    host->userdata = NULL;
    host->view = NULL;
    host->parent = NULL;
    host->PreviousSibling = NULL;
    host->NextSibling = NULL;
}

int CssElementHostIsUsable(const CssElementHost *host)
{
    return host && host->view != NULL;
}

/* ------------------------------------------------------------------ */
/* Element view                                                        */
/* ------------------------------------------------------------------ */

void CssElementViewInit(CssElementView *view)
{
    if (!view) {
        return;
    }
    view->ref = RoboCssElementRefNone;
    view->tagName = CssSliceEmpty();
    view->id = CssSliceEmpty();
    view->classes = NULL;
    view->classCount = 0u;
    view->attributes = NULL;
    view->attributeCount = 0u;
}

Result CssElementViewSetElement(CssElementView *view,
                                                CssElementRef ref,
                                                CssSlice tagName,
                                                CssDiag *diag)
{
    if (!view) {
        return RoboCssDiag(diag, RoboCssDomainStyle, RoboInvalidArgument,
                             "view is NULL");
    }
    if (ref == RoboCssElementRefNone) {
        return RoboCssDiag(diag, RoboCssDomainStyle, RoboInvalidArgument,
                             "an element view needs an element");
    }
    if (CssSliceIsEmpty(tagName)) {
        return RoboCssDiag(diag, RoboCssDomainStyle, RoboInvalidArgument,
                             "an element view needs a tag name");
    }
    view->ref = ref;
    view->tagName = tagName;
    return RoboOk;
}

Result CssElementViewSetId(CssElementView *view,
                                           CssSlice id,
                                           CssDiag *diag)
{
    if (!view) {
        return RoboCssDiag(diag, RoboCssDomainStyle, RoboInvalidArgument,
                             "view is NULL");
    }
    /* `id=""` is a real, empty id and is recorded as such; only "there is no id
     * attribute" is the absent slice, and that is what the caller passes. */
    view->id = id;
    return RoboOk;
}

Result CssElementViewSetClasses(CssElementView *view,
                                                const CssSlice *classes,
                                                size_t classCount,
                                                CssDiag *diag)
{
    if (!view) {
        return RoboCssDiag(diag, RoboCssDomainStyle, RoboInvalidArgument,
                             "view is NULL");
    }
    if (!classes && classCount > 0u) {
        return RoboCssDiag(diag, RoboCssDomainStyle, RoboInvalidArgument,
                             "NULL class array with a non-zero count");
    }
    view->classes = classes;
    view->classCount = classCount;
    return RoboOk;
}

Result CssElementViewSetAttributes(
    CssElementView *view,
    const CssAttribute *attributes,
    size_t attributeCount,
    CssDiag *diag)
{
    if (!view) {
        return RoboCssDiag(diag, RoboCssDomainStyle, RoboInvalidArgument,
                             "view is NULL");
    }
    if (!attributes && attributeCount > 0u) {
        return RoboCssDiag(diag, RoboCssDomainStyle, RoboInvalidArgument,
                             "NULL attribute array with a non-zero count");
    }
    view->attributes = attributes;
    view->attributeCount = attributeCount;
    return RoboOk;
}

int CssElementViewIsSet(const CssElementView *view)
{
    return view && view->ref != RoboCssElementRefNone;
}

int CssElementViewFindAttribute(const CssElementView *view,
                                        CssSlice name,
                                        CssSlice *outValue)
{
    size_t i;

    if (!view || !view->attributes || CssSliceIsEmpty(name)) {
        return 0;
    }
    for (i = 0u; i < view->attributeCount; i++) {
        if (CssSliceEqualsCi(view->attributes[i].name, name)) {
            if (outValue) {
                *outValue = view->attributes[i].value;
            }
            return 1;
        }
    }
    return 0;
}

int CssElementViewHasClass(const CssElementView *view,
                                    CssSlice name)
{
    size_t i;

    if (!view || !view->classes || CssSliceIsEmpty(name)) {
        return 0;
    }
    for (i = 0u; i < view->classCount; i++) {
        /* Class matching is byte-exact: HTML class names are case-sensitive. */
        if (CssSliceEquals(view->classes[i], name)) {
            return 1;
        }
    }
    return 0;
}

int CssElementViewIdEquals(const CssElementView *view,
                                    CssSlice id)
{
    if (!view) {
        return 0;
    }
    return CssSliceEquals(view->id, id);
}

/* ------------------------------------------------------------------ */
/* Style context                                                       */
/* ------------------------------------------------------------------ */

Result CssStyleContextCreate(CssStyleContext **outContext,
                                            CssDiag *diag)
{
    CssStyleContext *context;

    if (!outContext) {
        return RoboCssDiag(diag, RoboCssDomainStyle, RoboInvalidArgument,
                             "out_context is NULL");
    }
    *outContext = NULL;
    context = (CssStyleContext *)RoboCalloc(1u, sizeof(*context));
    if (!context) {
        return RoboCssDiag(diag, RoboCssDomainStyle, RoboOutOfMemory,
                             "could not allocate the style context");
    }
    *outContext = context;
    return RoboOk;
}

void CssStyleContextDestroy(CssStyleContext *context)
{
    size_t i;

    if (!context) {
        return;
    }
    for (i = 0u; i < (size_t)RoboCssOriginCount; i++) {
        if (context->origins[i].items) {
            RoboFree(context->origins[i].items);
        }
    }
    RoboFree(context);
}

static int OriginIsIndexable(CssOrigin origin)
{
    return origin > RoboCssOriginNone && origin < (CssOrigin)RoboCssOriginCount;
}

Result CssStyleContextAddSheet(CssStyleContext *context,
                                              CssOrigin origin,
                                              CssStylesheetId sheet,
                                              CssDiag *diag)
{
    CssOriginSheets *list;

    if (!context) {
        return RoboCssDiag(diag, RoboCssDomainStyle, RoboInvalidArgument,
                             "context is NULL");
    }
    if (!CssOriginIsStylesheetOrigin(origin)) {
        return RoboCssDiag(diag, RoboCssDomainStyle, RoboInvalidArgument,
                             "only runtime, user and author origins hold stylesheets");
    }
    if (sheet == RoboCssStylesheetIdNone) {
        return RoboCssDiag(diag, RoboCssDomainStyle, RoboInvalidArgument,
                             "no stylesheet id given");
    }
    if (!CssStylesheetIsLive(sheet)) {
        /* A context that accepted a dead id would fail much later, during a
         * resolution, in a place that does not explain where the id came from. */
        return RoboCssDiag(diag, RoboCssDomainStyle, RoboNotFound,
                             "stylesheet id does not resolve");
    }
    list = &context->origins[(size_t)origin];
    if (!CssGrow((void **)&list->items, &list->capacity, list->count,
                       sizeof(CssStylesheetId))) {
        return RoboCssDiag(diag, RoboCssDomainStyle, RoboOutOfMemory,
                             "could not grow the origin's stylesheet list");
    }
    list->items[list->count] = sheet;
    list->count++;
    return RoboOk;
}

size_t CssStyleContextSheetCount(const CssStyleContext *context,
                                         CssOrigin origin)
{
    if (!context || !OriginIsIndexable(origin)) {
        return 0u;
    }
    return context->origins[(size_t)origin].count;
}

CssStylesheetId CssStyleContextSheet(
    const CssStyleContext *context,
    CssOrigin origin,
    size_t index)
{
    if (!context || !OriginIsIndexable(origin)) {
        return RoboCssStylesheetIdNone;
    }
    if (index >= context->origins[(size_t)origin].count) {
        return RoboCssStylesheetIdNone;
    }
    return context->origins[(size_t)origin].items[index];
}

void CssStyleContextClearOrigin(CssStyleContext *context,
                                        CssOrigin origin)
{
    if (!context || !OriginIsIndexable(origin)) {
        return;
    }
    /* The ids are borrowed, so clearing the list releases nothing but the list. */
    context->origins[(size_t)origin].count = 0u;
}

void CssStyleContextClear(CssStyleContext *context)
{
    size_t i;

    if (!context) {
        return;
    }
    for (i = 0u; i < (size_t)RoboCssOriginCount; i++) {
        context->origins[i].count = 0u;
    }
}

size_t CssStyleContextTotalSheetCount(
    const CssStyleContext *context)
{
    size_t i;
    size_t total = 0u;

    if (!context) {
        return 0u;
    }
    for (i = 0u; i < (size_t)RoboCssOriginCount; i++) {
        total += context->origins[i].count;
    }
    return total;
}

Result CssStyleContextSetPropertyTable(
    CssStyleContext *context,
    const CssPropertyTable *table,
    CssDiag *diag)
{
    if (!context) {
        return RoboCssDiag(diag, RoboCssDomainStyle, RoboInvalidArgument,
                             "context is NULL");
    }
    context->propertyTable = table; /* borrowing: NULL clears it */
    return RoboOk;
}

const CssPropertyTable *CssStyleContextPropertyTable(
    const CssStyleContext *context)
{
    return context ? context->propertyTable : NULL;
}

Result CssStyleContextSetPropertyRegistry(
    CssStyleContext *context,
    const CssPropertyRegistry *registry,
    CssDiag *diag)
{
    if (!context) {
        return RoboCssDiag(diag, RoboCssDomainStyle, RoboInvalidArgument,
                             "context is NULL");
    }
    context->propertyRegistry = registry; /* borrowing: NULL clears it */
    return RoboOk;
}

const CssPropertyRegistry *CssStyleContextPropertyRegistry(
    const CssStyleContext *context)
{
    return context ? context->propertyRegistry : NULL;
}

Result CssStyleContextSetElementLookup(
    CssStyleContext *context,
    CssElementLookupFn lookup,
    void *userdata,
    CssDiag *diag)
{
    if (!context) {
        return RoboCssDiag(diag, RoboCssDomainStyle, RoboInvalidArgument,
                             "context is NULL");
    }
    if (lookup) {
        context->host.view = lookup;
        context->host.userdata = userdata;
    } else {
        /* Clearing the lookup clears the host: a host installs its callbacks as
         * a set, and leaving relations behind that no longer have a view to
         * describe elements would be half a host. */
        CssElementHostInit(&context->host);
    }
    return RoboOk;
}

CssElementLookupFn CssStyleContextElementLookup(
    const CssStyleContext *context)
{
    return context ? context->host.view : NULL;
}

Result CssStyleContextSetElementHost(
    CssStyleContext *context,
    const CssElementHost *host,
    CssDiag *diag)
{
    if (!context) {
        return RoboCssDiag(diag, RoboCssDomainStyle, RoboInvalidArgument,
                             "context is NULL");
    }
    if (!host) {
        CssElementHostInit(&context->host);
        return RoboOk;
    }
    if (!host->view &&
        (host->parent || host->PreviousSibling || host->NextSibling)) {
        /* A host that can say where an element sits but not what it is has
         * nothing a view could be filled from, so every selector would be
         * unevaluatable. Refusing here says so at the install site rather than
         * at the first match. */
        return RoboCssDiag(diag, RoboCssDomainStyle, RoboInvalidArgument,
                             "a host with relations needs a view callback");
    }
    context->host = *host;
    return RoboOk;
}

CssElementHost CssStyleContextElementHost(
    const CssStyleContext *context)
{
    CssElementHost host;

    CssElementHostInit(&host);
    if (context) {
        host = context->host;
    }
    return host;
}

Result CssStyleContextSetInlineStyle(
    CssStyleContext *context,
    CssInlineStyleFn provider,
    void *userdata,
    CssDiag *diag)
{
    if (!context) {
        return RoboCssDiag(diag, RoboCssDomainStyle, RoboInvalidArgument,
                             "context is NULL");
    }
    context->inlineStyle = provider;
    context->inlineUserdata = userdata;
    return RoboOk;
}

CssInlineStyleFn CssStyleContextInlineStyle(
    const CssStyleContext *context)
{
    return context ? context->inlineStyle : NULL;
}

void *CssStyleContextInlineUserdata(const CssStyleContext *context)
{
    return context ? context->inlineUserdata : NULL;
}

int CssStyleContextHasRuntimeSheet(
    const CssStyleContext *context)
{
    return CssStyleContextSheetCount(context, RoboCssOriginRuntime) > 0u;
}

/* ------------------------------------------------------------------ */
/* Resolution boundaries                                               */
/* ------------------------------------------------------------------ */

Result CssStyleContextMatchDeclarations(
    const CssStyleContext *context,
    CssElementRef element,
    CssCascade *out,
    CssError *outError,
    CssDiag *diag)
{
    if (!context || !out) {
        CssErrorSet(outError, RoboCssErrorKindNone, RoboInvalidArgument,
                           NULL, "context or output is NULL");
        return RoboInvalidArgument;
    }
    if (element == RoboCssElementRefNone) {
        /* A cascade is collected *for* an element. Nothing applies to no
         * element, so answering with an empty result would be a plausible lie
         * rather than a refusal, and every counter on the result would be
         * zero for a reason that had nothing to do with the stylesheets. */
        CssCascadeInit(out);
        CssErrorSet(outError, RoboCssErrorKindNone, RoboInvalidArgument,
                           NULL, "a collection needs an element");
        return RoboCssDiag(diag, RoboCssDomainStyle, RoboInvalidArgument,
                             "a collection needs an element");
    }
    /* The output is initialised first: a caller that ignores the result still
     * has a valid, disposable object rather than an uninitialised one. */
    return RoboCssCascadeCollect(context, element, out, outError, diag);
}

/**
 * Collect @p element's ancestor chain into @p chain, **leaf first**.
 *
 * The walk every resolution door in this file starts with, and the reason both
 * of them share it: "which elements are above this one" is a question about the
 * host, and a second answer to it would be a second chance to disagree with the
 * first about where a chain ends or when it is too long.
 *
 * Up first, resolve down afterwards — that order is what makes "the immediate
 * resolved parent" literal, because the level above is resolved before the
 * level below needs it.
 *
 * The chain is bounded by RoboCssInheritMaxDepth, and hitting the bound is
 * *reported* through @p outCapped rather than hidden: a DOM that reports a
 * cycle among parents (`a`'s parent is `b`, `b`'s is `a`) would otherwise walk
 * forever, and a truncated chain is not a root.
 *
 * @param capacity  entries @p chain can hold; must be at least 1.
 * @return the number of levels collected, at least 1 for a real element.
 */
static size_t StyleChainCollect(const CssStyleContext *context,
                                  CssElementRef element,
                                  CssElementRef *chain,
                                  size_t capacity,
                                  int *outCapped)
{
    CssElementHost host = CssStyleContextElementHost(context);
    size_t chainLength = 0u;

    *outCapped = 0;
    chain[chainLength++] = element;
    while (host.parent) {
        CssElementRef next;

        if (chainLength >= capacity) {
            /* Ask once more, so a chain that happens to be exactly the bound
             * long is not reported as truncated. */
            *outCapped = host.parent(host.userdata, chain[chainLength - 1u]) !=
                          RoboCssElementRefNone;
            break;
        }
        next = host.parent(host.userdata, chain[chainLength - 1u]);
        if (next == RoboCssElementRefNone) {
            break;
        }
        chain[chainLength++] = next;
    }
    return chainLength;
}

Result CssStyleContextResolveInheritedStyle(
    const CssStyleContext *context,
    CssElementRef element,
    CssInheritedStyle *out,
    CssError *outError,
    CssDiag *diag)
{
    /* One stack array rather than an allocation: the chain is bounded by the
     * same constant the walk is, and 64 element references is a size a frame
     * can hold. */
    CssElementRef chain[RoboCssInheritMaxDepth];
    CssInheritedStyle below = RoboCssCleared;
    CssInheritedStyle current = RoboCssCleared;
    size_t chainLength = 0u;
    size_t index;
    int capped = 0;
    int errorRecorded = 0;
    Result rc = RoboOk;

    if (!context || !out) {
        CssErrorSet(outError, RoboCssErrorKindNone, RoboInvalidArgument,
                           NULL, "context or output is NULL");
        return RoboInvalidArgument;
    }
    CssInheritedStyleInit(out);
    CssErrorInit(outError);
    if (element == RoboCssElementRefNone) {
        CssErrorSet(outError, RoboCssErrorKindNone, RoboInvalidArgument,
                           NULL, "a resolution needs an element");
        return RoboCssDiag(diag, RoboCssDomainInherit, RoboInvalidArgument,
                             "a resolution needs an element");
    }

    chainLength = StyleChainCollect(context, element, chain,
                                       (size_t)RoboCssInheritMaxDepth, &capped);

    for (index = chainLength; index-- > 0u;) {
        CssCascade cascade = RoboCssCleared;
        CssError error = RoboCssCleared;

        rc = RoboCssCascadeCollect(context, chain[index], &cascade, &error, diag);
        if (rc != RoboOk) {
            break;
        }
        /* The first CSS problem met anywhere on the chain, which is the same
         * rule every other door follows. */
        if (!errorRecorded && CssErrorIsError(&error)) {
            if (outError) {
                *outError = error;
            }
            errorRecorded = 1;
        }
        rc = RoboCssInheritResolve(context, &cascade,
                                      index + 1u < chainLength ? &below : NULL,
                                      &current, diag);
        CssCascadeDispose(&cascade);
        if (rc != RoboOk) {
            break;
        }
        CssInheritedStyleDispose(&below);
        below = current;
        CssInheritedStyleInit(&current);
    }

    if (rc != RoboOk) {
        CssInheritedStyleDispose(&below);
        CssInheritedStyleDispose(&current);
        /* Nothing was produced, so the caller gets an empty style rather than a
         * partial one it might mistake for an answer. */
        CssInheritedStyleInit(out);
        CssErrorSet(outError, RoboCssErrorKindOutOfMemory, rc, NULL,
                           "the inherited style could not be resolved");
        return rc;
    }

    *out = below;
    CssInheritedStyleInit(&below);
    if (capped) {
        /* The ancestor chain was longer than the walk goes, so the level this
         * resolution treated as a root is not one. Reported rather than
         * hidden: every value inherited from above that point is a lower bound. */
        out->unsupported = 1;
    }
    return RoboOk;
}

Result CssStyleContextComputeStyle(
    const CssStyleContext *context,
    CssElementRef element,
    CssComputedStyle *outStyle,
    CssError *outError,
    CssDiag *diag)
{
    /* Two computed styles, swapped each level: the one just computed is the
     * parent for the next, and the other is scratch. Nothing accumulates — the
     * intermediate styles cost one level each and are gone the moment the level
     * below has read them, which is what keeps computing a deep tree linear
     * rather than quadratic. */
    CssElementRef chain[RoboCssInheritMaxDepth];
    CssInheritedStyle below = RoboCssCleared;
    CssInheritedStyle current = RoboCssCleared;
    CssComputedStyle *upper = NULL;
    CssComputedStyle *lower = NULL;
    size_t chainLength = 0u;
    size_t index;
    int capped = 0;
    int errorRecorded = 0;
    Result rc;

    if (!context || !outStyle) {
        CssErrorSet(outError, RoboCssErrorKindNone, RoboInvalidArgument,
                           NULL, "context or output is NULL");
        return RoboInvalidArgument;
    }
    CssErrorInit(outError);
    CssComputedStyleClear(outStyle);
    if (element == RoboCssElementRefNone) {
        CssErrorSet(outError, RoboCssErrorKindNone, RoboInvalidArgument,
                           NULL, "a computation needs an element");
        return RoboCssDiag(diag, RoboCssDomainStyle, RoboInvalidArgument,
                             "a computation needs an element");
    }

    chainLength = StyleChainCollect(context, element, chain,
                                       (size_t)RoboCssInheritMaxDepth, &capped);
    rc = CssComputedStyleCreate(&upper, diag);
    if (rc == RoboOk) {
        rc = CssComputedStyleCreate(&lower, diag);
    }
    if (rc != RoboOk) {
        CssComputedStyleDestroy(upper);
        CssComputedStyleDestroy(lower);
        CssErrorSet(outError, RoboCssErrorKindOutOfMemory, rc, NULL,
                           "the computed style could not be built");
        return rc;
    }

    /* Root first, so every level below has its own computed parent in hand. */
    for (index = chainLength; index-- > 0u;) {
        CssCascade cascade = RoboCssCleared;
        CssError error = RoboCssCleared;

        rc = RoboCssCascadeCollect(context, chain[index], &cascade, &error, diag);
        if (rc != RoboOk) {
            break;
        }
        /* The first CSS problem met anywhere on the chain, which is the same
         * rule every other door follows. */
        if (!errorRecorded && CssErrorIsError(&error)) {
            if (outError) {
                *outError = error;
            }
            errorRecorded = 1;
        }
        rc = RoboCssInheritResolve(context, &cascade,
                                      index + 1u < chainLength ? &below : NULL,
                                      &current, diag);
        CssCascadeDispose(&cascade);
        if (rc != RoboOk) {
            break;
        }

        CssComputedStyleClear(lower);
        rc = CssStyleContextComputeResolvedStyle(context, &current, upper,
                                                           lower, &error, diag);
        if (rc != RoboOk) {
            break;
        }
        if (!errorRecorded && CssErrorIsError(&error)) {
            if (outError) {
                *outError = error;
            }
            errorRecorded = 1;
        }

        /* This level's computed style becomes the next level's parent. */
        {
            CssComputedStyle *swap = upper;

            upper = lower;
            lower = swap;
        }
        CssInheritedStyleDispose(&below);
        below = current;
        CssInheritedStyleInit(&current);
    }

    CssInheritedStyleDispose(&below);
    CssInheritedStyleDispose(&current);

    if (rc != RoboOk) {
        CssComputedStyleDestroy(upper);
        CssComputedStyleDestroy(lower);
        CssComputedStyleClear(outStyle);
        CssErrorSet(outError, RoboCssErrorKindOutOfMemory, rc, NULL,
                           "the computed style could not be built");
        return rc;
    }

    /* The last level computed is the element's own, and it is `upper` after the
     * final swap. Moving rather than copying: the caller asked for this style
     * and there is no reason to duplicate every value to hand it over. */
    rc = CssComputedStyleTake(outStyle, upper, diag);
    if (rc != RoboOk) {
        CssComputedStyleDestroy(upper);
        CssComputedStyleDestroy(lower);
        CssComputedStyleClear(outStyle);
        CssErrorSet(outError, RoboCssErrorKindOutOfMemory, rc, NULL,
                           "the computed style could not be handed over");
        return rc;
    }
    if (capped) {
        /* Every value inherited from above the point the walk stopped is a
         * lower bound, and saying so is what keeps the style from being read as
         * complete. */
        CssComputedStyleMarkTruncated(outStyle);
    }
    CssComputedStyleDestroy(upper);
    CssComputedStyleDestroy(lower);
    return RoboOk;
}
