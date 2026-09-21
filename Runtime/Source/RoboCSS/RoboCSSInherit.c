/*
 * RoboCSS inheritance (Phase 9.7) — see RoboCSSInherit.h.
 *
 * The whole file is arranged around one rule: a resolved style is a *pure
 * function* of (the element's cascade, the parent's resolved style, the
 * property registry). Nothing is cached between calls, no global "current
 * style" exists, and the parent arrives as an argument. That is what makes the
 * module testable one element at a time and what makes its invalidation story
 * "re-resolve" rather than "remember to invalidate".
 *
 * Four sets meet in RoboCssInheritResolve, and the order they are walked in
 * is the order they are reported in:
 *
 *   1. the properties the element's own cascade declares — one entry each,
 *      taking the *winner* through the cascade's own resolver rather than
 *      re-deciding precedence here;
 *   1b. the box model's longhands, distributed out of their shorthands (9.11);
 *   2. the properties that inherit by default and that the parent carries;
 *   3. the properties that inherit by default that neither of the above
 *      supplied, resolved to their initial value.
 *
 * Set 3 is what a root element gets, and what a caller who passes no parent is
 * asking for. It is deliberately *only* the inherited-by-default properties:
 * materialising `width: auto` on every element because nobody declared it would
 * put a value in a resolution that no input produced, and 9.8 can reach the
 * registry's initial value for that.
 *
 * Set 1b sits where it does because a box-model longhand is a *declaration* — it
 * is resolved exactly like one, keywords and all — and because one shorthand and
 * one longhand can both speak for the same edge, so the two have to be weighed
 * against each other rather than materialised independently. Settling that here,
 * before "what does the parent carry" is asked, keeps a resolved style looking
 * the way every other resolved style looks: one entry per property, each one
 * with its own provenance.
 */

#include "RoboCSS/RoboCSSInherit.h"

#include "RoboCSS/RoboCSSBorder.h"
#include "RoboCSS/RoboCSSBox.h"
#include "RoboCSS/RoboCSSError.h"
#include "RoboCSSInternal.h"
#include "RoboBase/RoboLog.h"
#include "RoboMem/RoboMem.h"

#include <string.h>

/* ------------------------------------------------------------------ */
/* Keywords                                                            */
/* ------------------------------------------------------------------ */

int CssWideKeywordIsKnown(CssWideKeyword keyword)
{
    return keyword >= RoboCssWideKeywordNone &&
           keyword < (CssWideKeyword)RoboCssWideKeywordCount;
}

const char *CssWideKeywordName(CssWideKeyword keyword)
{
    switch (keyword) {
    case RoboCssWideKeywordNone:
        return "none";
    case RoboCssWideKeywordInherit:
        return "inherit";
    case RoboCssWideKeywordInitial:
        return "initial";
    case RoboCssWideKeywordUnset:
        return "unset";
    case RoboCssWideKeywordRevert:
        return "revert";
    default:
        return "unknown";
    }
}

/**
 * The single identifier a value is, or NULL when it is anything else.
 *
 * A declaration's value is a LIST (the grammar builds every one that way), so
 * the shape to accept is "a list of exactly one token". A hand-built value may
 * be a bare TOKEN, so that shape is accepted too — and everything else, from an
 * empty list to a function to two idents, is refused. Refusing is the important
 * half: `inherit inherit` is not the keyword and neither is `calc(inherit)`,
 * and reading either as one would resolve a property from a value nobody wrote.
 */
static const CssToken *KeywordIdentifier(const CssValue *value)
{
    const CssValue *leaf = value;

    if (!value) {
        return NULL;
    }
    if (value->kind == RoboCssValueKindList) {
        if (value->children.count != 1u) {
            return NULL;
        }
        leaf = &value->children.items[0];
    }
    if (leaf->kind != RoboCssValueKindToken ||
        leaf->token.type != RoboCssTokenIdent) {
        return NULL;
    }
    return &leaf->token;
}

CssWideKeyword CssWideKeywordOf(const CssValue *value)
{
    const CssToken *identifier = KeywordIdentifier(value);

    if (!identifier) {
        return RoboCssWideKeywordNone;
    }
    /* Decoded, not raw: an escape changes which bytes spell a name, never which
     * name it is, so `\69 nherit` is the keyword. */
    if (CssTokenValueEqualsAsciiCi(identifier, "inherit")) {
        return RoboCssWideKeywordInherit;
    }
    if (CssTokenValueEqualsAsciiCi(identifier, "initial")) {
        return RoboCssWideKeywordInitial;
    }
    if (CssTokenValueEqualsAsciiCi(identifier, "unset")) {
        return RoboCssWideKeywordUnset;
    }
    if (CssTokenValueEqualsAsciiCi(identifier, "revert")) {
        return RoboCssWideKeywordRevert;
    }
    return RoboCssWideKeywordNone;
}

/* ------------------------------------------------------------------ */
/* Sources                                                             */
/* ------------------------------------------------------------------ */

int CssInheritedSourceIsKnown(CssInheritedSource source)
{
    return source >= RoboCssInheritedSourceNone &&
           source < (CssInheritedSource)RoboCssInheritedSourceCount;
}

const char *CssInheritedSourceName(CssInheritedSource source)
{
    switch (source) {
    case RoboCssInheritedSourceNone:
        return "none";
    case RoboCssInheritedSourceDeclared:
        return "declared";
    case RoboCssInheritedSourceParent:
        return "parent";
    case RoboCssInheritedSourceDefault:
        return "default";
    case RoboCssInheritedSourceDeferred:
        return "deferred";
    default:
        return "unknown";
    }
}

/* ------------------------------------------------------------------ */
/* Entries                                                             */
/* ------------------------------------------------------------------ */

CssSlice CssInheritedPropertyName(
    const CssInheritedProperty *property)
{
    if (!property || !property->name) {
        return CssSliceEmpty();
    }
    return CssSliceFromBytes((const uint8_t *)property->name,
                                     property->nameLength);
}

const CssValue *CssInheritedPropertyValue(
    const CssInheritedProperty *property)
{
    if (!property || property->value.kind == RoboCssValueKindNone) {
        return NULL;
    }
    return &property->value;
}

/**
 * Does @p entry name the property @p name is looking for?
 *
 * The same rule the registry uses, and it has to be: a lookup that disagreed
 * with registration about whether `--X` and `--x` are one property would find
 * the wrong entry, or none.
 */
