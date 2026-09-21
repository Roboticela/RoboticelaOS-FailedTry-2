/*
 * RoboCSS property registry (Phase 9.7) — see RoboCSSPropertyRegistry.h.
 *
 * Three things live here and nothing else: the name-comparison rule, the parse
 * of an initial value at registration time, and the standard property set. The
 * registry issues no ids and interprets no value, because identity is
 * RoboCSSProperty.h's job and interpretation is 9.8's.
 */

#include "RoboCSS/RoboCSSPropertyRegistry.h"

#include "RoboCSS/RoboCSSSource.h"
#include "RoboCSSInternal.h"
#include "RoboMem/RoboMem.h"

#include <string.h>

/*
 * An entry *is* a CssPropertyDefinition — the definition struct is
 * both the storage and the view, so there is no second layout that could drift
 * and no cast that could be wrong.
 *
 * Entries are individually allocated and addressed through the registry's
 * pointer array, so a later registration cannot relocate an earlier definition:
 * a borrowed definition stays valid until the registry is destroyed. That is
 * the whole reason for the extra indirection, and it is the same reason the
 * stylesheet module addresses slots by id rather than by pointer into a moving
 * table.
 */

struct CssPropertyRegistry {
    CssPropertyDefinition **entries; /* owned; each element owned too */
    size_t count;
    size_t capacity;
};

/* ------------------------------------------------------------------ */
/* Naming                                                             */
/* ------------------------------------------------------------------ */

int CssIsCustomPropertyName(CssSlice name)
{
    return name.length >= 3u && name.bytes && name.bytes[0] == (uint8_t)'-' &&
           name.bytes[1] == (uint8_t)'-';
}

/**
 * Does @p entry name the property @p name is looking for?
 *
 * CSS's own rule, in one place: an ordinary property name is compared ASCII
 * case-insensitively, a custom property byte-exactly. A caller cannot get this
 * wrong by choosing a different comparison at a call site, because there is no
 * other comparison to choose.
 */
static int RegistryEntryMatches(const CssPropertyDefinition *entry,
                                  CssSlice name)
{
    CssSlice stored =
        CssSliceFromBytes((const uint8_t *)entry->name, entry->nameLength);

    if (CssIsCustomPropertyName(name) ||
        CssIsCustomPropertyName(stored)) {
        return CssSliceEquals(stored, name);
    }
    return CssSliceEqualsCi(stored, name);
}

/**
 * A property name the property table would also accept: non-empty, bounded,
 * with no NUL, whitespace, `:` or `.`. Duplicated from RoboCSSProperty.c on
 * purpose? No — it is *the same rule*, and it is stated here rather than shared
 * because the two live in different translation units with no internal header
 * between them for it, and a name the registry accepts and the table refuses
 * would be a failure at apply time for no reason the caller could see.
 */
static int RegistryNameIsUsable(const char *name, size_t *outLength)
{
    size_t index;
    size_t length;

    if (!name) {
        return 0;
    }
    length = strlen(name);
    if (length == 0u || length > (size_t)RoboCssPropertyNameMax) {
        return 0;
    }
    for (index = 0u; index < length; index++) {
        uint8_t c = (uint8_t)name[index];

        if (c == 0u || CssAsciiIsSpace(c) || c == (uint8_t)':' ||
            c == (uint8_t)'.') {
            return 0;
        }
    }
    *outLength = length;
    return 1;
}

static char *RegistryCopyText(const char *text, size_t length)
{
    char *copy = (char *)Malloc(length + 1u);

    if (!copy) {
        return NULL;
    }
    if (length > 0u) {
        memcpy(copy, text, length);
    }
    copy[length] = '\0';
    return copy;
}

/* ------------------------------------------------------------------ */
/* Classification names                                               */
/* ------------------------------------------------------------------ */

int CssPropertyInheritanceIsKnown(CssPropertyInheritance inheritance)
{
    return inheritance == RoboCssPropertyInheritanceUnknown ||
           inheritance == RoboCssPropertyInheritanceInherited ||
           inheritance == RoboCssPropertyInheritanceNonInherited;
}

const char *CssPropertyInheritanceName(
    CssPropertyInheritance inheritance)
{
    switch (inheritance) {
    case RoboCssPropertyInheritanceUnknown:
        return "unknown";
    case RoboCssPropertyInheritanceInherited:
        return "inherited";
    case RoboCssPropertyInheritanceNonInherited:
        return "non-inherited";
    default:
        return "unknown";
    }
}

int CssPropertyComputationIsKnown(CssPropertyComputation computation)
{
    return computation >= RoboCssPropertyComputationPreserve &&
           computation < (CssPropertyComputation)RoboCssPropertyComputationCount;
}

