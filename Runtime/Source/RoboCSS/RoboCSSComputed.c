/*
 * RoboCSS computed-style container (Phase 9.1) — see RoboCSSComputed.h.
 *
 * A container, not a computation. Values are moved in and owned here; setting
 * an existing property replaces in place so that iteration order reflects the
 * order properties were first introduced rather than how often they were
 * written, which is the difference between a deterministic container and one
 * that merely happens to be deterministic in simple tests.
 */

#include "RoboCSS/RoboCSSComputed.h"

#include "RoboCSS/RoboCSSInherit.h"
#include "RoboCSS/RoboCSSStyle.h"
#include "RoboCSSInternal.h"
#include "RoboMem/RoboMem.h"

#include <string.h>

typedef struct CssComputedEntry {
    CssPropertyId property;
    /** Owned, NUL-terminated spelling; NULL when the entry has none. */
    char *name;
    size_t nameLength;
    /** Bitmask of CssComputedFlag. */
    uint32_t flags;
    /** The specified value; owns its children. */
    CssValue value;
    /** The typed computed value; KIND_NONE when nobody computed it. */
    CssComputedValue computed;
} CssComputedEntry;

struct CssComputedStyle {
    CssComputedEntry *entries; /* owned */
    size_t count;
    size_t capacity;
    /** Non-zero when the walk behind this style hit its ancestor bound. */
    int truncated;
};

Result CssComputedStyleCreate(CssComputedStyle **outStyle,
                                             CssDiag *diag)
{
    CssComputedStyle *style;

    if (!outStyle) {
        return RoboCssDiag(diag, RoboCssDomainComputed, RoboInvalidArgument,
                             "out_style is NULL");
    }
    *outStyle = NULL;
    style = (CssComputedStyle *)RoboCalloc(1u, sizeof(*style));
    if (!style) {
        return RoboCssDiag(diag, RoboCssDomainComputed, RoboOutOfMemory,
                             "could not allocate the computed style");
    }
    *outStyle = style;
    return RoboOk;
}

void CssComputedStyleClear(CssComputedStyle *style)
{
    size_t i;

    if (!style) {
        return;
    }
    for (i = 0u; i < style->count; i++) {
        CssValueDispose(&style->entries[i].value);
        CssComputedValueDispose(&style->entries[i].computed);
        RoboFree(style->entries[i].name);
        style->entries[i].name = NULL;
        style->entries[i].nameLength = 0u;
        style->entries[i].flags = RoboCssComputedFlagNone;
    }
    style->count = 0u;
}

void CssComputedStyleDestroy(CssComputedStyle *style)
{
    if (!style) {
        return;
    }
    CssComputedStyleClear(style);
    if (style->entries) {
        RoboFree(style->entries);
    }
    RoboFree(style);
}

/** Entry for @p property, or NULL when it is not present. */
static CssComputedEntry *ComputedFind(CssComputedStyle *style,
                                                CssPropertyId property)
{
    size_t i;

    if (!style || property == RoboCssPropertyIdNone) {
        return NULL;
    }
    for (i = 0u; i < style->count; i++) {
        if (style->entries[i].property == property) {
            return &style->entries[i];
        }
    }
    return NULL;
}

/**
 * Does @p entry name the property @p name is looking for?
 *
 * CSS's own rule, stated once: an ordinary property name is compared ASCII
 * case-insensitively, a custom property byte-exactly. The property registry and
 * the inheritance model use the same rule, and they have to — a lookup that
 * disagreed with registration about whether `--X` and `--x` are one property
 * would find the wrong entry, or none.
 */
static int ComputedEntryMatches(const CssComputedEntry *entry,
                                  CssSlice name)
{
    CssSlice stored;

    if (!entry->name) {
        return 0;
    }
    stored = CssSliceFromBytes((const uint8_t *)entry->name, entry->nameLength);
    if (CssIsCustomPropertyName(name) ||
        CssIsCustomPropertyName(stored)) {
        return CssSliceEquals(stored, name);
    }
    return CssSliceEqualsCi(stored, name);
}

/** The entry for @p name, or NULL. */
static CssComputedEntry *ComputedFindName(
    const CssComputedStyle *style,
    CssSlice name)
{
    size_t i;

    if (!style || CssSliceIsAbsent(name)) {
        return NULL;
    }
    for (i = 0u; i < style->count; i++) {
        if (ComputedEntryMatches(&style->entries[i], name)) {
            return &style->entries[i];
        }
    }
    return NULL;
}