static int InheritedEntryMatches(const CssInheritedProperty *entry,
                                   CssSlice name)
{
    CssSlice stored = CssInheritedPropertyName(entry);

    if (CssIsCustomPropertyName(name) ||
        CssIsCustomPropertyName(stored)) {
        return CssSliceEquals(stored, name);
    }
    return CssSliceEqualsCi(stored, name);
}

/* ------------------------------------------------------------------ */
/* The resolved style                                                  */
/* ------------------------------------------------------------------ */

void CssInheritedStyleInit(CssInheritedStyle *style)
{
    if (!style) {
        return;
    }
    style->items = NULL;
    style->count = 0u;
    style->capacity = 0u;
    style->element = RoboCssElementRefNone;
    style->hasParent = 0;
    style->declaredCount = 0u;
    style->inheritedCount = 0u;
    style->defaultCount = 0u;
    style->deferredCount = 0u;
    style->unresolvedCount = 0u;
    style->unsupported = 0;
}

void CssInheritedStyleDispose(CssInheritedStyle *style)
{
    size_t index;

    if (!style) {
        return;
    }
    for (index = 0u; index < style->count; index++) {
        CssInheritedProperty *entry = &style->items[index];

        CssValueDispose(&entry->value);
        RoboFree(entry->name);
    }
    RoboFree(style->items);
    CssInheritedStyleInit(style);
}

size_t CssInheritedStyleCount(const CssInheritedStyle *style)
{
    return style ? style->count : 0u;
}

const CssInheritedProperty *CssInheritedStyleAt(
    const CssInheritedStyle *style,
    size_t index)
{
    if (!style || index >= style->count) {
        return NULL;
    }
    return &style->items[index];
}

const CssInheritedProperty *CssInheritedStyleFind(
    const CssInheritedStyle *style,
    CssSlice name)
{
    size_t index;

    if (!style || CssSliceIsAbsent(name)) {
        return NULL;
    }
    for (index = 0u; index < style->count; index++) {
        if (InheritedEntryMatches(&style->items[index], name)) {
            return &style->items[index];
        }
    }
    return NULL;
}

const CssInheritedProperty *CssInheritedStyleFindProperty(
    const CssInheritedStyle *style,
    CssPropertyId property)
{
    size_t index;

    if (!style || property == RoboCssPropertyIdNone) {
        return NULL;
    }
    for (index = 0u; index < style->count; index++) {
        if (style->items[index].property == property) {
            return &style->items[index];
        }
    }
    return NULL;
}

int CssInheritedStyleHas(const CssInheritedStyle *style,
                                 CssSlice name)
{
    return CssInheritedStyleFind(style, name) != NULL;
}

int CssInheritedStyleIsComplete(const CssInheritedStyle *style)
{
    return style && !style->unsupported;
}

CssElementRef CssInheritedStyleElement(
    const CssInheritedStyle *style)
{
    return style ? style->element : RoboCssElementRefNone;
}

int CssInheritedStyleHasParent(const CssInheritedStyle *style)
{
    return style ? style->hasParent : 0;
}

size_t CssInheritedStyleShorthandsUnexpanded(
    const CssInheritedStyle *style)
{
    return style ? style->shorthandsUnexpanded : 0u;
}

/* ------------------------------------------------------------------ */
/* Resolution                                                          */
/* ------------------------------------------------------------------ */

/**
 * One entry as it is built. The name is copied from whatever named the
 * property, because a resolved style must not borrow its identity from a sheet
 * that may be destroyed while the style lives.
 */
static int InheritAppend(CssInheritedStyle *style,
                          CssPropertyId property,
                          CssSlice name,
                          CssInheritedSource source,
                          CssWideKeyword keyword,
                          const CssValue *value)
{
    CssInheritedProperty *slot;
    char *nameCopy;

    /* The name first, so a failure leaves the style exactly as it was. */
    nameCopy = (char *)Malloc(name.length + 1u);
    if (!nameCopy) {
        return 0;
    }
    if (name.length > 0u) {
        memcpy(nameCopy, name.bytes, name.length);
    }
    nameCopy[name.length] = '\0';

    if (!CssGrow((void **)&style->items, &style->capacity, style->count,
                       sizeof(CssInheritedProperty))) {
        RoboFree(nameCopy);
        return 0;
    }
    slot = &style->items[style->count];
    memset(slot, 0, sizeof(*slot));
    CssValueInit(&slot->value);
    slot->property = property;
    slot->name = nameCopy;
    slot->nameLength = name.length;
    slot->source = source;
    slot->keyword = keyword;

    if (value && value->kind != RoboCssValueKindNone) {
        if (CssValueCopy(&slot->value, value, NULL) != RoboOk) {
            RoboFree(nameCopy);
            return 0;
        }
    } else {
        /* No value to carry: the typed "I do not know this property's default"
         * state, which is a different fact from having no entry. */
        slot->unresolved = 1;
        style->unresolvedCount++;
        style->unsupported = 1;
    }
    style->count++;
    return 1;
}

/** Count one entry into the tally its source belongs to. */
static void InheritTally(CssInheritedStyle *style,
                          CssInheritedSource source)
{
    switch (source) {
    case RoboCssInheritedSourceDeclared:
        style->declaredCount++;
        break;
    case RoboCssInheritedSourceParent:
        style->inheritedCount++;
        break;
    case RoboCssInheritedSourceDefault:
        style->defaultCount++;
        break;
    case RoboCssInheritedSourceDeferred:
        style->deferredCount++;
        break;
    default:
        break;
    }
}

/**
 * Does @p name's property inherit by default?
 *
 * One decision, in one place, with the precedence stated once:
 *
 *   1. a custom property inherits — CSS defines them as inherited, always;
 *   2. otherwise the registry's classification, when it has one;
 *   3. otherwise the caller's property table, whose RoboCssPropertyFlagInherited
 *      bit exists for exactly this (its own header has said "9.7 reads this"
 *      since 9.1);
 *   4. otherwise it does not inherit.
 *
 * Step 4 is a decision rather than a silence. CSS drops a declaration for a
 * property it does not recognise, so a property nothing here recognises is not
 * one whose value should propagate down a tree: inheriting it would spread a
 * value no phase can interpret into every descendant.
 */
static int InheritClassify(const CssStyleContext *context,
                            CssSlice name)
{
    const CssPropertyRegistry *registry =
        CssStyleContextPropertyRegistry(context);
    const CssPropertyDefinition *definition;
    const CssPropertyTable *table;

    if (CssIsCustomPropertyName(name)) {
        return 1;
    }
    definition = CssPropertyRegistryFindSlice(registry, name);
    if (definition) {
        return CssPropertyDefinitionInherited(definition);
    }
    table = CssStyleContextPropertyTable(context);
    if (table) {
        CssPropertyInfo info = RoboCssCleared;

        if (CssPropertyTableInfo(table,
                                         CssPropertyTableLookupSlice(table, name),
                                         &info, NULL) == RoboOk) {
            return CssPropertyFlagsInherited(info.flags);
        }
    }
    return 0;
}