const char *CssPropertyComputationName(
    CssPropertyComputation computation)
{
    switch (computation) {
    case RoboCssPropertyComputationPreserve:
        return "preserve";
    case RoboCssPropertyComputationColor:
        return "colour";
    case RoboCssPropertyComputationLength:
        return "length";
    case RoboCssPropertyComputationKeyword:
        return "keyword";
    case RoboCssPropertyComputationFontFamily:
        return "font-family";
    case RoboCssPropertyComputationFontWeight:
        return "font-weight";
    case RoboCssPropertyComputationLineHeight:
        return "line-height";
    case RoboCssPropertyComputationEdgeOffset:
        return "edge offset";
    case RoboCssPropertyComputationEdgeInset:
        return "edge inset";
    case RoboCssPropertyComputationLineWidth:
        return "line width";
    case RoboCssPropertyComputationDisplay:
        return "display";
    case RoboCssPropertyComputationPosition:
        return "position";
    case RoboCssPropertyComputationZIndex:
        return "z-index";
    case RoboCssPropertyComputationBackgroundRepeat:
        return "background-repeat";
    case RoboCssPropertyComputationBackgroundAttachment:
        return "background-attachment";
    case RoboCssPropertyComputationBackgroundImage:
        return "background-image";
    case RoboCssPropertyComputationBackgroundPosition:
        return "background-position";
    case RoboCssPropertyComputationBackgroundSize:
        return "background-size";
    case RoboCssPropertyComputationBorderStyle:
        return "border-style";
    case RoboCssPropertyComputationOverflow:
        return "overflow";
    case RoboCssPropertyComputationOpacity:
        return "opacity";
    default:
        return "unknown";
    }
}

int CssPropertyDefinitionInherited(
    const CssPropertyDefinition *definition)
{
    return definition &&
           definition->inheritance == RoboCssPropertyInheritanceInherited;
}

const CssValue *CssPropertyDefinitionInitialValue(
    const CssPropertyDefinition *definition)
{
    if (!definition || definition->initialText == NULL) {
        return NULL;
    }
    return &definition->initialValue;
}

/* ------------------------------------------------------------------ */
/* Registry lifecycle                                                 */
/* ------------------------------------------------------------------ */

Result CssPropertyRegistryCreate(CssPropertyRegistry **outRegistry,
                                                CssDiag *diag)
{
    CssPropertyRegistry *registry;

    if (!outRegistry) {
        return RoboCssDiag(diag, RoboCssDomainPropertyRegistry, RoboInvalidArgument,
                             "output is NULL");
    }
    *outRegistry = NULL;
    registry = (CssPropertyRegistry *)RoboCalloc(
        1u, sizeof(CssPropertyRegistry));
    if (!registry) {
        return RoboCssDiag(diag, RoboCssDomainPropertyRegistry, RoboOutOfMemory,
                             "could not allocate the registry");
    }
    *outRegistry = registry;
    return RoboOk;
}

void CssPropertyRegistryDestroy(CssPropertyRegistry *registry)
{
    size_t index;

    if (!registry) {
        return;
    }
    for (index = 0u; index < registry->count; index++) {
        CssPropertyDefinition *entry = registry->entries[index];

        if (!entry) {
            continue;
        }
        /* The value before the text it borrows, so nothing reads freed bytes
         * during disposal even though disposal does not read them. */
        CssValueDispose(&entry->initialValue);
        RoboFree(entry->initialText);
        RoboFree(entry->name);
        RoboFree(entry);
    }
    RoboFree(registry->entries);
    RoboFree(registry);
}

const CssPropertyDefinition *CssPropertyRegistryAt(
    const CssPropertyRegistry *registry,
    size_t index)
{
    if (!registry || index >= registry->count) {
        return NULL;
    }
    return registry->entries[index];
}

const CssPropertyDefinition *CssPropertyRegistryFindSlice(
    const CssPropertyRegistry *registry,
    CssSlice name)
{
    size_t index;

    if (!registry || CssSliceIsAbsent(name)) {
        return NULL;
    }
    for (index = 0u; index < registry->count; index++) {
        if (RegistryEntryMatches(registry->entries[index], name)) {
            return CssPropertyRegistryAt(registry, index);
        }
    }
    return NULL;
}

const CssPropertyDefinition *CssPropertyRegistryFind(
    const CssPropertyRegistry *registry,
    const char *name)
{
    if (!name) {
        return NULL;
    }
    return CssPropertyRegistryFindSlice(registry,
                                                 CssSliceFromText(name));
}

size_t CssPropertyRegistryCount(const CssPropertyRegistry *registry)
{
    return registry ? registry->count : 0u;
}