/** Borrow a fresh entry for @p property, appending it. NULL on failure. */
static CssComputedEntry *ComputedClaim(CssComputedStyle *style,
                                                 CssPropertyId property)
{
    if (!CssGrow((void **)&style->entries, &style->capacity, style->count,
                       sizeof(CssComputedEntry))) {
        return NULL;
    }
    memset(&style->entries[style->count], 0, sizeof(CssComputedEntry));
    style->entries[style->count].property = property;
    style->entries[style->count].flags = RoboCssComputedFlagNone;
    CssValueInit(&style->entries[style->count].value);
    CssComputedValueInit(&style->entries[style->count].computed);
    style->count++;
    return &style->entries[style->count - 1u];
}

Result CssComputedStyleSet(CssComputedStyle *style,
                                          CssPropertyId property,
                                          CssValue *value,
                                          CssDiag *diag)
{
    CssComputedEntry *entry;

    if (!style || !value) {
        return RoboCssDiag(diag, RoboCssDomainComputed, RoboInvalidArgument,
                             "style or value is NULL");
    }
    if (property == RoboCssPropertyIdNone) {
        return RoboCssDiag(diag, RoboCssDomainComputed, RoboInvalidArgument,
                             "a computed value needs a property identity");
    }
    entry = ComputedFind(style, property);
    if (!entry) {
        entry = ComputedClaim(style, property);
        if (!entry) {
            return RoboCssDiag(diag, RoboCssDomainComputed, RoboOutOfMemory,
                                 "could not grow the computed style");
        }
    }
    return CssValueMove(value, &entry->value, diag);
}

const CssValue *CssComputedStyleGet(
    const CssComputedStyle *style,
    CssPropertyId property)
{
    CssComputedEntry *entry =
        ComputedFind((CssComputedStyle *)style, property);

    if (!entry) {
        return NULL;
    }
    return &entry->value;
}

int CssComputedStyleHas(const CssComputedStyle *style,
                                CssPropertyId property)
{
    return CssComputedStyleGet(style, property) != NULL;
}

Result CssComputedStyleRemove(CssComputedStyle *style,
                                             CssPropertyId property,
                                             CssDiag *diag)
{
    size_t i;

    if (!style) {
        return RoboCssDiag(diag, RoboCssDomainComputed, RoboInvalidArgument,
                             "style is NULL");
    }
    for (i = 0u; i < style->count; i++) {
        size_t j;

        if (style->entries[i].property != property) {
            continue;
        }
        CssValueDispose(&style->entries[i].value);
        CssComputedValueDispose(&style->entries[i].computed);
        RoboFree(style->entries[i].name);
        /* Move the tail down so the relative order of everything else is kept. */
        for (j = i + 1u; j < style->count; j++) {
            style->entries[j - 1u] = style->entries[j];
            style->entries[j].property = RoboCssPropertyIdNone;
            style->entries[j].name = NULL;
            style->entries[j].nameLength = 0u;
            style->entries[j].flags = RoboCssComputedFlagNone;
            CssValueInit(&style->entries[j].value);
            CssComputedValueInit(&style->entries[j].computed);
        }
        style->count--;
        return RoboOk;
    }
    return RoboCssDiag(diag, RoboCssDomainComputed, RoboNotFound,
                         "property is not in this computed style");
}

size_t CssComputedStyleCount(const CssComputedStyle *style)
{
    return style ? style->count : 0u;
}

Result CssComputedStylePropertyAt(
    const CssComputedStyle *style,
    size_t index,
    CssPropertyId *outProperty,
    CssDiag *diag)
{
    if (!style || !outProperty) {
        return RoboCssDiag(diag, RoboCssDomainComputed, RoboInvalidArgument,
                             "style or output is NULL");
    }
    if (index >= style->count) {
        return RoboCssDiag(diag, RoboCssDomainComputed, RoboNotFound,
                             "index is past the end of the computed style");
    }
    *outProperty = style->entries[index].property;
    return RoboOk;
}