/** The registry's initial value for @p name, or NULL when there is none. */
static const CssValue *InheritDefaultValue(
    const CssStyleContext *context,
    CssSlice name)
{
    const CssPropertyDefinition *definition =
        CssPropertyRegistryFindSlice(
            CssStyleContextPropertyRegistry(context), name);

    /* A custom property has no initial value in CSS — an unset one is invalid,
     * not `initial` — so it is never defaulted, whatever a registry might say. */
    if (!definition || CssIsCustomPropertyName(name)) {
        return NULL;
    }
    return CssPropertyDefinitionInitialValue(definition);
}

/**
 * The entry the parent supplies for @p name, or NULL.
 *
 * A parent entry that is itself unresolved or deferred is *not* a value, so it
 * is not inherited: carrying "I do not know" down a tree would turn one
 * unknown into every descendant's unknown, and the child can reach the same
 * state on its own by falling back to the default. The one exception is a
 * `revert` the parent carried: it is still a keyword the child would inherit as
 * a *value*, and dropping it would silently lose the author's declaration.
 */
static const CssInheritedProperty *InheritParentEntry(
    const CssInheritedStyle *parent,
    CssSlice name)
{
    const CssInheritedProperty *entry;

    if (!parent) {
        return NULL;
    }
    entry = CssInheritedStyleFind(parent, name);
    if (!entry || entry->unresolved) {
        return NULL;
    }
    return entry;
}

/**
 * Resolve one property from a value the element declared, obeying the CSS-wide
 * keyword that value may carry.
 *
 * Precedence is already decided when this is called — the winner is whatever the
 * cascade chose — so nothing here re-decides it: the function only obeys the
 * keyword or copies the value.
 *
 * It takes a *value* rather than the candidate that carried it, because 9.11
 * gave it a second caller: a longhand distributed out of a shorthand is declared
 * like any other, and it has to go through exactly this keyword handling. `margin:
 * inherit` and `margin-left: inherit` mean the same thing to the four edges, and a
 * second copy of the keyword logic is how the two would come to disagree.
 */
static int InheritValue(CssInheritedStyle *out,
                         const CssStyleContext *context,
                         CssPropertyId property,
                         CssSlice name,
                         const CssValue *value,
                         const CssInheritedStyle *parent,
                         int inheritedByDefault)
{
    CssWideKeyword keyword = CssWideKeywordOf(value);
    const CssInheritedProperty *fromParent;

    if (keyword == RoboCssWideKeywordRevert) {
        /* Recognised, and deliberately not obeyed: see the header. The keyword
         * is carried as the value so nothing is lost, and the entry is counted
         * and marked deferred so nothing reads it as an answer. */
        if (!InheritAppend(out, property, name,
                            RoboCssInheritedSourceDeferred, keyword, value)) {
            return 0;
        }
        out->unsupported = 1;
        InheritTally(out, RoboCssInheritedSourceDeferred);
        return 1;
    }

    if (keyword == RoboCssWideKeywordInherit ||
        (keyword == RoboCssWideKeywordUnset && inheritedByDefault)) {
        fromParent = InheritParentEntry(parent, name);
        if (fromParent &&
            InheritAppend(out, property, name,
                           RoboCssInheritedSourceParent, keyword,
                           &fromParent->value)) {
            InheritTally(out, RoboCssInheritedSourceParent);
            return 1;
        }
        /* `inherit` with no parent value is the parent's *computed* value, and
         * an undeclared non-inherited property's computed value is its initial
         * one — so the default is the right answer, not a failure. */
        if (!InheritAppend(out, property, name,
                            RoboCssInheritedSourceDefault, keyword,
                            InheritDefaultValue(context, name))) {
            return 0;
        }
        InheritTally(out, RoboCssInheritedSourceDefault);
        return 1;
    }

    if (keyword == RoboCssWideKeywordInitial ||
        (keyword == RoboCssWideKeywordUnset && !inheritedByDefault)) {
        if (!InheritAppend(out, property, name,
                            RoboCssInheritedSourceDefault, keyword,
                            InheritDefaultValue(context, name))) {
            return 0;
        }
        InheritTally(out, RoboCssInheritedSourceDefault);
        return 1;
    }

    if (!InheritAppend(out, property, name,
                        RoboCssInheritedSourceDeclared,
                        RoboCssWideKeywordNone, value)) {
        return 0;
    }
    InheritTally(out, RoboCssInheritedSourceDeclared);
    return 1;
}

/**
 * Resolve one property whose winning declaration the cascade already chose.
 *
 * The one-candidate spelling of the above, kept because that is how every
 * property that is not a box-model longhand arrives.
 */
static int InheritDeclared(CssInheritedStyle *out,
                            const CssStyleContext *context,
                            const CssCascadeCandidate *declared,
                            const CssInheritedStyle *parent,
                            CssSlice name,
                            int inheritedByDefault)
{
    return InheritValue(out, context, declared->property, name,
                         CssDeclarationValue(declared->declaration), parent,
                         inheritedByDefault);
}

/**
 * Carry @p entry from the parent, because @p name inherits by default and the
 * element did not declare it.
 */
static int InheritFromParent(CssInheritedStyle *out,
                               CssPropertyId property,
                               CssSlice name,
                               const CssInheritedProperty *entry)
{
    CssValue value = RoboCssCleared;
    int appended;

    /* A deferred keyword the parent carried is carried down as one, so the
     * author's declaration is never silently lost on the way; everything else
     * is copied as a value. */
    if (entry->source == RoboCssInheritedSourceDeferred) {
        appended = InheritAppend(out, property, name,
                                  RoboCssInheritedSourceDeferred,
                                  entry->keyword, &entry->value);
        if (appended) {
            out->unsupported = 1;
            InheritTally(out, RoboCssInheritedSourceDeferred);
        }
        return appended;
    }

    value = entry->value;
    appended = InheritAppend(out, property, name, RoboCssInheritedSourceParent,
                              RoboCssWideKeywordNone, &value);
    if (appended) {
        InheritTally(out, RoboCssInheritedSourceParent);
    }
    return appended;
}