const CssPropertyDefinition *CssPropertyRegistryFindProperty(
    const CssPropertyRegistry *registry,
    const CssPropertyTable *table,
    CssPropertyId property)
{
    size_t index;

    if (!registry || !table || property == RoboCssPropertyIdNone) {
        return NULL;
    }
    for (index = 0u; index < registry->count; index++) {
        const CssPropertyDefinition *entry = registry->entries[index];

        if (!entry) {
            continue;
        }
        if (CssPropertyTableLookup(table, entry->name) == property) {
            return entry;
        }
    }
    return NULL;
}

Result CssPropertyRegistryRegister(CssPropertyRegistry *registry,
                                                  const char *name,
                                                  CssPropertyInheritance inheritance,
                                                  const char *initialText,
                                                  uint32_t flags,
                                                  CssDiag *diag)
{
    return CssPropertyRegistryRegisterTyped(
        registry, name, inheritance, RoboCssPropertyComputationPreserve,
        initialText, flags, diag);
}

Result CssPropertyRegistryRegisterTyped(
    CssPropertyRegistry *registry,
    const char *name,
    CssPropertyInheritance inheritance,
    CssPropertyComputation computation,
    const char *initialText,
    uint32_t flags,
    CssDiag *diag)
{
    CssPropertyDefinition *entry;
    size_t nameLength = 0u;

    if (!registry) {
        return RoboCssDiag(diag, RoboCssDomainPropertyRegistry, RoboInvalidArgument,
                             "registry is NULL");
    }
    if (!CssPropertyComputationIsKnown(computation)) {
        return RoboCssDiag(diag, RoboCssDomainPropertyRegistry, RoboInvalidArgument,
                             "not a value-computation classification");
    }
    if (!RegistryNameIsUsable(name, &nameLength)) {
        return RoboCssDiag(diag, RoboCssDomainPropertyRegistry, RoboInvalidArgument,
                             "not a usable property name");
    }
    if (inheritance != RoboCssPropertyInheritanceInherited &&
        inheritance != RoboCssPropertyInheritanceNonInherited) {
        /* UNKNOWN is never *stored*: a definition that says nothing would be a
         * way for a lookup to succeed while classifying nothing, which is
         * strictly worse than not finding the property at all. */
        return RoboCssDiag(diag, RoboCssDomainPropertyRegistry, RoboInvalidArgument,
                             "a definition must classify the property");
    }
    if (CssPropertyRegistryFind(registry, name) != NULL) {
        return RoboCssDiag(diag, RoboCssDomainPropertyRegistry, RoboAlreadyExists,
                             "property is already registered");
    }

    entry = (CssPropertyDefinition *)RoboCalloc(
        1u, sizeof(CssPropertyDefinition));
    if (!entry) {
        return RoboCssDiag(diag, RoboCssDomainPropertyRegistry, RoboOutOfMemory,
                             "could not allocate the definition");
    }
    CssValueInit(&entry->initialValue);
    entry->name = RegistryCopyText(name, nameLength);
    if (!entry->name) {
        RoboFree(entry);
        return RoboCssDiag(diag, RoboCssDomainPropertyRegistry, RoboOutOfMemory,
                             "could not copy the property name");
    }
    entry->nameLength = nameLength;
    entry->inheritance = inheritance;
    entry->computation = computation;
    entry->flags = flags;

    if (initialText != NULL) {
        CssSource source = RoboCssCleared;
        CssError error = RoboCssCleared;
        size_t textLength = strlen(initialText);

        entry->initialText = RegistryCopyText(initialText, textLength);
        if (!entry->initialText) {
            RoboFree(entry->name);
            RoboFree(entry);
            return RoboCssDiag(diag, RoboCssDomainPropertyRegistry, RoboOutOfMemory,
                                 "could not copy the initial value text");
        }
        entry->initialTextLength = textLength;
        /* Parsed once, here. An initial value that does not parse is a defect
         * in the registry, and it (registry) is where it should be caught. */
        (void)CssSourceInit(&source);
        if (CssSourceSetMemory(&source, (const uint8_t *)entry->initialText,
                                       entry->initialTextLength, NULL) != RoboOk ||
            CssGrammarParseValue(&entry->initialValue, &source,
                                         RoboCssOriginNone, &error) != RoboOk ||
            CssErrorIsError(&error)) {
            CssSourceDispose(&source);
            CssValueDispose(&entry->initialValue);
            RoboFree(entry->initialText);
            RoboFree(entry->name);
            RoboFree(entry);
            return RoboCssDiag(diag, RoboCssDomainPropertyRegistry, RoboInvalidArgument,
                                 "the initial value text does not parse");
        }
        CssSourceDispose(&source);
    }

    if (!CssGrow((void **)&registry->entries, &registry->capacity, registry->count,
                       sizeof(CssPropertyDefinition *))) {
        CssValueDispose(&entry->initialValue);
        RoboFree(entry->initialText);
        RoboFree(entry->name);
        RoboFree(entry);
        return RoboCssDiag(diag, RoboCssDomainPropertyRegistry, RoboOutOfMemory,
                             "could not grow the registry");
    }
    registry->entries[registry->count] = entry;
    registry->count++;
    return RoboOk;
}