const CssValue *CssComputedStyleValueAt(
    const CssComputedStyle *style,
    size_t index)
{
    if (!style || index >= style->count) {
        return NULL;
    }
    return &style->entries[index].value;
}

/* ------------------------------------------------------------------ */
/* The computed half of an entry (9.8)                                 */
/* ------------------------------------------------------------------ */

Result CssComputedStyleSetEntry(
    CssComputedStyle *style,
    CssPropertyId property,
    CssSlice name,
    uint32_t flags,
    CssValue *specified,
    CssComputedValue *computed,
    CssDiag *diag)
{
    CssComputedEntry *entry;
    char *nameCopy = NULL;

    if (!style) {
        return RoboCssDiag(diag, RoboCssDomainComputed, RoboInvalidArgument,
                             "style is NULL");
    }
    if (property == RoboCssPropertyIdNone && CssSliceIsAbsent(name)) {
        return RoboCssDiag(diag, RoboCssDomainComputed, RoboInvalidArgument,
                             "an entry needs an identity or a name");
    }

    /* Find or claim first, but copy the name *before* moving anything in, so a
     * failure to allocate the name leaves the style exactly as it was rather
     * than with half an entry. */
    entry = property != RoboCssPropertyIdNone ? ComputedFind(style, property)
                                                  : ComputedFindName(style, name);
    if (!entry) {
        entry = ComputedClaim(style, property);
        if (!entry) {
            return RoboCssDiag(diag, RoboCssDomainComputed, RoboOutOfMemory,
                                 "could not grow the computed style");
        }
    }
    if (!CssSliceIsAbsent(name)) {
        nameCopy = (char *)Malloc(name.length + 1u);
        if (!nameCopy) {
            return RoboCssDiag(diag, RoboCssDomainComputed, RoboOutOfMemory,
                                 "could not copy the property name");
        }
        if (name.length > 0u) {
            memcpy(nameCopy, name.bytes, name.length);
        }
        nameCopy[name.length] = '\0';
        RoboFree(entry->name);
        entry->name = nameCopy;
        entry->nameLength = name.length;
    }
    if (property != RoboCssPropertyIdNone) {
        entry->property = property;
    }
    entry->flags = flags;
    if (specified) {
        Result result = CssValueMove(specified, &entry->value, diag);

        if (result != RoboOk) {
            return result;
        }
    }
    if (computed) {
        return CssComputedValueMove(computed, &entry->computed, diag);
    }
    return RoboOk;
}

size_t CssComputedStyleFind(const CssComputedStyle *style,
                                    CssSlice name)
{
    size_t i;

    if (!style || CssSliceIsAbsent(name)) {
        return (size_t)-1;
    }
    for (i = 0u; i < style->count; i++) {
        if (ComputedEntryMatches(&style->entries[i], name)) {
            return i;
        }
    }
    return (size_t)-1;
}

CssSlice CssComputedStyleNameAt(
    const CssComputedStyle *style,
    size_t index)
{
    if (!style || index >= style->count || !style->entries[index].name) {
        return CssSliceEmpty();
    }
    return CssSliceFromBytes((const uint8_t *)style->entries[index].name,
                                     style->entries[index].nameLength);
}

uint32_t CssComputedStyleFlagsAt(const CssComputedStyle *style,
                                          size_t index)
{
    if (!style || index >= style->count) {
        return RoboCssComputedFlagNone;
    }
    return style->entries[index].flags;
}

const CssComputedValue *CssComputedStyleComputedAt(
    const CssComputedStyle *style,
    size_t index)
{
    if (!style || index >= style->count ||
        style->entries[index].computed.kind == RoboCssComputedValueNone) {
        return NULL;
    }
    return &style->entries[index].computed;
}

const CssComputedValue *CssComputedStyleComputed(
    const CssComputedStyle *style,
    CssPropertyId property)
{
    CssComputedEntry *entry =
        ComputedFind((CssComputedStyle *)style, property);

    if (!entry || entry->computed.kind == RoboCssComputedValueNone) {
        return NULL;
    }
    return &entry->computed;
}

const CssComputedValue *CssComputedStyleComputedByName(
    const CssComputedStyle *style,
    CssSlice name)
{
    CssComputedEntry *entry = ComputedFindName(style, name);

    if (!entry || entry->computed.kind == RoboCssComputedValueNone) {
        return NULL;
    }
    return &entry->computed;
}