/**
 * Distribute the box model's shorthands over their longhands (9.11).
 *
 * A shorthand is one declaration that speaks for four properties, and an edge
 * can be spoken for twice:
 *
 *   .box { margin: 10px; margin-left: 20px; }   the longhand is later, so it wins
 *   .box { margin-left: 20px; margin: 10px; }   the shorthand is later, so it wins
 *
 * Both are the *same* question the cascade already answers for one property —
 * level, then specificity, then document order — asked across two property names
 * instead of within one, which is why it is asked of the cascade
 * (`CssCascadeCandidateCompare`) rather than answered here. Nothing in
 * this function weighs a level or compares a specificity.
 *
 * The three outcomes per edge, in the order they are decided:
 *
 *   the longhand's own declaration wins   materialise it, exactly as the
 *                                         declared pass would have;
 *   the shorthand wins, carrying a        obey the keyword — `inherit` on a
 *   CSS-wide keyword                      shorthand means the same four times;
 *   the shorthand wins, with a value      copy that side's component out of it.
 *
 * A shorthand whose value cannot be distributed — five components, or a `var()`
 * that substitution would expand into four — is counted and reported rather than
 * guessed at; the author's text stays on the shorthand's own entry either way.
 *
 * The longhands of a shorthand are skipped by the declared pass and materialised
 * here and *only* here, which is what keeps an edge from getting two entries: one
 * from its own declaration and one from the shorthand it lost to.
 */
static int InheritExpandBoxShorthands(CssInheritedStyle *out,
                                        const CssStyleContext *context,
                                        const CssCascade *cascade,
                                        const CssInheritedStyle *parent)
{
    const CssPropertyTable *table;
    size_t rowIndex;

    if (!cascade) {
        return 1;
    }
    table = CssStyleContextPropertyTable(context);
    for (rowIndex = 0u; rowIndex < CssBoxShorthandCount(); rowIndex++) {
        const CssBoxShorthand *shorthand =
            CssBoxShorthandAt(rowIndex);
        const CssCascadeCandidate *shorthandWinner = NULL;

        if (CssBorderDistributesShorthand(CssSliceFromText(
                shorthand->name))) {
            /* The three border rows are table rows here and the border family's
             * to distribute there (9.15): a border longhand can be set by a side
             * shorthand and by `border` as well, and one longhand cannot be
             * materialised by two passes. The table is the `margin`/`padding`
             * half's and theirs together because the twelve longhand names are a
             * vocabulary a single reader should be able to walk (RoboCSSBox.h). */
            continue;
        }
        const CssValue *shorthandValue = NULL;
        CssWideKeyword shorthandKeyword = RoboCssWideKeywordNone;
        size_t componentCount = 0u;
        int expandable = 0;
        size_t sideIndex;

        (void)CssCascadeResolveName(
            cascade, CssSliceFromText(shorthand->name), &shorthandWinner);
        if (shorthandWinner) {
            shorthandValue =
                CssDeclarationValue(shorthandWinner->declaration);
            shorthandKeyword = CssWideKeywordOf(shorthandValue);
            if (shorthandKeyword == RoboCssWideKeywordNone) {
                expandable = CssBoxIsExpandable(shorthandValue,
                                                        &componentCount);
                if (!expandable) {
                    /* Counted once for the shorthand rather than once per side,
                     * because it is one declaration that went undistributed. */
                    out->shorthandsUnexpanded++;
                    out->unsupported = 1;
                }
            }
        }

        for (sideIndex = 0u; sideIndex < (size_t)RoboCssBoxSideCount; sideIndex++) {
            CssBoxSide side = (CssBoxSide)sideIndex;
            const char *longhandName = CssBoxShorthandLonghand(shorthand, side);
            CssSlice name = CssSliceFromText(longhandName);
            const CssCascadeCandidate *longhandWinner = NULL;
            CssPropertyId property;
            int inheritedByDefault;
            int longhandWins;

            (void)CssCascadeResolveName(cascade, name, &longhandWinner);
            longhandWins = longhandWinner != NULL &&
                            (!shorthandWinner ||
                             CssCascadeCandidateCompare(cascade,
                                                                shorthandWinner,
                                                                longhandWinner) > 0);
            if (!longhandWins && !shorthandWinner) {
                /* Neither spoke for this edge: it has no entry, and it is not
                 * supposed to — see set 3 in the header comment. */
                continue;
            }
            property = table ? CssPropertyTableLookupSlice(table, name)
                             : RoboCssPropertyIdNone;
            inheritedByDefault = InheritClassify(context, name);

            if (longhandWins) {
                if (!InheritValue(out, context, longhandWinner->property, name,
                                   CssDeclarationValue(longhandWinner->declaration),
                                   parent, inheritedByDefault)) {
                    return 0;
                }
                continue;
            }
            if (shorthandKeyword != RoboCssWideKeywordNone) {
                /* The keyword itself, distributed: `inherit` means the parent's
                 * value for the edge, which is the same sentence `InheritValue`
                 * already knows how to obey. */
                if (!InheritValue(out, context, property, name, shorthandValue,
                                   parent, inheritedByDefault)) {
                    return 0;
                }
                continue;
            }
            if (expandable) {
                CssValue component = RoboCssCleared;

                if (CssBoxExpandSide(shorthandValue, componentCount, side,
                                             &component, NULL) != RoboOk) {
                    return 0;
                }
                if (!InheritValue(out, context, property, name, &component, parent,
                                   inheritedByDefault)) {
                    CssValueDispose(&component);
                    return 0;
                }
                CssValueDispose(&component);
            }
        }
    }
    return 1;
}

/**
 * Distribute the `background` shorthand over its six longhands (9.14).
 *
 * A second pass rather than rows in the box model's, because the *mechanism* is
 * different: the box table maps components to sides by position, while
 * `background`'s six longhands are six different shapes and a component goes to
 * whichever one its *kind* names. The two share the part that must not be
 * duplicated — which of a shorthand and a longhand wins is the cascade's answer,
 * asked here through `CssCascadeCandidateCompare` exactly as the box pass
 * asks it — and nothing here weighs a level or compares a specificity.
 *
 * The third outcome differs from the box pass's, and it is the whole reason this
 * is not a table row. A box edge the author did not write has *no* value, because
 * `margin: 10px` says nothing about a fifth edge. `background: red` does say
 * something about `background-image`: the shorthand stands for every one of the
 * six, so a longhand it did not mention is **reset to its initial value**. That is
 * why an absent part materialises the registry's initial value rather than being
 * skipped — and it is what makes `background: red` on an element an earlier rule
 * gave `background-image: url(a.png)` clear the image, which is the behaviour an
 * author relies on when they write the shorthand to mean "just this".
 */