/* ------------------------------------------------------------------ */
/* The standard set                                                   */
/* ------------------------------------------------------------------ */

/** One row of the standard set: name, classification, value shape, initial, flags. */
typedef struct registryStandardEntry {
    const char *name;
    CssPropertyInheritance inheritance;
    CssPropertyComputation computation;
    const char *initialText;
    uint32_t flags;
} RegistryStandardEntry;

/*
 * The standard set.
 *
 * Three facts about this table are decisions rather than transcriptions.
 *
 * The **initial values are spellings, not computed values**: `black` is the
 * CSS2.1 initial for `color`, `medium none currentcolor` is the shorthand
 * `border`'s concatenation of its longhands' initials, and none of them is
 * resolved into a pixel, a packed colour or a used width here. That is what
 * makes them safe to carry: an unresolved spelling is a representation, a
 * resolved one would be a guess about font metrics, the viewport and the
 * parent's geometry.
 *
 * The **computation column is the value's shape**, and it is what lets 9.8
 * normalise a declared value without a switch on the property name. `margin`
 * and the other shorthands are PRESERVE because their longhands are not
 * expanded yet: expanding `margin: 0 auto` would be inventing a shorthand
 * engine, and carrying `0 auto` intact is a computed representation that is
 * exactly as true as `10px` is for `width`.
 *
 * The **font and text properties are included although this build implements no
 * font or text semantics for CSS**, because the classification question they
 * answer — "does this propagate?" — is independent of whether anything reads
 * the value. A browser's own default for `font-family` is not `serif`: it is
 * the user agent's choice, and `serif` is recorded here as *this build's*
 * unresolved default rather than as a claim about the document. `width: auto`
 * and `display: inline` are the same kind of statement: the property's initial
 * value, spelled.
 *
 * Nothing here is a user-agent stylesheet. A default stylesheet is a set of
 * *rules*, and this is a set of property facts; conflating them would put
 * precedence questions in a table that has no notion of precedence.
 */