uint32_t CssComputedStyleFlags(const CssComputedStyle *style,
                                       CssSlice name)
{
    CssComputedEntry *entry = ComputedFindName(style, name);

    return entry ? entry->flags : RoboCssComputedFlagNone;
}

const CssValue *CssComputedStyleSpecifiedByName(
    const CssComputedStyle *style,
    CssSlice name)
{
    CssComputedEntry *entry = ComputedFindName(style, name);

    if (!entry || entry->value.kind == RoboCssValueKindNone) {
        return NULL;
    }
    return &entry->value;
}

int CssComputedStyleHasFlag(const CssComputedStyle *style,
                                     CssSlice name,
                                     uint32_t flag)
{
    return (CssComputedStyleFlags(style, name) & flag) != 0u;
}

int CssComputedStyleHasInvalid(const CssComputedStyle *style)
{
    size_t i;

    if (!style) {
        return 0;
    }
    for (i = 0u; i < style->count; i++) {
        if ((style->entries[i].flags & RoboCssComputedFlagInvalid) != 0u) {
            return 1;
        }
    }
    return 0;
}

int CssComputedStyleIsTruncated(const CssComputedStyle *style)
{
    return style ? style->truncated : 0;
}

void CssComputedStyleMarkTruncated(CssComputedStyle *style)
{
    if (style) {
        style->truncated = 1;
    }
}

size_t CssComputedStyleIncompleteCount(
    const CssComputedStyle *style)
{
    size_t i;
    size_t count = 0u;

    if (!style) {
        return 0u;
    }
    for (i = 0u; i < style->count; i++) {
        uint32_t flags = style->entries[i].flags;

        if ((flags & (RoboCssComputedFlagInvalid |
                      RoboCssComputedFlagUnresolved)) != 0u) {
            count++;
        }
    }
    return count;
}

/* ------------------------------------------------------------------ */
/* Moving a whole style                                               */
/* ------------------------------------------------------------------ */

Result CssComputedStyleTake(CssComputedStyle *dest,
                                           CssComputedStyle *src,
                                           CssDiag *diag)
{
    if (!dest || !src || dest == src) {
        return RoboCssDiag(diag, RoboCssDomainComputed, RoboInvalidArgument,
                             "a computed style cannot take from itself");
    }
    /* Room first, clearing second: a caller that ignores the result code still
     * owns whatever it owned before. */
    while (dest->capacity < src->count) {
        if (!CssGrow((void **)&dest->entries, &dest->capacity,
                           src->count - 1u, sizeof(CssComputedEntry))) {
            return RoboCssDiag(diag, RoboCssDomainComputed, RoboOutOfMemory,
                                 "the computed style could not be enlarged");
        }
    }
    CssComputedStyleClear(dest);
    dest->truncated = src->truncated;
    if (src->count > 0u) {
        memcpy(dest->entries, src->entries,
               src->count * sizeof(CssComputedEntry));
        dest->count = src->count;
        /* The source keeps its array but owns none of its contents any more. */
        memset(src->entries, 0, src->count * sizeof(CssComputedEntry));
        src->count = 0u;
    }
    src->truncated = 0;
    return RoboOk;
}

/* ------------------------------------------------------------------ */
/* Computing one resolved style (9.8)                                 */
/* ------------------------------------------------------------------ */

/** The flags @p source implies, before anything about the value is known. */
static uint32_t ComputedFlagsOfSource(CssInheritedSource source)
{
    switch (source) {
    case RoboCssInheritedSourceDeclared:
        return RoboCssComputedFlagDeclared;
    case RoboCssInheritedSourceParent:
        return RoboCssComputedFlagInherited;
    case RoboCssInheritedSourceDefault:
        return RoboCssComputedFlagDefault;
    case RoboCssInheritedSourceDeferred:
        /* `revert` is carried, not obeyed, and a carried keyword is not a value
         * this build can compute — so it is unresolved as well as deferred. */
        return RoboCssComputedFlagDeferred | RoboCssComputedFlagUnresolved;
    default:
        return RoboCssComputedFlagNone;
    }
}