static int InheritExpandBackgroundShorthand(CssInheritedStyle *out,
                                               const CssStyleContext *context,
                                               const CssCascade *cascade,
                                               const CssInheritedStyle *parent)
{
    const CssPropertyTable *table;
    const CssCascadeCandidate *shorthandWinner = NULL;
    const CssValue *shorthandValue = NULL;
    CssWideKeyword shorthandKeyword = RoboCssWideKeywordNone;
    CssValue parts[RoboCssBackgroundLonghandCount];
    int expandable = 0;
    int failed = 0;
    size_t index;

    for (index = 0u; index < RoboCssBackgroundLonghandCount; index++) {
        CssValueInit(&parts[index]);
    }
    if (!cascade) {
        return 1;
    }
    table = CssStyleContextPropertyTable(context);
    (void)CssCascadeResolveName(
        cascade, CssSliceFromText(CssBackgroundShorthandName()),
        &shorthandWinner);
    if (shorthandWinner) {
        shorthandValue = CssDeclarationValue(shorthandWinner->declaration);
        shorthandKeyword = CssWideKeywordOf(shorthandValue);
        if (shorthandKeyword == RoboCssWideKeywordNone) {
            /* Distributed once for the whole shorthand rather than once per
             * longhand, because the parts are shared by all six. */
            expandable = CssBackgroundExpand(shorthandValue, parts, NULL) ==
                         RoboOk;
            if (!expandable) {
                out->shorthandsUnexpanded++;
                out->unsupported = 1;
            }
        }
    }

    for (index = 0u; index < RoboCssBackgroundLonghandCount; index++) {
        CssSlice name = CssSliceFromText(
            CssBackgroundLonghandName(index));
        const CssCascadeCandidate *longhandWinner = NULL;
        CssPropertyId property;
        int inheritedByDefault;
        int longhandWins;

        (void)CssCascadeResolveName(cascade, name, &longhandWinner);
        longhandWins = longhandWinner != NULL &&
                        (!shorthandWinner ||
                         CssCascadeCandidateCompare(cascade, shorthandWinner,
                                                            longhandWinner) > 0);
        if (!longhandWins && !shorthandWinner) {
            /* Neither spoke for this longhand: it has no entry and is not
             * supposed to have one. */
            continue;
        }
        property = table ? CssPropertyTableLookupSlice(table, name)
                         : RoboCssPropertyIdNone;
        inheritedByDefault = InheritClassify(context, name);

        if (longhandWins) {
            if (!InheritValue(out, context, longhandWinner->property, name,
                               CssDeclarationValue(longhandWinner->declaration),
                               parent, inheritedByDefault)) {
                failed = 1;
                break;
            }
            continue;
        }
        if (shorthandKeyword != RoboCssWideKeywordNone) {
            /* The keyword itself, distributed: `background: inherit` means the
             * same sentence six times, which is what `InheritValue` already
             * knows how to obey. */
            if (!InheritValue(out, context, property, name, shorthandValue, parent,
                               inheritedByDefault)) {
                failed = 1;
                break;
            }
            continue;
        }
        if (!expandable) {
            /* The shorthand spoke and could not be distributed. The author's text
             * stays on the shorthand's own entry and nothing is guessed at here. */
            continue;
        }
        if (parts[index].kind == RoboCssValueKindNone) {
            /* Present in the shorthand and absent from its components: the longhand
             * is reset to the property's own initial value, and the registry is the
             * one place that value is written down. */
            const CssValue *initial = InheritDefaultValue(context, name);

            if (!initial) {
                continue;
            }
            if (!InheritValue(out, context, property, name, initial, parent,
                               inheritedByDefault)) {
                failed = 1;
                break;
            }
            continue;
        }
        if (!InheritValue(out, context, property, name, &parts[index], parent,
                           inheritedByDefault)) {
            failed = 1;
            break;
        }
    }

    for (index = 0u; index < RoboCssBackgroundLonghandCount; index++) {
        CssValueDispose(&parts[index]);
    }
    return failed ? 0 : 1;
}

/* ------------------------------------------------------------------ */
/* The border family (9.15)                                            */
/* ------------------------------------------------------------------ */

/**
 * One composite border shorthand, resolved and (if it can be) distributed.
 *
 * A record rather than five locals per side because a *composite* shorthand's
 * distribution is the same for all the longhands it covers: `border: 1px solid
 * red` splits into the same three parts whichever of the twelve longhands is
 * being materialised, so the split happens once and the loop below reads it.
 */
typedef struct inheritBorderComposite {
    const CssCascadeCandidate *candidate;
    const CssValue *value;
    CssWideKeyword keyword;
    CssValue parts[RoboCssBorderKindCount];
    int expandable;
} InheritBorderComposite;

/**
 * One positional border shorthand (`border-width` and its two siblings).
 *
 * Not a composite record with one kind filled in, because the *mechanism* is
 * different: a positional row's components map to sides by position — one kind,
 * four sides — so what is kept is how many components there were and the side's
 * own component is copied out per longhand.
 */
typedef struct inheritBorderPositional {
    const CssCascadeCandidate *candidate;
    const CssValue *value;
    CssWideKeyword keyword;
    size_t componentCount;
    int expandable;
} InheritBorderPositional;

static void InheritBorderCompositeInit(InheritBorderComposite *record)
{
    size_t kind;

    record->candidate = NULL;
    record->value = NULL;
    record->keyword = RoboCssWideKeywordNone;
    record->expandable = 0;
    for (kind = 0u; kind < RoboCssBorderKindCount; kind++) {
        CssValueInit(&record->parts[kind]);
    }
}

static void InheritBorderCompositeDispose(InheritBorderComposite *record)
{
    size_t kind;

    for (kind = 0u; kind < RoboCssBorderKindCount; kind++) {
        CssValueDispose(&record->parts[kind]);
    }
}

/**
 * Resolve one composite shorthand and try to distribute it.
 *
 * A shorthand that cannot be distributed is counted **once, here**, rather than
 * once per longhand it would have covered — it is one declaration that went
 * undistributed, and a counter that grew by twelve for one author mistake would
 * be reporting the wrong thing.
 */