static const RegistryStandardEntry registryStandardEntries[] = {
    /* --- inherited --- */
    { "color",         RoboCssPropertyInheritanceInherited,
      RoboCssPropertyComputationColor,      "black",      0 },
    { "cursor",        RoboCssPropertyInheritanceInherited,
      RoboCssPropertyComputationKeyword,    "auto",       0 },
    { "direction",     RoboCssPropertyInheritanceInherited,
      RoboCssPropertyComputationKeyword,    "ltr",        0 },
    { "font-family",   RoboCssPropertyInheritanceInherited,
      RoboCssPropertyComputationFontFamily,"serif",      0 },
    { "font-size",     RoboCssPropertyInheritanceInherited,
      RoboCssPropertyComputationLength,     "medium",     0 },
    { "font-style",    RoboCssPropertyInheritanceInherited,
      RoboCssPropertyComputationKeyword,    "normal",     0 },
    { "font-variant",  RoboCssPropertyInheritanceInherited,
      RoboCssPropertyComputationKeyword,    "normal",     0 },
    { "font-weight",   RoboCssPropertyInheritanceInherited,
      RoboCssPropertyComputationFontWeight,"normal",     0 },
    { "letter-spacing",RoboCssPropertyInheritanceInherited,
      RoboCssPropertyComputationLength,     "normal",     0 },
    { "line-height",   RoboCssPropertyInheritanceInherited,
      RoboCssPropertyComputationLineHeight,"normal",     0 },
    { "text-align",    RoboCssPropertyInheritanceInherited,
      RoboCssPropertyComputationKeyword,    "start",      0 },
    { "text-indent",   RoboCssPropertyInheritanceInherited,
      RoboCssPropertyComputationLength,     "0",          0 },
    { "text-transform",RoboCssPropertyInheritanceInherited,
      RoboCssPropertyComputationKeyword,    "none",       0 },
    { "visibility",    RoboCssPropertyInheritanceInherited,
      RoboCssPropertyComputationKeyword,    "visible",    0 },
    { "white-space",   RoboCssPropertyInheritanceInherited,
      RoboCssPropertyComputationKeyword,    "normal",     0 },
    { "word-spacing",  RoboCssPropertyInheritanceInherited,
      RoboCssPropertyComputationLength,     "normal",     0 },

    /* --- non-inherited --- */
    { "background",    RoboCssPropertyInheritanceNonInherited,
      RoboCssPropertyComputationPreserve,   "transparent",
      RoboCssPropertyFlagShorthand },
    /* 9.9 added this row, and the reason is not backgrounds: it is the only
     * second property whose value *is* a colour, and without one the rule that
     * resolves `currentColor` against the element's own `color` — as opposed to
     * the parent's, which is what it means on `color` itself — would be
     * unreachable and therefore unprovable. */
    { "background-color", RoboCssPropertyInheritanceNonInherited,
      RoboCssPropertyComputationColor,      "transparent", 0 },

    /*
     * --- the background family (9.14) ---
     *
     * Five longhands, and the shorthand above stays where it was: its own
     * classification is PRESERVE and its own initial value is what CSS says the
     * shorthand starts at (all six longhands at theirs). What 9.14 added is that
     * the six are *distributed* — the pass in RoboCSSInherit.c turns one
     * `background:` declaration into six longhand declarations, so a consumer
     * never has to read a shorthand.
     *
     * Every one of the six is non-inherited, and that is CSS's rule rather than a
     * choice: a background is painted per element, so a child that inherited one
     * would tile its parent's image over its own box. The initial values are the
     * ones CSS names, and two of them are worth reading twice — `background-position`
     * starts at `0% 0%` (the top-left, as a *percentage*, so it stays relative)
     * and `background-size` at `auto`, which is not a length.
     */
    { "background-image",      RoboCssPropertyInheritanceNonInherited,
      RoboCssPropertyComputationBackgroundImage,      "none",    0 },
    { "background-position",   RoboCssPropertyInheritanceNonInherited,
      RoboCssPropertyComputationBackgroundPosition,   "0% 0%",   0 },
    { "background-size",       RoboCssPropertyInheritanceNonInherited,
      RoboCssPropertyComputationBackgroundSize,       "auto",    0 },
    { "background-repeat",     RoboCssPropertyInheritanceNonInherited,
      RoboCssPropertyComputationBackgroundRepeat,     "repeat",  0 },
    { "background-attachment", RoboCssPropertyInheritanceNonInherited,
      RoboCssPropertyComputationBackgroundAttachment, "scroll",  0 },
    /*
     * --- the box model (9.11) ---
     *
     * Twelve rows: the eight edges and the four border widths. The four *styles*
     * and four *colours* belong to 9.15's block below, and the three kinds are
     * rows here rather than rules in the inheritance pass because every one of
     * them is a *property fact*: which longhands a positional shorthand stands
     * for is vocabulary (RoboCSSBox.h), but that a margin starts at zero, that a
     * border width starts at `medium`, and that none of them inherits is what a
     * registry is for. A phase that wrote those into a resolution path would be
     * one hard-coded name comparison away from the defect this table exists to
     * prevent.
     *
     * The two shorthands here are PRESERVE for the same reason `margin` is: a
     * shorthand's value is a *declaration about four properties*, the distribution
     * is expansion's job, and its own entry carries what the author wrote.
     *
     * `box-sizing` is registered and computes to a keyword, and that is the whole
     * of it: which box a width applies to is a layout question, and this build
     * has no layout. Its value model exists so that a declaration survives to
     * the phase that does, rather than being dropped on the floor by the phase
     * that does not.
     */
    { "border-bottom-width", RoboCssPropertyInheritanceNonInherited,
      RoboCssPropertyComputationLineWidth, "medium",     0 },
    { "border-left-width", RoboCssPropertyInheritanceNonInherited,
      RoboCssPropertyComputationLineWidth, "medium",     0 },
    { "border-right-width", RoboCssPropertyInheritanceNonInherited,
      RoboCssPropertyComputationLineWidth, "medium",     0 },
    { "border-top-width", RoboCssPropertyInheritanceNonInherited,
      RoboCssPropertyComputationLineWidth, "medium",     0 },

    /*
     * --- the border family (9.15) ---
     *
     * Seventeen rows: twelve longhands and five distributing shorthands. Their
     * names fall across the whole table — `border` here, `border-top` around the
     * box-model block, the three kind shorthands after it — so they are gathered
     * into a block rather than left where alphabetisation would put them, for the
     * same reason 9.11's box rows are: a reader meeting any one of the seventeen
     * should be able to see the other sixteen.
     *
     * `border` was a row before this phase, with the same initial value and the
     * same PRESERVE shape; what it gained is a *meaning* — the twelve longhands it
     * stands for are now distributed out of it (RoboCSSInherit.c), where before it
     * was a carried declaration with nothing downstream of it.
     *
     * Three facts in the rows themselves are worth naming.
     *
     * The **three kinds take three different shapes**, because that is what they
     * are: `border-width` reuses 9.11's `LINE_WIDTH`, `border-color` reuses 9.9's
     * `COLOR` — so `border-top-color: currentColor` and `color: currentColor` are
     * the same computation and are resolved by the same rule — and only
     * `border-style` needed a shape of its own.
     *
     * The **colour initial is `currentcolor`**, which is CSS's own and is the one
     * initial value in this table that is a *reference* rather than a value. It is
     * also why the colour longhands cannot be `transparent` the way
     * `background-color` is: `border-color` is `currentColor`, and a border whose
     * colour had been resolved to `transparent` at registration time would be a
     * border nobody could see and no author could explain.
     *
     * The **shorthands are PRESERVE**, exactly as `margin` and `background` are: a
     * shorthand's own entry carries what the author wrote, and the distribution is
     * expansion's job rather than computation's.
     */
    { "border",          RoboCssPropertyInheritanceNonInherited,
      RoboCssPropertyComputationPreserve,   "medium none currentcolor",
      RoboCssPropertyFlagShorthand },
    { "border-bottom",   RoboCssPropertyInheritanceNonInherited,
      RoboCssPropertyComputationPreserve,   "medium none currentcolor",
      RoboCssPropertyFlagShorthand },
    { "border-bottom-color", RoboCssPropertyInheritanceNonInherited,
      RoboCssPropertyComputationColor,      "currentcolor", 0 },
    { "border-bottom-style", RoboCssPropertyInheritanceNonInherited,
      RoboCssPropertyComputationBorderStyle, "none",       0 },
    { "border-color",    RoboCssPropertyInheritanceNonInherited,
      RoboCssPropertyComputationPreserve,   "currentcolor",
      RoboCssPropertyFlagShorthand },
    { "border-left",     RoboCssPropertyInheritanceNonInherited,
      RoboCssPropertyComputationPreserve,   "medium none currentcolor",
      RoboCssPropertyFlagShorthand },
    { "border-left-color", RoboCssPropertyInheritanceNonInherited,
      RoboCssPropertyComputationColor,      "currentcolor", 0 },
    { "border-left-style", RoboCssPropertyInheritanceNonInherited,
      RoboCssPropertyComputationBorderStyle, "none",       0 },
    { "border-right",    RoboCssPropertyInheritanceNonInherited,
      RoboCssPropertyComputationPreserve,   "medium none currentcolor",
      RoboCssPropertyFlagShorthand },
    { "border-right-color", RoboCssPropertyInheritanceNonInherited,
      RoboCssPropertyComputationColor,      "currentcolor", 0 },
    { "border-right-style", RoboCssPropertyInheritanceNonInherited,
      RoboCssPropertyComputationBorderStyle, "none",       0 },
    { "border-style",    RoboCssPropertyInheritanceNonInherited,
      RoboCssPropertyComputationPreserve,   "none",
      RoboCssPropertyFlagShorthand },
    { "border-top",      RoboCssPropertyInheritanceNonInherited,
      RoboCssPropertyComputationPreserve,   "medium none currentcolor",
      RoboCssPropertyFlagShorthand },
    { "border-top-color", RoboCssPropertyInheritanceNonInherited,
      RoboCssPropertyComputationColor,      "currentcolor", 0 },
    { "border-top-style", RoboCssPropertyInheritanceNonInherited,
      RoboCssPropertyComputationBorderStyle, "none",       0 },
    { "border-width",    RoboCssPropertyInheritanceNonInherited,
      RoboCssPropertyComputationPreserve,   "medium",
      RoboCssPropertyFlagShorthand },

    /*
     * --- positioning (9.13) ---
     *
     * Six rows, and they sit in alphabetical order with the rest of the table
     * rather than gathered into a block, because their names fall across the
     * whole of it — `bottom` here, `left` after `height`, `right` and `top`
     * around `position`, `z-index` last. The family is stated once, here, so a
     * reader meeting any of the six knows what the other five are.
     *
     * The four insets use the box model's EDGE_OFFSET shape, because
     * `<length-percentage> | auto` is the grammar CSS gives both `margin-top`
     * and `top`; what makes `top: 20%` a *positioning* value is which property
     * it was written on, and that is this table's answer rather than the
     * shape's. Their initial value is `auto`, which is not a placeholder: an
     * `auto` inset is what makes a box's own size and its containing block
     * decide where it goes, and turning it into `0` would place every
     * absolutely positioned box against the top-left corner of its containing
     * block. It is also why the shape refuses `min-content` and `normal` here —
     * there is no offset either keyword could mean.
     *
     * None of the six inherits, and `position` and `z-index` are the ones where
     * that matters most: a `<span>` inside an `absolute` paragraph is not itself
     * absolute, and a child does not join its parent's stacking level. Both are
     * shaped rather than left as keywords, so `position: banana` and
     * `z-index: 1.5` are reported mistakes instead of values a layout pass
     * would meet and have to fail on.
     */
    { "bottom",        RoboCssPropertyInheritanceNonInherited,
      RoboCssPropertyComputationEdgeOffset, "auto",      0 },
    { "box-sizing",    RoboCssPropertyInheritanceNonInherited,
      RoboCssPropertyComputationKeyword,    "content-box", 0 },
    /*
     * 9.12 moves `display` off the generic KEYWORD shape and onto one of its own.
     *
     * The row was here before — its classification and its `inline` initial value
     * are unchanged, and nothing about *this table* had to be re-decided. What
     * changed is that the value now has a shape that refuses an identifier it does
     * not know: `display: bogus` was a computed keyword with the text `bogus`, and
     * it is now an invalid value with a diagnostic. That is the whole point of
     * giving a property a shape, and it is why the KEYWORD shape stays for the
     * properties whose vocabulary belongs to a phase that has not landed.
     */
    { "display",       RoboCssPropertyInheritanceNonInherited,
      RoboCssPropertyComputationDisplay,    "inline",     0 },
    { "height",        RoboCssPropertyInheritanceNonInherited,
      RoboCssPropertyComputationLength,     "auto",       0 },
    { "left",          RoboCssPropertyInheritanceNonInherited,
      RoboCssPropertyComputationEdgeOffset, "auto",      0 },
    { "margin",        RoboCssPropertyInheritanceNonInherited,
      RoboCssPropertyComputationPreserve,   "0",
      RoboCssPropertyFlagShorthand },
    { "margin-bottom", RoboCssPropertyInheritanceNonInherited,
      RoboCssPropertyComputationEdgeOffset, "0",         0 },
    { "margin-left",   RoboCssPropertyInheritanceNonInherited,
      RoboCssPropertyComputationEdgeOffset, "0",         0 },
    { "margin-right",  RoboCssPropertyInheritanceNonInherited,
      RoboCssPropertyComputationEdgeOffset, "0",         0 },
    { "margin-top",    RoboCssPropertyInheritanceNonInherited,
      RoboCssPropertyComputationEdgeOffset, "0",         0 },
    /*
     * 11.9 adds `opacity`, the first property here registered by a phase of the *paint*
     * stack -- and therefore the first whose row a phase added without that phase having any
     * effect on geometry or on a colour.
     *
     * Three facts about the row are worth reading here rather than being discovered.
     *
     * It is **non-inherited**, which is CSS's classification and is *not* the same fact as
     * "a child is drawn at its parent's strength". Those look alike and are opposites: an
     * inherited `opacity` would make a child at `opacity: 1` inside a parent at `0.5` fully
     * opaque again, where CSS renders it at `0.5` because the parent's group opacity applies
     * to everything the parent contains. Inheritance copies a *value down*; opacity
     * *multiplies along a path*, which is an accumulation rather than an inheritance, and it
     * belongs to the layer that walks the path (RoboPaintOpacity.h) rather than to the
     * cascade. Registering it as inherited would be the mistake that produced the wrong
     * picture.
     *
     * Its initial value is `1`, written as text, and it is the only row here whose initial
     * value is not the zero of its own vocabulary -- see the shape's note above for what a
     * build that let a zeroed record stand in for it would render.
     *
     * And it is **not** a shorthand, unlike the `overflow` family below it. `opacity` is a
     * single longhand in CSS with nothing to expand into, so there is no second name for a
     * consumer to read and no partial form this build declines -- which is why the row is one
     * line rather than the three the overflow family needed to work around expansion.
     */
    { "opacity",       RoboCssPropertyInheritanceNonInherited,
      RoboCssPropertyComputationOpacity,    "1",         0 },
    /*
     * 10.13 adds the overflow family, and it is the first family here that is
     * *registered by a layout phase*: `overflow` changes geometry — an axis with a
     * boundary is a different box from one without — and no phase between 9.x and
     * 10.13 has any use for the value. It is registered rather than read from a
     * preserved token list for the reason every closed vocabulary above it is:
     * `overflow: banana` has to be a mistake, not a keyword carrying the text
     * `banana` that the phase with a box silently fails to interpret.
     *
     * The *shorthand's own name* takes the vocabulary too, and that is the one
     * unusual row here. Nothing in this build expands a shorthand, so `overflow`
     * left on the generic PRESERVE shape would carry tokens no consumer reads and a
     * document written as `overflow: hidden` would lay out exactly as if the
     * declaration had never been written — which would be a silent wrong answer about
     * a very common declaration. One vocabulary shared by the shorthand and its two
     * longhands is the shorthand's meaning one step early rather than a second
     * implementation of expansion, and the cost is stated where it is paid: the
     * two-value form (`overflow: hidden visible`) is not read
     * (`RoboCSSOverflow.h`). The flag records what the name is, for the phase that
     * does expand shorthands.
     */
    { "overflow",      RoboCssPropertyInheritanceNonInherited,
      RoboCssPropertyComputationOverflow,   "visible",
      RoboCssPropertyFlagShorthand },
    { "overflow-x",    RoboCssPropertyInheritanceNonInherited,
      RoboCssPropertyComputationOverflow,   "visible",   0 },
    { "overflow-y",    RoboCssPropertyInheritanceNonInherited,
      RoboCssPropertyComputationOverflow,   "visible",   0 },
    { "padding",       RoboCssPropertyInheritanceNonInherited,
      RoboCssPropertyComputationPreserve,   "0",
      RoboCssPropertyFlagShorthand },
    { "padding-bottom", RoboCssPropertyInheritanceNonInherited,
      RoboCssPropertyComputationEdgeInset,  "0",         0 },
    { "padding-left",  RoboCssPropertyInheritanceNonInherited,
      RoboCssPropertyComputationEdgeInset,  "0",         0 },
    { "padding-right", RoboCssPropertyInheritanceNonInherited,
      RoboCssPropertyComputationEdgeInset,  "0",         0 },
    { "padding-top",   RoboCssPropertyInheritanceNonInherited,
      RoboCssPropertyComputationEdgeInset,  "0",         0 },
    /*
     * 9.13 moves `position` off the generic KEYWORD shape and onto one of its
     * own, exactly as 9.12 moved `display`.
     *
     * The row was here before, with this classification and this initial value,
     * and nothing about *this table* had to be re-decided. What changed is that
     * the value is now a closed vocabulary with a diagnostic for an identifier
     * it does not know, because `position` is what a layout pass dispatches on
     * and a keyword carrying the text `banana` is a value that can only fail
     * later. The KEYWORD shape stays for the properties whose vocabulary belongs
     * to a phase that has not landed.
     */
    { "position",      RoboCssPropertyInheritanceNonInherited,
      RoboCssPropertyComputationPosition,   "static",     0 },
    { "right",         RoboCssPropertyInheritanceNonInherited,
      RoboCssPropertyComputationEdgeOffset, "auto",      0 },
    { "top",           RoboCssPropertyInheritanceNonInherited,
      RoboCssPropertyComputationEdgeOffset, "auto",      0 },
    { "width",         RoboCssPropertyInheritanceNonInherited,
      RoboCssPropertyComputationLength,     "auto",       0 },
    { "z-index",       RoboCssPropertyInheritanceNonInherited,
      RoboCssPropertyComputationZIndex,    "auto",       0 }
};