/*
 * Resolving `currentColor` (9.9)
 *
 * `currentColor` is a colour that is not a colour yet: it is whatever the
 * element's `color` turns out to be, and *which* `color` that is depends on the
 * property it was written on. That makes this the only layer that can resolve
 * it — the value layer has no element and no parent — and the two rules are:
 *
 *   on `color` itself     the **parent's** computed `color`.
 *                         `color: currentColor` means "the colour I would have
 *                         inherited", which is CSS's own rule and the only
 *                         reading that terminates: the element's own `color` is
 *                         the thing being computed.
 *   on anything else      the element's own computed `color`.
 *                         `background-color: currentColor` is "my text colour",
 *                         so it resolves against a value that must already be
 *                         final — which is why `color` is resolved first.
 *
 * Both rules are applied in place, on a finished style: a `currentColor` becomes
 * the channels it stands for, and the UNRESOLVED bit comes off, so a consumer
 * reading the style sees a colour rather than a promise. When there is nothing to
 * resolve it against — no parent computed style, which is what a root element and
 * a caller that computed one style at a time both look like — the colour **stays**
 * CURRENT and **stays** flagged unresolved. That is the honest answer, and
 * inventing black would be exactly the mistake `currentColor` exists to prevent.
 *
 * Only the UNRESOLVED *bit* is cleared, never the whole flag set: an entry can be
 * declared and unresolved and inherited all at once, and the other facts about it
 * are still true after its colour is known. Clearing it is safe for a colour
 * specifically because a COLOR value's own unresolved flag is set in exactly one
 * place — the `currentColor` branch of the colour computation — and not by
 * anything else that could leave the bit meaning something different.
 */

/** The property `currentColor` defers to. */
#define COMPUTED_COLOR_PROPERTY "color"

/**
 * Replace one entry's contextual colour with @p color, when it resolves against
 * it.
 *
 * A no-op for every entry that is not a `currentColor`, which is what makes the
 * pass below read as one sentence.
 */
static void ComputedResolveEntryColor(CssComputedEntry *entry,
                                         CssColor color)
{
    CssColor resolved;

    if (!entry || entry->computed.kind != RoboCssComputedValueColor) {
        return;
    }
    if (!CssColorIsCurrent(entry->computed.color)) {
        return;
    }
    if (!CssColorResolveCurrent(entry->computed.color, color, &resolved)) {
        /* `color` is not a resolved colour here either: the colour stays
         * contextual, and the flag keeps saying so rather than a default being
         * filled in behind the author's back. */
        return;
    }
    entry->computed.color = resolved;
    entry->computed.unresolved = 0;
    entry->flags &= ~(uint32_t)RoboCssComputedFlagUnresolved;
}

/** The computed colour of the entry for @p name, or NONE when there is none. */
static CssColor ComputedColorOf(const CssComputedStyle *style,
                                          const char *name)
{
    const CssComputedValue *value;

    if (!style) {
        return CssColorNone();
    }
    value = CssComputedStyleComputedByName(style,
                                                     CssSliceFromText(name));
    if (!value || value->kind != RoboCssComputedValueColor) {
        return CssColorNone();
    }
    return value->color;
}

/**
 * Resolve every `currentColor` in @p out against @p parentComputed.
 *
 * The order is the rule: `color` first, against the parent, so that the element
 * has a final colour; then everything else, against that. Nothing here can read
 * a value it has not settled, which is what makes two properties that both say
 * `currentColor` agree no matter which was declared first.
 */
static void ComputedResolveCurrentColors(CssComputedStyle *out,
                                            const CssComputedStyle *parentComputed)
{
    CssComputedEntry *colorEntry;
    CssColor elementColor;
    size_t index;

    colorEntry = ComputedFindName(out, CssSliceFromText(COMPUTED_COLOR_PROPERTY));
    ComputedResolveEntryColor(colorEntry, ComputedColorOf(parentComputed,
                                                               COMPUTED_COLOR_PROPERTY));
    elementColor = ComputedColorOf(out, COMPUTED_COLOR_PROPERTY);
    if (!CssColorIsResolved(elementColor)) {
        return;
    }
    for (index = 0u; index < out->count; index++) {
        CssComputedEntry *entry = &out->entries[index];

        if (entry != colorEntry) {
            ComputedResolveEntryColor(entry, elementColor);
        }
    }
}