static void InheritBorderCompositeRead(InheritBorderComposite *record,
                                          const CssCascade *cascade,
                                          CssSlice name,
                                          CssInheritedStyle *out)
{
    (void)CssCascadeResolveName(cascade, name, &record->candidate);
    if (!record->candidate) {
        return;
    }
    record->value = CssDeclarationValue(record->candidate->declaration);
    record->keyword = CssWideKeywordOf(record->value);
    if (record->keyword != RoboCssWideKeywordNone) {
        return;
    }
    record->expandable =
        CssBorderExpand(record->value, record->parts, NULL) == RoboOk;
    if (!record->expandable) {
        /* `border: var(--rule)` is the case: the substitution has not happened, so
         * how many components there are and which kind each is are both unknown.
         * The author's text stays on the shorthand's own entry and nothing is
         * guessed at here — see the header comment for why there is no fallback to
         * a lower-priority declaration. */
        out->shorthandsUnexpanded++;
        out->unsupported = 1;
    }
}

static void InheritBorderPositionalRead(InheritBorderPositional *record,
                                           const CssCascade *cascade,
                                           CssSlice name,
                                           CssInheritedStyle *out)
{
    (void)CssCascadeResolveName(cascade, name, &record->candidate);
    if (!record->candidate) {
        return;
    }
    record->value = CssDeclarationValue(record->candidate->declaration);
    record->keyword = CssWideKeywordOf(record->value);
    if (record->keyword != RoboCssWideKeywordNone) {
        return;
    }
    record->expandable =
        CssBoxIsExpandable(record->value, &record->componentCount);
    if (!record->expandable) {
        out->shorthandsUnexpanded++;
        out->unsupported = 1;
    }
}

/** Keep whichever of @p candidate and *@p best the cascade would order first. */
static void InheritBorderWeigh(const CssCascade *cascade,
                                 const CssCascadeCandidate *candidate,
                                 const CssCascadeCandidate **best)
{
    if (!candidate) {
        return;
    }
    /* A zero answer means the same candidate or two the cascade does not hold;
     * every candidate here comes from the cascade, so a zero is the first case and
     * keeping what is already held is the right answer. */
    if (!*best || CssCascadeCandidateCompare(cascade, candidate, *best) < 0) {
        *best = candidate;
    }
}

/**
 * Distribute the border family over its twelve longhands (9.15).
 *
 * One pass rather than two, and that is the phase's central decision. A single
 * border longhand can be set by as many as four declarations at once:
 *
 *     border-top-width: 5px;      the longhand itself
 *     border-width: 1px;          its positional row  (four sides, one kind)
 *     border-top: 1px solid red;  its side shorthand (one side, three kinds)
 *     border: 2px dashed blue;    the whole shorthand (four sides, three kinds)
 *
 * and a longhand cannot be materialised by two independent passes. So the pass
 * gathers every candidate that speaks for each longhand and takes the one the
 * **cascade** orders first — asked through `CssCascadeCandidateCompare`,
 * the same door the box and background passes ask, so nothing here weighs a level
 * or compares a specificity.
 *
 * The eight style and colour longhands would have failed on their own terms
 * anyway: `border-top-style` is covered by no box-table row before this phase, so
 * without a pass that can see `border` and `border-top` together there would be
 * no pass that could materialise it at all.
 *
 * Three outcomes per longhand, in the order they are decided:
 *
 *   nothing spoke              no entry, and there is not supposed to be one;
 *   a shorthand won with a     obey the keyword — `border: inherit` means the
 *   CSS-wide keyword           same sentence twelve times;
 *   a shorthand won with       a composite's component for that kind, or the
 *   a value                    positional row's component for that side.
 *
 * and one outcome that exists only for the composite shorthands: a kind the
 * author did not write is **reset to its initial value**, because `border` stands
 * for all three kinds on all four sides and `border: 1px solid` really does say
 * that the border has no colour of its own. The positional rows have no such case
 * — one to four components always cover all four sides, so nothing is left
 * unnamed.
 */