Result CssPropertyRegistryCreateStandard(
    CssPropertyRegistry **outRegistry,
    CssDiag *diag)
{
    CssPropertyRegistry *registry = NULL;
    size_t index;
    Result result;

    if (!outRegistry) {
        return RoboCssDiag(diag, RoboCssDomainPropertyRegistry, RoboInvalidArgument,
                             "output is NULL");
    }
    *outRegistry = NULL;
    result = CssPropertyRegistryCreate(&registry, diag);
    if (result != RoboOk) {
        return result;
    }
    for (index = 0u;
         index < sizeof(registryStandardEntries) / sizeof(registryStandardEntries[0]);
         index++) {
        const RegistryStandardEntry *entry = &registryStandardEntries[index];

        result = CssPropertyRegistryRegisterTyped(
            registry, entry->name, entry->inheritance, entry->computation,
            entry->initialText, entry->flags, diag);
        if (result != RoboOk) {
            /* Fully built or not at all: a half-populated standard set would
             * classify some properties and silently not others. */
            CssPropertyRegistryDestroy(registry);
            return result;
        }
    }
    *outRegistry = registry;
    return RoboOk;
}

Result CssPropertyRegistryApplyToTable(
    const CssPropertyRegistry *registry,
    CssPropertyTable *table,
    size_t *outRegistered,
    CssDiag *diag)
{
    size_t index;
    size_t registered = 0u;

    if (outRegistered) {
        *outRegistered = 0u;
    }
    if (!registry || !table) {
        return RoboCssDiag(diag, RoboCssDomainPropertyRegistry, RoboInvalidArgument,
                             "registry or table is NULL");
    }
    for (index = 0u; index < registry->count; index++) {
        const CssPropertyDefinition *definition =
            CssPropertyRegistryAt(registry, index);
        CssPropertyId id = RoboCssPropertyIdNone;
        uint32_t flags = definition->flags;
        Result result;

        if (!definition) {
            continue;
        }

        if (CssPropertyDefinitionInherited(definition)) {
            flags |= RoboCssPropertyFlagInherited;
        }
        result = CssPropertyTableRegister(table, definition->name, flags, &id, NULL);
        if (result == RoboAlreadyExists) {
            /* The caller numbered this property itself. Its flags stay its own
             * — first registration wins, the same rule the table already has. */
            continue;
        }
        if (result != RoboOk) {
            return RoboCssDiag(diag, RoboCssDomainPropertyRegistry, result,
                                 "a property could not be registered in the table");
        }
        registered++;
    }
    if (outRegistered) {
        *outRegistered = registered;
    }
    return RoboOk;
}