/**
 * Compute the entries of @p resolved into @p out, one level of a tree.
 *
 * The shape of the pass is one entry in, one entry out, and the three questions
 * asked of each entry are asked in the order they matter:
 *
 *   1. is there a value at all?      an entry 9.7 carried as "the default is not
 *                                    known to this build" has none, and the
 *                                    computed style says so rather than
 *                                    inventing one;
 *   2. did the parent supply it?     then take the parent's *computed* value,
 *                                    which is what CSS inheritance means and
 *                                    what keeps a child's inherited value
 *                                    identical to the parent's own computed one
 *                                    instead of merely equal-looking;
 *   3. otherwise compute it here,    through the property's declared value shape
 *      from the value in hand.       (RoboCSSPropertyRegistry.h) or, for a
 *                                    property nothing classifies, by preserving
 *                                    the tokens.
 *
 * The specified value travels with the computed one, because the container is
 * the place both halves of "specified vs computed" live (RoboCSSComputed.h) and
 * a caller holding one should be able to see the other without re-resolving.
 */
Result CssStyleContextComputeResolvedStyle(
    const CssStyleContext *context,
    const CssInheritedStyle *resolved,
    const CssComputedStyle *parentComputed,
    CssComputedStyle *out,
    CssError *outError,
    CssDiag *diag)
{
    const CssPropertyRegistry *registry;
    size_t index;

    if (!context || !resolved || !out) {
        CssErrorSet(outError, RoboCssErrorKindNone, RoboInvalidArgument,
                           NULL, "context, resolved style or output is NULL");
        return RoboCssDiag(diag, RoboCssDomainComputed, RoboInvalidArgument,
                             "context, resolved style or output is NULL");
    }
    CssErrorInit(outError);
    registry = CssStyleContextPropertyRegistry(context);

    for (index = 0u; index < resolved->count; index++) {
        const CssInheritedProperty *entry = &resolved->items[index];
        CssSlice name = CssInheritedPropertyName(entry);
        const CssPropertyDefinition *definition;
        CssPropertyComputation computation =
            RoboCssPropertyComputationPreserve;
        CssValue specified = RoboCssCleared;
        CssComputedValue computed = RoboCssCleared;
        uint32_t flags;
        int custom;
        int haveSpecified = 0;
        int haveComputed = 0;
        Result rc;

        if (CssSliceIsAbsent(name)) {
            continue;
        }
        custom = CssIsCustomPropertyName(name);
        /* A custom property never has a definition: it is an opaque token
         * sequence by definition, and the PRESERVE it gets is not a fallback but
         * the correct computation for it. */
        definition = custom ? NULL : CssPropertyRegistryFindSlice(registry, name);
        if (definition) {
            computation = definition->computation;
        }
        flags = ComputedFlagsOfSource(entry->source);
        if (custom) {
            flags |= RoboCssComputedFlagCustom;
        }

        CssComputedValueInit(&computed);
        if (entry->value.kind != RoboCssValueKindNone) {
            rc = CssValueCopy(&specified, &entry->value, diag);
            if (rc != RoboOk) {
                goto fail;
            }
            haveSpecified = 1;
        }

        if (entry->unresolved) {
            /* 9.7's typed "this build does not know the property's default".
             * There is nothing to compute, and the flag says why. */
            flags |= RoboCssComputedFlagUnresolved;
        } else if (entry->source == RoboCssInheritedSourceDeferred) {
            /* The `revert` keyword, carried. Preserved rather than dropped, so
             * the author's declaration survives to whoever can obey it. */
            rc = CssComputedValueMakePreserved(&computed, &specified, diag);
            if (rc != RoboOk) {
                goto fail;
            }
            computed.unresolved = 1;
            haveComputed = computed.kind != RoboCssComputedValueNone;
        } else if (entry->source == RoboCssInheritedSourceParent && parentComputed) {
            const CssComputedValue *inherited =
                CssComputedStyleComputedByName(parentComputed, name);

            if (inherited) {
                rc = CssComputedValueCopy(&computed, inherited, diag);
                if (rc != RoboOk) {
                    goto fail;
                }
                /* The parent's value may itself have been the property's
                 * default; that fact is true of this value too. */
                flags |= (CssComputedStyleFlags(parentComputed, name) &
                          RoboCssComputedFlagDefault);
                haveComputed = 1;
            }
        }

        if (!haveComputed && !entry->unresolved) {
            if (!haveSpecified) {
                /* Nothing was specified and nothing was inherited: the entry
                 * exists (9.7 made it so) but has no value, which is a fact the
                 * computed style records rather than guesses past. */
                flags |= RoboCssComputedFlagUnresolved;
            } else {
                rc = CssComputedValueCompute(computation, &specified,
                                                     &computed, diag);
                if (rc != RoboOk) {
                    goto fail;
                }
                if (computed.kind == RoboCssComputedValueInvalid) {
                    flags |= RoboCssComputedFlagInvalid;
                }
                if (computed.unresolved) {
                    flags |= RoboCssComputedFlagUnresolved;
                }
                haveComputed = computed.kind != RoboCssComputedValueNone;
            }
        }

        rc = CssComputedStyleSetEntry(
            out, entry->property, name, flags,
            haveSpecified ? &specified : NULL,
            haveComputed ? &computed : NULL, diag);
        if (rc != RoboOk) {
            CssValueDispose(&specified);
            CssComputedValueDispose(&computed);
            goto fail;
        }
        /* set_entry moved whatever it took; anything left is ours to free. */
        CssValueDispose(&specified);
        CssComputedValueDispose(&computed);
        continue;

    fail:
        CssValueDispose(&specified);
        CssComputedValueDispose(&computed);
        CssComputedStyleClear(out);
        CssErrorSet(outError, RoboCssErrorKindOutOfMemory, rc, NULL,
                           "the computed style could not be built");
        return rc;
    }
    /* Every value is computed, so the one thing a value cannot know about itself
     * can now be settled: `currentColor` is the element's `color`, and on `color`
     * it is the parent's — see the note above. */
    ComputedResolveCurrentColors(out, parentComputed);
    return RoboOk;
}