static int InheritExpandBorderShorthands(CssInheritedStyle *out,
                                            const CssStyleContext *context,
                                            const CssCascade *cascade,
                                            const CssInheritedStyle *parent)
{
    const CssPropertyTable *table;
    InheritBorderComposite whole;
    InheritBorderComposite sides[RoboCssBoxSideCount];
    InheritBorderPositional positional[RoboCssBorderKindCount];
    size_t sideIndex;
    size_t kindIndex;
    int failed = 0;

    InheritBorderCompositeInit(&whole);
    for (sideIndex = 0u; sideIndex < (size_t)RoboCssBoxSideCount; sideIndex++) {
        InheritBorderCompositeInit(&sides[sideIndex]);
    }
    for (kindIndex = 0u; kindIndex < RoboCssBorderKindCount; kindIndex++) {
        positional[kindIndex].candidate = NULL;
        positional[kindIndex].value = NULL;
        positional[kindIndex].keyword = RoboCssWideKeywordNone;
        positional[kindIndex].componentCount = 0u;
        positional[kindIndex].expandable = 0;
    }
    if (!cascade) {
        return 1;
    }
    table = CssStyleContextPropertyTable(context);

    /* The eight shorthands are resolved once rather than once per longhand: a
     * shorthand's candidate is the same one for all of the longhands it covers,
     * and resolving it twelve times would be twelve chances to resolve it
     * differently. */
    InheritBorderCompositeRead(&whole, cascade,
                                 CssSliceFromText(
                                     CssBorderCompositeName(0u)),
                                 out);
    for (sideIndex = 0u; sideIndex < (size_t)RoboCssBoxSideCount; sideIndex++) {
        InheritBorderCompositeRead(
            &sides[sideIndex], cascade,
            CssSliceFromText(
                CssBorderCompositeName(sideIndex + 1u)),
            out);
    }
    for (kindIndex = 0u; kindIndex < RoboCssBorderKindCount; kindIndex++) {
        InheritBorderPositionalRead(
            &positional[kindIndex], cascade,
            CssSliceFromText(
                CssBorderPositionalName(kindIndex)),
            out);
    }

    for (sideIndex = 0u; sideIndex < (size_t)RoboCssBoxSideCount; sideIndex++) {
        CssBoxSide side = (CssBoxSide)sideIndex;

        for (kindIndex = 0u; kindIndex < RoboCssBorderKindCount; kindIndex++) {
            CssBorderKind kind = (CssBorderKind)kindIndex;
            const char *longhandName = CssBorderLonghandName(side, kind);
            CssSlice name;
            const CssCascadeCandidate *longhandWinner = NULL;
            const CssCascadeCandidate *best = NULL;
            CssPropertyId property;
            int inheritedByDefault;

            if (!longhandName) {
                continue;
            }
            name = CssSliceFromText(longhandName);
            (void)CssCascadeResolveName(cascade, name, &longhandWinner);
            best = longhandWinner;
            InheritBorderWeigh(cascade, positional[kindIndex].candidate, &best);
            InheritBorderWeigh(cascade, sides[sideIndex].candidate, &best);
            InheritBorderWeigh(cascade, whole.candidate, &best);
            if (!best) {
                /* Nothing spoke for this longhand: it has no entry, and it is not
                 * supposed to — an element that declares no border at all has no
                 * border longhands, exactly as it has no margins. */
                continue;
            }
            property = table ? CssPropertyTableLookupSlice(table, name)
                             : RoboCssPropertyIdNone;
            inheritedByDefault = InheritClassify(context, name);

            if (best == longhandWinner) {
                if (!InheritValue(out, context, longhandWinner->property, name,
                                   CssDeclarationValue(
                                       longhandWinner->declaration),
                                   parent, inheritedByDefault)) {
                    failed = 1;
                    break;
                }
                continue;
            }
            if (best == positional[kindIndex].candidate) {
                if (positional[kindIndex].keyword != RoboCssWideKeywordNone) {
                    if (!InheritValue(out, context, property, name,
                                       positional[kindIndex].value, parent,
                                       inheritedByDefault)) {
                        failed = 1;
                        break;
                    }
                    continue;
                }
                if (!positional[kindIndex].expandable) {
                    /* Counted once when the shorthand was read; nothing guessed. */
                    continue;
                }
                {
                    CssValue component = RoboCssCleared;

                    if (CssBoxExpandSide(positional[kindIndex].value,
                                                 positional[kindIndex].componentCount,
                                                 side, &component, NULL) != RoboOk) {
                        failed = 1;
                        break;
                    }
                    if (!InheritValue(out, context, property, name, &component,
                                       parent, inheritedByDefault)) {
                        CssValueDispose(&component);
                        failed = 1;
                        break;
                    }
                    CssValueDispose(&component);
                }
                continue;
            }
            /* The winner is `border` or a side shorthand: both are composites, and
             * the only difference is which record holds the split. */
            {
                const InheritBorderComposite *source =
                    best == whole.candidate ? &whole : &sides[sideIndex];

                if (source->keyword != RoboCssWideKeywordNone) {
                    if (!InheritValue(out, context, property, name, source->value,
                                       parent, inheritedByDefault)) {
                        failed = 1;
                        break;
                    }
                    continue;
                }
                if (!source->expandable) {
                    continue;
                }
                if (source->parts[kindIndex].kind == RoboCssValueKindNone) {
                    /* The shorthand stands for this kind and did not name it: the
                     * longhand is reset to the property's own initial value, and
                     * the registry is the one place that value is written down. */
                    const CssValue *initial =
                        InheritDefaultValue(context, name);

                    if (!initial) {
                        continue;
                    }
                    if (!InheritValue(out, context, property, name, initial, parent,
                                       inheritedByDefault)) {
                        failed = 1;
                        break;
                    }
                    continue;
                }
                if (!InheritValue(out, context, property, name,
                                   &source->parts[kindIndex], parent,
                                   inheritedByDefault)) {
                    failed = 1;
                    break;
                }
            }
        }
        if (failed) {
            break;
        }
    }

    InheritBorderCompositeDispose(&whole);
    for (sideIndex = 0u; sideIndex < (size_t)RoboCssBoxSideCount; sideIndex++) {
        InheritBorderCompositeDispose(&sides[sideIndex]);
    }

    /* One line for the family, because this is the only pass that distributes a
     * *composite*: three kinds over four sides, arriving from as many as four
     * declarations at once, and a caller holding the resolved style cannot tell a
     * border longhand from a margin one. The refused half of the sentence is the
     * rest of it — the shorthands that met an author's `var()` or a component count
     * this grammar has no place for and were therefore not distributed, whose
     * declarations are still intact on their own entries. */
    {
        size_t materialised = 0u;
        size_t refused = 0u;

        for (sideIndex = 0u; sideIndex < (size_t)RoboCssBoxSideCount;
             sideIndex++) {
            for (kindIndex = 0u; kindIndex < (size_t)RoboCssBorderKindCount;
                 kindIndex++) {
                const char *name = CssBorderLonghandName(
                    (CssBoxSide)sideIndex,
                    (CssBorderKind)kindIndex);

                if (name && CssInheritedStyleHas(
                                out, CssSliceFromText(name))) {
                    materialised++;
                }
            }
        }
        if (whole.candidate && whole.keyword == RoboCssWideKeywordNone &&
            !whole.expandable) {
            refused++;
        }
        for (sideIndex = 0u; sideIndex < (size_t)RoboCssBoxSideCount;
             sideIndex++) {
            if (sides[sideIndex].candidate &&
                sides[sideIndex].keyword == RoboCssWideKeywordNone &&
                !sides[sideIndex].expandable) {
                refused++;
            }
        }
        for (kindIndex = 0u; kindIndex < (size_t)RoboCssBorderKindCount;
             kindIndex++) {
            if (positional[kindIndex].candidate &&
                positional[kindIndex].keyword == RoboCssWideKeywordNone &&
                !positional[kindIndex].expandable) {
                refused++;
            }
        }
        LogDebug("robo_css: inherit: border family: %llu of %llu longhand(s) "
                       "materialised, %llu shorthand(s) not distributable",
                       (unsigned long long)materialised,
                       (unsigned long long)((size_t)RoboCssBoxSideCount *
                                            (size_t)RoboCssBorderKindCount),
                       (unsigned long long)refused);
    }
    return failed ? 0 : 1;
}

Result RoboCssInheritResolve(const CssStyleContext *context,
                                       const CssCascade *cascade,
                                       const CssInheritedStyle *parent,
                                       CssInheritedStyle *out,
                                       CssDiag *diag)
{
    size_t index;

    if (!context || !out) {
        return RoboCssDiag(diag, RoboCssDomainInherit, RoboInvalidArgument,
                             "context or output is NULL");
    }
    CssInheritedStyleInit(out);
    out->element = cascade ? cascade->element : RoboCssElementRefNone;
    out->hasParent = parent != NULL;

    /* The whole pass is transactional: any failure disposes what it built, so a
     * caller that ignores the result code owns an empty style rather than half
     * of one. */
    for (index = 0u; index < (cascade ? cascade->count : 0u); index++) {
        const CssCascadeCandidate *candidate = &cascade->items[index];
        const CssDeclaration *declaration = candidate->declaration;
        CssSlice name;
        int inheritedByDefault;
        const CssCascadeCandidate *winner = NULL;

        if (!CssDeclarationIsUsable(declaration)) {
            continue;
        }
        name = CssDeclarationName(declaration);
        if (CssSliceIsAbsent(name)) {
            continue;
        }
        if (CssBoxShorthandOfLonghand(name, NULL) != NULL) {
            /* A box-model longhand is distributed out of its shorthand rather
             * than resolved one name at a time — it and the shorthand compete
             * for the same edge, and the pass below is where that is settled.
             * Materialising it here as well would give the edge two entries. */
            continue;
        }
        if (CssBackgroundIsLonghand(name)) {
            /* The same rule for the background family (9.14): the six longhands
             * are materialised by the pass below, and only there, so a longhand
             * cannot arrive twice — once as itself and once out of `background`. */
            continue;
        }
        /* Only the winner is materialised, and the cascade decides who that is:
         * asking it rather than comparing levels here is what keeps precedence
         * in one place. A declaration whose *name* is unknown has no id, so the
         * name is the question — the same split the cascade's two resolvers
         * have. */
        if (candidate->property != RoboCssPropertyIdNone) {
            (void)CssCascadeResolveProperty(cascade, candidate->property,
                                                    &winner);
        } else {
            (void)CssCascadeResolveName(cascade, name, &winner);
        }
        if (winner != candidate) {
            continue;
        }
        /* A key already materialised is not materialised twice: the winner is
         * unique per property, so a repeat here means the same *name* reached
         * the cascade through two candidates that resolved to no id and differ
         * in case. */
        if (CssInheritedStyleHas(out, name)) {
            continue;
        }
        inheritedByDefault = InheritClassify(context, name);
        if (!InheritDeclared(out, context, candidate, parent, name,
                              inheritedByDefault)) {
            CssInheritedStyleDispose(out);
            return RoboCssDiag(diag, RoboCssDomainInherit, RoboOutOfMemory,
                                 "the resolved style could not be built");
        }
    }

    /* The border family first, because it owns the twelve longhands the box table
     * shares with it — the three rows it distributes are skipped by the box pass
     * below, so the order is a fact about ownership rather than about precedence:
     * neither pass can overwrite the other. */
    if (!InheritExpandBorderShorthands(out, context, cascade, parent)) {
        CssInheritedStyleDispose(out);
        return RoboCssDiag(diag, RoboCssDomainInherit, RoboOutOfMemory,
                             "the resolved style could not be built");
    }
    /* The box model's longhands, over the shorthands that stand for them. */
    if (!InheritExpandBoxShorthands(out, context, cascade, parent)) {
        CssInheritedStyleDispose(out);
        return RoboCssDiag(diag, RoboCssDomainInherit, RoboOutOfMemory,
                             "the resolved style could not be built");
    }
    /* And the background family's (9.14), which is a pass of its own because its
     * shorthand's components map to longhands by kind rather than by position. */
    if (!InheritExpandBackgroundShorthand(out, context, cascade, parent)) {
        CssInheritedStyleDispose(out);
        return RoboCssDiag(diag, RoboCssDomainInherit, RoboOutOfMemory,
                             "the resolved style could not be built");
    }
    if (out->shorthandsUnexpanded > 0u) {
        /* Reported rather than silent, because this is the one quiet failure in
         * the pass: the shorthand's own entry is intact, so a caller reading the
         * style sees a declaration and no longhands and has no way to tell that a
         * box's four edges or a background's six parts went unanswered. One line
         * per resolution that met such a declaration, in the same spirit as the
         * tokenizer's error line. */
        LogDebug("robo_css: inherit: %llu shorthand(s) met that this build "
                       "cannot distribute over their longhands",
                       (unsigned long long)out->shorthandsUnexpanded);
    } else {
        LogDebug("robo_css: inherit: every shorthand distributed, %llu "
                       "longhand(s) materialised",
                       (unsigned long long)out->count);
    }

    /* What the parent carries, for the properties that inherit by default. */
    for (index = 0u; index < (parent ? parent->count : 0u); index++) {
        const CssInheritedProperty *entry = &parent->items[index];
        CssSlice name = CssInheritedPropertyName(entry);

        if (CssSliceIsAbsent(name) ||
            CssInheritedStyleHas(out, name)) {
            continue;
        }
        if (!InheritClassify(context, name)) {
            continue;
        }
        if (entry->unresolved) {
            /* The parent had no value either. Nothing is carried and nothing is
             * invented; set 3 below will reach the same conclusion. */
            continue;
        }
        if (!InheritFromParent(out, entry->property, name, entry)) {
            CssInheritedStyleDispose(out);
            return RoboCssDiag(diag, RoboCssDomainInherit, RoboOutOfMemory,
                                 "the resolved style could not be built");
        }
    }

    /* Every inherited-by-default property the registry knows that neither the
     * element nor the parent supplied: its initial value, or the typed
     * "default not known" state. This is where a root's inherited properties
     * come from, and it is why an element with no declarations at all still
     * resolves to a complete style. */
    {
        const CssPropertyRegistry *registry =
            CssStyleContextPropertyRegistry(context);
        size_t registryCount = CssPropertyRegistryCount(registry);

        for (index = 0u; index < registryCount; index++) {
            const CssPropertyDefinition *definition =
                CssPropertyRegistryAt(registry, index);
            CssSlice name;
            CssPropertyId id = RoboCssPropertyIdNone;

            if (!definition || !CssPropertyDefinitionInherited(definition)) {
                continue;
            }
            name = CssSliceFromText(definition->name);
            if (CssInheritedStyleHas(out, name)) {
                continue;
            }
            /* The id, when the context's table knows the name: the entry should
             * be as identity-carrying as a declared one, so a caller holding an
             * id finds it. */
            {
                const CssPropertyTable *table =
                    CssStyleContextPropertyTable(context);

                if (table) {
                    id = CssPropertyTableLookupSlice(table, name);
                }
            }
            if (!InheritAppend(out, id, name, RoboCssInheritedSourceDefault,
                                RoboCssWideKeywordNone,
                                CssPropertyDefinitionInitialValue(definition))) {
                CssInheritedStyleDispose(out);
                return RoboCssDiag(diag, RoboCssDomainInherit, RoboOutOfMemory,
                                     "the resolved style could not be built");
            }
            InheritTally(out, RoboCssInheritedSourceDefault);
        }
    }

    /* Incompleteness is inherited the way values are: a cascade that could not
     * decide a selector might have declared a property that is about to be
     * taken from the parent instead, and a parent that is itself incomplete
     * makes this element's inherited values no better. */
    if ((cascade && !CssCascadeIsComplete(cascade)) ||
        (parent && !CssInheritedStyleIsComplete(parent))) {
        out->unsupported = 1;
    }
    return RoboOk;
}