Result CssStyleContextQueryComputedValue(
    const CssStyleContext *context,
    const CssComputedStyle *style,
    CssSlice name,
    CssComputedValue *out,
    int *outFromInitial,
    CssDiag *diag)
{
    const CssComputedValue *present;
    const CssPropertyDefinition *definition;
    const CssValue *initial;
    Result rc;

    if (!context || !out || CssSliceIsAbsent(name)) {
        return RoboCssDiag(diag, RoboCssDomainComputed, RoboInvalidArgument,
                             "context, name and output are required");
    }
    if (outFromInitial) {
        *outFromInitial = 0;
    }
    present = style ? CssComputedStyleComputedByName(style, name) : NULL;
    if (present) {
        return CssComputedValueCopy(out, present, diag);
    }
    /* Nothing declared it here. For a property this build has no initial value
     * for — including every custom property, which CSS gives no initial value
     * at all — the honest answer is that there is none. */
    definition = CssPropertyRegistryFindSlice(
        CssStyleContextPropertyRegistry(context), name);
    if (!definition || CssIsCustomPropertyName(name)) {
        return RoboCssDiag(diag, RoboCssDomainComputed, RoboNotFound,
                             "no computed value and no initial value for the property");
    }
    initial = CssPropertyDefinitionInitialValue(definition);
    if (!initial) {
        return RoboCssDiag(diag, RoboCssDomainComputed, RoboNotFound,
                             "this build has no initial value for the property");
    }
    if (outFromInitial) {
        *outFromInitial = 1;
    }
    rc = CssComputedValueCompute(definition->computation, initial, out, diag);
    if (rc != RoboOk) {
        return rc;
    }
    /* One initial value in the table is a *reference* rather than a value: every
     * `border-*-color` is `currentcolor`, so a border nothing declared is the
     * element's own colour. Nothing else can reach this: `background-color` is
     * `transparent`, which is a colour and not a reference. Resolving it here, the
     * way the pass above resolves what a style *holds*, is what keeps the two doors
     * answering one question the same way — an element that declared no border
     * colour and one whose shorthand reset it to the initial value have the same
     * computed colour, because they do. */
    if (out->kind == RoboCssComputedValueColor &&
        CssColorIsCurrent(out->color)) {
        CssColor resolved;

        if (CssColorResolveCurrent(out->color,
                                          ComputedColorOf(style, COMPUTED_COLOR_PROPERTY),
                                          &resolved)) {
            out->color = resolved;
            out->unresolved = 0;
        }
    }
    return RoboOk;
}
